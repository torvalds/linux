//=== WebAssembly.h - Declare WebAssembly target feature support *- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares WebAssembly TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_WEBASSEMBLY_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_WEBASSEMBLY_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Compiler.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY WebAssemblyTargetInfo : public TargetInfo {
  static const Builtin::Info BuiltinInfo[];

  enum SIMDEnum {
    NoSIMD,
    SIMD128,
    UnimplementedSIMD128,
  } SIMDLevel = NoSIMD;

  bool HasNontrappingFPToInt;
  bool HasSignExt;
  bool HasExceptionHandling;

public:
  explicit WebAssemblyTargetInfo(const llvm::Triple &T, const TargetOptions &)
      : TargetInfo(T), SIMDLevel(NoSIMD), HasNontrappingFPToInt(false),
        HasSignExt(false), HasExceptionHandling(false) {
    NoAsmVariants = true;
    SuitableAlign = 128;
    LargeArrayMinWidth = 128;
    LargeArrayAlign = 128;
    SimdDefaultAlign = 128;
    SigAtomicType = SignedLong;
    LongDoubleWidth = LongDoubleAlign = 128;
    LongDoubleFormat = &llvm::APFloat::IEEEquad();
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 64;
    // size_t being unsigned long for both wasm32 and wasm64 makes mangled names
    // more consistent between the two.
    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    IntPtrType = SignedLong;
  }

protected:
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

private:
  static void setSIMDLevel(llvm::StringMap<bool> &Features, SIMDEnum Level);

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override;
  bool hasFeature(StringRef Feature) const final;

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) final;

  bool isValidCPUName(StringRef Name) const final;
  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const final;

  bool setCPU(const std::string &Name) final { return isValidCPUName(Name); }

  ArrayRef<Builtin::Info> getTargetBuiltins() const final;

  BuiltinVaListKind getBuiltinVaListKind() const final {
    return VoidPtrBuiltinVaList;
  }

  ArrayRef<const char *> getGCCRegNames() const final { return None; }

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const final {
    return None;
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const final {
    return false;
  }

  const char *getClobbers() const final { return ""; }

  bool isCLZForZeroUndef() const final { return false; }

  bool hasInt128Type() const final { return true; }

  IntType getIntTypeByWidth(unsigned BitWidth, bool IsSigned) const final {
    // WebAssembly prefers long long for explicitly 64-bit integers.
    return BitWidth == 64 ? (IsSigned ? SignedLongLong : UnsignedLongLong)
                          : TargetInfo::getIntTypeByWidth(BitWidth, IsSigned);
  }

  IntType getLeastIntTypeByWidth(unsigned BitWidth, bool IsSigned) const final {
    // WebAssembly uses long long for int_least64_t and int_fast64_t.
    return BitWidth == 64
               ? (IsSigned ? SignedLongLong : UnsignedLongLong)
               : TargetInfo::getLeastIntTypeByWidth(BitWidth, IsSigned);
  }
};
class LLVM_LIBRARY_VISIBILITY WebAssembly32TargetInfo
    : public WebAssemblyTargetInfo {
public:
  explicit WebAssembly32TargetInfo(const llvm::Triple &T,
                                   const TargetOptions &Opts)
      : WebAssemblyTargetInfo(T, Opts) {
    resetDataLayout("e-m:e-p:32:32-i64:64-n32:64-S128");
  }

protected:
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};

class LLVM_LIBRARY_VISIBILITY WebAssembly64TargetInfo
    : public WebAssemblyTargetInfo {
public:
  explicit WebAssembly64TargetInfo(const llvm::Triple &T,
                                   const TargetOptions &Opts)
      : WebAssemblyTargetInfo(T, Opts) {
    LongAlign = LongWidth = 64;
    PointerAlign = PointerWidth = 64;
    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    IntPtrType = SignedLong;
    resetDataLayout("e-m:e-p:64:64-i64:64-n32:64-S128");
  }

protected:
  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
};
} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_WEBASSEMBLY_H
