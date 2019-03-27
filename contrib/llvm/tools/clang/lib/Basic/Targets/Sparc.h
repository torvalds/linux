//===--- Sparc.h - declare sparc target feature support ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares Sparc TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_SPARC_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_SPARC_H
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Compiler.h"
namespace clang {
namespace targets {
// Shared base class for SPARC v8 (32-bit) and SPARC v9 (64-bit).
class LLVM_LIBRARY_VISIBILITY SparcTargetInfo : public TargetInfo {
  static const TargetInfo::GCCRegAlias GCCRegAliases[];
  static const char *const GCCRegNames[];
  bool SoftFloat;

public:
  SparcTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple), SoftFloat(false) {}

  int getEHDataRegisterNumber(unsigned RegNo) const override {
    if (RegNo == 0)
      return 24;
    if (RegNo == 1)
      return 25;
    return -1;
  }

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override {
    // Check if software floating point is enabled
    auto Feature = std::find(Features.begin(), Features.end(), "+soft-float");
    if (Feature != Features.end()) {
      SoftFloat = true;
    }
    return true;
  }
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  bool hasFeature(StringRef Feature) const override;

  bool hasSjLjLowering() const override { return true; }

  ArrayRef<Builtin::Info> getTargetBuiltins() const override {
    // FIXME: Implement!
    return None;
  }
  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }
  ArrayRef<const char *> getGCCRegNames() const override;
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;
  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &info) const override {
    // FIXME: Implement!
    switch (*Name) {
    case 'I': // Signed 13-bit constant
    case 'J': // Zero
    case 'K': // 32-bit constant with the low 12 bits clear
    case 'L': // A constant in the range supported by movcc (11-bit signed imm)
    case 'M': // A constant in the range supported by movrcc (19-bit signed imm)
    case 'N': // Same as 'K' but zext (required for SIMode)
    case 'O': // The constant 4096
      return true;

    case 'f':
    case 'e':
      info.setAllowsRegister();
      return true;
    }
    return false;
  }
  const char *getClobbers() const override {
    // FIXME: Implement!
    return "";
  }

  // No Sparc V7 for now, the backend doesn't support it anyway.
  enum CPUKind {
    CK_GENERIC,
    CK_V8,
    CK_SUPERSPARC,
    CK_SPARCLITE,
    CK_F934,
    CK_HYPERSPARC,
    CK_SPARCLITE86X,
    CK_SPARCLET,
    CK_TSC701,
    CK_V9,
    CK_ULTRASPARC,
    CK_ULTRASPARC3,
    CK_NIAGARA,
    CK_NIAGARA2,
    CK_NIAGARA3,
    CK_NIAGARA4,
    CK_MYRIAD2100,
    CK_MYRIAD2150,
    CK_MYRIAD2155,
    CK_MYRIAD2450,
    CK_MYRIAD2455,
    CK_MYRIAD2x5x,
    CK_MYRIAD2080,
    CK_MYRIAD2085,
    CK_MYRIAD2480,
    CK_MYRIAD2485,
    CK_MYRIAD2x8x,
    CK_LEON2,
    CK_LEON2_AT697E,
    CK_LEON2_AT697F,
    CK_LEON3,
    CK_LEON3_UT699,
    CK_LEON3_GR712RC,
    CK_LEON4,
    CK_LEON4_GR740
  } CPU = CK_GENERIC;

  enum CPUGeneration {
    CG_V8,
    CG_V9,
  };

  CPUGeneration getCPUGeneration(CPUKind Kind) const;

  CPUKind getCPUKind(StringRef Name) const;

  bool isValidCPUName(StringRef Name) const override {
    return getCPUKind(Name) != CK_GENERIC;
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(const std::string &Name) override {
    CPU = getCPUKind(Name);
    return CPU != CK_GENERIC;
  }
};

// SPARC v8 is the 32-bit mode selected by Triple::sparc.
class LLVM_LIBRARY_VISIBILITY SparcV8TargetInfo : public SparcTargetInfo {
public:
  SparcV8TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : SparcTargetInfo(Triple, Opts) {
    resetDataLayout("E-m:e-p:32:32-i64:64-f128:64-n32-S64");
    // NetBSD / OpenBSD use long (same as llvm default); everyone else uses int.
    switch (getTriple().getOS()) {
    default:
      SizeType = UnsignedInt;
      IntPtrType = SignedInt;
      PtrDiffType = SignedInt;
      break;
    case llvm::Triple::NetBSD:
    case llvm::Triple::OpenBSD:
      SizeType = UnsignedLong;
      IntPtrType = SignedLong;
      PtrDiffType = SignedLong;
      break;
    }
    // Up to 32 bits are lock-free atomic, but we're willing to do atomic ops
    // on up to 64 bits.
    MaxAtomicPromoteWidth = 64;
    MaxAtomicInlineWidth = 32;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  bool hasSjLjLowering() const override { return true; }
};

// SPARCV8el is the 32-bit little-endian mode selected by Triple::sparcel.
class LLVM_LIBRARY_VISIBILITY SparcV8elTargetInfo : public SparcV8TargetInfo {
public:
  SparcV8elTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : SparcV8TargetInfo(Triple, Opts) {
    resetDataLayout("e-m:e-p:32:32-i64:64-f128:64-n32-S64");
  }
};

// SPARC v9 is the 64-bit mode selected by Triple::sparcv9.
class LLVM_LIBRARY_VISIBILITY SparcV9TargetInfo : public SparcTargetInfo {
public:
  SparcV9TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : SparcTargetInfo(Triple, Opts) {
    // FIXME: Support Sparc quad-precision long double?
    resetDataLayout("E-m:e-i64:64-n32:64-S128");
    // This is an LP64 platform.
    LongWidth = LongAlign = PointerWidth = PointerAlign = 64;

    // OpenBSD uses long long for int64_t and intmax_t.
    if (getTriple().isOSOpenBSD())
      IntMaxType = SignedLongLong;
    else
      IntMaxType = SignedLong;
    Int64Type = IntMaxType;

    // The SPARCv8 System V ABI has long double 128-bits in size, but 64-bit
    // aligned. The SPARCv9 SCD 2.4.1 says 16-byte aligned.
    LongDoubleWidth = 128;
    LongDoubleAlign = 128;
    LongDoubleFormat = &llvm::APFloat::IEEEquad();
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 64;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  bool isValidCPUName(StringRef Name) const override {
    return getCPUGeneration(SparcTargetInfo::getCPUKind(Name)) == CG_V9;
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(const std::string &Name) override {
    if (!SparcTargetInfo::setCPU(Name))
      return false;
    return getCPUGeneration(CPU) == CG_V9;
  }
};
} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_SPARC_H
