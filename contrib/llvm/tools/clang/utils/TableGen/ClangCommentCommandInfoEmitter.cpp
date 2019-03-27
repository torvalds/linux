//===--- ClangCommentCommandInfoEmitter.cpp - Generate command lists -----====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits command lists and efficient matchers for command
// names that are used in documentation comments.
//
//===----------------------------------------------------------------------===//

#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringMatcher.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <vector>

using namespace llvm;

namespace clang {
void EmitClangCommentCommandInfo(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("A list of commands useable in documentation "
                       "comments", OS);

  OS << "namespace {\n"
        "const CommandInfo Commands[] = {\n";
  std::vector<Record *> Tags = Records.getAllDerivedDefinitions("Command");
  for (size_t i = 0, e = Tags.size(); i != e; ++i) {
    Record &Tag = *Tags[i];
    OS << "  { "
       << "\"" << Tag.getValueAsString("Name") << "\", "
       << "\"" << Tag.getValueAsString("EndCommandName") << "\", "
       << i << ", "
       << Tag.getValueAsInt("NumArgs") << ", "
       << Tag.getValueAsBit("IsInlineCommand") << ", "
       << Tag.getValueAsBit("IsBlockCommand") << ", "
       << Tag.getValueAsBit("IsBriefCommand") << ", "
       << Tag.getValueAsBit("IsReturnsCommand") << ", "
       << Tag.getValueAsBit("IsParamCommand") << ", "
       << Tag.getValueAsBit("IsTParamCommand") << ", "
       << Tag.getValueAsBit("IsThrowsCommand") << ", "
       << Tag.getValueAsBit("IsDeprecatedCommand") << ", "
       << Tag.getValueAsBit("IsHeaderfileCommand") << ", "
       << Tag.getValueAsBit("IsEmptyParagraphAllowed") << ", "
       << Tag.getValueAsBit("IsVerbatimBlockCommand") << ", "
       << Tag.getValueAsBit("IsVerbatimBlockEndCommand") << ", "
       << Tag.getValueAsBit("IsVerbatimLineCommand") << ", "
       << Tag.getValueAsBit("IsDeclarationCommand") << ", "
       << Tag.getValueAsBit("IsFunctionDeclarationCommand") << ", "
       << Tag.getValueAsBit("IsRecordLikeDetailCommand") << ", "
       << Tag.getValueAsBit("IsRecordLikeDeclarationCommand") << ", "
       << /* IsUnknownCommand = */ "0"
       << " }";
    if (i + 1 != e)
      OS << ",";
    OS << "\n";
  }
  OS << "};\n"
        "} // unnamed namespace\n\n";

  std::vector<StringMatcher::StringPair> Matches;
  for (size_t i = 0, e = Tags.size(); i != e; ++i) {
    Record &Tag = *Tags[i];
    std::string Name = Tag.getValueAsString("Name");
    std::string Return;
    raw_string_ostream(Return) << "return &Commands[" << i << "];";
    Matches.emplace_back(std::move(Name), std::move(Return));
  }

  OS << "const CommandInfo *CommandTraits::getBuiltinCommandInfo(\n"
     << "                                         StringRef Name) {\n";
  StringMatcher("Name", Matches, OS).Emit();
  OS << "  return nullptr;\n"
     << "}\n\n";
}

static std::string MangleName(StringRef Str) {
  std::string Mangled;
  for (unsigned i = 0, e = Str.size(); i != e; ++i) {
    switch (Str[i]) {
    default:
      Mangled += Str[i];
      break;
    case '[':
      Mangled += "lsquare";
      break;
    case ']':
      Mangled += "rsquare";
      break;
    case '{':
      Mangled += "lbrace";
      break;
    case '}':
      Mangled += "rbrace";
      break;
    case '$':
      Mangled += "dollar";
      break;
    case '/':
      Mangled += "slash";
      break;
    }
  }
  return Mangled;
}

void EmitClangCommentCommandList(RecordKeeper &Records, raw_ostream &OS) {
  emitSourceFileHeader("A list of commands useable in documentation "
                       "comments", OS);

  OS << "#ifndef COMMENT_COMMAND\n"
     << "#  define COMMENT_COMMAND(NAME)\n"
     << "#endif\n";

  std::vector<Record *> Tags = Records.getAllDerivedDefinitions("Command");
  for (size_t i = 0, e = Tags.size(); i != e; ++i) {
    Record &Tag = *Tags[i];
    std::string MangledName = MangleName(Tag.getValueAsString("Name"));

    OS << "COMMENT_COMMAND(" << MangledName << ")\n";
  }
}
} // end namespace clang
