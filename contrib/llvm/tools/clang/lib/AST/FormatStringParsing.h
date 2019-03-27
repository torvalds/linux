#ifndef LLVM_CLANG_LIB_ANALYSIS_FORMATSTRINGPARSING_H
#define LLVM_CLANG_LIB_ANALYSIS_FORMATSTRINGPARSING_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"
#include "clang/AST/FormatString.h"

namespace clang {

class LangOptions;

template <typename T>
class UpdateOnReturn {
  T &ValueToUpdate;
  const T &ValueToCopy;
public:
  UpdateOnReturn(T &valueToUpdate, const T &valueToCopy)
    : ValueToUpdate(valueToUpdate), ValueToCopy(valueToCopy) {}

  ~UpdateOnReturn() {
    ValueToUpdate = ValueToCopy;
  }
};

namespace analyze_format_string {

OptionalAmount ParseAmount(const char *&Beg, const char *E);
OptionalAmount ParseNonPositionAmount(const char *&Beg, const char *E,
                                      unsigned &argIndex);

OptionalAmount ParsePositionAmount(FormatStringHandler &H,
                                   const char *Start, const char *&Beg,
                                   const char *E, PositionContext p);

bool ParseFieldWidth(FormatStringHandler &H,
                     FormatSpecifier &CS,
                     const char *Start, const char *&Beg, const char *E,
                     unsigned *argIndex);

bool ParseArgPosition(FormatStringHandler &H,
                      FormatSpecifier &CS, const char *Start,
                      const char *&Beg, const char *E);

bool ParseVectorModifier(FormatStringHandler &H,
                         FormatSpecifier &FS, const char *&Beg, const char *E,
                         const LangOptions &LO);

/// Returns true if a LengthModifier was parsed and installed in the
/// FormatSpecifier& argument, and false otherwise.
bool ParseLengthModifier(FormatSpecifier &FS, const char *&Beg, const char *E,
                         const LangOptions &LO, bool IsScanf = false);

/// Returns true if the invalid specifier in \p SpecifierBegin is a UTF-8
/// string; check that it won't go further than \p FmtStrEnd and write
/// up the total size in \p Len.
bool ParseUTF8InvalidSpecifier(const char *SpecifierBegin,
                               const char *FmtStrEnd, unsigned &Len);

template <typename T> class SpecifierResult {
  T FS;
  const char *Start;
  bool Stop;
public:
  SpecifierResult(bool stop = false)
  : Start(nullptr), Stop(stop) {}
  SpecifierResult(const char *start,
                  const T &fs)
  : FS(fs), Start(start), Stop(false) {}

  const char *getStart() const { return Start; }
  bool shouldStop() const { return Stop; }
  bool hasValue() const { return Start != nullptr; }
  const T &getValue() const {
    assert(hasValue());
    return FS;
  }
  const T &getValue() { return FS; }
};

} // end analyze_format_string namespace
} // end clang namespace

#endif
