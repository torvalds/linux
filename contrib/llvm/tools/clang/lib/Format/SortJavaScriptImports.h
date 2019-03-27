//===--- SortJavaScriptImports.h - Sort ES6 Imports -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a sorter for JavaScript ES6 imports.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_SORTJAVASCRIPTIMPORTS_H
#define LLVM_CLANG_LIB_FORMAT_SORTJAVASCRIPTIMPORTS_H

#include "clang/Basic/LLVM.h"
#include "clang/Format/Format.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
namespace format {

// Sort JavaScript ES6 imports/exports in ``Code``. The generated replacements
// only monotonically increase the length of the given code.
tooling::Replacements sortJavaScriptImports(const FormatStyle &Style,
                                            StringRef Code,
                                            ArrayRef<tooling::Range> Ranges,
                                            StringRef FileName);

} // end namespace format
} // end namespace clang

#endif
