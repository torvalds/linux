//===- RDFCopy.h ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_RDFCOPY_H
#define LLVM_LIB_TARGET_HEXAGON_RDFCOPY_H

#include "RDFGraph.h"
#include "RDFLiveness.h"
#include "RDFRegisters.h"
#include "llvm/CodeGen/MachineFunction.h"
#include <map>
#include <vector>

namespace llvm {

class MachineBasicBlock;
class MachineDominatorTree;
class MachineInstr;

namespace rdf {

  struct CopyPropagation {
    CopyPropagation(DataFlowGraph &dfg) : MDT(dfg.getDT()), DFG(dfg),
        L(dfg.getMF().getRegInfo(), dfg) {}

    virtual ~CopyPropagation() = default;

    bool run();
    void trace(bool On) { Trace = On; }
    bool trace() const { return Trace; }
    DataFlowGraph &getDFG() { return DFG; }

    using EqualityMap = std::map<RegisterRef, RegisterRef>;

    virtual bool interpretAsCopy(const MachineInstr *MI, EqualityMap &EM);

  private:
    const MachineDominatorTree &MDT;
    DataFlowGraph &DFG;
    Liveness L;
    bool Trace = false;

    // map: statement -> (map: dst reg -> src reg)
    std::map<NodeId, EqualityMap> CopyMap;
    std::vector<NodeId> Copies;

    void recordCopy(NodeAddr<StmtNode*> SA, EqualityMap &EM);
    bool scanBlock(MachineBasicBlock *B);
    NodeId getLocalReachingDef(RegisterRef RefRR, NodeAddr<InstrNode*> IA);
  };

} // end namespace rdf

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_RDFCOPY_H
