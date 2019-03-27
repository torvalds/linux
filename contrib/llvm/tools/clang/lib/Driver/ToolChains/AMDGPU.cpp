//===--- AMDGPU.cpp - AMDGPU ToolChain Implementations ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "CommonArgs.h"
#include "InputInfo.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "llvm/Option/ArgList.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

void amdgpu::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {

  std::string Linker = getToolChain().GetProgramPath(getShortName());
  ArgStringList CmdArgs;
  AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);
  CmdArgs.push_back("-shared");
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());
  C.addCommand(llvm::make_unique<Command>(JA, *this, Args.MakeArgString(Linker),
                                          CmdArgs, Inputs));
}

void amdgpu::getAMDGPUTargetFeatures(const Driver &D,
                                     const llvm::opt::ArgList &Args,
                                     std::vector<StringRef> &Features) {
  if (const Arg *dAbi = Args.getLastArg(options::OPT_mamdgpu_debugger_abi)) {
    StringRef value = dAbi->getValue();
    if (value == "1.0") {
      Features.push_back("+amdgpu-debugger-insert-nops");
      Features.push_back("+amdgpu-debugger-emit-prologue");
    } else {
      D.Diag(diag::err_drv_clang_unsupported) << dAbi->getAsString(Args);
    }
  }

  handleTargetFeaturesGroup(
    Args, Features, options::OPT_m_amdgpu_Features_Group);
}

/// AMDGPU Toolchain
AMDGPUToolChain::AMDGPUToolChain(const Driver &D, const llvm::Triple &Triple,
                                 const ArgList &Args)
    : Generic_ELF(D, Triple, Args),
      OptionsDefault({{options::OPT_O, "3"},
                      {options::OPT_cl_std_EQ, "CL1.2"}}) {}

Tool *AMDGPUToolChain::buildLinker() const {
  return new tools::amdgpu::Linker(*this);
}

DerivedArgList *
AMDGPUToolChain::TranslateArgs(const DerivedArgList &Args, StringRef BoundArch,
                               Action::OffloadKind DeviceOffloadKind) const {

  DerivedArgList *DAL =
      Generic_ELF::TranslateArgs(Args, BoundArch, DeviceOffloadKind);

  // Do nothing if not OpenCL (-x cl)
  if (!Args.getLastArgValue(options::OPT_x).equals("cl"))
    return DAL;

  if (!DAL)
    DAL = new DerivedArgList(Args.getBaseArgs());
  for (auto *A : Args)
    DAL->append(A);

  const OptTable &Opts = getDriver().getOpts();

  // Phase 1 (.cl -> .bc)
  if (Args.hasArg(options::OPT_c) && Args.hasArg(options::OPT_emit_llvm)) {
    DAL->AddFlagArg(nullptr, Opts.getOption(getTriple().isArch64Bit()
                                                ? options::OPT_m64
                                                : options::OPT_m32));

    // Have to check OPT_O4, OPT_O0 & OPT_Ofast separately
    // as they defined that way in Options.td
    if (!Args.hasArg(options::OPT_O, options::OPT_O0, options::OPT_O4,
                     options::OPT_Ofast))
      DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_O),
                        getOptionDefault(options::OPT_O));
  }

  return DAL;
}

void AMDGPUToolChain::addClangTargetOptions(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadingKind) const {
  // Default to "hidden" visibility, as object level linking will not be
  // supported for the foreseeable future.
  if (!DriverArgs.hasArg(options::OPT_fvisibility_EQ,
                         options::OPT_fvisibility_ms_compat)) {
    CC1Args.push_back("-fvisibility");
    CC1Args.push_back("hidden");
  }
}
