//===-- NVPTXMCAsmInfo.cpp - NVPTX asm properties -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the NVPTXMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "NVPTXMCAsmInfo.h"
#include "llvm/ADT/Triple.h"

using namespace llvm;

void NVPTXMCAsmInfo::anchor() {}

NVPTXMCAsmInfo::NVPTXMCAsmInfo(const Triple &TheTriple) {
  if (TheTriple.getArch() == Triple::nvptx64) {
    CodePointerSize = CalleeSaveStackSlotSize = 8;
  }

  CommentString = "//";

  HasSingleParameterDotFile = false;

  InlineAsmStart = " begin inline asm";
  InlineAsmEnd = " end inline asm";

  SupportsDebugInformation = true;
  // PTX does not allow .align on functions.
  HasFunctionAlignment = false;
  HasDotTypeDotSizeDirective = false;
  // PTX does not allow .hidden or .protected
  HiddenDeclarationVisibilityAttr = HiddenVisibilityAttr = MCSA_Invalid;
  ProtectedVisibilityAttr = MCSA_Invalid;

  // FIXME: remove comment once debug info is properly supported.
  Data8bitsDirective = "// .b8 ";
  Data16bitsDirective = nullptr; // not supported
  Data32bitsDirective = "// .b32 ";
  Data64bitsDirective = "// .b64 ";
  ZeroDirective = "// .b8";
  AsciiDirective = nullptr; // not supported
  AscizDirective = nullptr; // not supported
  SupportsQuotedNames = false;
  SupportsExtendedDwarfLocDirective = false;

  // @TODO: Can we just disable this?
  WeakDirective = "\t// .weak\t";
  GlobalDirective = "\t// .globl\t";
}
