//===-- Hexagon.cpp -------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class Hexagon final : public TargetInfo {
public:
  Hexagon();
  uint32_t calcEFlags() const override;
  RelExpr getRelExpr(RelType Type, const Symbol &S,
                     const uint8_t *Loc) const override;
  void relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void writePltHeader(uint8_t *Buf) const override;
  void writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
};
} // namespace

Hexagon::Hexagon() {
  PltRel = R_HEX_JMP_SLOT;
  RelativeRel = R_HEX_RELATIVE;
  GotRel = R_HEX_GLOB_DAT;
  GotEntrySize = 4;
  // The zero'th GOT entry is reserved for the address of _DYNAMIC.  The
  // next 3 are reserved for the dynamic loader.
  GotPltHeaderEntriesNum = 4;
  GotPltEntrySize = 4;

  PltEntrySize = 16;
  PltHeaderSize = 32;

  // Hexagon Linux uses 64K pages by default.
  DefaultMaxPageSize = 0x10000;
  NoneRel = R_HEX_NONE;
}

uint32_t Hexagon::calcEFlags() const {
  assert(!ObjectFiles.empty());

  // The architecture revision must always be equal to or greater than
  // greatest revision in the list of inputs.
  uint32_t Ret = 0;
  for (InputFile *F : ObjectFiles) {
    uint32_t EFlags = cast<ObjFile<ELF32LE>>(F)->getObj().getHeader()->e_flags;
    if (EFlags > Ret)
      Ret = EFlags;
  }
  return Ret;
}

static uint32_t applyMask(uint32_t Mask, uint32_t Data) {
  uint32_t Result = 0;
  size_t Off = 0;

  for (size_t Bit = 0; Bit != 32; ++Bit) {
    uint32_t ValBit = (Data >> Off) & 1;
    uint32_t MaskBit = (Mask >> Bit) & 1;
    if (MaskBit) {
      Result |= (ValBit << Bit);
      ++Off;
    }
  }
  return Result;
}

RelExpr Hexagon::getRelExpr(RelType Type, const Symbol &S,
                            const uint8_t *Loc) const {
  switch (Type) {
  case R_HEX_B9_PCREL:
  case R_HEX_B9_PCREL_X:
  case R_HEX_B13_PCREL:
  case R_HEX_B15_PCREL:
  case R_HEX_B15_PCREL_X:
  case R_HEX_6_PCREL_X:
  case R_HEX_32_PCREL:
    return R_PC;
  case R_HEX_B22_PCREL:
  case R_HEX_PLT_B22_PCREL:
  case R_HEX_B22_PCREL_X:
  case R_HEX_B32_PCREL_X:
    return R_PLT_PC;
  case R_HEX_GOT_11_X:
  case R_HEX_GOT_16_X:
  case R_HEX_GOT_32_6_X:
    return R_HEXAGON_GOT;
  default:
    return R_ABS;
  }
}

static uint32_t findMaskR6(uint32_t Insn) {
  // There are (arguably too) many relocation masks for the DSP's
  // R_HEX_6_X type.  The table below is used to select the correct mask
  // for the given instruction.
  struct InstructionMask {
    uint32_t CmpMask;
    uint32_t RelocMask;
  };

  static const InstructionMask R6[] = {
      {0x38000000, 0x0000201f}, {0x39000000, 0x0000201f},
      {0x3e000000, 0x00001f80}, {0x3f000000, 0x00001f80},
      {0x40000000, 0x000020f8}, {0x41000000, 0x000007e0},
      {0x42000000, 0x000020f8}, {0x43000000, 0x000007e0},
      {0x44000000, 0x000020f8}, {0x45000000, 0x000007e0},
      {0x46000000, 0x000020f8}, {0x47000000, 0x000007e0},
      {0x6a000000, 0x00001f80}, {0x7c000000, 0x001f2000},
      {0x9a000000, 0x00000f60}, {0x9b000000, 0x00000f60},
      {0x9c000000, 0x00000f60}, {0x9d000000, 0x00000f60},
      {0x9f000000, 0x001f0100}, {0xab000000, 0x0000003f},
      {0xad000000, 0x0000003f}, {0xaf000000, 0x00030078},
      {0xd7000000, 0x006020e0}, {0xd8000000, 0x006020e0},
      {0xdb000000, 0x006020e0}, {0xdf000000, 0x006020e0}};

  // Duplex forms have a fixed mask and parse bits 15:14 are always
  // zero.  Non-duplex insns will always have at least one bit set in the
  // parse field.
  if ((0xC000 & Insn) == 0x0)
    return 0x03f00000;

  for (InstructionMask I : R6)
    if ((0xff000000 & Insn) == I.CmpMask)
      return I.RelocMask;

  error("unrecognized instruction for R_HEX_6 relocation: 0x" +
        utohexstr(Insn));
  return 0;
}

static uint32_t findMaskR8(uint32_t Insn) {
  if ((0xff000000 & Insn) == 0xde000000)
    return 0x00e020e8;
  if ((0xff000000 & Insn) == 0x3c000000)
    return 0x0000207f;
  return 0x00001fe0;
}

static uint32_t findMaskR11(uint32_t Insn) {
  if ((0xff000000 & Insn) == 0xa1000000)
    return 0x060020ff;
  return 0x06003fe0;
}

static uint32_t findMaskR16(uint32_t Insn) {
  if ((0xff000000 & Insn) == 0x48000000)
    return 0x061f20ff;
  if ((0xff000000 & Insn) == 0x49000000)
    return 0x061f3fe0;
  if ((0xff000000 & Insn) == 0x78000000)
    return 0x00df3fe0;
  if ((0xff000000 & Insn) == 0xb0000000)
    return 0x0fe03fe0;

  error("unrecognized instruction for R_HEX_16_X relocation: 0x" +
        utohexstr(Insn));
  return 0;
}

static void or32le(uint8_t *P, int32_t V) { write32le(P, read32le(P) | V); }

void Hexagon::relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const {
  switch (Type) {
  case R_HEX_NONE:
    break;
  case R_HEX_6_PCREL_X:
  case R_HEX_6_X:
    or32le(Loc, applyMask(findMaskR6(read32le(Loc)), Val));
    break;
  case R_HEX_8_X:
    or32le(Loc, applyMask(findMaskR8(read32le(Loc)), Val));
    break;
  case R_HEX_9_X:
    or32le(Loc, applyMask(0x00003fe0, Val & 0x3f));
    break;
  case R_HEX_10_X:
    or32le(Loc, applyMask(0x00203fe0, Val & 0x3f));
    break;
  case R_HEX_11_X:
  case R_HEX_GOT_11_X:
    or32le(Loc, applyMask(findMaskR11(read32le(Loc)), Val & 0x3f));
    break;
  case R_HEX_12_X:
    or32le(Loc, applyMask(0x000007e0, Val));
    break;
  case R_HEX_16_X: // These relocs only have 6 effective bits.
  case R_HEX_GOT_16_X:
    or32le(Loc, applyMask(findMaskR16(read32le(Loc)), Val & 0x3f));
    break;
  case R_HEX_32:
  case R_HEX_32_PCREL:
    or32le(Loc, Val);
    break;
  case R_HEX_32_6_X:
  case R_HEX_GOT_32_6_X:
    or32le(Loc, applyMask(0x0fff3fff, Val >> 6));
    break;
  case R_HEX_B9_PCREL:
    or32le(Loc, applyMask(0x003000fe, Val >> 2));
    break;
  case R_HEX_B9_PCREL_X:
    or32le(Loc, applyMask(0x003000fe, Val & 0x3f));
    break;
  case R_HEX_B13_PCREL:
    or32le(Loc, applyMask(0x00202ffe, Val >> 2));
    break;
  case R_HEX_B15_PCREL:
    or32le(Loc, applyMask(0x00df20fe, Val >> 2));
    break;
  case R_HEX_B15_PCREL_X:
    or32le(Loc, applyMask(0x00df20fe, Val & 0x3f));
    break;
  case R_HEX_B22_PCREL:
  case R_HEX_PLT_B22_PCREL:
    or32le(Loc, applyMask(0x1ff3ffe, Val >> 2));
    break;
  case R_HEX_B22_PCREL_X:
    or32le(Loc, applyMask(0x1ff3ffe, Val & 0x3f));
    break;
  case R_HEX_B32_PCREL_X:
    or32le(Loc, applyMask(0x0fff3fff, Val >> 6));
    break;
  case R_HEX_HI16:
    or32le(Loc, applyMask(0x00c03fff, Val >> 16));
    break;
  case R_HEX_LO16:
    or32le(Loc, applyMask(0x00c03fff, Val));
    break;
  default:
    error(getErrorLocation(Loc) + "unrecognized reloc " + toString(Type));
    break;
  }
}

void Hexagon::writePltHeader(uint8_t *Buf) const {
  const uint8_t PltData[] = {
      0x00, 0x40, 0x00, 0x00, // { immext (#0)
      0x1c, 0xc0, 0x49, 0x6a, //   r28 = add (pc, ##GOT0@PCREL) } # @GOT0
      0x0e, 0x42, 0x9c, 0xe2, // { r14 -= add (r28, #16)  # offset of GOTn
      0x4f, 0x40, 0x9c, 0x91, //   r15 = memw (r28 + #8)  # object ID at GOT2
      0x3c, 0xc0, 0x9c, 0x91, //   r28 = memw (r28 + #4) }# dynamic link at GOT1
      0x0e, 0x42, 0x0e, 0x8c, // { r14 = asr (r14, #2)    # index of PLTn
      0x00, 0xc0, 0x9c, 0x52, //   jumpr r28 }            # call dynamic linker
      0x0c, 0xdb, 0x00, 0x54, // trap0(#0xdb) # bring plt0 into 16byte alignment
  };
  memcpy(Buf, PltData, sizeof(PltData));

  // Offset from PLT0 to the GOT.
  uint64_t Off = In.GotPlt->getVA() - In.Plt->getVA();
  relocateOne(Buf, R_HEX_B32_PCREL_X, Off);
  relocateOne(Buf + 4, R_HEX_6_PCREL_X, Off);
}

void Hexagon::writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr,
                       uint64_t PltEntryAddr, int32_t Index,
                       unsigned RelOff) const {
  const uint8_t Inst[] = {
      0x00, 0x40, 0x00, 0x00, // { immext (#0)
      0x0e, 0xc0, 0x49, 0x6a, //   r14 = add (pc, ##GOTn@PCREL) }
      0x1c, 0xc0, 0x8e, 0x91, // r28 = memw (r14)
      0x00, 0xc0, 0x9c, 0x52, // jumpr r28
  };
  memcpy(Buf, Inst, sizeof(Inst));

  relocateOne(Buf, R_HEX_B32_PCREL_X, GotPltEntryAddr - PltEntryAddr);
  relocateOne(Buf + 4, R_HEX_6_PCREL_X, GotPltEntryAddr - PltEntryAddr);
}

TargetInfo *elf::getHexagonTargetInfo() {
  static Hexagon Target;
  return &Target;
}
