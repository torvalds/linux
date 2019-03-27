//===- X86_64.cpp ---------------------------------------------------------===//
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
#include "llvm/Object/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
template <class ELFT> class X86_64 : public TargetInfo {
public:
  X86_64();
  RelExpr getRelExpr(RelType Type, const Symbol &S,
                     const uint8_t *Loc) const override;
  RelType getDynRel(RelType Type) const override;
  void writeGotPltHeader(uint8_t *Buf) const override;
  void writeGotPlt(uint8_t *Buf, const Symbol &S) const override;
  void writePltHeader(uint8_t *Buf) const override;
  void writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
  void relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const override;

  RelExpr adjustRelaxExpr(RelType Type, const uint8_t *Data,
                          RelExpr Expr) const override;
  void relaxGot(uint8_t *Loc, uint64_t Val) const override;
  void relaxTlsGdToIe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsGdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsIeToLe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsLdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  bool adjustPrologueForCrossSplitStack(uint8_t *Loc, uint8_t *End,
                                        uint8_t StOther) const override;

private:
  void relaxGotNoPic(uint8_t *Loc, uint64_t Val, uint8_t Op,
                     uint8_t ModRm) const;
};
} // namespace

template <class ELFT> X86_64<ELFT>::X86_64() {
  CopyRel = R_X86_64_COPY;
  GotRel = R_X86_64_GLOB_DAT;
  NoneRel = R_X86_64_NONE;
  PltRel = R_X86_64_JUMP_SLOT;
  RelativeRel = R_X86_64_RELATIVE;
  IRelativeRel = R_X86_64_IRELATIVE;
  TlsGotRel = R_X86_64_TPOFF64;
  TlsModuleIndexRel = R_X86_64_DTPMOD64;
  TlsOffsetRel = R_X86_64_DTPOFF64;
  GotEntrySize = 8;
  GotPltEntrySize = 8;
  PltEntrySize = 16;
  PltHeaderSize = 16;
  TlsGdRelaxSkip = 2;
  TrapInstr = {0xcc, 0xcc, 0xcc, 0xcc}; // 0xcc = INT3

  // Align to the large page size (known as a superpage or huge page).
  // FreeBSD automatically promotes large, superpage-aligned allocations.
  DefaultImageBase = 0x200000;
}

template <class ELFT>
RelExpr X86_64<ELFT>::getRelExpr(RelType Type, const Symbol &S,
                                 const uint8_t *Loc) const {
  if (Type == R_X86_64_GOTTPOFF)
    Config->HasStaticTlsModel = true;

  switch (Type) {
  case R_X86_64_8:
  case R_X86_64_16:
  case R_X86_64_32:
  case R_X86_64_32S:
  case R_X86_64_64:
  case R_X86_64_DTPOFF32:
  case R_X86_64_DTPOFF64:
    return R_ABS;
  case R_X86_64_TPOFF32:
    return R_TLS;
  case R_X86_64_TLSLD:
    return R_TLSLD_PC;
  case R_X86_64_TLSGD:
    return R_TLSGD_PC;
  case R_X86_64_SIZE32:
  case R_X86_64_SIZE64:
    return R_SIZE;
  case R_X86_64_PLT32:
    return R_PLT_PC;
  case R_X86_64_PC32:
  case R_X86_64_PC64:
    return R_PC;
  case R_X86_64_GOT32:
  case R_X86_64_GOT64:
    return R_GOT_FROM_END;
  case R_X86_64_GOTPCREL:
  case R_X86_64_GOTPCRELX:
  case R_X86_64_REX_GOTPCRELX:
  case R_X86_64_GOTTPOFF:
    return R_GOT_PC;
  case R_X86_64_GOTOFF64:
    return R_GOTREL_FROM_END;
  case R_X86_64_GOTPC32:
  case R_X86_64_GOTPC64:
    return R_GOTONLY_PC_FROM_END;
  case R_X86_64_NONE:
    return R_NONE;
  default:
    return R_INVALID;
  }
}

template <class ELFT> void X86_64<ELFT>::writeGotPltHeader(uint8_t *Buf) const {
  // The first entry holds the value of _DYNAMIC. It is not clear why that is
  // required, but it is documented in the psabi and the glibc dynamic linker
  // seems to use it (note that this is relevant for linking ld.so, not any
  // other program).
  write64le(Buf, In.Dynamic->getVA());
}

template <class ELFT>
void X86_64<ELFT>::writeGotPlt(uint8_t *Buf, const Symbol &S) const {
  // See comments in X86::writeGotPlt.
  write64le(Buf, S.getPltVA() + 6);
}

template <class ELFT> void X86_64<ELFT>::writePltHeader(uint8_t *Buf) const {
  const uint8_t PltData[] = {
      0xff, 0x35, 0, 0, 0, 0, // pushq GOTPLT+8(%rip)
      0xff, 0x25, 0, 0, 0, 0, // jmp *GOTPLT+16(%rip)
      0x0f, 0x1f, 0x40, 0x00, // nop
  };
  memcpy(Buf, PltData, sizeof(PltData));
  uint64_t GotPlt = In.GotPlt->getVA();
  uint64_t Plt = In.Plt->getVA();
  write32le(Buf + 2, GotPlt - Plt + 2); // GOTPLT+8
  write32le(Buf + 8, GotPlt - Plt + 4); // GOTPLT+16
}

template <class ELFT>
void X86_64<ELFT>::writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr,
                            uint64_t PltEntryAddr, int32_t Index,
                            unsigned RelOff) const {
  const uint8_t Inst[] = {
      0xff, 0x25, 0, 0, 0, 0, // jmpq *got(%rip)
      0x68, 0, 0, 0, 0,       // pushq <relocation index>
      0xe9, 0, 0, 0, 0,       // jmpq plt[0]
  };
  memcpy(Buf, Inst, sizeof(Inst));

  write32le(Buf + 2, GotPltEntryAddr - PltEntryAddr - 6);
  write32le(Buf + 7, Index);
  write32le(Buf + 12, -getPltEntryOffset(Index) - 16);
}

template <class ELFT> RelType X86_64<ELFT>::getDynRel(RelType Type) const {
  if (Type == R_X86_64_64 || Type == R_X86_64_PC64 || Type == R_X86_64_SIZE32 ||
      Type == R_X86_64_SIZE64)
    return Type;
  return R_X86_64_NONE;
}

template <class ELFT>
void X86_64<ELFT>::relaxTlsGdToLe(uint8_t *Loc, RelType Type,
                                  uint64_t Val) const {
  // Convert
  //   .byte 0x66
  //   leaq x@tlsgd(%rip), %rdi
  //   .word 0x6666
  //   rex64
  //   call __tls_get_addr@plt
  // to
  //   mov %fs:0x0,%rax
  //   lea x@tpoff,%rax
  const uint8_t Inst[] = {
      0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, // mov %fs:0x0,%rax
      0x48, 0x8d, 0x80, 0, 0, 0, 0,                         // lea x@tpoff,%rax
  };
  memcpy(Loc - 4, Inst, sizeof(Inst));

  // The original code used a pc relative relocation and so we have to
  // compensate for the -4 in had in the addend.
  write32le(Loc + 8, Val + 4);
}

template <class ELFT>
void X86_64<ELFT>::relaxTlsGdToIe(uint8_t *Loc, RelType Type,
                                  uint64_t Val) const {
  // Convert
  //   .byte 0x66
  //   leaq x@tlsgd(%rip), %rdi
  //   .word 0x6666
  //   rex64
  //   call __tls_get_addr@plt
  // to
  //   mov %fs:0x0,%rax
  //   addq x@tpoff,%rax
  const uint8_t Inst[] = {
      0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, // mov %fs:0x0,%rax
      0x48, 0x03, 0x05, 0, 0, 0, 0,                         // addq x@tpoff,%rax
  };
  memcpy(Loc - 4, Inst, sizeof(Inst));

  // Both code sequences are PC relatives, but since we are moving the constant
  // forward by 8 bytes we have to subtract the value by 8.
  write32le(Loc + 8, Val - 8);
}

// In some conditions, R_X86_64_GOTTPOFF relocation can be optimized to
// R_X86_64_TPOFF32 so that it does not use GOT.
template <class ELFT>
void X86_64<ELFT>::relaxTlsIeToLe(uint8_t *Loc, RelType Type,
                                  uint64_t Val) const {
  uint8_t *Inst = Loc - 3;
  uint8_t Reg = Loc[-1] >> 3;
  uint8_t *RegSlot = Loc - 1;

  // Note that ADD with RSP or R12 is converted to ADD instead of LEA
  // because LEA with these registers needs 4 bytes to encode and thus
  // wouldn't fit the space.

  if (memcmp(Inst, "\x48\x03\x25", 3) == 0) {
    // "addq foo@gottpoff(%rip),%rsp" -> "addq $foo,%rsp"
    memcpy(Inst, "\x48\x81\xc4", 3);
  } else if (memcmp(Inst, "\x4c\x03\x25", 3) == 0) {
    // "addq foo@gottpoff(%rip),%r12" -> "addq $foo,%r12"
    memcpy(Inst, "\x49\x81\xc4", 3);
  } else if (memcmp(Inst, "\x4c\x03", 2) == 0) {
    // "addq foo@gottpoff(%rip),%r[8-15]" -> "leaq foo(%r[8-15]),%r[8-15]"
    memcpy(Inst, "\x4d\x8d", 2);
    *RegSlot = 0x80 | (Reg << 3) | Reg;
  } else if (memcmp(Inst, "\x48\x03", 2) == 0) {
    // "addq foo@gottpoff(%rip),%reg -> "leaq foo(%reg),%reg"
    memcpy(Inst, "\x48\x8d", 2);
    *RegSlot = 0x80 | (Reg << 3) | Reg;
  } else if (memcmp(Inst, "\x4c\x8b", 2) == 0) {
    // "movq foo@gottpoff(%rip),%r[8-15]" -> "movq $foo,%r[8-15]"
    memcpy(Inst, "\x49\xc7", 2);
    *RegSlot = 0xc0 | Reg;
  } else if (memcmp(Inst, "\x48\x8b", 2) == 0) {
    // "movq foo@gottpoff(%rip),%reg" -> "movq $foo,%reg"
    memcpy(Inst, "\x48\xc7", 2);
    *RegSlot = 0xc0 | Reg;
  } else {
    error(getErrorLocation(Loc - 3) +
          "R_X86_64_GOTTPOFF must be used in MOVQ or ADDQ instructions only");
  }

  // The original code used a PC relative relocation.
  // Need to compensate for the -4 it had in the addend.
  write32le(Loc, Val + 4);
}

template <class ELFT>
void X86_64<ELFT>::relaxTlsLdToLe(uint8_t *Loc, RelType Type,
                                  uint64_t Val) const {
  if (Type == R_X86_64_DTPOFF64) {
    write64le(Loc, Val);
    return;
  }
  if (Type == R_X86_64_DTPOFF32) {
    write32le(Loc, Val);
    return;
  }

  const uint8_t Inst[] = {
      0x66, 0x66,                                           // .word 0x6666
      0x66,                                                 // .byte 0x66
      0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, // mov %fs:0,%rax
  };

  if (Loc[4] == 0xe8) {
    // Convert
    //   leaq bar@tlsld(%rip), %rdi           # 48 8d 3d <Loc>
    //   callq __tls_get_addr@PLT             # e8 <disp32>
    //   leaq bar@dtpoff(%rax), %rcx
    // to
    //   .word 0x6666
    //   .byte 0x66
    //   mov %fs:0,%rax
    //   leaq bar@tpoff(%rax), %rcx
    memcpy(Loc - 3, Inst, sizeof(Inst));
    return;
  }

  if (Loc[4] == 0xff && Loc[5] == 0x15) {
    // Convert
    //   leaq  x@tlsld(%rip),%rdi               # 48 8d 3d <Loc>
    //   call *__tls_get_addr@GOTPCREL(%rip)    # ff 15 <disp32>
    // to
    //   .long  0x66666666
    //   movq   %fs:0,%rax
    // See "Table 11.9: LD -> LE Code Transition (LP64)" in
    // https://raw.githubusercontent.com/wiki/hjl-tools/x86-psABI/x86-64-psABI-1.0.pdf
    Loc[-3] = 0x66;
    memcpy(Loc - 2, Inst, sizeof(Inst));
    return;
  }

  error(getErrorLocation(Loc - 3) +
        "expected R_X86_64_PLT32 or R_X86_64_GOTPCRELX after R_X86_64_TLSLD");
}

template <class ELFT>
void X86_64<ELFT>::relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const {
  switch (Type) {
  case R_X86_64_8:
    checkUInt(Loc, Val, 8, Type);
    *Loc = Val;
    break;
  case R_X86_64_16:
    checkUInt(Loc, Val, 16, Type);
    write16le(Loc, Val);
    break;
  case R_X86_64_32:
    checkUInt(Loc, Val, 32, Type);
    write32le(Loc, Val);
    break;
  case R_X86_64_32S:
  case R_X86_64_TPOFF32:
  case R_X86_64_GOT32:
  case R_X86_64_GOTPC32:
  case R_X86_64_GOTPCREL:
  case R_X86_64_GOTPCRELX:
  case R_X86_64_REX_GOTPCRELX:
  case R_X86_64_PC32:
  case R_X86_64_GOTTPOFF:
  case R_X86_64_PLT32:
  case R_X86_64_TLSGD:
  case R_X86_64_TLSLD:
  case R_X86_64_DTPOFF32:
  case R_X86_64_SIZE32:
    checkInt(Loc, Val, 32, Type);
    write32le(Loc, Val);
    break;
  case R_X86_64_64:
  case R_X86_64_DTPOFF64:
  case R_X86_64_GLOB_DAT:
  case R_X86_64_PC64:
  case R_X86_64_SIZE64:
  case R_X86_64_GOT64:
  case R_X86_64_GOTOFF64:
  case R_X86_64_GOTPC64:
    write64le(Loc, Val);
    break;
  default:
    error(getErrorLocation(Loc) + "unrecognized reloc " + Twine(Type));
  }
}

template <class ELFT>
RelExpr X86_64<ELFT>::adjustRelaxExpr(RelType Type, const uint8_t *Data,
                                      RelExpr RelExpr) const {
  if (Type != R_X86_64_GOTPCRELX && Type != R_X86_64_REX_GOTPCRELX)
    return RelExpr;
  const uint8_t Op = Data[-2];
  const uint8_t ModRm = Data[-1];

  // FIXME: When PIC is disabled and foo is defined locally in the
  // lower 32 bit address space, memory operand in mov can be converted into
  // immediate operand. Otherwise, mov must be changed to lea. We support only
  // latter relaxation at this moment.
  if (Op == 0x8b)
    return R_RELAX_GOT_PC;

  // Relax call and jmp.
  if (Op == 0xff && (ModRm == 0x15 || ModRm == 0x25))
    return R_RELAX_GOT_PC;

  // Relaxation of test, adc, add, and, cmp, or, sbb, sub, xor.
  // If PIC then no relaxation is available.
  // We also don't relax test/binop instructions without REX byte,
  // they are 32bit operations and not common to have.
  assert(Type == R_X86_64_REX_GOTPCRELX);
  return Config->Pic ? RelExpr : R_RELAX_GOT_PC_NOPIC;
}

// A subset of relaxations can only be applied for no-PIC. This method
// handles such relaxations. Instructions encoding information was taken from:
// "Intel 64 and IA-32 Architectures Software Developer's Manual V2"
// (http://www.intel.com/content/dam/www/public/us/en/documents/manuals/
//    64-ia-32-architectures-software-developer-instruction-set-reference-manual-325383.pdf)
template <class ELFT>
void X86_64<ELFT>::relaxGotNoPic(uint8_t *Loc, uint64_t Val, uint8_t Op,
                                 uint8_t ModRm) const {
  const uint8_t Rex = Loc[-3];
  // Convert "test %reg, foo@GOTPCREL(%rip)" to "test $foo, %reg".
  if (Op == 0x85) {
    // See "TEST-Logical Compare" (4-428 Vol. 2B),
    // TEST r/m64, r64 uses "full" ModR / M byte (no opcode extension).

    // ModR/M byte has form XX YYY ZZZ, where
    // YYY is MODRM.reg(register 2), ZZZ is MODRM.rm(register 1).
    // XX has different meanings:
    // 00: The operand's memory address is in reg1.
    // 01: The operand's memory address is reg1 + a byte-sized displacement.
    // 10: The operand's memory address is reg1 + a word-sized displacement.
    // 11: The operand is reg1 itself.
    // If an instruction requires only one operand, the unused reg2 field
    // holds extra opcode bits rather than a register code
    // 0xC0 == 11 000 000 binary.
    // 0x38 == 00 111 000 binary.
    // We transfer reg2 to reg1 here as operand.
    // See "2.1.3 ModR/M and SIB Bytes" (Vol. 2A 2-3).
    Loc[-1] = 0xc0 | (ModRm & 0x38) >> 3; // ModR/M byte.

    // Change opcode from TEST r/m64, r64 to TEST r/m64, imm32
    // See "TEST-Logical Compare" (4-428 Vol. 2B).
    Loc[-2] = 0xf7;

    // Move R bit to the B bit in REX byte.
    // REX byte is encoded as 0100WRXB, where
    // 0100 is 4bit fixed pattern.
    // REX.W When 1, a 64-bit operand size is used. Otherwise, when 0, the
    //   default operand size is used (which is 32-bit for most but not all
    //   instructions).
    // REX.R This 1-bit value is an extension to the MODRM.reg field.
    // REX.X This 1-bit value is an extension to the SIB.index field.
    // REX.B This 1-bit value is an extension to the MODRM.rm field or the
    // SIB.base field.
    // See "2.2.1.2 More on REX Prefix Fields " (2-8 Vol. 2A).
    Loc[-3] = (Rex & ~0x4) | (Rex & 0x4) >> 2;
    write32le(Loc, Val);
    return;
  }

  // If we are here then we need to relax the adc, add, and, cmp, or, sbb, sub
  // or xor operations.

  // Convert "binop foo@GOTPCREL(%rip), %reg" to "binop $foo, %reg".
  // Logic is close to one for test instruction above, but we also
  // write opcode extension here, see below for details.
  Loc[-1] = 0xc0 | (ModRm & 0x38) >> 3 | (Op & 0x3c); // ModR/M byte.

  // Primary opcode is 0x81, opcode extension is one of:
  // 000b = ADD, 001b is OR, 010b is ADC, 011b is SBB,
  // 100b is AND, 101b is SUB, 110b is XOR, 111b is CMP.
  // This value was wrote to MODRM.reg in a line above.
  // See "3.2 INSTRUCTIONS (A-M)" (Vol. 2A 3-15),
  // "INSTRUCTION SET REFERENCE, N-Z" (Vol. 2B 4-1) for
  // descriptions about each operation.
  Loc[-2] = 0x81;
  Loc[-3] = (Rex & ~0x4) | (Rex & 0x4) >> 2;
  write32le(Loc, Val);
}

template <class ELFT>
void X86_64<ELFT>::relaxGot(uint8_t *Loc, uint64_t Val) const {
  const uint8_t Op = Loc[-2];
  const uint8_t ModRm = Loc[-1];

  // Convert "mov foo@GOTPCREL(%rip),%reg" to "lea foo(%rip),%reg".
  if (Op == 0x8b) {
    Loc[-2] = 0x8d;
    write32le(Loc, Val);
    return;
  }

  if (Op != 0xff) {
    // We are relaxing a rip relative to an absolute, so compensate
    // for the old -4 addend.
    assert(!Config->Pic);
    relaxGotNoPic(Loc, Val + 4, Op, ModRm);
    return;
  }

  // Convert call/jmp instructions.
  if (ModRm == 0x15) {
    // ABI says we can convert "call *foo@GOTPCREL(%rip)" to "nop; call foo".
    // Instead we convert to "addr32 call foo" where addr32 is an instruction
    // prefix. That makes result expression to be a single instruction.
    Loc[-2] = 0x67; // addr32 prefix
    Loc[-1] = 0xe8; // call
    write32le(Loc, Val);
    return;
  }

  // Convert "jmp *foo@GOTPCREL(%rip)" to "jmp foo; nop".
  // jmp doesn't return, so it is fine to use nop here, it is just a stub.
  assert(ModRm == 0x25);
  Loc[-2] = 0xe9; // jmp
  Loc[3] = 0x90;  // nop
  write32le(Loc - 1, Val + 1);
}

// This anonymous namespace works around a warning bug in
// old versions of gcc. See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=56480
namespace {

// A split-stack prologue starts by checking the amount of stack remaining
// in one of two ways:
// A) Comparing of the stack pointer to a field in the tcb.
// B) Or a load of a stack pointer offset with an lea to r10 or r11.
template <>
bool X86_64<ELF64LE>::adjustPrologueForCrossSplitStack(uint8_t *Loc,
                                                       uint8_t *End,
                                                       uint8_t StOther) const {
  if (Loc + 8 >= End)
    return false;

  // Replace "cmp %fs:0x70,%rsp" and subsequent branch
  // with "stc, nopl 0x0(%rax,%rax,1)"
  if (memcmp(Loc, "\x64\x48\x3b\x24\x25", 5) == 0) {
    memcpy(Loc, "\xf9\x0f\x1f\x84\x00\x00\x00\x00", 8);
    return true;
  }

  // Adjust "lea X(%rsp),%rYY" to lea "(X - 0x4000)(%rsp),%rYY" where rYY could
  // be r10 or r11. The lea instruction feeds a subsequent compare which checks
  // if there is X available stack space. Making X larger effectively reserves
  // that much additional space. The stack grows downward so subtract the value.
  if (memcmp(Loc, "\x4c\x8d\x94\x24", 4) == 0 ||
      memcmp(Loc, "\x4c\x8d\x9c\x24", 4) == 0) {
    // The offset bytes are encoded four bytes after the start of the
    // instruction.
    write32le(Loc + 4, read32le(Loc + 4) - 0x4000);
    return true;
  }
  return false;
}

template <>
bool X86_64<ELF32LE>::adjustPrologueForCrossSplitStack(uint8_t *Loc,
                                                       uint8_t *End,
                                                       uint8_t StOther) const {
  llvm_unreachable("Target doesn't support split stacks.");
}

} // namespace

// These nonstandard PLT entries are to migtigate Spectre v2 security
// vulnerability. In order to mitigate Spectre v2, we want to avoid indirect
// branch instructions such as `jmp *GOTPLT(%rip)`. So, in the following PLT
// entries, we use a CALL followed by MOV and RET to do the same thing as an
// indirect jump. That instruction sequence is so-called "retpoline".
//
// We have two types of retpoline PLTs as a size optimization. If `-z now`
// is specified, all dynamic symbols are resolved at load-time. Thus, when
// that option is given, we can omit code for symbol lazy resolution.
namespace {
template <class ELFT> class Retpoline : public X86_64<ELFT> {
public:
  Retpoline();
  void writeGotPlt(uint8_t *Buf, const Symbol &S) const override;
  void writePltHeader(uint8_t *Buf) const override;
  void writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
};

template <class ELFT> class RetpolineZNow : public X86_64<ELFT> {
public:
  RetpolineZNow();
  void writeGotPlt(uint8_t *Buf, const Symbol &S) const override {}
  void writePltHeader(uint8_t *Buf) const override;
  void writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
};
} // namespace

template <class ELFT> Retpoline<ELFT>::Retpoline() {
  TargetInfo::PltHeaderSize = 48;
  TargetInfo::PltEntrySize = 32;
}

template <class ELFT>
void Retpoline<ELFT>::writeGotPlt(uint8_t *Buf, const Symbol &S) const {
  write64le(Buf, S.getPltVA() + 17);
}

template <class ELFT> void Retpoline<ELFT>::writePltHeader(uint8_t *Buf) const {
  const uint8_t Insn[] = {
      0xff, 0x35, 0,    0,    0,    0,          // 0:    pushq GOTPLT+8(%rip)
      0x4c, 0x8b, 0x1d, 0,    0,    0,    0,    // 6:    mov GOTPLT+16(%rip), %r11
      0xe8, 0x0e, 0x00, 0x00, 0x00,             // d:    callq next
      0xf3, 0x90,                               // 12: loop: pause
      0x0f, 0xae, 0xe8,                         // 14:   lfence
      0xeb, 0xf9,                               // 17:   jmp loop
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 19:   int3; .align 16
      0x4c, 0x89, 0x1c, 0x24,                   // 20: next: mov %r11, (%rsp)
      0xc3,                                     // 24:   ret
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 25:   int3; padding
      0xcc, 0xcc, 0xcc, 0xcc,                   // 2c:   int3; padding
  };
  memcpy(Buf, Insn, sizeof(Insn));

  uint64_t GotPlt = In.GotPlt->getVA();
  uint64_t Plt = In.Plt->getVA();
  write32le(Buf + 2, GotPlt - Plt - 6 + 8);
  write32le(Buf + 9, GotPlt - Plt - 13 + 16);
}

template <class ELFT>
void Retpoline<ELFT>::writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr,
                               uint64_t PltEntryAddr, int32_t Index,
                               unsigned RelOff) const {
  const uint8_t Insn[] = {
      0x4c, 0x8b, 0x1d, 0, 0, 0, 0, // 0:  mov foo@GOTPLT(%rip), %r11
      0xe8, 0,    0,    0,    0,    // 7:  callq plt+0x20
      0xe9, 0,    0,    0,    0,    // c:  jmp plt+0x12
      0x68, 0,    0,    0,    0,    // 11: pushq <relocation index>
      0xe9, 0,    0,    0,    0,    // 16: jmp plt+0
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 1b: int3; padding
  };
  memcpy(Buf, Insn, sizeof(Insn));

  uint64_t Off = getPltEntryOffset(Index);

  write32le(Buf + 3, GotPltEntryAddr - PltEntryAddr - 7);
  write32le(Buf + 8, -Off - 12 + 32);
  write32le(Buf + 13, -Off - 17 + 18);
  write32le(Buf + 18, Index);
  write32le(Buf + 23, -Off - 27);
}

template <class ELFT> RetpolineZNow<ELFT>::RetpolineZNow() {
  TargetInfo::PltHeaderSize = 32;
  TargetInfo::PltEntrySize = 16;
}

template <class ELFT>
void RetpolineZNow<ELFT>::writePltHeader(uint8_t *Buf) const {
  const uint8_t Insn[] = {
      0xe8, 0x0b, 0x00, 0x00, 0x00, // 0:    call next
      0xf3, 0x90,                   // 5:  loop: pause
      0x0f, 0xae, 0xe8,             // 7:    lfence
      0xeb, 0xf9,                   // a:    jmp loop
      0xcc, 0xcc, 0xcc, 0xcc,       // c:    int3; .align 16
      0x4c, 0x89, 0x1c, 0x24,       // 10: next: mov %r11, (%rsp)
      0xc3,                         // 14:   ret
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 15:   int3; padding
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 1a:   int3; padding
      0xcc,                         // 1f:   int3; padding
  };
  memcpy(Buf, Insn, sizeof(Insn));
}

template <class ELFT>
void RetpolineZNow<ELFT>::writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr,
                                   uint64_t PltEntryAddr, int32_t Index,
                                   unsigned RelOff) const {
  const uint8_t Insn[] = {
      0x4c, 0x8b, 0x1d, 0,    0, 0, 0, // mov foo@GOTPLT(%rip), %r11
      0xe9, 0,    0,    0,    0,       // jmp plt+0
      0xcc, 0xcc, 0xcc, 0xcc,          // int3; padding
  };
  memcpy(Buf, Insn, sizeof(Insn));

  write32le(Buf + 3, GotPltEntryAddr - PltEntryAddr - 7);
  write32le(Buf + 8, -getPltEntryOffset(Index) - 12);
}

template <class ELFT> static TargetInfo *getTargetInfo() {
  if (Config->ZRetpolineplt) {
    if (Config->ZNow) {
      static RetpolineZNow<ELFT> T;
      return &T;
    }
    static Retpoline<ELFT> T;
    return &T;
  }

  static X86_64<ELFT> T;
  return &T;
}

TargetInfo *elf::getX32TargetInfo() { return getTargetInfo<ELF32LE>(); }
TargetInfo *elf::getX86_64TargetInfo() { return getTargetInfo<ELF64LE>(); }
