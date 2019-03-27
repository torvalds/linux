//===- FuzzerCommand.h - Interface representing a process -------*- C++ -* ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// FuzzerCommand represents a command to run in a subprocess.  It allows callers
// to manage command line arguments and output and error streams.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_COMMAND_H
#define LLVM_FUZZER_COMMAND_H

#include "FuzzerDefs.h"
#include "FuzzerIO.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace fuzzer {

class Command final {
public:
  // This command line flag is used to indicate that the remaining command line
  // is immutable, meaning this flag effectively marks the end of the mutable
  // argument list.
  static inline const char *ignoreRemainingArgs() {
    return "-ignore_remaining_args=1";
  }

  Command() : CombinedOutAndErr(false) {}

  explicit Command(const Vector<std::string> &ArgsToAdd)
      : Args(ArgsToAdd), CombinedOutAndErr(false) {}

  explicit Command(const Command &Other)
      : Args(Other.Args), CombinedOutAndErr(Other.CombinedOutAndErr),
        OutputFile(Other.OutputFile) {}

  Command &operator=(const Command &Other) {
    Args = Other.Args;
    CombinedOutAndErr = Other.CombinedOutAndErr;
    OutputFile = Other.OutputFile;
    return *this;
  }

  ~Command() {}

  // Returns true if the given Arg is present in Args.  Only checks up to
  // "-ignore_remaining_args=1".
  bool hasArgument(const std::string &Arg) const {
    auto i = endMutableArgs();
    return std::find(Args.begin(), i, Arg) != i;
  }

  // Gets all of the current command line arguments, **including** those after
  // "-ignore-remaining-args=1".
  const Vector<std::string> &getArguments() const { return Args; }

  // Adds the given argument before "-ignore_remaining_args=1", or at the end
  // if that flag isn't present.
  void addArgument(const std::string &Arg) {
    Args.insert(endMutableArgs(), Arg);
  }

  // Adds all given arguments before "-ignore_remaining_args=1", or at the end
  // if that flag isn't present.
  void addArguments(const Vector<std::string> &ArgsToAdd) {
    Args.insert(endMutableArgs(), ArgsToAdd.begin(), ArgsToAdd.end());
  }

  // Removes the given argument from the command argument list.  Ignores any
  // occurrences after "-ignore_remaining_args=1", if present.
  void removeArgument(const std::string &Arg) {
    auto i = endMutableArgs();
    Args.erase(std::remove(Args.begin(), i, Arg), i);
  }

  // Like hasArgument, but checks for "-[Flag]=...".
  bool hasFlag(const std::string &Flag) const {
    std::string Arg("-" + Flag + "=");
    auto IsMatch = [&](const std::string &Other) {
      return Arg.compare(0, std::string::npos, Other, 0, Arg.length()) == 0;
    };
    return std::any_of(Args.begin(), endMutableArgs(), IsMatch);
  }

  // Returns the value of the first instance of a given flag, or an empty string
  // if the flag isn't present.  Ignores any occurrences after
  // "-ignore_remaining_args=1", if present.
  std::string getFlagValue(const std::string &Flag) const {
    std::string Arg("-" + Flag + "=");
    auto IsMatch = [&](const std::string &Other) {
      return Arg.compare(0, std::string::npos, Other, 0, Arg.length()) == 0;
    };
    auto i = endMutableArgs();
    auto j = std::find_if(Args.begin(), i, IsMatch);
    std::string result;
    if (j != i) {
      result = j->substr(Arg.length());
    }
    return result;
  }

  // Like AddArgument, but adds "-[Flag]=[Value]".
  void addFlag(const std::string &Flag, const std::string &Value) {
    addArgument("-" + Flag + "=" + Value);
  }

  // Like RemoveArgument, but removes "-[Flag]=...".
  void removeFlag(const std::string &Flag) {
    std::string Arg("-" + Flag + "=");
    auto IsMatch = [&](const std::string &Other) {
      return Arg.compare(0, std::string::npos, Other, 0, Arg.length()) == 0;
    };
    auto i = endMutableArgs();
    Args.erase(std::remove_if(Args.begin(), i, IsMatch), i);
  }

  // Returns whether the command's stdout is being written to an output file.
  bool hasOutputFile() const { return !OutputFile.empty(); }

  // Returns the currently set output file.
  const std::string &getOutputFile() const { return OutputFile; }

  // Configures the command to redirect its output to the name file.
  void setOutputFile(const std::string &FileName) { OutputFile = FileName; }

  // Returns whether the command's stderr is redirected to stdout.
  bool isOutAndErrCombined() const { return CombinedOutAndErr; }

  // Sets whether to redirect the command's stderr to its stdout.
  void combineOutAndErr(bool combine = true) { CombinedOutAndErr = combine; }

  // Returns a string representation of the command.  On many systems this will
  // be the equivalent command line.
  std::string toString() const {
    std::stringstream SS;
    for (auto arg : getArguments())
      SS << arg << " ";
    if (hasOutputFile())
      SS << ">" << getOutputFile() << " ";
    if (isOutAndErrCombined())
      SS << "2>&1 ";
    std::string result = SS.str();
    if (!result.empty())
      result = result.substr(0, result.length() - 1);
    return result;
  }

private:
  Command(Command &&Other) = delete;
  Command &operator=(Command &&Other) = delete;

  Vector<std::string>::iterator endMutableArgs() {
    return std::find(Args.begin(), Args.end(), ignoreRemainingArgs());
  }

  Vector<std::string>::const_iterator endMutableArgs() const {
    return std::find(Args.begin(), Args.end(), ignoreRemainingArgs());
  }

  // The command arguments.  Args[0] is the command name.
  Vector<std::string> Args;

  // True indicates stderr is redirected to stdout.
  bool CombinedOutAndErr;

  // If not empty, stdout is redirected to the named file.
  std::string OutputFile;
};

} // namespace fuzzer

#endif // LLVM_FUZZER_COMMAND_H
