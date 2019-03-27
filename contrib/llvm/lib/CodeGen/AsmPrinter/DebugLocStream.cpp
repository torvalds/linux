//===- DebugLocStream.cpp - DWARF debug_loc stream --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DebugLocStream.h"
#include "DwarfDebug.h"
#include "llvm/CodeGen/AsmPrinter.h"

using namespace llvm;

bool DebugLocStream::finalizeList(AsmPrinter &Asm) {
  if (Lists.back().EntryOffset == Entries.size()) {
    // Empty list.  Delete it.
    Lists.pop_back();
    return false;
  }

  // Real list.  Generate a label for it.
  Lists.back().Label = Asm.createTempSymbol("debug_loc");
  return true;
}

void DebugLocStream::finalizeEntry() {
  if (Entries.back().ByteOffset != DWARFBytes.size())
    return;

  // The last entry was empty.  Delete it.
  Comments.erase(Comments.begin() + Entries.back().CommentOffset,
                 Comments.end());
  Entries.pop_back();

  assert(Lists.back().EntryOffset <= Entries.size() &&
         "Popped off more entries than are in the list");
}

DebugLocStream::ListBuilder::~ListBuilder() {
  if (!Locs.finalizeList(Asm))
    return;
  V.initializeDbgValue(&MI);
  V.setDebugLocListIndex(ListIndex);
}
