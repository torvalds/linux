//===- llvm/CodeGen/TargetLoweringObjectFileImpl.cpp - Object File Info ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements classes used to handle lowerings specific to common
// object file formats.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <string>

using namespace llvm;
using namespace dwarf;

static void GetObjCImageInfo(Module &M, unsigned &Version, unsigned &Flags,
                             StringRef &Section) {
  SmallVector<Module::ModuleFlagEntry, 8> ModuleFlags;
  M.getModuleFlagsMetadata(ModuleFlags);

  for (const auto &MFE: ModuleFlags) {
    // Ignore flags with 'Require' behaviour.
    if (MFE.Behavior == Module::Require)
      continue;

    StringRef Key = MFE.Key->getString();
    if (Key == "Objective-C Image Info Version") {
      Version = mdconst::extract<ConstantInt>(MFE.Val)->getZExtValue();
    } else if (Key == "Objective-C Garbage Collection" ||
               Key == "Objective-C GC Only" ||
               Key == "Objective-C Is Simulated" ||
               Key == "Objective-C Class Properties" ||
               Key == "Objective-C Image Swift Version") {
      Flags |= mdconst::extract<ConstantInt>(MFE.Val)->getZExtValue();
    } else if (Key == "Objective-C Image Info Section") {
      Section = cast<MDString>(MFE.Val)->getString();
    }
  }
}

//===----------------------------------------------------------------------===//
//                                  ELF
//===----------------------------------------------------------------------===//

void TargetLoweringObjectFileELF::Initialize(MCContext &Ctx,
                                             const TargetMachine &TgtM) {
  TargetLoweringObjectFile::Initialize(Ctx, TgtM);
  TM = &TgtM;

  CodeModel::Model CM = TgtM.getCodeModel();

  switch (TgtM.getTargetTriple().getArch()) {
  case Triple::arm:
  case Triple::armeb:
  case Triple::thumb:
  case Triple::thumbeb:
    if (Ctx.getAsmInfo()->getExceptionHandlingType() == ExceptionHandling::ARM)
      break;
    // Fallthrough if not using EHABI
    LLVM_FALLTHROUGH;
  case Triple::ppc:
  case Triple::x86:
    PersonalityEncoding = isPositionIndependent()
                              ? dwarf::DW_EH_PE_indirect |
                                    dwarf::DW_EH_PE_pcrel |
                                    dwarf::DW_EH_PE_sdata4
                              : dwarf::DW_EH_PE_absptr;
    LSDAEncoding = isPositionIndependent()
                       ? dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4
                       : dwarf::DW_EH_PE_absptr;
    TTypeEncoding = isPositionIndependent()
                        ? dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
                              dwarf::DW_EH_PE_sdata4
                        : dwarf::DW_EH_PE_absptr;
    break;
  case Triple::x86_64:
    if (isPositionIndependent()) {
      PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        ((CM == CodeModel::Small || CM == CodeModel::Medium)
         ? dwarf::DW_EH_PE_sdata4 : dwarf::DW_EH_PE_sdata8);
      LSDAEncoding = dwarf::DW_EH_PE_pcrel |
        (CM == CodeModel::Small
         ? dwarf::DW_EH_PE_sdata4 : dwarf::DW_EH_PE_sdata8);
      TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        ((CM == CodeModel::Small || CM == CodeModel::Medium)
         ? dwarf::DW_EH_PE_sdata8 : dwarf::DW_EH_PE_sdata4);
    } else {
      PersonalityEncoding =
        (CM == CodeModel::Small || CM == CodeModel::Medium)
        ? dwarf::DW_EH_PE_udata4 : dwarf::DW_EH_PE_absptr;
      LSDAEncoding = (CM == CodeModel::Small)
        ? dwarf::DW_EH_PE_udata4 : dwarf::DW_EH_PE_absptr;
      TTypeEncoding = (CM == CodeModel::Small)
        ? dwarf::DW_EH_PE_udata4 : dwarf::DW_EH_PE_absptr;
    }
    break;
  case Triple::hexagon:
    PersonalityEncoding = dwarf::DW_EH_PE_absptr;
    LSDAEncoding = dwarf::DW_EH_PE_absptr;
    TTypeEncoding = dwarf::DW_EH_PE_absptr;
    if (isPositionIndependent()) {
      PersonalityEncoding |= dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel;
      LSDAEncoding |= dwarf::DW_EH_PE_pcrel;
      TTypeEncoding |= dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel;
    }
    break;
  case Triple::aarch64:
  case Triple::aarch64_be:
    // The small model guarantees static code/data size < 4GB, but not where it
    // will be in memory. Most of these could end up >2GB away so even a signed
    // pc-relative 32-bit address is insufficient, theoretically.
    if (isPositionIndependent()) {
      PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata8;
      LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata8;
      TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata8;
    } else {
      PersonalityEncoding = dwarf::DW_EH_PE_absptr;
      LSDAEncoding = dwarf::DW_EH_PE_absptr;
      TTypeEncoding = dwarf::DW_EH_PE_absptr;
    }
    break;
  case Triple::lanai:
    LSDAEncoding = dwarf::DW_EH_PE_absptr;
    PersonalityEncoding = dwarf::DW_EH_PE_absptr;
    TTypeEncoding = dwarf::DW_EH_PE_absptr;
    break;
  case Triple::mips:
  case Triple::mipsel:
  case Triple::mips64:
  case Triple::mips64el:
    // MIPS uses indirect pointer to refer personality functions and types, so
    // that the eh_frame section can be read-only. DW.ref.personality will be
    // generated for relocation.
    PersonalityEncoding = dwarf::DW_EH_PE_indirect;
    // FIXME: The N64 ABI probably ought to use DW_EH_PE_sdata8 but we can't
    //        identify N64 from just a triple.
    TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
                    dwarf::DW_EH_PE_sdata4;
    // We don't support PC-relative LSDA references in GAS so we use the default
    // DW_EH_PE_absptr for those.

    // FreeBSD must be explicit about the data size and using pcrel since it's
    // assembler/linker won't do the automatic conversion that the Linux tools
    // do.
    if (TgtM.getTargetTriple().isOSFreeBSD()) {
      PersonalityEncoding |= dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
      LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
    }
    break;
  case Triple::ppc64:
  case Triple::ppc64le:
    PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
      dwarf::DW_EH_PE_udata8;
    LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_udata8;
    TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
      dwarf::DW_EH_PE_udata8;
    break;
  case Triple::sparcel:
  case Triple::sparc:
    if (isPositionIndependent()) {
      LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
      PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata4;
      TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata4;
    } else {
      LSDAEncoding = dwarf::DW_EH_PE_absptr;
      PersonalityEncoding = dwarf::DW_EH_PE_absptr;
      TTypeEncoding = dwarf::DW_EH_PE_absptr;
    }
    break;
  case Triple::sparcv9:
    LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
    if (isPositionIndependent()) {
      PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata4;
      TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata4;
    } else {
      PersonalityEncoding = dwarf::DW_EH_PE_absptr;
      TTypeEncoding = dwarf::DW_EH_PE_absptr;
    }
    break;
  case Triple::systemz:
    // All currently-defined code models guarantee that 4-byte PC-relative
    // values will be in range.
    if (isPositionIndependent()) {
      PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata4;
      LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
      TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata4;
    } else {
      PersonalityEncoding = dwarf::DW_EH_PE_absptr;
      LSDAEncoding = dwarf::DW_EH_PE_absptr;
      TTypeEncoding = dwarf::DW_EH_PE_absptr;
    }
    break;
  default:
    break;
  }
}

void TargetLoweringObjectFileELF::emitModuleMetadata(MCStreamer &Streamer,
                                                     Module &M) const {
  auto &C = getContext();

  if (NamedMDNode *LinkerOptions = M.getNamedMetadata("llvm.linker.options")) {
    auto *S = C.getELFSection(".linker-options", ELF::SHT_LLVM_LINKER_OPTIONS,
                              ELF::SHF_EXCLUDE);

    Streamer.SwitchSection(S);

    for (const auto &Operand : LinkerOptions->operands()) {
      if (cast<MDNode>(Operand)->getNumOperands() != 2)
        report_fatal_error("invalid llvm.linker.options");
      for (const auto &Option : cast<MDNode>(Operand)->operands()) {
        Streamer.EmitBytes(cast<MDString>(Option)->getString());
        Streamer.EmitIntValue(0, 1);
      }
    }
  }

  unsigned Version = 0;
  unsigned Flags = 0;
  StringRef Section;

  GetObjCImageInfo(M, Version, Flags, Section);
  if (!Section.empty()) {
    auto *S = C.getELFSection(Section, ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
    Streamer.SwitchSection(S);
    Streamer.EmitLabel(C.getOrCreateSymbol(StringRef("OBJC_IMAGE_INFO")));
    Streamer.EmitIntValue(Version, 4);
    Streamer.EmitIntValue(Flags, 4);
    Streamer.AddBlankLine();
  }

  SmallVector<Module::ModuleFlagEntry, 8> ModuleFlags;
  M.getModuleFlagsMetadata(ModuleFlags);

  MDNode *CFGProfile = nullptr;

  for (const auto &MFE : ModuleFlags) {
    StringRef Key = MFE.Key->getString();
    if (Key == "CG Profile") {
      CFGProfile = cast<MDNode>(MFE.Val);
      break;
    }
  }

  if (!CFGProfile)
    return;

  auto GetSym = [this](const MDOperand &MDO) -> MCSymbol * {
    if (!MDO)
      return nullptr;
    auto V = cast<ValueAsMetadata>(MDO);
    const Function *F = cast<Function>(V->getValue());
    return TM->getSymbol(F);
  };

  for (const auto &Edge : CFGProfile->operands()) {
    MDNode *E = cast<MDNode>(Edge);
    const MCSymbol *From = GetSym(E->getOperand(0));
    const MCSymbol *To = GetSym(E->getOperand(1));
    // Skip null functions. This can happen if functions are dead stripped after
    // the CGProfile pass has been run.
    if (!From || !To)
      continue;
    uint64_t Count = cast<ConstantAsMetadata>(E->getOperand(2))
                         ->getValue()
                         ->getUniqueInteger()
                         .getZExtValue();
    Streamer.emitCGProfileEntry(
        MCSymbolRefExpr::create(From, MCSymbolRefExpr::VK_None, C),
        MCSymbolRefExpr::create(To, MCSymbolRefExpr::VK_None, C), Count);
  }
}

MCSymbol *TargetLoweringObjectFileELF::getCFIPersonalitySymbol(
    const GlobalValue *GV, const TargetMachine &TM,
    MachineModuleInfo *MMI) const {
  unsigned Encoding = getPersonalityEncoding();
  if ((Encoding & 0x80) == DW_EH_PE_indirect)
    return getContext().getOrCreateSymbol(StringRef("DW.ref.") +
                                          TM.getSymbol(GV)->getName());
  if ((Encoding & 0x70) == DW_EH_PE_absptr)
    return TM.getSymbol(GV);
  report_fatal_error("We do not support this DWARF encoding yet!");
}

void TargetLoweringObjectFileELF::emitPersonalityValue(
    MCStreamer &Streamer, const DataLayout &DL, const MCSymbol *Sym) const {
  SmallString<64> NameData("DW.ref.");
  NameData += Sym->getName();
  MCSymbolELF *Label =
      cast<MCSymbolELF>(getContext().getOrCreateSymbol(NameData));
  Streamer.EmitSymbolAttribute(Label, MCSA_Hidden);
  Streamer.EmitSymbolAttribute(Label, MCSA_Weak);
  unsigned Flags = ELF::SHF_ALLOC | ELF::SHF_WRITE | ELF::SHF_GROUP;
  MCSection *Sec = getContext().getELFNamedSection(".data", Label->getName(),
                                                   ELF::SHT_PROGBITS, Flags, 0);
  unsigned Size = DL.getPointerSize();
  Streamer.SwitchSection(Sec);
  Streamer.EmitValueToAlignment(DL.getPointerABIAlignment(0));
  Streamer.EmitSymbolAttribute(Label, MCSA_ELF_TypeObject);
  const MCExpr *E = MCConstantExpr::create(Size, getContext());
  Streamer.emitELFSize(Label, E);
  Streamer.EmitLabel(Label);

  Streamer.EmitSymbolValue(Sym, Size);
}

const MCExpr *TargetLoweringObjectFileELF::getTTypeGlobalReference(
    const GlobalValue *GV, unsigned Encoding, const TargetMachine &TM,
    MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  if (Encoding & DW_EH_PE_indirect) {
    MachineModuleInfoELF &ELFMMI = MMI->getObjFileInfo<MachineModuleInfoELF>();

    MCSymbol *SSym = getSymbolWithGlobalValueBase(GV, ".DW.stub", TM);

    // Add information about the stub reference to ELFMMI so that the stub
    // gets emitted by the asmprinter.
    MachineModuleInfoImpl::StubValueTy &StubSym = ELFMMI.getGVStubEntry(SSym);
    if (!StubSym.getPointer()) {
      MCSymbol *Sym = TM.getSymbol(GV);
      StubSym = MachineModuleInfoImpl::StubValueTy(Sym, !GV->hasLocalLinkage());
    }

    return TargetLoweringObjectFile::
      getTTypeReference(MCSymbolRefExpr::create(SSym, getContext()),
                        Encoding & ~DW_EH_PE_indirect, Streamer);
  }

  return TargetLoweringObjectFile::getTTypeGlobalReference(GV, Encoding, TM,
                                                           MMI, Streamer);
}

static SectionKind getELFKindForNamedSection(StringRef Name, SectionKind K) {
  // N.B.: The defaults used in here are not the same ones used in MC.
  // We follow gcc, MC follows gas. For example, given ".section .eh_frame",
  // both gas and MC will produce a section with no flags. Given
  // section(".eh_frame") gcc will produce:
  //
  //   .section   .eh_frame,"a",@progbits

  if (Name == getInstrProfSectionName(IPSK_covmap, Triple::ELF,
                                      /*AddSegmentInfo=*/false))
    return SectionKind::getMetadata();

  if (Name.empty() || Name[0] != '.') return K;

  // Default implementation based on some magic section names.
  if (Name == ".bss" ||
      Name.startswith(".bss.") ||
      Name.startswith(".gnu.linkonce.b.") ||
      Name.startswith(".llvm.linkonce.b.") ||
      Name == ".sbss" ||
      Name.startswith(".sbss.") ||
      Name.startswith(".gnu.linkonce.sb.") ||
      Name.startswith(".llvm.linkonce.sb."))
    return SectionKind::getBSS();

  if (Name == ".tdata" ||
      Name.startswith(".tdata.") ||
      Name.startswith(".gnu.linkonce.td.") ||
      Name.startswith(".llvm.linkonce.td."))
    return SectionKind::getThreadData();

  if (Name == ".tbss" ||
      Name.startswith(".tbss.") ||
      Name.startswith(".gnu.linkonce.tb.") ||
      Name.startswith(".llvm.linkonce.tb."))
    return SectionKind::getThreadBSS();

  return K;
}

static unsigned getELFSectionType(StringRef Name, SectionKind K) {
  // Use SHT_NOTE for section whose name starts with ".note" to allow
  // emitting ELF notes from C variable declaration.
  // See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=77609
  if (Name.startswith(".note"))
    return ELF::SHT_NOTE;

  if (Name == ".init_array")
    return ELF::SHT_INIT_ARRAY;

  if (Name == ".fini_array")
    return ELF::SHT_FINI_ARRAY;

  if (Name == ".preinit_array")
    return ELF::SHT_PREINIT_ARRAY;

  if (K.isBSS() || K.isThreadBSS())
    return ELF::SHT_NOBITS;

  return ELF::SHT_PROGBITS;
}

static unsigned getELFSectionFlags(SectionKind K) {
  unsigned Flags = 0;

  if (!K.isMetadata())
    Flags |= ELF::SHF_ALLOC;

  if (K.isText())
    Flags |= ELF::SHF_EXECINSTR;

  if (K.isExecuteOnly())
    Flags |= ELF::SHF_ARM_PURECODE;

  if (K.isWriteable())
    Flags |= ELF::SHF_WRITE;

  if (K.isThreadLocal())
    Flags |= ELF::SHF_TLS;

  if (K.isMergeableCString() || K.isMergeableConst())
    Flags |= ELF::SHF_MERGE;

  if (K.isMergeableCString())
    Flags |= ELF::SHF_STRINGS;

  return Flags;
}

static const Comdat *getELFComdat(const GlobalValue *GV) {
  const Comdat *C = GV->getComdat();
  if (!C)
    return nullptr;

  if (C->getSelectionKind() != Comdat::Any)
    report_fatal_error("ELF COMDATs only support SelectionKind::Any, '" +
                       C->getName() + "' cannot be lowered.");

  return C;
}

static const MCSymbolELF *getAssociatedSymbol(const GlobalObject *GO,
                                              const TargetMachine &TM) {
  MDNode *MD = GO->getMetadata(LLVMContext::MD_associated);
  if (!MD)
    return nullptr;

  const MDOperand &Op = MD->getOperand(0);
  if (!Op.get())
    return nullptr;

  auto *VM = dyn_cast<ValueAsMetadata>(Op);
  if (!VM)
    report_fatal_error("MD_associated operand is not ValueAsMetadata");

  GlobalObject *OtherGO = dyn_cast<GlobalObject>(VM->getValue());
  return OtherGO ? dyn_cast<MCSymbolELF>(TM.getSymbol(OtherGO)) : nullptr;
}

static unsigned getEntrySizeForKind(SectionKind Kind) {
  if (Kind.isMergeable1ByteCString())
    return 1;
  else if (Kind.isMergeable2ByteCString())
    return 2;
  else if (Kind.isMergeable4ByteCString())
    return 4;
  else if (Kind.isMergeableConst4())
    return 4;
  else if (Kind.isMergeableConst8())
    return 8;
  else if (Kind.isMergeableConst16())
    return 16;
  else if (Kind.isMergeableConst32())
    return 32;
  else {
    // We shouldn't have mergeable C strings or mergeable constants that we
    // didn't handle above.
    assert(!Kind.isMergeableCString() && "unknown string width");
    assert(!Kind.isMergeableConst() && "unknown data width");
    return 0;
  }
}

MCSection *TargetLoweringObjectFileELF::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  StringRef SectionName = GO->getSection();

  // Check if '#pragma clang section' name is applicable.
  // Note that pragma directive overrides -ffunction-section, -fdata-section
  // and so section name is exactly as user specified and not uniqued.
  const GlobalVariable *GV = dyn_cast<GlobalVariable>(GO);
  if (GV && GV->hasImplicitSection()) {
    auto Attrs = GV->getAttributes();
    if (Attrs.hasAttribute("bss-section") && Kind.isBSS()) {
      SectionName = Attrs.getAttribute("bss-section").getValueAsString();
    } else if (Attrs.hasAttribute("rodata-section") && Kind.isReadOnly()) {
      SectionName = Attrs.getAttribute("rodata-section").getValueAsString();
    } else if (Attrs.hasAttribute("data-section") && Kind.isData()) {
      SectionName = Attrs.getAttribute("data-section").getValueAsString();
    }
  }
  const Function *F = dyn_cast<Function>(GO);
  if (F && F->hasFnAttribute("implicit-section-name")) {
    SectionName = F->getFnAttribute("implicit-section-name").getValueAsString();
  }

  // Infer section flags from the section name if we can.
  Kind = getELFKindForNamedSection(SectionName, Kind);

  StringRef Group = "";
  unsigned Flags = getELFSectionFlags(Kind);
  if (const Comdat *C = getELFComdat(GO)) {
    Group = C->getName();
    Flags |= ELF::SHF_GROUP;
  }

  // A section can have at most one associated section. Put each global with
  // MD_associated in a unique section.
  unsigned UniqueID = MCContext::GenericSectionID;
  const MCSymbolELF *AssociatedSymbol = getAssociatedSymbol(GO, TM);
  if (AssociatedSymbol) {
    UniqueID = NextUniqueID++;
    Flags |= ELF::SHF_LINK_ORDER;
  }

  MCSectionELF *Section = getContext().getELFSection(
      SectionName, getELFSectionType(SectionName, Kind), Flags,
      getEntrySizeForKind(Kind), Group, UniqueID, AssociatedSymbol);
  // Make sure that we did not get some other section with incompatible sh_link.
  // This should not be possible due to UniqueID code above.
  assert(Section->getAssociatedSymbol() == AssociatedSymbol &&
         "Associated symbol mismatch between sections");
  return Section;
}

/// Return the section prefix name used by options FunctionsSections and
/// DataSections.
static StringRef getSectionPrefixForGlobal(SectionKind Kind) {
  if (Kind.isText())
    return ".text";
  if (Kind.isReadOnly())
    return ".rodata";
  if (Kind.isBSS())
    return ".bss";
  if (Kind.isThreadData())
    return ".tdata";
  if (Kind.isThreadBSS())
    return ".tbss";
  if (Kind.isData())
    return ".data";
  assert(Kind.isReadOnlyWithRel() && "Unknown section kind");
  return ".data.rel.ro";
}

static MCSectionELF *selectELFSectionForGlobal(
    MCContext &Ctx, const GlobalObject *GO, SectionKind Kind, Mangler &Mang,
    const TargetMachine &TM, bool EmitUniqueSection, unsigned Flags,
    unsigned *NextUniqueID, const MCSymbolELF *AssociatedSymbol) {

  StringRef Group = "";
  if (const Comdat *C = getELFComdat(GO)) {
    Flags |= ELF::SHF_GROUP;
    Group = C->getName();
  }

  // Get the section entry size based on the kind.
  unsigned EntrySize = getEntrySizeForKind(Kind);

  SmallString<128> Name;
  if (Kind.isMergeableCString()) {
    // We also need alignment here.
    // FIXME: this is getting the alignment of the character, not the
    // alignment of the global!
    unsigned Align = GO->getParent()->getDataLayout().getPreferredAlignment(
        cast<GlobalVariable>(GO));

    std::string SizeSpec = ".rodata.str" + utostr(EntrySize) + ".";
    Name = SizeSpec + utostr(Align);
  } else if (Kind.isMergeableConst()) {
    Name = ".rodata.cst";
    Name += utostr(EntrySize);
  } else {
    Name = getSectionPrefixForGlobal(Kind);
  }

  if (const auto *F = dyn_cast<Function>(GO)) {
    const auto &OptionalPrefix = F->getSectionPrefix();
    if (OptionalPrefix)
      Name += *OptionalPrefix;
  }

  unsigned UniqueID = MCContext::GenericSectionID;
  if (EmitUniqueSection) {
    if (TM.getUniqueSectionNames()) {
      Name.push_back('.');
      TM.getNameWithPrefix(Name, GO, Mang, true /*MayAlwaysUsePrivate*/);
    } else {
      UniqueID = *NextUniqueID;
      (*NextUniqueID)++;
    }
  }
  // Use 0 as the unique ID for execute-only text.
  if (Kind.isExecuteOnly())
    UniqueID = 0;
  return Ctx.getELFSection(Name, getELFSectionType(Name, Kind), Flags,
                           EntrySize, Group, UniqueID, AssociatedSymbol);
}

MCSection *TargetLoweringObjectFileELF::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  unsigned Flags = getELFSectionFlags(Kind);

  // If we have -ffunction-section or -fdata-section then we should emit the
  // global value to a uniqued section specifically for it.
  bool EmitUniqueSection = false;
  if (!(Flags & ELF::SHF_MERGE) && !Kind.isCommon()) {
    if (Kind.isText())
      EmitUniqueSection = TM.getFunctionSections();
    else
      EmitUniqueSection = TM.getDataSections();
  }
  EmitUniqueSection |= GO->hasComdat();

  const MCSymbolELF *AssociatedSymbol = getAssociatedSymbol(GO, TM);
  if (AssociatedSymbol) {
    EmitUniqueSection = true;
    Flags |= ELF::SHF_LINK_ORDER;
  }

  MCSectionELF *Section = selectELFSectionForGlobal(
      getContext(), GO, Kind, getMangler(), TM, EmitUniqueSection, Flags,
      &NextUniqueID, AssociatedSymbol);
  assert(Section->getAssociatedSymbol() == AssociatedSymbol);
  return Section;
}

MCSection *TargetLoweringObjectFileELF::getSectionForJumpTable(
    const Function &F, const TargetMachine &TM) const {
  // If the function can be removed, produce a unique section so that
  // the table doesn't prevent the removal.
  const Comdat *C = F.getComdat();
  bool EmitUniqueSection = TM.getFunctionSections() || C;
  if (!EmitUniqueSection)
    return ReadOnlySection;

  return selectELFSectionForGlobal(getContext(), &F, SectionKind::getReadOnly(),
                                   getMangler(), TM, EmitUniqueSection,
                                   ELF::SHF_ALLOC, &NextUniqueID,
                                   /* AssociatedSymbol */ nullptr);
}

bool TargetLoweringObjectFileELF::shouldPutJumpTableInFunctionSection(
    bool UsesLabelDifference, const Function &F) const {
  // We can always create relative relocations, so use another section
  // that can be marked non-executable.
  return false;
}

/// Given a mergeable constant with the specified size and relocation
/// information, return a section that it should be placed in.
MCSection *TargetLoweringObjectFileELF::getSectionForConstant(
    const DataLayout &DL, SectionKind Kind, const Constant *C,
    unsigned &Align) const {
  if (Kind.isMergeableConst4() && MergeableConst4Section)
    return MergeableConst4Section;
  if (Kind.isMergeableConst8() && MergeableConst8Section)
    return MergeableConst8Section;
  if (Kind.isMergeableConst16() && MergeableConst16Section)
    return MergeableConst16Section;
  if (Kind.isMergeableConst32() && MergeableConst32Section)
    return MergeableConst32Section;
  if (Kind.isReadOnly())
    return ReadOnlySection;

  assert(Kind.isReadOnlyWithRel() && "Unknown section kind");
  return DataRelROSection;
}

static MCSectionELF *getStaticStructorSection(MCContext &Ctx, bool UseInitArray,
                                              bool IsCtor, unsigned Priority,
                                              const MCSymbol *KeySym) {
  std::string Name;
  unsigned Type;
  unsigned Flags = ELF::SHF_ALLOC | ELF::SHF_WRITE;
  StringRef COMDAT = KeySym ? KeySym->getName() : "";

  if (KeySym)
    Flags |= ELF::SHF_GROUP;

  if (UseInitArray) {
    if (IsCtor) {
      Type = ELF::SHT_INIT_ARRAY;
      Name = ".init_array";
    } else {
      Type = ELF::SHT_FINI_ARRAY;
      Name = ".fini_array";
    }
    if (Priority != 65535) {
      Name += '.';
      Name += utostr(Priority);
    }
  } else {
    // The default scheme is .ctor / .dtor, so we have to invert the priority
    // numbering.
    if (IsCtor)
      Name = ".ctors";
    else
      Name = ".dtors";
    if (Priority != 65535)
      raw_string_ostream(Name) << format(".%05u", 65535 - Priority);
    Type = ELF::SHT_PROGBITS;
  }

  return Ctx.getELFSection(Name, Type, Flags, 0, COMDAT);
}

MCSection *TargetLoweringObjectFileELF::getStaticCtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  return getStaticStructorSection(getContext(), UseInitArray, true, Priority,
                                  KeySym);
}

MCSection *TargetLoweringObjectFileELF::getStaticDtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  return getStaticStructorSection(getContext(), UseInitArray, false, Priority,
                                  KeySym);
}

const MCExpr *TargetLoweringObjectFileELF::lowerRelativeReference(
    const GlobalValue *LHS, const GlobalValue *RHS,
    const TargetMachine &TM) const {
  // We may only use a PLT-relative relocation to refer to unnamed_addr
  // functions.
  if (!LHS->hasGlobalUnnamedAddr() || !LHS->getValueType()->isFunctionTy())
    return nullptr;

  // Basic sanity checks.
  if (LHS->getType()->getPointerAddressSpace() != 0 ||
      RHS->getType()->getPointerAddressSpace() != 0 || LHS->isThreadLocal() ||
      RHS->isThreadLocal())
    return nullptr;

  return MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(TM.getSymbol(LHS), PLTRelativeVariantKind,
                              getContext()),
      MCSymbolRefExpr::create(TM.getSymbol(RHS), getContext()), getContext());
}

MCSection *TargetLoweringObjectFileELF::getSectionForCommandLines() const {
  // Use ".GCC.command.line" since this feature is to support clang's
  // -frecord-gcc-switches which in turn attempts to mimic GCC's switch of the
  // same name.
  return getContext().getELFSection(".GCC.command.line", ELF::SHT_PROGBITS,
                                    ELF::SHF_MERGE | ELF::SHF_STRINGS, 1, "");
}

void
TargetLoweringObjectFileELF::InitializeELF(bool UseInitArray_) {
  UseInitArray = UseInitArray_;
  MCContext &Ctx = getContext();
  if (!UseInitArray) {
    StaticCtorSection = Ctx.getELFSection(".ctors", ELF::SHT_PROGBITS,
                                          ELF::SHF_ALLOC | ELF::SHF_WRITE);

    StaticDtorSection = Ctx.getELFSection(".dtors", ELF::SHT_PROGBITS,
                                          ELF::SHF_ALLOC | ELF::SHF_WRITE);
    return;
  }

  StaticCtorSection = Ctx.getELFSection(".init_array", ELF::SHT_INIT_ARRAY,
                                        ELF::SHF_WRITE | ELF::SHF_ALLOC);
  StaticDtorSection = Ctx.getELFSection(".fini_array", ELF::SHT_FINI_ARRAY,
                                        ELF::SHF_WRITE | ELF::SHF_ALLOC);
}

//===----------------------------------------------------------------------===//
//                                 MachO
//===----------------------------------------------------------------------===//

TargetLoweringObjectFileMachO::TargetLoweringObjectFileMachO()
  : TargetLoweringObjectFile() {
  SupportIndirectSymViaGOTPCRel = true;
}

void TargetLoweringObjectFileMachO::Initialize(MCContext &Ctx,
                                               const TargetMachine &TM) {
  TargetLoweringObjectFile::Initialize(Ctx, TM);
  if (TM.getRelocationModel() == Reloc::Static) {
    StaticCtorSection = Ctx.getMachOSection("__TEXT", "__constructor", 0,
                                            SectionKind::getData());
    StaticDtorSection = Ctx.getMachOSection("__TEXT", "__destructor", 0,
                                            SectionKind::getData());
  } else {
    StaticCtorSection = Ctx.getMachOSection("__DATA", "__mod_init_func",
                                            MachO::S_MOD_INIT_FUNC_POINTERS,
                                            SectionKind::getData());
    StaticDtorSection = Ctx.getMachOSection("__DATA", "__mod_term_func",
                                            MachO::S_MOD_TERM_FUNC_POINTERS,
                                            SectionKind::getData());
  }

  PersonalityEncoding =
      dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
  LSDAEncoding = dwarf::DW_EH_PE_pcrel;
  TTypeEncoding =
      dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
}

void TargetLoweringObjectFileMachO::emitModuleMetadata(MCStreamer &Streamer,
                                                       Module &M) const {
  // Emit the linker options if present.
  if (auto *LinkerOptions = M.getNamedMetadata("llvm.linker.options")) {
    for (const auto &Option : LinkerOptions->operands()) {
      SmallVector<std::string, 4> StrOptions;
      for (const auto &Piece : cast<MDNode>(Option)->operands())
        StrOptions.push_back(cast<MDString>(Piece)->getString());
      Streamer.EmitLinkerOptions(StrOptions);
    }
  }

  unsigned VersionVal = 0;
  unsigned ImageInfoFlags = 0;
  StringRef SectionVal;

  GetObjCImageInfo(M, VersionVal, ImageInfoFlags, SectionVal);

  // The section is mandatory. If we don't have it, then we don't have GC info.
  if (SectionVal.empty())
    return;

  StringRef Segment, Section;
  unsigned TAA = 0, StubSize = 0;
  bool TAAParsed;
  std::string ErrorCode =
    MCSectionMachO::ParseSectionSpecifier(SectionVal, Segment, Section,
                                          TAA, TAAParsed, StubSize);
  if (!ErrorCode.empty())
    // If invalid, report the error with report_fatal_error.
    report_fatal_error("Invalid section specifier '" + Section + "': " +
                       ErrorCode + ".");

  // Get the section.
  MCSectionMachO *S = getContext().getMachOSection(
      Segment, Section, TAA, StubSize, SectionKind::getData());
  Streamer.SwitchSection(S);
  Streamer.EmitLabel(getContext().
                     getOrCreateSymbol(StringRef("L_OBJC_IMAGE_INFO")));
  Streamer.EmitIntValue(VersionVal, 4);
  Streamer.EmitIntValue(ImageInfoFlags, 4);
  Streamer.AddBlankLine();
}

static void checkMachOComdat(const GlobalValue *GV) {
  const Comdat *C = GV->getComdat();
  if (!C)
    return;

  report_fatal_error("MachO doesn't support COMDATs, '" + C->getName() +
                     "' cannot be lowered.");
}

MCSection *TargetLoweringObjectFileMachO::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // Parse the section specifier and create it if valid.
  StringRef Segment, Section;
  unsigned TAA = 0, StubSize = 0;
  bool TAAParsed;

  checkMachOComdat(GO);

  std::string ErrorCode =
    MCSectionMachO::ParseSectionSpecifier(GO->getSection(), Segment, Section,
                                          TAA, TAAParsed, StubSize);
  if (!ErrorCode.empty()) {
    // If invalid, report the error with report_fatal_error.
    report_fatal_error("Global variable '" + GO->getName() +
                       "' has an invalid section specifier '" +
                       GO->getSection() + "': " + ErrorCode + ".");
  }

  // Get the section.
  MCSectionMachO *S =
      getContext().getMachOSection(Segment, Section, TAA, StubSize, Kind);

  // If TAA wasn't set by ParseSectionSpecifier() above,
  // use the value returned by getMachOSection() as a default.
  if (!TAAParsed)
    TAA = S->getTypeAndAttributes();

  // Okay, now that we got the section, verify that the TAA & StubSize agree.
  // If the user declared multiple globals with different section flags, we need
  // to reject it here.
  if (S->getTypeAndAttributes() != TAA || S->getStubSize() != StubSize) {
    // If invalid, report the error with report_fatal_error.
    report_fatal_error("Global variable '" + GO->getName() +
                       "' section type or attributes does not match previous"
                       " section specifier");
  }

  return S;
}

MCSection *TargetLoweringObjectFileMachO::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  checkMachOComdat(GO);

  // Handle thread local data.
  if (Kind.isThreadBSS()) return TLSBSSSection;
  if (Kind.isThreadData()) return TLSDataSection;

  if (Kind.isText())
    return GO->isWeakForLinker() ? TextCoalSection : TextSection;

  // If this is weak/linkonce, put this in a coalescable section, either in text
  // or data depending on if it is writable.
  if (GO->isWeakForLinker()) {
    if (Kind.isReadOnly())
      return ConstTextCoalSection;
    if (Kind.isReadOnlyWithRel())
      return ConstDataCoalSection;
    return DataCoalSection;
  }

  // FIXME: Alignment check should be handled by section classifier.
  if (Kind.isMergeable1ByteCString() &&
      GO->getParent()->getDataLayout().getPreferredAlignment(
          cast<GlobalVariable>(GO)) < 32)
    return CStringSection;

  // Do not put 16-bit arrays in the UString section if they have an
  // externally visible label, this runs into issues with certain linker
  // versions.
  if (Kind.isMergeable2ByteCString() && !GO->hasExternalLinkage() &&
      GO->getParent()->getDataLayout().getPreferredAlignment(
          cast<GlobalVariable>(GO)) < 32)
    return UStringSection;

  // With MachO only variables whose corresponding symbol starts with 'l' or
  // 'L' can be merged, so we only try merging GVs with private linkage.
  if (GO->hasPrivateLinkage() && Kind.isMergeableConst()) {
    if (Kind.isMergeableConst4())
      return FourByteConstantSection;
    if (Kind.isMergeableConst8())
      return EightByteConstantSection;
    if (Kind.isMergeableConst16())
      return SixteenByteConstantSection;
  }

  // Otherwise, if it is readonly, but not something we can specially optimize,
  // just drop it in .const.
  if (Kind.isReadOnly())
    return ReadOnlySection;

  // If this is marked const, put it into a const section.  But if the dynamic
  // linker needs to write to it, put it in the data segment.
  if (Kind.isReadOnlyWithRel())
    return ConstDataSection;

  // Put zero initialized globals with strong external linkage in the
  // DATA, __common section with the .zerofill directive.
  if (Kind.isBSSExtern())
    return DataCommonSection;

  // Put zero initialized globals with local linkage in __DATA,__bss directive
  // with the .zerofill directive (aka .lcomm).
  if (Kind.isBSSLocal())
    return DataBSSSection;

  // Otherwise, just drop the variable in the normal data section.
  return DataSection;
}

MCSection *TargetLoweringObjectFileMachO::getSectionForConstant(
    const DataLayout &DL, SectionKind Kind, const Constant *C,
    unsigned &Align) const {
  // If this constant requires a relocation, we have to put it in the data
  // segment, not in the text segment.
  if (Kind.isData() || Kind.isReadOnlyWithRel())
    return ConstDataSection;

  if (Kind.isMergeableConst4())
    return FourByteConstantSection;
  if (Kind.isMergeableConst8())
    return EightByteConstantSection;
  if (Kind.isMergeableConst16())
    return SixteenByteConstantSection;
  return ReadOnlySection;  // .const
}

const MCExpr *TargetLoweringObjectFileMachO::getTTypeGlobalReference(
    const GlobalValue *GV, unsigned Encoding, const TargetMachine &TM,
    MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  // The mach-o version of this method defaults to returning a stub reference.

  if (Encoding & DW_EH_PE_indirect) {
    MachineModuleInfoMachO &MachOMMI =
      MMI->getObjFileInfo<MachineModuleInfoMachO>();

    MCSymbol *SSym = getSymbolWithGlobalValueBase(GV, "$non_lazy_ptr", TM);

    // Add information about the stub reference to MachOMMI so that the stub
    // gets emitted by the asmprinter.
    MachineModuleInfoImpl::StubValueTy &StubSym = MachOMMI.getGVStubEntry(SSym);
    if (!StubSym.getPointer()) {
      MCSymbol *Sym = TM.getSymbol(GV);
      StubSym = MachineModuleInfoImpl::StubValueTy(Sym, !GV->hasLocalLinkage());
    }

    return TargetLoweringObjectFile::
      getTTypeReference(MCSymbolRefExpr::create(SSym, getContext()),
                        Encoding & ~DW_EH_PE_indirect, Streamer);
  }

  return TargetLoweringObjectFile::getTTypeGlobalReference(GV, Encoding, TM,
                                                           MMI, Streamer);
}

MCSymbol *TargetLoweringObjectFileMachO::getCFIPersonalitySymbol(
    const GlobalValue *GV, const TargetMachine &TM,
    MachineModuleInfo *MMI) const {
  // The mach-o version of this method defaults to returning a stub reference.
  MachineModuleInfoMachO &MachOMMI =
    MMI->getObjFileInfo<MachineModuleInfoMachO>();

  MCSymbol *SSym = getSymbolWithGlobalValueBase(GV, "$non_lazy_ptr", TM);

  // Add information about the stub reference to MachOMMI so that the stub
  // gets emitted by the asmprinter.
  MachineModuleInfoImpl::StubValueTy &StubSym = MachOMMI.getGVStubEntry(SSym);
  if (!StubSym.getPointer()) {
    MCSymbol *Sym = TM.getSymbol(GV);
    StubSym = MachineModuleInfoImpl::StubValueTy(Sym, !GV->hasLocalLinkage());
  }

  return SSym;
}

const MCExpr *TargetLoweringObjectFileMachO::getIndirectSymViaGOTPCRel(
    const MCSymbol *Sym, const MCValue &MV, int64_t Offset,
    MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  // Although MachO 32-bit targets do not explicitly have a GOTPCREL relocation
  // as 64-bit do, we replace the GOT equivalent by accessing the final symbol
  // through a non_lazy_ptr stub instead. One advantage is that it allows the
  // computation of deltas to final external symbols. Example:
  //
  //    _extgotequiv:
  //       .long   _extfoo
  //
  //    _delta:
  //       .long   _extgotequiv-_delta
  //
  // is transformed to:
  //
  //    _delta:
  //       .long   L_extfoo$non_lazy_ptr-(_delta+0)
  //
  //       .section        __IMPORT,__pointers,non_lazy_symbol_pointers
  //    L_extfoo$non_lazy_ptr:
  //       .indirect_symbol        _extfoo
  //       .long   0
  //
  // The indirect symbol table (and sections of non_lazy_symbol_pointers type)
  // may point to both local (same translation unit) and global (other
  // translation units) symbols. Example:
  //
  // .section __DATA,__pointers,non_lazy_symbol_pointers
  // L1:
  //    .indirect_symbol _myGlobal
  //    .long 0
  // L2:
  //    .indirect_symbol _myLocal
  //    .long _myLocal
  //
  // If the symbol is local, instead of the symbol's index, the assembler
  // places the constant INDIRECT_SYMBOL_LOCAL into the indirect symbol table.
  // Then the linker will notice the constant in the table and will look at the
  // content of the symbol.
  MachineModuleInfoMachO &MachOMMI =
    MMI->getObjFileInfo<MachineModuleInfoMachO>();
  MCContext &Ctx = getContext();

  // The offset must consider the original displacement from the base symbol
  // since 32-bit targets don't have a GOTPCREL to fold the PC displacement.
  Offset = -MV.getConstant();
  const MCSymbol *BaseSym = &MV.getSymB()->getSymbol();

  // Access the final symbol via sym$non_lazy_ptr and generate the appropriated
  // non_lazy_ptr stubs.
  SmallString<128> Name;
  StringRef Suffix = "$non_lazy_ptr";
  Name += MMI->getModule()->getDataLayout().getPrivateGlobalPrefix();
  Name += Sym->getName();
  Name += Suffix;
  MCSymbol *Stub = Ctx.getOrCreateSymbol(Name);

  MachineModuleInfoImpl::StubValueTy &StubSym = MachOMMI.getGVStubEntry(Stub);
  if (!StubSym.getPointer()) {
    bool IsIndirectLocal = Sym->isDefined() && !Sym->isExternal();
    // With the assumption that IsIndirectLocal == GV->hasLocalLinkage().
    StubSym = MachineModuleInfoImpl::StubValueTy(const_cast<MCSymbol *>(Sym),
                                                 !IsIndirectLocal);
  }

  const MCExpr *BSymExpr =
    MCSymbolRefExpr::create(BaseSym, MCSymbolRefExpr::VK_None, Ctx);
  const MCExpr *LHS =
    MCSymbolRefExpr::create(Stub, MCSymbolRefExpr::VK_None, Ctx);

  if (!Offset)
    return MCBinaryExpr::createSub(LHS, BSymExpr, Ctx);

  const MCExpr *RHS =
    MCBinaryExpr::createAdd(BSymExpr, MCConstantExpr::create(Offset, Ctx), Ctx);
  return MCBinaryExpr::createSub(LHS, RHS, Ctx);
}

static bool canUsePrivateLabel(const MCAsmInfo &AsmInfo,
                               const MCSection &Section) {
  if (!AsmInfo.isSectionAtomizableBySymbols(Section))
    return true;

  // If it is not dead stripped, it is safe to use private labels.
  const MCSectionMachO &SMO = cast<MCSectionMachO>(Section);
  if (SMO.hasAttribute(MachO::S_ATTR_NO_DEAD_STRIP))
    return true;

  return false;
}

void TargetLoweringObjectFileMachO::getNameWithPrefix(
    SmallVectorImpl<char> &OutName, const GlobalValue *GV,
    const TargetMachine &TM) const {
  bool CannotUsePrivateLabel = true;
  if (auto *GO = GV->getBaseObject()) {
    SectionKind GOKind = TargetLoweringObjectFile::getKindForGlobal(GO, TM);
    const MCSection *TheSection = SectionForGlobal(GO, GOKind, TM);
    CannotUsePrivateLabel =
        !canUsePrivateLabel(*TM.getMCAsmInfo(), *TheSection);
  }
  getMangler().getNameWithPrefix(OutName, GV, CannotUsePrivateLabel);
}

//===----------------------------------------------------------------------===//
//                                  COFF
//===----------------------------------------------------------------------===//

static unsigned
getCOFFSectionFlags(SectionKind K, const TargetMachine &TM) {
  unsigned Flags = 0;
  bool isThumb = TM.getTargetTriple().getArch() == Triple::thumb;

  if (K.isMetadata())
    Flags |=
      COFF::IMAGE_SCN_MEM_DISCARDABLE;
  else if (K.isText())
    Flags |=
      COFF::IMAGE_SCN_MEM_EXECUTE |
      COFF::IMAGE_SCN_MEM_READ |
      COFF::IMAGE_SCN_CNT_CODE |
      (isThumb ? COFF::IMAGE_SCN_MEM_16BIT : (COFF::SectionCharacteristics)0);
  else if (K.isBSS())
    Flags |=
      COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA |
      COFF::IMAGE_SCN_MEM_READ |
      COFF::IMAGE_SCN_MEM_WRITE;
  else if (K.isThreadLocal())
    Flags |=
      COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
      COFF::IMAGE_SCN_MEM_READ |
      COFF::IMAGE_SCN_MEM_WRITE;
  else if (K.isReadOnly() || K.isReadOnlyWithRel())
    Flags |=
      COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
      COFF::IMAGE_SCN_MEM_READ;
  else if (K.isWriteable())
    Flags |=
      COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
      COFF::IMAGE_SCN_MEM_READ |
      COFF::IMAGE_SCN_MEM_WRITE;

  return Flags;
}

static const GlobalValue *getComdatGVForCOFF(const GlobalValue *GV) {
  const Comdat *C = GV->getComdat();
  assert(C && "expected GV to have a Comdat!");

  StringRef ComdatGVName = C->getName();
  const GlobalValue *ComdatGV = GV->getParent()->getNamedValue(ComdatGVName);
  if (!ComdatGV)
    report_fatal_error("Associative COMDAT symbol '" + ComdatGVName +
                       "' does not exist.");

  if (ComdatGV->getComdat() != C)
    report_fatal_error("Associative COMDAT symbol '" + ComdatGVName +
                       "' is not a key for its COMDAT.");

  return ComdatGV;
}

static int getSelectionForCOFF(const GlobalValue *GV) {
  if (const Comdat *C = GV->getComdat()) {
    const GlobalValue *ComdatKey = getComdatGVForCOFF(GV);
    if (const auto *GA = dyn_cast<GlobalAlias>(ComdatKey))
      ComdatKey = GA->getBaseObject();
    if (ComdatKey == GV) {
      switch (C->getSelectionKind()) {
      case Comdat::Any:
        return COFF::IMAGE_COMDAT_SELECT_ANY;
      case Comdat::ExactMatch:
        return COFF::IMAGE_COMDAT_SELECT_EXACT_MATCH;
      case Comdat::Largest:
        return COFF::IMAGE_COMDAT_SELECT_LARGEST;
      case Comdat::NoDuplicates:
        return COFF::IMAGE_COMDAT_SELECT_NODUPLICATES;
      case Comdat::SameSize:
        return COFF::IMAGE_COMDAT_SELECT_SAME_SIZE;
      }
    } else {
      return COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE;
    }
  }
  return 0;
}

MCSection *TargetLoweringObjectFileCOFF::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  int Selection = 0;
  unsigned Characteristics = getCOFFSectionFlags(Kind, TM);
  StringRef Name = GO->getSection();
  StringRef COMDATSymName = "";
  if (GO->hasComdat()) {
    Selection = getSelectionForCOFF(GO);
    const GlobalValue *ComdatGV;
    if (Selection == COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE)
      ComdatGV = getComdatGVForCOFF(GO);
    else
      ComdatGV = GO;

    if (!ComdatGV->hasPrivateLinkage()) {
      MCSymbol *Sym = TM.getSymbol(ComdatGV);
      COMDATSymName = Sym->getName();
      Characteristics |= COFF::IMAGE_SCN_LNK_COMDAT;
    } else {
      Selection = 0;
    }
  }

  return getContext().getCOFFSection(Name, Characteristics, Kind, COMDATSymName,
                                     Selection);
}

static StringRef getCOFFSectionNameForUniqueGlobal(SectionKind Kind) {
  if (Kind.isText())
    return ".text";
  if (Kind.isBSS())
    return ".bss";
  if (Kind.isThreadLocal())
    return ".tls$";
  if (Kind.isReadOnly() || Kind.isReadOnlyWithRel())
    return ".rdata";
  return ".data";
}

MCSection *TargetLoweringObjectFileCOFF::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // If we have -ffunction-sections then we should emit the global value to a
  // uniqued section specifically for it.
  bool EmitUniquedSection;
  if (Kind.isText())
    EmitUniquedSection = TM.getFunctionSections();
  else
    EmitUniquedSection = TM.getDataSections();

  if ((EmitUniquedSection && !Kind.isCommon()) || GO->hasComdat()) {
    SmallString<256> Name = getCOFFSectionNameForUniqueGlobal(Kind);

    unsigned Characteristics = getCOFFSectionFlags(Kind, TM);

    Characteristics |= COFF::IMAGE_SCN_LNK_COMDAT;
    int Selection = getSelectionForCOFF(GO);
    if (!Selection)
      Selection = COFF::IMAGE_COMDAT_SELECT_NODUPLICATES;
    const GlobalValue *ComdatGV;
    if (GO->hasComdat())
      ComdatGV = getComdatGVForCOFF(GO);
    else
      ComdatGV = GO;

    unsigned UniqueID = MCContext::GenericSectionID;
    if (EmitUniquedSection)
      UniqueID = NextUniqueID++;

    if (!ComdatGV->hasPrivateLinkage()) {
      MCSymbol *Sym = TM.getSymbol(ComdatGV);
      StringRef COMDATSymName = Sym->getName();

      // Append "$symbol" to the section name *before* IR-level mangling is
      // applied when targetting mingw. This is what GCC does, and the ld.bfd
      // COFF linker will not properly handle comdats otherwise.
      if (getTargetTriple().isWindowsGNUEnvironment())
        raw_svector_ostream(Name) << '$' << ComdatGV->getName();

      return getContext().getCOFFSection(Name, Characteristics, Kind,
                                         COMDATSymName, Selection, UniqueID);
    } else {
      SmallString<256> TmpData;
      getMangler().getNameWithPrefix(TmpData, GO, /*CannotUsePrivateLabel=*/true);
      return getContext().getCOFFSection(Name, Characteristics, Kind, TmpData,
                                         Selection, UniqueID);
    }
  }

  if (Kind.isText())
    return TextSection;

  if (Kind.isThreadLocal())
    return TLSDataSection;

  if (Kind.isReadOnly() || Kind.isReadOnlyWithRel())
    return ReadOnlySection;

  // Note: we claim that common symbols are put in BSSSection, but they are
  // really emitted with the magic .comm directive, which creates a symbol table
  // entry but not a section.
  if (Kind.isBSS() || Kind.isCommon())
    return BSSSection;

  return DataSection;
}

void TargetLoweringObjectFileCOFF::getNameWithPrefix(
    SmallVectorImpl<char> &OutName, const GlobalValue *GV,
    const TargetMachine &TM) const {
  bool CannotUsePrivateLabel = false;
  if (GV->hasPrivateLinkage() &&
      ((isa<Function>(GV) && TM.getFunctionSections()) ||
       (isa<GlobalVariable>(GV) && TM.getDataSections())))
    CannotUsePrivateLabel = true;

  getMangler().getNameWithPrefix(OutName, GV, CannotUsePrivateLabel);
}

MCSection *TargetLoweringObjectFileCOFF::getSectionForJumpTable(
    const Function &F, const TargetMachine &TM) const {
  // If the function can be removed, produce a unique section so that
  // the table doesn't prevent the removal.
  const Comdat *C = F.getComdat();
  bool EmitUniqueSection = TM.getFunctionSections() || C;
  if (!EmitUniqueSection)
    return ReadOnlySection;

  // FIXME: we should produce a symbol for F instead.
  if (F.hasPrivateLinkage())
    return ReadOnlySection;

  MCSymbol *Sym = TM.getSymbol(&F);
  StringRef COMDATSymName = Sym->getName();

  SectionKind Kind = SectionKind::getReadOnly();
  StringRef SecName = getCOFFSectionNameForUniqueGlobal(Kind);
  unsigned Characteristics = getCOFFSectionFlags(Kind, TM);
  Characteristics |= COFF::IMAGE_SCN_LNK_COMDAT;
  unsigned UniqueID = NextUniqueID++;

  return getContext().getCOFFSection(
      SecName, Characteristics, Kind, COMDATSymName,
      COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE, UniqueID);
}

void TargetLoweringObjectFileCOFF::emitModuleMetadata(MCStreamer &Streamer,
                                                      Module &M) const {
  if (NamedMDNode *LinkerOptions = M.getNamedMetadata("llvm.linker.options")) {
    // Emit the linker options to the linker .drectve section.  According to the
    // spec, this section is a space-separated string containing flags for
    // linker.
    MCSection *Sec = getDrectveSection();
    Streamer.SwitchSection(Sec);
    for (const auto &Option : LinkerOptions->operands()) {
      for (const auto &Piece : cast<MDNode>(Option)->operands()) {
        // Lead with a space for consistency with our dllexport implementation.
        std::string Directive(" ");
        Directive.append(cast<MDString>(Piece)->getString());
        Streamer.EmitBytes(Directive);
      }
    }
  }

  unsigned Version = 0;
  unsigned Flags = 0;
  StringRef Section;

  GetObjCImageInfo(M, Version, Flags, Section);
  if (Section.empty())
    return;

  auto &C = getContext();
  auto *S = C.getCOFFSection(
      Section, COFF::IMAGE_SCN_CNT_INITIALIZED_DATA | COFF::IMAGE_SCN_MEM_READ,
      SectionKind::getReadOnly());
  Streamer.SwitchSection(S);
  Streamer.EmitLabel(C.getOrCreateSymbol(StringRef("OBJC_IMAGE_INFO")));
  Streamer.EmitIntValue(Version, 4);
  Streamer.EmitIntValue(Flags, 4);
  Streamer.AddBlankLine();
}

void TargetLoweringObjectFileCOFF::Initialize(MCContext &Ctx,
                                              const TargetMachine &TM) {
  TargetLoweringObjectFile::Initialize(Ctx, TM);
  const Triple &T = TM.getTargetTriple();
  if (T.isKnownWindowsMSVCEnvironment() || T.isWindowsItaniumEnvironment()) {
    StaticCtorSection =
        Ctx.getCOFFSection(".CRT$XCU", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                           COFF::IMAGE_SCN_MEM_READ,
                           SectionKind::getReadOnly());
    StaticDtorSection =
        Ctx.getCOFFSection(".CRT$XTX", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                           COFF::IMAGE_SCN_MEM_READ,
                           SectionKind::getReadOnly());
  } else {
    StaticCtorSection = Ctx.getCOFFSection(
        ".ctors", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                      COFF::IMAGE_SCN_MEM_READ | COFF::IMAGE_SCN_MEM_WRITE,
        SectionKind::getData());
    StaticDtorSection = Ctx.getCOFFSection(
        ".dtors", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                      COFF::IMAGE_SCN_MEM_READ | COFF::IMAGE_SCN_MEM_WRITE,
        SectionKind::getData());
  }
}

static MCSectionCOFF *getCOFFStaticStructorSection(MCContext &Ctx,
                                                   const Triple &T, bool IsCtor,
                                                   unsigned Priority,
                                                   const MCSymbol *KeySym,
                                                   MCSectionCOFF *Default) {
  if (T.isKnownWindowsMSVCEnvironment() || T.isWindowsItaniumEnvironment()) {
    // If the priority is the default, use .CRT$XCU, possibly associative.
    if (Priority == 65535)
      return Ctx.getAssociativeCOFFSection(Default, KeySym, 0);

    // Otherwise, we need to compute a new section name. Low priorities should
    // run earlier. The linker will sort sections ASCII-betically, and we need a
    // string that sorts between .CRT$XCA and .CRT$XCU. In the general case, we
    // make a name like ".CRT$XCT12345", since that runs before .CRT$XCU. Really
    // low priorities need to sort before 'L', since the CRT uses that
    // internally, so we use ".CRT$XCA00001" for them.
    SmallString<24> Name;
    raw_svector_ostream OS(Name);
    OS << ".CRT$XC" << (Priority < 200 ? 'A' : 'T') << format("%05u", Priority);
    MCSectionCOFF *Sec = Ctx.getCOFFSection(
        Name, COFF::IMAGE_SCN_CNT_INITIALIZED_DATA | COFF::IMAGE_SCN_MEM_READ,
        SectionKind::getReadOnly());
    return Ctx.getAssociativeCOFFSection(Sec, KeySym, 0);
  }

  std::string Name = IsCtor ? ".ctors" : ".dtors";
  if (Priority != 65535)
    raw_string_ostream(Name) << format(".%05u", 65535 - Priority);

  return Ctx.getAssociativeCOFFSection(
      Ctx.getCOFFSection(Name, COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                   COFF::IMAGE_SCN_MEM_READ |
                                   COFF::IMAGE_SCN_MEM_WRITE,
                         SectionKind::getData()),
      KeySym, 0);
}

MCSection *TargetLoweringObjectFileCOFF::getStaticCtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  return getCOFFStaticStructorSection(getContext(), getTargetTriple(), true,
                                      Priority, KeySym,
                                      cast<MCSectionCOFF>(StaticCtorSection));
}

MCSection *TargetLoweringObjectFileCOFF::getStaticDtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  return getCOFFStaticStructorSection(getContext(), getTargetTriple(), false,
                                      Priority, KeySym,
                                      cast<MCSectionCOFF>(StaticDtorSection));
}

void TargetLoweringObjectFileCOFF::emitLinkerFlagsForGlobal(
    raw_ostream &OS, const GlobalValue *GV) const {
  emitLinkerFlagsForGlobalCOFF(OS, GV, getTargetTriple(), getMangler());
}

void TargetLoweringObjectFileCOFF::emitLinkerFlagsForUsed(
    raw_ostream &OS, const GlobalValue *GV) const {
  emitLinkerFlagsForUsedCOFF(OS, GV, getTargetTriple(), getMangler());
}

const MCExpr *TargetLoweringObjectFileCOFF::lowerRelativeReference(
    const GlobalValue *LHS, const GlobalValue *RHS,
    const TargetMachine &TM) const {
  const Triple &T = TM.getTargetTriple();
  if (!T.isKnownWindowsMSVCEnvironment() &&
      !T.isWindowsItaniumEnvironment() &&
      !T.isWindowsCoreCLREnvironment())
    return nullptr;

  // Our symbols should exist in address space zero, cowardly no-op if
  // otherwise.
  if (LHS->getType()->getPointerAddressSpace() != 0 ||
      RHS->getType()->getPointerAddressSpace() != 0)
    return nullptr;

  // Both ptrtoint instructions must wrap global objects:
  // - Only global variables are eligible for image relative relocations.
  // - The subtrahend refers to the special symbol __ImageBase, a GlobalVariable.
  // We expect __ImageBase to be a global variable without a section, externally
  // defined.
  //
  // It should look something like this: @__ImageBase = external constant i8
  if (!isa<GlobalObject>(LHS) || !isa<GlobalVariable>(RHS) ||
      LHS->isThreadLocal() || RHS->isThreadLocal() ||
      RHS->getName() != "__ImageBase" || !RHS->hasExternalLinkage() ||
      cast<GlobalVariable>(RHS)->hasInitializer() || RHS->hasSection())
    return nullptr;

  return MCSymbolRefExpr::create(TM.getSymbol(LHS),
                                 MCSymbolRefExpr::VK_COFF_IMGREL32,
                                 getContext());
}

static std::string APIntToHexString(const APInt &AI) {
  unsigned Width = (AI.getBitWidth() / 8) * 2;
  std::string HexString = utohexstr(AI.getLimitedValue(), /*LowerCase=*/true);
  unsigned Size = HexString.size();
  assert(Width >= Size && "hex string is too large!");
  HexString.insert(HexString.begin(), Width - Size, '0');

  return HexString;
}

static std::string scalarConstantToHexString(const Constant *C) {
  Type *Ty = C->getType();
  if (isa<UndefValue>(C)) {
    return APIntToHexString(APInt::getNullValue(Ty->getPrimitiveSizeInBits()));
  } else if (const auto *CFP = dyn_cast<ConstantFP>(C)) {
    return APIntToHexString(CFP->getValueAPF().bitcastToAPInt());
  } else if (const auto *CI = dyn_cast<ConstantInt>(C)) {
    return APIntToHexString(CI->getValue());
  } else {
    unsigned NumElements;
    if (isa<VectorType>(Ty))
      NumElements = Ty->getVectorNumElements();
    else
      NumElements = Ty->getArrayNumElements();
    std::string HexString;
    for (int I = NumElements - 1, E = -1; I != E; --I)
      HexString += scalarConstantToHexString(C->getAggregateElement(I));
    return HexString;
  }
}

MCSection *TargetLoweringObjectFileCOFF::getSectionForConstant(
    const DataLayout &DL, SectionKind Kind, const Constant *C,
    unsigned &Align) const {
  if (Kind.isMergeableConst() && C &&
      getContext().getAsmInfo()->hasCOFFComdatConstants()) {
    // This creates comdat sections with the given symbol name, but unless
    // AsmPrinter::GetCPISymbol actually makes the symbol global, the symbol
    // will be created with a null storage class, which makes GNU binutils
    // error out.
    const unsigned Characteristics = COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                     COFF::IMAGE_SCN_MEM_READ |
                                     COFF::IMAGE_SCN_LNK_COMDAT;
    std::string COMDATSymName;
    if (Kind.isMergeableConst4()) {
      if (Align <= 4) {
        COMDATSymName = "__real@" + scalarConstantToHexString(C);
        Align = 4;
      }
    } else if (Kind.isMergeableConst8()) {
      if (Align <= 8) {
        COMDATSymName = "__real@" + scalarConstantToHexString(C);
        Align = 8;
      }
    } else if (Kind.isMergeableConst16()) {
      // FIXME: These may not be appropriate for non-x86 architectures.
      if (Align <= 16) {
        COMDATSymName = "__xmm@" + scalarConstantToHexString(C);
        Align = 16;
      }
    } else if (Kind.isMergeableConst32()) {
      if (Align <= 32) {
        COMDATSymName = "__ymm@" + scalarConstantToHexString(C);
        Align = 32;
      }
    }

    if (!COMDATSymName.empty())
      return getContext().getCOFFSection(".rdata", Characteristics, Kind,
                                         COMDATSymName,
                                         COFF::IMAGE_COMDAT_SELECT_ANY);
  }

  return TargetLoweringObjectFile::getSectionForConstant(DL, Kind, C, Align);
}


//===----------------------------------------------------------------------===//
//                                  Wasm
//===----------------------------------------------------------------------===//

static const Comdat *getWasmComdat(const GlobalValue *GV) {
  const Comdat *C = GV->getComdat();
  if (!C)
    return nullptr;

  if (C->getSelectionKind() != Comdat::Any)
    report_fatal_error("WebAssembly COMDATs only support "
                       "SelectionKind::Any, '" + C->getName() + "' cannot be "
                       "lowered.");

  return C;
}

static SectionKind getWasmKindForNamedSection(StringRef Name, SectionKind K) {
  // If we're told we have function data, then use that.
  if (K.isText())
    return SectionKind::getText();

  // Otherwise, ignore whatever section type the generic impl detected and use
  // a plain data section.
  return SectionKind::getData();
}

MCSection *TargetLoweringObjectFileWasm::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // We don't support explict section names for functions in the wasm object
  // format.  Each function has to be in its own unique section.
  if (isa<Function>(GO)) {
    return SelectSectionForGlobal(GO, Kind, TM);
  }

  StringRef Name = GO->getSection();

  Kind = getWasmKindForNamedSection(Name, Kind);

  StringRef Group = "";
  if (const Comdat *C = getWasmComdat(GO)) {
    Group = C->getName();
  }

  return getContext().getWasmSection(Name, Kind, Group,
                                     MCContext::GenericSectionID);
}

static MCSectionWasm *selectWasmSectionForGlobal(
    MCContext &Ctx, const GlobalObject *GO, SectionKind Kind, Mangler &Mang,
    const TargetMachine &TM, bool EmitUniqueSection, unsigned *NextUniqueID) {
  StringRef Group = "";
  if (const Comdat *C = getWasmComdat(GO)) {
    Group = C->getName();
  }

  bool UniqueSectionNames = TM.getUniqueSectionNames();
  SmallString<128> Name = getSectionPrefixForGlobal(Kind);

  if (const auto *F = dyn_cast<Function>(GO)) {
    const auto &OptionalPrefix = F->getSectionPrefix();
    if (OptionalPrefix)
      Name += *OptionalPrefix;
  }

  if (EmitUniqueSection && UniqueSectionNames) {
    Name.push_back('.');
    TM.getNameWithPrefix(Name, GO, Mang, true);
  }
  unsigned UniqueID = MCContext::GenericSectionID;
  if (EmitUniqueSection && !UniqueSectionNames) {
    UniqueID = *NextUniqueID;
    (*NextUniqueID)++;
  }
  return Ctx.getWasmSection(Name, Kind, Group, UniqueID);
}

MCSection *TargetLoweringObjectFileWasm::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {

  if (Kind.isCommon())
    report_fatal_error("mergable sections not supported yet on wasm");

  // If we have -ffunction-section or -fdata-section then we should emit the
  // global value to a uniqued section specifically for it.
  bool EmitUniqueSection = false;
  if (Kind.isText())
    EmitUniqueSection = TM.getFunctionSections();
  else
    EmitUniqueSection = TM.getDataSections();
  EmitUniqueSection |= GO->hasComdat();

  return selectWasmSectionForGlobal(getContext(), GO, Kind, getMangler(), TM,
                                    EmitUniqueSection, &NextUniqueID);
}

bool TargetLoweringObjectFileWasm::shouldPutJumpTableInFunctionSection(
    bool UsesLabelDifference, const Function &F) const {
  // We can always create relative relocations, so use another section
  // that can be marked non-executable.
  return false;
}

const MCExpr *TargetLoweringObjectFileWasm::lowerRelativeReference(
    const GlobalValue *LHS, const GlobalValue *RHS,
    const TargetMachine &TM) const {
  // We may only use a PLT-relative relocation to refer to unnamed_addr
  // functions.
  if (!LHS->hasGlobalUnnamedAddr() || !LHS->getValueType()->isFunctionTy())
    return nullptr;

  // Basic sanity checks.
  if (LHS->getType()->getPointerAddressSpace() != 0 ||
      RHS->getType()->getPointerAddressSpace() != 0 || LHS->isThreadLocal() ||
      RHS->isThreadLocal())
    return nullptr;

  return MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(TM.getSymbol(LHS), MCSymbolRefExpr::VK_None,
                              getContext()),
      MCSymbolRefExpr::create(TM.getSymbol(RHS), getContext()), getContext());
}

void TargetLoweringObjectFileWasm::InitializeWasm() {
  StaticCtorSection =
      getContext().getWasmSection(".init_array", SectionKind::getData());

  // We don't use PersonalityEncoding and LSDAEncoding because we don't emit
  // .cfi directives. We use TTypeEncoding to encode typeinfo global variables.
  TTypeEncoding = dwarf::DW_EH_PE_absptr;
}

MCSection *TargetLoweringObjectFileWasm::getStaticCtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  return Priority == UINT16_MAX ?
         StaticCtorSection :
         getContext().getWasmSection(".init_array." + utostr(Priority),
                                     SectionKind::getData());
}

MCSection *TargetLoweringObjectFileWasm::getStaticDtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  llvm_unreachable("@llvm.global_dtors should have been lowered already");
  return nullptr;
}
