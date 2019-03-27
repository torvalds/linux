//===- ValueSymbolTable.cpp - Implement the ValueSymbolTable class --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ValueSymbolTable class for the IR library.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "valuesymtab"

// Class destructor
ValueSymbolTable::~ValueSymbolTable() {
#ifndef NDEBUG   // Only do this in -g mode...
  for (const auto &VI : vmap)
    dbgs() << "Value still in symbol table! Type = '"
           << *VI.getValue()->getType() << "' Name = '" << VI.getKeyData()
           << "'\n";
  assert(vmap.empty() && "Values remain in symbol table!");
#endif
}

ValueName *ValueSymbolTable::makeUniqueName(Value *V,
                                            SmallString<256> &UniqueName) {
  unsigned BaseSize = UniqueName.size();
  while (true) {
    // Trim any suffix off and append the next number.
    UniqueName.resize(BaseSize);
    raw_svector_ostream S(UniqueName);
    if (auto *GV = dyn_cast<GlobalValue>(V)) {
      // A dot is appended to mark it as clone during ABI demangling so that
      // for example "_Z1fv" and "_Z1fv.1" both demangle to "f()", the second
      // one being a clone.
      // On NVPTX we cannot use a dot because PTX only allows [A-Za-z0-9_$] for
      // identifiers. This breaks ABI demangling but at least ptxas accepts and
      // compiles the program.
      const Module *M = GV->getParent();
      if (!(M && Triple(M->getTargetTriple()).isNVPTX()))
        S << ".";
    }
    S << ++LastUnique;

    // Try insert the vmap entry with this suffix.
    auto IterBool = vmap.insert(std::make_pair(UniqueName, V));
    if (IterBool.second)
      return &*IterBool.first;
  }
}

// Insert a value into the symbol table with the specified name...
//
void ValueSymbolTable::reinsertValue(Value* V) {
  assert(V->hasName() && "Can't insert nameless Value into symbol table");

  // Try inserting the name, assuming it won't conflict.
  if (vmap.insert(V->getValueName())) {
    // LLVM_DEBUG(dbgs() << " Inserted value: " << V->getValueName() << ": " <<
    // *V << "\n");
    return;
  }

  // Otherwise, there is a naming conflict.  Rename this value.
  SmallString<256> UniqueName(V->getName().begin(), V->getName().end());

  // The name is too already used, just free it so we can allocate a new name.
  V->getValueName()->Destroy();

  ValueName *VN = makeUniqueName(V, UniqueName);
  V->setValueName(VN);
}

void ValueSymbolTable::removeValueName(ValueName *V) {
  // LLVM_DEBUG(dbgs() << " Removing Value: " << V->getKeyData() << "\n");
  // Remove the value from the symbol table.
  vmap.remove(V);
}

/// createValueName - This method attempts to create a value name and insert
/// it into the symbol table with the specified name.  If it conflicts, it
/// auto-renames the name and returns that instead.
ValueName *ValueSymbolTable::createValueName(StringRef Name, Value *V) {
  // In the common case, the name is not already in the symbol table.
  auto IterBool = vmap.insert(std::make_pair(Name, V));
  if (IterBool.second) {
    // LLVM_DEBUG(dbgs() << " Inserted value: " << Entry.getKeyData() << ": "
    //           << *V << "\n");
    return &*IterBool.first;
  }

  // Otherwise, there is a naming conflict.  Rename this value.
  SmallString<256> UniqueName(Name.begin(), Name.end());
  return makeUniqueName(V, UniqueName);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
// dump - print out the symbol table
//
LLVM_DUMP_METHOD void ValueSymbolTable::dump() const {
  //dbgs() << "ValueSymbolTable:\n";
  for (const auto &I : *this) {
    //dbgs() << "  '" << I->getKeyData() << "' = ";
    I.getValue()->dump();
    //dbgs() << "\n";
  }
}
#endif
