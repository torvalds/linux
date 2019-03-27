//===--- Diagnostics.h - Helper class for error diagnostics -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Diagnostics class to manage error messages.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ASTMATCHERS_DYNAMIC_DIAGNOSTICS_H
#define LLVM_CLANG_ASTMATCHERS_DYNAMIC_DIAGNOSTICS_H

#include "clang/ASTMatchers/Dynamic/VariantValue.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

namespace clang {
namespace ast_matchers {
namespace dynamic {

struct SourceLocation {
  SourceLocation() : Line(), Column() {}
  unsigned Line;
  unsigned Column;
};

struct SourceRange {
  SourceLocation Start;
  SourceLocation End;
};

/// A VariantValue instance annotated with its parser context.
struct ParserValue {
  ParserValue() : Text(), Range(), Value() {}
  StringRef Text;
  SourceRange Range;
  VariantValue Value;
};

/// Helper class to manage error messages.
class Diagnostics {
public:
  /// Parser context types.
  enum ContextType {
    CT_MatcherArg = 0,
    CT_MatcherConstruct = 1
  };

  /// All errors from the system.
  enum ErrorType {
    ET_None = 0,

    ET_RegistryMatcherNotFound = 1,
    ET_RegistryWrongArgCount = 2,
    ET_RegistryWrongArgType = 3,
    ET_RegistryNotBindable = 4,
    ET_RegistryAmbiguousOverload = 5,
    ET_RegistryValueNotFound = 6,

    ET_ParserStringError = 100,
    ET_ParserNoOpenParen = 101,
    ET_ParserNoCloseParen = 102,
    ET_ParserNoComma = 103,
    ET_ParserNoCode = 104,
    ET_ParserNotAMatcher = 105,
    ET_ParserInvalidToken = 106,
    ET_ParserMalformedBindExpr = 107,
    ET_ParserTrailingCode = 108,
    ET_ParserNumberError = 109,
    ET_ParserOverloadedType = 110
  };

  /// Helper stream class.
  class ArgStream {
  public:
    ArgStream(std::vector<std::string> *Out) : Out(Out) {}
    template <class T> ArgStream &operator<<(const T &Arg) {
      return operator<<(Twine(Arg));
    }
    ArgStream &operator<<(const Twine &Arg);

  private:
    std::vector<std::string> *Out;
  };

  /// Class defining a parser context.
  ///
  /// Used by the parser to specify (possibly recursive) contexts where the
  /// parsing/construction can fail. Any error triggered within a context will
  /// keep information about the context chain.
  /// This class should be used as a RAII instance in the stack.
  struct Context {
  public:
    /// About to call the constructor for a matcher.
    enum ConstructMatcherEnum { ConstructMatcher };
    Context(ConstructMatcherEnum, Diagnostics *Error, StringRef MatcherName,
            SourceRange MatcherRange);
    /// About to recurse into parsing one argument for a matcher.
    enum MatcherArgEnum { MatcherArg };
    Context(MatcherArgEnum, Diagnostics *Error, StringRef MatcherName,
            SourceRange MatcherRange, unsigned ArgNumber);
    ~Context();

  private:
    Diagnostics *const Error;
  };

  /// Context for overloaded matcher construction.
  ///
  /// This context will take care of merging all errors that happen within it
  /// as "candidate" overloads for the same matcher.
  struct OverloadContext {
  public:
   OverloadContext(Diagnostics* Error);
   ~OverloadContext();

   /// Revert all errors that happened within this context.
   void revertErrors();

  private:
    Diagnostics *const Error;
    unsigned BeginIndex;
  };

  /// Add an error to the diagnostics.
  ///
  /// All the context information will be kept on the error message.
  /// \return a helper class to allow the caller to pass the arguments for the
  /// error message, using the << operator.
  ArgStream addError(SourceRange Range, ErrorType Error);

  /// Information stored for one frame of the context.
  struct ContextFrame {
    ContextType Type;
    SourceRange Range;
    std::vector<std::string> Args;
  };

  /// Information stored for each error found.
  struct ErrorContent {
    std::vector<ContextFrame> ContextStack;
    struct Message {
      SourceRange Range;
      ErrorType Type;
      std::vector<std::string> Args;
    };
    std::vector<Message> Messages;
  };
  ArrayRef<ErrorContent> errors() const { return Errors; }

  /// Returns a simple string representation of each error.
  ///
  /// Each error only shows the error message without any context.
  void printToStream(llvm::raw_ostream &OS) const;
  std::string toString() const;

  /// Returns the full string representation of each error.
  ///
  /// Each error message contains the full context.
  void printToStreamFull(llvm::raw_ostream &OS) const;
  std::string toStringFull() const;

private:
  /// Helper function used by the constructors of ContextFrame.
  ArgStream pushContextFrame(ContextType Type, SourceRange Range);

  std::vector<ContextFrame> ContextStack;
  std::vector<ErrorContent> Errors;
};

}  // namespace dynamic
}  // namespace ast_matchers
}  // namespace clang

#endif  // LLVM_CLANG_AST_MATCHERS_DYNAMIC_DIAGNOSTICS_H
