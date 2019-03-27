//===- Hash.h - PDB hash functions ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_HASH_H
#define LLVM_DEBUGINFO_PDB_NATIVE_HASH_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace llvm {
namespace pdb {

uint32_t hashStringV1(StringRef Str);
uint32_t hashStringV2(StringRef Str);
uint32_t hashBufferV8(ArrayRef<uint8_t> Data);

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_HASH_H
