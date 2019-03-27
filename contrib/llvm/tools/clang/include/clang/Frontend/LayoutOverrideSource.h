//===--- LayoutOverrideSource.h --Override Record Layouts -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_LAYOUTOVERRIDESOURCE_H
#define LLVM_CLANG_FRONTEND_LAYOUTOVERRIDESOURCE_H

#include "clang/AST/ExternalASTSource.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
  /// An external AST source that overrides the layout of
  /// a specified set of record types.
  ///
  /// This class is used only for testing the ability of external AST sources
  /// to override the layout of record types. Its input is the output format
  /// of the command-line argument -fdump-record-layouts.
  class LayoutOverrideSource : public ExternalASTSource {
    /// The layout of a given record.
    struct Layout {
      /// The size of the record.
      uint64_t Size;

      /// The alignment of the record.
      uint64_t Align;

      /// The offsets of the fields, in source order.
      SmallVector<uint64_t, 8> FieldOffsets;
    };

    /// The set of layouts that will be overridden.
    llvm::StringMap<Layout> Layouts;

  public:
    /// Create a new AST source that overrides the layout of some
    /// set of record types.
    ///
    /// The file is the result of passing -fdump-record-layouts to a file.
    explicit LayoutOverrideSource(StringRef Filename);

    /// If this particular record type has an overridden layout,
    /// return that layout.
    bool
    layoutRecordType(const RecordDecl *Record,
       uint64_t &Size, uint64_t &Alignment,
       llvm::DenseMap<const FieldDecl *, uint64_t> &FieldOffsets,
       llvm::DenseMap<const CXXRecordDecl *, CharUnits> &BaseOffsets,
       llvm::DenseMap<const CXXRecordDecl *,
                      CharUnits> &VirtualBaseOffsets) override;

    /// Dump the overridden layouts.
    void dump();
  };
}

#endif
