//===- Memory.cpp ---------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Memory.h"

using namespace llvm;
using namespace lld;

BumpPtrAllocator lld::BAlloc;
StringSaver lld::Saver{BAlloc};
std::vector<SpecificAllocBase *> lld::SpecificAllocBase::Instances;

void lld::freeArena() {
  for (SpecificAllocBase *Alloc : SpecificAllocBase::Instances)
    Alloc->reset();
  BAlloc.Reset();
}
