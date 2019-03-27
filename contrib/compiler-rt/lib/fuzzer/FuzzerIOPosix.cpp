//===- FuzzerIOPosix.cpp - IO utils for Posix. ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// IO functions implementation using Posix API.
//===----------------------------------------------------------------------===//
#include "FuzzerDefs.h"
#if LIBFUZZER_POSIX || LIBFUZZER_FUCHSIA

#include "FuzzerExtFunctions.h"
#include "FuzzerIO.h"
#include <cstdarg>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fuzzer {

bool IsFile(const std::string &Path) {
  struct stat St;
  if (stat(Path.c_str(), &St))
    return false;
  return S_ISREG(St.st_mode);
}

static bool IsDirectory(const std::string &Path) {
  struct stat St;
  if (stat(Path.c_str(), &St))
    return false;
  return S_ISDIR(St.st_mode);
}

size_t FileSize(const std::string &Path) {
  struct stat St;
  if (stat(Path.c_str(), &St))
    return 0;
  return St.st_size;
}

std::string Basename(const std::string &Path) {
  size_t Pos = Path.rfind(GetSeparator());
  if (Pos == std::string::npos) return Path;
  assert(Pos < Path.size());
  return Path.substr(Pos + 1);
}

void ListFilesInDirRecursive(const std::string &Dir, long *Epoch,
                             Vector<std::string> *V, bool TopDir) {
  auto E = GetEpoch(Dir);
  if (Epoch)
    if (E && *Epoch >= E) return;

  DIR *D = opendir(Dir.c_str());
  if (!D) {
    Printf("%s: %s; exiting\n", strerror(errno), Dir.c_str());
    exit(1);
  }
  while (auto E = readdir(D)) {
    std::string Path = DirPlusFile(Dir, E->d_name);
    if (E->d_type == DT_REG || E->d_type == DT_LNK ||
        (E->d_type == DT_UNKNOWN && IsFile(Path)))
      V->push_back(Path);
    else if ((E->d_type == DT_DIR ||
             (E->d_type == DT_UNKNOWN && IsDirectory(Path))) &&
             *E->d_name != '.')
      ListFilesInDirRecursive(Path, Epoch, V, false);
  }
  closedir(D);
  if (Epoch && TopDir)
    *Epoch = E;
}

char GetSeparator() {
  return '/';
}

FILE* OpenFile(int Fd, const char* Mode) {
  return fdopen(Fd, Mode);
}

int CloseFile(int fd) {
  return close(fd);
}

int DuplicateFile(int Fd) {
  return dup(Fd);
}

void RemoveFile(const std::string &Path) {
  unlink(Path.c_str());
}

void DiscardOutput(int Fd) {
  FILE* Temp = fopen("/dev/null", "w");
  if (!Temp)
    return;
  dup2(fileno(Temp), Fd);
  fclose(Temp);
}

intptr_t GetHandleFromFd(int fd) {
  return static_cast<intptr_t>(fd);
}

std::string DirName(const std::string &FileName) {
  char *Tmp = new char[FileName.size() + 1];
  memcpy(Tmp, FileName.c_str(), FileName.size() + 1);
  std::string Res = dirname(Tmp);
  delete [] Tmp;
  return Res;
}

std::string TmpDir() {
  if (auto Env = getenv("TMPDIR"))
    return Env;
  return "/tmp";
}

bool IsInterestingCoverageFile(const std::string &FileName) {
  if (FileName.find("compiler-rt/lib/") != std::string::npos)
    return false; // sanitizer internal.
  if (FileName.find("/usr/lib/") != std::string::npos)
    return false;
  if (FileName.find("/usr/include/") != std::string::npos)
    return false;
  if (FileName == "<null>")
    return false;
  return true;
}


void RawPrint(const char *Str) {
  write(2, Str, strlen(Str));
}

}  // namespace fuzzer

#endif // LIBFUZZER_POSIX
