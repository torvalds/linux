//===--- DragonFly.cpp - DragonFly ToolChain Implementations ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DragonFly.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

/// DragonFly Tools

// For now, DragonFly Assemble does just about the same as for
// FreeBSD, but this may change soon.
void dragonfly::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                        const InputInfo &Output,
                                        const InputInfoList &Inputs,
                                        const ArgList &Args,
                                        const char *LinkingOutput) const {
  claimNoWarnArgs(Args);
  ArgStringList CmdArgs;

  // When building 32-bit code on DragonFly/pc64, we have to explicitly
  // instruct as in the base system to assemble 32-bit code.
  if (getToolChain().getArch() == llvm::Triple::x86)
    CmdArgs.push_back("--32");

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  for (const auto &II : Inputs)
    CmdArgs.push_back(II.getFilename());

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("as"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

void dragonfly::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                     const InputInfo &Output,
                                     const InputInfoList &Inputs,
                                     const ArgList &Args,
                                     const char *LinkingOutput) const {
  const Driver &D = getToolChain().getDriver();
  ArgStringList CmdArgs;

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  CmdArgs.push_back("--eh-frame-hdr");
  if (Args.hasArg(options::OPT_static)) {
    CmdArgs.push_back("-Bstatic");
  } else {
    if (Args.hasArg(options::OPT_rdynamic))
      CmdArgs.push_back("-export-dynamic");
    if (Args.hasArg(options::OPT_shared))
      CmdArgs.push_back("-Bshareable");
    else {
      CmdArgs.push_back("-dynamic-linker");
      CmdArgs.push_back("/usr/libexec/ld-elf.so.2");
    }
    CmdArgs.push_back("--hash-style=gnu");
    CmdArgs.push_back("--enable-new-dtags");
  }

  // When building 32-bit code on DragonFly/pc64, we have to explicitly
  // instruct ld in the base system to link 32-bit code.
  if (getToolChain().getArch() == llvm::Triple::x86) {
    CmdArgs.push_back("-m");
    CmdArgs.push_back("elf_i386");
  }

  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  } else {
    assert(Output.isNothing() && "Invalid output.");
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    if (!Args.hasArg(options::OPT_shared)) {
      if (Args.hasArg(options::OPT_pg))
        CmdArgs.push_back(
            Args.MakeArgString(getToolChain().GetFilePath("gcrt1.o")));
      else {
        if (Args.hasArg(options::OPT_pie))
          CmdArgs.push_back(
              Args.MakeArgString(getToolChain().GetFilePath("Scrt1.o")));
        else
          CmdArgs.push_back(
              Args.MakeArgString(getToolChain().GetFilePath("crt1.o")));
      }
    }
    CmdArgs.push_back(Args.MakeArgString(getToolChain().GetFilePath("crti.o")));
    if (Args.hasArg(options::OPT_shared) || Args.hasArg(options::OPT_pie))
      CmdArgs.push_back(
          Args.MakeArgString(getToolChain().GetFilePath("crtbeginS.o")));
    else
      CmdArgs.push_back(
          Args.MakeArgString(getToolChain().GetFilePath("crtbegin.o")));
  }

  Args.AddAllArgs(CmdArgs,
                  {options::OPT_L, options::OPT_T_Group, options::OPT_e});

  AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    CmdArgs.push_back("-L/usr/lib/gcc50");

    if (!Args.hasArg(options::OPT_static)) {
      CmdArgs.push_back("-rpath");
      CmdArgs.push_back("/usr/lib/gcc50");
    }

    if (D.CCCIsCXX()) {
      if (getToolChain().ShouldLinkCXXStdlib(Args))
        getToolChain().AddCXXStdlibLibArgs(Args, CmdArgs);
      CmdArgs.push_back("-lm");
    }

    if (Args.hasArg(options::OPT_pthread))
      CmdArgs.push_back("-lpthread");

    if (!Args.hasArg(options::OPT_nolibc)) {
      CmdArgs.push_back("-lc");
    }

    if (Args.hasArg(options::OPT_static) ||
        Args.hasArg(options::OPT_static_libgcc)) {
        CmdArgs.push_back("-lgcc");
        CmdArgs.push_back("-lgcc_eh");
    } else {
      if (Args.hasArg(options::OPT_shared_libgcc)) {
          CmdArgs.push_back("-lgcc_pic");
          if (!Args.hasArg(options::OPT_shared))
            CmdArgs.push_back("-lgcc");
      } else {
          CmdArgs.push_back("-lgcc");
          CmdArgs.push_back("--as-needed");
          CmdArgs.push_back("-lgcc_pic");
          CmdArgs.push_back("--no-as-needed");
      }
    }
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    if (Args.hasArg(options::OPT_shared) || Args.hasArg(options::OPT_pie))
      CmdArgs.push_back(
          Args.MakeArgString(getToolChain().GetFilePath("crtendS.o")));
    else
      CmdArgs.push_back(
          Args.MakeArgString(getToolChain().GetFilePath("crtend.o")));
    CmdArgs.push_back(Args.MakeArgString(getToolChain().GetFilePath("crtn.o")));
  }

  getToolChain().addProfileRTLibs(Args, CmdArgs);

  const char *Exec = Args.MakeArgString(getToolChain().GetLinkerPath());
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

/// DragonFly - DragonFly tool chain which can call as(1) and ld(1) directly.

DragonFly::DragonFly(const Driver &D, const llvm::Triple &Triple,
                     const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {

  // Path mangling to find libexec
  getProgramPaths().push_back(getDriver().getInstalledDir());
  if (getDriver().getInstalledDir() != getDriver().Dir)
    getProgramPaths().push_back(getDriver().Dir);

  getFilePaths().push_back(getDriver().Dir + "/../lib");
  getFilePaths().push_back("/usr/lib");
  getFilePaths().push_back("/usr/lib/gcc50");
}

Tool *DragonFly::buildAssembler() const {
  return new tools::dragonfly::Assembler(*this);
}

Tool *DragonFly::buildLinker() const {
  return new tools::dragonfly::Linker(*this);
}
