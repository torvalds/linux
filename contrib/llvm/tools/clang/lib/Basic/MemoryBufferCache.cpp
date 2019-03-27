//===- MemoryBufferCache.cpp - Cache for loaded memory buffers ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/MemoryBufferCache.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace clang;

llvm::MemoryBuffer &
MemoryBufferCache::addBuffer(llvm::StringRef Filename,
                             std::unique_ptr<llvm::MemoryBuffer> Buffer) {
  auto Insertion =
      Buffers.insert({Filename, BufferEntry{std::move(Buffer), NextIndex++}});
  assert(Insertion.second && "Already has a buffer");
  return *Insertion.first->second.Buffer;
}

llvm::MemoryBuffer *MemoryBufferCache::lookupBuffer(llvm::StringRef Filename) {
  auto I = Buffers.find(Filename);
  if (I == Buffers.end())
    return nullptr;
  return I->second.Buffer.get();
}

bool MemoryBufferCache::isBufferFinal(llvm::StringRef Filename) {
  auto I = Buffers.find(Filename);
  if (I == Buffers.end())
    return false;
  return I->second.Index < FirstRemovableIndex;
}

bool MemoryBufferCache::tryToRemoveBuffer(llvm::StringRef Filename) {
  auto I = Buffers.find(Filename);
  assert(I != Buffers.end() && "No buffer to remove...");
  if (I->second.Index < FirstRemovableIndex)
    return true;

  Buffers.erase(I);
  return false;
}

void MemoryBufferCache::finalizeCurrentBuffers() { FirstRemovableIndex = NextIndex; }
