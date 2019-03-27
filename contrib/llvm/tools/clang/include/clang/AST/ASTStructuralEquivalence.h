//===- ASTStructuralEquivalence.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the StructuralEquivalenceContext class which checks for
//  structural equivalence between types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTSTRUCTURALEQUIVALENCE_H
#define LLVM_CLANG_AST_ASTSTRUCTURALEQUIVALENCE_H

#include "clang/AST/DeclBase.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Optional.h"
#include <deque>
#include <utility>

namespace clang {

class ASTContext;
class Decl;
class DiagnosticBuilder;
class QualType;
class RecordDecl;
class SourceLocation;

/// \brief Whether to perform a normal or minimal equivalence check.
/// In case of `Minimal`, we do not perform a recursive check of decls with
/// external storage.
enum class StructuralEquivalenceKind {
  Default,
  Minimal,
};

struct StructuralEquivalenceContext {
  /// AST contexts for which we are checking structural equivalence.
  ASTContext &FromCtx, &ToCtx;

  /// The set of "tentative" equivalences between two canonical
  /// declarations, mapping from a declaration in the first context to the
  /// declaration in the second context that we believe to be equivalent.
  llvm::DenseMap<Decl *, Decl *> TentativeEquivalences;

  /// Queue of declarations in the first context whose equivalence
  /// with a declaration in the second context still needs to be verified.
  std::deque<Decl *> DeclsToCheck;

  /// Declaration (from, to) pairs that are known not to be equivalent
  /// (which we have already complained about).
  llvm::DenseSet<std::pair<Decl *, Decl *>> &NonEquivalentDecls;

  StructuralEquivalenceKind EqKind;

  /// Whether we're being strict about the spelling of types when
  /// unifying two types.
  bool StrictTypeSpelling;

  /// Whether warn or error on tag type mismatches.
  bool ErrorOnTagTypeMismatch;

  /// Whether to complain about failures.
  bool Complain;

  /// \c true if the last diagnostic came from ToCtx.
  bool LastDiagFromC2 = false;

  StructuralEquivalenceContext(
      ASTContext &FromCtx, ASTContext &ToCtx,
      llvm::DenseSet<std::pair<Decl *, Decl *>> &NonEquivalentDecls,
      StructuralEquivalenceKind EqKind,
      bool StrictTypeSpelling = false, bool Complain = true,
      bool ErrorOnTagTypeMismatch = false)
      : FromCtx(FromCtx), ToCtx(ToCtx), NonEquivalentDecls(NonEquivalentDecls),
        EqKind(EqKind), StrictTypeSpelling(StrictTypeSpelling),
        ErrorOnTagTypeMismatch(ErrorOnTagTypeMismatch), Complain(Complain) {}

  DiagnosticBuilder Diag1(SourceLocation Loc, unsigned DiagID);
  DiagnosticBuilder Diag2(SourceLocation Loc, unsigned DiagID);

  /// Determine whether the two declarations are structurally
  /// equivalent.
  /// Implementation functions (all static functions in
  /// ASTStructuralEquivalence.cpp) must never call this function because that
  /// will wreak havoc the internal state (\c DeclsToCheck and
  /// \c TentativeEquivalences members) and can cause faulty equivalent results.
  bool IsEquivalent(Decl *D1, Decl *D2);

  /// Determine whether the two types are structurally equivalent.
  /// Implementation functions (all static functions in
  /// ASTStructuralEquivalence.cpp) must never call this function because that
  /// will wreak havoc the internal state (\c DeclsToCheck and
  /// \c TentativeEquivalences members) and can cause faulty equivalent results.
  bool IsEquivalent(QualType T1, QualType T2);

  /// Find the index of the given anonymous struct/union within its
  /// context.
  ///
  /// \returns Returns the index of this anonymous struct/union in its context,
  /// including the next assigned index (if none of them match). Returns an
  /// empty option if the context is not a record, i.e.. if the anonymous
  /// struct/union is at namespace or block scope.
  ///
  /// FIXME: This is needed by ASTImporter and ASTStructureEquivalence. It
  /// probably makes more sense in some other common place then here.
  static llvm::Optional<unsigned>
  findUntaggedStructOrUnionIndex(RecordDecl *Anon);

private:
  /// Finish checking all of the structural equivalences.
  ///
  /// \returns true if the equivalence check failed (non-equivalence detected),
  /// false if equivalence was detected.
  bool Finish();

  /// Check for common properties at Finish.
  /// \returns true if D1 and D2 may be equivalent,
  /// false if they are for sure not.
  bool CheckCommonEquivalence(Decl *D1, Decl *D2);

  /// Check for class dependent properties at Finish.
  /// \returns true if D1 and D2 may be equivalent,
  /// false if they are for sure not.
  bool CheckKindSpecificEquivalence(Decl *D1, Decl *D2);
};

} // namespace clang

#endif // LLVM_CLANG_AST_ASTSTRUCTURALEQUIVALENCE_H
