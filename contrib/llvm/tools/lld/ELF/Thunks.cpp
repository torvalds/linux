//===- Thunks.cpp --------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//
//
// This file contains Thunk subclasses.
//
// A thunk is a small piece of code written after an input section
// which is used to jump between "incompatible" functions
// such as MIPS PIC and non-PIC or ARM non-Thumb and Thumb functions.
//
// If a jump target is too far and its address doesn't fit to a
// short jump instruction, we need to create a thunk too, but we
// haven't supported it yet.
//
// i386 and x86-64 don't need thunks.
//
//===---------------------------------------------------------------------===//

#include "Thunks.h"
#include "Config.h"
#include "InputSection.h"
#include "OutputSections.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>
#include <cstring>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;

namespace lld {
namespace elf {

namespace {

// AArch64 long range Thunks
class AArch64ABSLongThunk final : public Thunk {
public:
  AArch64ABSLongThunk(Symbol &Dest) : Thunk(Dest) {}
  uint32_t size() override { return 16; }
  void writeTo(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
};

class AArch64ADRPThunk final : public Thunk {
public:
  AArch64ADRPThunk(Symbol &Dest) : Thunk(Dest) {}
  uint32_t size() override { return 12; }
  void writeTo(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
};

// Base class for ARM thunks.
//
// An ARM thunk may be either short or long. A short thunk is simply a branch
// (B) instruction, and it may be used to call ARM functions when the distance
// from the thunk to the target is less than 32MB. Long thunks can branch to any
// virtual address and can switch between ARM and Thumb, and they are
// implemented in the derived classes. This class tries to create a short thunk
// if the target is in range, otherwise it creates a long thunk.
class ARMThunk : public Thunk {
public:
  ARMThunk(Symbol &Dest) : Thunk(Dest) {}

  bool mayUseShortThunk();
  uint32_t size() override { return mayUseShortThunk() ? 4 : sizeLong(); }
  void writeTo(uint8_t *Buf) override;
  bool isCompatibleWith(RelType Type) const override;

  // Returns the size of a long thunk.
  virtual uint32_t sizeLong() = 0;

  // Writes a long thunk to Buf.
  virtual void writeLong(uint8_t *Buf) = 0;

private:
  // This field tracks whether all previously considered layouts would allow
  // this thunk to be short. If we have ever needed a long thunk, we always
  // create a long thunk, even if the thunk may be short given the current
  // distance to the target. We do this because transitioning from long to short
  // can create layout oscillations in certain corner cases which would prevent
  // the layout from converging.
  bool MayUseShortThunk = true;
};

// Base class for Thumb-2 thunks.
//
// This class is similar to ARMThunk, but it uses the Thumb-2 B.W instruction
// which has a range of 16MB.
class ThumbThunk : public Thunk {
public:
  ThumbThunk(Symbol &Dest) : Thunk(Dest) { Alignment = 2; }

  bool mayUseShortThunk();
  uint32_t size() override { return mayUseShortThunk() ? 4 : sizeLong(); }
  void writeTo(uint8_t *Buf) override;
  bool isCompatibleWith(RelType Type) const override;

  // Returns the size of a long thunk.
  virtual uint32_t sizeLong() = 0;

  // Writes a long thunk to Buf.
  virtual void writeLong(uint8_t *Buf) = 0;

private:
  // See comment in ARMThunk above.
  bool MayUseShortThunk = true;
};

// Specific ARM Thunk implementations. The naming convention is:
// Source State, TargetState, Target Requirement, ABS or PI, Range
class ARMV7ABSLongThunk final : public ARMThunk {
public:
  ARMV7ABSLongThunk(Symbol &Dest) : ARMThunk(Dest) {}

  uint32_t sizeLong() override { return 12; }
  void writeLong(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
};

class ARMV7PILongThunk final : public ARMThunk {
public:
  ARMV7PILongThunk(Symbol &Dest) : ARMThunk(Dest) {}

  uint32_t sizeLong() override { return 16; }
  void writeLong(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
};

class ThumbV7ABSLongThunk final : public ThumbThunk {
public:
  ThumbV7ABSLongThunk(Symbol &Dest) : ThumbThunk(Dest) {}

  uint32_t sizeLong() override { return 10; }
  void writeLong(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
};

class ThumbV7PILongThunk final : public ThumbThunk {
public:
  ThumbV7PILongThunk(Symbol &Dest) : ThumbThunk(Dest) {}

  uint32_t sizeLong() override { return 12; }
  void writeLong(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
};

// Implementations of Thunks for older Arm architectures that do not support
// the movt/movw instructions. These thunks require at least Architecture v5
// as used on processors such as the Arm926ej-s. There are no Thumb entry
// points as there is no Thumb branch instruction on these architecture that
// can result in a thunk
class ARMV5ABSLongThunk final : public ARMThunk {
public:
  ARMV5ABSLongThunk(Symbol &Dest) : ARMThunk(Dest) {}

  uint32_t sizeLong() override { return 8; }
  void writeLong(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
  bool isCompatibleWith(uint32_t RelocType) const override;
};

class ARMV5PILongThunk final : public ARMThunk {
public:
  ARMV5PILongThunk(Symbol &Dest) : ARMThunk(Dest) {}

  uint32_t sizeLong() override { return 16; }
  void writeLong(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
  bool isCompatibleWith(uint32_t RelocType) const override;
};

// Implementations of Thunks for Arm v6-M. Only Thumb instructions are permitted
class ThumbV6MABSLongThunk final : public ThumbThunk {
public:
  ThumbV6MABSLongThunk(Symbol &Dest) : ThumbThunk(Dest) {}

  uint32_t sizeLong() override { return 12; }
  void writeLong(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
};

class ThumbV6MPILongThunk final : public ThumbThunk {
public:
  ThumbV6MPILongThunk(Symbol &Dest) : ThumbThunk(Dest) {}

  uint32_t sizeLong() override { return 16; }
  void writeLong(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
};

// MIPS LA25 thunk
class MipsThunk final : public Thunk {
public:
  MipsThunk(Symbol &Dest) : Thunk(Dest) {}

  uint32_t size() override { return 16; }
  void writeTo(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
  InputSection *getTargetInputSection() const override;
};

// microMIPS R2-R5 LA25 thunk
class MicroMipsThunk final : public Thunk {
public:
  MicroMipsThunk(Symbol &Dest) : Thunk(Dest) {}

  uint32_t size() override { return 14; }
  void writeTo(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
  InputSection *getTargetInputSection() const override;
};

// microMIPS R6 LA25 thunk
class MicroMipsR6Thunk final : public Thunk {
public:
  MicroMipsR6Thunk(Symbol &Dest) : Thunk(Dest) {}

  uint32_t size() override { return 12; }
  void writeTo(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
  InputSection *getTargetInputSection() const override;
};


// PPC64 Plt call stubs.
// Any call site that needs to call through a plt entry needs a call stub in
// the .text section. The call stub is responsible for:
// 1) Saving the toc-pointer to the stack.
// 2) Loading the target functions address from the procedure linkage table into
//    r12 for use by the target functions global entry point, and into the count
//    register.
// 3) Transfering control to the target function through an indirect branch.
class PPC64PltCallStub final : public Thunk {
public:
  PPC64PltCallStub(Symbol &Dest) : Thunk(Dest) {}
  uint32_t size() override { return 20; }
  void writeTo(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;
};

// A bl instruction uses a signed 24 bit offset, with an implicit 4 byte
// alignment. This gives a possible 26 bits of 'reach'. If the call offset is
// larger then that we need to emit a long-branch thunk. The target address
// of the callee is stored in a table to be accessed TOC-relative. Since the
// call must be local (a non-local call will have a PltCallStub instead) the
// table stores the address of the callee's local entry point. For
// position-independent code a corresponding relative dynamic relocation is
// used.
class PPC64LongBranchThunk : public Thunk {
public:
  uint32_t size() override { return 16; }
  void writeTo(uint8_t *Buf) override;
  void addSymbols(ThunkSection &IS) override;

protected:
  PPC64LongBranchThunk(Symbol &Dest) : Thunk(Dest) {}
};

class PPC64PILongBranchThunk final : public PPC64LongBranchThunk {
public:
  PPC64PILongBranchThunk(Symbol &Dest) : PPC64LongBranchThunk(Dest) {
    assert(!Dest.IsPreemptible);
    if (Dest.isInPPC64Branchlt())
      return;

    In.PPC64LongBranchTarget->addEntry(Dest);
    In.RelaDyn->addReloc({Target->RelativeRel, In.PPC64LongBranchTarget,
                          Dest.getPPC64LongBranchOffset(), true, &Dest,
                          getPPC64GlobalEntryToLocalEntryOffset(Dest.StOther)});
  }
};

class PPC64PDLongBranchThunk final : public PPC64LongBranchThunk {
public:
  PPC64PDLongBranchThunk(Symbol &Dest) : PPC64LongBranchThunk(Dest) {
    if (!Dest.isInPPC64Branchlt())
      In.PPC64LongBranchTarget->addEntry(Dest);
  }
};

} // end anonymous namespace

Defined *Thunk::addSymbol(StringRef Name, uint8_t Type, uint64_t Value,
                          InputSectionBase &Section) {
  Defined *D = addSyntheticLocal(Name, Type, Value, /*Size=*/0, Section);
  Syms.push_back(D);
  return D;
}

void Thunk::setOffset(uint64_t NewOffset) {
  for (Defined *D : Syms)
    D->Value = D->Value - Offset + NewOffset;
  Offset = NewOffset;
}

// AArch64 long range Thunks

static uint64_t getAArch64ThunkDestVA(const Symbol &S) {
  uint64_t V = S.isInPlt() ? S.getPltVA() : S.getVA();
  return V;
}

void AArch64ABSLongThunk::writeTo(uint8_t *Buf) {
  const uint8_t Data[] = {
    0x50, 0x00, 0x00, 0x58, //     ldr x16, L0
    0x00, 0x02, 0x1f, 0xd6, //     br  x16
    0x00, 0x00, 0x00, 0x00, // L0: .xword S
    0x00, 0x00, 0x00, 0x00,
  };
  uint64_t S = getAArch64ThunkDestVA(Destination);
  memcpy(Buf, Data, sizeof(Data));
  Target->relocateOne(Buf + 8, R_AARCH64_ABS64, S);
}

void AArch64ABSLongThunk::addSymbols(ThunkSection &IS) {
  addSymbol(Saver.save("__AArch64AbsLongThunk_" + Destination.getName()),
            STT_FUNC, 0, IS);
  addSymbol("$x", STT_NOTYPE, 0, IS);
  addSymbol("$d", STT_NOTYPE, 8, IS);
}

// This Thunk has a maximum range of 4Gb, this is sufficient for all programs
// using the small code model, including pc-relative ones. At time of writing
// clang and gcc do not support the large code model for position independent
// code so it is safe to use this for position independent thunks without
// worrying about the destination being more than 4Gb away.
void AArch64ADRPThunk::writeTo(uint8_t *Buf) {
  const uint8_t Data[] = {
      0x10, 0x00, 0x00, 0x90, // adrp x16, Dest R_AARCH64_ADR_PREL_PG_HI21(Dest)
      0x10, 0x02, 0x00, 0x91, // add  x16, x16, R_AARCH64_ADD_ABS_LO12_NC(Dest)
      0x00, 0x02, 0x1f, 0xd6, // br   x16
  };
  uint64_t S = getAArch64ThunkDestVA(Destination);
  uint64_t P = getThunkTargetSym()->getVA();
  memcpy(Buf, Data, sizeof(Data));
  Target->relocateOne(Buf, R_AARCH64_ADR_PREL_PG_HI21,
                      getAArch64Page(S) - getAArch64Page(P));
  Target->relocateOne(Buf + 4, R_AARCH64_ADD_ABS_LO12_NC, S);
}

void AArch64ADRPThunk::addSymbols(ThunkSection &IS) {
  addSymbol(Saver.save("__AArch64ADRPThunk_" + Destination.getName()), STT_FUNC,
            0, IS);
  addSymbol("$x", STT_NOTYPE, 0, IS);
}

// ARM Target Thunks
static uint64_t getARMThunkDestVA(const Symbol &S) {
  uint64_t V = S.isInPlt() ? S.getPltVA() : S.getVA();
  return SignExtend64<32>(V);
}

// This function returns true if the target is not Thumb and is within 2^26, and
// it has not previously returned false (see comment for MayUseShortThunk).
bool ARMThunk::mayUseShortThunk() {
  if (!MayUseShortThunk)
    return false;
  uint64_t S = getARMThunkDestVA(Destination);
  if (S & 1) {
    MayUseShortThunk = false;
    return false;
  }
  uint64_t P = getThunkTargetSym()->getVA();
  int64_t Offset = S - P - 8;
  MayUseShortThunk = llvm::isInt<26>(Offset);
  return MayUseShortThunk;
}

void ARMThunk::writeTo(uint8_t *Buf) {
  if (!mayUseShortThunk()) {
    writeLong(Buf);
    return;
  }

  uint64_t S = getARMThunkDestVA(Destination);
  uint64_t P = getThunkTargetSym()->getVA();
  int64_t Offset = S - P - 8;
  const uint8_t Data[] = {
    0x00, 0x00, 0x00, 0xea, // b S
  };
  memcpy(Buf, Data, sizeof(Data));
  Target->relocateOne(Buf, R_ARM_JUMP24, Offset);
}

bool ARMThunk::isCompatibleWith(RelType Type) const {
  // Thumb branch relocations can't use BLX
  return Type != R_ARM_THM_JUMP19 && Type != R_ARM_THM_JUMP24;
}

// This function returns true if the target is Thumb and is within 2^25, and
// it has not previously returned false (see comment for MayUseShortThunk).
bool ThumbThunk::mayUseShortThunk() {
  if (!MayUseShortThunk)
    return false;
  uint64_t S = getARMThunkDestVA(Destination);
  if ((S & 1) == 0) {
    MayUseShortThunk = false;
    return false;
  }
  uint64_t P = getThunkTargetSym()->getVA() & ~1;
  int64_t Offset = S - P - 4;
  MayUseShortThunk = llvm::isInt<25>(Offset);
  return MayUseShortThunk;
}

void ThumbThunk::writeTo(uint8_t *Buf) {
  if (!mayUseShortThunk()) {
    writeLong(Buf);
    return;
  }

  uint64_t S = getARMThunkDestVA(Destination);
  uint64_t P = getThunkTargetSym()->getVA();
  int64_t Offset = S - P - 4;
  const uint8_t Data[] = {
      0x00, 0xf0, 0x00, 0xb0, // b.w S
  };
  memcpy(Buf, Data, sizeof(Data));
  Target->relocateOne(Buf, R_ARM_THM_JUMP24, Offset);
}

bool ThumbThunk::isCompatibleWith(RelType Type) const {
  // ARM branch relocations can't use BLX
  return Type != R_ARM_JUMP24 && Type != R_ARM_PC24 && Type != R_ARM_PLT32;
}

void ARMV7ABSLongThunk::writeLong(uint8_t *Buf) {
  const uint8_t Data[] = {
      0x00, 0xc0, 0x00, 0xe3, // movw         ip,:lower16:S
      0x00, 0xc0, 0x40, 0xe3, // movt         ip,:upper16:S
      0x1c, 0xff, 0x2f, 0xe1, // bx   ip
  };
  uint64_t S = getARMThunkDestVA(Destination);
  memcpy(Buf, Data, sizeof(Data));
  Target->relocateOne(Buf, R_ARM_MOVW_ABS_NC, S);
  Target->relocateOne(Buf + 4, R_ARM_MOVT_ABS, S);
}

void ARMV7ABSLongThunk::addSymbols(ThunkSection &IS) {
  addSymbol(Saver.save("__ARMv7ABSLongThunk_" + Destination.getName()),
            STT_FUNC, 0, IS);
  addSymbol("$a", STT_NOTYPE, 0, IS);
}

void ThumbV7ABSLongThunk::writeLong(uint8_t *Buf) {
  const uint8_t Data[] = {
      0x40, 0xf2, 0x00, 0x0c, // movw         ip, :lower16:S
      0xc0, 0xf2, 0x00, 0x0c, // movt         ip, :upper16:S
      0x60, 0x47,             // bx   ip
  };
  uint64_t S = getARMThunkDestVA(Destination);
  memcpy(Buf, Data, sizeof(Data));
  Target->relocateOne(Buf, R_ARM_THM_MOVW_ABS_NC, S);
  Target->relocateOne(Buf + 4, R_ARM_THM_MOVT_ABS, S);
}

void ThumbV7ABSLongThunk::addSymbols(ThunkSection &IS) {
  addSymbol(Saver.save("__Thumbv7ABSLongThunk_" + Destination.getName()),
            STT_FUNC, 1, IS);
  addSymbol("$t", STT_NOTYPE, 0, IS);
}

void ARMV7PILongThunk::writeLong(uint8_t *Buf) {
  const uint8_t Data[] = {
      0xf0, 0xcf, 0x0f, 0xe3, // P:  movw ip,:lower16:S - (P + (L1-P) + 8)
      0x00, 0xc0, 0x40, 0xe3, //     movt ip,:upper16:S - (P + (L1-P) + 8)
      0x0f, 0xc0, 0x8c, 0xe0, // L1: add  ip, ip, pc
      0x1c, 0xff, 0x2f, 0xe1, //     bx   ip
  };
  uint64_t S = getARMThunkDestVA(Destination);
  uint64_t P = getThunkTargetSym()->getVA();
  int64_t Offset = S - P - 16;
  memcpy(Buf, Data, sizeof(Data));
  Target->relocateOne(Buf, R_ARM_MOVW_PREL_NC, Offset);
  Target->relocateOne(Buf + 4, R_ARM_MOVT_PREL, Offset);
}

void ARMV7PILongThunk::addSymbols(ThunkSection &IS) {
  addSymbol(Saver.save("__ARMV7PILongThunk_" + Destination.getName()), STT_FUNC,
            0, IS);
  addSymbol("$a", STT_NOTYPE, 0, IS);
}

void ThumbV7PILongThunk::writeLong(uint8_t *Buf) {
  const uint8_t Data[] = {
      0x4f, 0xf6, 0xf4, 0x7c, // P:  movw ip,:lower16:S - (P + (L1-P) + 4)
      0xc0, 0xf2, 0x00, 0x0c, //     movt ip,:upper16:S - (P + (L1-P) + 4)
      0xfc, 0x44,             // L1: add  ip, pc
      0x60, 0x47,             //     bx   ip
  };
  uint64_t S = getARMThunkDestVA(Destination);
  uint64_t P = getThunkTargetSym()->getVA() & ~0x1;
  int64_t Offset = S - P - 12;
  memcpy(Buf, Data, sizeof(Data));
  Target->relocateOne(Buf, R_ARM_THM_MOVW_PREL_NC, Offset);
  Target->relocateOne(Buf + 4, R_ARM_THM_MOVT_PREL, Offset);
}

void ThumbV7PILongThunk::addSymbols(ThunkSection &IS) {
  addSymbol(Saver.save("__ThumbV7PILongThunk_" + Destination.getName()),
            STT_FUNC, 1, IS);
  addSymbol("$t", STT_NOTYPE, 0, IS);
}

void ARMV5ABSLongThunk::writeLong(uint8_t *Buf) {
  const uint8_t Data[] = {
      0x04, 0xf0, 0x1f, 0xe5, //     ldr pc, [pc,#-4] ; L1
      0x00, 0x00, 0x00, 0x00, // L1: .word S
  };
  memcpy(Buf, Data, sizeof(Data));
  Target->relocateOne(Buf + 4, R_ARM_ABS32, getARMThunkDestVA(Destination));
}

void ARMV5ABSLongThunk::addSymbols(ThunkSection &IS) {
  addSymbol(Saver.save("__ARMv5ABSLongThunk_" + Destination.getName()),
            STT_FUNC, 0, IS);
  addSymbol("$a", STT_NOTYPE, 0, IS);
  addSymbol("$d", STT_NOTYPE, 4, IS);
}

bool ARMV5ABSLongThunk::isCompatibleWith(uint32_t RelocType) const {
  // Thumb branch relocations can't use BLX
  return RelocType != R_ARM_THM_JUMP19 && RelocType != R_ARM_THM_JUMP24;
}

void ARMV5PILongThunk::writeLong(uint8_t *Buf) {
  const uint8_t Data[] = {
      0x04, 0xc0, 0x9f, 0xe5, // P:  ldr ip, [pc,#4] ; L2
      0x0c, 0xc0, 0x8f, 0xe0, // L1: add ip, pc, ip
      0x1c, 0xff, 0x2f, 0xe1, //     bx ip
      0x00, 0x00, 0x00, 0x00, // L2: .word S - (P + (L1 - P) + 8)
  };
  uint64_t S = getARMThunkDestVA(Destination);
  uint64_t P = getThunkTargetSym()->getVA() & ~0x1;
  memcpy(Buf, Data, sizeof(Data));
  Target->relocateOne(Buf + 12, R_ARM_REL32, S - P - 12);
}

void ARMV5PILongThunk::addSymbols(ThunkSection &IS) {
  addSymbol(Saver.save("__ARMV5PILongThunk_" + Destination.getName()), STT_FUNC,
            0, IS);
  addSymbol("$a", STT_NOTYPE, 0, IS);
  addSymbol("$d", STT_NOTYPE, 12, IS);
}

bool ARMV5PILongThunk::isCompatibleWith(uint32_t RelocType) const {
  // Thumb branch relocations can't use BLX
  return RelocType != R_ARM_THM_JUMP19 && RelocType != R_ARM_THM_JUMP24;
}

void ThumbV6MABSLongThunk::writeLong(uint8_t *Buf) {
  // Most Thumb instructions cannot access the high registers r8 - r15. As the
  // only register we can corrupt is r12 we must instead spill a low register
  // to the stack to use as a scratch register. We push r1 even though we
  // don't need to get some space to use for the return address.
  const uint8_t Data[] = {
      0x03, 0xb4,            // push {r0, r1} ; Obtain scratch registers
      0x01, 0x48,            // ldr r0, [pc, #4] ; L1
      0x01, 0x90,            // str r0, [sp, #4] ; SP + 4 = S
      0x01, 0xbd,            // pop {r0, pc} ; restore r0 and branch to dest
      0x00, 0x00, 0x00, 0x00 // L1: .word S
  };
  uint64_t S = getARMThunkDestVA(Destination);
  memcpy(Buf, Data, sizeof(Data));
  Target->relocateOne(Buf + 8, R_ARM_ABS32, S);
}

void ThumbV6MABSLongThunk::addSymbols(ThunkSection &IS) {
  addSymbol(Saver.save("__Thumbv6MABSLongThunk_" + Destination.getName()),
            STT_FUNC, 1, IS);
  addSymbol("$t", STT_NOTYPE, 0, IS);
  addSymbol("$d", STT_NOTYPE, 8, IS);
}

void ThumbV6MPILongThunk::writeLong(uint8_t *Buf) {
  // Most Thumb instructions cannot access the high registers r8 - r15. As the
  // only register we can corrupt is ip (r12) we must instead spill a low
  // register to the stack to use as a scratch register.
  const uint8_t Data[] = {
      0x01, 0xb4,             // P:  push {r0}        ; Obtain scratch register
      0x02, 0x48,             //     ldr r0, [pc, #8] ; L2
      0x84, 0x46,             //     mov ip, r0       ; high to low register
      0x01, 0xbc,             //     pop {r0}         ; restore scratch register
      0xe7, 0x44,             // L1: add pc, ip       ; transfer control
      0xc0, 0x46,             //     nop              ; pad to 4-byte boundary
      0x00, 0x00, 0x00, 0x00, // L2: .word S - (P + (L1 - P) + 4)
  };
  uint64_t S = getARMThunkDestVA(Destination);
  uint64_t P = getThunkTargetSym()->getVA() & ~0x1;
  memcpy(Buf, Data, sizeof(Data));
  Target->relocateOne(Buf + 12, R_ARM_REL32, S - P - 12);
}

void ThumbV6MPILongThunk::addSymbols(ThunkSection &IS) {
  addSymbol(Saver.save("__Thumbv6MPILongThunk_" + Destination.getName()),
            STT_FUNC, 1, IS);
  addSymbol("$t", STT_NOTYPE, 0, IS);
  addSymbol("$d", STT_NOTYPE, 12, IS);
}

// Write MIPS LA25 thunk code to call PIC function from the non-PIC one.
void MipsThunk::writeTo(uint8_t *Buf) {
  uint64_t S = Destination.getVA();
  write32(Buf, 0x3c190000); // lui   $25, %hi(func)
  write32(Buf + 4, 0x08000000 | (S >> 2)); // j     func
  write32(Buf + 8, 0x27390000); // addiu $25, $25, %lo(func)
  write32(Buf + 12, 0x00000000); // nop
  Target->relocateOne(Buf, R_MIPS_HI16, S);
  Target->relocateOne(Buf + 8, R_MIPS_LO16, S);
}

void MipsThunk::addSymbols(ThunkSection &IS) {
  addSymbol(Saver.save("__LA25Thunk_" + Destination.getName()), STT_FUNC, 0,
            IS);
}

InputSection *MipsThunk::getTargetInputSection() const {
  auto &DR = cast<Defined>(Destination);
  return dyn_cast<InputSection>(DR.Section);
}

// Write microMIPS R2-R5 LA25 thunk code
// to call PIC function from the non-PIC one.
void MicroMipsThunk::writeTo(uint8_t *Buf) {
  uint64_t S = Destination.getVA() | 1;
  write16(Buf, 0x41b9);       // lui   $25, %hi(func)
  write16(Buf + 4, 0xd400);   // j     func
  write16(Buf + 8, 0x3339);   // addiu $25, $25, %lo(func)
  write16(Buf + 12, 0x0c00);  // nop
  Target->relocateOne(Buf, R_MICROMIPS_HI16, S);
  Target->relocateOne(Buf + 4, R_MICROMIPS_26_S1, S);
  Target->relocateOne(Buf + 8, R_MICROMIPS_LO16, S);
}

void MicroMipsThunk::addSymbols(ThunkSection &IS) {
  Defined *D = addSymbol(
      Saver.save("__microLA25Thunk_" + Destination.getName()), STT_FUNC, 0, IS);
  D->StOther |= STO_MIPS_MICROMIPS;
}

InputSection *MicroMipsThunk::getTargetInputSection() const {
  auto &DR = cast<Defined>(Destination);
  return dyn_cast<InputSection>(DR.Section);
}

// Write microMIPS R6 LA25 thunk code
// to call PIC function from the non-PIC one.
void MicroMipsR6Thunk::writeTo(uint8_t *Buf) {
  uint64_t S = Destination.getVA() | 1;
  uint64_t P = getThunkTargetSym()->getVA();
  write16(Buf, 0x1320);       // lui   $25, %hi(func)
  write16(Buf + 4, 0x3339);   // addiu $25, $25, %lo(func)
  write16(Buf + 8, 0x9400);   // bc    func
  Target->relocateOne(Buf, R_MICROMIPS_HI16, S);
  Target->relocateOne(Buf + 4, R_MICROMIPS_LO16, S);
  Target->relocateOne(Buf + 8, R_MICROMIPS_PC26_S1, S - P - 12);
}

void MicroMipsR6Thunk::addSymbols(ThunkSection &IS) {
  Defined *D = addSymbol(
      Saver.save("__microLA25Thunk_" + Destination.getName()), STT_FUNC, 0, IS);
  D->StOther |= STO_MIPS_MICROMIPS;
}

InputSection *MicroMipsR6Thunk::getTargetInputSection() const {
  auto &DR = cast<Defined>(Destination);
  return dyn_cast<InputSection>(DR.Section);
}

static void writePPCLoadAndBranch(uint8_t *Buf, int64_t Offset) {
  uint16_t OffHa = (Offset + 0x8000) >> 16;
  uint16_t OffLo = Offset & 0xffff;

  write32(Buf + 0, 0x3d820000 | OffHa); // addis r12, r2, OffHa
  write32(Buf + 4, 0xe98c0000 | OffLo); // ld    r12, OffLo(r12)
  write32(Buf + 8, 0x7d8903a6);         // mtctr r12
  write32(Buf + 12, 0x4e800420);        // bctr
}

void PPC64PltCallStub::writeTo(uint8_t *Buf) {
  int64_t Offset = Destination.getGotPltVA() - getPPC64TocBase();
  // Save the TOC pointer to the save-slot reserved in the call frame.
  write32(Buf + 0, 0xf8410018); // std     r2,24(r1)
  writePPCLoadAndBranch(Buf + 4, Offset);
}

void PPC64PltCallStub::addSymbols(ThunkSection &IS) {
  Defined *S = addSymbol(Saver.save("__plt_" + Destination.getName()), STT_FUNC,
                         0, IS);
  S->NeedsTocRestore = true;
}

void PPC64LongBranchThunk::writeTo(uint8_t *Buf) {
  int64_t Offset = Destination.getPPC64LongBranchTableVA() - getPPC64TocBase();
  writePPCLoadAndBranch(Buf, Offset);
}

void PPC64LongBranchThunk::addSymbols(ThunkSection &IS) {
  addSymbol(Saver.save("__long_branch_" + Destination.getName()), STT_FUNC, 0,
            IS);
}

Thunk::Thunk(Symbol &D) : Destination(D), Offset(0) {}

Thunk::~Thunk() = default;

static Thunk *addThunkAArch64(RelType Type, Symbol &S) {
  if (Type != R_AARCH64_CALL26 && Type != R_AARCH64_JUMP26)
    fatal("unrecognized relocation type");
  if (Config->PicThunk)
    return make<AArch64ADRPThunk>(S);
  return make<AArch64ABSLongThunk>(S);
}

// Creates a thunk for Thumb-ARM interworking.
// Arm Architectures v5 and v6 do not support Thumb2 technology. This means
// - MOVT and MOVW instructions cannot be used
// - Only Thumb relocation that can generate a Thunk is a BL, this can always
//   be transformed into a BLX
static Thunk *addThunkPreArmv7(RelType Reloc, Symbol &S) {
  switch (Reloc) {
  case R_ARM_PC24:
  case R_ARM_PLT32:
  case R_ARM_JUMP24:
  case R_ARM_CALL:
  case R_ARM_THM_CALL:
    if (Config->PicThunk)
      return make<ARMV5PILongThunk>(S);
    return make<ARMV5ABSLongThunk>(S);
  }
  fatal("relocation " + toString(Reloc) + " to " + toString(S) +
        " not supported for Armv5 or Armv6 targets");
}

// Create a thunk for Thumb long branch on V6-M.
// Arm Architecture v6-M only supports Thumb instructions. This means
// - MOVT and MOVW instructions cannot be used.
// - Only a limited number of instructions can access registers r8 and above
// - No interworking support is needed (all Thumb).
static Thunk *addThunkV6M(RelType Reloc, Symbol &S) {
  switch (Reloc) {
  case R_ARM_THM_JUMP19:
  case R_ARM_THM_JUMP24:
  case R_ARM_THM_CALL:
    if (Config->Pic)
      return make<ThumbV6MPILongThunk>(S);
    return make<ThumbV6MABSLongThunk>(S);
  }
  fatal("relocation " + toString(Reloc) + " to " + toString(S) +
        " not supported for Armv6-M targets");
}

// Creates a thunk for Thumb-ARM interworking or branch range extension.
static Thunk *addThunkArm(RelType Reloc, Symbol &S) {
  // Decide which Thunk is needed based on:
  // Available instruction set
  // - An Arm Thunk can only be used if Arm state is available.
  // - A Thumb Thunk can only be used if Thumb state is available.
  // - Can only use a Thunk if it uses instructions that the Target supports.
  // Relocation is branch or branch and link
  // - Branch instructions cannot change state, can only select Thunk that
  //   starts in the same state as the caller.
  // - Branch and link relocations can change state, can select Thunks from
  //   either Arm or Thumb.
  // Position independent Thunks if we require position independent code.

  // Handle architectures that have restrictions on the instructions that they
  // can use in Thunks. The flags below are set by reading the BuildAttributes
  // of the input objects. InputFiles.cpp contains the mapping from ARM
  // architecture to flag.
  if (!Config->ARMHasMovtMovw) {
    if (!Config->ARMJ1J2BranchEncoding)
      return addThunkPreArmv7(Reloc, S);
    return addThunkV6M(Reloc, S);
  }

  switch (Reloc) {
  case R_ARM_PC24:
  case R_ARM_PLT32:
  case R_ARM_JUMP24:
  case R_ARM_CALL:
    if (Config->PicThunk)
      return make<ARMV7PILongThunk>(S);
    return make<ARMV7ABSLongThunk>(S);
  case R_ARM_THM_JUMP19:
  case R_ARM_THM_JUMP24:
  case R_ARM_THM_CALL:
    if (Config->PicThunk)
      return make<ThumbV7PILongThunk>(S);
    return make<ThumbV7ABSLongThunk>(S);
  }
  fatal("unrecognized relocation type");
}

static Thunk *addThunkMips(RelType Type, Symbol &S) {
  if ((S.StOther & STO_MIPS_MICROMIPS) && isMipsR6())
    return make<MicroMipsR6Thunk>(S);
  if (S.StOther & STO_MIPS_MICROMIPS)
    return make<MicroMipsThunk>(S);
  return make<MipsThunk>(S);
}

static Thunk *addThunkPPC64(RelType Type, Symbol &S) {
  assert(Type == R_PPC64_REL24 && "unexpected relocation type for thunk");
  if (S.isInPlt())
    return make<PPC64PltCallStub>(S);

  if (Config->PicThunk)
    return make<PPC64PILongBranchThunk>(S);

  return make<PPC64PDLongBranchThunk>(S);
}

Thunk *addThunk(RelType Type, Symbol &S) {
  if (Config->EMachine == EM_AARCH64)
    return addThunkAArch64(Type, S);

  if (Config->EMachine == EM_ARM)
    return addThunkArm(Type, S);

  if (Config->EMachine == EM_MIPS)
    return addThunkMips(Type, S);

  if (Config->EMachine == EM_PPC64)
    return addThunkPPC64(Type, S);

  llvm_unreachable("add Thunk only supported for ARM, Mips and PowerPC");
}

} // end namespace elf
} // end namespace lld
