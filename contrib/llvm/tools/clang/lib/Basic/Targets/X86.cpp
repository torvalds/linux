//===--- X86.cpp - Implement X86 target feature support -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements X86 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/TargetParser.h"

namespace clang {
namespace targets {

const Builtin::Info BuiltinInfoX86[] = {
#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, ALL_LANGUAGES, nullptr},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, nullptr, ALL_LANGUAGES, FEATURE},
#define TARGET_HEADER_BUILTIN(ID, TYPE, ATTRS, HEADER, LANGS, FEATURE)         \
  {#ID, TYPE, ATTRS, HEADER, LANGS, FEATURE},
#include "clang/Basic/BuiltinsX86.def"

#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, ALL_LANGUAGES, nullptr},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, nullptr, ALL_LANGUAGES, FEATURE},
#define TARGET_HEADER_BUILTIN(ID, TYPE, ATTRS, HEADER, LANGS, FEATURE)         \
  {#ID, TYPE, ATTRS, HEADER, LANGS, FEATURE},
#include "clang/Basic/BuiltinsX86_64.def"
};

static const char *const GCCRegNames[] = {
    "ax",    "dx",    "cx",    "bx",    "si",      "di",    "bp",    "sp",
    "st",    "st(1)", "st(2)", "st(3)", "st(4)",   "st(5)", "st(6)", "st(7)",
    "argp",  "flags", "fpcr",  "fpsr",  "dirflag", "frame", "xmm0",  "xmm1",
    "xmm2",  "xmm3",  "xmm4",  "xmm5",  "xmm6",    "xmm7",  "mm0",   "mm1",
    "mm2",   "mm3",   "mm4",   "mm5",   "mm6",     "mm7",   "r8",    "r9",
    "r10",   "r11",   "r12",   "r13",   "r14",     "r15",   "xmm8",  "xmm9",
    "xmm10", "xmm11", "xmm12", "xmm13", "xmm14",   "xmm15", "ymm0",  "ymm1",
    "ymm2",  "ymm3",  "ymm4",  "ymm5",  "ymm6",    "ymm7",  "ymm8",  "ymm9",
    "ymm10", "ymm11", "ymm12", "ymm13", "ymm14",   "ymm15", "xmm16", "xmm17",
    "xmm18", "xmm19", "xmm20", "xmm21", "xmm22",   "xmm23", "xmm24", "xmm25",
    "xmm26", "xmm27", "xmm28", "xmm29", "xmm30",   "xmm31", "ymm16", "ymm17",
    "ymm18", "ymm19", "ymm20", "ymm21", "ymm22",   "ymm23", "ymm24", "ymm25",
    "ymm26", "ymm27", "ymm28", "ymm29", "ymm30",   "ymm31", "zmm0",  "zmm1",
    "zmm2",  "zmm3",  "zmm4",  "zmm5",  "zmm6",    "zmm7",  "zmm8",  "zmm9",
    "zmm10", "zmm11", "zmm12", "zmm13", "zmm14",   "zmm15", "zmm16", "zmm17",
    "zmm18", "zmm19", "zmm20", "zmm21", "zmm22",   "zmm23", "zmm24", "zmm25",
    "zmm26", "zmm27", "zmm28", "zmm29", "zmm30",   "zmm31", "k0",    "k1",
    "k2",    "k3",    "k4",    "k5",    "k6",      "k7",
    "cr0",   "cr2",   "cr3",   "cr4",   "cr8",
    "dr0",   "dr1",   "dr2",   "dr3",   "dr6",     "dr7",
    "bnd0",  "bnd1",  "bnd2",  "bnd3",
};

const TargetInfo::AddlRegName AddlRegNames[] = {
    {{"al", "ah", "eax", "rax"}, 0},
    {{"bl", "bh", "ebx", "rbx"}, 3},
    {{"cl", "ch", "ecx", "rcx"}, 2},
    {{"dl", "dh", "edx", "rdx"}, 1},
    {{"esi", "rsi"}, 4},
    {{"edi", "rdi"}, 5},
    {{"esp", "rsp"}, 7},
    {{"ebp", "rbp"}, 6},
    {{"r8d", "r8w", "r8b"}, 38},
    {{"r9d", "r9w", "r9b"}, 39},
    {{"r10d", "r10w", "r10b"}, 40},
    {{"r11d", "r11w", "r11b"}, 41},
    {{"r12d", "r12w", "r12b"}, 42},
    {{"r13d", "r13w", "r13b"}, 43},
    {{"r14d", "r14w", "r14b"}, 44},
    {{"r15d", "r15w", "r15b"}, 45},
};

} // namespace targets
} // namespace clang

using namespace clang;
using namespace clang::targets;

bool X86TargetInfo::setFPMath(StringRef Name) {
  if (Name == "387") {
    FPMath = FP_387;
    return true;
  }
  if (Name == "sse") {
    FPMath = FP_SSE;
    return true;
  }
  return false;
}

bool X86TargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeaturesVec) const {
  // FIXME: This *really* should not be here.
  // X86_64 always has SSE2.
  if (getTriple().getArch() == llvm::Triple::x86_64)
    setFeatureEnabledImpl(Features, "sse2", true);

  const CPUKind Kind = getCPUKind(CPU);

  // Enable X87 for all X86 processors but Lakemont.
  if (Kind != CK_Lakemont)
    setFeatureEnabledImpl(Features, "x87", true);

  switch (Kind) {
  case CK_Generic:
  case CK_i386:
  case CK_i486:
  case CK_i586:
  case CK_Pentium:
  case CK_PentiumPro:
  case CK_Lakemont:
    break;

  case CK_PentiumMMX:
  case CK_Pentium2:
  case CK_K6:
  case CK_WinChipC6:
    setFeatureEnabledImpl(Features, "mmx", true);
    break;

  case CK_IcelakeServer:
    setFeatureEnabledImpl(Features, "pconfig", true);
    setFeatureEnabledImpl(Features, "wbnoinvd", true);
    LLVM_FALLTHROUGH;
  case CK_IcelakeClient:
    setFeatureEnabledImpl(Features, "vaes", true);
    setFeatureEnabledImpl(Features, "gfni", true);
    setFeatureEnabledImpl(Features, "vpclmulqdq", true);
    setFeatureEnabledImpl(Features, "avx512bitalg", true);
    setFeatureEnabledImpl(Features, "avx512vbmi2", true);
    setFeatureEnabledImpl(Features, "avx512vpopcntdq", true);
    setFeatureEnabledImpl(Features, "rdpid", true);
    LLVM_FALLTHROUGH;
  case CK_Cannonlake:
    setFeatureEnabledImpl(Features, "avx512ifma", true);
    setFeatureEnabledImpl(Features, "avx512vbmi", true);
    setFeatureEnabledImpl(Features, "sha", true);
    LLVM_FALLTHROUGH;
  case CK_Cascadelake:
    //Cannonlake has no VNNI feature inside while Icelake has
    if (Kind != CK_Cannonlake)
      // CLK inherits all SKX features plus AVX512_VNNI
      setFeatureEnabledImpl(Features, "avx512vnni", true);
    LLVM_FALLTHROUGH;
  case CK_SkylakeServer:
    setFeatureEnabledImpl(Features, "avx512f", true);
    setFeatureEnabledImpl(Features, "avx512cd", true);
    setFeatureEnabledImpl(Features, "avx512dq", true);
    setFeatureEnabledImpl(Features, "avx512bw", true);
    setFeatureEnabledImpl(Features, "avx512vl", true);
    setFeatureEnabledImpl(Features, "pku", true);
    if (Kind != CK_Cannonlake) // CNL inherits all SKX features, except CLWB
      setFeatureEnabledImpl(Features, "clwb", true);
    LLVM_FALLTHROUGH;
  case CK_SkylakeClient:
    setFeatureEnabledImpl(Features, "xsavec", true);
    setFeatureEnabledImpl(Features, "xsaves", true);
    setFeatureEnabledImpl(Features, "mpx", true);
    if (Kind != CK_SkylakeServer
        && Kind != CK_Cascadelake)
      // SKX/CLX inherits all SKL features, except SGX
      setFeatureEnabledImpl(Features, "sgx", true);
    setFeatureEnabledImpl(Features, "clflushopt", true);
    setFeatureEnabledImpl(Features, "aes", true);
    LLVM_FALLTHROUGH;
  case CK_Broadwell:
    setFeatureEnabledImpl(Features, "rdseed", true);
    setFeatureEnabledImpl(Features, "adx", true);
    setFeatureEnabledImpl(Features, "prfchw", true);
    LLVM_FALLTHROUGH;
  case CK_Haswell:
    setFeatureEnabledImpl(Features, "avx2", true);
    setFeatureEnabledImpl(Features, "lzcnt", true);
    setFeatureEnabledImpl(Features, "bmi", true);
    setFeatureEnabledImpl(Features, "bmi2", true);
    setFeatureEnabledImpl(Features, "fma", true);
    setFeatureEnabledImpl(Features, "invpcid", true);
    setFeatureEnabledImpl(Features, "movbe", true);
    LLVM_FALLTHROUGH;
  case CK_IvyBridge:
    setFeatureEnabledImpl(Features, "rdrnd", true);
    setFeatureEnabledImpl(Features, "f16c", true);
    setFeatureEnabledImpl(Features, "fsgsbase", true);
    LLVM_FALLTHROUGH;
  case CK_SandyBridge:
    setFeatureEnabledImpl(Features, "avx", true);
    setFeatureEnabledImpl(Features, "xsave", true);
    setFeatureEnabledImpl(Features, "xsaveopt", true);
    LLVM_FALLTHROUGH;
  case CK_Westmere:
    setFeatureEnabledImpl(Features, "pclmul", true);
    LLVM_FALLTHROUGH;
  case CK_Nehalem:
    setFeatureEnabledImpl(Features, "sse4.2", true);
    LLVM_FALLTHROUGH;
  case CK_Penryn:
    setFeatureEnabledImpl(Features, "sse4.1", true);
    LLVM_FALLTHROUGH;
  case CK_Core2:
    setFeatureEnabledImpl(Features, "ssse3", true);
    setFeatureEnabledImpl(Features, "sahf", true);
    LLVM_FALLTHROUGH;
  case CK_Yonah:
  case CK_Prescott:
  case CK_Nocona:
    setFeatureEnabledImpl(Features, "sse3", true);
    setFeatureEnabledImpl(Features, "cx16", true);
    LLVM_FALLTHROUGH;
  case CK_PentiumM:
  case CK_Pentium4:
  case CK_x86_64:
    setFeatureEnabledImpl(Features, "sse2", true);
    LLVM_FALLTHROUGH;
  case CK_Pentium3:
  case CK_C3_2:
    setFeatureEnabledImpl(Features, "sse", true);
    setFeatureEnabledImpl(Features, "fxsr", true);
    break;

  case CK_Tremont:
    setFeatureEnabledImpl(Features, "cldemote", true);
    setFeatureEnabledImpl(Features, "movdiri", true);
    setFeatureEnabledImpl(Features, "movdir64b", true);
    setFeatureEnabledImpl(Features, "gfni", true);
    setFeatureEnabledImpl(Features, "waitpkg", true);
    LLVM_FALLTHROUGH;
  case CK_GoldmontPlus:
    setFeatureEnabledImpl(Features, "ptwrite", true);
    setFeatureEnabledImpl(Features, "rdpid", true);
    setFeatureEnabledImpl(Features, "sgx", true);
    LLVM_FALLTHROUGH;
  case CK_Goldmont:
    setFeatureEnabledImpl(Features, "sha", true);
    setFeatureEnabledImpl(Features, "rdseed", true);
    setFeatureEnabledImpl(Features, "xsave", true);
    setFeatureEnabledImpl(Features, "xsaveopt", true);
    setFeatureEnabledImpl(Features, "xsavec", true);
    setFeatureEnabledImpl(Features, "xsaves", true);
    setFeatureEnabledImpl(Features, "clflushopt", true);
    setFeatureEnabledImpl(Features, "mpx", true);
    setFeatureEnabledImpl(Features, "fsgsbase", true);
    setFeatureEnabledImpl(Features, "aes", true);
    LLVM_FALLTHROUGH;
  case CK_Silvermont:
    setFeatureEnabledImpl(Features, "rdrnd", true);
    setFeatureEnabledImpl(Features, "pclmul", true);
    setFeatureEnabledImpl(Features, "sse4.2", true);
    setFeatureEnabledImpl(Features, "prfchw", true);
    LLVM_FALLTHROUGH;
  case CK_Bonnell:
    setFeatureEnabledImpl(Features, "movbe", true);
    setFeatureEnabledImpl(Features, "ssse3", true);
    setFeatureEnabledImpl(Features, "fxsr", true);
    setFeatureEnabledImpl(Features, "cx16", true);
    setFeatureEnabledImpl(Features, "sahf", true);
    break;

  case CK_KNM:
    // TODO: Add avx5124fmaps/avx5124vnniw.
    setFeatureEnabledImpl(Features, "avx512vpopcntdq", true);
    LLVM_FALLTHROUGH;
  case CK_KNL:
    setFeatureEnabledImpl(Features, "avx512f", true);
    setFeatureEnabledImpl(Features, "avx512cd", true);
    setFeatureEnabledImpl(Features, "avx512er", true);
    setFeatureEnabledImpl(Features, "avx512pf", true);
    setFeatureEnabledImpl(Features, "prfchw", true);
    setFeatureEnabledImpl(Features, "prefetchwt1", true);
    setFeatureEnabledImpl(Features, "fxsr", true);
    setFeatureEnabledImpl(Features, "rdseed", true);
    setFeatureEnabledImpl(Features, "adx", true);
    setFeatureEnabledImpl(Features, "lzcnt", true);
    setFeatureEnabledImpl(Features, "bmi", true);
    setFeatureEnabledImpl(Features, "bmi2", true);
    setFeatureEnabledImpl(Features, "fma", true);
    setFeatureEnabledImpl(Features, "rdrnd", true);
    setFeatureEnabledImpl(Features, "f16c", true);
    setFeatureEnabledImpl(Features, "fsgsbase", true);
    setFeatureEnabledImpl(Features, "aes", true);
    setFeatureEnabledImpl(Features, "pclmul", true);
    setFeatureEnabledImpl(Features, "cx16", true);
    setFeatureEnabledImpl(Features, "xsaveopt", true);
    setFeatureEnabledImpl(Features, "xsave", true);
    setFeatureEnabledImpl(Features, "movbe", true);
    setFeatureEnabledImpl(Features, "sahf", true);
    break;

  case CK_K6_2:
  case CK_K6_3:
  case CK_WinChip2:
  case CK_C3:
    setFeatureEnabledImpl(Features, "3dnow", true);
    break;

  case CK_AMDFAM10:
    setFeatureEnabledImpl(Features, "sse4a", true);
    setFeatureEnabledImpl(Features, "lzcnt", true);
    setFeatureEnabledImpl(Features, "popcnt", true);
    setFeatureEnabledImpl(Features, "sahf", true);
    LLVM_FALLTHROUGH;
  case CK_K8SSE3:
    setFeatureEnabledImpl(Features, "sse3", true);
    LLVM_FALLTHROUGH;
  case CK_K8:
    setFeatureEnabledImpl(Features, "sse2", true);
    LLVM_FALLTHROUGH;
  case CK_AthlonXP:
    setFeatureEnabledImpl(Features, "sse", true);
    setFeatureEnabledImpl(Features, "fxsr", true);
    LLVM_FALLTHROUGH;
  case CK_Athlon:
  case CK_Geode:
    setFeatureEnabledImpl(Features, "3dnowa", true);
    break;

  case CK_BTVER2:
    setFeatureEnabledImpl(Features, "avx", true);
    setFeatureEnabledImpl(Features, "aes", true);
    setFeatureEnabledImpl(Features, "pclmul", true);
    setFeatureEnabledImpl(Features, "bmi", true);
    setFeatureEnabledImpl(Features, "f16c", true);
    setFeatureEnabledImpl(Features, "xsaveopt", true);
    setFeatureEnabledImpl(Features, "movbe", true);
    LLVM_FALLTHROUGH;
  case CK_BTVER1:
    setFeatureEnabledImpl(Features, "ssse3", true);
    setFeatureEnabledImpl(Features, "sse4a", true);
    setFeatureEnabledImpl(Features, "lzcnt", true);
    setFeatureEnabledImpl(Features, "popcnt", true);
    setFeatureEnabledImpl(Features, "prfchw", true);
    setFeatureEnabledImpl(Features, "cx16", true);
    setFeatureEnabledImpl(Features, "fxsr", true);
    setFeatureEnabledImpl(Features, "sahf", true);
    break;

  case CK_ZNVER1:
    setFeatureEnabledImpl(Features, "adx", true);
    setFeatureEnabledImpl(Features, "aes", true);
    setFeatureEnabledImpl(Features, "avx2", true);
    setFeatureEnabledImpl(Features, "bmi", true);
    setFeatureEnabledImpl(Features, "bmi2", true);
    setFeatureEnabledImpl(Features, "clflushopt", true);
    setFeatureEnabledImpl(Features, "clzero", true);
    setFeatureEnabledImpl(Features, "cx16", true);
    setFeatureEnabledImpl(Features, "f16c", true);
    setFeatureEnabledImpl(Features, "fma", true);
    setFeatureEnabledImpl(Features, "fsgsbase", true);
    setFeatureEnabledImpl(Features, "fxsr", true);
    setFeatureEnabledImpl(Features, "lzcnt", true);
    setFeatureEnabledImpl(Features, "mwaitx", true);
    setFeatureEnabledImpl(Features, "movbe", true);
    setFeatureEnabledImpl(Features, "pclmul", true);
    setFeatureEnabledImpl(Features, "popcnt", true);
    setFeatureEnabledImpl(Features, "prfchw", true);
    setFeatureEnabledImpl(Features, "rdrnd", true);
    setFeatureEnabledImpl(Features, "rdseed", true);
    setFeatureEnabledImpl(Features, "sahf", true);
    setFeatureEnabledImpl(Features, "sha", true);
    setFeatureEnabledImpl(Features, "sse4a", true);
    setFeatureEnabledImpl(Features, "xsave", true);
    setFeatureEnabledImpl(Features, "xsavec", true);
    setFeatureEnabledImpl(Features, "xsaveopt", true);
    setFeatureEnabledImpl(Features, "xsaves", true);
    break;

  case CK_BDVER4:
    setFeatureEnabledImpl(Features, "avx2", true);
    setFeatureEnabledImpl(Features, "bmi2", true);
    setFeatureEnabledImpl(Features, "mwaitx", true);
    LLVM_FALLTHROUGH;
  case CK_BDVER3:
    setFeatureEnabledImpl(Features, "fsgsbase", true);
    setFeatureEnabledImpl(Features, "xsaveopt", true);
    LLVM_FALLTHROUGH;
  case CK_BDVER2:
    setFeatureEnabledImpl(Features, "bmi", true);
    setFeatureEnabledImpl(Features, "fma", true);
    setFeatureEnabledImpl(Features, "f16c", true);
    setFeatureEnabledImpl(Features, "tbm", true);
    LLVM_FALLTHROUGH;
  case CK_BDVER1:
    // xop implies avx, sse4a and fma4.
    setFeatureEnabledImpl(Features, "xop", true);
    setFeatureEnabledImpl(Features, "lwp", true);
    setFeatureEnabledImpl(Features, "lzcnt", true);
    setFeatureEnabledImpl(Features, "aes", true);
    setFeatureEnabledImpl(Features, "pclmul", true);
    setFeatureEnabledImpl(Features, "prfchw", true);
    setFeatureEnabledImpl(Features, "cx16", true);
    setFeatureEnabledImpl(Features, "fxsr", true);
    setFeatureEnabledImpl(Features, "xsave", true);
    setFeatureEnabledImpl(Features, "sahf", true);
    break;
  }
  if (!TargetInfo::initFeatureMap(Features, Diags, CPU, FeaturesVec))
    return false;

  // Can't do this earlier because we need to be able to explicitly enable
  // or disable these features and the things that they depend upon.

  // Enable popcnt if sse4.2 is enabled and popcnt is not explicitly disabled.
  auto I = Features.find("sse4.2");
  if (I != Features.end() && I->getValue() &&
      std::find(FeaturesVec.begin(), FeaturesVec.end(), "-popcnt") ==
          FeaturesVec.end())
    Features["popcnt"] = true;

  // Enable prfchw if 3DNow! is enabled and prfchw is not explicitly disabled.
  I = Features.find("3dnow");
  if (I != Features.end() && I->getValue() &&
      std::find(FeaturesVec.begin(), FeaturesVec.end(), "-prfchw") ==
          FeaturesVec.end())
    Features["prfchw"] = true;

  // Additionally, if SSE is enabled and mmx is not explicitly disabled,
  // then enable MMX.
  I = Features.find("sse");
  if (I != Features.end() && I->getValue() &&
      std::find(FeaturesVec.begin(), FeaturesVec.end(), "-mmx") ==
          FeaturesVec.end())
    Features["mmx"] = true;

  return true;
}

void X86TargetInfo::setSSELevel(llvm::StringMap<bool> &Features,
                                X86SSEEnum Level, bool Enabled) {
  if (Enabled) {
    switch (Level) {
    case AVX512F:
      Features["avx512f"] = Features["fma"] = Features["f16c"] = true;
      LLVM_FALLTHROUGH;
    case AVX2:
      Features["avx2"] = true;
      LLVM_FALLTHROUGH;
    case AVX:
      Features["avx"] = true;
      Features["xsave"] = true;
      LLVM_FALLTHROUGH;
    case SSE42:
      Features["sse4.2"] = true;
      LLVM_FALLTHROUGH;
    case SSE41:
      Features["sse4.1"] = true;
      LLVM_FALLTHROUGH;
    case SSSE3:
      Features["ssse3"] = true;
      LLVM_FALLTHROUGH;
    case SSE3:
      Features["sse3"] = true;
      LLVM_FALLTHROUGH;
    case SSE2:
      Features["sse2"] = true;
      LLVM_FALLTHROUGH;
    case SSE1:
      Features["sse"] = true;
      LLVM_FALLTHROUGH;
    case NoSSE:
      break;
    }
    return;
  }

  switch (Level) {
  case NoSSE:
  case SSE1:
    Features["sse"] = false;
    LLVM_FALLTHROUGH;
  case SSE2:
    Features["sse2"] = Features["pclmul"] = Features["aes"] = Features["sha"] =
        Features["gfni"] = false;
    LLVM_FALLTHROUGH;
  case SSE3:
    Features["sse3"] = false;
    setXOPLevel(Features, NoXOP, false);
    LLVM_FALLTHROUGH;
  case SSSE3:
    Features["ssse3"] = false;
    LLVM_FALLTHROUGH;
  case SSE41:
    Features["sse4.1"] = false;
    LLVM_FALLTHROUGH;
  case SSE42:
    Features["sse4.2"] = false;
    LLVM_FALLTHROUGH;
  case AVX:
    Features["fma"] = Features["avx"] = Features["f16c"] = Features["xsave"] =
        Features["xsaveopt"] = Features["vaes"] = Features["vpclmulqdq"] = false;
    setXOPLevel(Features, FMA4, false);
    LLVM_FALLTHROUGH;
  case AVX2:
    Features["avx2"] = false;
    LLVM_FALLTHROUGH;
  case AVX512F:
    Features["avx512f"] = Features["avx512cd"] = Features["avx512er"] =
        Features["avx512pf"] = Features["avx512dq"] = Features["avx512bw"] =
            Features["avx512vl"] = Features["avx512vbmi"] =
                Features["avx512ifma"] = Features["avx512vpopcntdq"] =
                    Features["avx512bitalg"] = Features["avx512vnni"] =
                        Features["avx512vbmi2"] = false;
    break;
  }
}

void X86TargetInfo::setMMXLevel(llvm::StringMap<bool> &Features,
                                MMX3DNowEnum Level, bool Enabled) {
  if (Enabled) {
    switch (Level) {
    case AMD3DNowAthlon:
      Features["3dnowa"] = true;
      LLVM_FALLTHROUGH;
    case AMD3DNow:
      Features["3dnow"] = true;
      LLVM_FALLTHROUGH;
    case MMX:
      Features["mmx"] = true;
      LLVM_FALLTHROUGH;
    case NoMMX3DNow:
      break;
    }
    return;
  }

  switch (Level) {
  case NoMMX3DNow:
  case MMX:
    Features["mmx"] = false;
    LLVM_FALLTHROUGH;
  case AMD3DNow:
    Features["3dnow"] = false;
    LLVM_FALLTHROUGH;
  case AMD3DNowAthlon:
    Features["3dnowa"] = false;
    break;
  }
}

void X86TargetInfo::setXOPLevel(llvm::StringMap<bool> &Features, XOPEnum Level,
                                bool Enabled) {
  if (Enabled) {
    switch (Level) {
    case XOP:
      Features["xop"] = true;
      LLVM_FALLTHROUGH;
    case FMA4:
      Features["fma4"] = true;
      setSSELevel(Features, AVX, true);
      LLVM_FALLTHROUGH;
    case SSE4A:
      Features["sse4a"] = true;
      setSSELevel(Features, SSE3, true);
      LLVM_FALLTHROUGH;
    case NoXOP:
      break;
    }
    return;
  }

  switch (Level) {
  case NoXOP:
  case SSE4A:
    Features["sse4a"] = false;
    LLVM_FALLTHROUGH;
  case FMA4:
    Features["fma4"] = false;
    LLVM_FALLTHROUGH;
  case XOP:
    Features["xop"] = false;
    break;
  }
}

void X86TargetInfo::setFeatureEnabledImpl(llvm::StringMap<bool> &Features,
                                          StringRef Name, bool Enabled) {
  // This is a bit of a hack to deal with the sse4 target feature when used
  // as part of the target attribute. We handle sse4 correctly everywhere
  // else. See below for more information on how we handle the sse4 options.
  if (Name != "sse4")
    Features[Name] = Enabled;

  if (Name == "mmx") {
    setMMXLevel(Features, MMX, Enabled);
  } else if (Name == "sse") {
    setSSELevel(Features, SSE1, Enabled);
  } else if (Name == "sse2") {
    setSSELevel(Features, SSE2, Enabled);
  } else if (Name == "sse3") {
    setSSELevel(Features, SSE3, Enabled);
  } else if (Name == "ssse3") {
    setSSELevel(Features, SSSE3, Enabled);
  } else if (Name == "sse4.2") {
    setSSELevel(Features, SSE42, Enabled);
  } else if (Name == "sse4.1") {
    setSSELevel(Features, SSE41, Enabled);
  } else if (Name == "3dnow") {
    setMMXLevel(Features, AMD3DNow, Enabled);
  } else if (Name == "3dnowa") {
    setMMXLevel(Features, AMD3DNowAthlon, Enabled);
  } else if (Name == "aes") {
    if (Enabled)
      setSSELevel(Features, SSE2, Enabled);
    else
      Features["vaes"] = false;
  } else if (Name == "vaes") {
    if (Enabled) {
      setSSELevel(Features, AVX, Enabled);
      Features["aes"] = true;
    }
  } else if (Name == "pclmul") {
    if (Enabled)
      setSSELevel(Features, SSE2, Enabled);
    else
      Features["vpclmulqdq"] = false;
  } else if (Name == "vpclmulqdq") {
    if (Enabled) {
      setSSELevel(Features, AVX, Enabled);
      Features["pclmul"] = true;
    }
  } else if (Name == "gfni") {
     if (Enabled)
      setSSELevel(Features, SSE2, Enabled);
  } else if (Name == "avx") {
    setSSELevel(Features, AVX, Enabled);
  } else if (Name == "avx2") {
    setSSELevel(Features, AVX2, Enabled);
  } else if (Name == "avx512f") {
    setSSELevel(Features, AVX512F, Enabled);
  } else if (Name == "avx512cd" || Name == "avx512er" || Name == "avx512pf" ||
             Name == "avx512dq" || Name == "avx512bw" || Name == "avx512vl" ||
             Name == "avx512vbmi" || Name == "avx512ifma" ||
             Name == "avx512vpopcntdq" || Name == "avx512bitalg" ||
             Name == "avx512vnni" || Name == "avx512vbmi2") {
    if (Enabled)
      setSSELevel(Features, AVX512F, Enabled);
    // Enable BWI instruction if VBMI/VBMI2/BITALG is being enabled.
    if ((Name.startswith("avx512vbmi") || Name == "avx512bitalg") && Enabled)
      Features["avx512bw"] = true;
    // Also disable VBMI/VBMI2/BITALG if BWI is being disabled.
    if (Name == "avx512bw" && !Enabled)
      Features["avx512vbmi"] = Features["avx512vbmi2"] =
      Features["avx512bitalg"] = false;
  } else if (Name == "fma") {
    if (Enabled)
      setSSELevel(Features, AVX, Enabled);
    else
      setSSELevel(Features, AVX512F, Enabled);
  } else if (Name == "fma4") {
    setXOPLevel(Features, FMA4, Enabled);
  } else if (Name == "xop") {
    setXOPLevel(Features, XOP, Enabled);
  } else if (Name == "sse4a") {
    setXOPLevel(Features, SSE4A, Enabled);
  } else if (Name == "f16c") {
    if (Enabled)
      setSSELevel(Features, AVX, Enabled);
    else
      setSSELevel(Features, AVX512F, Enabled);
  } else if (Name == "sha") {
    if (Enabled)
      setSSELevel(Features, SSE2, Enabled);
  } else if (Name == "sse4") {
    // We can get here via the __target__ attribute since that's not controlled
    // via the -msse4/-mno-sse4 command line alias. Handle this the same way
    // here - turn on the sse4.2 if enabled, turn off the sse4.1 level if
    // disabled.
    if (Enabled)
      setSSELevel(Features, SSE42, Enabled);
    else
      setSSELevel(Features, SSE41, Enabled);
  } else if (Name == "xsave") {
    if (!Enabled)
      Features["xsaveopt"] = false;
  } else if (Name == "xsaveopt" || Name == "xsavec" || Name == "xsaves") {
    if (Enabled)
      Features["xsave"] = true;
  }
}

/// handleTargetFeatures - Perform initialization based on the user
/// configured set of features.
bool X86TargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                         DiagnosticsEngine &Diags) {
  for (const auto &Feature : Features) {
    if (Feature[0] != '+')
      continue;

    if (Feature == "+aes") {
      HasAES = true;
    } else if (Feature == "+vaes") {
      HasVAES = true;
    } else if (Feature == "+pclmul") {
      HasPCLMUL = true;
    } else if (Feature == "+vpclmulqdq") {
      HasVPCLMULQDQ = true;
    } else if (Feature == "+lzcnt") {
      HasLZCNT = true;
    } else if (Feature == "+rdrnd") {
      HasRDRND = true;
    } else if (Feature == "+fsgsbase") {
      HasFSGSBASE = true;
    } else if (Feature == "+bmi") {
      HasBMI = true;
    } else if (Feature == "+bmi2") {
      HasBMI2 = true;
    } else if (Feature == "+popcnt") {
      HasPOPCNT = true;
    } else if (Feature == "+rtm") {
      HasRTM = true;
    } else if (Feature == "+prfchw") {
      HasPRFCHW = true;
    } else if (Feature == "+rdseed") {
      HasRDSEED = true;
    } else if (Feature == "+adx") {
      HasADX = true;
    } else if (Feature == "+tbm") {
      HasTBM = true;
    } else if (Feature == "+lwp") {
      HasLWP = true;
    } else if (Feature == "+fma") {
      HasFMA = true;
    } else if (Feature == "+f16c") {
      HasF16C = true;
    } else if (Feature == "+gfni") {
      HasGFNI = true;
    } else if (Feature == "+avx512cd") {
      HasAVX512CD = true;
    } else if (Feature == "+avx512vpopcntdq") {
      HasAVX512VPOPCNTDQ = true;
    } else if (Feature == "+avx512vnni") {
      HasAVX512VNNI = true;
    } else if (Feature == "+avx512er") {
      HasAVX512ER = true;
    } else if (Feature == "+avx512pf") {
      HasAVX512PF = true;
    } else if (Feature == "+avx512dq") {
      HasAVX512DQ = true;
    } else if (Feature == "+avx512bitalg") {
      HasAVX512BITALG = true;
    } else if (Feature == "+avx512bw") {
      HasAVX512BW = true;
    } else if (Feature == "+avx512vl") {
      HasAVX512VL = true;
    } else if (Feature == "+avx512vbmi") {
      HasAVX512VBMI = true;
    } else if (Feature == "+avx512vbmi2") {
      HasAVX512VBMI2 = true;
    } else if (Feature == "+avx512ifma") {
      HasAVX512IFMA = true;
    } else if (Feature == "+sha") {
      HasSHA = true;
    } else if (Feature == "+mpx") {
      HasMPX = true;
    } else if (Feature == "+shstk") {
      HasSHSTK = true;
    } else if (Feature == "+movbe") {
      HasMOVBE = true;
    } else if (Feature == "+sgx") {
      HasSGX = true;
    } else if (Feature == "+cx16") {
      HasCX16 = true;
    } else if (Feature == "+fxsr") {
      HasFXSR = true;
    } else if (Feature == "+xsave") {
      HasXSAVE = true;
    } else if (Feature == "+xsaveopt") {
      HasXSAVEOPT = true;
    } else if (Feature == "+xsavec") {
      HasXSAVEC = true;
    } else if (Feature == "+xsaves") {
      HasXSAVES = true;
    } else if (Feature == "+mwaitx") {
      HasMWAITX = true;
    } else if (Feature == "+pku") {
      HasPKU = true;
    } else if (Feature == "+clflushopt") {
      HasCLFLUSHOPT = true;
    } else if (Feature == "+clwb") {
      HasCLWB = true;
    } else if (Feature == "+wbnoinvd") {
      HasWBNOINVD = true;
    } else if (Feature == "+prefetchwt1") {
      HasPREFETCHWT1 = true;
    } else if (Feature == "+clzero") {
      HasCLZERO = true;
    } else if (Feature == "+cldemote") {
      HasCLDEMOTE = true;
    } else if (Feature == "+rdpid") {
      HasRDPID = true;
    } else if (Feature == "+retpoline-external-thunk") {
      HasRetpolineExternalThunk = true;
    } else if (Feature == "+sahf") {
      HasLAHFSAHF = true;
    } else if (Feature == "+waitpkg") {
      HasWAITPKG = true;
    } else if (Feature == "+movdiri") {
      HasMOVDIRI = true;
    } else if (Feature == "+movdir64b") {
      HasMOVDIR64B = true;
    } else if (Feature == "+pconfig") {
      HasPCONFIG = true;
    } else if (Feature == "+ptwrite") {
      HasPTWRITE = true;
    } else if (Feature == "+invpcid") {
      HasINVPCID = true;
    }

    X86SSEEnum Level = llvm::StringSwitch<X86SSEEnum>(Feature)
                           .Case("+avx512f", AVX512F)
                           .Case("+avx2", AVX2)
                           .Case("+avx", AVX)
                           .Case("+sse4.2", SSE42)
                           .Case("+sse4.1", SSE41)
                           .Case("+ssse3", SSSE3)
                           .Case("+sse3", SSE3)
                           .Case("+sse2", SSE2)
                           .Case("+sse", SSE1)
                           .Default(NoSSE);
    SSELevel = std::max(SSELevel, Level);

    MMX3DNowEnum ThreeDNowLevel = llvm::StringSwitch<MMX3DNowEnum>(Feature)
                                      .Case("+3dnowa", AMD3DNowAthlon)
                                      .Case("+3dnow", AMD3DNow)
                                      .Case("+mmx", MMX)
                                      .Default(NoMMX3DNow);
    MMX3DNowLevel = std::max(MMX3DNowLevel, ThreeDNowLevel);

    XOPEnum XLevel = llvm::StringSwitch<XOPEnum>(Feature)
                         .Case("+xop", XOP)
                         .Case("+fma4", FMA4)
                         .Case("+sse4a", SSE4A)
                         .Default(NoXOP);
    XOPLevel = std::max(XOPLevel, XLevel);
  }

  // LLVM doesn't have a separate switch for fpmath, so only accept it if it
  // matches the selected sse level.
  if ((FPMath == FP_SSE && SSELevel < SSE1) ||
      (FPMath == FP_387 && SSELevel >= SSE1)) {
    Diags.Report(diag::err_target_unsupported_fpmath)
        << (FPMath == FP_SSE ? "sse" : "387");
    return false;
  }

  SimdDefaultAlign =
      hasFeature("avx512f") ? 512 : hasFeature("avx") ? 256 : 128;
  return true;
}

/// X86TargetInfo::getTargetDefines - Return the set of the X86-specific macro
/// definitions for this particular subtarget.
void X86TargetInfo::getTargetDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  std::string CodeModel = getTargetOpts().CodeModel;
  if (CodeModel == "default")
    CodeModel = "small";
  Builder.defineMacro("__code_model_" + CodeModel + "_");

  // Target identification.
  if (getTriple().getArch() == llvm::Triple::x86_64) {
    Builder.defineMacro("__amd64__");
    Builder.defineMacro("__amd64");
    Builder.defineMacro("__x86_64");
    Builder.defineMacro("__x86_64__");
    if (getTriple().getArchName() == "x86_64h") {
      Builder.defineMacro("__x86_64h");
      Builder.defineMacro("__x86_64h__");
    }
  } else {
    DefineStd(Builder, "i386", Opts);
  }

  // Subtarget options.
  // FIXME: We are hard-coding the tune parameters based on the CPU, but they
  // truly should be based on -mtune options.
  switch (CPU) {
  case CK_Generic:
    break;
  case CK_i386:
    // The rest are coming from the i386 define above.
    Builder.defineMacro("__tune_i386__");
    break;
  case CK_i486:
  case CK_WinChipC6:
  case CK_WinChip2:
  case CK_C3:
    defineCPUMacros(Builder, "i486");
    break;
  case CK_PentiumMMX:
    Builder.defineMacro("__pentium_mmx__");
    Builder.defineMacro("__tune_pentium_mmx__");
    LLVM_FALLTHROUGH;
  case CK_i586:
  case CK_Pentium:
    defineCPUMacros(Builder, "i586");
    defineCPUMacros(Builder, "pentium");
    break;
  case CK_Pentium3:
  case CK_PentiumM:
    Builder.defineMacro("__tune_pentium3__");
    LLVM_FALLTHROUGH;
  case CK_Pentium2:
  case CK_C3_2:
    Builder.defineMacro("__tune_pentium2__");
    LLVM_FALLTHROUGH;
  case CK_PentiumPro:
    defineCPUMacros(Builder, "i686");
    defineCPUMacros(Builder, "pentiumpro");
    break;
  case CK_Pentium4:
    defineCPUMacros(Builder, "pentium4");
    break;
  case CK_Yonah:
  case CK_Prescott:
  case CK_Nocona:
    defineCPUMacros(Builder, "nocona");
    break;
  case CK_Core2:
  case CK_Penryn:
    defineCPUMacros(Builder, "core2");
    break;
  case CK_Bonnell:
    defineCPUMacros(Builder, "atom");
    break;
  case CK_Silvermont:
    defineCPUMacros(Builder, "slm");
    break;
  case CK_Goldmont:
    defineCPUMacros(Builder, "goldmont");
    break;
  case CK_GoldmontPlus:
    defineCPUMacros(Builder, "goldmont_plus");
    break;
  case CK_Tremont:
    defineCPUMacros(Builder, "tremont");
    break;
  case CK_Nehalem:
  case CK_Westmere:
  case CK_SandyBridge:
  case CK_IvyBridge:
  case CK_Haswell:
  case CK_Broadwell:
  case CK_SkylakeClient:
  case CK_SkylakeServer:
  case CK_Cascadelake:
  case CK_Cannonlake:
  case CK_IcelakeClient:
  case CK_IcelakeServer:
    // FIXME: Historically, we defined this legacy name, it would be nice to
    // remove it at some point. We've never exposed fine-grained names for
    // recent primary x86 CPUs, and we should keep it that way.
    defineCPUMacros(Builder, "corei7");
    break;
  case CK_KNL:
    defineCPUMacros(Builder, "knl");
    break;
  case CK_KNM:
    break;
  case CK_Lakemont:
    defineCPUMacros(Builder, "i586", /*Tuning*/false);
    defineCPUMacros(Builder, "pentium", /*Tuning*/false);
    Builder.defineMacro("__tune_lakemont__");
    break;
  case CK_K6_2:
    Builder.defineMacro("__k6_2__");
    Builder.defineMacro("__tune_k6_2__");
    LLVM_FALLTHROUGH;
  case CK_K6_3:
    if (CPU != CK_K6_2) { // In case of fallthrough
      // FIXME: GCC may be enabling these in cases where some other k6
      // architecture is specified but -m3dnow is explicitly provided. The
      // exact semantics need to be determined and emulated here.
      Builder.defineMacro("__k6_3__");
      Builder.defineMacro("__tune_k6_3__");
    }
    LLVM_FALLTHROUGH;
  case CK_K6:
    defineCPUMacros(Builder, "k6");
    break;
  case CK_Athlon:
  case CK_AthlonXP:
    defineCPUMacros(Builder, "athlon");
    if (SSELevel != NoSSE) {
      Builder.defineMacro("__athlon_sse__");
      Builder.defineMacro("__tune_athlon_sse__");
    }
    break;
  case CK_K8:
  case CK_K8SSE3:
  case CK_x86_64:
    defineCPUMacros(Builder, "k8");
    break;
  case CK_AMDFAM10:
    defineCPUMacros(Builder, "amdfam10");
    break;
  case CK_BTVER1:
    defineCPUMacros(Builder, "btver1");
    break;
  case CK_BTVER2:
    defineCPUMacros(Builder, "btver2");
    break;
  case CK_BDVER1:
    defineCPUMacros(Builder, "bdver1");
    break;
  case CK_BDVER2:
    defineCPUMacros(Builder, "bdver2");
    break;
  case CK_BDVER3:
    defineCPUMacros(Builder, "bdver3");
    break;
  case CK_BDVER4:
    defineCPUMacros(Builder, "bdver4");
    break;
  case CK_ZNVER1:
    defineCPUMacros(Builder, "znver1");
    break;
  case CK_Geode:
    defineCPUMacros(Builder, "geode");
    break;
  }

  // Target properties.
  Builder.defineMacro("__REGISTER_PREFIX__", "");

  // Define __NO_MATH_INLINES on linux/x86 so that we don't get inline
  // functions in glibc header files that use FP Stack inline asm which the
  // backend can't deal with (PR879).
  Builder.defineMacro("__NO_MATH_INLINES");

  if (HasAES)
    Builder.defineMacro("__AES__");

  if (HasVAES)
    Builder.defineMacro("__VAES__");

  if (HasPCLMUL)
    Builder.defineMacro("__PCLMUL__");

  if (HasVPCLMULQDQ)
    Builder.defineMacro("__VPCLMULQDQ__");

  if (HasLZCNT)
    Builder.defineMacro("__LZCNT__");

  if (HasRDRND)
    Builder.defineMacro("__RDRND__");

  if (HasFSGSBASE)
    Builder.defineMacro("__FSGSBASE__");

  if (HasBMI)
    Builder.defineMacro("__BMI__");

  if (HasBMI2)
    Builder.defineMacro("__BMI2__");

  if (HasPOPCNT)
    Builder.defineMacro("__POPCNT__");

  if (HasRTM)
    Builder.defineMacro("__RTM__");

  if (HasPRFCHW)
    Builder.defineMacro("__PRFCHW__");

  if (HasRDSEED)
    Builder.defineMacro("__RDSEED__");

  if (HasADX)
    Builder.defineMacro("__ADX__");

  if (HasTBM)
    Builder.defineMacro("__TBM__");

  if (HasLWP)
    Builder.defineMacro("__LWP__");

  if (HasMWAITX)
    Builder.defineMacro("__MWAITX__");

  if (HasMOVBE)
    Builder.defineMacro("__MOVBE__");

  switch (XOPLevel) {
  case XOP:
    Builder.defineMacro("__XOP__");
    LLVM_FALLTHROUGH;
  case FMA4:
    Builder.defineMacro("__FMA4__");
    LLVM_FALLTHROUGH;
  case SSE4A:
    Builder.defineMacro("__SSE4A__");
    LLVM_FALLTHROUGH;
  case NoXOP:
    break;
  }

  if (HasFMA)
    Builder.defineMacro("__FMA__");

  if (HasF16C)
    Builder.defineMacro("__F16C__");

  if (HasGFNI)
    Builder.defineMacro("__GFNI__");

  if (HasAVX512CD)
    Builder.defineMacro("__AVX512CD__");
  if (HasAVX512VPOPCNTDQ)
    Builder.defineMacro("__AVX512VPOPCNTDQ__");
  if (HasAVX512VNNI)
    Builder.defineMacro("__AVX512VNNI__");
  if (HasAVX512ER)
    Builder.defineMacro("__AVX512ER__");
  if (HasAVX512PF)
    Builder.defineMacro("__AVX512PF__");
  if (HasAVX512DQ)
    Builder.defineMacro("__AVX512DQ__");
  if (HasAVX512BITALG)
    Builder.defineMacro("__AVX512BITALG__");
  if (HasAVX512BW)
    Builder.defineMacro("__AVX512BW__");
  if (HasAVX512VL)
    Builder.defineMacro("__AVX512VL__");
  if (HasAVX512VBMI)
    Builder.defineMacro("__AVX512VBMI__");
  if (HasAVX512VBMI2)
    Builder.defineMacro("__AVX512VBMI2__");
  if (HasAVX512IFMA)
    Builder.defineMacro("__AVX512IFMA__");

  if (HasSHA)
    Builder.defineMacro("__SHA__");

  if (HasFXSR)
    Builder.defineMacro("__FXSR__");
  if (HasXSAVE)
    Builder.defineMacro("__XSAVE__");
  if (HasXSAVEOPT)
    Builder.defineMacro("__XSAVEOPT__");
  if (HasXSAVEC)
    Builder.defineMacro("__XSAVEC__");
  if (HasXSAVES)
    Builder.defineMacro("__XSAVES__");
  if (HasPKU)
    Builder.defineMacro("__PKU__");
  if (HasCLFLUSHOPT)
    Builder.defineMacro("__CLFLUSHOPT__");
  if (HasCLWB)
    Builder.defineMacro("__CLWB__");
  if (HasWBNOINVD)
    Builder.defineMacro("__WBNOINVD__");
  if (HasMPX)
    Builder.defineMacro("__MPX__");
  if (HasSHSTK)
    Builder.defineMacro("__SHSTK__");
  if (HasSGX)
    Builder.defineMacro("__SGX__");
  if (HasPREFETCHWT1)
    Builder.defineMacro("__PREFETCHWT1__");
  if (HasCLZERO)
    Builder.defineMacro("__CLZERO__");
  if (HasRDPID)
    Builder.defineMacro("__RDPID__");
  if (HasCLDEMOTE)
    Builder.defineMacro("__CLDEMOTE__");
  if (HasWAITPKG)
    Builder.defineMacro("__WAITPKG__");
  if (HasMOVDIRI)
    Builder.defineMacro("__MOVDIRI__");
  if (HasMOVDIR64B)
    Builder.defineMacro("__MOVDIR64B__");
  if (HasPCONFIG)
    Builder.defineMacro("__PCONFIG__");
  if (HasPTWRITE)
    Builder.defineMacro("__PTWRITE__");
  if (HasINVPCID)
    Builder.defineMacro("__INVPCID__");

  // Each case falls through to the previous one here.
  switch (SSELevel) {
  case AVX512F:
    Builder.defineMacro("__AVX512F__");
    LLVM_FALLTHROUGH;
  case AVX2:
    Builder.defineMacro("__AVX2__");
    LLVM_FALLTHROUGH;
  case AVX:
    Builder.defineMacro("__AVX__");
    LLVM_FALLTHROUGH;
  case SSE42:
    Builder.defineMacro("__SSE4_2__");
    LLVM_FALLTHROUGH;
  case SSE41:
    Builder.defineMacro("__SSE4_1__");
    LLVM_FALLTHROUGH;
  case SSSE3:
    Builder.defineMacro("__SSSE3__");
    LLVM_FALLTHROUGH;
  case SSE3:
    Builder.defineMacro("__SSE3__");
    LLVM_FALLTHROUGH;
  case SSE2:
    Builder.defineMacro("__SSE2__");
    Builder.defineMacro("__SSE2_MATH__"); // -mfp-math=sse always implied.
    LLVM_FALLTHROUGH;
  case SSE1:
    Builder.defineMacro("__SSE__");
    Builder.defineMacro("__SSE_MATH__"); // -mfp-math=sse always implied.
    LLVM_FALLTHROUGH;
  case NoSSE:
    break;
  }

  if (Opts.MicrosoftExt && getTriple().getArch() == llvm::Triple::x86) {
    switch (SSELevel) {
    case AVX512F:
    case AVX2:
    case AVX:
    case SSE42:
    case SSE41:
    case SSSE3:
    case SSE3:
    case SSE2:
      Builder.defineMacro("_M_IX86_FP", Twine(2));
      break;
    case SSE1:
      Builder.defineMacro("_M_IX86_FP", Twine(1));
      break;
    default:
      Builder.defineMacro("_M_IX86_FP", Twine(0));
      break;
    }
  }

  // Each case falls through to the previous one here.
  switch (MMX3DNowLevel) {
  case AMD3DNowAthlon:
    Builder.defineMacro("__3dNOW_A__");
    LLVM_FALLTHROUGH;
  case AMD3DNow:
    Builder.defineMacro("__3dNOW__");
    LLVM_FALLTHROUGH;
  case MMX:
    Builder.defineMacro("__MMX__");
    LLVM_FALLTHROUGH;
  case NoMMX3DNow:
    break;
  }

  if (CPU >= CK_i486) {
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
  }
  if (CPU >= CK_i586)
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
  if (HasCX16)
    Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16");

  if (HasFloat128)
    Builder.defineMacro("__SIZEOF_FLOAT128__", "16");
}

bool X86TargetInfo::isValidFeatureName(StringRef Name) const {
  return llvm::StringSwitch<bool>(Name)
      .Case("3dnow", true)
      .Case("3dnowa", true)
      .Case("adx", true)
      .Case("aes", true)
      .Case("avx", true)
      .Case("avx2", true)
      .Case("avx512f", true)
      .Case("avx512cd", true)
      .Case("avx512vpopcntdq", true)
      .Case("avx512vnni", true)
      .Case("avx512er", true)
      .Case("avx512pf", true)
      .Case("avx512dq", true)
      .Case("avx512bitalg", true)
      .Case("avx512bw", true)
      .Case("avx512vl", true)
      .Case("avx512vbmi", true)
      .Case("avx512vbmi2", true)
      .Case("avx512ifma", true)
      .Case("bmi", true)
      .Case("bmi2", true)
      .Case("cldemote", true)
      .Case("clflushopt", true)
      .Case("clwb", true)
      .Case("clzero", true)
      .Case("cx16", true)
      .Case("f16c", true)
      .Case("fma", true)
      .Case("fma4", true)
      .Case("fsgsbase", true)
      .Case("fxsr", true)
      .Case("gfni", true)
      .Case("invpcid", true)
      .Case("lwp", true)
      .Case("lzcnt", true)
      .Case("mmx", true)
      .Case("movbe", true)
      .Case("movdiri", true)
      .Case("movdir64b", true)
      .Case("mpx", true)
      .Case("mwaitx", true)
      .Case("pclmul", true)
      .Case("pconfig", true)
      .Case("pku", true)
      .Case("popcnt", true)
      .Case("prefetchwt1", true)
      .Case("prfchw", true)
      .Case("ptwrite", true)
      .Case("rdpid", true)
      .Case("rdrnd", true)
      .Case("rdseed", true)
      .Case("rtm", true)
      .Case("sahf", true)
      .Case("sgx", true)
      .Case("sha", true)
      .Case("shstk", true)
      .Case("sse", true)
      .Case("sse2", true)
      .Case("sse3", true)
      .Case("ssse3", true)
      .Case("sse4", true)
      .Case("sse4.1", true)
      .Case("sse4.2", true)
      .Case("sse4a", true)
      .Case("tbm", true)
      .Case("vaes", true)
      .Case("vpclmulqdq", true)
      .Case("wbnoinvd", true)
      .Case("waitpkg", true)
      .Case("x87", true)
      .Case("xop", true)
      .Case("xsave", true)
      .Case("xsavec", true)
      .Case("xsaves", true)
      .Case("xsaveopt", true)
      .Default(false);
}

bool X86TargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("adx", HasADX)
      .Case("aes", HasAES)
      .Case("avx", SSELevel >= AVX)
      .Case("avx2", SSELevel >= AVX2)
      .Case("avx512f", SSELevel >= AVX512F)
      .Case("avx512cd", HasAVX512CD)
      .Case("avx512vpopcntdq", HasAVX512VPOPCNTDQ)
      .Case("avx512vnni", HasAVX512VNNI)
      .Case("avx512er", HasAVX512ER)
      .Case("avx512pf", HasAVX512PF)
      .Case("avx512dq", HasAVX512DQ)
      .Case("avx512bitalg", HasAVX512BITALG)
      .Case("avx512bw", HasAVX512BW)
      .Case("avx512vl", HasAVX512VL)
      .Case("avx512vbmi", HasAVX512VBMI)
      .Case("avx512vbmi2", HasAVX512VBMI2)
      .Case("avx512ifma", HasAVX512IFMA)
      .Case("bmi", HasBMI)
      .Case("bmi2", HasBMI2)
      .Case("cldemote", HasCLDEMOTE)
      .Case("clflushopt", HasCLFLUSHOPT)
      .Case("clwb", HasCLWB)
      .Case("clzero", HasCLZERO)
      .Case("cx16", HasCX16)
      .Case("f16c", HasF16C)
      .Case("fma", HasFMA)
      .Case("fma4", XOPLevel >= FMA4)
      .Case("fsgsbase", HasFSGSBASE)
      .Case("fxsr", HasFXSR)
      .Case("gfni", HasGFNI)
      .Case("invpcid", HasINVPCID)
      .Case("lwp", HasLWP)
      .Case("lzcnt", HasLZCNT)
      .Case("mm3dnow", MMX3DNowLevel >= AMD3DNow)
      .Case("mm3dnowa", MMX3DNowLevel >= AMD3DNowAthlon)
      .Case("mmx", MMX3DNowLevel >= MMX)
      .Case("movbe", HasMOVBE)
      .Case("movdiri", HasMOVDIRI)
      .Case("movdir64b", HasMOVDIR64B)
      .Case("mpx", HasMPX)
      .Case("mwaitx", HasMWAITX)
      .Case("pclmul", HasPCLMUL)
      .Case("pconfig", HasPCONFIG)
      .Case("pku", HasPKU)
      .Case("popcnt", HasPOPCNT)
      .Case("prefetchwt1", HasPREFETCHWT1)
      .Case("prfchw", HasPRFCHW)
      .Case("ptwrite", HasPTWRITE)
      .Case("rdpid", HasRDPID)
      .Case("rdrnd", HasRDRND)
      .Case("rdseed", HasRDSEED)
      .Case("retpoline-external-thunk", HasRetpolineExternalThunk)
      .Case("rtm", HasRTM)
      .Case("sahf", HasLAHFSAHF)
      .Case("sgx", HasSGX)
      .Case("sha", HasSHA)
      .Case("shstk", HasSHSTK)
      .Case("sse", SSELevel >= SSE1)
      .Case("sse2", SSELevel >= SSE2)
      .Case("sse3", SSELevel >= SSE3)
      .Case("ssse3", SSELevel >= SSSE3)
      .Case("sse4.1", SSELevel >= SSE41)
      .Case("sse4.2", SSELevel >= SSE42)
      .Case("sse4a", XOPLevel >= SSE4A)
      .Case("tbm", HasTBM)
      .Case("vaes", HasVAES)
      .Case("vpclmulqdq", HasVPCLMULQDQ)
      .Case("wbnoinvd", HasWBNOINVD)
      .Case("waitpkg", HasWAITPKG)
      .Case("x86", true)
      .Case("x86_32", getTriple().getArch() == llvm::Triple::x86)
      .Case("x86_64", getTriple().getArch() == llvm::Triple::x86_64)
      .Case("xop", XOPLevel >= XOP)
      .Case("xsave", HasXSAVE)
      .Case("xsavec", HasXSAVEC)
      .Case("xsaves", HasXSAVES)
      .Case("xsaveopt", HasXSAVEOPT)
      .Default(false);
}

// We can't use a generic validation scheme for the features accepted here
// versus subtarget features accepted in the target attribute because the
// bitfield structure that's initialized in the runtime only supports the
// below currently rather than the full range of subtarget features. (See
// X86TargetInfo::hasFeature for a somewhat comprehensive list).
bool X86TargetInfo::validateCpuSupports(StringRef FeatureStr) const {
  return llvm::StringSwitch<bool>(FeatureStr)
#define X86_FEATURE_COMPAT(VAL, ENUM, STR) .Case(STR, true)
#include "llvm/Support/X86TargetParser.def"
      .Default(false);
}

static llvm::X86::ProcessorFeatures getFeature(StringRef Name) {
  return llvm::StringSwitch<llvm::X86::ProcessorFeatures>(Name)
#define X86_FEATURE_COMPAT(VAL, ENUM, STR) .Case(STR, llvm::X86::ENUM)
#include "llvm/Support/X86TargetParser.def"
      ;
  // Note, this function should only be used after ensuring the value is
  // correct, so it asserts if the value is out of range.
}

static unsigned getFeaturePriority(llvm::X86::ProcessorFeatures Feat) {
  enum class FeatPriority {
#define FEATURE(FEAT) FEAT,
#include "clang/Basic/X86Target.def"
  };
  switch (Feat) {
#define FEATURE(FEAT)                                                          \
  case llvm::X86::FEAT:                                                        \
    return static_cast<unsigned>(FeatPriority::FEAT);
#include "clang/Basic/X86Target.def"
  default:
    llvm_unreachable("No Feature Priority for non-CPUSupports Features");
  }
}

unsigned X86TargetInfo::multiVersionSortPriority(StringRef Name) const {
  // Valid CPUs have a 'key feature' that compares just better than its key
  // feature.
  CPUKind Kind = getCPUKind(Name);
  if (Kind != CK_Generic) {
    switch (Kind) {
    default:
      llvm_unreachable(
          "CPU Type without a key feature used in 'target' attribute");
#define PROC_WITH_FEAT(ENUM, STR, IS64, KEY_FEAT)                              \
  case CK_##ENUM:                                                              \
    return (getFeaturePriority(llvm::X86::KEY_FEAT) << 1) + 1;
#include "clang/Basic/X86Target.def"
    }
  }

  // Now we know we have a feature, so get its priority and shift it a few so
  // that we have sufficient room for the CPUs (above).
  return getFeaturePriority(getFeature(Name)) << 1;
}

bool X86TargetInfo::validateCPUSpecificCPUDispatch(StringRef Name) const {
  return llvm::StringSwitch<bool>(Name)
#define CPU_SPECIFIC(NAME, MANGLING, FEATURES) .Case(NAME, true)
#define CPU_SPECIFIC_ALIAS(NEW_NAME, NAME) .Case(NEW_NAME, true)
#include "clang/Basic/X86Target.def"
      .Default(false);
}

static StringRef CPUSpecificCPUDispatchNameDealias(StringRef Name) {
  return llvm::StringSwitch<StringRef>(Name)
#define CPU_SPECIFIC_ALIAS(NEW_NAME, NAME) .Case(NEW_NAME, NAME)
#include "clang/Basic/X86Target.def"
      .Default(Name);
}

char X86TargetInfo::CPUSpecificManglingCharacter(StringRef Name) const {
  return llvm::StringSwitch<char>(CPUSpecificCPUDispatchNameDealias(Name))
#define CPU_SPECIFIC(NAME, MANGLING, FEATURES) .Case(NAME, MANGLING)
#include "clang/Basic/X86Target.def"
      .Default(0);
}

void X86TargetInfo::getCPUSpecificCPUDispatchFeatures(
    StringRef Name, llvm::SmallVectorImpl<StringRef> &Features) const {
  StringRef WholeList =
      llvm::StringSwitch<StringRef>(CPUSpecificCPUDispatchNameDealias(Name))
#define CPU_SPECIFIC(NAME, MANGLING, FEATURES) .Case(NAME, FEATURES)
#include "clang/Basic/X86Target.def"
          .Default("");
  WholeList.split(Features, ',', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
}

std::string X86TargetInfo::getCPUKindCanonicalName(CPUKind Kind) const {
  switch (Kind) {
  case CK_Generic:
    return "";
#define PROC(ENUM, STRING, IS64BIT)                                            \
  case CK_##ENUM:                                                              \
    return STRING;
#include "clang/Basic/X86Target.def"
  }
  llvm_unreachable("Invalid CPUKind");
}

// We can't use a generic validation scheme for the cpus accepted here
// versus subtarget cpus accepted in the target attribute because the
// variables intitialized by the runtime only support the below currently
// rather than the full range of cpus.
bool X86TargetInfo::validateCpuIs(StringRef FeatureStr) const {
  return llvm::StringSwitch<bool>(FeatureStr)
#define X86_VENDOR(ENUM, STRING) .Case(STRING, true)
#define X86_CPU_TYPE_COMPAT_WITH_ALIAS(ARCHNAME, ENUM, STR, ALIAS)             \
  .Cases(STR, ALIAS, true)
#define X86_CPU_TYPE_COMPAT(ARCHNAME, ENUM, STR) .Case(STR, true)
#define X86_CPU_SUBTYPE_COMPAT(ARCHNAME, ENUM, STR) .Case(STR, true)
#include "llvm/Support/X86TargetParser.def"
      .Default(false);
}

bool X86TargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  switch (*Name) {
  default:
    return false;
  // Constant constraints.
  case 'e': // 32-bit signed integer constant for use with sign-extending x86_64
            // instructions.
  case 'Z': // 32-bit unsigned integer constant for use with zero-extending
            // x86_64 instructions.
  case 's':
    Info.setRequiresImmediate();
    return true;
  case 'I':
    Info.setRequiresImmediate(0, 31);
    return true;
  case 'J':
    Info.setRequiresImmediate(0, 63);
    return true;
  case 'K':
    Info.setRequiresImmediate(-128, 127);
    return true;
  case 'L':
    Info.setRequiresImmediate({int(0xff), int(0xffff), int(0xffffffff)});
    return true;
  case 'M':
    Info.setRequiresImmediate(0, 3);
    return true;
  case 'N':
    Info.setRequiresImmediate(0, 255);
    return true;
  case 'O':
    Info.setRequiresImmediate(0, 127);
    return true;
  // Register constraints.
  case 'Y': // 'Y' is the first character for several 2-character constraints.
    // Shift the pointer to the second character of the constraint.
    Name++;
    switch (*Name) {
    default:
      return false;
    case 'z':
    case '0': // First SSE register.
    case '2':
    case 't': // Any SSE register, when SSE2 is enabled.
    case 'i': // Any SSE register, when SSE2 and inter-unit moves enabled.
    case 'm': // Any MMX register, when inter-unit moves enabled.
    case 'k': // AVX512 arch mask registers: k1-k7.
      Info.setAllowsRegister();
      return true;
    }
  case 'f': // Any x87 floating point stack register.
    // Constraint 'f' cannot be used for output operands.
    if (Info.ConstraintStr[0] == '=')
      return false;
    Info.setAllowsRegister();
    return true;
  case 'a': // eax.
  case 'b': // ebx.
  case 'c': // ecx.
  case 'd': // edx.
  case 'S': // esi.
  case 'D': // edi.
  case 'A': // edx:eax.
  case 't': // Top of floating point stack.
  case 'u': // Second from top of floating point stack.
  case 'q': // Any register accessible as [r]l: a, b, c, and d.
  case 'y': // Any MMX register.
  case 'v': // Any {X,Y,Z}MM register (Arch & context dependent)
  case 'x': // Any SSE register.
  case 'k': // Any AVX512 mask register (same as Yk, additionally allows k0
            // for intermideate k reg operations).
  case 'Q': // Any register accessible as [r]h: a, b, c, and d.
  case 'R': // "Legacy" registers: ax, bx, cx, dx, di, si, sp, bp.
  case 'l': // "Index" registers: any general register that can be used as an
            // index in a base+index memory access.
    Info.setAllowsRegister();
    return true;
  // Floating point constant constraints.
  case 'C': // SSE floating point constant.
  case 'G': // x87 floating point constant.
    return true;
  }
}

bool X86TargetInfo::validateOutputSize(StringRef Constraint,
                                       unsigned Size) const {
  // Strip off constraint modifiers.
  while (Constraint[0] == '=' || Constraint[0] == '+' || Constraint[0] == '&')
    Constraint = Constraint.substr(1);

  return validateOperandSize(Constraint, Size);
}

bool X86TargetInfo::validateInputSize(StringRef Constraint,
                                      unsigned Size) const {
  return validateOperandSize(Constraint, Size);
}

bool X86TargetInfo::validateOperandSize(StringRef Constraint,
                                        unsigned Size) const {
  switch (Constraint[0]) {
  default:
    break;
  case 'k':
  // Registers k0-k7 (AVX512) size limit is 64 bit.
  case 'y':
    return Size <= 64;
  case 'f':
  case 't':
  case 'u':
    return Size <= 128;
  case 'Y':
    // 'Y' is the first character for several 2-character constraints.
    switch (Constraint[1]) {
    default:
      return false;
    case 'm':
      // 'Ym' is synonymous with 'y'.
    case 'k':
      return Size <= 64;
    case 'z':
    case '0':
      // XMM0
      if (SSELevel >= SSE1)
        return Size <= 128U;
      return false;
    case 'i':
    case 't':
    case '2':
      // 'Yi','Yt','Y2' are synonymous with 'x' when SSE2 is enabled.
      if (SSELevel < SSE2)
        return false;
      break;
    }
    LLVM_FALLTHROUGH;
  case 'v':
  case 'x':
    if (SSELevel >= AVX512F)
      // 512-bit zmm registers can be used if target supports AVX512F.
      return Size <= 512U;
    else if (SSELevel >= AVX)
      // 256-bit ymm registers can be used if target supports AVX.
      return Size <= 256U;
    return Size <= 128U;

  }

  return true;
}

std::string X86TargetInfo::convertConstraint(const char *&Constraint) const {
  switch (*Constraint) {
  case 'a':
    return std::string("{ax}");
  case 'b':
    return std::string("{bx}");
  case 'c':
    return std::string("{cx}");
  case 'd':
    return std::string("{dx}");
  case 'S':
    return std::string("{si}");
  case 'D':
    return std::string("{di}");
  case 'p': // address
    return std::string("im");
  case 't': // top of floating point stack.
    return std::string("{st}");
  case 'u':                        // second from top of floating point stack.
    return std::string("{st(1)}"); // second from top of floating point stack.
  case 'Y':
    switch (Constraint[1]) {
    default:
      // Break from inner switch and fall through (copy single char),
      // continue parsing after copying the current constraint into
      // the return string.
      break;
    case 'k':
    case 'm':
    case 'i':
    case 't':
    case 'z':
    case '0':
    case '2':
      // "^" hints llvm that this is a 2 letter constraint.
      // "Constraint++" is used to promote the string iterator
      // to the next constraint.
      return std::string("^") + std::string(Constraint++, 2);
    }
    LLVM_FALLTHROUGH;
  default:
    return std::string(1, *Constraint);
  }
}

bool X86TargetInfo::checkCPUKind(CPUKind Kind) const {
  // Perform any per-CPU checks necessary to determine if this CPU is
  // acceptable.
  switch (Kind) {
  case CK_Generic:
    // No processor selected!
    return false;
#define PROC(ENUM, STRING, IS64BIT)                                            \
  case CK_##ENUM:                                                              \
    return IS64BIT || getTriple().getArch() == llvm::Triple::x86;
#include "clang/Basic/X86Target.def"
  }
  llvm_unreachable("Unhandled CPU kind");
}

void X86TargetInfo::fillValidCPUList(SmallVectorImpl<StringRef> &Values) const {
#define PROC(ENUM, STRING, IS64BIT)                                            \
  if (IS64BIT || getTriple().getArch() == llvm::Triple::x86)                   \
    Values.emplace_back(STRING);
  // Go through CPUKind checking to ensure that the alias is de-aliased and
  // 64 bit-ness is checked.
#define PROC_ALIAS(ENUM, ALIAS)                                                \
  if (checkCPUKind(getCPUKind(ALIAS)))                                         \
    Values.emplace_back(ALIAS);
#include "clang/Basic/X86Target.def"
}

X86TargetInfo::CPUKind X86TargetInfo::getCPUKind(StringRef CPU) const {
  return llvm::StringSwitch<CPUKind>(CPU)
#define PROC(ENUM, STRING, IS64BIT) .Case(STRING, CK_##ENUM)
#define PROC_ALIAS(ENUM, ALIAS) .Case(ALIAS, CK_##ENUM)
#include "clang/Basic/X86Target.def"
      .Default(CK_Generic);
}

ArrayRef<const char *> X86TargetInfo::getGCCRegNames() const {
  return llvm::makeArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::AddlRegName> X86TargetInfo::getGCCAddlRegNames() const {
  return llvm::makeArrayRef(AddlRegNames);
}

ArrayRef<Builtin::Info> X86_32TargetInfo::getTargetBuiltins() const {
  return llvm::makeArrayRef(BuiltinInfoX86, clang::X86::LastX86CommonBuiltin -
                                                Builtin::FirstTSBuiltin + 1);
}

ArrayRef<Builtin::Info> X86_64TargetInfo::getTargetBuiltins() const {
  return llvm::makeArrayRef(BuiltinInfoX86,
                            X86::LastTSBuiltin - Builtin::FirstTSBuiltin);
}
