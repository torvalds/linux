//===--- AMDGPU.cpp - Implement AMDGPU target feature support -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements AMDGPU TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/MacroBuilder.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

namespace clang {
namespace targets {

// If you edit the description strings, make sure you update
// getPointerWidthV().

static const char *const DataLayoutStringR600 =
    "e-p:32:32-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128"
    "-v192:256-v256:256-v512:512-v1024:1024-v2048:2048-n32:64-S32-A5";

static const char *const DataLayoutStringAMDGCN =
    "e-p:64:64-p1:64:64-p2:32:32-p3:32:32-p4:64:64-p5:32:32-p6:32:32"
    "-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128"
    "-v192:256-v256:256-v512:512-v1024:1024-v2048:2048-n32:64-S32-A5";

const LangASMap AMDGPUTargetInfo::AMDGPUDefIsGenMap = {
    Generic,  // Default
    Global,   // opencl_global
    Local,    // opencl_local
    Constant, // opencl_constant
    Private,  // opencl_private
    Generic,  // opencl_generic
    Global,   // cuda_device
    Constant, // cuda_constant
    Local     // cuda_shared
};

const LangASMap AMDGPUTargetInfo::AMDGPUDefIsPrivMap = {
    Private,  // Default
    Global,   // opencl_global
    Local,    // opencl_local
    Constant, // opencl_constant
    Private,  // opencl_private
    Generic,  // opencl_generic
    Global,   // cuda_device
    Constant, // cuda_constant
    Local     // cuda_shared
};
} // namespace targets
} // namespace clang

const Builtin::Info AMDGPUTargetInfo::BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, ALL_LANGUAGES, nullptr},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, nullptr, ALL_LANGUAGES, FEATURE},
#include "clang/Basic/BuiltinsAMDGPU.def"
};

const char *const AMDGPUTargetInfo::GCCRegNames[] = {
  "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8",
  "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17",
  "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26",
  "v27", "v28", "v29", "v30", "v31", "v32", "v33", "v34", "v35",
  "v36", "v37", "v38", "v39", "v40", "v41", "v42", "v43", "v44",
  "v45", "v46", "v47", "v48", "v49", "v50", "v51", "v52", "v53",
  "v54", "v55", "v56", "v57", "v58", "v59", "v60", "v61", "v62",
  "v63", "v64", "v65", "v66", "v67", "v68", "v69", "v70", "v71",
  "v72", "v73", "v74", "v75", "v76", "v77", "v78", "v79", "v80",
  "v81", "v82", "v83", "v84", "v85", "v86", "v87", "v88", "v89",
  "v90", "v91", "v92", "v93", "v94", "v95", "v96", "v97", "v98",
  "v99", "v100", "v101", "v102", "v103", "v104", "v105", "v106", "v107",
  "v108", "v109", "v110", "v111", "v112", "v113", "v114", "v115", "v116",
  "v117", "v118", "v119", "v120", "v121", "v122", "v123", "v124", "v125",
  "v126", "v127", "v128", "v129", "v130", "v131", "v132", "v133", "v134",
  "v135", "v136", "v137", "v138", "v139", "v140", "v141", "v142", "v143",
  "v144", "v145", "v146", "v147", "v148", "v149", "v150", "v151", "v152",
  "v153", "v154", "v155", "v156", "v157", "v158", "v159", "v160", "v161",
  "v162", "v163", "v164", "v165", "v166", "v167", "v168", "v169", "v170",
  "v171", "v172", "v173", "v174", "v175", "v176", "v177", "v178", "v179",
  "v180", "v181", "v182", "v183", "v184", "v185", "v186", "v187", "v188",
  "v189", "v190", "v191", "v192", "v193", "v194", "v195", "v196", "v197",
  "v198", "v199", "v200", "v201", "v202", "v203", "v204", "v205", "v206",
  "v207", "v208", "v209", "v210", "v211", "v212", "v213", "v214", "v215",
  "v216", "v217", "v218", "v219", "v220", "v221", "v222", "v223", "v224",
  "v225", "v226", "v227", "v228", "v229", "v230", "v231", "v232", "v233",
  "v234", "v235", "v236", "v237", "v238", "v239", "v240", "v241", "v242",
  "v243", "v244", "v245", "v246", "v247", "v248", "v249", "v250", "v251",
  "v252", "v253", "v254", "v255", "s0", "s1", "s2", "s3", "s4",
  "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s12", "s13",
  "s14", "s15", "s16", "s17", "s18", "s19", "s20", "s21", "s22",
  "s23", "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31",
  "s32", "s33", "s34", "s35", "s36", "s37", "s38", "s39", "s40",
  "s41", "s42", "s43", "s44", "s45", "s46", "s47", "s48", "s49",
  "s50", "s51", "s52", "s53", "s54", "s55", "s56", "s57", "s58",
  "s59", "s60", "s61", "s62", "s63", "s64", "s65", "s66", "s67",
  "s68", "s69", "s70", "s71", "s72", "s73", "s74", "s75", "s76",
  "s77", "s78", "s79", "s80", "s81", "s82", "s83", "s84", "s85",
  "s86", "s87", "s88", "s89", "s90", "s91", "s92", "s93", "s94",
  "s95", "s96", "s97", "s98", "s99", "s100", "s101", "s102", "s103",
  "s104", "s105", "s106", "s107", "s108", "s109", "s110", "s111", "s112",
  "s113", "s114", "s115", "s116", "s117", "s118", "s119", "s120", "s121",
  "s122", "s123", "s124", "s125", "s126", "s127", "exec", "vcc", "scc",
  "m0", "flat_scratch", "exec_lo", "exec_hi", "vcc_lo", "vcc_hi",
  "flat_scratch_lo", "flat_scratch_hi"
};

ArrayRef<const char *> AMDGPUTargetInfo::getGCCRegNames() const {
  return llvm::makeArrayRef(GCCRegNames);
}

bool AMDGPUTargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeatureVec) const {

  using namespace llvm::AMDGPU;

  // XXX - What does the member GPU mean if device name string passed here?
  if (isAMDGCN(getTriple())) {
    if (CPU.empty())
      CPU = "gfx600";

    switch (llvm::AMDGPU::parseArchAMDGCN(CPU)) {
    case GK_GFX906:
      Features["dl-insts"] = true;
      Features["dot-insts"] = true;
      LLVM_FALLTHROUGH;
    case GK_GFX909:
    case GK_GFX904:
    case GK_GFX902:
    case GK_GFX900:
      Features["gfx9-insts"] = true;
      LLVM_FALLTHROUGH;
    case GK_GFX810:
    case GK_GFX803:
    case GK_GFX802:
    case GK_GFX801:
      Features["vi-insts"] = true;
      Features["16-bit-insts"] = true;
      Features["dpp"] = true;
      Features["s-memrealtime"] = true;
      LLVM_FALLTHROUGH;
    case GK_GFX704:
    case GK_GFX703:
    case GK_GFX702:
    case GK_GFX701:
    case GK_GFX700:
      Features["ci-insts"] = true;
      LLVM_FALLTHROUGH;
    case GK_GFX601:
    case GK_GFX600:
      break;
    case GK_NONE:
      return false;
    default:
      llvm_unreachable("Unhandled GPU!");
    }
  } else {
    if (CPU.empty())
      CPU = "r600";

    switch (llvm::AMDGPU::parseArchR600(CPU)) {
    case GK_CAYMAN:
    case GK_CYPRESS:
    case GK_RV770:
    case GK_RV670:
      // TODO: Add fp64 when implemented.
      break;
    case GK_TURKS:
    case GK_CAICOS:
    case GK_BARTS:
    case GK_SUMO:
    case GK_REDWOOD:
    case GK_JUNIPER:
    case GK_CEDAR:
    case GK_RV730:
    case GK_RV710:
    case GK_RS880:
    case GK_R630:
    case GK_R600:
      break;
    default:
      llvm_unreachable("Unhandled GPU!");
    }
  }

  return TargetInfo::initFeatureMap(Features, Diags, CPU, FeatureVec);
}

void AMDGPUTargetInfo::adjustTargetOptions(const CodeGenOptions &CGOpts,
                                           TargetOptions &TargetOpts) const {
  bool hasFP32Denormals = false;
  bool hasFP64Denormals = false;

  for (auto &I : TargetOpts.FeaturesAsWritten) {
    if (I == "+fp32-denormals" || I == "-fp32-denormals")
      hasFP32Denormals = true;
    if (I == "+fp64-fp16-denormals" || I == "-fp64-fp16-denormals")
      hasFP64Denormals = true;
  }
  if (!hasFP32Denormals)
    TargetOpts.Features.push_back(
      (Twine(hasFastFMAF() && hasFullRateDenormalsF32() && !CGOpts.FlushDenorm
             ? '+' : '-') + Twine("fp32-denormals"))
            .str());
  // Always do not flush fp64 or fp16 denorms.
  if (!hasFP64Denormals && hasFP64())
    TargetOpts.Features.push_back("+fp64-fp16-denormals");
}

void AMDGPUTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  if (isAMDGCN(getTriple()))
    llvm::AMDGPU::fillValidArchListAMDGCN(Values);
  else
    llvm::AMDGPU::fillValidArchListR600(Values);
}

void AMDGPUTargetInfo::setAddressSpaceMap(bool DefaultIsPrivate) {
  AddrSpaceMap = DefaultIsPrivate ? &AMDGPUDefIsPrivMap : &AMDGPUDefIsGenMap;
}

AMDGPUTargetInfo::AMDGPUTargetInfo(const llvm::Triple &Triple,
                                   const TargetOptions &Opts)
    : TargetInfo(Triple),
      GPUKind(isAMDGCN(Triple) ?
              llvm::AMDGPU::parseArchAMDGCN(Opts.CPU) :
              llvm::AMDGPU::parseArchR600(Opts.CPU)),
      GPUFeatures(isAMDGCN(Triple) ?
                  llvm::AMDGPU::getArchAttrAMDGCN(GPUKind) :
                  llvm::AMDGPU::getArchAttrR600(GPUKind)) {
  resetDataLayout(isAMDGCN(getTriple()) ? DataLayoutStringAMDGCN
                                        : DataLayoutStringR600);
  assert(DataLayout->getAllocaAddrSpace() == Private);

  setAddressSpaceMap(Triple.getOS() == llvm::Triple::Mesa3D ||
                     !isAMDGCN(Triple));
  UseAddrSpaceMapMangling = true;

  // Set pointer width and alignment for target address space 0.
  PointerWidth = PointerAlign = DataLayout->getPointerSizeInBits();
  if (getMaxPointerWidth() == 64) {
    LongWidth = LongAlign = 64;
    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    IntPtrType = SignedLong;
  }

  MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 64;
}

void AMDGPUTargetInfo::adjust(LangOptions &Opts) {
  TargetInfo::adjust(Opts);
  // ToDo: There are still a few places using default address space as private
  // address space in OpenCL, which needs to be cleaned up, then Opts.OpenCL
  // can be removed from the following line.
  setAddressSpaceMap(/*DefaultIsPrivate=*/Opts.OpenCL ||
                     !isAMDGCN(getTriple()));
}

ArrayRef<Builtin::Info> AMDGPUTargetInfo::getTargetBuiltins() const {
  return llvm::makeArrayRef(BuiltinInfo, clang::AMDGPU::LastTSBuiltin -
                                             Builtin::FirstTSBuiltin);
}

void AMDGPUTargetInfo::getTargetDefines(const LangOptions &Opts,
                                        MacroBuilder &Builder) const {
  Builder.defineMacro("__AMD__");
  Builder.defineMacro("__AMDGPU__");

  if (isAMDGCN(getTriple()))
    Builder.defineMacro("__AMDGCN__");
  else
    Builder.defineMacro("__R600__");

  if (GPUKind != llvm::AMDGPU::GK_NONE) {
    StringRef CanonName = isAMDGCN(getTriple()) ?
      getArchNameAMDGCN(GPUKind) : getArchNameR600(GPUKind);
    Builder.defineMacro(Twine("__") + Twine(CanonName) + Twine("__"));
  }

  // TODO: __HAS_FMAF__, __HAS_LDEXPF__, __HAS_FP64__ are deprecated and will be
  // removed in the near future.
  if (hasFMAF())
    Builder.defineMacro("__HAS_FMAF__");
  if (hasFastFMAF())
    Builder.defineMacro("FP_FAST_FMAF");
  if (hasLDEXPF())
    Builder.defineMacro("__HAS_LDEXPF__");
  if (hasFP64())
    Builder.defineMacro("__HAS_FP64__");
  if (hasFastFMA())
    Builder.defineMacro("FP_FAST_FMA");
}
