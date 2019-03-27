//===- JSONCompilationDatabase.cpp ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file contains the implementation of the JSONCompilationDatabase.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/JSONCompilationDatabase.h"
#include "clang/Basic/LLVM.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/CompilationDatabasePluginRegistry.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;
using namespace tooling;

namespace {

/// A parser for escaped strings of command line arguments.
///
/// Assumes \-escaping for quoted arguments (see the documentation of
/// unescapeCommandLine(...)).
class CommandLineArgumentParser {
 public:
  CommandLineArgumentParser(StringRef CommandLine)
      : Input(CommandLine), Position(Input.begin()-1) {}

  std::vector<std::string> parse() {
    bool HasMoreInput = true;
    while (HasMoreInput && nextNonWhitespace()) {
      std::string Argument;
      HasMoreInput = parseStringInto(Argument);
      CommandLine.push_back(Argument);
    }
    return CommandLine;
  }

 private:
  // All private methods return true if there is more input available.

  bool parseStringInto(std::string &String) {
    do {
      if (*Position == '"') {
        if (!parseDoubleQuotedStringInto(String)) return false;
      } else if (*Position == '\'') {
        if (!parseSingleQuotedStringInto(String)) return false;
      } else {
        if (!parseFreeStringInto(String)) return false;
      }
    } while (*Position != ' ');
    return true;
  }

  bool parseDoubleQuotedStringInto(std::string &String) {
    if (!next()) return false;
    while (*Position != '"') {
      if (!skipEscapeCharacter()) return false;
      String.push_back(*Position);
      if (!next()) return false;
    }
    return next();
  }

  bool parseSingleQuotedStringInto(std::string &String) {
    if (!next()) return false;
    while (*Position != '\'') {
      String.push_back(*Position);
      if (!next()) return false;
    }
    return next();
  }

  bool parseFreeStringInto(std::string &String) {
    do {
      if (!skipEscapeCharacter()) return false;
      String.push_back(*Position);
      if (!next()) return false;
    } while (*Position != ' ' && *Position != '"' && *Position != '\'');
    return true;
  }

  bool skipEscapeCharacter() {
    if (*Position == '\\') {
      return next();
    }
    return true;
  }

  bool nextNonWhitespace() {
    do {
      if (!next()) return false;
    } while (*Position == ' ');
    return true;
  }

  bool next() {
    ++Position;
    return Position != Input.end();
  }

  const StringRef Input;
  StringRef::iterator Position;
  std::vector<std::string> CommandLine;
};

std::vector<std::string> unescapeCommandLine(JSONCommandLineSyntax Syntax,
                                             StringRef EscapedCommandLine) {
  if (Syntax == JSONCommandLineSyntax::AutoDetect) {
    Syntax = JSONCommandLineSyntax::Gnu;
    llvm::Triple Triple(llvm::sys::getProcessTriple());
    if (Triple.getOS() == llvm::Triple::OSType::Win32) {
      // Assume Windows command line parsing on Win32 unless the triple
      // explicitly tells us otherwise.
      if (!Triple.hasEnvironment() ||
          Triple.getEnvironment() == llvm::Triple::EnvironmentType::MSVC)
        Syntax = JSONCommandLineSyntax::Windows;
    }
  }

  if (Syntax == JSONCommandLineSyntax::Windows) {
    llvm::BumpPtrAllocator Alloc;
    llvm::StringSaver Saver(Alloc);
    llvm::SmallVector<const char *, 64> T;
    llvm::cl::TokenizeWindowsCommandLine(EscapedCommandLine, Saver, T);
    std::vector<std::string> Result(T.begin(), T.end());
    return Result;
  }
  assert(Syntax == JSONCommandLineSyntax::Gnu);
  CommandLineArgumentParser parser(EscapedCommandLine);
  return parser.parse();
}

// This plugin locates a nearby compile_command.json file, and also infers
// compile commands for files not present in the database.
class JSONCompilationDatabasePlugin : public CompilationDatabasePlugin {
  std::unique_ptr<CompilationDatabase>
  loadFromDirectory(StringRef Directory, std::string &ErrorMessage) override {
    SmallString<1024> JSONDatabasePath(Directory);
    llvm::sys::path::append(JSONDatabasePath, "compile_commands.json");
    auto Base = JSONCompilationDatabase::loadFromFile(
        JSONDatabasePath, ErrorMessage, JSONCommandLineSyntax::AutoDetect);
    return Base ? inferMissingCompileCommands(std::move(Base)) : nullptr;
  }
};

} // namespace

// Register the JSONCompilationDatabasePlugin with the
// CompilationDatabasePluginRegistry using this statically initialized variable.
static CompilationDatabasePluginRegistry::Add<JSONCompilationDatabasePlugin>
X("json-compilation-database", "Reads JSON formatted compilation databases");

namespace clang {
namespace tooling {

// This anchor is used to force the linker to link in the generated object file
// and thus register the JSONCompilationDatabasePlugin.
volatile int JSONAnchorSource = 0;

} // namespace tooling
} // namespace clang

std::unique_ptr<JSONCompilationDatabase>
JSONCompilationDatabase::loadFromFile(StringRef FilePath,
                                      std::string &ErrorMessage,
                                      JSONCommandLineSyntax Syntax) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> DatabaseBuffer =
      llvm::MemoryBuffer::getFile(FilePath);
  if (std::error_code Result = DatabaseBuffer.getError()) {
    ErrorMessage = "Error while opening JSON database: " + Result.message();
    return nullptr;
  }
  std::unique_ptr<JSONCompilationDatabase> Database(
      new JSONCompilationDatabase(std::move(*DatabaseBuffer), Syntax));
  if (!Database->parse(ErrorMessage))
    return nullptr;
  return Database;
}

std::unique_ptr<JSONCompilationDatabase>
JSONCompilationDatabase::loadFromBuffer(StringRef DatabaseString,
                                        std::string &ErrorMessage,
                                        JSONCommandLineSyntax Syntax) {
  std::unique_ptr<llvm::MemoryBuffer> DatabaseBuffer(
      llvm::MemoryBuffer::getMemBuffer(DatabaseString));
  std::unique_ptr<JSONCompilationDatabase> Database(
      new JSONCompilationDatabase(std::move(DatabaseBuffer), Syntax));
  if (!Database->parse(ErrorMessage))
    return nullptr;
  return Database;
}

std::vector<CompileCommand>
JSONCompilationDatabase::getCompileCommands(StringRef FilePath) const {
  SmallString<128> NativeFilePath;
  llvm::sys::path::native(FilePath, NativeFilePath);

  std::string Error;
  llvm::raw_string_ostream ES(Error);
  StringRef Match = MatchTrie.findEquivalent(NativeFilePath, ES);
  if (Match.empty())
    return {};
  const auto CommandsRefI = IndexByFile.find(Match);
  if (CommandsRefI == IndexByFile.end())
    return {};
  std::vector<CompileCommand> Commands;
  getCommands(CommandsRefI->getValue(), Commands);
  return Commands;
}

std::vector<std::string>
JSONCompilationDatabase::getAllFiles() const {
  std::vector<std::string> Result;
  for (const auto &CommandRef : IndexByFile)
    Result.push_back(CommandRef.first().str());
  return Result;
}

std::vector<CompileCommand>
JSONCompilationDatabase::getAllCompileCommands() const {
  std::vector<CompileCommand> Commands;
  getCommands(AllCommands, Commands);
  return Commands;
}

static std::vector<std::string>
nodeToCommandLine(JSONCommandLineSyntax Syntax,
                  const std::vector<llvm::yaml::ScalarNode *> &Nodes) {
  SmallString<1024> Storage;
  if (Nodes.size() == 1)
    return unescapeCommandLine(Syntax, Nodes[0]->getValue(Storage));
  std::vector<std::string> Arguments;
  for (const auto *Node : Nodes)
    Arguments.push_back(Node->getValue(Storage));
  return Arguments;
}

void JSONCompilationDatabase::getCommands(
    ArrayRef<CompileCommandRef> CommandsRef,
    std::vector<CompileCommand> &Commands) const {
  for (const auto &CommandRef : CommandsRef) {
    SmallString<8> DirectoryStorage;
    SmallString<32> FilenameStorage;
    SmallString<32> OutputStorage;
    auto Output = std::get<3>(CommandRef);
    Commands.emplace_back(
        std::get<0>(CommandRef)->getValue(DirectoryStorage),
        std::get<1>(CommandRef)->getValue(FilenameStorage),
        nodeToCommandLine(Syntax, std::get<2>(CommandRef)),
        Output ? Output->getValue(OutputStorage) : "");
  }
}

bool JSONCompilationDatabase::parse(std::string &ErrorMessage) {
  llvm::yaml::document_iterator I = YAMLStream.begin();
  if (I == YAMLStream.end()) {
    ErrorMessage = "Error while parsing YAML.";
    return false;
  }
  llvm::yaml::Node *Root = I->getRoot();
  if (!Root) {
    ErrorMessage = "Error while parsing YAML.";
    return false;
  }
  auto *Array = dyn_cast<llvm::yaml::SequenceNode>(Root);
  if (!Array) {
    ErrorMessage = "Expected array.";
    return false;
  }
  for (auto &NextObject : *Array) {
    auto *Object = dyn_cast<llvm::yaml::MappingNode>(&NextObject);
    if (!Object) {
      ErrorMessage = "Expected object.";
      return false;
    }
    llvm::yaml::ScalarNode *Directory = nullptr;
    llvm::Optional<std::vector<llvm::yaml::ScalarNode *>> Command;
    llvm::yaml::ScalarNode *File = nullptr;
    llvm::yaml::ScalarNode *Output = nullptr;
    for (auto& NextKeyValue : *Object) {
      auto *KeyString = dyn_cast<llvm::yaml::ScalarNode>(NextKeyValue.getKey());
      if (!KeyString) {
        ErrorMessage = "Expected strings as key.";
        return false;
      }
      SmallString<10> KeyStorage;
      StringRef KeyValue = KeyString->getValue(KeyStorage);
      llvm::yaml::Node *Value = NextKeyValue.getValue();
      if (!Value) {
        ErrorMessage = "Expected value.";
        return false;
      }
      auto *ValueString = dyn_cast<llvm::yaml::ScalarNode>(Value);
      auto *SequenceString = dyn_cast<llvm::yaml::SequenceNode>(Value);
      if (KeyValue == "arguments" && !SequenceString) {
        ErrorMessage = "Expected sequence as value.";
        return false;
      } else if (KeyValue != "arguments" && !ValueString) {
        ErrorMessage = "Expected string as value.";
        return false;
      }
      if (KeyValue == "directory") {
        Directory = ValueString;
      } else if (KeyValue == "arguments") {
        Command = std::vector<llvm::yaml::ScalarNode *>();
        for (auto &Argument : *SequenceString) {
          auto *Scalar = dyn_cast<llvm::yaml::ScalarNode>(&Argument);
          if (!Scalar) {
            ErrorMessage = "Only strings are allowed in 'arguments'.";
            return false;
          }
          Command->push_back(Scalar);
        }
      } else if (KeyValue == "command") {
        if (!Command)
          Command = std::vector<llvm::yaml::ScalarNode *>(1, ValueString);
      } else if (KeyValue == "file") {
        File = ValueString;
      } else if (KeyValue == "output") {
        Output = ValueString;
      } else {
        ErrorMessage = ("Unknown key: \"" +
                        KeyString->getRawValue() + "\"").str();
        return false;
      }
    }
    if (!File) {
      ErrorMessage = "Missing key: \"file\".";
      return false;
    }
    if (!Command) {
      ErrorMessage = "Missing key: \"command\" or \"arguments\".";
      return false;
    }
    if (!Directory) {
      ErrorMessage = "Missing key: \"directory\".";
      return false;
    }
    SmallString<8> FileStorage;
    StringRef FileName = File->getValue(FileStorage);
    SmallString<128> NativeFilePath;
    if (llvm::sys::path::is_relative(FileName)) {
      SmallString<8> DirectoryStorage;
      SmallString<128> AbsolutePath(
          Directory->getValue(DirectoryStorage));
      llvm::sys::path::append(AbsolutePath, FileName);
      llvm::sys::path::native(AbsolutePath, NativeFilePath);
    } else {
      llvm::sys::path::native(FileName, NativeFilePath);
    }
    auto Cmd = CompileCommandRef(Directory, File, *Command, Output);
    IndexByFile[NativeFilePath].push_back(Cmd);
    AllCommands.push_back(Cmd);
    MatchTrie.insert(NativeFilePath);
  }
  return true;
}
