//===--- OpenBSD.cpp - OpenBSD ToolChain Implementations --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "OpenBSD.h"
#include "Arch/Mips.h"
#include "Arch/Sparc.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "llvm/Option/ArgList.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

void openbsd::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                      const InputInfo &Output,
                                      const InputInfoList &Inputs,
                                      const ArgList &Args,
                                      const char *LinkingOutput) const {
  claimNoWarnArgs(Args);
  ArgStringList CmdArgs;

  switch (getToolChain().getArch()) {
  case llvm::Triple::x86:
    // When building 32-bit code on OpenBSD/amd64, we have to explicitly
    // instruct as in the base system to assemble 32-bit code.
    CmdArgs.push_back("--32");
    break;

  case llvm::Triple::ppc:
    CmdArgs.push_back("-mppc");
    CmdArgs.push_back("-many");
    break;

  case llvm::Triple::sparc:
  case llvm::Triple::sparcel: {
    CmdArgs.push_back("-32");
    std::string CPU = getCPUName(Args, getToolChain().getTriple());
    CmdArgs.push_back(sparc::getSparcAsmModeForCPU(CPU, getToolChain().getTriple()));
    AddAssemblerKPIC(getToolChain(), Args, CmdArgs);
    break;
  }

  case llvm::Triple::sparcv9: {
    CmdArgs.push_back("-64");
    std::string CPU = getCPUName(Args, getToolChain().getTriple());
    CmdArgs.push_back(sparc::getSparcAsmModeForCPU(CPU, getToolChain().getTriple()));
    AddAssemblerKPIC(getToolChain(), Args, CmdArgs);
    break;
  }

  case llvm::Triple::mips64:
  case llvm::Triple::mips64el: {
    StringRef CPUName;
    StringRef ABIName;
    mips::getMipsCPUAndABI(Args, getToolChain().getTriple(), CPUName, ABIName);

    CmdArgs.push_back("-mabi");
    CmdArgs.push_back(mips::getGnuCompatibleMipsABIName(ABIName).data());

    if (getToolChain().getTriple().isLittleEndian())
      CmdArgs.push_back("-EL");
    else
      CmdArgs.push_back("-EB");

    AddAssemblerKPIC(getToolChain(), Args, CmdArgs);
    break;
  }

  default:
    break;
  }

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  for (const auto &II : Inputs)
    CmdArgs.push_back(II.getFilename());

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("as"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

void openbsd::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                   const InputInfo &Output,
                                   const InputInfoList &Inputs,
                                   const ArgList &Args,
                                   const char *LinkingOutput) const {
  const toolchains::OpenBSD &ToolChain =
      static_cast<const toolchains::OpenBSD &>(getToolChain());
  const Driver &D = getToolChain().getDriver();
  ArgStringList CmdArgs;

  // Silence warning for "clang -g foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_g_Group);
  // and "clang -emit-llvm foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  // and for "clang -w foo.o -o foo". Other warning options are already
  // handled somewhere else.
  Args.ClaimAllArgs(options::OPT_w);

  if (ToolChain.getArch() == llvm::Triple::mips64)
    CmdArgs.push_back("-EB");
  else if (ToolChain.getArch() == llvm::Triple::mips64el)
    CmdArgs.push_back("-EL");

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_shared)) {
    CmdArgs.push_back("-e");
    CmdArgs.push_back("__start");
  }

  CmdArgs.push_back("--eh-frame-hdr");
  if (Args.hasArg(options::OPT_static)) {
    CmdArgs.push_back("-Bstatic");
  } else {
    if (Args.hasArg(options::OPT_rdynamic))
      CmdArgs.push_back("-export-dynamic");
    CmdArgs.push_back("-Bdynamic");
    if (Args.hasArg(options::OPT_shared)) {
      CmdArgs.push_back("-shared");
    } else {
      CmdArgs.push_back("-dynamic-linker");
      CmdArgs.push_back("/usr/libexec/ld.so");
    }
  }

  if (Args.hasArg(options::OPT_pie))
    CmdArgs.push_back("-pie");
  if (Args.hasArg(options::OPT_nopie) || Args.hasArg(options::OPT_pg))
    CmdArgs.push_back("-nopie");

  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  } else {
    assert(Output.isNothing() && "Invalid output.");
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    const char *crt0 = nullptr;
    const char *crtbegin = nullptr;
    if (!Args.hasArg(options::OPT_shared)) {
      if (Args.hasArg(options::OPT_pg))
        crt0 = "gcrt0.o";
      else if (Args.hasArg(options::OPT_static) &&
               !Args.hasArg(options::OPT_nopie))
        crt0 = "rcrt0.o";
      else
        crt0 = "crt0.o";
      crtbegin = "crtbegin.o";
    } else {
      crtbegin = "crtbeginS.o";
    }

    if (crt0)
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crt0)));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtbegin)));
  }

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  ToolChain.AddFilePathLibArgs(Args, CmdArgs);
  Args.AddAllArgs(CmdArgs, {options::OPT_T_Group, options::OPT_e,
                            options::OPT_s, options::OPT_t,
                            options::OPT_Z_Flag, options::OPT_r});

  bool NeedsSanitizerDeps = addSanitizerRuntimes(ToolChain, Args, CmdArgs);
  bool NeedsXRayDeps = addXRayRuntime(ToolChain, Args, CmdArgs);
  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    if (D.CCCIsCXX()) {
      if (ToolChain.ShouldLinkCXXStdlib(Args))
        ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);
      if (Args.hasArg(options::OPT_pg))
        CmdArgs.push_back("-lm_p");
      else
        CmdArgs.push_back("-lm");
    }
    if (NeedsSanitizerDeps) {
      CmdArgs.push_back(ToolChain.getCompilerRTArgString(Args, "builtins", false));
      linkSanitizerRuntimeDeps(ToolChain, CmdArgs);
    }
    if (NeedsXRayDeps) {
      CmdArgs.push_back(ToolChain.getCompilerRTArgString(Args, "builtins", false));
      linkXRayRuntimeDeps(ToolChain, CmdArgs);
    }
    // FIXME: For some reason GCC passes -lgcc before adding
    // the default system libraries. Just mimic this for now.
    CmdArgs.push_back("-lcompiler_rt");

    if (Args.hasArg(options::OPT_pthread)) {
      if (!Args.hasArg(options::OPT_shared) && Args.hasArg(options::OPT_pg))
        CmdArgs.push_back("-lpthread_p");
      else
        CmdArgs.push_back("-lpthread");
    }

    if (!Args.hasArg(options::OPT_shared)) {
      if (Args.hasArg(options::OPT_pg))
        CmdArgs.push_back("-lc_p");
      else
        CmdArgs.push_back("-lc");
    }

    CmdArgs.push_back("-lcompiler_rt");
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    const char *crtend = nullptr;
    if (!Args.hasArg(options::OPT_shared))
      crtend = "crtend.o";
    else
      crtend = "crtendS.o";

    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtend)));
  }

  const char *Exec = Args.MakeArgString(ToolChain.GetLinkerPath());
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

SanitizerMask OpenBSD::getSupportedSanitizers() const {
  const bool IsX86 = getTriple().getArch() == llvm::Triple::x86;
  const bool IsX86_64 = getTriple().getArch() == llvm::Triple::x86_64;

  // For future use, only UBsan at the moment
  SanitizerMask Res = ToolChain::getSupportedSanitizers();

  if (IsX86 || IsX86_64) {
    Res |= SanitizerKind::Vptr;
    Res |= SanitizerKind::Fuzzer;
    Res |= SanitizerKind::FuzzerNoLink;
  }

  return Res;
}

/// OpenBSD - OpenBSD tool chain which can call as(1) and ld(1) directly.

OpenBSD::OpenBSD(const Driver &D, const llvm::Triple &Triple,
                 const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  getFilePaths().push_back(getDriver().SysRoot + "/usr/lib");
}

void OpenBSD::AddCXXStdlibLibArgs(const ArgList &Args,
                                  ArgStringList &CmdArgs) const {
  bool Profiling = Args.hasArg(options::OPT_pg);

  CmdArgs.push_back(Profiling ? "-lc++_p" : "-lc++");
  CmdArgs.push_back(Profiling ? "-lc++abi_p" : "-lc++abi");
}

Tool *OpenBSD::buildAssembler() const {
  return new tools::openbsd::Assembler(*this);
}

Tool *OpenBSD::buildLinker() const { return new tools::openbsd::Linker(*this); }
