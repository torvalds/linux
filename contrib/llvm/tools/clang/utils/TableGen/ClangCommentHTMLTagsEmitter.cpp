//===--- ClangCommentHTMLTagsEmitter.cpp - Generate HTML tag list for Clang -=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits efficient matchers for HTML tags that are used
// in documentation comments.
//
//===----------------------------------------------------------------------===//

#include "TableGenBackends.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringMatcher.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <vector>

using namespace llvm;

void clang::EmitClangCommentHTMLTags(RecordKeeper &Records, raw_ostream &OS) {
  std::vector<Record *> Tags = Records.getAllDerivedDefinitions("Tag");
  std::vector<StringMatcher::StringPair> Matches;
  for (Record *Tag : Tags) {
    Matches.emplace_back(Tag->getValueAsString("Spelling"), "return true;");
  }

  emitSourceFileHeader("HTML tag name matcher", OS);

  OS << "bool isHTMLTagName(StringRef Name) {\n";
  StringMatcher("Name", Matches, OS).Emit();
  OS << "  return false;\n"
     << "}\n\n";
}

void clang::EmitClangCommentHTMLTagsProperties(RecordKeeper &Records,
                                               raw_ostream &OS) {
  std::vector<Record *> Tags = Records.getAllDerivedDefinitions("Tag");
  std::vector<StringMatcher::StringPair> MatchesEndTagOptional;
  std::vector<StringMatcher::StringPair> MatchesEndTagForbidden;
  for (Record *Tag : Tags) {
    std::string Spelling = Tag->getValueAsString("Spelling");
    StringMatcher::StringPair Match(Spelling, "return true;");
    if (Tag->getValueAsBit("EndTagOptional"))
      MatchesEndTagOptional.push_back(Match);
    if (Tag->getValueAsBit("EndTagForbidden"))
      MatchesEndTagForbidden.push_back(Match);
  }

  emitSourceFileHeader("HTML tag properties", OS);

  OS << "bool isHTMLEndTagOptional(StringRef Name) {\n";
  StringMatcher("Name", MatchesEndTagOptional, OS).Emit();
  OS << "  return false;\n"
     << "}\n\n";

  OS << "bool isHTMLEndTagForbidden(StringRef Name) {\n";
  StringMatcher("Name", MatchesEndTagForbidden, OS).Emit();
  OS << "  return false;\n"
     << "}\n\n";
}

