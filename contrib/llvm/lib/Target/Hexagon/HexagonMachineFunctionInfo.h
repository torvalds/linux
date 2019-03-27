//=- HexagonMachineFunctionInfo.h - Hexagon machine function info -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"
#include <map>

namespace llvm {

namespace Hexagon {

    const unsigned int StartPacket = 0x1;
    const unsigned int EndPacket = 0x2;

} // end namespace Hexagon

/// Hexagon target-specific information for each MachineFunction.
class HexagonMachineFunctionInfo : public MachineFunctionInfo {
  // SRetReturnReg - Some subtargets require that sret lowering includes
  // returning the value of the returned struct in a register. This field
  // holds the virtual register into which the sret argument is passed.
  unsigned SRetReturnReg = 0;
  unsigned StackAlignBaseVReg = 0;    // Aligned-stack base register (virtual)
  unsigned StackAlignBasePhysReg = 0; //                             (physical)
  int VarArgsFrameIndex;
  bool HasClobberLR = false;
  bool HasEHReturn = false;
  std::map<const MachineInstr*, unsigned> PacketInfo;
  virtual void anchor();

public:
  HexagonMachineFunctionInfo() = default;

  HexagonMachineFunctionInfo(MachineFunction &MF) {}

  unsigned getSRetReturnReg() const { return SRetReturnReg; }
  void setSRetReturnReg(unsigned Reg) { SRetReturnReg = Reg; }

  void setVarArgsFrameIndex(int v) { VarArgsFrameIndex = v; }
  int getVarArgsFrameIndex() { return VarArgsFrameIndex; }

  void setStartPacket(MachineInstr* MI) {
    PacketInfo[MI] |= Hexagon::StartPacket;
  }
  void setEndPacket(MachineInstr* MI)   {
    PacketInfo[MI] |= Hexagon::EndPacket;
  }
  bool isStartPacket(const MachineInstr* MI) const {
    return (PacketInfo.count(MI) &&
            (PacketInfo.find(MI)->second & Hexagon::StartPacket));
  }
  bool isEndPacket(const MachineInstr* MI) const {
    return (PacketInfo.count(MI) &&
            (PacketInfo.find(MI)->second & Hexagon::EndPacket));
  }
  void setHasClobberLR(bool v) { HasClobberLR = v;  }
  bool hasClobberLR() const { return HasClobberLR; }

  bool hasEHReturn() const { return HasEHReturn; };
  void setHasEHReturn(bool H = true) { HasEHReturn = H; };

  void setStackAlignBaseVReg(unsigned R) { StackAlignBaseVReg = R; }
  unsigned getStackAlignBaseVReg() const { return StackAlignBaseVReg; }

  void setStackAlignBasePhysReg(unsigned R) { StackAlignBasePhysReg = R; }
  unsigned getStackAlignBasePhysReg() const { return StackAlignBasePhysReg; }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONMACHINEFUNCTIONINFO_H
