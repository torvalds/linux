//===--- DiagnosticIDs.h - Diagnostic IDs Handling --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the Diagnostic IDs-related interfaces.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_DIAGNOSTICIDS_H
#define LLVM_CLANG_BASIC_DIAGNOSTICIDS_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include <vector>

namespace clang {
  class DiagnosticsEngine;
  class SourceLocation;

  // Import the diagnostic enums themselves.
  namespace diag {
    // Size of each of the diagnostic categories.
    enum {
      DIAG_SIZE_COMMON        =  300,
      DIAG_SIZE_DRIVER        =  200,
      DIAG_SIZE_FRONTEND      =  150,
      DIAG_SIZE_SERIALIZATION =  120,
      DIAG_SIZE_LEX           =  400,
      DIAG_SIZE_PARSE         =  500,
      DIAG_SIZE_AST           =  150,
      DIAG_SIZE_COMMENT       =  100,
      DIAG_SIZE_CROSSTU       =  100,
      DIAG_SIZE_SEMA          = 3500,
      DIAG_SIZE_ANALYSIS      =  100,
      DIAG_SIZE_REFACTORING   = 1000,
    };
    // Start position for diagnostics.
    enum {
      DIAG_START_COMMON        =                          0,
      DIAG_START_DRIVER        = DIAG_START_COMMON        + DIAG_SIZE_COMMON,
      DIAG_START_FRONTEND      = DIAG_START_DRIVER        + DIAG_SIZE_DRIVER,
      DIAG_START_SERIALIZATION = DIAG_START_FRONTEND      + DIAG_SIZE_FRONTEND,
      DIAG_START_LEX           = DIAG_START_SERIALIZATION + DIAG_SIZE_SERIALIZATION,
      DIAG_START_PARSE         = DIAG_START_LEX           + DIAG_SIZE_LEX,
      DIAG_START_AST           = DIAG_START_PARSE         + DIAG_SIZE_PARSE,
      DIAG_START_COMMENT       = DIAG_START_AST           + DIAG_SIZE_AST,
      DIAG_START_CROSSTU       = DIAG_START_COMMENT       + DIAG_SIZE_CROSSTU,
      DIAG_START_SEMA          = DIAG_START_CROSSTU       + DIAG_SIZE_COMMENT,
      DIAG_START_ANALYSIS      = DIAG_START_SEMA          + DIAG_SIZE_SEMA,
      DIAG_START_REFACTORING   = DIAG_START_ANALYSIS      + DIAG_SIZE_ANALYSIS,
      DIAG_UPPER_LIMIT         = DIAG_START_REFACTORING   + DIAG_SIZE_REFACTORING
    };

    class CustomDiagInfo;

    /// All of the diagnostics that can be emitted by the frontend.
    typedef unsigned kind;

    // Get typedefs for common diagnostics.
    enum {
#define DIAG(ENUM,FLAGS,DEFAULT_MAPPING,DESC,GROUP,\
             SFINAE,CATEGORY,NOWERROR,SHOWINSYSHEADER) ENUM,
#define COMMONSTART
#include "clang/Basic/DiagnosticCommonKinds.inc"
      NUM_BUILTIN_COMMON_DIAGNOSTICS
#undef DIAG
    };

    /// Enum values that allow the client to map NOTEs, WARNINGs, and EXTENSIONs
    /// to either Ignore (nothing), Remark (emit a remark), Warning
    /// (emit a warning) or Error (emit as an error).  It allows clients to
    /// map ERRORs to Error or Fatal (stop emitting diagnostics after this one).
    enum class Severity {
      // NOTE: 0 means "uncomputed".
      Ignored = 1, ///< Do not present this diagnostic, ignore it.
      Remark = 2,  ///< Present this diagnostic as a remark.
      Warning = 3, ///< Present this diagnostic as a warning.
      Error = 4,   ///< Present this diagnostic as an error.
      Fatal = 5    ///< Present this diagnostic as a fatal error.
    };

    /// Flavors of diagnostics we can emit. Used to filter for a particular
    /// kind of diagnostic (for instance, for -W/-R flags).
    enum class Flavor {
      WarningOrError, ///< A diagnostic that indicates a problem or potential
                      ///< problem. Can be made fatal by -Werror.
      Remark          ///< A diagnostic that indicates normal progress through
                      ///< compilation.
    };
  }

class DiagnosticMapping {
  unsigned Severity : 3;
  unsigned IsUser : 1;
  unsigned IsPragma : 1;
  unsigned HasNoWarningAsError : 1;
  unsigned HasNoErrorAsFatal : 1;
  unsigned WasUpgradedFromWarning : 1;

public:
  static DiagnosticMapping Make(diag::Severity Severity, bool IsUser,
                                bool IsPragma) {
    DiagnosticMapping Result;
    Result.Severity = (unsigned)Severity;
    Result.IsUser = IsUser;
    Result.IsPragma = IsPragma;
    Result.HasNoWarningAsError = 0;
    Result.HasNoErrorAsFatal = 0;
    Result.WasUpgradedFromWarning = 0;
    return Result;
  }

  diag::Severity getSeverity() const { return (diag::Severity)Severity; }
  void setSeverity(diag::Severity Value) { Severity = (unsigned)Value; }

  bool isUser() const { return IsUser; }
  bool isPragma() const { return IsPragma; }

  bool isErrorOrFatal() const {
    return getSeverity() == diag::Severity::Error ||
           getSeverity() == diag::Severity::Fatal;
  }

  bool hasNoWarningAsError() const { return HasNoWarningAsError; }
  void setNoWarningAsError(bool Value) { HasNoWarningAsError = Value; }

  bool hasNoErrorAsFatal() const { return HasNoErrorAsFatal; }
  void setNoErrorAsFatal(bool Value) { HasNoErrorAsFatal = Value; }

  /// Whether this mapping attempted to map the diagnostic to a warning, but
  /// was overruled because the diagnostic was already mapped to an error or
  /// fatal error.
  bool wasUpgradedFromWarning() const { return WasUpgradedFromWarning; }
  void setUpgradedFromWarning(bool Value) { WasUpgradedFromWarning = Value; }

  /// Serialize this mapping as a raw integer.
  unsigned serialize() const {
    return (IsUser << 7) | (IsPragma << 6) | (HasNoWarningAsError << 5) |
           (HasNoErrorAsFatal << 4) | (WasUpgradedFromWarning << 3) | Severity;
  }
  /// Deserialize a mapping.
  static DiagnosticMapping deserialize(unsigned Bits) {
    DiagnosticMapping Result;
    Result.IsUser = (Bits >> 7) & 1;
    Result.IsPragma = (Bits >> 6) & 1;
    Result.HasNoWarningAsError = (Bits >> 5) & 1;
    Result.HasNoErrorAsFatal = (Bits >> 4) & 1;
    Result.WasUpgradedFromWarning = (Bits >> 3) & 1;
    Result.Severity = Bits & 0x7;
    return Result;
  }
};

/// Used for handling and querying diagnostic IDs.
///
/// Can be used and shared by multiple Diagnostics for multiple translation units.
class DiagnosticIDs : public RefCountedBase<DiagnosticIDs> {
public:
  /// The level of the diagnostic, after it has been through mapping.
  enum Level {
    Ignored, Note, Remark, Warning, Error, Fatal
  };

private:
  /// Information for uniquing and looking up custom diags.
  diag::CustomDiagInfo *CustomDiagInfo;

public:
  DiagnosticIDs();
  ~DiagnosticIDs();

  /// Return an ID for a diagnostic with the specified format string and
  /// level.
  ///
  /// If this is the first request for this diagnostic, it is registered and
  /// created, otherwise the existing ID is returned.

  // FIXME: Replace this function with a create-only facilty like
  // createCustomDiagIDFromFormatString() to enforce safe usage. At the time of
  // writing, nearly all callers of this function were invalid.
  unsigned getCustomDiagID(Level L, StringRef FormatString);

  //===--------------------------------------------------------------------===//
  // Diagnostic classification and reporting interfaces.
  //

  /// Given a diagnostic ID, return a description of the issue.
  StringRef getDescription(unsigned DiagID) const;

  /// Return true if the unmapped diagnostic levelof the specified
  /// diagnostic ID is a Warning or Extension.
  ///
  /// This only works on builtin diagnostics, not custom ones, and is not
  /// legal to call on NOTEs.
  static bool isBuiltinWarningOrExtension(unsigned DiagID);

  /// Return true if the specified diagnostic is mapped to errors by
  /// default.
  static bool isDefaultMappingAsError(unsigned DiagID);

  /// Determine whether the given built-in diagnostic ID is a Note.
  static bool isBuiltinNote(unsigned DiagID);

  /// Determine whether the given built-in diagnostic ID is for an
  /// extension of some sort.
  static bool isBuiltinExtensionDiag(unsigned DiagID) {
    bool ignored;
    return isBuiltinExtensionDiag(DiagID, ignored);
  }

  /// Determine whether the given built-in diagnostic ID is for an
  /// extension of some sort, and whether it is enabled by default.
  ///
  /// This also returns EnabledByDefault, which is set to indicate whether the
  /// diagnostic is ignored by default (in which case -pedantic enables it) or
  /// treated as a warning/error by default.
  ///
  static bool isBuiltinExtensionDiag(unsigned DiagID, bool &EnabledByDefault);


  /// Return the lowest-level warning option that enables the specified
  /// diagnostic.
  ///
  /// If there is no -Wfoo flag that controls the diagnostic, this returns null.
  static StringRef getWarningOptionForDiag(unsigned DiagID);

  /// Return the category number that a specified \p DiagID belongs to,
  /// or 0 if no category.
  static unsigned getCategoryNumberForDiag(unsigned DiagID);

  /// Return the number of diagnostic categories.
  static unsigned getNumberOfCategories();

  /// Given a category ID, return the name of the category.
  static StringRef getCategoryNameFromID(unsigned CategoryID);

  /// Return true if a given diagnostic falls into an ARC diagnostic
  /// category.
  static bool isARCDiagnostic(unsigned DiagID);

  /// Enumeration describing how the emission of a diagnostic should
  /// be treated when it occurs during C++ template argument deduction.
  enum SFINAEResponse {
    /// The diagnostic should not be reported, but it should cause
    /// template argument deduction to fail.
    ///
    /// The vast majority of errors that occur during template argument
    /// deduction fall into this category.
    SFINAE_SubstitutionFailure,

    /// The diagnostic should be suppressed entirely.
    ///
    /// Warnings generally fall into this category.
    SFINAE_Suppress,

    /// The diagnostic should be reported.
    ///
    /// The diagnostic should be reported. Various fatal errors (e.g.,
    /// template instantiation depth exceeded) fall into this category.
    SFINAE_Report,

    /// The diagnostic is an access-control diagnostic, which will be
    /// substitution failures in some contexts and reported in others.
    SFINAE_AccessControl
  };

  /// Determines whether the given built-in diagnostic ID is
  /// for an error that is suppressed if it occurs during C++ template
  /// argument deduction.
  ///
  /// When an error is suppressed due to SFINAE, the template argument
  /// deduction fails but no diagnostic is emitted. Certain classes of
  /// errors, such as those errors that involve C++ access control,
  /// are not SFINAE errors.
  static SFINAEResponse getDiagnosticSFINAEResponse(unsigned DiagID);

  /// Get the string of all diagnostic flags.
  ///
  /// \returns A list of all diagnostics flags as they would be written in a
  /// command line invocation including their `no-` variants. For example:
  /// `{"-Wempty-body", "-Wno-empty-body", ...}`
  static std::vector<std::string> getDiagnosticFlags();

  /// Get the set of all diagnostic IDs in the group with the given name.
  ///
  /// \param[out] Diags - On return, the diagnostics in the group.
  /// \returns \c true if the given group is unknown, \c false otherwise.
  bool getDiagnosticsInGroup(diag::Flavor Flavor, StringRef Group,
                             SmallVectorImpl<diag::kind> &Diags) const;

  /// Get the set of all diagnostic IDs.
  static void getAllDiagnostics(diag::Flavor Flavor,
                                std::vector<diag::kind> &Diags);

  /// Get the diagnostic option with the closest edit distance to the
  /// given group name.
  static StringRef getNearestOption(diag::Flavor Flavor, StringRef Group);

private:
  /// Classify the specified diagnostic ID into a Level, consumable by
  /// the DiagnosticClient.
  ///
  /// The classification is based on the way the client configured the
  /// DiagnosticsEngine object.
  ///
  /// \param Loc The source location for which we are interested in finding out
  /// the diagnostic state. Can be null in order to query the latest state.
  DiagnosticIDs::Level
  getDiagnosticLevel(unsigned DiagID, SourceLocation Loc,
                     const DiagnosticsEngine &Diag) const LLVM_READONLY;

  diag::Severity
  getDiagnosticSeverity(unsigned DiagID, SourceLocation Loc,
                        const DiagnosticsEngine &Diag) const LLVM_READONLY;

  /// Used to report a diagnostic that is finally fully formed.
  ///
  /// \returns \c true if the diagnostic was emitted, \c false if it was
  /// suppressed.
  bool ProcessDiag(DiagnosticsEngine &Diag) const;

  /// Used to emit a diagnostic that is finally fully formed,
  /// ignoring suppression.
  void EmitDiag(DiagnosticsEngine &Diag, Level DiagLevel) const;

  /// Whether the diagnostic may leave the AST in a state where some
  /// invariants can break.
  bool isUnrecoverable(unsigned DiagID) const;

  friend class DiagnosticsEngine;
};

}  // end namespace clang

#endif
