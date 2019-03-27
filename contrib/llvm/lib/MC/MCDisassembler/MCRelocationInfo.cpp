//===-- MCRelocationInfo.cpp ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCDisassembler/MCRelocationInfo.h"
#include "llvm-c/Disassembler.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

MCRelocationInfo::MCRelocationInfo(MCContext &Ctx) : Ctx(Ctx) {}

MCRelocationInfo::~MCRelocationInfo() = default;

const MCExpr *
MCRelocationInfo::createExprForCAPIVariantKind(const MCExpr *SubExpr,
                                               unsigned VariantKind) {
  if (VariantKind != LLVMDisassembler_VariantKind_None)
    return nullptr;
  return SubExpr;
}

MCRelocationInfo *llvm::createMCRelocationInfo(const Triple &TT,
                                               MCContext &Ctx) {
  return new MCRelocationInfo(Ctx);
}
