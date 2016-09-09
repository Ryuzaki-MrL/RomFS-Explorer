#include <dirent.h>
#include <stdio.h>
#include <cstring>
#include <string>
#include <vector>
#include <stack>
#include <algorithm>
#include <3ds.h>

PrintConsole top;
PrintConsole bot;

Handle romfs_handle;

typedef struct {
	u32 magic;
	u16 version;
	u16 reserved;
} smdhHeader_s;

typedef struct {
	u16 shortDescription[0x40];
	u16 longDescription[0x80];
	u16 publisher[0x40];
} smdhTitle_s;

typedef struct {
	u8 gameRatings[0x10];
	u32 regionLock;
	u8 matchMakerId[0xC];
	u32 flags;
	u16 eulaVersion;
	u16 reserved;
	u32 defaultFrame;
	u32 cecId;
} smdhSettings_s;

typedef struct {
	smdhHeader_s header;
	smdhTitle_s titles[16];
	smdhSettings_s settings;
	u8 reserved[0x8];
	u16 smallIconData[0x240];
	u16 bigIconData[0x900];
} smdh_s;

typedef struct {
    std::string name;
    std::string path;
    bool isDir;
    u64 size;
} filedata;

// Modified from https://github.com/Rinnegatamante/lpp-3ds/blob/master/source/include/utils.cpp#L70-L74
std::string utf2ascii(u16 *src) {
    if (!src) return "";
    char buffer[0x80];
    char *dst = buffer;
    while(*src) *(dst++)=(*(src++))&0xFF;
    *dst=0x00;
    return std::string(buffer);
}

Result MYFSUSER_GetMediaType(Handle fsuHandle, u8* mediatype) {
    u32* cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0x868,0,0);

    Result ret = 0;
    if((ret = svcSendSyncRequest(fsuHandle))) return ret;

    if(mediatype) *mediatype = cmdbuf[2];

    return cmdbuf[1];
}

Result MYFSUSER_OpenFileDirectly(Handle fsuHandle, Handle *out, FS_ArchiveID archiveId, FS_Path archivePath, FS_Path filePath, u32 openFlags, u32 attributes) noexcept {
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[ 0] = IPC_MakeHeader(0x803,8,4);
    cmdbuf[ 1] = 0;
    cmdbuf[ 2] = archiveId;
    cmdbuf[ 3] = archivePath.type;
    cmdbuf[ 4] = archivePath.size;
    cmdbuf[ 5] = filePath.type;
    cmdbuf[ 6] = filePath.size;
    cmdbuf[ 7] = openFlags;
    cmdbuf[ 8] = attributes;
    cmdbuf[ 9] = IPC_Desc_StaticBuffer(archivePath.size,2);
    cmdbuf[10] = (u32)archivePath.data;
    cmdbuf[11] = IPC_Desc_StaticBuffer(filePath.size,0);
    cmdbuf[12] = (u32)filePath.data;

    Result ret = 0;
    if((ret = svcSendSyncRequest(fsuHandle))) return ret;

    if(out) *out = cmdbuf[3];

    return cmdbuf[1];
}

bool promptConfirm(std::string strg) {
    consoleSelect(&top);
    consoleClear();
    printf("\x1b[14;%uH%s", (25 - (strg.size() / 2)), strg.c_str());
    printf("\x1b[16;14H(A) Confirm / (B) Cancel");
    u32 kDown = 0;
    while (aptMainLoop()) {
        hidScanInput();
        kDown = hidKeysDown();
        if (kDown) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    consoleClear();
    consoleSelect(&bot);
    if (kDown & KEY_A) return true;
    else return false;
}

void promptError(std::string strg) {
    consoleSelect(&top);
    consoleClear();
    printf("\x1b[14;%uH%s", (25 - (strg.size() / 2)), strg.c_str());
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    consoleClear();
    consoleSelect(&bot);
}

void printSource(bool is3dsx, std::string fname, bool mounted) {
    consoleSelect(&top);
    consoleClear();
    printf("ROMFS STATUS\n");
    if (!mounted) printf("Not mounted");
    else if (fname!="") printf("Mounted from file\n%.50s", fname.c_str());
    else if (is3dsx) {
        u64 id = 0;
        APT_GetProgramID(&id);
        FS_MediaType mediatype;
        Handle localFsHandle;
        srvGetServiceHandleDirect(&localFsHandle, "fs:USER");
        FSUSER_Initialize(localFsHandle);
        Result ret = MYFSUSER_GetMediaType(localFsHandle, (u8*)&mediatype);
        if (ret) return;
        svcCloseHandle(localFsHandle);
        Handle fileHandle;
        smdh_s smdh;
        u32 archivePath[] = {(u32)(id & 0xFFFFFFFF), (u32)(id >> 32), mediatype, 0x00000000};
        const u32 filePathData[] = {0x00000000, 0x00000000, 0x00000002, 0x6E6F6369, 0x00000000};
        const FS_Path filePath = (FS_Path) {PATH_BINARY, 0x14, (u8*) filePathData};
        ret = FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SAVEDATA_AND_CONTENT, (FS_Path){PATH_BINARY, 0x10, (u8*) archivePath}, filePath, FS_OPEN_READ, 0);
        if (ret) return;
        u32 bytesRead;
        ret = FSFILE_Read(fileHandle, &bytesRead, 0x0, &smdh, sizeof(smdh_s));
        FSFILE_Close(fileHandle);
        if (ret) return;
        u8 lang = CFG_LANGUAGE_EN;
        cfguInit();
        CFGU_GetSystemLanguage(&lang);
        cfguExit();
        std::string shortDesc = utf2ascii(smdh.titles[lang].shortDescription);
        std::string longDesc = utf2ascii(smdh.titles[lang].longDescription);
        std::string publisher = utf2ascii(smdh.titles[lang].publisher);
        printf("Mounted from title\n%s\n%s\n%s", shortDesc.c_str(), longDesc.c_str(), publisher.c_str());
    }
    consoleSelect(&bot);
    consoleClear();
}

void printHelp(bool selected, bool is3dsx, bool mounted, u8 source) {
    consoleSelect(&top);
    consoleClear();
    if (selected) printf("D-PAD: Navigate\nA: Select\nB: Go back\nL: Show help\nR: Show clipboard\nY: %s", (source==0 ? "Copy files to this folder" : "Copy files to clipboard"));
    else printf("D-PAD: Navigate\nA: Select\nL: Show help\nSTART: Quit\n%s\n%s", (mounted ? "SELECT: Unmount romfs" : (is3dsx ? "SELECT: Remount romfs from title" : " ")), (is3dsx ? "Y: Dump romfs" : " "));
    consoleSelect(&bot);
}

void sortFileList(std::vector<filedata> *filelist) {
    struct abc {
        inline bool operator() (filedata a, filedata b) {
            if(a.isDir == b.isDir)
                return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
            else return a.isDir;
        }
    } alphabetically;
    std::sort((*filelist).begin(), (*filelist).end(), alphabetically);
}

bool isDirectory(std::string path) {
    bool result = false;
    DIR *dir = opendir(path.c_str());
    if (dir) result = true;
    closedir(dir);
    return result;
}

bool getFileList(std::vector<filedata> *dest, std::string directory) {
    std::vector<filedata> result;
    DIR* dir = opendir(directory.c_str());
    if(dir == NULL) return false;
    dirent* ent = NULL;
    do {
        ent = readdir(dir);
        if (ent != NULL) {
            std::string file(ent->d_name);
            bool isDir = isDirectory(directory + file + "/");
            u64 size = 0;
            if (!isDir) {
                FILE *tmp = fopen((directory + file).c_str(), "r");
                fseek(tmp, 0L, SEEK_END);
                size = ftell(tmp);
                fclose(tmp);
            }
            if ((file!=".") && (file!="..")) result.push_back({file, directory + file, isDir, size});
        }
    } while (ent != NULL);
    closedir(dir);
    sortFileList(&result);
    dest->swap(result);
    return true;
}

bool fileExists(std::string fname) {
    if (FILE *file = fopen(fname.c_str(), "r")) {
        fclose(file);
        return true;
    }
    else return false;
}

void printFiles(u32 cursor, u32 scroll, u32 count, std::vector<filedata> *files, std::string curdir) {
    consoleSelect(&top);
    consoleClear();
    if (cursor>0) {
        printf("\x1b[0;0H%.50s", (*files)[cursor+scroll-1].name.c_str());
        if ((*files)[cursor+scroll-1].isDir) printf("\x1b[1;0HDIR");
        else {
            printf("\x1b[1;0HFILE");
            printf("\x1b[2;0H%llu bytes", (*files)[cursor+scroll-1].size);
        }
    }
    consoleSelect(&bot);
    bool isroot = ((curdir=="/") || (curdir=="romfs:/"));
    if (curdir.size() <= 40) printf("\x1b[0;0H%-40s", curdir.c_str());
    else printf("\x1b[0;0H%.37s...", curdir.c_str());
    printf("\x1b[%lu;0H> ", 1 + cursor);
    u32 i = 0;
    while (i < count) {
        if (i > 27) break;
        u32 len = (*files)[i+scroll].name.size();
        if (len > 38) {
            if ((*files)[i+scroll].isDir) printf("\x1b[%lu;2H\x1b[33m%.35s...\x1b[0m", 2 + i, (*files)[i+scroll].name.c_str());
            else printf("\x1b[%lu;2H%.35s...", 2 + i, (*files)[i+scroll].name.c_str());
        }
        else {
            if ((*files)[i+scroll].isDir) printf("\x1b[%lu;2H\x1b[33m%-38s\x1b[0m", 2 + i, (*files)[i+scroll].name.c_str());
            else printf("\x1b[%lu;2H%-38s", 2 + i, (*files)[i+scroll].name.c_str());
        }
        i++;
    }
    while (i < 28) {
        printf("\x1b[%lu;2H%-38c", 2 + i, ' ');
        i++;
    }
    if (isroot) printf("\x1b[1;2H\x1b[35m%-38s\x1b[0m", "[root]");
    else printf("\x1b[1;2H\x1b[37m%-38s\x1b[0m", "..");
}

void printClipboard(std::vector<filedata> *clipboard) {
    consoleSelect(&top);
    consoleClear();
    u32 i = 0;
    while (i < clipboard->size()) {
        if (i > 29) break;
        printf("\x1b[%lu;0H%.50s", i, (*clipboard)[i].path.c_str());
        i++;
    }
    consoleSelect(&bot);
}

bool copyClipboard(std::vector<filedata> *source, std::string dest) {
    u32 i = source->size();
    while (source->size() != 0) {
        i--;
        hidScanInput();
        u32 kHeld = hidKeysHeld();
        if ((kHeld & KEY_B) && (promptConfirm("Cancel operation?"))) break;
        if ((*source)[i].isDir) {
            std::vector<filedata> contents;
            if (getFileList(&contents, (*source)[i].path + "/")) {
                mkdir((dest + (*source)[i].name).c_str(), 0777);
                copyClipboard(&contents, dest + (*source)[i].name + "/");
            }
        }
        else {
            bool exists = fileExists(dest + (*source)[i].name);
            FILE *dst = fopen((dest + (*source)[i].name).c_str(), "wb");
            FILE *src = fopen((*source)[i].path.c_str(), "rb");
            size_t fsize = (*source)[i].size;
            size_t size = 0;
            if (exists && !(promptConfirm("Overwrite file " + (*source)[i].name + "?")));
            else if (src && dst) {
                consoleSelect(&top);
                consoleClear();
                printf("\x1b[14;%uHCopying %.42s", (25 - ((*source)[i].path.size() / 2)), (*source)[i].path.c_str());
                gfxFlushBuffers();
                gfxSwapBuffers();
                gspWaitForVBlank();
                char *buffer = (char*)malloc(0x50000);
                while (!feof(src)) {
                    size_t rsize = fread(buffer, 1, 0x50000, src);
                    if (rsize==0) { promptError("Error reading file."); break; }
                    size += fwrite(buffer, 1, rsize, dst);
                    printf("\x1b[15;12H%zu b / %zu b (%zu%%)", size, fsize, (size * 100) / fsize);
                    gfxFlushBuffers();
                    gfxSwapBuffers();
                    gspWaitForVBlank();
                }
                if (size < fsize) promptError("Error copying file.");
                fclose(src);
                fclose(dst);
                free(buffer);
            }
            else promptError("Error opening file.");
        }
        source->pop_back();
    }
    if (source->size() == 0) return true;
    else return false;
}

bool getRomFSHandle(Handle *file_handle) {
    char arch_path[] = "";
    char low_path[0xC];
    memset(low_path, 0, sizeof(low_path));

    Handle local_fs_handle;
    Result ret = srvGetServiceHandleDirect(&local_fs_handle, "fs:USER");
    if (ret!=0) return false;

    ret = FSUSER_Initialize(local_fs_handle);
    if (ret!=0) svcCloseHandle(local_fs_handle);

    ret = MYFSUSER_OpenFileDirectly(local_fs_handle, file_handle, ARCHIVE_ROMFS, (FS_Path){PATH_EMPTY, 1, (u8*)arch_path}, (FS_Path){PATH_BINARY, sizeof(low_path), (u8*)low_path}, FS_OPEN_READ, 0);
    if (ret!=0) return false;

    return true;
}

bool dumpRomFS() {
    bool success = false;
    char arch_path[] = "";
    char low_path[0xC];
    memset(low_path, 0, sizeof(low_path));
    Handle fs_handle;
    Handle file_handle;
    srvGetServiceHandleDirect(&fs_handle, "fs:USER");
    FSUSER_Initialize(fs_handle);
    Result ret = MYFSUSER_OpenFileDirectly(fs_handle, &file_handle, ARCHIVE_ROMFS, (FS_Path){PATH_EMPTY, 1, (u8*)arch_path}, (FS_Path){PATH_BINARY, sizeof(low_path), (u8*)low_path}, FS_OPEN_READ, 0);
    if (ret!=0) promptError("Failed to open romfs file.");
    else {
        u64 id = 0;
        APT_GetProgramID(&id);
        char fname[26];
        sprintf(fname, "/%016llx.romfs", id);
        bool exists = fileExists(std::string(fname));
        u64 fsize = 0;
        u64 offset = 0;
        FSFILE_GetSize(file_handle, &fsize);
        if (exists && !(promptConfirm("Overwrite file " + std::string(fname) + "?")));
        else {
            consoleSelect(&top);
            consoleClear();
            printf("\x1b[14;18HDumping romfs");
            char header[0x1000] = "IVFC";
            memset(header + 4, 0, 0xFFC);
            FILE *dst = fopen(fname, "wb");
            fwrite(header, 1, 0x1000, dst);
            char *buffer = (char*)malloc(0x100000);
            u32 bufsiz = 0x100000;
            while (offset != fsize) {
                u32 rsize;
                ret = FSFILE_Read(file_handle, &rsize, offset, buffer, bufsiz);
                if (ret != 0 || rsize==0) { promptError("Failed to read from romfs file."); break; }
                size_t wsize = fwrite(buffer, sizeof(char), rsize, dst);
                if (wsize < rsize) { promptError("Failed to write to output file."); break; }
                offset += rsize;
                printf("\x1b[15;13H%llu b / %llu b (%llu%%)", offset, fsize, (offset * 100) / fsize);
                gfxFlushBuffers();
                gfxSwapBuffers();
                gspWaitForVBlank();
            }
            fclose(dst);
            free(buffer);
            success = (offset==fsize);
        }
    }
    FSFILE_Close(file_handle);
    svcCloseHandle(fs_handle);
    return success;
}

int main(int argc, char **argv) {
    // service initialization
    fsInit();
    amInit();
    gfxInitDefault();
    consoleInit(GFX_TOP, &top);
    consoleInit(GFX_BOTTOM, &bot);

    // general stuff
    u32 cursor = 0;
    u32 scroll = 0;
    u32 count = 0;
    u8 timer = 0;
    u8 source = 0;
    bool is3dsx = false;
    bool selected = false;
    bool mounted = false;
    int easteregg[2] = {0};
    std::string romfs_file = "";
    std::string root[] = {"/", "romfs:/"};
    std::string curdir;
    std::stack<std::string> innerpath;
    std::vector<filedata> filelist;
    std::vector<filedata> clipboard;

    // romFS initialization
    is3dsx = getRomFSHandle(&romfs_handle);
    if (is3dsx) mounted = (romfsInitFromFile(romfs_handle, 0x0)==0);
    printSource(is3dsx, romfs_file, mounted);

    // main loop
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (!selected) {
            printf("\x1b[1;2HSD\x1b[2;2HROMFS");
            printf("\x1b[29;0HPress L for help");
            printf("\x1b[%lu;0H \n>\n ", cursor);
        }

        if (kDown & KEY_DOWN) {
            if (!selected) cursor=1;
            else {
                printf("\x1b[%lu;0H  ", 1 + cursor);
                if (count < 28) {
                    if (cursor < count) cursor++;
                    else cursor=0;
                }
                else if (count > 0) {
                    if (cursor<14) cursor++;
                    else if ((cursor + scroll) < (count - 14)) scroll++;
                    else if (cursor<28) cursor++;
                    else { cursor = 0; scroll = 0; }
                }
                printFiles(cursor, scroll, count, &filelist, curdir);
            }
        }
        if (kDown & KEY_UP) {
            if (!selected) cursor=0;
            else {
                printf("\x1b[%lu;0H  ", 1 + cursor);
                if (cursor>13) cursor--;
                else if (scroll>0) scroll--;
                else if (cursor>0) cursor--;
                else {
                    if (count>27) { cursor = 28; scroll = count - 28; }
                    else if (count>0) cursor = count;
                }
                printFiles(cursor, scroll, count, &filelist, curdir);
            }
        }

        // select action
        if (kDown & KEY_A) {
            if (!selected) {
                if (cursor==1 && !mounted) {
                    easteregg[0]++;
                    if (easteregg[0]>=30) promptError("THE ROMFS IS NOT FUCKING MOUNTED!");
                    else promptError("RomFS not mounted.");
                }
                else {
                    printf("\x1b[%lu;0H  ", 1 + cursor);
                    selected = true;
                    source = cursor;
                    curdir = root[source];
                    if (!getFileList(&filelist, curdir)) promptError("Failed to scan current directory.");
                    cursor = 0; scroll = 0; count = filelist.size();
                    consoleSelect(&bot);
                    consoleClear();
                    printFiles(cursor, scroll, count, &filelist, curdir);
                }
            }
            else {
                if (cursor > 0) {
                    if (filelist[cursor+scroll-1].isDir) {
                        innerpath.push(curdir);
                        curdir = curdir + filelist[cursor+scroll-1].name + "/";
                        if (!getFileList(&filelist, curdir)) promptError("Failed to scan current directory.");
                        printf("\x1b[%lu;0H  ", 1 + cursor);
                        cursor = 0; scroll = 0; count = filelist.size();
                        printFiles(cursor, scroll, count, &filelist, curdir);
                    }
                    else if (source==0) {
                        if (promptConfirm("Mount romFS from this file?")) {
                            u32 magic = 0x0;
                            FSUSER_OpenFileDirectly(&romfs_handle, ARCHIVE_SDMC, (FS_Path){PATH_EMPTY, 1, (u8*)""}, (FS_Path)fsMakePath(PATH_ASCII, (curdir + filelist[cursor+scroll-1].name).c_str()), FS_OPEN_READ, 0);
                            FSFILE_Read(romfs_handle, NULL, 0x0, &magic, 0x4);
                            if (magic==0x43465649) {
                                romfsExit();
                                mounted = false;
                                romfs_file = curdir + filelist[cursor+scroll-1].name;
                                Result res = romfsInitFromFile(romfs_handle, 0x1000);
                                if (res!=0) promptError("Couldn't not mount romFS from file.");
                                else mounted = true;
                            }
                            else promptError("Not a valid romFS file.");
                        }
                    }
                }
                else {
                    if (curdir!=root[source]) {
                        curdir = innerpath.top();
                        innerpath.pop();
                        if (!getFileList(&filelist, curdir)) promptError("Failed to scan current directory.");
                        printf("\x1b[%lu;0H  ", 1 + cursor);
                        cursor = 0; scroll = 0; count = filelist.size();
                        printFiles(cursor, scroll, count, &filelist, curdir);
                    }
                }
            }
        }

        // copy files
        if (kDown & KEY_Y) {
            if (selected) {
                if (source==1) {
                    if (cursor > 0) {
                        size_t cbsize = clipboard.size();
                        for (size_t i=0; i < cbsize; i++) {
                            if (clipboard[i].path == (curdir + filelist[cursor+scroll-1].name)) {
                                clipboard.erase(clipboard.begin()+i);
                                break;
                            }
                            else if (i == (cbsize-1)) {
                                if (cbsize < 30) clipboard.push_back(filelist[cursor+scroll-1]);
                                else promptError("Clipboard full.");
                                break;
                            }
                        }
                        if (cbsize==0) clipboard.push_back(filelist[cursor+scroll-1]);
                        printClipboard(&clipboard);
                    }
                    else {
                        easteregg[1]++;
                        if ((easteregg[1]>=10) && promptConfirm("Copy files to this folder?")) promptError("Can't let you do that, Star Fox!");
                    }
                }
                else if (clipboard.size() > 0) {
                    if (promptConfirm("Copy files to this folder?")) {
                        if (copyClipboard(&clipboard, curdir)) promptError("Copy done.");
                        else promptError("Copy failed.");
                        if (!getFileList(&filelist, curdir)) promptError("Failed to scan current directory.");
                        count = filelist.size();
                        printFiles(cursor, scroll, count, &filelist, curdir);
                    }
                }
            }
            else if ((cursor==1 && is3dsx) && (promptConfirm("Dump romfs to SD card?"))) {
                if (dumpRomFS()) promptError("RomFS dump done.");
                else promptError("RomFS dump failed.");
            }
        }

        // clear clipboard
        if ((kDown & KEY_X) && (promptConfirm("Clear clipboard?"))) clipboard.clear();

        // cancel/go back
        if (kDown & KEY_B) {
            cursor = 0; scroll = 0;
            if (curdir==root[source]) selected = false;
            else if (!innerpath.empty()) {
                curdir = innerpath.top();
                innerpath.pop();
                if (!getFileList(&filelist, curdir)) promptError("Failed to scan current directory.");
                count = filelist.size();
                consoleSelect(&bot);
                consoleClear();
                printFiles(cursor, scroll, count, &filelist, curdir);
            }
            else selected = false;
            if (!selected) printSource(is3dsx, romfs_file, mounted);
        }

        // print help
        if (kDown & KEY_L) printHelp(selected, is3dsx, mounted, source);

        // print clipboard
        if (kDown & KEY_R) printClipboard(&clipboard);

        // unmount/remount romfs
        if (kDown & KEY_SELECT) {
            if ((mounted && !selected) && (promptConfirm("Unmount romFS?"))) {
                mounted = false;
                cursor = 0; scroll = 0;
                romfsExit();
                printSource(is3dsx, romfs_file, mounted);
            }
            else if ((!mounted && is3dsx) && (promptConfirm("Remount romFS from title?"))) {
                romfs_file = "";
                getRomFSHandle(&romfs_handle);
                mounted = (romfsInitFromFile(romfs_handle, 0x0)==0);
            }
        }

        // exit
        if ((kDown & KEY_START) && promptConfirm("Exit RomFS Explorer?")) break;

        // fast scrolling
        if (selected) {
            u32 kHeld = hidKeysHeld();
            if (kHeld & KEY_DOWN) {
                if (timer>=30) {
                    printf("\x1b[%lu;0H  ", 1 + cursor);
                    if (count < 28) {
                        if (cursor < count) cursor++;
                        else cursor=0;
                    }
                    else if (count > 0) {
                        if (cursor<14) cursor++;
                        else if ((cursor + scroll) < (count - 14)) scroll++;
                        else if (cursor<28) cursor++;
                        else { cursor = 0; scroll = 0; }
                    }
                    printFiles(cursor, scroll, count, &filelist, curdir);
                    timer=26;
                }
                else timer++;
            }
            else if (kHeld & KEY_UP) {
                if (timer>=30) {
                    printf("\x1b[%lu;0H  ", 1 + cursor);
                    if (cursor>13) cursor--;
                    else if (scroll>0) scroll--;
                    else if (cursor>0) cursor--;
                    else {
                        if (count>27) { cursor = 28; scroll = count - 28; }
                        else if (count>0) cursor = count;
                    }
                    printFiles(cursor, scroll, count, &filelist, curdir);
                    timer=26;
                }
                else timer++;
            }
            else timer = 0;
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    consoleSelect(&top);
    consoleClear();
    consoleSelect(&bot);
    consoleClear();
    gfxExit();
    if (mounted) romfsExit();
    amExit();
    fsExit();
    return 0;
}
