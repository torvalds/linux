//===- EditedSource.h - Collection of source edits --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_EDIT_EDITSRECEIVER_H
#define LLVM_CLANG_EDIT_EDITSRECEIVER_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
namespace edit {

class EditsReceiver {
public:
  virtual ~EditsReceiver() = default;

  virtual void insert(SourceLocation loc, StringRef text) = 0;
  virtual void replace(CharSourceRange range, StringRef text) = 0;

  /// By default it calls replace with an empty string.
  virtual void remove(CharSourceRange range);
};

} // namespace edit
} // namespace clang

#endif // LLVM_CLANG_EDIT_EDITSRECEIVER_H
