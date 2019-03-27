//==--- InstrEmitter.cpp - Emit MachineInstrs for the SelectionDAG class ---==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements the Emit routines for the SelectionDAG class, which creates
// MachineInstrs based on the decisions of the SelectionDAG instruction
// selection.
//
//===----------------------------------------------------------------------===//

#include "InstrEmitter.h"
#include "SDNodeDbgValue.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
using namespace llvm;

#define DEBUG_TYPE "instr-emitter"

/// MinRCSize - Smallest register class we allow when constraining virtual
/// registers.  If satisfying all register class constraints would require
/// using a smaller register class, emit a COPY to a new virtual register
/// instead.
const unsigned MinRCSize = 4;

/// CountResults - The results of target nodes have register or immediate
/// operands first, then an optional chain, and optional glue operands (which do
/// not go into the resulting MachineInstr).
unsigned InstrEmitter::CountResults(SDNode *Node) {
  unsigned N = Node->getNumValues();
  while (N && Node->getValueType(N - 1) == MVT::Glue)
    --N;
  if (N && Node->getValueType(N - 1) == MVT::Other)
    --N;    // Skip over chain result.
  return N;
}

/// countOperands - The inputs to target nodes have any actual inputs first,
/// followed by an optional chain operand, then an optional glue operand.
/// Compute the number of actual operands that will go into the resulting
/// MachineInstr.
///
/// Also count physreg RegisterSDNode and RegisterMaskSDNode operands preceding
/// the chain and glue. These operands may be implicit on the machine instr.
static unsigned countOperands(SDNode *Node, unsigned NumExpUses,
                              unsigned &NumImpUses) {
  unsigned N = Node->getNumOperands();
  while (N && Node->getOperand(N - 1).getValueType() == MVT::Glue)
    --N;
  if (N && Node->getOperand(N - 1).getValueType() == MVT::Other)
    --N; // Ignore chain if it exists.

  // Count RegisterSDNode and RegisterMaskSDNode operands for NumImpUses.
  NumImpUses = N - NumExpUses;
  for (unsigned I = N; I > NumExpUses; --I) {
    if (isa<RegisterMaskSDNode>(Node->getOperand(I - 1)))
      continue;
    if (RegisterSDNode *RN = dyn_cast<RegisterSDNode>(Node->getOperand(I - 1)))
      if (TargetRegisterInfo::isPhysicalRegister(RN->getReg()))
        continue;
    NumImpUses = N - I;
    break;
  }

  return N;
}

/// EmitCopyFromReg - Generate machine code for an CopyFromReg node or an
/// implicit physical register output.
void InstrEmitter::
EmitCopyFromReg(SDNode *Node, unsigned ResNo, bool IsClone, bool IsCloned,
                unsigned SrcReg, DenseMap<SDValue, unsigned> &VRBaseMap) {
  unsigned VRBase = 0;
  if (TargetRegisterInfo::isVirtualRegister(SrcReg)) {
    // Just use the input register directly!
    SDValue Op(Node, ResNo);
    if (IsClone)
      VRBaseMap.erase(Op);
    bool isNew = VRBaseMap.insert(std::make_pair(Op, SrcReg)).second;
    (void)isNew; // Silence compiler warning.
    assert(isNew && "Node emitted out of order - early");
    return;
  }

  // If the node is only used by a CopyToReg and the dest reg is a vreg, use
  // the CopyToReg'd destination register instead of creating a new vreg.
  bool MatchReg = true;
  const TargetRegisterClass *UseRC = nullptr;
  MVT VT = Node->getSimpleValueType(ResNo);

  // Stick to the preferred register classes for legal types.
  if (TLI->isTypeLegal(VT))
    UseRC = TLI->getRegClassFor(VT);

  if (!IsClone && !IsCloned)
    for (SDNode *User : Node->uses()) {
      bool Match = true;
      if (User->getOpcode() == ISD::CopyToReg &&
          User->getOperand(2).getNode() == Node &&
          User->getOperand(2).getResNo() == ResNo) {
        unsigned DestReg = cast<RegisterSDNode>(User->getOperand(1))->getReg();
        if (TargetRegisterInfo::isVirtualRegister(DestReg)) {
          VRBase = DestReg;
          Match = false;
        } else if (DestReg != SrcReg)
          Match = false;
      } else {
        for (unsigned i = 0, e = User->getNumOperands(); i != e; ++i) {
          SDValue Op = User->getOperand(i);
          if (Op.getNode() != Node || Op.getResNo() != ResNo)
            continue;
          MVT VT = Node->getSimpleValueType(Op.getResNo());
          if (VT == MVT::Other || VT == MVT::Glue)
            continue;
          Match = false;
          if (User->isMachineOpcode()) {
            const MCInstrDesc &II = TII->get(User->getMachineOpcode());
            const TargetRegisterClass *RC = nullptr;
            if (i+II.getNumDefs() < II.getNumOperands()) {
              RC = TRI->getAllocatableClass(
                TII->getRegClass(II, i+II.getNumDefs(), TRI, *MF));
            }
            if (!UseRC)
              UseRC = RC;
            else if (RC) {
              const TargetRegisterClass *ComRC =
                TRI->getCommonSubClass(UseRC, RC, VT.SimpleTy);
              // If multiple uses expect disjoint register classes, we emit
              // copies in AddRegisterOperand.
              if (ComRC)
                UseRC = ComRC;
            }
          }
        }
      }
      MatchReg &= Match;
      if (VRBase)
        break;
    }

  const TargetRegisterClass *SrcRC = nullptr, *DstRC = nullptr;
  SrcRC = TRI->getMinimalPhysRegClass(SrcReg, VT);

  // Figure out the register class to create for the destreg.
  if (VRBase) {
    DstRC = MRI->getRegClass(VRBase);
  } else if (UseRC) {
    assert(TRI->isTypeLegalForClass(*UseRC, VT) &&
           "Incompatible phys register def and uses!");
    DstRC = UseRC;
  } else {
    DstRC = TLI->getRegClassFor(VT);
  }

  // If all uses are reading from the src physical register and copying the
  // register is either impossible or very expensive, then don't create a copy.
  if (MatchReg && SrcRC->getCopyCost() < 0) {
    VRBase = SrcReg;
  } else {
    // Create the reg, emit the copy.
    VRBase = MRI->createVirtualRegister(DstRC);
    BuildMI(*MBB, InsertPos, Node->getDebugLoc(), TII->get(TargetOpcode::COPY),
            VRBase).addReg(SrcReg);
  }

  SDValue Op(Node, ResNo);
  if (IsClone)
    VRBaseMap.erase(Op);
  bool isNew = VRBaseMap.insert(std::make_pair(Op, VRBase)).second;
  (void)isNew; // Silence compiler warning.
  assert(isNew && "Node emitted out of order - early");
}

/// getDstOfCopyToRegUse - If the only use of the specified result number of
/// node is a CopyToReg, return its destination register. Return 0 otherwise.
unsigned InstrEmitter::getDstOfOnlyCopyToRegUse(SDNode *Node,
                                                unsigned ResNo) const {
  if (!Node->hasOneUse())
    return 0;

  SDNode *User = *Node->use_begin();
  if (User->getOpcode() == ISD::CopyToReg &&
      User->getOperand(2).getNode() == Node &&
      User->getOperand(2).getResNo() == ResNo) {
    unsigned Reg = cast<RegisterSDNode>(User->getOperand(1))->getReg();
    if (TargetRegisterInfo::isVirtualRegister(Reg))
      return Reg;
  }
  return 0;
}

void InstrEmitter::CreateVirtualRegisters(SDNode *Node,
                                       MachineInstrBuilder &MIB,
                                       const MCInstrDesc &II,
                                       bool IsClone, bool IsCloned,
                                       DenseMap<SDValue, unsigned> &VRBaseMap) {
  assert(Node->getMachineOpcode() != TargetOpcode::IMPLICIT_DEF &&
         "IMPLICIT_DEF should have been handled as a special case elsewhere!");

  unsigned NumResults = CountResults(Node);
  for (unsigned i = 0; i < II.getNumDefs(); ++i) {
    // If the specific node value is only used by a CopyToReg and the dest reg
    // is a vreg in the same register class, use the CopyToReg'd destination
    // register instead of creating a new vreg.
    unsigned VRBase = 0;
    const TargetRegisterClass *RC =
      TRI->getAllocatableClass(TII->getRegClass(II, i, TRI, *MF));
    // Always let the value type influence the used register class. The
    // constraints on the instruction may be too lax to represent the value
    // type correctly. For example, a 64-bit float (X86::FR64) can't live in
    // the 32-bit float super-class (X86::FR32).
    if (i < NumResults && TLI->isTypeLegal(Node->getSimpleValueType(i))) {
      const TargetRegisterClass *VTRC =
        TLI->getRegClassFor(Node->getSimpleValueType(i));
      if (RC)
        VTRC = TRI->getCommonSubClass(RC, VTRC);
      if (VTRC)
        RC = VTRC;
    }

    if (II.OpInfo[i].isOptionalDef()) {
      // Optional def must be a physical register.
      VRBase = cast<RegisterSDNode>(Node->getOperand(i-NumResults))->getReg();
      assert(TargetRegisterInfo::isPhysicalRegister(VRBase));
      MIB.addReg(VRBase, RegState::Define);
    }

    if (!VRBase && !IsClone && !IsCloned)
      for (SDNode *User : Node->uses()) {
        if (User->getOpcode() == ISD::CopyToReg &&
            User->getOperand(2).getNode() == Node &&
            User->getOperand(2).getResNo() == i) {
          unsigned Reg = cast<RegisterSDNode>(User->getOperand(1))->getReg();
          if (TargetRegisterInfo::isVirtualRegister(Reg)) {
            const TargetRegisterClass *RegRC = MRI->getRegClass(Reg);
            if (RegRC == RC) {
              VRBase = Reg;
              MIB.addReg(VRBase, RegState::Define);
              break;
            }
          }
        }
      }

    // Create the result registers for this node and add the result regs to
    // the machine instruction.
    if (VRBase == 0) {
      assert(RC && "Isn't a register operand!");
      VRBase = MRI->createVirtualRegister(RC);
      MIB.addReg(VRBase, RegState::Define);
    }

    // If this def corresponds to a result of the SDNode insert the VRBase into
    // the lookup map.
    if (i < NumResults) {
      SDValue Op(Node, i);
      if (IsClone)
        VRBaseMap.erase(Op);
      bool isNew = VRBaseMap.insert(std::make_pair(Op, VRBase)).second;
      (void)isNew; // Silence compiler warning.
      assert(isNew && "Node emitted out of order - early");
    }
  }
}

/// getVR - Return the virtual register corresponding to the specified result
/// of the specified node.
unsigned InstrEmitter::getVR(SDValue Op,
                             DenseMap<SDValue, unsigned> &VRBaseMap) {
  if (Op.isMachineOpcode() &&
      Op.getMachineOpcode() == TargetOpcode::IMPLICIT_DEF) {
    // Add an IMPLICIT_DEF instruction before every use.
    unsigned VReg = getDstOfOnlyCopyToRegUse(Op.getNode(), Op.getResNo());
    // IMPLICIT_DEF can produce any type of result so its MCInstrDesc
    // does not include operand register class info.
    if (!VReg) {
      const TargetRegisterClass *RC =
        TLI->getRegClassFor(Op.getSimpleValueType());
      VReg = MRI->createVirtualRegister(RC);
    }
    BuildMI(*MBB, InsertPos, Op.getDebugLoc(),
            TII->get(TargetOpcode::IMPLICIT_DEF), VReg);
    return VReg;
  }

  DenseMap<SDValue, unsigned>::iterator I = VRBaseMap.find(Op);
  assert(I != VRBaseMap.end() && "Node emitted out of order - late");
  return I->second;
}


/// AddRegisterOperand - Add the specified register as an operand to the
/// specified machine instr. Insert register copies if the register is
/// not in the required register class.
void
InstrEmitter::AddRegisterOperand(MachineInstrBuilder &MIB,
                                 SDValue Op,
                                 unsigned IIOpNum,
                                 const MCInstrDesc *II,
                                 DenseMap<SDValue, unsigned> &VRBaseMap,
                                 bool IsDebug, bool IsClone, bool IsCloned) {
  assert(Op.getValueType() != MVT::Other &&
         Op.getValueType() != MVT::Glue &&
         "Chain and glue operands should occur at end of operand list!");
  // Get/emit the operand.
  unsigned VReg = getVR(Op, VRBaseMap);

  const MCInstrDesc &MCID = MIB->getDesc();
  bool isOptDef = IIOpNum < MCID.getNumOperands() &&
    MCID.OpInfo[IIOpNum].isOptionalDef();

  // If the instruction requires a register in a different class, create
  // a new virtual register and copy the value into it, but first attempt to
  // shrink VReg's register class within reason.  For example, if VReg == GR32
  // and II requires a GR32_NOSP, just constrain VReg to GR32_NOSP.
  if (II) {
    const TargetRegisterClass *OpRC = nullptr;
    if (IIOpNum < II->getNumOperands())
      OpRC = TII->getRegClass(*II, IIOpNum, TRI, *MF);

    if (OpRC) {
      const TargetRegisterClass *ConstrainedRC
        = MRI->constrainRegClass(VReg, OpRC, MinRCSize);
      if (!ConstrainedRC) {
        OpRC = TRI->getAllocatableClass(OpRC);
        assert(OpRC && "Constraints cannot be fulfilled for allocation");
        unsigned NewVReg = MRI->createVirtualRegister(OpRC);
        BuildMI(*MBB, InsertPos, Op.getNode()->getDebugLoc(),
                TII->get(TargetOpcode::COPY), NewVReg).addReg(VReg);
        VReg = NewVReg;
      } else {
        assert(ConstrainedRC->isAllocatable() &&
           "Constraining an allocatable VReg produced an unallocatable class?");
      }
    }
  }

  // If this value has only one use, that use is a kill. This is a
  // conservative approximation. InstrEmitter does trivial coalescing
  // with CopyFromReg nodes, so don't emit kill flags for them.
  // Avoid kill flags on Schedule cloned nodes, since there will be
  // multiple uses.
  // Tied operands are never killed, so we need to check that. And that
  // means we need to determine the index of the operand.
  bool isKill = Op.hasOneUse() &&
                Op.getNode()->getOpcode() != ISD::CopyFromReg &&
                !IsDebug &&
                !(IsClone || IsCloned);
  if (isKill) {
    unsigned Idx = MIB->getNumOperands();
    while (Idx > 0 &&
           MIB->getOperand(Idx-1).isReg() &&
           MIB->getOperand(Idx-1).isImplicit())
      --Idx;
    bool isTied = MCID.getOperandConstraint(Idx, MCOI::TIED_TO) != -1;
    if (isTied)
      isKill = false;
  }

  MIB.addReg(VReg, getDefRegState(isOptDef) | getKillRegState(isKill) |
             getDebugRegState(IsDebug));
}

/// AddOperand - Add the specified operand to the specified machine instr.  II
/// specifies the instruction information for the node, and IIOpNum is the
/// operand number (in the II) that we are adding.
void InstrEmitter::AddOperand(MachineInstrBuilder &MIB,
                              SDValue Op,
                              unsigned IIOpNum,
                              const MCInstrDesc *II,
                              DenseMap<SDValue, unsigned> &VRBaseMap,
                              bool IsDebug, bool IsClone, bool IsCloned) {
  if (Op.isMachineOpcode()) {
    AddRegisterOperand(MIB, Op, IIOpNum, II, VRBaseMap,
                       IsDebug, IsClone, IsCloned);
  } else if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op)) {
    MIB.addImm(C->getSExtValue());
  } else if (ConstantFPSDNode *F = dyn_cast<ConstantFPSDNode>(Op)) {
    MIB.addFPImm(F->getConstantFPValue());
  } else if (RegisterSDNode *R = dyn_cast<RegisterSDNode>(Op)) {
    unsigned VReg = R->getReg();
    MVT OpVT = Op.getSimpleValueType();
    const TargetRegisterClass *OpRC =
        TLI->isTypeLegal(OpVT) ? TLI->getRegClassFor(OpVT) : nullptr;
    const TargetRegisterClass *IIRC =
        II ? TRI->getAllocatableClass(TII->getRegClass(*II, IIOpNum, TRI, *MF))
           : nullptr;

    if (OpRC && IIRC && OpRC != IIRC &&
        TargetRegisterInfo::isVirtualRegister(VReg)) {
      unsigned NewVReg = MRI->createVirtualRegister(IIRC);
      BuildMI(*MBB, InsertPos, Op.getNode()->getDebugLoc(),
               TII->get(TargetOpcode::COPY), NewVReg).addReg(VReg);
      VReg = NewVReg;
    }
    // Turn additional physreg operands into implicit uses on non-variadic
    // instructions. This is used by call and return instructions passing
    // arguments in registers.
    bool Imp = II && (IIOpNum >= II->getNumOperands() && !II->isVariadic());
    MIB.addReg(VReg, getImplRegState(Imp));
  } else if (RegisterMaskSDNode *RM = dyn_cast<RegisterMaskSDNode>(Op)) {
    MIB.addRegMask(RM->getRegMask());
  } else if (GlobalAddressSDNode *TGA = dyn_cast<GlobalAddressSDNode>(Op)) {
    MIB.addGlobalAddress(TGA->getGlobal(), TGA->getOffset(),
                         TGA->getTargetFlags());
  } else if (BasicBlockSDNode *BBNode = dyn_cast<BasicBlockSDNode>(Op)) {
    MIB.addMBB(BBNode->getBasicBlock());
  } else if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(Op)) {
    MIB.addFrameIndex(FI->getIndex());
  } else if (JumpTableSDNode *JT = dyn_cast<JumpTableSDNode>(Op)) {
    MIB.addJumpTableIndex(JT->getIndex(), JT->getTargetFlags());
  } else if (ConstantPoolSDNode *CP = dyn_cast<ConstantPoolSDNode>(Op)) {
    int Offset = CP->getOffset();
    unsigned Align = CP->getAlignment();
    Type *Type = CP->getType();
    // MachineConstantPool wants an explicit alignment.
    if (Align == 0) {
      Align = MF->getDataLayout().getPrefTypeAlignment(Type);
      if (Align == 0) {
        // Alignment of vector types.  FIXME!
        Align = MF->getDataLayout().getTypeAllocSize(Type);
      }
    }

    unsigned Idx;
    MachineConstantPool *MCP = MF->getConstantPool();
    if (CP->isMachineConstantPoolEntry())
      Idx = MCP->getConstantPoolIndex(CP->getMachineCPVal(), Align);
    else
      Idx = MCP->getConstantPoolIndex(CP->getConstVal(), Align);
    MIB.addConstantPoolIndex(Idx, Offset, CP->getTargetFlags());
  } else if (ExternalSymbolSDNode *ES = dyn_cast<ExternalSymbolSDNode>(Op)) {
    MIB.addExternalSymbol(ES->getSymbol(), ES->getTargetFlags());
  } else if (auto *SymNode = dyn_cast<MCSymbolSDNode>(Op)) {
    MIB.addSym(SymNode->getMCSymbol());
  } else if (BlockAddressSDNode *BA = dyn_cast<BlockAddressSDNode>(Op)) {
    MIB.addBlockAddress(BA->getBlockAddress(),
                        BA->getOffset(),
                        BA->getTargetFlags());
  } else if (TargetIndexSDNode *TI = dyn_cast<TargetIndexSDNode>(Op)) {
    MIB.addTargetIndex(TI->getIndex(), TI->getOffset(), TI->getTargetFlags());
  } else {
    assert(Op.getValueType() != MVT::Other &&
           Op.getValueType() != MVT::Glue &&
           "Chain and glue operands should occur at end of operand list!");
    AddRegisterOperand(MIB, Op, IIOpNum, II, VRBaseMap,
                       IsDebug, IsClone, IsCloned);
  }
}

unsigned InstrEmitter::ConstrainForSubReg(unsigned VReg, unsigned SubIdx,
                                          MVT VT, const DebugLoc &DL) {
  const TargetRegisterClass *VRC = MRI->getRegClass(VReg);
  const TargetRegisterClass *RC = TRI->getSubClassWithSubReg(VRC, SubIdx);

  // RC is a sub-class of VRC that supports SubIdx.  Try to constrain VReg
  // within reason.
  if (RC && RC != VRC)
    RC = MRI->constrainRegClass(VReg, RC, MinRCSize);

  // VReg has been adjusted.  It can be used with SubIdx operands now.
  if (RC)
    return VReg;

  // VReg couldn't be reasonably constrained.  Emit a COPY to a new virtual
  // register instead.
  RC = TRI->getSubClassWithSubReg(TLI->getRegClassFor(VT), SubIdx);
  assert(RC && "No legal register class for VT supports that SubIdx");
  unsigned NewReg = MRI->createVirtualRegister(RC);
  BuildMI(*MBB, InsertPos, DL, TII->get(TargetOpcode::COPY), NewReg)
    .addReg(VReg);
  return NewReg;
}

/// EmitSubregNode - Generate machine code for subreg nodes.
///
void InstrEmitter::EmitSubregNode(SDNode *Node,
                                  DenseMap<SDValue, unsigned> &VRBaseMap,
                                  bool IsClone, bool IsCloned) {
  unsigned VRBase = 0;
  unsigned Opc = Node->getMachineOpcode();

  // If the node is only used by a CopyToReg and the dest reg is a vreg, use
  // the CopyToReg'd destination register instead of creating a new vreg.
  for (SDNode *User : Node->uses()) {
    if (User->getOpcode() == ISD::CopyToReg &&
        User->getOperand(2).getNode() == Node) {
      unsigned DestReg = cast<RegisterSDNode>(User->getOperand(1))->getReg();
      if (TargetRegisterInfo::isVirtualRegister(DestReg)) {
        VRBase = DestReg;
        break;
      }
    }
  }

  if (Opc == TargetOpcode::EXTRACT_SUBREG) {
    // EXTRACT_SUBREG is lowered as %dst = COPY %src:sub.  There are no
    // constraints on the %dst register, COPY can target all legal register
    // classes.
    unsigned SubIdx = cast<ConstantSDNode>(Node->getOperand(1))->getZExtValue();
    const TargetRegisterClass *TRC =
      TLI->getRegClassFor(Node->getSimpleValueType(0));

    unsigned Reg;
    MachineInstr *DefMI;
    RegisterSDNode *R = dyn_cast<RegisterSDNode>(Node->getOperand(0));
    if (R && TargetRegisterInfo::isPhysicalRegister(R->getReg())) {
      Reg = R->getReg();
      DefMI = nullptr;
    } else {
      Reg = R ? R->getReg() : getVR(Node->getOperand(0), VRBaseMap);
      DefMI = MRI->getVRegDef(Reg);
    }

    unsigned SrcReg, DstReg, DefSubIdx;
    if (DefMI &&
        TII->isCoalescableExtInstr(*DefMI, SrcReg, DstReg, DefSubIdx) &&
        SubIdx == DefSubIdx &&
        TRC == MRI->getRegClass(SrcReg)) {
      // Optimize these:
      // r1025 = s/zext r1024, 4
      // r1026 = extract_subreg r1025, 4
      // to a copy
      // r1026 = copy r1024
      VRBase = MRI->createVirtualRegister(TRC);
      BuildMI(*MBB, InsertPos, Node->getDebugLoc(),
              TII->get(TargetOpcode::COPY), VRBase).addReg(SrcReg);
      MRI->clearKillFlags(SrcReg);
    } else {
      // Reg may not support a SubIdx sub-register, and we may need to
      // constrain its register class or issue a COPY to a compatible register
      // class.
      if (TargetRegisterInfo::isVirtualRegister(Reg))
        Reg = ConstrainForSubReg(Reg, SubIdx,
                                 Node->getOperand(0).getSimpleValueType(),
                                 Node->getDebugLoc());

      // Create the destreg if it is missing.
      if (VRBase == 0)
        VRBase = MRI->createVirtualRegister(TRC);

      // Create the extract_subreg machine instruction.
      MachineInstrBuilder CopyMI =
          BuildMI(*MBB, InsertPos, Node->getDebugLoc(),
                  TII->get(TargetOpcode::COPY), VRBase);
      if (TargetRegisterInfo::isVirtualRegister(Reg))
        CopyMI.addReg(Reg, 0, SubIdx);
      else
        CopyMI.addReg(TRI->getSubReg(Reg, SubIdx));
    }
  } else if (Opc == TargetOpcode::INSERT_SUBREG ||
             Opc == TargetOpcode::SUBREG_TO_REG) {
    SDValue N0 = Node->getOperand(0);
    SDValue N1 = Node->getOperand(1);
    SDValue N2 = Node->getOperand(2);
    unsigned SubIdx = cast<ConstantSDNode>(N2)->getZExtValue();

    // Figure out the register class to create for the destreg.  It should be
    // the largest legal register class supporting SubIdx sub-registers.
    // RegisterCoalescer will constrain it further if it decides to eliminate
    // the INSERT_SUBREG instruction.
    //
    //   %dst = INSERT_SUBREG %src, %sub, SubIdx
    //
    // is lowered by TwoAddressInstructionPass to:
    //
    //   %dst = COPY %src
    //   %dst:SubIdx = COPY %sub
    //
    // There is no constraint on the %src register class.
    //
    const TargetRegisterClass *SRC = TLI->getRegClassFor(Node->getSimpleValueType(0));
    SRC = TRI->getSubClassWithSubReg(SRC, SubIdx);
    assert(SRC && "No register class supports VT and SubIdx for INSERT_SUBREG");

    if (VRBase == 0 || !SRC->hasSubClassEq(MRI->getRegClass(VRBase)))
      VRBase = MRI->createVirtualRegister(SRC);

    // Create the insert_subreg or subreg_to_reg machine instruction.
    MachineInstrBuilder MIB =
      BuildMI(*MF, Node->getDebugLoc(), TII->get(Opc), VRBase);

    // If creating a subreg_to_reg, then the first input operand
    // is an implicit value immediate, otherwise it's a register
    if (Opc == TargetOpcode::SUBREG_TO_REG) {
      const ConstantSDNode *SD = cast<ConstantSDNode>(N0);
      MIB.addImm(SD->getZExtValue());
    } else
      AddOperand(MIB, N0, 0, nullptr, VRBaseMap, /*IsDebug=*/false,
                 IsClone, IsCloned);
    // Add the subregister being inserted
    AddOperand(MIB, N1, 0, nullptr, VRBaseMap, /*IsDebug=*/false,
               IsClone, IsCloned);
    MIB.addImm(SubIdx);
    MBB->insert(InsertPos, MIB);
  } else
    llvm_unreachable("Node is not insert_subreg, extract_subreg, or subreg_to_reg");

  SDValue Op(Node, 0);
  bool isNew = VRBaseMap.insert(std::make_pair(Op, VRBase)).second;
  (void)isNew; // Silence compiler warning.
  assert(isNew && "Node emitted out of order - early");
}

/// EmitCopyToRegClassNode - Generate machine code for COPY_TO_REGCLASS nodes.
/// COPY_TO_REGCLASS is just a normal copy, except that the destination
/// register is constrained to be in a particular register class.
///
void
InstrEmitter::EmitCopyToRegClassNode(SDNode *Node,
                                     DenseMap<SDValue, unsigned> &VRBaseMap) {
  unsigned VReg = getVR(Node->getOperand(0), VRBaseMap);

  // Create the new VReg in the destination class and emit a copy.
  unsigned DstRCIdx = cast<ConstantSDNode>(Node->getOperand(1))->getZExtValue();
  const TargetRegisterClass *DstRC =
    TRI->getAllocatableClass(TRI->getRegClass(DstRCIdx));
  unsigned NewVReg = MRI->createVirtualRegister(DstRC);
  BuildMI(*MBB, InsertPos, Node->getDebugLoc(), TII->get(TargetOpcode::COPY),
    NewVReg).addReg(VReg);

  SDValue Op(Node, 0);
  bool isNew = VRBaseMap.insert(std::make_pair(Op, NewVReg)).second;
  (void)isNew; // Silence compiler warning.
  assert(isNew && "Node emitted out of order - early");
}

/// EmitRegSequence - Generate machine code for REG_SEQUENCE nodes.
///
void InstrEmitter::EmitRegSequence(SDNode *Node,
                                  DenseMap<SDValue, unsigned> &VRBaseMap,
                                  bool IsClone, bool IsCloned) {
  unsigned DstRCIdx = cast<ConstantSDNode>(Node->getOperand(0))->getZExtValue();
  const TargetRegisterClass *RC = TRI->getRegClass(DstRCIdx);
  unsigned NewVReg = MRI->createVirtualRegister(TRI->getAllocatableClass(RC));
  const MCInstrDesc &II = TII->get(TargetOpcode::REG_SEQUENCE);
  MachineInstrBuilder MIB = BuildMI(*MF, Node->getDebugLoc(), II, NewVReg);
  unsigned NumOps = Node->getNumOperands();
  // If the input pattern has a chain, then the root of the corresponding
  // output pattern will get a chain as well. This can happen to be a
  // REG_SEQUENCE (which is not "guarded" by countOperands/CountResults).
  if (NumOps && Node->getOperand(NumOps-1).getValueType() == MVT::Other)
    --NumOps; // Ignore chain if it exists.

  assert((NumOps & 1) == 1 &&
         "REG_SEQUENCE must have an odd number of operands!");
  for (unsigned i = 1; i != NumOps; ++i) {
    SDValue Op = Node->getOperand(i);
    if ((i & 1) == 0) {
      RegisterSDNode *R = dyn_cast<RegisterSDNode>(Node->getOperand(i-1));
      // Skip physical registers as they don't have a vreg to get and we'll
      // insert copies for them in TwoAddressInstructionPass anyway.
      if (!R || !TargetRegisterInfo::isPhysicalRegister(R->getReg())) {
        unsigned SubIdx = cast<ConstantSDNode>(Op)->getZExtValue();
        unsigned SubReg = getVR(Node->getOperand(i-1), VRBaseMap);
        const TargetRegisterClass *TRC = MRI->getRegClass(SubReg);
        const TargetRegisterClass *SRC =
        TRI->getMatchingSuperRegClass(RC, TRC, SubIdx);
        if (SRC && SRC != RC) {
          MRI->setRegClass(NewVReg, SRC);
          RC = SRC;
        }
      }
    }
    AddOperand(MIB, Op, i+1, &II, VRBaseMap, /*IsDebug=*/false,
               IsClone, IsCloned);
  }

  MBB->insert(InsertPos, MIB);
  SDValue Op(Node, 0);
  bool isNew = VRBaseMap.insert(std::make_pair(Op, NewVReg)).second;
  (void)isNew; // Silence compiler warning.
  assert(isNew && "Node emitted out of order - early");
}

/// EmitDbgValue - Generate machine instruction for a dbg_value node.
///
MachineInstr *
InstrEmitter::EmitDbgValue(SDDbgValue *SD,
                           DenseMap<SDValue, unsigned> &VRBaseMap) {
  MDNode *Var = SD->getVariable();
  MDNode *Expr = SD->getExpression();
  DebugLoc DL = SD->getDebugLoc();
  assert(cast<DILocalVariable>(Var)->isValidLocationForIntrinsic(DL) &&
         "Expected inlined-at fields to agree");

  SD->setIsEmitted();

  if (SD->isInvalidated()) {
    // An invalidated SDNode must generate an undef DBG_VALUE: although the
    // original value is no longer computed, earlier DBG_VALUEs live ranges
    // must not leak into later code.
    auto MIB = BuildMI(*MF, DL, TII->get(TargetOpcode::DBG_VALUE));
    MIB.addReg(0U);
    MIB.addReg(0U, RegState::Debug);
    MIB.addMetadata(Var);
    MIB.addMetadata(Expr);
    return &*MIB;
  }

  if (SD->getKind() == SDDbgValue::FRAMEIX) {
    // Stack address; this needs to be lowered in target-dependent fashion.
    // EmitTargetCodeForFrameDebugValue is responsible for allocation.
    auto FrameMI = BuildMI(*MF, DL, TII->get(TargetOpcode::DBG_VALUE))
                       .addFrameIndex(SD->getFrameIx());
    if (SD->isIndirect())
      // Push [fi + 0] onto the DIExpression stack.
      FrameMI.addImm(0);
    else
      // Push fi onto the DIExpression stack.
      FrameMI.addReg(0);
    return FrameMI.addMetadata(Var).addMetadata(Expr);
  }
  // Otherwise, we're going to create an instruction here.
  const MCInstrDesc &II = TII->get(TargetOpcode::DBG_VALUE);
  MachineInstrBuilder MIB = BuildMI(*MF, DL, II);
  if (SD->getKind() == SDDbgValue::SDNODE) {
    SDNode *Node = SD->getSDNode();
    SDValue Op = SDValue(Node, SD->getResNo());
    // It's possible we replaced this SDNode with other(s) and therefore
    // didn't generate code for it.  It's better to catch these cases where
    // they happen and transfer the debug info, but trying to guarantee that
    // in all cases would be very fragile; this is a safeguard for any
    // that were missed.
    DenseMap<SDValue, unsigned>::iterator I = VRBaseMap.find(Op);
    if (I==VRBaseMap.end())
      MIB.addReg(0U);       // undef
    else
      AddOperand(MIB, Op, (*MIB).getNumOperands(), &II, VRBaseMap,
                 /*IsDebug=*/true, /*IsClone=*/false, /*IsCloned=*/false);
  } else if (SD->getKind() == SDDbgValue::VREG) {
    MIB.addReg(SD->getVReg(), RegState::Debug);
  } else if (SD->getKind() == SDDbgValue::CONST) {
    const Value *V = SD->getConst();
    if (const ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
      if (CI->getBitWidth() > 64)
        MIB.addCImm(CI);
      else
        MIB.addImm(CI->getSExtValue());
    } else if (const ConstantFP *CF = dyn_cast<ConstantFP>(V)) {
      MIB.addFPImm(CF);
    } else if (isa<ConstantPointerNull>(V)) {
      // Note: This assumes that all nullptr constants are zero-valued.
      MIB.addImm(0);
    } else {
      // Could be an Undef.  In any case insert an Undef so we can see what we
      // dropped.
      MIB.addReg(0U);
    }
  } else {
    // Insert an Undef so we can see what we dropped.
    MIB.addReg(0U);
  }

  // Indirect addressing is indicated by an Imm as the second parameter.
  if (SD->isIndirect())
    MIB.addImm(0U);
  else
    MIB.addReg(0U, RegState::Debug);

  MIB.addMetadata(Var);
  MIB.addMetadata(Expr);

  return &*MIB;
}

MachineInstr *
InstrEmitter::EmitDbgLabel(SDDbgLabel *SD) {
  MDNode *Label = SD->getLabel();
  DebugLoc DL = SD->getDebugLoc();
  assert(cast<DILabel>(Label)->isValidLocationForIntrinsic(DL) &&
         "Expected inlined-at fields to agree");

  const MCInstrDesc &II = TII->get(TargetOpcode::DBG_LABEL);
  MachineInstrBuilder MIB = BuildMI(*MF, DL, II);
  MIB.addMetadata(Label);

  return &*MIB;
}

/// EmitMachineNode - Generate machine code for a target-specific node and
/// needed dependencies.
///
void InstrEmitter::
EmitMachineNode(SDNode *Node, bool IsClone, bool IsCloned,
                DenseMap<SDValue, unsigned> &VRBaseMap) {
  unsigned Opc = Node->getMachineOpcode();

  // Handle subreg insert/extract specially
  if (Opc == TargetOpcode::EXTRACT_SUBREG ||
      Opc == TargetOpcode::INSERT_SUBREG ||
      Opc == TargetOpcode::SUBREG_TO_REG) {
    EmitSubregNode(Node, VRBaseMap, IsClone, IsCloned);
    return;
  }

  // Handle COPY_TO_REGCLASS specially.
  if (Opc == TargetOpcode::COPY_TO_REGCLASS) {
    EmitCopyToRegClassNode(Node, VRBaseMap);
    return;
  }

  // Handle REG_SEQUENCE specially.
  if (Opc == TargetOpcode::REG_SEQUENCE) {
    EmitRegSequence(Node, VRBaseMap, IsClone, IsCloned);
    return;
  }

  if (Opc == TargetOpcode::IMPLICIT_DEF)
    // We want a unique VR for each IMPLICIT_DEF use.
    return;

  const MCInstrDesc &II = TII->get(Opc);
  unsigned NumResults = CountResults(Node);
  unsigned NumDefs = II.getNumDefs();
  const MCPhysReg *ScratchRegs = nullptr;

  // Handle STACKMAP and PATCHPOINT specially and then use the generic code.
  if (Opc == TargetOpcode::STACKMAP || Opc == TargetOpcode::PATCHPOINT) {
    // Stackmaps do not have arguments and do not preserve their calling
    // convention. However, to simplify runtime support, they clobber the same
    // scratch registers as AnyRegCC.
    unsigned CC = CallingConv::AnyReg;
    if (Opc == TargetOpcode::PATCHPOINT) {
      CC = Node->getConstantOperandVal(PatchPointOpers::CCPos);
      NumDefs = NumResults;
    }
    ScratchRegs = TLI->getScratchRegisters((CallingConv::ID) CC);
  }

  unsigned NumImpUses = 0;
  unsigned NodeOperands =
    countOperands(Node, II.getNumOperands() - NumDefs, NumImpUses);
  bool HasPhysRegOuts = NumResults > NumDefs && II.getImplicitDefs()!=nullptr;
#ifndef NDEBUG
  unsigned NumMIOperands = NodeOperands + NumResults;
  if (II.isVariadic())
    assert(NumMIOperands >= II.getNumOperands() &&
           "Too few operands for a variadic node!");
  else
    assert(NumMIOperands >= II.getNumOperands() &&
           NumMIOperands <= II.getNumOperands() + II.getNumImplicitDefs() +
                            NumImpUses &&
           "#operands for dag node doesn't match .td file!");
#endif

  // Create the new machine instruction.
  MachineInstrBuilder MIB = BuildMI(*MF, Node->getDebugLoc(), II);

  // Add result register values for things that are defined by this
  // instruction.
  if (NumResults) {
    CreateVirtualRegisters(Node, MIB, II, IsClone, IsCloned, VRBaseMap);

    // Transfer any IR flags from the SDNode to the MachineInstr
    MachineInstr *MI = MIB.getInstr();
    const SDNodeFlags Flags = Node->getFlags();
    if (Flags.hasNoSignedZeros())
      MI->setFlag(MachineInstr::MIFlag::FmNsz);

    if (Flags.hasAllowReciprocal())
      MI->setFlag(MachineInstr::MIFlag::FmArcp);

    if (Flags.hasNoNaNs())
      MI->setFlag(MachineInstr::MIFlag::FmNoNans);

    if (Flags.hasNoInfs())
      MI->setFlag(MachineInstr::MIFlag::FmNoInfs);

    if (Flags.hasAllowContract())
      MI->setFlag(MachineInstr::MIFlag::FmContract);

    if (Flags.hasApproximateFuncs())
      MI->setFlag(MachineInstr::MIFlag::FmAfn);

    if (Flags.hasAllowReassociation())
      MI->setFlag(MachineInstr::MIFlag::FmReassoc);

    if (Flags.hasNoUnsignedWrap())
      MI->setFlag(MachineInstr::MIFlag::NoUWrap);

    if (Flags.hasNoSignedWrap())
      MI->setFlag(MachineInstr::MIFlag::NoSWrap);

    if (Flags.hasExact())
      MI->setFlag(MachineInstr::MIFlag::IsExact);
  }

  // Emit all of the actual operands of this instruction, adding them to the
  // instruction as appropriate.
  bool HasOptPRefs = NumDefs > NumResults;
  assert((!HasOptPRefs || !HasPhysRegOuts) &&
         "Unable to cope with optional defs and phys regs defs!");
  unsigned NumSkip = HasOptPRefs ? NumDefs - NumResults : 0;
  for (unsigned i = NumSkip; i != NodeOperands; ++i)
    AddOperand(MIB, Node->getOperand(i), i-NumSkip+NumDefs, &II,
               VRBaseMap, /*IsDebug=*/false, IsClone, IsCloned);

  // Add scratch registers as implicit def and early clobber
  if (ScratchRegs)
    for (unsigned i = 0; ScratchRegs[i]; ++i)
      MIB.addReg(ScratchRegs[i], RegState::ImplicitDefine |
                                 RegState::EarlyClobber);

  // Set the memory reference descriptions of this instruction now that it is
  // part of the function.
  MIB.setMemRefs(cast<MachineSDNode>(Node)->memoperands());

  // Insert the instruction into position in the block. This needs to
  // happen before any custom inserter hook is called so that the
  // hook knows where in the block to insert the replacement code.
  MBB->insert(InsertPos, MIB);

  // The MachineInstr may also define physregs instead of virtregs.  These
  // physreg values can reach other instructions in different ways:
  //
  // 1. When there is a use of a Node value beyond the explicitly defined
  //    virtual registers, we emit a CopyFromReg for one of the implicitly
  //    defined physregs.  This only happens when HasPhysRegOuts is true.
  //
  // 2. A CopyFromReg reading a physreg may be glued to this instruction.
  //
  // 3. A glued instruction may implicitly use a physreg.
  //
  // 4. A glued instruction may use a RegisterSDNode operand.
  //
  // Collect all the used physreg defs, and make sure that any unused physreg
  // defs are marked as dead.
  SmallVector<unsigned, 8> UsedRegs;

  // Additional results must be physical register defs.
  if (HasPhysRegOuts) {
    for (unsigned i = NumDefs; i < NumResults; ++i) {
      unsigned Reg = II.getImplicitDefs()[i - NumDefs];
      if (!Node->hasAnyUseOfValue(i))
        continue;
      // This implicitly defined physreg has a use.
      UsedRegs.push_back(Reg);
      EmitCopyFromReg(Node, i, IsClone, IsCloned, Reg, VRBaseMap);
    }
  }

  // Scan the glue chain for any used physregs.
  if (Node->getValueType(Node->getNumValues()-1) == MVT::Glue) {
    for (SDNode *F = Node->getGluedUser(); F; F = F->getGluedUser()) {
      if (F->getOpcode() == ISD::CopyFromReg) {
        UsedRegs.push_back(cast<RegisterSDNode>(F->getOperand(1))->getReg());
        continue;
      } else if (F->getOpcode() == ISD::CopyToReg) {
        // Skip CopyToReg nodes that are internal to the glue chain.
        continue;
      }
      // Collect declared implicit uses.
      const MCInstrDesc &MCID = TII->get(F->getMachineOpcode());
      UsedRegs.append(MCID.getImplicitUses(),
                      MCID.getImplicitUses() + MCID.getNumImplicitUses());
      // In addition to declared implicit uses, we must also check for
      // direct RegisterSDNode operands.
      for (unsigned i = 0, e = F->getNumOperands(); i != e; ++i)
        if (RegisterSDNode *R = dyn_cast<RegisterSDNode>(F->getOperand(i))) {
          unsigned Reg = R->getReg();
          if (TargetRegisterInfo::isPhysicalRegister(Reg))
            UsedRegs.push_back(Reg);
        }
    }
  }

  // Finally mark unused registers as dead.
  if (!UsedRegs.empty() || II.getImplicitDefs() || II.hasOptionalDef())
    MIB->setPhysRegsDeadExcept(UsedRegs, *TRI);

  // Run post-isel target hook to adjust this instruction if needed.
  if (II.hasPostISelHook())
    TLI->AdjustInstrPostInstrSelection(*MIB, Node);
}

/// EmitSpecialNode - Generate machine code for a target-independent node and
/// needed dependencies.
void InstrEmitter::
EmitSpecialNode(SDNode *Node, bool IsClone, bool IsCloned,
                DenseMap<SDValue, unsigned> &VRBaseMap) {
  switch (Node->getOpcode()) {
  default:
#ifndef NDEBUG
    Node->dump();
#endif
    llvm_unreachable("This target-independent node should have been selected!");
  case ISD::EntryToken:
    llvm_unreachable("EntryToken should have been excluded from the schedule!");
  case ISD::MERGE_VALUES:
  case ISD::TokenFactor: // fall thru
    break;
  case ISD::CopyToReg: {
    unsigned SrcReg;
    SDValue SrcVal = Node->getOperand(2);
    if (RegisterSDNode *R = dyn_cast<RegisterSDNode>(SrcVal))
      SrcReg = R->getReg();
    else
      SrcReg = getVR(SrcVal, VRBaseMap);

    unsigned DestReg = cast<RegisterSDNode>(Node->getOperand(1))->getReg();
    if (SrcReg == DestReg) // Coalesced away the copy? Ignore.
      break;

    BuildMI(*MBB, InsertPos, Node->getDebugLoc(), TII->get(TargetOpcode::COPY),
            DestReg).addReg(SrcReg);
    break;
  }
  case ISD::CopyFromReg: {
    unsigned SrcReg = cast<RegisterSDNode>(Node->getOperand(1))->getReg();
    EmitCopyFromReg(Node, 0, IsClone, IsCloned, SrcReg, VRBaseMap);
    break;
  }
  case ISD::EH_LABEL:
  case ISD::ANNOTATION_LABEL: {
    unsigned Opc = (Node->getOpcode() == ISD::EH_LABEL)
                       ? TargetOpcode::EH_LABEL
                       : TargetOpcode::ANNOTATION_LABEL;
    MCSymbol *S = cast<LabelSDNode>(Node)->getLabel();
    BuildMI(*MBB, InsertPos, Node->getDebugLoc(),
            TII->get(Opc)).addSym(S);
    break;
  }

  case ISD::LIFETIME_START:
  case ISD::LIFETIME_END: {
    unsigned TarOp = (Node->getOpcode() == ISD::LIFETIME_START) ?
    TargetOpcode::LIFETIME_START : TargetOpcode::LIFETIME_END;

    FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(Node->getOperand(1));
    BuildMI(*MBB, InsertPos, Node->getDebugLoc(), TII->get(TarOp))
    .addFrameIndex(FI->getIndex());
    break;
  }

  case ISD::INLINEASM: {
    unsigned NumOps = Node->getNumOperands();
    if (Node->getOperand(NumOps-1).getValueType() == MVT::Glue)
      --NumOps;  // Ignore the glue operand.

    // Create the inline asm machine instruction.
    MachineInstrBuilder MIB = BuildMI(*MF, Node->getDebugLoc(),
                                      TII->get(TargetOpcode::INLINEASM));

    // Add the asm string as an external symbol operand.
    SDValue AsmStrV = Node->getOperand(InlineAsm::Op_AsmString);
    const char *AsmStr = cast<ExternalSymbolSDNode>(AsmStrV)->getSymbol();
    MIB.addExternalSymbol(AsmStr);

    // Add the HasSideEffect, isAlignStack, AsmDialect, MayLoad and MayStore
    // bits.
    int64_t ExtraInfo =
      cast<ConstantSDNode>(Node->getOperand(InlineAsm::Op_ExtraInfo))->
                          getZExtValue();
    MIB.addImm(ExtraInfo);

    // Remember to operand index of the group flags.
    SmallVector<unsigned, 8> GroupIdx;

    // Remember registers that are part of early-clobber defs.
    SmallVector<unsigned, 8> ECRegs;

    // Add all of the operand registers to the instruction.
    for (unsigned i = InlineAsm::Op_FirstOperand; i != NumOps;) {
      unsigned Flags =
        cast<ConstantSDNode>(Node->getOperand(i))->getZExtValue();
      const unsigned NumVals = InlineAsm::getNumOperandRegisters(Flags);

      GroupIdx.push_back(MIB->getNumOperands());
      MIB.addImm(Flags);
      ++i;  // Skip the ID value.

      switch (InlineAsm::getKind(Flags)) {
      default: llvm_unreachable("Bad flags!");
        case InlineAsm::Kind_RegDef:
        for (unsigned j = 0; j != NumVals; ++j, ++i) {
          unsigned Reg = cast<RegisterSDNode>(Node->getOperand(i))->getReg();
          // FIXME: Add dead flags for physical and virtual registers defined.
          // For now, mark physical register defs as implicit to help fast
          // regalloc. This makes inline asm look a lot like calls.
          MIB.addReg(Reg, RegState::Define |
                  getImplRegState(TargetRegisterInfo::isPhysicalRegister(Reg)));
        }
        break;
      case InlineAsm::Kind_RegDefEarlyClobber:
      case InlineAsm::Kind_Clobber:
        for (unsigned j = 0; j != NumVals; ++j, ++i) {
          unsigned Reg = cast<RegisterSDNode>(Node->getOperand(i))->getReg();
          MIB.addReg(Reg, RegState::Define | RegState::EarlyClobber |
                  getImplRegState(TargetRegisterInfo::isPhysicalRegister(Reg)));
          ECRegs.push_back(Reg);
        }
        break;
      case InlineAsm::Kind_RegUse:  // Use of register.
      case InlineAsm::Kind_Imm:  // Immediate.
      case InlineAsm::Kind_Mem:  // Addressing mode.
        // The addressing mode has been selected, just add all of the
        // operands to the machine instruction.
        for (unsigned j = 0; j != NumVals; ++j, ++i)
          AddOperand(MIB, Node->getOperand(i), 0, nullptr, VRBaseMap,
                     /*IsDebug=*/false, IsClone, IsCloned);

        // Manually set isTied bits.
        if (InlineAsm::getKind(Flags) == InlineAsm::Kind_RegUse) {
          unsigned DefGroup = 0;
          if (InlineAsm::isUseOperandTiedToDef(Flags, DefGroup)) {
            unsigned DefIdx = GroupIdx[DefGroup] + 1;
            unsigned UseIdx = GroupIdx.back() + 1;
            for (unsigned j = 0; j != NumVals; ++j)
              MIB->tieOperands(DefIdx + j, UseIdx + j);
          }
        }
        break;
      }
    }

    // GCC inline assembly allows input operands to also be early-clobber
    // output operands (so long as the operand is written only after it's
    // used), but this does not match the semantics of our early-clobber flag.
    // If an early-clobber operand register is also an input operand register,
    // then remove the early-clobber flag.
    for (unsigned Reg : ECRegs) {
      if (MIB->readsRegister(Reg, TRI)) {
        MachineOperand *MO = MIB->findRegisterDefOperand(Reg, false, TRI);
        assert(MO && "No def operand for clobbered register?");
        MO->setIsEarlyClobber(false);
      }
    }

    // Get the mdnode from the asm if it exists and add it to the instruction.
    SDValue MDV = Node->getOperand(InlineAsm::Op_MDNode);
    const MDNode *MD = cast<MDNodeSDNode>(MDV)->getMD();
    if (MD)
      MIB.addMetadata(MD);

    MBB->insert(InsertPos, MIB);
    break;
  }
  }
}

/// InstrEmitter - Construct an InstrEmitter and set it to start inserting
/// at the given position in the given block.
InstrEmitter::InstrEmitter(MachineBasicBlock *mbb,
                           MachineBasicBlock::iterator insertpos)
    : MF(mbb->getParent()), MRI(&MF->getRegInfo()),
      TII(MF->getSubtarget().getInstrInfo()),
      TRI(MF->getSubtarget().getRegisterInfo()),
      TLI(MF->getSubtarget().getTargetLowering()), MBB(mbb),
      InsertPos(insertpos) {}
