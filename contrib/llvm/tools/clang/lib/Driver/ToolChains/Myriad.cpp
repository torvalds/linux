//===--- Myriad.cpp - Myriad ToolChain Implementations ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Myriad.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

using tools::addPathIfExists;

void tools::SHAVE::Compiler::ConstructJob(Compilation &C, const JobAction &JA,
                                          const InputInfo &Output,
                                          const InputInfoList &Inputs,
                                          const ArgList &Args,
                                          const char *LinkingOutput) const {
  ArgStringList CmdArgs;
  assert(Inputs.size() == 1);
  const InputInfo &II = Inputs[0];
  assert(II.getType() == types::TY_C || II.getType() == types::TY_CXX ||
         II.getType() == types::TY_PP_CXX);

  if (JA.getKind() == Action::PreprocessJobClass) {
    Args.ClaimAllArgs();
    CmdArgs.push_back("-E");
  } else {
    assert(Output.getType() == types::TY_PP_Asm); // Require preprocessed asm.
    CmdArgs.push_back("-S");
    CmdArgs.push_back("-fno-exceptions"); // Always do this even if unspecified.
  }
  CmdArgs.push_back("-DMYRIAD2");

  // Append all -I, -iquote, -isystem paths, defines/undefines, 'f'
  // flags, 'g' flags, 'M' flags, optimize flags, warning options,
  // mcpu flags, mllvm flags, and Xclang flags.
  // These are spelled the same way in clang and moviCompile.
  Args.AddAllArgsExcept(
      CmdArgs,
      {options::OPT_I_Group, options::OPT_clang_i_Group, options::OPT_std_EQ,
       options::OPT_D, options::OPT_U, options::OPT_f_Group,
       options::OPT_f_clang_Group, options::OPT_g_Group, options::OPT_M_Group,
       options::OPT_O_Group, options::OPT_W_Group, options::OPT_mcpu_EQ,
       options::OPT_mllvm, options::OPT_Xclang},
      {options::OPT_fno_split_dwarf_inlining});
  Args.hasArg(options::OPT_fno_split_dwarf_inlining); // Claim it if present.

  // If we're producing a dependency file, and assembly is the final action,
  // then the name of the target in the dependency file should be the '.o'
  // file, not the '.s' file produced by this step. For example, instead of
  //  /tmp/mumble.s: mumble.c .../someheader.h
  // the filename on the lefthand side should be "mumble.o"
  if (Args.getLastArg(options::OPT_MF) && !Args.getLastArg(options::OPT_MT) &&
      C.getActions().size() == 1 &&
      C.getActions()[0]->getKind() == Action::AssembleJobClass) {
    Arg *A = Args.getLastArg(options::OPT_o);
    if (A) {
      CmdArgs.push_back("-MT");
      CmdArgs.push_back(Args.MakeArgString(A->getValue()));
    }
  }

  CmdArgs.push_back(II.getFilename());
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  std::string Exec =
      Args.MakeArgString(getToolChain().GetProgramPath("moviCompile"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Args.MakeArgString(Exec),
                                          CmdArgs, Inputs));
}

void tools::SHAVE::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                           const InputInfo &Output,
                                           const InputInfoList &Inputs,
                                           const ArgList &Args,
                                           const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  assert(Inputs.size() == 1);
  const InputInfo &II = Inputs[0];
  assert(II.getType() == types::TY_PP_Asm); // Require preprocessed asm input.
  assert(Output.getType() == types::TY_Object);

  CmdArgs.push_back("-no6thSlotCompression");
  const Arg *CPUArg = Args.getLastArg(options::OPT_mcpu_EQ);
  if (CPUArg)
    CmdArgs.push_back(
        Args.MakeArgString("-cv:" + StringRef(CPUArg->getValue())));
  CmdArgs.push_back("-noSPrefixing");
  CmdArgs.push_back("-a"); // Mystery option.
  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);
  for (const Arg *A : Args.filtered(options::OPT_I, options::OPT_isystem)) {
    A->claim();
    CmdArgs.push_back(
        Args.MakeArgString(std::string("-i:") + A->getValue(0)));
  }
  CmdArgs.push_back(II.getFilename());
  CmdArgs.push_back(
      Args.MakeArgString(std::string("-o:") + Output.getFilename()));

  std::string Exec =
      Args.MakeArgString(getToolChain().GetProgramPath("moviAsm"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Args.MakeArgString(Exec),
                                          CmdArgs, Inputs));
}

void tools::Myriad::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                         const InputInfo &Output,
                                         const InputInfoList &Inputs,
                                         const ArgList &Args,
                                         const char *LinkingOutput) const {
  const auto &TC =
      static_cast<const toolchains::MyriadToolChain &>(getToolChain());
  const llvm::Triple &T = TC.getTriple();
  ArgStringList CmdArgs;
  bool UseStartfiles =
      !Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles);
  bool UseDefaultLibs =
      !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs);
  // Silence warning if the args contain both -nostdlib and -stdlib=.
  Args.getLastArg(options::OPT_stdlib_EQ);

  if (T.getArch() == llvm::Triple::sparc)
    CmdArgs.push_back("-EB");
  else // SHAVE assumes little-endian, and sparcel is expressly so.
    CmdArgs.push_back("-EL");

  // The remaining logic is mostly like gnutools::Linker::ConstructJob,
  // but we never pass through a --sysroot option and various other bits.
  // For example, there are no sanitizers (yet) nor gold linker.

  // Eat some arguments that may be present but have no effect.
  Args.ClaimAllArgs(options::OPT_g_Group);
  Args.ClaimAllArgs(options::OPT_w);
  Args.ClaimAllArgs(options::OPT_static_libgcc);

  if (Args.hasArg(options::OPT_s)) // Pass the 'strip' option.
    CmdArgs.push_back("-s");

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  if (UseStartfiles) {
    // If you want startfiles, it means you want the builtin crti and crtbegin,
    // but not crt0. Myriad link commands provide their own crt0.o as needed.
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("crti.o")));
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("crtbegin.o")));
  }

  Args.AddAllArgs(CmdArgs, {options::OPT_L, options::OPT_T_Group,
                            options::OPT_e, options::OPT_s, options::OPT_t,
                            options::OPT_Z_Flag, options::OPT_r});

  TC.AddFilePathLibArgs(Args, CmdArgs);

  bool NeedsSanitizerDeps = addSanitizerRuntimes(TC, Args, CmdArgs);
  AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);

  if (UseDefaultLibs) {
    if (NeedsSanitizerDeps)
      linkSanitizerRuntimeDeps(TC, CmdArgs);
    if (C.getDriver().CCCIsCXX()) {
      if (TC.GetCXXStdlibType(Args) == ToolChain::CST_Libcxx) {
        CmdArgs.push_back("-lc++");
        CmdArgs.push_back("-lc++abi");
      } else
        CmdArgs.push_back("-lstdc++");
    }
    if (T.getOS() == llvm::Triple::RTEMS) {
      CmdArgs.push_back("--start-group");
      CmdArgs.push_back("-lc");
      CmdArgs.push_back("-lgcc"); // circularly dependent on rtems
      // You must provide your own "-L" option to enable finding these.
      CmdArgs.push_back("-lrtemscpu");
      CmdArgs.push_back("-lrtemsbsp");
      CmdArgs.push_back("--end-group");
    } else {
      CmdArgs.push_back("-lc");
      CmdArgs.push_back("-lgcc");
    }
  }
  if (UseStartfiles) {
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("crtend.o")));
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("crtn.o")));
  }

  std::string Exec =
      Args.MakeArgString(TC.GetProgramPath("sparc-myriad-rtems-ld"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Args.MakeArgString(Exec),
                                          CmdArgs, Inputs));
}

MyriadToolChain::MyriadToolChain(const Driver &D, const llvm::Triple &Triple,
                                 const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  // If a target of 'sparc-myriad-elf' is specified to clang, it wants to use
  // 'sparc-myriad--elf' (note the unknown OS) as the canonical triple.
  // This won't work to find gcc. Instead we give the installation detector an
  // extra triple, which is preferable to further hacks of the logic that at
  // present is based solely on getArch(). In particular, it would be wrong to
  // choose the myriad installation when targeting a non-myriad sparc install.
  switch (Triple.getArch()) {
  default:
    D.Diag(clang::diag::err_target_unsupported_arch)
        << Triple.getArchName() << "myriad";
    LLVM_FALLTHROUGH;
  case llvm::Triple::shave:
    return;
  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
    GCCInstallation.init(Triple, Args, {"sparc-myriad-rtems"});
  }

  if (GCCInstallation.isValid()) {
    // This directory contains crt{i,n,begin,end}.o as well as libgcc.
    // These files are tied to a particular version of gcc.
    SmallString<128> CompilerSupportDir(GCCInstallation.getInstallPath());
    addPathIfExists(D, CompilerSupportDir, getFilePaths());
  }
  // libstd++ and libc++ must both be found in this one place.
  addPathIfExists(D, D.Dir + "/../sparc-myriad-rtems/lib", getFilePaths());
}

MyriadToolChain::~MyriadToolChain() {}

void MyriadToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                                ArgStringList &CC1Args) const {
  if (!DriverArgs.hasArg(clang::driver::options::OPT_nostdinc))
    addSystemInclude(DriverArgs, CC1Args, getDriver().SysRoot + "/include");
}

void MyriadToolChain::addLibCxxIncludePaths(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  std::string Path(getDriver().getInstalledDir());
  addSystemInclude(DriverArgs, CC1Args, Path + "/../include/c++/v1");
}

void MyriadToolChain::addLibStdCxxIncludePaths(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  StringRef LibDir = GCCInstallation.getParentLibPath();
  const GCCVersion &Version = GCCInstallation.getVersion();
  StringRef TripleStr = GCCInstallation.getTriple().str();
  const Multilib &Multilib = GCCInstallation.getMultilib();
  addLibStdCXXIncludePaths(
      LibDir.str() + "/../" + TripleStr.str() + "/include/c++/" + Version.Text,
      "", TripleStr, "", "", Multilib.includeSuffix(), DriverArgs, CC1Args);
}

// MyriadToolChain handles several triples:
//  {shave,sparc{,el}}-myriad-{rtems,unknown}-elf
Tool *MyriadToolChain::SelectTool(const JobAction &JA) const {
  // The inherited method works fine if not targeting the SHAVE.
  if (!isShaveCompilation(getTriple()))
    return ToolChain::SelectTool(JA);
  switch (JA.getKind()) {
  case Action::PreprocessJobClass:
  case Action::CompileJobClass:
    if (!Compiler)
      Compiler.reset(new tools::SHAVE::Compiler(*this));
    return Compiler.get();
  case Action::AssembleJobClass:
    if (!Assembler)
      Assembler.reset(new tools::SHAVE::Assembler(*this));
    return Assembler.get();
  default:
    return ToolChain::getTool(JA.getKind());
  }
}

Tool *MyriadToolChain::buildLinker() const {
  return new tools::Myriad::Linker(*this);
}

SanitizerMask MyriadToolChain::getSupportedSanitizers() const {
  return SanitizerKind::Address;
}
