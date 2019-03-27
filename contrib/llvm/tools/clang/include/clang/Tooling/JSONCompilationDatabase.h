//===- JSONCompilationDatabase.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  The JSONCompilationDatabase finds compilation databases supplied as a file
//  'compile_commands.json'.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_JSONCOMPILATIONDATABASE_H
#define LLVM_CLANG_TOOLING_JSONCOMPILATIONDATABASE_H

#include "clang/Basic/LLVM.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/FileMatchTrie.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/YAMLParser.h"
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace clang {
namespace tooling {

/// A JSON based compilation database.
///
/// JSON compilation database files must contain a list of JSON objects which
/// provide the command lines in the attributes 'directory', 'command',
/// 'arguments' and 'file':
/// [
///   { "directory": "<working directory of the compile>",
///     "command": "<compile command line>",
///     "file": "<path to source file>"
///   },
///   { "directory": "<working directory of the compile>",
///     "arguments": ["<raw>", "<command>" "<line>" "<parameters>"],
///     "file": "<path to source file>"
///   },
///   ...
/// ]
/// Each object entry defines one compile action. The specified file is
/// considered to be the main source file for the translation unit.
///
/// 'command' is a full command line that will be unescaped.
///
/// 'arguments' is a list of command line arguments that will not be unescaped.
///
/// JSON compilation databases can for example be generated in CMake projects
/// by setting the flag -DCMAKE_EXPORT_COMPILE_COMMANDS.
enum class JSONCommandLineSyntax { Windows, Gnu, AutoDetect };
class JSONCompilationDatabase : public CompilationDatabase {
public:
  /// Loads a JSON compilation database from the specified file.
  ///
  /// Returns NULL and sets ErrorMessage if the database could not be
  /// loaded from the given file.
  static std::unique_ptr<JSONCompilationDatabase>
  loadFromFile(StringRef FilePath, std::string &ErrorMessage,
               JSONCommandLineSyntax Syntax);

  /// Loads a JSON compilation database from a data buffer.
  ///
  /// Returns NULL and sets ErrorMessage if the database could not be loaded.
  static std::unique_ptr<JSONCompilationDatabase>
  loadFromBuffer(StringRef DatabaseString, std::string &ErrorMessage,
                 JSONCommandLineSyntax Syntax);

  /// Returns all compile commands in which the specified file was
  /// compiled.
  ///
  /// FIXME: Currently FilePath must be an absolute path inside the
  /// source directory which does not have symlinks resolved.
  std::vector<CompileCommand>
  getCompileCommands(StringRef FilePath) const override;

  /// Returns the list of all files available in the compilation database.
  ///
  /// These are the 'file' entries of the JSON objects.
  std::vector<std::string> getAllFiles() const override;

  /// Returns all compile commands for all the files in the compilation
  /// database.
  std::vector<CompileCommand> getAllCompileCommands() const override;

private:
  /// Constructs a JSON compilation database on a memory buffer.
  JSONCompilationDatabase(std::unique_ptr<llvm::MemoryBuffer> Database,
                          JSONCommandLineSyntax Syntax)
      : Database(std::move(Database)), Syntax(Syntax),
        YAMLStream(this->Database->getBuffer(), SM) {}

  /// Parses the database file and creates the index.
  ///
  /// Returns whether parsing succeeded. Sets ErrorMessage if parsing
  /// failed.
  bool parse(std::string &ErrorMessage);

  // Tuple (directory, filename, commandline, output) where 'commandline'
  // points to the corresponding scalar nodes in the YAML stream.
  // If the command line contains a single argument, it is a shell-escaped
  // command line.
  // Otherwise, each entry in the command line vector is a literal
  // argument to the compiler.
  // The output field may be a nullptr.
  using CompileCommandRef =
      std::tuple<llvm::yaml::ScalarNode *, llvm::yaml::ScalarNode *,
                 std::vector<llvm::yaml::ScalarNode *>,
                 llvm::yaml::ScalarNode *>;

  /// Converts the given array of CompileCommandRefs to CompileCommands.
  void getCommands(ArrayRef<CompileCommandRef> CommandsRef,
                   std::vector<CompileCommand> &Commands) const;

  // Maps file paths to the compile command lines for that file.
  llvm::StringMap<std::vector<CompileCommandRef>> IndexByFile;

  /// All the compile commands in the order that they were provided in the
  /// JSON stream.
  std::vector<CompileCommandRef> AllCommands;

  FileMatchTrie MatchTrie;

  std::unique_ptr<llvm::MemoryBuffer> Database;
  JSONCommandLineSyntax Syntax;
  llvm::SourceMgr SM;
  llvm::yaml::Stream YAMLStream;
};

} // namespace tooling
} // namespace clang

#endif // LLVM_CLANG_TOOLING_JSONCOMPILATIONDATABASE_H
