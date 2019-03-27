//===--- AMDGPU.h - Declare AMDGPU target feature support -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares AMDGPU TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_AMDGPU_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_AMDGPU_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TargetParser.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY AMDGPUTargetInfo final : public TargetInfo {

  static const Builtin::Info BuiltinInfo[];
  static const char *const GCCRegNames[];

  enum AddrSpace {
    Generic = 0,
    Global = 1,
    Local = 3,
    Constant = 4,
    Private = 5
  };
  static const LangASMap AMDGPUDefIsGenMap;
  static const LangASMap AMDGPUDefIsPrivMap;

  llvm::AMDGPU::GPUKind GPUKind;
  unsigned GPUFeatures;


  bool hasFP64() const {
    return getTriple().getArch() == llvm::Triple::amdgcn ||
           !!(GPUFeatures & llvm::AMDGPU::FEATURE_FP64);
  }

  /// Has fast fma f32
  bool hasFastFMAF() const {
    return !!(GPUFeatures & llvm::AMDGPU::FEATURE_FAST_FMA_F32);
  }

  /// Has fast fma f64
  bool hasFastFMA() const {
    return getTriple().getArch() == llvm::Triple::amdgcn;
  }

  bool hasFMAF() const {
    return getTriple().getArch() == llvm::Triple::amdgcn ||
           !!(GPUFeatures & llvm::AMDGPU::FEATURE_FMA);
  }

  bool hasFullRateDenormalsF32() const {
    return !!(GPUFeatures & llvm::AMDGPU::FEATURE_FAST_DENORMAL_F32);
  }

  bool hasLDEXPF() const {
    return getTriple().getArch() == llvm::Triple::amdgcn ||
           !!(GPUFeatures & llvm::AMDGPU::FEATURE_LDEXP);
  }

  static bool isAMDGCN(const llvm::Triple &TT) {
    return TT.getArch() == llvm::Triple::amdgcn;
  }

  static bool isR600(const llvm::Triple &TT) {
    return TT.getArch() == llvm::Triple::r600;
  }

public:
  AMDGPUTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts);

  void setAddressSpaceMap(bool DefaultIsPrivate);

  void adjust(LangOptions &Opts) override;

  uint64_t getPointerWidthV(unsigned AddrSpace) const override {
    if (isR600(getTriple()))
      return 32;

    if (AddrSpace == Private || AddrSpace == Local)
      return 32;

    return 64;
  }

  uint64_t getPointerAlignV(unsigned AddrSpace) const override {
    return getPointerWidthV(AddrSpace);
  }

  uint64_t getMaxPointerWidth() const override {
    return getTriple().getArch() == llvm::Triple::amdgcn ? 64 : 32;
  }

  const char *getClobbers() const override { return ""; }

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return None;
  }

  /// Accepted register names: (n, m is unsigned integer, n < m)
  /// v
  /// s
  /// {vn}, {v[n]}
  /// {sn}, {s[n]}
  /// {S} , where S is a special register name
  ////{v[n:m]}
  /// {s[n:m]}
  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    static const ::llvm::StringSet<> SpecialRegs({
        "exec", "vcc", "flat_scratch", "m0", "scc", "tba", "tma",
        "flat_scratch_lo", "flat_scratch_hi", "vcc_lo", "vcc_hi", "exec_lo",
        "exec_hi", "tma_lo", "tma_hi", "tba_lo", "tba_hi",
    });

    StringRef S(Name);
    bool HasLeftParen = false;
    if (S.front() == '{') {
      HasLeftParen = true;
      S = S.drop_front();
    }
    if (S.empty())
      return false;
    if (S.front() != 'v' && S.front() != 's') {
      if (!HasLeftParen)
        return false;
      auto E = S.find('}');
      if (!SpecialRegs.count(S.substr(0, E)))
        return false;
      S = S.drop_front(E + 1);
      if (!S.empty())
        return false;
      // Found {S} where S is a special register.
      Info.setAllowsRegister();
      Name = S.data() - 1;
      return true;
    }
    S = S.drop_front();
    if (!HasLeftParen) {
      if (!S.empty())
        return false;
      // Found s or v.
      Info.setAllowsRegister();
      Name = S.data() - 1;
      return true;
    }
    bool HasLeftBracket = false;
    if (!S.empty() && S.front() == '[') {
      HasLeftBracket = true;
      S = S.drop_front();
    }
    unsigned long long N;
    if (S.empty() || consumeUnsignedInteger(S, 10, N))
      return false;
    if (!S.empty() && S.front() == ':') {
      if (!HasLeftBracket)
        return false;
      S = S.drop_front();
      unsigned long long M;
      if (consumeUnsignedInteger(S, 10, M) || N >= M)
        return false;
    }
    if (HasLeftBracket) {
      if (S.empty() || S.front() != ']')
        return false;
      S = S.drop_front();
    }
    if (S.empty() || S.front() != '}')
      return false;
    S = S.drop_front();
    if (!S.empty())
      return false;
    // Found {vn}, {sn}, {v[n]}, {s[n]}, {v[n:m]}, or {s[n:m]}.
    Info.setAllowsRegister();
    Name = S.data() - 1;
    return true;
  }

  // \p Constraint will be left pointing at the last character of
  // the constraint.  In practice, it won't be changed unless the
  // constraint is longer than one character.
  std::string convertConstraint(const char *&Constraint) const override {
    const char *Begin = Constraint;
    TargetInfo::ConstraintInfo Info("", "");
    if (validateAsmConstraint(Constraint, Info))
      return std::string(Begin).substr(0, Constraint - Begin + 1);

    Constraint = Begin;
    return std::string(1, *Constraint);
  }

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeatureVec) const override;

  void adjustTargetOptions(const CodeGenOptions &CGOpts,
                           TargetOptions &TargetOpts) const override;

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::CharPtrBuiltinVaList;
  }

  bool isValidCPUName(StringRef Name) const override {
    if (getTriple().getArch() == llvm::Triple::amdgcn)
      return llvm::AMDGPU::parseArchAMDGCN(Name) != llvm::AMDGPU::GK_NONE;
    return llvm::AMDGPU::parseArchR600(Name) != llvm::AMDGPU::GK_NONE;
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(const std::string &Name) override {
    if (getTriple().getArch() == llvm::Triple::amdgcn) {
      GPUKind = llvm::AMDGPU::parseArchAMDGCN(Name);
      GPUFeatures = llvm::AMDGPU::getArchAttrAMDGCN(GPUKind);
    } else {
      GPUKind = llvm::AMDGPU::parseArchR600(Name);
      GPUFeatures = llvm::AMDGPU::getArchAttrR600(GPUKind);
    }

    return GPUKind != llvm::AMDGPU::GK_NONE;
  }

  void setSupportedOpenCLOpts() override {
    auto &Opts = getSupportedOpenCLOpts();
    Opts.support("cl_clang_storage_class_specifiers");
    Opts.support("cl_khr_icd");

    bool IsAMDGCN = isAMDGCN(getTriple());

    if (hasFP64())
      Opts.support("cl_khr_fp64");

    if (IsAMDGCN || GPUKind >= llvm::AMDGPU::GK_CEDAR) {
      Opts.support("cl_khr_byte_addressable_store");
      Opts.support("cl_khr_global_int32_base_atomics");
      Opts.support("cl_khr_global_int32_extended_atomics");
      Opts.support("cl_khr_local_int32_base_atomics");
      Opts.support("cl_khr_local_int32_extended_atomics");
    }

    if (IsAMDGCN) {
      Opts.support("cl_khr_fp16");
      Opts.support("cl_khr_int64_base_atomics");
      Opts.support("cl_khr_int64_extended_atomics");
      Opts.support("cl_khr_mipmap_image");
      Opts.support("cl_khr_subgroups");
      Opts.support("cl_khr_3d_image_writes");
      Opts.support("cl_amd_media_ops");
      Opts.support("cl_amd_media_ops2");
    }
  }

  LangAS getOpenCLTypeAddrSpace(OpenCLTypeKind TK) const override {
    switch (TK) {
    case OCLTK_Image:
      return LangAS::opencl_constant;

    case OCLTK_ClkEvent:
    case OCLTK_Queue:
    case OCLTK_ReserveID:
      return LangAS::opencl_global;

    default:
      return TargetInfo::getOpenCLTypeAddrSpace(TK);
    }
  }

  LangAS getOpenCLBuiltinAddressSpace(unsigned AS) const override {
    switch (AS) {
    case 0:
      return LangAS::opencl_generic;
    case 1:
      return LangAS::opencl_global;
    case 3:
      return LangAS::opencl_local;
    case 4:
      return LangAS::opencl_constant;
    case 5:
      return LangAS::opencl_private;
    default:
      return getLangASFromTargetAS(AS);
    }
  }

  LangAS getCUDABuiltinAddressSpace(unsigned AS) const override {
    return LangAS::Default;
  }

  llvm::Optional<LangAS> getConstantAddressSpace() const override {
    return getLangASFromTargetAS(Constant);
  }

  /// \returns Target specific vtbl ptr address space.
  unsigned getVtblPtrAddressSpace() const override {
    return static_cast<unsigned>(Constant);
  }

  /// \returns If a target requires an address within a target specific address
  /// space \p AddressSpace to be converted in order to be used, then return the
  /// corresponding target specific DWARF address space.
  ///
  /// \returns Otherwise return None and no conversion will be emitted in the
  /// DWARF.
  Optional<unsigned>
  getDWARFAddressSpace(unsigned AddressSpace) const override {
    const unsigned DWARF_Private = 1;
    const unsigned DWARF_Local = 2;
    if (AddressSpace == Private) {
      return DWARF_Private;
    } else if (AddressSpace == Local) {
      return DWARF_Local;
    } else {
      return None;
    }
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    switch (CC) {
    default:
      return CCCR_Warning;
    case CC_C:
    case CC_OpenCLKernel:
      return CCCR_OK;
    }
  }

  // In amdgcn target the null pointer in global, constant, and generic
  // address space has value 0 but in private and local address space has
  // value ~0.
  uint64_t getNullPointerValue(LangAS AS) const override {
    return AS == LangAS::opencl_local ? ~0 : 0;
  }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_AMDGPU_H
