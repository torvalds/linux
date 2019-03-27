//===- CompilationDatabase.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file provides an interface and multiple implementations for
//  CompilationDatabases.
//
//  While C++ refactoring and analysis tools are not compilers, and thus
//  don't run as part of the build system, they need the exact information
//  of a build in order to be able to correctly understand the C++ code of
//  the project. This information is provided via the CompilationDatabase
//  interface.
//
//  To create a CompilationDatabase from a build directory one can call
//  CompilationDatabase::loadFromDirectory(), which deduces the correct
//  compilation database from the root of the build tree.
//
//  See the concrete subclasses of CompilationDatabase for currently supported
//  formats.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_COMPILATIONDATABASE_H
#define LLVM_CLANG_TOOLING_COMPILATIONDATABASE_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace clang {
namespace tooling {

/// Specifies the working directory and command of a compilation.
struct CompileCommand {
  CompileCommand() = default;
  CompileCommand(Twine Directory, Twine Filename,
                 std::vector<std::string> CommandLine, Twine Output)
      : Directory(Directory.str()), Filename(Filename.str()),
        CommandLine(std::move(CommandLine)), Output(Output.str()){}

  /// The working directory the command was executed from.
  std::string Directory;

  /// The source file associated with the command.
  std::string Filename;

  /// The command line that was executed.
  std::vector<std::string> CommandLine;

  /// The output file associated with the command.
  std::string Output;

  friend bool operator==(const CompileCommand &LHS, const CompileCommand &RHS) {
    return LHS.Directory == RHS.Directory && LHS.Filename == RHS.Filename &&
           LHS.CommandLine == RHS.CommandLine && LHS.Output == RHS.Output;
  }

  friend bool operator!=(const CompileCommand &LHS, const CompileCommand &RHS) {
    return !(LHS == RHS);
  }
};

/// Interface for compilation databases.
///
/// A compilation database allows the user to retrieve compile command lines
/// for the files in a project.
///
/// Many implementations are enumerable, allowing all command lines to be
/// retrieved. These can be used to run clang tools over a subset of the files
/// in a project.
class CompilationDatabase {
public:
  virtual ~CompilationDatabase();

  /// Loads a compilation database from a build directory.
  ///
  /// Looks at the specified 'BuildDirectory' and creates a compilation database
  /// that allows to query compile commands for source files in the
  /// corresponding source tree.
  ///
  /// Returns NULL and sets ErrorMessage if we were not able to build up a
  /// compilation database for the build directory.
  ///
  /// FIXME: Currently only supports JSON compilation databases, which
  /// are named 'compile_commands.json' in the given directory. Extend this
  /// for other build types (like ninja build files).
  static std::unique_ptr<CompilationDatabase>
  loadFromDirectory(StringRef BuildDirectory, std::string &ErrorMessage);

  /// Tries to detect a compilation database location and load it.
  ///
  /// Looks for a compilation database in all parent paths of file 'SourceFile'
  /// by calling loadFromDirectory.
  static std::unique_ptr<CompilationDatabase>
  autoDetectFromSource(StringRef SourceFile, std::string &ErrorMessage);

  /// Tries to detect a compilation database location and load it.
  ///
  /// Looks for a compilation database in directory 'SourceDir' and all
  /// its parent paths by calling loadFromDirectory.
  static std::unique_ptr<CompilationDatabase>
  autoDetectFromDirectory(StringRef SourceDir, std::string &ErrorMessage);

  /// Returns all compile commands in which the specified file was
  /// compiled.
  ///
  /// This includes compile commands that span multiple source files.
  /// For example, consider a project with the following compilations:
  /// $ clang++ -o test a.cc b.cc t.cc
  /// $ clang++ -o production a.cc b.cc -DPRODUCTION
  /// A compilation database representing the project would return both command
  /// lines for a.cc and b.cc and only the first command line for t.cc.
  virtual std::vector<CompileCommand> getCompileCommands(
      StringRef FilePath) const = 0;

  /// Returns the list of all files available in the compilation database.
  ///
  /// By default, returns nothing. Implementations should override this if they
  /// can enumerate their source files.
  virtual std::vector<std::string> getAllFiles() const { return {}; }

  /// Returns all compile commands for all the files in the compilation
  /// database.
  ///
  /// FIXME: Add a layer in Tooling that provides an interface to run a tool
  /// over all files in a compilation database. Not all build systems have the
  /// ability to provide a feasible implementation for \c getAllCompileCommands.
  ///
  /// By default, this is implemented in terms of getAllFiles() and
  /// getCompileCommands(). Subclasses may override this for efficiency.
  virtual std::vector<CompileCommand> getAllCompileCommands() const;
};

/// A compilation database that returns a single compile command line.
///
/// Useful when we want a tool to behave more like a compiler invocation.
/// This compilation database is not enumerable: getAllFiles() returns {}.
class FixedCompilationDatabase : public CompilationDatabase {
public:
  /// Creates a FixedCompilationDatabase from the arguments after "--".
  ///
  /// Parses the given command line for "--". If "--" is found, the rest of
  /// the arguments will make up the command line in the returned
  /// FixedCompilationDatabase.
  /// The arguments after "--" must not include positional parameters or the
  /// argv[0] of the tool. Those will be added by the FixedCompilationDatabase
  /// when a CompileCommand is requested. The argv[0] of the returned command
  /// line will be "clang-tool".
  ///
  /// Returns NULL in case "--" is not found.
  ///
  /// The argument list is meant to be compatible with normal llvm command line
  /// parsing in main methods.
  /// int main(int argc, char **argv) {
  ///   std::unique_ptr<FixedCompilationDatabase> Compilations(
  ///     FixedCompilationDatabase::loadFromCommandLine(argc, argv));
  ///   cl::ParseCommandLineOptions(argc, argv);
  ///   ...
  /// }
  ///
  /// \param Argc The number of command line arguments - will be changed to
  /// the number of arguments before "--", if "--" was found in the argument
  /// list.
  /// \param Argv Points to the command line arguments.
  /// \param ErrorMsg Contains error text if the function returns null pointer.
  /// \param Directory The base directory used in the FixedCompilationDatabase.
  static std::unique_ptr<FixedCompilationDatabase> loadFromCommandLine(
      int &Argc, const char *const *Argv, std::string &ErrorMsg,
      Twine Directory = ".");

  /// Reads flags from the given file, one-per line.
  /// Returns nullptr and sets ErrorMessage if we can't read the file.
  static std::unique_ptr<FixedCompilationDatabase>
  loadFromFile(StringRef Path, std::string &ErrorMsg);

  /// Constructs a compilation data base from a specified directory
  /// and command line.
  FixedCompilationDatabase(Twine Directory, ArrayRef<std::string> CommandLine);

  /// Returns the given compile command.
  ///
  /// Will always return a vector with one entry that contains the directory
  /// and command line specified at construction with "clang-tool" as argv[0]
  /// and 'FilePath' as positional argument.
  std::vector<CompileCommand>
  getCompileCommands(StringRef FilePath) const override;

private:
  /// This is built up to contain a single entry vector to be returned from
  /// getCompileCommands after adding the positional argument.
  std::vector<CompileCommand> CompileCommands;
};

/// Returns a wrapped CompilationDatabase that defers to the provided one,
/// but getCompileCommands() will infer commands for unknown files.
/// The return value of getAllFiles() or getAllCompileCommands() is unchanged.
/// See InterpolatingCompilationDatabase.cpp for details on heuristics.
std::unique_ptr<CompilationDatabase>
    inferMissingCompileCommands(std::unique_ptr<CompilationDatabase>);

} // namespace tooling
} // namespace clang

#endif // LLVM_CLANG_TOOLING_COMPILATIONDATABASE_H
