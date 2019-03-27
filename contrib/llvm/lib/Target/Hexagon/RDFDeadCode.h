//===--- RDFDeadCode.h ----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// RDF-based generic dead code elimination.
//
// The main interface of this class are functions "collect" and "erase".
// This allows custom processing of the function being optimized by a
// particular consumer. The simplest way to use this class would be to
// instantiate an object, and then simply call "collect" and "erase",
// passing the result of "getDeadInstrs()" to it.
// A more complex scenario would be to call "collect" first, then visit
// all post-increment instructions to see if the address update is dead
// or not, and if it is, convert the instruction to a non-updating form.
// After that "erase" can be called with the set of nodes including both,
// dead defs from the updating instructions and the nodes corresponding
// to the dead instructions.

#ifndef RDF_DEADCODE_H
#define RDF_DEADCODE_H

#include "RDFGraph.h"
#include "RDFLiveness.h"
#include "llvm/ADT/SetVector.h"

namespace llvm {
  class MachineRegisterInfo;

namespace rdf {
  struct DeadCodeElimination {
    DeadCodeElimination(DataFlowGraph &dfg, MachineRegisterInfo &mri)
      : Trace(false), DFG(dfg), MRI(mri), LV(mri, dfg) {}

    bool collect();
    bool erase(const SetVector<NodeId> &Nodes);
    void trace(bool On) { Trace = On; }
    bool trace() const { return Trace; }

    SetVector<NodeId> getDeadNodes() { return DeadNodes; }
    SetVector<NodeId> getDeadInstrs() { return DeadInstrs; }
    DataFlowGraph &getDFG() { return DFG; }

  private:
    bool Trace;
    SetVector<NodeId> LiveNodes;
    SetVector<NodeId> DeadNodes;
    SetVector<NodeId> DeadInstrs;
    DataFlowGraph &DFG;
    MachineRegisterInfo &MRI;
    Liveness LV;

    template<typename T> struct SetQueue;

    bool isLiveInstr(const MachineInstr *MI) const;
    void scanInstr(NodeAddr<InstrNode*> IA, SetQueue<NodeId> &WorkQ);
    void processDef(NodeAddr<DefNode*> DA, SetQueue<NodeId> &WorkQ);
    void processUse(NodeAddr<UseNode*> UA, SetQueue<NodeId> &WorkQ);
  };
} // namespace rdf
} // namespace llvm

#endif
