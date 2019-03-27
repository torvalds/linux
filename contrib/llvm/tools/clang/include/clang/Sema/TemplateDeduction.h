//===- TemplateDeduction.h - C++ template argument deduction ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides types used with Sema's template argument deduction
// routines.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_TEMPLATEDEDUCTION_H
#define LLVM_CLANG_SEMA_TEMPLATEDEDUCTION_H

#include "clang/AST/DeclAccessPair.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/TemplateBase.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <cstddef>
#include <utility>

namespace clang {

class Decl;
struct DeducedPack;
class Sema;

namespace sema {

/// Provides information about an attempted template argument
/// deduction, whose success or failure was described by a
/// TemplateDeductionResult value.
class TemplateDeductionInfo {
  /// The deduced template argument list.
  TemplateArgumentList *Deduced = nullptr;

  /// The source location at which template argument
  /// deduction is occurring.
  SourceLocation Loc;

  /// Have we suppressed an error during deduction?
  bool HasSFINAEDiagnostic = false;

  /// The template parameter depth for which we're performing deduction.
  unsigned DeducedDepth;

  /// The number of parameters with explicitly-specified template arguments,
  /// up to and including the partially-specified pack (if any).
  unsigned ExplicitArgs = 0;

  /// Warnings (and follow-on notes) that were suppressed due to
  /// SFINAE while performing template argument deduction.
  SmallVector<PartialDiagnosticAt, 4> SuppressedDiagnostics;

public:
  TemplateDeductionInfo(SourceLocation Loc, unsigned DeducedDepth = 0)
      : Loc(Loc), DeducedDepth(DeducedDepth) {}
  TemplateDeductionInfo(const TemplateDeductionInfo &) = delete;
  TemplateDeductionInfo &operator=(const TemplateDeductionInfo &) = delete;

  /// Returns the location at which template argument is
  /// occurring.
  SourceLocation getLocation() const {
    return Loc;
  }

  /// The depth of template parameters for which deduction is being
  /// performed.
  unsigned getDeducedDepth() const {
    return DeducedDepth;
  }

  /// Get the number of explicitly-specified arguments.
  unsigned getNumExplicitArgs() const {
    return ExplicitArgs;
  }

  /// Take ownership of the deduced template argument list.
  TemplateArgumentList *take() {
    TemplateArgumentList *Result = Deduced;
    Deduced = nullptr;
    return Result;
  }

  /// Take ownership of the SFINAE diagnostic.
  void takeSFINAEDiagnostic(PartialDiagnosticAt &PD) {
    assert(HasSFINAEDiagnostic);
    PD.first = SuppressedDiagnostics.front().first;
    PD.second.swap(SuppressedDiagnostics.front().second);
    clearSFINAEDiagnostic();
  }

  /// Discard any SFINAE diagnostics.
  void clearSFINAEDiagnostic() {
    SuppressedDiagnostics.clear();
    HasSFINAEDiagnostic = false;
  }

  /// Peek at the SFINAE diagnostic.
  const PartialDiagnosticAt &peekSFINAEDiagnostic() const {
    assert(HasSFINAEDiagnostic);
    return SuppressedDiagnostics.front();
  }

  /// Provide an initial template argument list that contains the
  /// explicitly-specified arguments.
  void setExplicitArgs(TemplateArgumentList *NewDeduced) {
    Deduced = NewDeduced;
    ExplicitArgs = Deduced->size();
  }

  /// Provide a new template argument list that contains the
  /// results of template argument deduction.
  void reset(TemplateArgumentList *NewDeduced) {
    Deduced = NewDeduced;
  }

  /// Is a SFINAE diagnostic available?
  bool hasSFINAEDiagnostic() const {
    return HasSFINAEDiagnostic;
  }

  /// Set the diagnostic which caused the SFINAE failure.
  void addSFINAEDiagnostic(SourceLocation Loc, PartialDiagnostic PD) {
    // Only collect the first diagnostic.
    if (HasSFINAEDiagnostic)
      return;
    SuppressedDiagnostics.clear();
    SuppressedDiagnostics.emplace_back(Loc, std::move(PD));
    HasSFINAEDiagnostic = true;
  }

  /// Add a new diagnostic to the set of diagnostics
  void addSuppressedDiagnostic(SourceLocation Loc,
                               PartialDiagnostic PD) {
    if (HasSFINAEDiagnostic)
      return;
    SuppressedDiagnostics.emplace_back(Loc, std::move(PD));
  }

  /// Iterator over the set of suppressed diagnostics.
  using diag_iterator = SmallVectorImpl<PartialDiagnosticAt>::const_iterator;

  /// Returns an iterator at the beginning of the sequence of suppressed
  /// diagnostics.
  diag_iterator diag_begin() const { return SuppressedDiagnostics.begin(); }

  /// Returns an iterator at the end of the sequence of suppressed
  /// diagnostics.
  diag_iterator diag_end() const { return SuppressedDiagnostics.end(); }

  /// The template parameter to which a template argument
  /// deduction failure refers.
  ///
  /// Depending on the result of template argument deduction, this
  /// template parameter may have different meanings:
  ///
  ///   TDK_Incomplete: this is the first template parameter whose
  ///   corresponding template argument was not deduced.
  ///
  ///   TDK_IncompletePack: this is the expanded parameter pack for
  ///   which we deduced too few arguments.
  ///
  ///   TDK_Inconsistent: this is the template parameter for which
  ///   two different template argument values were deduced.
  TemplateParameter Param;

  /// The first template argument to which the template
  /// argument deduction failure refers.
  ///
  /// Depending on the result of the template argument deduction,
  /// this template argument may have different meanings:
  ///
  ///   TDK_IncompletePack: this is the number of arguments we deduced
  ///   for the pack.
  ///
  ///   TDK_Inconsistent: this argument is the first value deduced
  ///   for the corresponding template parameter.
  ///
  ///   TDK_SubstitutionFailure: this argument is the template
  ///   argument we were instantiating when we encountered an error.
  ///
  ///   TDK_DeducedMismatch: this is the parameter type, after substituting
  ///   deduced arguments.
  ///
  ///   TDK_NonDeducedMismatch: this is the component of the 'parameter'
  ///   of the deduction, directly provided in the source code.
  TemplateArgument FirstArg;

  /// The second template argument to which the template
  /// argument deduction failure refers.
  ///
  ///   TDK_Inconsistent: this argument is the second value deduced
  ///   for the corresponding template parameter.
  ///
  ///   TDK_DeducedMismatch: this is the (adjusted) call argument type.
  ///
  ///   TDK_NonDeducedMismatch: this is the mismatching component of the
  ///   'argument' of the deduction, from which we are deducing arguments.
  ///
  /// FIXME: Finish documenting this.
  TemplateArgument SecondArg;

  /// The index of the function argument that caused a deduction
  /// failure.
  ///
  ///   TDK_DeducedMismatch: this is the index of the argument that had a
  ///   different argument type from its substituted parameter type.
  unsigned CallArgIndex = 0;

  /// Information on packs that we're currently expanding.
  ///
  /// FIXME: This should be kept internal to SemaTemplateDeduction.
  SmallVector<DeducedPack *, 8> PendingDeducedPacks;
};

} // namespace sema

/// A structure used to record information about a failed
/// template argument deduction, for diagnosis.
struct DeductionFailureInfo {
  /// A Sema::TemplateDeductionResult.
  unsigned Result : 8;

  /// Indicates whether a diagnostic is stored in Diagnostic.
  unsigned HasDiagnostic : 1;

  /// Opaque pointer containing additional data about
  /// this deduction failure.
  void *Data;

  /// A diagnostic indicating why deduction failed.
  alignas(PartialDiagnosticAt) char Diagnostic[sizeof(PartialDiagnosticAt)];

  /// Retrieve the diagnostic which caused this deduction failure,
  /// if any.
  PartialDiagnosticAt *getSFINAEDiagnostic();

  /// Retrieve the template parameter this deduction failure
  /// refers to, if any.
  TemplateParameter getTemplateParameter();

  /// Retrieve the template argument list associated with this
  /// deduction failure, if any.
  TemplateArgumentList *getTemplateArgumentList();

  /// Return the first template argument this deduction failure
  /// refers to, if any.
  const TemplateArgument *getFirstArg();

  /// Return the second template argument this deduction failure
  /// refers to, if any.
  const TemplateArgument *getSecondArg();

  /// Return the index of the call argument that this deduction
  /// failure refers to, if any.
  llvm::Optional<unsigned> getCallArgIndex();

  /// Free any memory associated with this deduction failure.
  void Destroy();
};

/// TemplateSpecCandidate - This is a generalization of OverloadCandidate
/// which keeps track of template argument deduction failure info, when
/// handling explicit specializations (and instantiations) of templates
/// beyond function overloading.
/// For now, assume that the candidates are non-matching specializations.
/// TODO: In the future, we may need to unify/generalize this with
/// OverloadCandidate.
struct TemplateSpecCandidate {
  /// The declaration that was looked up, together with its access.
  /// Might be a UsingShadowDecl, but usually a FunctionTemplateDecl.
  DeclAccessPair FoundDecl;

  /// Specialization - The actual specialization that this candidate
  /// represents. When NULL, this may be a built-in candidate.
  Decl *Specialization;

  /// Template argument deduction info
  DeductionFailureInfo DeductionFailure;

  void set(DeclAccessPair Found, Decl *Spec, DeductionFailureInfo Info) {
    FoundDecl = Found;
    Specialization = Spec;
    DeductionFailure = Info;
  }

  /// Diagnose a template argument deduction failure.
  void NoteDeductionFailure(Sema &S, bool ForTakingAddress);
};

/// TemplateSpecCandidateSet - A set of generalized overload candidates,
/// used in template specializations.
/// TODO: In the future, we may need to unify/generalize this with
/// OverloadCandidateSet.
class TemplateSpecCandidateSet {
  SmallVector<TemplateSpecCandidate, 16> Candidates;
  SourceLocation Loc;

  // Stores whether we're taking the address of these candidates. This helps us
  // produce better error messages when dealing with the pass_object_size
  // attribute on parameters.
  bool ForTakingAddress;

  void destroyCandidates();

public:
  TemplateSpecCandidateSet(SourceLocation Loc, bool ForTakingAddress = false)
      : Loc(Loc), ForTakingAddress(ForTakingAddress) {}
  TemplateSpecCandidateSet(const TemplateSpecCandidateSet &) = delete;
  TemplateSpecCandidateSet &
  operator=(const TemplateSpecCandidateSet &) = delete;
  ~TemplateSpecCandidateSet() { destroyCandidates(); }

  SourceLocation getLocation() const { return Loc; }

  /// Clear out all of the candidates.
  /// TODO: This may be unnecessary.
  void clear();

  using iterator = SmallVector<TemplateSpecCandidate, 16>::iterator;

  iterator begin() { return Candidates.begin(); }
  iterator end() { return Candidates.end(); }

  size_t size() const { return Candidates.size(); }
  bool empty() const { return Candidates.empty(); }

  /// Add a new candidate with NumConversions conversion sequence slots
  /// to the overload set.
  TemplateSpecCandidate &addCandidate() {
    Candidates.emplace_back();
    return Candidates.back();
  }

  void NoteCandidates(Sema &S, SourceLocation Loc);

  void NoteCandidates(Sema &S, SourceLocation Loc) const {
    const_cast<TemplateSpecCandidateSet *>(this)->NoteCandidates(S, Loc);
  }
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_TEMPLATEDEDUCTION_H
