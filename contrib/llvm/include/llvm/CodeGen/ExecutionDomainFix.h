//==-- llvm/CodeGen/ExecutionDomainFix.h - Execution Domain Fix -*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file Execution Domain Fix pass.
///
/// Some X86 SSE instructions like mov, and, or, xor are available in different
/// variants for different operand types. These variant instructions are
/// equivalent, but on Nehalem and newer cpus there is extra latency
/// transferring data between integer and floating point domains.  ARM cores
/// have similar issues when they are configured with both VFP and NEON
/// pipelines.
///
/// This pass changes the variant instructions to minimize domain crossings.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_EXECUTIONDOMAINFIX_H
#define LLVM_CODEGEN_EXECUTIONDOMAINFIX_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LoopTraversal.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/ReachingDefAnalysis.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

namespace llvm {

class MachineBasicBlock;
class MachineInstr;
class TargetInstrInfo;

/// A DomainValue is a bit like LiveIntervals' ValNo, but it also keeps track
/// of execution domains.
///
/// An open DomainValue represents a set of instructions that can still switch
/// execution domain. Multiple registers may refer to the same open
/// DomainValue - they will eventually be collapsed to the same execution
/// domain.
///
/// A collapsed DomainValue represents a single register that has been forced
/// into one of more execution domains. There is a separate collapsed
/// DomainValue for each register, but it may contain multiple execution
/// domains. A register value is initially created in a single execution
/// domain, but if we were forced to pay the penalty of a domain crossing, we
/// keep track of the fact that the register is now available in multiple
/// domains.
struct DomainValue {
  /// Basic reference counting.
  unsigned Refs = 0;

  /// Bitmask of available domains. For an open DomainValue, it is the still
  /// possible domains for collapsing. For a collapsed DomainValue it is the
  /// domains where the register is available for free.
  unsigned AvailableDomains;

  /// Pointer to the next DomainValue in a chain.  When two DomainValues are
  /// merged, Victim.Next is set to point to Victor, so old DomainValue
  /// references can be updated by following the chain.
  DomainValue *Next;

  /// Twiddleable instructions using or defining these registers.
  SmallVector<MachineInstr *, 8> Instrs;

  DomainValue() { clear(); }

  /// A collapsed DomainValue has no instructions to twiddle - it simply keeps
  /// track of the domains where the registers are already available.
  bool isCollapsed() const { return Instrs.empty(); }

  /// Is domain available?
  bool hasDomain(unsigned domain) const {
    assert(domain <
               static_cast<unsigned>(std::numeric_limits<unsigned>::digits) &&
           "undefined behavior");
    return AvailableDomains & (1u << domain);
  }

  /// Mark domain as available.
  void addDomain(unsigned domain) { AvailableDomains |= 1u << domain; }

  // Restrict to a single domain available.
  void setSingleDomain(unsigned domain) { AvailableDomains = 1u << domain; }

  /// Return bitmask of domains that are available and in mask.
  unsigned getCommonDomains(unsigned mask) const {
    return AvailableDomains & mask;
  }

  /// First domain available.
  unsigned getFirstDomain() const {
    return countTrailingZeros(AvailableDomains);
  }

  /// Clear this DomainValue and point to next which has all its data.
  void clear() {
    AvailableDomains = 0;
    Next = nullptr;
    Instrs.clear();
  }
};

class ExecutionDomainFix : public MachineFunctionPass {
  SpecificBumpPtrAllocator<DomainValue> Allocator;
  SmallVector<DomainValue *, 16> Avail;

  const TargetRegisterClass *const RC;
  MachineFunction *MF;
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  std::vector<SmallVector<int, 1>> AliasMap;
  const unsigned NumRegs;
  /// Value currently in each register, or NULL when no value is being tracked.
  /// This counts as a DomainValue reference.
  using LiveRegsDVInfo = std::vector<DomainValue *>;
  LiveRegsDVInfo LiveRegs;
  /// Keeps domain information for all registers. Note that this
  /// is different from the usual definition notion of liveness. The CPU
  /// doesn't care whether or not we consider a register killed.
  using OutRegsInfoMap = SmallVector<LiveRegsDVInfo, 4>;
  OutRegsInfoMap MBBOutRegsInfos;

  ReachingDefAnalysis *RDA;

public:
  ExecutionDomainFix(char &PassID, const TargetRegisterClass &RC)
      : MachineFunctionPass(PassID), RC(&RC), NumRegs(RC.getNumRegs()) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<ReachingDefAnalysis>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  /// Translate TRI register number to a list of indices into our smaller tables
  /// of interesting registers.
  iterator_range<SmallVectorImpl<int>::const_iterator>
  regIndices(unsigned Reg) const;

  /// DomainValue allocation.
  DomainValue *alloc(int domain = -1);

  /// Add reference to DV.
  DomainValue *retain(DomainValue *DV) {
    if (DV)
      ++DV->Refs;
    return DV;
  }

  /// Release a reference to DV.  When the last reference is released,
  /// collapse if needed.
  void release(DomainValue *);

  /// Follow the chain of dead DomainValues until a live DomainValue is reached.
  /// Update the referenced pointer when necessary.
  DomainValue *resolve(DomainValue *&);

  /// Set LiveRegs[rx] = dv, updating reference counts.
  void setLiveReg(int rx, DomainValue *DV);

  /// Kill register rx, recycle or collapse any DomainValue.
  void kill(int rx);

  /// Force register rx into domain.
  void force(int rx, unsigned domain);

  /// Collapse open DomainValue into given domain. If there are multiple
  /// registers using dv, they each get a unique collapsed DomainValue.
  void collapse(DomainValue *dv, unsigned domain);

  /// All instructions and registers in B are moved to A, and B is released.
  bool merge(DomainValue *A, DomainValue *B);

  /// Set up LiveRegs by merging predecessor live-out values.
  void enterBasicBlock(const LoopTraversal::TraversedMBBInfo &TraversedMBB);

  /// Update live-out values.
  void leaveBasicBlock(const LoopTraversal::TraversedMBBInfo &TraversedMBB);

  /// Process he given basic block.
  void processBasicBlock(const LoopTraversal::TraversedMBBInfo &TraversedMBB);

  /// Visit given insturcion.
  bool visitInstr(MachineInstr *);

  /// Update def-ages for registers defined by MI.
  /// If Kill is set, also kill off DomainValues clobbered by the defs.
  void processDefs(MachineInstr *, bool Kill);

  /// A soft instruction can be changed to work in other domains given by mask.
  void visitSoftInstr(MachineInstr *, unsigned mask);

  /// A hard instruction only works in one domain. All input registers will be
  /// forced into that domain.
  void visitHardInstr(MachineInstr *, unsigned domain);
};

} // namespace llvm

#endif // LLVM_CODEGEN_EXECUTIONDOMAINFIX_H
