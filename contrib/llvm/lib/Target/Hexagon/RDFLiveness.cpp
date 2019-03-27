//===- RDFLiveness.cpp ----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Computation of the liveness information from the data-flow graph.
//
// The main functionality of this code is to compute block live-in
// information. With the live-in information in place, the placement
// of kill flags can also be recalculated.
//
// The block live-in calculation is based on the ideas from the following
// publication:
//
// Dibyendu Das, Ramakrishna Upadrasta, Benoit Dupont de Dinechin.
// "Efficient Liveness Computation Using Merge Sets and DJ-Graphs."
// ACM Transactions on Architecture and Code Optimization, Association for
// Computing Machinery, 2012, ACM TACO Special Issue on "High-Performance
// and Embedded Architectures and Compilers", 8 (4),
// <10.1145/2086696.2086706>. <hal-00647369>
//
#include "RDFLiveness.h"
#include "RDFGraph.h"
#include "RDFRegisters.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominanceFrontier.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

using namespace llvm;
using namespace rdf;

static cl::opt<unsigned> MaxRecNest("rdf-liveness-max-rec", cl::init(25),
  cl::Hidden, cl::desc("Maximum recursion level"));

namespace llvm {
namespace rdf {

  template<>
  raw_ostream &operator<< (raw_ostream &OS, const Print<Liveness::RefMap> &P) {
    OS << '{';
    for (auto &I : P.Obj) {
      OS << ' ' << printReg(I.first, &P.G.getTRI()) << '{';
      for (auto J = I.second.begin(), E = I.second.end(); J != E; ) {
        OS << Print<NodeId>(J->first, P.G) << PrintLaneMaskOpt(J->second);
        if (++J != E)
          OS << ',';
      }
      OS << '}';
    }
    OS << " }";
    return OS;
  }

} // end namespace rdf
} // end namespace llvm

// The order in the returned sequence is the order of reaching defs in the
// upward traversal: the first def is the closest to the given reference RefA,
// the next one is further up, and so on.
// The list ends at a reaching phi def, or when the reference from RefA is
// covered by the defs in the list (see FullChain).
// This function provides two modes of operation:
// (1) Returning the sequence of reaching defs for a particular reference
// node. This sequence will terminate at the first phi node [1].
// (2) Returning a partial sequence of reaching defs, where the final goal
// is to traverse past phi nodes to the actual defs arising from the code
// itself.
// In mode (2), the register reference for which the search was started
// may be different from the reference node RefA, for which this call was
// made, hence the argument RefRR, which holds the original register.
// Also, some definitions may have already been encountered in a previous
// call that will influence register covering. The register references
// already defined are passed in through DefRRs.
// In mode (1), the "continuation" considerations do not apply, and the
// RefRR is the same as the register in RefA, and the set DefRRs is empty.
//
// [1] It is possible for multiple phi nodes to be included in the returned
// sequence:
//   SubA = phi ...
//   SubB = phi ...
//   ...  = SuperAB(rdef:SubA), SuperAB"(rdef:SubB)
// However, these phi nodes are independent from one another in terms of
// the data-flow.

NodeList Liveness::getAllReachingDefs(RegisterRef RefRR,
      NodeAddr<RefNode*> RefA, bool TopShadows, bool FullChain,
      const RegisterAggr &DefRRs) {
  NodeList RDefs; // Return value.
  SetVector<NodeId> DefQ;
  SetVector<NodeId> Owners;

  // Dead defs will be treated as if they were live, since they are actually
  // on the data-flow path. They cannot be ignored because even though they
  // do not generate meaningful values, they still modify registers.

  // If the reference is undefined, there is nothing to do.
  if (RefA.Addr->getFlags() & NodeAttrs::Undef)
    return RDefs;

  // The initial queue should not have reaching defs for shadows. The
  // whole point of a shadow is that it will have a reaching def that
  // is not aliased to the reaching defs of the related shadows.
  NodeId Start = RefA.Id;
  auto SNA = DFG.addr<RefNode*>(Start);
  if (NodeId RD = SNA.Addr->getReachingDef())
    DefQ.insert(RD);
  if (TopShadows) {
    for (auto S : DFG.getRelatedRefs(RefA.Addr->getOwner(DFG), RefA))
      if (NodeId RD = NodeAddr<RefNode*>(S).Addr->getReachingDef())
        DefQ.insert(RD);
  }

  // Collect all the reaching defs, going up until a phi node is encountered,
  // or there are no more reaching defs. From this set, the actual set of
  // reaching defs will be selected.
  // The traversal upwards must go on until a covering def is encountered.
  // It is possible that a collection of non-covering (individually) defs
  // will be sufficient, but keep going until a covering one is found.
  for (unsigned i = 0; i < DefQ.size(); ++i) {
    auto TA = DFG.addr<DefNode*>(DefQ[i]);
    if (TA.Addr->getFlags() & NodeAttrs::PhiRef)
      continue;
    // Stop at the covering/overwriting def of the initial register reference.
    RegisterRef RR = TA.Addr->getRegRef(DFG);
    if (!DFG.IsPreservingDef(TA))
      if (RegisterAggr::isCoverOf(RR, RefRR, PRI))
        continue;
    // Get the next level of reaching defs. This will include multiple
    // reaching defs for shadows.
    for (auto S : DFG.getRelatedRefs(TA.Addr->getOwner(DFG), TA))
      if (NodeId RD = NodeAddr<RefNode*>(S).Addr->getReachingDef())
        DefQ.insert(RD);
  }

  // Remove all non-phi defs that are not aliased to RefRR, and collect
  // the owners of the remaining defs.
  SetVector<NodeId> Defs;
  for (NodeId N : DefQ) {
    auto TA = DFG.addr<DefNode*>(N);
    bool IsPhi = TA.Addr->getFlags() & NodeAttrs::PhiRef;
    if (!IsPhi && !PRI.alias(RefRR, TA.Addr->getRegRef(DFG)))
      continue;
    Defs.insert(TA.Id);
    Owners.insert(TA.Addr->getOwner(DFG).Id);
  }

  // Return the MachineBasicBlock containing a given instruction.
  auto Block = [this] (NodeAddr<InstrNode*> IA) -> MachineBasicBlock* {
    if (IA.Addr->getKind() == NodeAttrs::Stmt)
      return NodeAddr<StmtNode*>(IA).Addr->getCode()->getParent();
    assert(IA.Addr->getKind() == NodeAttrs::Phi);
    NodeAddr<PhiNode*> PA = IA;
    NodeAddr<BlockNode*> BA = PA.Addr->getOwner(DFG);
    return BA.Addr->getCode();
  };
  // Less(A,B) iff instruction A is further down in the dominator tree than B.
  auto Less = [&Block,this] (NodeId A, NodeId B) -> bool {
    if (A == B)
      return false;
    auto OA = DFG.addr<InstrNode*>(A), OB = DFG.addr<InstrNode*>(B);
    MachineBasicBlock *BA = Block(OA), *BB = Block(OB);
    if (BA != BB)
      return MDT.dominates(BB, BA);
    // They are in the same block.
    bool StmtA = OA.Addr->getKind() == NodeAttrs::Stmt;
    bool StmtB = OB.Addr->getKind() == NodeAttrs::Stmt;
    if (StmtA) {
      if (!StmtB)   // OB is a phi and phis dominate statements.
        return true;
      MachineInstr *CA = NodeAddr<StmtNode*>(OA).Addr->getCode();
      MachineInstr *CB = NodeAddr<StmtNode*>(OB).Addr->getCode();
      // The order must be linear, so tie-break such equalities.
      if (CA == CB)
        return A < B;
      return MDT.dominates(CB, CA);
    } else {
      // OA is a phi.
      if (StmtB)
        return false;
      // Both are phis. There is no ordering between phis (in terms of
      // the data-flow), so tie-break this via node id comparison.
      return A < B;
    }
  };

  std::vector<NodeId> Tmp(Owners.begin(), Owners.end());
  llvm::sort(Tmp, Less);

  // The vector is a list of instructions, so that defs coming from
  // the same instruction don't need to be artificially ordered.
  // Then, when computing the initial segment, and iterating over an
  // instruction, pick the defs that contribute to the covering (i.e. is
  // not covered by previously added defs). Check the defs individually,
  // i.e. first check each def if is covered or not (without adding them
  // to the tracking set), and then add all the selected ones.

  // The reason for this is this example:
  // *d1<A>, *d2<B>, ... Assume A and B are aliased (can happen in phi nodes).
  // *d3<C>              If A \incl BuC, and B \incl AuC, then *d2 would be
  //                     covered if we added A first, and A would be covered
  //                     if we added B first.

  RegisterAggr RRs(DefRRs);

  auto DefInSet = [&Defs] (NodeAddr<RefNode*> TA) -> bool {
    return TA.Addr->getKind() == NodeAttrs::Def &&
           Defs.count(TA.Id);
  };
  for (NodeId T : Tmp) {
    if (!FullChain && RRs.hasCoverOf(RefRR))
      break;
    auto TA = DFG.addr<InstrNode*>(T);
    bool IsPhi = DFG.IsCode<NodeAttrs::Phi>(TA);
    NodeList Ds;
    for (NodeAddr<DefNode*> DA : TA.Addr->members_if(DefInSet, DFG)) {
      RegisterRef QR = DA.Addr->getRegRef(DFG);
      // Add phi defs even if they are covered by subsequent defs. This is
      // for cases where the reached use is not covered by any of the defs
      // encountered so far: the phi def is needed to expose the liveness
      // of that use to the entry of the block.
      // Example:
      //   phi d1<R3>(,d2,), ...  Phi def d1 is covered by d2.
      //   d2<R3>(d1,,u3), ...
      //   ..., u3<D1>(d2)        This use needs to be live on entry.
      if (FullChain || IsPhi || !RRs.hasCoverOf(QR))
        Ds.push_back(DA);
    }
    RDefs.insert(RDefs.end(), Ds.begin(), Ds.end());
    for (NodeAddr<DefNode*> DA : Ds) {
      // When collecting a full chain of definitions, do not consider phi
      // defs to actually define a register.
      uint16_t Flags = DA.Addr->getFlags();
      if (!FullChain || !(Flags & NodeAttrs::PhiRef))
        if (!(Flags & NodeAttrs::Preserving)) // Don't care about Undef here.
          RRs.insert(DA.Addr->getRegRef(DFG));
    }
  }

  auto DeadP = [](const NodeAddr<DefNode*> DA) -> bool {
    return DA.Addr->getFlags() & NodeAttrs::Dead;
  };
  RDefs.resize(std::distance(RDefs.begin(), llvm::remove_if(RDefs, DeadP)));

  return RDefs;
}

std::pair<NodeSet,bool>
Liveness::getAllReachingDefsRec(RegisterRef RefRR, NodeAddr<RefNode*> RefA,
      NodeSet &Visited, const NodeSet &Defs) {
  return getAllReachingDefsRecImpl(RefRR, RefA, Visited, Defs, 0, MaxRecNest);
}

std::pair<NodeSet,bool>
Liveness::getAllReachingDefsRecImpl(RegisterRef RefRR, NodeAddr<RefNode*> RefA,
      NodeSet &Visited, const NodeSet &Defs, unsigned Nest, unsigned MaxNest) {
  if (Nest > MaxNest)
    return { NodeSet(), false };
  // Collect all defined registers. Do not consider phis to be defining
  // anything, only collect "real" definitions.
  RegisterAggr DefRRs(PRI);
  for (NodeId D : Defs) {
    const auto DA = DFG.addr<const DefNode*>(D);
    if (!(DA.Addr->getFlags() & NodeAttrs::PhiRef))
      DefRRs.insert(DA.Addr->getRegRef(DFG));
  }

  NodeList RDs = getAllReachingDefs(RefRR, RefA, false, true, DefRRs);
  if (RDs.empty())
    return { Defs, true };

  // Make a copy of the preexisting definitions and add the newly found ones.
  NodeSet TmpDefs = Defs;
  for (NodeAddr<NodeBase*> R : RDs)
    TmpDefs.insert(R.Id);

  NodeSet Result = Defs;

  for (NodeAddr<DefNode*> DA : RDs) {
    Result.insert(DA.Id);
    if (!(DA.Addr->getFlags() & NodeAttrs::PhiRef))
      continue;
    NodeAddr<PhiNode*> PA = DA.Addr->getOwner(DFG);
    if (Visited.count(PA.Id))
      continue;
    Visited.insert(PA.Id);
    // Go over all phi uses and get the reaching defs for each use.
    for (auto U : PA.Addr->members_if(DFG.IsRef<NodeAttrs::Use>, DFG)) {
      const auto &T = getAllReachingDefsRecImpl(RefRR, U, Visited, TmpDefs,
                                                Nest+1, MaxNest);
      if (!T.second)
        return { T.first, false };
      Result.insert(T.first.begin(), T.first.end());
    }
  }

  return { Result, true };
}

/// Find the nearest ref node aliased to RefRR, going upwards in the data
/// flow, starting from the instruction immediately preceding Inst.
NodeAddr<RefNode*> Liveness::getNearestAliasedRef(RegisterRef RefRR,
      NodeAddr<InstrNode*> IA) {
  NodeAddr<BlockNode*> BA = IA.Addr->getOwner(DFG);
  NodeList Ins = BA.Addr->members(DFG);
  NodeId FindId = IA.Id;
  auto E = Ins.rend();
  auto B = std::find_if(Ins.rbegin(), E,
                        [FindId] (const NodeAddr<InstrNode*> T) {
                          return T.Id == FindId;
                        });
  // Do not scan IA (which is what B would point to).
  if (B != E)
    ++B;

  do {
    // Process the range of instructions from B to E.
    for (NodeAddr<InstrNode*> I : make_range(B, E)) {
      NodeList Refs = I.Addr->members(DFG);
      NodeAddr<RefNode*> Clob, Use;
      // Scan all the refs in I aliased to RefRR, and return the one that
      // is the closest to the output of I, i.e. def > clobber > use.
      for (NodeAddr<RefNode*> R : Refs) {
        if (!PRI.alias(R.Addr->getRegRef(DFG), RefRR))
          continue;
        if (DFG.IsDef(R)) {
          // If it's a non-clobbering def, just return it.
          if (!(R.Addr->getFlags() & NodeAttrs::Clobbering))
            return R;
          Clob = R;
        } else {
          Use = R;
        }
      }
      if (Clob.Id != 0)
        return Clob;
      if (Use.Id != 0)
        return Use;
    }

    // Go up to the immediate dominator, if any.
    MachineBasicBlock *BB = BA.Addr->getCode();
    BA = NodeAddr<BlockNode*>();
    if (MachineDomTreeNode *N = MDT.getNode(BB)) {
      if ((N = N->getIDom()))
        BA = DFG.findBlock(N->getBlock());
    }
    if (!BA.Id)
      break;

    Ins = BA.Addr->members(DFG);
    B = Ins.rbegin();
    E = Ins.rend();
  } while (true);

  return NodeAddr<RefNode*>();
}

NodeSet Liveness::getAllReachedUses(RegisterRef RefRR,
      NodeAddr<DefNode*> DefA, const RegisterAggr &DefRRs) {
  NodeSet Uses;

  // If the original register is already covered by all the intervening
  // defs, no more uses can be reached.
  if (DefRRs.hasCoverOf(RefRR))
    return Uses;

  // Add all directly reached uses.
  // If the def is dead, it does not provide a value for any use.
  bool IsDead = DefA.Addr->getFlags() & NodeAttrs::Dead;
  NodeId U = !IsDead ? DefA.Addr->getReachedUse() : 0;
  while (U != 0) {
    auto UA = DFG.addr<UseNode*>(U);
    if (!(UA.Addr->getFlags() & NodeAttrs::Undef)) {
      RegisterRef UR = UA.Addr->getRegRef(DFG);
      if (PRI.alias(RefRR, UR) && !DefRRs.hasCoverOf(UR))
        Uses.insert(U);
    }
    U = UA.Addr->getSibling();
  }

  // Traverse all reached defs. This time dead defs cannot be ignored.
  for (NodeId D = DefA.Addr->getReachedDef(), NextD; D != 0; D = NextD) {
    auto DA = DFG.addr<DefNode*>(D);
    NextD = DA.Addr->getSibling();
    RegisterRef DR = DA.Addr->getRegRef(DFG);
    // If this def is already covered, it cannot reach anything new.
    // Similarly, skip it if it is not aliased to the interesting register.
    if (DefRRs.hasCoverOf(DR) || !PRI.alias(RefRR, DR))
      continue;
    NodeSet T;
    if (DFG.IsPreservingDef(DA)) {
      // If it is a preserving def, do not update the set of intervening defs.
      T = getAllReachedUses(RefRR, DA, DefRRs);
    } else {
      RegisterAggr NewDefRRs = DefRRs;
      NewDefRRs.insert(DR);
      T = getAllReachedUses(RefRR, DA, NewDefRRs);
    }
    Uses.insert(T.begin(), T.end());
  }
  return Uses;
}

void Liveness::computePhiInfo() {
  RealUseMap.clear();

  NodeList Phis;
  NodeAddr<FuncNode*> FA = DFG.getFunc();
  NodeList Blocks = FA.Addr->members(DFG);
  for (NodeAddr<BlockNode*> BA : Blocks) {
    auto Ps = BA.Addr->members_if(DFG.IsCode<NodeAttrs::Phi>, DFG);
    Phis.insert(Phis.end(), Ps.begin(), Ps.end());
  }

  // phi use -> (map: reaching phi -> set of registers defined in between)
  std::map<NodeId,std::map<NodeId,RegisterAggr>> PhiUp;
  std::vector<NodeId> PhiUQ;  // Work list of phis for upward propagation.
  std::map<NodeId,RegisterAggr> PhiDRs;  // Phi -> registers defined by it.

  // Go over all phis.
  for (NodeAddr<PhiNode*> PhiA : Phis) {
    // Go over all defs and collect the reached uses that are non-phi uses
    // (i.e. the "real uses").
    RefMap &RealUses = RealUseMap[PhiA.Id];
    NodeList PhiRefs = PhiA.Addr->members(DFG);

    // Have a work queue of defs whose reached uses need to be found.
    // For each def, add to the queue all reached (non-phi) defs.
    SetVector<NodeId> DefQ;
    NodeSet PhiDefs;
    RegisterAggr DRs(PRI);
    for (NodeAddr<RefNode*> R : PhiRefs) {
      if (!DFG.IsRef<NodeAttrs::Def>(R))
        continue;
      DRs.insert(R.Addr->getRegRef(DFG));
      DefQ.insert(R.Id);
      PhiDefs.insert(R.Id);
    }
    PhiDRs.insert(std::make_pair(PhiA.Id, DRs));

    // Collect the super-set of all possible reached uses. This set will
    // contain all uses reached from this phi, either directly from the
    // phi defs, or (recursively) via non-phi defs reached by the phi defs.
    // This set of uses will later be trimmed to only contain these uses that
    // are actually reached by the phi defs.
    for (unsigned i = 0; i < DefQ.size(); ++i) {
      NodeAddr<DefNode*> DA = DFG.addr<DefNode*>(DefQ[i]);
      // Visit all reached uses. Phi defs should not really have the "dead"
      // flag set, but check it anyway for consistency.
      bool IsDead = DA.Addr->getFlags() & NodeAttrs::Dead;
      NodeId UN = !IsDead ? DA.Addr->getReachedUse() : 0;
      while (UN != 0) {
        NodeAddr<UseNode*> A = DFG.addr<UseNode*>(UN);
        uint16_t F = A.Addr->getFlags();
        if ((F & (NodeAttrs::Undef | NodeAttrs::PhiRef)) == 0) {
          RegisterRef R = PRI.normalize(A.Addr->getRegRef(DFG));
          RealUses[R.Reg].insert({A.Id,R.Mask});
        }
        UN = A.Addr->getSibling();
      }
      // Visit all reached defs, and add them to the queue. These defs may
      // override some of the uses collected here, but that will be handled
      // later.
      NodeId DN = DA.Addr->getReachedDef();
      while (DN != 0) {
        NodeAddr<DefNode*> A = DFG.addr<DefNode*>(DN);
        for (auto T : DFG.getRelatedRefs(A.Addr->getOwner(DFG), A)) {
          uint16_t Flags = NodeAddr<DefNode*>(T).Addr->getFlags();
          // Must traverse the reached-def chain. Consider:
          //   def(D0) -> def(R0) -> def(R0) -> use(D0)
          // The reachable use of D0 passes through a def of R0.
          if (!(Flags & NodeAttrs::PhiRef))
            DefQ.insert(T.Id);
        }
        DN = A.Addr->getSibling();
      }
    }
    // Filter out these uses that appear to be reachable, but really
    // are not. For example:
    //
    // R1:0 =          d1
    //      = R1:0     u2     Reached by d1.
    //   R0 =          d3
    //      = R1:0     u4     Still reached by d1: indirectly through
    //                        the def d3.
    //   R1 =          d5
    //      = R1:0     u6     Not reached by d1 (covered collectively
    //                        by d3 and d5), but following reached
    //                        defs and uses from d1 will lead here.
    for (auto UI = RealUses.begin(), UE = RealUses.end(); UI != UE; ) {
      // For each reached register UI->first, there is a set UI->second, of
      // uses of it. For each such use, check if it is reached by this phi,
      // i.e. check if the set of its reaching uses intersects the set of
      // this phi's defs.
      NodeRefSet Uses = UI->second;
      UI->second.clear();
      for (std::pair<NodeId,LaneBitmask> I : Uses) {
        auto UA = DFG.addr<UseNode*>(I.first);
        // Undef flag is checked above.
        assert((UA.Addr->getFlags() & NodeAttrs::Undef) == 0);
        RegisterRef R(UI->first, I.second);
        // Calculate the exposed part of the reached use.
        RegisterAggr Covered(PRI);
        for (NodeAddr<DefNode*> DA : getAllReachingDefs(R, UA)) {
          if (PhiDefs.count(DA.Id))
            break;
          Covered.insert(DA.Addr->getRegRef(DFG));
        }
        if (RegisterRef RC = Covered.clearIn(R)) {
          // We are updating the map for register UI->first, so we need
          // to map RC to be expressed in terms of that register.
          RegisterRef S = PRI.mapTo(RC, UI->first);
          UI->second.insert({I.first, S.Mask});
        }
      }
      UI = UI->second.empty() ? RealUses.erase(UI) : std::next(UI);
    }

    // If this phi reaches some "real" uses, add it to the queue for upward
    // propagation.
    if (!RealUses.empty())
      PhiUQ.push_back(PhiA.Id);

    // Go over all phi uses and check if the reaching def is another phi.
    // Collect the phis that are among the reaching defs of these uses.
    // While traversing the list of reaching defs for each phi use, accumulate
    // the set of registers defined between this phi (PhiA) and the owner phi
    // of the reaching def.
    NodeSet SeenUses;

    for (auto I : PhiRefs) {
      if (!DFG.IsRef<NodeAttrs::Use>(I) || SeenUses.count(I.Id))
        continue;
      NodeAddr<PhiUseNode*> PUA = I;
      if (PUA.Addr->getReachingDef() == 0)
        continue;

      RegisterRef UR = PUA.Addr->getRegRef(DFG);
      NodeList Ds = getAllReachingDefs(UR, PUA, true, false, NoRegs);
      RegisterAggr DefRRs(PRI);

      for (NodeAddr<DefNode*> D : Ds) {
        if (D.Addr->getFlags() & NodeAttrs::PhiRef) {
          NodeId RP = D.Addr->getOwner(DFG).Id;
          std::map<NodeId,RegisterAggr> &M = PhiUp[PUA.Id];
          auto F = M.find(RP);
          if (F == M.end())
            M.insert(std::make_pair(RP, DefRRs));
          else
            F->second.insert(DefRRs);
        }
        DefRRs.insert(D.Addr->getRegRef(DFG));
      }

      for (NodeAddr<PhiUseNode*> T : DFG.getRelatedRefs(PhiA, PUA))
        SeenUses.insert(T.Id);
    }
  }

  if (Trace) {
    dbgs() << "Phi-up-to-phi map with intervening defs:\n";
    for (auto I : PhiUp) {
      dbgs() << "phi " << Print<NodeId>(I.first, DFG) << " -> {";
      for (auto R : I.second)
        dbgs() << ' ' << Print<NodeId>(R.first, DFG)
               << Print<RegisterAggr>(R.second, DFG);
      dbgs() << " }\n";
    }
  }

  // Propagate the reached registers up in the phi chain.
  //
  // The following type of situation needs careful handling:
  //
  //   phi d1<R1:0>  (1)
  //        |
  //   ... d2<R1>
  //        |
  //   phi u3<R1:0>  (2)
  //        |
  //   ... u4<R1>
  //
  // The phi node (2) defines a register pair R1:0, and reaches a "real"
  // use u4 of just R1. The same phi node is also known to reach (upwards)
  // the phi node (1). However, the use u4 is not reached by phi (1),
  // because of the intervening definition d2 of R1. The data flow between
  // phis (1) and (2) is restricted to R1:0 minus R1, i.e. R0.
  //
  // When propagating uses up the phi chains, get the all reaching defs
  // for a given phi use, and traverse the list until the propagated ref
  // is covered, or until reaching the final phi. Only assume that the
  // reference reaches the phi in the latter case.

  for (unsigned i = 0; i < PhiUQ.size(); ++i) {
    auto PA = DFG.addr<PhiNode*>(PhiUQ[i]);
    NodeList PUs = PA.Addr->members_if(DFG.IsRef<NodeAttrs::Use>, DFG);
    RefMap &RUM = RealUseMap[PA.Id];

    for (NodeAddr<UseNode*> UA : PUs) {
      std::map<NodeId,RegisterAggr> &PUM = PhiUp[UA.Id];
      RegisterRef UR = PRI.normalize(UA.Addr->getRegRef(DFG));
      for (const std::pair<NodeId,RegisterAggr> &P : PUM) {
        bool Changed = false;
        const RegisterAggr &MidDefs = P.second;

        // Collect the set PropUp of uses that are reached by the current
        // phi PA, and are not covered by any intervening def between the
        // currently visited use UA and the upward phi P.

        if (MidDefs.hasCoverOf(UR))
          continue;

        // General algorithm:
        //   for each (R,U) : U is use node of R, U is reached by PA
        //     if MidDefs does not cover (R,U)
        //       then add (R-MidDefs,U) to RealUseMap[P]
        //
        for (const std::pair<RegisterId,NodeRefSet> &T : RUM) {
          RegisterRef R(T.first);
          // The current phi (PA) could be a phi for a regmask. It could
          // reach a whole variety of uses that are not related to the
          // specific upward phi (P.first).
          const RegisterAggr &DRs = PhiDRs.at(P.first);
          if (!DRs.hasAliasOf(R))
            continue;
          R = PRI.mapTo(DRs.intersectWith(R), T.first);
          for (std::pair<NodeId,LaneBitmask> V : T.second) {
            LaneBitmask M = R.Mask & V.second;
            if (M.none())
              continue;
            if (RegisterRef SS = MidDefs.clearIn(RegisterRef(R.Reg, M))) {
              NodeRefSet &RS = RealUseMap[P.first][SS.Reg];
              Changed |= RS.insert({V.first,SS.Mask}).second;
            }
          }
        }

        if (Changed)
          PhiUQ.push_back(P.first);
      }
    }
  }

  if (Trace) {
    dbgs() << "Real use map:\n";
    for (auto I : RealUseMap) {
      dbgs() << "phi " << Print<NodeId>(I.first, DFG);
      NodeAddr<PhiNode*> PA = DFG.addr<PhiNode*>(I.first);
      NodeList Ds = PA.Addr->members_if(DFG.IsRef<NodeAttrs::Def>, DFG);
      if (!Ds.empty()) {
        RegisterRef RR = NodeAddr<DefNode*>(Ds[0]).Addr->getRegRef(DFG);
        dbgs() << '<' << Print<RegisterRef>(RR, DFG) << '>';
      } else {
        dbgs() << "<noreg>";
      }
      dbgs() << " -> " << Print<RefMap>(I.second, DFG) << '\n';
    }
  }
}

void Liveness::computeLiveIns() {
  // Populate the node-to-block map. This speeds up the calculations
  // significantly.
  NBMap.clear();
  for (NodeAddr<BlockNode*> BA : DFG.getFunc().Addr->members(DFG)) {
    MachineBasicBlock *BB = BA.Addr->getCode();
    for (NodeAddr<InstrNode*> IA : BA.Addr->members(DFG)) {
      for (NodeAddr<RefNode*> RA : IA.Addr->members(DFG))
        NBMap.insert(std::make_pair(RA.Id, BB));
      NBMap.insert(std::make_pair(IA.Id, BB));
    }
  }

  MachineFunction &MF = DFG.getMF();

  // Compute IDF first, then the inverse.
  decltype(IIDF) IDF;
  for (MachineBasicBlock &B : MF) {
    auto F1 = MDF.find(&B);
    if (F1 == MDF.end())
      continue;
    SetVector<MachineBasicBlock*> IDFB(F1->second.begin(), F1->second.end());
    for (unsigned i = 0; i < IDFB.size(); ++i) {
      auto F2 = MDF.find(IDFB[i]);
      if (F2 != MDF.end())
        IDFB.insert(F2->second.begin(), F2->second.end());
    }
    // Add B to the IDF(B). This will put B in the IIDF(B).
    IDFB.insert(&B);
    IDF[&B].insert(IDFB.begin(), IDFB.end());
  }

  for (auto I : IDF)
    for (auto S : I.second)
      IIDF[S].insert(I.first);

  computePhiInfo();

  NodeAddr<FuncNode*> FA = DFG.getFunc();
  NodeList Blocks = FA.Addr->members(DFG);

  // Build the phi live-on-entry map.
  for (NodeAddr<BlockNode*> BA : Blocks) {
    MachineBasicBlock *MB = BA.Addr->getCode();
    RefMap &LON = PhiLON[MB];
    for (auto P : BA.Addr->members_if(DFG.IsCode<NodeAttrs::Phi>, DFG))
      for (const RefMap::value_type &S : RealUseMap[P.Id])
        LON[S.first].insert(S.second.begin(), S.second.end());
  }

  if (Trace) {
    dbgs() << "Phi live-on-entry map:\n";
    for (auto &I : PhiLON)
      dbgs() << "block #" << I.first->getNumber() << " -> "
             << Print<RefMap>(I.second, DFG) << '\n';
  }

  // Build the phi live-on-exit map. Each phi node has some set of reached
  // "real" uses. Propagate this set backwards into the block predecessors
  // through the reaching defs of the corresponding phi uses.
  for (NodeAddr<BlockNode*> BA : Blocks) {
    NodeList Phis = BA.Addr->members_if(DFG.IsCode<NodeAttrs::Phi>, DFG);
    for (NodeAddr<PhiNode*> PA : Phis) {
      RefMap &RUs = RealUseMap[PA.Id];
      if (RUs.empty())
        continue;

      NodeSet SeenUses;
      for (auto U : PA.Addr->members_if(DFG.IsRef<NodeAttrs::Use>, DFG)) {
        if (!SeenUses.insert(U.Id).second)
          continue;
        NodeAddr<PhiUseNode*> PUA = U;
        if (PUA.Addr->getReachingDef() == 0)
          continue;

        // Each phi has some set (possibly empty) of reached "real" uses,
        // that is, uses that are part of the compiled program. Such a use
        // may be located in some farther block, but following a chain of
        // reaching defs will eventually lead to this phi.
        // Any chain of reaching defs may fork at a phi node, but there
        // will be a path upwards that will lead to this phi. Now, this
        // chain will need to fork at this phi, since some of the reached
        // uses may have definitions joining in from multiple predecessors.
        // For each reached "real" use, identify the set of reaching defs
        // coming from each predecessor P, and add them to PhiLOX[P].
        //
        auto PrA = DFG.addr<BlockNode*>(PUA.Addr->getPredecessor());
        RefMap &LOX = PhiLOX[PrA.Addr->getCode()];

        for (const std::pair<RegisterId,NodeRefSet> &RS : RUs) {
          // We need to visit each individual use.
          for (std::pair<NodeId,LaneBitmask> P : RS.second) {
            // Create a register ref corresponding to the use, and find
            // all reaching defs starting from the phi use, and treating
            // all related shadows as a single use cluster.
            RegisterRef S(RS.first, P.second);
            NodeList Ds = getAllReachingDefs(S, PUA, true, false, NoRegs);
            for (NodeAddr<DefNode*> D : Ds) {
              // Calculate the mask corresponding to the visited def.
              RegisterAggr TA(PRI);
              TA.insert(D.Addr->getRegRef(DFG)).intersect(S);
              LaneBitmask TM = TA.makeRegRef().Mask;
              LOX[S.Reg].insert({D.Id, TM});
            }
          }
        }

        for (NodeAddr<PhiUseNode*> T : DFG.getRelatedRefs(PA, PUA))
          SeenUses.insert(T.Id);
      }  // for U : phi uses
    }  // for P : Phis
  }  // for B : Blocks

  if (Trace) {
    dbgs() << "Phi live-on-exit map:\n";
    for (auto &I : PhiLOX)
      dbgs() << "block #" << I.first->getNumber() << " -> "
             << Print<RefMap>(I.second, DFG) << '\n';
  }

  RefMap LiveIn;
  traverse(&MF.front(), LiveIn);

  // Add function live-ins to the live-in set of the function entry block.
  LiveMap[&MF.front()].insert(DFG.getLiveIns());

  if (Trace) {
    // Dump the liveness map
    for (MachineBasicBlock &B : MF) {
      std::vector<RegisterRef> LV;
      for (auto I = B.livein_begin(), E = B.livein_end(); I != E; ++I)
        LV.push_back(RegisterRef(I->PhysReg, I->LaneMask));
      llvm::sort(LV);
      dbgs() << printMBBReference(B) << "\t rec = {";
      for (auto I : LV)
        dbgs() << ' ' << Print<RegisterRef>(I, DFG);
      dbgs() << " }\n";
      //dbgs() << "\tcomp = " << Print<RegisterAggr>(LiveMap[&B], DFG) << '\n';

      LV.clear();
      const RegisterAggr &LG = LiveMap[&B];
      for (auto I = LG.rr_begin(), E = LG.rr_end(); I != E; ++I)
        LV.push_back(*I);
      llvm::sort(LV);
      dbgs() << "\tcomp = {";
      for (auto I : LV)
        dbgs() << ' ' << Print<RegisterRef>(I, DFG);
      dbgs() << " }\n";

    }
  }
}

void Liveness::resetLiveIns() {
  for (auto &B : DFG.getMF()) {
    // Remove all live-ins.
    std::vector<unsigned> T;
    for (auto I = B.livein_begin(), E = B.livein_end(); I != E; ++I)
      T.push_back(I->PhysReg);
    for (auto I : T)
      B.removeLiveIn(I);
    // Add the newly computed live-ins.
    const RegisterAggr &LiveIns = LiveMap[&B];
    for (auto I = LiveIns.rr_begin(), E = LiveIns.rr_end(); I != E; ++I) {
      RegisterRef R = *I;
      B.addLiveIn({MCPhysReg(R.Reg), R.Mask});
    }
  }
}

void Liveness::resetKills() {
  for (auto &B : DFG.getMF())
    resetKills(&B);
}

void Liveness::resetKills(MachineBasicBlock *B) {
  auto CopyLiveIns = [this] (MachineBasicBlock *B, BitVector &LV) -> void {
    for (auto I : B->liveins()) {
      MCSubRegIndexIterator S(I.PhysReg, &TRI);
      if (!S.isValid()) {
        LV.set(I.PhysReg);
        continue;
      }
      do {
        LaneBitmask M = TRI.getSubRegIndexLaneMask(S.getSubRegIndex());
        if ((M & I.LaneMask).any())
          LV.set(S.getSubReg());
        ++S;
      } while (S.isValid());
    }
  };

  BitVector LiveIn(TRI.getNumRegs()), Live(TRI.getNumRegs());
  CopyLiveIns(B, LiveIn);
  for (auto SI : B->successors())
    CopyLiveIns(SI, Live);

  for (auto I = B->rbegin(), E = B->rend(); I != E; ++I) {
    MachineInstr *MI = &*I;
    if (MI->isDebugInstr())
      continue;

    MI->clearKillInfo();
    for (auto &Op : MI->operands()) {
      // An implicit def of a super-register may not necessarily start a
      // live range of it, since an implicit use could be used to keep parts
      // of it live. Instead of analyzing the implicit operands, ignore
      // implicit defs.
      if (!Op.isReg() || !Op.isDef() || Op.isImplicit())
        continue;
      unsigned R = Op.getReg();
      if (!TargetRegisterInfo::isPhysicalRegister(R))
        continue;
      for (MCSubRegIterator SR(R, &TRI, true); SR.isValid(); ++SR)
        Live.reset(*SR);
    }
    for (auto &Op : MI->operands()) {
      if (!Op.isReg() || !Op.isUse() || Op.isUndef())
        continue;
      unsigned R = Op.getReg();
      if (!TargetRegisterInfo::isPhysicalRegister(R))
        continue;
      bool IsLive = false;
      for (MCRegAliasIterator AR(R, &TRI, true); AR.isValid(); ++AR) {
        if (!Live[*AR])
          continue;
        IsLive = true;
        break;
      }
      if (!IsLive)
        Op.setIsKill(true);
      for (MCSubRegIterator SR(R, &TRI, true); SR.isValid(); ++SR)
        Live.set(*SR);
    }
  }
}

// Helper function to obtain the basic block containing the reaching def
// of the given use.
MachineBasicBlock *Liveness::getBlockWithRef(NodeId RN) const {
  auto F = NBMap.find(RN);
  if (F != NBMap.end())
    return F->second;
  llvm_unreachable("Node id not in map");
}

void Liveness::traverse(MachineBasicBlock *B, RefMap &LiveIn) {
  // The LiveIn map, for each (physical) register, contains the set of live
  // reaching defs of that register that are live on entry to the associated
  // block.

  // The summary of the traversal algorithm:
  //
  // R is live-in in B, if there exists a U(R), such that rdef(R) dom B
  // and (U \in IDF(B) or B dom U).
  //
  // for (C : children) {
  //   LU = {}
  //   traverse(C, LU)
  //   LiveUses += LU
  // }
  //
  // LiveUses -= Defs(B);
  // LiveUses += UpwardExposedUses(B);
  // for (C : IIDF[B])
  //   for (U : LiveUses)
  //     if (Rdef(U) dom C)
  //       C.addLiveIn(U)
  //

  // Go up the dominator tree (depth-first).
  MachineDomTreeNode *N = MDT.getNode(B);
  for (auto I : *N) {
    RefMap L;
    MachineBasicBlock *SB = I->getBlock();
    traverse(SB, L);

    for (auto S : L)
      LiveIn[S.first].insert(S.second.begin(), S.second.end());
  }

  if (Trace) {
    dbgs() << "\n-- " << printMBBReference(*B) << ": " << __func__
           << " after recursion into: {";
    for (auto I : *N)
      dbgs() << ' ' << I->getBlock()->getNumber();
    dbgs() << " }\n";
    dbgs() << "  LiveIn: " << Print<RefMap>(LiveIn, DFG) << '\n';
    dbgs() << "  Local:  " << Print<RegisterAggr>(LiveMap[B], DFG) << '\n';
  }

  // Add reaching defs of phi uses that are live on exit from this block.
  RefMap &PUs = PhiLOX[B];
  for (auto &S : PUs)
    LiveIn[S.first].insert(S.second.begin(), S.second.end());

  if (Trace) {
    dbgs() << "after LOX\n";
    dbgs() << "  LiveIn: " << Print<RefMap>(LiveIn, DFG) << '\n';
    dbgs() << "  Local:  " << Print<RegisterAggr>(LiveMap[B], DFG) << '\n';
  }

  // The LiveIn map at this point has all defs that are live-on-exit from B,
  // as if they were live-on-entry to B. First, we need to filter out all
  // defs that are present in this block. Then we will add reaching defs of
  // all upward-exposed uses.

  // To filter out the defs, first make a copy of LiveIn, and then re-populate
  // LiveIn with the defs that should remain.
  RefMap LiveInCopy = LiveIn;
  LiveIn.clear();

  for (const std::pair<RegisterId,NodeRefSet> &LE : LiveInCopy) {
    RegisterRef LRef(LE.first);
    NodeRefSet &NewDefs = LiveIn[LRef.Reg]; // To be filled.
    const NodeRefSet &OldDefs = LE.second;
    for (NodeRef OR : OldDefs) {
      // R is a def node that was live-on-exit
      auto DA = DFG.addr<DefNode*>(OR.first);
      NodeAddr<InstrNode*> IA = DA.Addr->getOwner(DFG);
      NodeAddr<BlockNode*> BA = IA.Addr->getOwner(DFG);
      if (B != BA.Addr->getCode()) {
        // Defs from a different block need to be preserved. Defs from this
        // block will need to be processed further, except for phi defs, the
        // liveness of which is handled through the PhiLON/PhiLOX maps.
        NewDefs.insert(OR);
        continue;
      }

      // Defs from this block need to stop the liveness from being
      // propagated upwards. This only applies to non-preserving defs,
      // and to the parts of the register actually covered by those defs.
      // (Note that phi defs should always be preserving.)
      RegisterAggr RRs(PRI);
      LRef.Mask = OR.second;

      if (!DFG.IsPreservingDef(DA)) {
        assert(!(IA.Addr->getFlags() & NodeAttrs::Phi));
        // DA is a non-phi def that is live-on-exit from this block, and
        // that is also located in this block. LRef is a register ref
        // whose use this def reaches. If DA covers LRef, then no part
        // of LRef is exposed upwards.A
        if (RRs.insert(DA.Addr->getRegRef(DFG)).hasCoverOf(LRef))
          continue;
      }

      // DA itself was not sufficient to cover LRef. In general, it is
      // the last in a chain of aliased defs before the exit from this block.
      // There could be other defs in this block that are a part of that
      // chain. Check that now: accumulate the registers from these defs,
      // and if they all together cover LRef, it is not live-on-entry.
      for (NodeAddr<DefNode*> TA : getAllReachingDefs(DA)) {
        // DefNode -> InstrNode -> BlockNode.
        NodeAddr<InstrNode*> ITA = TA.Addr->getOwner(DFG);
        NodeAddr<BlockNode*> BTA = ITA.Addr->getOwner(DFG);
        // Reaching defs are ordered in the upward direction.
        if (BTA.Addr->getCode() != B) {
          // We have reached past the beginning of B, and the accumulated
          // registers are not covering LRef. The first def from the
          // upward chain will be live.
          // Subtract all accumulated defs (RRs) from LRef.
          RegisterRef T = RRs.clearIn(LRef);
          assert(T);
          NewDefs.insert({TA.Id,T.Mask});
          break;
        }

        // TA is in B. Only add this def to the accumulated cover if it is
        // not preserving.
        if (!(TA.Addr->getFlags() & NodeAttrs::Preserving))
          RRs.insert(TA.Addr->getRegRef(DFG));
        // If this is enough to cover LRef, then stop.
        if (RRs.hasCoverOf(LRef))
          break;
      }
    }
  }

  emptify(LiveIn);

  if (Trace) {
    dbgs() << "after defs in block\n";
    dbgs() << "  LiveIn: " << Print<RefMap>(LiveIn, DFG) << '\n';
    dbgs() << "  Local:  " << Print<RegisterAggr>(LiveMap[B], DFG) << '\n';
  }

  // Scan the block for upward-exposed uses and add them to the tracking set.
  for (auto I : DFG.getFunc().Addr->findBlock(B, DFG).Addr->members(DFG)) {
    NodeAddr<InstrNode*> IA = I;
    if (IA.Addr->getKind() != NodeAttrs::Stmt)
      continue;
    for (NodeAddr<UseNode*> UA : IA.Addr->members_if(DFG.IsUse, DFG)) {
      if (UA.Addr->getFlags() & NodeAttrs::Undef)
        continue;
      RegisterRef RR = PRI.normalize(UA.Addr->getRegRef(DFG));
      for (NodeAddr<DefNode*> D : getAllReachingDefs(UA))
        if (getBlockWithRef(D.Id) != B)
          LiveIn[RR.Reg].insert({D.Id,RR.Mask});
    }
  }

  if (Trace) {
    dbgs() << "after uses in block\n";
    dbgs() << "  LiveIn: " << Print<RefMap>(LiveIn, DFG) << '\n';
    dbgs() << "  Local:  " << Print<RegisterAggr>(LiveMap[B], DFG) << '\n';
  }

  // Phi uses should not be propagated up the dominator tree, since they
  // are not dominated by their corresponding reaching defs.
  RegisterAggr &Local = LiveMap[B];
  RefMap &LON = PhiLON[B];
  for (auto &R : LON) {
    LaneBitmask M;
    for (auto P : R.second)
      M |= P.second;
    Local.insert(RegisterRef(R.first,M));
  }

  if (Trace) {
    dbgs() << "after phi uses in block\n";
    dbgs() << "  LiveIn: " << Print<RefMap>(LiveIn, DFG) << '\n';
    dbgs() << "  Local:  " << Print<RegisterAggr>(Local, DFG) << '\n';
  }

  for (auto C : IIDF[B]) {
    RegisterAggr &LiveC = LiveMap[C];
    for (const std::pair<RegisterId,NodeRefSet> &S : LiveIn)
      for (auto R : S.second)
        if (MDT.properlyDominates(getBlockWithRef(R.first), C))
          LiveC.insert(RegisterRef(S.first, R.second));
  }
}

void Liveness::emptify(RefMap &M) {
  for (auto I = M.begin(), E = M.end(); I != E; )
    I = I->second.empty() ? M.erase(I) : std::next(I);
}
