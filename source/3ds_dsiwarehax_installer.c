#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <3ds.h>

static int menu_curprintscreen = 0;
static PrintConsole menu_printscreen[2];

typedef struct {
	int initialized;
	u64 titleid;
	char dirpath[64];
	char desc[256];
} dsiware_entry;

#define MAX_DSIWARE 16
u32 dsiware_total;
dsiware_entry dsiware_list[MAX_DSIWARE];

char *dsiware_menuentries[MAX_DSIWARE];

extern u8 ampatch_start[];
extern u8 amstub_start[];
extern u32 amstub_size;

void display_menu(char **menu_entries, int total_entries, int *menuindex, char *headerstr)
{
	int i;
	u32 redraw = 1;
	u32 kDown = 0;

	while(1)
	{
		gspWaitForVBlank();
		hidScanInput();

		kDown = hidKeysDown();

		if(redraw)
		{
			redraw = 0;

			consoleClear();
			printf("%s.\n\n", headerstr);

			for(i=0; i<total_entries; i++)
			{
				if(*menuindex==i)
				{
					printf("-> ");
				}
				else
				{
					printf("   ");
				}

				printf("%s\n", menu_entries[i]);
			}
		}

		if(kDown & KEY_B)
		{
			*menuindex = -1;
			return;
		}

		if(kDown & (KEY_DDOWN | KEY_CPAD_DOWN))
		{
			(*menuindex)++;
			if(*menuindex>=total_entries)*menuindex = 0;
			redraw = 1;

			continue;
		}
		else if(kDown & (KEY_DUP | KEY_CPAD_UP))
		{
			(*menuindex)--;
			if(*menuindex<0)*menuindex = total_entries-1;
			redraw = 1;

			continue;
		}

		if(kDown & KEY_Y)
		{
			gspWaitForVBlank();
			consoleClear();

			menu_curprintscreen = !menu_curprintscreen;
			consoleSelect(&menu_printscreen[menu_curprintscreen]);
			redraw = 1;
		}

		if(kDown & KEY_A)
		{
			break;
		}
	}
}

void displaymessage_waitbutton()
{
	printf("\nPress the A button to continue.\n");
	while(1)
	{
		gspWaitForVBlank();
		hidScanInput();
		if(hidKeysDown() & KEY_A)break;
	}
}

Result load_file(char *path, u8 *buffer, u32 size, u32 *readsize)
{
	FILE *f;
	struct stat filestats;
	u32 tmp;

	if(stat(path, &filestats)==-1)return -7;

	if(size > filestats.st_size)size = filestats.st_size;
	if(size == 0)return -6;

	f = fopen(path, "rb");
	if(f==NULL)return -5;

	tmp = fread(buffer, 1, size, f);
	if(readsize)*readsize = tmp;
	fclose(f);

	if(tmp == 0)return -6;

	return 0;
}

Result loadnand_dsiware_titlelist()
{
	Result ret=0;
	u32 titlecount=0;
	u32 titlesRead=0;
	u64 *tidbuf;
	u32 pos, i;
	u32 entcount = 0;
	dsiware_entry *ent = &dsiware_list[0];
	char str[256];

	ret = AM_GetTitleCount(MEDIATYPE_NAND, &titlecount);
	if(ret)
	{
		printf("AM_GetTitleCount failed: 0x%08x.\n", (unsigned int)ret);
		return ret;
	}

	if((titlecount & 0xE0000000) || titlecount==0)
	{
		printf("Invalid titlecount: 0x%08x.\n", (unsigned int)titlecount);
		return -1;
	}

	tidbuf = malloc(titlecount << 3);
	if(tidbuf==NULL)
	{
		printf("Failed to allocate tidbuf.\n");
		return -2;
	}

	ret = AM_GetTitleList(&titlesRead, MEDIATYPE_NAND, titlecount, tidbuf);
	if(ret || titlesRead!=titlecount)
	{
		free(tidbuf);
		printf("AM_GetTitleIdList failed: 0x%08x. titlesRead = 0x%x while titlecount = 0x%x.\n", (unsigned int)ret, (unsigned int)titlesRead, (unsigned int)titlecount);
		return ret;
	}

	for(pos=0; pos<titlecount; pos++)
	{
		if((tidbuf[pos] >> 32) == 0x00048004)
		{
			memset(ent, 0, sizeof(dsiware_entry));

			memset(str, 0, sizeof(str));

			ent->titleid = tidbuf[pos];

			snprintf(ent->dirpath, sizeof(ent->dirpath)-1, "dsiware/%08X", (unsigned int)(ent->titleid & 0xffffffff));
			snprintf(str, sizeof(str)-1, "%s/info", ent->dirpath);

			ret = load_file(str, (u8*)ent->desc, sizeof(ent->desc)-1, NULL);
			if(ret==0)
			{
				ent->initialized = 1;

				i = strlen(ent->desc);
				if(i)
				{
					while(i>0)
					{
						i--;
						if(ent->desc[i] == 0x0a || ent->desc[i] == 0x0d)
						{
							ent->desc[i] = 0;
						}
						else
						{
							break;
						}
					}
				}

				snprintf(dsiware_menuentries[entcount], 63, "%08X %s", (unsigned int)(ent->titleid & 0xffffffff), ent->desc);
				printf("%u: %s\n", (unsigned int)entcount, dsiware_menuentries[entcount]);

				entcount++;
				if(entcount>=MAX_DSIWARE)break;
				ent = &dsiware_list[entcount];
			}
		}
	}

	dsiware_total = entcount;

	free(tidbuf);

	return 0;
}

Result loadsave_dsiwarehax(dsiware_entry *ent, u8 *savebuf, u32 savebuf_maxsize, u32 *actual_savesize)
{
	char str[256];

	memset(str, 0, sizeof(str));

	snprintf(str, sizeof(str)-1, "%s/public.sav", ent->dirpath);

	return load_file(str, savebuf, savebuf_maxsize, actual_savesize);
}

Result launch_am(u32 *pid)
{
	Result ret=0;
	u64 titleid = 0x0004013000001502ULL;

	ret = nsInit();
	if(ret)return ret;

	ret = NS_LaunchTitle(titleid, 0, pid);

	nsExit();

	return ret;
}

Result setup_am_patches(u8 *tmpbuf, u32 tmpbuf_size, u32 *out_offset)
{
	u8 *amtext = (u8*)0x0f000000;
	u32 ampxi_funcoffset = 0x0010e568-0x00100000;

	//Target function is LT_10e568/pxiam_cmd46, called @ thumb 0x10d1ee. (sysmodule v9217 from >=10.0.0-27)

	if(out_offset)*out_offset = ampxi_funcoffset;

	if(tmpbuf_size < 8+amstub_size)return -1;

	memcpy(&tmpbuf[8], amtext, amstub_size);
	memcpy(tmpbuf, &amtext[ampxi_funcoffset], 8);

	memcpy(amtext, amstub_start, amstub_size);
	memcpy(amtext, &amtext[ampxi_funcoffset], 8);
	*((u32*)&amtext[0xc]) = (ampxi_funcoffset+0x00100000+0x8) | 1;
	memcpy(&amtext[ampxi_funcoffset], ampatch_start, 8);

	return 0;
}

Result restore_amtext(u8 *tmpbuf, u32 tmpbuf_size, u32 ampxi_funcoffset)
{
	u8 *amtext = (u8*)0x0f000000;

	if(tmpbuf_size < 8+amstub_size)return -1;

	memcpy(amtext, &tmpbuf[8], amstub_size);
	memcpy(&amtext[ampxi_funcoffset], tmpbuf, 8);

	return 0;
}

Result install_dsiwarehax(dsiware_entry *ent, u8 *savebuf, u32 savesize)
{
	Result ret=0;
	u8 *tmpbuf = NULL;
	u32 tmpbuf_size = 0x1000;
	u8 *workbuf = NULL;
	u32 workbuf_size = 0x20000;
	Handle filehandle=0;
	u32 ampid = 0;
	Handle amproc = 0;
	u32 ampxi_funcoffset=0;

	FS_Path archPath = { PATH_EMPTY, 1, (u8*)"" };
	FS_Path filePath;

	size_t len=255;
	ssize_t units=0;
	uint16_t filepath16[256];

	tmpbuf = malloc(tmpbuf_size);
	if(tmpbuf==NULL)return -1;
	memset(tmpbuf, 0, tmpbuf_size);

	printf("Getting AM-module PID...\n");
	ret = launch_am(&ampid);
	if(R_FAILED(ret))
	{
		free(tmpbuf);
		return ret;
	}

	printf("SVCs will now be used which are normally not accessible, this will hang if these are not accessible.\n");

	ret = svcOpenProcess(&amproc, ampid);
	if(R_FAILED(ret))
	{
		free(tmpbuf);
		return ret;
	}

	//Map AM-module .text+0(0x00100000) up to size 0x14000, to vaddr 0x0f000000 in the current process.
	ret = svcMapProcessMemory(amproc, 0x0f000000, 0x14000);
	if(R_FAILED(ret))
	{
		free(tmpbuf);
		svcCloseHandle(amproc);
		return ret;
	}

	ret = setup_am_patches(tmpbuf, tmpbuf_size, &ampxi_funcoffset);
	if(R_FAILED(ret))
	{
		free(tmpbuf);
		svcCloseHandle(amproc);
		return ret;
	}

	ret = svcUnmapProcessMemory(amproc, 0x0f000000, 0x14000);
	if(R_FAILED(ret))
	{
		free(tmpbuf);
		svcCloseHandle(amproc);
		return ret;
	}

	printf("AM-module patching finished.\n");

	workbuf_size+= savesize;

	workbuf = malloc(workbuf_size);
	if(workbuf==NULL)
	{
		free(tmpbuf);
		svcCloseHandle(amproc);
		return -1;
	}
	memset(workbuf, 0, workbuf_size);

	printf("Exporting DSiWare to SD, this may take a while...\n");
	ret = AM_ExportTwlBackup(ent->titleid, 11, workbuf, workbuf_size, "sdmc:/3dsdsiware.bin");
	if(R_FAILED(ret))
	{
		free(tmpbuf);
		free(workbuf);
		svcCloseHandle(amproc);
		return ret;
	}

	memset(filepath16, 0, sizeof(filepath16));
	units = utf8_to_utf16(filepath16, (uint8_t*)"/3dsdsiware.bin", len);
	if(units < 0 || units > len)
	{
		free(tmpbuf);
		free(workbuf);
		svcCloseHandle(amproc);
		return -2;
	}

	filePath.type = PATH_UTF16;
	filePath.size = (units+1)*sizeof(uint16_t);
	filePath.data = (const u8*)filepath16;

	printf("Opening exported file...\n");

	ret = FSUSER_OpenFileDirectly(&filehandle, ARCHIVE_SDMC, archPath, filePath, FS_OPEN_READ, 0);
	if(R_FAILED(ret))
	{
		free(tmpbuf);
		free(workbuf);
		svcCloseHandle(amproc);
		return ret;
	}

	printf("Importing DSiWare...\n");

	memset(workbuf, 0, workbuf_size);
	memcpy(&workbuf[0x20000], savebuf, savesize);//Used by the amstub, so that this custom savedata is used instead of the data originally from the DSiWare export.
	ret = AM_ImportTwlBackup(filehandle, 11, workbuf, workbuf_size);

	FSFILE_Close(filehandle);

	memset(workbuf, 0, workbuf_size);
	free(workbuf);

	printf("Restoring AM-module .text...\n");

	ret = svcMapProcessMemory(amproc, 0x0f000000, 0x14000);
	if(R_FAILED(ret))
	{
		free(tmpbuf);
		svcCloseHandle(amproc);
		return ret;
	}

	ret = restore_amtext(tmpbuf, tmpbuf_size, ampxi_funcoffset);
	memset(tmpbuf, 0, tmpbuf_size);
	free(tmpbuf);
	if(R_FAILED(ret))
	{
		svcCloseHandle(amproc);
		return ret;
	}

	ret = svcUnmapProcessMemory(amproc, 0x0f000000, 0x14000);
	svcCloseHandle(amproc);

	return ret;
}

int main(int argc, char **argv)
{
	Result ret = 0;
	int menuindex = 0;
	u32 pos;

	u8 *savebuf;
	u32 savebuf_maxsize = 0x100000, savesize;

	char headerstr[512];

	// Initialize services
	gfxInitDefault();

	menu_curprintscreen = 0;
	consoleInit(GFX_TOP, &menu_printscreen[0]);
	consoleInit(GFX_BOTTOM, &menu_printscreen[1]);
	consoleSelect(&menu_printscreen[menu_curprintscreen]);

	memset(dsiware_list, 0, sizeof(dsiware_list));
	dsiware_total = 0;

	printf("3ds_dsiwarehax_installer %s by yellows8.\n", VERSION);

	if(ret==0)
	{
		ret = amInit();
		if(ret!=0)
		{
			printf("Failed to initialize AM: 0x%08x.\n", (unsigned int)ret);
			if(ret==0xd8e06406)
			{
				printf("The AM service is inaccessible. With the *hax payloads this should never happen. This is normal with plain ninjhax v1.x: this app isn't usable from ninjhax v1.x without any further hax.\n");
			}
		}

		if(ret==0)
		{
			for(pos=0; pos<MAX_DSIWARE; pos++)
			{
				dsiware_menuentries[pos] = malloc(256);
				if(dsiware_menuentries[pos]==NULL)
				{
					ret = -2;
					printf("Failed to allocate memory for a menuentry.\n");
					break;
				}
				memset(dsiware_menuentries[pos], 0, 256);
			}

			ret = loadnand_dsiware_titlelist();

			if(ret)printf("Failed to load the DSiWare titlelist: 0x%08x.\n", (unsigned int)ret);
		}

		if(ret==0 && dsiware_total==0)
		{
			ret = -10;
			printf("No DSiWare titles were detected in NAND with a matching exploit directory on SD.\n");
		}
	}

	if(ret>=0)
	{
		memset(headerstr, 0, sizeof(headerstr));
		snprintf(headerstr, sizeof(headerstr)-1, "3ds_dsiwarehax_installer %s by yellows8.\n\nSelect a DSiWare exploit to install with the below menu(the hex word is the detected DSiWare titleID-low on your system). You can press the B button to exit. You can press the Y button at any time while at a menu like the below one, to toggle the screen being used by this app", VERSION);

		while(ret==0)
		{
			display_menu(dsiware_menuentries, dsiware_total, &menuindex, headerstr);

			if(menuindex==-1)break;

			consoleClear();

			savesize = 0;

			savebuf = malloc(savebuf_maxsize);
			if(savebuf==NULL)
			{
				printf("Failed to allocate memory for the savedata.\n");
				ret = -2;
			}
			memset(savebuf, 0, savebuf_maxsize);

			if(ret==0)
			{
				printf("Loading the savedata from SD...\n");
				ret = loadsave_dsiwarehax(&dsiware_list[menuindex], savebuf, savebuf_maxsize, &savesize);
				if(ret)
				{
					printf("Failed to load the dsiwarehax savedata: 0x%08x.\n", (unsigned int)ret);
				}
			}

			if(ret==0)
			{
				printf("Preparing exploit installation...\n");
				ret = install_dsiwarehax(&dsiware_list[menuindex], savebuf, savesize);
				if(ret)printf("Failed to install the savedata: 0x%08x.\n", (unsigned int)ret);
			}

			if(savebuf)free(savebuf);

			if(ret==0)
			{
				printf("The exploit was successfully installed.\n");
				displaymessage_waitbutton();
			}
			else
			{
				printf("Exploit installation failed: 0x%08x.\n", (unsigned int)ret);
			}
		}
	}

	if(ret!=0)printf("An error occured. You can report this to here if it persists(or comment on an already existing issue if needed), with a screenshot: https://github.com/yellows8/3ds_dsiwarehax_installer/issues\n");

	amExit();

	for(pos=0; pos<MAX_DSIWARE; pos++)
	{
		if(dsiware_menuentries[pos])free(dsiware_menuentries[pos]);
	}

	printf("Press the START button to exit.\n");
	// Main loop
	while (aptMainLoop())
	{
		gspWaitForVBlank();
		hidScanInput();

		u32 kDown = hidKeysDown();
		if (kDown & KEY_START)
			break; // break in order to return to hbmenu
	}

	// Exit services
	gfxExit();
	return 0;
}

