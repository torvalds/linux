//===- ARM.cpp ------------------------------------------------------------===//
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
#include "Thunks.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class ARM final : public TargetInfo {
public:
  ARM();
  uint32_t calcEFlags() const override;
  RelExpr getRelExpr(RelType Type, const Symbol &S,
                     const uint8_t *Loc) const override;
  RelType getDynRel(RelType Type) const override;
  int64_t getImplicitAddend(const uint8_t *Buf, RelType Type) const override;
  void writeGotPlt(uint8_t *Buf, const Symbol &S) const override;
  void writeIgotPlt(uint8_t *Buf, const Symbol &S) const override;
  void writePltHeader(uint8_t *Buf) const override;
  void writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
  void addPltSymbols(InputSection &IS, uint64_t Off) const override;
  void addPltHeaderSymbols(InputSection &ISD) const override;
  bool needsThunk(RelExpr Expr, RelType Type, const InputFile *File,
                  uint64_t BranchAddr, const Symbol &S) const override;
  uint32_t getThunkSectionSpacing() const override;
  bool inBranchRange(RelType Type, uint64_t Src, uint64_t Dst) const override;
  void relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const override;
};
} // namespace

ARM::ARM() {
  CopyRel = R_ARM_COPY;
  RelativeRel = R_ARM_RELATIVE;
  IRelativeRel = R_ARM_IRELATIVE;
  GotRel = R_ARM_GLOB_DAT;
  NoneRel = R_ARM_NONE;
  PltRel = R_ARM_JUMP_SLOT;
  TlsGotRel = R_ARM_TLS_TPOFF32;
  TlsModuleIndexRel = R_ARM_TLS_DTPMOD32;
  TlsOffsetRel = R_ARM_TLS_DTPOFF32;
  GotBaseSymInGotPlt = false;
  GotEntrySize = 4;
  GotPltEntrySize = 4;
  PltEntrySize = 16;
  PltHeaderSize = 32;
  TrapInstr = {0xd4, 0xd4, 0xd4, 0xd4};
  NeedsThunks = true;
}

uint32_t ARM::calcEFlags() const {
  // The ABIFloatType is used by loaders to detect the floating point calling
  // convention.
  uint32_t ABIFloatType = 0;
  if (Config->ARMVFPArgs == ARMVFPArgKind::Base ||
      Config->ARMVFPArgs == ARMVFPArgKind::Default)
    ABIFloatType = EF_ARM_ABI_FLOAT_SOFT;
  else if (Config->ARMVFPArgs == ARMVFPArgKind::VFP)
    ABIFloatType = EF_ARM_ABI_FLOAT_HARD;

  // We don't currently use any features incompatible with EF_ARM_EABI_VER5,
  // but we don't have any firm guarantees of conformance. Linux AArch64
  // kernels (as of 2016) require an EABI version to be set.
  return EF_ARM_EABI_VER5 | ABIFloatType;
}

RelExpr ARM::getRelExpr(RelType Type, const Symbol &S,
                        const uint8_t *Loc) const {
  switch (Type) {
  case R_ARM_THM_JUMP11:
    return R_PC;
  case R_ARM_CALL:
  case R_ARM_JUMP24:
  case R_ARM_PC24:
  case R_ARM_PLT32:
  case R_ARM_PREL31:
  case R_ARM_THM_JUMP19:
  case R_ARM_THM_JUMP24:
  case R_ARM_THM_CALL:
    return R_PLT_PC;
  case R_ARM_GOTOFF32:
    // (S + A) - GOT_ORG
    return R_GOTREL;
  case R_ARM_GOT_BREL:
    // GOT(S) + A - GOT_ORG
    return R_GOT_OFF;
  case R_ARM_GOT_PREL:
  case R_ARM_TLS_IE32:
    // GOT(S) + A - P
    return R_GOT_PC;
  case R_ARM_SBREL32:
    return R_ARM_SBREL;
  case R_ARM_TARGET1:
    return Config->Target1Rel ? R_PC : R_ABS;
  case R_ARM_TARGET2:
    if (Config->Target2 == Target2Policy::Rel)
      return R_PC;
    if (Config->Target2 == Target2Policy::Abs)
      return R_ABS;
    return R_GOT_PC;
  case R_ARM_TLS_GD32:
    return R_TLSGD_PC;
  case R_ARM_TLS_LDM32:
    return R_TLSLD_PC;
  case R_ARM_BASE_PREL:
    // B(S) + A - P
    // FIXME: currently B(S) assumed to be .got, this may not hold for all
    // platforms.
    return R_GOTONLY_PC;
  case R_ARM_MOVW_PREL_NC:
  case R_ARM_MOVT_PREL:
  case R_ARM_REL32:
  case R_ARM_THM_MOVW_PREL_NC:
  case R_ARM_THM_MOVT_PREL:
    return R_PC;
  case R_ARM_NONE:
    return R_NONE;
  case R_ARM_TLS_LE32:
    return R_TLS;
  case R_ARM_V4BX:
    // V4BX is just a marker to indicate there's a "bx rN" instruction at the
    // given address. It can be used to implement a special linker mode which
    // rewrites ARMv4T inputs to ARMv4. Since we support only ARMv4 input and
    // not ARMv4 output, we can just ignore it.
    return R_HINT;
  default:
    return R_ABS;
  }
}

RelType ARM::getDynRel(RelType Type) const {
  if ((Type == R_ARM_ABS32) || (Type == R_ARM_TARGET1 && !Config->Target1Rel))
    return R_ARM_ABS32;
  return R_ARM_NONE;
}

void ARM::writeGotPlt(uint8_t *Buf, const Symbol &) const {
  write32le(Buf, In.Plt->getVA());
}

void ARM::writeIgotPlt(uint8_t *Buf, const Symbol &S) const {
  // An ARM entry is the address of the ifunc resolver function.
  write32le(Buf, S.getVA());
}

// Long form PLT Header that does not have any restrictions on the displacement
// of the .plt from the .plt.got.
static void writePltHeaderLong(uint8_t *Buf) {
  const uint8_t PltData[] = {
      0x04, 0xe0, 0x2d, 0xe5, //     str lr, [sp,#-4]!
      0x04, 0xe0, 0x9f, 0xe5, //     ldr lr, L2
      0x0e, 0xe0, 0x8f, 0xe0, // L1: add lr, pc, lr
      0x08, 0xf0, 0xbe, 0xe5, //     ldr pc, [lr, #8]
      0x00, 0x00, 0x00, 0x00, // L2: .word   &(.got.plt) - L1 - 8
      0xd4, 0xd4, 0xd4, 0xd4, //     Pad to 32-byte boundary
      0xd4, 0xd4, 0xd4, 0xd4, //     Pad to 32-byte boundary
      0xd4, 0xd4, 0xd4, 0xd4};
  memcpy(Buf, PltData, sizeof(PltData));
  uint64_t GotPlt = In.GotPlt->getVA();
  uint64_t L1 = In.Plt->getVA() + 8;
  write32le(Buf + 16, GotPlt - L1 - 8);
}

// The default PLT header requires the .plt.got to be within 128 Mb of the
// .plt in the positive direction.
void ARM::writePltHeader(uint8_t *Buf) const {
  // Use a similar sequence to that in writePlt(), the difference is the calling
  // conventions mean we use lr instead of ip. The PLT entry is responsible for
  // saving lr on the stack, the dynamic loader is responsible for reloading
  // it.
  const uint32_t PltData[] = {
      0xe52de004, // L1: str lr, [sp,#-4]!
      0xe28fe600, //     add lr, pc,  #0x0NN00000 &(.got.plt - L1 - 4)
      0xe28eea00, //     add lr, lr,  #0x000NN000 &(.got.plt - L1 - 4)
      0xe5bef000, //     ldr pc, [lr, #0x00000NNN] &(.got.plt -L1 - 4)
  };

  uint64_t Offset = In.GotPlt->getVA() - In.Plt->getVA() - 4;
  if (!llvm::isUInt<27>(Offset)) {
    // We cannot encode the Offset, use the long form.
    writePltHeaderLong(Buf);
    return;
  }
  write32le(Buf + 0, PltData[0]);
  write32le(Buf + 4, PltData[1] | ((Offset >> 20) & 0xff));
  write32le(Buf + 8, PltData[2] | ((Offset >> 12) & 0xff));
  write32le(Buf + 12, PltData[3] | (Offset & 0xfff));
  memcpy(Buf + 16, TrapInstr.data(), 4); // Pad to 32-byte boundary
  memcpy(Buf + 20, TrapInstr.data(), 4);
  memcpy(Buf + 24, TrapInstr.data(), 4);
  memcpy(Buf + 28, TrapInstr.data(), 4);
}

void ARM::addPltHeaderSymbols(InputSection &IS) const {
  addSyntheticLocal("$a", STT_NOTYPE, 0, 0, IS);
  addSyntheticLocal("$d", STT_NOTYPE, 16, 0, IS);
}

// Long form PLT entries that do not have any restrictions on the displacement
// of the .plt from the .plt.got.
static void writePltLong(uint8_t *Buf, uint64_t GotPltEntryAddr,
                         uint64_t PltEntryAddr, int32_t Index,
                         unsigned RelOff) {
  const uint8_t PltData[] = {
      0x04, 0xc0, 0x9f, 0xe5, //     ldr ip, L2
      0x0f, 0xc0, 0x8c, 0xe0, // L1: add ip, ip, pc
      0x00, 0xf0, 0x9c, 0xe5, //     ldr pc, [ip]
      0x00, 0x00, 0x00, 0x00, // L2: .word   Offset(&(.plt.got) - L1 - 8
  };
  memcpy(Buf, PltData, sizeof(PltData));
  uint64_t L1 = PltEntryAddr + 4;
  write32le(Buf + 12, GotPltEntryAddr - L1 - 8);
}

// The default PLT entries require the .plt.got to be within 128 Mb of the
// .plt in the positive direction.
void ARM::writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr,
                   uint64_t PltEntryAddr, int32_t Index,
                   unsigned RelOff) const {
  // The PLT entry is similar to the example given in Appendix A of ELF for
  // the Arm Architecture. Instead of using the Group Relocations to find the
  // optimal rotation for the 8-bit immediate used in the add instructions we
  // hard code the most compact rotations for simplicity. This saves a load
  // instruction over the long plt sequences.
  const uint32_t PltData[] = {
      0xe28fc600, // L1: add ip, pc,  #0x0NN00000  Offset(&(.plt.got) - L1 - 8
      0xe28cca00, //     add ip, ip,  #0x000NN000  Offset(&(.plt.got) - L1 - 8
      0xe5bcf000, //     ldr pc, [ip, #0x00000NNN] Offset(&(.plt.got) - L1 - 8
  };

  uint64_t Offset = GotPltEntryAddr - PltEntryAddr - 8;
  if (!llvm::isUInt<27>(Offset)) {
    // We cannot encode the Offset, use the long form.
    writePltLong(Buf, GotPltEntryAddr, PltEntryAddr, Index, RelOff);
    return;
  }
  write32le(Buf + 0, PltData[0] | ((Offset >> 20) & 0xff));
  write32le(Buf + 4, PltData[1] | ((Offset >> 12) & 0xff));
  write32le(Buf + 8, PltData[2] | (Offset & 0xfff));
  memcpy(Buf + 12, TrapInstr.data(), 4); // Pad to 16-byte boundary
}

void ARM::addPltSymbols(InputSection &IS, uint64_t Off) const {
  addSyntheticLocal("$a", STT_NOTYPE, Off, 0, IS);
  addSyntheticLocal("$d", STT_NOTYPE, Off + 12, 0, IS);
}

bool ARM::needsThunk(RelExpr Expr, RelType Type, const InputFile *File,
                     uint64_t BranchAddr, const Symbol &S) const {
  // If S is an undefined weak symbol and does not have a PLT entry then it
  // will be resolved as a branch to the next instruction.
  if (S.isUndefWeak() && !S.isInPlt())
    return false;
  // A state change from ARM to Thumb and vice versa must go through an
  // interworking thunk if the relocation type is not R_ARM_CALL or
  // R_ARM_THM_CALL.
  switch (Type) {
  case R_ARM_PC24:
  case R_ARM_PLT32:
  case R_ARM_JUMP24:
    // Source is ARM, all PLT entries are ARM so no interworking required.
    // Otherwise we need to interwork if Symbol has bit 0 set (Thumb).
    if (Expr == R_PC && ((S.getVA() & 1) == 1))
      return true;
    LLVM_FALLTHROUGH;
  case R_ARM_CALL: {
    uint64_t Dst = (Expr == R_PLT_PC) ? S.getPltVA() : S.getVA();
    return !inBranchRange(Type, BranchAddr, Dst);
  }
  case R_ARM_THM_JUMP19:
  case R_ARM_THM_JUMP24:
    // Source is Thumb, all PLT entries are ARM so interworking is required.
    // Otherwise we need to interwork if Symbol has bit 0 clear (ARM).
    if (Expr == R_PLT_PC || ((S.getVA() & 1) == 0))
      return true;
    LLVM_FALLTHROUGH;
  case R_ARM_THM_CALL: {
    uint64_t Dst = (Expr == R_PLT_PC) ? S.getPltVA() : S.getVA();
    return !inBranchRange(Type, BranchAddr, Dst);
  }
  }
  return false;
}

uint32_t ARM::getThunkSectionSpacing() const {
  // The placing of pre-created ThunkSections is controlled by the value
  // ThunkSectionSpacing returned by getThunkSectionSpacing(). The aim is to
  // place the ThunkSection such that all branches from the InputSections
  // prior to the ThunkSection can reach a Thunk placed at the end of the
  // ThunkSection. Graphically:
  // | up to ThunkSectionSpacing .text input sections |
  // | ThunkSection                                   |
  // | up to ThunkSectionSpacing .text input sections |
  // | ThunkSection                                   |

  // Pre-created ThunkSections are spaced roughly 16MiB apart on ARMv7. This
  // is to match the most common expected case of a Thumb 2 encoded BL, BLX or
  // B.W:
  // ARM B, BL, BLX range +/- 32MiB
  // Thumb B.W, BL, BLX range +/- 16MiB
  // Thumb B<cc>.W range +/- 1MiB
  // If a branch cannot reach a pre-created ThunkSection a new one will be
  // created so we can handle the rare cases of a Thumb 2 conditional branch.
  // We intentionally use a lower size for ThunkSectionSpacing than the maximum
  // branch range so the end of the ThunkSection is more likely to be within
  // range of the branch instruction that is furthest away. The value we shorten
  // ThunkSectionSpacing by is set conservatively to allow us to create 16,384
  // 12 byte Thunks at any offset in a ThunkSection without risk of a branch to
  // one of the Thunks going out of range.

  // On Arm the ThunkSectionSpacing depends on the range of the Thumb Branch
  // range. On earlier Architectures such as ARMv4, ARMv5 and ARMv6 (except
  // ARMv6T2) the range is +/- 4MiB.

  return (Config->ARMJ1J2BranchEncoding) ? 0x1000000 - 0x30000
                                         : 0x400000 - 0x7500;
}

bool ARM::inBranchRange(RelType Type, uint64_t Src, uint64_t Dst) const {
  uint64_t Range;
  uint64_t InstrSize;

  switch (Type) {
  case R_ARM_PC24:
  case R_ARM_PLT32:
  case R_ARM_JUMP24:
  case R_ARM_CALL:
    Range = 0x2000000;
    InstrSize = 4;
    break;
  case R_ARM_THM_JUMP19:
    Range = 0x100000;
    InstrSize = 2;
    break;
  case R_ARM_THM_JUMP24:
  case R_ARM_THM_CALL:
    Range = Config->ARMJ1J2BranchEncoding ? 0x1000000 : 0x400000;
    InstrSize = 2;
    break;
  default:
    return true;
  }
  // PC at Src is 2 instructions ahead, immediate of branch is signed
  if (Src > Dst)
    Range -= 2 * InstrSize;
  else
    Range += InstrSize;

  if ((Dst & 0x1) == 0)
    // Destination is ARM, if ARM caller then Src is already 4-byte aligned.
    // If Thumb Caller (BLX) the Src address has bottom 2 bits cleared to ensure
    // destination will be 4 byte aligned.
    Src &= ~0x3;
  else
    // Bit 0 == 1 denotes Thumb state, it is not part of the range
    Dst &= ~0x1;

  uint64_t Distance = (Src > Dst) ? Src - Dst : Dst - Src;
  return Distance <= Range;
}

void ARM::relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const {
  switch (Type) {
  case R_ARM_ABS32:
  case R_ARM_BASE_PREL:
  case R_ARM_GLOB_DAT:
  case R_ARM_GOTOFF32:
  case R_ARM_GOT_BREL:
  case R_ARM_GOT_PREL:
  case R_ARM_REL32:
  case R_ARM_RELATIVE:
  case R_ARM_SBREL32:
  case R_ARM_TARGET1:
  case R_ARM_TARGET2:
  case R_ARM_TLS_GD32:
  case R_ARM_TLS_IE32:
  case R_ARM_TLS_LDM32:
  case R_ARM_TLS_LDO32:
  case R_ARM_TLS_LE32:
  case R_ARM_TLS_TPOFF32:
  case R_ARM_TLS_DTPOFF32:
    write32le(Loc, Val);
    break;
  case R_ARM_TLS_DTPMOD32:
    write32le(Loc, 1);
    break;
  case R_ARM_PREL31:
    checkInt(Loc, Val, 31, Type);
    write32le(Loc, (read32le(Loc) & 0x80000000) | (Val & ~0x80000000));
    break;
  case R_ARM_CALL:
    // R_ARM_CALL is used for BL and BLX instructions, depending on the
    // value of bit 0 of Val, we must select a BL or BLX instruction
    if (Val & 1) {
      // If bit 0 of Val is 1 the target is Thumb, we must select a BLX.
      // The BLX encoding is 0xfa:H:imm24 where Val = imm24:H:'1'
      checkInt(Loc, Val, 26, Type);
      write32le(Loc, 0xfa000000 |                    // opcode
                         ((Val & 2) << 23) |         // H
                         ((Val >> 2) & 0x00ffffff)); // imm24
      break;
    }
    if ((read32le(Loc) & 0xfe000000) == 0xfa000000)
      // BLX (always unconditional) instruction to an ARM Target, select an
      // unconditional BL.
      write32le(Loc, 0xeb000000 | (read32le(Loc) & 0x00ffffff));
    // fall through as BL encoding is shared with B
    LLVM_FALLTHROUGH;
  case R_ARM_JUMP24:
  case R_ARM_PC24:
  case R_ARM_PLT32:
    checkInt(Loc, Val, 26, Type);
    write32le(Loc, (read32le(Loc) & ~0x00ffffff) | ((Val >> 2) & 0x00ffffff));
    break;
  case R_ARM_THM_JUMP11:
    checkInt(Loc, Val, 12, Type);
    write16le(Loc, (read32le(Loc) & 0xf800) | ((Val >> 1) & 0x07ff));
    break;
  case R_ARM_THM_JUMP19:
    // Encoding T3: Val = S:J2:J1:imm6:imm11:0
    checkInt(Loc, Val, 21, Type);
    write16le(Loc,
              (read16le(Loc) & 0xfbc0) |   // opcode cond
                  ((Val >> 10) & 0x0400) | // S
                  ((Val >> 12) & 0x003f)); // imm6
    write16le(Loc + 2,
              0x8000 |                    // opcode
                  ((Val >> 8) & 0x0800) | // J2
                  ((Val >> 5) & 0x2000) | // J1
                  ((Val >> 1) & 0x07ff)); // imm11
    break;
  case R_ARM_THM_CALL:
    // R_ARM_THM_CALL is used for BL and BLX instructions, depending on the
    // value of bit 0 of Val, we must select a BL or BLX instruction
    if ((Val & 1) == 0) {
      // Ensure BLX destination is 4-byte aligned. As BLX instruction may
      // only be two byte aligned. This must be done before overflow check
      Val = alignTo(Val, 4);
    }
    // Bit 12 is 0 for BLX, 1 for BL
    write16le(Loc + 2, (read16le(Loc + 2) & ~0x1000) | (Val & 1) << 12);
    if (!Config->ARMJ1J2BranchEncoding) {
      // Older Arm architectures do not support R_ARM_THM_JUMP24 and have
      // different encoding rules and range due to J1 and J2 always being 1.
      checkInt(Loc, Val, 23, Type);
      write16le(Loc,
                0xf000 |                     // opcode
                    ((Val >> 12) & 0x07ff)); // imm11
      write16le(Loc + 2,
                (read16le(Loc + 2) & 0xd000) | // opcode
                    0x2800 |                   // J1 == J2 == 1
                    ((Val >> 1) & 0x07ff));    // imm11
      break;
    }
    // Fall through as rest of encoding is the same as B.W
    LLVM_FALLTHROUGH;
  case R_ARM_THM_JUMP24:
    // Encoding B  T4, BL T1, BLX T2: Val = S:I1:I2:imm10:imm11:0
    checkInt(Loc, Val, 25, Type);
    write16le(Loc,
              0xf000 |                     // opcode
                  ((Val >> 14) & 0x0400) | // S
                  ((Val >> 12) & 0x03ff)); // imm10
    write16le(Loc + 2,
              (read16le(Loc + 2) & 0xd000) |                  // opcode
                  (((~(Val >> 10)) ^ (Val >> 11)) & 0x2000) | // J1
                  (((~(Val >> 11)) ^ (Val >> 13)) & 0x0800) | // J2
                  ((Val >> 1) & 0x07ff));                     // imm11
    break;
  case R_ARM_MOVW_ABS_NC:
  case R_ARM_MOVW_PREL_NC:
    write32le(Loc, (read32le(Loc) & ~0x000f0fff) | ((Val & 0xf000) << 4) |
                       (Val & 0x0fff));
    break;
  case R_ARM_MOVT_ABS:
  case R_ARM_MOVT_PREL:
    write32le(Loc, (read32le(Loc) & ~0x000f0fff) |
                       (((Val >> 16) & 0xf000) << 4) | ((Val >> 16) & 0xfff));
    break;
  case R_ARM_THM_MOVT_ABS:
  case R_ARM_THM_MOVT_PREL:
    // Encoding T1: A = imm4:i:imm3:imm8
    write16le(Loc,
              0xf2c0 |                     // opcode
                  ((Val >> 17) & 0x0400) | // i
                  ((Val >> 28) & 0x000f)); // imm4
    write16le(Loc + 2,
              (read16le(Loc + 2) & 0x8f00) | // opcode
                  ((Val >> 12) & 0x7000) |   // imm3
                  ((Val >> 16) & 0x00ff));   // imm8
    break;
  case R_ARM_THM_MOVW_ABS_NC:
  case R_ARM_THM_MOVW_PREL_NC:
    // Encoding T3: A = imm4:i:imm3:imm8
    write16le(Loc,
              0xf240 |                     // opcode
                  ((Val >> 1) & 0x0400) |  // i
                  ((Val >> 12) & 0x000f)); // imm4
    write16le(Loc + 2,
              (read16le(Loc + 2) & 0x8f00) | // opcode
                  ((Val << 4) & 0x7000) |    // imm3
                  (Val & 0x00ff));           // imm8
    break;
  default:
    error(getErrorLocation(Loc) + "unrecognized reloc " + Twine(Type));
  }
}

int64_t ARM::getImplicitAddend(const uint8_t *Buf, RelType Type) const {
  switch (Type) {
  default:
    return 0;
  case R_ARM_ABS32:
  case R_ARM_BASE_PREL:
  case R_ARM_GOTOFF32:
  case R_ARM_GOT_BREL:
  case R_ARM_GOT_PREL:
  case R_ARM_REL32:
  case R_ARM_TARGET1:
  case R_ARM_TARGET2:
  case R_ARM_TLS_GD32:
  case R_ARM_TLS_LDM32:
  case R_ARM_TLS_LDO32:
  case R_ARM_TLS_IE32:
  case R_ARM_TLS_LE32:
    return SignExtend64<32>(read32le(Buf));
  case R_ARM_PREL31:
    return SignExtend64<31>(read32le(Buf));
  case R_ARM_CALL:
  case R_ARM_JUMP24:
  case R_ARM_PC24:
  case R_ARM_PLT32:
    return SignExtend64<26>(read32le(Buf) << 2);
  case R_ARM_THM_JUMP11:
    return SignExtend64<12>(read16le(Buf) << 1);
  case R_ARM_THM_JUMP19: {
    // Encoding T3: A = S:J2:J1:imm10:imm6:0
    uint16_t Hi = read16le(Buf);
    uint16_t Lo = read16le(Buf + 2);
    return SignExtend64<20>(((Hi & 0x0400) << 10) | // S
                            ((Lo & 0x0800) << 8) |  // J2
                            ((Lo & 0x2000) << 5) |  // J1
                            ((Hi & 0x003f) << 12) | // imm6
                            ((Lo & 0x07ff) << 1));  // imm11:0
  }
  case R_ARM_THM_CALL:
    if (!Config->ARMJ1J2BranchEncoding) {
      // Older Arm architectures do not support R_ARM_THM_JUMP24 and have
      // different encoding rules and range due to J1 and J2 always being 1.
      uint16_t Hi = read16le(Buf);
      uint16_t Lo = read16le(Buf + 2);
      return SignExtend64<22>(((Hi & 0x7ff) << 12) | // imm11
                              ((Lo & 0x7ff) << 1));  // imm11:0
      break;
    }
    LLVM_FALLTHROUGH;
  case R_ARM_THM_JUMP24: {
    // Encoding B T4, BL T1, BLX T2: A = S:I1:I2:imm10:imm11:0
    // I1 = NOT(J1 EOR S), I2 = NOT(J2 EOR S)
    uint16_t Hi = read16le(Buf);
    uint16_t Lo = read16le(Buf + 2);
    return SignExtend64<24>(((Hi & 0x0400) << 14) |                    // S
                            (~((Lo ^ (Hi << 3)) << 10) & 0x00800000) | // I1
                            (~((Lo ^ (Hi << 1)) << 11) & 0x00400000) | // I2
                            ((Hi & 0x003ff) << 12) |                   // imm0
                            ((Lo & 0x007ff) << 1)); // imm11:0
  }
  // ELF for the ARM Architecture 4.6.1.1 the implicit addend for MOVW and
  // MOVT is in the range -32768 <= A < 32768
  case R_ARM_MOVW_ABS_NC:
  case R_ARM_MOVT_ABS:
  case R_ARM_MOVW_PREL_NC:
  case R_ARM_MOVT_PREL: {
    uint64_t Val = read32le(Buf) & 0x000f0fff;
    return SignExtend64<16>(((Val & 0x000f0000) >> 4) | (Val & 0x00fff));
  }
  case R_ARM_THM_MOVW_ABS_NC:
  case R_ARM_THM_MOVT_ABS:
  case R_ARM_THM_MOVW_PREL_NC:
  case R_ARM_THM_MOVT_PREL: {
    // Encoding T3: A = imm4:i:imm3:imm8
    uint16_t Hi = read16le(Buf);
    uint16_t Lo = read16le(Buf + 2);
    return SignExtend64<16>(((Hi & 0x000f) << 12) | // imm4
                            ((Hi & 0x0400) << 1) |  // i
                            ((Lo & 0x7000) >> 4) |  // imm3
                            (Lo & 0x00ff));         // imm8
  }
  }
}

TargetInfo *elf::getARMTargetInfo() {
  static ARM Target;
  return &Target;
}
