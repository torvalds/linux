//===- MCAsmBackend.cpp - Target MC Assembly Backend ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmBackend.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCCodePadder.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCWasmObjectWriter.h"
#include "llvm/MC/MCWinCOFFObjectWriter.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace llvm;

MCAsmBackend::MCAsmBackend(support::endianness Endian)
    : CodePadder(new MCCodePadder()), Endian(Endian) {}

MCAsmBackend::~MCAsmBackend() = default;

std::unique_ptr<MCObjectWriter>
MCAsmBackend::createObjectWriter(raw_pwrite_stream &OS) const {
  auto TW = createObjectTargetWriter();
  switch (TW->getFormat()) {
  case Triple::ELF:
    return createELFObjectWriter(cast<MCELFObjectTargetWriter>(std::move(TW)), OS,
                                 Endian == support::little);
  case Triple::MachO:
    return createMachObjectWriter(cast<MCMachObjectTargetWriter>(std::move(TW)),
                                  OS, Endian == support::little);
  case Triple::COFF:
    return createWinCOFFObjectWriter(
        cast<MCWinCOFFObjectTargetWriter>(std::move(TW)), OS);
  case Triple::Wasm:
    return createWasmObjectWriter(cast<MCWasmObjectTargetWriter>(std::move(TW)),
                                  OS);
  default:
    llvm_unreachable("unexpected object format");
  }
}

std::unique_ptr<MCObjectWriter>
MCAsmBackend::createDwoObjectWriter(raw_pwrite_stream &OS,
                                    raw_pwrite_stream &DwoOS) const {
  auto TW = createObjectTargetWriter();
  if (TW->getFormat() != Triple::ELF)
    report_fatal_error("dwo only supported with ELF");
  return createELFDwoObjectWriter(cast<MCELFObjectTargetWriter>(std::move(TW)),
                                  OS, DwoOS, Endian == support::little);
}

Optional<MCFixupKind> MCAsmBackend::getFixupKind(StringRef Name) const {
  return None;
}

const MCFixupKindInfo &MCAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  static const MCFixupKindInfo Builtins[] = {
      {"FK_Data_1", 0, 8, 0},
      {"FK_Data_2", 0, 16, 0},
      {"FK_Data_4", 0, 32, 0},
      {"FK_Data_8", 0, 64, 0},
      {"FK_PCRel_1", 0, 8, MCFixupKindInfo::FKF_IsPCRel},
      {"FK_PCRel_2", 0, 16, MCFixupKindInfo::FKF_IsPCRel},
      {"FK_PCRel_4", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"FK_PCRel_8", 0, 64, MCFixupKindInfo::FKF_IsPCRel},
      {"FK_GPRel_1", 0, 8, 0},
      {"FK_GPRel_2", 0, 16, 0},
      {"FK_GPRel_4", 0, 32, 0},
      {"FK_GPRel_8", 0, 64, 0},
      {"FK_DTPRel_4", 0, 32, 0},
      {"FK_DTPRel_8", 0, 64, 0},
      {"FK_TPRel_4", 0, 32, 0},
      {"FK_TPRel_8", 0, 64, 0},
      {"FK_SecRel_1", 0, 8, 0},
      {"FK_SecRel_2", 0, 16, 0},
      {"FK_SecRel_4", 0, 32, 0},
      {"FK_SecRel_8", 0, 64, 0},
      {"FK_Data_Add_1", 0, 8, 0},
      {"FK_Data_Add_2", 0, 16, 0},
      {"FK_Data_Add_4", 0, 32, 0},
      {"FK_Data_Add_8", 0, 64, 0},
      {"FK_Data_Sub_1", 0, 8, 0},
      {"FK_Data_Sub_2", 0, 16, 0},
      {"FK_Data_Sub_4", 0, 32, 0},
      {"FK_Data_Sub_8", 0, 64, 0}};

  assert((size_t)Kind <= array_lengthof(Builtins) && "Unknown fixup kind");
  return Builtins[Kind];
}

bool MCAsmBackend::fixupNeedsRelaxationAdvanced(
    const MCFixup &Fixup, bool Resolved, uint64_t Value,
    const MCRelaxableFragment *DF, const MCAsmLayout &Layout,
    const bool WasForced) const {
  if (!Resolved)
    return true;
  return fixupNeedsRelaxation(Fixup, Value, DF, Layout);
}

void MCAsmBackend::handleCodePaddingBasicBlockStart(
    MCObjectStreamer *OS, const MCCodePaddingContext &Context) {
  CodePadder->handleBasicBlockStart(OS, Context);
}

void MCAsmBackend::handleCodePaddingBasicBlockEnd(
    const MCCodePaddingContext &Context) {
  CodePadder->handleBasicBlockEnd(Context);
}

void MCAsmBackend::handleCodePaddingInstructionBegin(const MCInst &Inst) {
  CodePadder->handleInstructionBegin(Inst);
}

void MCAsmBackend::handleCodePaddingInstructionEnd(const MCInst &Inst) {
  CodePadder->handleInstructionEnd(Inst);
}

bool MCAsmBackend::relaxFragment(MCPaddingFragment *PF, MCAsmLayout &Layout) {
  return CodePadder->relaxFragment(PF, Layout);
}
