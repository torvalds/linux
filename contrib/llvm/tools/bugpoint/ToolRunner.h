//===-- tools/bugpoint/ToolRunner.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file exposes an abstraction around a platform C compiler, used to
// compile C and assembly code.  It also exposes an "AbstractIntepreter"
// interface, which is used to execute code using one of the LLVM execution
// engines.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_BUGPOINT_TOOLRUNNER_H
#define LLVM_TOOLS_BUGPOINT_TOOLRUNNER_H

#include "llvm/ADT/Triple.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SystemUtils.h"
#include <exception>
#include <vector>

namespace llvm {

extern cl::opt<bool> SaveTemps;
extern Triple TargetTriple;

class LLC;

//===---------------------------------------------------------------------===//
// CC abstraction
//
class CC {
  std::string CCPath;              // The path to the cc executable.
  std::string RemoteClientPath;    // The path to the rsh / ssh executable.
  std::vector<std::string> ccArgs; // CC-specific arguments.
  CC(StringRef ccPath, StringRef RemotePath,
     const std::vector<std::string> *CCArgs)
      : CCPath(ccPath), RemoteClientPath(RemotePath) {
    if (CCArgs)
      ccArgs = *CCArgs;
  }

public:
  enum FileType { AsmFile, ObjectFile, CFile };

  static CC *create(const char *Argv0, std::string &Message,
                    const std::string &CCBinary,
                    const std::vector<std::string> *Args);

  /// ExecuteProgram - Execute the program specified by "ProgramFile" (which is
  /// either a .s file, or a .c file, specified by FileType), with the specified
  /// arguments.  Standard input is specified with InputFile, and standard
  /// Output is captured to the specified OutputFile location.  The SharedLibs
  /// option specifies optional native shared objects that can be loaded into
  /// the program for execution.
  ///
  Expected<int> ExecuteProgram(
      const std::string &ProgramFile, const std::vector<std::string> &Args,
      FileType fileType, const std::string &InputFile,
      const std::string &OutputFile,
      const std::vector<std::string> &CCArgs = std::vector<std::string>(),
      unsigned Timeout = 0, unsigned MemoryLimit = 0);

  /// MakeSharedObject - This compiles the specified file (which is either a .c
  /// file or a .s file) into a shared object.
  ///
  Error MakeSharedObject(const std::string &InputFile, FileType fileType,
                         std::string &OutputFile,
                         const std::vector<std::string> &ArgsForCC);
};

//===---------------------------------------------------------------------===//
/// AbstractInterpreter Class - Subclasses of this class are used to execute
/// LLVM bitcode in a variety of ways.  This abstract interface hides this
/// complexity behind a simple interface.
///
class AbstractInterpreter {
  virtual void anchor();

public:
  static LLC *createLLC(const char *Argv0, std::string &Message,
                        const std::string &CCBinary,
                        const std::vector<std::string> *Args = nullptr,
                        const std::vector<std::string> *CCArgs = nullptr,
                        bool UseIntegratedAssembler = false);

  static AbstractInterpreter *
  createLLI(const char *Argv0, std::string &Message,
            const std::vector<std::string> *Args = nullptr);

  static AbstractInterpreter *
  createJIT(const char *Argv0, std::string &Message,
            const std::vector<std::string> *Args = nullptr);

  static AbstractInterpreter *
  createCustomCompiler(const char *Argv0, std::string &Message,
                       const std::string &CompileCommandLine);

  static AbstractInterpreter *
  createCustomExecutor(const char *Argv0, std::string &Message,
                       const std::string &ExecCommandLine);

  virtual ~AbstractInterpreter() {}

  /// compileProgram - Compile the specified program from bitcode to executable
  /// code.  This does not produce any output, it is only used when debugging
  /// the code generator.  It returns false if the code generator fails.
  virtual Error compileProgram(const std::string &Bitcode, unsigned Timeout = 0,
                               unsigned MemoryLimit = 0) {
    return Error::success();
  }

  /// Compile the specified program from bitcode to code understood by the CC
  /// driver (either C or asm).  Returns an error if the code generator fails,,
  /// otherwise, the type of code emitted.
  virtual Expected<CC::FileType> OutputCode(const std::string &Bitcode,
                                            std::string &OutFile,
                                            unsigned Timeout = 0,
                                            unsigned MemoryLimit = 0) {
    return make_error<StringError>(
        "OutputCode not supported by this AbstractInterpreter!",
        inconvertibleErrorCode());
  }

  /// ExecuteProgram - Run the specified bitcode file, emitting output to the
  /// specified filename.  This sets RetVal to the exit code of the program or
  /// returns an Error if a problem was encountered that prevented execution of
  /// the program.
  ///
  virtual Expected<int> ExecuteProgram(
      const std::string &Bitcode, const std::vector<std::string> &Args,
      const std::string &InputFile, const std::string &OutputFile,
      const std::vector<std::string> &CCArgs = std::vector<std::string>(),
      const std::vector<std::string> &SharedLibs = std::vector<std::string>(),
      unsigned Timeout = 0, unsigned MemoryLimit = 0) = 0;
};

//===---------------------------------------------------------------------===//
// LLC Implementation of AbstractIntepreter interface
//
class LLC : public AbstractInterpreter {
  std::string LLCPath;               // The path to the LLC executable.
  std::vector<std::string> ToolArgs; // Extra args to pass to LLC.
  CC *cc;
  bool UseIntegratedAssembler;

public:
  LLC(const std::string &llcPath, CC *cc, const std::vector<std::string> *Args,
      bool useIntegratedAssembler)
      : LLCPath(llcPath), cc(cc),
        UseIntegratedAssembler(useIntegratedAssembler) {
    ToolArgs.clear();
    if (Args)
      ToolArgs = *Args;
  }
  ~LLC() override { delete cc; }

  /// compileProgram - Compile the specified program from bitcode to executable
  /// code.  This does not produce any output, it is only used when debugging
  /// the code generator.  Returns false if the code generator fails.
  Error compileProgram(const std::string &Bitcode, unsigned Timeout = 0,
                       unsigned MemoryLimit = 0) override;

  Expected<int> ExecuteProgram(
      const std::string &Bitcode, const std::vector<std::string> &Args,
      const std::string &InputFile, const std::string &OutputFile,
      const std::vector<std::string> &CCArgs = std::vector<std::string>(),
      const std::vector<std::string> &SharedLibs = std::vector<std::string>(),
      unsigned Timeout = 0, unsigned MemoryLimit = 0) override;

  Expected<CC::FileType> OutputCode(const std::string &Bitcode,
                                    std::string &OutFile, unsigned Timeout = 0,
                                    unsigned MemoryLimit = 0) override;
};

/// Find the first executable file \ExeName, either in the user's PATH or,
/// failing that, in the same directory as argv[0]. This allows us to find
/// another LLVM tool if it is built in the same directory. If no executable is
/// found, an error is returned.
ErrorOr<std::string> FindProgramByName(const std::string &ExeName,
                                       const char *Argv0, void *MainAddr);

} // End llvm namespace

#endif
