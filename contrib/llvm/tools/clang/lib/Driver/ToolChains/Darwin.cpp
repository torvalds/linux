//===--- Darwin.cpp - Darwin Tool and ToolChain Implementations -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Darwin.h"
#include "Arch/ARM.h"
#include "CommonArgs.h"
#include "clang/Basic/AlignedAllocation.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/TargetParser.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <cstdlib> // ::getenv

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

llvm::Triple::ArchType darwin::getArchTypeForMachOArchName(StringRef Str) {
  // See arch(3) and llvm-gcc's driver-driver.c. We don't implement support for
  // archs which Darwin doesn't use.

  // The matching this routine does is fairly pointless, since it is neither the
  // complete architecture list, nor a reasonable subset. The problem is that
  // historically the driver driver accepts this and also ties its -march=
  // handling to the architecture name, so we need to be careful before removing
  // support for it.

  // This code must be kept in sync with Clang's Darwin specific argument
  // translation.

  return llvm::StringSwitch<llvm::Triple::ArchType>(Str)
      .Cases("ppc", "ppc601", "ppc603", "ppc604", "ppc604e", llvm::Triple::ppc)
      .Cases("ppc750", "ppc7400", "ppc7450", "ppc970", llvm::Triple::ppc)
      .Case("ppc64", llvm::Triple::ppc64)
      .Cases("i386", "i486", "i486SX", "i586", "i686", llvm::Triple::x86)
      .Cases("pentium", "pentpro", "pentIIm3", "pentIIm5", "pentium4",
             llvm::Triple::x86)
      .Cases("x86_64", "x86_64h", llvm::Triple::x86_64)
      // This is derived from the driver driver.
      .Cases("arm", "armv4t", "armv5", "armv6", "armv6m", llvm::Triple::arm)
      .Cases("armv7", "armv7em", "armv7k", "armv7m", llvm::Triple::arm)
      .Cases("armv7s", "xscale", llvm::Triple::arm)
      .Case("arm64", llvm::Triple::aarch64)
      .Case("r600", llvm::Triple::r600)
      .Case("amdgcn", llvm::Triple::amdgcn)
      .Case("nvptx", llvm::Triple::nvptx)
      .Case("nvptx64", llvm::Triple::nvptx64)
      .Case("amdil", llvm::Triple::amdil)
      .Case("spir", llvm::Triple::spir)
      .Default(llvm::Triple::UnknownArch);
}

void darwin::setTripleTypeForMachOArchName(llvm::Triple &T, StringRef Str) {
  const llvm::Triple::ArchType Arch = getArchTypeForMachOArchName(Str);
  llvm::ARM::ArchKind ArchKind = llvm::ARM::parseArch(Str);
  T.setArch(Arch);

  if (Str == "x86_64h")
    T.setArchName(Str);
  else if (ArchKind == llvm::ARM::ArchKind::ARMV6M ||
           ArchKind == llvm::ARM::ArchKind::ARMV7M ||
           ArchKind == llvm::ARM::ArchKind::ARMV7EM) {
    T.setOS(llvm::Triple::UnknownOS);
    T.setObjectFormat(llvm::Triple::MachO);
  }
}

void darwin::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                     const InputInfo &Output,
                                     const InputInfoList &Inputs,
                                     const ArgList &Args,
                                     const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  assert(Inputs.size() == 1 && "Unexpected number of inputs.");
  const InputInfo &Input = Inputs[0];

  // Determine the original source input.
  const Action *SourceAction = &JA;
  while (SourceAction->getKind() != Action::InputClass) {
    assert(!SourceAction->getInputs().empty() && "unexpected root action!");
    SourceAction = SourceAction->getInputs()[0];
  }

  // If -fno-integrated-as is used add -Q to the darwin assembler driver to make
  // sure it runs its system assembler not clang's integrated assembler.
  // Applicable to darwin11+ and Xcode 4+.  darwin<10 lacked integrated-as.
  // FIXME: at run-time detect assembler capabilities or rely on version
  // information forwarded by -target-assembler-version.
  if (Args.hasArg(options::OPT_fno_integrated_as)) {
    const llvm::Triple &T(getToolChain().getTriple());
    if (!(T.isMacOSX() && T.isMacOSXVersionLT(10, 7)))
      CmdArgs.push_back("-Q");
  }

  // Forward -g, assuming we are dealing with an actual assembly file.
  if (SourceAction->getType() == types::TY_Asm ||
      SourceAction->getType() == types::TY_PP_Asm) {
    if (Args.hasArg(options::OPT_gstabs))
      CmdArgs.push_back("--gstabs");
    else if (Args.hasArg(options::OPT_g_Group))
      CmdArgs.push_back("-g");
  }

  // Derived from asm spec.
  AddMachOArch(Args, CmdArgs);

  // Use -force_cpusubtype_ALL on x86 by default.
  if (getToolChain().getArch() == llvm::Triple::x86 ||
      getToolChain().getArch() == llvm::Triple::x86_64 ||
      Args.hasArg(options::OPT_force__cpusubtype__ALL))
    CmdArgs.push_back("-force_cpusubtype_ALL");

  if (getToolChain().getArch() != llvm::Triple::x86_64 &&
      (((Args.hasArg(options::OPT_mkernel) ||
         Args.hasArg(options::OPT_fapple_kext)) &&
        getMachOToolChain().isKernelStatic()) ||
       Args.hasArg(options::OPT_static)))
    CmdArgs.push_back("-static");

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  assert(Output.isFilename() && "Unexpected lipo output.");
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  assert(Input.isFilename() && "Invalid input.");
  CmdArgs.push_back(Input.getFilename());

  // asm_final spec is empty.

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("as"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

void darwin::MachOTool::anchor() {}

void darwin::MachOTool::AddMachOArch(const ArgList &Args,
                                     ArgStringList &CmdArgs) const {
  StringRef ArchName = getMachOToolChain().getMachOArchName(Args);

  // Derived from darwin_arch spec.
  CmdArgs.push_back("-arch");
  CmdArgs.push_back(Args.MakeArgString(ArchName));

  // FIXME: Is this needed anymore?
  if (ArchName == "arm")
    CmdArgs.push_back("-force_cpusubtype_ALL");
}

bool darwin::Linker::NeedsTempPath(const InputInfoList &Inputs) const {
  // We only need to generate a temp path for LTO if we aren't compiling object
  // files. When compiling source files, we run 'dsymutil' after linking. We
  // don't run 'dsymutil' when compiling object files.
  for (const auto &Input : Inputs)
    if (Input.getType() != types::TY_Object)
      return true;

  return false;
}

/// Pass -no_deduplicate to ld64 under certain conditions:
///
/// - Either -O0 or -O1 is explicitly specified
/// - No -O option is specified *and* this is a compile+link (implicit -O0)
///
/// Also do *not* add -no_deduplicate when no -O option is specified and this
/// is just a link (we can't imply -O0)
static bool shouldLinkerNotDedup(bool IsLinkerOnlyAction, const ArgList &Args) {
  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O0))
      return true;
    if (A->getOption().matches(options::OPT_O))
      return llvm::StringSwitch<bool>(A->getValue())
                    .Case("1", true)
                    .Default(false);
    return false; // OPT_Ofast & OPT_O4
  }

  if (!IsLinkerOnlyAction) // Implicit -O0 for compile+linker only.
    return true;
  return false;
}

void darwin::Linker::AddLinkArgs(Compilation &C, const ArgList &Args,
                                 ArgStringList &CmdArgs,
                                 const InputInfoList &Inputs) const {
  const Driver &D = getToolChain().getDriver();
  const toolchains::MachO &MachOTC = getMachOToolChain();

  unsigned Version[5] = {0, 0, 0, 0, 0};
  if (Arg *A = Args.getLastArg(options::OPT_mlinker_version_EQ)) {
    if (!Driver::GetReleaseVersion(A->getValue(), Version))
      D.Diag(diag::err_drv_invalid_version_number) << A->getAsString(Args);
  }

  // Newer linkers support -demangle. Pass it if supported and not disabled by
  // the user.
  if (Version[0] >= 100 && !Args.hasArg(options::OPT_Z_Xlinker__no_demangle))
    CmdArgs.push_back("-demangle");

  if (Args.hasArg(options::OPT_rdynamic) && Version[0] >= 137)
    CmdArgs.push_back("-export_dynamic");

  // If we are using App Extension restrictions, pass a flag to the linker
  // telling it that the compiled code has been audited.
  if (Args.hasFlag(options::OPT_fapplication_extension,
                   options::OPT_fno_application_extension, false))
    CmdArgs.push_back("-application_extension");

  if (D.isUsingLTO() && Version[0] >= 116 && NeedsTempPath(Inputs)) {
    std::string TmpPathName;
    if (D.getLTOMode() == LTOK_Full) {
      // If we are using full LTO, then automatically create a temporary file
      // path for the linker to use, so that it's lifetime will extend past a
      // possible dsymutil step.
      TmpPathName =
          D.GetTemporaryPath("cc", types::getTypeTempSuffix(types::TY_Object));
    } else if (D.getLTOMode() == LTOK_Thin)
      // If we are using thin LTO, then create a directory instead.
      TmpPathName = D.GetTemporaryDirectory("thinlto");

    if (!TmpPathName.empty()) {
      auto *TmpPath = C.getArgs().MakeArgString(TmpPathName);
      C.addTempFile(TmpPath);
      CmdArgs.push_back("-object_path_lto");
      CmdArgs.push_back(TmpPath);
    }
  }

  // Use -lto_library option to specify the libLTO.dylib path. Try to find
  // it in clang installed libraries. ld64 will only look at this argument
  // when it actually uses LTO, so libLTO.dylib only needs to exist at link
  // time if ld64 decides that it needs to use LTO.
  // Since this is passed unconditionally, ld64 will never look for libLTO.dylib
  // next to it. That's ok since ld64 using a libLTO.dylib not matching the
  // clang version won't work anyways.
  if (Version[0] >= 133) {
    // Search for libLTO in <InstalledDir>/../lib/libLTO.dylib
    StringRef P = llvm::sys::path::parent_path(D.Dir);
    SmallString<128> LibLTOPath(P);
    llvm::sys::path::append(LibLTOPath, "lib");
    llvm::sys::path::append(LibLTOPath, "libLTO.dylib");
    CmdArgs.push_back("-lto_library");
    CmdArgs.push_back(C.getArgs().MakeArgString(LibLTOPath));
  }

  // ld64 version 262 and above run the deduplicate pass by default.
  if (Version[0] >= 262 && shouldLinkerNotDedup(C.getJobs().empty(), Args))
    CmdArgs.push_back("-no_deduplicate");

  // Derived from the "link" spec.
  Args.AddAllArgs(CmdArgs, options::OPT_static);
  if (!Args.hasArg(options::OPT_static))
    CmdArgs.push_back("-dynamic");
  if (Args.hasArg(options::OPT_fgnu_runtime)) {
    // FIXME: gcc replaces -lobjc in forward args with -lobjc-gnu
    // here. How do we wish to handle such things?
  }

  if (!Args.hasArg(options::OPT_dynamiclib)) {
    AddMachOArch(Args, CmdArgs);
    // FIXME: Why do this only on this path?
    Args.AddLastArg(CmdArgs, options::OPT_force__cpusubtype__ALL);

    Args.AddLastArg(CmdArgs, options::OPT_bundle);
    Args.AddAllArgs(CmdArgs, options::OPT_bundle__loader);
    Args.AddAllArgs(CmdArgs, options::OPT_client__name);

    Arg *A;
    if ((A = Args.getLastArg(options::OPT_compatibility__version)) ||
        (A = Args.getLastArg(options::OPT_current__version)) ||
        (A = Args.getLastArg(options::OPT_install__name)))
      D.Diag(diag::err_drv_argument_only_allowed_with) << A->getAsString(Args)
                                                       << "-dynamiclib";

    Args.AddLastArg(CmdArgs, options::OPT_force__flat__namespace);
    Args.AddLastArg(CmdArgs, options::OPT_keep__private__externs);
    Args.AddLastArg(CmdArgs, options::OPT_private__bundle);
  } else {
    CmdArgs.push_back("-dylib");

    Arg *A;
    if ((A = Args.getLastArg(options::OPT_bundle)) ||
        (A = Args.getLastArg(options::OPT_bundle__loader)) ||
        (A = Args.getLastArg(options::OPT_client__name)) ||
        (A = Args.getLastArg(options::OPT_force__flat__namespace)) ||
        (A = Args.getLastArg(options::OPT_keep__private__externs)) ||
        (A = Args.getLastArg(options::OPT_private__bundle)))
      D.Diag(diag::err_drv_argument_not_allowed_with) << A->getAsString(Args)
                                                      << "-dynamiclib";

    Args.AddAllArgsTranslated(CmdArgs, options::OPT_compatibility__version,
                              "-dylib_compatibility_version");
    Args.AddAllArgsTranslated(CmdArgs, options::OPT_current__version,
                              "-dylib_current_version");

    AddMachOArch(Args, CmdArgs);

    Args.AddAllArgsTranslated(CmdArgs, options::OPT_install__name,
                              "-dylib_install_name");
  }

  Args.AddLastArg(CmdArgs, options::OPT_all__load);
  Args.AddAllArgs(CmdArgs, options::OPT_allowable__client);
  Args.AddLastArg(CmdArgs, options::OPT_bind__at__load);
  if (MachOTC.isTargetIOSBased())
    Args.AddLastArg(CmdArgs, options::OPT_arch__errors__fatal);
  Args.AddLastArg(CmdArgs, options::OPT_dead__strip);
  Args.AddLastArg(CmdArgs, options::OPT_no__dead__strip__inits__and__terms);
  Args.AddAllArgs(CmdArgs, options::OPT_dylib__file);
  Args.AddLastArg(CmdArgs, options::OPT_dynamic);
  Args.AddAllArgs(CmdArgs, options::OPT_exported__symbols__list);
  Args.AddLastArg(CmdArgs, options::OPT_flat__namespace);
  Args.AddAllArgs(CmdArgs, options::OPT_force__load);
  Args.AddAllArgs(CmdArgs, options::OPT_headerpad__max__install__names);
  Args.AddAllArgs(CmdArgs, options::OPT_image__base);
  Args.AddAllArgs(CmdArgs, options::OPT_init);

  // Add the deployment target.
  MachOTC.addMinVersionArgs(Args, CmdArgs);

  Args.AddLastArg(CmdArgs, options::OPT_nomultidefs);
  Args.AddLastArg(CmdArgs, options::OPT_multi__module);
  Args.AddLastArg(CmdArgs, options::OPT_single__module);
  Args.AddAllArgs(CmdArgs, options::OPT_multiply__defined);
  Args.AddAllArgs(CmdArgs, options::OPT_multiply__defined__unused);

  if (const Arg *A =
          Args.getLastArg(options::OPT_fpie, options::OPT_fPIE,
                          options::OPT_fno_pie, options::OPT_fno_PIE)) {
    if (A->getOption().matches(options::OPT_fpie) ||
        A->getOption().matches(options::OPT_fPIE))
      CmdArgs.push_back("-pie");
    else
      CmdArgs.push_back("-no_pie");
  }

  // for embed-bitcode, use -bitcode_bundle in linker command
  if (C.getDriver().embedBitcodeEnabled()) {
    // Check if the toolchain supports bitcode build flow.
    if (MachOTC.SupportsEmbeddedBitcode()) {
      CmdArgs.push_back("-bitcode_bundle");
      if (C.getDriver().embedBitcodeMarkerOnly() && Version[0] >= 278) {
        CmdArgs.push_back("-bitcode_process_mode");
        CmdArgs.push_back("marker");
      }
    } else
      D.Diag(diag::err_drv_bitcode_unsupported_on_toolchain);
  }

  Args.AddLastArg(CmdArgs, options::OPT_prebind);
  Args.AddLastArg(CmdArgs, options::OPT_noprebind);
  Args.AddLastArg(CmdArgs, options::OPT_nofixprebinding);
  Args.AddLastArg(CmdArgs, options::OPT_prebind__all__twolevel__modules);
  Args.AddLastArg(CmdArgs, options::OPT_read__only__relocs);
  Args.AddAllArgs(CmdArgs, options::OPT_sectcreate);
  Args.AddAllArgs(CmdArgs, options::OPT_sectorder);
  Args.AddAllArgs(CmdArgs, options::OPT_seg1addr);
  Args.AddAllArgs(CmdArgs, options::OPT_segprot);
  Args.AddAllArgs(CmdArgs, options::OPT_segaddr);
  Args.AddAllArgs(CmdArgs, options::OPT_segs__read__only__addr);
  Args.AddAllArgs(CmdArgs, options::OPT_segs__read__write__addr);
  Args.AddAllArgs(CmdArgs, options::OPT_seg__addr__table);
  Args.AddAllArgs(CmdArgs, options::OPT_seg__addr__table__filename);
  Args.AddAllArgs(CmdArgs, options::OPT_sub__library);
  Args.AddAllArgs(CmdArgs, options::OPT_sub__umbrella);

  // Give --sysroot= preference, over the Apple specific behavior to also use
  // --isysroot as the syslibroot.
  StringRef sysroot = C.getSysRoot();
  if (sysroot != "") {
    CmdArgs.push_back("-syslibroot");
    CmdArgs.push_back(C.getArgs().MakeArgString(sysroot));
  } else if (const Arg *A = Args.getLastArg(options::OPT_isysroot)) {
    CmdArgs.push_back("-syslibroot");
    CmdArgs.push_back(A->getValue());
  }

  Args.AddLastArg(CmdArgs, options::OPT_twolevel__namespace);
  Args.AddLastArg(CmdArgs, options::OPT_twolevel__namespace__hints);
  Args.AddAllArgs(CmdArgs, options::OPT_umbrella);
  Args.AddAllArgs(CmdArgs, options::OPT_undefined);
  Args.AddAllArgs(CmdArgs, options::OPT_unexported__symbols__list);
  Args.AddAllArgs(CmdArgs, options::OPT_weak__reference__mismatches);
  Args.AddLastArg(CmdArgs, options::OPT_X_Flag);
  Args.AddAllArgs(CmdArgs, options::OPT_y);
  Args.AddLastArg(CmdArgs, options::OPT_w);
  Args.AddAllArgs(CmdArgs, options::OPT_pagezero__size);
  Args.AddAllArgs(CmdArgs, options::OPT_segs__read__);
  Args.AddLastArg(CmdArgs, options::OPT_seglinkedit);
  Args.AddLastArg(CmdArgs, options::OPT_noseglinkedit);
  Args.AddAllArgs(CmdArgs, options::OPT_sectalign);
  Args.AddAllArgs(CmdArgs, options::OPT_sectobjectsymbols);
  Args.AddAllArgs(CmdArgs, options::OPT_segcreate);
  Args.AddLastArg(CmdArgs, options::OPT_whyload);
  Args.AddLastArg(CmdArgs, options::OPT_whatsloaded);
  Args.AddAllArgs(CmdArgs, options::OPT_dylinker__install__name);
  Args.AddLastArg(CmdArgs, options::OPT_dylinker);
  Args.AddLastArg(CmdArgs, options::OPT_Mach);
}

/// Determine whether we are linking the ObjC runtime.
static bool isObjCRuntimeLinked(const ArgList &Args) {
  if (isObjCAutoRefCount(Args)) {
    Args.ClaimAllArgs(options::OPT_fobjc_link_runtime);
    return true;
  }
  return Args.hasArg(options::OPT_fobjc_link_runtime);
}

void darwin::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  assert(Output.getType() == types::TY_Image && "Invalid linker output type.");

  // If the number of arguments surpasses the system limits, we will encode the
  // input files in a separate file, shortening the command line. To this end,
  // build a list of input file names that can be passed via a file with the
  // -filelist linker option.
  llvm::opt::ArgStringList InputFileList;

  // The logic here is derived from gcc's behavior; most of which
  // comes from specs (starting with link_command). Consult gcc for
  // more information.
  ArgStringList CmdArgs;

  /// Hack(tm) to ignore linking errors when we are doing ARC migration.
  if (Args.hasArg(options::OPT_ccc_arcmt_check,
                  options::OPT_ccc_arcmt_migrate)) {
    for (const auto &Arg : Args)
      Arg->claim();
    const char *Exec =
        Args.MakeArgString(getToolChain().GetProgramPath("touch"));
    CmdArgs.push_back(Output.getFilename());
    C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, None));
    return;
  }

  // I'm not sure why this particular decomposition exists in gcc, but
  // we follow suite for ease of comparison.
  AddLinkArgs(C, Args, CmdArgs, Inputs);

  // For LTO, pass the name of the optimization record file and other
  // opt-remarks flags.
  if (Args.hasFlag(options::OPT_fsave_optimization_record,
                   options::OPT_fno_save_optimization_record, false)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-lto-pass-remarks-output");
    CmdArgs.push_back("-mllvm");

    SmallString<128> F;
    F = Output.getFilename();
    F += ".opt.yaml";
    CmdArgs.push_back(Args.MakeArgString(F));

    if (getLastProfileUseArg(Args)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-lto-pass-remarks-with-hotness");

      if (const Arg *A =
              Args.getLastArg(options::OPT_fdiagnostics_hotness_threshold_EQ)) {
        CmdArgs.push_back("-mllvm");
        std::string Opt =
            std::string("-lto-pass-remarks-hotness-threshold=") + A->getValue();
        CmdArgs.push_back(Args.MakeArgString(Opt));
      }
    }
  }

  // Propagate the -moutline flag to the linker in LTO.
  if (Args.hasFlag(options::OPT_moutline, options::OPT_mno_outline, false)) {
    if (getMachOToolChain().getMachOArchName(Args) == "arm64") {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-enable-machine-outliner");

      // Outline from linkonceodr functions by default in LTO.
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-enable-linkonceodr-outlining");
    }
  }

  // It seems that the 'e' option is completely ignored for dynamic executables
  // (the default), and with static executables, the last one wins, as expected.
  Args.AddAllArgs(CmdArgs, {options::OPT_d_Flag, options::OPT_s, options::OPT_t,
                            options::OPT_Z_Flag, options::OPT_u_Group,
                            options::OPT_e, options::OPT_r});

  // Forward -ObjC when either -ObjC or -ObjC++ is used, to force loading
  // members of static archive libraries which implement Objective-C classes or
  // categories.
  if (Args.hasArg(options::OPT_ObjC) || Args.hasArg(options::OPT_ObjCXX))
    CmdArgs.push_back("-ObjC");

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles))
    getMachOToolChain().addStartObjectFileArgs(Args, CmdArgs);

  Args.AddAllArgs(CmdArgs, options::OPT_L);

  AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);
  // Build the input file for -filelist (list of linker input files) in case we
  // need it later
  for (const auto &II : Inputs) {
    if (!II.isFilename()) {
      // This is a linker input argument.
      // We cannot mix input arguments and file names in a -filelist input, thus
      // we prematurely stop our list (remaining files shall be passed as
      // arguments).
      if (InputFileList.size() > 0)
        break;

      continue;
    }

    InputFileList.push_back(II.getFilename());
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs))
    addOpenMPRuntime(CmdArgs, getToolChain(), Args);

  if (isObjCRuntimeLinked(Args) &&
      !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    // We use arclite library for both ARC and subscripting support.
    getMachOToolChain().AddLinkARCArgs(Args, CmdArgs);

    CmdArgs.push_back("-framework");
    CmdArgs.push_back("Foundation");
    // Link libobj.
    CmdArgs.push_back("-lobjc");
  }

  if (LinkingOutput) {
    CmdArgs.push_back("-arch_multiple");
    CmdArgs.push_back("-final_output");
    CmdArgs.push_back(LinkingOutput);
  }

  if (Args.hasArg(options::OPT_fnested_functions))
    CmdArgs.push_back("-allow_stack_execute");

  getMachOToolChain().addProfileRTLibs(Args, CmdArgs);

  if (unsigned Parallelism =
          getLTOParallelism(Args, getToolChain().getDriver())) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(Args.MakeArgString("-threads=" + Twine(Parallelism)));
  }

  if (getToolChain().ShouldLinkCXXStdlib(Args))
    getToolChain().AddCXXStdlibLibArgs(Args, CmdArgs);
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    // link_ssp spec is empty.

    // Let the tool chain choose which runtime library to link.
    getMachOToolChain().AddLinkRuntimeLibArgs(Args, CmdArgs);

    // No need to do anything for pthreads. Claim argument to avoid warning.
    Args.ClaimAllArgs(options::OPT_pthread);
    Args.ClaimAllArgs(options::OPT_pthreads);
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    // endfile_spec is empty.
  }

  Args.AddAllArgs(CmdArgs, options::OPT_T_Group);
  Args.AddAllArgs(CmdArgs, options::OPT_F);

  // -iframework should be forwarded as -F.
  for (const Arg *A : Args.filtered(options::OPT_iframework))
    CmdArgs.push_back(Args.MakeArgString(std::string("-F") + A->getValue()));

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    if (Arg *A = Args.getLastArg(options::OPT_fveclib)) {
      if (A->getValue() == StringRef("Accelerate")) {
        CmdArgs.push_back("-framework");
        CmdArgs.push_back("Accelerate");
      }
    }
  }

  const char *Exec = Args.MakeArgString(getToolChain().GetLinkerPath());
  std::unique_ptr<Command> Cmd =
      llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs);
  Cmd->setInputFileList(std::move(InputFileList));
  C.addCommand(std::move(Cmd));
}

void darwin::Lipo::ConstructJob(Compilation &C, const JobAction &JA,
                                const InputInfo &Output,
                                const InputInfoList &Inputs,
                                const ArgList &Args,
                                const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  CmdArgs.push_back("-create");
  assert(Output.isFilename() && "Unexpected lipo output.");

  CmdArgs.push_back("-output");
  CmdArgs.push_back(Output.getFilename());

  for (const auto &II : Inputs) {
    assert(II.isFilename() && "Unexpected lipo input.");
    CmdArgs.push_back(II.getFilename());
  }

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("lipo"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

void darwin::Dsymutil::ConstructJob(Compilation &C, const JobAction &JA,
                                    const InputInfo &Output,
                                    const InputInfoList &Inputs,
                                    const ArgList &Args,
                                    const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  assert(Inputs.size() == 1 && "Unable to handle multiple inputs.");
  const InputInfo &Input = Inputs[0];
  assert(Input.isFilename() && "Unexpected dsymutil input.");
  CmdArgs.push_back(Input.getFilename());

  const char *Exec =
      Args.MakeArgString(getToolChain().GetProgramPath("dsymutil"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

void darwin::VerifyDebug::ConstructJob(Compilation &C, const JobAction &JA,
                                       const InputInfo &Output,
                                       const InputInfoList &Inputs,
                                       const ArgList &Args,
                                       const char *LinkingOutput) const {
  ArgStringList CmdArgs;
  CmdArgs.push_back("--verify");
  CmdArgs.push_back("--debug-info");
  CmdArgs.push_back("--eh-frame");
  CmdArgs.push_back("--quiet");

  assert(Inputs.size() == 1 && "Unable to handle multiple inputs.");
  const InputInfo &Input = Inputs[0];
  assert(Input.isFilename() && "Unexpected verify input");

  // Grabbing the output of the earlier dsymutil run.
  CmdArgs.push_back(Input.getFilename());

  const char *Exec =
      Args.MakeArgString(getToolChain().GetProgramPath("dwarfdump"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

MachO::MachO(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  // We expect 'as', 'ld', etc. to be adjacent to our install dir.
  getProgramPaths().push_back(getDriver().getInstalledDir());
  if (getDriver().getInstalledDir() != getDriver().Dir)
    getProgramPaths().push_back(getDriver().Dir);
}

/// Darwin - Darwin tool chain for i386 and x86_64.
Darwin::Darwin(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : MachO(D, Triple, Args), TargetInitialized(false),
      CudaInstallation(D, Triple, Args) {}

types::ID MachO::LookupTypeForExtension(StringRef Ext) const {
  types::ID Ty = types::lookupTypeForExtension(Ext);

  // Darwin always preprocesses assembly files (unless -x is used explicitly).
  if (Ty == types::TY_PP_Asm)
    return types::TY_Asm;

  return Ty;
}

bool MachO::HasNativeLLVMSupport() const { return true; }

ToolChain::CXXStdlibType Darwin::GetDefaultCXXStdlibType() const {
  // Default to use libc++ on OS X 10.9+ and iOS 7+.
  if ((isTargetMacOS() && !isMacosxVersionLT(10, 9)) ||
       (isTargetIOSBased() && !isIPhoneOSVersionLT(7, 0)) ||
       isTargetWatchOSBased())
    return ToolChain::CST_Libcxx;

  return ToolChain::CST_Libstdcxx;
}

/// Darwin provides an ARC runtime starting in MacOS X 10.7 and iOS 5.0.
ObjCRuntime Darwin::getDefaultObjCRuntime(bool isNonFragile) const {
  if (isTargetWatchOSBased())
    return ObjCRuntime(ObjCRuntime::WatchOS, TargetVersion);
  if (isTargetIOSBased())
    return ObjCRuntime(ObjCRuntime::iOS, TargetVersion);
  if (isNonFragile)
    return ObjCRuntime(ObjCRuntime::MacOSX, TargetVersion);
  return ObjCRuntime(ObjCRuntime::FragileMacOSX, TargetVersion);
}

/// Darwin provides a blocks runtime starting in MacOS X 10.6 and iOS 3.2.
bool Darwin::hasBlocksRuntime() const {
  if (isTargetWatchOSBased())
    return true;
  else if (isTargetIOSBased())
    return !isIPhoneOSVersionLT(3, 2);
  else {
    assert(isTargetMacOS() && "unexpected darwin target");
    return !isMacosxVersionLT(10, 6);
  }
}

void Darwin::AddCudaIncludeArgs(const ArgList &DriverArgs,
                                ArgStringList &CC1Args) const {
  CudaInstallation.AddCudaIncludeArgs(DriverArgs, CC1Args);
}

// This is just a MachO name translation routine and there's no
// way to join this into ARMTargetParser without breaking all
// other assumptions. Maybe MachO should consider standardising
// their nomenclature.
static const char *ArmMachOArchName(StringRef Arch) {
  return llvm::StringSwitch<const char *>(Arch)
      .Case("armv6k", "armv6")
      .Case("armv6m", "armv6m")
      .Case("armv5tej", "armv5")
      .Case("xscale", "xscale")
      .Case("armv4t", "armv4t")
      .Case("armv7", "armv7")
      .Cases("armv7a", "armv7-a", "armv7")
      .Cases("armv7r", "armv7-r", "armv7")
      .Cases("armv7em", "armv7e-m", "armv7em")
      .Cases("armv7k", "armv7-k", "armv7k")
      .Cases("armv7m", "armv7-m", "armv7m")
      .Cases("armv7s", "armv7-s", "armv7s")
      .Default(nullptr);
}

static const char *ArmMachOArchNameCPU(StringRef CPU) {
  llvm::ARM::ArchKind ArchKind = llvm::ARM::parseCPUArch(CPU);
  if (ArchKind == llvm::ARM::ArchKind::INVALID)
    return nullptr;
  StringRef Arch = llvm::ARM::getArchName(ArchKind);

  // FIXME: Make sure this MachO triple mangling is really necessary.
  // ARMv5* normalises to ARMv5.
  if (Arch.startswith("armv5"))
    Arch = Arch.substr(0, 5);
  // ARMv6*, except ARMv6M, normalises to ARMv6.
  else if (Arch.startswith("armv6") && !Arch.endswith("6m"))
    Arch = Arch.substr(0, 5);
  // ARMv7A normalises to ARMv7.
  else if (Arch.endswith("v7a"))
    Arch = Arch.substr(0, 5);
  return Arch.data();
}

StringRef MachO::getMachOArchName(const ArgList &Args) const {
  switch (getTriple().getArch()) {
  default:
    return getDefaultUniversalArchName();

  case llvm::Triple::aarch64:
    return "arm64";

  case llvm::Triple::thumb:
  case llvm::Triple::arm:
    if (const Arg *A = Args.getLastArg(clang::driver::options::OPT_march_EQ))
      if (const char *Arch = ArmMachOArchName(A->getValue()))
        return Arch;

    if (const Arg *A = Args.getLastArg(options::OPT_mcpu_EQ))
      if (const char *Arch = ArmMachOArchNameCPU(A->getValue()))
        return Arch;

    return "arm";
  }
}

Darwin::~Darwin() {}

MachO::~MachO() {}

std::string Darwin::ComputeEffectiveClangTriple(const ArgList &Args,
                                                types::ID InputType) const {
  llvm::Triple Triple(ComputeLLVMTriple(Args, InputType));

  // If the target isn't initialized (e.g., an unknown Darwin platform, return
  // the default triple).
  if (!isTargetInitialized())
    return Triple.getTriple();

  SmallString<16> Str;
  if (isTargetWatchOSBased())
    Str += "watchos";
  else if (isTargetTvOSBased())
    Str += "tvos";
  else if (isTargetIOSBased())
    Str += "ios";
  else
    Str += "macosx";
  Str += getTargetVersion().getAsString();
  Triple.setOSName(Str);

  return Triple.getTriple();
}

Tool *MachO::getTool(Action::ActionClass AC) const {
  switch (AC) {
  case Action::LipoJobClass:
    if (!Lipo)
      Lipo.reset(new tools::darwin::Lipo(*this));
    return Lipo.get();
  case Action::DsymutilJobClass:
    if (!Dsymutil)
      Dsymutil.reset(new tools::darwin::Dsymutil(*this));
    return Dsymutil.get();
  case Action::VerifyDebugInfoJobClass:
    if (!VerifyDebug)
      VerifyDebug.reset(new tools::darwin::VerifyDebug(*this));
    return VerifyDebug.get();
  default:
    return ToolChain::getTool(AC);
  }
}

Tool *MachO::buildLinker() const { return new tools::darwin::Linker(*this); }

Tool *MachO::buildAssembler() const {
  return new tools::darwin::Assembler(*this);
}

DarwinClang::DarwinClang(const Driver &D, const llvm::Triple &Triple,
                         const ArgList &Args)
    : Darwin(D, Triple, Args) {}

void DarwinClang::addClangWarningOptions(ArgStringList &CC1Args) const {
  // For modern targets, promote certain warnings to errors.
  if (isTargetWatchOSBased() || getTriple().isArch64Bit()) {
    // Always enable -Wdeprecated-objc-isa-usage and promote it
    // to an error.
    CC1Args.push_back("-Wdeprecated-objc-isa-usage");
    CC1Args.push_back("-Werror=deprecated-objc-isa-usage");

    // For iOS and watchOS, also error about implicit function declarations,
    // as that can impact calling conventions.
    if (!isTargetMacOS())
      CC1Args.push_back("-Werror=implicit-function-declaration");
  }
}

void DarwinClang::AddLinkARCArgs(const ArgList &Args,
                                 ArgStringList &CmdArgs) const {
  // Avoid linking compatibility stubs on i386 mac.
  if (isTargetMacOS() && getArch() == llvm::Triple::x86)
    return;

  ObjCRuntime runtime = getDefaultObjCRuntime(/*nonfragile*/ true);

  if ((runtime.hasNativeARC() || !isObjCAutoRefCount(Args)) &&
      runtime.hasSubscripting())
    return;

  CmdArgs.push_back("-force_load");
  SmallString<128> P(getDriver().ClangExecutable);
  llvm::sys::path::remove_filename(P); // 'clang'
  llvm::sys::path::remove_filename(P); // 'bin'
  llvm::sys::path::append(P, "lib", "arc", "libarclite_");
  // Mash in the platform.
  if (isTargetWatchOSSimulator())
    P += "watchsimulator";
  else if (isTargetWatchOS())
    P += "watchos";
  else if (isTargetTvOSSimulator())
    P += "appletvsimulator";
  else if (isTargetTvOS())
    P += "appletvos";
  else if (isTargetIOSSimulator())
    P += "iphonesimulator";
  else if (isTargetIPhoneOS())
    P += "iphoneos";
  else
    P += "macosx";
  P += ".a";

  CmdArgs.push_back(Args.MakeArgString(P));
}

unsigned DarwinClang::GetDefaultDwarfVersion() const {
  // Default to use DWARF 2 on OS X 10.10 / iOS 8 and lower.
  if ((isTargetMacOS() && isMacosxVersionLT(10, 11)) ||
      (isTargetIOSBased() && isIPhoneOSVersionLT(9)))
    return 2;
  return 4;
}

void MachO::AddLinkRuntimeLib(const ArgList &Args, ArgStringList &CmdArgs,
                              StringRef Component, RuntimeLinkOptions Opts,
                              bool IsShared) const {
  SmallString<64> DarwinLibName = StringRef("libclang_rt.");
  // an Darwin the builtins compomnent is not in the library name
  if (Component != "builtins") {
    DarwinLibName += Component;
    if (!(Opts & RLO_IsEmbedded))
      DarwinLibName += "_";
    DarwinLibName += getOSLibraryNameSuffix();
  } else
    DarwinLibName += getOSLibraryNameSuffix(true);

  DarwinLibName += IsShared ? "_dynamic.dylib" : ".a";
  SmallString<128> Dir(getDriver().ResourceDir);
  llvm::sys::path::append(
      Dir, "lib", (Opts & RLO_IsEmbedded) ? "macho_embedded" : "darwin");

  SmallString<128> P(Dir);
  llvm::sys::path::append(P, DarwinLibName);

  // For now, allow missing resource libraries to support developers who may
  // not have compiler-rt checked out or integrated into their build (unless
  // we explicitly force linking with this library).
  if ((Opts & RLO_AlwaysLink) || getVFS().exists(P)) {
    const char *LibArg = Args.MakeArgString(P);
    if (Opts & RLO_FirstLink)
      CmdArgs.insert(CmdArgs.begin(), LibArg);
    else
      CmdArgs.push_back(LibArg);
  }

  // Adding the rpaths might negatively interact when other rpaths are involved,
  // so we should make sure we add the rpaths last, after all user-specified
  // rpaths. This is currently true from this place, but we need to be
  // careful if this function is ever called before user's rpaths are emitted.
  if (Opts & RLO_AddRPath) {
    assert(DarwinLibName.endswith(".dylib") && "must be a dynamic library");

    // Add @executable_path to rpath to support having the dylib copied with
    // the executable.
    CmdArgs.push_back("-rpath");
    CmdArgs.push_back("@executable_path");

    // Add the path to the resource dir to rpath to support using the dylib
    // from the default location without copying.
    CmdArgs.push_back("-rpath");
    CmdArgs.push_back(Args.MakeArgString(Dir));
  }
}

StringRef Darwin::getPlatformFamily() const {
  switch (TargetPlatform) {
    case DarwinPlatformKind::MacOS:
      return "MacOSX";
    case DarwinPlatformKind::IPhoneOS:
      return "iPhone";
    case DarwinPlatformKind::TvOS:
      return "AppleTV";
    case DarwinPlatformKind::WatchOS:
      return "Watch";
  }
  llvm_unreachable("Unsupported platform");
}

StringRef Darwin::getSDKName(StringRef isysroot) {
  // Assume SDK has path: SOME_PATH/SDKs/PlatformXX.YY.sdk
  llvm::sys::path::const_iterator SDKDir;
  auto BeginSDK = llvm::sys::path::begin(isysroot);
  auto EndSDK = llvm::sys::path::end(isysroot);
  for (auto IT = BeginSDK; IT != EndSDK; ++IT) {
    StringRef SDK = *IT;
    if (SDK.endswith(".sdk"))
      return SDK.slice(0, SDK.size() - 4);
  }
  return "";
}

StringRef Darwin::getOSLibraryNameSuffix(bool IgnoreSim) const {
  switch (TargetPlatform) {
  case DarwinPlatformKind::MacOS:
    return "osx";
  case DarwinPlatformKind::IPhoneOS:
    return TargetEnvironment == NativeEnvironment || IgnoreSim ? "ios"
                                                               : "iossim";
  case DarwinPlatformKind::TvOS:
    return TargetEnvironment == NativeEnvironment || IgnoreSim ? "tvos"
                                                               : "tvossim";
  case DarwinPlatformKind::WatchOS:
    return TargetEnvironment == NativeEnvironment || IgnoreSim ? "watchos"
                                                               : "watchossim";
  }
  llvm_unreachable("Unsupported platform");
}

/// Check if the link command contains a symbol export directive.
static bool hasExportSymbolDirective(const ArgList &Args) {
  for (Arg *A : Args) {
    if (A->getOption().matches(options::OPT_exported__symbols__list))
      return true;
    if (!A->getOption().matches(options::OPT_Wl_COMMA) &&
        !A->getOption().matches(options::OPT_Xlinker))
      continue;
    if (A->containsValue("-exported_symbols_list") ||
        A->containsValue("-exported_symbol"))
      return true;
  }
  return false;
}

/// Add an export directive for \p Symbol to the link command.
static void addExportedSymbol(ArgStringList &CmdArgs, const char *Symbol) {
  CmdArgs.push_back("-exported_symbol");
  CmdArgs.push_back(Symbol);
}

void Darwin::addProfileRTLibs(const ArgList &Args,
                              ArgStringList &CmdArgs) const {
  if (!needsProfileRT(Args)) return;

  AddLinkRuntimeLib(Args, CmdArgs, "profile",
                    RuntimeLinkOptions(RLO_AlwaysLink | RLO_FirstLink));

  // If we have a symbol export directive and we're linking in the profile
  // runtime, automatically export symbols necessary to implement some of the
  // runtime's functionality.
  if (hasExportSymbolDirective(Args)) {
    if (needsGCovInstrumentation(Args)) {
      addExportedSymbol(CmdArgs, "___gcov_flush");
      addExportedSymbol(CmdArgs, "_flush_fn_list");
      addExportedSymbol(CmdArgs, "_writeout_fn_list");
    } else {
      addExportedSymbol(CmdArgs, "___llvm_profile_filename");
      addExportedSymbol(CmdArgs, "___llvm_profile_raw_version");
      addExportedSymbol(CmdArgs, "_lprofCurFilename");
      addExportedSymbol(CmdArgs, "_lprofMergeValueProfData");
    }
    addExportedSymbol(CmdArgs, "_lprofDirMode");
  }
}

void DarwinClang::AddLinkSanitizerLibArgs(const ArgList &Args,
                                          ArgStringList &CmdArgs,
                                          StringRef Sanitizer,
                                          bool Shared) const {
  auto RLO = RuntimeLinkOptions(RLO_AlwaysLink | (Shared ? RLO_AddRPath : 0U));
  AddLinkRuntimeLib(Args, CmdArgs, Sanitizer, RLO, Shared);
}

ToolChain::RuntimeLibType DarwinClang::GetRuntimeLibType(
    const ArgList &Args) const {
  if (Arg* A = Args.getLastArg(options::OPT_rtlib_EQ)) {
    StringRef Value = A->getValue();
    if (Value != "compiler-rt")
      getDriver().Diag(clang::diag::err_drv_unsupported_rtlib_for_platform)
          << Value << "darwin";
  }

  return ToolChain::RLT_CompilerRT;
}

void DarwinClang::AddLinkRuntimeLibArgs(const ArgList &Args,
                                        ArgStringList &CmdArgs) const {
  // Call once to ensure diagnostic is printed if wrong value was specified
  GetRuntimeLibType(Args);

  // Darwin doesn't support real static executables, don't link any runtime
  // libraries with -static.
  if (Args.hasArg(options::OPT_static) ||
      Args.hasArg(options::OPT_fapple_kext) ||
      Args.hasArg(options::OPT_mkernel))
    return;

  // Reject -static-libgcc for now, we can deal with this when and if someone
  // cares. This is useful in situations where someone wants to statically link
  // something like libstdc++, and needs its runtime support routines.
  if (const Arg *A = Args.getLastArg(options::OPT_static_libgcc)) {
    getDriver().Diag(diag::err_drv_unsupported_opt) << A->getAsString(Args);
    return;
  }

  const SanitizerArgs &Sanitize = getSanitizerArgs();
  if (Sanitize.needsAsanRt())
    AddLinkSanitizerLibArgs(Args, CmdArgs, "asan");
  if (Sanitize.needsLsanRt())
    AddLinkSanitizerLibArgs(Args, CmdArgs, "lsan");
  if (Sanitize.needsUbsanRt())
    AddLinkSanitizerLibArgs(Args, CmdArgs,
                            Sanitize.requiresMinimalRuntime() ? "ubsan_minimal"
                                                              : "ubsan",
                            Sanitize.needsSharedRt());
  if (Sanitize.needsTsanRt())
    AddLinkSanitizerLibArgs(Args, CmdArgs, "tsan");
  if (Sanitize.needsFuzzer() && !Args.hasArg(options::OPT_dynamiclib)) {
    AddLinkSanitizerLibArgs(Args, CmdArgs, "fuzzer", /*shared=*/false);

    // Libfuzzer is written in C++ and requires libcxx.
    AddCXXStdlibLibArgs(Args, CmdArgs);
  }
  if (Sanitize.needsStatsRt()) {
    AddLinkRuntimeLib(Args, CmdArgs, "stats_client", RLO_AlwaysLink);
    AddLinkSanitizerLibArgs(Args, CmdArgs, "stats");
  }
  if (Sanitize.needsEsanRt())
    AddLinkSanitizerLibArgs(Args, CmdArgs, "esan");

  const XRayArgs &XRay = getXRayArgs();
  if (XRay.needsXRayRt()) {
    AddLinkRuntimeLib(Args, CmdArgs, "xray");
    AddLinkRuntimeLib(Args, CmdArgs, "xray-basic");
    AddLinkRuntimeLib(Args, CmdArgs, "xray-fdr");
  }

  // Otherwise link libSystem, then the dynamic runtime library, and finally any
  // target specific static runtime library.
  CmdArgs.push_back("-lSystem");

  // Select the dynamic runtime library and the target specific static library.
  if (isTargetIOSBased()) {
    // If we are compiling as iOS / simulator, don't attempt to link libgcc_s.1,
    // it never went into the SDK.
    // Linking against libgcc_s.1 isn't needed for iOS 5.0+
    if (isIPhoneOSVersionLT(5, 0) && !isTargetIOSSimulator() &&
        getTriple().getArch() != llvm::Triple::aarch64)
      CmdArgs.push_back("-lgcc_s.1");
  }
  AddLinkRuntimeLib(Args, CmdArgs, "builtins");
}

/// Returns the most appropriate macOS target version for the current process.
///
/// If the macOS SDK version is the same or earlier than the system version,
/// then the SDK version is returned. Otherwise the system version is returned.
static std::string getSystemOrSDKMacOSVersion(StringRef MacOSSDKVersion) {
  unsigned Major, Minor, Micro;
  llvm::Triple SystemTriple(llvm::sys::getProcessTriple());
  if (!SystemTriple.isMacOSX())
    return MacOSSDKVersion;
  SystemTriple.getMacOSXVersion(Major, Minor, Micro);
  VersionTuple SystemVersion(Major, Minor, Micro);
  bool HadExtra;
  if (!Driver::GetReleaseVersion(MacOSSDKVersion, Major, Minor, Micro,
                                 HadExtra))
    return MacOSSDKVersion;
  VersionTuple SDKVersion(Major, Minor, Micro);
  if (SDKVersion > SystemVersion)
    return SystemVersion.getAsString();
  return MacOSSDKVersion;
}

namespace {

/// The Darwin OS that was selected or inferred from arguments / environment.
struct DarwinPlatform {
  enum SourceKind {
    /// The OS was specified using the -target argument.
    TargetArg,
    /// The OS was specified using the -m<os>-version-min argument.
    OSVersionArg,
    /// The OS was specified using the OS_DEPLOYMENT_TARGET environment.
    DeploymentTargetEnv,
    /// The OS was inferred from the SDK.
    InferredFromSDK,
    /// The OS was inferred from the -arch.
    InferredFromArch
  };

  using DarwinPlatformKind = Darwin::DarwinPlatformKind;
  using DarwinEnvironmentKind = Darwin::DarwinEnvironmentKind;

  DarwinPlatformKind getPlatform() const { return Platform; }

  DarwinEnvironmentKind getEnvironment() const { return Environment; }

  void setEnvironment(DarwinEnvironmentKind Kind) {
    Environment = Kind;
    InferSimulatorFromArch = false;
  }

  StringRef getOSVersion() const {
    if (Kind == OSVersionArg)
      return Argument->getValue();
    return OSVersion;
  }

  void setOSVersion(StringRef S) {
    assert(Kind == TargetArg && "Unexpected kind!");
    OSVersion = S;
  }

  bool hasOSVersion() const { return HasOSVersion; }

  /// Returns true if the target OS was explicitly specified.
  bool isExplicitlySpecified() const { return Kind <= DeploymentTargetEnv; }

  /// Returns true if the simulator environment can be inferred from the arch.
  bool canInferSimulatorFromArch() const { return InferSimulatorFromArch; }

  /// Adds the -m<os>-version-min argument to the compiler invocation.
  void addOSVersionMinArgument(DerivedArgList &Args, const OptTable &Opts) {
    if (Argument)
      return;
    assert(Kind != TargetArg && Kind != OSVersionArg && "Invalid kind");
    options::ID Opt;
    switch (Platform) {
    case DarwinPlatformKind::MacOS:
      Opt = options::OPT_mmacosx_version_min_EQ;
      break;
    case DarwinPlatformKind::IPhoneOS:
      Opt = options::OPT_miphoneos_version_min_EQ;
      break;
    case DarwinPlatformKind::TvOS:
      Opt = options::OPT_mtvos_version_min_EQ;
      break;
    case DarwinPlatformKind::WatchOS:
      Opt = options::OPT_mwatchos_version_min_EQ;
      break;
    }
    Argument = Args.MakeJoinedArg(nullptr, Opts.getOption(Opt), OSVersion);
    Args.append(Argument);
  }

  /// Returns the OS version with the argument / environment variable that
  /// specified it.
  std::string getAsString(DerivedArgList &Args, const OptTable &Opts) {
    switch (Kind) {
    case TargetArg:
    case OSVersionArg:
    case InferredFromSDK:
    case InferredFromArch:
      assert(Argument && "OS version argument not yet inferred");
      return Argument->getAsString(Args);
    case DeploymentTargetEnv:
      return (llvm::Twine(EnvVarName) + "=" + OSVersion).str();
    }
    llvm_unreachable("Unsupported Darwin Source Kind");
  }

  static DarwinPlatform createFromTarget(const llvm::Triple &TT,
                                         StringRef OSVersion, Arg *A) {
    DarwinPlatform Result(TargetArg, getPlatformFromOS(TT.getOS()), OSVersion,
                          A);
    switch (TT.getEnvironment()) {
    case llvm::Triple::Simulator:
      Result.Environment = DarwinEnvironmentKind::Simulator;
      break;
    default:
      break;
    }
    unsigned Major, Minor, Micro;
    TT.getOSVersion(Major, Minor, Micro);
    if (Major == 0)
      Result.HasOSVersion = false;
    return Result;
  }
  static DarwinPlatform createOSVersionArg(DarwinPlatformKind Platform,
                                           Arg *A) {
    return DarwinPlatform(OSVersionArg, Platform, A);
  }
  static DarwinPlatform createDeploymentTargetEnv(DarwinPlatformKind Platform,
                                                  StringRef EnvVarName,
                                                  StringRef Value) {
    DarwinPlatform Result(DeploymentTargetEnv, Platform, Value);
    Result.EnvVarName = EnvVarName;
    return Result;
  }
  static DarwinPlatform createFromSDK(DarwinPlatformKind Platform,
                                      StringRef Value,
                                      bool IsSimulator = false) {
    DarwinPlatform Result(InferredFromSDK, Platform, Value);
    if (IsSimulator)
      Result.Environment = DarwinEnvironmentKind::Simulator;
    Result.InferSimulatorFromArch = false;
    return Result;
  }
  static DarwinPlatform createFromArch(llvm::Triple::OSType OS,
                                       StringRef Value) {
    return DarwinPlatform(InferredFromArch, getPlatformFromOS(OS), Value);
  }

  /// Constructs an inferred SDKInfo value based on the version inferred from
  /// the SDK path itself. Only works for values that were created by inferring
  /// the platform from the SDKPath.
  DarwinSDKInfo inferSDKInfo() {
    assert(Kind == InferredFromSDK && "can infer SDK info only");
    llvm::VersionTuple Version;
    bool IsValid = !Version.tryParse(OSVersion);
    (void)IsValid;
    assert(IsValid && "invalid SDK version");
    return DarwinSDKInfo(Version);
  }

private:
  DarwinPlatform(SourceKind Kind, DarwinPlatformKind Platform, Arg *Argument)
      : Kind(Kind), Platform(Platform), Argument(Argument) {}
  DarwinPlatform(SourceKind Kind, DarwinPlatformKind Platform, StringRef Value,
                 Arg *Argument = nullptr)
      : Kind(Kind), Platform(Platform), OSVersion(Value), Argument(Argument) {}

  static DarwinPlatformKind getPlatformFromOS(llvm::Triple::OSType OS) {
    switch (OS) {
    case llvm::Triple::Darwin:
    case llvm::Triple::MacOSX:
      return DarwinPlatformKind::MacOS;
    case llvm::Triple::IOS:
      return DarwinPlatformKind::IPhoneOS;
    case llvm::Triple::TvOS:
      return DarwinPlatformKind::TvOS;
    case llvm::Triple::WatchOS:
      return DarwinPlatformKind::WatchOS;
    default:
      llvm_unreachable("Unable to infer Darwin variant");
    }
  }

  SourceKind Kind;
  DarwinPlatformKind Platform;
  DarwinEnvironmentKind Environment = DarwinEnvironmentKind::NativeEnvironment;
  std::string OSVersion;
  bool HasOSVersion = true, InferSimulatorFromArch = true;
  Arg *Argument;
  StringRef EnvVarName;
};

/// Returns the deployment target that's specified using the -m<os>-version-min
/// argument.
Optional<DarwinPlatform>
getDeploymentTargetFromOSVersionArg(DerivedArgList &Args,
                                    const Driver &TheDriver) {
  Arg *OSXVersion = Args.getLastArg(options::OPT_mmacosx_version_min_EQ);
  Arg *iOSVersion = Args.getLastArg(options::OPT_miphoneos_version_min_EQ,
                                    options::OPT_mios_simulator_version_min_EQ);
  Arg *TvOSVersion =
      Args.getLastArg(options::OPT_mtvos_version_min_EQ,
                      options::OPT_mtvos_simulator_version_min_EQ);
  Arg *WatchOSVersion =
      Args.getLastArg(options::OPT_mwatchos_version_min_EQ,
                      options::OPT_mwatchos_simulator_version_min_EQ);
  if (OSXVersion) {
    if (iOSVersion || TvOSVersion || WatchOSVersion) {
      TheDriver.Diag(diag::err_drv_argument_not_allowed_with)
          << OSXVersion->getAsString(Args)
          << (iOSVersion ? iOSVersion
                         : TvOSVersion ? TvOSVersion : WatchOSVersion)
                 ->getAsString(Args);
    }
    return DarwinPlatform::createOSVersionArg(Darwin::MacOS, OSXVersion);
  } else if (iOSVersion) {
    if (TvOSVersion || WatchOSVersion) {
      TheDriver.Diag(diag::err_drv_argument_not_allowed_with)
          << iOSVersion->getAsString(Args)
          << (TvOSVersion ? TvOSVersion : WatchOSVersion)->getAsString(Args);
    }
    return DarwinPlatform::createOSVersionArg(Darwin::IPhoneOS, iOSVersion);
  } else if (TvOSVersion) {
    if (WatchOSVersion) {
      TheDriver.Diag(diag::err_drv_argument_not_allowed_with)
          << TvOSVersion->getAsString(Args)
          << WatchOSVersion->getAsString(Args);
    }
    return DarwinPlatform::createOSVersionArg(Darwin::TvOS, TvOSVersion);
  } else if (WatchOSVersion)
    return DarwinPlatform::createOSVersionArg(Darwin::WatchOS, WatchOSVersion);
  return None;
}

/// Returns the deployment target that's specified using the
/// OS_DEPLOYMENT_TARGET environment variable.
Optional<DarwinPlatform>
getDeploymentTargetFromEnvironmentVariables(const Driver &TheDriver,
                                            const llvm::Triple &Triple) {
  std::string Targets[Darwin::LastDarwinPlatform + 1];
  const char *EnvVars[] = {
      "MACOSX_DEPLOYMENT_TARGET",
      "IPHONEOS_DEPLOYMENT_TARGET",
      "TVOS_DEPLOYMENT_TARGET",
      "WATCHOS_DEPLOYMENT_TARGET",
  };
  static_assert(llvm::array_lengthof(EnvVars) == Darwin::LastDarwinPlatform + 1,
                "Missing platform");
  for (const auto &I : llvm::enumerate(llvm::makeArrayRef(EnvVars))) {
    if (char *Env = ::getenv(I.value()))
      Targets[I.index()] = Env;
  }

  // Do not allow conflicts with the watchOS target.
  if (!Targets[Darwin::WatchOS].empty() &&
      (!Targets[Darwin::IPhoneOS].empty() || !Targets[Darwin::TvOS].empty())) {
    TheDriver.Diag(diag::err_drv_conflicting_deployment_targets)
        << "WATCHOS_DEPLOYMENT_TARGET"
        << (!Targets[Darwin::IPhoneOS].empty() ? "IPHONEOS_DEPLOYMENT_TARGET"
                                               : "TVOS_DEPLOYMENT_TARGET");
  }

  // Do not allow conflicts with the tvOS target.
  if (!Targets[Darwin::TvOS].empty() && !Targets[Darwin::IPhoneOS].empty()) {
    TheDriver.Diag(diag::err_drv_conflicting_deployment_targets)
        << "TVOS_DEPLOYMENT_TARGET"
        << "IPHONEOS_DEPLOYMENT_TARGET";
  }

  // Allow conflicts among OSX and iOS for historical reasons, but choose the
  // default platform.
  if (!Targets[Darwin::MacOS].empty() &&
      (!Targets[Darwin::IPhoneOS].empty() ||
       !Targets[Darwin::WatchOS].empty() || !Targets[Darwin::TvOS].empty())) {
    if (Triple.getArch() == llvm::Triple::arm ||
        Triple.getArch() == llvm::Triple::aarch64 ||
        Triple.getArch() == llvm::Triple::thumb)
      Targets[Darwin::MacOS] = "";
    else
      Targets[Darwin::IPhoneOS] = Targets[Darwin::WatchOS] =
          Targets[Darwin::TvOS] = "";
  }

  for (const auto &Target : llvm::enumerate(llvm::makeArrayRef(Targets))) {
    if (!Target.value().empty())
      return DarwinPlatform::createDeploymentTargetEnv(
          (Darwin::DarwinPlatformKind)Target.index(), EnvVars[Target.index()],
          Target.value());
  }
  return None;
}

/// Tries to infer the deployment target from the SDK specified by -isysroot
/// (or SDKROOT). Uses the version specified in the SDKSettings.json file if
/// it's available.
Optional<DarwinPlatform>
inferDeploymentTargetFromSDK(DerivedArgList &Args,
                             const Optional<DarwinSDKInfo> &SDKInfo) {
  const Arg *A = Args.getLastArg(options::OPT_isysroot);
  if (!A)
    return None;
  StringRef isysroot = A->getValue();
  StringRef SDK = Darwin::getSDKName(isysroot);
  if (!SDK.size())
    return None;

  std::string Version;
  if (SDKInfo) {
    // Get the version from the SDKSettings.json if it's available.
    Version = SDKInfo->getVersion().getAsString();
  } else {
    // Slice the version number out.
    // Version number is between the first and the last number.
    size_t StartVer = SDK.find_first_of("0123456789");
    size_t EndVer = SDK.find_last_of("0123456789");
    if (StartVer != StringRef::npos && EndVer > StartVer)
      Version = SDK.slice(StartVer, EndVer + 1);
  }
  if (Version.empty())
    return None;

  if (SDK.startswith("iPhoneOS") || SDK.startswith("iPhoneSimulator"))
    return DarwinPlatform::createFromSDK(
        Darwin::IPhoneOS, Version,
        /*IsSimulator=*/SDK.startswith("iPhoneSimulator"));
  else if (SDK.startswith("MacOSX"))
    return DarwinPlatform::createFromSDK(Darwin::MacOS,
                                         getSystemOrSDKMacOSVersion(Version));
  else if (SDK.startswith("WatchOS") || SDK.startswith("WatchSimulator"))
    return DarwinPlatform::createFromSDK(
        Darwin::WatchOS, Version,
        /*IsSimulator=*/SDK.startswith("WatchSimulator"));
  else if (SDK.startswith("AppleTVOS") || SDK.startswith("AppleTVSimulator"))
    return DarwinPlatform::createFromSDK(
        Darwin::TvOS, Version,
        /*IsSimulator=*/SDK.startswith("AppleTVSimulator"));
  return None;
}

std::string getOSVersion(llvm::Triple::OSType OS, const llvm::Triple &Triple,
                         const Driver &TheDriver) {
  unsigned Major, Minor, Micro;
  llvm::Triple SystemTriple(llvm::sys::getProcessTriple());
  switch (OS) {
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
    // If there is no version specified on triple, and both host and target are
    // macos, use the host triple to infer OS version.
    if (Triple.isMacOSX() && SystemTriple.isMacOSX() &&
        !Triple.getOSMajorVersion())
      SystemTriple.getMacOSXVersion(Major, Minor, Micro);
    else if (!Triple.getMacOSXVersion(Major, Minor, Micro))
      TheDriver.Diag(diag::err_drv_invalid_darwin_version)
          << Triple.getOSName();
    break;
  case llvm::Triple::IOS:
    Triple.getiOSVersion(Major, Minor, Micro);
    break;
  case llvm::Triple::TvOS:
    Triple.getOSVersion(Major, Minor, Micro);
    break;
  case llvm::Triple::WatchOS:
    Triple.getWatchOSVersion(Major, Minor, Micro);
    break;
  default:
    llvm_unreachable("Unexpected OS type");
    break;
  }

  std::string OSVersion;
  llvm::raw_string_ostream(OSVersion) << Major << '.' << Minor << '.' << Micro;
  return OSVersion;
}

/// Tries to infer the target OS from the -arch.
Optional<DarwinPlatform>
inferDeploymentTargetFromArch(DerivedArgList &Args, const Darwin &Toolchain,
                              const llvm::Triple &Triple,
                              const Driver &TheDriver) {
  llvm::Triple::OSType OSTy = llvm::Triple::UnknownOS;

  StringRef MachOArchName = Toolchain.getMachOArchName(Args);
  if (MachOArchName == "armv7" || MachOArchName == "armv7s" ||
      MachOArchName == "arm64")
    OSTy = llvm::Triple::IOS;
  else if (MachOArchName == "armv7k")
    OSTy = llvm::Triple::WatchOS;
  else if (MachOArchName != "armv6m" && MachOArchName != "armv7m" &&
           MachOArchName != "armv7em")
    OSTy = llvm::Triple::MacOSX;

  if (OSTy == llvm::Triple::UnknownOS)
    return None;
  return DarwinPlatform::createFromArch(OSTy,
                                        getOSVersion(OSTy, Triple, TheDriver));
}

/// Returns the deployment target that's specified using the -target option.
Optional<DarwinPlatform> getDeploymentTargetFromTargetArg(
    DerivedArgList &Args, const llvm::Triple &Triple, const Driver &TheDriver) {
  if (!Args.hasArg(options::OPT_target))
    return None;
  if (Triple.getOS() == llvm::Triple::Darwin ||
      Triple.getOS() == llvm::Triple::UnknownOS)
    return None;
  std::string OSVersion = getOSVersion(Triple.getOS(), Triple, TheDriver);
  return DarwinPlatform::createFromTarget(Triple, OSVersion,
                                          Args.getLastArg(options::OPT_target));
}

Optional<DarwinSDKInfo> parseSDKSettings(llvm::vfs::FileSystem &VFS,
                                         const ArgList &Args,
                                         const Driver &TheDriver) {
  const Arg *A = Args.getLastArg(options::OPT_isysroot);
  if (!A)
    return None;
  StringRef isysroot = A->getValue();
  auto SDKInfoOrErr = driver::parseDarwinSDKInfo(VFS, isysroot);
  if (!SDKInfoOrErr) {
    llvm::consumeError(SDKInfoOrErr.takeError());
    TheDriver.Diag(diag::warn_drv_darwin_sdk_invalid_settings);
    return None;
  }
  return *SDKInfoOrErr;
}

} // namespace

void Darwin::AddDeploymentTarget(DerivedArgList &Args) const {
  const OptTable &Opts = getDriver().getOpts();

  // Support allowing the SDKROOT environment variable used by xcrun and other
  // Xcode tools to define the default sysroot, by making it the default for
  // isysroot.
  if (const Arg *A = Args.getLastArg(options::OPT_isysroot)) {
    // Warn if the path does not exist.
    if (!getVFS().exists(A->getValue()))
      getDriver().Diag(clang::diag::warn_missing_sysroot) << A->getValue();
  } else {
    if (char *env = ::getenv("SDKROOT")) {
      // We only use this value as the default if it is an absolute path,
      // exists, and it is not the root path.
      if (llvm::sys::path::is_absolute(env) && getVFS().exists(env) &&
          StringRef(env) != "/") {
        Args.append(Args.MakeSeparateArg(
            nullptr, Opts.getOption(options::OPT_isysroot), env));
      }
    }
  }

  // Read the SDKSettings.json file for more information, like the SDK version
  // that we can pass down to the compiler.
  SDKInfo = parseSDKSettings(getVFS(), Args, getDriver());

  // The OS and the version can be specified using the -target argument.
  Optional<DarwinPlatform> OSTarget =
      getDeploymentTargetFromTargetArg(Args, getTriple(), getDriver());
  if (OSTarget) {
    Optional<DarwinPlatform> OSVersionArgTarget =
        getDeploymentTargetFromOSVersionArg(Args, getDriver());
    if (OSVersionArgTarget) {
      unsigned TargetMajor, TargetMinor, TargetMicro;
      bool TargetExtra;
      unsigned ArgMajor, ArgMinor, ArgMicro;
      bool ArgExtra;
      if (OSTarget->getPlatform() != OSVersionArgTarget->getPlatform() ||
          (Driver::GetReleaseVersion(OSTarget->getOSVersion(), TargetMajor,
                                     TargetMinor, TargetMicro, TargetExtra) &&
           Driver::GetReleaseVersion(OSVersionArgTarget->getOSVersion(),
                                     ArgMajor, ArgMinor, ArgMicro, ArgExtra) &&
           (VersionTuple(TargetMajor, TargetMinor, TargetMicro) !=
                VersionTuple(ArgMajor, ArgMinor, ArgMicro) ||
            TargetExtra != ArgExtra))) {
        // Select the OS version from the -m<os>-version-min argument when
        // the -target does not include an OS version.
        if (OSTarget->getPlatform() == OSVersionArgTarget->getPlatform() &&
            !OSTarget->hasOSVersion()) {
          OSTarget->setOSVersion(OSVersionArgTarget->getOSVersion());
        } else {
          // Warn about -m<os>-version-min that doesn't match the OS version
          // that's specified in the target.
          std::string OSVersionArg =
              OSVersionArgTarget->getAsString(Args, Opts);
          std::string TargetArg = OSTarget->getAsString(Args, Opts);
          getDriver().Diag(clang::diag::warn_drv_overriding_flag_option)
              << OSVersionArg << TargetArg;
        }
      }
    }
  } else {
    // The OS target can be specified using the -m<os>version-min argument.
    OSTarget = getDeploymentTargetFromOSVersionArg(Args, getDriver());
    // If no deployment target was specified on the command line, check for
    // environment defines.
    if (!OSTarget) {
      OSTarget =
          getDeploymentTargetFromEnvironmentVariables(getDriver(), getTriple());
      if (OSTarget) {
        // Don't infer simulator from the arch when the SDK is also specified.
        Optional<DarwinPlatform> SDKTarget =
            inferDeploymentTargetFromSDK(Args, SDKInfo);
        if (SDKTarget)
          OSTarget->setEnvironment(SDKTarget->getEnvironment());
      }
    }
    // If there is no command-line argument to specify the Target version and
    // no environment variable defined, see if we can set the default based
    // on -isysroot using SDKSettings.json if it exists.
    if (!OSTarget) {
      OSTarget = inferDeploymentTargetFromSDK(Args, SDKInfo);
      /// If the target was successfully constructed from the SDK path, try to
      /// infer the SDK info if the SDK doesn't have it.
      if (OSTarget && !SDKInfo)
        SDKInfo = OSTarget->inferSDKInfo();
    }
    // If no OS targets have been specified, try to guess platform from -target
    // or arch name and compute the version from the triple.
    if (!OSTarget)
      OSTarget =
          inferDeploymentTargetFromArch(Args, *this, getTriple(), getDriver());
  }

  assert(OSTarget && "Unable to infer Darwin variant");
  OSTarget->addOSVersionMinArgument(Args, Opts);
  DarwinPlatformKind Platform = OSTarget->getPlatform();

  unsigned Major, Minor, Micro;
  bool HadExtra;
  // Set the tool chain target information.
  if (Platform == MacOS) {
    if (!Driver::GetReleaseVersion(OSTarget->getOSVersion(), Major, Minor,
                                   Micro, HadExtra) ||
        HadExtra || Major != 10 || Minor >= 100 || Micro >= 100)
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
  } else if (Platform == IPhoneOS) {
    if (!Driver::GetReleaseVersion(OSTarget->getOSVersion(), Major, Minor,
                                   Micro, HadExtra) ||
        HadExtra || Major >= 100 || Minor >= 100 || Micro >= 100)
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
    ;
    // For 32-bit targets, the deployment target for iOS has to be earlier than
    // iOS 11.
    if (getTriple().isArch32Bit() && Major >= 11) {
      // If the deployment target is explicitly specified, print a diagnostic.
      if (OSTarget->isExplicitlySpecified()) {
        getDriver().Diag(diag::warn_invalid_ios_deployment_target)
            << OSTarget->getAsString(Args, Opts);
        // Otherwise, set it to 10.99.99.
      } else {
        Major = 10;
        Minor = 99;
        Micro = 99;
      }
    }
  } else if (Platform == TvOS) {
    if (!Driver::GetReleaseVersion(OSTarget->getOSVersion(), Major, Minor,
                                   Micro, HadExtra) ||
        HadExtra || Major >= 100 || Minor >= 100 || Micro >= 100)
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
  } else if (Platform == WatchOS) {
    if (!Driver::GetReleaseVersion(OSTarget->getOSVersion(), Major, Minor,
                                   Micro, HadExtra) ||
        HadExtra || Major >= 10 || Minor >= 100 || Micro >= 100)
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
  } else
    llvm_unreachable("unknown kind of Darwin platform");

  DarwinEnvironmentKind Environment = OSTarget->getEnvironment();
  // Recognize iOS targets with an x86 architecture as the iOS simulator.
  if (Environment == NativeEnvironment && Platform != MacOS &&
      OSTarget->canInferSimulatorFromArch() &&
      (getTriple().getArch() == llvm::Triple::x86 ||
       getTriple().getArch() == llvm::Triple::x86_64))
    Environment = Simulator;

  setTarget(Platform, Environment, Major, Minor, Micro);

  if (const Arg *A = Args.getLastArg(options::OPT_isysroot)) {
    StringRef SDK = getSDKName(A->getValue());
    if (SDK.size() > 0) {
      size_t StartVer = SDK.find_first_of("0123456789");
      StringRef SDKName = SDK.slice(0, StartVer);
      if (!SDKName.startswith(getPlatformFamily()))
        getDriver().Diag(diag::warn_incompatible_sysroot)
            << SDKName << getPlatformFamily();
    }
  }
}

void DarwinClang::AddClangCXXStdlibIncludeArgs(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  // The implementation from a base class will pass through the -stdlib to
  // CC1Args.
  // FIXME: this should not be necessary, remove usages in the frontend
  //        (e.g. HeaderSearchOptions::UseLibcxx) and don't pipe -stdlib.
  ToolChain::AddClangCXXStdlibIncludeArgs(DriverArgs, CC1Args);

  if (DriverArgs.hasArg(options::OPT_nostdlibinc) ||
      DriverArgs.hasArg(options::OPT_nostdincxx))
    return;

  switch (GetCXXStdlibType(DriverArgs)) {
  case ToolChain::CST_Libcxx: {
    llvm::StringRef InstallDir = getDriver().getInstalledDir();
    if (InstallDir.empty())
      break;
    // On Darwin, libc++ may be installed alongside the compiler in
    // include/c++/v1.
    // Get from 'foo/bin' to 'foo/include/c++/v1'.
    SmallString<128> P = InstallDir;
    // Note that InstallDir can be relative, so we have to '..' and not
    // parent_path.
    llvm::sys::path::append(P, "..", "include", "c++", "v1");
    addSystemInclude(DriverArgs, CC1Args, P);
    break;
  }
  case ToolChain::CST_Libstdcxx:
    // FIXME: should we do something about it?
    break;
  }
}
void DarwinClang::AddCXXStdlibLibArgs(const ArgList &Args,
                                      ArgStringList &CmdArgs) const {
  CXXStdlibType Type = GetCXXStdlibType(Args);

  switch (Type) {
  case ToolChain::CST_Libcxx:
    CmdArgs.push_back("-lc++");
    break;

  case ToolChain::CST_Libstdcxx:
    // Unfortunately, -lstdc++ doesn't always exist in the standard search path;
    // it was previously found in the gcc lib dir. However, for all the Darwin
    // platforms we care about it was -lstdc++.6, so we search for that
    // explicitly if we can't see an obvious -lstdc++ candidate.

    // Check in the sysroot first.
    if (const Arg *A = Args.getLastArg(options::OPT_isysroot)) {
      SmallString<128> P(A->getValue());
      llvm::sys::path::append(P, "usr", "lib", "libstdc++.dylib");

      if (!getVFS().exists(P)) {
        llvm::sys::path::remove_filename(P);
        llvm::sys::path::append(P, "libstdc++.6.dylib");
        if (getVFS().exists(P)) {
          CmdArgs.push_back(Args.MakeArgString(P));
          return;
        }
      }
    }

    // Otherwise, look in the root.
    // FIXME: This should be removed someday when we don't have to care about
    // 10.6 and earlier, where /usr/lib/libstdc++.dylib does not exist.
    if (!getVFS().exists("/usr/lib/libstdc++.dylib") &&
        getVFS().exists("/usr/lib/libstdc++.6.dylib")) {
      CmdArgs.push_back("/usr/lib/libstdc++.6.dylib");
      return;
    }

    // Otherwise, let the linker search.
    CmdArgs.push_back("-lstdc++");
    break;
  }
}

void DarwinClang::AddCCKextLibArgs(const ArgList &Args,
                                   ArgStringList &CmdArgs) const {
  // For Darwin platforms, use the compiler-rt-based support library
  // instead of the gcc-provided one (which is also incidentally
  // only present in the gcc lib dir, which makes it hard to find).

  SmallString<128> P(getDriver().ResourceDir);
  llvm::sys::path::append(P, "lib", "darwin");

  // Use the newer cc_kext for iOS ARM after 6.0.
  if (isTargetWatchOS()) {
    llvm::sys::path::append(P, "libclang_rt.cc_kext_watchos.a");
  } else if (isTargetTvOS()) {
    llvm::sys::path::append(P, "libclang_rt.cc_kext_tvos.a");
  } else if (isTargetIPhoneOS()) {
    llvm::sys::path::append(P, "libclang_rt.cc_kext_ios.a");
  } else {
    llvm::sys::path::append(P, "libclang_rt.cc_kext.a");
  }

  // For now, allow missing resource libraries to support developers who may
  // not have compiler-rt checked out or integrated into their build.
  if (getVFS().exists(P))
    CmdArgs.push_back(Args.MakeArgString(P));
}

DerivedArgList *MachO::TranslateArgs(const DerivedArgList &Args,
                                     StringRef BoundArch,
                                     Action::OffloadKind) const {
  DerivedArgList *DAL = new DerivedArgList(Args.getBaseArgs());
  const OptTable &Opts = getDriver().getOpts();

  // FIXME: We really want to get out of the tool chain level argument
  // translation business, as it makes the driver functionality much
  // more opaque. For now, we follow gcc closely solely for the
  // purpose of easily achieving feature parity & testability. Once we
  // have something that works, we should reevaluate each translation
  // and try to push it down into tool specific logic.

  for (Arg *A : Args) {
    if (A->getOption().matches(options::OPT_Xarch__)) {
      // Skip this argument unless the architecture matches either the toolchain
      // triple arch, or the arch being bound.
      llvm::Triple::ArchType XarchArch =
          tools::darwin::getArchTypeForMachOArchName(A->getValue(0));
      if (!(XarchArch == getArch() ||
            (!BoundArch.empty() &&
             XarchArch ==
                 tools::darwin::getArchTypeForMachOArchName(BoundArch))))
        continue;

      Arg *OriginalArg = A;
      unsigned Index = Args.getBaseArgs().MakeIndex(A->getValue(1));
      unsigned Prev = Index;
      std::unique_ptr<Arg> XarchArg(Opts.ParseOneArg(Args, Index));

      // If the argument parsing failed or more than one argument was
      // consumed, the -Xarch_ argument's parameter tried to consume
      // extra arguments. Emit an error and ignore.
      //
      // We also want to disallow any options which would alter the
      // driver behavior; that isn't going to work in our model. We
      // use isDriverOption() as an approximation, although things
      // like -O4 are going to slip through.
      if (!XarchArg || Index > Prev + 1) {
        getDriver().Diag(diag::err_drv_invalid_Xarch_argument_with_args)
            << A->getAsString(Args);
        continue;
      } else if (XarchArg->getOption().hasFlag(options::DriverOption)) {
        getDriver().Diag(diag::err_drv_invalid_Xarch_argument_isdriver)
            << A->getAsString(Args);
        continue;
      }

      XarchArg->setBaseArg(A);

      A = XarchArg.release();
      DAL->AddSynthesizedArg(A);

      // Linker input arguments require custom handling. The problem is that we
      // have already constructed the phase actions, so we can not treat them as
      // "input arguments".
      if (A->getOption().hasFlag(options::LinkerInput)) {
        // Convert the argument into individual Zlinker_input_args.
        for (const char *Value : A->getValues()) {
          DAL->AddSeparateArg(
              OriginalArg, Opts.getOption(options::OPT_Zlinker_input), Value);
        }
        continue;
      }
    }

    // Sob. These is strictly gcc compatible for the time being. Apple
    // gcc translates options twice, which means that self-expanding
    // options add duplicates.
    switch ((options::ID)A->getOption().getID()) {
    default:
      DAL->append(A);
      break;

    case options::OPT_mkernel:
    case options::OPT_fapple_kext:
      DAL->append(A);
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_static));
      break;

    case options::OPT_dependency_file:
      DAL->AddSeparateArg(A, Opts.getOption(options::OPT_MF), A->getValue());
      break;

    case options::OPT_gfull:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_g_Flag));
      DAL->AddFlagArg(
          A, Opts.getOption(options::OPT_fno_eliminate_unused_debug_symbols));
      break;

    case options::OPT_gused:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_g_Flag));
      DAL->AddFlagArg(
          A, Opts.getOption(options::OPT_feliminate_unused_debug_symbols));
      break;

    case options::OPT_shared:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_dynamiclib));
      break;

    case options::OPT_fconstant_cfstrings:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_mconstant_cfstrings));
      break;

    case options::OPT_fno_constant_cfstrings:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_mno_constant_cfstrings));
      break;

    case options::OPT_Wnonportable_cfstrings:
      DAL->AddFlagArg(A,
                      Opts.getOption(options::OPT_mwarn_nonportable_cfstrings));
      break;

    case options::OPT_Wno_nonportable_cfstrings:
      DAL->AddFlagArg(
          A, Opts.getOption(options::OPT_mno_warn_nonportable_cfstrings));
      break;

    case options::OPT_fpascal_strings:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_mpascal_strings));
      break;

    case options::OPT_fno_pascal_strings:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_mno_pascal_strings));
      break;
    }
  }

  if (getTriple().getArch() == llvm::Triple::x86 ||
      getTriple().getArch() == llvm::Triple::x86_64)
    if (!Args.hasArgNoClaim(options::OPT_mtune_EQ))
      DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_mtune_EQ),
                        "core2");

  // Add the arch options based on the particular spelling of -arch, to match
  // how the driver driver works.
  if (!BoundArch.empty()) {
    StringRef Name = BoundArch;
    const Option MCpu = Opts.getOption(options::OPT_mcpu_EQ);
    const Option MArch = Opts.getOption(clang::driver::options::OPT_march_EQ);

    // This code must be kept in sync with LLVM's getArchTypeForDarwinArch,
    // which defines the list of which architectures we accept.
    if (Name == "ppc")
      ;
    else if (Name == "ppc601")
      DAL->AddJoinedArg(nullptr, MCpu, "601");
    else if (Name == "ppc603")
      DAL->AddJoinedArg(nullptr, MCpu, "603");
    else if (Name == "ppc604")
      DAL->AddJoinedArg(nullptr, MCpu, "604");
    else if (Name == "ppc604e")
      DAL->AddJoinedArg(nullptr, MCpu, "604e");
    else if (Name == "ppc750")
      DAL->AddJoinedArg(nullptr, MCpu, "750");
    else if (Name == "ppc7400")
      DAL->AddJoinedArg(nullptr, MCpu, "7400");
    else if (Name == "ppc7450")
      DAL->AddJoinedArg(nullptr, MCpu, "7450");
    else if (Name == "ppc970")
      DAL->AddJoinedArg(nullptr, MCpu, "970");

    else if (Name == "ppc64" || Name == "ppc64le")
      DAL->AddFlagArg(nullptr, Opts.getOption(options::OPT_m64));

    else if (Name == "i386")
      ;
    else if (Name == "i486")
      DAL->AddJoinedArg(nullptr, MArch, "i486");
    else if (Name == "i586")
      DAL->AddJoinedArg(nullptr, MArch, "i586");
    else if (Name == "i686")
      DAL->AddJoinedArg(nullptr, MArch, "i686");
    else if (Name == "pentium")
      DAL->AddJoinedArg(nullptr, MArch, "pentium");
    else if (Name == "pentium2")
      DAL->AddJoinedArg(nullptr, MArch, "pentium2");
    else if (Name == "pentpro")
      DAL->AddJoinedArg(nullptr, MArch, "pentiumpro");
    else if (Name == "pentIIm3")
      DAL->AddJoinedArg(nullptr, MArch, "pentium2");

    else if (Name == "x86_64" || Name == "x86_64h")
      DAL->AddFlagArg(nullptr, Opts.getOption(options::OPT_m64));

    else if (Name == "arm")
      DAL->AddJoinedArg(nullptr, MArch, "armv4t");
    else if (Name == "armv4t")
      DAL->AddJoinedArg(nullptr, MArch, "armv4t");
    else if (Name == "armv5")
      DAL->AddJoinedArg(nullptr, MArch, "armv5tej");
    else if (Name == "xscale")
      DAL->AddJoinedArg(nullptr, MArch, "xscale");
    else if (Name == "armv6")
      DAL->AddJoinedArg(nullptr, MArch, "armv6k");
    else if (Name == "armv6m")
      DAL->AddJoinedArg(nullptr, MArch, "armv6m");
    else if (Name == "armv7")
      DAL->AddJoinedArg(nullptr, MArch, "armv7a");
    else if (Name == "armv7em")
      DAL->AddJoinedArg(nullptr, MArch, "armv7em");
    else if (Name == "armv7k")
      DAL->AddJoinedArg(nullptr, MArch, "armv7k");
    else if (Name == "armv7m")
      DAL->AddJoinedArg(nullptr, MArch, "armv7m");
    else if (Name == "armv7s")
      DAL->AddJoinedArg(nullptr, MArch, "armv7s");
  }

  return DAL;
}

void MachO::AddLinkRuntimeLibArgs(const ArgList &Args,
                                  ArgStringList &CmdArgs) const {
  // Embedded targets are simple at the moment, not supporting sanitizers and
  // with different libraries for each member of the product { static, PIC } x
  // { hard-float, soft-float }
  llvm::SmallString<32> CompilerRT = StringRef("");
  CompilerRT +=
      (tools::arm::getARMFloatABI(*this, Args) == tools::arm::FloatABI::Hard)
          ? "hard"
          : "soft";
  CompilerRT += Args.hasArg(options::OPT_fPIC) ? "_pic" : "_static";

  AddLinkRuntimeLib(Args, CmdArgs, CompilerRT, RLO_IsEmbedded);
}

bool Darwin::isAlignedAllocationUnavailable() const {
  llvm::Triple::OSType OS;

  switch (TargetPlatform) {
  case MacOS: // Earlier than 10.13.
    OS = llvm::Triple::MacOSX;
    break;
  case IPhoneOS:
    OS = llvm::Triple::IOS;
    break;
  case TvOS: // Earlier than 11.0.
    OS = llvm::Triple::TvOS;
    break;
  case WatchOS: // Earlier than 4.0.
    OS = llvm::Triple::WatchOS;
    break;
  }

  return TargetVersion < alignedAllocMinVersion(OS);
}

void Darwin::addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                                   llvm::opt::ArgStringList &CC1Args,
                                   Action::OffloadKind DeviceOffloadKind) const {
  // Pass "-faligned-alloc-unavailable" only when the user hasn't manually
  // enabled or disabled aligned allocations.
  if (!DriverArgs.hasArgNoClaim(options::OPT_faligned_allocation,
                                options::OPT_fno_aligned_allocation) &&
      isAlignedAllocationUnavailable())
    CC1Args.push_back("-faligned-alloc-unavailable");

  if (SDKInfo) {
    /// Pass the SDK version to the compiler when the SDK information is
    /// available.
    std::string Arg;
    llvm::raw_string_ostream OS(Arg);
    OS << "-target-sdk-version=" << SDKInfo->getVersion();
    CC1Args.push_back(DriverArgs.MakeArgString(OS.str()));
  }
}

DerivedArgList *
Darwin::TranslateArgs(const DerivedArgList &Args, StringRef BoundArch,
                      Action::OffloadKind DeviceOffloadKind) const {
  // First get the generic Apple args, before moving onto Darwin-specific ones.
  DerivedArgList *DAL =
      MachO::TranslateArgs(Args, BoundArch, DeviceOffloadKind);
  const OptTable &Opts = getDriver().getOpts();

  // If no architecture is bound, none of the translations here are relevant.
  if (BoundArch.empty())
    return DAL;

  // Add an explicit version min argument for the deployment target. We do this
  // after argument translation because -Xarch_ arguments may add a version min
  // argument.
  AddDeploymentTarget(*DAL);

  // For iOS 6, undo the translation to add -static for -mkernel/-fapple-kext.
  // FIXME: It would be far better to avoid inserting those -static arguments,
  // but we can't check the deployment target in the translation code until
  // it is set here.
  if (isTargetWatchOSBased() ||
      (isTargetIOSBased() && !isIPhoneOSVersionLT(6, 0))) {
    for (ArgList::iterator it = DAL->begin(), ie = DAL->end(); it != ie; ) {
      Arg *A = *it;
      ++it;
      if (A->getOption().getID() != options::OPT_mkernel &&
          A->getOption().getID() != options::OPT_fapple_kext)
        continue;
      assert(it != ie && "unexpected argument translation");
      A = *it;
      assert(A->getOption().getID() == options::OPT_static &&
             "missing expected -static argument");
      *it = nullptr;
      ++it;
    }
  }

  if (!Args.getLastArg(options::OPT_stdlib_EQ) &&
      GetCXXStdlibType(Args) == ToolChain::CST_Libcxx)
    DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_stdlib_EQ),
                      "libc++");

  // Validate the C++ standard library choice.
  CXXStdlibType Type = GetCXXStdlibType(*DAL);
  if (Type == ToolChain::CST_Libcxx) {
    // Check whether the target provides libc++.
    StringRef where;

    // Complain about targeting iOS < 5.0 in any way.
    if (isTargetIOSBased() && isIPhoneOSVersionLT(5, 0))
      where = "iOS 5.0";

    if (where != StringRef()) {
      getDriver().Diag(clang::diag::err_drv_invalid_libcxx_deployment) << where;
    }
  }

  auto Arch = tools::darwin::getArchTypeForMachOArchName(BoundArch);
  if ((Arch == llvm::Triple::arm || Arch == llvm::Triple::thumb)) {
    if (Args.hasFlag(options::OPT_fomit_frame_pointer,
                     options::OPT_fno_omit_frame_pointer, false))
      getDriver().Diag(clang::diag::warn_drv_unsupported_opt_for_target)
          << "-fomit-frame-pointer" << BoundArch;
  }

  return DAL;
}

bool MachO::IsUnwindTablesDefault(const ArgList &Args) const {
  // Unwind tables are not emitted if -fno-exceptions is supplied (except when
  // targeting x86_64).
  return getArch() == llvm::Triple::x86_64 ||
         (GetExceptionModel(Args) != llvm::ExceptionHandling::SjLj &&
          Args.hasFlag(options::OPT_fexceptions, options::OPT_fno_exceptions,
                       true));
}

bool MachO::UseDwarfDebugFlags() const {
  if (const char *S = ::getenv("RC_DEBUG_OPTIONS"))
    return S[0] != '\0';
  return false;
}

llvm::ExceptionHandling Darwin::GetExceptionModel(const ArgList &Args) const {
  // Darwin uses SjLj exceptions on ARM.
  if (getTriple().getArch() != llvm::Triple::arm &&
      getTriple().getArch() != llvm::Triple::thumb)
    return llvm::ExceptionHandling::None;

  // Only watchOS uses the new DWARF/Compact unwinding method.
  llvm::Triple Triple(ComputeLLVMTriple(Args));
  if (Triple.isWatchABI())
    return llvm::ExceptionHandling::DwarfCFI;

  return llvm::ExceptionHandling::SjLj;
}

bool Darwin::SupportsEmbeddedBitcode() const {
  assert(TargetInitialized && "Target not initialized!");
  if (isTargetIPhoneOS() && isIPhoneOSVersionLT(6, 0))
    return false;
  return true;
}

bool MachO::isPICDefault() const { return true; }

bool MachO::isPIEDefault() const { return false; }

bool MachO::isPICDefaultForced() const {
  return (getArch() == llvm::Triple::x86_64 ||
          getArch() == llvm::Triple::aarch64);
}

bool MachO::SupportsProfiling() const {
  // Profiling instrumentation is only supported on x86.
  return getArch() == llvm::Triple::x86 || getArch() == llvm::Triple::x86_64;
}

void Darwin::addMinVersionArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  VersionTuple TargetVersion = getTargetVersion();

  if (isTargetWatchOS())
    CmdArgs.push_back("-watchos_version_min");
  else if (isTargetWatchOSSimulator())
    CmdArgs.push_back("-watchos_simulator_version_min");
  else if (isTargetTvOS())
    CmdArgs.push_back("-tvos_version_min");
  else if (isTargetTvOSSimulator())
    CmdArgs.push_back("-tvos_simulator_version_min");
  else if (isTargetIOSSimulator())
    CmdArgs.push_back("-ios_simulator_version_min");
  else if (isTargetIOSBased())
    CmdArgs.push_back("-iphoneos_version_min");
  else {
    assert(isTargetMacOS() && "unexpected target");
    CmdArgs.push_back("-macosx_version_min");
  }

  CmdArgs.push_back(Args.MakeArgString(TargetVersion.getAsString()));
}

void Darwin::addStartObjectFileArgs(const ArgList &Args,
                                    ArgStringList &CmdArgs) const {
  // Derived from startfile spec.
  if (Args.hasArg(options::OPT_dynamiclib)) {
    // Derived from darwin_dylib1 spec.
    if (isTargetWatchOSBased()) {
      ; // watchOS does not need dylib1.o.
    } else if (isTargetIOSSimulator()) {
      ; // iOS simulator does not need dylib1.o.
    } else if (isTargetIPhoneOS()) {
      if (isIPhoneOSVersionLT(3, 1))
        CmdArgs.push_back("-ldylib1.o");
    } else {
      if (isMacosxVersionLT(10, 5))
        CmdArgs.push_back("-ldylib1.o");
      else if (isMacosxVersionLT(10, 6))
        CmdArgs.push_back("-ldylib1.10.5.o");
    }
  } else {
    if (Args.hasArg(options::OPT_bundle)) {
      if (!Args.hasArg(options::OPT_static)) {
        // Derived from darwin_bundle1 spec.
        if (isTargetWatchOSBased()) {
          ; // watchOS does not need bundle1.o.
        } else if (isTargetIOSSimulator()) {
          ; // iOS simulator does not need bundle1.o.
        } else if (isTargetIPhoneOS()) {
          if (isIPhoneOSVersionLT(3, 1))
            CmdArgs.push_back("-lbundle1.o");
        } else {
          if (isMacosxVersionLT(10, 6))
            CmdArgs.push_back("-lbundle1.o");
        }
      }
    } else {
      if (Args.hasArg(options::OPT_pg) && SupportsProfiling()) {
        if (Args.hasArg(options::OPT_static) ||
            Args.hasArg(options::OPT_object) ||
            Args.hasArg(options::OPT_preload)) {
          CmdArgs.push_back("-lgcrt0.o");
        } else {
          CmdArgs.push_back("-lgcrt1.o");

          // darwin_crt2 spec is empty.
        }
        // By default on OS X 10.8 and later, we don't link with a crt1.o
        // file and the linker knows to use _main as the entry point.  But,
        // when compiling with -pg, we need to link with the gcrt1.o file,
        // so pass the -no_new_main option to tell the linker to use the
        // "start" symbol as the entry point.
        if (isTargetMacOS() && !isMacosxVersionLT(10, 8))
          CmdArgs.push_back("-no_new_main");
      } else {
        if (Args.hasArg(options::OPT_static) ||
            Args.hasArg(options::OPT_object) ||
            Args.hasArg(options::OPT_preload)) {
          CmdArgs.push_back("-lcrt0.o");
        } else {
          // Derived from darwin_crt1 spec.
          if (isTargetWatchOSBased()) {
            ; // watchOS does not need crt1.o.
          } else if (isTargetIOSSimulator()) {
            ; // iOS simulator does not need crt1.o.
          } else if (isTargetIPhoneOS()) {
            if (getArch() == llvm::Triple::aarch64)
              ; // iOS does not need any crt1 files for arm64
            else if (isIPhoneOSVersionLT(3, 1))
              CmdArgs.push_back("-lcrt1.o");
            else if (isIPhoneOSVersionLT(6, 0))
              CmdArgs.push_back("-lcrt1.3.1.o");
          } else {
            if (isMacosxVersionLT(10, 5))
              CmdArgs.push_back("-lcrt1.o");
            else if (isMacosxVersionLT(10, 6))
              CmdArgs.push_back("-lcrt1.10.5.o");
            else if (isMacosxVersionLT(10, 8))
              CmdArgs.push_back("-lcrt1.10.6.o");

            // darwin_crt2 spec is empty.
          }
        }
      }
    }
  }

  if (!isTargetIPhoneOS() && Args.hasArg(options::OPT_shared_libgcc) &&
      !isTargetWatchOS() && isMacosxVersionLT(10, 5)) {
    const char *Str = Args.MakeArgString(GetFilePath("crt3.o"));
    CmdArgs.push_back(Str);
  }
}

void Darwin::CheckObjCARC() const {
  if (isTargetIOSBased() || isTargetWatchOSBased() ||
      (isTargetMacOS() && !isMacosxVersionLT(10, 6)))
    return;
  getDriver().Diag(diag::err_arc_unsupported_on_toolchain);
}

SanitizerMask Darwin::getSupportedSanitizers() const {
  const bool IsX86_64 = getTriple().getArch() == llvm::Triple::x86_64;
  SanitizerMask Res = ToolChain::getSupportedSanitizers();
  Res |= SanitizerKind::Address;
  Res |= SanitizerKind::Leak;
  Res |= SanitizerKind::Fuzzer;
  Res |= SanitizerKind::FuzzerNoLink;
  Res |= SanitizerKind::Function;

  // Prior to 10.9, macOS shipped a version of the C++ standard library without
  // C++11 support. The same is true of iOS prior to version 5. These OS'es are
  // incompatible with -fsanitize=vptr.
  if (!(isTargetMacOS() && isMacosxVersionLT(10, 9))
      && !(isTargetIPhoneOS() && isIPhoneOSVersionLT(5, 0)))
    Res |= SanitizerKind::Vptr;

  if (isTargetMacOS()) {
    if (IsX86_64)
      Res |= SanitizerKind::Thread;
  } else if (isTargetIOSSimulator() || isTargetTvOSSimulator()) {
    if (IsX86_64)
      Res |= SanitizerKind::Thread;
  }
  return Res;
}

void Darwin::printVerboseInfo(raw_ostream &OS) const {
  CudaInstallation.print(OS);
}
