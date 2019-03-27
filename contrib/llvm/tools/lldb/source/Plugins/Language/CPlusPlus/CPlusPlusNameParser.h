//===-- CPlusPlusNameParser.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CPlusPlusNameParser_h_
#define liblldb_CPlusPlusNameParser_h_


#include "clang/Lex/Lexer.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

// Helps to validate and obtain various parts of C++ definitions.
class CPlusPlusNameParser {
public:
  CPlusPlusNameParser(llvm::StringRef text) : m_text(text) { ExtractTokens(); }

  struct ParsedName {
    llvm::StringRef basename;
    llvm::StringRef context;
  };

  struct ParsedFunction {
    ParsedName name;
    llvm::StringRef arguments;
    llvm::StringRef qualifiers;
  };

  // Treats given text as a function definition and parses it.
  // Function definition might or might not have a return type and this should
  // change parsing result.
  // Examples:
  //    main(int, chat const*)
  //    T fun(int, bool)
  //    std::vector<int>::push_back(int)
  //    int& map<int, pair<short, int>>::operator[](short) const
  //    int (*get_function(const chat *))()
  llvm::Optional<ParsedFunction> ParseAsFunctionDefinition();

  // Treats given text as a potentially nested name of C++ entity (function,
  // class, field) and parses it.
  // Examples:
  //    main
  //    fun
  //    std::vector<int>::push_back
  //    map<int, pair<short, int>>::operator[]
  //    func<C>(int, C&)::nested_class::method
  llvm::Optional<ParsedName> ParseAsFullName();

private:
  // A C++ definition to parse.
  llvm::StringRef m_text;
  // Tokens extracted from m_text.
  llvm::SmallVector<clang::Token, 30> m_tokens;
  // Index of the next token to look at from m_tokens.
  size_t m_next_token_index = 0;

  // Range of tokens saved in m_next_token_index.
  struct Range {
    size_t begin_index = 0;
    size_t end_index = 0;

    Range() {}
    Range(size_t begin, size_t end) : begin_index(begin), end_index(end) {
      assert(end >= begin);
    }

    size_t size() const { return end_index - begin_index; }

    bool empty() const { return size() == 0; }
  };

  struct ParsedNameRanges {
    Range basename_range;
    Range context_range;
  };

  // Bookmark automatically restores parsing position (m_next_token_index)
  // when destructed unless it's manually removed with Remove().
  class Bookmark {
  public:
    Bookmark(size_t &position)
        : m_position(position), m_position_value(position) {}
    Bookmark(const Bookmark &) = delete;
    Bookmark(Bookmark &&b)
        : m_position(b.m_position), m_position_value(b.m_position_value),
          m_restore(b.m_restore) {
      b.Remove();
    }
    Bookmark &operator=(Bookmark &&) = delete;
    Bookmark &operator=(const Bookmark &) = delete;

    void Remove() { m_restore = false; }
    size_t GetSavedPosition() { return m_position_value; }
    ~Bookmark() {
      if (m_restore) {
        m_position = m_position_value;
      }
    }

  private:
    size_t &m_position;
    size_t m_position_value;
    bool m_restore = true;
  };

  bool HasMoreTokens();
  void Advance();
  void TakeBack();
  bool ConsumeToken(clang::tok::TokenKind kind);
  template <typename... Ts> bool ConsumeToken(Ts... kinds);
  Bookmark SetBookmark();
  size_t GetCurrentPosition();
  clang::Token &Peek();
  bool ConsumeBrackets(clang::tok::TokenKind left, clang::tok::TokenKind right);

  llvm::Optional<ParsedFunction> ParseFunctionImpl(bool expect_return_type);

  // Parses functions returning function pointers 'string (*f(int x))(float y)'
  llvm::Optional<ParsedFunction> ParseFuncPtr(bool expect_return_type);

  // Consumes function arguments enclosed within '(' ... ')'
  bool ConsumeArguments();

  // Consumes template arguments enclosed within '<' ... '>'
  bool ConsumeTemplateArgs();

  // Consumes '(anonymous namespace)'
  bool ConsumeAnonymousNamespace();

  // Consumes '{lambda ...}'
  bool ConsumeLambda();

  // Consumes operator declaration like 'operator *' or 'operator delete []'
  bool ConsumeOperator();

  // Skips 'const' and 'volatile'
  void SkipTypeQualifiers();

  // Skips 'const', 'volatile', '&', '&&' in the end of the function.
  void SkipFunctionQualifiers();

  // Consumes built-in types like 'int' or 'unsigned long long int'
  bool ConsumeBuiltinType();

  // Consumes types defined via decltype keyword.
  bool ConsumeDecltype();

  // Skips 'const' and 'volatile'
  void SkipPtrsAndRefs();

  // Consumes things like 'const * const &'
  bool ConsumePtrsAndRefs();

  // Consumes full type name like 'Namespace::Class<int>::Method()::InnerClass'
  bool ConsumeTypename();

  llvm::Optional<ParsedNameRanges> ParseFullNameImpl();
  llvm::StringRef GetTextForRange(const Range &range);

  // Populate m_tokens by calling clang lexer on m_text.
  void ExtractTokens();
};

} // namespace lldb_private

#endif // liblldb_CPlusPlusNameParser_h_
