//===-- AVRMCAsmInfo.cpp - AVR asm properties -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the AVRMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "AVRMCAsmInfo.h"

#include "llvm/ADT/Triple.h"

namespace llvm {

AVRMCAsmInfo::AVRMCAsmInfo(const Triple &TT) {
  CodePointerSize = 2;
  CalleeSaveStackSlotSize = 2;
  CommentString = ";";
  PrivateGlobalPrefix = ".L";
  UsesELFSectionDirectiveForBSS = true;
  UseIntegratedAssembler = true;
}

} // end of namespace llvm
