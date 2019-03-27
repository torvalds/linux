//===- PDBSymbolTypeFunctionSig.cpp - --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"

#include "llvm/DebugInfo/PDB/ConcreteSymbolEnumerator.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/PDBSymDumper.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionArg.h"

#include <utility>

using namespace llvm;
using namespace llvm::pdb;

namespace {
class FunctionArgEnumerator : public IPDBEnumSymbols {
public:
  typedef ConcreteSymbolEnumerator<PDBSymbolTypeFunctionArg> ArgEnumeratorType;

  FunctionArgEnumerator(const IPDBSession &PDBSession,
                        const PDBSymbolTypeFunctionSig &Sig)
      : Session(PDBSession),
        Enumerator(Sig.findAllChildren<PDBSymbolTypeFunctionArg>()) {}

  FunctionArgEnumerator(const IPDBSession &PDBSession,
                        std::unique_ptr<ArgEnumeratorType> ArgEnumerator)
      : Session(PDBSession), Enumerator(std::move(ArgEnumerator)) {}

  uint32_t getChildCount() const override {
    return Enumerator->getChildCount();
  }

  std::unique_ptr<PDBSymbol> getChildAtIndex(uint32_t Index) const override {
    auto FunctionArgSymbol = Enumerator->getChildAtIndex(Index);
    if (!FunctionArgSymbol)
      return nullptr;
    return Session.getSymbolById(FunctionArgSymbol->getTypeId());
  }

  std::unique_ptr<PDBSymbol> getNext() override {
    auto FunctionArgSymbol = Enumerator->getNext();
    if (!FunctionArgSymbol)
      return nullptr;
    return Session.getSymbolById(FunctionArgSymbol->getTypeId());
  }

  void reset() override { Enumerator->reset(); }

private:
  const IPDBSession &Session;
  std::unique_ptr<ArgEnumeratorType> Enumerator;
};
}

std::unique_ptr<IPDBEnumSymbols>
PDBSymbolTypeFunctionSig::getArguments() const {
  return llvm::make_unique<FunctionArgEnumerator>(Session, *this);
}

void PDBSymbolTypeFunctionSig::dump(PDBSymDumper &Dumper) const {
  Dumper.dump(*this);
}

void PDBSymbolTypeFunctionSig::dumpRight(PDBSymDumper &Dumper) const {
  Dumper.dumpRight(*this);
}

bool PDBSymbolTypeFunctionSig::isCVarArgs() const {
  auto SigArguments = getArguments();
  if (!SigArguments)
    return false;
  uint32_t NumArgs = SigArguments->getChildCount();
  if (NumArgs == 0)
    return false;
  auto Last = SigArguments->getChildAtIndex(NumArgs - 1);
  if (auto Builtin = llvm::dyn_cast_or_null<PDBSymbolTypeBuiltin>(Last.get())) {
    if (Builtin->getBuiltinType() == PDB_BuiltinType::None)
      return true;
  }

  // Note that for a variadic template signature, this method always returns
  // false since the parameters of the template are specialized.
  return false;
}
