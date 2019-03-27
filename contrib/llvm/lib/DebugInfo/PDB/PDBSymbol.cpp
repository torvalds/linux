//===- PDBSymbol.cpp - base class for user-facing symbol types --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/PDBExtras.h"
#include "llvm/DebugInfo/PDB/PDBSymbolAnnotation.h"
#include "llvm/DebugInfo/PDB/PDBSymbolBlock.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompiland.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompilandDetails.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompilandEnv.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCustom.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolExe.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFuncDebugEnd.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFuncDebugStart.h"
#include "llvm/DebugInfo/PDB/PDBSymbolLabel.h"
#include "llvm/DebugInfo/PDB/PDBSymbolPublicSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolThunk.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeArray.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBaseClass.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeCustom.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeDimension.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFriend.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionArg.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeManaged.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypePointer.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeTypedef.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeVTable.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeVTableShape.h"
#include "llvm/DebugInfo/PDB/PDBSymbolUnknown.h"
#include "llvm/DebugInfo/PDB/PDBSymbolUsingNamespace.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include <algorithm>
#include <memory>

using namespace llvm;
using namespace llvm::pdb;

PDBSymbol::PDBSymbol(const IPDBSession &PDBSession) : Session(PDBSession) {}

PDBSymbol::PDBSymbol(PDBSymbol &&Other)
    : Session(Other.Session), RawSymbol(std::move(Other.RawSymbol)) {}

PDBSymbol::~PDBSymbol() = default;

#define FACTORY_SYMTAG_CASE(Tag, Type)                                         \
  case PDB_SymType::Tag:                                                       \
    return std::unique_ptr<PDBSymbol>(new Type(PDBSession));

std::unique_ptr<PDBSymbol>
PDBSymbol::createSymbol(const IPDBSession &PDBSession, PDB_SymType Tag) {
  switch (Tag) {
    FACTORY_SYMTAG_CASE(Exe, PDBSymbolExe)
    FACTORY_SYMTAG_CASE(Compiland, PDBSymbolCompiland)
    FACTORY_SYMTAG_CASE(CompilandDetails, PDBSymbolCompilandDetails)
    FACTORY_SYMTAG_CASE(CompilandEnv, PDBSymbolCompilandEnv)
    FACTORY_SYMTAG_CASE(Function, PDBSymbolFunc)
    FACTORY_SYMTAG_CASE(Block, PDBSymbolBlock)
    FACTORY_SYMTAG_CASE(Data, PDBSymbolData)
    FACTORY_SYMTAG_CASE(Annotation, PDBSymbolAnnotation)
    FACTORY_SYMTAG_CASE(Label, PDBSymbolLabel)
    FACTORY_SYMTAG_CASE(PublicSymbol, PDBSymbolPublicSymbol)
    FACTORY_SYMTAG_CASE(UDT, PDBSymbolTypeUDT)
    FACTORY_SYMTAG_CASE(Enum, PDBSymbolTypeEnum)
    FACTORY_SYMTAG_CASE(FunctionSig, PDBSymbolTypeFunctionSig)
    FACTORY_SYMTAG_CASE(PointerType, PDBSymbolTypePointer)
    FACTORY_SYMTAG_CASE(ArrayType, PDBSymbolTypeArray)
    FACTORY_SYMTAG_CASE(BuiltinType, PDBSymbolTypeBuiltin)
    FACTORY_SYMTAG_CASE(Typedef, PDBSymbolTypeTypedef)
    FACTORY_SYMTAG_CASE(BaseClass, PDBSymbolTypeBaseClass)
    FACTORY_SYMTAG_CASE(Friend, PDBSymbolTypeFriend)
    FACTORY_SYMTAG_CASE(FunctionArg, PDBSymbolTypeFunctionArg)
    FACTORY_SYMTAG_CASE(FuncDebugStart, PDBSymbolFuncDebugStart)
    FACTORY_SYMTAG_CASE(FuncDebugEnd, PDBSymbolFuncDebugEnd)
    FACTORY_SYMTAG_CASE(UsingNamespace, PDBSymbolUsingNamespace)
    FACTORY_SYMTAG_CASE(VTableShape, PDBSymbolTypeVTableShape)
    FACTORY_SYMTAG_CASE(VTable, PDBSymbolTypeVTable)
    FACTORY_SYMTAG_CASE(Custom, PDBSymbolCustom)
    FACTORY_SYMTAG_CASE(Thunk, PDBSymbolThunk)
    FACTORY_SYMTAG_CASE(CustomType, PDBSymbolTypeCustom)
    FACTORY_SYMTAG_CASE(ManagedType, PDBSymbolTypeManaged)
    FACTORY_SYMTAG_CASE(Dimension, PDBSymbolTypeDimension)
  default:
    return std::unique_ptr<PDBSymbol>(new PDBSymbolUnknown(PDBSession));
  }
}

std::unique_ptr<PDBSymbol>
PDBSymbol::create(const IPDBSession &PDBSession,
                  std::unique_ptr<IPDBRawSymbol> RawSymbol) {
  auto SymbolPtr = createSymbol(PDBSession, RawSymbol->getSymTag());
  SymbolPtr->RawSymbol = RawSymbol.get();
  SymbolPtr->OwnedRawSymbol = std::move(RawSymbol);
  return SymbolPtr;
}

std::unique_ptr<PDBSymbol> PDBSymbol::create(const IPDBSession &PDBSession,
                                             IPDBRawSymbol &RawSymbol) {
  auto SymbolPtr = createSymbol(PDBSession, RawSymbol.getSymTag());
  SymbolPtr->RawSymbol = &RawSymbol;
  return SymbolPtr;
}

void PDBSymbol::defaultDump(raw_ostream &OS, int Indent,
                            PdbSymbolIdField ShowFlags,
                            PdbSymbolIdField RecurseFlags) const {
  RawSymbol->dump(OS, Indent, ShowFlags, RecurseFlags);
}

void PDBSymbol::dumpProperties() const {
  outs() << "\n";
  defaultDump(outs(), 0, PdbSymbolIdField::All, PdbSymbolIdField::None);
  outs().flush();
}

void PDBSymbol::dumpChildStats() const {
  TagStats Stats;
  getChildStats(Stats);
  outs() << "\n";
  for (auto &Stat : Stats) {
    outs() << Stat.first << ": " << Stat.second << "\n";
  }
  outs().flush();
}

PDB_SymType PDBSymbol::getSymTag() const { return RawSymbol->getSymTag(); }
uint32_t PDBSymbol::getSymIndexId() const { return RawSymbol->getSymIndexId(); }

std::unique_ptr<IPDBEnumSymbols> PDBSymbol::findAllChildren() const {
  return findAllChildren(PDB_SymType::None);
}

std::unique_ptr<IPDBEnumSymbols>
PDBSymbol::findAllChildren(PDB_SymType Type) const {
  return RawSymbol->findChildren(Type);
}

std::unique_ptr<IPDBEnumSymbols>
PDBSymbol::findChildren(PDB_SymType Type, StringRef Name,
                        PDB_NameSearchFlags Flags) const {
  return RawSymbol->findChildren(Type, Name, Flags);
}

std::unique_ptr<IPDBEnumSymbols>
PDBSymbol::findChildrenByRVA(PDB_SymType Type, StringRef Name,
                             PDB_NameSearchFlags Flags, uint32_t RVA) const {
  return RawSymbol->findChildrenByRVA(Type, Name, Flags, RVA);
}

std::unique_ptr<IPDBEnumSymbols>
PDBSymbol::findInlineFramesByRVA(uint32_t RVA) const {
  return RawSymbol->findInlineFramesByRVA(RVA);
}

std::unique_ptr<IPDBEnumSymbols>
PDBSymbol::getChildStats(TagStats &Stats) const {
  std::unique_ptr<IPDBEnumSymbols> Result(findAllChildren());
  if (!Result)
    return nullptr;
  Stats.clear();
  while (auto Child = Result->getNext()) {
    ++Stats[Child->getSymTag()];
  }
  Result->reset();
  return Result;
}

std::unique_ptr<PDBSymbol> PDBSymbol::getSymbolByIdHelper(uint32_t Id) const {
  return Session.getSymbolById(Id);
}

void llvm::pdb::dumpSymbolIdField(raw_ostream &OS, StringRef Name,
                                  SymIndexId Value, int Indent,
                                  const IPDBSession &Session,
                                  PdbSymbolIdField FieldId,
                                  PdbSymbolIdField ShowFlags,
                                  PdbSymbolIdField RecurseFlags) {
  if ((FieldId & ShowFlags) == PdbSymbolIdField::None)
    return;

  OS << "\n";
  OS.indent(Indent);
  OS << Name << ": " << Value;
  // Don't recurse unless the user requested it.
  if ((FieldId & RecurseFlags) == PdbSymbolIdField::None)
    return;
  // And obviously don't recurse on the symbol itself.
  if (FieldId == PdbSymbolIdField::SymIndexId)
    return;

  auto Child = Session.getSymbolById(Value);

  // It could have been a placeholder symbol for a type we don't yet support,
  // so just exit in that case.
  if (!Child)
    return;

  // Don't recurse more than once, so pass PdbSymbolIdField::None) for the
  // recurse flags.
  Child->defaultDump(OS, Indent + 2, ShowFlags, PdbSymbolIdField::None);
}
