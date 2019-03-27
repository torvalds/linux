//===- FuzzerIO.h - Internal header for IO utils ----------------*- C++ -* ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// IO interface.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_IO_H
#define LLVM_FUZZER_IO_H

#include "FuzzerDefs.h"

namespace fuzzer {

long GetEpoch(const std::string &Path);

Unit FileToVector(const std::string &Path, size_t MaxSize = 0,
                  bool ExitOnError = true);

std::string FileToString(const std::string &Path);

void CopyFileToErr(const std::string &Path);

void WriteToFile(const Unit &U, const std::string &Path);

void ReadDirToVectorOfUnits(const char *Path, Vector<Unit> *V,
                            long *Epoch, size_t MaxSize, bool ExitOnError);

// Returns "Dir/FileName" or equivalent for the current OS.
std::string DirPlusFile(const std::string &DirPath,
                        const std::string &FileName);

// Returns the name of the dir, similar to the 'dirname' utility.
std::string DirName(const std::string &FileName);

// Returns path to a TmpDir.
std::string TmpDir();

bool IsInterestingCoverageFile(const std::string &FileName);

void DupAndCloseStderr();

void CloseStdout();

void Printf(const char *Fmt, ...);

// Print using raw syscalls, useful when printing at early init stages.
void RawPrint(const char *Str);

// Platform specific functions:
bool IsFile(const std::string &Path);
size_t FileSize(const std::string &Path);

void ListFilesInDirRecursive(const std::string &Dir, long *Epoch,
                             Vector<std::string> *V, bool TopDir);

struct SizedFile {
  std::string File;
  size_t Size;
  bool operator<(const SizedFile &B) const { return Size < B.Size; }
};

void GetSizedFilesFromDir(const std::string &Dir, Vector<SizedFile> *V);

char GetSeparator();
// Similar to the basename utility: returns the file name w/o the dir prefix.
std::string Basename(const std::string &Path);

FILE* OpenFile(int Fd, const char *Mode);

int CloseFile(int Fd);

int DuplicateFile(int Fd);

void RemoveFile(const std::string &Path);

void DiscardOutput(int Fd);

intptr_t GetHandleFromFd(int fd);

}  // namespace fuzzer

#endif  // LLVM_FUZZER_IO_H
