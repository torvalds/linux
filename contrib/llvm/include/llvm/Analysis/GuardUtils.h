//===-- GuardUtils.h - Utils for work with guards ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Utils that are used to perform analyzes related to guards and their
// conditions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_GUARDUTILS_H
#define LLVM_ANALYSIS_GUARDUTILS_H

namespace llvm {

class User;

/// Returns true iff \p U has semantics of a guard.
bool isGuard(const User *U);

} // llvm

#endif // LLVM_ANALYSIS_GUARDUTILS_H

