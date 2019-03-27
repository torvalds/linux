//===--- HeaderIncludes.cpp - Insert/Delete #includes --*- C++ -*----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Inclusions/HeaderIncludes.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/FormatVariadic.h"

namespace clang {
namespace tooling {
namespace {

LangOptions createLangOpts() {
  LangOptions LangOpts;
  LangOpts.CPlusPlus = 1;
  LangOpts.CPlusPlus11 = 1;
  LangOpts.CPlusPlus14 = 1;
  LangOpts.LineComment = 1;
  LangOpts.CXXOperatorNames = 1;
  LangOpts.Bool = 1;
  LangOpts.ObjC = 1;
  LangOpts.MicrosoftExt = 1;    // To get kw___try, kw___finally.
  LangOpts.DeclSpecKeyword = 1; // To get __declspec.
  LangOpts.WChar = 1;           // To get wchar_t
  return LangOpts;
}

// Returns the offset after skipping a sequence of tokens, matched by \p
// GetOffsetAfterSequence, from the start of the code.
// \p GetOffsetAfterSequence should be a function that matches a sequence of
// tokens and returns an offset after the sequence.
unsigned getOffsetAfterTokenSequence(
    StringRef FileName, StringRef Code, const IncludeStyle &Style,
    llvm::function_ref<unsigned(const SourceManager &, Lexer &, Token &)>
        GetOffsetAfterSequence) {
  SourceManagerForFile VirtualSM(FileName, Code);
  SourceManager &SM = VirtualSM.get();
  Lexer Lex(SM.getMainFileID(), SM.getBuffer(SM.getMainFileID()), SM,
            createLangOpts());
  Token Tok;
  // Get the first token.
  Lex.LexFromRawLexer(Tok);
  return GetOffsetAfterSequence(SM, Lex, Tok);
}

// Check if a sequence of tokens is like "#<Name> <raw_identifier>". If it is,
// \p Tok will be the token after this directive; otherwise, it can be any token
// after the given \p Tok (including \p Tok).
bool checkAndConsumeDirectiveWithName(Lexer &Lex, StringRef Name, Token &Tok) {
  bool Matched = Tok.is(tok::hash) && !Lex.LexFromRawLexer(Tok) &&
                 Tok.is(tok::raw_identifier) &&
                 Tok.getRawIdentifier() == Name && !Lex.LexFromRawLexer(Tok) &&
                 Tok.is(tok::raw_identifier);
  if (Matched)
    Lex.LexFromRawLexer(Tok);
  return Matched;
}

void skipComments(Lexer &Lex, Token &Tok) {
  while (Tok.is(tok::comment))
    if (Lex.LexFromRawLexer(Tok))
      return;
}

// Returns the offset after header guard directives and any comments
// before/after header guards. If no header guard presents in the code, this
// will returns the offset after skipping all comments from the start of the
// code.
unsigned getOffsetAfterHeaderGuardsAndComments(StringRef FileName,
                                               StringRef Code,
                                               const IncludeStyle &Style) {
  return getOffsetAfterTokenSequence(
      FileName, Code, Style,
      [](const SourceManager &SM, Lexer &Lex, Token Tok) {
        skipComments(Lex, Tok);
        unsigned InitialOffset = SM.getFileOffset(Tok.getLocation());
        if (checkAndConsumeDirectiveWithName(Lex, "ifndef", Tok)) {
          skipComments(Lex, Tok);
          if (checkAndConsumeDirectiveWithName(Lex, "define", Tok))
            return SM.getFileOffset(Tok.getLocation());
        }
        return InitialOffset;
      });
}

// Check if a sequence of tokens is like
//    "#include ("header.h" | <header.h>)".
// If it is, \p Tok will be the token after this directive; otherwise, it can be
// any token after the given \p Tok (including \p Tok).
bool checkAndConsumeInclusiveDirective(Lexer &Lex, Token &Tok) {
  auto Matched = [&]() {
    Lex.LexFromRawLexer(Tok);
    return true;
  };
  if (Tok.is(tok::hash) && !Lex.LexFromRawLexer(Tok) &&
      Tok.is(tok::raw_identifier) && Tok.getRawIdentifier() == "include") {
    if (Lex.LexFromRawLexer(Tok))
      return false;
    if (Tok.is(tok::string_literal))
      return Matched();
    if (Tok.is(tok::less)) {
      while (!Lex.LexFromRawLexer(Tok) && Tok.isNot(tok::greater)) {
      }
      if (Tok.is(tok::greater))
        return Matched();
    }
  }
  return false;
}

// Returns the offset of the last #include directive after which a new
// #include can be inserted. This ignores #include's after the #include block(s)
// in the beginning of a file to avoid inserting headers into code sections
// where new #include's should not be added by default.
// These code sections include:
//      - raw string literals (containing #include).
//      - #if blocks.
//      - Special #include's among declarations (e.g. functions).
//
// If no #include after which a new #include can be inserted, this returns the
// offset after skipping all comments from the start of the code.
// Inserting after an #include is not allowed if it comes after code that is not
// #include (e.g. pre-processing directive that is not #include, declarations).
unsigned getMaxHeaderInsertionOffset(StringRef FileName, StringRef Code,
                                     const IncludeStyle &Style) {
  return getOffsetAfterTokenSequence(
      FileName, Code, Style,
      [](const SourceManager &SM, Lexer &Lex, Token Tok) {
        skipComments(Lex, Tok);
        unsigned MaxOffset = SM.getFileOffset(Tok.getLocation());
        while (checkAndConsumeInclusiveDirective(Lex, Tok))
          MaxOffset = SM.getFileOffset(Tok.getLocation());
        return MaxOffset;
      });
}

inline StringRef trimInclude(StringRef IncludeName) {
  return IncludeName.trim("\"<>");
}

const char IncludeRegexPattern[] =
    R"(^[\t\ ]*#[\t\ ]*(import|include)[^"<]*(["<][^">]*[">]))";

} // anonymous namespace

IncludeCategoryManager::IncludeCategoryManager(const IncludeStyle &Style,
                                               StringRef FileName)
    : Style(Style), FileName(FileName) {
  FileStem = llvm::sys::path::stem(FileName);
  for (const auto &Category : Style.IncludeCategories)
    CategoryRegexs.emplace_back(Category.Regex, llvm::Regex::IgnoreCase);
  IsMainFile = FileName.endswith(".c") || FileName.endswith(".cc") ||
               FileName.endswith(".cpp") || FileName.endswith(".c++") ||
               FileName.endswith(".cxx") || FileName.endswith(".m") ||
               FileName.endswith(".mm");
}

int IncludeCategoryManager::getIncludePriority(StringRef IncludeName,
                                               bool CheckMainHeader) const {
  int Ret = INT_MAX;
  for (unsigned i = 0, e = CategoryRegexs.size(); i != e; ++i)
    if (CategoryRegexs[i].match(IncludeName)) {
      Ret = Style.IncludeCategories[i].Priority;
      break;
    }
  if (CheckMainHeader && IsMainFile && Ret > 0 && isMainHeader(IncludeName))
    Ret = 0;
  return Ret;
}

bool IncludeCategoryManager::isMainHeader(StringRef IncludeName) const {
  if (!IncludeName.startswith("\""))
    return false;
  StringRef HeaderStem =
      llvm::sys::path::stem(IncludeName.drop_front(1).drop_back(1));
  if (FileStem.startswith(HeaderStem) ||
      FileStem.startswith_lower(HeaderStem)) {
    llvm::Regex MainIncludeRegex(HeaderStem.str() + Style.IncludeIsMainRegex,
                                 llvm::Regex::IgnoreCase);
    if (MainIncludeRegex.match(FileStem))
      return true;
  }
  return false;
}

HeaderIncludes::HeaderIncludes(StringRef FileName, StringRef Code,
                               const IncludeStyle &Style)
    : FileName(FileName), Code(Code), FirstIncludeOffset(-1),
      MinInsertOffset(
          getOffsetAfterHeaderGuardsAndComments(FileName, Code, Style)),
      MaxInsertOffset(MinInsertOffset +
                      getMaxHeaderInsertionOffset(
                          FileName, Code.drop_front(MinInsertOffset), Style)),
      Categories(Style, FileName),
      IncludeRegex(llvm::Regex(IncludeRegexPattern)) {
  // Add 0 for main header and INT_MAX for headers that are not in any
  // category.
  Priorities = {0, INT_MAX};
  for (const auto &Category : Style.IncludeCategories)
    Priorities.insert(Category.Priority);
  SmallVector<StringRef, 32> Lines;
  Code.drop_front(MinInsertOffset).split(Lines, "\n");

  unsigned Offset = MinInsertOffset;
  unsigned NextLineOffset;
  SmallVector<StringRef, 4> Matches;
  for (auto Line : Lines) {
    NextLineOffset = std::min(Code.size(), Offset + Line.size() + 1);
    if (IncludeRegex.match(Line, &Matches)) {
      // If this is the last line without trailing newline, we need to make
      // sure we don't delete across the file boundary.
      addExistingInclude(
          Include(Matches[2],
                  tooling::Range(
                      Offset, std::min(Line.size() + 1, Code.size() - Offset))),
          NextLineOffset);
    }
    Offset = NextLineOffset;
  }

  // Populate CategoryEndOfssets:
  // - Ensure that CategoryEndOffset[Highest] is always populated.
  // - If CategoryEndOffset[Priority] isn't set, use the next higher value
  // that is set, up to CategoryEndOffset[Highest].
  auto Highest = Priorities.begin();
  if (CategoryEndOffsets.find(*Highest) == CategoryEndOffsets.end()) {
    if (FirstIncludeOffset >= 0)
      CategoryEndOffsets[*Highest] = FirstIncludeOffset;
    else
      CategoryEndOffsets[*Highest] = MinInsertOffset;
  }
  // By this point, CategoryEndOffset[Highest] is always set appropriately:
  //  - to an appropriate location before/after existing #includes, or
  //  - to right after the header guard, or
  //  - to the beginning of the file.
  for (auto I = ++Priorities.begin(), E = Priorities.end(); I != E; ++I)
    if (CategoryEndOffsets.find(*I) == CategoryEndOffsets.end())
      CategoryEndOffsets[*I] = CategoryEndOffsets[*std::prev(I)];
}

// \p Offset: the start of the line following this include directive.
void HeaderIncludes::addExistingInclude(Include IncludeToAdd,
                                        unsigned NextLineOffset) {
  auto Iter =
      ExistingIncludes.try_emplace(trimInclude(IncludeToAdd.Name)).first;
  Iter->second.push_back(std::move(IncludeToAdd));
  auto &CurInclude = Iter->second.back();
  // The header name with quotes or angle brackets.
  // Only record the offset of current #include if we can insert after it.
  if (CurInclude.R.getOffset() <= MaxInsertOffset) {
    int Priority = Categories.getIncludePriority(
        CurInclude.Name, /*CheckMainHeader=*/FirstIncludeOffset < 0);
    CategoryEndOffsets[Priority] = NextLineOffset;
    IncludesByPriority[Priority].push_back(&CurInclude);
    if (FirstIncludeOffset < 0)
      FirstIncludeOffset = CurInclude.R.getOffset();
  }
}

llvm::Optional<tooling::Replacement>
HeaderIncludes::insert(llvm::StringRef IncludeName, bool IsAngled) const {
  assert(IncludeName == trimInclude(IncludeName));
  // If a <header> ("header") already exists in code, "header" (<header>) with
  // different quotation will still be inserted.
  // FIXME: figure out if this is the best behavior.
  auto It = ExistingIncludes.find(IncludeName);
  if (It != ExistingIncludes.end())
    for (const auto &Inc : It->second)
      if ((IsAngled && StringRef(Inc.Name).startswith("<")) ||
          (!IsAngled && StringRef(Inc.Name).startswith("\"")))
        return llvm::None;
  std::string Quoted =
      llvm::formatv(IsAngled ? "<{0}>" : "\"{0}\"", IncludeName);
  StringRef QuotedName = Quoted;
  int Priority = Categories.getIncludePriority(
      QuotedName, /*CheckMainHeader=*/FirstIncludeOffset < 0);
  auto CatOffset = CategoryEndOffsets.find(Priority);
  assert(CatOffset != CategoryEndOffsets.end());
  unsigned InsertOffset = CatOffset->second; // Fall back offset
  auto Iter = IncludesByPriority.find(Priority);
  if (Iter != IncludesByPriority.end()) {
    for (const auto *Inc : Iter->second) {
      if (QuotedName < Inc->Name) {
        InsertOffset = Inc->R.getOffset();
        break;
      }
    }
  }
  assert(InsertOffset <= Code.size());
  std::string NewInclude = llvm::formatv("#include {0}\n", QuotedName);
  // When inserting headers at end of the code, also append '\n' to the code
  // if it does not end with '\n'.
  // FIXME: when inserting multiple #includes at the end of code, only one
  // newline should be added.
  if (InsertOffset == Code.size() && (!Code.empty() && Code.back() != '\n'))
    NewInclude = "\n" + NewInclude;
  return tooling::Replacement(FileName, InsertOffset, 0, NewInclude);
}

tooling::Replacements HeaderIncludes::remove(llvm::StringRef IncludeName,
                                             bool IsAngled) const {
  assert(IncludeName == trimInclude(IncludeName));
  tooling::Replacements Result;
  auto Iter = ExistingIncludes.find(IncludeName);
  if (Iter == ExistingIncludes.end())
    return Result;
  for (const auto &Inc : Iter->second) {
    if ((IsAngled && StringRef(Inc.Name).startswith("\"")) ||
        (!IsAngled && StringRef(Inc.Name).startswith("<")))
      continue;
    llvm::Error Err = Result.add(tooling::Replacement(
        FileName, Inc.R.getOffset(), Inc.R.getLength(), ""));
    if (Err) {
      auto ErrMsg = "Unexpected conflicts in #include deletions: " +
                    llvm::toString(std::move(Err));
      llvm_unreachable(ErrMsg.c_str());
    }
  }
  return Result;
}


} // namespace tooling
} // namespace clang
