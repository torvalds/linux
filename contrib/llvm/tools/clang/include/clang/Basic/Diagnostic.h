//===- Diagnostic.h - C Language Family Diagnostic Handling -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the Diagnostic-related interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_DIAGNOSTIC_H
#define LLVM_CLANG_BASIC_DIAGNOSTIC_H

#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace clang {

class DeclContext;
class DiagnosticBuilder;
class DiagnosticConsumer;
class IdentifierInfo;
class LangOptions;
class Preprocessor;
class SourceManager;
class StoredDiagnostic;

namespace tok {

enum TokenKind : unsigned short;

} // namespace tok

/// Annotates a diagnostic with some code that should be
/// inserted, removed, or replaced to fix the problem.
///
/// This kind of hint should be used when we are certain that the
/// introduction, removal, or modification of a particular (small!)
/// amount of code will correct a compilation error. The compiler
/// should also provide full recovery from such errors, such that
/// suppressing the diagnostic output can still result in successful
/// compilation.
class FixItHint {
public:
  /// Code that should be replaced to correct the error. Empty for an
  /// insertion hint.
  CharSourceRange RemoveRange;

  /// Code in the specific range that should be inserted in the insertion
  /// location.
  CharSourceRange InsertFromRange;

  /// The actual code to insert at the insertion location, as a
  /// string.
  std::string CodeToInsert;

  bool BeforePreviousInsertions = false;

  /// Empty code modification hint, indicating that no code
  /// modification is known.
  FixItHint() = default;

  bool isNull() const {
    return !RemoveRange.isValid();
  }

  /// Create a code modification hint that inserts the given
  /// code string at a specific location.
  static FixItHint CreateInsertion(SourceLocation InsertionLoc,
                                   StringRef Code,
                                   bool BeforePreviousInsertions = false) {
    FixItHint Hint;
    Hint.RemoveRange =
      CharSourceRange::getCharRange(InsertionLoc, InsertionLoc);
    Hint.CodeToInsert = Code;
    Hint.BeforePreviousInsertions = BeforePreviousInsertions;
    return Hint;
  }

  /// Create a code modification hint that inserts the given
  /// code from \p FromRange at a specific location.
  static FixItHint CreateInsertionFromRange(SourceLocation InsertionLoc,
                                            CharSourceRange FromRange,
                                        bool BeforePreviousInsertions = false) {
    FixItHint Hint;
    Hint.RemoveRange =
      CharSourceRange::getCharRange(InsertionLoc, InsertionLoc);
    Hint.InsertFromRange = FromRange;
    Hint.BeforePreviousInsertions = BeforePreviousInsertions;
    return Hint;
  }

  /// Create a code modification hint that removes the given
  /// source range.
  static FixItHint CreateRemoval(CharSourceRange RemoveRange) {
    FixItHint Hint;
    Hint.RemoveRange = RemoveRange;
    return Hint;
  }
  static FixItHint CreateRemoval(SourceRange RemoveRange) {
    return CreateRemoval(CharSourceRange::getTokenRange(RemoveRange));
  }

  /// Create a code modification hint that replaces the given
  /// source range with the given code string.
  static FixItHint CreateReplacement(CharSourceRange RemoveRange,
                                     StringRef Code) {
    FixItHint Hint;
    Hint.RemoveRange = RemoveRange;
    Hint.CodeToInsert = Code;
    return Hint;
  }

  static FixItHint CreateReplacement(SourceRange RemoveRange,
                                     StringRef Code) {
    return CreateReplacement(CharSourceRange::getTokenRange(RemoveRange), Code);
  }
};

/// Concrete class used by the front-end to report problems and issues.
///
/// This massages the diagnostics (e.g. handling things like "report warnings
/// as errors" and passes them off to the DiagnosticConsumer for reporting to
/// the user. DiagnosticsEngine is tied to one translation unit and one
/// SourceManager.
class DiagnosticsEngine : public RefCountedBase<DiagnosticsEngine> {
public:
  /// The level of the diagnostic, after it has been through mapping.
  enum Level {
    Ignored = DiagnosticIDs::Ignored,
    Note = DiagnosticIDs::Note,
    Remark = DiagnosticIDs::Remark,
    Warning = DiagnosticIDs::Warning,
    Error = DiagnosticIDs::Error,
    Fatal = DiagnosticIDs::Fatal
  };

  enum ArgumentKind {
    /// std::string
    ak_std_string,

    /// const char *
    ak_c_string,

    /// int
    ak_sint,

    /// unsigned
    ak_uint,

    /// enum TokenKind : unsigned
    ak_tokenkind,

    /// IdentifierInfo
    ak_identifierinfo,

    /// Qualifiers
    ak_qual,

    /// QualType
    ak_qualtype,

    /// DeclarationName
    ak_declarationname,

    /// NamedDecl *
    ak_nameddecl,

    /// NestedNameSpecifier *
    ak_nestednamespec,

    /// DeclContext *
    ak_declcontext,

    /// pair<QualType, QualType>
    ak_qualtype_pair,

    /// Attr *
    ak_attr
  };

  /// Represents on argument value, which is a union discriminated
  /// by ArgumentKind, with a value.
  using ArgumentValue = std::pair<ArgumentKind, intptr_t>;

private:
  // Used by __extension__
  unsigned char AllExtensionsSilenced = 0;

  // Suppress diagnostics after a fatal error?
  bool SuppressAfterFatalError = true;

  // Suppress all diagnostics.
  bool SuppressAllDiagnostics = false;

  // Elide common types of templates.
  bool ElideType = true;

  // Print a tree when comparing templates.
  bool PrintTemplateTree = false;

  // Color printing is enabled.
  bool ShowColors = false;

  // Which overload candidates to show.
  OverloadsShown ShowOverloads = Ovl_All;

  // Cap of # errors emitted, 0 -> no limit.
  unsigned ErrorLimit = 0;

  // Cap on depth of template backtrace stack, 0 -> no limit.
  unsigned TemplateBacktraceLimit = 0;

  // Cap on depth of constexpr evaluation backtrace stack, 0 -> no limit.
  unsigned ConstexprBacktraceLimit = 0;

  IntrusiveRefCntPtr<DiagnosticIDs> Diags;
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts;
  DiagnosticConsumer *Client = nullptr;
  std::unique_ptr<DiagnosticConsumer> Owner;
  SourceManager *SourceMgr = nullptr;

  /// Mapping information for diagnostics.
  ///
  /// Mapping info is packed into four bits per diagnostic.  The low three
  /// bits are the mapping (an instance of diag::Severity), or zero if unset.
  /// The high bit is set when the mapping was established as a user mapping.
  /// If the high bit is clear, then the low bits are set to the default
  /// value, and should be mapped with -pedantic, -Werror, etc.
  ///
  /// A new DiagState is created and kept around when diagnostic pragmas modify
  /// the state so that we know what is the diagnostic state at any given
  /// source location.
  class DiagState {
    llvm::DenseMap<unsigned, DiagnosticMapping> DiagMap;

  public:
    // "Global" configuration state that can actually vary between modules.

    // Ignore all warnings: -w
    unsigned IgnoreAllWarnings : 1;

    // Enable all warnings.
    unsigned EnableAllWarnings : 1;

    // Treat warnings like errors.
    unsigned WarningsAsErrors : 1;

    // Treat errors like fatal errors.
    unsigned ErrorsAsFatal : 1;

    // Suppress warnings in system headers.
    unsigned SuppressSystemWarnings : 1;

    // Map extensions to warnings or errors?
    diag::Severity ExtBehavior = diag::Severity::Ignored;

    DiagState()
        : IgnoreAllWarnings(false), EnableAllWarnings(false),
          WarningsAsErrors(false), ErrorsAsFatal(false),
          SuppressSystemWarnings(false) {}

    using iterator = llvm::DenseMap<unsigned, DiagnosticMapping>::iterator;
    using const_iterator =
        llvm::DenseMap<unsigned, DiagnosticMapping>::const_iterator;

    void setMapping(diag::kind Diag, DiagnosticMapping Info) {
      DiagMap[Diag] = Info;
    }

    DiagnosticMapping lookupMapping(diag::kind Diag) const {
      return DiagMap.lookup(Diag);
    }

    DiagnosticMapping &getOrAddMapping(diag::kind Diag);

    const_iterator begin() const { return DiagMap.begin(); }
    const_iterator end() const { return DiagMap.end(); }
  };

  /// Keeps and automatically disposes all DiagStates that we create.
  std::list<DiagState> DiagStates;

  /// A mapping from files to the diagnostic states for those files. Lazily
  /// built on demand for files in which the diagnostic state has not changed.
  class DiagStateMap {
  public:
    /// Add an initial diagnostic state.
    void appendFirst(DiagState *State);

    /// Add a new latest state point.
    void append(SourceManager &SrcMgr, SourceLocation Loc, DiagState *State);

    /// Look up the diagnostic state at a given source location.
    DiagState *lookup(SourceManager &SrcMgr, SourceLocation Loc) const;

    /// Determine whether this map is empty.
    bool empty() const { return Files.empty(); }

    /// Clear out this map.
    void clear() {
      Files.clear();
      FirstDiagState = CurDiagState = nullptr;
      CurDiagStateLoc = SourceLocation();
    }

    /// Produce a debugging dump of the diagnostic state.
    LLVM_DUMP_METHOD void dump(SourceManager &SrcMgr,
                               StringRef DiagName = StringRef()) const;

    /// Grab the most-recently-added state point.
    DiagState *getCurDiagState() const { return CurDiagState; }

    /// Get the location at which a diagnostic state was last added.
    SourceLocation getCurDiagStateLoc() const { return CurDiagStateLoc; }

  private:
    friend class ASTReader;
    friend class ASTWriter;

    /// Represents a point in source where the diagnostic state was
    /// modified because of a pragma.
    ///
    /// 'Loc' can be null if the point represents the diagnostic state
    /// modifications done through the command-line.
    struct DiagStatePoint {
      DiagState *State;
      unsigned Offset;

      DiagStatePoint(DiagState *State, unsigned Offset)
          : State(State), Offset(Offset) {}
    };

    /// Description of the diagnostic states and state transitions for a
    /// particular FileID.
    struct File {
      /// The diagnostic state for the parent file. This is strictly redundant,
      /// as looking up the DecomposedIncludedLoc for the FileID in the Files
      /// map would give us this, but we cache it here for performance.
      File *Parent = nullptr;

      /// The offset of this file within its parent.
      unsigned ParentOffset = 0;

      /// Whether this file has any local (not imported from an AST file)
      /// diagnostic state transitions.
      bool HasLocalTransitions = false;

      /// The points within the file where the state changes. There will always
      /// be at least one of these (the state on entry to the file).
      llvm::SmallVector<DiagStatePoint, 4> StateTransitions;

      DiagState *lookup(unsigned Offset) const;
    };

    /// The diagnostic states for each file.
    mutable std::map<FileID, File> Files;

    /// The initial diagnostic state.
    DiagState *FirstDiagState;

    /// The current diagnostic state.
    DiagState *CurDiagState;

    /// The location at which the current diagnostic state was established.
    SourceLocation CurDiagStateLoc;

    /// Get the diagnostic state information for a file.
    File *getFile(SourceManager &SrcMgr, FileID ID) const;
  };

  DiagStateMap DiagStatesByLoc;

  /// Keeps the DiagState that was active during each diagnostic 'push'
  /// so we can get back at it when we 'pop'.
  std::vector<DiagState *> DiagStateOnPushStack;

  DiagState *GetCurDiagState() const {
    return DiagStatesByLoc.getCurDiagState();
  }

  void PushDiagStatePoint(DiagState *State, SourceLocation L);

  /// Finds the DiagStatePoint that contains the diagnostic state of
  /// the given source location.
  DiagState *GetDiagStateForLoc(SourceLocation Loc) const {
    return SourceMgr ? DiagStatesByLoc.lookup(*SourceMgr, Loc)
                     : DiagStatesByLoc.getCurDiagState();
  }

  /// Sticky flag set to \c true when an error is emitted.
  bool ErrorOccurred;

  /// Sticky flag set to \c true when an "uncompilable error" occurs.
  /// I.e. an error that was not upgraded from a warning by -Werror.
  bool UncompilableErrorOccurred;

  /// Sticky flag set to \c true when a fatal error is emitted.
  bool FatalErrorOccurred;

  /// Indicates that an unrecoverable error has occurred.
  bool UnrecoverableErrorOccurred;

  /// Counts for DiagnosticErrorTrap to check whether an error occurred
  /// during a parsing section, e.g. during parsing a function.
  unsigned TrapNumErrorsOccurred;
  unsigned TrapNumUnrecoverableErrorsOccurred;

  /// The level of the last diagnostic emitted.
  ///
  /// This is used to emit continuation diagnostics with the same level as the
  /// diagnostic that they follow.
  DiagnosticIDs::Level LastDiagLevel;

  /// Number of warnings reported
  unsigned NumWarnings;

  /// Number of errors reported
  unsigned NumErrors;

  /// A function pointer that converts an opaque diagnostic
  /// argument to a strings.
  ///
  /// This takes the modifiers and argument that was present in the diagnostic.
  ///
  /// The PrevArgs array indicates the previous arguments formatted for this
  /// diagnostic.  Implementations of this function can use this information to
  /// avoid redundancy across arguments.
  ///
  /// This is a hack to avoid a layering violation between libbasic and libsema.
  using ArgToStringFnTy = void (*)(
      ArgumentKind Kind, intptr_t Val,
      StringRef Modifier, StringRef Argument,
      ArrayRef<ArgumentValue> PrevArgs,
      SmallVectorImpl<char> &Output,
      void *Cookie,
      ArrayRef<intptr_t> QualTypeVals);

  void *ArgToStringCookie = nullptr;
  ArgToStringFnTy ArgToStringFn;

  /// ID of the "delayed" diagnostic, which is a (typically
  /// fatal) diagnostic that had to be delayed because it was found
  /// while emitting another diagnostic.
  unsigned DelayedDiagID;

  /// First string argument for the delayed diagnostic.
  std::string DelayedDiagArg1;

  /// Second string argument for the delayed diagnostic.
  std::string DelayedDiagArg2;

  /// Optional flag value.
  ///
  /// Some flags accept values, for instance: -Wframe-larger-than=<value> and
  /// -Rpass=<value>. The content of this string is emitted after the flag name
  /// and '='.
  std::string FlagValue;

public:
  explicit DiagnosticsEngine(IntrusiveRefCntPtr<DiagnosticIDs> Diags,
                             IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts,
                             DiagnosticConsumer *client = nullptr,
                             bool ShouldOwnClient = true);
  DiagnosticsEngine(const DiagnosticsEngine &) = delete;
  DiagnosticsEngine &operator=(const DiagnosticsEngine &) = delete;
  ~DiagnosticsEngine();

  LLVM_DUMP_METHOD void dump() const;
  LLVM_DUMP_METHOD void dump(StringRef DiagName) const;

  const IntrusiveRefCntPtr<DiagnosticIDs> &getDiagnosticIDs() const {
    return Diags;
  }

  /// Retrieve the diagnostic options.
  DiagnosticOptions &getDiagnosticOptions() const { return *DiagOpts; }

  using diag_mapping_range = llvm::iterator_range<DiagState::const_iterator>;

  /// Get the current set of diagnostic mappings.
  diag_mapping_range getDiagnosticMappings() const {
    const DiagState &DS = *GetCurDiagState();
    return diag_mapping_range(DS.begin(), DS.end());
  }

  DiagnosticConsumer *getClient() { return Client; }
  const DiagnosticConsumer *getClient() const { return Client; }

  /// Determine whether this \c DiagnosticsEngine object own its client.
  bool ownsClient() const { return Owner != nullptr; }

  /// Return the current diagnostic client along with ownership of that
  /// client.
  std::unique_ptr<DiagnosticConsumer> takeClient() { return std::move(Owner); }

  bool hasSourceManager() const { return SourceMgr != nullptr; }

  SourceManager &getSourceManager() const {
    assert(SourceMgr && "SourceManager not set!");
    return *SourceMgr;
  }

  void setSourceManager(SourceManager *SrcMgr) {
    assert(DiagStatesByLoc.empty() &&
           "Leftover diag state from a different SourceManager.");
    SourceMgr = SrcMgr;
  }

  //===--------------------------------------------------------------------===//
  //  DiagnosticsEngine characterization methods, used by a client to customize
  //  how diagnostics are emitted.
  //

  /// Copies the current DiagMappings and pushes the new copy
  /// onto the top of the stack.
  void pushMappings(SourceLocation Loc);

  /// Pops the current DiagMappings off the top of the stack,
  /// causing the new top of the stack to be the active mappings.
  ///
  /// \returns \c true if the pop happens, \c false if there is only one
  /// DiagMapping on the stack.
  bool popMappings(SourceLocation Loc);

  /// Set the diagnostic client associated with this diagnostic object.
  ///
  /// \param ShouldOwnClient true if the diagnostic object should take
  /// ownership of \c client.
  void setClient(DiagnosticConsumer *client, bool ShouldOwnClient = true);

  /// Specify a limit for the number of errors we should
  /// emit before giving up.
  ///
  /// Zero disables the limit.
  void setErrorLimit(unsigned Limit) { ErrorLimit = Limit; }

  /// Specify the maximum number of template instantiation
  /// notes to emit along with a given diagnostic.
  void setTemplateBacktraceLimit(unsigned Limit) {
    TemplateBacktraceLimit = Limit;
  }

  /// Retrieve the maximum number of template instantiation
  /// notes to emit along with a given diagnostic.
  unsigned getTemplateBacktraceLimit() const {
    return TemplateBacktraceLimit;
  }

  /// Specify the maximum number of constexpr evaluation
  /// notes to emit along with a given diagnostic.
  void setConstexprBacktraceLimit(unsigned Limit) {
    ConstexprBacktraceLimit = Limit;
  }

  /// Retrieve the maximum number of constexpr evaluation
  /// notes to emit along with a given diagnostic.
  unsigned getConstexprBacktraceLimit() const {
    return ConstexprBacktraceLimit;
  }

  /// When set to true, any unmapped warnings are ignored.
  ///
  /// If this and WarningsAsErrors are both set, then this one wins.
  void setIgnoreAllWarnings(bool Val) {
    GetCurDiagState()->IgnoreAllWarnings = Val;
  }
  bool getIgnoreAllWarnings() const {
    return GetCurDiagState()->IgnoreAllWarnings;
  }

  /// When set to true, any unmapped ignored warnings are no longer
  /// ignored.
  ///
  /// If this and IgnoreAllWarnings are both set, then that one wins.
  void setEnableAllWarnings(bool Val) {
    GetCurDiagState()->EnableAllWarnings = Val;
  }
  bool getEnableAllWarnings() const {
    return GetCurDiagState()->EnableAllWarnings;
  }

  /// When set to true, any warnings reported are issued as errors.
  void setWarningsAsErrors(bool Val) {
    GetCurDiagState()->WarningsAsErrors = Val;
  }
  bool getWarningsAsErrors() const {
    return GetCurDiagState()->WarningsAsErrors;
  }

  /// When set to true, any error reported is made a fatal error.
  void setErrorsAsFatal(bool Val) { GetCurDiagState()->ErrorsAsFatal = Val; }
  bool getErrorsAsFatal() const { return GetCurDiagState()->ErrorsAsFatal; }

  /// When set to true (the default), suppress further diagnostics after
  /// a fatal error.
  void setSuppressAfterFatalError(bool Val) { SuppressAfterFatalError = Val; }

  /// When set to true mask warnings that come from system headers.
  void setSuppressSystemWarnings(bool Val) {
    GetCurDiagState()->SuppressSystemWarnings = Val;
  }
  bool getSuppressSystemWarnings() const {
    return GetCurDiagState()->SuppressSystemWarnings;
  }

  /// Suppress all diagnostics, to silence the front end when we
  /// know that we don't want any more diagnostics to be passed along to the
  /// client
  void setSuppressAllDiagnostics(bool Val = true) {
    SuppressAllDiagnostics = Val;
  }
  bool getSuppressAllDiagnostics() const { return SuppressAllDiagnostics; }

  /// Set type eliding, to skip outputting same types occurring in
  /// template types.
  void setElideType(bool Val = true) { ElideType = Val; }
  bool getElideType() { return ElideType; }

  /// Set tree printing, to outputting the template difference in a
  /// tree format.
  void setPrintTemplateTree(bool Val = false) { PrintTemplateTree = Val; }
  bool getPrintTemplateTree() { return PrintTemplateTree; }

  /// Set color printing, so the type diffing will inject color markers
  /// into the output.
  void setShowColors(bool Val = false) { ShowColors = Val; }
  bool getShowColors() { return ShowColors; }

  /// Specify which overload candidates to show when overload resolution
  /// fails.
  ///
  /// By default, we show all candidates.
  void setShowOverloads(OverloadsShown Val) {
    ShowOverloads = Val;
  }
  OverloadsShown getShowOverloads() const { return ShowOverloads; }

  /// Pretend that the last diagnostic issued was ignored, so any
  /// subsequent notes will be suppressed, or restore a prior ignoring
  /// state after ignoring some diagnostics and their notes, possibly in
  /// the middle of another diagnostic.
  ///
  /// This can be used by clients who suppress diagnostics themselves.
  void setLastDiagnosticIgnored(bool Ignored = true) {
    if (LastDiagLevel == DiagnosticIDs::Fatal)
      FatalErrorOccurred = true;
    LastDiagLevel = Ignored ? DiagnosticIDs::Ignored : DiagnosticIDs::Warning;
  }

  /// Determine whether the previous diagnostic was ignored. This can
  /// be used by clients that want to determine whether notes attached to a
  /// diagnostic will be suppressed.
  bool isLastDiagnosticIgnored() const {
    return LastDiagLevel == DiagnosticIDs::Ignored;
  }

  /// Controls whether otherwise-unmapped extension diagnostics are
  /// mapped onto ignore/warning/error.
  ///
  /// This corresponds to the GCC -pedantic and -pedantic-errors option.
  void setExtensionHandlingBehavior(diag::Severity H) {
    GetCurDiagState()->ExtBehavior = H;
  }
  diag::Severity getExtensionHandlingBehavior() const {
    return GetCurDiagState()->ExtBehavior;
  }

  /// Counter bumped when an __extension__  block is/ encountered.
  ///
  /// When non-zero, all extension diagnostics are entirely silenced, no
  /// matter how they are mapped.
  void IncrementAllExtensionsSilenced() { ++AllExtensionsSilenced; }
  void DecrementAllExtensionsSilenced() { --AllExtensionsSilenced; }
  bool hasAllExtensionsSilenced() { return AllExtensionsSilenced != 0; }

  /// This allows the client to specify that certain warnings are
  /// ignored.
  ///
  /// Notes can never be mapped, errors can only be mapped to fatal, and
  /// WARNINGs and EXTENSIONs can be mapped arbitrarily.
  ///
  /// \param Loc The source location that this change of diagnostic state should
  /// take affect. It can be null if we are setting the latest state.
  void setSeverity(diag::kind Diag, diag::Severity Map, SourceLocation Loc);

  /// Change an entire diagnostic group (e.g. "unknown-pragmas") to
  /// have the specified mapping.
  ///
  /// \returns true (and ignores the request) if "Group" was unknown, false
  /// otherwise.
  ///
  /// \param Flavor The flavor of group to affect. -Rfoo does not affect the
  /// state of the -Wfoo group and vice versa.
  ///
  /// \param Loc The source location that this change of diagnostic state should
  /// take affect. It can be null if we are setting the state from command-line.
  bool setSeverityForGroup(diag::Flavor Flavor, StringRef Group,
                           diag::Severity Map,
                           SourceLocation Loc = SourceLocation());

  /// Set the warning-as-error flag for the given diagnostic group.
  ///
  /// This function always only operates on the current diagnostic state.
  ///
  /// \returns True if the given group is unknown, false otherwise.
  bool setDiagnosticGroupWarningAsError(StringRef Group, bool Enabled);

  /// Set the error-as-fatal flag for the given diagnostic group.
  ///
  /// This function always only operates on the current diagnostic state.
  ///
  /// \returns True if the given group is unknown, false otherwise.
  bool setDiagnosticGroupErrorAsFatal(StringRef Group, bool Enabled);

  /// Add the specified mapping to all diagnostics of the specified
  /// flavor.
  ///
  /// Mainly to be used by -Wno-everything to disable all warnings but allow
  /// subsequent -W options to enable specific warnings.
  void setSeverityForAll(diag::Flavor Flavor, diag::Severity Map,
                         SourceLocation Loc = SourceLocation());

  bool hasErrorOccurred() const { return ErrorOccurred; }

  /// Errors that actually prevent compilation, not those that are
  /// upgraded from a warning by -Werror.
  bool hasUncompilableErrorOccurred() const {
    return UncompilableErrorOccurred;
  }
  bool hasFatalErrorOccurred() const { return FatalErrorOccurred; }

  /// Determine whether any kind of unrecoverable error has occurred.
  bool hasUnrecoverableErrorOccurred() const {
    return FatalErrorOccurred || UnrecoverableErrorOccurred;
  }

  unsigned getNumWarnings() const { return NumWarnings; }

  void setNumWarnings(unsigned NumWarnings) {
    this->NumWarnings = NumWarnings;
  }

  /// Return an ID for a diagnostic with the specified format string and
  /// level.
  ///
  /// If this is the first request for this diagnostic, it is registered and
  /// created, otherwise the existing ID is returned.
  ///
  /// \param FormatString A fixed diagnostic format string that will be hashed
  /// and mapped to a unique DiagID.
  template <unsigned N>
  unsigned getCustomDiagID(Level L, const char (&FormatString)[N]) {
    return Diags->getCustomDiagID((DiagnosticIDs::Level)L,
                                  StringRef(FormatString, N - 1));
  }

  /// Converts a diagnostic argument (as an intptr_t) into the string
  /// that represents it.
  void ConvertArgToString(ArgumentKind Kind, intptr_t Val,
                          StringRef Modifier, StringRef Argument,
                          ArrayRef<ArgumentValue> PrevArgs,
                          SmallVectorImpl<char> &Output,
                          ArrayRef<intptr_t> QualTypeVals) const {
    ArgToStringFn(Kind, Val, Modifier, Argument, PrevArgs, Output,
                  ArgToStringCookie, QualTypeVals);
  }

  void SetArgToStringFn(ArgToStringFnTy Fn, void *Cookie) {
    ArgToStringFn = Fn;
    ArgToStringCookie = Cookie;
  }

  /// Note that the prior diagnostic was emitted by some other
  /// \c DiagnosticsEngine, and we may be attaching a note to that diagnostic.
  void notePriorDiagnosticFrom(const DiagnosticsEngine &Other) {
    LastDiagLevel = Other.LastDiagLevel;
  }

  /// Reset the state of the diagnostic object to its initial
  /// configuration.
  void Reset();

  //===--------------------------------------------------------------------===//
  // DiagnosticsEngine classification and reporting interfaces.
  //

  /// Determine whether the diagnostic is known to be ignored.
  ///
  /// This can be used to opportunistically avoid expensive checks when it's
  /// known for certain that the diagnostic has been suppressed at the
  /// specified location \p Loc.
  ///
  /// \param Loc The source location we are interested in finding out the
  /// diagnostic state. Can be null in order to query the latest state.
  bool isIgnored(unsigned DiagID, SourceLocation Loc) const {
    return Diags->getDiagnosticSeverity(DiagID, Loc, *this) ==
           diag::Severity::Ignored;
  }

  /// Based on the way the client configured the DiagnosticsEngine
  /// object, classify the specified diagnostic ID into a Level, consumable by
  /// the DiagnosticConsumer.
  ///
  /// To preserve invariant assumptions, this function should not be used to
  /// influence parse or semantic analysis actions. Instead consider using
  /// \c isIgnored().
  ///
  /// \param Loc The source location we are interested in finding out the
  /// diagnostic state. Can be null in order to query the latest state.
  Level getDiagnosticLevel(unsigned DiagID, SourceLocation Loc) const {
    return (Level)Diags->getDiagnosticLevel(DiagID, Loc, *this);
  }

  /// Issue the message to the client.
  ///
  /// This actually returns an instance of DiagnosticBuilder which emits the
  /// diagnostics (through @c ProcessDiag) when it is destroyed.
  ///
  /// \param DiagID A member of the @c diag::kind enum.
  /// \param Loc Represents the source location associated with the diagnostic,
  /// which can be an invalid location if no position information is available.
  inline DiagnosticBuilder Report(SourceLocation Loc, unsigned DiagID);
  inline DiagnosticBuilder Report(unsigned DiagID);

  void Report(const StoredDiagnostic &storedDiag);

  /// Determine whethere there is already a diagnostic in flight.
  bool isDiagnosticInFlight() const {
    return CurDiagID != std::numeric_limits<unsigned>::max();
  }

  /// Set the "delayed" diagnostic that will be emitted once
  /// the current diagnostic completes.
  ///
  ///  If a diagnostic is already in-flight but the front end must
  ///  report a problem (e.g., with an inconsistent file system
  ///  state), this routine sets a "delayed" diagnostic that will be
  ///  emitted after the current diagnostic completes. This should
  ///  only be used for fatal errors detected at inconvenient
  ///  times. If emitting a delayed diagnostic causes a second delayed
  ///  diagnostic to be introduced, that second delayed diagnostic
  ///  will be ignored.
  ///
  /// \param DiagID The ID of the diagnostic being delayed.
  ///
  /// \param Arg1 A string argument that will be provided to the
  /// diagnostic. A copy of this string will be stored in the
  /// DiagnosticsEngine object itself.
  ///
  /// \param Arg2 A string argument that will be provided to the
  /// diagnostic. A copy of this string will be stored in the
  /// DiagnosticsEngine object itself.
  void SetDelayedDiagnostic(unsigned DiagID, StringRef Arg1 = "",
                            StringRef Arg2 = "");

  /// Clear out the current diagnostic.
  void Clear() { CurDiagID = std::numeric_limits<unsigned>::max(); }

  /// Return the value associated with this diagnostic flag.
  StringRef getFlagValue() const { return FlagValue; }

private:
  // This is private state used by DiagnosticBuilder.  We put it here instead of
  // in DiagnosticBuilder in order to keep DiagnosticBuilder a small lightweight
  // object.  This implementation choice means that we can only have one
  // diagnostic "in flight" at a time, but this seems to be a reasonable
  // tradeoff to keep these objects small.  Assertions verify that only one
  // diagnostic is in flight at a time.
  friend class Diagnostic;
  friend class DiagnosticBuilder;
  friend class DiagnosticErrorTrap;
  friend class DiagnosticIDs;
  friend class PartialDiagnostic;

  /// Report the delayed diagnostic.
  void ReportDelayed();

  /// The location of the current diagnostic that is in flight.
  SourceLocation CurDiagLoc;

  /// The ID of the current diagnostic that is in flight.
  ///
  /// This is set to std::numeric_limits<unsigned>::max() when there is no
  /// diagnostic in flight.
  unsigned CurDiagID;

  enum {
    /// The maximum number of arguments we can hold.
    ///
    /// We currently only support up to 10 arguments (%0-%9).  A single
    /// diagnostic with more than that almost certainly has to be simplified
    /// anyway.
    MaxArguments = 10,
  };

  /// The number of entries in Arguments.
  signed char NumDiagArgs;

  /// Specifies whether an argument is in DiagArgumentsStr or
  /// in DiagArguments.
  ///
  /// This is an array of ArgumentKind::ArgumentKind enum values, one for each
  /// argument.
  unsigned char DiagArgumentsKind[MaxArguments];

  /// Holds the values of each string argument for the current
  /// diagnostic.
  ///
  /// This is only used when the corresponding ArgumentKind is ak_std_string.
  std::string DiagArgumentsStr[MaxArguments];

  /// The values for the various substitution positions.
  ///
  /// This is used when the argument is not an std::string.  The specific
  /// value is mangled into an intptr_t and the interpretation depends on
  /// exactly what sort of argument kind it is.
  intptr_t DiagArgumentsVal[MaxArguments];

  /// The list of ranges added to this diagnostic.
  SmallVector<CharSourceRange, 8> DiagRanges;

  /// If valid, provides a hint with some code to insert, remove,
  /// or modify at a particular position.
  SmallVector<FixItHint, 8> DiagFixItHints;

  DiagnosticMapping makeUserMapping(diag::Severity Map, SourceLocation L) {
    bool isPragma = L.isValid();
    DiagnosticMapping Mapping =
        DiagnosticMapping::Make(Map, /*IsUser=*/true, isPragma);

    // If this is a pragma mapping, then set the diagnostic mapping flags so
    // that we override command line options.
    if (isPragma) {
      Mapping.setNoWarningAsError(true);
      Mapping.setNoErrorAsFatal(true);
    }

    return Mapping;
  }

  /// Used to report a diagnostic that is finally fully formed.
  ///
  /// \returns true if the diagnostic was emitted, false if it was suppressed.
  bool ProcessDiag() {
    return Diags->ProcessDiag(*this);
  }

  /// @name Diagnostic Emission
  /// @{
protected:
  friend class ASTReader;
  friend class ASTWriter;

  // Sema requires access to the following functions because the current design
  // of SFINAE requires it to use its own SemaDiagnosticBuilder, which needs to
  // access us directly to ensure we minimize the emitted code for the common
  // Sema::Diag() patterns.
  friend class Sema;

  /// Emit the current diagnostic and clear the diagnostic state.
  ///
  /// \param Force Emit the diagnostic regardless of suppression settings.
  bool EmitCurrentDiagnostic(bool Force = false);

  unsigned getCurrentDiagID() const { return CurDiagID; }

  SourceLocation getCurrentDiagLoc() const { return CurDiagLoc; }

  /// @}
};

/// RAII class that determines when any errors have occurred
/// between the time the instance was created and the time it was
/// queried.
class DiagnosticErrorTrap {
  DiagnosticsEngine &Diag;
  unsigned NumErrors;
  unsigned NumUnrecoverableErrors;

public:
  explicit DiagnosticErrorTrap(DiagnosticsEngine &Diag)
      : Diag(Diag) { reset(); }

  /// Determine whether any errors have occurred since this
  /// object instance was created.
  bool hasErrorOccurred() const {
    return Diag.TrapNumErrorsOccurred > NumErrors;
  }

  /// Determine whether any unrecoverable errors have occurred since this
  /// object instance was created.
  bool hasUnrecoverableErrorOccurred() const {
    return Diag.TrapNumUnrecoverableErrorsOccurred > NumUnrecoverableErrors;
  }

  /// Set to initial state of "no errors occurred".
  void reset() {
    NumErrors = Diag.TrapNumErrorsOccurred;
    NumUnrecoverableErrors = Diag.TrapNumUnrecoverableErrorsOccurred;
  }
};

//===----------------------------------------------------------------------===//
// DiagnosticBuilder
//===----------------------------------------------------------------------===//

/// A little helper class used to produce diagnostics.
///
/// This is constructed by the DiagnosticsEngine::Report method, and
/// allows insertion of extra information (arguments and source ranges) into
/// the currently "in flight" diagnostic.  When the temporary for the builder
/// is destroyed, the diagnostic is issued.
///
/// Note that many of these will be created as temporary objects (many call
/// sites), so we want them to be small and we never want their address taken.
/// This ensures that compilers with somewhat reasonable optimizers will promote
/// the common fields to registers, eliminating increments of the NumArgs field,
/// for example.
class DiagnosticBuilder {
  friend class DiagnosticsEngine;
  friend class PartialDiagnostic;

  mutable DiagnosticsEngine *DiagObj = nullptr;
  mutable unsigned NumArgs = 0;

  /// Status variable indicating if this diagnostic is still active.
  ///
  // NOTE: This field is redundant with DiagObj (IsActive iff (DiagObj == 0)),
  // but LLVM is not currently smart enough to eliminate the null check that
  // Emit() would end up with if we used that as our status variable.
  mutable bool IsActive = false;

  /// Flag indicating that this diagnostic is being emitted via a
  /// call to ForceEmit.
  mutable bool IsForceEmit = false;

  DiagnosticBuilder() = default;

  explicit DiagnosticBuilder(DiagnosticsEngine *diagObj)
      : DiagObj(diagObj), IsActive(true) {
    assert(diagObj && "DiagnosticBuilder requires a valid DiagnosticsEngine!");
    diagObj->DiagRanges.clear();
    diagObj->DiagFixItHints.clear();
  }

protected:
  void FlushCounts() {
    DiagObj->NumDiagArgs = NumArgs;
  }

  /// Clear out the current diagnostic.
  void Clear() const {
    DiagObj = nullptr;
    IsActive = false;
    IsForceEmit = false;
  }

  /// Determine whether this diagnostic is still active.
  bool isActive() const { return IsActive; }

  /// Force the diagnostic builder to emit the diagnostic now.
  ///
  /// Once this function has been called, the DiagnosticBuilder object
  /// should not be used again before it is destroyed.
  ///
  /// \returns true if a diagnostic was emitted, false if the
  /// diagnostic was suppressed.
  bool Emit() {
    // If this diagnostic is inactive, then its soul was stolen by the copy ctor
    // (or by a subclass, as in SemaDiagnosticBuilder).
    if (!isActive()) return false;

    // When emitting diagnostics, we set the final argument count into
    // the DiagnosticsEngine object.
    FlushCounts();

    // Process the diagnostic.
    bool Result = DiagObj->EmitCurrentDiagnostic(IsForceEmit);

    // This diagnostic is dead.
    Clear();

    return Result;
  }

public:
  /// Copy constructor.  When copied, this "takes" the diagnostic info from the
  /// input and neuters it.
  DiagnosticBuilder(const DiagnosticBuilder &D) {
    DiagObj = D.DiagObj;
    IsActive = D.IsActive;
    IsForceEmit = D.IsForceEmit;
    D.Clear();
    NumArgs = D.NumArgs;
  }

  DiagnosticBuilder &operator=(const DiagnosticBuilder &) = delete;

  /// Emits the diagnostic.
  ~DiagnosticBuilder() {
    Emit();
  }

  /// Retrieve an empty diagnostic builder.
  static DiagnosticBuilder getEmpty() {
    return {};
  }

  /// Forces the diagnostic to be emitted.
  const DiagnosticBuilder &setForceEmit() const {
    IsForceEmit = true;
    return *this;
  }

  /// Conversion of DiagnosticBuilder to bool always returns \c true.
  ///
  /// This allows is to be used in boolean error contexts (where \c true is
  /// used to indicate that an error has occurred), like:
  /// \code
  /// return Diag(...);
  /// \endcode
  operator bool() const { return true; }

  void AddString(StringRef S) const {
    assert(isActive() && "Clients must not add to cleared diagnostic!");
    assert(NumArgs < DiagnosticsEngine::MaxArguments &&
           "Too many arguments to diagnostic!");
    DiagObj->DiagArgumentsKind[NumArgs] = DiagnosticsEngine::ak_std_string;
    DiagObj->DiagArgumentsStr[NumArgs++] = S;
  }

  void AddTaggedVal(intptr_t V, DiagnosticsEngine::ArgumentKind Kind) const {
    assert(isActive() && "Clients must not add to cleared diagnostic!");
    assert(NumArgs < DiagnosticsEngine::MaxArguments &&
           "Too many arguments to diagnostic!");
    DiagObj->DiagArgumentsKind[NumArgs] = Kind;
    DiagObj->DiagArgumentsVal[NumArgs++] = V;
  }

  void AddSourceRange(const CharSourceRange &R) const {
    assert(isActive() && "Clients must not add to cleared diagnostic!");
    DiagObj->DiagRanges.push_back(R);
  }

  void AddFixItHint(const FixItHint &Hint) const {
    assert(isActive() && "Clients must not add to cleared diagnostic!");
    if (!Hint.isNull())
      DiagObj->DiagFixItHints.push_back(Hint);
  }

  void addFlagValue(StringRef V) const { DiagObj->FlagValue = V; }
};

struct AddFlagValue {
  StringRef Val;

  explicit AddFlagValue(StringRef V) : Val(V) {}
};

/// Register a value for the flag in the current diagnostic. This
/// value will be shown as the suffix "=value" after the flag name. It is
/// useful in cases where the diagnostic flag accepts values (e.g.,
/// -Rpass or -Wframe-larger-than).
inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           const AddFlagValue V) {
  DB.addFlagValue(V.Val);
  return DB;
}

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           StringRef S) {
  DB.AddString(S);
  return DB;
}

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           const char *Str) {
  DB.AddTaggedVal(reinterpret_cast<intptr_t>(Str),
                  DiagnosticsEngine::ak_c_string);
  return DB;
}

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB, int I) {
  DB.AddTaggedVal(I, DiagnosticsEngine::ak_sint);
  return DB;
}

// We use enable_if here to prevent that this overload is selected for
// pointers or other arguments that are implicitly convertible to bool.
template <typename T>
inline
typename std::enable_if<std::is_same<T, bool>::value,
                        const DiagnosticBuilder &>::type
operator<<(const DiagnosticBuilder &DB, T I) {
  DB.AddTaggedVal(I, DiagnosticsEngine::ak_sint);
  return DB;
}

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           unsigned I) {
  DB.AddTaggedVal(I, DiagnosticsEngine::ak_uint);
  return DB;
}

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           tok::TokenKind I) {
  DB.AddTaggedVal(static_cast<unsigned>(I), DiagnosticsEngine::ak_tokenkind);
  return DB;
}

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           const IdentifierInfo *II) {
  DB.AddTaggedVal(reinterpret_cast<intptr_t>(II),
                  DiagnosticsEngine::ak_identifierinfo);
  return DB;
}

// Adds a DeclContext to the diagnostic. The enable_if template magic is here
// so that we only match those arguments that are (statically) DeclContexts;
// other arguments that derive from DeclContext (e.g., RecordDecls) will not
// match.
template <typename T>
inline typename std::enable_if<
    std::is_same<typename std::remove_const<T>::type, DeclContext>::value,
    const DiagnosticBuilder &>::type
operator<<(const DiagnosticBuilder &DB, T *DC) {
  DB.AddTaggedVal(reinterpret_cast<intptr_t>(DC),
                  DiagnosticsEngine::ak_declcontext);
  return DB;
}

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           SourceRange R) {
  DB.AddSourceRange(CharSourceRange::getTokenRange(R));
  return DB;
}

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           ArrayRef<SourceRange> Ranges) {
  for (SourceRange R : Ranges)
    DB.AddSourceRange(CharSourceRange::getTokenRange(R));
  return DB;
}

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           const CharSourceRange &R) {
  DB.AddSourceRange(R);
  return DB;
}

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           const FixItHint &Hint) {
  DB.AddFixItHint(Hint);
  return DB;
}

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           ArrayRef<FixItHint> Hints) {
  for (const FixItHint &Hint : Hints)
    DB.AddFixItHint(Hint);
  return DB;
}

/// A nullability kind paired with a bit indicating whether it used a
/// context-sensitive keyword.
using DiagNullabilityKind = std::pair<NullabilityKind, bool>;

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                    DiagNullabilityKind nullability);

inline DiagnosticBuilder DiagnosticsEngine::Report(SourceLocation Loc,
                                                   unsigned DiagID) {
  assert(CurDiagID == std::numeric_limits<unsigned>::max() &&
         "Multiple diagnostics in flight at once!");
  CurDiagLoc = Loc;
  CurDiagID = DiagID;
  FlagValue.clear();
  return DiagnosticBuilder(this);
}

inline DiagnosticBuilder DiagnosticsEngine::Report(unsigned DiagID) {
  return Report(SourceLocation(), DiagID);
}

//===----------------------------------------------------------------------===//
// Diagnostic
//===----------------------------------------------------------------------===//

/// A little helper class (which is basically a smart pointer that forwards
/// info from DiagnosticsEngine) that allows clients to enquire about the
/// currently in-flight diagnostic.
class Diagnostic {
  const DiagnosticsEngine *DiagObj;
  StringRef StoredDiagMessage;

public:
  explicit Diagnostic(const DiagnosticsEngine *DO) : DiagObj(DO) {}
  Diagnostic(const DiagnosticsEngine *DO, StringRef storedDiagMessage)
      : DiagObj(DO), StoredDiagMessage(storedDiagMessage) {}

  const DiagnosticsEngine *getDiags() const { return DiagObj; }
  unsigned getID() const { return DiagObj->CurDiagID; }
  const SourceLocation &getLocation() const { return DiagObj->CurDiagLoc; }
  bool hasSourceManager() const { return DiagObj->hasSourceManager(); }
  SourceManager &getSourceManager() const { return DiagObj->getSourceManager();}

  unsigned getNumArgs() const { return DiagObj->NumDiagArgs; }

  /// Return the kind of the specified index.
  ///
  /// Based on the kind of argument, the accessors below can be used to get
  /// the value.
  ///
  /// \pre Idx < getNumArgs()
  DiagnosticsEngine::ArgumentKind getArgKind(unsigned Idx) const {
    assert(Idx < getNumArgs() && "Argument index out of range!");
    return (DiagnosticsEngine::ArgumentKind)DiagObj->DiagArgumentsKind[Idx];
  }

  /// Return the provided argument string specified by \p Idx.
  /// \pre getArgKind(Idx) == DiagnosticsEngine::ak_std_string
  const std::string &getArgStdStr(unsigned Idx) const {
    assert(getArgKind(Idx) == DiagnosticsEngine::ak_std_string &&
           "invalid argument accessor!");
    return DiagObj->DiagArgumentsStr[Idx];
  }

  /// Return the specified C string argument.
  /// \pre getArgKind(Idx) == DiagnosticsEngine::ak_c_string
  const char *getArgCStr(unsigned Idx) const {
    assert(getArgKind(Idx) == DiagnosticsEngine::ak_c_string &&
           "invalid argument accessor!");
    return reinterpret_cast<const char*>(DiagObj->DiagArgumentsVal[Idx]);
  }

  /// Return the specified signed integer argument.
  /// \pre getArgKind(Idx) == DiagnosticsEngine::ak_sint
  int getArgSInt(unsigned Idx) const {
    assert(getArgKind(Idx) == DiagnosticsEngine::ak_sint &&
           "invalid argument accessor!");
    return (int)DiagObj->DiagArgumentsVal[Idx];
  }

  /// Return the specified unsigned integer argument.
  /// \pre getArgKind(Idx) == DiagnosticsEngine::ak_uint
  unsigned getArgUInt(unsigned Idx) const {
    assert(getArgKind(Idx) == DiagnosticsEngine::ak_uint &&
           "invalid argument accessor!");
    return (unsigned)DiagObj->DiagArgumentsVal[Idx];
  }

  /// Return the specified IdentifierInfo argument.
  /// \pre getArgKind(Idx) == DiagnosticsEngine::ak_identifierinfo
  const IdentifierInfo *getArgIdentifier(unsigned Idx) const {
    assert(getArgKind(Idx) == DiagnosticsEngine::ak_identifierinfo &&
           "invalid argument accessor!");
    return reinterpret_cast<IdentifierInfo*>(DiagObj->DiagArgumentsVal[Idx]);
  }

  /// Return the specified non-string argument in an opaque form.
  /// \pre getArgKind(Idx) != DiagnosticsEngine::ak_std_string
  intptr_t getRawArg(unsigned Idx) const {
    assert(getArgKind(Idx) != DiagnosticsEngine::ak_std_string &&
           "invalid argument accessor!");
    return DiagObj->DiagArgumentsVal[Idx];
  }

  /// Return the number of source ranges associated with this diagnostic.
  unsigned getNumRanges() const {
    return DiagObj->DiagRanges.size();
  }

  /// \pre Idx < getNumRanges()
  const CharSourceRange &getRange(unsigned Idx) const {
    assert(Idx < getNumRanges() && "Invalid diagnostic range index!");
    return DiagObj->DiagRanges[Idx];
  }

  /// Return an array reference for this diagnostic's ranges.
  ArrayRef<CharSourceRange> getRanges() const {
    return DiagObj->DiagRanges;
  }

  unsigned getNumFixItHints() const {
    return DiagObj->DiagFixItHints.size();
  }

  const FixItHint &getFixItHint(unsigned Idx) const {
    assert(Idx < getNumFixItHints() && "Invalid index!");
    return DiagObj->DiagFixItHints[Idx];
  }

  ArrayRef<FixItHint> getFixItHints() const {
    return DiagObj->DiagFixItHints;
  }

  /// Format this diagnostic into a string, substituting the
  /// formal arguments into the %0 slots.
  ///
  /// The result is appended onto the \p OutStr array.
  void FormatDiagnostic(SmallVectorImpl<char> &OutStr) const;

  /// Format the given format-string into the output buffer using the
  /// arguments stored in this diagnostic.
  void FormatDiagnostic(const char *DiagStr, const char *DiagEnd,
                        SmallVectorImpl<char> &OutStr) const;
};

/**
 * Represents a diagnostic in a form that can be retained until its
 * corresponding source manager is destroyed.
 */
class StoredDiagnostic {
  unsigned ID;
  DiagnosticsEngine::Level Level;
  FullSourceLoc Loc;
  std::string Message;
  std::vector<CharSourceRange> Ranges;
  std::vector<FixItHint> FixIts;

public:
  StoredDiagnostic() = default;
  StoredDiagnostic(DiagnosticsEngine::Level Level, const Diagnostic &Info);
  StoredDiagnostic(DiagnosticsEngine::Level Level, unsigned ID,
                   StringRef Message);
  StoredDiagnostic(DiagnosticsEngine::Level Level, unsigned ID,
                   StringRef Message, FullSourceLoc Loc,
                   ArrayRef<CharSourceRange> Ranges,
                   ArrayRef<FixItHint> Fixits);

  /// Evaluates true when this object stores a diagnostic.
  explicit operator bool() const { return !Message.empty(); }

  unsigned getID() const { return ID; }
  DiagnosticsEngine::Level getLevel() const { return Level; }
  const FullSourceLoc &getLocation() const { return Loc; }
  StringRef getMessage() const { return Message; }

  void setLocation(FullSourceLoc Loc) { this->Loc = Loc; }

  using range_iterator = std::vector<CharSourceRange>::const_iterator;

  range_iterator range_begin() const { return Ranges.begin(); }
  range_iterator range_end() const { return Ranges.end(); }
  unsigned range_size() const { return Ranges.size(); }

  ArrayRef<CharSourceRange> getRanges() const {
    return llvm::makeArrayRef(Ranges);
  }

  using fixit_iterator = std::vector<FixItHint>::const_iterator;

  fixit_iterator fixit_begin() const { return FixIts.begin(); }
  fixit_iterator fixit_end() const { return FixIts.end(); }
  unsigned fixit_size() const { return FixIts.size(); }

  ArrayRef<FixItHint> getFixIts() const {
    return llvm::makeArrayRef(FixIts);
  }
};

/// Abstract interface, implemented by clients of the front-end, which
/// formats and prints fully processed diagnostics.
class DiagnosticConsumer {
protected:
  unsigned NumWarnings = 0;       ///< Number of warnings reported
  unsigned NumErrors = 0;         ///< Number of errors reported

public:
  DiagnosticConsumer() = default;
  virtual ~DiagnosticConsumer();

  unsigned getNumErrors() const { return NumErrors; }
  unsigned getNumWarnings() const { return NumWarnings; }
  virtual void clear() { NumWarnings = NumErrors = 0; }

  /// Callback to inform the diagnostic client that processing
  /// of a source file is beginning.
  ///
  /// Note that diagnostics may be emitted outside the processing of a source
  /// file, for example during the parsing of command line options. However,
  /// diagnostics with source range information are required to only be emitted
  /// in between BeginSourceFile() and EndSourceFile().
  ///
  /// \param LangOpts The language options for the source file being processed.
  /// \param PP The preprocessor object being used for the source; this is
  /// optional, e.g., it may not be present when processing AST source files.
  virtual void BeginSourceFile(const LangOptions &LangOpts,
                               const Preprocessor *PP = nullptr) {}

  /// Callback to inform the diagnostic client that processing
  /// of a source file has ended.
  ///
  /// The diagnostic client should assume that any objects made available via
  /// BeginSourceFile() are inaccessible.
  virtual void EndSourceFile() {}

  /// Callback to inform the diagnostic client that processing of all
  /// source files has ended.
  virtual void finish() {}

  /// Indicates whether the diagnostics handled by this
  /// DiagnosticConsumer should be included in the number of diagnostics
  /// reported by DiagnosticsEngine.
  ///
  /// The default implementation returns true.
  virtual bool IncludeInDiagnosticCounts() const;

  /// Handle this diagnostic, reporting it to the user or
  /// capturing it to a log as needed.
  ///
  /// The default implementation just keeps track of the total number of
  /// warnings and errors.
  virtual void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                                const Diagnostic &Info);
};

/// A diagnostic client that ignores all diagnostics.
class IgnoringDiagConsumer : public DiagnosticConsumer {
  virtual void anchor();

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override {
    // Just ignore it.
  }
};

/// Diagnostic consumer that forwards diagnostics along to an
/// existing, already-initialized diagnostic consumer.
///
class ForwardingDiagnosticConsumer : public DiagnosticConsumer {
  DiagnosticConsumer &Target;

public:
  ForwardingDiagnosticConsumer(DiagnosticConsumer &Target) : Target(Target) {}
  ~ForwardingDiagnosticConsumer() override;

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override;
  void clear() override;

  bool IncludeInDiagnosticCounts() const override;
};

// Struct used for sending info about how a type should be printed.
struct TemplateDiffTypes {
  intptr_t FromType;
  intptr_t ToType;
  unsigned PrintTree : 1;
  unsigned PrintFromType : 1;
  unsigned ElideType : 1;
  unsigned ShowColors : 1;

  // The printer sets this variable to true if the template diff was used.
  unsigned TemplateDiffUsed : 1;
};

/// Special character that the diagnostic printer will use to toggle the bold
/// attribute.  The character itself will be not be printed.
const char ToggleHighlight = 127;

/// ProcessWarningOptions - Initialize the diagnostic client and process the
/// warning options specified on the command line.
void ProcessWarningOptions(DiagnosticsEngine &Diags,
                           const DiagnosticOptions &Opts,
                           bool ReportDiags = true);

} // namespace clang

#endif // LLVM_CLANG_BASIC_DIAGNOSTIC_H
