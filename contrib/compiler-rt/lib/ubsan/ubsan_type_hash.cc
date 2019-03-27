//===-- ubsan_type_hash.cc ------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of a hash table for fast checking of inheritance
// relationships. This file is only linked into C++ compilations, and is
// permitted to use language features which require a C++ ABI library.
//
// Most of the implementation lives in an ABI-specific source file
// (ubsan_type_hash_{itanium,win}.cc).
//
//===----------------------------------------------------------------------===//

#include "ubsan_platform.h"
#if CAN_SANITIZE_UB
#include "ubsan_type_hash.h"

#include "sanitizer_common/sanitizer_common.h"

/// A cache of recently-checked hashes. Mini hash table with "random" evictions.
__ubsan::HashValue
__ubsan::__ubsan_vptr_type_cache[__ubsan::VptrTypeCacheSize];

__ubsan::DynamicTypeInfo __ubsan::getDynamicTypeInfoFromObject(void *Object) {
  void *VtablePtr = *reinterpret_cast<void **>(Object);
  return getDynamicTypeInfoFromVtable(VtablePtr);
}

#endif  // CAN_SANITIZE_UB
