//===- FuzzerIOWindows.cpp - IO utils for Windows. ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// IO functions implementation for Windows.
//===----------------------------------------------------------------------===//
#include "FuzzerDefs.h"
#if LIBFUZZER_WINDOWS

#include "FuzzerExtFunctions.h"
#include "FuzzerIO.h"
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <io.h>
#include <iterator>
#include <sys/stat.h>
#include <sys/types.h>
#include <windows.h>

namespace fuzzer {

static bool IsFile(const std::string &Path, const DWORD &FileAttributes) {

  if (FileAttributes & FILE_ATTRIBUTE_NORMAL)
    return true;

  if (FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    return false;

  HANDLE FileHandle(
      CreateFileA(Path.c_str(), 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                  FILE_FLAG_BACKUP_SEMANTICS, 0));

  if (FileHandle == INVALID_HANDLE_VALUE) {
    Printf("CreateFileA() failed for \"%s\" (Error code: %lu).\n", Path.c_str(),
        GetLastError());
    return false;
  }

  DWORD FileType = GetFileType(FileHandle);

  if (FileType == FILE_TYPE_UNKNOWN) {
    Printf("GetFileType() failed for \"%s\" (Error code: %lu).\n", Path.c_str(),
        GetLastError());
    CloseHandle(FileHandle);
    return false;
  }

  if (FileType != FILE_TYPE_DISK) {
    CloseHandle(FileHandle);
    return false;
  }

  CloseHandle(FileHandle);
  return true;
}

bool IsFile(const std::string &Path) {
  DWORD Att = GetFileAttributesA(Path.c_str());

  if (Att == INVALID_FILE_ATTRIBUTES) {
    Printf("GetFileAttributesA() failed for \"%s\" (Error code: %lu).\n",
        Path.c_str(), GetLastError());
    return false;
  }

  return IsFile(Path, Att);
}

std::string Basename(const std::string &Path) {
  size_t Pos = Path.find_last_of("/\\");
  if (Pos == std::string::npos) return Path;
  assert(Pos < Path.size());
  return Path.substr(Pos + 1);
}

size_t FileSize(const std::string &Path) {
  WIN32_FILE_ATTRIBUTE_DATA attr;
  if (!GetFileAttributesExA(Path.c_str(), GetFileExInfoStandard, &attr)) {
    Printf("GetFileAttributesExA() failed for \"%s\" (Error code: %lu).\n",
           Path.c_str(), GetLastError());
    return 0;
  }
  ULARGE_INTEGER size;
  size.HighPart = attr.nFileSizeHigh;
  size.LowPart = attr.nFileSizeLow;
  return size.QuadPart;
}

void ListFilesInDirRecursive(const std::string &Dir, long *Epoch,
                             Vector<std::string> *V, bool TopDir) {
  auto E = GetEpoch(Dir);
  if (Epoch)
    if (E && *Epoch >= E) return;

  std::string Path(Dir);
  assert(!Path.empty());
  if (Path.back() != '\\')
      Path.push_back('\\');
  Path.push_back('*');

  // Get the first directory entry.
  WIN32_FIND_DATAA FindInfo;
  HANDLE FindHandle(FindFirstFileA(Path.c_str(), &FindInfo));
  if (FindHandle == INVALID_HANDLE_VALUE)
  {
    if (GetLastError() == ERROR_FILE_NOT_FOUND)
      return;
    Printf("No such file or directory: %s; exiting\n", Dir.c_str());
    exit(1);
  }

  do {
    std::string FileName = DirPlusFile(Dir, FindInfo.cFileName);

    if (FindInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      size_t FilenameLen = strlen(FindInfo.cFileName);
      if ((FilenameLen == 1 && FindInfo.cFileName[0] == '.') ||
          (FilenameLen == 2 && FindInfo.cFileName[0] == '.' &&
                               FindInfo.cFileName[1] == '.'))
        continue;

      ListFilesInDirRecursive(FileName, Epoch, V, false);
    }
    else if (IsFile(FileName, FindInfo.dwFileAttributes))
      V->push_back(FileName);
  } while (FindNextFileA(FindHandle, &FindInfo));

  DWORD LastError = GetLastError();
  if (LastError != ERROR_NO_MORE_FILES)
    Printf("FindNextFileA failed (Error code: %lu).\n", LastError);

  FindClose(FindHandle);

  if (Epoch && TopDir)
    *Epoch = E;
}

char GetSeparator() {
  return '\\';
}

FILE* OpenFile(int Fd, const char* Mode) {
  return _fdopen(Fd, Mode);
}

int CloseFile(int Fd) {
  return _close(Fd);
}

int DuplicateFile(int Fd) {
  return _dup(Fd);
}

void RemoveFile(const std::string &Path) {
  _unlink(Path.c_str());
}

void DiscardOutput(int Fd) {
  FILE* Temp = fopen("nul", "w");
  if (!Temp)
    return;
  _dup2(_fileno(Temp), Fd);
  fclose(Temp);
}

intptr_t GetHandleFromFd(int fd) {
  return _get_osfhandle(fd);
}

static bool IsSeparator(char C) {
  return C == '\\' || C == '/';
}

// Parse disk designators, like "C:\". If Relative == true, also accepts: "C:".
// Returns number of characters considered if successful.
static size_t ParseDrive(const std::string &FileName, const size_t Offset,
                         bool Relative = true) {
  if (Offset + 1 >= FileName.size() || FileName[Offset + 1] != ':')
    return 0;
  if (Offset + 2 >= FileName.size() || !IsSeparator(FileName[Offset + 2])) {
    if (!Relative) // Accept relative path?
      return 0;
    else
      return 2;
  }
  return 3;
}

// Parse a file name, like: SomeFile.txt
// Returns number of characters considered if successful.
static size_t ParseFileName(const std::string &FileName, const size_t Offset) {
  size_t Pos = Offset;
  const size_t End = FileName.size();
  for(; Pos < End && !IsSeparator(FileName[Pos]); ++Pos)
    ;
  return Pos - Offset;
}

// Parse a directory ending in separator, like: `SomeDir\`
// Returns number of characters considered if successful.
static size_t ParseDir(const std::string &FileName, const size_t Offset) {
  size_t Pos = Offset;
  const size_t End = FileName.size();
  if (Pos >= End || IsSeparator(FileName[Pos]))
    return 0;
  for(; Pos < End && !IsSeparator(FileName[Pos]); ++Pos)
    ;
  if (Pos >= End)
    return 0;
  ++Pos; // Include separator.
  return Pos - Offset;
}

// Parse a servername and share, like: `SomeServer\SomeShare\`
// Returns number of characters considered if successful.
static size_t ParseServerAndShare(const std::string &FileName,
                                  const size_t Offset) {
  size_t Pos = Offset, Res;
  if (!(Res = ParseDir(FileName, Pos)))
    return 0;
  Pos += Res;
  if (!(Res = ParseDir(FileName, Pos)))
    return 0;
  Pos += Res;
  return Pos - Offset;
}

// Parse the given Ref string from the position Offset, to exactly match the given
// string Patt.
// Returns number of characters considered if successful.
static size_t ParseCustomString(const std::string &Ref, size_t Offset,
                                const char *Patt) {
  size_t Len = strlen(Patt);
  if (Offset + Len > Ref.size())
    return 0;
  return Ref.compare(Offset, Len, Patt) == 0 ? Len : 0;
}

// Parse a location, like:
// \\?\UNC\Server\Share\  \\?\C:\  \\Server\Share\  \  C:\  C:
// Returns number of characters considered if successful.
static size_t ParseLocation(const std::string &FileName) {
  size_t Pos = 0, Res;

  if ((Res = ParseCustomString(FileName, Pos, R"(\\?\)"))) {
    Pos += Res;
    if ((Res = ParseCustomString(FileName, Pos, R"(UNC\)"))) {
      Pos += Res;
      if ((Res = ParseServerAndShare(FileName, Pos)))
        return Pos + Res;
      return 0;
    }
    if ((Res = ParseDrive(FileName, Pos, false)))
      return Pos + Res;
    return 0;
  }

  if (Pos < FileName.size() && IsSeparator(FileName[Pos])) {
    ++Pos;
    if (Pos < FileName.size() && IsSeparator(FileName[Pos])) {
      ++Pos;
      if ((Res = ParseServerAndShare(FileName, Pos)))
        return Pos + Res;
      return 0;
    }
    return Pos;
  }

  if ((Res = ParseDrive(FileName, Pos)))
    return Pos + Res;

  return Pos;
}

std::string DirName(const std::string &FileName) {
  size_t LocationLen = ParseLocation(FileName);
  size_t DirLen = 0, Res;
  while ((Res = ParseDir(FileName, LocationLen + DirLen)))
    DirLen += Res;
  size_t FileLen = ParseFileName(FileName, LocationLen + DirLen);

  if (LocationLen + DirLen + FileLen != FileName.size()) {
    Printf("DirName() failed for \"%s\", invalid path.\n", FileName.c_str());
    exit(1);
  }

  if (DirLen) {
    --DirLen; // Remove trailing separator.
    if (!FileLen) { // Path ended in separator.
      assert(DirLen);
      // Remove file name from Dir.
      while (DirLen && !IsSeparator(FileName[LocationLen + DirLen - 1]))
        --DirLen;
      if (DirLen) // Remove trailing separator.
        --DirLen;
    }
  }

  if (!LocationLen) { // Relative path.
    if (!DirLen)
      return ".";
    return std::string(".\\").append(FileName, 0, DirLen);
  }

  return FileName.substr(0, LocationLen + DirLen);
}

std::string TmpDir() {
  std::string Tmp;
  Tmp.resize(MAX_PATH + 1);
  DWORD Size = GetTempPathA(Tmp.size(), &Tmp[0]);
  if (Size == 0) {
    Printf("Couldn't get Tmp path.\n");
    exit(1);
  }
  Tmp.resize(Size);
  return Tmp;
}

bool IsInterestingCoverageFile(const std::string &FileName) {
  if (FileName.find("Program Files") != std::string::npos)
    return false;
  if (FileName.find("compiler-rt\\lib\\") != std::string::npos)
    return false; // sanitizer internal.
  if (FileName == "<null>")
    return false;
  return true;
}

void RawPrint(const char *Str) {
  // Not tested, may or may not work. Fix if needed.
  Printf("%s", Str);
}

}  // namespace fuzzer

#endif // LIBFUZZER_WINDOWS
