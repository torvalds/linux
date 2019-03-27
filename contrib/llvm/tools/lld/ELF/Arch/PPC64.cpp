//===- PPC64.cpp ----------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

static uint64_t PPC64TocOffset = 0x8000;
static uint64_t DynamicThreadPointerOffset = 0x8000;

// The instruction encoding of bits 21-30 from the ISA for the Xform and Dform
// instructions that can be used as part of the initial exec TLS sequence.
enum XFormOpcd {
  LBZX = 87,
  LHZX = 279,
  LWZX = 23,
  LDX = 21,
  STBX = 215,
  STHX = 407,
  STWX = 151,
  STDX = 149,
  ADD = 266,
};

enum DFormOpcd {
  LBZ = 34,
  LBZU = 35,
  LHZ = 40,
  LHZU = 41,
  LHAU = 43,
  LWZ = 32,
  LWZU = 33,
  LFSU = 49,
  LD = 58,
  LFDU = 51,
  STB = 38,
  STBU = 39,
  STH = 44,
  STHU = 45,
  STW = 36,
  STWU = 37,
  STFSU = 53,
  STFDU = 55,
  STD = 62,
  ADDI = 14
};

uint64_t elf::getPPC64TocBase() {
  // The TOC consists of sections .got, .toc, .tocbss, .plt in that order. The
  // TOC starts where the first of these sections starts. We always create a
  // .got when we see a relocation that uses it, so for us the start is always
  // the .got.
  uint64_t TocVA = In.Got->getVA();

  // Per the ppc64-elf-linux ABI, The TOC base is TOC value plus 0x8000
  // thus permitting a full 64 Kbytes segment. Note that the glibc startup
  // code (crt1.o) assumes that you can get from the TOC base to the
  // start of the .toc section with only a single (signed) 16-bit relocation.
  return TocVA + PPC64TocOffset;
}

unsigned elf::getPPC64GlobalEntryToLocalEntryOffset(uint8_t StOther) {
  // The offset is encoded into the 3 most significant bits of the st_other
  // field, with some special values described in section 3.4.1 of the ABI:
  // 0   --> Zero offset between the GEP and LEP, and the function does NOT use
  //         the TOC pointer (r2). r2 will hold the same value on returning from
  //         the function as it did on entering the function.
  // 1   --> Zero offset between the GEP and LEP, and r2 should be treated as a
  //         caller-saved register for all callers.
  // 2-6 --> The  binary logarithm of the offset eg:
  //         2 --> 2^2 = 4 bytes -->  1 instruction.
  //         6 --> 2^6 = 64 bytes --> 16 instructions.
  // 7   --> Reserved.
  uint8_t GepToLep = (StOther >> 5) & 7;
  if (GepToLep < 2)
    return 0;

  // The value encoded in the st_other bits is the
  // log-base-2(offset).
  if (GepToLep < 7)
    return 1 << GepToLep;

  error("reserved value of 7 in the 3 most-significant-bits of st_other");
  return 0;
}

namespace {
class PPC64 final : public TargetInfo {
public:
  PPC64();
  uint32_t calcEFlags() const override;
  RelExpr getRelExpr(RelType Type, const Symbol &S,
                     const uint8_t *Loc) const override;
  void writePltHeader(uint8_t *Buf) const override;
  void writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
  void relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void writeGotHeader(uint8_t *Buf) const override;
  bool needsThunk(RelExpr Expr, RelType Type, const InputFile *File,
                  uint64_t BranchAddr, const Symbol &S) const override;
  bool inBranchRange(RelType Type, uint64_t Src, uint64_t Dst) const override;
  RelExpr adjustRelaxExpr(RelType Type, const uint8_t *Data,
                          RelExpr Expr) const override;
  void relaxTlsGdToIe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsGdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsLdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsIeToLe(uint8_t *Loc, RelType Type, uint64_t Val) const override;

  bool adjustPrologueForCrossSplitStack(uint8_t *Loc, uint8_t *End,
                                        uint8_t StOther) const override;
};
} // namespace

// Relocation masks following the #lo(value), #hi(value), #ha(value),
// #higher(value), #highera(value), #highest(value), and #highesta(value)
// macros defined in section 4.5.1. Relocation Types of the PPC-elf64abi
// document.
static uint16_t lo(uint64_t V) { return V; }
static uint16_t hi(uint64_t V) { return V >> 16; }
static uint16_t ha(uint64_t V) { return (V + 0x8000) >> 16; }
static uint16_t higher(uint64_t V) { return V >> 32; }
static uint16_t highera(uint64_t V) { return (V + 0x8000) >> 32; }
static uint16_t highest(uint64_t V) { return V >> 48; }
static uint16_t highesta(uint64_t V) { return (V + 0x8000) >> 48; }

// Extracts the 'PO' field of an instruction encoding.
static uint8_t getPrimaryOpCode(uint32_t Encoding) { return (Encoding >> 26); }

static bool isDQFormInstruction(uint32_t Encoding) {
  switch (getPrimaryOpCode(Encoding)) {
  default:
    return false;
  case 56:
    // The only instruction with a primary opcode of 56 is `lq`.
    return true;
  case 61:
    // There are both DS and DQ instruction forms with this primary opcode.
    // Namely `lxv` and `stxv` are the DQ-forms that use it.
    // The DS 'XO' bits being set to 01 is restricted to DQ form.
    return (Encoding & 3) == 0x1;
  }
}

static bool isInstructionUpdateForm(uint32_t Encoding) {
  switch (getPrimaryOpCode(Encoding)) {
  default:
    return false;
  case LBZU:
  case LHAU:
  case LHZU:
  case LWZU:
  case LFSU:
  case LFDU:
  case STBU:
  case STHU:
  case STWU:
  case STFSU:
  case STFDU:
    return true;
    // LWA has the same opcode as LD, and the DS bits is what differentiates
    // between LD/LDU/LWA
  case LD:
  case STD:
    return (Encoding & 3) == 1;
  }
}

// There are a number of places when we either want to read or write an
// instruction when handling a half16 relocation type. On big-endian the buffer
// pointer is pointing into the middle of the word we want to extract, and on
// little-endian it is pointing to the start of the word. These 2 helpers are to
// simplify reading and writing in that context.
static void writeInstrFromHalf16(uint8_t *Loc, uint32_t Instr) {
  write32(Loc - (Config->EKind == ELF64BEKind ? 2 : 0), Instr);
}

static uint32_t readInstrFromHalf16(const uint8_t *Loc) {
  return read32(Loc - (Config->EKind == ELF64BEKind ? 2 : 0));
}

PPC64::PPC64() {
  GotRel = R_PPC64_GLOB_DAT;
  NoneRel = R_PPC64_NONE;
  PltRel = R_PPC64_JMP_SLOT;
  RelativeRel = R_PPC64_RELATIVE;
  IRelativeRel = R_PPC64_IRELATIVE;
  GotEntrySize = 8;
  PltEntrySize = 4;
  GotPltEntrySize = 8;
  GotBaseSymInGotPlt = false;
  GotBaseSymOff = 0x8000;
  GotHeaderEntriesNum = 1;
  GotPltHeaderEntriesNum = 2;
  PltHeaderSize = 60;
  NeedsThunks = true;

  TlsModuleIndexRel = R_PPC64_DTPMOD64;
  TlsOffsetRel = R_PPC64_DTPREL64;

  TlsGotRel = R_PPC64_TPREL64;

  NeedsMoreStackNonSplit = false;

  // We need 64K pages (at least under glibc/Linux, the loader won't
  // set different permissions on a finer granularity than that).
  DefaultMaxPageSize = 65536;

  // The PPC64 ELF ABI v1 spec, says:
  //
  //   It is normally desirable to put segments with different characteristics
  //   in separate 256 Mbyte portions of the address space, to give the
  //   operating system full paging flexibility in the 64-bit address space.
  //
  // And because the lowest non-zero 256M boundary is 0x10000000, PPC64 linkers
  // use 0x10000000 as the starting address.
  DefaultImageBase = 0x10000000;

  write32(TrapInstr.data(), 0x7fe00008);
}

static uint32_t getEFlags(InputFile *File) {
  if (Config->EKind == ELF64BEKind)
    return cast<ObjFile<ELF64BE>>(File)->getObj().getHeader()->e_flags;
  return cast<ObjFile<ELF64LE>>(File)->getObj().getHeader()->e_flags;
}

// This file implements v2 ABI. This function makes sure that all
// object files have v2 or an unspecified version as an ABI version.
uint32_t PPC64::calcEFlags() const {
  for (InputFile *F : ObjectFiles) {
    uint32_t Flag = getEFlags(F);
    if (Flag == 1)
      error(toString(F) + ": ABI version 1 is not supported");
    else if (Flag > 2)
      error(toString(F) + ": unrecognized e_flags: " + Twine(Flag));
  }
  return 2;
}

void PPC64::relaxTlsGdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const {
  // Reference: 3.7.4.2 of the 64-bit ELF V2 abi supplement.
  // The general dynamic code sequence for a global `x` will look like:
  // Instruction                    Relocation                Symbol
  // addis r3, r2, x@got@tlsgd@ha   R_PPC64_GOT_TLSGD16_HA      x
  // addi  r3, r3, x@got@tlsgd@l    R_PPC64_GOT_TLSGD16_LO      x
  // bl __tls_get_addr(x@tlsgd)     R_PPC64_TLSGD               x
  //                                R_PPC64_REL24               __tls_get_addr
  // nop                            None                       None

  // Relaxing to local exec entails converting:
  // addis r3, r2, x@got@tlsgd@ha    into      nop
  // addi  r3, r3, x@got@tlsgd@l     into      addis r3, r13, x@tprel@ha
  // bl __tls_get_addr(x@tlsgd)      into      nop
  // nop                             into      addi r3, r3, x@tprel@l

  switch (Type) {
  case R_PPC64_GOT_TLSGD16_HA:
    writeInstrFromHalf16(Loc, 0x60000000); // nop
    break;
  case R_PPC64_GOT_TLSGD16:
  case R_PPC64_GOT_TLSGD16_LO:
    writeInstrFromHalf16(Loc, 0x3c6d0000); // addis r3, r13
    relocateOne(Loc, R_PPC64_TPREL16_HA, Val);
    break;
  case R_PPC64_TLSGD:
    write32(Loc, 0x60000000);     // nop
    write32(Loc + 4, 0x38630000); // addi r3, r3
    // Since we are relocating a half16 type relocation and Loc + 4 points to
    // the start of an instruction we need to advance the buffer by an extra
    // 2 bytes on BE.
    relocateOne(Loc + 4 + (Config->EKind == ELF64BEKind ? 2 : 0),
                R_PPC64_TPREL16_LO, Val);
    break;
  default:
    llvm_unreachable("unsupported relocation for TLS GD to LE relaxation");
  }
}

void PPC64::relaxTlsLdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const {
  // Reference: 3.7.4.3 of the 64-bit ELF V2 abi supplement.
  // The local dynamic code sequence for a global `x` will look like:
  // Instruction                    Relocation                Symbol
  // addis r3, r2, x@got@tlsld@ha   R_PPC64_GOT_TLSLD16_HA      x
  // addi  r3, r3, x@got@tlsld@l    R_PPC64_GOT_TLSLD16_LO      x
  // bl __tls_get_addr(x@tlsgd)     R_PPC64_TLSLD               x
  //                                R_PPC64_REL24               __tls_get_addr
  // nop                            None                       None

  // Relaxing to local exec entails converting:
  // addis r3, r2, x@got@tlsld@ha   into      nop
  // addi  r3, r3, x@got@tlsld@l    into      addis r3, r13, 0
  // bl __tls_get_addr(x@tlsgd)     into      nop
  // nop                            into      addi r3, r3, 4096

  switch (Type) {
  case R_PPC64_GOT_TLSLD16_HA:
    writeInstrFromHalf16(Loc, 0x60000000); // nop
    break;
  case R_PPC64_GOT_TLSLD16_LO:
    writeInstrFromHalf16(Loc, 0x3c6d0000); // addis r3, r13, 0
    break;
  case R_PPC64_TLSLD:
    write32(Loc, 0x60000000);     // nop
    write32(Loc + 4, 0x38631000); // addi r3, r3, 4096
    break;
  case R_PPC64_DTPREL16:
  case R_PPC64_DTPREL16_HA:
  case R_PPC64_DTPREL16_HI:
  case R_PPC64_DTPREL16_DS:
  case R_PPC64_DTPREL16_LO:
  case R_PPC64_DTPREL16_LO_DS:
  case R_PPC64_GOT_DTPREL16_HA:
  case R_PPC64_GOT_DTPREL16_LO_DS:
  case R_PPC64_GOT_DTPREL16_DS:
  case R_PPC64_GOT_DTPREL16_HI:
    relocateOne(Loc, Type, Val);
    break;
  default:
    llvm_unreachable("unsupported relocation for TLS LD to LE relaxation");
  }
}

static unsigned getDFormOp(unsigned SecondaryOp) {
  switch (SecondaryOp) {
  case LBZX:
    return LBZ;
  case LHZX:
    return LHZ;
  case LWZX:
    return LWZ;
  case LDX:
    return LD;
  case STBX:
    return STB;
  case STHX:
    return STH;
  case STWX:
    return STW;
  case STDX:
    return STD;
  case ADD:
    return ADDI;
  default:
    error("unrecognized instruction for IE to LE R_PPC64_TLS");
    return 0;
  }
}

void PPC64::relaxTlsIeToLe(uint8_t *Loc, RelType Type, uint64_t Val) const {
  // The initial exec code sequence for a global `x` will look like:
  // Instruction                    Relocation                Symbol
  // addis r9, r2, x@got@tprel@ha   R_PPC64_GOT_TPREL16_HA      x
  // ld    r9, x@got@tprel@l(r9)    R_PPC64_GOT_TPREL16_LO_DS   x
  // add r9, r9, x@tls              R_PPC64_TLS                 x

  // Relaxing to local exec entails converting:
  // addis r9, r2, x@got@tprel@ha       into        nop
  // ld r9, x@got@tprel@l(r9)           into        addis r9, r13, x@tprel@ha
  // add r9, r9, x@tls                  into        addi r9, r9, x@tprel@l

  // x@tls R_PPC64_TLS is a relocation which does not compute anything,
  // it is replaced with r13 (thread pointer).

  // The add instruction in the initial exec sequence has multiple variations
  // that need to be handled. If we are building an address it will use an add
  // instruction, if we are accessing memory it will use any of the X-form
  // indexed load or store instructions.

  unsigned Offset = (Config->EKind == ELF64BEKind) ? 2 : 0;
  switch (Type) {
  case R_PPC64_GOT_TPREL16_HA:
    write32(Loc - Offset, 0x60000000); // nop
    break;
  case R_PPC64_GOT_TPREL16_LO_DS:
  case R_PPC64_GOT_TPREL16_DS: {
    uint32_t RegNo = read32(Loc - Offset) & 0x03E00000; // bits 6-10
    write32(Loc - Offset, 0x3C0D0000 | RegNo);          // addis RegNo, r13
    relocateOne(Loc, R_PPC64_TPREL16_HA, Val);
    break;
  }
  case R_PPC64_TLS: {
    uint32_t PrimaryOp = getPrimaryOpCode(read32(Loc));
    if (PrimaryOp != 31)
      error("unrecognized instruction for IE to LE R_PPC64_TLS");
    uint32_t SecondaryOp = (read32(Loc) & 0x000007FE) >> 1; // bits 21-30
    uint32_t DFormOp = getDFormOp(SecondaryOp);
    write32(Loc, ((DFormOp << 26) | (read32(Loc) & 0x03FFFFFF)));
    relocateOne(Loc + Offset, R_PPC64_TPREL16_LO, Val);
    break;
  }
  default:
    llvm_unreachable("unknown relocation for IE to LE");
    break;
  }
}

RelExpr PPC64::getRelExpr(RelType Type, const Symbol &S,
                          const uint8_t *Loc) const {
  switch (Type) {
  case R_PPC64_GOT16:
  case R_PPC64_GOT16_DS:
  case R_PPC64_GOT16_HA:
  case R_PPC64_GOT16_HI:
  case R_PPC64_GOT16_LO:
  case R_PPC64_GOT16_LO_DS:
    return R_GOT_OFF;
  case R_PPC64_TOC16:
  case R_PPC64_TOC16_DS:
  case R_PPC64_TOC16_HA:
  case R_PPC64_TOC16_HI:
  case R_PPC64_TOC16_LO:
  case R_PPC64_TOC16_LO_DS:
    return R_GOTREL;
  case R_PPC64_TOC:
    return R_PPC_TOC;
  case R_PPC64_REL14:
  case R_PPC64_REL24:
    return R_PPC_CALL_PLT;
  case R_PPC64_REL16_LO:
  case R_PPC64_REL16_HA:
  case R_PPC64_REL32:
  case R_PPC64_REL64:
    return R_PC;
  case R_PPC64_GOT_TLSGD16:
  case R_PPC64_GOT_TLSGD16_HA:
  case R_PPC64_GOT_TLSGD16_HI:
  case R_PPC64_GOT_TLSGD16_LO:
    return R_TLSGD_GOT;
  case R_PPC64_GOT_TLSLD16:
  case R_PPC64_GOT_TLSLD16_HA:
  case R_PPC64_GOT_TLSLD16_HI:
  case R_PPC64_GOT_TLSLD16_LO:
    return R_TLSLD_GOT;
  case R_PPC64_GOT_TPREL16_HA:
  case R_PPC64_GOT_TPREL16_LO_DS:
  case R_PPC64_GOT_TPREL16_DS:
  case R_PPC64_GOT_TPREL16_HI:
    return R_GOT_OFF;
  case R_PPC64_GOT_DTPREL16_HA:
  case R_PPC64_GOT_DTPREL16_LO_DS:
  case R_PPC64_GOT_DTPREL16_DS:
  case R_PPC64_GOT_DTPREL16_HI:
    return R_TLSLD_GOT_OFF;
  case R_PPC64_TPREL16:
  case R_PPC64_TPREL16_HA:
  case R_PPC64_TPREL16_LO:
  case R_PPC64_TPREL16_HI:
  case R_PPC64_TPREL16_DS:
  case R_PPC64_TPREL16_LO_DS:
  case R_PPC64_TPREL16_HIGHER:
  case R_PPC64_TPREL16_HIGHERA:
  case R_PPC64_TPREL16_HIGHEST:
  case R_PPC64_TPREL16_HIGHESTA:
    return R_TLS;
  case R_PPC64_DTPREL16:
  case R_PPC64_DTPREL16_DS:
  case R_PPC64_DTPREL16_HA:
  case R_PPC64_DTPREL16_HI:
  case R_PPC64_DTPREL16_HIGHER:
  case R_PPC64_DTPREL16_HIGHERA:
  case R_PPC64_DTPREL16_HIGHEST:
  case R_PPC64_DTPREL16_HIGHESTA:
  case R_PPC64_DTPREL16_LO:
  case R_PPC64_DTPREL16_LO_DS:
  case R_PPC64_DTPREL64:
    return R_ABS;
  case R_PPC64_TLSGD:
    return R_TLSDESC_CALL;
  case R_PPC64_TLSLD:
    return R_TLSLD_HINT;
  case R_PPC64_TLS:
    return R_TLSIE_HINT;
  default:
    return R_ABS;
  }
}

void PPC64::writeGotHeader(uint8_t *Buf) const {
  write64(Buf, getPPC64TocBase());
}

void PPC64::writePltHeader(uint8_t *Buf) const {
  // The generic resolver stub goes first.
  write32(Buf +  0, 0x7c0802a6); // mflr r0
  write32(Buf +  4, 0x429f0005); // bcl  20,4*cr7+so,8 <_glink+0x8>
  write32(Buf +  8, 0x7d6802a6); // mflr r11
  write32(Buf + 12, 0x7c0803a6); // mtlr r0
  write32(Buf + 16, 0x7d8b6050); // subf r12, r11, r12
  write32(Buf + 20, 0x380cffcc); // subi r0,r12,52
  write32(Buf + 24, 0x7800f082); // srdi r0,r0,62,2
  write32(Buf + 28, 0xe98b002c); // ld   r12,44(r11)
  write32(Buf + 32, 0x7d6c5a14); // add  r11,r12,r11
  write32(Buf + 36, 0xe98b0000); // ld   r12,0(r11)
  write32(Buf + 40, 0xe96b0008); // ld   r11,8(r11)
  write32(Buf + 44, 0x7d8903a6); // mtctr   r12
  write32(Buf + 48, 0x4e800420); // bctr

  // The 'bcl' instruction will set the link register to the address of the
  // following instruction ('mflr r11'). Here we store the offset from that
  // instruction  to the first entry in the GotPlt section.
  int64_t GotPltOffset = In.GotPlt->getVA() - (In.Plt->getVA() + 8);
  write64(Buf + 52, GotPltOffset);
}

void PPC64::writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr,
                     uint64_t PltEntryAddr, int32_t Index,
                     unsigned RelOff) const {
  int32_t Offset = PltHeaderSize + Index * PltEntrySize;
  // bl __glink_PLTresolve
  write32(Buf, 0x48000000 | ((-Offset) & 0x03FFFFFc));
}

static std::pair<RelType, uint64_t> toAddr16Rel(RelType Type, uint64_t Val) {
  // Relocations relative to the toc-base need to be adjusted by the Toc offset.
  uint64_t TocBiasedVal = Val - PPC64TocOffset;
  // Relocations relative to dtv[dtpmod] need to be adjusted by the DTP offset.
  uint64_t DTPBiasedVal = Val - DynamicThreadPointerOffset;

  switch (Type) {
  // TOC biased relocation.
  case R_PPC64_GOT16:
  case R_PPC64_GOT_TLSGD16:
  case R_PPC64_GOT_TLSLD16:
  case R_PPC64_TOC16:
    return {R_PPC64_ADDR16, TocBiasedVal};
  case R_PPC64_GOT16_DS:
  case R_PPC64_TOC16_DS:
  case R_PPC64_GOT_TPREL16_DS:
  case R_PPC64_GOT_DTPREL16_DS:
    return {R_PPC64_ADDR16_DS, TocBiasedVal};
  case R_PPC64_GOT16_HA:
  case R_PPC64_GOT_TLSGD16_HA:
  case R_PPC64_GOT_TLSLD16_HA:
  case R_PPC64_GOT_TPREL16_HA:
  case R_PPC64_GOT_DTPREL16_HA:
  case R_PPC64_TOC16_HA:
    return {R_PPC64_ADDR16_HA, TocBiasedVal};
  case R_PPC64_GOT16_HI:
  case R_PPC64_GOT_TLSGD16_HI:
  case R_PPC64_GOT_TLSLD16_HI:
  case R_PPC64_GOT_TPREL16_HI:
  case R_PPC64_GOT_DTPREL16_HI:
  case R_PPC64_TOC16_HI:
    return {R_PPC64_ADDR16_HI, TocBiasedVal};
  case R_PPC64_GOT16_LO:
  case R_PPC64_GOT_TLSGD16_LO:
  case R_PPC64_GOT_TLSLD16_LO:
  case R_PPC64_TOC16_LO:
    return {R_PPC64_ADDR16_LO, TocBiasedVal};
  case R_PPC64_GOT16_LO_DS:
  case R_PPC64_TOC16_LO_DS:
  case R_PPC64_GOT_TPREL16_LO_DS:
  case R_PPC64_GOT_DTPREL16_LO_DS:
    return {R_PPC64_ADDR16_LO_DS, TocBiasedVal};

  // Dynamic Thread pointer biased relocation types.
  case R_PPC64_DTPREL16:
    return {R_PPC64_ADDR16, DTPBiasedVal};
  case R_PPC64_DTPREL16_DS:
    return {R_PPC64_ADDR16_DS, DTPBiasedVal};
  case R_PPC64_DTPREL16_HA:
    return {R_PPC64_ADDR16_HA, DTPBiasedVal};
  case R_PPC64_DTPREL16_HI:
    return {R_PPC64_ADDR16_HI, DTPBiasedVal};
  case R_PPC64_DTPREL16_HIGHER:
    return {R_PPC64_ADDR16_HIGHER, DTPBiasedVal};
  case R_PPC64_DTPREL16_HIGHERA:
    return {R_PPC64_ADDR16_HIGHERA, DTPBiasedVal};
  case R_PPC64_DTPREL16_HIGHEST:
    return {R_PPC64_ADDR16_HIGHEST, DTPBiasedVal};
  case R_PPC64_DTPREL16_HIGHESTA:
    return {R_PPC64_ADDR16_HIGHESTA, DTPBiasedVal};
  case R_PPC64_DTPREL16_LO:
    return {R_PPC64_ADDR16_LO, DTPBiasedVal};
  case R_PPC64_DTPREL16_LO_DS:
    return {R_PPC64_ADDR16_LO_DS, DTPBiasedVal};
  case R_PPC64_DTPREL64:
    return {R_PPC64_ADDR64, DTPBiasedVal};

  default:
    return {Type, Val};
  }
}

static bool isTocOptType(RelType Type) {
  switch (Type) {
  case R_PPC64_GOT16_HA:
  case R_PPC64_GOT16_LO_DS:
  case R_PPC64_TOC16_HA:
  case R_PPC64_TOC16_LO_DS:
  case R_PPC64_TOC16_LO:
    return true;
  default:
    return false;
  }
}

void PPC64::relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const {
  // We need to save the original relocation type to use in diagnostics, and
  // use the original type to determine if we should toc-optimize the
  // instructions being relocated.
  RelType OriginalType = Type;
  bool ShouldTocOptimize =  isTocOptType(Type);
  // For dynamic thread pointer relative, toc-relative, and got-indirect
  // relocations, proceed in terms of the corresponding ADDR16 relocation type.
  std::tie(Type, Val) = toAddr16Rel(Type, Val);

  switch (Type) {
  case R_PPC64_ADDR14: {
    checkAlignment(Loc, Val, 4, Type);
    // Preserve the AA/LK bits in the branch instruction
    uint8_t AALK = Loc[3];
    write16(Loc + 2, (AALK & 3) | (Val & 0xfffc));
    break;
  }
  case R_PPC64_ADDR16:
  case R_PPC64_TPREL16:
    checkInt(Loc, Val, 16, OriginalType);
    write16(Loc, Val);
    break;
  case R_PPC64_ADDR16_DS:
  case R_PPC64_TPREL16_DS: {
    checkInt(Loc, Val, 16, OriginalType);
    // DQ-form instructions use bits 28-31 as part of the instruction encoding
    // DS-form instructions only use bits 30-31.
    uint16_t Mask = isDQFormInstruction(readInstrFromHalf16(Loc)) ? 0xF : 0x3;
    checkAlignment(Loc, lo(Val), Mask + 1, OriginalType);
    write16(Loc, (read16(Loc) & Mask) | lo(Val));
  } break;
  case R_PPC64_ADDR16_HA:
  case R_PPC64_REL16_HA:
  case R_PPC64_TPREL16_HA:
    if (Config->TocOptimize && ShouldTocOptimize && ha(Val) == 0)
      writeInstrFromHalf16(Loc, 0x60000000);
    else
      write16(Loc, ha(Val));
    break;
  case R_PPC64_ADDR16_HI:
  case R_PPC64_REL16_HI:
  case R_PPC64_TPREL16_HI:
    write16(Loc, hi(Val));
    break;
  case R_PPC64_ADDR16_HIGHER:
  case R_PPC64_TPREL16_HIGHER:
    write16(Loc, higher(Val));
    break;
  case R_PPC64_ADDR16_HIGHERA:
  case R_PPC64_TPREL16_HIGHERA:
    write16(Loc, highera(Val));
    break;
  case R_PPC64_ADDR16_HIGHEST:
  case R_PPC64_TPREL16_HIGHEST:
    write16(Loc, highest(Val));
    break;
  case R_PPC64_ADDR16_HIGHESTA:
  case R_PPC64_TPREL16_HIGHESTA:
    write16(Loc, highesta(Val));
    break;
  case R_PPC64_ADDR16_LO:
  case R_PPC64_REL16_LO:
  case R_PPC64_TPREL16_LO:
    // When the high-adjusted part of a toc relocation evalutes to 0, it is
    // changed into a nop. The lo part then needs to be updated to use the
    // toc-pointer register r2, as the base register.
    if (Config->TocOptimize && ShouldTocOptimize && ha(Val) == 0) {
      uint32_t Instr = readInstrFromHalf16(Loc);
      if (isInstructionUpdateForm(Instr))
        error(getErrorLocation(Loc) +
              "can't toc-optimize an update instruction: 0x" +
              utohexstr(Instr));
      Instr = (Instr & 0xFFE00000) | 0x00020000;
      writeInstrFromHalf16(Loc, Instr);
    }
    write16(Loc, lo(Val));
    break;
  case R_PPC64_ADDR16_LO_DS:
  case R_PPC64_TPREL16_LO_DS: {
    // DQ-form instructions use bits 28-31 as part of the instruction encoding
    // DS-form instructions only use bits 30-31.
    uint32_t Inst = readInstrFromHalf16(Loc);
    uint16_t Mask = isDQFormInstruction(Inst) ? 0xF : 0x3;
    checkAlignment(Loc, lo(Val), Mask + 1, OriginalType);
    if (Config->TocOptimize && ShouldTocOptimize && ha(Val) == 0) {
      // When the high-adjusted part of a toc relocation evalutes to 0, it is
      // changed into a nop. The lo part then needs to be updated to use the toc
      // pointer register r2, as the base register.
      if (isInstructionUpdateForm(Inst))
        error(getErrorLocation(Loc) +
              "Can't toc-optimize an update instruction: 0x" +
              Twine::utohexstr(Inst));
      Inst = (Inst & 0xFFE0000F) | 0x00020000;
      writeInstrFromHalf16(Loc, Inst);
    }
    write16(Loc, (read16(Loc) & Mask) | lo(Val));
  } break;
  case R_PPC64_ADDR32:
  case R_PPC64_REL32:
    checkInt(Loc, Val, 32, Type);
    write32(Loc, Val);
    break;
  case R_PPC64_ADDR64:
  case R_PPC64_REL64:
  case R_PPC64_TOC:
    write64(Loc, Val);
    break;
  case R_PPC64_REL14: {
    uint32_t Mask = 0x0000FFFC;
    checkInt(Loc, Val, 16, Type);
    checkAlignment(Loc, Val, 4, Type);
    write32(Loc, (read32(Loc) & ~Mask) | (Val & Mask));
    break;
  }
  case R_PPC64_REL24: {
    uint32_t Mask = 0x03FFFFFC;
    checkInt(Loc, Val, 26, Type);
    checkAlignment(Loc, Val, 4, Type);
    write32(Loc, (read32(Loc) & ~Mask) | (Val & Mask));
    break;
  }
  case R_PPC64_DTPREL64:
    write64(Loc, Val - DynamicThreadPointerOffset);
    break;
  default:
    error(getErrorLocation(Loc) + "unrecognized reloc " + Twine(Type));
  }
}

bool PPC64::needsThunk(RelExpr Expr, RelType Type, const InputFile *File,
                       uint64_t BranchAddr, const Symbol &S) const {
  if (Type != R_PPC64_REL14 && Type != R_PPC64_REL24)
    return false;

  // If a function is in the Plt it needs to be called with a call-stub.
  if (S.isInPlt())
    return true;

  // If a symbol is a weak undefined and we are compiling an executable
  // it doesn't need a range-extending thunk since it can't be called.
  if (S.isUndefWeak() && !Config->Shared)
    return false;

  // If the offset exceeds the range of the branch type then it will need
  // a range-extending thunk.
  return !inBranchRange(Type, BranchAddr, S.getVA());
}

bool PPC64::inBranchRange(RelType Type, uint64_t Src, uint64_t Dst) const {
  int64_t Offset = Dst - Src;
  if (Type == R_PPC64_REL14)
    return isInt<16>(Offset);
  if (Type == R_PPC64_REL24)
    return isInt<26>(Offset);
  llvm_unreachable("unsupported relocation type used in branch");
}

RelExpr PPC64::adjustRelaxExpr(RelType Type, const uint8_t *Data,
                               RelExpr Expr) const {
  if (Expr == R_RELAX_TLS_GD_TO_IE)
    return R_RELAX_TLS_GD_TO_IE_GOT_OFF;
  if (Expr == R_RELAX_TLS_LD_TO_LE)
    return R_RELAX_TLS_LD_TO_LE_ABS;
  return Expr;
}

// Reference: 3.7.4.1 of the 64-bit ELF V2 abi supplement.
// The general dynamic code sequence for a global `x` uses 4 instructions.
// Instruction                    Relocation                Symbol
// addis r3, r2, x@got@tlsgd@ha   R_PPC64_GOT_TLSGD16_HA      x
// addi  r3, r3, x@got@tlsgd@l    R_PPC64_GOT_TLSGD16_LO      x
// bl __tls_get_addr(x@tlsgd)     R_PPC64_TLSGD               x
//                                R_PPC64_REL24               __tls_get_addr
// nop                            None                       None
//
// Relaxing to initial-exec entails:
// 1) Convert the addis/addi pair that builds the address of the tls_index
//    struct for 'x' to an addis/ld pair that loads an offset from a got-entry.
// 2) Convert the call to __tls_get_addr to a nop.
// 3) Convert the nop following the call to an add of the loaded offset to the
//    thread pointer.
// Since the nop must directly follow the call, the R_PPC64_TLSGD relocation is
// used as the relaxation hint for both steps 2 and 3.
void PPC64::relaxTlsGdToIe(uint8_t *Loc, RelType Type, uint64_t Val) const {
  switch (Type) {
  case R_PPC64_GOT_TLSGD16_HA:
    // This is relaxed from addis rT, r2, sym@got@tlsgd@ha to
    //                      addis rT, r2, sym@got@tprel@ha.
    relocateOne(Loc, R_PPC64_GOT_TPREL16_HA, Val);
    return;
  case R_PPC64_GOT_TLSGD16_LO: {
    // Relax from addi  r3, rA, sym@got@tlsgd@l to
    //            ld r3, sym@got@tprel@l(rA)
    uint32_t InputRegister = (readInstrFromHalf16(Loc) & (0x1f << 16));
    writeInstrFromHalf16(Loc, 0xE8600000 | InputRegister);
    relocateOne(Loc, R_PPC64_GOT_TPREL16_LO_DS, Val);
    return;
  }
  case R_PPC64_TLSGD:
    write32(Loc, 0x60000000);     // bl __tls_get_addr(sym@tlsgd) --> nop
    write32(Loc + 4, 0x7c636A14); // nop --> add r3, r3, r13
    return;
  default:
    llvm_unreachable("unsupported relocation for TLS GD to IE relaxation");
  }
}

// The prologue for a split-stack function is expected to look roughly
// like this:
//    .Lglobal_entry_point:
//      # TOC pointer initalization.
//      ...
//    .Llocal_entry_point:
//      # load the __private_ss member of the threads tcbhead.
//      ld r0,-0x7000-64(r13)
//      # subtract the functions stack size from the stack pointer.
//      addis r12, r1, ha(-stack-frame size)
//      addi  r12, r12, l(-stack-frame size)
//      # compare needed to actual and branch to allocate_more_stack if more
//      # space is needed, otherwise fallthrough to 'normal' function body.
//      cmpld cr7,r12,r0
//      blt- cr7, .Lallocate_more_stack
//
// -) The allocate_more_stack block might be placed after the split-stack
//    prologue and the `blt-` replaced with a `bge+ .Lnormal_func_body`
//    instead.
// -) If either the addis or addi is not needed due to the stack size being
//    smaller then 32K or a multiple of 64K they will be replaced with a nop,
//    but there will always be 2 instructions the linker can overwrite for the
//    adjusted stack size.
//
// The linkers job here is to increase the stack size used in the addis/addi
// pair by split-stack-size-adjust.
// addis r12, r1, ha(-stack-frame size - split-stack-adjust-size)
// addi  r12, r12, l(-stack-frame size - split-stack-adjust-size)
bool PPC64::adjustPrologueForCrossSplitStack(uint8_t *Loc, uint8_t *End,
                                             uint8_t StOther) const {
  // If the caller has a global entry point adjust the buffer past it. The start
  // of the split-stack prologue will be at the local entry point.
  Loc += getPPC64GlobalEntryToLocalEntryOffset(StOther);

  // At the very least we expect to see a load of some split-stack data from the
  // tcb, and 2 instructions that calculate the ending stack address this
  // function will require. If there is not enough room for at least 3
  // instructions it can't be a split-stack prologue.
  if (Loc + 12 >= End)
    return false;

  // First instruction must be `ld r0, -0x7000-64(r13)`
  if (read32(Loc) != 0xe80d8fc0)
    return false;

  int16_t HiImm = 0;
  int16_t LoImm = 0;
  // First instruction can be either an addis if the frame size is larger then
  // 32K, or an addi if the size is less then 32K.
  int32_t FirstInstr = read32(Loc + 4);
  if (getPrimaryOpCode(FirstInstr) == 15) {
    HiImm = FirstInstr & 0xFFFF;
  } else if (getPrimaryOpCode(FirstInstr) == 14) {
    LoImm = FirstInstr & 0xFFFF;
  } else {
    return false;
  }

  // Second instruction is either an addi or a nop. If the first instruction was
  // an addi then LoImm is set and the second instruction must be a nop.
  uint32_t SecondInstr = read32(Loc + 8);
  if (!LoImm && getPrimaryOpCode(SecondInstr) == 14) {
    LoImm = SecondInstr & 0xFFFF;
  } else if (SecondInstr != 0x60000000) {
    return false;
  }

  // The register operands of the first instruction should be the stack-pointer
  // (r1) as the input (RA) and r12 as the output (RT). If the second
  // instruction is not a nop, then it should use r12 as both input and output.
  auto CheckRegOperands = [](uint32_t Instr, uint8_t ExpectedRT,
                             uint8_t ExpectedRA) {
    return ((Instr & 0x3E00000) >> 21 == ExpectedRT) &&
           ((Instr & 0x1F0000) >> 16 == ExpectedRA);
  };
  if (!CheckRegOperands(FirstInstr, 12, 1))
    return false;
  if (SecondInstr != 0x60000000 && !CheckRegOperands(SecondInstr, 12, 12))
    return false;

  int32_t StackFrameSize = (HiImm * 65536) + LoImm;
  // Check that the adjusted size doesn't overflow what we can represent with 2
  // instructions.
  if (StackFrameSize < Config->SplitStackAdjustSize + INT32_MIN) {
    error(getErrorLocation(Loc) + "split-stack prologue adjustment overflows");
    return false;
  }

  int32_t AdjustedStackFrameSize =
      StackFrameSize - Config->SplitStackAdjustSize;

  LoImm = AdjustedStackFrameSize & 0xFFFF;
  HiImm = (AdjustedStackFrameSize + 0x8000) >> 16;
  if (HiImm) {
    write32(Loc + 4, 0x3D810000 | (uint16_t)HiImm);
    // If the low immediate is zero the second instruction will be a nop.
    SecondInstr = LoImm ? 0x398C0000 | (uint16_t)LoImm : 0x60000000;
    write32(Loc + 8, SecondInstr);
  } else {
    // addi r12, r1, imm
    write32(Loc + 4, (0x39810000) | (uint16_t)LoImm);
    write32(Loc + 8, 0x60000000);
  }

  return true;
}

TargetInfo *elf::getPPC64TargetInfo() {
  static PPC64 Target;
  return &Target;
}
