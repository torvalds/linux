//==- llvm/CodeGen/AggressiveAntiDepBreaker.h - Anti-Dep Support -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the AggressiveAntiDepBreaker class, which
// implements register anti-dependence breaking during post-RA
// scheduling. It attempts to break all anti-dependencies within a
// block.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_AGGRESSIVEANTIDEPBREAKER_H
#define LLVM_LIB_CODEGEN_AGGRESSIVEANTIDEPBREAKER_H

#include "AntiDepBreaker.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/Compiler.h"
#include <map>
#include <set>
#include <vector>

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MachineOperand;
class MachineRegisterInfo;
class RegisterClassInfo;
class TargetInstrInfo;
class TargetRegisterClass;
class TargetRegisterInfo;

  /// Contains all the state necessary for anti-dep breaking.
class LLVM_LIBRARY_VISIBILITY AggressiveAntiDepState {
  public:
    /// Information about a register reference within a liverange
    struct RegisterReference {
      /// The registers operand
      MachineOperand *Operand;

      /// The register class
      const TargetRegisterClass *RC;
    };

  private:
    /// Number of non-virtual target registers (i.e. TRI->getNumRegs()).
    const unsigned NumTargetRegs;

    /// Implements a disjoint-union data structure to
    /// form register groups. A node is represented by an index into
    /// the vector. A node can "point to" itself to indicate that it
    /// is the parent of a group, or point to another node to indicate
    /// that it is a member of the same group as that node.
    std::vector<unsigned> GroupNodes;

    /// For each register, the index of the GroupNode
    /// currently representing the group that the register belongs to.
    /// Register 0 is always represented by the 0 group, a group
    /// composed of registers that are not eligible for anti-aliasing.
    std::vector<unsigned> GroupNodeIndices;

    /// Map registers to all their references within a live range.
    std::multimap<unsigned, RegisterReference> RegRefs;

    /// The index of the most recent kill (proceeding bottom-up),
    /// or ~0u if the register is not live.
    std::vector<unsigned> KillIndices;

    /// The index of the most recent complete def (proceeding bottom
    /// up), or ~0u if the register is live.
    std::vector<unsigned> DefIndices;

  public:
    AggressiveAntiDepState(const unsigned TargetRegs, MachineBasicBlock *BB);

    /// Return the kill indices.
    std::vector<unsigned> &GetKillIndices() { return KillIndices; }

    /// Return the define indices.
    std::vector<unsigned> &GetDefIndices() { return DefIndices; }

    /// Return the RegRefs map.
    std::multimap<unsigned, RegisterReference>& GetRegRefs() { return RegRefs; }

    // Get the group for a register. The returned value is
    // the index of the GroupNode representing the group.
    unsigned GetGroup(unsigned Reg);

    // Return a vector of the registers belonging to a group.
    // If RegRefs is non-NULL then only included referenced registers.
    void GetGroupRegs(
       unsigned Group,
       std::vector<unsigned> &Regs,
       std::multimap<unsigned,
         AggressiveAntiDepState::RegisterReference> *RegRefs);

    // Union Reg1's and Reg2's groups to form a new group.
    // Return the index of the GroupNode representing the group.
    unsigned UnionGroups(unsigned Reg1, unsigned Reg2);

    // Remove a register from its current group and place
    // it alone in its own group. Return the index of the GroupNode
    // representing the registers new group.
    unsigned LeaveGroup(unsigned Reg);

    /// Return true if Reg is live.
    bool IsLive(unsigned Reg);
  };

  class LLVM_LIBRARY_VISIBILITY AggressiveAntiDepBreaker
      : public AntiDepBreaker {
    MachineFunction &MF;
    MachineRegisterInfo &MRI;
    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    const RegisterClassInfo &RegClassInfo;

    /// The set of registers that should only be
    /// renamed if they are on the critical path.
    BitVector CriticalPathSet;

    /// The state used to identify and rename anti-dependence registers.
    AggressiveAntiDepState *State = nullptr;

  public:
    AggressiveAntiDepBreaker(MachineFunction &MFi,
                          const RegisterClassInfo &RCI,
                          TargetSubtargetInfo::RegClassVector& CriticalPathRCs);
    ~AggressiveAntiDepBreaker() override;

    /// Initialize anti-dep breaking for a new basic block.
    void StartBlock(MachineBasicBlock *BB) override;

    /// Identifiy anti-dependencies along the critical path
    /// of the ScheduleDAG and break them by renaming registers.
    unsigned BreakAntiDependencies(const std::vector<SUnit> &SUnits,
                                   MachineBasicBlock::iterator Begin,
                                   MachineBasicBlock::iterator End,
                                   unsigned InsertPosIndex,
                                   DbgValueVector &DbgValues) override;

    /// Update liveness information to account for the current
    /// instruction, which will not be scheduled.
    void Observe(MachineInstr &MI, unsigned Count,
                 unsigned InsertPosIndex) override;

    /// Finish anti-dep breaking for a basic block.
    void FinishBlock() override;

  private:
    /// Keep track of a position in the allocation order for each regclass.
    using RenameOrderType = std::map<const TargetRegisterClass *, unsigned>;

    /// Return true if MO represents a register
    /// that is both implicitly used and defined in MI
    bool IsImplicitDefUse(MachineInstr &MI, MachineOperand &MO);

    /// If MI implicitly def/uses a register, then
    /// return that register and all subregisters.
    void GetPassthruRegs(MachineInstr &MI, std::set<unsigned> &PassthruRegs);

    void HandleLastUse(unsigned Reg, unsigned KillIdx, const char *tag,
                       const char *header = nullptr,
                       const char *footer = nullptr);

    void PrescanInstruction(MachineInstr &MI, unsigned Count,
                            std::set<unsigned> &PassthruRegs);
    void ScanInstruction(MachineInstr &MI, unsigned Count);
    BitVector GetRenameRegisters(unsigned Reg);
    bool FindSuitableFreeRegisters(unsigned AntiDepGroupIndex,
                                   RenameOrderType& RenameOrder,
                                   std::map<unsigned, unsigned> &RenameMap);
  };

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_AGGRESSIVEANTIDEPBREAKER_H
