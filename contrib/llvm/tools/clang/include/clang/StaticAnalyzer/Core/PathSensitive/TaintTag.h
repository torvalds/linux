//===- TaintTag.h - Path-sensitive "State" for tracking values --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines a set of taint tags. Several tags are used to differentiate kinds
// of taint.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_TAINTTAG_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_TAINTTAG_H

namespace clang {
namespace ento {

/// The type of taint, which helps to differentiate between different types of
/// taint.
using TaintTagType = unsigned;

static const TaintTagType TaintTagGeneric = 0;

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_TAINTTAG_H
