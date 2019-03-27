//===- lib/MC/MCSymbol.cpp - MCSymbol implementation ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSymbol.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>

using namespace llvm;

// Only the address of this fragment is ever actually used.
static MCDummyFragment SentinelFragment(nullptr);

// Sentinel value for the absolute pseudo fragment.
MCFragment *MCSymbol::AbsolutePseudoFragment = &SentinelFragment;

void *MCSymbol::operator new(size_t s, const StringMapEntry<bool> *Name,
                             MCContext &Ctx) {
  // We may need more space for a Name to account for alignment.  So allocate
  // space for the storage type and not the name pointer.
  size_t Size = s + (Name ? sizeof(NameEntryStorageTy) : 0);

  // For safety, ensure that the alignment of a pointer is enough for an
  // MCSymbol.  This also ensures we don't need padding between the name and
  // symbol.
  static_assert((unsigned)alignof(MCSymbol) <= alignof(NameEntryStorageTy),
                "Bad alignment of MCSymbol");
  void *Storage = Ctx.allocate(Size, alignof(NameEntryStorageTy));
  NameEntryStorageTy *Start = static_cast<NameEntryStorageTy*>(Storage);
  NameEntryStorageTy *End = Start + (Name ? 1 : 0);
  return End;
}

void MCSymbol::setVariableValue(const MCExpr *Value) {
  assert(!IsUsed && "Cannot set a variable that has already been used.");
  assert(Value && "Invalid variable value!");
  assert((SymbolContents == SymContentsUnset ||
          SymbolContents == SymContentsVariable) &&
         "Cannot give common/offset symbol a variable value");
  this->Value = Value;
  SymbolContents = SymContentsVariable;
  setUndefined();
}

void MCSymbol::print(raw_ostream &OS, const MCAsmInfo *MAI) const {
  // The name for this MCSymbol is required to be a valid target name.  However,
  // some targets support quoting names with funny characters.  If the name
  // contains a funny character, then print it quoted.
  StringRef Name = getName();
  if (!MAI || MAI->isValidUnquotedName(Name)) {
    OS << Name;
    return;
  }

  if (MAI && !MAI->supportsNameQuoting())
    report_fatal_error("Symbol name with unsupported characters");

  OS << '"';
  for (char C : Name) {
    if (C == '\n')
      OS << "\\n";
    else if (C == '"')
      OS << "\\\"";
    else
      OS << C;
  }
  OS << '"';
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MCSymbol::dump() const {
  dbgs() << *this;
}
#endif
