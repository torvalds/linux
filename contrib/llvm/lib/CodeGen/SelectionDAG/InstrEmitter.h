//===- InstrEmitter.h - Emit MachineInstrs for the SelectionDAG -*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This declares the Emit routines for the SelectionDAG class, which creates
// MachineInstrs based on the decisions of the SelectionDAG instruction
// selection.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_SELECTIONDAG_INSTREMITTER_H
#define LLVM_LIB_CODEGEN_SELECTIONDAG_INSTREMITTER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/SelectionDAG.h"

namespace llvm {

class MachineInstrBuilder;
class MCInstrDesc;
class SDDbgValue;

class LLVM_LIBRARY_VISIBILITY InstrEmitter {
  MachineFunction *MF;
  MachineRegisterInfo *MRI;
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  const TargetLowering *TLI;

  MachineBasicBlock *MBB;
  MachineBasicBlock::iterator InsertPos;

  /// EmitCopyFromReg - Generate machine code for an CopyFromReg node or an
  /// implicit physical register output.
  void EmitCopyFromReg(SDNode *Node, unsigned ResNo,
                       bool IsClone, bool IsCloned,
                       unsigned SrcReg,
                       DenseMap<SDValue, unsigned> &VRBaseMap);

  /// getDstOfCopyToRegUse - If the only use of the specified result number of
  /// node is a CopyToReg, return its destination register. Return 0 otherwise.
  unsigned getDstOfOnlyCopyToRegUse(SDNode *Node,
                                    unsigned ResNo) const;

  void CreateVirtualRegisters(SDNode *Node,
                              MachineInstrBuilder &MIB,
                              const MCInstrDesc &II,
                              bool IsClone, bool IsCloned,
                              DenseMap<SDValue, unsigned> &VRBaseMap);

  /// getVR - Return the virtual register corresponding to the specified result
  /// of the specified node.
  unsigned getVR(SDValue Op,
                 DenseMap<SDValue, unsigned> &VRBaseMap);

  /// AddRegisterOperand - Add the specified register as an operand to the
  /// specified machine instr. Insert register copies if the register is
  /// not in the required register class.
  void AddRegisterOperand(MachineInstrBuilder &MIB,
                          SDValue Op,
                          unsigned IIOpNum,
                          const MCInstrDesc *II,
                          DenseMap<SDValue, unsigned> &VRBaseMap,
                          bool IsDebug, bool IsClone, bool IsCloned);

  /// AddOperand - Add the specified operand to the specified machine instr.  II
  /// specifies the instruction information for the node, and IIOpNum is the
  /// operand number (in the II) that we are adding. IIOpNum and II are used for
  /// assertions only.
  void AddOperand(MachineInstrBuilder &MIB,
                  SDValue Op,
                  unsigned IIOpNum,
                  const MCInstrDesc *II,
                  DenseMap<SDValue, unsigned> &VRBaseMap,
                  bool IsDebug, bool IsClone, bool IsCloned);

  /// ConstrainForSubReg - Try to constrain VReg to a register class that
  /// supports SubIdx sub-registers.  Emit a copy if that isn't possible.
  /// Return the virtual register to use.
  unsigned ConstrainForSubReg(unsigned VReg, unsigned SubIdx, MVT VT,
                              const DebugLoc &DL);

  /// EmitSubregNode - Generate machine code for subreg nodes.
  ///
  void EmitSubregNode(SDNode *Node, DenseMap<SDValue, unsigned> &VRBaseMap,
                      bool IsClone, bool IsCloned);

  /// EmitCopyToRegClassNode - Generate machine code for COPY_TO_REGCLASS nodes.
  /// COPY_TO_REGCLASS is just a normal copy, except that the destination
  /// register is constrained to be in a particular register class.
  ///
  void EmitCopyToRegClassNode(SDNode *Node,
                              DenseMap<SDValue, unsigned> &VRBaseMap);

  /// EmitRegSequence - Generate machine code for REG_SEQUENCE nodes.
  ///
  void EmitRegSequence(SDNode *Node, DenseMap<SDValue, unsigned> &VRBaseMap,
                       bool IsClone, bool IsCloned);
public:
  /// CountResults - The results of target nodes have register or immediate
  /// operands first, then an optional chain, and optional flag operands
  /// (which do not go into the machine instrs.)
  static unsigned CountResults(SDNode *Node);

  /// EmitDbgValue - Generate machine instruction for a dbg_value node.
  ///
  MachineInstr *EmitDbgValue(SDDbgValue *SD,
                             DenseMap<SDValue, unsigned> &VRBaseMap);

  /// Generate machine instruction for a dbg_label node.
  MachineInstr *EmitDbgLabel(SDDbgLabel *SD);

  /// EmitNode - Generate machine code for a node and needed dependencies.
  ///
  void EmitNode(SDNode *Node, bool IsClone, bool IsCloned,
                DenseMap<SDValue, unsigned> &VRBaseMap) {
    if (Node->isMachineOpcode())
      EmitMachineNode(Node, IsClone, IsCloned, VRBaseMap);
    else
      EmitSpecialNode(Node, IsClone, IsCloned, VRBaseMap);
  }

  /// getBlock - Return the current basic block.
  MachineBasicBlock *getBlock() { return MBB; }

  /// getInsertPos - Return the current insertion position.
  MachineBasicBlock::iterator getInsertPos() { return InsertPos; }

  /// InstrEmitter - Construct an InstrEmitter and set it to start inserting
  /// at the given position in the given block.
  InstrEmitter(MachineBasicBlock *mbb, MachineBasicBlock::iterator insertpos);

private:
  void EmitMachineNode(SDNode *Node, bool IsClone, bool IsCloned,
                       DenseMap<SDValue, unsigned> &VRBaseMap);
  void EmitSpecialNode(SDNode *Node, bool IsClone, bool IsCloned,
                       DenseMap<SDValue, unsigned> &VRBaseMap);
};

}

#endif
