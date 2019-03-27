//===- MSP430.cpp ---------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The MSP430 is a 16-bit microcontroller RISC architecture. The instruction set
// has only 27 core instructions orthogonally augmented with a variety
// of addressing modes for source and destination operands. Entire address space
// of MSP430 is 64KB (the extended MSP430X architecture is not considered here).
// A typical MSP430 MCU has several kilobytes of RAM and ROM, plenty
// of peripherals and is generally optimized for a low power consumption.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Symbols.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class MSP430 final : public TargetInfo {
public:
  MSP430();
  RelExpr getRelExpr(RelType Type, const Symbol &S,
                     const uint8_t *Loc) const override;
  void relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const override;
};
} // namespace

MSP430::MSP430() {
  // mov.b #0, r3
  TrapInstr = {0x43, 0x43, 0x43, 0x43};
}

RelExpr MSP430::getRelExpr(RelType Type, const Symbol &S,
                           const uint8_t *Loc) const {
  switch (Type) {
  case R_MSP430_10_PCREL:
  case R_MSP430_16_PCREL:
  case R_MSP430_16_PCREL_BYTE:
  case R_MSP430_2X_PCREL:
  case R_MSP430_RL_PCREL:
  case R_MSP430_SYM_DIFF:
    return R_PC;
  default:
    return R_ABS;
  }
}

void MSP430::relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const {
  switch (Type) {
  case R_MSP430_8:
    checkIntUInt(Loc, Val, 8, Type);
    *Loc = Val;
    break;
  case R_MSP430_16:
  case R_MSP430_16_PCREL:
  case R_MSP430_16_BYTE:
  case R_MSP430_16_PCREL_BYTE:
    checkIntUInt(Loc, Val, 16, Type);
    write16le(Loc, Val);
    break;
  case R_MSP430_32:
    checkIntUInt(Loc, Val, 32, Type);
    write32le(Loc, Val);
    break;
  case R_MSP430_10_PCREL: {
    int16_t Offset = ((int16_t)Val >> 1) - 1;
    checkInt(Loc, Offset, 10, Type);
    write16le(Loc, (read16le(Loc) & 0xFC00) | (Offset & 0x3FF));
    break;
  }
  default:
    error(getErrorLocation(Loc) + "unrecognized reloc " + toString(Type));
  }
}

TargetInfo *elf::getMSP430TargetInfo() {
  static MSP430 Target;
  return &Target;
}
