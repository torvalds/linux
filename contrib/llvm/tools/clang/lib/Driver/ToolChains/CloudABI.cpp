//===--- CloudABI.cpp - CloudABI ToolChain Implementations ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CloudABI.h"
#include "InputInfo.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

void cloudabi::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                    const InputInfo &Output,
                                    const InputInfoList &Inputs,
                                    const ArgList &Args,
                                    const char *LinkingOutput) const {
  const ToolChain &ToolChain = getToolChain();
  const Driver &D = ToolChain.getDriver();
  ArgStringList CmdArgs;

  // Silence warning for "clang -g foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_g_Group);
  // and "clang -emit-llvm foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  // and for "clang -w foo.o -o foo". Other warning options are already
  // handled somewhere else.
  Args.ClaimAllArgs(options::OPT_w);

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  // CloudABI only supports static linkage.
  CmdArgs.push_back("-Bstatic");
  CmdArgs.push_back("--no-dynamic-linker");

  // Provide PIE linker flags in case PIE is default for the architecture.
  if (ToolChain.isPIEDefault()) {
    CmdArgs.push_back("-pie");
    CmdArgs.push_back("-zrelro");
  }

  CmdArgs.push_back("--eh-frame-hdr");
  CmdArgs.push_back("--gc-sections");

  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  } else {
    assert(Output.isNothing() && "Invalid output.");
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crt0.o")));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtbegin.o")));
  }

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  ToolChain.AddFilePathLibArgs(Args, CmdArgs);
  Args.AddAllArgs(CmdArgs,
                  {options::OPT_T_Group, options::OPT_e, options::OPT_s,
                   options::OPT_t, options::OPT_Z_Flag, options::OPT_r});

  if (D.isUsingLTO()) {
    assert(!Inputs.empty() && "Must have at least one input.");
    AddGoldPlugin(ToolChain, Args, CmdArgs, Output, Inputs[0],
                  D.getLTOMode() == LTOK_Thin);
  }

  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  if (ToolChain.ShouldLinkCXXStdlib(Args))
    ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    CmdArgs.push_back("-lc");
    CmdArgs.push_back("-lcompiler_rt");
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles))
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtend.o")));

  const char *Exec = Args.MakeArgString(ToolChain.GetLinkerPath());
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

// CloudABI - CloudABI tool chain which can call ld(1) directly.

CloudABI::CloudABI(const Driver &D, const llvm::Triple &Triple,
                   const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  SmallString<128> P(getDriver().Dir);
  llvm::sys::path::append(P, "..", getTriple().str(), "lib");
  getFilePaths().push_back(P.str());
}

void CloudABI::addLibCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                     llvm::opt::ArgStringList &CC1Args) const {
  SmallString<128> P(getDriver().Dir);
  llvm::sys::path::append(P, "..", getTriple().str(), "include/c++/v1");
  addSystemInclude(DriverArgs, CC1Args, P.str());
}

void CloudABI::AddCXXStdlibLibArgs(const ArgList &Args,
                                   ArgStringList &CmdArgs) const {
  CmdArgs.push_back("-lc++");
  CmdArgs.push_back("-lc++abi");
  CmdArgs.push_back("-lunwind");
}

Tool *CloudABI::buildLinker() const {
  return new tools::cloudabi::Linker(*this);
}

bool CloudABI::isPIEDefault() const {
  // Only enable PIE on architectures that support PC-relative
  // addressing. PC-relative addressing is required, as the process
  // startup code must be able to relocate itself.
  switch (getTriple().getArch()) {
  case llvm::Triple::aarch64:
  case llvm::Triple::x86_64:
    return true;
  default:
    return false;
  }
}

SanitizerMask CloudABI::getSupportedSanitizers() const {
  SanitizerMask Res = ToolChain::getSupportedSanitizers();
  Res |= SanitizerKind::SafeStack;
  return Res;
}

SanitizerMask CloudABI::getDefaultSanitizers() const {
  return SanitizerKind::SafeStack;
}
