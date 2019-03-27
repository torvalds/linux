//===-- MCTargetDesc/AMDGPUMCAsmInfo.cpp - Assembly Info ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
/// \file
//===----------------------------------------------------------------------===//

#include "AMDGPUMCAsmInfo.h"
#include "llvm/ADT/Triple.h"

using namespace llvm;

AMDGPUMCAsmInfo::AMDGPUMCAsmInfo(const Triple &TT) : MCAsmInfoELF() {
  CodePointerSize = (TT.getArch() == Triple::amdgcn) ? 8 : 4;
  StackGrowsUp = true;
  HasSingleParameterDotFile = false;
  //===------------------------------------------------------------------===//
  MinInstAlignment = 4;
  MaxInstLength = (TT.getArch() == Triple::amdgcn) ? 8 : 16;
  SeparatorString = "\n";
  CommentString = ";";
  PrivateLabelPrefix = "";
  InlineAsmStart = ";#ASMSTART";
  InlineAsmEnd = ";#ASMEND";

  //===--- Data Emission Directives -------------------------------------===//
  SunStyleELFSectionSwitchSyntax = true;
  UsesELFSectionDirectiveForBSS = true;

  //===--- Global Variable Emission Directives --------------------------===//
  HasAggressiveSymbolFolding = true;
  COMMDirectiveAlignmentIsInBytes = false;
  HasNoDeadStrip = true;
  WeakRefDirective = ".weakref\t";
  //===--- Dwarf Emission Directives -----------------------------------===//
  SupportsDebugInformation = true;
}

bool AMDGPUMCAsmInfo::shouldOmitSectionDirective(StringRef SectionName) const {
  return SectionName == ".hsatext" || SectionName == ".hsadata_global_agent" ||
         SectionName == ".hsadata_global_program" ||
         SectionName == ".hsarodata_readonly_agent" ||
         MCAsmInfo::shouldOmitSectionDirective(SectionName);
}
