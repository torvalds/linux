//===- ASTImporterLookupTable.h - ASTImporter specific lookup--*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTImporterLookupTable class which implements a
//  lookup procedure for the import mechanism.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTIMPORTERLOOKUPTABLE_H
#define LLVM_CLANG_AST_ASTIMPORTERLOOKUPTABLE_H

#include "clang/AST/DeclBase.h" // lookup_result
#include "clang/AST/DeclarationName.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"

namespace clang {

class ASTContext;
class NamedDecl;
class DeclContext;

// There are certain cases when normal C/C++ lookup (localUncachedLookup)
// does not find AST nodes. E.g.:
// Example 1:
//   template <class T>
//   struct X {
//     friend void foo(); // this is never found in the DC of the TU.
//   };
// Example 2:
//   // The fwd decl to Foo is not found in the lookupPtr of the DC of the
//   // translation unit decl.
//   // Here we could find the node by doing a traverse throught the list of
//   // the Decls in the DC, but that would not scale.
//   struct A { struct Foo *p; };
// This is a severe problem because the importer decides if it has to create a
// new Decl or not based on the lookup results.
// To overcome these cases we need an importer specific lookup table which
// holds every node and we are not interested in any C/C++ specific visibility
// considerations. Simply, we must know if there is an existing Decl in a
// given DC. Once we found it then we can handle any visibility related tasks.
class ASTImporterLookupTable {

  // We store a list of declarations for each name.
  // And we collect these lists for each DeclContext.
  // We could have a flat map with (DeclContext, Name) tuple as key, but a two
  // level map seems easier to handle.
  using DeclList = llvm::SmallSetVector<NamedDecl *, 2>;
  using NameMap = llvm::SmallDenseMap<DeclarationName, DeclList, 4>;
  using DCMap = llvm::DenseMap<DeclContext *, NameMap>;

  void add(DeclContext *DC, NamedDecl *ND);
  void remove(DeclContext *DC, NamedDecl *ND);

  DCMap LookupTable;

public:
  ASTImporterLookupTable(TranslationUnitDecl &TU);
  void add(NamedDecl *ND);
  void remove(NamedDecl *ND);
  using LookupResult = DeclList;
  LookupResult lookup(DeclContext *DC, DeclarationName Name) const;
  void dump(DeclContext *DC) const;
  void dump() const;
};

} // namespace clang

#endif // LLVM_CLANG_AST_ASTIMPORTERLOOKUPTABLE_H
