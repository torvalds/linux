//===- FuzzerShmemWindows.cpp - Posix shared memory -------------*- C++ -* ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// SharedMemoryRegion
//===----------------------------------------------------------------------===//
#include "FuzzerDefs.h"
#if LIBFUZZER_WINDOWS

#include "FuzzerIO.h"
#include "FuzzerShmem.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace fuzzer {

std::string SharedMemoryRegion::Path(const char *Name) {
  return DirPlusFile(TmpDir(), Name);
}

std::string SharedMemoryRegion::SemName(const char *Name, int Idx) {
  std::string Res(Name);
  return Res + (char)('0' + Idx);
}

bool SharedMemoryRegion::Map(int fd) {
  assert(0 && "UNIMPLEMENTED");
  return false;
}

bool SharedMemoryRegion::Create(const char *Name) {
  assert(0 && "UNIMPLEMENTED");
  return false;
}

bool SharedMemoryRegion::Open(const char *Name) {
  assert(0 && "UNIMPLEMENTED");
  return false;
}

bool SharedMemoryRegion::Destroy(const char *Name) {
  assert(0 && "UNIMPLEMENTED");
  return false;
}

void SharedMemoryRegion::Post(int Idx) {
  assert(0 && "UNIMPLEMENTED");
}

void SharedMemoryRegion::Wait(int Idx) {
  Semaphore[1] = nullptr;
  assert(0 && "UNIMPLEMENTED");
}

}  // namespace fuzzer

#endif  // LIBFUZZER_WINDOWS
