//===--- Ananas.cpp - Ananas ToolChain Implementations ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Ananas.h"
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

void ananas::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                     const InputInfo &Output,
                                     const InputInfoList &Inputs,
                                     const ArgList &Args,
                                     const char *LinkingOutput) const {
  claimNoWarnArgs(Args);
  ArgStringList CmdArgs;

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  for (const auto &II : Inputs)
    CmdArgs.push_back(II.getFilename());

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("as"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

void ananas::Linker::ConstructJob(Compilation &C, const JobAction &JA,
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

  if (Args.hasArg(options::OPT_static)) {
    CmdArgs.push_back("-Bstatic");
  } else {
    if (Args.hasArg(options::OPT_rdynamic))
      CmdArgs.push_back("-export-dynamic");
    if (Args.hasArg(options::OPT_shared)) {
      CmdArgs.push_back("-Bshareable");
    } else {
      Args.AddAllArgs(CmdArgs, options::OPT_pie);
      CmdArgs.push_back("-dynamic-linker");
      CmdArgs.push_back("/lib/ld-ananas.so");
    }
  }

  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  } else {
    assert(Output.isNothing() && "Invalid output.");
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    if (!Args.hasArg(options::OPT_shared)) {
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crt0.o")));
    }
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crti.o")));
    if (Args.hasArg(options::OPT_shared) || Args.hasArg(options::OPT_pie)) {
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtbeginS.o")));
    } else {
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtbegin.o")));
    }
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
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs))
    CmdArgs.push_back("-lc");

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    if (Args.hasArg(options::OPT_shared) || Args.hasArg(options::OPT_pie))
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtendS.o")));
    else
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtend.o")));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtn.o")));
  }

  const char *Exec = Args.MakeArgString(ToolChain.GetLinkerPath());
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

// Ananas - Ananas tool chain which can call as(1) and ld(1) directly.

Ananas::Ananas(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  getFilePaths().push_back(getDriver().SysRoot + "/usr/lib");
}

Tool *Ananas::buildAssembler() const {
  return new tools::ananas::Assembler(*this);
}

Tool *Ananas::buildLinker() const { return new tools::ananas::Linker(*this); }
