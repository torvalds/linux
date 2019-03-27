//===- FuzzerShmem.h - shared memory interface ------------------*- C++ -* ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// SharedMemoryRegion
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_SHMEM_H
#define LLVM_FUZZER_SHMEM_H

#include <algorithm>
#include <cstring>
#include <string>

#include "FuzzerDefs.h"

namespace fuzzer {

class SharedMemoryRegion {
 public:
  bool Create(const char *Name);
  bool Open(const char *Name);
  bool Destroy(const char *Name);
  uint8_t *GetData() { return Data; }
  void PostServer() {Post(0);}
  void WaitServer() {Wait(0);}
  void PostClient() {Post(1);}
  void WaitClient() {Wait(1);}

  size_t WriteByteArray(const uint8_t *Bytes, size_t N) {
    assert(N <= kShmemSize - sizeof(N));
    memcpy(GetData(), &N, sizeof(N));
    memcpy(GetData() + sizeof(N), Bytes, N);
    assert(N == ReadByteArraySize());
    return N;
  }
  size_t ReadByteArraySize() {
    size_t Res;
    memcpy(&Res, GetData(), sizeof(Res));
    return Res;
  }
  uint8_t *GetByteArray() { return GetData() + sizeof(size_t); }

  bool IsServer() const { return Data && IAmServer; }
  bool IsClient() const { return Data && !IAmServer; }

private:

  static const size_t kShmemSize = 1 << 22;
  bool IAmServer;
  std::string Path(const char *Name);
  std::string SemName(const char *Name, int Idx);
  void Post(int Idx);
  void Wait(int Idx);

  bool Map(int fd);
  uint8_t *Data = nullptr;
  void *Semaphore[2];
};

extern SharedMemoryRegion SMR;

}  // namespace fuzzer

#endif  // LLVM_FUZZER_SHMEM_H
