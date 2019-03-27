//===--------------------- StringLexer.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_StringLexer_h_
#define utility_StringLexer_h_

#include <initializer_list>
#include <string>
#include <utility>

namespace lldb_utility {

class StringLexer {
public:
  typedef std::string::size_type Position;
  typedef std::string::size_type Size;

  typedef std::string::value_type Character;

  StringLexer(std::string s);

  StringLexer(const StringLexer &rhs);

  // These APIs are not bounds-checked.  Use HasAtLeast() if you're not sure.
  Character Peek();

  bool NextIf(Character c);

  std::pair<bool, Character> NextIf(std::initializer_list<Character> cs);

  bool AdvanceIf(const std::string &token);

  Character Next();

  bool HasAtLeast(Size s);

  std::string GetUnlexed();

  // This will assert if there are less than s characters preceding the cursor.
  void PutBack(Size s);

  StringLexer &operator=(const StringLexer &rhs);

private:
  std::string m_data;
  Position m_position;

  void Consume();
};

} // namespace lldb_private

#endif // #ifndef utility_StringLexer_h_
