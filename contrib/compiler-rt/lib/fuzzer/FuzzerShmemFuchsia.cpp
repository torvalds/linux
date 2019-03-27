//===- FuzzerShmemPosix.cpp - Posix shared memory ---------------*- C++ -* ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// SharedMemoryRegion.  For Fuchsia, this is just stubs as equivalence servers
// are not currently supported.
//===----------------------------------------------------------------------===//
#include "FuzzerDefs.h"

#if LIBFUZZER_FUCHSIA

#include "FuzzerShmem.h"

namespace fuzzer {

bool SharedMemoryRegion::Create(const char *Name) {
  return false;
}

bool SharedMemoryRegion::Open(const char *Name) {
  return false;
}

bool SharedMemoryRegion::Destroy(const char *Name) {
  return false;
}

void SharedMemoryRegion::Post(int Idx) {}

void SharedMemoryRegion::Wait(int Idx) {}

}  // namespace fuzzer

#endif  // LIBFUZZER_FUCHSIA
