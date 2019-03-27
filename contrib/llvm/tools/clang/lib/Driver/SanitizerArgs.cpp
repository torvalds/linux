//===--- SanitizerArgs.cpp - Arguments for sanitizer tools  ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "clang/Driver/SanitizerArgs.h"
#include "ToolChains/CommonArgs.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/ToolChain.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/TargetParser.h"
#include <memory>

using namespace clang;
using namespace clang::SanitizerKind;
using namespace clang::driver;
using namespace llvm::opt;

enum : SanitizerMask {
  NeedsUbsanRt = Undefined | Integer | ImplicitConversion | Nullability | CFI,
  NeedsUbsanCxxRt = Vptr | CFI,
  NotAllowedWithTrap = Vptr,
  NotAllowedWithMinimalRuntime = Vptr,
  RequiresPIE = DataFlow | HWAddress | Scudo,
  NeedsUnwindTables = Address | HWAddress | Thread | Memory | DataFlow,
  SupportsCoverage = Address | HWAddress | KernelAddress | KernelHWAddress |
                     Memory | KernelMemory | Leak | Undefined | Integer |
                     ImplicitConversion | Nullability | DataFlow | Fuzzer |
                     FuzzerNoLink,
  RecoverableByDefault = Undefined | Integer | ImplicitConversion | Nullability,
  Unrecoverable = Unreachable | Return,
  AlwaysRecoverable = KernelAddress | KernelHWAddress,
  LegacyFsanitizeRecoverMask = Undefined | Integer,
  NeedsLTO = CFI,
  TrappingSupported = (Undefined & ~Vptr) | UnsignedIntegerOverflow |
                      ImplicitConversion | Nullability | LocalBounds | CFI,
  TrappingDefault = CFI,
  CFIClasses =
      CFIVCall | CFINVCall | CFIMFCall | CFIDerivedCast | CFIUnrelatedCast,
  CompatibleWithMinimalRuntime = TrappingSupported | Scudo | ShadowCallStack,
};

enum CoverageFeature {
  CoverageFunc = 1 << 0,
  CoverageBB = 1 << 1,
  CoverageEdge = 1 << 2,
  CoverageIndirCall = 1 << 3,
  CoverageTraceBB = 1 << 4,  // Deprecated.
  CoverageTraceCmp = 1 << 5,
  CoverageTraceDiv = 1 << 6,
  CoverageTraceGep = 1 << 7,
  Coverage8bitCounters = 1 << 8,  // Deprecated.
  CoverageTracePC = 1 << 9,
  CoverageTracePCGuard = 1 << 10,
  CoverageNoPrune = 1 << 11,
  CoverageInline8bitCounters = 1 << 12,
  CoveragePCTable = 1 << 13,
  CoverageStackDepth = 1 << 14,
};

/// Parse a -fsanitize= or -fno-sanitize= argument's values, diagnosing any
/// invalid components. Returns a SanitizerMask.
static SanitizerMask parseArgValues(const Driver &D, const llvm::opt::Arg *A,
                                    bool DiagnoseErrors);

/// Parse -f(no-)?sanitize-coverage= flag values, diagnosing any invalid
/// components. Returns OR of members of \c CoverageFeature enumeration.
static int parseCoverageFeatures(const Driver &D, const llvm::opt::Arg *A);

/// Produce an argument string from ArgList \p Args, which shows how it
/// provides some sanitizer kind from \p Mask. For example, the argument list
/// "-fsanitize=thread,vptr -fsanitize=address" with mask \c NeedsUbsanRt
/// would produce "-fsanitize=vptr".
static std::string lastArgumentForMask(const Driver &D,
                                       const llvm::opt::ArgList &Args,
                                       SanitizerMask Mask);

/// Produce an argument string from argument \p A, which shows how it provides
/// a value in \p Mask. For instance, the argument
/// "-fsanitize=address,alignment" with mask \c NeedsUbsanRt would produce
/// "-fsanitize=alignment".
static std::string describeSanitizeArg(const llvm::opt::Arg *A,
                                       SanitizerMask Mask);

/// Produce a string containing comma-separated names of sanitizers in \p
/// Sanitizers set.
static std::string toString(const clang::SanitizerSet &Sanitizers);

static void addDefaultBlacklists(const Driver &D, SanitizerMask Kinds,
                                 std::vector<std::string> &BlacklistFiles) {
  struct Blacklist {
    const char *File;
    SanitizerMask Mask;
  } Blacklists[] = {{"asan_blacklist.txt", Address},
                    {"hwasan_blacklist.txt", HWAddress},
                    {"msan_blacklist.txt", Memory},
                    {"tsan_blacklist.txt", Thread},
                    {"dfsan_abilist.txt", DataFlow},
                    {"cfi_blacklist.txt", CFI},
                    {"ubsan_blacklist.txt", Undefined | Integer | Nullability}};

  for (auto BL : Blacklists) {
    if (!(Kinds & BL.Mask))
      continue;

    clang::SmallString<64> Path(D.ResourceDir);
    llvm::sys::path::append(Path, "share", BL.File);
    if (llvm::sys::fs::exists(Path))
      BlacklistFiles.push_back(Path.str());
    else if (BL.Mask == CFI)
      // If cfi_blacklist.txt cannot be found in the resource dir, driver
      // should fail.
      D.Diag(clang::diag::err_drv_no_such_file) << Path;
  }
}

/// Sets group bits for every group that has at least one representative already
/// enabled in \p Kinds.
static SanitizerMask setGroupBits(SanitizerMask Kinds) {
#define SANITIZER(NAME, ID)
#define SANITIZER_GROUP(NAME, ID, ALIAS)                                       \
  if (Kinds & SanitizerKind::ID)                                               \
    Kinds |= SanitizerKind::ID##Group;
#include "clang/Basic/Sanitizers.def"
  return Kinds;
}

static SanitizerMask parseSanitizeTrapArgs(const Driver &D,
                                           const llvm::opt::ArgList &Args) {
  SanitizerMask TrapRemove = 0; // During the loop below, the accumulated set of
                                // sanitizers disabled by the current sanitizer
                                // argument or any argument after it.
  SanitizerMask TrappingKinds = 0;
  SanitizerMask TrappingSupportedWithGroups = setGroupBits(TrappingSupported);

  for (ArgList::const_reverse_iterator I = Args.rbegin(), E = Args.rend();
       I != E; ++I) {
    const auto *Arg = *I;
    if (Arg->getOption().matches(options::OPT_fsanitize_trap_EQ)) {
      Arg->claim();
      SanitizerMask Add = parseArgValues(D, Arg, true);
      Add &= ~TrapRemove;
      if (SanitizerMask InvalidValues = Add & ~TrappingSupportedWithGroups) {
        SanitizerSet S;
        S.Mask = InvalidValues;
        D.Diag(diag::err_drv_unsupported_option_argument) << "-fsanitize-trap"
                                                          << toString(S);
      }
      TrappingKinds |= expandSanitizerGroups(Add) & ~TrapRemove;
    } else if (Arg->getOption().matches(options::OPT_fno_sanitize_trap_EQ)) {
      Arg->claim();
      TrapRemove |= expandSanitizerGroups(parseArgValues(D, Arg, true));
    } else if (Arg->getOption().matches(
                   options::OPT_fsanitize_undefined_trap_on_error)) {
      Arg->claim();
      TrappingKinds |=
          expandSanitizerGroups(UndefinedGroup & ~TrapRemove) & ~TrapRemove;
    } else if (Arg->getOption().matches(
                   options::OPT_fno_sanitize_undefined_trap_on_error)) {
      Arg->claim();
      TrapRemove |= expandSanitizerGroups(UndefinedGroup);
    }
  }

  // Apply default trapping behavior.
  TrappingKinds |= TrappingDefault & ~TrapRemove;

  return TrappingKinds;
}

bool SanitizerArgs::needsUbsanRt() const {
  // All of these include ubsan.
  if (needsAsanRt() || needsMsanRt() || needsHwasanRt() || needsTsanRt() ||
      needsDfsanRt() || needsLsanRt() || needsCfiDiagRt() ||
      (needsScudoRt() && !requiresMinimalRuntime()))
    return false;

  return (Sanitizers.Mask & NeedsUbsanRt & ~TrapSanitizers.Mask) ||
         CoverageFeatures;
}

bool SanitizerArgs::needsCfiRt() const {
  return !(Sanitizers.Mask & CFI & ~TrapSanitizers.Mask) && CfiCrossDso &&
         !ImplicitCfiRuntime;
}

bool SanitizerArgs::needsCfiDiagRt() const {
  return (Sanitizers.Mask & CFI & ~TrapSanitizers.Mask) && CfiCrossDso &&
         !ImplicitCfiRuntime;
}

bool SanitizerArgs::requiresPIE() const {
  return NeedPIE || (Sanitizers.Mask & RequiresPIE);
}

bool SanitizerArgs::needsUnwindTables() const {
  return Sanitizers.Mask & NeedsUnwindTables;
}

bool SanitizerArgs::needsLTO() const { return Sanitizers.Mask & NeedsLTO; }

SanitizerArgs::SanitizerArgs(const ToolChain &TC,
                             const llvm::opt::ArgList &Args) {
  SanitizerMask AllRemove = 0;  // During the loop below, the accumulated set of
                                // sanitizers disabled by the current sanitizer
                                // argument or any argument after it.
  SanitizerMask AllAddedKinds = 0;  // Mask of all sanitizers ever enabled by
                                    // -fsanitize= flags (directly or via group
                                    // expansion), some of which may be disabled
                                    // later. Used to carefully prune
                                    // unused-argument diagnostics.
  SanitizerMask DiagnosedKinds = 0;  // All Kinds we have diagnosed up to now.
                                     // Used to deduplicate diagnostics.
  SanitizerMask Kinds = 0;
  const SanitizerMask Supported = setGroupBits(TC.getSupportedSanitizers());

  CfiCrossDso = Args.hasFlag(options::OPT_fsanitize_cfi_cross_dso,
                             options::OPT_fno_sanitize_cfi_cross_dso, false);

  ToolChain::RTTIMode RTTIMode = TC.getRTTIMode();

  const Driver &D = TC.getDriver();
  SanitizerMask TrappingKinds = parseSanitizeTrapArgs(D, Args);
  SanitizerMask InvalidTrappingKinds = TrappingKinds & NotAllowedWithTrap;

  MinimalRuntime =
      Args.hasFlag(options::OPT_fsanitize_minimal_runtime,
                   options::OPT_fno_sanitize_minimal_runtime, MinimalRuntime);

  // The object size sanitizer should not be enabled at -O0.
  Arg *OptLevel = Args.getLastArg(options::OPT_O_Group);
  bool RemoveObjectSizeAtO0 =
      !OptLevel || OptLevel->getOption().matches(options::OPT_O0);

  for (ArgList::const_reverse_iterator I = Args.rbegin(), E = Args.rend();
       I != E; ++I) {
    const auto *Arg = *I;
    if (Arg->getOption().matches(options::OPT_fsanitize_EQ)) {
      Arg->claim();
      SanitizerMask Add = parseArgValues(D, Arg, /*AllowGroups=*/true);

      if (RemoveObjectSizeAtO0) {
        AllRemove |= SanitizerKind::ObjectSize;

        // The user explicitly enabled the object size sanitizer. Warn that
        // that this does nothing at -O0.
        if (Add & SanitizerKind::ObjectSize)
          D.Diag(diag::warn_drv_object_size_disabled_O0)
              << Arg->getAsString(Args);
      }

      AllAddedKinds |= expandSanitizerGroups(Add);

      // Avoid diagnosing any sanitizer which is disabled later.
      Add &= ~AllRemove;
      // At this point we have not expanded groups, so any unsupported
      // sanitizers in Add are those which have been explicitly enabled.
      // Diagnose them.
      if (SanitizerMask KindsToDiagnose =
              Add & InvalidTrappingKinds & ~DiagnosedKinds) {
        std::string Desc = describeSanitizeArg(*I, KindsToDiagnose);
        D.Diag(diag::err_drv_argument_not_allowed_with)
            << Desc << "-fsanitize-trap=undefined";
        DiagnosedKinds |= KindsToDiagnose;
      }
      Add &= ~InvalidTrappingKinds;

      if (MinimalRuntime) {
        if (SanitizerMask KindsToDiagnose =
                Add & NotAllowedWithMinimalRuntime & ~DiagnosedKinds) {
          std::string Desc = describeSanitizeArg(*I, KindsToDiagnose);
          D.Diag(diag::err_drv_argument_not_allowed_with)
              << Desc << "-fsanitize-minimal-runtime";
          DiagnosedKinds |= KindsToDiagnose;
        }
        Add &= ~NotAllowedWithMinimalRuntime;
      }

      // FIXME: Make CFI on member function calls compatible with cross-DSO CFI.
      // There are currently two problems:
      // - Virtual function call checks need to pass a pointer to the function
      //   address to llvm.type.test and a pointer to the address point to the
      //   diagnostic function. Currently we pass the same pointer to both
      //   places.
      // - Non-virtual function call checks may need to check multiple type
      //   identifiers.
      // Fixing both of those may require changes to the cross-DSO CFI
      // interface.
      if (CfiCrossDso && (Add & CFIMFCall & ~DiagnosedKinds)) {
        D.Diag(diag::err_drv_argument_not_allowed_with)
            << "-fsanitize=cfi-mfcall"
            << "-fsanitize-cfi-cross-dso";
        Add &= ~CFIMFCall;
        DiagnosedKinds |= CFIMFCall;
      }

      if (SanitizerMask KindsToDiagnose = Add & ~Supported & ~DiagnosedKinds) {
        std::string Desc = describeSanitizeArg(*I, KindsToDiagnose);
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << Desc << TC.getTriple().str();
        DiagnosedKinds |= KindsToDiagnose;
      }
      Add &= Supported;

      // Test for -fno-rtti + explicit -fsanitizer=vptr before expanding groups
      // so we don't error out if -fno-rtti and -fsanitize=undefined were
      // passed.
      if ((Add & Vptr) && (RTTIMode == ToolChain::RM_Disabled)) {
        if (const llvm::opt::Arg *NoRTTIArg = TC.getRTTIArg()) {
          assert(NoRTTIArg->getOption().matches(options::OPT_fno_rtti) &&
                  "RTTI disabled without -fno-rtti option?");
          // The user explicitly passed -fno-rtti with -fsanitize=vptr, but
          // the vptr sanitizer requires RTTI, so this is a user error.
          D.Diag(diag::err_drv_argument_not_allowed_with)
              << "-fsanitize=vptr" << NoRTTIArg->getAsString(Args);
        } else {
          // The vptr sanitizer requires RTTI, but RTTI is disabled (by
          // default). Warn that the vptr sanitizer is being disabled.
          D.Diag(diag::warn_drv_disabling_vptr_no_rtti_default);
        }

        // Take out the Vptr sanitizer from the enabled sanitizers
        AllRemove |= Vptr;
      }

      Add = expandSanitizerGroups(Add);
      // Group expansion may have enabled a sanitizer which is disabled later.
      Add &= ~AllRemove;
      // Silently discard any unsupported sanitizers implicitly enabled through
      // group expansion.
      Add &= ~InvalidTrappingKinds;
      if (MinimalRuntime) {
        Add &= ~NotAllowedWithMinimalRuntime;
      }
      if (CfiCrossDso)
        Add &= ~CFIMFCall;
      Add &= Supported;

      if (Add & Fuzzer)
        Add |= FuzzerNoLink;

      // Enable coverage if the fuzzing flag is set.
      if (Add & FuzzerNoLink) {
        CoverageFeatures |= CoverageInline8bitCounters | CoverageIndirCall |
                            CoverageTraceCmp | CoveragePCTable;
        // Due to TLS differences, stack depth tracking is only enabled on Linux
        if (TC.getTriple().isOSLinux())
          CoverageFeatures |= CoverageStackDepth;
      }

      Kinds |= Add;
    } else if (Arg->getOption().matches(options::OPT_fno_sanitize_EQ)) {
      Arg->claim();
      SanitizerMask Remove = parseArgValues(D, Arg, true);
      AllRemove |= expandSanitizerGroups(Remove);
    }
  }

  std::pair<SanitizerMask, SanitizerMask> IncompatibleGroups[] = {
      std::make_pair(Address, Thread | Memory),
      std::make_pair(Thread, Memory),
      std::make_pair(Leak, Thread | Memory),
      std::make_pair(KernelAddress, Address | Leak | Thread | Memory),
      std::make_pair(HWAddress, Address | Thread | Memory | KernelAddress),
      std::make_pair(Efficiency, Address | HWAddress | Leak | Thread | Memory |
                                     KernelAddress),
      std::make_pair(Scudo, Address | HWAddress | Leak | Thread | Memory |
                                KernelAddress | Efficiency),
      std::make_pair(SafeStack, Address | HWAddress | Leak | Thread | Memory |
                                    KernelAddress | Efficiency),
      std::make_pair(KernelHWAddress, Address | HWAddress | Leak | Thread |
                                          Memory | KernelAddress | Efficiency |
                                          SafeStack),
      std::make_pair(KernelMemory, Address | HWAddress | Leak | Thread |
                                       Memory | KernelAddress | Efficiency |
                                       Scudo | SafeStack)};
  // Enable toolchain specific default sanitizers if not explicitly disabled.
  SanitizerMask Default = TC.getDefaultSanitizers() & ~AllRemove;

  // Disable default sanitizers that are incompatible with explicitly requested
  // ones.
  for (auto G : IncompatibleGroups) {
    SanitizerMask Group = G.first;
    if ((Default & Group) && (Kinds & G.second))
      Default &= ~Group;
  }

  Kinds |= Default;

  // We disable the vptr sanitizer if it was enabled by group expansion but RTTI
  // is disabled.
  if ((Kinds & Vptr) && (RTTIMode == ToolChain::RM_Disabled)) {
    Kinds &= ~Vptr;
  }

  // Check that LTO is enabled if we need it.
  if ((Kinds & NeedsLTO) && !D.isUsingLTO()) {
    D.Diag(diag::err_drv_argument_only_allowed_with)
        << lastArgumentForMask(D, Args, Kinds & NeedsLTO) << "-flto";
  }

  if ((Kinds & ShadowCallStack) &&
      TC.getTriple().getArch() == llvm::Triple::aarch64 &&
      !llvm::AArch64::isX18ReservedByDefault(TC.getTriple()) &&
      !Args.hasArg(options::OPT_ffixed_x18)) {
    D.Diag(diag::err_drv_argument_only_allowed_with)
        << lastArgumentForMask(D, Args, Kinds & ShadowCallStack)
        << "-ffixed-x18";
  }

  // Report error if there are non-trapping sanitizers that require
  // c++abi-specific  parts of UBSan runtime, and they are not provided by the
  // toolchain. We don't have a good way to check the latter, so we just
  // check if the toolchan supports vptr.
  if (~Supported & Vptr) {
    SanitizerMask KindsToDiagnose = Kinds & ~TrappingKinds & NeedsUbsanCxxRt;
    // The runtime library supports the Microsoft C++ ABI, but only well enough
    // for CFI. FIXME: Remove this once we support vptr on Windows.
    if (TC.getTriple().isOSWindows())
      KindsToDiagnose &= ~CFI;
    if (KindsToDiagnose) {
      SanitizerSet S;
      S.Mask = KindsToDiagnose;
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << ("-fno-sanitize-trap=" + toString(S)) << TC.getTriple().str();
      Kinds &= ~KindsToDiagnose;
    }
  }

  // Warn about incompatible groups of sanitizers.
  for (auto G : IncompatibleGroups) {
    SanitizerMask Group = G.first;
    if (Kinds & Group) {
      if (SanitizerMask Incompatible = Kinds & G.second) {
        D.Diag(clang::diag::err_drv_argument_not_allowed_with)
            << lastArgumentForMask(D, Args, Group)
            << lastArgumentForMask(D, Args, Incompatible);
        Kinds &= ~Incompatible;
      }
    }
  }
  // FIXME: Currently -fsanitize=leak is silently ignored in the presence of
  // -fsanitize=address. Perhaps it should print an error, or perhaps
  // -f(-no)sanitize=leak should change whether leak detection is enabled by
  // default in ASan?

  // Parse -f(no-)?sanitize-recover flags.
  SanitizerMask RecoverableKinds = RecoverableByDefault | AlwaysRecoverable;
  SanitizerMask DiagnosedUnrecoverableKinds = 0;
  SanitizerMask DiagnosedAlwaysRecoverableKinds = 0;
  for (const auto *Arg : Args) {
    const char *DeprecatedReplacement = nullptr;
    if (Arg->getOption().matches(options::OPT_fsanitize_recover)) {
      DeprecatedReplacement =
          "-fsanitize-recover=undefined,integer' or '-fsanitize-recover=all";
      RecoverableKinds |= expandSanitizerGroups(LegacyFsanitizeRecoverMask);
      Arg->claim();
    } else if (Arg->getOption().matches(options::OPT_fno_sanitize_recover)) {
      DeprecatedReplacement = "-fno-sanitize-recover=undefined,integer' or "
                              "'-fno-sanitize-recover=all";
      RecoverableKinds &= ~expandSanitizerGroups(LegacyFsanitizeRecoverMask);
      Arg->claim();
    } else if (Arg->getOption().matches(options::OPT_fsanitize_recover_EQ)) {
      SanitizerMask Add = parseArgValues(D, Arg, true);
      // Report error if user explicitly tries to recover from unrecoverable
      // sanitizer.
      if (SanitizerMask KindsToDiagnose =
              Add & Unrecoverable & ~DiagnosedUnrecoverableKinds) {
        SanitizerSet SetToDiagnose;
        SetToDiagnose.Mask |= KindsToDiagnose;
        D.Diag(diag::err_drv_unsupported_option_argument)
            << Arg->getOption().getName() << toString(SetToDiagnose);
        DiagnosedUnrecoverableKinds |= KindsToDiagnose;
      }
      RecoverableKinds |= expandSanitizerGroups(Add);
      Arg->claim();
    } else if (Arg->getOption().matches(options::OPT_fno_sanitize_recover_EQ)) {
      SanitizerMask Remove = parseArgValues(D, Arg, true);
      // Report error if user explicitly tries to disable recovery from
      // always recoverable sanitizer.
      if (SanitizerMask KindsToDiagnose =
              Remove & AlwaysRecoverable & ~DiagnosedAlwaysRecoverableKinds) {
        SanitizerSet SetToDiagnose;
        SetToDiagnose.Mask |= KindsToDiagnose;
        D.Diag(diag::err_drv_unsupported_option_argument)
            << Arg->getOption().getName() << toString(SetToDiagnose);
        DiagnosedAlwaysRecoverableKinds |= KindsToDiagnose;
      }
      RecoverableKinds &= ~expandSanitizerGroups(Remove);
      Arg->claim();
    }
    if (DeprecatedReplacement) {
      D.Diag(diag::warn_drv_deprecated_arg) << Arg->getAsString(Args)
                                            << DeprecatedReplacement;
    }
  }
  RecoverableKinds &= Kinds;
  RecoverableKinds &= ~Unrecoverable;

  TrappingKinds &= Kinds;
  RecoverableKinds &= ~TrappingKinds;

  // Setup blacklist files.
  // Add default blacklist from resource directory.
  addDefaultBlacklists(D, Kinds, BlacklistFiles);
  // Parse -f(no-)sanitize-blacklist options.
  for (const auto *Arg : Args) {
    if (Arg->getOption().matches(options::OPT_fsanitize_blacklist)) {
      Arg->claim();
      std::string BLPath = Arg->getValue();
      if (llvm::sys::fs::exists(BLPath)) {
        BlacklistFiles.push_back(BLPath);
        ExtraDeps.push_back(BLPath);
      } else {
        D.Diag(clang::diag::err_drv_no_such_file) << BLPath;
      }
    } else if (Arg->getOption().matches(options::OPT_fno_sanitize_blacklist)) {
      Arg->claim();
      BlacklistFiles.clear();
      ExtraDeps.clear();
    }
  }
  // Validate blacklists format.
  {
    std::string BLError;
    std::unique_ptr<llvm::SpecialCaseList> SCL(
        llvm::SpecialCaseList::create(BlacklistFiles, BLError));
    if (!SCL.get())
      D.Diag(clang::diag::err_drv_malformed_sanitizer_blacklist) << BLError;
  }

  // Parse -f[no-]sanitize-memory-track-origins[=level] options.
  if (AllAddedKinds & Memory) {
    if (Arg *A =
            Args.getLastArg(options::OPT_fsanitize_memory_track_origins_EQ,
                            options::OPT_fsanitize_memory_track_origins,
                            options::OPT_fno_sanitize_memory_track_origins)) {
      if (A->getOption().matches(options::OPT_fsanitize_memory_track_origins)) {
        MsanTrackOrigins = 2;
      } else if (A->getOption().matches(
                     options::OPT_fno_sanitize_memory_track_origins)) {
        MsanTrackOrigins = 0;
      } else {
        StringRef S = A->getValue();
        if (S.getAsInteger(0, MsanTrackOrigins) || MsanTrackOrigins < 0 ||
            MsanTrackOrigins > 2) {
          D.Diag(clang::diag::err_drv_invalid_value) << A->getAsString(Args) << S;
        }
      }
    }
    MsanUseAfterDtor =
        Args.hasFlag(options::OPT_fsanitize_memory_use_after_dtor,
                     options::OPT_fno_sanitize_memory_use_after_dtor,
                     MsanUseAfterDtor);
    NeedPIE |= !(TC.getTriple().isOSLinux() &&
                 TC.getTriple().getArch() == llvm::Triple::x86_64);
  } else {
    MsanUseAfterDtor = false;
  }

  if (AllAddedKinds & Thread) {
    TsanMemoryAccess = Args.hasFlag(options::OPT_fsanitize_thread_memory_access,
                                    options::OPT_fno_sanitize_thread_memory_access,
                                    TsanMemoryAccess);
    TsanFuncEntryExit = Args.hasFlag(options::OPT_fsanitize_thread_func_entry_exit,
                                     options::OPT_fno_sanitize_thread_func_entry_exit,
                                     TsanFuncEntryExit);
    TsanAtomics = Args.hasFlag(options::OPT_fsanitize_thread_atomics,
                               options::OPT_fno_sanitize_thread_atomics,
                               TsanAtomics);
  }

  if (AllAddedKinds & CFI) {
    // Without PIE, external function address may resolve to a PLT record, which
    // can not be verified by the target module.
    NeedPIE |= CfiCrossDso;
    CfiICallGeneralizePointers =
        Args.hasArg(options::OPT_fsanitize_cfi_icall_generalize_pointers);

    if (CfiCrossDso && CfiICallGeneralizePointers)
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << "-fsanitize-cfi-cross-dso"
          << "-fsanitize-cfi-icall-generalize-pointers";
  }

  Stats = Args.hasFlag(options::OPT_fsanitize_stats,
                       options::OPT_fno_sanitize_stats, false);

  if (MinimalRuntime) {
    SanitizerMask IncompatibleMask =
        Kinds & ~setGroupBits(CompatibleWithMinimalRuntime);
    if (IncompatibleMask)
      D.Diag(clang::diag::err_drv_argument_not_allowed_with)
          << "-fsanitize-minimal-runtime"
          << lastArgumentForMask(D, Args, IncompatibleMask);

    SanitizerMask NonTrappingCfi = Kinds & CFI & ~TrappingKinds;
    if (NonTrappingCfi)
      D.Diag(clang::diag::err_drv_argument_only_allowed_with)
          << "fsanitize-minimal-runtime"
          << "fsanitize-trap=cfi";
  }

  // Parse -f(no-)?sanitize-coverage flags if coverage is supported by the
  // enabled sanitizers.
  for (const auto *Arg : Args) {
    if (Arg->getOption().matches(options::OPT_fsanitize_coverage)) {
      int LegacySanitizeCoverage;
      if (Arg->getNumValues() == 1 &&
          !StringRef(Arg->getValue(0))
               .getAsInteger(0, LegacySanitizeCoverage)) {
        CoverageFeatures = 0;
        Arg->claim();
        if (LegacySanitizeCoverage != 0) {
          D.Diag(diag::warn_drv_deprecated_arg)
              << Arg->getAsString(Args) << "-fsanitize-coverage=trace-pc-guard";
        }
        continue;
      }
      CoverageFeatures |= parseCoverageFeatures(D, Arg);

      // Disable coverage and not claim the flags if there is at least one
      // non-supporting sanitizer.
      if (!(AllAddedKinds & ~AllRemove & ~setGroupBits(SupportsCoverage))) {
        Arg->claim();
      } else {
        CoverageFeatures = 0;
      }
    } else if (Arg->getOption().matches(options::OPT_fno_sanitize_coverage)) {
      Arg->claim();
      CoverageFeatures &= ~parseCoverageFeatures(D, Arg);
    }
  }
  // Choose at most one coverage type: function, bb, or edge.
  if ((CoverageFeatures & CoverageFunc) && (CoverageFeatures & CoverageBB))
    D.Diag(clang::diag::err_drv_argument_not_allowed_with)
        << "-fsanitize-coverage=func"
        << "-fsanitize-coverage=bb";
  if ((CoverageFeatures & CoverageFunc) && (CoverageFeatures & CoverageEdge))
    D.Diag(clang::diag::err_drv_argument_not_allowed_with)
        << "-fsanitize-coverage=func"
        << "-fsanitize-coverage=edge";
  if ((CoverageFeatures & CoverageBB) && (CoverageFeatures & CoverageEdge))
    D.Diag(clang::diag::err_drv_argument_not_allowed_with)
        << "-fsanitize-coverage=bb"
        << "-fsanitize-coverage=edge";
  // Basic block tracing and 8-bit counters require some type of coverage
  // enabled.
  if (CoverageFeatures & CoverageTraceBB)
    D.Diag(clang::diag::warn_drv_deprecated_arg)
        << "-fsanitize-coverage=trace-bb"
        << "-fsanitize-coverage=trace-pc-guard";
  if (CoverageFeatures & Coverage8bitCounters)
    D.Diag(clang::diag::warn_drv_deprecated_arg)
        << "-fsanitize-coverage=8bit-counters"
        << "-fsanitize-coverage=trace-pc-guard";

  int InsertionPointTypes = CoverageFunc | CoverageBB | CoverageEdge;
  int InstrumentationTypes =
      CoverageTracePC | CoverageTracePCGuard | CoverageInline8bitCounters;
  if ((CoverageFeatures & InsertionPointTypes) &&
      !(CoverageFeatures & InstrumentationTypes)) {
    D.Diag(clang::diag::warn_drv_deprecated_arg)
        << "-fsanitize-coverage=[func|bb|edge]"
        << "-fsanitize-coverage=[func|bb|edge],[trace-pc-guard|trace-pc]";
  }

  // trace-pc w/o func/bb/edge implies edge.
  if (!(CoverageFeatures & InsertionPointTypes)) {
    if (CoverageFeatures &
        (CoverageTracePC | CoverageTracePCGuard | CoverageInline8bitCounters))
      CoverageFeatures |= CoverageEdge;

    if (CoverageFeatures & CoverageStackDepth)
      CoverageFeatures |= CoverageFunc;
  }

  SharedRuntime =
      Args.hasFlag(options::OPT_shared_libsan, options::OPT_static_libsan,
                   TC.getTriple().isAndroid() || TC.getTriple().isOSFuchsia() ||
                       TC.getTriple().isOSDarwin());

  ImplicitCfiRuntime = TC.getTriple().isAndroid();

  if (AllAddedKinds & Address) {
    NeedPIE |= TC.getTriple().isOSFuchsia();
    if (Arg *A =
            Args.getLastArg(options::OPT_fsanitize_address_field_padding)) {
        StringRef S = A->getValue();
        // Legal values are 0 and 1, 2, but in future we may add more levels.
        if (S.getAsInteger(0, AsanFieldPadding) || AsanFieldPadding < 0 ||
            AsanFieldPadding > 2) {
          D.Diag(clang::diag::err_drv_invalid_value) << A->getAsString(Args) << S;
        }
    }

    if (Arg *WindowsDebugRTArg =
            Args.getLastArg(options::OPT__SLASH_MTd, options::OPT__SLASH_MT,
                            options::OPT__SLASH_MDd, options::OPT__SLASH_MD,
                            options::OPT__SLASH_LDd, options::OPT__SLASH_LD)) {
      switch (WindowsDebugRTArg->getOption().getID()) {
      case options::OPT__SLASH_MTd:
      case options::OPT__SLASH_MDd:
      case options::OPT__SLASH_LDd:
        D.Diag(clang::diag::err_drv_argument_not_allowed_with)
            << WindowsDebugRTArg->getAsString(Args)
            << lastArgumentForMask(D, Args, Address);
        D.Diag(clang::diag::note_drv_address_sanitizer_debug_runtime);
      }
    }

    AsanUseAfterScope = Args.hasFlag(
        options::OPT_fsanitize_address_use_after_scope,
        options::OPT_fno_sanitize_address_use_after_scope, AsanUseAfterScope);

    AsanPoisonCustomArrayCookie = Args.hasFlag(
        options::OPT_fsanitize_address_poison_custom_array_cookie,
        options::OPT_fno_sanitize_address_poison_custom_array_cookie,
        AsanPoisonCustomArrayCookie);

    // As a workaround for a bug in gold 2.26 and earlier, dead stripping of
    // globals in ASan is disabled by default on ELF targets.
    // See https://sourceware.org/bugzilla/show_bug.cgi?id=19002
    AsanGlobalsDeadStripping =
        !TC.getTriple().isOSBinFormatELF() || TC.getTriple().isOSFuchsia() ||
        Args.hasArg(options::OPT_fsanitize_address_globals_dead_stripping);

    AsanUseOdrIndicator =
        Args.hasFlag(options::OPT_fsanitize_address_use_odr_indicator,
                     options::OPT_fno_sanitize_address_use_odr_indicator,
                     AsanUseOdrIndicator);
  } else {
    AsanUseAfterScope = false;
  }

  if (AllAddedKinds & HWAddress) {
    if (Arg *HwasanAbiArg =
            Args.getLastArg(options::OPT_fsanitize_hwaddress_abi_EQ)) {
      HwasanAbi = HwasanAbiArg->getValue();
      if (HwasanAbi != "platform" && HwasanAbi != "interceptor")
        D.Diag(clang::diag::err_drv_invalid_value)
            << HwasanAbiArg->getAsString(Args) << HwasanAbi;
    } else {
      HwasanAbi = "interceptor";
    }
  }

  if (AllAddedKinds & SafeStack) {
    // SafeStack runtime is built into the system on Fuchsia.
    SafeStackRuntime = !TC.getTriple().isOSFuchsia();
  }

  // Parse -link-cxx-sanitizer flag.
  LinkCXXRuntimes =
      Args.hasArg(options::OPT_fsanitize_link_cxx_runtime) || D.CCCIsCXX();

  // Finally, initialize the set of available and recoverable sanitizers.
  Sanitizers.Mask |= Kinds;
  RecoverableSanitizers.Mask |= RecoverableKinds;
  TrapSanitizers.Mask |= TrappingKinds;
  assert(!(RecoverableKinds & TrappingKinds) &&
         "Overlap between recoverable and trapping sanitizers");
}

static std::string toString(const clang::SanitizerSet &Sanitizers) {
  std::string Res;
#define SANITIZER(NAME, ID)                                                    \
  if (Sanitizers.has(ID)) {                                                    \
    if (!Res.empty())                                                          \
      Res += ",";                                                              \
    Res += NAME;                                                               \
  }
#include "clang/Basic/Sanitizers.def"
  return Res;
}

static void addIncludeLinkerOption(const ToolChain &TC,
                                   const llvm::opt::ArgList &Args,
                                   llvm::opt::ArgStringList &CmdArgs,
                                   StringRef SymbolName) {
  SmallString<64> LinkerOptionFlag;
  LinkerOptionFlag = "--linker-option=/include:";
  if (TC.getTriple().getArch() == llvm::Triple::x86) {
    // Win32 mangles C function names with a '_' prefix.
    LinkerOptionFlag += '_';
  }
  LinkerOptionFlag += SymbolName;
  CmdArgs.push_back(Args.MakeArgString(LinkerOptionFlag));
}

void SanitizerArgs::addArgs(const ToolChain &TC, const llvm::opt::ArgList &Args,
                            llvm::opt::ArgStringList &CmdArgs,
                            types::ID InputType) const {
  // NVPTX doesn't currently support sanitizers.  Bailing out here means that
  // e.g. -fsanitize=address applies only to host code, which is what we want
  // for now.
  if (TC.getTriple().isNVPTX())
    return;

  // Translate available CoverageFeatures to corresponding clang-cc1 flags.
  // Do it even if Sanitizers.empty() since some forms of coverage don't require
  // sanitizers.
  std::pair<int, const char *> CoverageFlags[] = {
    std::make_pair(CoverageFunc, "-fsanitize-coverage-type=1"),
    std::make_pair(CoverageBB, "-fsanitize-coverage-type=2"),
    std::make_pair(CoverageEdge, "-fsanitize-coverage-type=3"),
    std::make_pair(CoverageIndirCall, "-fsanitize-coverage-indirect-calls"),
    std::make_pair(CoverageTraceBB, "-fsanitize-coverage-trace-bb"),
    std::make_pair(CoverageTraceCmp, "-fsanitize-coverage-trace-cmp"),
    std::make_pair(CoverageTraceDiv, "-fsanitize-coverage-trace-div"),
    std::make_pair(CoverageTraceGep, "-fsanitize-coverage-trace-gep"),
    std::make_pair(Coverage8bitCounters, "-fsanitize-coverage-8bit-counters"),
    std::make_pair(CoverageTracePC, "-fsanitize-coverage-trace-pc"),
    std::make_pair(CoverageTracePCGuard, "-fsanitize-coverage-trace-pc-guard"),
    std::make_pair(CoverageInline8bitCounters, "-fsanitize-coverage-inline-8bit-counters"),
    std::make_pair(CoveragePCTable, "-fsanitize-coverage-pc-table"),
    std::make_pair(CoverageNoPrune, "-fsanitize-coverage-no-prune"),
    std::make_pair(CoverageStackDepth, "-fsanitize-coverage-stack-depth")};
  for (auto F : CoverageFlags) {
    if (CoverageFeatures & F.first)
      CmdArgs.push_back(F.second);
  }

  if (TC.getTriple().isOSWindows() && needsUbsanRt()) {
    // Instruct the code generator to embed linker directives in the object file
    // that cause the required runtime libraries to be linked.
    CmdArgs.push_back(Args.MakeArgString(
        "--dependent-lib=" + TC.getCompilerRT(Args, "ubsan_standalone")));
    if (types::isCXX(InputType))
      CmdArgs.push_back(Args.MakeArgString(
          "--dependent-lib=" + TC.getCompilerRT(Args, "ubsan_standalone_cxx")));
  }
  if (TC.getTriple().isOSWindows() && needsStatsRt()) {
    CmdArgs.push_back(Args.MakeArgString("--dependent-lib=" +
                                         TC.getCompilerRT(Args, "stats_client")));

    // The main executable must export the stats runtime.
    // FIXME: Only exporting from the main executable (e.g. based on whether the
    // translation unit defines main()) would save a little space, but having
    // multiple copies of the runtime shouldn't hurt.
    CmdArgs.push_back(Args.MakeArgString("--dependent-lib=" +
                                         TC.getCompilerRT(Args, "stats")));
    addIncludeLinkerOption(TC, Args, CmdArgs, "__sanitizer_stats_register");
  }

  if (Sanitizers.empty())
    return;
  CmdArgs.push_back(Args.MakeArgString("-fsanitize=" + toString(Sanitizers)));

  if (!RecoverableSanitizers.empty())
    CmdArgs.push_back(Args.MakeArgString("-fsanitize-recover=" +
                                         toString(RecoverableSanitizers)));

  if (!TrapSanitizers.empty())
    CmdArgs.push_back(
        Args.MakeArgString("-fsanitize-trap=" + toString(TrapSanitizers)));

  for (const auto &BLPath : BlacklistFiles) {
    SmallString<64> BlacklistOpt("-fsanitize-blacklist=");
    BlacklistOpt += BLPath;
    CmdArgs.push_back(Args.MakeArgString(BlacklistOpt));
  }
  for (const auto &Dep : ExtraDeps) {
    SmallString<64> ExtraDepOpt("-fdepfile-entry=");
    ExtraDepOpt += Dep;
    CmdArgs.push_back(Args.MakeArgString(ExtraDepOpt));
  }

  if (MsanTrackOrigins)
    CmdArgs.push_back(Args.MakeArgString("-fsanitize-memory-track-origins=" +
                                         Twine(MsanTrackOrigins)));

  if (MsanUseAfterDtor)
    CmdArgs.push_back("-fsanitize-memory-use-after-dtor");

  // FIXME: Pass these parameters as function attributes, not as -llvm flags.
  if (!TsanMemoryAccess) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-tsan-instrument-memory-accesses=0");
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-tsan-instrument-memintrinsics=0");
  }
  if (!TsanFuncEntryExit) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-tsan-instrument-func-entry-exit=0");
  }
  if (!TsanAtomics) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-tsan-instrument-atomics=0");
  }

  if (CfiCrossDso)
    CmdArgs.push_back("-fsanitize-cfi-cross-dso");

  if (CfiICallGeneralizePointers)
    CmdArgs.push_back("-fsanitize-cfi-icall-generalize-pointers");

  if (Stats)
    CmdArgs.push_back("-fsanitize-stats");

  if (MinimalRuntime)
    CmdArgs.push_back("-fsanitize-minimal-runtime");

  if (AsanFieldPadding)
    CmdArgs.push_back(Args.MakeArgString("-fsanitize-address-field-padding=" +
                                         Twine(AsanFieldPadding)));

  if (AsanUseAfterScope)
    CmdArgs.push_back("-fsanitize-address-use-after-scope");

  if (AsanPoisonCustomArrayCookie)
    CmdArgs.push_back("-fsanitize-address-poison-custom-array-cookie");

  if (AsanGlobalsDeadStripping)
    CmdArgs.push_back("-fsanitize-address-globals-dead-stripping");

  if (AsanUseOdrIndicator)
    CmdArgs.push_back("-fsanitize-address-use-odr-indicator");

  if (!HwasanAbi.empty()) {
    CmdArgs.push_back("-default-function-attr");
    CmdArgs.push_back(Args.MakeArgString("hwasan-abi=" + HwasanAbi));
  }

  // MSan: Workaround for PR16386.
  // ASan: This is mainly to help LSan with cases such as
  // https://github.com/google/sanitizers/issues/373
  // We can't make this conditional on -fsanitize=leak, as that flag shouldn't
  // affect compilation.
  if (Sanitizers.has(Memory) || Sanitizers.has(Address))
    CmdArgs.push_back("-fno-assume-sane-operator-new");

  // Require -fvisibility= flag on non-Windows when compiling if vptr CFI is
  // enabled.
  if (Sanitizers.hasOneOf(CFIClasses) && !TC.getTriple().isOSWindows() &&
      !Args.hasArg(options::OPT_fvisibility_EQ)) {
    TC.getDriver().Diag(clang::diag::err_drv_argument_only_allowed_with)
        << lastArgumentForMask(TC.getDriver(), Args,
                               Sanitizers.Mask & CFIClasses)
        << "-fvisibility=";
  }
}

SanitizerMask parseArgValues(const Driver &D, const llvm::opt::Arg *A,
                             bool DiagnoseErrors) {
  assert((A->getOption().matches(options::OPT_fsanitize_EQ) ||
          A->getOption().matches(options::OPT_fno_sanitize_EQ) ||
          A->getOption().matches(options::OPT_fsanitize_recover_EQ) ||
          A->getOption().matches(options::OPT_fno_sanitize_recover_EQ) ||
          A->getOption().matches(options::OPT_fsanitize_trap_EQ) ||
          A->getOption().matches(options::OPT_fno_sanitize_trap_EQ)) &&
         "Invalid argument in parseArgValues!");
  SanitizerMask Kinds = 0;
  for (int i = 0, n = A->getNumValues(); i != n; ++i) {
    const char *Value = A->getValue(i);
    SanitizerMask Kind;
    // Special case: don't accept -fsanitize=all.
    if (A->getOption().matches(options::OPT_fsanitize_EQ) &&
        0 == strcmp("all", Value))
      Kind = 0;
    // Similarly, don't accept -fsanitize=efficiency-all.
    else if (A->getOption().matches(options::OPT_fsanitize_EQ) &&
        0 == strcmp("efficiency-all", Value))
      Kind = 0;
    else
      Kind = parseSanitizerValue(Value, /*AllowGroups=*/true);

    if (Kind)
      Kinds |= Kind;
    else if (DiagnoseErrors)
      D.Diag(clang::diag::err_drv_unsupported_option_argument)
          << A->getOption().getName() << Value;
  }
  return Kinds;
}

int parseCoverageFeatures(const Driver &D, const llvm::opt::Arg *A) {
  assert(A->getOption().matches(options::OPT_fsanitize_coverage) ||
         A->getOption().matches(options::OPT_fno_sanitize_coverage));
  int Features = 0;
  for (int i = 0, n = A->getNumValues(); i != n; ++i) {
    const char *Value = A->getValue(i);
    int F = llvm::StringSwitch<int>(Value)
        .Case("func", CoverageFunc)
        .Case("bb", CoverageBB)
        .Case("edge", CoverageEdge)
        .Case("indirect-calls", CoverageIndirCall)
        .Case("trace-bb", CoverageTraceBB)
        .Case("trace-cmp", CoverageTraceCmp)
        .Case("trace-div", CoverageTraceDiv)
        .Case("trace-gep", CoverageTraceGep)
        .Case("8bit-counters", Coverage8bitCounters)
        .Case("trace-pc", CoverageTracePC)
        .Case("trace-pc-guard", CoverageTracePCGuard)
        .Case("no-prune", CoverageNoPrune)
        .Case("inline-8bit-counters", CoverageInline8bitCounters)
        .Case("pc-table", CoveragePCTable)
        .Case("stack-depth", CoverageStackDepth)
        .Default(0);
    if (F == 0)
      D.Diag(clang::diag::err_drv_unsupported_option_argument)
          << A->getOption().getName() << Value;
    Features |= F;
  }
  return Features;
}

std::string lastArgumentForMask(const Driver &D, const llvm::opt::ArgList &Args,
                                SanitizerMask Mask) {
  for (llvm::opt::ArgList::const_reverse_iterator I = Args.rbegin(),
                                                  E = Args.rend();
       I != E; ++I) {
    const auto *Arg = *I;
    if (Arg->getOption().matches(options::OPT_fsanitize_EQ)) {
      SanitizerMask AddKinds =
          expandSanitizerGroups(parseArgValues(D, Arg, false));
      if (AddKinds & Mask)
        return describeSanitizeArg(Arg, Mask);
    } else if (Arg->getOption().matches(options::OPT_fno_sanitize_EQ)) {
      SanitizerMask RemoveKinds =
          expandSanitizerGroups(parseArgValues(D, Arg, false));
      Mask &= ~RemoveKinds;
    }
  }
  llvm_unreachable("arg list didn't provide expected value");
}

std::string describeSanitizeArg(const llvm::opt::Arg *A, SanitizerMask Mask) {
  assert(A->getOption().matches(options::OPT_fsanitize_EQ)
         && "Invalid argument in describeSanitizerArg!");

  std::string Sanitizers;
  for (int i = 0, n = A->getNumValues(); i != n; ++i) {
    if (expandSanitizerGroups(
            parseSanitizerValue(A->getValue(i), /*AllowGroups=*/true)) &
        Mask) {
      if (!Sanitizers.empty())
        Sanitizers += ",";
      Sanitizers += A->getValue(i);
    }
  }

  assert(!Sanitizers.empty() && "arg didn't provide expected value");
  return "-fsanitize=" + Sanitizers;
}
