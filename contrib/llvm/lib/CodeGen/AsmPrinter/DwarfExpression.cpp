//===- llvm/CodeGen/DwarfExpression.cpp - Dwarf Debug Framework -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing dwarf debug info into asm files.
//
//===----------------------------------------------------------------------===//

#include "DwarfExpression.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

using namespace llvm;

void DwarfExpression::emitConstu(uint64_t Value) {
  if (Value < 32)
    emitOp(dwarf::DW_OP_lit0 + Value);
  else if (Value == std::numeric_limits<uint64_t>::max()) {
    // Only do this for 64-bit values as the DWARF expression stack uses
    // target-address-size values.
    emitOp(dwarf::DW_OP_lit0);
    emitOp(dwarf::DW_OP_not);
  } else {
    emitOp(dwarf::DW_OP_constu);
    emitUnsigned(Value);
  }
}

void DwarfExpression::addReg(int DwarfReg, const char *Comment) {
 assert(DwarfReg >= 0 && "invalid negative dwarf register number");
 assert((LocationKind == Unknown || LocationKind == Register) &&
        "location description already locked down");
 LocationKind = Register;
 if (DwarfReg < 32) {
   emitOp(dwarf::DW_OP_reg0 + DwarfReg, Comment);
  } else {
    emitOp(dwarf::DW_OP_regx, Comment);
    emitUnsigned(DwarfReg);
  }
}

void DwarfExpression::addBReg(int DwarfReg, int Offset) {
  assert(DwarfReg >= 0 && "invalid negative dwarf register number");
  assert(LocationKind != Register && "location description already locked down");
  if (DwarfReg < 32) {
    emitOp(dwarf::DW_OP_breg0 + DwarfReg);
  } else {
    emitOp(dwarf::DW_OP_bregx);
    emitUnsigned(DwarfReg);
  }
  emitSigned(Offset);
}

void DwarfExpression::addFBReg(int Offset) {
  emitOp(dwarf::DW_OP_fbreg);
  emitSigned(Offset);
}

void DwarfExpression::addOpPiece(unsigned SizeInBits, unsigned OffsetInBits) {
  if (!SizeInBits)
    return;

  const unsigned SizeOfByte = 8;
  if (OffsetInBits > 0 || SizeInBits % SizeOfByte) {
    emitOp(dwarf::DW_OP_bit_piece);
    emitUnsigned(SizeInBits);
    emitUnsigned(OffsetInBits);
  } else {
    emitOp(dwarf::DW_OP_piece);
    unsigned ByteSize = SizeInBits / SizeOfByte;
    emitUnsigned(ByteSize);
  }
  this->OffsetInBits += SizeInBits;
}

void DwarfExpression::addShr(unsigned ShiftBy) {
  emitConstu(ShiftBy);
  emitOp(dwarf::DW_OP_shr);
}

void DwarfExpression::addAnd(unsigned Mask) {
  emitConstu(Mask);
  emitOp(dwarf::DW_OP_and);
}

bool DwarfExpression::addMachineReg(const TargetRegisterInfo &TRI,
                                    unsigned MachineReg, unsigned MaxSize) {
  if (!TRI.isPhysicalRegister(MachineReg)) {
    if (isFrameRegister(TRI, MachineReg)) {
      DwarfRegs.push_back({-1, 0, nullptr});
      return true;
    }
    return false;
  }

  int Reg = TRI.getDwarfRegNum(MachineReg, false);

  // If this is a valid register number, emit it.
  if (Reg >= 0) {
    DwarfRegs.push_back({Reg, 0, nullptr});
    return true;
  }

  // Walk up the super-register chain until we find a valid number.
  // For example, EAX on x86_64 is a 32-bit fragment of RAX with offset 0.
  for (MCSuperRegIterator SR(MachineReg, &TRI); SR.isValid(); ++SR) {
    Reg = TRI.getDwarfRegNum(*SR, false);
    if (Reg >= 0) {
      unsigned Idx = TRI.getSubRegIndex(*SR, MachineReg);
      unsigned Size = TRI.getSubRegIdxSize(Idx);
      unsigned RegOffset = TRI.getSubRegIdxOffset(Idx);
      DwarfRegs.push_back({Reg, 0, "super-register"});
      // Use a DW_OP_bit_piece to describe the sub-register.
      setSubRegisterPiece(Size, RegOffset);
      return true;
    }
  }

  // Otherwise, attempt to find a covering set of sub-register numbers.
  // For example, Q0 on ARM is a composition of D0+D1.
  unsigned CurPos = 0;
  // The size of the register in bits.
  const TargetRegisterClass *RC = TRI.getMinimalPhysRegClass(MachineReg);
  unsigned RegSize = TRI.getRegSizeInBits(*RC);
  // Keep track of the bits in the register we already emitted, so we
  // can avoid emitting redundant aliasing subregs. Because this is
  // just doing a greedy scan of all subregisters, it is possible that
  // this doesn't find a combination of subregisters that fully cover
  // the register (even though one may exist).
  SmallBitVector Coverage(RegSize, false);
  for (MCSubRegIterator SR(MachineReg, &TRI); SR.isValid(); ++SR) {
    unsigned Idx = TRI.getSubRegIndex(MachineReg, *SR);
    unsigned Size = TRI.getSubRegIdxSize(Idx);
    unsigned Offset = TRI.getSubRegIdxOffset(Idx);
    Reg = TRI.getDwarfRegNum(*SR, false);
    if (Reg < 0)
      continue;

    // Intersection between the bits we already emitted and the bits
    // covered by this subregister.
    SmallBitVector CurSubReg(RegSize, false);
    CurSubReg.set(Offset, Offset + Size);

    // If this sub-register has a DWARF number and we haven't covered
    // its range, emit a DWARF piece for it.
    if (CurSubReg.test(Coverage)) {
      // Emit a piece for any gap in the coverage.
      if (Offset > CurPos)
        DwarfRegs.push_back({-1, Offset - CurPos, "no DWARF register encoding"});
      DwarfRegs.push_back(
          {Reg, std::min<unsigned>(Size, MaxSize - Offset), "sub-register"});
      if (Offset >= MaxSize)
        break;

      // Mark it as emitted.
      Coverage.set(Offset, Offset + Size);
      CurPos = Offset + Size;
    }
  }
  // Failed to find any DWARF encoding.
  if (CurPos == 0)
    return false;
  // Found a partial or complete DWARF encoding.
  if (CurPos < RegSize)
    DwarfRegs.push_back({-1, RegSize - CurPos, "no DWARF register encoding"});
  return true;
}

void DwarfExpression::addStackValue() {
  if (DwarfVersion >= 4)
    emitOp(dwarf::DW_OP_stack_value);
}

void DwarfExpression::addSignedConstant(int64_t Value) {
  assert(LocationKind == Implicit || LocationKind == Unknown);
  LocationKind = Implicit;
  emitOp(dwarf::DW_OP_consts);
  emitSigned(Value);
}

void DwarfExpression::addUnsignedConstant(uint64_t Value) {
  assert(LocationKind == Implicit || LocationKind == Unknown);
  LocationKind = Implicit;
  emitConstu(Value);
}

void DwarfExpression::addUnsignedConstant(const APInt &Value) {
  assert(LocationKind == Implicit || LocationKind == Unknown);
  LocationKind = Implicit;

  unsigned Size = Value.getBitWidth();
  const uint64_t *Data = Value.getRawData();

  // Chop it up into 64-bit pieces, because that's the maximum that
  // addUnsignedConstant takes.
  unsigned Offset = 0;
  while (Offset < Size) {
    addUnsignedConstant(*Data++);
    if (Offset == 0 && Size <= 64)
      break;
    addStackValue();
    addOpPiece(std::min(Size - Offset, 64u), Offset);
    Offset += 64;
  }
}

bool DwarfExpression::addMachineRegExpression(const TargetRegisterInfo &TRI,
                                              DIExpressionCursor &ExprCursor,
                                              unsigned MachineReg,
                                              unsigned FragmentOffsetInBits) {
  auto Fragment = ExprCursor.getFragmentInfo();
  if (!addMachineReg(TRI, MachineReg, Fragment ? Fragment->SizeInBits : ~1U)) {
    LocationKind = Unknown;
    return false;
  }

  bool HasComplexExpression = false;
  auto Op = ExprCursor.peek();
  if (Op && Op->getOp() != dwarf::DW_OP_LLVM_fragment)
    HasComplexExpression = true;

  // If the register can only be described by a complex expression (i.e.,
  // multiple subregisters) it doesn't safely compose with another complex
  // expression. For example, it is not possible to apply a DW_OP_deref
  // operation to multiple DW_OP_pieces.
  if (HasComplexExpression && DwarfRegs.size() > 1) {
    DwarfRegs.clear();
    LocationKind = Unknown;
    return false;
  }

  // Handle simple register locations.
  if (LocationKind != Memory && !HasComplexExpression) {
    for (auto &Reg : DwarfRegs) {
      if (Reg.DwarfRegNo >= 0)
        addReg(Reg.DwarfRegNo, Reg.Comment);
      addOpPiece(Reg.Size);
    }
    DwarfRegs.clear();
    return true;
  }

  // Don't emit locations that cannot be expressed without DW_OP_stack_value.
  if (DwarfVersion < 4)
    if (any_of(ExprCursor, [](DIExpression::ExprOperand Op) -> bool {
          return Op.getOp() == dwarf::DW_OP_stack_value;
        })) {
      DwarfRegs.clear();
      LocationKind = Unknown;
      return false;
    }

  assert(DwarfRegs.size() == 1);
  auto Reg = DwarfRegs[0];
  bool FBReg = isFrameRegister(TRI, MachineReg);
  int SignedOffset = 0;
  assert(Reg.Size == 0 && "subregister has same size as superregister");

  // Pattern-match combinations for which more efficient representations exist.
  // [Reg, DW_OP_plus_uconst, Offset] --> [DW_OP_breg, Offset].
  if (Op && (Op->getOp() == dwarf::DW_OP_plus_uconst)) {
    SignedOffset = Op->getArg(0);
    ExprCursor.take();
  }

  // [Reg, DW_OP_constu, Offset, DW_OP_plus]  --> [DW_OP_breg, Offset]
  // [Reg, DW_OP_constu, Offset, DW_OP_minus] --> [DW_OP_breg,-Offset]
  // If Reg is a subregister we need to mask it out before subtracting.
  if (Op && Op->getOp() == dwarf::DW_OP_constu) {
    auto N = ExprCursor.peekNext();
    if (N && (N->getOp() == dwarf::DW_OP_plus ||
             (N->getOp() == dwarf::DW_OP_minus && !SubRegisterSizeInBits))) {
      int Offset = Op->getArg(0);
      SignedOffset = (N->getOp() == dwarf::DW_OP_minus) ? -Offset : Offset;
      ExprCursor.consume(2);
    }
  }

  if (FBReg)
    addFBReg(SignedOffset);
  else
    addBReg(Reg.DwarfRegNo, SignedOffset);
  DwarfRegs.clear();
  return true;
}

/// Assuming a well-formed expression, match "DW_OP_deref* DW_OP_LLVM_fragment?".
static bool isMemoryLocation(DIExpressionCursor ExprCursor) {
  while (ExprCursor) {
    auto Op = ExprCursor.take();
    switch (Op->getOp()) {
    case dwarf::DW_OP_deref:
    case dwarf::DW_OP_LLVM_fragment:
      break;
    default:
      return false;
    }
  }
  return true;
}

void DwarfExpression::addExpression(DIExpressionCursor &&ExprCursor,
                                    unsigned FragmentOffsetInBits) {
  // If we need to mask out a subregister, do it now, unless the next
  // operation would emit an OpPiece anyway.
  auto N = ExprCursor.peek();
  if (SubRegisterSizeInBits && N && (N->getOp() != dwarf::DW_OP_LLVM_fragment))
    maskSubRegister();

  while (ExprCursor) {
    auto Op = ExprCursor.take();
    switch (Op->getOp()) {
    case dwarf::DW_OP_LLVM_fragment: {
      unsigned SizeInBits = Op->getArg(1);
      unsigned FragmentOffset = Op->getArg(0);
      // The fragment offset must have already been adjusted by emitting an
      // empty DW_OP_piece / DW_OP_bit_piece before we emitted the base
      // location.
      assert(OffsetInBits >= FragmentOffset && "fragment offset not added?");

      // If addMachineReg already emitted DW_OP_piece operations to represent
      // a super-register by splicing together sub-registers, subtract the size
      // of the pieces that was already emitted.
      SizeInBits -= OffsetInBits - FragmentOffset;

      // If addMachineReg requested a DW_OP_bit_piece to stencil out a
      // sub-register that is smaller than the current fragment's size, use it.
      if (SubRegisterSizeInBits)
        SizeInBits = std::min<unsigned>(SizeInBits, SubRegisterSizeInBits);

      // Emit a DW_OP_stack_value for implicit location descriptions.
      if (LocationKind == Implicit)
        addStackValue();

      // Emit the DW_OP_piece.
      addOpPiece(SizeInBits, SubRegisterOffsetInBits);
      setSubRegisterPiece(0, 0);
      // Reset the location description kind.
      LocationKind = Unknown;
      return;
    }
    case dwarf::DW_OP_plus_uconst:
      assert(LocationKind != Register);
      emitOp(dwarf::DW_OP_plus_uconst);
      emitUnsigned(Op->getArg(0));
      break;
    case dwarf::DW_OP_plus:
    case dwarf::DW_OP_minus:
    case dwarf::DW_OP_mul:
    case dwarf::DW_OP_div:
    case dwarf::DW_OP_mod:
    case dwarf::DW_OP_or:
    case dwarf::DW_OP_and:
    case dwarf::DW_OP_xor:
    case dwarf::DW_OP_shl:
    case dwarf::DW_OP_shr:
    case dwarf::DW_OP_shra:
    case dwarf::DW_OP_lit0:
    case dwarf::DW_OP_not:
    case dwarf::DW_OP_dup:
      emitOp(Op->getOp());
      break;
    case dwarf::DW_OP_deref:
      assert(LocationKind != Register);
      if (LocationKind != Memory && ::isMemoryLocation(ExprCursor))
        // Turning this into a memory location description makes the deref
        // implicit.
        LocationKind = Memory;
      else
        emitOp(dwarf::DW_OP_deref);
      break;
    case dwarf::DW_OP_constu:
      assert(LocationKind != Register);
      emitConstu(Op->getArg(0));
      break;
    case dwarf::DW_OP_stack_value:
      LocationKind = Implicit;
      break;
    case dwarf::DW_OP_swap:
      assert(LocationKind != Register);
      emitOp(dwarf::DW_OP_swap);
      break;
    case dwarf::DW_OP_xderef:
      assert(LocationKind != Register);
      emitOp(dwarf::DW_OP_xderef);
      break;
    default:
      llvm_unreachable("unhandled opcode found in expression");
    }
  }

  if (LocationKind == Implicit)
    // Turn this into an implicit location description.
    addStackValue();
}

/// add masking operations to stencil out a subregister.
void DwarfExpression::maskSubRegister() {
  assert(SubRegisterSizeInBits && "no subregister was registered");
  if (SubRegisterOffsetInBits > 0)
    addShr(SubRegisterOffsetInBits);
  uint64_t Mask = (1ULL << (uint64_t)SubRegisterSizeInBits) - 1ULL;
  addAnd(Mask);
}

void DwarfExpression::finalize() {
  assert(DwarfRegs.size() == 0 && "dwarf registers not emitted");
  // Emit any outstanding DW_OP_piece operations to mask out subregisters.
  if (SubRegisterSizeInBits == 0)
    return;
  // Don't emit a DW_OP_piece for a subregister at offset 0.
  if (SubRegisterOffsetInBits == 0)
    return;
  addOpPiece(SubRegisterSizeInBits, SubRegisterOffsetInBits);
}

void DwarfExpression::addFragmentOffset(const DIExpression *Expr) {
  if (!Expr || !Expr->isFragment())
    return;

  uint64_t FragmentOffset = Expr->getFragmentInfo()->OffsetInBits;
  assert(FragmentOffset >= OffsetInBits &&
         "overlapping or duplicate fragments");
  if (FragmentOffset > OffsetInBits)
    addOpPiece(FragmentOffset - OffsetInBits);
  OffsetInBits = FragmentOffset;
}
