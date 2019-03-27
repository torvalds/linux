//===--- Gnu.cpp - Gnu Tool and ToolChain Implementations -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Gnu.h"
#include "Arch/ARM.h"
#include "Arch/Mips.h"
#include "Arch/PPC.h"
#include "Arch/RISCV.h"
#include "Arch/Sparc.h"
#include "Arch/SystemZ.h"
#include "CommonArgs.h"
#include "Linux.h"
#include "clang/Config/config.h" // for GCC_INSTALL_PREFIX
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/Tool.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetParser.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <system_error>

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

void tools::GnuTool::anchor() {}

static bool forwardToGCC(const Option &O) {
  // Don't forward inputs from the original command line.  They are added from
  // InputInfoList.
  return O.getKind() != Option::InputClass &&
         !O.hasFlag(options::DriverOption) && !O.hasFlag(options::LinkerInput);
}

// Switch CPU names not recognized by GNU assembler to a close CPU that it does
// recognize, instead of a lower march from being picked in the absence of a cpu
// flag.
static void normalizeCPUNamesForAssembler(const ArgList &Args,
                                          ArgStringList &CmdArgs) {
  if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ)) {
    StringRef CPUArg(A->getValue());
    if (CPUArg.equals_lower("krait"))
      CmdArgs.push_back("-mcpu=cortex-a15");
    else if(CPUArg.equals_lower("kryo"))
      CmdArgs.push_back("-mcpu=cortex-a57");
    else
      Args.AddLastArg(CmdArgs, options::OPT_mcpu_EQ);
  }
}

void tools::gcc::Common::ConstructJob(Compilation &C, const JobAction &JA,
                                      const InputInfo &Output,
                                      const InputInfoList &Inputs,
                                      const ArgList &Args,
                                      const char *LinkingOutput) const {
  const Driver &D = getToolChain().getDriver();
  ArgStringList CmdArgs;

  for (const auto &A : Args) {
    if (forwardToGCC(A->getOption())) {
      // It is unfortunate that we have to claim here, as this means
      // we will basically never report anything interesting for
      // platforms using a generic gcc, even if we are just using gcc
      // to get to the assembler.
      A->claim();

      // Don't forward any -g arguments to assembly steps.
      if (isa<AssembleJobAction>(JA) &&
          A->getOption().matches(options::OPT_g_Group))
        continue;

      // Don't forward any -W arguments to assembly and link steps.
      if ((isa<AssembleJobAction>(JA) || isa<LinkJobAction>(JA)) &&
          A->getOption().matches(options::OPT_W_Group))
        continue;

      // Don't forward -mno-unaligned-access since GCC doesn't understand
      // it and because it doesn't affect the assembly or link steps.
      if ((isa<AssembleJobAction>(JA) || isa<LinkJobAction>(JA)) &&
          (A->getOption().matches(options::OPT_munaligned_access) ||
           A->getOption().matches(options::OPT_mno_unaligned_access)))
        continue;

      A->render(Args, CmdArgs);
    }
  }

  RenderExtraToolArgs(JA, CmdArgs);

  // If using a driver driver, force the arch.
  if (getToolChain().getTriple().isOSDarwin()) {
    CmdArgs.push_back("-arch");
    CmdArgs.push_back(
        Args.MakeArgString(getToolChain().getDefaultUniversalArchName()));
  }

  // Try to force gcc to match the tool chain we want, if we recognize
  // the arch.
  //
  // FIXME: The triple class should directly provide the information we want
  // here.
  switch (getToolChain().getArch()) {
  default:
    break;
  case llvm::Triple::x86:
  case llvm::Triple::ppc:
    CmdArgs.push_back("-m32");
    break;
  case llvm::Triple::x86_64:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
    CmdArgs.push_back("-m64");
    break;
  case llvm::Triple::sparcel:
    CmdArgs.push_back("-EL");
    break;
  }

  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  } else {
    assert(Output.isNothing() && "Unexpected output");
    CmdArgs.push_back("-fsyntax-only");
  }

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  // Only pass -x if gcc will understand it; otherwise hope gcc
  // understands the suffix correctly. The main use case this would go
  // wrong in is for linker inputs if they happened to have an odd
  // suffix; really the only way to get this to happen is a command
  // like '-x foobar a.c' which will treat a.c like a linker input.
  //
  // FIXME: For the linker case specifically, can we safely convert
  // inputs into '-Wl,' options?
  for (const auto &II : Inputs) {
    // Don't try to pass LLVM or AST inputs to a generic gcc.
    if (types::isLLVMIR(II.getType()))
      D.Diag(clang::diag::err_drv_no_linker_llvm_support)
          << getToolChain().getTripleString();
    else if (II.getType() == types::TY_AST)
      D.Diag(diag::err_drv_no_ast_support) << getToolChain().getTripleString();
    else if (II.getType() == types::TY_ModuleFile)
      D.Diag(diag::err_drv_no_module_support)
          << getToolChain().getTripleString();

    if (types::canTypeBeUserSpecified(II.getType())) {
      CmdArgs.push_back("-x");
      CmdArgs.push_back(types::getTypeName(II.getType()));
    }

    if (II.isFilename())
      CmdArgs.push_back(II.getFilename());
    else {
      const Arg &A = II.getInputArg();

      // Reverse translate some rewritten options.
      if (A.getOption().matches(options::OPT_Z_reserved_lib_stdcxx)) {
        CmdArgs.push_back("-lstdc++");
        continue;
      }

      // Don't render as input, we need gcc to do the translations.
      A.render(Args, CmdArgs);
    }
  }

  const std::string &customGCCName = D.getCCCGenericGCCName();
  const char *GCCName;
  if (!customGCCName.empty())
    GCCName = customGCCName.c_str();
  else if (D.CCCIsCXX()) {
    GCCName = "g++";
  } else
    GCCName = "gcc";

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath(GCCName));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

void tools::gcc::Preprocessor::RenderExtraToolArgs(
    const JobAction &JA, ArgStringList &CmdArgs) const {
  CmdArgs.push_back("-E");
}

void tools::gcc::Compiler::RenderExtraToolArgs(const JobAction &JA,
                                               ArgStringList &CmdArgs) const {
  const Driver &D = getToolChain().getDriver();

  switch (JA.getType()) {
  // If -flto, etc. are present then make sure not to force assembly output.
  case types::TY_LLVM_IR:
  case types::TY_LTO_IR:
  case types::TY_LLVM_BC:
  case types::TY_LTO_BC:
    CmdArgs.push_back("-c");
    break;
  // We assume we've got an "integrated" assembler in that gcc will produce an
  // object file itself.
  case types::TY_Object:
    CmdArgs.push_back("-c");
    break;
  case types::TY_PP_Asm:
    CmdArgs.push_back("-S");
    break;
  case types::TY_Nothing:
    CmdArgs.push_back("-fsyntax-only");
    break;
  default:
    D.Diag(diag::err_drv_invalid_gcc_output_type) << getTypeName(JA.getType());
  }
}

void tools::gcc::Linker::RenderExtraToolArgs(const JobAction &JA,
                                             ArgStringList &CmdArgs) const {
  // The types are (hopefully) good enough.
}

// On Arm the endianness of the output file is determined by the target and
// can be overridden by the pseudo-target flags '-mlittle-endian'/'-EL' and
// '-mbig-endian'/'-EB'. Unlike other targets the flag does not result in a
// normalized triple so we must handle the flag here.
static bool isArmBigEndian(const llvm::Triple &Triple,
                           const ArgList &Args) {
  bool IsBigEndian = false;
  switch (Triple.getArch()) {
  case llvm::Triple::armeb:
  case llvm::Triple::thumbeb:
    IsBigEndian = true;
    LLVM_FALLTHROUGH;
  case llvm::Triple::arm:
  case llvm::Triple::thumb:
    if (Arg *A = Args.getLastArg(options::OPT_mlittle_endian,
                               options::OPT_mbig_endian))
      IsBigEndian = !A->getOption().matches(options::OPT_mlittle_endian);
    break;
  default:
    break;
  }
  return IsBigEndian;
}

static const char *getLDMOption(const llvm::Triple &T, const ArgList &Args) {
  switch (T.getArch()) {
  case llvm::Triple::x86:
    if (T.isOSIAMCU())
      return "elf_iamcu";
    return "elf_i386";
  case llvm::Triple::aarch64:
    return "aarch64linux";
  case llvm::Triple::aarch64_be:
    return "aarch64linuxb";
  case llvm::Triple::arm:
  case llvm::Triple::thumb:
  case llvm::Triple::armeb:
  case llvm::Triple::thumbeb:
    return isArmBigEndian(T, Args) ? "armelfb_linux_eabi" : "armelf_linux_eabi";
  case llvm::Triple::ppc:
    return "elf32ppclinux";
  case llvm::Triple::ppc64:
    return "elf64ppc";
  case llvm::Triple::ppc64le:
    return "elf64lppc";
  case llvm::Triple::riscv32:
    return "elf32lriscv";
  case llvm::Triple::riscv64:
    return "elf64lriscv";
  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
    return "elf32_sparc";
  case llvm::Triple::sparcv9:
    return "elf64_sparc";
  case llvm::Triple::mips:
    return "elf32btsmip";
  case llvm::Triple::mipsel:
    return "elf32ltsmip";
  case llvm::Triple::mips64:
    if (tools::mips::hasMipsAbiArg(Args, "n32") ||
        T.getEnvironment() == llvm::Triple::GNUABIN32)
      return "elf32btsmipn32";
    return "elf64btsmip";
  case llvm::Triple::mips64el:
    if (tools::mips::hasMipsAbiArg(Args, "n32") ||
        T.getEnvironment() == llvm::Triple::GNUABIN32)
      return "elf32ltsmipn32";
    return "elf64ltsmip";
  case llvm::Triple::systemz:
    return "elf64_s390";
  case llvm::Triple::x86_64:
    if (T.getEnvironment() == llvm::Triple::GNUX32)
      return "elf32_x86_64";
    return "elf_x86_64";
  default:
    return nullptr;
  }
}

static bool getPIE(const ArgList &Args, const toolchains::Linux &ToolChain) {
  if (Args.hasArg(options::OPT_shared) || Args.hasArg(options::OPT_static) ||
      Args.hasArg(options::OPT_r))
    return false;

  Arg *A = Args.getLastArg(options::OPT_pie, options::OPT_no_pie,
                           options::OPT_nopie);
  if (!A)
    return ToolChain.isPIEDefault();
  return A->getOption().matches(options::OPT_pie);
}

void tools::gnutools::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                           const InputInfo &Output,
                                           const InputInfoList &Inputs,
                                           const ArgList &Args,
                                           const char *LinkingOutput) const {
  const toolchains::Linux &ToolChain =
      static_cast<const toolchains::Linux &>(getToolChain());
  const Driver &D = ToolChain.getDriver();

  const llvm::Triple &Triple = getToolChain().getEffectiveTriple();

  const llvm::Triple::ArchType Arch = ToolChain.getArch();
  const bool isAndroid = ToolChain.getTriple().isAndroid();
  const bool IsIAMCU = ToolChain.getTriple().isOSIAMCU();
  const bool IsPIE = getPIE(Args, ToolChain);
  const bool HasCRTBeginEndFiles =
      ToolChain.getTriple().hasEnvironment() ||
      (ToolChain.getTriple().getVendor() != llvm::Triple::MipsTechnologies);

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

  if (IsPIE)
    CmdArgs.push_back("-pie");

  if (Args.hasArg(options::OPT_rdynamic))
    CmdArgs.push_back("-export-dynamic");

  if (Args.hasArg(options::OPT_s))
    CmdArgs.push_back("-s");

  if (Triple.isARM() || Triple.isThumb() || Triple.isAArch64()) {
    bool IsBigEndian = isArmBigEndian(Triple, Args);
    if (IsBigEndian)
      arm::appendBE8LinkFlag(Args, CmdArgs, Triple);
    IsBigEndian = IsBigEndian || Arch == llvm::Triple::aarch64_be;
    CmdArgs.push_back(IsBigEndian ? "-EB" : "-EL");
  }

  // Most Android ARM64 targets should enable the linker fix for erratum
  // 843419. Only non-Cortex-A53 devices are allowed to skip this flag.
  if (Arch == llvm::Triple::aarch64 && isAndroid) {
    std::string CPU = getCPUName(Args, Triple);
    if (CPU.empty() || CPU == "generic" || CPU == "cortex-a53")
      CmdArgs.push_back("--fix-cortex-a53-843419");
  }

  for (const auto &Opt : ToolChain.ExtraOpts)
    CmdArgs.push_back(Opt.c_str());

  CmdArgs.push_back("--eh-frame-hdr");

  if (const char *LDMOption = getLDMOption(ToolChain.getTriple(), Args)) {
    CmdArgs.push_back("-m");
    CmdArgs.push_back(LDMOption);
  } else {
    D.Diag(diag::err_target_unknown_triple) << Triple.str();
    return;
  }

  if (Args.hasArg(options::OPT_static)) {
    if (Arch == llvm::Triple::arm || Arch == llvm::Triple::armeb ||
        Arch == llvm::Triple::thumb || Arch == llvm::Triple::thumbeb)
      CmdArgs.push_back("-Bstatic");
    else
      CmdArgs.push_back("-static");
  } else if (Args.hasArg(options::OPT_shared)) {
    CmdArgs.push_back("-shared");
  }

  if (!Args.hasArg(options::OPT_static)) {
    if (Args.hasArg(options::OPT_rdynamic))
      CmdArgs.push_back("-export-dynamic");

    if (!Args.hasArg(options::OPT_shared)) {
      const std::string Loader =
          D.DyldPrefix + ToolChain.getDynamicLinker(Args);
      CmdArgs.push_back("-dynamic-linker");
      CmdArgs.push_back(Args.MakeArgString(Loader));
    }
  }

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    if (!isAndroid && !IsIAMCU) {
      const char *crt1 = nullptr;
      if (!Args.hasArg(options::OPT_shared)) {
        if (Args.hasArg(options::OPT_pg))
          crt1 = "gcrt1.o";
        else if (IsPIE)
          crt1 = "Scrt1.o";
        else
          crt1 = "crt1.o";
      }
      if (crt1)
        CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crt1)));

      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crti.o")));
    }

    if (IsIAMCU)
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crt0.o")));
    else {
      const char *crtbegin;
      if (Args.hasArg(options::OPT_static))
        crtbegin = isAndroid ? "crtbegin_static.o" : "crtbeginT.o";
      else if (Args.hasArg(options::OPT_shared))
        crtbegin = isAndroid ? "crtbegin_so.o" : "crtbeginS.o";
      else if (IsPIE)
        crtbegin = isAndroid ? "crtbegin_dynamic.o" : "crtbeginS.o";
      else
        crtbegin = isAndroid ? "crtbegin_dynamic.o" : "crtbegin.o";

      if (HasCRTBeginEndFiles)
        CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtbegin)));
    }

    // Add crtfastmath.o if available and fast math is enabled.
    ToolChain.AddFastMathRuntimeIfAvailable(Args, CmdArgs);
  }

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  Args.AddAllArgs(CmdArgs, options::OPT_u);

  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  if (D.isUsingLTO()) {
    assert(!Inputs.empty() && "Must have at least one input.");
    AddGoldPlugin(ToolChain, Args, CmdArgs, Output, Inputs[0],
                  D.getLTOMode() == LTOK_Thin);
  }

  if (Args.hasArg(options::OPT_Z_Xlinker__no_demangle))
    CmdArgs.push_back("--no-demangle");

  bool NeedsSanitizerDeps = addSanitizerRuntimes(ToolChain, Args, CmdArgs);
  bool NeedsXRayDeps = addXRayRuntime(ToolChain, Args, CmdArgs);
  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);
  // The profile runtime also needs access to system libraries.
  getToolChain().addProfileRTLibs(Args, CmdArgs);

  if (D.CCCIsCXX() &&
      !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    if (ToolChain.ShouldLinkCXXStdlib(Args)) {
      bool OnlyLibstdcxxStatic = Args.hasArg(options::OPT_static_libstdcxx) &&
                                 !Args.hasArg(options::OPT_static);
      if (OnlyLibstdcxxStatic)
        CmdArgs.push_back("-Bstatic");
      ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);
      if (OnlyLibstdcxxStatic)
        CmdArgs.push_back("-Bdynamic");
    }
    CmdArgs.push_back("-lm");
  }
  // Silence warnings when linking C code with a C++ '-stdlib' argument.
  Args.ClaimAllArgs(options::OPT_stdlib_EQ);

  if (!Args.hasArg(options::OPT_nostdlib)) {
    if (!Args.hasArg(options::OPT_nodefaultlibs)) {
      if (Args.hasArg(options::OPT_static))
        CmdArgs.push_back("--start-group");

      if (NeedsSanitizerDeps)
        linkSanitizerRuntimeDeps(ToolChain, CmdArgs);

      if (NeedsXRayDeps)
        linkXRayRuntimeDeps(ToolChain, CmdArgs);

      bool WantPthread = Args.hasArg(options::OPT_pthread) ||
                         Args.hasArg(options::OPT_pthreads);

      // FIXME: Only pass GompNeedsRT = true for platforms with libgomp that
      // require librt. Most modern Linux platforms do, but some may not.
      if (addOpenMPRuntime(CmdArgs, ToolChain, Args,
                           JA.isHostOffloading(Action::OFK_OpenMP),
                           /* GompNeedsRT= */ true))
        // OpenMP runtimes implies pthreads when using the GNU toolchain.
        // FIXME: Does this really make sense for all GNU toolchains?
        WantPthread = true;

      AddRunTimeLibs(ToolChain, D, CmdArgs, Args);

      if (WantPthread && !isAndroid)
        CmdArgs.push_back("-lpthread");

      if (Args.hasArg(options::OPT_fsplit_stack))
        CmdArgs.push_back("--wrap=pthread_create");

      CmdArgs.push_back("-lc");

      // Add IAMCU specific libs, if needed.
      if (IsIAMCU)
        CmdArgs.push_back("-lgloss");

      if (Args.hasArg(options::OPT_static))
        CmdArgs.push_back("--end-group");
      else
        AddRunTimeLibs(ToolChain, D, CmdArgs, Args);

      // Add IAMCU specific libs (outside the group), if needed.
      if (IsIAMCU) {
        CmdArgs.push_back("--as-needed");
        CmdArgs.push_back("-lsoftfp");
        CmdArgs.push_back("--no-as-needed");
      }
    }

    if (!Args.hasArg(options::OPT_nostartfiles) && !IsIAMCU) {
      const char *crtend;
      if (Args.hasArg(options::OPT_shared))
        crtend = isAndroid ? "crtend_so.o" : "crtendS.o";
      else if (IsPIE)
        crtend = isAndroid ? "crtend_android.o" : "crtendS.o";
      else
        crtend = isAndroid ? "crtend_android.o" : "crtend.o";

      if (HasCRTBeginEndFiles)
        CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtend)));
      if (!isAndroid)
        CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtn.o")));
    }
  }

  // Add OpenMP offloading linker script args if required.
  AddOpenMPLinkerScript(getToolChain(), C, Output, Inputs, Args, CmdArgs, JA);

  // Add HIP offloading linker script args if required.
  AddHIPLinkerScript(getToolChain(), C, Output, Inputs, Args, CmdArgs, JA,
                     *this);

  const char *Exec = Args.MakeArgString(ToolChain.GetLinkerPath());
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

void tools::gnutools::Assembler::ConstructJob(Compilation &C,
                                              const JobAction &JA,
                                              const InputInfo &Output,
                                              const InputInfoList &Inputs,
                                              const ArgList &Args,
                                              const char *LinkingOutput) const {
  const auto &D = getToolChain().getDriver();

  claimNoWarnArgs(Args);

  ArgStringList CmdArgs;

  llvm::Reloc::Model RelocationModel;
  unsigned PICLevel;
  bool IsPIE;
  std::tie(RelocationModel, PICLevel, IsPIE) =
      ParsePICArgs(getToolChain(), Args);

  if (const Arg *A = Args.getLastArg(options::OPT_gz, options::OPT_gz_EQ)) {
    if (A->getOption().getID() == options::OPT_gz) {
      CmdArgs.push_back("-compress-debug-sections");
    } else {
      StringRef Value = A->getValue();
      if (Value == "none") {
        CmdArgs.push_back("-compress-debug-sections=none");
      } else if (Value == "zlib" || Value == "zlib-gnu") {
        CmdArgs.push_back(
            Args.MakeArgString("-compress-debug-sections=" + Twine(Value)));
      } else {
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getOption().getName() << Value;
      }
    }
  }

  switch (getToolChain().getArch()) {
  default:
    break;
  // Add --32/--64 to make sure we get the format we want.
  // This is incomplete
  case llvm::Triple::x86:
    CmdArgs.push_back("--32");
    break;
  case llvm::Triple::x86_64:
    if (getToolChain().getTriple().getEnvironment() == llvm::Triple::GNUX32)
      CmdArgs.push_back("--x32");
    else
      CmdArgs.push_back("--64");
    break;
  case llvm::Triple::ppc: {
    CmdArgs.push_back("-a32");
    CmdArgs.push_back("-mppc");
    CmdArgs.push_back(
      ppc::getPPCAsmModeForCPU(getCPUName(Args, getToolChain().getTriple())));
    break;
  }
  case llvm::Triple::ppc64: {
    CmdArgs.push_back("-a64");
    CmdArgs.push_back("-mppc64");
    CmdArgs.push_back(
      ppc::getPPCAsmModeForCPU(getCPUName(Args, getToolChain().getTriple())));
    break;
  }
  case llvm::Triple::ppc64le: {
    CmdArgs.push_back("-a64");
    CmdArgs.push_back("-mppc64");
    CmdArgs.push_back("-mlittle-endian");
    CmdArgs.push_back(
      ppc::getPPCAsmModeForCPU(getCPUName(Args, getToolChain().getTriple())));
    break;
  }
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64: {
    StringRef ABIName = riscv::getRISCVABI(Args, getToolChain().getTriple());
    CmdArgs.push_back("-mabi");
    CmdArgs.push_back(ABIName.data());
    if (const Arg *A = Args.getLastArg(options::OPT_march_EQ)) {
      StringRef MArch = A->getValue();
      CmdArgs.push_back("-march");
      CmdArgs.push_back(MArch.data());
    }
    break;
  }
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
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb: {
    const llvm::Triple &Triple2 = getToolChain().getTriple();
    CmdArgs.push_back(isArmBigEndian(Triple2, Args) ? "-EB" : "-EL");
    switch (Triple2.getSubArch()) {
    case llvm::Triple::ARMSubArch_v7:
      CmdArgs.push_back("-mfpu=neon");
      break;
    case llvm::Triple::ARMSubArch_v8:
      CmdArgs.push_back("-mfpu=crypto-neon-fp-armv8");
      break;
    default:
      break;
    }

    switch (arm::getARMFloatABI(getToolChain(), Args)) {
    case arm::FloatABI::Invalid: llvm_unreachable("must have an ABI!");
    case arm::FloatABI::Soft:
      CmdArgs.push_back(Args.MakeArgString("-mfloat-abi=soft"));
      break;
    case arm::FloatABI::SoftFP:
      CmdArgs.push_back(Args.MakeArgString("-mfloat-abi=softfp"));
      break;
    case arm::FloatABI::Hard:
      CmdArgs.push_back(Args.MakeArgString("-mfloat-abi=hard"));
      break;
    }

    Args.AddLastArg(CmdArgs, options::OPT_march_EQ);
    normalizeCPUNamesForAssembler(Args, CmdArgs);

    Args.AddLastArg(CmdArgs, options::OPT_mfpu_EQ);
    break;
  }
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be: {
    CmdArgs.push_back(
        getToolChain().getArch() == llvm::Triple::aarch64_be ? "-EB" : "-EL");
    Args.AddLastArg(CmdArgs, options::OPT_march_EQ);
    normalizeCPUNamesForAssembler(Args, CmdArgs);

    break;
  }
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el: {
    StringRef CPUName;
    StringRef ABIName;
    mips::getMipsCPUAndABI(Args, getToolChain().getTriple(), CPUName, ABIName);
    ABIName = mips::getGnuCompatibleMipsABIName(ABIName);

    CmdArgs.push_back("-march");
    CmdArgs.push_back(CPUName.data());

    CmdArgs.push_back("-mabi");
    CmdArgs.push_back(ABIName.data());

    // -mno-shared should be emitted unless -fpic, -fpie, -fPIC, -fPIE,
    // or -mshared (not implemented) is in effect.
    if (RelocationModel == llvm::Reloc::Static)
      CmdArgs.push_back("-mno-shared");

    // LLVM doesn't support -mplt yet and acts as if it is always given.
    // However, -mplt has no effect with the N64 ABI.
    if (ABIName != "64" && !Args.hasArg(options::OPT_mno_abicalls))
      CmdArgs.push_back("-call_nonpic");

    if (getToolChain().getTriple().isLittleEndian())
      CmdArgs.push_back("-EL");
    else
      CmdArgs.push_back("-EB");

    if (Arg *A = Args.getLastArg(options::OPT_mnan_EQ)) {
      if (StringRef(A->getValue()) == "2008")
        CmdArgs.push_back(Args.MakeArgString("-mnan=2008"));
    }

    // Add the last -mfp32/-mfpxx/-mfp64 or -mfpxx if it is enabled by default.
    if (Arg *A = Args.getLastArg(options::OPT_mfp32, options::OPT_mfpxx,
                                 options::OPT_mfp64)) {
      A->claim();
      A->render(Args, CmdArgs);
    } else if (mips::shouldUseFPXX(
                   Args, getToolChain().getTriple(), CPUName, ABIName,
                   mips::getMipsFloatABI(getToolChain().getDriver(), Args)))
      CmdArgs.push_back("-mfpxx");

    // Pass on -mmips16 or -mno-mips16. However, the assembler equivalent of
    // -mno-mips16 is actually -no-mips16.
    if (Arg *A =
            Args.getLastArg(options::OPT_mips16, options::OPT_mno_mips16)) {
      if (A->getOption().matches(options::OPT_mips16)) {
        A->claim();
        A->render(Args, CmdArgs);
      } else {
        A->claim();
        CmdArgs.push_back("-no-mips16");
      }
    }

    Args.AddLastArg(CmdArgs, options::OPT_mmicromips,
                    options::OPT_mno_micromips);
    Args.AddLastArg(CmdArgs, options::OPT_mdsp, options::OPT_mno_dsp);
    Args.AddLastArg(CmdArgs, options::OPT_mdspr2, options::OPT_mno_dspr2);

    if (Arg *A = Args.getLastArg(options::OPT_mmsa, options::OPT_mno_msa)) {
      // Do not use AddLastArg because not all versions of MIPS assembler
      // support -mmsa / -mno-msa options.
      if (A->getOption().matches(options::OPT_mmsa))
        CmdArgs.push_back(Args.MakeArgString("-mmsa"));
    }

    Args.AddLastArg(CmdArgs, options::OPT_mhard_float,
                    options::OPT_msoft_float);

    Args.AddLastArg(CmdArgs, options::OPT_mdouble_float,
                    options::OPT_msingle_float);

    Args.AddLastArg(CmdArgs, options::OPT_modd_spreg,
                    options::OPT_mno_odd_spreg);

    AddAssemblerKPIC(getToolChain(), Args, CmdArgs);
    break;
  }
  case llvm::Triple::systemz: {
    // Always pass an -march option, since our default of z10 is later
    // than the GNU assembler's default.
    StringRef CPUName = systemz::getSystemZTargetCPU(Args);
    CmdArgs.push_back(Args.MakeArgString("-march=" + CPUName));
    break;
  }
  }

  Args.AddAllArgs(CmdArgs, options::OPT_I);
  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  for (const auto &II : Inputs)
    CmdArgs.push_back(II.getFilename());

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("as"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));

  // Handle the debug info splitting at object creation time if we're
  // creating an object.
  // TODO: Currently only works on linux with newer objcopy.
  if (Args.hasArg(options::OPT_gsplit_dwarf) &&
      getToolChain().getTriple().isOSLinux())
    SplitDebugInfo(getToolChain(), C, *this, JA, Args, Output,
                   SplitDebugName(Args, Output));
}

namespace {
// Filter to remove Multilibs that don't exist as a suffix to Path
class FilterNonExistent {
  StringRef Base, File;
  llvm::vfs::FileSystem &VFS;

public:
  FilterNonExistent(StringRef Base, StringRef File, llvm::vfs::FileSystem &VFS)
      : Base(Base), File(File), VFS(VFS) {}
  bool operator()(const Multilib &M) {
    return !VFS.exists(Base + M.gccSuffix() + File);
  }
};
} // end anonymous namespace

static bool isSoftFloatABI(const ArgList &Args) {
  Arg *A = Args.getLastArg(options::OPT_msoft_float, options::OPT_mhard_float,
                           options::OPT_mfloat_abi_EQ);
  if (!A)
    return false;

  return A->getOption().matches(options::OPT_msoft_float) ||
         (A->getOption().matches(options::OPT_mfloat_abi_EQ) &&
          A->getValue() == StringRef("soft"));
}

/// \p Flag must be a flag accepted by the driver with its leading '-' removed,
//     otherwise '-print-multi-lib' will not emit them correctly.
static void addMultilibFlag(bool Enabled, const char *const Flag,
                            std::vector<std::string> &Flags) {
  if (Enabled)
    Flags.push_back(std::string("+") + Flag);
  else
    Flags.push_back(std::string("-") + Flag);
}

static bool isArmOrThumbArch(llvm::Triple::ArchType Arch) {
  return Arch == llvm::Triple::arm || Arch == llvm::Triple::thumb;
}

static bool isMipsEL(llvm::Triple::ArchType Arch) {
  return Arch == llvm::Triple::mipsel || Arch == llvm::Triple::mips64el;
}

static bool isMips16(const ArgList &Args) {
  Arg *A = Args.getLastArg(options::OPT_mips16, options::OPT_mno_mips16);
  return A && A->getOption().matches(options::OPT_mips16);
}

static bool isMicroMips(const ArgList &Args) {
  Arg *A = Args.getLastArg(options::OPT_mmicromips, options::OPT_mno_micromips);
  return A && A->getOption().matches(options::OPT_mmicromips);
}

static bool isRISCV(llvm::Triple::ArchType Arch) {
  return Arch == llvm::Triple::riscv32 || Arch == llvm::Triple::riscv64;
}

static bool isMSP430(llvm::Triple::ArchType Arch) {
  return Arch == llvm::Triple::msp430;
}

static Multilib makeMultilib(StringRef commonSuffix) {
  return Multilib(commonSuffix, commonSuffix, commonSuffix);
}

static bool findMipsCsMultilibs(const Multilib::flags_list &Flags,
                                FilterNonExistent &NonExistent,
                                DetectedMultilibs &Result) {
  // Check for Code Sourcery toolchain multilibs
  MultilibSet CSMipsMultilibs;
  {
    auto MArchMips16 = makeMultilib("/mips16").flag("+m32").flag("+mips16");

    auto MArchMicroMips =
        makeMultilib("/micromips").flag("+m32").flag("+mmicromips");

    auto MArchDefault = makeMultilib("").flag("-mips16").flag("-mmicromips");

    auto UCLibc = makeMultilib("/uclibc").flag("+muclibc");

    auto SoftFloat = makeMultilib("/soft-float").flag("+msoft-float");

    auto Nan2008 = makeMultilib("/nan2008").flag("+mnan=2008");

    auto DefaultFloat =
        makeMultilib("").flag("-msoft-float").flag("-mnan=2008");

    auto BigEndian = makeMultilib("").flag("+EB").flag("-EL");

    auto LittleEndian = makeMultilib("/el").flag("+EL").flag("-EB");

    // Note that this one's osSuffix is ""
    auto MAbi64 = makeMultilib("")
                      .gccSuffix("/64")
                      .includeSuffix("/64")
                      .flag("+mabi=n64")
                      .flag("-mabi=n32")
                      .flag("-m32");

    CSMipsMultilibs =
        MultilibSet()
            .Either(MArchMips16, MArchMicroMips, MArchDefault)
            .Maybe(UCLibc)
            .Either(SoftFloat, Nan2008, DefaultFloat)
            .FilterOut("/micromips/nan2008")
            .FilterOut("/mips16/nan2008")
            .Either(BigEndian, LittleEndian)
            .Maybe(MAbi64)
            .FilterOut("/mips16.*/64")
            .FilterOut("/micromips.*/64")
            .FilterOut(NonExistent)
            .setIncludeDirsCallback([](const Multilib &M) {
              std::vector<std::string> Dirs({"/include"});
              if (StringRef(M.includeSuffix()).startswith("/uclibc"))
                Dirs.push_back(
                    "/../../../../mips-linux-gnu/libc/uclibc/usr/include");
              else
                Dirs.push_back("/../../../../mips-linux-gnu/libc/usr/include");
              return Dirs;
            });
  }

  MultilibSet DebianMipsMultilibs;
  {
    Multilib MAbiN32 =
        Multilib().gccSuffix("/n32").includeSuffix("/n32").flag("+mabi=n32");

    Multilib M64 = Multilib()
                       .gccSuffix("/64")
                       .includeSuffix("/64")
                       .flag("+m64")
                       .flag("-m32")
                       .flag("-mabi=n32");

    Multilib M32 = Multilib().flag("-m64").flag("+m32").flag("-mabi=n32");

    DebianMipsMultilibs =
        MultilibSet().Either(M32, M64, MAbiN32).FilterOut(NonExistent);
  }

  // Sort candidates. Toolchain that best meets the directories tree goes first.
  // Then select the first toolchains matches command line flags.
  MultilibSet *Candidates[] = {&CSMipsMultilibs, &DebianMipsMultilibs};
  if (CSMipsMultilibs.size() < DebianMipsMultilibs.size())
    std::iter_swap(Candidates, Candidates + 1);
  for (const MultilibSet *Candidate : Candidates) {
    if (Candidate->select(Flags, Result.SelectedMultilib)) {
      if (Candidate == &DebianMipsMultilibs)
        Result.BiarchSibling = Multilib();
      Result.Multilibs = *Candidate;
      return true;
    }
  }
  return false;
}

static bool findMipsAndroidMultilibs(llvm::vfs::FileSystem &VFS, StringRef Path,
                                     const Multilib::flags_list &Flags,
                                     FilterNonExistent &NonExistent,
                                     DetectedMultilibs &Result) {

  MultilibSet AndroidMipsMultilibs =
      MultilibSet()
          .Maybe(Multilib("/mips-r2").flag("+march=mips32r2"))
          .Maybe(Multilib("/mips-r6").flag("+march=mips32r6"))
          .FilterOut(NonExistent);

  MultilibSet AndroidMipselMultilibs =
      MultilibSet()
          .Either(Multilib().flag("+march=mips32"),
                  Multilib("/mips-r2", "", "/mips-r2").flag("+march=mips32r2"),
                  Multilib("/mips-r6", "", "/mips-r6").flag("+march=mips32r6"))
          .FilterOut(NonExistent);

  MultilibSet AndroidMips64elMultilibs =
      MultilibSet()
          .Either(
              Multilib().flag("+march=mips64r6"),
              Multilib("/32/mips-r1", "", "/mips-r1").flag("+march=mips32"),
              Multilib("/32/mips-r2", "", "/mips-r2").flag("+march=mips32r2"),
              Multilib("/32/mips-r6", "", "/mips-r6").flag("+march=mips32r6"))
          .FilterOut(NonExistent);

  MultilibSet *MS = &AndroidMipsMultilibs;
  if (VFS.exists(Path + "/mips-r6"))
    MS = &AndroidMipselMultilibs;
  else if (VFS.exists(Path + "/32"))
    MS = &AndroidMips64elMultilibs;
  if (MS->select(Flags, Result.SelectedMultilib)) {
    Result.Multilibs = *MS;
    return true;
  }
  return false;
}

static bool findMipsMuslMultilibs(const Multilib::flags_list &Flags,
                                  FilterNonExistent &NonExistent,
                                  DetectedMultilibs &Result) {
  // Musl toolchain multilibs
  MultilibSet MuslMipsMultilibs;
  {
    auto MArchMipsR2 = makeMultilib("")
                           .osSuffix("/mips-r2-hard-musl")
                           .flag("+EB")
                           .flag("-EL")
                           .flag("+march=mips32r2");

    auto MArchMipselR2 = makeMultilib("/mipsel-r2-hard-musl")
                             .flag("-EB")
                             .flag("+EL")
                             .flag("+march=mips32r2");

    MuslMipsMultilibs = MultilibSet().Either(MArchMipsR2, MArchMipselR2);

    // Specify the callback that computes the include directories.
    MuslMipsMultilibs.setIncludeDirsCallback([](const Multilib &M) {
      return std::vector<std::string>(
          {"/../sysroot" + M.osSuffix() + "/usr/include"});
    });
  }
  if (MuslMipsMultilibs.select(Flags, Result.SelectedMultilib)) {
    Result.Multilibs = MuslMipsMultilibs;
    return true;
  }
  return false;
}

static bool findMipsMtiMultilibs(const Multilib::flags_list &Flags,
                                 FilterNonExistent &NonExistent,
                                 DetectedMultilibs &Result) {
  // CodeScape MTI toolchain v1.2 and early.
  MultilibSet MtiMipsMultilibsV1;
  {
    auto MArchMips32 = makeMultilib("/mips32")
                           .flag("+m32")
                           .flag("-m64")
                           .flag("-mmicromips")
                           .flag("+march=mips32");

    auto MArchMicroMips = makeMultilib("/micromips")
                              .flag("+m32")
                              .flag("-m64")
                              .flag("+mmicromips");

    auto MArchMips64r2 = makeMultilib("/mips64r2")
                             .flag("-m32")
                             .flag("+m64")
                             .flag("+march=mips64r2");

    auto MArchMips64 = makeMultilib("/mips64").flag("-m32").flag("+m64").flag(
        "-march=mips64r2");

    auto MArchDefault = makeMultilib("")
                            .flag("+m32")
                            .flag("-m64")
                            .flag("-mmicromips")
                            .flag("+march=mips32r2");

    auto Mips16 = makeMultilib("/mips16").flag("+mips16");

    auto UCLibc = makeMultilib("/uclibc").flag("+muclibc");

    auto MAbi64 =
        makeMultilib("/64").flag("+mabi=n64").flag("-mabi=n32").flag("-m32");

    auto BigEndian = makeMultilib("").flag("+EB").flag("-EL");

    auto LittleEndian = makeMultilib("/el").flag("+EL").flag("-EB");

    auto SoftFloat = makeMultilib("/sof").flag("+msoft-float");

    auto Nan2008 = makeMultilib("/nan2008").flag("+mnan=2008");

    MtiMipsMultilibsV1 =
        MultilibSet()
            .Either(MArchMips32, MArchMicroMips, MArchMips64r2, MArchMips64,
                    MArchDefault)
            .Maybe(UCLibc)
            .Maybe(Mips16)
            .FilterOut("/mips64/mips16")
            .FilterOut("/mips64r2/mips16")
            .FilterOut("/micromips/mips16")
            .Maybe(MAbi64)
            .FilterOut("/micromips/64")
            .FilterOut("/mips32/64")
            .FilterOut("^/64")
            .FilterOut("/mips16/64")
            .Either(BigEndian, LittleEndian)
            .Maybe(SoftFloat)
            .Maybe(Nan2008)
            .FilterOut(".*sof/nan2008")
            .FilterOut(NonExistent)
            .setIncludeDirsCallback([](const Multilib &M) {
              std::vector<std::string> Dirs({"/include"});
              if (StringRef(M.includeSuffix()).startswith("/uclibc"))
                Dirs.push_back("/../../../../sysroot/uclibc/usr/include");
              else
                Dirs.push_back("/../../../../sysroot/usr/include");
              return Dirs;
            });
  }

  // CodeScape IMG toolchain starting from v1.3.
  MultilibSet MtiMipsMultilibsV2;
  {
    auto BeHard = makeMultilib("/mips-r2-hard")
                      .flag("+EB")
                      .flag("-msoft-float")
                      .flag("-mnan=2008")
                      .flag("-muclibc");
    auto BeSoft = makeMultilib("/mips-r2-soft")
                      .flag("+EB")
                      .flag("+msoft-float")
                      .flag("-mnan=2008");
    auto ElHard = makeMultilib("/mipsel-r2-hard")
                      .flag("+EL")
                      .flag("-msoft-float")
                      .flag("-mnan=2008")
                      .flag("-muclibc");
    auto ElSoft = makeMultilib("/mipsel-r2-soft")
                      .flag("+EL")
                      .flag("+msoft-float")
                      .flag("-mnan=2008")
                      .flag("-mmicromips");
    auto BeHardNan = makeMultilib("/mips-r2-hard-nan2008")
                         .flag("+EB")
                         .flag("-msoft-float")
                         .flag("+mnan=2008")
                         .flag("-muclibc");
    auto ElHardNan = makeMultilib("/mipsel-r2-hard-nan2008")
                         .flag("+EL")
                         .flag("-msoft-float")
                         .flag("+mnan=2008")
                         .flag("-muclibc")
                         .flag("-mmicromips");
    auto BeHardNanUclibc = makeMultilib("/mips-r2-hard-nan2008-uclibc")
                               .flag("+EB")
                               .flag("-msoft-float")
                               .flag("+mnan=2008")
                               .flag("+muclibc");
    auto ElHardNanUclibc = makeMultilib("/mipsel-r2-hard-nan2008-uclibc")
                               .flag("+EL")
                               .flag("-msoft-float")
                               .flag("+mnan=2008")
                               .flag("+muclibc");
    auto BeHardUclibc = makeMultilib("/mips-r2-hard-uclibc")
                            .flag("+EB")
                            .flag("-msoft-float")
                            .flag("-mnan=2008")
                            .flag("+muclibc");
    auto ElHardUclibc = makeMultilib("/mipsel-r2-hard-uclibc")
                            .flag("+EL")
                            .flag("-msoft-float")
                            .flag("-mnan=2008")
                            .flag("+muclibc");
    auto ElMicroHardNan = makeMultilib("/micromipsel-r2-hard-nan2008")
                              .flag("+EL")
                              .flag("-msoft-float")
                              .flag("+mnan=2008")
                              .flag("+mmicromips");
    auto ElMicroSoft = makeMultilib("/micromipsel-r2-soft")
                           .flag("+EL")
                           .flag("+msoft-float")
                           .flag("-mnan=2008")
                           .flag("+mmicromips");

    auto O32 =
        makeMultilib("/lib").osSuffix("").flag("-mabi=n32").flag("-mabi=n64");
    auto N32 =
        makeMultilib("/lib32").osSuffix("").flag("+mabi=n32").flag("-mabi=n64");
    auto N64 =
        makeMultilib("/lib64").osSuffix("").flag("-mabi=n32").flag("+mabi=n64");

    MtiMipsMultilibsV2 =
        MultilibSet()
            .Either({BeHard, BeSoft, ElHard, ElSoft, BeHardNan, ElHardNan,
                     BeHardNanUclibc, ElHardNanUclibc, BeHardUclibc,
                     ElHardUclibc, ElMicroHardNan, ElMicroSoft})
            .Either(O32, N32, N64)
            .FilterOut(NonExistent)
            .setIncludeDirsCallback([](const Multilib &M) {
              return std::vector<std::string>({"/../../../../sysroot" +
                                               M.includeSuffix() +
                                               "/../usr/include"});
            })
            .setFilePathsCallback([](const Multilib &M) {
              return std::vector<std::string>(
                  {"/../../../../mips-mti-linux-gnu/lib" + M.gccSuffix()});
            });
  }
  for (auto Candidate : {&MtiMipsMultilibsV1, &MtiMipsMultilibsV2}) {
    if (Candidate->select(Flags, Result.SelectedMultilib)) {
      Result.Multilibs = *Candidate;
      return true;
    }
  }
  return false;
}

static bool findMipsImgMultilibs(const Multilib::flags_list &Flags,
                                 FilterNonExistent &NonExistent,
                                 DetectedMultilibs &Result) {
  // CodeScape IMG toolchain v1.2 and early.
  MultilibSet ImgMultilibsV1;
  {
    auto Mips64r6 = makeMultilib("/mips64r6").flag("+m64").flag("-m32");

    auto LittleEndian = makeMultilib("/el").flag("+EL").flag("-EB");

    auto MAbi64 =
        makeMultilib("/64").flag("+mabi=n64").flag("-mabi=n32").flag("-m32");

    ImgMultilibsV1 =
        MultilibSet()
            .Maybe(Mips64r6)
            .Maybe(MAbi64)
            .Maybe(LittleEndian)
            .FilterOut(NonExistent)
            .setIncludeDirsCallback([](const Multilib &M) {
              return std::vector<std::string>(
                  {"/include", "/../../../../sysroot/usr/include"});
            });
  }

  // CodeScape IMG toolchain starting from v1.3.
  MultilibSet ImgMultilibsV2;
  {
    auto BeHard = makeMultilib("/mips-r6-hard")
                      .flag("+EB")
                      .flag("-msoft-float")
                      .flag("-mmicromips");
    auto BeSoft = makeMultilib("/mips-r6-soft")
                      .flag("+EB")
                      .flag("+msoft-float")
                      .flag("-mmicromips");
    auto ElHard = makeMultilib("/mipsel-r6-hard")
                      .flag("+EL")
                      .flag("-msoft-float")
                      .flag("-mmicromips");
    auto ElSoft = makeMultilib("/mipsel-r6-soft")
                      .flag("+EL")
                      .flag("+msoft-float")
                      .flag("-mmicromips");
    auto BeMicroHard = makeMultilib("/micromips-r6-hard")
                           .flag("+EB")
                           .flag("-msoft-float")
                           .flag("+mmicromips");
    auto BeMicroSoft = makeMultilib("/micromips-r6-soft")
                           .flag("+EB")
                           .flag("+msoft-float")
                           .flag("+mmicromips");
    auto ElMicroHard = makeMultilib("/micromipsel-r6-hard")
                           .flag("+EL")
                           .flag("-msoft-float")
                           .flag("+mmicromips");
    auto ElMicroSoft = makeMultilib("/micromipsel-r6-soft")
                           .flag("+EL")
                           .flag("+msoft-float")
                           .flag("+mmicromips");

    auto O32 =
        makeMultilib("/lib").osSuffix("").flag("-mabi=n32").flag("-mabi=n64");
    auto N32 =
        makeMultilib("/lib32").osSuffix("").flag("+mabi=n32").flag("-mabi=n64");
    auto N64 =
        makeMultilib("/lib64").osSuffix("").flag("-mabi=n32").flag("+mabi=n64");

    ImgMultilibsV2 =
        MultilibSet()
            .Either({BeHard, BeSoft, ElHard, ElSoft, BeMicroHard, BeMicroSoft,
                     ElMicroHard, ElMicroSoft})
            .Either(O32, N32, N64)
            .FilterOut(NonExistent)
            .setIncludeDirsCallback([](const Multilib &M) {
              return std::vector<std::string>({"/../../../../sysroot" +
                                               M.includeSuffix() +
                                               "/../usr/include"});
            })
            .setFilePathsCallback([](const Multilib &M) {
              return std::vector<std::string>(
                  {"/../../../../mips-img-linux-gnu/lib" + M.gccSuffix()});
            });
  }
  for (auto Candidate : {&ImgMultilibsV1, &ImgMultilibsV2}) {
    if (Candidate->select(Flags, Result.SelectedMultilib)) {
      Result.Multilibs = *Candidate;
      return true;
    }
  }
  return false;
}

bool clang::driver::findMIPSMultilibs(const Driver &D,
                                      const llvm::Triple &TargetTriple,
                                      StringRef Path, const ArgList &Args,
                                      DetectedMultilibs &Result) {
  FilterNonExistent NonExistent(Path, "/crtbegin.o", D.getVFS());

  StringRef CPUName;
  StringRef ABIName;
  tools::mips::getMipsCPUAndABI(Args, TargetTriple, CPUName, ABIName);

  llvm::Triple::ArchType TargetArch = TargetTriple.getArch();

  Multilib::flags_list Flags;
  addMultilibFlag(TargetTriple.isMIPS32(), "m32", Flags);
  addMultilibFlag(TargetTriple.isMIPS64(), "m64", Flags);
  addMultilibFlag(isMips16(Args), "mips16", Flags);
  addMultilibFlag(CPUName == "mips32", "march=mips32", Flags);
  addMultilibFlag(CPUName == "mips32r2" || CPUName == "mips32r3" ||
                      CPUName == "mips32r5" || CPUName == "p5600",
                  "march=mips32r2", Flags);
  addMultilibFlag(CPUName == "mips32r6", "march=mips32r6", Flags);
  addMultilibFlag(CPUName == "mips64", "march=mips64", Flags);
  addMultilibFlag(CPUName == "mips64r2" || CPUName == "mips64r3" ||
                      CPUName == "mips64r5" || CPUName == "octeon",
                  "march=mips64r2", Flags);
  addMultilibFlag(CPUName == "mips64r6", "march=mips64r6", Flags);
  addMultilibFlag(isMicroMips(Args), "mmicromips", Flags);
  addMultilibFlag(tools::mips::isUCLibc(Args), "muclibc", Flags);
  addMultilibFlag(tools::mips::isNaN2008(Args, TargetTriple), "mnan=2008",
                  Flags);
  addMultilibFlag(ABIName == "n32", "mabi=n32", Flags);
  addMultilibFlag(ABIName == "n64", "mabi=n64", Flags);
  addMultilibFlag(isSoftFloatABI(Args), "msoft-float", Flags);
  addMultilibFlag(!isSoftFloatABI(Args), "mhard-float", Flags);
  addMultilibFlag(isMipsEL(TargetArch), "EL", Flags);
  addMultilibFlag(!isMipsEL(TargetArch), "EB", Flags);

  if (TargetTriple.isAndroid())
    return findMipsAndroidMultilibs(D.getVFS(), Path, Flags, NonExistent,
                                    Result);

  if (TargetTriple.getVendor() == llvm::Triple::MipsTechnologies &&
      TargetTriple.getOS() == llvm::Triple::Linux &&
      TargetTriple.getEnvironment() == llvm::Triple::UnknownEnvironment)
    return findMipsMuslMultilibs(Flags, NonExistent, Result);

  if (TargetTriple.getVendor() == llvm::Triple::MipsTechnologies &&
      TargetTriple.getOS() == llvm::Triple::Linux &&
      TargetTriple.isGNUEnvironment())
    return findMipsMtiMultilibs(Flags, NonExistent, Result);

  if (TargetTriple.getVendor() == llvm::Triple::ImaginationTechnologies &&
      TargetTriple.getOS() == llvm::Triple::Linux &&
      TargetTriple.isGNUEnvironment())
    return findMipsImgMultilibs(Flags, NonExistent, Result);

  if (findMipsCsMultilibs(Flags, NonExistent, Result))
    return true;

  // Fallback to the regular toolchain-tree structure.
  Multilib Default;
  Result.Multilibs.push_back(Default);
  Result.Multilibs.FilterOut(NonExistent);

  if (Result.Multilibs.select(Flags, Result.SelectedMultilib)) {
    Result.BiarchSibling = Multilib();
    return true;
  }

  return false;
}

static void findAndroidArmMultilibs(const Driver &D,
                                    const llvm::Triple &TargetTriple,
                                    StringRef Path, const ArgList &Args,
                                    DetectedMultilibs &Result) {
  // Find multilibs with subdirectories like armv7-a, thumb, armv7-a/thumb.
  FilterNonExistent NonExistent(Path, "/crtbegin.o", D.getVFS());
  Multilib ArmV7Multilib = makeMultilib("/armv7-a")
                               .flag("+march=armv7-a")
                               .flag("-mthumb");
  Multilib ThumbMultilib = makeMultilib("/thumb")
                               .flag("-march=armv7-a")
                               .flag("+mthumb");
  Multilib ArmV7ThumbMultilib = makeMultilib("/armv7-a/thumb")
                               .flag("+march=armv7-a")
                               .flag("+mthumb");
  Multilib DefaultMultilib = makeMultilib("")
                               .flag("-march=armv7-a")
                               .flag("-mthumb");
  MultilibSet AndroidArmMultilibs =
      MultilibSet()
          .Either(ThumbMultilib, ArmV7Multilib,
                  ArmV7ThumbMultilib, DefaultMultilib)
          .FilterOut(NonExistent);

  Multilib::flags_list Flags;
  llvm::StringRef Arch = Args.getLastArgValue(options::OPT_march_EQ);
  bool IsArmArch = TargetTriple.getArch() == llvm::Triple::arm;
  bool IsThumbArch = TargetTriple.getArch() == llvm::Triple::thumb;
  bool IsV7SubArch = TargetTriple.getSubArch() == llvm::Triple::ARMSubArch_v7;
  bool IsThumbMode = IsThumbArch ||
      Args.hasFlag(options::OPT_mthumb, options::OPT_mno_thumb, false) ||
      (IsArmArch && llvm::ARM::parseArchISA(Arch) == llvm::ARM::ISAKind::THUMB);
  bool IsArmV7Mode = (IsArmArch || IsThumbArch) &&
      (llvm::ARM::parseArchVersion(Arch) == 7 ||
       (IsArmArch && Arch == "" && IsV7SubArch));
  addMultilibFlag(IsArmV7Mode, "march=armv7-a", Flags);
  addMultilibFlag(IsThumbMode, "mthumb", Flags);

  if (AndroidArmMultilibs.select(Flags, Result.SelectedMultilib))
    Result.Multilibs = AndroidArmMultilibs;
}

static bool findMSP430Multilibs(const Driver &D,
                                const llvm::Triple &TargetTriple,
                                StringRef Path, const ArgList &Args,
                                DetectedMultilibs &Result) {
  FilterNonExistent NonExistent(Path, "/crtbegin.o", D.getVFS());
  Multilib MSP430Multilib = makeMultilib("/430");
  // FIXME: when clang starts to support msp430x ISA additional logic
  // to select between multilib must be implemented
  // Multilib MSP430xMultilib = makeMultilib("/large");

  Result.Multilibs.push_back(MSP430Multilib);
  Result.Multilibs.FilterOut(NonExistent);

  Multilib::flags_list Flags;
  if (Result.Multilibs.select(Flags, Result.SelectedMultilib))
    return true;

  return false;
}

static void findRISCVMultilibs(const Driver &D,
                               const llvm::Triple &TargetTriple, StringRef Path,
                               const ArgList &Args, DetectedMultilibs &Result) {

  FilterNonExistent NonExistent(Path, "/crtbegin.o", D.getVFS());
  Multilib Ilp32 = makeMultilib("lib32/ilp32").flag("+m32").flag("+mabi=ilp32");
  Multilib Ilp32f =
      makeMultilib("lib32/ilp32f").flag("+m32").flag("+mabi=ilp32f");
  Multilib Ilp32d =
      makeMultilib("lib32/ilp32d").flag("+m32").flag("+mabi=ilp32d");
  Multilib Lp64 = makeMultilib("lib64/lp64").flag("+m64").flag("+mabi=lp64");
  Multilib Lp64f = makeMultilib("lib64/lp64f").flag("+m64").flag("+mabi=lp64f");
  Multilib Lp64d = makeMultilib("lib64/lp64d").flag("+m64").flag("+mabi=lp64d");
  MultilibSet RISCVMultilibs =
      MultilibSet()
          .Either({Ilp32, Ilp32f, Ilp32d, Lp64, Lp64f, Lp64d})
          .FilterOut(NonExistent);

  Multilib::flags_list Flags;
  bool IsRV64 = TargetTriple.getArch() == llvm::Triple::riscv64;
  StringRef ABIName = tools::riscv::getRISCVABI(Args, TargetTriple);

  addMultilibFlag(!IsRV64, "m32", Flags);
  addMultilibFlag(IsRV64, "m64", Flags);
  addMultilibFlag(ABIName == "ilp32", "mabi=ilp32", Flags);
  addMultilibFlag(ABIName == "ilp32f", "mabi=ilp32f", Flags);
  addMultilibFlag(ABIName == "ilp32d", "mabi=ilp32d", Flags);
  addMultilibFlag(ABIName == "lp64", "mabi=lp64", Flags);
  addMultilibFlag(ABIName == "lp64f", "mabi=lp64f", Flags);
  addMultilibFlag(ABIName == "lp64d", "mabi=lp64d", Flags);

  if (RISCVMultilibs.select(Flags, Result.SelectedMultilib))
    Result.Multilibs = RISCVMultilibs;
}

static bool findBiarchMultilibs(const Driver &D,
                                const llvm::Triple &TargetTriple,
                                StringRef Path, const ArgList &Args,
                                bool NeedsBiarchSuffix,
                                DetectedMultilibs &Result) {
  Multilib Default;

  // Some versions of SUSE and Fedora on ppc64 put 32-bit libs
  // in what would normally be GCCInstallPath and put the 64-bit
  // libs in a subdirectory named 64. The simple logic we follow is that
  // *if* there is a subdirectory of the right name with crtbegin.o in it,
  // we use that. If not, and if not a biarch triple alias, we look for
  // crtbegin.o without the subdirectory.

  StringRef Suff64 = "/64";
  // Solaris uses platform-specific suffixes instead of /64.
  if (TargetTriple.getOS() == llvm::Triple::Solaris) {
    switch (TargetTriple.getArch()) {
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      Suff64 = "/amd64";
      break;
    case llvm::Triple::sparc:
    case llvm::Triple::sparcv9:
      Suff64 = "/sparcv9";
      break;
    default:
      break;
    }
  }

  Multilib Alt64 = Multilib()
                       .gccSuffix(Suff64)
                       .includeSuffix(Suff64)
                       .flag("-m32")
                       .flag("+m64")
                       .flag("-mx32");
  Multilib Alt32 = Multilib()
                       .gccSuffix("/32")
                       .includeSuffix("/32")
                       .flag("+m32")
                       .flag("-m64")
                       .flag("-mx32");
  Multilib Altx32 = Multilib()
                        .gccSuffix("/x32")
                        .includeSuffix("/x32")
                        .flag("-m32")
                        .flag("-m64")
                        .flag("+mx32");

  // GCC toolchain for IAMCU doesn't have crtbegin.o, so look for libgcc.a.
  FilterNonExistent NonExistent(
      Path, TargetTriple.isOSIAMCU() ? "/libgcc.a" : "/crtbegin.o", D.getVFS());

  // Determine default multilib from: 32, 64, x32
  // Also handle cases such as 64 on 32, 32 on 64, etc.
  enum { UNKNOWN, WANT32, WANT64, WANTX32 } Want = UNKNOWN;
  const bool IsX32 = TargetTriple.getEnvironment() == llvm::Triple::GNUX32;
  if (TargetTriple.isArch32Bit() && !NonExistent(Alt32))
    Want = WANT64;
  else if (TargetTriple.isArch64Bit() && IsX32 && !NonExistent(Altx32))
    Want = WANT64;
  else if (TargetTriple.isArch64Bit() && !IsX32 && !NonExistent(Alt64))
    Want = WANT32;
  else {
    if (TargetTriple.isArch32Bit())
      Want = NeedsBiarchSuffix ? WANT64 : WANT32;
    else if (IsX32)
      Want = NeedsBiarchSuffix ? WANT64 : WANTX32;
    else
      Want = NeedsBiarchSuffix ? WANT32 : WANT64;
  }

  if (Want == WANT32)
    Default.flag("+m32").flag("-m64").flag("-mx32");
  else if (Want == WANT64)
    Default.flag("-m32").flag("+m64").flag("-mx32");
  else if (Want == WANTX32)
    Default.flag("-m32").flag("-m64").flag("+mx32");
  else
    return false;

  Result.Multilibs.push_back(Default);
  Result.Multilibs.push_back(Alt64);
  Result.Multilibs.push_back(Alt32);
  Result.Multilibs.push_back(Altx32);

  Result.Multilibs.FilterOut(NonExistent);

  Multilib::flags_list Flags;
  addMultilibFlag(TargetTriple.isArch64Bit() && !IsX32, "m64", Flags);
  addMultilibFlag(TargetTriple.isArch32Bit(), "m32", Flags);
  addMultilibFlag(TargetTriple.isArch64Bit() && IsX32, "mx32", Flags);

  if (!Result.Multilibs.select(Flags, Result.SelectedMultilib))
    return false;

  if (Result.SelectedMultilib == Alt64 || Result.SelectedMultilib == Alt32 ||
      Result.SelectedMultilib == Altx32)
    Result.BiarchSibling = Default;

  return true;
}

/// Generic_GCC - A tool chain using the 'gcc' command to perform
/// all subcommands; this relies on gcc translating the majority of
/// command line options.

/// Less-than for GCCVersion, implementing a Strict Weak Ordering.
bool Generic_GCC::GCCVersion::isOlderThan(int RHSMajor, int RHSMinor,
                                          int RHSPatch,
                                          StringRef RHSPatchSuffix) const {
  if (Major != RHSMajor)
    return Major < RHSMajor;
  if (Minor != RHSMinor)
    return Minor < RHSMinor;
  if (Patch != RHSPatch) {
    // Note that versions without a specified patch sort higher than those with
    // a patch.
    if (RHSPatch == -1)
      return true;
    if (Patch == -1)
      return false;

    // Otherwise just sort on the patch itself.
    return Patch < RHSPatch;
  }
  if (PatchSuffix != RHSPatchSuffix) {
    // Sort empty suffixes higher.
    if (RHSPatchSuffix.empty())
      return true;
    if (PatchSuffix.empty())
      return false;

    // Provide a lexicographic sort to make this a total ordering.
    return PatchSuffix < RHSPatchSuffix;
  }

  // The versions are equal.
  return false;
}

/// Parse a GCCVersion object out of a string of text.
///
/// This is the primary means of forming GCCVersion objects.
/*static*/
Generic_GCC::GCCVersion Generic_GCC::GCCVersion::Parse(StringRef VersionText) {
  const GCCVersion BadVersion = {VersionText.str(), -1, -1, -1, "", "", ""};
  std::pair<StringRef, StringRef> First = VersionText.split('.');
  std::pair<StringRef, StringRef> Second = First.second.split('.');

  GCCVersion GoodVersion = {VersionText.str(), -1, -1, -1, "", "", ""};
  if (First.first.getAsInteger(10, GoodVersion.Major) || GoodVersion.Major < 0)
    return BadVersion;
  GoodVersion.MajorStr = First.first.str();
  if (First.second.empty())
    return GoodVersion;
  StringRef MinorStr = Second.first;
  if (Second.second.empty()) {
    if (size_t EndNumber = MinorStr.find_first_not_of("0123456789")) {
      GoodVersion.PatchSuffix = MinorStr.substr(EndNumber);
      MinorStr = MinorStr.slice(0, EndNumber);
    }
  }
  if (MinorStr.getAsInteger(10, GoodVersion.Minor) || GoodVersion.Minor < 0)
    return BadVersion;
  GoodVersion.MinorStr = MinorStr.str();

  // First look for a number prefix and parse that if present. Otherwise just
  // stash the entire patch string in the suffix, and leave the number
  // unspecified. This covers versions strings such as:
  //   5        (handled above)
  //   4.4
  //   4.4-patched
  //   4.4.0
  //   4.4.x
  //   4.4.2-rc4
  //   4.4.x-patched
  // And retains any patch number it finds.
  StringRef PatchText = Second.second;
  if (!PatchText.empty()) {
    if (size_t EndNumber = PatchText.find_first_not_of("0123456789")) {
      // Try to parse the number and any suffix.
      if (PatchText.slice(0, EndNumber).getAsInteger(10, GoodVersion.Patch) ||
          GoodVersion.Patch < 0)
        return BadVersion;
      GoodVersion.PatchSuffix = PatchText.substr(EndNumber);
    }
  }

  return GoodVersion;
}

static llvm::StringRef getGCCToolchainDir(const ArgList &Args,
                                          llvm::StringRef SysRoot) {
  const Arg *A = Args.getLastArg(clang::driver::options::OPT_gcc_toolchain);
  if (A)
    return A->getValue();

  // If we have a SysRoot, ignore GCC_INSTALL_PREFIX.
  // GCC_INSTALL_PREFIX specifies the gcc installation for the default
  // sysroot and is likely not valid with a different sysroot.
  if (!SysRoot.empty())
    return "";

  return GCC_INSTALL_PREFIX;
}

/// Initialize a GCCInstallationDetector from the driver.
///
/// This performs all of the autodetection and sets up the various paths.
/// Once constructed, a GCCInstallationDetector is essentially immutable.
///
/// FIXME: We shouldn't need an explicit TargetTriple parameter here, and
/// should instead pull the target out of the driver. This is currently
/// necessary because the driver doesn't store the final version of the target
/// triple.
void Generic_GCC::GCCInstallationDetector::init(
    const llvm::Triple &TargetTriple, const ArgList &Args,
    ArrayRef<std::string> ExtraTripleAliases) {
  llvm::Triple BiarchVariantTriple = TargetTriple.isArch32Bit()
                                         ? TargetTriple.get64BitArchVariant()
                                         : TargetTriple.get32BitArchVariant();
  // The library directories which may contain GCC installations.
  SmallVector<StringRef, 4> CandidateLibDirs, CandidateBiarchLibDirs;
  // The compatible GCC triples for this particular architecture.
  SmallVector<StringRef, 16> CandidateTripleAliases;
  SmallVector<StringRef, 16> CandidateBiarchTripleAliases;
  CollectLibDirsAndTriples(TargetTriple, BiarchVariantTriple, CandidateLibDirs,
                           CandidateTripleAliases, CandidateBiarchLibDirs,
                           CandidateBiarchTripleAliases);

  // Compute the set of prefixes for our search.
  SmallVector<std::string, 8> Prefixes(D.PrefixDirs.begin(),
                                       D.PrefixDirs.end());

  StringRef GCCToolchainDir = getGCCToolchainDir(Args, D.SysRoot);
  if (GCCToolchainDir != "") {
    if (GCCToolchainDir.back() == '/')
      GCCToolchainDir = GCCToolchainDir.drop_back(); // remove the /

    Prefixes.push_back(GCCToolchainDir);
  } else {
    // If we have a SysRoot, try that first.
    if (!D.SysRoot.empty()) {
      Prefixes.push_back(D.SysRoot);
      AddDefaultGCCPrefixes(TargetTriple, Prefixes, D.SysRoot);
    }

    // Then look for gcc installed alongside clang.
    Prefixes.push_back(D.InstalledDir + "/..");

    // Next, look for prefix(es) that correspond to distribution-supplied gcc
    // installations.
    if (D.SysRoot.empty()) {
      // Typically /usr.
      AddDefaultGCCPrefixes(TargetTriple, Prefixes, D.SysRoot);
    }
  }

  // Try to respect gcc-config on Gentoo. However, do that only
  // if --gcc-toolchain is not provided or equal to the Gentoo install
  // in /usr. This avoids accidentally enforcing the system GCC version
  // when using a custom toolchain.
  if (GCCToolchainDir == "" || GCCToolchainDir == D.SysRoot + "/usr") {
    SmallVector<StringRef, 16> GentooTestTriples;
    // Try to match an exact triple as target triple first.
    // e.g. crossdev -S x86_64-gentoo-linux-gnu will install gcc libs for
    // x86_64-gentoo-linux-gnu. But "clang -target x86_64-gentoo-linux-gnu"
    // may pick the libraries for x86_64-pc-linux-gnu even when exact matching
    // triple x86_64-gentoo-linux-gnu is present.
    GentooTestTriples.push_back(TargetTriple.str());
    // Check rest of triples.
    GentooTestTriples.append(ExtraTripleAliases.begin(),
                             ExtraTripleAliases.end());
    GentooTestTriples.append(CandidateTripleAliases.begin(),
                             CandidateTripleAliases.end());
    if (ScanGentooConfigs(TargetTriple, Args, GentooTestTriples,
                          CandidateBiarchTripleAliases))
      return;
  }

  // Loop over the various components which exist and select the best GCC
  // installation available. GCC installs are ranked by version number.
  Version = GCCVersion::Parse("0.0.0");
  for (const std::string &Prefix : Prefixes) {
    if (!D.getVFS().exists(Prefix))
      continue;
    for (StringRef Suffix : CandidateLibDirs) {
      const std::string LibDir = Prefix + Suffix.str();
      if (!D.getVFS().exists(LibDir))
        continue;
      // Try to match the exact target triple first.
      ScanLibDirForGCCTriple(TargetTriple, Args, LibDir, TargetTriple.str());
      // Try rest of possible triples.
      for (StringRef Candidate : ExtraTripleAliases) // Try these first.
        ScanLibDirForGCCTriple(TargetTriple, Args, LibDir, Candidate);
      for (StringRef Candidate : CandidateTripleAliases)
        ScanLibDirForGCCTriple(TargetTriple, Args, LibDir, Candidate);
    }
    for (StringRef Suffix : CandidateBiarchLibDirs) {
      const std::string LibDir = Prefix + Suffix.str();
      if (!D.getVFS().exists(LibDir))
        continue;
      for (StringRef Candidate : CandidateBiarchTripleAliases)
        ScanLibDirForGCCTriple(TargetTriple, Args, LibDir, Candidate,
                               /*NeedsBiarchSuffix=*/ true);
    }
  }
}

void Generic_GCC::GCCInstallationDetector::print(raw_ostream &OS) const {
  for (const auto &InstallPath : CandidateGCCInstallPaths)
    OS << "Found candidate GCC installation: " << InstallPath << "\n";

  if (!GCCInstallPath.empty())
    OS << "Selected GCC installation: " << GCCInstallPath << "\n";

  for (const auto &Multilib : Multilibs)
    OS << "Candidate multilib: " << Multilib << "\n";

  if (Multilibs.size() != 0 || !SelectedMultilib.isDefault())
    OS << "Selected multilib: " << SelectedMultilib << "\n";
}

bool Generic_GCC::GCCInstallationDetector::getBiarchSibling(Multilib &M) const {
  if (BiarchSibling.hasValue()) {
    M = BiarchSibling.getValue();
    return true;
  }
  return false;
}

void Generic_GCC::GCCInstallationDetector::AddDefaultGCCPrefixes(
    const llvm::Triple &TargetTriple, SmallVectorImpl<std::string> &Prefixes,
    StringRef SysRoot) {
  if (TargetTriple.getOS() == llvm::Triple::Solaris) {
    // Solaris is a special case.
    // The GCC installation is under
    //   /usr/gcc/<major>.<minor>/lib/gcc/<triple>/<major>.<minor>.<patch>/
    // so we need to find those /usr/gcc/*/lib/gcc libdirs and go with
    // /usr/gcc/<version> as a prefix.

    std::string PrefixDir = SysRoot.str() + "/usr/gcc";
    std::error_code EC;
    for (llvm::vfs::directory_iterator LI = D.getVFS().dir_begin(PrefixDir, EC),
                                       LE;
         !EC && LI != LE; LI = LI.increment(EC)) {
      StringRef VersionText = llvm::sys::path::filename(LI->path());
      GCCVersion CandidateVersion = GCCVersion::Parse(VersionText);

      // Filter out obviously bad entries.
      if (CandidateVersion.Major == -1 || CandidateVersion.isOlderThan(4, 1, 1))
        continue;

      std::string CandidatePrefix = PrefixDir + "/" + VersionText.str();
      std::string CandidateLibPath = CandidatePrefix + "/lib/gcc";
      if (!D.getVFS().exists(CandidateLibPath))
        continue;

      Prefixes.push_back(CandidatePrefix);
    }
    return;
  }

  // Non-Solaris is much simpler - most systems just go with "/usr".
  if (SysRoot.empty() && TargetTriple.getOS() == llvm::Triple::Linux) {
    // Yet, still look for RHEL devtoolsets.
    Prefixes.push_back("/opt/rh/devtoolset-7/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-6/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-4/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-3/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-2/root/usr");
  }
  Prefixes.push_back(SysRoot.str() + "/usr");
}

/*static*/ void Generic_GCC::GCCInstallationDetector::CollectLibDirsAndTriples(
    const llvm::Triple &TargetTriple, const llvm::Triple &BiarchTriple,
    SmallVectorImpl<StringRef> &LibDirs,
    SmallVectorImpl<StringRef> &TripleAliases,
    SmallVectorImpl<StringRef> &BiarchLibDirs,
    SmallVectorImpl<StringRef> &BiarchTripleAliases) {
  // Declare a bunch of static data sets that we'll select between below. These
  // are specifically designed to always refer to string literals to avoid any
  // lifetime or initialization issues.
  static const char *const AArch64LibDirs[] = {"/lib64", "/lib"};
  static const char *const AArch64Triples[] = {
      "aarch64-none-linux-gnu", "aarch64-linux-gnu", "aarch64-redhat-linux",
      "aarch64-suse-linux", "aarch64-linux-android"};
  static const char *const AArch64beLibDirs[] = {"/lib"};
  static const char *const AArch64beTriples[] = {"aarch64_be-none-linux-gnu",
                                                 "aarch64_be-linux-gnu"};

  static const char *const ARMLibDirs[] = {"/lib"};
  static const char *const ARMTriples[] = {"arm-linux-gnueabi",
                                           "arm-linux-androideabi"};
  static const char *const ARMHFTriples[] = {"arm-linux-gnueabihf",
                                             "armv7hl-redhat-linux-gnueabi",
                                             "armv6hl-suse-linux-gnueabi",
                                             "armv7hl-suse-linux-gnueabi"};
  static const char *const ARMebLibDirs[] = {"/lib"};
  static const char *const ARMebTriples[] = {"armeb-linux-gnueabi",
                                             "armeb-linux-androideabi"};
  static const char *const ARMebHFTriples[] = {
      "armeb-linux-gnueabihf", "armebv7hl-redhat-linux-gnueabi"};

  static const char *const X86_64LibDirs[] = {"/lib64", "/lib"};
  static const char *const X86_64Triples[] = {
      "x86_64-linux-gnu",       "x86_64-unknown-linux-gnu",
      "x86_64-pc-linux-gnu",    "x86_64-redhat-linux6E",
      "x86_64-redhat-linux",    "x86_64-suse-linux",
      "x86_64-manbo-linux-gnu", "x86_64-linux-gnu",
      "x86_64-slackware-linux", "x86_64-unknown-linux",
      "x86_64-amazon-linux",    "x86_64-linux-android"};
  static const char *const X32LibDirs[] = {"/libx32"};
  static const char *const X86LibDirs[] = {"/lib32", "/lib"};
  static const char *const X86Triples[] = {
      "i686-linux-gnu",       "i686-pc-linux-gnu",     "i486-linux-gnu",
      "i386-linux-gnu",       "i386-redhat-linux6E",   "i686-redhat-linux",
      "i586-redhat-linux",    "i386-redhat-linux",     "i586-suse-linux",
      "i486-slackware-linux", "i686-montavista-linux", "i586-linux-gnu",
      "i686-linux-android",   "i386-gnu",              "i486-gnu",
      "i586-gnu",             "i686-gnu"};

  static const char *const MIPSLibDirs[] = {"/lib"};
  static const char *const MIPSTriples[] = {
      "mips-linux-gnu", "mips-mti-linux", "mips-mti-linux-gnu",
      "mips-img-linux-gnu", "mipsisa32r6-linux-gnu"};
  static const char *const MIPSELLibDirs[] = {"/lib"};
  static const char *const MIPSELTriples[] = {
      "mipsel-linux-gnu", "mips-img-linux-gnu", "mipsisa32r6el-linux-gnu",
      "mipsel-linux-android"};

  static const char *const MIPS64LibDirs[] = {"/lib64", "/lib"};
  static const char *const MIPS64Triples[] = {
      "mips64-linux-gnu",      "mips-mti-linux-gnu",
      "mips-img-linux-gnu",    "mips64-linux-gnuabi64",
      "mipsisa64r6-linux-gnu", "mipsisa64r6-linux-gnuabi64"};
  static const char *const MIPS64ELLibDirs[] = {"/lib64", "/lib"};
  static const char *const MIPS64ELTriples[] = {
      "mips64el-linux-gnu",      "mips-mti-linux-gnu",
      "mips-img-linux-gnu",      "mips64el-linux-gnuabi64",
      "mipsisa64r6el-linux-gnu", "mipsisa64r6el-linux-gnuabi64",
      "mips64el-linux-android"};

  static const char *const MIPSN32LibDirs[] = {"/lib32"};
  static const char *const MIPSN32Triples[] = {"mips64-linux-gnuabin32",
                                               "mipsisa64r6-linux-gnuabin32"};
  static const char *const MIPSN32ELLibDirs[] = {"/lib32"};
  static const char *const MIPSN32ELTriples[] = {
      "mips64el-linux-gnuabin32", "mipsisa64r6el-linux-gnuabin32"};

  static const char *const MSP430LibDirs[] = {"/lib"};
  static const char *const MSP430Triples[] = {"msp430-elf"};

  static const char *const PPCLibDirs[] = {"/lib32", "/lib"};
  static const char *const PPCTriples[] = {
      "powerpc-linux-gnu", "powerpc-unknown-linux-gnu", "powerpc-linux-gnuspe",
      "powerpc-suse-linux", "powerpc-montavista-linuxspe"};
  static const char *const PPC64LibDirs[] = {"/lib64", "/lib"};
  static const char *const PPC64Triples[] = {
      "powerpc64-linux-gnu", "powerpc64-unknown-linux-gnu",
      "powerpc64-suse-linux", "ppc64-redhat-linux"};
  static const char *const PPC64LELibDirs[] = {"/lib64", "/lib"};
  static const char *const PPC64LETriples[] = {
      "powerpc64le-linux-gnu", "powerpc64le-unknown-linux-gnu",
      "powerpc64le-suse-linux", "ppc64le-redhat-linux"};

  static const char *const RISCV32LibDirs[] = {"/lib", "/lib32"};
  static const char *const RISCVTriples[] = {"riscv32-unknown-linux-gnu",
                                             "riscv64-unknown-linux-gnu",
                                             "riscv32-unknown-elf"};

  static const char *const SPARCv8LibDirs[] = {"/lib32", "/lib"};
  static const char *const SPARCv8Triples[] = {"sparc-linux-gnu",
                                               "sparcv8-linux-gnu"};
  static const char *const SPARCv9LibDirs[] = {"/lib64", "/lib"};
  static const char *const SPARCv9Triples[] = {"sparc64-linux-gnu",
                                               "sparcv9-linux-gnu"};

  static const char *const SystemZLibDirs[] = {"/lib64", "/lib"};
  static const char *const SystemZTriples[] = {
      "s390x-linux-gnu", "s390x-unknown-linux-gnu", "s390x-ibm-linux-gnu",
      "s390x-suse-linux", "s390x-redhat-linux"};


  using std::begin;
  using std::end;

  if (TargetTriple.getOS() == llvm::Triple::Solaris) {
    static const char *const SolarisLibDirs[] = {"/lib"};
    static const char *const SolarisSparcV8Triples[] = {
        "sparc-sun-solaris2.11", "sparc-sun-solaris2.12"};
    static const char *const SolarisSparcV9Triples[] = {
        "sparcv9-sun-solaris2.11", "sparcv9-sun-solaris2.12"};
    static const char *const SolarisX86Triples[] = {"i386-pc-solaris2.11",
                                                    "i386-pc-solaris2.12"};
    static const char *const SolarisX86_64Triples[] = {"x86_64-pc-solaris2.11",
                                                       "x86_64-pc-solaris2.12"};
    LibDirs.append(begin(SolarisLibDirs), end(SolarisLibDirs));
    BiarchLibDirs.append(begin(SolarisLibDirs), end(SolarisLibDirs));
    switch (TargetTriple.getArch()) {
    case llvm::Triple::x86:
      TripleAliases.append(begin(SolarisX86Triples), end(SolarisX86Triples));
      BiarchTripleAliases.append(begin(SolarisX86_64Triples),
                                 end(SolarisX86_64Triples));
      break;
    case llvm::Triple::x86_64:
      TripleAliases.append(begin(SolarisX86_64Triples),
                           end(SolarisX86_64Triples));
      BiarchTripleAliases.append(begin(SolarisX86Triples),
                                 end(SolarisX86Triples));
      break;
    case llvm::Triple::sparc:
      TripleAliases.append(begin(SolarisSparcV8Triples),
                           end(SolarisSparcV8Triples));
      BiarchTripleAliases.append(begin(SolarisSparcV9Triples),
                                 end(SolarisSparcV9Triples));
      break;
    case llvm::Triple::sparcv9:
      TripleAliases.append(begin(SolarisSparcV9Triples),
                           end(SolarisSparcV9Triples));
      BiarchTripleAliases.append(begin(SolarisSparcV8Triples),
                                 end(SolarisSparcV8Triples));
      break;
    default:
      break;
    }
    return;
  }

  // Android targets should not use GNU/Linux tools or libraries.
  if (TargetTriple.isAndroid()) {
    static const char *const AArch64AndroidTriples[] = {
        "aarch64-linux-android"};
    static const char *const ARMAndroidTriples[] = {"arm-linux-androideabi"};
    static const char *const MIPSELAndroidTriples[] = {"mipsel-linux-android"};
    static const char *const MIPS64ELAndroidTriples[] = {
        "mips64el-linux-android"};
    static const char *const X86AndroidTriples[] = {"i686-linux-android"};
    static const char *const X86_64AndroidTriples[] = {"x86_64-linux-android"};

    switch (TargetTriple.getArch()) {
    case llvm::Triple::aarch64:
      LibDirs.append(begin(AArch64LibDirs), end(AArch64LibDirs));
      TripleAliases.append(begin(AArch64AndroidTriples),
                           end(AArch64AndroidTriples));
      break;
    case llvm::Triple::arm:
    case llvm::Triple::thumb:
      LibDirs.append(begin(ARMLibDirs), end(ARMLibDirs));
      TripleAliases.append(begin(ARMAndroidTriples), end(ARMAndroidTriples));
      break;
    case llvm::Triple::mipsel:
      LibDirs.append(begin(MIPSELLibDirs), end(MIPSELLibDirs));
      TripleAliases.append(begin(MIPSELAndroidTriples),
                           end(MIPSELAndroidTriples));
      BiarchLibDirs.append(begin(MIPS64ELLibDirs), end(MIPS64ELLibDirs));
      BiarchTripleAliases.append(begin(MIPS64ELAndroidTriples),
                                 end(MIPS64ELAndroidTriples));
      break;
    case llvm::Triple::mips64el:
      LibDirs.append(begin(MIPS64ELLibDirs), end(MIPS64ELLibDirs));
      TripleAliases.append(begin(MIPS64ELAndroidTriples),
                           end(MIPS64ELAndroidTriples));
      BiarchLibDirs.append(begin(MIPSELLibDirs), end(MIPSELLibDirs));
      BiarchTripleAliases.append(begin(MIPSELAndroidTriples),
                                 end(MIPSELAndroidTriples));
      break;
    case llvm::Triple::x86_64:
      LibDirs.append(begin(X86_64LibDirs), end(X86_64LibDirs));
      TripleAliases.append(begin(X86_64AndroidTriples),
                           end(X86_64AndroidTriples));
      BiarchLibDirs.append(begin(X86LibDirs), end(X86LibDirs));
      BiarchTripleAliases.append(begin(X86AndroidTriples),
                                 end(X86AndroidTriples));
      break;
    case llvm::Triple::x86:
      LibDirs.append(begin(X86LibDirs), end(X86LibDirs));
      TripleAliases.append(begin(X86AndroidTriples), end(X86AndroidTriples));
      BiarchLibDirs.append(begin(X86_64LibDirs), end(X86_64LibDirs));
      BiarchTripleAliases.append(begin(X86_64AndroidTriples),
                                 end(X86_64AndroidTriples));
      break;
    default:
      break;
    }

    return;
  }

  switch (TargetTriple.getArch()) {
  case llvm::Triple::aarch64:
    LibDirs.append(begin(AArch64LibDirs), end(AArch64LibDirs));
    TripleAliases.append(begin(AArch64Triples), end(AArch64Triples));
    BiarchLibDirs.append(begin(AArch64LibDirs), end(AArch64LibDirs));
    BiarchTripleAliases.append(begin(AArch64Triples), end(AArch64Triples));
    break;
  case llvm::Triple::aarch64_be:
    LibDirs.append(begin(AArch64beLibDirs), end(AArch64beLibDirs));
    TripleAliases.append(begin(AArch64beTriples), end(AArch64beTriples));
    BiarchLibDirs.append(begin(AArch64beLibDirs), end(AArch64beLibDirs));
    BiarchTripleAliases.append(begin(AArch64beTriples), end(AArch64beTriples));
    break;
  case llvm::Triple::arm:
  case llvm::Triple::thumb:
    LibDirs.append(begin(ARMLibDirs), end(ARMLibDirs));
    if (TargetTriple.getEnvironment() == llvm::Triple::GNUEABIHF) {
      TripleAliases.append(begin(ARMHFTriples), end(ARMHFTriples));
    } else {
      TripleAliases.append(begin(ARMTriples), end(ARMTriples));
    }
    break;
  case llvm::Triple::armeb:
  case llvm::Triple::thumbeb:
    LibDirs.append(begin(ARMebLibDirs), end(ARMebLibDirs));
    if (TargetTriple.getEnvironment() == llvm::Triple::GNUEABIHF) {
      TripleAliases.append(begin(ARMebHFTriples), end(ARMebHFTriples));
    } else {
      TripleAliases.append(begin(ARMebTriples), end(ARMebTriples));
    }
    break;
  case llvm::Triple::x86_64:
    LibDirs.append(begin(X86_64LibDirs), end(X86_64LibDirs));
    TripleAliases.append(begin(X86_64Triples), end(X86_64Triples));
    // x32 is always available when x86_64 is available, so adding it as
    // secondary arch with x86_64 triples
    if (TargetTriple.getEnvironment() == llvm::Triple::GNUX32) {
      BiarchLibDirs.append(begin(X32LibDirs), end(X32LibDirs));
      BiarchTripleAliases.append(begin(X86_64Triples), end(X86_64Triples));
    } else {
      BiarchLibDirs.append(begin(X86LibDirs), end(X86LibDirs));
      BiarchTripleAliases.append(begin(X86Triples), end(X86Triples));
    }
    break;
  case llvm::Triple::x86:
    LibDirs.append(begin(X86LibDirs), end(X86LibDirs));
    // MCU toolchain is 32 bit only and its triple alias is TargetTriple
    // itself, which will be appended below.
    if (!TargetTriple.isOSIAMCU()) {
      TripleAliases.append(begin(X86Triples), end(X86Triples));
      BiarchLibDirs.append(begin(X86_64LibDirs), end(X86_64LibDirs));
      BiarchTripleAliases.append(begin(X86_64Triples), end(X86_64Triples));
    }
    break;
  case llvm::Triple::mips:
    LibDirs.append(begin(MIPSLibDirs), end(MIPSLibDirs));
    TripleAliases.append(begin(MIPSTriples), end(MIPSTriples));
    BiarchLibDirs.append(begin(MIPS64LibDirs), end(MIPS64LibDirs));
    BiarchTripleAliases.append(begin(MIPS64Triples), end(MIPS64Triples));
    BiarchLibDirs.append(begin(MIPSN32LibDirs), end(MIPSN32LibDirs));
    BiarchTripleAliases.append(begin(MIPSN32Triples), end(MIPSN32Triples));
    break;
  case llvm::Triple::mipsel:
    LibDirs.append(begin(MIPSELLibDirs), end(MIPSELLibDirs));
    TripleAliases.append(begin(MIPSELTriples), end(MIPSELTriples));
    TripleAliases.append(begin(MIPSTriples), end(MIPSTriples));
    BiarchLibDirs.append(begin(MIPS64ELLibDirs), end(MIPS64ELLibDirs));
    BiarchTripleAliases.append(begin(MIPS64ELTriples), end(MIPS64ELTriples));
    BiarchLibDirs.append(begin(MIPSN32ELLibDirs), end(MIPSN32ELLibDirs));
    BiarchTripleAliases.append(begin(MIPSN32ELTriples), end(MIPSN32ELTriples));
    break;
  case llvm::Triple::mips64:
    LibDirs.append(begin(MIPS64LibDirs), end(MIPS64LibDirs));
    TripleAliases.append(begin(MIPS64Triples), end(MIPS64Triples));
    BiarchLibDirs.append(begin(MIPSLibDirs), end(MIPSLibDirs));
    BiarchTripleAliases.append(begin(MIPSTriples), end(MIPSTriples));
    BiarchLibDirs.append(begin(MIPSN32LibDirs), end(MIPSN32LibDirs));
    BiarchTripleAliases.append(begin(MIPSN32Triples), end(MIPSN32Triples));
    break;
  case llvm::Triple::mips64el:
    LibDirs.append(begin(MIPS64ELLibDirs), end(MIPS64ELLibDirs));
    TripleAliases.append(begin(MIPS64ELTriples), end(MIPS64ELTriples));
    BiarchLibDirs.append(begin(MIPSELLibDirs), end(MIPSELLibDirs));
    BiarchTripleAliases.append(begin(MIPSELTriples), end(MIPSELTriples));
    BiarchLibDirs.append(begin(MIPSN32ELLibDirs), end(MIPSN32ELLibDirs));
    BiarchTripleAliases.append(begin(MIPSN32ELTriples), end(MIPSN32ELTriples));
    BiarchTripleAliases.append(begin(MIPSTriples), end(MIPSTriples));
    break;
  case llvm::Triple::msp430:
    LibDirs.append(begin(MSP430LibDirs), end(MSP430LibDirs));
    TripleAliases.append(begin(MSP430Triples), end(MSP430Triples));
    break;
  case llvm::Triple::ppc:
    LibDirs.append(begin(PPCLibDirs), end(PPCLibDirs));
    TripleAliases.append(begin(PPCTriples), end(PPCTriples));
    BiarchLibDirs.append(begin(PPC64LibDirs), end(PPC64LibDirs));
    BiarchTripleAliases.append(begin(PPC64Triples), end(PPC64Triples));
    break;
  case llvm::Triple::ppc64:
    LibDirs.append(begin(PPC64LibDirs), end(PPC64LibDirs));
    TripleAliases.append(begin(PPC64Triples), end(PPC64Triples));
    BiarchLibDirs.append(begin(PPCLibDirs), end(PPCLibDirs));
    BiarchTripleAliases.append(begin(PPCTriples), end(PPCTriples));
    break;
  case llvm::Triple::ppc64le:
    LibDirs.append(begin(PPC64LELibDirs), end(PPC64LELibDirs));
    TripleAliases.append(begin(PPC64LETriples), end(PPC64LETriples));
    break;
  case llvm::Triple::riscv32:
    LibDirs.append(begin(RISCV32LibDirs), end(RISCV32LibDirs));
    BiarchLibDirs.append(begin(RISCV32LibDirs), end(RISCV32LibDirs));
    TripleAliases.append(begin(RISCVTriples), end(RISCVTriples));
    BiarchTripleAliases.append(begin(RISCVTriples), end(RISCVTriples));
    break;
  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
    LibDirs.append(begin(SPARCv8LibDirs), end(SPARCv8LibDirs));
    TripleAliases.append(begin(SPARCv8Triples), end(SPARCv8Triples));
    BiarchLibDirs.append(begin(SPARCv9LibDirs), end(SPARCv9LibDirs));
    BiarchTripleAliases.append(begin(SPARCv9Triples), end(SPARCv9Triples));
    break;
  case llvm::Triple::sparcv9:
    LibDirs.append(begin(SPARCv9LibDirs), end(SPARCv9LibDirs));
    TripleAliases.append(begin(SPARCv9Triples), end(SPARCv9Triples));
    BiarchLibDirs.append(begin(SPARCv8LibDirs), end(SPARCv8LibDirs));
    BiarchTripleAliases.append(begin(SPARCv8Triples), end(SPARCv8Triples));
    break;
  case llvm::Triple::systemz:
    LibDirs.append(begin(SystemZLibDirs), end(SystemZLibDirs));
    TripleAliases.append(begin(SystemZTriples), end(SystemZTriples));
    break;
  default:
    // By default, just rely on the standard lib directories and the original
    // triple.
    break;
  }

  // Always append the drivers target triple to the end, in case it doesn't
  // match any of our aliases.
  TripleAliases.push_back(TargetTriple.str());

  // Also include the multiarch variant if it's different.
  if (TargetTriple.str() != BiarchTriple.str())
    BiarchTripleAliases.push_back(BiarchTriple.str());
}

bool Generic_GCC::GCCInstallationDetector::ScanGCCForMultilibs(
    const llvm::Triple &TargetTriple, const ArgList &Args,
    StringRef Path, bool NeedsBiarchSuffix) {
  llvm::Triple::ArchType TargetArch = TargetTriple.getArch();
  DetectedMultilibs Detected;

  // Android standalone toolchain could have multilibs for ARM and Thumb.
  // Debian mips multilibs behave more like the rest of the biarch ones,
  // so handle them there
  if (isArmOrThumbArch(TargetArch) && TargetTriple.isAndroid()) {
    // It should also work without multilibs in a simplified toolchain.
    findAndroidArmMultilibs(D, TargetTriple, Path, Args, Detected);
  } else if (TargetTriple.isMIPS()) {
    if (!findMIPSMultilibs(D, TargetTriple, Path, Args, Detected))
      return false;
  } else if (isRISCV(TargetArch)) {
    findRISCVMultilibs(D, TargetTriple, Path, Args, Detected);
  } else if (isMSP430(TargetArch)) {
    findMSP430Multilibs(D, TargetTriple, Path, Args, Detected);
  } else if (!findBiarchMultilibs(D, TargetTriple, Path, Args,
                                  NeedsBiarchSuffix, Detected)) {
    return false;
  }

  Multilibs = Detected.Multilibs;
  SelectedMultilib = Detected.SelectedMultilib;
  BiarchSibling = Detected.BiarchSibling;

  return true;
}

void Generic_GCC::GCCInstallationDetector::ScanLibDirForGCCTriple(
    const llvm::Triple &TargetTriple, const ArgList &Args,
    const std::string &LibDir, StringRef CandidateTriple,
    bool NeedsBiarchSuffix) {
  llvm::Triple::ArchType TargetArch = TargetTriple.getArch();
  // Locations relative to the system lib directory where GCC's triple-specific
  // directories might reside.
  struct GCCLibSuffix {
    // Path from system lib directory to GCC triple-specific directory.
    std::string LibSuffix;
    // Path from GCC triple-specific directory back to system lib directory.
    // This is one '..' component per component in LibSuffix.
    StringRef ReversePath;
    // Whether this library suffix is relevant for the triple.
    bool Active;
  } Suffixes[] = {
      // This is the normal place.
      {"gcc/" + CandidateTriple.str(), "../..", true},

      // Debian puts cross-compilers in gcc-cross.
      {"gcc-cross/" + CandidateTriple.str(), "../..",
       TargetTriple.getOS() != llvm::Triple::Solaris},

      // The Freescale PPC SDK has the gcc libraries in
      // <sysroot>/usr/lib/<triple>/x.y.z so have a look there as well. Only do
      // this on Freescale triples, though, since some systems put a *lot* of
      // files in that location, not just GCC installation data.
      {CandidateTriple.str(), "..",
       TargetTriple.getVendor() == llvm::Triple::Freescale ||
       TargetTriple.getVendor() == llvm::Triple::OpenEmbedded},

      // Natively multiarch systems sometimes put the GCC triple-specific
      // directory within their multiarch lib directory, resulting in the
      // triple appearing twice.
      {CandidateTriple.str() + "/gcc/" + CandidateTriple.str(), "../../..",
       TargetTriple.getOS() != llvm::Triple::Solaris},

      // Deal with cases (on Ubuntu) where the system architecture could be i386
      // but the GCC target architecture could be (say) i686.
      // FIXME: It may be worthwhile to generalize this and look for a second
      // triple.
      {"i386-linux-gnu/gcc/" + CandidateTriple.str(), "../../..",
       (TargetArch == llvm::Triple::x86 &&
        TargetTriple.getOS() != llvm::Triple::Solaris)},
      {"i386-gnu/gcc/" + CandidateTriple.str(), "../../..",
       (TargetArch == llvm::Triple::x86 &&
        TargetTriple.getOS() != llvm::Triple::Solaris)}};

  for (auto &Suffix : Suffixes) {
    if (!Suffix.Active)
      continue;

    StringRef LibSuffix = Suffix.LibSuffix;
    std::error_code EC;
    for (llvm::vfs::directory_iterator
             LI = D.getVFS().dir_begin(LibDir + "/" + LibSuffix, EC),
             LE;
         !EC && LI != LE; LI = LI.increment(EC)) {
      StringRef VersionText = llvm::sys::path::filename(LI->path());
      GCCVersion CandidateVersion = GCCVersion::Parse(VersionText);
      if (CandidateVersion.Major != -1) // Filter obviously bad entries.
        if (!CandidateGCCInstallPaths.insert(LI->path()).second)
          continue; // Saw this path before; no need to look at it again.
      if (CandidateVersion.isOlderThan(4, 1, 1))
        continue;
      if (CandidateVersion <= Version)
        continue;

      if (!ScanGCCForMultilibs(TargetTriple, Args, LI->path(),
                               NeedsBiarchSuffix))
        continue;

      Version = CandidateVersion;
      GCCTriple.setTriple(CandidateTriple);
      // FIXME: We hack together the directory name here instead of
      // using LI to ensure stable path separators across Windows and
      // Linux.
      GCCInstallPath = (LibDir + "/" + LibSuffix + "/" + VersionText).str();
      GCCParentLibPath = (GCCInstallPath + "/../" + Suffix.ReversePath).str();
      IsValid = true;
    }
  }
}

bool Generic_GCC::GCCInstallationDetector::ScanGentooConfigs(
    const llvm::Triple &TargetTriple, const ArgList &Args,
    const SmallVectorImpl<StringRef> &CandidateTriples,
    const SmallVectorImpl<StringRef> &CandidateBiarchTriples) {
  for (StringRef CandidateTriple : CandidateTriples) {
    if (ScanGentooGccConfig(TargetTriple, Args, CandidateTriple))
      return true;
  }

  for (StringRef CandidateTriple : CandidateBiarchTriples) {
    if (ScanGentooGccConfig(TargetTriple, Args, CandidateTriple, true))
      return true;
  }
  return false;
}

bool Generic_GCC::GCCInstallationDetector::ScanGentooGccConfig(
    const llvm::Triple &TargetTriple, const ArgList &Args,
    StringRef CandidateTriple, bool NeedsBiarchSuffix) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
      D.getVFS().getBufferForFile(D.SysRoot + "/etc/env.d/gcc/config-" +
                                  CandidateTriple.str());
  if (File) {
    SmallVector<StringRef, 2> Lines;
    File.get()->getBuffer().split(Lines, "\n");
    for (StringRef Line : Lines) {
      Line = Line.trim();
      // CURRENT=triple-version
      if (!Line.consume_front("CURRENT="))
        continue;
      // Process the config file pointed to by CURRENT.
      llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> ConfigFile =
          D.getVFS().getBufferForFile(D.SysRoot + "/etc/env.d/gcc/" +
                                      Line.str());
      std::pair<StringRef, StringRef> ActiveVersion = Line.rsplit('-');
      // List of paths to scan for libraries.
      SmallVector<StringRef, 4> GentooScanPaths;
      // Scan the Config file to find installed GCC libraries path.
      // Typical content of the GCC config file:
      // LDPATH="/usr/lib/gcc/x86_64-pc-linux-gnu/4.9.x:/usr/lib/gcc/
      // (continued from previous line) x86_64-pc-linux-gnu/4.9.x/32"
      // MANPATH="/usr/share/gcc-data/x86_64-pc-linux-gnu/4.9.x/man"
      // INFOPATH="/usr/share/gcc-data/x86_64-pc-linux-gnu/4.9.x/info"
      // STDCXX_INCDIR="/usr/lib/gcc/x86_64-pc-linux-gnu/4.9.x/include/g++-v4"
      // We are looking for the paths listed in LDPATH=... .
      if (ConfigFile) {
        SmallVector<StringRef, 2> ConfigLines;
        ConfigFile.get()->getBuffer().split(ConfigLines, "\n");
        for (StringRef ConfLine : ConfigLines) {
          ConfLine = ConfLine.trim();
          if (ConfLine.consume_front("LDPATH=")) {
            // Drop '"' from front and back if present.
            ConfLine.consume_back("\"");
            ConfLine.consume_front("\"");
            // Get all paths sperated by ':'
            ConfLine.split(GentooScanPaths, ':', -1, /*AllowEmpty*/ false);
          }
        }
      }
      // Test the path based on the version in /etc/env.d/gcc/config-{tuple}.
      std::string basePath = "/usr/lib/gcc/" + ActiveVersion.first.str() + "/"
          + ActiveVersion.second.str();
      GentooScanPaths.push_back(StringRef(basePath));

      // Scan all paths for GCC libraries.
      for (const auto &GentooScanPath : GentooScanPaths) {
        std::string GentooPath = D.SysRoot + std::string(GentooScanPath);
        if (D.getVFS().exists(GentooPath + "/crtbegin.o")) {
          if (!ScanGCCForMultilibs(TargetTriple, Args, GentooPath,
                                   NeedsBiarchSuffix))
            continue;

          Version = GCCVersion::Parse(ActiveVersion.second);
          GCCInstallPath = GentooPath;
          GCCParentLibPath = GentooPath + std::string("/../../..");
          GCCTriple.setTriple(ActiveVersion.first);
          IsValid = true;
          return true;
        }
      }
    }
  }

  return false;
}

Generic_GCC::Generic_GCC(const Driver &D, const llvm::Triple &Triple,
                         const ArgList &Args)
    : ToolChain(D, Triple, Args), GCCInstallation(D),
      CudaInstallation(D, Triple, Args) {
  getProgramPaths().push_back(getDriver().getInstalledDir());
  if (getDriver().getInstalledDir() != getDriver().Dir)
    getProgramPaths().push_back(getDriver().Dir);
}

Generic_GCC::~Generic_GCC() {}

Tool *Generic_GCC::getTool(Action::ActionClass AC) const {
  switch (AC) {
  case Action::PreprocessJobClass:
    if (!Preprocess)
      Preprocess.reset(new clang::driver::tools::gcc::Preprocessor(*this));
    return Preprocess.get();
  case Action::CompileJobClass:
    if (!Compile)
      Compile.reset(new tools::gcc::Compiler(*this));
    return Compile.get();
  default:
    return ToolChain::getTool(AC);
  }
}

Tool *Generic_GCC::buildAssembler() const {
  return new tools::gnutools::Assembler(*this);
}

Tool *Generic_GCC::buildLinker() const { return new tools::gcc::Linker(*this); }

void Generic_GCC::printVerboseInfo(raw_ostream &OS) const {
  // Print the information about how we detected the GCC installation.
  GCCInstallation.print(OS);
  CudaInstallation.print(OS);
}

bool Generic_GCC::IsUnwindTablesDefault(const ArgList &Args) const {
  return getArch() == llvm::Triple::x86_64;
}

bool Generic_GCC::isPICDefault() const {
  switch (getArch()) {
  case llvm::Triple::x86_64:
    return getTriple().isOSWindows();
  case llvm::Triple::ppc64:
    // Big endian PPC is PIC by default
    return !getTriple().isOSBinFormatMachO() && !getTriple().isMacOSX();
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    return true;
  default:
    return false;
  }
}

bool Generic_GCC::isPIEDefault() const { return false; }

bool Generic_GCC::isPICDefaultForced() const {
  return getArch() == llvm::Triple::x86_64 && getTriple().isOSWindows();
}

bool Generic_GCC::IsIntegratedAssemblerDefault() const {
  switch (getTriple().getArch()) {
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::avr:
  case llvm::Triple::bpfel:
  case llvm::Triple::bpfeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
  case llvm::Triple::ppc:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
  case llvm::Triple::systemz:
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
  case llvm::Triple::msp430:
    return true;
  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
  case llvm::Triple::sparcv9:
    if (getTriple().isOSSolaris() || getTriple().isOSOpenBSD())
      return true;
    return false;
  default:
    return false;
  }
}

void Generic_GCC::AddClangCXXStdlibIncludeArgs(const ArgList &DriverArgs,
                                               ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdlibinc) ||
      DriverArgs.hasArg(options::OPT_nostdincxx))
    return;

  switch (GetCXXStdlibType(DriverArgs)) {
  case ToolChain::CST_Libcxx:
    addLibCxxIncludePaths(DriverArgs, CC1Args);
    break;

  case ToolChain::CST_Libstdcxx:
    addLibStdCxxIncludePaths(DriverArgs, CC1Args);
    break;
  }
}

void
Generic_GCC::addLibCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                   llvm::opt::ArgStringList &CC1Args) const {
  // FIXME: The Linux behavior would probaby be a better approach here.
  addSystemInclude(DriverArgs, CC1Args,
                   getDriver().SysRoot + "/usr/include/c++/v1");
}

void
Generic_GCC::addLibStdCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                      llvm::opt::ArgStringList &CC1Args) const {
  // By default, we don't assume we know where libstdc++ might be installed.
  // FIXME: If we have a valid GCCInstallation, use it.
}

/// Helper to add the variant paths of a libstdc++ installation.
bool Generic_GCC::addLibStdCXXIncludePaths(
    Twine Base, Twine Suffix, StringRef GCCTriple, StringRef GCCMultiarchTriple,
    StringRef TargetMultiarchTriple, Twine IncludeSuffix,
    const ArgList &DriverArgs, ArgStringList &CC1Args) const {
  if (!getVFS().exists(Base + Suffix))
    return false;

  addSystemInclude(DriverArgs, CC1Args, Base + Suffix);

  // The vanilla GCC layout of libstdc++ headers uses a triple subdirectory. If
  // that path exists or we have neither a GCC nor target multiarch triple, use
  // this vanilla search path.
  if ((GCCMultiarchTriple.empty() && TargetMultiarchTriple.empty()) ||
      getVFS().exists(Base + Suffix + "/" + GCCTriple + IncludeSuffix)) {
    addSystemInclude(DriverArgs, CC1Args,
                     Base + Suffix + "/" + GCCTriple + IncludeSuffix);
  } else {
    // Otherwise try to use multiarch naming schemes which have normalized the
    // triples and put the triple before the suffix.
    //
    // GCC surprisingly uses *both* the GCC triple with a multilib suffix and
    // the target triple, so we support that here.
    addSystemInclude(DriverArgs, CC1Args,
                     Base + "/" + GCCMultiarchTriple + Suffix + IncludeSuffix);
    addSystemInclude(DriverArgs, CC1Args,
                     Base + "/" + TargetMultiarchTriple + Suffix);
  }

  addSystemInclude(DriverArgs, CC1Args, Base + Suffix + "/backward");
  return true;
}

llvm::opt::DerivedArgList *
Generic_GCC::TranslateArgs(const llvm::opt::DerivedArgList &Args, StringRef,
                           Action::OffloadKind DeviceOffloadKind) const {

  // If this tool chain is used for an OpenMP offloading device we have to make
  // sure we always generate a shared library regardless of the commands the
  // user passed to the host. This is required because the runtime library
  // is required to load the device image dynamically at run time.
  if (DeviceOffloadKind == Action::OFK_OpenMP) {
    DerivedArgList *DAL = new DerivedArgList(Args.getBaseArgs());
    const OptTable &Opts = getDriver().getOpts();

    // Request the shared library. Given that these options are decided
    // implicitly, they do not refer to any base argument.
    DAL->AddFlagArg(/*BaseArg=*/nullptr, Opts.getOption(options::OPT_shared));
    DAL->AddFlagArg(/*BaseArg=*/nullptr, Opts.getOption(options::OPT_fPIC));

    // Filter all the arguments we don't care passing to the offloading
    // toolchain as they can mess up with the creation of a shared library.
    for (auto *A : Args) {
      switch ((options::ID)A->getOption().getID()) {
      default:
        DAL->append(A);
        break;
      case options::OPT_shared:
      case options::OPT_dynamic:
      case options::OPT_static:
      case options::OPT_fPIC:
      case options::OPT_fno_PIC:
      case options::OPT_fpic:
      case options::OPT_fno_pic:
      case options::OPT_fPIE:
      case options::OPT_fno_PIE:
      case options::OPT_fpie:
      case options::OPT_fno_pie:
        break;
      }
    }
    return DAL;
  }
  return nullptr;
}

void Generic_ELF::anchor() {}

void Generic_ELF::addClangTargetOptions(const ArgList &DriverArgs,
                                        ArgStringList &CC1Args,
                                        Action::OffloadKind) const {
  const Generic_GCC::GCCVersion &V = GCCInstallation.getVersion();
  bool UseInitArrayDefault =
      getTriple().getArch() == llvm::Triple::aarch64 ||
      getTriple().getArch() == llvm::Triple::aarch64_be ||
      (getTriple().isOSFreeBSD() &&
       getTriple().getOSMajorVersion() >= 12) ||
      (getTriple().getOS() == llvm::Triple::Linux &&
       ((!GCCInstallation.isValid() || !V.isOlderThan(4, 7, 0)) ||
        getTriple().isAndroid())) ||
      getTriple().getOS() == llvm::Triple::NaCl ||
      (getTriple().getVendor() == llvm::Triple::MipsTechnologies &&
       !getTriple().hasEnvironment()) ||
      getTriple().getOS() == llvm::Triple::Solaris ||
      getTriple().getArch() == llvm::Triple::riscv32 ||
      getTriple().getArch() == llvm::Triple::riscv64;

  if (DriverArgs.hasFlag(options::OPT_fuse_init_array,
                         options::OPT_fno_use_init_array, UseInitArrayDefault))
    CC1Args.push_back("-fuse-init-array");
}
