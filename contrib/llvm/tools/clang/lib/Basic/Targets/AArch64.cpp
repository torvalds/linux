//===--- AArch64.cpp - Implement AArch64 target feature support -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements AArch64 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"

using namespace clang;
using namespace clang::targets;

const Builtin::Info AArch64TargetInfo::BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS)                                               \
   {#ID, TYPE, ATTRS, nullptr, ALL_LANGUAGES, nullptr},
#include "clang/Basic/BuiltinsNEON.def"

#define BUILTIN(ID, TYPE, ATTRS)                                               \
   {#ID, TYPE, ATTRS, nullptr, ALL_LANGUAGES, nullptr},
#define LANGBUILTIN(ID, TYPE, ATTRS, LANG)                                     \
  {#ID, TYPE, ATTRS, nullptr, LANG, nullptr},
#define TARGET_HEADER_BUILTIN(ID, TYPE, ATTRS, HEADER, LANGS, FEATURE)         \
  {#ID, TYPE, ATTRS, HEADER, LANGS, FEATURE},
#include "clang/Basic/BuiltinsAArch64.def"
};

AArch64TargetInfo::AArch64TargetInfo(const llvm::Triple &Triple,
                                     const TargetOptions &Opts)
    : TargetInfo(Triple), ABI("aapcs") {
  if (getTriple().isOSOpenBSD()) {
    Int64Type = SignedLongLong;
    IntMaxType = SignedLongLong;
  } else {
    if (!getTriple().isOSDarwin() && !getTriple().isOSNetBSD())
      WCharType = UnsignedInt;

    Int64Type = SignedLong;
    IntMaxType = SignedLong;
  }

  // All AArch64 implementations support ARMv8 FP, which makes half a legal type.
  HasLegalHalfType = true;
  HasFloat16 = true;

  LongWidth = LongAlign = PointerWidth = PointerAlign = 64;
  MaxVectorAlign = 128;
  MaxAtomicInlineWidth = 128;
  MaxAtomicPromoteWidth = 128;

  LongDoubleWidth = LongDoubleAlign = SuitableAlign = 128;
  LongDoubleFormat = &llvm::APFloat::IEEEquad();

  // Make __builtin_ms_va_list available.
  HasBuiltinMSVaList = true;

  // {} in inline assembly are neon specifiers, not assembly variant
  // specifiers.
  NoAsmVariants = true;

  // AAPCS gives rules for bitfields. 7.1.7 says: "The container type
  // contributes to the alignment of the containing aggregate in the same way
  // a plain (non bit-field) member of that type would, without exception for
  // zero-sized or anonymous bit-fields."
  assert(UseBitFieldTypeAlignment && "bitfields affect type alignment");
  UseZeroLengthBitfieldAlignment = true;

  // AArch64 targets default to using the ARM C++ ABI.
  TheCXXABI.set(TargetCXXABI::GenericAArch64);

  if (Triple.getOS() == llvm::Triple::Linux)
    this->MCountName = "\01_mcount";
  else if (Triple.getOS() == llvm::Triple::UnknownOS)
    this->MCountName =
        Opts.EABIVersion == llvm::EABI::GNU ? "\01_mcount" : "mcount";
}

StringRef AArch64TargetInfo::getABI() const { return ABI; }

bool AArch64TargetInfo::setABI(const std::string &Name) {
  if (Name != "aapcs" && Name != "darwinpcs")
    return false;

  ABI = Name;
  return true;
}

bool AArch64TargetInfo::isValidCPUName(StringRef Name) const {
  return Name == "generic" ||
         llvm::AArch64::parseCPUArch(Name) != llvm::AArch64::ArchKind::INVALID;
}

bool AArch64TargetInfo::setCPU(const std::string &Name) {
  return isValidCPUName(Name);
}

void AArch64TargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  llvm::AArch64::fillValidCPUArchList(Values);
}

void AArch64TargetInfo::getTargetDefinesARMV81A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  Builder.defineMacro("__ARM_FEATURE_QRDMX", "1");
}

void AArch64TargetInfo::getTargetDefinesARMV82A(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  // Also include the ARMv8.1 defines
  getTargetDefinesARMV81A(Opts, Builder);
}

void AArch64TargetInfo::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  // Target identification.
  Builder.defineMacro("__aarch64__");
  // For bare-metal.
  if (getTriple().getOS() == llvm::Triple::UnknownOS &&
      getTriple().isOSBinFormatELF())
    Builder.defineMacro("__ELF__");

  // Target properties.
  if (!getTriple().isOSWindows()) {
    Builder.defineMacro("_LP64");
    Builder.defineMacro("__LP64__");
  }

  // ACLE predefines. Many can only have one possible value on v8 AArch64.
  Builder.defineMacro("__ARM_ACLE", "200");
  Builder.defineMacro("__ARM_ARCH", "8");
  Builder.defineMacro("__ARM_ARCH_PROFILE", "'A'");

  Builder.defineMacro("__ARM_64BIT_STATE", "1");
  Builder.defineMacro("__ARM_PCS_AAPCS64", "1");
  Builder.defineMacro("__ARM_ARCH_ISA_A64", "1");

  Builder.defineMacro("__ARM_FEATURE_CLZ", "1");
  Builder.defineMacro("__ARM_FEATURE_FMA", "1");
  Builder.defineMacro("__ARM_FEATURE_LDREX", "0xF");
  Builder.defineMacro("__ARM_FEATURE_IDIV", "1"); // As specified in ACLE
  Builder.defineMacro("__ARM_FEATURE_DIV");       // For backwards compatibility
  Builder.defineMacro("__ARM_FEATURE_NUMERIC_MAXMIN", "1");
  Builder.defineMacro("__ARM_FEATURE_DIRECTED_ROUNDING", "1");

  Builder.defineMacro("__ARM_ALIGN_MAX_STACK_PWR", "4");

  // 0xe implies support for half, single and double precision operations.
  Builder.defineMacro("__ARM_FP", "0xE");

  // PCS specifies this for SysV variants, which is all we support. Other ABIs
  // may choose __ARM_FP16_FORMAT_ALTERNATIVE.
  Builder.defineMacro("__ARM_FP16_FORMAT_IEEE", "1");
  Builder.defineMacro("__ARM_FP16_ARGS", "1");

  if (Opts.UnsafeFPMath)
    Builder.defineMacro("__ARM_FP_FAST", "1");

  Builder.defineMacro("__ARM_SIZEOF_WCHAR_T",
                      Twine(Opts.WCharSize ? Opts.WCharSize : 4));

  Builder.defineMacro("__ARM_SIZEOF_MINIMAL_ENUM", Opts.ShortEnums ? "1" : "4");

  if (FPU & NeonMode) {
    Builder.defineMacro("__ARM_NEON", "1");
    // 64-bit NEON supports half, single and double precision operations.
    Builder.defineMacro("__ARM_NEON_FP", "0xE");
  }

  if (FPU & SveMode)
    Builder.defineMacro("__ARM_FEATURE_SVE", "1");

  if (CRC)
    Builder.defineMacro("__ARM_FEATURE_CRC32", "1");

  if (Crypto)
    Builder.defineMacro("__ARM_FEATURE_CRYPTO", "1");

  if (Unaligned)
    Builder.defineMacro("__ARM_FEATURE_UNALIGNED", "1");

  if ((FPU & NeonMode) && HasFullFP16)
    Builder.defineMacro("__ARM_FEATURE_FP16_VECTOR_ARITHMETIC", "1");
  if (HasFullFP16)
   Builder.defineMacro("__ARM_FEATURE_FP16_SCALAR_ARITHMETIC", "1");

  if (HasDotProd)
    Builder.defineMacro("__ARM_FEATURE_DOTPROD", "1");

  if ((FPU & NeonMode) && HasFP16FML)
    Builder.defineMacro("__ARM_FEATURE_FP16FML", "1");

  switch (ArchKind) {
  default:
    break;
  case llvm::AArch64::ArchKind::ARMV8_1A:
    getTargetDefinesARMV81A(Opts, Builder);
    break;
  case llvm::AArch64::ArchKind::ARMV8_2A:
    getTargetDefinesARMV82A(Opts, Builder);
    break;
  }

  // All of the __sync_(bool|val)_compare_and_swap_(1|2|4|8) builtins work.
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
}

ArrayRef<Builtin::Info> AArch64TargetInfo::getTargetBuiltins() const {
  return llvm::makeArrayRef(BuiltinInfo, clang::AArch64::LastTSBuiltin -
                                             Builtin::FirstTSBuiltin);
}

bool AArch64TargetInfo::hasFeature(StringRef Feature) const {
  return Feature == "aarch64" || Feature == "arm64" || Feature == "arm" ||
         (Feature == "neon" && (FPU & NeonMode)) ||
         (Feature == "sve" && (FPU & SveMode));
}

bool AArch64TargetInfo::handleTargetFeatures(std::vector<std::string> &Features,
                                             DiagnosticsEngine &Diags) {
  FPU = FPUMode;
  CRC = 0;
  Crypto = 0;
  Unaligned = 1;
  HasFullFP16 = 0;
  HasDotProd = 0;
  HasFP16FML = 0;
  ArchKind = llvm::AArch64::ArchKind::ARMV8A;

  for (const auto &Feature : Features) {
    if (Feature == "+neon")
      FPU |= NeonMode;
    if (Feature == "+sve")
      FPU |= SveMode;
    if (Feature == "+crc")
      CRC = 1;
    if (Feature == "+crypto")
      Crypto = 1;
    if (Feature == "+strict-align")
      Unaligned = 0;
    if (Feature == "+v8.1a")
      ArchKind = llvm::AArch64::ArchKind::ARMV8_1A;
    if (Feature == "+v8.2a")
      ArchKind = llvm::AArch64::ArchKind::ARMV8_2A;
    if (Feature == "+fullfp16")
      HasFullFP16 = 1;
    if (Feature == "+dotprod")
      HasDotProd = 1;
    if (Feature == "+fp16fml")
      HasFP16FML = 1;
  }

  setDataLayout();

  return true;
}

TargetInfo::CallingConvCheckResult
AArch64TargetInfo::checkCallingConvention(CallingConv CC) const {
  switch (CC) {
  case CC_C:
  case CC_Swift:
  case CC_PreserveMost:
  case CC_PreserveAll:
  case CC_OpenCLKernel:
  case CC_AArch64VectorCall:
  case CC_Win64:
    return CCCR_OK;
  default:
    return CCCR_Warning;
  }
}

bool AArch64TargetInfo::isCLZForZeroUndef() const { return false; }

TargetInfo::BuiltinVaListKind AArch64TargetInfo::getBuiltinVaListKind() const {
  return TargetInfo::AArch64ABIBuiltinVaList;
}

const char *const AArch64TargetInfo::GCCRegNames[] = {
    // 32-bit Integer registers
    "w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7", "w8", "w9", "w10", "w11",
    "w12", "w13", "w14", "w15", "w16", "w17", "w18", "w19", "w20", "w21", "w22",
    "w23", "w24", "w25", "w26", "w27", "w28", "w29", "w30", "wsp",

    // 64-bit Integer registers
    "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
    "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20", "x21", "x22",
    "x23", "x24", "x25", "x26", "x27", "x28", "fp", "lr", "sp",

    // 32-bit floating point regsisters
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
    "s12", "s13", "s14", "s15", "s16", "s17", "s18", "s19", "s20", "s21", "s22",
    "s23", "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31",

    // 64-bit floating point regsisters
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "d10", "d11",
    "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21", "d22",
    "d23", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31",

    // Vector registers
    "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11",
    "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21", "v22",
    "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"
};

ArrayRef<const char *> AArch64TargetInfo::getGCCRegNames() const {
  return llvm::makeArrayRef(GCCRegNames);
}

const TargetInfo::GCCRegAlias AArch64TargetInfo::GCCRegAliases[] = {
    {{"w31"}, "wsp"},
    {{"x31"}, "sp"},
    // GCC rN registers are aliases of xN registers.
    {{"r0"}, "x0"},
    {{"r1"}, "x1"},
    {{"r2"}, "x2"},
    {{"r3"}, "x3"},
    {{"r4"}, "x4"},
    {{"r5"}, "x5"},
    {{"r6"}, "x6"},
    {{"r7"}, "x7"},
    {{"r8"}, "x8"},
    {{"r9"}, "x9"},
    {{"r10"}, "x10"},
    {{"r11"}, "x11"},
    {{"r12"}, "x12"},
    {{"r13"}, "x13"},
    {{"r14"}, "x14"},
    {{"r15"}, "x15"},
    {{"r16"}, "x16"},
    {{"r17"}, "x17"},
    {{"r18"}, "x18"},
    {{"r19"}, "x19"},
    {{"r20"}, "x20"},
    {{"r21"}, "x21"},
    {{"r22"}, "x22"},
    {{"r23"}, "x23"},
    {{"r24"}, "x24"},
    {{"r25"}, "x25"},
    {{"r26"}, "x26"},
    {{"r27"}, "x27"},
    {{"r28"}, "x28"},
    {{"r29", "x29"}, "fp"},
    {{"r30", "x30"}, "lr"},
    // The S/D/Q and W/X registers overlap, but aren't really aliases; we
    // don't want to substitute one of these for a different-sized one.
};

ArrayRef<TargetInfo::GCCRegAlias> AArch64TargetInfo::getGCCRegAliases() const {
  return llvm::makeArrayRef(GCCRegAliases);
}

bool AArch64TargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  switch (*Name) {
  default:
    return false;
  case 'w': // Floating point and SIMD registers (V0-V31)
    Info.setAllowsRegister();
    return true;
  case 'I': // Constant that can be used with an ADD instruction
  case 'J': // Constant that can be used with a SUB instruction
  case 'K': // Constant that can be used with a 32-bit logical instruction
  case 'L': // Constant that can be used with a 64-bit logical instruction
  case 'M': // Constant that can be used as a 32-bit MOV immediate
  case 'N': // Constant that can be used as a 64-bit MOV immediate
  case 'Y': // Floating point constant zero
  case 'Z': // Integer constant zero
    return true;
  case 'Q': // A memory reference with base register and no offset
    Info.setAllowsMemory();
    return true;
  case 'S': // A symbolic address
    Info.setAllowsRegister();
    return true;
  case 'U':
    // Ump: A memory address suitable for ldp/stp in SI, DI, SF and DF modes.
    // Utf: A memory address suitable for ldp/stp in TF mode.
    // Usa: An absolute symbolic address.
    // Ush: The high part (bits 32:12) of a pc-relative symbolic address.
    llvm_unreachable("FIXME: Unimplemented support for U* constraints.");
  case 'z': // Zero register, wzr or xzr
    Info.setAllowsRegister();
    return true;
  case 'x': // Floating point and SIMD registers (V0-V15)
    Info.setAllowsRegister();
    return true;
  }
  return false;
}

bool AArch64TargetInfo::validateConstraintModifier(
    StringRef Constraint, char Modifier, unsigned Size,
    std::string &SuggestedModifier) const {
  // Strip off constraint modifiers.
  while (Constraint[0] == '=' || Constraint[0] == '+' || Constraint[0] == '&')
    Constraint = Constraint.substr(1);

  switch (Constraint[0]) {
  default:
    return true;
  case 'z':
  case 'r': {
    switch (Modifier) {
    case 'x':
    case 'w':
      // For now assume that the person knows what they're
      // doing with the modifier.
      return true;
    default:
      // By default an 'r' constraint will be in the 'x'
      // registers.
      if (Size == 64)
        return true;

      SuggestedModifier = "w";
      return false;
    }
  }
  }
}

const char *AArch64TargetInfo::getClobbers() const { return ""; }

int AArch64TargetInfo::getEHDataRegisterNumber(unsigned RegNo) const {
  if (RegNo == 0)
    return 0;
  if (RegNo == 1)
    return 1;
  return -1;
}

AArch64leTargetInfo::AArch64leTargetInfo(const llvm::Triple &Triple,
                                         const TargetOptions &Opts)
    : AArch64TargetInfo(Triple, Opts) {}

void AArch64leTargetInfo::setDataLayout() {
  if (getTriple().isOSBinFormatMachO())
    resetDataLayout("e-m:o-i64:64-i128:128-n32:64-S128");
  else
    resetDataLayout("e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128");
}

void AArch64leTargetInfo::getTargetDefines(const LangOptions &Opts,
                                           MacroBuilder &Builder) const {
  Builder.defineMacro("__AARCH64EL__");
  AArch64TargetInfo::getTargetDefines(Opts, Builder);
}

AArch64beTargetInfo::AArch64beTargetInfo(const llvm::Triple &Triple,
                                         const TargetOptions &Opts)
    : AArch64TargetInfo(Triple, Opts) {}

void AArch64beTargetInfo::getTargetDefines(const LangOptions &Opts,
                                           MacroBuilder &Builder) const {
  Builder.defineMacro("__AARCH64EB__");
  Builder.defineMacro("__AARCH_BIG_ENDIAN");
  Builder.defineMacro("__ARM_BIG_ENDIAN");
  AArch64TargetInfo::getTargetDefines(Opts, Builder);
}

void AArch64beTargetInfo::setDataLayout() {
  assert(!getTriple().isOSBinFormatMachO());
  resetDataLayout("E-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128");
}

WindowsARM64TargetInfo::WindowsARM64TargetInfo(const llvm::Triple &Triple,
                                               const TargetOptions &Opts)
    : WindowsTargetInfo<AArch64leTargetInfo>(Triple, Opts), Triple(Triple) {

  // This is an LLP64 platform.
  // int:4, long:4, long long:8, long double:8.
  IntWidth = IntAlign = 32;
  LongWidth = LongAlign = 32;
  DoubleAlign = LongLongAlign = 64;
  LongDoubleWidth = LongDoubleAlign = 64;
  LongDoubleFormat = &llvm::APFloat::IEEEdouble();
  IntMaxType = SignedLongLong;
  Int64Type = SignedLongLong;
  SizeType = UnsignedLongLong;
  PtrDiffType = SignedLongLong;
  IntPtrType = SignedLongLong;
}

void WindowsARM64TargetInfo::setDataLayout() {
  resetDataLayout("e-m:w-p:64:64-i32:32-i64:64-i128:128-n32:64-S128");
}

TargetInfo::BuiltinVaListKind
WindowsARM64TargetInfo::getBuiltinVaListKind() const {
  return TargetInfo::CharPtrBuiltinVaList;
}

TargetInfo::CallingConvCheckResult
WindowsARM64TargetInfo::checkCallingConvention(CallingConv CC) const {
  switch (CC) {
  case CC_X86StdCall:
  case CC_X86ThisCall:
  case CC_X86FastCall:
  case CC_X86VectorCall:
    return CCCR_Ignore;
  case CC_C:
  case CC_OpenCLKernel:
  case CC_PreserveMost:
  case CC_PreserveAll:
  case CC_Swift:
  case CC_Win64:
    return CCCR_OK;
  default:
    return CCCR_Warning;
  }
}

MicrosoftARM64TargetInfo::MicrosoftARM64TargetInfo(const llvm::Triple &Triple,
                                                   const TargetOptions &Opts)
    : WindowsARM64TargetInfo(Triple, Opts) {
  TheCXXABI.set(TargetCXXABI::Microsoft);
}

void MicrosoftARM64TargetInfo::getVisualStudioDefines(
    const LangOptions &Opts, MacroBuilder &Builder) const {
  WindowsTargetInfo<AArch64leTargetInfo>::getVisualStudioDefines(Opts, Builder);
  Builder.defineMacro("_M_ARM64", "1");
}

void MicrosoftARM64TargetInfo::getTargetDefines(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  WindowsTargetInfo::getTargetDefines(Opts, Builder);
  getVisualStudioDefines(Opts, Builder);
}

TargetInfo::CallingConvKind
MicrosoftARM64TargetInfo::getCallingConvKind(bool ClangABICompat4) const {
  return CCK_MicrosoftWin64;
}

MinGWARM64TargetInfo::MinGWARM64TargetInfo(const llvm::Triple &Triple,
                                           const TargetOptions &Opts)
    : WindowsARM64TargetInfo(Triple, Opts) {
  TheCXXABI.set(TargetCXXABI::GenericAArch64);
}

DarwinAArch64TargetInfo::DarwinAArch64TargetInfo(const llvm::Triple &Triple,
                                                 const TargetOptions &Opts)
    : DarwinTargetInfo<AArch64leTargetInfo>(Triple, Opts) {
  Int64Type = SignedLongLong;
  UseSignedCharForObjCBool = false;

  LongDoubleWidth = LongDoubleAlign = SuitableAlign = 64;
  LongDoubleFormat = &llvm::APFloat::IEEEdouble();

  TheCXXABI.set(TargetCXXABI::iOS64);
}

void DarwinAArch64TargetInfo::getOSDefines(const LangOptions &Opts,
                                           const llvm::Triple &Triple,
                                           MacroBuilder &Builder) const {
  Builder.defineMacro("__AARCH64_SIMD__");
  Builder.defineMacro("__ARM64_ARCH_8__");
  Builder.defineMacro("__ARM_NEON__");
  Builder.defineMacro("__LITTLE_ENDIAN__");
  Builder.defineMacro("__REGISTER_PREFIX__", "");
  Builder.defineMacro("__arm64", "1");
  Builder.defineMacro("__arm64__", "1");

  getDarwinDefines(Builder, Opts, Triple, PlatformName, PlatformMinVersion);
}

TargetInfo::BuiltinVaListKind
DarwinAArch64TargetInfo::getBuiltinVaListKind() const {
  return TargetInfo::CharPtrBuiltinVaList;
}

// 64-bit RenderScript is aarch64
RenderScript64TargetInfo::RenderScript64TargetInfo(const llvm::Triple &Triple,
                                                   const TargetOptions &Opts)
    : AArch64leTargetInfo(llvm::Triple("aarch64", Triple.getVendorName(),
                                       Triple.getOSName(),
                                       Triple.getEnvironmentName()),
                          Opts) {
  IsRenderScriptTarget = true;
}

void RenderScript64TargetInfo::getTargetDefines(const LangOptions &Opts,
                                                MacroBuilder &Builder) const {
  Builder.defineMacro("__RENDERSCRIPT__");
  AArch64leTargetInfo::getTargetDefines(Opts, Builder);
}
