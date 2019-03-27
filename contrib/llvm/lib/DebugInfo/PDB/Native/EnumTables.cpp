//===- EnumTables.cpp - Enum to string conversion tables --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/EnumTables.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"

using namespace llvm;
using namespace llvm::pdb;

#define PDB_ENUM_CLASS_ENT(enum_class, enum)                                   \
  { #enum, std::underlying_type < enum_class > ::type(enum_class::enum) }

#define PDB_ENUM_ENT(ns, enum)                                                 \
  { #enum, ns::enum }

static const EnumEntry<uint16_t> OMFSegMapDescFlagNames[] = {
    PDB_ENUM_CLASS_ENT(OMFSegDescFlags, Read),
    PDB_ENUM_CLASS_ENT(OMFSegDescFlags, Write),
    PDB_ENUM_CLASS_ENT(OMFSegDescFlags, Execute),
    PDB_ENUM_CLASS_ENT(OMFSegDescFlags, AddressIs32Bit),
    PDB_ENUM_CLASS_ENT(OMFSegDescFlags, IsSelector),
    PDB_ENUM_CLASS_ENT(OMFSegDescFlags, IsAbsoluteAddress),
    PDB_ENUM_CLASS_ENT(OMFSegDescFlags, IsGroup),
};

namespace llvm {
namespace pdb {
ArrayRef<EnumEntry<uint16_t>> getOMFSegMapDescFlagNames() {
  return makeArrayRef(OMFSegMapDescFlagNames);
}
}
}