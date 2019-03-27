//===-- XCoreTargetObjectFile.cpp - XCore object files --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "XCoreTargetObjectFile.h"
#include "XCoreSubtarget.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;


void XCoreTargetObjectFile::Initialize(MCContext &Ctx, const TargetMachine &TM){
  TargetLoweringObjectFileELF::Initialize(Ctx, TM);

  BSSSection = Ctx.getELFSection(".dp.bss", ELF::SHT_NOBITS,
                                 ELF::SHF_ALLOC | ELF::SHF_WRITE |
                                     ELF::XCORE_SHF_DP_SECTION);
  BSSSectionLarge = Ctx.getELFSection(".dp.bss.large", ELF::SHT_NOBITS,
                                      ELF::SHF_ALLOC | ELF::SHF_WRITE |
                                          ELF::XCORE_SHF_DP_SECTION);
  DataSection = Ctx.getELFSection(".dp.data", ELF::SHT_PROGBITS,
                                  ELF::SHF_ALLOC | ELF::SHF_WRITE |
                                      ELF::XCORE_SHF_DP_SECTION);
  DataSectionLarge = Ctx.getELFSection(".dp.data.large", ELF::SHT_PROGBITS,
                                       ELF::SHF_ALLOC | ELF::SHF_WRITE |
                                           ELF::XCORE_SHF_DP_SECTION);
  DataRelROSection = Ctx.getELFSection(".dp.rodata", ELF::SHT_PROGBITS,
                                       ELF::SHF_ALLOC | ELF::SHF_WRITE |
                                           ELF::XCORE_SHF_DP_SECTION);
  DataRelROSectionLarge = Ctx.getELFSection(
      ".dp.rodata.large", ELF::SHT_PROGBITS,
      ELF::SHF_ALLOC | ELF::SHF_WRITE | ELF::XCORE_SHF_DP_SECTION);
  ReadOnlySection =
      Ctx.getELFSection(".cp.rodata", ELF::SHT_PROGBITS,
                        ELF::SHF_ALLOC | ELF::XCORE_SHF_CP_SECTION);
  ReadOnlySectionLarge =
      Ctx.getELFSection(".cp.rodata.large", ELF::SHT_PROGBITS,
                        ELF::SHF_ALLOC | ELF::XCORE_SHF_CP_SECTION);
  MergeableConst4Section = Ctx.getELFSection(
      ".cp.rodata.cst4", ELF::SHT_PROGBITS,
      ELF::SHF_ALLOC | ELF::SHF_MERGE | ELF::XCORE_SHF_CP_SECTION, 4, "");
  MergeableConst8Section = Ctx.getELFSection(
      ".cp.rodata.cst8", ELF::SHT_PROGBITS,
      ELF::SHF_ALLOC | ELF::SHF_MERGE | ELF::XCORE_SHF_CP_SECTION, 8, "");
  MergeableConst16Section = Ctx.getELFSection(
      ".cp.rodata.cst16", ELF::SHT_PROGBITS,
      ELF::SHF_ALLOC | ELF::SHF_MERGE | ELF::XCORE_SHF_CP_SECTION, 16, "");
  CStringSection =
      Ctx.getELFSection(".cp.rodata.string", ELF::SHT_PROGBITS,
                        ELF::SHF_ALLOC | ELF::SHF_MERGE | ELF::SHF_STRINGS |
                            ELF::XCORE_SHF_CP_SECTION);
  // TextSection       - see MObjectFileInfo.cpp
  // StaticCtorSection - see MObjectFileInfo.cpp
  // StaticDtorSection - see MObjectFileInfo.cpp
 }

static unsigned getXCoreSectionType(SectionKind K) {
  if (K.isBSS())
    return ELF::SHT_NOBITS;
  return ELF::SHT_PROGBITS;
}

static unsigned getXCoreSectionFlags(SectionKind K, bool IsCPRel) {
  unsigned Flags = 0;

  if (!K.isMetadata())
    Flags |= ELF::SHF_ALLOC;

  if (K.isText())
    Flags |= ELF::SHF_EXECINSTR;
  else if (IsCPRel)
    Flags |= ELF::XCORE_SHF_CP_SECTION;
  else
    Flags |= ELF::XCORE_SHF_DP_SECTION;

  if (K.isWriteable())
    Flags |= ELF::SHF_WRITE;

  if (K.isMergeableCString() || K.isMergeableConst4() ||
      K.isMergeableConst8() || K.isMergeableConst16())
    Flags |= ELF::SHF_MERGE;

  if (K.isMergeableCString())
    Flags |= ELF::SHF_STRINGS;

  return Flags;
}

MCSection *XCoreTargetObjectFile::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  StringRef SectionName = GO->getSection();
  // Infer section flags from the section name if we can.
  bool IsCPRel = SectionName.startswith(".cp.");
  if (IsCPRel && !Kind.isReadOnly())
    report_fatal_error("Using .cp. section for writeable object.");
  return getContext().getELFSection(SectionName, getXCoreSectionType(Kind),
                                    getXCoreSectionFlags(Kind, IsCPRel));
}

MCSection *XCoreTargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {

  bool UseCPRel = GO->hasLocalLinkage();

  if (Kind.isText())                    return TextSection;
  if (UseCPRel) {
    if (Kind.isMergeable1ByteCString()) return CStringSection;
    if (Kind.isMergeableConst4())       return MergeableConst4Section;
    if (Kind.isMergeableConst8())       return MergeableConst8Section;
    if (Kind.isMergeableConst16())      return MergeableConst16Section;
  }
  Type *ObjType = GO->getValueType();
  auto &DL = GO->getParent()->getDataLayout();
  if (TM.getCodeModel() == CodeModel::Small || !ObjType->isSized() ||
      DL.getTypeAllocSize(ObjType) < CodeModelLargeSize) {
    if (Kind.isReadOnly())              return UseCPRel? ReadOnlySection
                                                       : DataRelROSection;
    if (Kind.isBSS() || Kind.isCommon())return BSSSection;
    if (Kind.isData())
      return DataSection;
    if (Kind.isReadOnlyWithRel())       return DataRelROSection;
  } else {
    if (Kind.isReadOnly())              return UseCPRel? ReadOnlySectionLarge
                                                       : DataRelROSectionLarge;
    if (Kind.isBSS() || Kind.isCommon())return BSSSectionLarge;
    if (Kind.isData())
      return DataSectionLarge;
    if (Kind.isReadOnlyWithRel())       return DataRelROSectionLarge;
  }

  assert((Kind.isThreadLocal() || Kind.isCommon()) && "Unknown section kind");
  report_fatal_error("Target does not support TLS or Common sections");
}

MCSection *XCoreTargetObjectFile::getSectionForConstant(const DataLayout &DL,
                                                        SectionKind Kind,
                                                        const Constant *C,
                                                        unsigned &Align) const {
  if (Kind.isMergeableConst4())           return MergeableConst4Section;
  if (Kind.isMergeableConst8())           return MergeableConst8Section;
  if (Kind.isMergeableConst16())          return MergeableConst16Section;
  assert((Kind.isReadOnly() || Kind.isReadOnlyWithRel()) &&
         "Unknown section kind");
  // We assume the size of the object is never greater than CodeModelLargeSize.
  // To handle CodeModelLargeSize changes to AsmPrinter would be required.
  return ReadOnlySection;
}
