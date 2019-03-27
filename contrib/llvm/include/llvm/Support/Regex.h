//===-- Regex.h - Regular Expression matcher implementation -*- C++ -*-----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a POSIX regular expression matcher.  Both Basic and
// Extended POSIX regular expressions (ERE) are supported.  EREs were extended
// to support backreferences in matches.
// This implementation also supports matching strings with embedded NUL chars.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_REGEX_H
#define LLVM_SUPPORT_REGEX_H

#include <string>

struct llvm_regex;

namespace llvm {
  class StringRef;
  template<typename T> class SmallVectorImpl;

  class Regex {
  public:
    enum {
      NoFlags=0,
      /// Compile for matching that ignores upper/lower case distinctions.
      IgnoreCase=1,
      /// Compile for newline-sensitive matching. With this flag '[^' bracket
      /// expressions and '.' never match newline. A ^ anchor matches the
      /// null string after any newline in the string in addition to its normal
      /// function, and the $ anchor matches the null string before any
      /// newline in the string in addition to its normal function.
      Newline=2,
      /// By default, the POSIX extended regular expression (ERE) syntax is
      /// assumed. Pass this flag to turn on basic regular expressions (BRE)
      /// instead.
      BasicRegex=4
    };

    Regex();
    /// Compiles the given regular expression \p Regex.
    Regex(StringRef Regex, unsigned Flags = NoFlags);
    Regex(const Regex &) = delete;
    Regex &operator=(Regex regex) {
      std::swap(preg, regex.preg);
      std::swap(error, regex.error);
      return *this;
    }
    Regex(Regex &&regex);
    ~Regex();

    /// isValid - returns the error encountered during regex compilation, or
    /// matching, if any.
    bool isValid(std::string &Error) const;

    /// getNumMatches - In a valid regex, return the number of parenthesized
    /// matches it contains.  The number filled in by match will include this
    /// many entries plus one for the whole regex (as element 0).
    unsigned getNumMatches() const;

    /// matches - Match the regex against a given \p String.
    ///
    /// \param Matches - If given, on a successful match this will be filled in
    /// with references to the matched group expressions (inside \p String),
    /// the first group is always the entire pattern.
    ///
    /// This returns true on a successful match.
    bool match(StringRef String, SmallVectorImpl<StringRef> *Matches = nullptr);

    /// sub - Return the result of replacing the first match of the regex in
    /// \p String with the \p Repl string. Backreferences like "\0" in the
    /// replacement string are replaced with the appropriate match substring.
    ///
    /// Note that the replacement string has backslash escaping performed on
    /// it. Invalid backreferences are ignored (replaced by empty strings).
    ///
    /// \param Error If non-null, any errors in the substitution (invalid
    /// backreferences, trailing backslashes) will be recorded as a non-empty
    /// string.
    std::string sub(StringRef Repl, StringRef String,
                    std::string *Error = nullptr);

    /// If this function returns true, ^Str$ is an extended regular
    /// expression that matches Str and only Str.
    static bool isLiteralERE(StringRef Str);

    /// Turn String into a regex by escaping its special characters.
    static std::string escape(StringRef String);

  private:
    struct llvm_regex *preg;
    int error;
  };
}

#endif // LLVM_SUPPORT_REGEX_H
