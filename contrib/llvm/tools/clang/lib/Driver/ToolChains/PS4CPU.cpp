//===--- PS4CPU.cpp - PS4CPU ToolChain Implementations ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PS4CPU.h"
#include "FreeBSD.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <cstdlib> // ::getenv

using namespace clang::driver;
using namespace clang;
using namespace llvm::opt;

using clang::driver::tools::AddLinkerInputs;

void tools::PS4cpu::addProfileRTArgs(const ToolChain &TC, const ArgList &Args,
                                     ArgStringList &CmdArgs) {
  if ((Args.hasFlag(options::OPT_fprofile_arcs, options::OPT_fno_profile_arcs,
                    false) ||
       Args.hasFlag(options::OPT_fprofile_generate,
                    options::OPT_fno_profile_instr_generate, false) ||
       Args.hasFlag(options::OPT_fprofile_generate_EQ,
                    options::OPT_fno_profile_instr_generate, false) ||
       Args.hasFlag(options::OPT_fprofile_instr_generate,
                    options::OPT_fno_profile_instr_generate, false) ||
       Args.hasFlag(options::OPT_fprofile_instr_generate_EQ,
                    options::OPT_fno_profile_instr_generate, false) ||
       Args.hasArg(options::OPT_fcreate_profile) ||
       Args.hasArg(options::OPT_coverage)))
    CmdArgs.push_back("--dependent-lib=libclang_rt.profile-x86_64.a");
}

void tools::PS4cpu::Assemble::ConstructJob(Compilation &C, const JobAction &JA,
                                           const InputInfo &Output,
                                           const InputInfoList &Inputs,
                                           const ArgList &Args,
                                           const char *LinkingOutput) const {
  claimNoWarnArgs(Args);
  ArgStringList CmdArgs;

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  assert(Inputs.size() == 1 && "Unexpected number of inputs.");
  const InputInfo &Input = Inputs[0];
  assert(Input.isFilename() && "Invalid input.");
  CmdArgs.push_back(Input.getFilename());

  const char *Exec =
      Args.MakeArgString(getToolChain().GetProgramPath("orbis-as"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

static void AddPS4SanitizerArgs(const ToolChain &TC, ArgStringList &CmdArgs) {
  const SanitizerArgs &SanArgs = TC.getSanitizerArgs();
  if (SanArgs.needsUbsanRt()) {
    CmdArgs.push_back("-lSceDbgUBSanitizer_stub_weak");
  }
  if (SanArgs.needsAsanRt()) {
    CmdArgs.push_back("-lSceDbgAddressSanitizer_stub_weak");
  }
}

void tools::PS4cpu::addSanitizerArgs(const ToolChain &TC,
                                     ArgStringList &CmdArgs) {
  const SanitizerArgs &SanArgs = TC.getSanitizerArgs();
  if (SanArgs.needsUbsanRt())
    CmdArgs.push_back("--dependent-lib=libSceDbgUBSanitizer_stub_weak.a");
  if (SanArgs.needsAsanRt())
    CmdArgs.push_back("--dependent-lib=libSceDbgAddressSanitizer_stub_weak.a");
}

static void ConstructPS4LinkJob(const Tool &T, Compilation &C,
                                const JobAction &JA, const InputInfo &Output,
                                const InputInfoList &Inputs,
                                const ArgList &Args,
                                const char *LinkingOutput) {
  const toolchains::FreeBSD &ToolChain =
      static_cast<const toolchains::FreeBSD &>(T.getToolChain());
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

  if (Args.hasArg(options::OPT_pie))
    CmdArgs.push_back("-pie");

  if (Args.hasArg(options::OPT_rdynamic))
    CmdArgs.push_back("-export-dynamic");
  if (Args.hasArg(options::OPT_shared))
    CmdArgs.push_back("--oformat=so");

  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  } else {
    assert(Output.isNothing() && "Invalid output.");
  }

  if(!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs))
    AddPS4SanitizerArgs(ToolChain, CmdArgs);

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  Args.AddAllArgs(CmdArgs, options::OPT_T_Group);
  Args.AddAllArgs(CmdArgs, options::OPT_e);
  Args.AddAllArgs(CmdArgs, options::OPT_s);
  Args.AddAllArgs(CmdArgs, options::OPT_t);
  Args.AddAllArgs(CmdArgs, options::OPT_r);

  if (Args.hasArg(options::OPT_Z_Xlinker__no_demangle))
    CmdArgs.push_back("--no-demangle");

  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  if (Args.hasArg(options::OPT_pthread)) {
    CmdArgs.push_back("-lpthread");
  }

  const char *Exec = Args.MakeArgString(ToolChain.GetProgramPath("orbis-ld"));

  C.addCommand(llvm::make_unique<Command>(JA, T, Exec, CmdArgs, Inputs));
}

static void ConstructGoldLinkJob(const Tool &T, Compilation &C,
                                 const JobAction &JA, const InputInfo &Output,
                                 const InputInfoList &Inputs,
                                 const ArgList &Args,
                                 const char *LinkingOutput) {
  const toolchains::FreeBSD &ToolChain =
      static_cast<const toolchains::FreeBSD &>(T.getToolChain());
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

  if (Args.hasArg(options::OPT_pie))
    CmdArgs.push_back("-pie");

  if (Args.hasArg(options::OPT_static)) {
    CmdArgs.push_back("-Bstatic");
  } else {
    if (Args.hasArg(options::OPT_rdynamic))
      CmdArgs.push_back("-export-dynamic");
    CmdArgs.push_back("--eh-frame-hdr");
    if (Args.hasArg(options::OPT_shared)) {
      CmdArgs.push_back("-Bshareable");
    } else {
      CmdArgs.push_back("-dynamic-linker");
      CmdArgs.push_back("/libexec/ld-elf.so.1");
    }
    CmdArgs.push_back("--enable-new-dtags");
  }

  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  } else {
    assert(Output.isNothing() && "Invalid output.");
  }

  if(!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs))
    AddPS4SanitizerArgs(ToolChain, CmdArgs);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    const char *crt1 = nullptr;
    if (!Args.hasArg(options::OPT_shared)) {
      if (Args.hasArg(options::OPT_pg))
        crt1 = "gcrt1.o";
      else if (Args.hasArg(options::OPT_pie))
        crt1 = "Scrt1.o";
      else
        crt1 = "crt1.o";
    }
    if (crt1)
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crt1)));

    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crti.o")));

    const char *crtbegin = nullptr;
    if (Args.hasArg(options::OPT_static))
      crtbegin = "crtbeginT.o";
    else if (Args.hasArg(options::OPT_shared) || Args.hasArg(options::OPT_pie))
      crtbegin = "crtbeginS.o";
    else
      crtbegin = "crtbegin.o";

    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtbegin)));
  }

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  ToolChain.AddFilePathLibArgs(Args, CmdArgs);
  Args.AddAllArgs(CmdArgs, options::OPT_T_Group);
  Args.AddAllArgs(CmdArgs, options::OPT_e);
  Args.AddAllArgs(CmdArgs, options::OPT_s);
  Args.AddAllArgs(CmdArgs, options::OPT_t);
  Args.AddAllArgs(CmdArgs, options::OPT_r);

  if (Args.hasArg(options::OPT_Z_Xlinker__no_demangle))
    CmdArgs.push_back("--no-demangle");

  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    // For PS4, we always want to pass libm, libstdc++ and libkernel
    // libraries for both C and C++ compilations.
    CmdArgs.push_back("-lkernel");
    if (D.CCCIsCXX()) {
      if (ToolChain.ShouldLinkCXXStdlib(Args))
        ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);
      if (Args.hasArg(options::OPT_pg))
        CmdArgs.push_back("-lm_p");
      else
        CmdArgs.push_back("-lm");
    }
    // FIXME: For some reason GCC passes -lgcc and -lgcc_s before adding
    // the default system libraries. Just mimic this for now.
    if (Args.hasArg(options::OPT_pg))
      CmdArgs.push_back("-lgcc_p");
    else
      CmdArgs.push_back("-lcompiler_rt");
    if (Args.hasArg(options::OPT_static)) {
      CmdArgs.push_back("-lstdc++");
    } else if (Args.hasArg(options::OPT_pg)) {
      CmdArgs.push_back("-lgcc_eh_p");
    } else {
      CmdArgs.push_back("--as-needed");
      CmdArgs.push_back("-lstdc++");
      CmdArgs.push_back("--no-as-needed");
    }

    if (Args.hasArg(options::OPT_pthread)) {
      if (Args.hasArg(options::OPT_pg))
        CmdArgs.push_back("-lpthread_p");
      else
        CmdArgs.push_back("-lpthread");
    }

    if (Args.hasArg(options::OPT_pg)) {
      if (Args.hasArg(options::OPT_shared))
        CmdArgs.push_back("-lc");
      else {
        if (Args.hasArg(options::OPT_static)) {
          CmdArgs.push_back("--start-group");
          CmdArgs.push_back("-lc_p");
          CmdArgs.push_back("-lpthread_p");
          CmdArgs.push_back("--end-group");
        } else {
          CmdArgs.push_back("-lc_p");
        }
      }
      CmdArgs.push_back("-lgcc_p");
    } else {
      if (Args.hasArg(options::OPT_static)) {
        CmdArgs.push_back("--start-group");
        CmdArgs.push_back("-lc");
        CmdArgs.push_back("-lpthread");
        CmdArgs.push_back("--end-group");
      } else {
        CmdArgs.push_back("-lc");
      }
      CmdArgs.push_back("-lcompiler_rt");
    }

    if (Args.hasArg(options::OPT_static)) {
      CmdArgs.push_back("-lstdc++");
    } else if (Args.hasArg(options::OPT_pg)) {
      CmdArgs.push_back("-lgcc_eh_p");
    } else {
      CmdArgs.push_back("--as-needed");
      CmdArgs.push_back("-lstdc++");
      CmdArgs.push_back("--no-as-needed");
    }
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    if (Args.hasArg(options::OPT_shared) || Args.hasArg(options::OPT_pie))
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtendS.o")));
    else
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtend.o")));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtn.o")));
  }

  const char *Exec =
#ifdef _WIN32
      Args.MakeArgString(ToolChain.GetProgramPath("orbis-ld.gold"));
#else
      Args.MakeArgString(ToolChain.GetProgramPath("orbis-ld"));
#endif

  C.addCommand(llvm::make_unique<Command>(JA, T, Exec, CmdArgs, Inputs));
}

void tools::PS4cpu::Link::ConstructJob(Compilation &C, const JobAction &JA,
                                       const InputInfo &Output,
                                       const InputInfoList &Inputs,
                                       const ArgList &Args,
                                       const char *LinkingOutput) const {
  const toolchains::FreeBSD &ToolChain =
      static_cast<const toolchains::FreeBSD &>(getToolChain());
  const Driver &D = ToolChain.getDriver();
  bool PS4Linker;
  StringRef LinkerOptName;
  if (const Arg *A = Args.getLastArg(options::OPT_fuse_ld_EQ)) {
    LinkerOptName = A->getValue();
    if (LinkerOptName != "ps4" && LinkerOptName != "gold")
      D.Diag(diag::err_drv_unsupported_linker) << LinkerOptName;
  }

  if (LinkerOptName == "gold")
    PS4Linker = false;
  else if (LinkerOptName == "ps4")
    PS4Linker = true;
  else
    PS4Linker = !Args.hasArg(options::OPT_shared);

  if (PS4Linker)
    ConstructPS4LinkJob(*this, C, JA, Output, Inputs, Args, LinkingOutput);
  else
    ConstructGoldLinkJob(*this, C, JA, Output, Inputs, Args, LinkingOutput);
}

toolchains::PS4CPU::PS4CPU(const Driver &D, const llvm::Triple &Triple,
                           const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  if (Args.hasArg(clang::driver::options::OPT_static))
    D.Diag(clang::diag::err_drv_unsupported_opt_for_target) << "-static"
                                                            << "PS4";

  // Determine where to find the PS4 libraries. We use SCE_ORBIS_SDK_DIR
  // if it exists; otherwise use the driver's installation path, which
  // should be <SDK_DIR>/host_tools/bin.

  SmallString<512> PS4SDKDir;
  if (const char *EnvValue = getenv("SCE_ORBIS_SDK_DIR")) {
    if (!llvm::sys::fs::exists(EnvValue))
      getDriver().Diag(clang::diag::warn_drv_ps4_sdk_dir) << EnvValue;
    PS4SDKDir = EnvValue;
  } else {
    PS4SDKDir = getDriver().Dir;
    llvm::sys::path::append(PS4SDKDir, "/../../");
  }

  // By default, the driver won't report a warning if it can't find
  // PS4's include or lib directories. This behavior could be changed if
  // -Weverything or -Winvalid-or-nonexistent-directory options are passed.
  // If -isysroot was passed, use that as the SDK base path.
  std::string PrefixDir;
  if (const Arg *A = Args.getLastArg(options::OPT_isysroot)) {
    PrefixDir = A->getValue();
    if (!llvm::sys::fs::exists(PrefixDir))
      getDriver().Diag(clang::diag::warn_missing_sysroot) << PrefixDir;
  } else
    PrefixDir = PS4SDKDir.str();

  SmallString<512> PS4SDKIncludeDir(PrefixDir);
  llvm::sys::path::append(PS4SDKIncludeDir, "target/include");
  if (!Args.hasArg(options::OPT_nostdinc) &&
      !Args.hasArg(options::OPT_nostdlibinc) &&
      !Args.hasArg(options::OPT_isysroot) &&
      !Args.hasArg(options::OPT__sysroot_EQ) &&
      !llvm::sys::fs::exists(PS4SDKIncludeDir)) {
    getDriver().Diag(clang::diag::warn_drv_unable_to_find_directory_expected)
        << "PS4 system headers" << PS4SDKIncludeDir;
  }

  SmallString<512> PS4SDKLibDir(PS4SDKDir);
  llvm::sys::path::append(PS4SDKLibDir, "target/lib");
  if (!Args.hasArg(options::OPT_nostdlib) &&
      !Args.hasArg(options::OPT_nodefaultlibs) &&
      !Args.hasArg(options::OPT__sysroot_EQ) && !Args.hasArg(options::OPT_E) &&
      !Args.hasArg(options::OPT_c) && !Args.hasArg(options::OPT_S) &&
      !Args.hasArg(options::OPT_emit_ast) &&
      !llvm::sys::fs::exists(PS4SDKLibDir)) {
    getDriver().Diag(clang::diag::warn_drv_unable_to_find_directory_expected)
        << "PS4 system libraries" << PS4SDKLibDir;
    return;
  }
  getFilePaths().push_back(PS4SDKLibDir.str());
}

Tool *toolchains::PS4CPU::buildAssembler() const {
  return new tools::PS4cpu::Assemble(*this);
}

Tool *toolchains::PS4CPU::buildLinker() const {
  return new tools::PS4cpu::Link(*this);
}

bool toolchains::PS4CPU::isPICDefault() const { return true; }

bool toolchains::PS4CPU::HasNativeLLVMSupport() const { return true; }

SanitizerMask toolchains::PS4CPU::getSupportedSanitizers() const {
  SanitizerMask Res = ToolChain::getSupportedSanitizers();
  Res |= SanitizerKind::Address;
  Res |= SanitizerKind::Vptr;
  return Res;
}
