//===-- FuzzerCLI.cpp -----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/FuzzMutate/FuzzerCLI.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Verifier.h"

using namespace llvm;

void llvm::parseFuzzerCLOpts(int ArgC, char *ArgV[]) {
  std::vector<const char *> CLArgs;
  CLArgs.push_back(ArgV[0]);

  int I = 1;
  while (I < ArgC)
    if (StringRef(ArgV[I++]).equals("-ignore_remaining_args=1"))
      break;
  while (I < ArgC)
    CLArgs.push_back(ArgV[I++]);

  cl::ParseCommandLineOptions(CLArgs.size(), CLArgs.data());
}

void llvm::handleExecNameEncodedBEOpts(StringRef ExecName) {
  std::vector<std::string> Args{ExecName};

  auto NameAndArgs = ExecName.split("--");
  if (NameAndArgs.second.empty())
    return;

  SmallVector<StringRef, 4> Opts;
  NameAndArgs.second.split(Opts, '-');
  for (StringRef Opt : Opts) {
    if (Opt.equals("gisel")) {
      Args.push_back("-global-isel");
      // For now we default GlobalISel to -O0
      Args.push_back("-O0");
    } else if (Opt.startswith("O")) {
      Args.push_back("-" + Opt.str());
    } else if (Triple(Opt).getArch()) {
      Args.push_back("-mtriple=" + Opt.str());
    } else {
      errs() << ExecName << ": Unknown option: " << Opt << ".\n";
      exit(1);
    }
  }
  errs() << NameAndArgs.first << ": Injected args:";
  for (int I = 1, E = Args.size(); I < E; ++I)
    errs() << " " << Args[I];
  errs() << "\n";

  std::vector<const char *> CLArgs;
  CLArgs.reserve(Args.size());
  for (std::string &S : Args)
    CLArgs.push_back(S.c_str());

  cl::ParseCommandLineOptions(CLArgs.size(), CLArgs.data());
}

void llvm::handleExecNameEncodedOptimizerOpts(StringRef ExecName) {
  // TODO: Refactor parts common with the 'handleExecNameEncodedBEOpts'
  std::vector<std::string> Args{ExecName};

  auto NameAndArgs = ExecName.split("--");
  if (NameAndArgs.second.empty())
    return;

  SmallVector<StringRef, 4> Opts;
  NameAndArgs.second.split(Opts, '-');
  for (StringRef Opt : Opts) {
    if (Opt == "instcombine") {
      Args.push_back("-passes=instcombine");
    } else if (Opt == "earlycse") {
      Args.push_back("-passes=early-cse");
    } else if (Opt == "simplifycfg") {
      Args.push_back("-passes=simplify-cfg");
    } else if (Opt == "gvn") {
      Args.push_back("-passes=gvn");
    } else if (Opt == "sccp") {
      Args.push_back("-passes=sccp");

    } else if (Opt == "loop_predication") {
      Args.push_back("-passes=loop-predication");
    } else if (Opt == "guard_widening") {
      Args.push_back("-passes=guard-widening");
    } else if (Opt == "loop_rotate") {
      Args.push_back("-passes=loop(rotate)");
    } else if (Opt == "loop_unswitch") {
      Args.push_back("-passes=loop(unswitch)");
    } else if (Opt == "loop_unroll") {
      Args.push_back("-passes=unroll");
    } else if (Opt == "loop_vectorize") {
      Args.push_back("-passes=loop-vectorize");
    } else if (Opt == "licm") {
      Args.push_back("-passes=licm");
    } else if (Opt == "indvars") {
      Args.push_back("-passes=indvars");
    } else if (Opt == "strength_reduce") {
      Args.push_back("-passes=strength-reduce");
    } else if (Opt == "irce") {
      Args.push_back("-passes=irce");

    } else if (Triple(Opt).getArch()) {
      Args.push_back("-mtriple=" + Opt.str());
    } else {
      errs() << ExecName << ": Unknown option: " << Opt << ".\n";
      exit(1);
    }
  }

  errs() << NameAndArgs.first << ": Injected args:";
  for (int I = 1, E = Args.size(); I < E; ++I)
    errs() << " " << Args[I];
  errs() << "\n";

  std::vector<const char *> CLArgs;
  CLArgs.reserve(Args.size());
  for (std::string &S : Args)
    CLArgs.push_back(S.c_str());

  cl::ParseCommandLineOptions(CLArgs.size(), CLArgs.data());
}

int llvm::runFuzzerOnInputs(int ArgC, char *ArgV[], FuzzerTestFun TestOne,
                            FuzzerInitFun Init) {
  errs() << "*** This tool was not linked to libFuzzer.\n"
         << "*** No fuzzing will be performed.\n";
  if (int RC = Init(&ArgC, &ArgV)) {
    errs() << "Initialization failed\n";
    return RC;
  }

  for (int I = 1; I < ArgC; ++I) {
    StringRef Arg(ArgV[I]);
    if (Arg.startswith("-")) {
      if (Arg.equals("-ignore_remaining_args=1"))
        break;
      continue;
    }

    auto BufOrErr = MemoryBuffer::getFile(Arg, /*FileSize-*/ -1,
                                          /*RequiresNullTerminator=*/false);
    if (std::error_code EC = BufOrErr.getError()) {
      errs() << "Error reading file: " << Arg << ": " << EC.message() << "\n";
      return 1;
    }
    std::unique_ptr<MemoryBuffer> Buf = std::move(BufOrErr.get());
    errs() << "Running: " << Arg << " (" << Buf->getBufferSize() << " bytes)\n";
    TestOne(reinterpret_cast<const uint8_t *>(Buf->getBufferStart()),
            Buf->getBufferSize());
  }
  return 0;
}

std::unique_ptr<Module> llvm::parseModule(
    const uint8_t *Data, size_t Size, LLVMContext &Context) {

  if (Size <= 1)
    // We get bogus data given an empty corpus - just create a new module.
    return llvm::make_unique<Module>("M", Context);

  auto Buffer = MemoryBuffer::getMemBuffer(
      StringRef(reinterpret_cast<const char *>(Data), Size), "Fuzzer input",
      /*RequiresNullTerminator=*/false);

  SMDiagnostic Err;
  auto M = parseBitcodeFile(Buffer->getMemBufferRef(), Context);
  if (Error E = M.takeError()) {
    errs() << toString(std::move(E)) << "\n";
    return nullptr;
  }
  return std::move(M.get());
}

size_t llvm::writeModule(const Module &M, uint8_t *Dest, size_t MaxSize) {
  std::string Buf;
  {
    raw_string_ostream OS(Buf);
    WriteBitcodeToFile(M, OS);
  }
  if (Buf.size() > MaxSize)
      return 0;
  memcpy(Dest, Buf.data(), Buf.size());
  return Buf.size();
}

std::unique_ptr<Module> llvm::parseAndVerify(const uint8_t *Data, size_t Size,
                                             LLVMContext &Context) {
  auto M = parseModule(Data, Size, Context);
  if (!M || verifyModule(*M, &errs()))
    return nullptr;

  return M;
}
