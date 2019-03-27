//===- FuzzerShmemPosix.cpp - Posix shared memory ---------------*- C++ -* ===//
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
#if LIBFUZZER_POSIX

#include "FuzzerIO.h"
#include "FuzzerShmem.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fuzzer {

std::string SharedMemoryRegion::Path(const char *Name) {
  return DirPlusFile(TmpDir(), Name);
}

std::string SharedMemoryRegion::SemName(const char *Name, int Idx) {
  std::string Res(Name);
  // When passing a name without a leading <slash> character to
  // sem_open, the behaviour is unspecified in POSIX. Add a leading
  // <slash> character for the name if there is no such one.
  if (!Res.empty() && Res[0] != '/')
    Res.insert(Res.begin(), '/');
  return Res + (char)('0' + Idx);
}

bool SharedMemoryRegion::Map(int fd) {
  Data =
      (uint8_t *)mmap(0, kShmemSize, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
  if (Data == (uint8_t*)-1)
    return false;
  return true;
}

bool SharedMemoryRegion::Create(const char *Name) {
  int fd = open(Path(Name).c_str(), O_CREAT | O_RDWR, 0777);
  if (fd < 0) return false;
  if (ftruncate(fd, kShmemSize) < 0) return false;
  if (!Map(fd))
    return false;
  for (int i = 0; i < 2; i++) {
    sem_unlink(SemName(Name, i).c_str());
    Semaphore[i] = sem_open(SemName(Name, i).c_str(), O_CREAT, 0644, 0);
    if (Semaphore[i] == SEM_FAILED)
      return false;
  }
  IAmServer = true;
  return true;
}

bool SharedMemoryRegion::Open(const char *Name) {
  int fd = open(Path(Name).c_str(), O_RDWR);
  if (fd < 0) return false;
  struct stat stat_res;
  if (0 != fstat(fd, &stat_res))
    return false;
  assert(stat_res.st_size == kShmemSize);
  if (!Map(fd))
    return false;
  for (int i = 0; i < 2; i++) {
    Semaphore[i] = sem_open(SemName(Name, i).c_str(), 0);
    if (Semaphore[i] == SEM_FAILED)
      return false;
  }
  IAmServer = false;
  return true;
}

bool SharedMemoryRegion::Destroy(const char *Name) {
  return 0 == unlink(Path(Name).c_str());
}

void SharedMemoryRegion::Post(int Idx) {
  assert(Idx == 0 || Idx == 1);
  sem_post((sem_t*)Semaphore[Idx]);
}

void SharedMemoryRegion::Wait(int Idx) {
  assert(Idx == 0 || Idx == 1);
  for (int i = 0; i < 10 && sem_wait((sem_t*)Semaphore[Idx]); i++) {
    // sem_wait may fail if interrupted by a signal.
    sleep(i);
    if (i)
      Printf("%s: sem_wait[%d] failed %s\n", i < 9 ? "WARNING" : "ERROR", i,
             strerror(errno));
    if (i == 9) abort();
  }
}

}  // namespace fuzzer

#endif  // LIBFUZZER_POSIX
