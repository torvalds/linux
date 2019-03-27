//===- MarkLive.cpp -------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Chunks.h"
#include "Symbols.h"
#include "lld/Common/Timer.h"
#include "llvm/ADT/STLExtras.h"
#include <vector>

namespace lld {
namespace coff {

static Timer GCTimer("GC", Timer::root());

// Set live bit on for each reachable chunk. Unmarked (unreachable)
// COMDAT chunks will be ignored by Writer, so they will be excluded
// from the final output.
void markLive(ArrayRef<Chunk *> Chunks) {
  ScopedTimer T(GCTimer);

  // We build up a worklist of sections which have been marked as live. We only
  // push into the worklist when we discover an unmarked section, and we mark
  // as we push, so sections never appear twice in the list.
  SmallVector<SectionChunk *, 256> Worklist;

  // COMDAT section chunks are dead by default. Add non-COMDAT chunks.
  for (Chunk *C : Chunks)
    if (auto *SC = dyn_cast<SectionChunk>(C))
      if (SC->Live)
        Worklist.push_back(SC);

  auto Enqueue = [&](SectionChunk *C) {
    if (C->Live)
      return;
    C->Live = true;
    Worklist.push_back(C);
  };

  auto AddSym = [&](Symbol *B) {
    if (auto *Sym = dyn_cast<DefinedRegular>(B))
      Enqueue(Sym->getChunk());
    else if (auto *Sym = dyn_cast<DefinedImportData>(B))
      Sym->File->Live = true;
    else if (auto *Sym = dyn_cast<DefinedImportThunk>(B))
      Sym->WrappedSym->File->Live = Sym->WrappedSym->File->ThunkLive = true;
  };

  // Add GC root chunks.
  for (Symbol *B : Config->GCRoot)
    AddSym(B);

  while (!Worklist.empty()) {
    SectionChunk *SC = Worklist.pop_back_val();
    assert(SC->Live && "We mark as live when pushing onto the worklist!");

    // Mark all symbols listed in the relocation table for this section.
    for (Symbol *B : SC->symbols())
      if (B)
        AddSym(B);

    // Mark associative sections if any.
    for (SectionChunk *C : SC->children())
      Enqueue(C);
  }
}

}
}
