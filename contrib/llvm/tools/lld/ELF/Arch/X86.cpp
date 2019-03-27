//===- X86.cpp ------------------------------------------------------------===//
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
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class X86 : public TargetInfo {
public:
  X86();
  RelExpr getRelExpr(RelType Type, const Symbol &S,
                     const uint8_t *Loc) const override;
  int64_t getImplicitAddend(const uint8_t *Buf, RelType Type) const override;
  void writeGotPltHeader(uint8_t *Buf) const override;
  RelType getDynRel(RelType Type) const override;
  void writeGotPlt(uint8_t *Buf, const Symbol &S) const override;
  void writeIgotPlt(uint8_t *Buf, const Symbol &S) const override;
  void writePltHeader(uint8_t *Buf) const override;
  void writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
  void relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const override;

  RelExpr adjustRelaxExpr(RelType Type, const uint8_t *Data,
                          RelExpr Expr) const override;
  void relaxTlsGdToIe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsGdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsIeToLe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsLdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
};
} // namespace

X86::X86() {
  CopyRel = R_386_COPY;
  GotRel = R_386_GLOB_DAT;
  NoneRel = R_386_NONE;
  PltRel = R_386_JUMP_SLOT;
  IRelativeRel = R_386_IRELATIVE;
  RelativeRel = R_386_RELATIVE;
  TlsGotRel = R_386_TLS_TPOFF;
  TlsModuleIndexRel = R_386_TLS_DTPMOD32;
  TlsOffsetRel = R_386_TLS_DTPOFF32;
  GotEntrySize = 4;
  GotPltEntrySize = 4;
  PltEntrySize = 16;
  PltHeaderSize = 16;
  TlsGdRelaxSkip = 2;
  TrapInstr = {0xcc, 0xcc, 0xcc, 0xcc}; // 0xcc = INT3

  // Align to the non-PAE large page size (known as a superpage or huge page).
  // FreeBSD automatically promotes large, superpage-aligned allocations.
  DefaultImageBase = 0x400000;
}

static bool hasBaseReg(uint8_t ModRM) { return (ModRM & 0xc7) != 0x5; }

RelExpr X86::getRelExpr(RelType Type, const Symbol &S,
                        const uint8_t *Loc) const {
  // There are 4 different TLS variable models with varying degrees of
  // flexibility and performance. LocalExec and InitialExec models are fast but
  // less-flexible models. If they are in use, we set DF_STATIC_TLS flag in the
  // dynamic section to let runtime know about that.
  if (Type == R_386_TLS_LE || Type == R_386_TLS_LE_32 || Type == R_386_TLS_IE ||
      Type == R_386_TLS_GOTIE)
    Config->HasStaticTlsModel = true;

  switch (Type) {
  case R_386_8:
  case R_386_16:
  case R_386_32:
  case R_386_TLS_LDO_32:
    return R_ABS;
  case R_386_TLS_GD:
    return R_TLSGD_GOT_FROM_END;
  case R_386_TLS_LDM:
    return R_TLSLD_GOT_FROM_END;
  case R_386_PLT32:
    return R_PLT_PC;
  case R_386_PC8:
  case R_386_PC16:
  case R_386_PC32:
    return R_PC;
  case R_386_GOTPC:
    return R_GOTONLY_PC_FROM_END;
  case R_386_TLS_IE:
    return R_GOT;
  case R_386_GOT32:
  case R_386_GOT32X:
    // These relocations are arguably mis-designed because their calculations
    // depend on the instructions they are applied to. This is bad because we
    // usually don't care about whether the target section contains valid
    // machine instructions or not. But this is part of the documented ABI, so
    // we had to implement as the standard requires.
    //
    // x86 does not support PC-relative data access. Therefore, in order to
    // access GOT contents, a GOT address needs to be known at link-time
    // (which means non-PIC) or compilers have to emit code to get a GOT
    // address at runtime (which means code is position-independent but
    // compilers need to emit extra code for each GOT access.) This decision
    // is made at compile-time. In the latter case, compilers emit code to
    // load an GOT address to a register, which is usually %ebx.
    //
    // So, there are two ways to refer to symbol foo's GOT entry: foo@GOT or
    // foo@GOT(%reg).
    //
    // foo@GOT is not usable in PIC. If we are creating a PIC output and if we
    // find such relocation, we should report an error. foo@GOT is resolved to
    // an *absolute* address of foo's GOT entry, because both GOT address and
    // foo's offset are known. In other words, it's G + A.
    //
    // foo@GOT(%reg) needs to be resolved to a *relative* offset from a GOT to
    // foo's GOT entry in the table, because GOT address is not known but foo's
    // offset in the table is known. It's G + A - GOT.
    //
    // It's unfortunate that compilers emit the same relocation for these
    // different use cases. In order to distinguish them, we have to read a
    // machine instruction.
    //
    // The following code implements it. We assume that Loc[0] is the first
    // byte of a displacement or an immediate field of a valid machine
    // instruction. That means a ModRM byte is at Loc[-1]. By taking a look at
    // the byte, we can determine whether the instruction is register-relative
    // (i.e. it was generated for foo@GOT(%reg)) or absolute (i.e. foo@GOT).
    return hasBaseReg(Loc[-1]) ? R_GOT_FROM_END : R_GOT;
  case R_386_TLS_GOTIE:
    return R_GOT_FROM_END;
  case R_386_GOTOFF:
    return R_GOTREL_FROM_END;
  case R_386_TLS_LE:
    return R_TLS;
  case R_386_TLS_LE_32:
    return R_NEG_TLS;
  case R_386_NONE:
    return R_NONE;
  default:
    return R_INVALID;
  }
}

RelExpr X86::adjustRelaxExpr(RelType Type, const uint8_t *Data,
                             RelExpr Expr) const {
  switch (Expr) {
  default:
    return Expr;
  case R_RELAX_TLS_GD_TO_IE:
    return R_RELAX_TLS_GD_TO_IE_END;
  case R_RELAX_TLS_GD_TO_LE:
    return R_RELAX_TLS_GD_TO_LE_NEG;
  }
}

void X86::writeGotPltHeader(uint8_t *Buf) const {
  write32le(Buf, In.Dynamic->getVA());
}

void X86::writeGotPlt(uint8_t *Buf, const Symbol &S) const {
  // Entries in .got.plt initially points back to the corresponding
  // PLT entries with a fixed offset to skip the first instruction.
  write32le(Buf, S.getPltVA() + 6);
}

void X86::writeIgotPlt(uint8_t *Buf, const Symbol &S) const {
  // An x86 entry is the address of the ifunc resolver function.
  write32le(Buf, S.getVA());
}

RelType X86::getDynRel(RelType Type) const {
  if (Type == R_386_TLS_LE)
    return R_386_TLS_TPOFF;
  if (Type == R_386_TLS_LE_32)
    return R_386_TLS_TPOFF32;
  return Type;
}

void X86::writePltHeader(uint8_t *Buf) const {
  if (Config->Pic) {
    const uint8_t V[] = {
        0xff, 0xb3, 0x04, 0x00, 0x00, 0x00, // pushl GOTPLT+4(%ebx)
        0xff, 0xa3, 0x08, 0x00, 0x00, 0x00, // jmp *GOTPLT+8(%ebx)
        0x90, 0x90, 0x90, 0x90              // nop
    };
    memcpy(Buf, V, sizeof(V));

    uint32_t Ebx = In.Got->getVA() + In.Got->getSize();
    uint32_t GotPlt = In.GotPlt->getVA() - Ebx;
    write32le(Buf + 2, GotPlt + 4);
    write32le(Buf + 8, GotPlt + 8);
    return;
  }

  const uint8_t PltData[] = {
      0xff, 0x35, 0, 0, 0, 0, // pushl (GOTPLT+4)
      0xff, 0x25, 0, 0, 0, 0, // jmp *(GOTPLT+8)
      0x90, 0x90, 0x90, 0x90, // nop
  };
  memcpy(Buf, PltData, sizeof(PltData));
  uint32_t GotPlt = In.GotPlt->getVA();
  write32le(Buf + 2, GotPlt + 4);
  write32le(Buf + 8, GotPlt + 8);
}

void X86::writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr,
                   uint64_t PltEntryAddr, int32_t Index,
                   unsigned RelOff) const {
  const uint8_t Inst[] = {
      0xff, 0x00, 0, 0, 0, 0, // jmp *foo_in_GOT or jmp *foo@GOT(%ebx)
      0x68, 0, 0, 0, 0,       // pushl $reloc_offset
      0xe9, 0, 0, 0, 0,       // jmp .PLT0@PC
  };
  memcpy(Buf, Inst, sizeof(Inst));

  if (Config->Pic) {
    // jmp *foo@GOT(%ebx)
    uint32_t Ebx = In.Got->getVA() + In.Got->getSize();
    Buf[1] = 0xa3;
    write32le(Buf + 2, GotPltEntryAddr - Ebx);
  } else {
    // jmp *foo_in_GOT
    Buf[1] = 0x25;
    write32le(Buf + 2, GotPltEntryAddr);
  }

  write32le(Buf + 7, RelOff);
  write32le(Buf + 12, -getPltEntryOffset(Index) - 16);
}

int64_t X86::getImplicitAddend(const uint8_t *Buf, RelType Type) const {
  switch (Type) {
  case R_386_8:
  case R_386_PC8:
    return SignExtend64<8>(*Buf);
  case R_386_16:
  case R_386_PC16:
    return SignExtend64<16>(read16le(Buf));
  case R_386_32:
  case R_386_GOT32:
  case R_386_GOT32X:
  case R_386_GOTOFF:
  case R_386_GOTPC:
  case R_386_PC32:
  case R_386_PLT32:
  case R_386_TLS_LDO_32:
  case R_386_TLS_LE:
    return SignExtend64<32>(read32le(Buf));
  default:
    return 0;
  }
}

void X86::relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const {
  switch (Type) {
  case R_386_8:
    // R_386_{PC,}{8,16} are not part of the i386 psABI, but they are
    // being used for some 16-bit programs such as boot loaders, so
    // we want to support them.
    checkIntUInt(Loc, Val, 8, Type);
    *Loc = Val;
    break;
  case R_386_PC8:
    checkInt(Loc, Val, 8, Type);
    *Loc = Val;
    break;
  case R_386_16:
    checkIntUInt(Loc, Val, 16, Type);
    write16le(Loc, Val);
    break;
  case R_386_PC16:
    // R_386_PC16 is normally used with 16 bit code. In that situation
    // the PC is 16 bits, just like the addend. This means that it can
    // point from any 16 bit address to any other if the possibility
    // of wrapping is included.
    // The only restriction we have to check then is that the destination
    // address fits in 16 bits. That is impossible to do here. The problem is
    // that we are passed the final value, which already had the
    // current location subtracted from it.
    // We just check that Val fits in 17 bits. This misses some cases, but
    // should have no false positives.
    checkInt(Loc, Val, 17, Type);
    write16le(Loc, Val);
    break;
  case R_386_32:
  case R_386_GLOB_DAT:
  case R_386_GOT32:
  case R_386_GOT32X:
  case R_386_GOTOFF:
  case R_386_GOTPC:
  case R_386_PC32:
  case R_386_PLT32:
  case R_386_RELATIVE:
  case R_386_TLS_DTPMOD32:
  case R_386_TLS_DTPOFF32:
  case R_386_TLS_GD:
  case R_386_TLS_GOTIE:
  case R_386_TLS_IE:
  case R_386_TLS_LDM:
  case R_386_TLS_LDO_32:
  case R_386_TLS_LE:
  case R_386_TLS_LE_32:
  case R_386_TLS_TPOFF:
  case R_386_TLS_TPOFF32:
    checkInt(Loc, Val, 32, Type);
    write32le(Loc, Val);
    break;
  default:
    error(getErrorLocation(Loc) + "unrecognized reloc " + Twine(Type));
  }
}

void X86::relaxTlsGdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const {
  // Convert
  //   leal x@tlsgd(, %ebx, 1),
  //   call __tls_get_addr@plt
  // to
  //   movl %gs:0,%eax
  //   subl $x@ntpoff,%eax
  const uint8_t Inst[] = {
      0x65, 0xa1, 0x00, 0x00, 0x00, 0x00, // movl %gs:0, %eax
      0x81, 0xe8, 0, 0, 0, 0,             // subl Val(%ebx), %eax
  };
  memcpy(Loc - 3, Inst, sizeof(Inst));
  write32le(Loc + 5, Val);
}

void X86::relaxTlsGdToIe(uint8_t *Loc, RelType Type, uint64_t Val) const {
  // Convert
  //   leal x@tlsgd(, %ebx, 1),
  //   call __tls_get_addr@plt
  // to
  //   movl %gs:0, %eax
  //   addl x@gotntpoff(%ebx), %eax
  const uint8_t Inst[] = {
      0x65, 0xa1, 0x00, 0x00, 0x00, 0x00, // movl %gs:0, %eax
      0x03, 0x83, 0, 0, 0, 0,             // addl Val(%ebx), %eax
  };
  memcpy(Loc - 3, Inst, sizeof(Inst));
  write32le(Loc + 5, Val);
}

// In some conditions, relocations can be optimized to avoid using GOT.
// This function does that for Initial Exec to Local Exec case.
void X86::relaxTlsIeToLe(uint8_t *Loc, RelType Type, uint64_t Val) const {
  // Ulrich's document section 6.2 says that @gotntpoff can
  // be used with MOVL or ADDL instructions.
  // @indntpoff is similar to @gotntpoff, but for use in
  // position dependent code.
  uint8_t Reg = (Loc[-1] >> 3) & 7;

  if (Type == R_386_TLS_IE) {
    if (Loc[-1] == 0xa1) {
      // "movl foo@indntpoff,%eax" -> "movl $foo,%eax"
      // This case is different from the generic case below because
      // this is a 5 byte instruction while below is 6 bytes.
      Loc[-1] = 0xb8;
    } else if (Loc[-2] == 0x8b) {
      // "movl foo@indntpoff,%reg" -> "movl $foo,%reg"
      Loc[-2] = 0xc7;
      Loc[-1] = 0xc0 | Reg;
    } else {
      // "addl foo@indntpoff,%reg" -> "addl $foo,%reg"
      Loc[-2] = 0x81;
      Loc[-1] = 0xc0 | Reg;
    }
  } else {
    assert(Type == R_386_TLS_GOTIE);
    if (Loc[-2] == 0x8b) {
      // "movl foo@gottpoff(%rip),%reg" -> "movl $foo,%reg"
      Loc[-2] = 0xc7;
      Loc[-1] = 0xc0 | Reg;
    } else {
      // "addl foo@gotntpoff(%rip),%reg" -> "leal foo(%reg),%reg"
      Loc[-2] = 0x8d;
      Loc[-1] = 0x80 | (Reg << 3) | Reg;
    }
  }
  write32le(Loc, Val);
}

void X86::relaxTlsLdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const {
  if (Type == R_386_TLS_LDO_32) {
    write32le(Loc, Val);
    return;
  }

  // Convert
  //   leal foo(%reg),%eax
  //   call ___tls_get_addr
  // to
  //   movl %gs:0,%eax
  //   nop
  //   leal 0(%esi,1),%esi
  const uint8_t Inst[] = {
      0x65, 0xa1, 0x00, 0x00, 0x00, 0x00, // movl %gs:0,%eax
      0x90,                               // nop
      0x8d, 0x74, 0x26, 0x00,             // leal 0(%esi,1),%esi
  };
  memcpy(Loc - 2, Inst, sizeof(Inst));
}

namespace {
class RetpolinePic : public X86 {
public:
  RetpolinePic();
  void writeGotPlt(uint8_t *Buf, const Symbol &S) const override;
  void writePltHeader(uint8_t *Buf) const override;
  void writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
};

class RetpolineNoPic : public X86 {
public:
  RetpolineNoPic();
  void writeGotPlt(uint8_t *Buf, const Symbol &S) const override;
  void writePltHeader(uint8_t *Buf) const override;
  void writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
};
} // namespace

RetpolinePic::RetpolinePic() {
  PltHeaderSize = 48;
  PltEntrySize = 32;
}

void RetpolinePic::writeGotPlt(uint8_t *Buf, const Symbol &S) const {
  write32le(Buf, S.getPltVA() + 17);
}

void RetpolinePic::writePltHeader(uint8_t *Buf) const {
  const uint8_t Insn[] = {
      0xff, 0xb3, 0,    0,    0,    0,          // 0:    pushl GOTPLT+4(%ebx)
      0x50,                                     // 6:    pushl %eax
      0x8b, 0x83, 0,    0,    0,    0,          // 7:    mov GOTPLT+8(%ebx), %eax
      0xe8, 0x0e, 0x00, 0x00, 0x00,             // d:    call next
      0xf3, 0x90,                               // 12: loop: pause
      0x0f, 0xae, 0xe8,                         // 14:   lfence
      0xeb, 0xf9,                               // 17:   jmp loop
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 19:   int3; .align 16
      0x89, 0x0c, 0x24,                         // 20: next: mov %ecx, (%esp)
      0x8b, 0x4c, 0x24, 0x04,                   // 23:   mov 0x4(%esp), %ecx
      0x89, 0x44, 0x24, 0x04,                   // 27:   mov %eax ,0x4(%esp)
      0x89, 0xc8,                               // 2b:   mov %ecx, %eax
      0x59,                                     // 2d:   pop %ecx
      0xc3,                                     // 2e:   ret
      0xcc,                                     // 2f:   int3; padding
  };
  memcpy(Buf, Insn, sizeof(Insn));

  uint32_t Ebx = In.Got->getVA() + In.Got->getSize();
  uint32_t GotPlt = In.GotPlt->getVA() - Ebx;
  write32le(Buf + 2, GotPlt + 4);
  write32le(Buf + 9, GotPlt + 8);
}

void RetpolinePic::writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr,
                            uint64_t PltEntryAddr, int32_t Index,
                            unsigned RelOff) const {
  const uint8_t Insn[] = {
      0x50,                            // pushl %eax
      0x8b, 0x83, 0,    0,    0,    0, // mov foo@GOT(%ebx), %eax
      0xe8, 0,    0,    0,    0,       // call plt+0x20
      0xe9, 0,    0,    0,    0,       // jmp plt+0x12
      0x68, 0,    0,    0,    0,       // pushl $reloc_offset
      0xe9, 0,    0,    0,    0,       // jmp plt+0
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc,    // int3; padding
  };
  memcpy(Buf, Insn, sizeof(Insn));

  uint32_t Ebx = In.Got->getVA() + In.Got->getSize();
  unsigned Off = getPltEntryOffset(Index);
  write32le(Buf + 3, GotPltEntryAddr - Ebx);
  write32le(Buf + 8, -Off - 12 + 32);
  write32le(Buf + 13, -Off - 17 + 18);
  write32le(Buf + 18, RelOff);
  write32le(Buf + 23, -Off - 27);
}

RetpolineNoPic::RetpolineNoPic() {
  PltHeaderSize = 48;
  PltEntrySize = 32;
}

void RetpolineNoPic::writeGotPlt(uint8_t *Buf, const Symbol &S) const {
  write32le(Buf, S.getPltVA() + 16);
}

void RetpolineNoPic::writePltHeader(uint8_t *Buf) const {
  const uint8_t Insn[] = {
      0xff, 0x35, 0,    0,    0,    0, // 0:    pushl GOTPLT+4
      0x50,                            // 6:    pushl %eax
      0xa1, 0,    0,    0,    0,       // 7:    mov GOTPLT+8, %eax
      0xe8, 0x0f, 0x00, 0x00, 0x00,    // c:    call next
      0xf3, 0x90,                      // 11: loop: pause
      0x0f, 0xae, 0xe8,                // 13:   lfence
      0xeb, 0xf9,                      // 16:   jmp loop
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc,    // 18:   int3
      0xcc, 0xcc, 0xcc,                // 1f:   int3; .align 16
      0x89, 0x0c, 0x24,                // 20: next: mov %ecx, (%esp)
      0x8b, 0x4c, 0x24, 0x04,          // 23:   mov 0x4(%esp), %ecx
      0x89, 0x44, 0x24, 0x04,          // 27:   mov %eax ,0x4(%esp)
      0x89, 0xc8,                      // 2b:   mov %ecx, %eax
      0x59,                            // 2d:   pop %ecx
      0xc3,                            // 2e:   ret
      0xcc,                            // 2f:   int3; padding
  };
  memcpy(Buf, Insn, sizeof(Insn));

  uint32_t GotPlt = In.GotPlt->getVA();
  write32le(Buf + 2, GotPlt + 4);
  write32le(Buf + 8, GotPlt + 8);
}

void RetpolineNoPic::writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr,
                              uint64_t PltEntryAddr, int32_t Index,
                              unsigned RelOff) const {
  const uint8_t Insn[] = {
      0x50,                         // 0:  pushl %eax
      0xa1, 0,    0,    0,    0,    // 1:  mov foo_in_GOT, %eax
      0xe8, 0,    0,    0,    0,    // 6:  call plt+0x20
      0xe9, 0,    0,    0,    0,    // b:  jmp plt+0x11
      0x68, 0,    0,    0,    0,    // 10: pushl $reloc_offset
      0xe9, 0,    0,    0,    0,    // 15: jmp plt+0
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 1a: int3; padding
      0xcc,                         // 1f: int3; padding
  };
  memcpy(Buf, Insn, sizeof(Insn));

  unsigned Off = getPltEntryOffset(Index);
  write32le(Buf + 2, GotPltEntryAddr);
  write32le(Buf + 7, -Off - 11 + 32);
  write32le(Buf + 12, -Off - 16 + 17);
  write32le(Buf + 17, RelOff);
  write32le(Buf + 22, -Off - 26);
}

TargetInfo *elf::getX86TargetInfo() {
  if (Config->ZRetpolineplt) {
    if (Config->Pic) {
      static RetpolinePic T;
      return &T;
    }
    static RetpolineNoPic T;
    return &T;
  }

  static X86 T;
  return &T;
}
