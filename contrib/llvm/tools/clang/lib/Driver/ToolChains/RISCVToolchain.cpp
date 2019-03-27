//===--- RISCVToolchain.cpp - RISCV ToolChain Implementations ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RISCVToolchain.h"
#include "CommonArgs.h"
#include "InputInfo.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

/// RISCV Toolchain
RISCVToolChain::RISCVToolChain(const Driver &D, const llvm::Triple &Triple,
                               const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  GCCInstallation.init(Triple, Args);
  getFilePaths().push_back(computeSysRoot() + "/lib");
  if (GCCInstallation.isValid()) {
    getFilePaths().push_back(GCCInstallation.getInstallPath().str());
    getProgramPaths().push_back(
        (GCCInstallation.getParentLibPath() + "/../bin").str());
  }
}

Tool *RISCVToolChain::buildLinker() const {
  return new tools::RISCV::Linker(*this);
}

void RISCVToolChain::addClangTargetOptions(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind) const {
  CC1Args.push_back("-nostdsysteminc");
  CC1Args.push_back("-fuse-init-array");
}

void RISCVToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                               ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nostdlibinc)) {
    SmallString<128> Dir(computeSysRoot());
    llvm::sys::path::append(Dir, "include");
    addSystemInclude(DriverArgs, CC1Args, Dir.str());
  }
}

void RISCVToolChain::addLibStdCxxIncludePaths(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  const GCCVersion &Version = GCCInstallation.getVersion();
  StringRef TripleStr = GCCInstallation.getTriple().str();
  const Multilib &Multilib = GCCInstallation.getMultilib();
  addLibStdCXXIncludePaths(computeSysRoot() + "/include/c++/" + Version.Text,
      "", TripleStr, "", "", Multilib.includeSuffix(), DriverArgs, CC1Args);
}

std::string RISCVToolChain::computeSysRoot() const {
  if (!getDriver().SysRoot.empty())
    return getDriver().SysRoot;

  if (!GCCInstallation.isValid())
    return std::string();

  StringRef LibDir = GCCInstallation.getParentLibPath();
  StringRef TripleStr = GCCInstallation.getTriple().str();
  std::string SysRootDir = LibDir.str() + "/../" + TripleStr.str();

  if (!llvm::sys::fs::exists(SysRootDir))
    return std::string();

  return SysRootDir;
}

void RISCV::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                 const InputInfo &Output,
                                 const InputInfoList &Inputs,
                                 const ArgList &Args,
                                 const char *LinkingOutput) const {
  const ToolChain &ToolChain = getToolChain();
  const Driver &D = ToolChain.getDriver();
  ArgStringList CmdArgs;

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  std::string Linker = getToolChain().GetProgramPath(getShortName());

  bool WantCRTs =
      !Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles);

  if (WantCRTs) {
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crt0.o")));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtbegin.o")));
  }

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  ToolChain.AddFilePathLibArgs(Args, CmdArgs);
  Args.AddAllArgs(CmdArgs,
                  {options::OPT_T_Group, options::OPT_e, options::OPT_s,
                   options::OPT_t, options::OPT_Z_Flag, options::OPT_r});

  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  // TODO: add C++ includes and libs if compiling C++.

  if (!Args.hasArg(options::OPT_nostdlib) &&
      !Args.hasArg(options::OPT_nodefaultlibs)) {
    if (ToolChain.ShouldLinkCXXStdlib(Args))
      ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);
    CmdArgs.push_back("--start-group");
    CmdArgs.push_back("-lc");
    CmdArgs.push_back("-lgloss");
    CmdArgs.push_back("--end-group");
    CmdArgs.push_back("-lgcc");
  }

  if (WantCRTs)
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtend.o")));

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());
  C.addCommand(llvm::make_unique<Command>(JA, *this, Args.MakeArgString(Linker),
                                          CmdArgs, Inputs));
}
// RISCV tools end.
