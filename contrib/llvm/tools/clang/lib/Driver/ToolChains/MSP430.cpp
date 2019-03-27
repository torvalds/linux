//===--- MSP430.cpp - MSP430 Helpers for Tools ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MSP430.h"
#include "CommonArgs.h"
#include "Gnu.h"
#include "InputInfo.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Multilib.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

static bool isSupportedMCU(const StringRef MCU) {
  return llvm::StringSwitch<bool>(MCU)
#define MSP430_MCU(NAME) .Case(NAME, true)
#include "clang/Basic/MSP430Target.def"
      .Default(false);
}

static StringRef getSupportedHWMult(const Arg *MCU) {
  if (!MCU)
    return "none";

  return llvm::StringSwitch<StringRef>(MCU->getValue())
#define MSP430_MCU_FEAT(NAME, HWMULT) .Case(NAME, HWMULT)
#include "clang/Basic/MSP430Target.def"
      .Default("none");
}

static StringRef getHWMultLib(const ArgList &Args) {
  StringRef HWMult = Args.getLastArgValue(options::OPT_mhwmult_EQ, "auto");
  if (HWMult == "auto") {
    HWMult = getSupportedHWMult(Args.getLastArg(options::OPT_mmcu_EQ));
  }

  return llvm::StringSwitch<StringRef>(HWMult)
      .Case("16bit", "-lmul_16")
      .Case("32bit", "-lmul_32")
      .Case("f5series", "-lmul_f5")
      .Default("-lmul_none");
}

void msp430::getMSP430TargetFeatures(const Driver &D, const ArgList &Args,
                                     std::vector<StringRef> &Features) {
  const Arg *MCU = Args.getLastArg(options::OPT_mmcu_EQ);
  if (MCU && !isSupportedMCU(MCU->getValue())) {
    D.Diag(diag::err_drv_clang_unsupported) << MCU->getValue();
    return;
  }

  const Arg *HWMultArg = Args.getLastArg(options::OPT_mhwmult_EQ);
  if (!MCU && !HWMultArg)
    return;

  StringRef HWMult = HWMultArg ? HWMultArg->getValue() : "auto";
  StringRef SupportedHWMult = getSupportedHWMult(MCU);

  if (HWMult == "auto") {
    // 'auto' - deduce hw multiplier support based on mcu name provided.
    // If no mcu name is provided, assume no hw multiplier is supported.
    if (!MCU)
      D.Diag(clang::diag::warn_drv_msp430_hwmult_no_device);
    HWMult = SupportedHWMult;
  }

  if (HWMult == "none") {
    // 'none' - disable hw multiplier.
    Features.push_back("-hwmult16");
    Features.push_back("-hwmult32");
    Features.push_back("-hwmultf5");
    return;
  }

  if (MCU && SupportedHWMult == "none")
    D.Diag(clang::diag::warn_drv_msp430_hwmult_unsupported) << HWMult;
  if (MCU && HWMult != SupportedHWMult)
    D.Diag(clang::diag::warn_drv_msp430_hwmult_mismatch)
        << SupportedHWMult << HWMult;

  if (HWMult == "16bit") {
    // '16bit' - for 16-bit only hw multiplier.
    Features.push_back("+hwmult16");
  } else if (HWMult == "32bit") {
    // '32bit' - for 16/32-bit hw multiplier.
    Features.push_back("+hwmult32");
  } else if (HWMult == "f5series") {
    // 'f5series' - for 16/32-bit hw multiplier supported by F5 series mcus.
    Features.push_back("+hwmultf5");
  } else {
    D.Diag(clang::diag::err_drv_unsupported_option_argument)
        << HWMultArg->getAsString(Args) << HWMult;
  }
}

/// MSP430 Toolchain
MSP430ToolChain::MSP430ToolChain(const Driver &D, const llvm::Triple &Triple,
                                 const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {

  StringRef MultilibSuf = "";

  GCCInstallation.init(Triple, Args);
  if (GCCInstallation.isValid()) {
    MultilibSuf = GCCInstallation.getMultilib().gccSuffix();

    SmallString<128> GCCBinPath;
    llvm::sys::path::append(GCCBinPath,
                            GCCInstallation.getParentLibPath(), "..", "bin");
    addPathIfExists(D, GCCBinPath, getProgramPaths());

    SmallString<128> GCCRtPath;
    llvm::sys::path::append(GCCRtPath,
                            GCCInstallation.getInstallPath(), MultilibSuf);
    addPathIfExists(D, GCCRtPath, getFilePaths());
  }

  SmallString<128> SysRootDir(computeSysRoot());
  llvm::sys::path::append(SysRootDir, "lib", MultilibSuf);
  addPathIfExists(D, SysRootDir, getFilePaths());
}

std::string MSP430ToolChain::computeSysRoot() const {
  if (!getDriver().SysRoot.empty())
    return getDriver().SysRoot;

  SmallString<128> Dir;
  if (GCCInstallation.isValid())
    llvm::sys::path::append(Dir, GCCInstallation.getParentLibPath(), "..",
                            GCCInstallation.getTriple().str());
  else
    llvm::sys::path::append(Dir, getDriver().Dir, "..", getTriple().str());

  return Dir.str();
}

void MSP430ToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                                ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc) ||
      DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  SmallString<128> Dir(computeSysRoot());
  llvm::sys::path::append(Dir, "include");
  addSystemInclude(DriverArgs, CC1Args, Dir.str());
}

void MSP430ToolChain::addClangTargetOptions(const ArgList &DriverArgs,
                                            ArgStringList &CC1Args,
                                            Action::OffloadKind) const {
  CC1Args.push_back("-nostdsysteminc");

  const auto *MCUArg = DriverArgs.getLastArg(options::OPT_mmcu_EQ);
  if (!MCUArg)
    return;

  const StringRef MCU = MCUArg->getValue();
  if (MCU.startswith("msp430i")) {
    // 'i' should be in lower case as it's defined in TI MSP430-GCC headers
    CC1Args.push_back(DriverArgs.MakeArgString(
        "-D__MSP430i" + MCU.drop_front(7).upper() + "__"));
  } else {
    CC1Args.push_back(DriverArgs.MakeArgString("-D__" + MCU.upper() + "__"));
  }
}

Tool *MSP430ToolChain::buildLinker() const {
  return new tools::msp430::Linker(*this);
}

void msp430::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  const ToolChain &ToolChain = getToolChain();
  const Driver &D = ToolChain.getDriver();
  std::string Linker = ToolChain.GetProgramPath(getShortName());
  ArgStringList CmdArgs;

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  if (!Args.hasArg(options::OPT_T)) {
    if (const Arg *MCUArg = Args.getLastArg(options::OPT_mmcu_EQ))
      CmdArgs.push_back(
          Args.MakeArgString("-T" + StringRef(MCUArg->getValue()) + ".ld"));
  } else {
    Args.AddAllArgs(CmdArgs, options::OPT_T);
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crt0.o")));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtbegin.o")));
  }

  AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);

  CmdArgs.push_back("--start-group");
  CmdArgs.push_back(Args.MakeArgString(getHWMultLib(Args)));
  CmdArgs.push_back("-lgcc");
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    CmdArgs.push_back("-lc");
    CmdArgs.push_back("-lcrt");
    CmdArgs.push_back("-lnosys");
  }
  CmdArgs.push_back("--end-group");

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtend.o")));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtn.o")));
  }
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());
  C.addCommand(llvm::make_unique<Command>(JA, *this, Args.MakeArgString(Linker),
                                          CmdArgs, Inputs));
}
