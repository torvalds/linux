//===- CXXFieldCollector.h - Utility class for C++ class semantic analysis ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file provides CXXFieldCollector that is used during parsing & semantic
//  analysis of C++ classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_CXXFIELDCOLLECTOR_H
#define LLVM_CLANG_SEMA_CXXFIELDCOLLECTOR_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {
  class FieldDecl;

/// CXXFieldCollector - Used to keep track of CXXFieldDecls during parsing of
/// C++ classes.
class CXXFieldCollector {
  /// Fields - Contains all FieldDecls collected during parsing of a C++
  /// class. When a nested class is entered, its fields are appended to the
  /// fields of its parent class, when it is exited its fields are removed.
  SmallVector<FieldDecl*, 32> Fields;

  /// FieldCount - Each entry represents the number of fields collected during
  /// the parsing of a C++ class. When a nested class is entered, a new field
  /// count is pushed, when it is exited, the field count is popped.
  SmallVector<size_t, 4> FieldCount;

  // Example:
  //
  // class C {
  //   int x,y;
  //   class NC {
  //     int q;
  //     // At this point, Fields contains [x,y,q] decls and FieldCount contains
  //     // [2,1].
  //   };
  //   int z;
  //   // At this point, Fields contains [x,y,z] decls and FieldCount contains
  //   // [3].
  // };

public:
  /// StartClass - Called by Sema::ActOnStartCXXClassDef.
  void StartClass() { FieldCount.push_back(0); }

  /// Add - Called by Sema::ActOnCXXMemberDeclarator.
  void Add(FieldDecl *D) {
    Fields.push_back(D);
    ++FieldCount.back();
  }

  /// getCurNumField - The number of fields added to the currently parsed class.
  size_t getCurNumFields() const {
    assert(!FieldCount.empty() && "no currently-parsed class");
    return FieldCount.back();
  }

  /// getCurFields - Pointer to array of fields added to the currently parsed
  /// class.
  FieldDecl **getCurFields() { return &*(Fields.end() - getCurNumFields()); }

  /// FinishClass - Called by Sema::ActOnFinishCXXClassDef.
  void FinishClass() {
    Fields.resize(Fields.size() - getCurNumFields());
    FieldCount.pop_back();
  }
};

} // end namespace clang

#endif
