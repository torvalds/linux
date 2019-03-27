//===-- ScheduleDAGPrinter.cpp - Implement ScheduleDAG::viewGraph() -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements the ScheduleDAG::viewGraph method.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace llvm {
  template<>
  struct DOTGraphTraits<ScheduleDAG*> : public DefaultDOTGraphTraits {

  DOTGraphTraits (bool isSimple=false) : DefaultDOTGraphTraits(isSimple) {}

    static std::string getGraphName(const ScheduleDAG *G) {
      return G->MF.getName();
    }

    static bool renderGraphFromBottomUp() {
      return true;
    }

    static bool isNodeHidden(const SUnit *Node) {
      return (Node->NumPreds > 10 || Node->NumSuccs > 10);
    }

    static std::string getNodeIdentifierLabel(const SUnit *Node,
                                              const ScheduleDAG *Graph) {
      std::string R;
      raw_string_ostream OS(R);
      OS << static_cast<const void *>(Node);
      return R;
    }

    /// If you want to override the dot attributes printed for a particular
    /// edge, override this method.
    static std::string getEdgeAttributes(const SUnit *Node,
                                         SUnitIterator EI,
                                         const ScheduleDAG *Graph) {
      if (EI.isArtificialDep())
        return "color=cyan,style=dashed";
      if (EI.isCtrlDep())
        return "color=blue,style=dashed";
      return "";
    }


    std::string getNodeLabel(const SUnit *SU, const ScheduleDAG *Graph);
    static std::string getNodeAttributes(const SUnit *N,
                                         const ScheduleDAG *Graph) {
      return "shape=Mrecord";
    }

    static void addCustomGraphFeatures(ScheduleDAG *G,
                                       GraphWriter<ScheduleDAG*> &GW) {
      return G->addCustomGraphFeatures(GW);
    }
  };
}

std::string DOTGraphTraits<ScheduleDAG*>::getNodeLabel(const SUnit *SU,
                                                       const ScheduleDAG *G) {
  return G->getGraphNodeLabel(SU);
}

/// viewGraph - Pop up a ghostview window with the reachable parts of the DAG
/// rendered using 'dot'.
///
void ScheduleDAG::viewGraph(const Twine &Name, const Twine &Title) {
  // This code is only for debugging!
#ifndef NDEBUG
  ViewGraph(this, Name, false, Title);
#else
  errs() << "ScheduleDAG::viewGraph is only available in debug builds on "
         << "systems with Graphviz or gv!\n";
#endif  // NDEBUG
}

/// Out-of-line implementation with no arguments is handy for gdb.
void ScheduleDAG::viewGraph() {
  viewGraph(getDAGName(), "Scheduling-Units Graph for " + getDAGName());
}
