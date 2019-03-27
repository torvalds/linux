//===-- RegularExpression.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegularExpression_h_
#define liblldb_RegularExpression_h_

#ifdef _WIN32
#include "../lib/Support/regex_impl.h"

typedef llvm_regmatch_t regmatch_t;
typedef llvm_regex_t regex_t;

inline int regcomp(llvm_regex_t *a, const char *b, int c) {
  return llvm_regcomp(a, b, c);
}

inline size_t regerror(int a, const llvm_regex_t *b, char *c, size_t d) {
  return llvm_regerror(a, b, c, d);
}

inline int regexec(const llvm_regex_t *a, const char *b, size_t c,
                   llvm_regmatch_t d[], int e) {
  return llvm_regexec(a, b, c, d, e);
}

inline void regfree(llvm_regex_t *a) { llvm_regfree(a); }
#else
#ifdef __ANDROID__
#include <regex>
#endif
#include <regex.h>
#endif

#include <string>
#include <vector>

#include <stddef.h>
#include <stdint.h>

namespace llvm {
class StringRef;
} // namespace llvm

namespace lldb_private {

//----------------------------------------------------------------------
/// @class RegularExpression RegularExpression.h
/// "lldb/Utility/RegularExpression.h"
/// A C++ wrapper class for regex.
///
/// This regular expression class wraps the posix regex functions \c
/// regcomp(), \c regerror(), \c regexec(), and \c regfree() from the header
/// file in \c /usr/include/regex\.h.
//----------------------------------------------------------------------
class RegularExpression {
public:
  class Match {
  public:
    Match(uint32_t max_matches) : m_matches() {
      if (max_matches > 0)
        m_matches.resize(max_matches + 1);
    }

    void Clear() {
      const size_t num_matches = m_matches.size();
      regmatch_t invalid_match = {-1, -1};
      for (size_t i = 0; i < num_matches; ++i)
        m_matches[i] = invalid_match;
    }

    size_t GetSize() const { return m_matches.size(); }

    regmatch_t *GetData() {
      return (m_matches.empty() ? nullptr : m_matches.data());
    }

    bool GetMatchAtIndex(llvm::StringRef s, uint32_t idx,
                         std::string &match_str) const;

    bool GetMatchAtIndex(llvm::StringRef s, uint32_t idx,
                         llvm::StringRef &match_str) const;

    bool GetMatchSpanningIndices(llvm::StringRef s, uint32_t idx1,
                                 uint32_t idx2,
                                 llvm::StringRef &match_str) const;

  protected:
    std::vector<regmatch_t>
        m_matches; ///< Where parenthesized subexpressions results are stored
  };

  //------------------------------------------------------------------
  /// Default constructor.
  ///
  /// The default constructor that initializes the object state such that it
  /// contains no compiled regular expression.
  //------------------------------------------------------------------
  RegularExpression();

  explicit RegularExpression(llvm::StringRef string);

  //------------------------------------------------------------------
  /// Destructor.
  ///
  /// Any previously compiled regular expression contained in this object will
  /// be freed.
  //------------------------------------------------------------------
  ~RegularExpression();

  RegularExpression(const RegularExpression &rhs);

  const RegularExpression &operator=(const RegularExpression &rhs);

  //------------------------------------------------------------------
  /// Compile a regular expression.
  ///
  /// Compile a regular expression using the supplied regular expression text.
  /// The compiled regular expression lives in this object so that it can be
  /// readily used for regular expression matches. Execute() can be called
  /// after the regular expression is compiled. Any previously compiled
  /// regular expression contained in this object will be freed.
  ///
  /// @param[in] re
  ///     A NULL terminated C string that represents the regular
  ///     expression to compile.
  ///
  /// @return
  ///     \b true if the regular expression compiles successfully,
  ///     \b false otherwise.
  //------------------------------------------------------------------
  bool Compile(llvm::StringRef string);
  bool Compile(const char *) = delete;

  //------------------------------------------------------------------
  /// Executes a regular expression.
  ///
  /// Execute a regular expression match using the compiled regular expression
  /// that is already in this object against the match string \a s. If any
  /// parens are used for regular expression matches \a match_count should
  /// indicate the number of regmatch_t values that are present in \a
  /// match_ptr.
  ///
  /// @param[in] string
  ///     The string to match against the compile regular expression.
  ///
  /// @param[in] match
  ///     A pointer to a RegularExpression::Match structure that was
  ///     properly initialized with the desired number of maximum
  ///     matches, or nullptr if no parenthesized matching is needed.
  ///
  /// @return
  ///     \b true if \a string matches the compiled regular
  ///     expression, \b false otherwise.
  //------------------------------------------------------------------
  bool Execute(llvm::StringRef string, Match *match = nullptr) const;
  bool Execute(const char *, Match * = nullptr) = delete;

  size_t GetErrorAsCString(char *err_str, size_t err_str_max_len) const;

  //------------------------------------------------------------------
  /// Free the compiled regular expression.
  ///
  /// If this object contains a valid compiled regular expression, this
  /// function will free any resources it was consuming.
  //------------------------------------------------------------------
  void Free();

  //------------------------------------------------------------------
  /// Access the regular expression text.
  ///
  /// Returns the text that was used to compile the current regular
  /// expression.
  ///
  /// @return
  ///     The NULL terminated C string that was used to compile the
  ///     current regular expression
  //------------------------------------------------------------------
  llvm::StringRef GetText() const;

  //------------------------------------------------------------------
  /// Test if valid.
  ///
  /// Test if this object contains a valid regular expression.
  ///
  /// @return
  ///     \b true if the regular expression compiled and is ready
  ///     for execution, \b false otherwise.
  //------------------------------------------------------------------
  bool IsValid() const;

  void Clear() {
    Free();
    m_re.clear();
    m_comp_err = 1;
  }

  int GetErrorCode() const { return m_comp_err; }

  bool operator<(const RegularExpression &rhs) const;

private:
  //------------------------------------------------------------------
  // Member variables
  //------------------------------------------------------------------
  std::string m_re; ///< A copy of the original regular expression text
  int m_comp_err;   ///< Status code for the regular expression compilation
  regex_t m_preg;   ///< The compiled regular expression
};

} // namespace lldb_private

#endif // liblldb_RegularExpression_h_
