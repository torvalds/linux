//===- NativeTypeFunctionSig.h - info about function signature ---*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEFUNCTIONSIG_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEFUNCTIONSIG_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"

namespace llvm {
namespace pdb {

class NativeTypeUDT;

class NativeTypeFunctionSig : public NativeRawSymbol {
protected:
  void initialize() override;

public:
  NativeTypeFunctionSig(NativeSession &Session, SymIndexId Id,
                        codeview::TypeIndex TI, codeview::ProcedureRecord Proc);

  NativeTypeFunctionSig(NativeSession &Session, SymIndexId Id,
                        codeview::TypeIndex TI,
                        codeview::MemberFunctionRecord MemberFunc);

  ~NativeTypeFunctionSig() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type) const override;

  SymIndexId getClassParentId() const override;
  PDB_CallingConv getCallingConvention() const override;
  uint32_t getCount() const override;
  SymIndexId getTypeId() const override;
  int32_t getThisAdjust() const override;
  bool hasConstructor() const override;
  bool isConstType() const override;
  bool isConstructorVirtualBase() const override;
  bool isCxxReturnUdt() const override;
  bool isUnalignedType() const override;
  bool isVolatileType() const override;

private:
  void initializeArgList(codeview::TypeIndex ArgListTI);

  union {
    codeview::MemberFunctionRecord MemberFunc;
    codeview::ProcedureRecord Proc;
  };

  SymIndexId ClassParentId = 0;
  codeview::TypeIndex Index;
  codeview::ArgListRecord ArgList;
  bool IsMemberFunction = false;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEPOINTER_H