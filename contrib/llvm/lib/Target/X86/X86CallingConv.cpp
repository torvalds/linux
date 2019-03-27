//=== X86CallingConv.cpp - X86 Custom Calling Convention Impl   -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of custom routines for the X86
// Calling Convention that aren't done by tablegen.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86MCTargetDesc.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/IR/CallingConv.h"

namespace llvm {

bool CC_X86_32_RegCall_Assign2Regs(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                   CCValAssign::LocInfo &LocInfo,
                                   ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  // List of GPR registers that are available to store values in regcall
  // calling convention.
  static const MCPhysReg RegList[] = {X86::EAX, X86::ECX, X86::EDX, X86::EDI,
                                      X86::ESI};

  // The vector will save all the available registers for allocation.
  SmallVector<unsigned, 5> AvailableRegs;

  // searching for the available registers.
  for (auto Reg : RegList) {
    if (!State.isAllocated(Reg))
      AvailableRegs.push_back(Reg);
  }

  const size_t RequiredGprsUponSplit = 2;
  if (AvailableRegs.size() < RequiredGprsUponSplit)
    return false; // Not enough free registers - continue the search.

  // Allocating the available registers.
  for (unsigned I = 0; I < RequiredGprsUponSplit; I++) {

    // Marking the register as located.
    unsigned Reg = State.AllocateReg(AvailableRegs[I]);

    // Since we previously made sure that 2 registers are available
    // we expect that a real register number will be returned.
    assert(Reg && "Expecting a register will be available");

    // Assign the value to the allocated register
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  }

  // Successful in allocating regsiters - stop scanning next rules.
  return true;
}

static ArrayRef<MCPhysReg> CC_X86_VectorCallGetSSEs(const MVT &ValVT) {
  if (ValVT.is512BitVector()) {
    static const MCPhysReg RegListZMM[] = {X86::ZMM0, X86::ZMM1, X86::ZMM2,
                                           X86::ZMM3, X86::ZMM4, X86::ZMM5};
    return makeArrayRef(std::begin(RegListZMM), std::end(RegListZMM));
  }

  if (ValVT.is256BitVector()) {
    static const MCPhysReg RegListYMM[] = {X86::YMM0, X86::YMM1, X86::YMM2,
                                           X86::YMM3, X86::YMM4, X86::YMM5};
    return makeArrayRef(std::begin(RegListYMM), std::end(RegListYMM));
  }

  static const MCPhysReg RegListXMM[] = {X86::XMM0, X86::XMM1, X86::XMM2,
                                         X86::XMM3, X86::XMM4, X86::XMM5};
  return makeArrayRef(std::begin(RegListXMM), std::end(RegListXMM));
}

static ArrayRef<MCPhysReg> CC_X86_64_VectorCallGetGPRs() {
  static const MCPhysReg RegListGPR[] = {X86::RCX, X86::RDX, X86::R8, X86::R9};
  return makeArrayRef(std::begin(RegListGPR), std::end(RegListGPR));
}

static bool CC_X86_VectorCallAssignRegister(unsigned &ValNo, MVT &ValVT,
                                            MVT &LocVT,
                                            CCValAssign::LocInfo &LocInfo,
                                            ISD::ArgFlagsTy &ArgFlags,
                                            CCState &State) {

  ArrayRef<MCPhysReg> RegList = CC_X86_VectorCallGetSSEs(ValVT);
  bool Is64bit = static_cast<const X86Subtarget &>(
                     State.getMachineFunction().getSubtarget())
                     .is64Bit();

  for (auto Reg : RegList) {
    // If the register is not marked as allocated - assign to it.
    if (!State.isAllocated(Reg)) {
      unsigned AssigedReg = State.AllocateReg(Reg);
      assert(AssigedReg == Reg && "Expecting a valid register allocation");
      State.addLoc(
          CCValAssign::getReg(ValNo, ValVT, AssigedReg, LocVT, LocInfo));
      return true;
    }
    // If the register is marked as shadow allocated - assign to it.
    if (Is64bit && State.IsShadowAllocatedReg(Reg)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return true;
    }
  }

  llvm_unreachable("Clang should ensure that hva marked vectors will have "
                   "an available register.");
  return false;
}

bool CC_X86_64_VectorCall(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                          CCValAssign::LocInfo &LocInfo,
                          ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  // On the second pass, go through the HVAs only.
  if (ArgFlags.isSecArgPass()) {
    if (ArgFlags.isHva())
      return CC_X86_VectorCallAssignRegister(ValNo, ValVT, LocVT, LocInfo,
                                             ArgFlags, State);
    return true;
  }

  // Process only vector types as defined by vectorcall spec:
  // "A vector type is either a floating-point type, for example,
  //  a float or double, or an SIMD vector type, for example, __m128 or __m256".
  if (!(ValVT.isFloatingPoint() ||
        (ValVT.isVector() && ValVT.getSizeInBits() >= 128))) {
    // If R9 was already assigned it means that we are after the fourth element
    // and because this is not an HVA / Vector type, we need to allocate
    // shadow XMM register.
    if (State.isAllocated(X86::R9)) {
      // Assign shadow XMM register.
      (void)State.AllocateReg(CC_X86_VectorCallGetSSEs(ValVT));
    }

    return false;
  }

  if (!ArgFlags.isHva() || ArgFlags.isHvaStart()) {
    // Assign shadow GPR register.
    (void)State.AllocateReg(CC_X86_64_VectorCallGetGPRs());

    // Assign XMM register - (shadow for HVA and non-shadow for non HVA).
    if (unsigned Reg = State.AllocateReg(CC_X86_VectorCallGetSSEs(ValVT))) {
      // In Vectorcall Calling convention, additional shadow stack can be
      // created on top of the basic 32 bytes of win64.
      // It can happen if the fifth or sixth argument is vector type or HVA.
      // At that case for each argument a shadow stack of 8 bytes is allocated.
      if (Reg == X86::XMM4 || Reg == X86::XMM5)
        State.AllocateStack(8, 8);

      if (!ArgFlags.isHva()) {
        State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
        return true; // Allocated a register - Stop the search.
      }
    }
  }

  // If this is an HVA - Stop the search,
  // otherwise continue the search.
  return ArgFlags.isHva();
}

bool CC_X86_32_VectorCall(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                          CCValAssign::LocInfo &LocInfo,
                          ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  // On the second pass, go through the HVAs only.
  if (ArgFlags.isSecArgPass()) {
    if (ArgFlags.isHva())
      return CC_X86_VectorCallAssignRegister(ValNo, ValVT, LocVT, LocInfo,
                                             ArgFlags, State);
    return true;
  }

  // Process only vector types as defined by vectorcall spec:
  // "A vector type is either a floating point type, for example,
  //  a float or double, or an SIMD vector type, for example, __m128 or __m256".
  if (!(ValVT.isFloatingPoint() ||
        (ValVT.isVector() && ValVT.getSizeInBits() >= 128))) {
    return false;
  }

  if (ArgFlags.isHva())
    return true; // If this is an HVA - Stop the search.

  // Assign XMM register.
  if (unsigned Reg = State.AllocateReg(CC_X86_VectorCallGetSSEs(ValVT))) {
    State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
    return true;
  }

  // In case we did not find an available XMM register for a vector -
  // pass it indirectly.
  // It is similar to CCPassIndirect, with the addition of inreg.
  if (!ValVT.isFloatingPoint()) {
    LocVT = MVT::i32;
    LocInfo = CCValAssign::Indirect;
    ArgFlags.setInReg();
  }

  return false; // No register was assigned - Continue the search.
}

} // End llvm namespace
