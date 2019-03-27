//===-- CostTable.h - Instruction Cost Table handling -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Cost tables and simple lookup functions
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_COSTTABLE_H_
#define LLVM_CODEGEN_COSTTABLE_H_

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/MachineValueType.h"

namespace llvm {

/// Cost Table Entry
struct CostTblEntry {
  int ISD;
  MVT::SimpleValueType Type;
  unsigned Cost;
};

/// Find in cost table, TypeTy must be comparable to CompareTy by ==
inline const CostTblEntry *CostTableLookup(ArrayRef<CostTblEntry> Tbl,
                                           int ISD, MVT Ty) {
  auto I = find_if(Tbl, [=](const CostTblEntry &Entry) {
    return ISD == Entry.ISD && Ty == Entry.Type;
  });
  if (I != Tbl.end())
    return I;

  // Could not find an entry.
  return nullptr;
}

/// Type Conversion Cost Table
struct TypeConversionCostTblEntry {
  int ISD;
  MVT::SimpleValueType Dst;
  MVT::SimpleValueType Src;
  unsigned Cost;
};

/// Find in type conversion cost table, TypeTy must be comparable to CompareTy
/// by ==
inline const TypeConversionCostTblEntry *
ConvertCostTableLookup(ArrayRef<TypeConversionCostTblEntry> Tbl,
                       int ISD, MVT Dst, MVT Src) {
  auto I = find_if(Tbl, [=](const TypeConversionCostTblEntry &Entry) {
    return ISD == Entry.ISD && Src == Entry.Src && Dst == Entry.Dst;
  });
  if (I != Tbl.end())
    return I;

  // Could not find an entry.
  return nullptr;
}

} // namespace llvm

#endif /* LLVM_CODEGEN_COSTTABLE_H_ */
