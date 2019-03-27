//===-- RegularExpression.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/RegularExpression.h"

#include "llvm/ADT/StringRef.h"

#include <string>

//----------------------------------------------------------------------
// Enable enhanced mode if it is available. This allows for things like \d for
// digit, \s for space, and many more, but it isn't available everywhere.
//----------------------------------------------------------------------
#if defined(REG_ENHANCED)
#define DEFAULT_COMPILE_FLAGS (REG_ENHANCED | REG_EXTENDED)
#else
#define DEFAULT_COMPILE_FLAGS (REG_EXTENDED)
#endif

using namespace lldb_private;

RegularExpression::RegularExpression() : m_re(), m_comp_err(1), m_preg() {
  memset(&m_preg, 0, sizeof(m_preg));
}

//----------------------------------------------------------------------
// Constructor that compiles "re" using "flags" and stores the resulting
// compiled regular expression into this object.
//----------------------------------------------------------------------
RegularExpression::RegularExpression(llvm::StringRef str)
    : m_re(), m_comp_err(1), m_preg() {
  memset(&m_preg, 0, sizeof(m_preg));
  Compile(str);
}

RegularExpression::RegularExpression(const RegularExpression &rhs) {
  memset(&m_preg, 0, sizeof(m_preg));
  Compile(rhs.GetText());
}

const RegularExpression &RegularExpression::
operator=(const RegularExpression &rhs) {
  if (&rhs != this)
    Compile(rhs.GetText());
  return *this;
}

//----------------------------------------------------------------------
// Destructor
//
// Any previously compiled regular expression contained in this object will be
// freed.
//----------------------------------------------------------------------
RegularExpression::~RegularExpression() { Free(); }

//----------------------------------------------------------------------
// Compile a regular expression using the supplied regular expression text and
// flags. The compiled regular expression lives in this object so that it can
// be readily used for regular expression matches. Execute() can be called
// after the regular expression is compiled. Any previously compiled regular
// expression contained in this object will be freed.
//
// RETURNS
//  True if the regular expression compiles successfully, false
//  otherwise.
//----------------------------------------------------------------------
bool RegularExpression::Compile(llvm::StringRef str) {
  Free();

  // regcomp() on darwin does not recognize "" as a valid regular expression,
  // so we substitute it with an equivalent non-empty one.
  m_re = str.empty() ? "()" : str;
  m_comp_err = ::regcomp(&m_preg, m_re.c_str(), DEFAULT_COMPILE_FLAGS);
  return m_comp_err == 0;
}

//----------------------------------------------------------------------
// Execute a regular expression match using the compiled regular expression
// that is already in this object against the match string "s". If any parens
// are used for regular expression matches "match_count" should indicate the
// number of regmatch_t values that are present in "match_ptr". The regular
// expression will be executed using the "execute_flags".
//---------------------------------------------------------------------
bool RegularExpression::Execute(llvm::StringRef str, Match *match) const {
  int err = 1;
  if (m_comp_err == 0) {
    // Argument to regexec must be null-terminated.
    std::string reg_str = str;
    if (match) {
      err = ::regexec(&m_preg, reg_str.c_str(), match->GetSize(),
                      match->GetData(), 0);
    } else {
      err = ::regexec(&m_preg, reg_str.c_str(), 0, nullptr, 0);
    }
  }

  if (err != 0) {
    // The regular expression didn't compile, so clear the matches
    if (match)
      match->Clear();
    return false;
  }
  return true;
}

bool RegularExpression::Match::GetMatchAtIndex(llvm::StringRef s, uint32_t idx,
                                               std::string &match_str) const {
  llvm::StringRef match_str_ref;
  if (GetMatchAtIndex(s, idx, match_str_ref)) {
    match_str = match_str_ref.str();
    return true;
  }
  return false;
}

bool RegularExpression::Match::GetMatchAtIndex(
    llvm::StringRef s, uint32_t idx, llvm::StringRef &match_str) const {
  if (idx < m_matches.size()) {
    if (m_matches[idx].rm_eo == -1 && m_matches[idx].rm_so == -1)
      return false;

    if (m_matches[idx].rm_eo == m_matches[idx].rm_so) {
      // Matched the empty string...
      match_str = llvm::StringRef();
      return true;
    } else if (m_matches[idx].rm_eo > m_matches[idx].rm_so) {
      match_str = s.substr(m_matches[idx].rm_so,
                           m_matches[idx].rm_eo - m_matches[idx].rm_so);
      return true;
    }
  }
  return false;
}

bool RegularExpression::Match::GetMatchSpanningIndices(
    llvm::StringRef s, uint32_t idx1, uint32_t idx2,
    llvm::StringRef &match_str) const {
  if (idx1 < m_matches.size() && idx2 < m_matches.size()) {
    if (m_matches[idx1].rm_so == m_matches[idx2].rm_eo) {
      // Matched the empty string...
      match_str = llvm::StringRef();
      return true;
    } else if (m_matches[idx1].rm_so < m_matches[idx2].rm_eo) {
      match_str = s.substr(m_matches[idx1].rm_so,
                           m_matches[idx2].rm_eo - m_matches[idx1].rm_so);
      return true;
    }
  }
  return false;
}

//----------------------------------------------------------------------
// Returns true if the regular expression compiled and is ready for execution.
//----------------------------------------------------------------------
bool RegularExpression::IsValid() const { return m_comp_err == 0; }

//----------------------------------------------------------------------
// Returns the text that was used to compile the current regular expression.
//----------------------------------------------------------------------
llvm::StringRef RegularExpression::GetText() const { return m_re; }

//----------------------------------------------------------------------
// Free any contained compiled regular expressions.
//----------------------------------------------------------------------
void RegularExpression::Free() {
  if (m_comp_err == 0) {
    m_re.clear();
    regfree(&m_preg);
    // Set a compile error since we no longer have a valid regex
    m_comp_err = 1;
  }
}

size_t RegularExpression::GetErrorAsCString(char *err_str,
                                            size_t err_str_max_len) const {
  if (m_comp_err == 0) {
    if (err_str && err_str_max_len)
      *err_str = '\0';
    return 0;
  }

  return ::regerror(m_comp_err, &m_preg, err_str, err_str_max_len);
}

bool RegularExpression::operator<(const RegularExpression &rhs) const {
  return (m_re < rhs.m_re);
}
