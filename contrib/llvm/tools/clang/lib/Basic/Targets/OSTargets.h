//===--- OSTargets.h - Declare OS target feature support --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares OS specific TargetInfo types.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_OSTARGETS_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_OSTARGETS_H

#include "Targets.h"
#include "llvm/MC/MCSectionMachO.h"

namespace clang {
namespace targets {

template <typename TgtInfo>
class LLVM_LIBRARY_VISIBILITY OSTargetInfo : public TgtInfo {
protected:
  virtual void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                            MacroBuilder &Builder) const = 0;

public:
  OSTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : TgtInfo(Triple, Opts) {}

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    TgtInfo::getTargetDefines(Opts, Builder);
    getOSDefines(Opts, TgtInfo::getTriple(), Builder);
  }
};

// CloudABI Target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY CloudABITargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    Builder.defineMacro("__CloudABI__");
    Builder.defineMacro("__ELF__");

    // CloudABI uses ISO/IEC 10646:2012 for wchar_t, char16_t and char32_t.
    Builder.defineMacro("__STDC_ISO_10646__", "201206L");
    Builder.defineMacro("__STDC_UTF_16__");
    Builder.defineMacro("__STDC_UTF_32__");
  }

public:
  CloudABITargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {}
};

// Ananas target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY AnanasTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // Ananas defines
    Builder.defineMacro("__Ananas__");
    Builder.defineMacro("__ELF__");
  }

public:
  AnanasTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {}
};

void getDarwinDefines(MacroBuilder &Builder, const LangOptions &Opts,
                      const llvm::Triple &Triple, StringRef &PlatformName,
                      VersionTuple &PlatformMinVersion);

template <typename Target>
class LLVM_LIBRARY_VISIBILITY DarwinTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    getDarwinDefines(Builder, Opts, Triple, this->PlatformName,
                     this->PlatformMinVersion);
  }

public:
  DarwinTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    // By default, no TLS, and we whitelist permitted architecture/OS
    // combinations.
    this->TLSSupported = false;

    if (Triple.isMacOSX())
      this->TLSSupported = !Triple.isMacOSXVersionLT(10, 7);
    else if (Triple.isiOS()) {
      // 64-bit iOS supported it from 8 onwards, 32-bit device from 9 onwards,
      // 32-bit simulator from 10 onwards.
      if (Triple.isArch64Bit())
        this->TLSSupported = !Triple.isOSVersionLT(8);
      else if (Triple.isArch32Bit()) {
        if (!Triple.isSimulatorEnvironment())
          this->TLSSupported = !Triple.isOSVersionLT(9);
        else
          this->TLSSupported = !Triple.isOSVersionLT(10);
      }
    } else if (Triple.isWatchOS()) {
      if (!Triple.isSimulatorEnvironment())
        this->TLSSupported = !Triple.isOSVersionLT(2);
      else
        this->TLSSupported = !Triple.isOSVersionLT(3);
    }

    this->MCountName = "\01mcount";
  }

  std::string isValidSectionSpecifier(StringRef SR) const override {
    // Let MCSectionMachO validate this.
    StringRef Segment, Section;
    unsigned TAA, StubSize;
    bool HasTAA;
    return llvm::MCSectionMachO::ParseSectionSpecifier(SR, Segment, Section,
                                                       TAA, HasTAA, StubSize);
  }

  const char *getStaticInitSectionSpecifier() const override {
    // FIXME: We should return 0 when building kexts.
    return "__TEXT,__StaticInit,regular,pure_instructions";
  }

  /// Darwin does not support protected visibility.  Darwin's "default"
  /// is very similar to ELF's "protected";  Darwin requires a "weak"
  /// attribute on declarations that can be dynamically replaced.
  bool hasProtectedVisibility() const override { return false; }

  TargetInfo::IntType getLeastIntTypeByWidth(unsigned BitWidth,
                                             bool IsSigned) const final {
    // Darwin uses `long long` for `int_least64_t` and `int_fast64_t`.
    return BitWidth == 64
               ? (IsSigned ? TargetInfo::SignedLongLong
                           : TargetInfo::UnsignedLongLong)
               : TargetInfo::getLeastIntTypeByWidth(BitWidth, IsSigned);
  }
};

// DragonFlyBSD Target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY DragonFlyBSDTargetInfo
    : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // DragonFly defines; list based off of gcc output
    Builder.defineMacro("__DragonFly__");
    Builder.defineMacro("__DragonFly_cc_version", "100001");
    Builder.defineMacro("__ELF__");
    Builder.defineMacro("__KPRINTF_ATTRIBUTE__");
    Builder.defineMacro("__tune_i386__");
    DefineStd(Builder, "unix", Opts);
  }

public:
  DragonFlyBSDTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    switch (Triple.getArch()) {
    default:
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      this->MCountName = ".mcount";
      break;
    }
  }
};

#ifndef FREEBSD_CC_VERSION
#define FREEBSD_CC_VERSION 0U
#endif

// FreeBSD Target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY FreeBSDTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // FreeBSD defines; list based off of gcc output

    unsigned Release = Triple.getOSMajorVersion();
    if (Release == 0U)
      Release = 8U;
    unsigned CCVersion = FREEBSD_CC_VERSION;
    if (CCVersion == 0U)
      CCVersion = Release * 100000U + 1U;

    Builder.defineMacro("__FreeBSD__", Twine(Release));
    Builder.defineMacro("__FreeBSD_cc_version", Twine(CCVersion));
    Builder.defineMacro("__KPRINTF_ATTRIBUTE__");
    DefineStd(Builder, "unix", Opts);
    Builder.defineMacro("__ELF__");

    // On FreeBSD, wchar_t contains the number of the code point as
    // used by the character set of the locale. These character sets are
    // not necessarily a superset of ASCII.
    //
    // FIXME: This is wrong; the macro refers to the numerical values
    // of wchar_t *literals*, which are not locale-dependent. However,
    // FreeBSD systems apparently depend on us getting this wrong, and
    // setting this to 1 is conforming even if all the basic source
    // character literals have the same encoding as char and wchar_t.
    Builder.defineMacro("__STDC_MB_MIGHT_NEQ_WC__", "1");
  }

public:
  FreeBSDTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    switch (Triple.getArch()) {
    default:
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      this->MCountName = ".mcount";
      break;
    case llvm::Triple::mips:
    case llvm::Triple::mipsel:
    case llvm::Triple::ppc:
    case llvm::Triple::ppc64:
    case llvm::Triple::ppc64le:
      this->MCountName = "_mcount";
      break;
    case llvm::Triple::arm:
      this->MCountName = "__mcount";
      break;
    }
  }
};

// GNU/kFreeBSD Target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY KFreeBSDTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // GNU/kFreeBSD defines; list based off of gcc output

    DefineStd(Builder, "unix", Opts);
    Builder.defineMacro("__FreeBSD_kernel__");
    Builder.defineMacro("__GLIBC__");
    Builder.defineMacro("__ELF__");
    if (Opts.POSIXThreads)
      Builder.defineMacro("_REENTRANT");
    if (Opts.CPlusPlus)
      Builder.defineMacro("_GNU_SOURCE");
  }

public:
  KFreeBSDTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {}
};

// Haiku Target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY HaikuTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // Haiku defines; list based off of gcc output
    Builder.defineMacro("__HAIKU__");
    Builder.defineMacro("__ELF__");
    DefineStd(Builder, "unix", Opts);
    if (this->HasFloat128) 
      Builder.defineMacro("__FLOAT128__");
  }

public:
  HaikuTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    this->SizeType = TargetInfo::UnsignedLong;
    this->IntPtrType = TargetInfo::SignedLong;
    this->PtrDiffType = TargetInfo::SignedLong;
    this->ProcessIDType = TargetInfo::SignedLong;
    this->TLSSupported = false;
    switch (Triple.getArch()) {
    default:
      break;
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      this->HasFloat128 = true;
      break;
    }
  }
};

// Hurd target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY HurdTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // Hurd defines; list based off of gcc output.
    DefineStd(Builder, "unix", Opts);
    Builder.defineMacro("__GNU__");
    Builder.defineMacro("__gnu_hurd__");
    Builder.defineMacro("__MACH__");
    Builder.defineMacro("__GLIBC__");
    Builder.defineMacro("__ELF__");
    if (Opts.POSIXThreads)
      Builder.defineMacro("_REENTRANT");
    if (Opts.CPlusPlus)
      Builder.defineMacro("_GNU_SOURCE");
  }
public:
  HurdTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {}
};

// Minix Target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY MinixTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // Minix defines

    Builder.defineMacro("__minix", "3");
    Builder.defineMacro("_EM_WSIZE", "4");
    Builder.defineMacro("_EM_PSIZE", "4");
    Builder.defineMacro("_EM_SSIZE", "2");
    Builder.defineMacro("_EM_LSIZE", "4");
    Builder.defineMacro("_EM_FSIZE", "4");
    Builder.defineMacro("_EM_DSIZE", "8");
    Builder.defineMacro("__ELF__");
    DefineStd(Builder, "unix", Opts);
  }

public:
  MinixTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {}
};

// Linux target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY LinuxTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // Linux defines; list based off of gcc output
    DefineStd(Builder, "unix", Opts);
    DefineStd(Builder, "linux", Opts);
    Builder.defineMacro("__ELF__");
    if (Triple.isAndroid()) {
      Builder.defineMacro("__ANDROID__", "1");
      unsigned Maj, Min, Rev;
      Triple.getEnvironmentVersion(Maj, Min, Rev);
      this->PlatformName = "android";
      this->PlatformMinVersion = VersionTuple(Maj, Min, Rev);
      if (Maj)
        Builder.defineMacro("__ANDROID_API__", Twine(Maj));
    } else {
        Builder.defineMacro("__gnu_linux__");
    }
    if (Opts.POSIXThreads)
      Builder.defineMacro("_REENTRANT");
    if (Opts.CPlusPlus)
      Builder.defineMacro("_GNU_SOURCE");
    if (this->HasFloat128)
      Builder.defineMacro("__FLOAT128__");
  }

public:
  LinuxTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    this->WIntType = TargetInfo::UnsignedInt;

    switch (Triple.getArch()) {
    default:
      break;
    case llvm::Triple::mips:
    case llvm::Triple::mipsel:
    case llvm::Triple::mips64:
    case llvm::Triple::mips64el:
    case llvm::Triple::ppc:
    case llvm::Triple::ppc64:
    case llvm::Triple::ppc64le:
      this->MCountName = "_mcount";
      break;
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      this->HasFloat128 = true;
      break;
    }
  }

  const char *getStaticInitSectionSpecifier() const override {
    return ".text.startup";
  }
};

// NetBSD Target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY NetBSDTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // NetBSD defines; list based off of gcc output
    Builder.defineMacro("__NetBSD__");
    Builder.defineMacro("__unix__");
    Builder.defineMacro("__ELF__");
    if (Opts.POSIXThreads)
      Builder.defineMacro("_REENTRANT");
  }

public:
  NetBSDTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    this->MCountName = "__mcount";
  }
};

// OpenBSD Target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY OpenBSDTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // OpenBSD defines; list based off of gcc output

    Builder.defineMacro("__OpenBSD__");
    DefineStd(Builder, "unix", Opts);
    Builder.defineMacro("__ELF__");
    if (Opts.POSIXThreads)
      Builder.defineMacro("_REENTRANT");
    if (this->HasFloat128)
      Builder.defineMacro("__FLOAT128__");
  }

public:
  OpenBSDTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    switch (Triple.getArch()) {
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      this->HasFloat128 = true;
      LLVM_FALLTHROUGH;
    default:
      this->MCountName = "__mcount";
      break;
    case llvm::Triple::mips64:
    case llvm::Triple::mips64el:
    case llvm::Triple::ppc:
    case llvm::Triple::sparcv9:
      this->MCountName = "_mcount";
      break;
    }
  }
};

// PSP Target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY PSPTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // PSP defines; list based on the output of the pspdev gcc toolchain.
    Builder.defineMacro("PSP");
    Builder.defineMacro("_PSP");
    Builder.defineMacro("__psp__");
    Builder.defineMacro("__ELF__");
  }

public:
  PSPTargetInfo(const llvm::Triple &Triple) : OSTargetInfo<Target>(Triple) {}
};

// PS3 PPU Target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY PS3PPUTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // PS3 PPU defines.
    Builder.defineMacro("__PPC__");
    Builder.defineMacro("__PPU__");
    Builder.defineMacro("__CELLOS_LV2__");
    Builder.defineMacro("__ELF__");
    Builder.defineMacro("__LP32__");
    Builder.defineMacro("_ARCH_PPC64");
    Builder.defineMacro("__powerpc64__");
  }

public:
  PS3PPUTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    this->LongWidth = this->LongAlign = 32;
    this->PointerWidth = this->PointerAlign = 32;
    this->IntMaxType = TargetInfo::SignedLongLong;
    this->Int64Type = TargetInfo::SignedLongLong;
    this->SizeType = TargetInfo::UnsignedInt;
    this->resetDataLayout("E-m:e-p:32:32-i64:64-n32:64");
  }
};

template <typename Target>
class LLVM_LIBRARY_VISIBILITY PS4OSTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    Builder.defineMacro("__FreeBSD__", "9");
    Builder.defineMacro("__FreeBSD_cc_version", "900001");
    Builder.defineMacro("__KPRINTF_ATTRIBUTE__");
    DefineStd(Builder, "unix", Opts);
    Builder.defineMacro("__ELF__");
    Builder.defineMacro("__ORBIS__");
  }

public:
  PS4OSTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    this->WCharType = TargetInfo::UnsignedShort;

    // On PS4, TLS variable cannot be aligned to more than 32 bytes (256 bits).
    this->MaxTLSAlign = 256;

    // On PS4, do not honor explicit bit field alignment,
    // as in "__attribute__((aligned(2))) int b : 1;".
    this->UseExplicitBitFieldAlignment = false;

    switch (Triple.getArch()) {
    default:
    case llvm::Triple::x86_64:
      this->MCountName = ".mcount";
      this->NewAlign = 256;
      break;
    }
  }
};

// RTEMS Target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY RTEMSTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    // RTEMS defines; list based off of gcc output

    Builder.defineMacro("__rtems__");
    Builder.defineMacro("__ELF__");
    if (Opts.CPlusPlus)
      Builder.defineMacro("_GNU_SOURCE");
  }

public:
  RTEMSTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    switch (Triple.getArch()) {
    default:
    case llvm::Triple::x86:
      // this->MCountName = ".mcount";
      break;
    case llvm::Triple::mips:
    case llvm::Triple::mipsel:
    case llvm::Triple::ppc:
    case llvm::Triple::ppc64:
    case llvm::Triple::ppc64le:
      // this->MCountName = "_mcount";
      break;
    case llvm::Triple::arm:
      // this->MCountName = "__mcount";
      break;
    }
  }
};

// Solaris target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY SolarisTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    DefineStd(Builder, "sun", Opts);
    DefineStd(Builder, "unix", Opts);
    Builder.defineMacro("__ELF__");
    Builder.defineMacro("__svr4__");
    Builder.defineMacro("__SVR4");
    // Solaris headers require _XOPEN_SOURCE to be set to 600 for C99 and
    // newer, but to 500 for everything else.  feature_test.h has a check to
    // ensure that you are not using C99 with an old version of X/Open or C89
    // with a new version.
    if (Opts.C99)
      Builder.defineMacro("_XOPEN_SOURCE", "600");
    else
      Builder.defineMacro("_XOPEN_SOURCE", "500");
    if (Opts.CPlusPlus)
      Builder.defineMacro("__C99FEATURES__");
    Builder.defineMacro("_LARGEFILE_SOURCE");
    Builder.defineMacro("_LARGEFILE64_SOURCE");
    Builder.defineMacro("__EXTENSIONS__");
    if (Opts.POSIXThreads)
      Builder.defineMacro("_REENTRANT");
    if (this->HasFloat128)
      Builder.defineMacro("__FLOAT128__");
  }

public:
  SolarisTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    // FIXME: WIntType should be SignedLong
    switch (Triple.getArch()) {
    default:
      break;
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      this->HasFloat128 = true;
      break;
    }
  }
};

// Windows target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY WindowsTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    Builder.defineMacro("_WIN32");
    if (Triple.isArch64Bit())
      Builder.defineMacro("_WIN64");
    if (Triple.isWindowsGNUEnvironment())
      addMinGWDefines(Triple, Opts, Builder);

  }
  void getVisualStudioDefines(const LangOptions &Opts,
                              MacroBuilder &Builder) const {
    if (Opts.CPlusPlus) {
      if (Opts.RTTIData)
        Builder.defineMacro("_CPPRTTI");

      if (Opts.CXXExceptions)
        Builder.defineMacro("_CPPUNWIND");
    }

    if (Opts.Bool)
      Builder.defineMacro("__BOOL_DEFINED");

    if (!Opts.CharIsSigned)
      Builder.defineMacro("_CHAR_UNSIGNED");

    // FIXME: POSIXThreads isn't exactly the option this should be defined for,
    //        but it works for now.
    if (Opts.POSIXThreads)
      Builder.defineMacro("_MT");

    if (Opts.MSCompatibilityVersion) {
      Builder.defineMacro("_MSC_VER",
                          Twine(Opts.MSCompatibilityVersion / 100000));
      Builder.defineMacro("_MSC_FULL_VER", Twine(Opts.MSCompatibilityVersion));
      // FIXME We cannot encode the revision information into 32-bits
      Builder.defineMacro("_MSC_BUILD", Twine(1));

      if (Opts.CPlusPlus11 && Opts.isCompatibleWithMSVC(LangOptions::MSVC2015))
        Builder.defineMacro("_HAS_CHAR16_T_LANGUAGE_SUPPORT", Twine(1));

      if (Opts.isCompatibleWithMSVC(LangOptions::MSVC2015)) {
        if (Opts.CPlusPlus2a)
          Builder.defineMacro("_MSVC_LANG", "201704L");
        else if (Opts.CPlusPlus17)
          Builder.defineMacro("_MSVC_LANG", "201703L");
        else if (Opts.CPlusPlus14)
          Builder.defineMacro("_MSVC_LANG", "201402L");
      }
    }

    if (Opts.MicrosoftExt) {
      Builder.defineMacro("_MSC_EXTENSIONS");

      if (Opts.CPlusPlus11) {
        Builder.defineMacro("_RVALUE_REFERENCES_V2_SUPPORTED");
        Builder.defineMacro("_RVALUE_REFERENCES_SUPPORTED");
        Builder.defineMacro("_NATIVE_NULLPTR_SUPPORTED");
      }
    }

    Builder.defineMacro("_INTEGRAL_MAX_BITS", "64");
  }

public:
  WindowsTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    this->WCharType = TargetInfo::UnsignedShort;
    this->WIntType = TargetInfo::UnsignedShort;
  }
};

template <typename Target>
class LLVM_LIBRARY_VISIBILITY NaClTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    if (Opts.POSIXThreads)
      Builder.defineMacro("_REENTRANT");
    if (Opts.CPlusPlus)
      Builder.defineMacro("_GNU_SOURCE");

    DefineStd(Builder, "unix", Opts);
    Builder.defineMacro("__ELF__");
    Builder.defineMacro("__native_client__");
  }

public:
  NaClTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    this->LongAlign = 32;
    this->LongWidth = 32;
    this->PointerAlign = 32;
    this->PointerWidth = 32;
    this->IntMaxType = TargetInfo::SignedLongLong;
    this->Int64Type = TargetInfo::SignedLongLong;
    this->DoubleAlign = 64;
    this->LongDoubleWidth = 64;
    this->LongDoubleAlign = 64;
    this->LongLongWidth = 64;
    this->LongLongAlign = 64;
    this->SizeType = TargetInfo::UnsignedInt;
    this->PtrDiffType = TargetInfo::SignedInt;
    this->IntPtrType = TargetInfo::SignedInt;
    // RegParmMax is inherited from the underlying architecture.
    this->LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    if (Triple.getArch() == llvm::Triple::arm) {
      // Handled in ARM's setABI().
    } else if (Triple.getArch() == llvm::Triple::x86) {
      this->resetDataLayout("e-m:e-p:32:32-i64:64-n8:16:32-S128");
    } else if (Triple.getArch() == llvm::Triple::x86_64) {
      this->resetDataLayout("e-m:e-p:32:32-i64:64-n8:16:32:64-S128");
    } else if (Triple.getArch() == llvm::Triple::mipsel) {
      // Handled on mips' setDataLayout.
    } else {
      assert(Triple.getArch() == llvm::Triple::le32);
      this->resetDataLayout("e-p:32:32-i64:64");
    }
  }
};

// Fuchsia Target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY FuchsiaTargetInfo : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override {
    Builder.defineMacro("__Fuchsia__");
    Builder.defineMacro("__ELF__");
    if (Opts.POSIXThreads)
      Builder.defineMacro("_REENTRANT");
    // Required by the libc++ locale support.
    if (Opts.CPlusPlus)
      Builder.defineMacro("_GNU_SOURCE");
  }

public:
  FuchsiaTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    this->MCountName = "__mcount";
  }
};

// WebAssembly target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY WebAssemblyOSTargetInfo
    : public OSTargetInfo<Target> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const {
    // A common platform macro.
    if (Opts.POSIXThreads)
      Builder.defineMacro("_REENTRANT");
    // Follow g++ convention and predefine _GNU_SOURCE for C++.
    if (Opts.CPlusPlus)
      Builder.defineMacro("_GNU_SOURCE");
  }

public:
  explicit WebAssemblyOSTargetInfo(const llvm::Triple &Triple,
                                   const TargetOptions &Opts)
      : OSTargetInfo<Target>(Triple, Opts) {
    this->MCountName = "__mcount";
    this->TheCXXABI.set(TargetCXXABI::WebAssembly);
  }
};

// WASI target
template <typename Target>
class LLVM_LIBRARY_VISIBILITY WASITargetInfo
    : public WebAssemblyOSTargetInfo<Target> {
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const final {
    WebAssemblyOSTargetInfo<Target>::getOSDefines(Opts, Triple, Builder);
    Builder.defineMacro("__wasi__");
  }

public:
  explicit WASITargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : WebAssemblyOSTargetInfo<Target>(Triple, Opts) {}
};

} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_OSTARGETS_H
