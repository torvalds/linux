//===- RDFCopy.cpp --------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// RDF-based copy propagation.
//
//===----------------------------------------------------------------------===//

#include "RDFCopy.h"
#include "RDFGraph.h"
#include "RDFLiveness.h"
#include "RDFRegisters.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <utility>

using namespace llvm;
using namespace rdf;

#ifndef NDEBUG
static cl::opt<unsigned> CpLimit("rdf-cp-limit", cl::init(0), cl::Hidden);
static unsigned CpCount = 0;
#endif

bool CopyPropagation::interpretAsCopy(const MachineInstr *MI, EqualityMap &EM) {
  unsigned Opc = MI->getOpcode();
  switch (Opc) {
    case TargetOpcode::COPY: {
      const MachineOperand &Dst = MI->getOperand(0);
      const MachineOperand &Src = MI->getOperand(1);
      RegisterRef DstR = DFG.makeRegRef(Dst.getReg(), Dst.getSubReg());
      RegisterRef SrcR = DFG.makeRegRef(Src.getReg(), Src.getSubReg());
      assert(TargetRegisterInfo::isPhysicalRegister(DstR.Reg));
      assert(TargetRegisterInfo::isPhysicalRegister(SrcR.Reg));
      const TargetRegisterInfo &TRI = DFG.getTRI();
      if (TRI.getMinimalPhysRegClass(DstR.Reg) !=
          TRI.getMinimalPhysRegClass(SrcR.Reg))
        return false;
      EM.insert(std::make_pair(DstR, SrcR));
      return true;
    }
    case TargetOpcode::REG_SEQUENCE:
      llvm_unreachable("Unexpected REG_SEQUENCE");
  }
  return false;
}

void CopyPropagation::recordCopy(NodeAddr<StmtNode*> SA, EqualityMap &EM) {
  CopyMap.insert(std::make_pair(SA.Id, EM));
  Copies.push_back(SA.Id);
}

bool CopyPropagation::scanBlock(MachineBasicBlock *B) {
  bool Changed = false;
  NodeAddr<BlockNode*> BA = DFG.findBlock(B);

  for (NodeAddr<InstrNode*> IA : BA.Addr->members(DFG)) {
    if (DFG.IsCode<NodeAttrs::Stmt>(IA)) {
      NodeAddr<StmtNode*> SA = IA;
      EqualityMap EM;
      if (interpretAsCopy(SA.Addr->getCode(), EM))
        recordCopy(SA, EM);
    }
  }

  MachineDomTreeNode *N = MDT.getNode(B);
  for (auto I : *N)
    Changed |= scanBlock(I->getBlock());

  return Changed;
}

NodeId CopyPropagation::getLocalReachingDef(RegisterRef RefRR,
      NodeAddr<InstrNode*> IA) {
  NodeAddr<RefNode*> RA = L.getNearestAliasedRef(RefRR, IA);
  if (RA.Id != 0) {
    if (RA.Addr->getKind() == NodeAttrs::Def)
      return RA.Id;
    assert(RA.Addr->getKind() == NodeAttrs::Use);
    if (NodeId RD = RA.Addr->getReachingDef())
      return RD;
  }
  return 0;
}

bool CopyPropagation::run() {
  scanBlock(&DFG.getMF().front());

  if (trace()) {
    dbgs() << "Copies:\n";
    for (NodeId I : Copies) {
      dbgs() << "Instr: " << *DFG.addr<StmtNode*>(I).Addr->getCode();
      dbgs() << "   eq: {";
      for (auto J : CopyMap[I])
        dbgs() << ' ' << Print<RegisterRef>(J.first, DFG) << '='
               << Print<RegisterRef>(J.second, DFG);
      dbgs() << " }\n";
    }
  }

  bool Changed = false;
#ifndef NDEBUG
  bool HasLimit = CpLimit.getNumOccurrences() > 0;
#endif

  auto MinPhysReg = [this] (RegisterRef RR) -> unsigned {
    const TargetRegisterInfo &TRI = DFG.getTRI();
    const TargetRegisterClass &RC = *TRI.getMinimalPhysRegClass(RR.Reg);
    if ((RC.LaneMask & RR.Mask) == RC.LaneMask)
      return RR.Reg;
    for (MCSubRegIndexIterator S(RR.Reg, &TRI); S.isValid(); ++S)
      if (RR.Mask == TRI.getSubRegIndexLaneMask(S.getSubRegIndex()))
        return S.getSubReg();
    llvm_unreachable("Should have found a register");
    return 0;
  };

  for (NodeId C : Copies) {
#ifndef NDEBUG
    if (HasLimit && CpCount >= CpLimit)
      break;
#endif
    auto SA = DFG.addr<InstrNode*>(C);
    auto FS = CopyMap.find(SA.Id);
    if (FS == CopyMap.end())
      continue;

    EqualityMap &EM = FS->second;
    for (NodeAddr<DefNode*> DA : SA.Addr->members_if(DFG.IsDef, DFG)) {
      RegisterRef DR = DA.Addr->getRegRef(DFG);
      auto FR = EM.find(DR);
      if (FR == EM.end())
        continue;
      RegisterRef SR = FR->second;
      if (DR == SR)
        continue;

      NodeId AtCopy = getLocalReachingDef(SR, SA);

      for (NodeId N = DA.Addr->getReachedUse(), NextN; N; N = NextN) {
        auto UA = DFG.addr<UseNode*>(N);
        NextN = UA.Addr->getSibling();
        uint16_t F = UA.Addr->getFlags();
        if ((F & NodeAttrs::PhiRef) || (F & NodeAttrs::Fixed))
          continue;
        if (UA.Addr->getRegRef(DFG) != DR)
          continue;

        NodeAddr<InstrNode*> IA = UA.Addr->getOwner(DFG);
        assert(DFG.IsCode<NodeAttrs::Stmt>(IA));
        NodeId AtUse = getLocalReachingDef(SR, IA);
        if (AtCopy != AtUse)
          continue;

        MachineOperand &Op = UA.Addr->getOp();
        if (Op.isTied())
          continue;
        if (trace()) {
          dbgs() << "Can replace " << Print<RegisterRef>(DR, DFG)
                 << " with " << Print<RegisterRef>(SR, DFG) << " in "
                 << *NodeAddr<StmtNode*>(IA).Addr->getCode();
        }

        unsigned NewReg = MinPhysReg(SR);
        Op.setReg(NewReg);
        Op.setSubReg(0);
        DFG.unlinkUse(UA, false);
        if (AtCopy != 0) {
          UA.Addr->linkToDef(UA.Id, DFG.addr<DefNode*>(AtCopy));
        } else {
          UA.Addr->setReachingDef(0);
          UA.Addr->setSibling(0);
        }

        Changed = true;
  #ifndef NDEBUG
        if (HasLimit && CpCount >= CpLimit)
          break;
        CpCount++;
  #endif

        auto FC = CopyMap.find(IA.Id);
        if (FC != CopyMap.end()) {
          // Update the EM map in the copy's entry.
          auto &M = FC->second;
          for (auto &J : M) {
            if (J.second != DR)
              continue;
            J.second = SR;
            break;
          }
        }
      } // for (N in reached-uses)
    } // for (DA in defs)
  } // for (C in Copies)

  return Changed;
}
