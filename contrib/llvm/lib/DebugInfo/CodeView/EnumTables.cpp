//===- EnumTables.cpp - Enum to string conversion tables ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/EnumTables.h"
#include "llvm/Support/ScopedPrinter.h"
#include <type_traits>

using namespace llvm;
using namespace codeview;

#define CV_ENUM_CLASS_ENT(enum_class, enum)                                    \
  { #enum, std::underlying_type < enum_class > ::type(enum_class::enum) }

#define CV_ENUM_ENT(ns, enum)                                                  \
  { #enum, ns::enum }

static const EnumEntry<SymbolKind> SymbolTypeNames[] = {
#define CV_SYMBOL(enum, val) {#enum, enum},
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"
#undef CV_SYMBOL
};

static const EnumEntry<TypeLeafKind> TypeLeafNames[] = {
#define CV_TYPE(name, val) {#name, name},
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
#undef CV_TYPE
};

static const EnumEntry<uint16_t> RegisterNames[] = {
#define CV_REGISTER(name, val) CV_ENUM_CLASS_ENT(RegisterId, name),
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
};

static const EnumEntry<uint32_t> PublicSymFlagNames[] = {
    CV_ENUM_CLASS_ENT(PublicSymFlags, Code),
    CV_ENUM_CLASS_ENT(PublicSymFlags, Function),
    CV_ENUM_CLASS_ENT(PublicSymFlags, Managed),
    CV_ENUM_CLASS_ENT(PublicSymFlags, MSIL),
};

static const EnumEntry<uint8_t> ProcSymFlagNames[] = {
    CV_ENUM_CLASS_ENT(ProcSymFlags, HasFP),
    CV_ENUM_CLASS_ENT(ProcSymFlags, HasIRET),
    CV_ENUM_CLASS_ENT(ProcSymFlags, HasFRET),
    CV_ENUM_CLASS_ENT(ProcSymFlags, IsNoReturn),
    CV_ENUM_CLASS_ENT(ProcSymFlags, IsUnreachable),
    CV_ENUM_CLASS_ENT(ProcSymFlags, HasCustomCallingConv),
    CV_ENUM_CLASS_ENT(ProcSymFlags, IsNoInline),
    CV_ENUM_CLASS_ENT(ProcSymFlags, HasOptimizedDebugInfo),
};

static const EnumEntry<uint16_t> LocalFlags[] = {
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsParameter),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsAddressTaken),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsCompilerGenerated),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsAggregate),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsAggregated),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsAliased),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsAlias),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsReturnValue),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsOptimizedOut),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsEnregisteredGlobal),
    CV_ENUM_CLASS_ENT(LocalSymFlags, IsEnregisteredStatic),
};

static const EnumEntry<uint8_t> FrameCookieKinds[] = {
    CV_ENUM_CLASS_ENT(FrameCookieKind, Copy),
    CV_ENUM_CLASS_ENT(FrameCookieKind, XorStackPointer),
    CV_ENUM_CLASS_ENT(FrameCookieKind, XorFramePointer),
    CV_ENUM_CLASS_ENT(FrameCookieKind, XorR13),
};

static const EnumEntry<codeview::SourceLanguage> SourceLanguages[] = {
    CV_ENUM_ENT(SourceLanguage, C),       CV_ENUM_ENT(SourceLanguage, Cpp),
    CV_ENUM_ENT(SourceLanguage, Fortran), CV_ENUM_ENT(SourceLanguage, Masm),
    CV_ENUM_ENT(SourceLanguage, Pascal),  CV_ENUM_ENT(SourceLanguage, Basic),
    CV_ENUM_ENT(SourceLanguage, Cobol),   CV_ENUM_ENT(SourceLanguage, Link),
    CV_ENUM_ENT(SourceLanguage, Cvtres),  CV_ENUM_ENT(SourceLanguage, Cvtpgd),
    CV_ENUM_ENT(SourceLanguage, CSharp),  CV_ENUM_ENT(SourceLanguage, VB),
    CV_ENUM_ENT(SourceLanguage, ILAsm),   CV_ENUM_ENT(SourceLanguage, Java),
    CV_ENUM_ENT(SourceLanguage, JScript), CV_ENUM_ENT(SourceLanguage, MSIL),
    CV_ENUM_ENT(SourceLanguage, HLSL),    CV_ENUM_ENT(SourceLanguage, D),
};

static const EnumEntry<uint32_t> CompileSym2FlagNames[] = {
    CV_ENUM_CLASS_ENT(CompileSym2Flags, EC),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, NoDbgInfo),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, LTCG),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, NoDataAlign),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, ManagedPresent),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, SecurityChecks),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, HotPatch),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, CVTCIL),
    CV_ENUM_CLASS_ENT(CompileSym2Flags, MSILModule),
};

static const EnumEntry<uint32_t> CompileSym3FlagNames[] = {
    CV_ENUM_CLASS_ENT(CompileSym3Flags, EC),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, NoDbgInfo),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, LTCG),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, NoDataAlign),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, ManagedPresent),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, SecurityChecks),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, HotPatch),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, CVTCIL),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, MSILModule),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, Sdl),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, PGO),
    CV_ENUM_CLASS_ENT(CompileSym3Flags, Exp),
};

static const EnumEntry<uint32_t> FileChecksumNames[] = {
    CV_ENUM_CLASS_ENT(FileChecksumKind, None),
    CV_ENUM_CLASS_ENT(FileChecksumKind, MD5),
    CV_ENUM_CLASS_ENT(FileChecksumKind, SHA1),
    CV_ENUM_CLASS_ENT(FileChecksumKind, SHA256),
};

static const EnumEntry<unsigned> CPUTypeNames[] = {
    CV_ENUM_CLASS_ENT(CPUType, Intel8080),
    CV_ENUM_CLASS_ENT(CPUType, Intel8086),
    CV_ENUM_CLASS_ENT(CPUType, Intel80286),
    CV_ENUM_CLASS_ENT(CPUType, Intel80386),
    CV_ENUM_CLASS_ENT(CPUType, Intel80486),
    CV_ENUM_CLASS_ENT(CPUType, Pentium),
    CV_ENUM_CLASS_ENT(CPUType, PentiumPro),
    CV_ENUM_CLASS_ENT(CPUType, Pentium3),
    CV_ENUM_CLASS_ENT(CPUType, MIPS),
    CV_ENUM_CLASS_ENT(CPUType, MIPS16),
    CV_ENUM_CLASS_ENT(CPUType, MIPS32),
    CV_ENUM_CLASS_ENT(CPUType, MIPS64),
    CV_ENUM_CLASS_ENT(CPUType, MIPSI),
    CV_ENUM_CLASS_ENT(CPUType, MIPSII),
    CV_ENUM_CLASS_ENT(CPUType, MIPSIII),
    CV_ENUM_CLASS_ENT(CPUType, MIPSIV),
    CV_ENUM_CLASS_ENT(CPUType, MIPSV),
    CV_ENUM_CLASS_ENT(CPUType, M68000),
    CV_ENUM_CLASS_ENT(CPUType, M68010),
    CV_ENUM_CLASS_ENT(CPUType, M68020),
    CV_ENUM_CLASS_ENT(CPUType, M68030),
    CV_ENUM_CLASS_ENT(CPUType, M68040),
    CV_ENUM_CLASS_ENT(CPUType, Alpha),
    CV_ENUM_CLASS_ENT(CPUType, Alpha21164),
    CV_ENUM_CLASS_ENT(CPUType, Alpha21164A),
    CV_ENUM_CLASS_ENT(CPUType, Alpha21264),
    CV_ENUM_CLASS_ENT(CPUType, Alpha21364),
    CV_ENUM_CLASS_ENT(CPUType, PPC601),
    CV_ENUM_CLASS_ENT(CPUType, PPC603),
    CV_ENUM_CLASS_ENT(CPUType, PPC604),
    CV_ENUM_CLASS_ENT(CPUType, PPC620),
    CV_ENUM_CLASS_ENT(CPUType, PPCFP),
    CV_ENUM_CLASS_ENT(CPUType, PPCBE),
    CV_ENUM_CLASS_ENT(CPUType, SH3),
    CV_ENUM_CLASS_ENT(CPUType, SH3E),
    CV_ENUM_CLASS_ENT(CPUType, SH3DSP),
    CV_ENUM_CLASS_ENT(CPUType, SH4),
    CV_ENUM_CLASS_ENT(CPUType, SHMedia),
    CV_ENUM_CLASS_ENT(CPUType, ARM3),
    CV_ENUM_CLASS_ENT(CPUType, ARM4),
    CV_ENUM_CLASS_ENT(CPUType, ARM4T),
    CV_ENUM_CLASS_ENT(CPUType, ARM5),
    CV_ENUM_CLASS_ENT(CPUType, ARM5T),
    CV_ENUM_CLASS_ENT(CPUType, ARM6),
    CV_ENUM_CLASS_ENT(CPUType, ARM_XMAC),
    CV_ENUM_CLASS_ENT(CPUType, ARM_WMMX),
    CV_ENUM_CLASS_ENT(CPUType, ARM7),
    CV_ENUM_CLASS_ENT(CPUType, Omni),
    CV_ENUM_CLASS_ENT(CPUType, Ia64),
    CV_ENUM_CLASS_ENT(CPUType, Ia64_2),
    CV_ENUM_CLASS_ENT(CPUType, CEE),
    CV_ENUM_CLASS_ENT(CPUType, AM33),
    CV_ENUM_CLASS_ENT(CPUType, M32R),
    CV_ENUM_CLASS_ENT(CPUType, TriCore),
    CV_ENUM_CLASS_ENT(CPUType, X64),
    CV_ENUM_CLASS_ENT(CPUType, EBC),
    CV_ENUM_CLASS_ENT(CPUType, Thumb),
    CV_ENUM_CLASS_ENT(CPUType, ARMNT),
    CV_ENUM_CLASS_ENT(CPUType, D3D11_Shader),
};

static const EnumEntry<uint32_t> FrameProcSymFlagNames[] = {
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, HasAlloca),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, HasSetJmp),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, HasLongJmp),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, HasInlineAssembly),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, HasExceptionHandling),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, MarkedInline),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, HasStructuredExceptionHandling),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, Naked),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, SecurityChecks),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, AsynchronousExceptionHandling),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, NoStackOrderingForSecurityChecks),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, Inlined),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, StrictSecurityChecks),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, SafeBuffers),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, EncodedLocalBasePointerMask),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, EncodedParamBasePointerMask),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, ProfileGuidedOptimization),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, ValidProfileCounts),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, OptimizedForSpeed),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, GuardCfg),
    CV_ENUM_CLASS_ENT(FrameProcedureOptions, GuardCfw),
};

static const EnumEntry<uint32_t> ModuleSubstreamKindNames[] = {
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, None),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, Symbols),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, Lines),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, StringTable),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, FileChecksums),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, FrameData),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, InlineeLines),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, CrossScopeImports),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, CrossScopeExports),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, ILLines),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, FuncMDTokenMap),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, TypeMDTokenMap),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, MergedAssemblyInput),
    CV_ENUM_CLASS_ENT(DebugSubsectionKind, CoffSymbolRVA),
};

static const EnumEntry<uint16_t> ExportSymFlagNames[] = {
    CV_ENUM_CLASS_ENT(ExportFlags, IsConstant),
    CV_ENUM_CLASS_ENT(ExportFlags, IsData),
    CV_ENUM_CLASS_ENT(ExportFlags, IsPrivate),
    CV_ENUM_CLASS_ENT(ExportFlags, HasNoName),
    CV_ENUM_CLASS_ENT(ExportFlags, HasExplicitOrdinal),
    CV_ENUM_CLASS_ENT(ExportFlags, IsForwarder),
};

static const EnumEntry<uint8_t> ThunkOrdinalNames[] = {
    CV_ENUM_CLASS_ENT(ThunkOrdinal, Standard),
    CV_ENUM_CLASS_ENT(ThunkOrdinal, ThisAdjustor),
    CV_ENUM_CLASS_ENT(ThunkOrdinal, Vcall),
    CV_ENUM_CLASS_ENT(ThunkOrdinal, Pcode),
    CV_ENUM_CLASS_ENT(ThunkOrdinal, UnknownLoad),
    CV_ENUM_CLASS_ENT(ThunkOrdinal, TrampIncremental),
    CV_ENUM_CLASS_ENT(ThunkOrdinal, BranchIsland),
};

static const EnumEntry<uint16_t> TrampolineNames[] = {
    CV_ENUM_CLASS_ENT(TrampolineType, TrampIncremental),
    CV_ENUM_CLASS_ENT(TrampolineType, BranchIsland),
};

static const EnumEntry<COFF::SectionCharacteristics>
    ImageSectionCharacteristicNames[] = {
        CV_ENUM_ENT(COFF, IMAGE_SCN_TYPE_NOLOAD),
        CV_ENUM_ENT(COFF, IMAGE_SCN_TYPE_NO_PAD),
        CV_ENUM_ENT(COFF, IMAGE_SCN_CNT_CODE),
        CV_ENUM_ENT(COFF, IMAGE_SCN_CNT_INITIALIZED_DATA),
        CV_ENUM_ENT(COFF, IMAGE_SCN_CNT_UNINITIALIZED_DATA),
        CV_ENUM_ENT(COFF, IMAGE_SCN_LNK_OTHER),
        CV_ENUM_ENT(COFF, IMAGE_SCN_LNK_INFO),
        CV_ENUM_ENT(COFF, IMAGE_SCN_LNK_REMOVE),
        CV_ENUM_ENT(COFF, IMAGE_SCN_LNK_COMDAT),
        CV_ENUM_ENT(COFF, IMAGE_SCN_GPREL),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_PURGEABLE),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_16BIT),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_LOCKED),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_PRELOAD),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_1BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_2BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_4BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_8BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_16BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_32BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_64BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_128BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_256BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_512BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_1024BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_2048BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_4096BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_8192BYTES),
        CV_ENUM_ENT(COFF, IMAGE_SCN_LNK_NRELOC_OVFL),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_DISCARDABLE),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_NOT_CACHED),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_NOT_PAGED),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_SHARED),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_EXECUTE),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_READ),
        CV_ENUM_ENT(COFF, IMAGE_SCN_MEM_WRITE)};

namespace llvm {
namespace codeview {

ArrayRef<EnumEntry<SymbolKind>> getSymbolTypeNames() {
  return makeArrayRef(SymbolTypeNames);
}

ArrayRef<EnumEntry<TypeLeafKind>> getTypeLeafNames() {
  return makeArrayRef(TypeLeafNames);
}

ArrayRef<EnumEntry<uint16_t>> getRegisterNames() {
  return makeArrayRef(RegisterNames);
}

ArrayRef<EnumEntry<uint32_t>> getPublicSymFlagNames() {
  return makeArrayRef(PublicSymFlagNames);
}

ArrayRef<EnumEntry<uint8_t>> getProcSymFlagNames() {
  return makeArrayRef(ProcSymFlagNames);
}

ArrayRef<EnumEntry<uint16_t>> getLocalFlagNames() {
  return makeArrayRef(LocalFlags);
}

ArrayRef<EnumEntry<uint8_t>> getFrameCookieKindNames() {
  return makeArrayRef(FrameCookieKinds);
}

ArrayRef<EnumEntry<SourceLanguage>> getSourceLanguageNames() {
  return makeArrayRef(SourceLanguages);
}

ArrayRef<EnumEntry<uint32_t>> getCompileSym2FlagNames() {
  return makeArrayRef(CompileSym2FlagNames);
}

ArrayRef<EnumEntry<uint32_t>> getCompileSym3FlagNames() {
  return makeArrayRef(CompileSym3FlagNames);
}

ArrayRef<EnumEntry<uint32_t>> getFileChecksumNames() {
  return makeArrayRef(FileChecksumNames);
}

ArrayRef<EnumEntry<unsigned>> getCPUTypeNames() {
  return makeArrayRef(CPUTypeNames);
}

ArrayRef<EnumEntry<uint32_t>> getFrameProcSymFlagNames() {
  return makeArrayRef(FrameProcSymFlagNames);
}

ArrayRef<EnumEntry<uint16_t>> getExportSymFlagNames() {
  return makeArrayRef(ExportSymFlagNames);
}

ArrayRef<EnumEntry<uint32_t>> getModuleSubstreamKindNames() {
  return makeArrayRef(ModuleSubstreamKindNames);
}

ArrayRef<EnumEntry<uint8_t>> getThunkOrdinalNames() {
  return makeArrayRef(ThunkOrdinalNames);
}

ArrayRef<EnumEntry<uint16_t>> getTrampolineNames() {
  return makeArrayRef(TrampolineNames);
}

ArrayRef<EnumEntry<COFF::SectionCharacteristics>>
getImageSectionCharacteristicNames() {
  return makeArrayRef(ImageSectionCharacteristicNames);
}

} // end namespace codeview
} // end namespace llvm
