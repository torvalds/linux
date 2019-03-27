//===-- AMDGPUHSATargetObjectFile.cpp - AMDGPU Object Files ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUTargetObjectFile.h"
#include "AMDGPU.h"
#include "AMDGPUTargetMachine.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// Generic Object File
//===----------------------------------------------------------------------===//

MCSection *AMDGPUTargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  if (Kind.isReadOnly() && AMDGPU::isReadOnlySegment(GO) &&
      AMDGPU::shouldEmitConstantsToTextSection(TM.getTargetTriple()))
    return TextSection;

  return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, Kind, TM);
}

MCSection *AMDGPUTargetObjectFile::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind SK, const TargetMachine &TM) const {
  // Set metadata access for the explicit section
  StringRef SectionName = GO->getSection();
  if (SectionName.startswith(".AMDGPU.comment."))
    SK = SectionKind::getMetadata();

  return TargetLoweringObjectFileELF::getExplicitSectionGlobal(GO, SK, TM);
}
