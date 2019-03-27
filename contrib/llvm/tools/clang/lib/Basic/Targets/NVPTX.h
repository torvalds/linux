//===--- NVPTX.h - Declare NVPTX target feature support ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares NVPTX TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_NVPTX_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_NVPTX_H

#include "clang/Basic/Cuda.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Compiler.h"

namespace clang {
namespace targets {

static const unsigned NVPTXAddrSpaceMap[] = {
    0, // Default
    1, // opencl_global
    3, // opencl_local
    4, // opencl_constant
    0, // opencl_private
    // FIXME: generic has to be added to the target
    0, // opencl_generic
    1, // cuda_device
    4, // cuda_constant
    3, // cuda_shared
};

class LLVM_LIBRARY_VISIBILITY NVPTXTargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];
  static const Builtin::Info BuiltinInfo[];
  CudaArch GPU;
  uint32_t PTXVersion;
  std::unique_ptr<TargetInfo> HostTarget;

public:
  NVPTXTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts,
                  unsigned TargetPointerWidth);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  ArrayRef<Builtin::Info> getTargetBuiltins() const override;

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeaturesVec) const override {
    Features[CudaArchToString(GPU)] = true;
    Features["ptx" + std::to_string(PTXVersion)] = true;
    return TargetInfo::initFeatureMap(Features, Diags, CPU, FeaturesVec);
  }

  bool hasFeature(StringRef Feature) const override;

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    // No aliases.
    return None;
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    switch (*Name) {
    default:
      return false;
    case 'c':
    case 'h':
    case 'r':
    case 'l':
    case 'f':
    case 'd':
      Info.setAllowsRegister();
      return true;
    }
  }

  const char *getClobbers() const override {
    // FIXME: Is this really right?
    return "";
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    // FIXME: implement
    return TargetInfo::CharPtrBuiltinVaList;
  }

  bool isValidCPUName(StringRef Name) const override {
    return StringToCudaArch(Name) != CudaArch::UNKNOWN;
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override {
    for (int i = static_cast<int>(CudaArch::SM_20);
         i < static_cast<int>(CudaArch::LAST); ++i)
      Values.emplace_back(CudaArchToString(static_cast<CudaArch>(i)));
  }

  bool setCPU(const std::string &Name) override {
    GPU = StringToCudaArch(Name);
    return GPU != CudaArch::UNKNOWN;
  }

  void setSupportedOpenCLOpts() override {
    auto &Opts = getSupportedOpenCLOpts();
    Opts.support("cl_clang_storage_class_specifiers");
    Opts.support("cl_khr_gl_sharing");
    Opts.support("cl_khr_icd");

    Opts.support("cl_khr_fp64");
    Opts.support("cl_khr_byte_addressable_store");
    Opts.support("cl_khr_global_int32_base_atomics");
    Opts.support("cl_khr_global_int32_extended_atomics");
    Opts.support("cl_khr_local_int32_base_atomics");
    Opts.support("cl_khr_local_int32_extended_atomics");
  }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    // CUDA compilations support all of the host's calling conventions.
    //
    // TODO: We should warn if you apply a non-default CC to anything other than
    // a host function.
    if (HostTarget)
      return HostTarget->checkCallingConvention(CC);
    return CCCR_Warning;
  }
};
} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_NVPTX_H
