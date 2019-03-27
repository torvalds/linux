//===- Compilation.cpp - Compilation Task Implementation ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Compilation.h"
#include "clang/Basic/LLVM.h"
#include "clang/Driver/Action.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/ToolChain.h"
#include "clang/Driver/Util.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>
#include <system_error>
#include <utility>

using namespace clang;
using namespace driver;
using namespace llvm::opt;

Compilation::Compilation(const Driver &D, const ToolChain &_DefaultToolChain,
                         InputArgList *_Args, DerivedArgList *_TranslatedArgs,
                         bool ContainsError)
    : TheDriver(D), DefaultToolChain(_DefaultToolChain), Args(_Args),
      TranslatedArgs(_TranslatedArgs), ContainsError(ContainsError) {
  // The offloading host toolchain is the default toolchain.
  OrderedOffloadingToolchains.insert(
      std::make_pair(Action::OFK_Host, &DefaultToolChain));
}

Compilation::~Compilation() {
  // Remove temporary files. This must be done before arguments are freed, as
  // the file names might be derived from the input arguments.
  if (!TheDriver.isSaveTempsEnabled() && !ForceKeepTempFiles)
    CleanupFileList(TempFiles);

  delete TranslatedArgs;
  delete Args;

  // Free any derived arg lists.
  for (auto Arg : TCArgs)
    if (Arg.second != TranslatedArgs)
      delete Arg.second;
}

const DerivedArgList &
Compilation::getArgsForToolChain(const ToolChain *TC, StringRef BoundArch,
                                 Action::OffloadKind DeviceOffloadKind) {
  if (!TC)
    TC = &DefaultToolChain;

  DerivedArgList *&Entry = TCArgs[{TC, BoundArch, DeviceOffloadKind}];
  if (!Entry) {
    SmallVector<Arg *, 4> AllocatedArgs;
    DerivedArgList *OpenMPArgs = nullptr;
    // Translate OpenMP toolchain arguments provided via the -Xopenmp-target flags.
    if (DeviceOffloadKind == Action::OFK_OpenMP) {
      const ToolChain *HostTC = getSingleOffloadToolChain<Action::OFK_Host>();
      bool SameTripleAsHost = (TC->getTriple() == HostTC->getTriple());
      OpenMPArgs = TC->TranslateOpenMPTargetArgs(
          *TranslatedArgs, SameTripleAsHost, AllocatedArgs);
    }

    if (!OpenMPArgs) {
      Entry = TC->TranslateArgs(*TranslatedArgs, BoundArch, DeviceOffloadKind);
      if (!Entry)
        Entry = TranslatedArgs;
    } else {
      Entry = TC->TranslateArgs(*OpenMPArgs, BoundArch, DeviceOffloadKind);
      if (!Entry)
        Entry = OpenMPArgs;
      else
        delete OpenMPArgs;
    }

    // Add allocated arguments to the final DAL.
    for (auto ArgPtr : AllocatedArgs)
      Entry->AddSynthesizedArg(ArgPtr);
  }

  return *Entry;
}

bool Compilation::CleanupFile(const char *File, bool IssueErrors) const {
  // FIXME: Why are we trying to remove files that we have not created? For
  // example we should only try to remove a temporary assembly file if
  // "clang -cc1" succeed in writing it. Was this a workaround for when
  // clang was writing directly to a .s file and sometimes leaving it behind
  // during a failure?

  // FIXME: If this is necessary, we can still try to split
  // llvm::sys::fs::remove into a removeFile and a removeDir and avoid the
  // duplicated stat from is_regular_file.

  // Don't try to remove files which we don't have write access to (but may be
  // able to remove), or non-regular files. Underlying tools may have
  // intentionally not overwritten them.
  if (!llvm::sys::fs::can_write(File) || !llvm::sys::fs::is_regular_file(File))
    return true;

  if (std::error_code EC = llvm::sys::fs::remove(File)) {
    // Failure is only failure if the file exists and is "regular". We checked
    // for it being regular before, and llvm::sys::fs::remove ignores ENOENT,
    // so we don't need to check again.

    if (IssueErrors)
      getDriver().Diag(diag::err_drv_unable_to_remove_file)
        << EC.message();
    return false;
  }
  return true;
}

bool Compilation::CleanupFileList(const llvm::opt::ArgStringList &Files,
                                  bool IssueErrors) const {
  bool Success = true;
  for (const auto &File: Files)
    Success &= CleanupFile(File, IssueErrors);
  return Success;
}

bool Compilation::CleanupFileMap(const ArgStringMap &Files,
                                 const JobAction *JA,
                                 bool IssueErrors) const {
  bool Success = true;
  for (const auto &File : Files) {
    // If specified, only delete the files associated with the JobAction.
    // Otherwise, delete all files in the map.
    if (JA && File.first != JA)
      continue;
    Success &= CleanupFile(File.second, IssueErrors);
  }
  return Success;
}

int Compilation::ExecuteCommand(const Command &C,
                                const Command *&FailingCommand) const {
  if ((getDriver().CCPrintOptions ||
       getArgs().hasArg(options::OPT_v)) && !getDriver().CCGenDiagnostics) {
    raw_ostream *OS = &llvm::errs();

    // Follow gcc implementation of CC_PRINT_OPTIONS; we could also cache the
    // output stream.
    if (getDriver().CCPrintOptions && getDriver().CCPrintOptionsFilename) {
      std::error_code EC;
      OS = new llvm::raw_fd_ostream(getDriver().CCPrintOptionsFilename, EC,
                                    llvm::sys::fs::F_Append |
                                        llvm::sys::fs::F_Text);
      if (EC) {
        getDriver().Diag(diag::err_drv_cc_print_options_failure)
            << EC.message();
        FailingCommand = &C;
        delete OS;
        return 1;
      }
    }

    if (getDriver().CCPrintOptions)
      *OS << "[Logging clang options]";

    C.Print(*OS, "\n", /*Quote=*/getDriver().CCPrintOptions);

    if (OS != &llvm::errs())
      delete OS;
  }

  std::string Error;
  bool ExecutionFailed;
  int Res = C.Execute(Redirects, &Error, &ExecutionFailed);
  if (!Error.empty()) {
    assert(Res && "Error string set with 0 result code!");
    getDriver().Diag(diag::err_drv_command_failure) << Error;
  }

  if (Res)
    FailingCommand = &C;

  return ExecutionFailed ? 1 : Res;
}

using FailingCommandList = SmallVectorImpl<std::pair<int, const Command *>>;

static bool ActionFailed(const Action *A,
                         const FailingCommandList &FailingCommands) {
  if (FailingCommands.empty())
    return false;

  // CUDA/HIP can have the same input source code compiled multiple times so do
  // not compiled again if there are already failures. It is OK to abort the
  // CUDA pipeline on errors.
  if (A->isOffloading(Action::OFK_Cuda) || A->isOffloading(Action::OFK_HIP))
    return true;

  for (const auto &CI : FailingCommands)
    if (A == &(CI.second->getSource()))
      return true;

  for (const auto *AI : A->inputs())
    if (ActionFailed(AI, FailingCommands))
      return true;

  return false;
}

static bool InputsOk(const Command &C,
                     const FailingCommandList &FailingCommands) {
  return !ActionFailed(&C.getSource(), FailingCommands);
}

void Compilation::ExecuteJobs(const JobList &Jobs,
                              FailingCommandList &FailingCommands) const {
  // According to UNIX standard, driver need to continue compiling all the
  // inputs on the command line even one of them failed.
  // In all but CLMode, execute all the jobs unless the necessary inputs for the
  // job is missing due to previous failures.
  for (const auto &Job : Jobs) {
    if (!InputsOk(Job, FailingCommands))
      continue;
    const Command *FailingCommand = nullptr;
    if (int Res = ExecuteCommand(Job, FailingCommand)) {
      FailingCommands.push_back(std::make_pair(Res, FailingCommand));
      // Bail as soon as one command fails in cl driver mode.
      if (TheDriver.IsCLMode())
        return;
    }
  }
}

void Compilation::initCompilationForDiagnostics() {
  ForDiagnostics = true;

  // Free actions and jobs.
  Actions.clear();
  AllActions.clear();
  Jobs.clear();

  // Remove temporary files.
  if (!TheDriver.isSaveTempsEnabled() && !ForceKeepTempFiles)
    CleanupFileList(TempFiles);

  // Clear temporary/results file lists.
  TempFiles.clear();
  ResultFiles.clear();
  FailureResultFiles.clear();

  // Remove any user specified output.  Claim any unclaimed arguments, so as
  // to avoid emitting warnings about unused args.
  OptSpecifier OutputOpts[] = { options::OPT_o, options::OPT_MD,
                                options::OPT_MMD };
  for (unsigned i = 0, e = llvm::array_lengthof(OutputOpts); i != e; ++i) {
    if (TranslatedArgs->hasArg(OutputOpts[i]))
      TranslatedArgs->eraseArg(OutputOpts[i]);
  }
  TranslatedArgs->ClaimAllArgs();

  // Redirect stdout/stderr to /dev/null.
  Redirects = {None, {""}, {""}};

  // Temporary files added by diagnostics should be kept.
  ForceKeepTempFiles = true;
}

StringRef Compilation::getSysRoot() const {
  return getDriver().SysRoot;
}

void Compilation::Redirect(ArrayRef<Optional<StringRef>> Redirects) {
  this->Redirects = Redirects;
}
