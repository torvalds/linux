//===-- PDBContext.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===/

#include "llvm/DebugInfo/PDB/PDBContext.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/IPDBSourceFile.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"
#include "llvm/DebugInfo/PDB/PDBSymbolPublicSymbol.h"
#include "llvm/Object/COFF.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::pdb;

PDBContext::PDBContext(const COFFObjectFile &Object,
                       std::unique_ptr<IPDBSession> PDBSession)
    : DIContext(CK_PDB), Session(std::move(PDBSession)) {
  ErrorOr<uint64_t> ImageBase = Object.getImageBase();
  if (ImageBase)
    Session->setLoadAddress(ImageBase.get());
}

void PDBContext::dump(raw_ostream &OS, DIDumpOptions DumpOpts){}

DILineInfo PDBContext::getLineInfoForAddress(uint64_t Address,
                                             DILineInfoSpecifier Specifier) {
  DILineInfo Result;
  Result.FunctionName = getFunctionName(Address, Specifier.FNKind);

  uint32_t Length = 1;
  std::unique_ptr<PDBSymbol> Symbol =
      Session->findSymbolByAddress(Address, PDB_SymType::None);
  if (auto Func = dyn_cast_or_null<PDBSymbolFunc>(Symbol.get())) {
    Length = Func->getLength();
  } else if (auto Data = dyn_cast_or_null<PDBSymbolData>(Symbol.get())) {
    Length = Data->getLength();
  }

  // If we couldn't find a symbol, then just assume 1 byte, so that we get
  // only the line number of the first instruction.
  auto LineNumbers = Session->findLineNumbersByAddress(Address, Length);
  if (!LineNumbers || LineNumbers->getChildCount() == 0)
    return Result;

  auto LineInfo = LineNumbers->getNext();
  assert(LineInfo);
  auto SourceFile = Session->getSourceFileById(LineInfo->getSourceFileId());

  if (SourceFile &&
      Specifier.FLIKind != DILineInfoSpecifier::FileLineInfoKind::None)
    Result.FileName = SourceFile->getFileName();
  Result.Column = LineInfo->getColumnNumber();
  Result.Line = LineInfo->getLineNumber();
  return Result;
}

DILineInfoTable
PDBContext::getLineInfoForAddressRange(uint64_t Address, uint64_t Size,
                                       DILineInfoSpecifier Specifier) {
  if (Size == 0)
    return DILineInfoTable();

  DILineInfoTable Table;
  auto LineNumbers = Session->findLineNumbersByAddress(Address, Size);
  if (!LineNumbers || LineNumbers->getChildCount() == 0)
    return Table;

  while (auto LineInfo = LineNumbers->getNext()) {
    DILineInfo LineEntry =
        getLineInfoForAddress(LineInfo->getVirtualAddress(), Specifier);
    Table.push_back(std::make_pair(LineInfo->getVirtualAddress(), LineEntry));
  }
  return Table;
}

DIInliningInfo
PDBContext::getInliningInfoForAddress(uint64_t Address,
                                      DILineInfoSpecifier Specifier) {
  DIInliningInfo InlineInfo;
  DILineInfo Frame = getLineInfoForAddress(Address, Specifier);
  InlineInfo.addFrame(Frame);
  return InlineInfo;
}

std::string PDBContext::getFunctionName(uint64_t Address,
                                        DINameKind NameKind) const {
  if (NameKind == DINameKind::None)
    return std::string();

  std::unique_ptr<PDBSymbol> FuncSymbol =
      Session->findSymbolByAddress(Address, PDB_SymType::Function);
  auto *Func = dyn_cast_or_null<PDBSymbolFunc>(FuncSymbol.get());

  if (NameKind == DINameKind::LinkageName) {
    // It is not possible to get the mangled linkage name through a
    // PDBSymbolFunc.  For that we have to specifically request a
    // PDBSymbolPublicSymbol.
    auto PublicSym =
        Session->findSymbolByAddress(Address, PDB_SymType::PublicSymbol);
    if (auto *PS = dyn_cast_or_null<PDBSymbolPublicSymbol>(PublicSym.get())) {
      // If we also have a function symbol, prefer the use of public symbol name
      // only if it refers to the same address. The public symbol uses the
      // linkage name while the function does not.
      if (!Func || Func->getVirtualAddress() == PS->getVirtualAddress())
        return PS->getName();
    }
  }

  return Func ? Func->getName() : std::string();
}
