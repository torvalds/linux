//===-- cc1gen_reproducer_main.cpp - Clang reproducer generator  ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the entry point to the clang -cc1gen-reproducer functionality, which
// generates reproducers for invocations for clang-based tools.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

namespace {

struct UnsavedFileHash {
  std::string Name;
  std::string MD5;
};

struct ClangInvocationInfo {
  std::string Toolchain;
  std::string LibclangOperation;
  std::string LibclangOptions;
  std::vector<std::string> Arguments;
  std::vector<std::string> InvocationArguments;
  std::vector<UnsavedFileHash> UnsavedFileHashes;
  bool Dump = false;
};

} // end anonymous namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(UnsavedFileHash)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<UnsavedFileHash> {
  static void mapping(IO &IO, UnsavedFileHash &Info) {
    IO.mapRequired("name", Info.Name);
    IO.mapRequired("md5", Info.MD5);
  }
};

template <> struct MappingTraits<ClangInvocationInfo> {
  static void mapping(IO &IO, ClangInvocationInfo &Info) {
    IO.mapRequired("toolchain", Info.Toolchain);
    IO.mapOptional("libclang.operation", Info.LibclangOperation);
    IO.mapOptional("libclang.opts", Info.LibclangOptions);
    IO.mapRequired("args", Info.Arguments);
    IO.mapOptional("invocation-args", Info.InvocationArguments);
    IO.mapOptional("unsaved_file_hashes", Info.UnsavedFileHashes);
  }
};

} // end namespace yaml
} // end namespace llvm

static std::string generateReproducerMetaInfo(const ClangInvocationInfo &Info) {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  OS << '{';
  bool NeedComma = false;
  auto EmitKey = [&](StringRef Key) {
    if (NeedComma)
      OS << ", ";
    NeedComma = true;
    OS << '"' << Key << "\": ";
  };
  auto EmitStringKey = [&](StringRef Key, StringRef Value) {
    if (Value.empty())
      return;
    EmitKey(Key);
    OS << '"' << Value << '"';
  };
  EmitStringKey("libclang.operation", Info.LibclangOperation);
  EmitStringKey("libclang.opts", Info.LibclangOptions);
  if (!Info.InvocationArguments.empty()) {
    EmitKey("invocation-args");
    OS << '[';
    for (const auto &Arg : llvm::enumerate(Info.InvocationArguments)) {
      if (Arg.index())
        OS << ',';
      OS << '"' << Arg.value() << '"';
    }
    OS << ']';
  }
  OS << '}';
  // FIXME: Compare unsaved file hashes and report mismatch in the reproducer.
  if (Info.Dump)
    llvm::outs() << "REPRODUCER METAINFO: " << OS.str() << "\n";
  return std::move(OS.str());
}

/// Generates a reproducer for a set of arguments from a specific invocation.
static llvm::Optional<driver::Driver::CompilationDiagnosticReport>
generateReproducerForInvocationArguments(ArrayRef<const char *> Argv,
                                         const ClangInvocationInfo &Info) {
  using namespace driver;
  auto TargetAndMode = ToolChain::getTargetAndModeFromProgramName(Argv[0]);

  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions;

  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  DiagnosticsEngine Diags(DiagID, &*DiagOpts, new IgnoringDiagConsumer());
  ProcessWarningOptions(Diags, *DiagOpts, /*ReportDiags=*/false);
  Driver TheDriver(Argv[0], llvm::sys::getDefaultTargetTriple(), Diags);
  TheDriver.setTargetAndMode(TargetAndMode);

  std::unique_ptr<Compilation> C(TheDriver.BuildCompilation(Argv));
  if (C && !C->containsError()) {
    for (const auto &J : C->getJobs()) {
      if (const Command *Cmd = dyn_cast<Command>(&J)) {
        Driver::CompilationDiagnosticReport Report;
        TheDriver.generateCompilationDiagnostics(
            *C, *Cmd, generateReproducerMetaInfo(Info), &Report);
        return Report;
      }
    }
  }

  return None;
}

std::string GetExecutablePath(const char *Argv0, bool CanonicalPrefixes);

static void printReproducerInformation(
    llvm::raw_ostream &OS, const ClangInvocationInfo &Info,
    const driver::Driver::CompilationDiagnosticReport &Report) {
  OS << "REPRODUCER:\n";
  OS << "{\n";
  OS << R"("files":[)";
  for (const auto &File : llvm::enumerate(Report.TemporaryFiles)) {
    if (File.index())
      OS << ',';
    OS << '"' << File.value() << '"';
  }
  OS << "]\n}\n";
}

int cc1gen_reproducer_main(ArrayRef<const char *> Argv, const char *Argv0,
                           void *MainAddr) {
  if (Argv.size() < 1) {
    llvm::errs() << "error: missing invocation file\n";
    return 1;
  }
  // Parse the invocation descriptor.
  StringRef Input = Argv[0];
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Buffer =
      llvm::MemoryBuffer::getFile(Input);
  if (!Buffer) {
    llvm::errs() << "error: failed to read " << Input << ": "
                 << Buffer.getError().message() << "\n";
    return 1;
  }
  llvm::yaml::Input YAML(Buffer.get()->getBuffer());
  ClangInvocationInfo InvocationInfo;
  YAML >> InvocationInfo;
  if (Argv.size() > 1 && Argv[1] == StringRef("-v"))
    InvocationInfo.Dump = true;

  // Create an invocation that will produce the reproducer.
  std::vector<const char *> DriverArgs;
  for (const auto &Arg : InvocationInfo.Arguments)
    DriverArgs.push_back(Arg.c_str());
  std::string Path = GetExecutablePath(Argv0, /*CanonicalPrefixes=*/true);
  DriverArgs[0] = Path.c_str();
  llvm::Optional<driver::Driver::CompilationDiagnosticReport> Report =
      generateReproducerForInvocationArguments(DriverArgs, InvocationInfo);

  // Emit the information about the reproduce files to stdout.
  int Result = 1;
  if (Report) {
    printReproducerInformation(llvm::outs(), InvocationInfo, *Report);
    Result = 0;
  }

  // Remove the input file.
  llvm::sys::fs::remove(Input);
  return Result;
}
