//===-- ubsan_diag.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Diagnostics emission for Clang's undefined behavior sanitizer.
//
//===----------------------------------------------------------------------===//
#ifndef UBSAN_DIAG_H
#define UBSAN_DIAG_H

#include "ubsan_value.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_symbolizer.h"

namespace __ubsan {

class SymbolizedStackHolder {
  SymbolizedStack *Stack;

  void clear() {
    if (Stack)
      Stack->ClearAll();
  }

public:
  explicit SymbolizedStackHolder(SymbolizedStack *Stack = nullptr)
      : Stack(Stack) {}
  ~SymbolizedStackHolder() { clear(); }
  void reset(SymbolizedStack *S) {
    if (Stack != S)
      clear();
    Stack = S;
  }
  const SymbolizedStack *get() const { return Stack; }
};

SymbolizedStack *getSymbolizedLocation(uptr PC);

inline SymbolizedStack *getCallerLocation(uptr CallerPC) {
  CHECK(CallerPC);
  uptr PC = StackTrace::GetPreviousInstructionPc(CallerPC);
  return getSymbolizedLocation(PC);
}

/// A location of some data within the program's address space.
typedef uptr MemoryLocation;

/// \brief Location at which a diagnostic can be emitted. Either a
/// SourceLocation, a MemoryLocation, or a SymbolizedStack.
class Location {
public:
  enum LocationKind { LK_Null, LK_Source, LK_Memory, LK_Symbolized };

private:
  LocationKind Kind;
  // FIXME: In C++11, wrap these in an anonymous union.
  SourceLocation SourceLoc;
  MemoryLocation MemoryLoc;
  const SymbolizedStack *SymbolizedLoc;  // Not owned.

public:
  Location() : Kind(LK_Null) {}
  Location(SourceLocation Loc) :
    Kind(LK_Source), SourceLoc(Loc) {}
  Location(MemoryLocation Loc) :
    Kind(LK_Memory), MemoryLoc(Loc) {}
  // SymbolizedStackHolder must outlive Location object.
  Location(const SymbolizedStackHolder &Stack) :
    Kind(LK_Symbolized), SymbolizedLoc(Stack.get()) {}

  LocationKind getKind() const { return Kind; }

  bool isSourceLocation() const { return Kind == LK_Source; }
  bool isMemoryLocation() const { return Kind == LK_Memory; }
  bool isSymbolizedStack() const { return Kind == LK_Symbolized; }

  SourceLocation getSourceLocation() const {
    CHECK(isSourceLocation());
    return SourceLoc;
  }
  MemoryLocation getMemoryLocation() const {
    CHECK(isMemoryLocation());
    return MemoryLoc;
  }
  const SymbolizedStack *getSymbolizedStack() const {
    CHECK(isSymbolizedStack());
    return SymbolizedLoc;
  }
};

/// A diagnostic severity level.
enum DiagLevel {
  DL_Error, ///< An error.
  DL_Note   ///< A note, attached to a prior diagnostic.
};

/// \brief Annotation for a range of locations in a diagnostic.
class Range {
  Location Start, End;
  const char *Text;

public:
  Range() : Start(), End(), Text() {}
  Range(MemoryLocation Start, MemoryLocation End, const char *Text)
    : Start(Start), End(End), Text(Text) {}
  Location getStart() const { return Start; }
  Location getEnd() const { return End; }
  const char *getText() const { return Text; }
};

/// \brief A C++ type name. Really just a strong typedef for 'const char*'.
class TypeName {
  const char *Name;
public:
  TypeName(const char *Name) : Name(Name) {}
  const char *getName() const { return Name; }
};

enum class ErrorType {
#define UBSAN_CHECK(Name, SummaryKind, FSanitizeFlagName) Name,
#include "ubsan_checks.inc"
#undef UBSAN_CHECK
};

/// \brief Representation of an in-flight diagnostic.
///
/// Temporary \c Diag instances are created by the handler routines to
/// accumulate arguments for a diagnostic. The destructor emits the diagnostic
/// message.
class Diag {
  /// The location at which the problem occurred.
  Location Loc;

  /// The diagnostic level.
  DiagLevel Level;

  /// The error type.
  ErrorType ET;

  /// The message which will be emitted, with %0, %1, ... placeholders for
  /// arguments.
  const char *Message;

public:
  /// Kinds of arguments, corresponding to members of \c Arg's union.
  enum ArgKind {
    AK_String, ///< A string argument, displayed as-is.
    AK_TypeName,///< A C++ type name, possibly demangled before display.
    AK_UInt,   ///< An unsigned integer argument.
    AK_SInt,   ///< A signed integer argument.
    AK_Float,  ///< A floating-point argument.
    AK_Pointer ///< A pointer argument, displayed in hexadecimal.
  };

  /// An individual diagnostic message argument.
  struct Arg {
    Arg() {}
    Arg(const char *String) : Kind(AK_String), String(String) {}
    Arg(TypeName TN) : Kind(AK_TypeName), String(TN.getName()) {}
    Arg(UIntMax UInt) : Kind(AK_UInt), UInt(UInt) {}
    Arg(SIntMax SInt) : Kind(AK_SInt), SInt(SInt) {}
    Arg(FloatMax Float) : Kind(AK_Float), Float(Float) {}
    Arg(const void *Pointer) : Kind(AK_Pointer), Pointer(Pointer) {}

    ArgKind Kind;
    union {
      const char *String;
      UIntMax UInt;
      SIntMax SInt;
      FloatMax Float;
      const void *Pointer;
    };
  };

private:
  static const unsigned MaxArgs = 8;
  static const unsigned MaxRanges = 1;

  /// The arguments which have been added to this diagnostic so far.
  Arg Args[MaxArgs];
  unsigned NumArgs;

  /// The ranges which have been added to this diagnostic so far.
  Range Ranges[MaxRanges];
  unsigned NumRanges;

  Diag &AddArg(Arg A) {
    CHECK(NumArgs != MaxArgs);
    Args[NumArgs++] = A;
    return *this;
  }

  Diag &AddRange(Range A) {
    CHECK(NumRanges != MaxRanges);
    Ranges[NumRanges++] = A;
    return *this;
  }

  /// \c Diag objects are not copyable.
  Diag(const Diag &); // NOT IMPLEMENTED
  Diag &operator=(const Diag &);

public:
  Diag(Location Loc, DiagLevel Level, ErrorType ET, const char *Message)
      : Loc(Loc), Level(Level), ET(ET), Message(Message), NumArgs(0),
        NumRanges(0) {}
  ~Diag();

  Diag &operator<<(const char *Str) { return AddArg(Str); }
  Diag &operator<<(TypeName TN) { return AddArg(TN); }
  Diag &operator<<(unsigned long long V) { return AddArg(UIntMax(V)); }
  Diag &operator<<(const void *V) { return AddArg(V); }
  Diag &operator<<(const TypeDescriptor &V);
  Diag &operator<<(const Value &V);
  Diag &operator<<(const Range &R) { return AddRange(R); }
};

struct ReportOptions {
  // If FromUnrecoverableHandler is specified, UBSan runtime handler is not
  // expected to return.
  bool FromUnrecoverableHandler;
  /// pc/bp are used to unwind the stack trace.
  uptr pc;
  uptr bp;
};

bool ignoreReport(SourceLocation SLoc, ReportOptions Opts, ErrorType ET);

#define GET_REPORT_OPTIONS(unrecoverable_handler) \
    GET_CALLER_PC_BP; \
    ReportOptions Opts = {unrecoverable_handler, pc, bp}

void GetStackTrace(BufferedStackTrace *stack, uptr max_depth, uptr pc, uptr bp,
                   void *context, bool fast);

/// \brief Instantiate this class before printing diagnostics in the error
/// report. This class ensures that reports from different threads and from
/// different sanitizers won't be mixed.
class ScopedReport {
  struct Initializer {
    Initializer();
  };
  Initializer initializer_;
  ScopedErrorReportLock report_lock_;

  ReportOptions Opts;
  Location SummaryLoc;
  ErrorType Type;

public:
  ScopedReport(ReportOptions Opts, Location SummaryLoc, ErrorType Type);
  ~ScopedReport();

  static void CheckLocked() { ScopedErrorReportLock::CheckLocked(); }
};

void InitializeSuppressions();
bool IsVptrCheckSuppressed(const char *TypeName);
// Sometimes UBSan runtime can know filename from handlers arguments, even if
// debug info is missing.
bool IsPCSuppressed(ErrorType ET, uptr PC, const char *Filename);

} // namespace __ubsan

#endif // UBSAN_DIAG_H
