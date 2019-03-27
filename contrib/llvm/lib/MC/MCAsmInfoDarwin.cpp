//===- MCAsmInfoDarwin.cpp - Darwin asm properties ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines target asm properties related what form asm statements
// should take in general on Darwin-based targets
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmInfoDarwin.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCSectionMachO.h"

using namespace llvm;

bool MCAsmInfoDarwin::isSectionAtomizableBySymbols(
    const MCSection &Section) const {
  const MCSectionMachO &SMO = static_cast<const MCSectionMachO &>(Section);

  // Sections holding 1 byte strings are atomized based on the data they
  // contain.
  // Sections holding 2 byte strings require symbols in order to be atomized.
  // There is no dedicated section for 4 byte strings.
  if (SMO.getType() == MachO::S_CSTRING_LITERALS)
    return false;

  if (SMO.getSegmentName() == "__DATA" && SMO.getSectionName() == "__cfstring")
    return false;

  if (SMO.getSegmentName() == "__DATA" &&
      SMO.getSectionName() == "__objc_classrefs")
    return false;

  switch (SMO.getType()) {
  default:
    return true;

  // These sections are atomized at the element boundaries without using
  // symbols.
  case MachO::S_4BYTE_LITERALS:
  case MachO::S_8BYTE_LITERALS:
  case MachO::S_16BYTE_LITERALS:
  case MachO::S_LITERAL_POINTERS:
  case MachO::S_NON_LAZY_SYMBOL_POINTERS:
  case MachO::S_LAZY_SYMBOL_POINTERS:
  case MachO::S_THREAD_LOCAL_VARIABLE_POINTERS:
  case MachO::S_MOD_INIT_FUNC_POINTERS:
  case MachO::S_MOD_TERM_FUNC_POINTERS:
  case MachO::S_INTERPOSING:
    return false;
  }
}

MCAsmInfoDarwin::MCAsmInfoDarwin() {
  // Common settings for all Darwin targets.
  // Syntax:
  LinkerPrivateGlobalPrefix = "l";
  HasSingleParameterDotFile = false;
  HasSubsectionsViaSymbols = true;

  AlignmentIsInBytes = false;
  COMMDirectiveAlignmentIsInBytes = false;
  LCOMMDirectiveAlignmentType = LCOMM::Log2Alignment;
  InlineAsmStart = " InlineAsm Start";
  InlineAsmEnd = " InlineAsm End";

  // Directives:
  HasWeakDefDirective = true;
  HasWeakDefCanBeHiddenDirective = true;
  WeakRefDirective = "\t.weak_reference ";
  ZeroDirective = "\t.space\t";  // ".space N" emits N zeros.
  HasMachoZeroFillDirective = true;  // Uses .zerofill
  HasMachoTBSSDirective = true; // Uses .tbss

  // FIXME: Change this once MC is the system assembler.
  HasAggressiveSymbolFolding = false;

  HiddenVisibilityAttr = MCSA_PrivateExtern;
  HiddenDeclarationVisibilityAttr = MCSA_Invalid;

  // Doesn't support protected visibility.
  ProtectedVisibilityAttr = MCSA_Invalid;

  HasDotTypeDotSizeDirective = false;
  HasNoDeadStrip = true;
  HasAltEntry = true;

  DwarfUsesRelocationsAcrossSections = false;

  UseIntegratedAssembler = true;
  SetDirectiveSuppressesReloc = true;
}
