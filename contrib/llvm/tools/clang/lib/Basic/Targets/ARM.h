//===--- ARM.h - Declare ARM target feature support -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares ARM TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_ARM_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_ARM_H

#include "OSTargets.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TargetParser.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY ARMTargetInfo : public TargetInfo {
  // Possible FPU choices.
  enum FPUMode {
    VFP2FPU = (1 << 0),
    VFP3FPU = (1 << 1),
    VFP4FPU = (1 << 2),
    NeonFPU = (1 << 3),
    FPARMV8 = (1 << 4)
  };

  // Possible HWDiv features.
  enum HWDivMode { HWDivThumb = (1 << 0), HWDivARM = (1 << 1) };

  static bool FPUModeIsVFP(FPUMode Mode) {
    return Mode & (VFP2FPU | VFP3FPU | VFP4FPU | NeonFPU | FPARMV8);
  }

  static const TargetInfo::GCCRegAlias GCCRegAliases[];
  static const char *const GCCRegNames[];

  std::string ABI, CPU;

  StringRef CPUProfile;
  StringRef CPUAttr;

  enum { FP_Default, FP_VFP, FP_Neon } FPMath;

  llvm::ARM::ISAKind ArchISA;
  llvm::ARM::ArchKind ArchKind = llvm::ARM::ArchKind::ARMV4T;
  llvm::ARM::ProfileKind ArchProfile;
  unsigned ArchVersion;

  unsigned FPU : 5;

  unsigned IsAAPCS : 1;
  unsigned HWDiv : 2;

  // Initialized via features.
  unsigned SoftFloat : 1;
  unsigned SoftFloatABI : 1;

  unsigned CRC : 1;
  unsigned Crypto : 1;
  unsigned DSP : 1;
  unsigned Unaligned : 1;
  unsigned DotProd : 1;

  enum {
    LDREX_B = (1 << 0), /// byte (8-bit)
    LDREX_H = (1 << 1), /// half (16-bit)
    LDREX_W = (1 << 2), /// word (32-bit)
    LDREX_D = (1 << 3), /// double (64-bit)
  };

  uint32_t LDREX;

  // ACLE 6.5.1 Hardware floating point
  enum {
    HW_FP_HP = (1 << 1), /// half (16-bit)
    HW_FP_SP = (1 << 2), /// single (32-bit)
    HW_FP_DP = (1 << 3), /// double (64-bit)
  };
  uint32_t HW_FP;

  static const Builtin::Info BuiltinInfo[];

  void setABIAAPCS();
  void setABIAPCS(bool IsAAPCS16);

  void setArchInfo();
  void setArchInfo(llvm::ARM::ArchKind Kind);

  void setAtomic();

  bool isThumb() const;
  bool supportsThumb() const;
  bool supportsThumb2() const;

  StringRef getCPUAttr() const;
  StringRef getCPUProfile() const;

public:
  ARMTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  StringRef getABI() const override;
  bool setABI(const std::string &Name) override;

  // FIXME: This should be based on Arch attributes, not CPU names.
  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override;

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;

  bool hasFeature(StringRef Feature) const override;

  bool isValidCPUName(StringRef Name) const override;
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(const std::string &Name) override;

  bool setFPMath(StringRef Name) override;

  bool useFP16ConversionIntrinsics() const override {
    return false;
  }

  void getTargetDefinesARMV81A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;

  void getTargetDefinesARMV82A(const LangOptions &Opts,
                               MacroBuilder &Builder) const;
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  bool isCLZForZeroUndef() const override;
  BuiltinVaListKind getBuiltinVaListKind() const override;

  ArrayRef<const char *> getGCCRegNames() const override;
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;
  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override;
  std::string convertConstraint(const char *&Constraint) const override;
  bool
  validateConstraintModifier(StringRef Constraint, char Modifier, unsigned Size,
                             std::string &SuggestedModifier) const override;
  const char *getClobbers() const override;

  StringRef getConstraintRegister(StringRef Constraint,
                                  StringRef Expression) const override {
    return Expression;
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override;

  int getEHDataRegisterNumber(unsigned RegNo) const override;

  bool hasSjLjLowering() const override;
};

class LLVM_LIBRARY_VISIBILITY ARMleTargetInfo : public ARMTargetInfo {
public:
  ARMleTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

class LLVM_LIBRARY_VISIBILITY ARMbeTargetInfo : public ARMTargetInfo {
public:
  ARMbeTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

class LLVM_LIBRARY_VISIBILITY WindowsARMTargetInfo
    : public WindowsTargetInfo<ARMleTargetInfo> {
  const llvm::Triple Triple;

public:
  WindowsARMTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  void getVisualStudioDefines(const LangOptions &Opts,
                              MacroBuilder &Builder) const;

  BuiltinVaListKind getBuiltinVaListKind() const override;

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override;
};

// Windows ARM + Itanium C++ ABI Target
class LLVM_LIBRARY_VISIBILITY ItaniumWindowsARMleTargetInfo
    : public WindowsARMTargetInfo {
public:
  ItaniumWindowsARMleTargetInfo(const llvm::Triple &Triple,
                                const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

// Windows ARM, MS (C++) ABI
class LLVM_LIBRARY_VISIBILITY MicrosoftARMleTargetInfo
    : public WindowsARMTargetInfo {
public:
  MicrosoftARMleTargetInfo(const llvm::Triple &Triple,
                           const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

// ARM MinGW target
class LLVM_LIBRARY_VISIBILITY MinGWARMTargetInfo : public WindowsARMTargetInfo {
public:
  MinGWARMTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

// ARM Cygwin target
class LLVM_LIBRARY_VISIBILITY CygwinARMTargetInfo : public ARMleTargetInfo {
public:
  CygwinARMTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

class LLVM_LIBRARY_VISIBILITY DarwinARMTargetInfo
    : public DarwinTargetInfo<ARMleTargetInfo> {
protected:
  void getOSDefines(const LangOptions &Opts, const llvm::Triple &Triple,
                    MacroBuilder &Builder) const override;

public:
  DarwinARMTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);
};

// 32-bit RenderScript is armv7 with width and align of 'long' set to 8-bytes
class LLVM_LIBRARY_VISIBILITY RenderScript32TargetInfo
    : public ARMleTargetInfo {
public:
  RenderScript32TargetInfo(const llvm::Triple &Triple,
                           const TargetOptions &Opts);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_ARM_H
