//===- EnumTables.h - Enum to string conversion tables ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_ENUMTABLES_H
#define LLVM_DEBUGINFO_PDB_RAW_ENUMTABLES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/ScopedPrinter.h"

namespace llvm {
namespace pdb {
ArrayRef<EnumEntry<uint16_t>> getOMFSegMapDescFlagNames();
}
}

#endif // LLVM_DEBUGINFO_PDB_RAW_ENUMTABLES_H
