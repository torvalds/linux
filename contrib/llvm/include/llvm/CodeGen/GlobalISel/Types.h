//===- llvm/CodeGen/GlobalISel/Types.h - Types used by GISel ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes high level types that are used by several passes or
/// APIs involved in the GlobalISel pipeline.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_TYPES_H
#define LLVM_CODEGEN_GLOBALISEL_TYPES_H

#include "llvm/ADT/DenseMap.h"

namespace llvm {

class Value;

/// Map a value to a virtual register.
/// For now, we chose to map aggregate types to on single virtual
/// register. This might be revisited if it turns out to be inefficient.
/// PR26161 tracks that.
/// Note: We need to expose this type to the target hooks for thing like
/// ABI lowering that would be used during IRTranslation.
using ValueToVReg = DenseMap<const Value *, unsigned>;

} // end namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_TYPES_H
