//===-- HexagonISelDAGToDAGHVX.cpp ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "HexagonISelDAGToDAG.h"
#include "HexagonISelLowering.h"
#include "HexagonTargetMachine.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#include <deque>
#include <map>
#include <set>
#include <utility>
#include <vector>

#define DEBUG_TYPE "hexagon-isel"

using namespace llvm;

namespace {

// --------------------------------------------------------------------
// Implementation of permutation networks.

// Implementation of the node routing through butterfly networks:
// - Forward delta.
// - Reverse delta.
// - Benes.
//
//
// Forward delta network consists of log(N) steps, where N is the number
// of inputs. In each step, an input can stay in place, or it can get
// routed to another position[1]. The step after that consists of two
// networks, each half in size in terms of the number of nodes. In those
// terms, in the given step, an input can go to either the upper or the
// lower network in the next step.
//
// [1] Hexagon's vdelta/vrdelta allow an element to be routed to both
// positions as long as there is no conflict.

// Here's a delta network for 8 inputs, only the switching routes are
// shown:
//
//         Steps:
//         |- 1 ---------------|- 2 -----|- 3 -|
//
// Inp[0] ***                 ***       ***   *** Out[0]
//           \               /   \     /   \ /
//            \             /     \   /     X
//             \           /       \ /     / \
// Inp[1] ***   \         /   ***   X   ***   *** Out[1]
//           \   \       /   /   \ / \ /
//            \   \     /   /     X   X
//             \   \   /   /     / \ / \
// Inp[2] ***   \   \ /   /   ***   X   ***   *** Out[2]
//           \   \   X   /   /     / \     \ /
//            \   \ / \ /   /     /   \     X
//             \   X   X   /     /     \   / \
// Inp[3] ***   \ / \ / \ /   ***       ***   *** Out[3]
//           \   X   X   X   /
//            \ / \ / \ / \ /
//             X   X   X   X
//            / \ / \ / \ / \
//           /   X   X   X   \
// Inp[4] ***   / \ / \ / \   ***       ***   *** Out[4]
//             /   X   X   \     \     /   \ /
//            /   / \ / \   \     \   /     X
//           /   /   X   \   \     \ /     / \
// Inp[5] ***   /   / \   \   ***   X   ***   *** Out[5]
//             /   /   \   \     \ / \ /
//            /   /     \   \     X   X
//           /   /       \   \   / \ / \
// Inp[6] ***   /         \   ***   X   ***   *** Out[6]
//             /           \       / \     \ /
//            /             \     /   \     X
//           /               \   /     \   / \
// Inp[7] ***                 ***       ***   *** Out[7]
//
//
// Reverse delta network is same as delta network, with the steps in
// the opposite order.
//
//
// Benes network is a forward delta network immediately followed by
// a reverse delta network.

enum class ColorKind { None, Red, Black };

// Graph coloring utility used to partition nodes into two groups:
// they will correspond to nodes routed to the upper and lower networks.
struct Coloring {
  using Node = int;
  using MapType = std::map<Node, ColorKind>;
  static constexpr Node Ignore = Node(-1);

  Coloring(ArrayRef<Node> Ord) : Order(Ord) {
    build();
    if (!color())
      Colors.clear();
  }

  const MapType &colors() const {
    return Colors;
  }

  ColorKind other(ColorKind Color) {
    if (Color == ColorKind::None)
      return ColorKind::Red;
    return Color == ColorKind::Red ? ColorKind::Black : ColorKind::Red;
  }

  LLVM_DUMP_METHOD void dump() const;

private:
  ArrayRef<Node> Order;
  MapType Colors;
  std::set<Node> Needed;

  using NodeSet = std::set<Node>;
  std::map<Node,NodeSet> Edges;

  Node conj(Node Pos) {
    Node Num = Order.size();
    return (Pos < Num/2) ? Pos + Num/2 : Pos - Num/2;
  }

  ColorKind getColor(Node N) {
    auto F = Colors.find(N);
    return F != Colors.end() ? F->second : ColorKind::None;
  }

  std::pair<bool, ColorKind> getUniqueColor(const NodeSet &Nodes);

  void build();
  bool color();
};
} // namespace

std::pair<bool, ColorKind> Coloring::getUniqueColor(const NodeSet &Nodes) {
  auto Color = ColorKind::None;
  for (Node N : Nodes) {
    ColorKind ColorN = getColor(N);
    if (ColorN == ColorKind::None)
      continue;
    if (Color == ColorKind::None)
      Color = ColorN;
    else if (Color != ColorKind::None && Color != ColorN)
      return { false, ColorKind::None };
  }
  return { true, Color };
}

void Coloring::build() {
  // Add Order[P] and Order[conj(P)] to Edges.
  for (unsigned P = 0; P != Order.size(); ++P) {
    Node I = Order[P];
    if (I != Ignore) {
      Needed.insert(I);
      Node PC = Order[conj(P)];
      if (PC != Ignore && PC != I)
        Edges[I].insert(PC);
    }
  }
  // Add I and conj(I) to Edges.
  for (unsigned I = 0; I != Order.size(); ++I) {
    if (!Needed.count(I))
      continue;
    Node C = conj(I);
    // This will create an entry in the edge table, even if I is not
    // connected to any other node. This is necessary, because it still
    // needs to be colored.
    NodeSet &Is = Edges[I];
    if (Needed.count(C))
      Is.insert(C);
  }
}

bool Coloring::color() {
  SetVector<Node> FirstQ;
  auto Enqueue = [this,&FirstQ] (Node N) {
    SetVector<Node> Q;
    Q.insert(N);
    for (unsigned I = 0; I != Q.size(); ++I) {
      NodeSet &Ns = Edges[Q[I]];
      Q.insert(Ns.begin(), Ns.end());
    }
    FirstQ.insert(Q.begin(), Q.end());
  };
  for (Node N : Needed)
    Enqueue(N);

  for (Node N : FirstQ) {
    if (Colors.count(N))
      continue;
    NodeSet &Ns = Edges[N];
    auto P = getUniqueColor(Ns);
    if (!P.first)
      return false;
    Colors[N] = other(P.second);
  }

  // First, color nodes that don't have any dups.
  for (auto E : Edges) {
    Node N = E.first;
    if (!Needed.count(conj(N)) || Colors.count(N))
      continue;
    auto P = getUniqueColor(E.second);
    if (!P.first)
      return false;
    Colors[N] = other(P.second);
  }

  // Now, nodes that are still uncolored. Since the graph can be modified
  // in this step, create a work queue.
  std::vector<Node> WorkQ;
  for (auto E : Edges) {
    Node N = E.first;
    if (!Colors.count(N))
      WorkQ.push_back(N);
  }

  for (unsigned I = 0; I < WorkQ.size(); ++I) {
    Node N = WorkQ[I];
    NodeSet &Ns = Edges[N];
    auto P = getUniqueColor(Ns);
    if (P.first) {
      Colors[N] = other(P.second);
      continue;
    }

    // Coloring failed. Split this node.
    Node C = conj(N);
    ColorKind ColorN = other(ColorKind::None);
    ColorKind ColorC = other(ColorN);
    NodeSet &Cs = Edges[C];
    NodeSet CopyNs = Ns;
    for (Node M : CopyNs) {
      ColorKind ColorM = getColor(M);
      if (ColorM == ColorC) {
        // Connect M with C, disconnect M from N.
        Cs.insert(M);
        Edges[M].insert(C);
        Ns.erase(M);
        Edges[M].erase(N);
      }
    }
    Colors[N] = ColorN;
    Colors[C] = ColorC;
  }

  // Explicitly assign "None" to all uncolored nodes.
  for (unsigned I = 0; I != Order.size(); ++I)
    if (Colors.count(I) == 0)
      Colors[I] = ColorKind::None;

  return true;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void Coloring::dump() const {
  dbgs() << "{ Order:   {";
  for (unsigned I = 0; I != Order.size(); ++I) {
    Node P = Order[I];
    if (P != Ignore)
      dbgs() << ' ' << P;
    else
      dbgs() << " -";
  }
  dbgs() << " }\n";
  dbgs() << "  Needed: {";
  for (Node N : Needed)
    dbgs() << ' ' << N;
  dbgs() << " }\n";

  dbgs() << "  Edges: {\n";
  for (auto E : Edges) {
    dbgs() << "    " << E.first << " -> {";
    for (auto N : E.second)
      dbgs() << ' ' << N;
    dbgs() << " }\n";
  }
  dbgs() << "  }\n";

  auto ColorKindToName = [](ColorKind C) {
    switch (C) {
    case ColorKind::None:
      return "None";
    case ColorKind::Red:
      return "Red";
    case ColorKind::Black:
      return "Black";
    }
    llvm_unreachable("all ColorKinds should be handled by the switch above");
  };

  dbgs() << "  Colors: {\n";
  for (auto C : Colors)
    dbgs() << "    " << C.first << " -> " << ColorKindToName(C.second) << "\n";
  dbgs() << "  }\n}\n";
}
#endif

namespace {
// Base class of for reordering networks. They don't strictly need to be
// permutations, as outputs with repeated occurrences of an input element
// are allowed.
struct PermNetwork {
  using Controls = std::vector<uint8_t>;
  using ElemType = int;
  static constexpr ElemType Ignore = ElemType(-1);

  enum : uint8_t {
    None,
    Pass,
    Switch
  };
  enum : uint8_t {
    Forward,
    Reverse
  };

  PermNetwork(ArrayRef<ElemType> Ord, unsigned Mult = 1) {
    Order.assign(Ord.data(), Ord.data()+Ord.size());
    Log = 0;

    unsigned S = Order.size();
    while (S >>= 1)
      ++Log;

    Table.resize(Order.size());
    for (RowType &Row : Table)
      Row.resize(Mult*Log, None);
  }

  void getControls(Controls &V, unsigned StartAt, uint8_t Dir) const {
    unsigned Size = Order.size();
    V.resize(Size);
    for (unsigned I = 0; I != Size; ++I) {
      unsigned W = 0;
      for (unsigned L = 0; L != Log; ++L) {
        unsigned C = ctl(I, StartAt+L) == Switch;
        if (Dir == Forward)
          W |= C << (Log-1-L);
        else
          W |= C << L;
      }
      assert(isUInt<8>(W));
      V[I] = uint8_t(W);
    }
  }

  uint8_t ctl(ElemType Pos, unsigned Step) const {
    return Table[Pos][Step];
  }
  unsigned size() const {
    return Order.size();
  }
  unsigned steps() const {
    return Log;
  }

protected:
  unsigned Log;
  std::vector<ElemType> Order;
  using RowType = std::vector<uint8_t>;
  std::vector<RowType> Table;
};

struct ForwardDeltaNetwork : public PermNetwork {
  ForwardDeltaNetwork(ArrayRef<ElemType> Ord) : PermNetwork(Ord) {}

  bool run(Controls &V) {
    if (!route(Order.data(), Table.data(), size(), 0))
      return false;
    getControls(V, 0, Forward);
    return true;
  }

private:
  bool route(ElemType *P, RowType *T, unsigned Size, unsigned Step);
};

struct ReverseDeltaNetwork : public PermNetwork {
  ReverseDeltaNetwork(ArrayRef<ElemType> Ord) : PermNetwork(Ord) {}

  bool run(Controls &V) {
    if (!route(Order.data(), Table.data(), size(), 0))
      return false;
    getControls(V, 0, Reverse);
    return true;
  }

private:
  bool route(ElemType *P, RowType *T, unsigned Size, unsigned Step);
};

struct BenesNetwork : public PermNetwork {
  BenesNetwork(ArrayRef<ElemType> Ord) : PermNetwork(Ord, 2) {}

  bool run(Controls &F, Controls &R) {
    if (!route(Order.data(), Table.data(), size(), 0))
      return false;

    getControls(F, 0, Forward);
    getControls(R, Log, Reverse);
    return true;
  }

private:
  bool route(ElemType *P, RowType *T, unsigned Size, unsigned Step);
};
} // namespace

bool ForwardDeltaNetwork::route(ElemType *P, RowType *T, unsigned Size,
                                unsigned Step) {
  bool UseUp = false, UseDown = false;
  ElemType Num = Size;

  // Cannot use coloring here, because coloring is used to determine
  // the "big" switch, i.e. the one that changes halves, and in a forward
  // network, a color can be simultaneously routed to both halves in the
  // step we're working on.
  for (ElemType J = 0; J != Num; ++J) {
    ElemType I = P[J];
    // I is the position in the input,
    // J is the position in the output.
    if (I == Ignore)
      continue;
    uint8_t S;
    if (I < Num/2)
      S = (J < Num/2) ? Pass : Switch;
    else
      S = (J < Num/2) ? Switch : Pass;

    // U is the element in the table that needs to be updated.
    ElemType U = (S == Pass) ? I : (I < Num/2 ? I+Num/2 : I-Num/2);
    if (U < Num/2)
      UseUp = true;
    else
      UseDown = true;
    if (T[U][Step] != S && T[U][Step] != None)
      return false;
    T[U][Step] = S;
  }

  for (ElemType J = 0; J != Num; ++J)
    if (P[J] != Ignore && P[J] >= Num/2)
      P[J] -= Num/2;

  if (Step+1 < Log) {
    if (UseUp   && !route(P,        T,        Size/2, Step+1))
      return false;
    if (UseDown && !route(P+Size/2, T+Size/2, Size/2, Step+1))
      return false;
  }
  return true;
}

bool ReverseDeltaNetwork::route(ElemType *P, RowType *T, unsigned Size,
                                unsigned Step) {
  unsigned Pets = Log-1 - Step;
  bool UseUp = false, UseDown = false;
  ElemType Num = Size;

  // In this step half-switching occurs, so coloring can be used.
  Coloring G({P,Size});
  const Coloring::MapType &M = G.colors();
  if (M.empty())
    return false;

  ColorKind ColorUp = ColorKind::None;
  for (ElemType J = 0; J != Num; ++J) {
    ElemType I = P[J];
    // I is the position in the input,
    // J is the position in the output.
    if (I == Ignore)
      continue;
    ColorKind C = M.at(I);
    if (C == ColorKind::None)
      continue;
    // During "Step", inputs cannot switch halves, so if the "up" color
    // is still unknown, make sure that it is selected in such a way that
    // "I" will stay in the same half.
    bool InpUp = I < Num/2;
    if (ColorUp == ColorKind::None)
      ColorUp = InpUp ? C : G.other(C);
    if ((C == ColorUp) != InpUp) {
      // If I should go to a different half than where is it now, give up.
      return false;
    }

    uint8_t S;
    if (InpUp) {
      S = (J < Num/2) ? Pass : Switch;
      UseUp = true;
    } else {
      S = (J < Num/2) ? Switch : Pass;
      UseDown = true;
    }
    T[J][Pets] = S;
  }

  // Reorder the working permutation according to the computed switch table
  // for the last step (i.e. Pets).
  for (ElemType J = 0, E = Size / 2; J != E; ++J) {
    ElemType PJ = P[J];         // Current values of P[J]
    ElemType PC = P[J+Size/2];  // and P[conj(J)]
    ElemType QJ = PJ;           // New values of P[J]
    ElemType QC = PC;           // and P[conj(J)]
    if (T[J][Pets] == Switch)
      QC = PJ;
    if (T[J+Size/2][Pets] == Switch)
      QJ = PC;
    P[J] = QJ;
    P[J+Size/2] = QC;
  }

  for (ElemType J = 0; J != Num; ++J)
    if (P[J] != Ignore && P[J] >= Num/2)
      P[J] -= Num/2;

  if (Step+1 < Log) {
    if (UseUp && !route(P, T, Size/2, Step+1))
      return false;
    if (UseDown && !route(P+Size/2, T+Size/2, Size/2, Step+1))
      return false;
  }
  return true;
}

bool BenesNetwork::route(ElemType *P, RowType *T, unsigned Size,
                         unsigned Step) {
  Coloring G({P,Size});
  const Coloring::MapType &M = G.colors();
  if (M.empty())
    return false;
  ElemType Num = Size;

  unsigned Pets = 2*Log-1 - Step;
  bool UseUp = false, UseDown = false;

  // Both assignments, i.e. Red->Up and Red->Down are valid, but they will
  // result in different controls. Let's pick the one where the first
  // control will be "Pass".
  ColorKind ColorUp = ColorKind::None;
  for (ElemType J = 0; J != Num; ++J) {
    ElemType I = P[J];
    if (I == Ignore)
      continue;
    ColorKind C = M.at(I);
    if (C == ColorKind::None)
      continue;
    if (ColorUp == ColorKind::None) {
      ColorUp = (I < Num / 2) ? ColorKind::Red : ColorKind::Black;
    }
    unsigned CI = (I < Num/2) ? I+Num/2 : I-Num/2;
    if (C == ColorUp) {
      if (I < Num/2)
        T[I][Step] = Pass;
      else
        T[CI][Step] = Switch;
      T[J][Pets] = (J < Num/2) ? Pass : Switch;
      UseUp = true;
    } else { // Down
      if (I < Num/2)
        T[CI][Step] = Switch;
      else
        T[I][Step] = Pass;
      T[J][Pets] = (J < Num/2) ? Switch : Pass;
      UseDown = true;
    }
  }

  // Reorder the working permutation according to the computed switch table
  // for the last step (i.e. Pets).
  for (ElemType J = 0; J != Num/2; ++J) {
    ElemType PJ = P[J];         // Current values of P[J]
    ElemType PC = P[J+Num/2];   // and P[conj(J)]
    ElemType QJ = PJ;           // New values of P[J]
    ElemType QC = PC;           // and P[conj(J)]
    if (T[J][Pets] == Switch)
      QC = PJ;
    if (T[J+Num/2][Pets] == Switch)
      QJ = PC;
    P[J] = QJ;
    P[J+Num/2] = QC;
  }

  for (ElemType J = 0; J != Num; ++J)
    if (P[J] != Ignore && P[J] >= Num/2)
      P[J] -= Num/2;

  if (Step+1 < Log) {
    if (UseUp && !route(P, T, Size/2, Step+1))
      return false;
    if (UseDown && !route(P+Size/2, T+Size/2, Size/2, Step+1))
      return false;
  }
  return true;
}

// --------------------------------------------------------------------
// Support for building selection results (output instructions that are
// parts of the final selection).

namespace {
struct OpRef {
  OpRef(SDValue V) : OpV(V) {}
  bool isValue() const { return OpV.getNode() != nullptr; }
  bool isValid() const { return isValue() || !(OpN & Invalid); }
  static OpRef res(int N) { return OpRef(Whole | (N & Index)); }
  static OpRef fail() { return OpRef(Invalid); }

  static OpRef lo(const OpRef &R) {
    assert(!R.isValue());
    return OpRef(R.OpN & (Undef | Index | LoHalf));
  }
  static OpRef hi(const OpRef &R) {
    assert(!R.isValue());
    return OpRef(R.OpN & (Undef | Index | HiHalf));
  }
  static OpRef undef(MVT Ty) { return OpRef(Undef | Ty.SimpleTy); }

  // Direct value.
  SDValue OpV = SDValue();

  // Reference to the operand of the input node:
  // If the 31st bit is 1, it's undef, otherwise, bits 28..0 are the
  // operand index:
  // If bit 30 is set, it's the high half of the operand.
  // If bit 29 is set, it's the low half of the operand.
  unsigned OpN = 0;

  enum : unsigned {
    Invalid = 0x10000000,
    LoHalf  = 0x20000000,
    HiHalf  = 0x40000000,
    Whole   = LoHalf | HiHalf,
    Undef   = 0x80000000,
    Index   = 0x0FFFFFFF,  // Mask of the index value.
    IndexBits = 28,
  };

  LLVM_DUMP_METHOD
  void print(raw_ostream &OS, const SelectionDAG &G) const;

private:
  OpRef(unsigned N) : OpN(N) {}
};

struct NodeTemplate {
  NodeTemplate() = default;
  unsigned Opc = 0;
  MVT Ty = MVT::Other;
  std::vector<OpRef> Ops;

  LLVM_DUMP_METHOD void print(raw_ostream &OS, const SelectionDAG &G) const;
};

struct ResultStack {
  ResultStack(SDNode *Inp)
    : InpNode(Inp), InpTy(Inp->getValueType(0).getSimpleVT()) {}
  SDNode *InpNode;
  MVT InpTy;
  unsigned push(const NodeTemplate &Res) {
    List.push_back(Res);
    return List.size()-1;
  }
  unsigned push(unsigned Opc, MVT Ty, std::vector<OpRef> &&Ops) {
    NodeTemplate Res;
    Res.Opc = Opc;
    Res.Ty = Ty;
    Res.Ops = Ops;
    return push(Res);
  }
  bool empty() const { return List.empty(); }
  unsigned size() const { return List.size(); }
  unsigned top() const { return size()-1; }
  const NodeTemplate &operator[](unsigned I) const { return List[I]; }
  unsigned reset(unsigned NewTop) {
    List.resize(NewTop+1);
    return NewTop;
  }

  using BaseType = std::vector<NodeTemplate>;
  BaseType::iterator begin() { return List.begin(); }
  BaseType::iterator end()   { return List.end(); }
  BaseType::const_iterator begin() const { return List.begin(); }
  BaseType::const_iterator end() const   { return List.end(); }

  BaseType List;

  LLVM_DUMP_METHOD
  void print(raw_ostream &OS, const SelectionDAG &G) const;
};
} // namespace

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void OpRef::print(raw_ostream &OS, const SelectionDAG &G) const {
  if (isValue()) {
    OpV.getNode()->print(OS, &G);
    return;
  }
  if (OpN & Invalid) {
    OS << "invalid";
    return;
  }
  if (OpN & Undef) {
    OS << "undef";
    return;
  }
  if ((OpN & Whole) != Whole) {
    assert((OpN & Whole) == LoHalf || (OpN & Whole) == HiHalf);
    if (OpN & LoHalf)
      OS << "lo ";
    else
      OS << "hi ";
  }
  OS << '#' << SignExtend32(OpN & Index, IndexBits);
}

void NodeTemplate::print(raw_ostream &OS, const SelectionDAG &G) const {
  const TargetInstrInfo &TII = *G.getSubtarget().getInstrInfo();
  OS << format("%8s", EVT(Ty).getEVTString().c_str()) << "  "
     << TII.getName(Opc);
  bool Comma = false;
  for (const auto &R : Ops) {
    if (Comma)
      OS << ',';
    Comma = true;
    OS << ' ';
    R.print(OS, G);
  }
}

void ResultStack::print(raw_ostream &OS, const SelectionDAG &G) const {
  OS << "Input node:\n";
#ifndef NDEBUG
  InpNode->dumpr(&G);
#endif
  OS << "Result templates:\n";
  for (unsigned I = 0, E = List.size(); I != E; ++I) {
    OS << '[' << I << "] ";
    List[I].print(OS, G);
    OS << '\n';
  }
}
#endif

namespace {
struct ShuffleMask {
  ShuffleMask(ArrayRef<int> M) : Mask(M) {
    for (unsigned I = 0, E = Mask.size(); I != E; ++I) {
      int M = Mask[I];
      if (M == -1)
        continue;
      MinSrc = (MinSrc == -1) ? M : std::min(MinSrc, M);
      MaxSrc = (MaxSrc == -1) ? M : std::max(MaxSrc, M);
    }
  }

  ArrayRef<int> Mask;
  int MinSrc = -1, MaxSrc = -1;

  ShuffleMask lo() const {
    size_t H = Mask.size()/2;
    return ShuffleMask(Mask.take_front(H));
  }
  ShuffleMask hi() const {
    size_t H = Mask.size()/2;
    return ShuffleMask(Mask.take_back(H));
  }

  void print(raw_ostream &OS) const {
    OS << "MinSrc:" << MinSrc << ", MaxSrc:" << MaxSrc << " {";
    for (int M : Mask)
      OS << ' ' << M;
    OS << " }";
  }
};
} // namespace

// --------------------------------------------------------------------
// The HvxSelector class.

static const HexagonTargetLowering &getHexagonLowering(SelectionDAG &G) {
  return static_cast<const HexagonTargetLowering&>(G.getTargetLoweringInfo());
}
static const HexagonSubtarget &getHexagonSubtarget(SelectionDAG &G) {
  return static_cast<const HexagonSubtarget&>(G.getSubtarget());
}

namespace llvm {
  struct HvxSelector {
    const HexagonTargetLowering &Lower;
    HexagonDAGToDAGISel &ISel;
    SelectionDAG &DAG;
    const HexagonSubtarget &HST;
    const unsigned HwLen;

    HvxSelector(HexagonDAGToDAGISel &HS, SelectionDAG &G)
      : Lower(getHexagonLowering(G)),  ISel(HS), DAG(G),
        HST(getHexagonSubtarget(G)), HwLen(HST.getVectorLength()) {}

    MVT getSingleVT(MVT ElemTy) const {
      unsigned NumElems = HwLen / (ElemTy.getSizeInBits()/8);
      return MVT::getVectorVT(ElemTy, NumElems);
    }

    MVT getPairVT(MVT ElemTy) const {
      unsigned NumElems = (2*HwLen) / (ElemTy.getSizeInBits()/8);
      return MVT::getVectorVT(ElemTy, NumElems);
    }

    void selectShuffle(SDNode *N);
    void selectRor(SDNode *N);
    void selectVAlign(SDNode *N);

  private:
    void materialize(const ResultStack &Results);

    SDValue getVectorConstant(ArrayRef<uint8_t> Data, const SDLoc &dl);

    enum : unsigned {
      None,
      PackMux,
    };
    OpRef concat(OpRef Va, OpRef Vb, ResultStack &Results);
    OpRef packs(ShuffleMask SM, OpRef Va, OpRef Vb, ResultStack &Results,
                MutableArrayRef<int> NewMask, unsigned Options = None);
    OpRef packp(ShuffleMask SM, OpRef Va, OpRef Vb, ResultStack &Results,
                MutableArrayRef<int> NewMask);
    OpRef vmuxs(ArrayRef<uint8_t> Bytes, OpRef Va, OpRef Vb,
                ResultStack &Results);
    OpRef vmuxp(ArrayRef<uint8_t> Bytes, OpRef Va, OpRef Vb,
                ResultStack &Results);

    OpRef shuffs1(ShuffleMask SM, OpRef Va, ResultStack &Results);
    OpRef shuffs2(ShuffleMask SM, OpRef Va, OpRef Vb, ResultStack &Results);
    OpRef shuffp1(ShuffleMask SM, OpRef Va, ResultStack &Results);
    OpRef shuffp2(ShuffleMask SM, OpRef Va, OpRef Vb, ResultStack &Results);

    OpRef butterfly(ShuffleMask SM, OpRef Va, ResultStack &Results);
    OpRef contracting(ShuffleMask SM, OpRef Va, OpRef Vb, ResultStack &Results);
    OpRef expanding(ShuffleMask SM, OpRef Va, ResultStack &Results);
    OpRef perfect(ShuffleMask SM, OpRef Va, ResultStack &Results);

    bool selectVectorConstants(SDNode *N);
    bool scalarizeShuffle(ArrayRef<int> Mask, const SDLoc &dl, MVT ResTy,
                          SDValue Va, SDValue Vb, SDNode *N);

  };
}

static void splitMask(ArrayRef<int> Mask, MutableArrayRef<int> MaskL,
                      MutableArrayRef<int> MaskR) {
  unsigned VecLen = Mask.size();
  assert(MaskL.size() == VecLen && MaskR.size() == VecLen);
  for (unsigned I = 0; I != VecLen; ++I) {
    int M = Mask[I];
    if (M < 0) {
      MaskL[I] = MaskR[I] = -1;
    } else if (unsigned(M) < VecLen) {
      MaskL[I] = M;
      MaskR[I] = -1;
    } else {
      MaskL[I] = -1;
      MaskR[I] = M-VecLen;
    }
  }
}

static std::pair<int,unsigned> findStrip(ArrayRef<int> A, int Inc,
                                         unsigned MaxLen) {
  assert(A.size() > 0 && A.size() >= MaxLen);
  int F = A[0];
  int E = F;
  for (unsigned I = 1; I != MaxLen; ++I) {
    if (A[I] - E != Inc)
      return { F, I };
    E = A[I];
  }
  return { F, MaxLen };
}

static bool isUndef(ArrayRef<int> Mask) {
  for (int Idx : Mask)
    if (Idx != -1)
      return false;
  return true;
}

static bool isIdentity(ArrayRef<int> Mask) {
  for (int I = 0, E = Mask.size(); I != E; ++I) {
    int M = Mask[I];
    if (M >= 0 && M != I)
      return false;
  }
  return true;
}

static bool isPermutation(ArrayRef<int> Mask) {
  // Check by adding all numbers only works if there is no overflow.
  assert(Mask.size() < 0x00007FFF && "Sanity failure");
  int Sum = 0;
  for (int Idx : Mask) {
    if (Idx == -1)
      return false;
    Sum += Idx;
  }
  int N = Mask.size();
  return 2*Sum == N*(N-1);
}

bool HvxSelector::selectVectorConstants(SDNode *N) {
  // Constant vectors are generated as loads from constant pools or as
  // splats of a constant value. Since they are generated during the
  // selection process, the main selection algorithm is not aware of them.
  // Select them directly here.
  SmallVector<SDNode*,4> Nodes;
  SetVector<SDNode*> WorkQ;

  // The one-use test for VSPLATW's operand may fail due to dead nodes
  // left over in the DAG.
  DAG.RemoveDeadNodes();

  // The DAG can change (due to CSE) during selection, so cache all the
  // unselected nodes first to avoid traversing a mutating DAG.

  auto IsNodeToSelect = [] (SDNode *N) {
    if (N->isMachineOpcode())
      return false;
    switch (N->getOpcode()) {
      case HexagonISD::VZERO:
      case HexagonISD::VSPLATW:
        return true;
      case ISD::LOAD: {
        SDValue Addr = cast<LoadSDNode>(N)->getBasePtr();
        unsigned AddrOpc = Addr.getOpcode();
        if (AddrOpc == HexagonISD::AT_PCREL || AddrOpc == HexagonISD::CP)
          if (Addr.getOperand(0).getOpcode() == ISD::TargetConstantPool)
            return true;
      }
      break;
    }
    // Make sure to select the operand of VSPLATW.
    bool IsSplatOp = N->hasOneUse() &&
                     N->use_begin()->getOpcode() == HexagonISD::VSPLATW;
    return IsSplatOp;
  };

  WorkQ.insert(N);
  for (unsigned i = 0; i != WorkQ.size(); ++i) {
    SDNode *W = WorkQ[i];
    if (IsNodeToSelect(W))
      Nodes.push_back(W);
    for (unsigned j = 0, f = W->getNumOperands(); j != f; ++j)
      WorkQ.insert(W->getOperand(j).getNode());
  }

  for (SDNode *L : Nodes)
    ISel.Select(L);

  return !Nodes.empty();
}

void HvxSelector::materialize(const ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {
    dbgs() << "Materializing\n";
    Results.print(dbgs(), DAG);
  });
  if (Results.empty())
    return;
  const SDLoc &dl(Results.InpNode);
  std::vector<SDValue> Output;

  for (unsigned I = 0, E = Results.size(); I != E; ++I) {
    const NodeTemplate &Node = Results[I];
    std::vector<SDValue> Ops;
    for (const OpRef &R : Node.Ops) {
      assert(R.isValid());
      if (R.isValue()) {
        Ops.push_back(R.OpV);
        continue;
      }
      if (R.OpN & OpRef::Undef) {
        MVT::SimpleValueType SVT = MVT::SimpleValueType(R.OpN & OpRef::Index);
        Ops.push_back(ISel.selectUndef(dl, MVT(SVT)));
        continue;
      }
      // R is an index of a result.
      unsigned Part = R.OpN & OpRef::Whole;
      int Idx = SignExtend32(R.OpN & OpRef::Index, OpRef::IndexBits);
      if (Idx < 0)
        Idx += I;
      assert(Idx >= 0 && unsigned(Idx) < Output.size());
      SDValue Op = Output[Idx];
      MVT OpTy = Op.getValueType().getSimpleVT();
      if (Part != OpRef::Whole) {
        assert(Part == OpRef::LoHalf || Part == OpRef::HiHalf);
        MVT HalfTy = MVT::getVectorVT(OpTy.getVectorElementType(),
                                      OpTy.getVectorNumElements()/2);
        unsigned Sub = (Part == OpRef::LoHalf) ? Hexagon::vsub_lo
                                               : Hexagon::vsub_hi;
        Op = DAG.getTargetExtractSubreg(Sub, dl, HalfTy, Op);
      }
      Ops.push_back(Op);
    } // for (Node : Results)

    assert(Node.Ty != MVT::Other);
    SDNode *ResN = (Node.Opc == TargetOpcode::COPY)
                      ? Ops.front().getNode()
                      : DAG.getMachineNode(Node.Opc, dl, Node.Ty, Ops);
    Output.push_back(SDValue(ResN, 0));
  }

  SDNode *OutN = Output.back().getNode();
  SDNode *InpN = Results.InpNode;
  DEBUG_WITH_TYPE("isel", {
    dbgs() << "Generated node:\n";
    OutN->dumpr(&DAG);
  });

  ISel.ReplaceNode(InpN, OutN);
  selectVectorConstants(OutN);
  DAG.RemoveDeadNodes();
}

OpRef HvxSelector::concat(OpRef Lo, OpRef Hi, ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  const SDLoc &dl(Results.InpNode);
  Results.push(TargetOpcode::REG_SEQUENCE, getPairVT(MVT::i8), {
    DAG.getTargetConstant(Hexagon::HvxWRRegClassID, dl, MVT::i32),
    Lo, DAG.getTargetConstant(Hexagon::vsub_lo, dl, MVT::i32),
    Hi, DAG.getTargetConstant(Hexagon::vsub_hi, dl, MVT::i32),
  });
  return OpRef::res(Results.top());
}

// Va, Vb are single vectors, SM can be arbitrarily long.
OpRef HvxSelector::packs(ShuffleMask SM, OpRef Va, OpRef Vb,
                         ResultStack &Results, MutableArrayRef<int> NewMask,
                         unsigned Options) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  if (!Va.isValid() || !Vb.isValid())
    return OpRef::fail();

  int VecLen = SM.Mask.size();
  MVT Ty = getSingleVT(MVT::i8);

  auto IsExtSubvector = [] (ShuffleMask M) {
    assert(M.MinSrc >= 0 && M.MaxSrc >= 0);
    for (int I = 0, E = M.Mask.size(); I != E; ++I) {
      if (M.Mask[I] >= 0 && M.Mask[I]-I != M.MinSrc)
        return false;
    }
    return true;
  };

  if (SM.MaxSrc - SM.MinSrc < int(HwLen)) {
    if (SM.MinSrc == 0 || SM.MinSrc == int(HwLen) || !IsExtSubvector(SM)) {
      // If the mask picks elements from only one of the operands, return
      // that operand, and update the mask to use index 0 to refer to the
      // first element of that operand.
      // If the mask extracts a subvector, it will be handled below, so
      // skip it here.
      if (SM.MaxSrc < int(HwLen)) {
        memcpy(NewMask.data(), SM.Mask.data(), sizeof(int)*VecLen);
        return Va;
      }
      if (SM.MinSrc >= int(HwLen)) {
        for (int I = 0; I != VecLen; ++I) {
          int M = SM.Mask[I];
          if (M != -1)
            M -= HwLen;
          NewMask[I] = M;
        }
        return Vb;
      }
    }
    int MinSrc = SM.MinSrc;
    if (SM.MaxSrc < int(HwLen)) {
      Vb = Va;
    } else if (SM.MinSrc > int(HwLen)) {
      Va = Vb;
      MinSrc = SM.MinSrc - HwLen;
    }
    const SDLoc &dl(Results.InpNode);
    if (isUInt<3>(MinSrc) || isUInt<3>(HwLen-MinSrc)) {
      bool IsRight = isUInt<3>(MinSrc); // Right align.
      SDValue S = DAG.getTargetConstant(IsRight ? MinSrc : HwLen-MinSrc,
                                        dl, MVT::i32);
      unsigned Opc = IsRight ? Hexagon::V6_valignbi
                             : Hexagon::V6_vlalignbi;
      Results.push(Opc, Ty, {Vb, Va, S});
    } else {
      SDValue S = DAG.getTargetConstant(MinSrc, dl, MVT::i32);
      Results.push(Hexagon::A2_tfrsi, MVT::i32, {S});
      unsigned Top = Results.top();
      Results.push(Hexagon::V6_valignb, Ty, {Vb, Va, OpRef::res(Top)});
    }
    for (int I = 0; I != VecLen; ++I) {
      int M = SM.Mask[I];
      if (M != -1)
        M -= SM.MinSrc;
      NewMask[I] = M;
    }
    return OpRef::res(Results.top());
  }

  if (Options & PackMux) {
    // If elements picked from Va and Vb have all different (source) indexes
    // (relative to the start of the argument), do a mux, and update the mask.
    BitVector Picked(HwLen);
    SmallVector<uint8_t,128> MuxBytes(HwLen);
    bool CanMux = true;
    for (int I = 0; I != VecLen; ++I) {
      int M = SM.Mask[I];
      if (M == -1)
        continue;
      if (M >= int(HwLen))
        M -= HwLen;
      else
        MuxBytes[M] = 0xFF;
      if (Picked[M]) {
        CanMux = false;
        break;
      }
      NewMask[I] = M;
    }
    if (CanMux)
      return vmuxs(MuxBytes, Va, Vb, Results);
  }

  return OpRef::fail();
}

OpRef HvxSelector::packp(ShuffleMask SM, OpRef Va, OpRef Vb,
                         ResultStack &Results, MutableArrayRef<int> NewMask) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  unsigned HalfMask = 0;
  unsigned LogHw = Log2_32(HwLen);
  for (int M : SM.Mask) {
    if (M == -1)
      continue;
    HalfMask |= (1u << (M >> LogHw));
  }

  if (HalfMask == 0)
    return OpRef::undef(getPairVT(MVT::i8));

  // If more than two halves are used, bail.
  // TODO: be more aggressive here?
  if (countPopulation(HalfMask) > 2)
    return OpRef::fail();

  MVT HalfTy = getSingleVT(MVT::i8);

  OpRef Inp[2] = { Va, Vb };
  OpRef Out[2] = { OpRef::undef(HalfTy), OpRef::undef(HalfTy) };

  uint8_t HalfIdx[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
  unsigned Idx = 0;
  for (unsigned I = 0; I != 4; ++I) {
    if ((HalfMask & (1u << I)) == 0)
      continue;
    assert(Idx < 2);
    OpRef Op = Inp[I/2];
    Out[Idx] = (I & 1) ? OpRef::hi(Op) : OpRef::lo(Op);
    HalfIdx[I] = Idx++;
  }

  int VecLen = SM.Mask.size();
  for (int I = 0; I != VecLen; ++I) {
    int M = SM.Mask[I];
    if (M >= 0) {
      uint8_t Idx = HalfIdx[M >> LogHw];
      assert(Idx == 0 || Idx == 1);
      M = (M & (HwLen-1)) + HwLen*Idx;
    }
    NewMask[I] = M;
  }

  return concat(Out[0], Out[1], Results);
}

OpRef HvxSelector::vmuxs(ArrayRef<uint8_t> Bytes, OpRef Va, OpRef Vb,
                         ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  MVT ByteTy = getSingleVT(MVT::i8);
  MVT BoolTy = MVT::getVectorVT(MVT::i1, 8*HwLen); // XXX
  const SDLoc &dl(Results.InpNode);
  SDValue B = getVectorConstant(Bytes, dl);
  Results.push(Hexagon::V6_vd0, ByteTy, {});
  Results.push(Hexagon::V6_veqb, BoolTy, {OpRef(B), OpRef::res(-1)});
  Results.push(Hexagon::V6_vmux, ByteTy, {OpRef::res(-1), Vb, Va});
  return OpRef::res(Results.top());
}

OpRef HvxSelector::vmuxp(ArrayRef<uint8_t> Bytes, OpRef Va, OpRef Vb,
                         ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  size_t S = Bytes.size() / 2;
  OpRef L = vmuxs(Bytes.take_front(S), OpRef::lo(Va), OpRef::lo(Vb), Results);
  OpRef H = vmuxs(Bytes.drop_front(S), OpRef::hi(Va), OpRef::hi(Vb), Results);
  return concat(L, H, Results);
}

OpRef HvxSelector::shuffs1(ShuffleMask SM, OpRef Va, ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  unsigned VecLen = SM.Mask.size();
  assert(HwLen == VecLen);
  (void)VecLen;
  assert(all_of(SM.Mask, [this](int M) { return M == -1 || M < int(HwLen); }));

  if (isIdentity(SM.Mask))
    return Va;
  if (isUndef(SM.Mask))
    return OpRef::undef(getSingleVT(MVT::i8));

  OpRef P = perfect(SM, Va, Results);
  if (P.isValid())
    return P;
  return butterfly(SM, Va, Results);
}

OpRef HvxSelector::shuffs2(ShuffleMask SM, OpRef Va, OpRef Vb,
                           ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  if (isUndef(SM.Mask))
    return OpRef::undef(getSingleVT(MVT::i8));

  OpRef C = contracting(SM, Va, Vb, Results);
  if (C.isValid())
    return C;

  int VecLen = SM.Mask.size();
  SmallVector<int,128> NewMask(VecLen);
  OpRef P = packs(SM, Va, Vb, Results, NewMask);
  if (P.isValid())
    return shuffs1(ShuffleMask(NewMask), P, Results);

  SmallVector<int,128> MaskL(VecLen), MaskR(VecLen);
  splitMask(SM.Mask, MaskL, MaskR);

  OpRef L = shuffs1(ShuffleMask(MaskL), Va, Results);
  OpRef R = shuffs1(ShuffleMask(MaskR), Vb, Results);
  if (!L.isValid() || !R.isValid())
    return OpRef::fail();

  SmallVector<uint8_t,128> Bytes(VecLen);
  for (int I = 0; I != VecLen; ++I) {
    if (MaskL[I] != -1)
      Bytes[I] = 0xFF;
  }
  return vmuxs(Bytes, L, R, Results);
}

OpRef HvxSelector::shuffp1(ShuffleMask SM, OpRef Va, ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  int VecLen = SM.Mask.size();

  if (isIdentity(SM.Mask))
    return Va;
  if (isUndef(SM.Mask))
    return OpRef::undef(getPairVT(MVT::i8));

  SmallVector<int,128> PackedMask(VecLen);
  OpRef P = packs(SM, OpRef::lo(Va), OpRef::hi(Va), Results, PackedMask);
  if (P.isValid()) {
    ShuffleMask PM(PackedMask);
    OpRef E = expanding(PM, P, Results);
    if (E.isValid())
      return E;

    OpRef L = shuffs1(PM.lo(), P, Results);
    OpRef H = shuffs1(PM.hi(), P, Results);
    if (L.isValid() && H.isValid())
      return concat(L, H, Results);
  }

  OpRef R = perfect(SM, Va, Results);
  if (R.isValid())
    return R;
  // TODO commute the mask and try the opposite order of the halves.

  OpRef L = shuffs2(SM.lo(), OpRef::lo(Va), OpRef::hi(Va), Results);
  OpRef H = shuffs2(SM.hi(), OpRef::lo(Va), OpRef::hi(Va), Results);
  if (L.isValid() && H.isValid())
    return concat(L, H, Results);

  return OpRef::fail();
}

OpRef HvxSelector::shuffp2(ShuffleMask SM, OpRef Va, OpRef Vb,
                           ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  if (isUndef(SM.Mask))
    return OpRef::undef(getPairVT(MVT::i8));

  int VecLen = SM.Mask.size();
  SmallVector<int,256> PackedMask(VecLen);
  OpRef P = packp(SM, Va, Vb, Results, PackedMask);
  if (P.isValid())
    return shuffp1(ShuffleMask(PackedMask), P, Results);

  SmallVector<int,256> MaskL(VecLen), MaskR(VecLen);
  splitMask(SM.Mask, MaskL, MaskR);

  OpRef L = shuffp1(ShuffleMask(MaskL), Va, Results);
  OpRef R = shuffp1(ShuffleMask(MaskR), Vb, Results);
  if (!L.isValid() || !R.isValid())
    return OpRef::fail();

  // Mux the results.
  SmallVector<uint8_t,256> Bytes(VecLen);
  for (int I = 0; I != VecLen; ++I) {
    if (MaskL[I] != -1)
      Bytes[I] = 0xFF;
  }
  return vmuxp(Bytes, L, R, Results);
}

namespace {
  struct Deleter : public SelectionDAG::DAGNodeDeletedListener {
    template <typename T>
    Deleter(SelectionDAG &D, T &C)
      : SelectionDAG::DAGNodeDeletedListener(D, [&C] (SDNode *N, SDNode *E) {
                                                  C.erase(N);
                                                }) {}
  };

  template <typename T>
  struct NullifyingVector : public T {
    DenseMap<SDNode*, SDNode**> Refs;
    NullifyingVector(T &&V) : T(V) {
      for (unsigned i = 0, e = T::size(); i != e; ++i) {
        SDNode *&N = T::operator[](i);
        Refs[N] = &N;
      }
    }
    void erase(SDNode *N) {
      auto F = Refs.find(N);
      if (F != Refs.end())
        *F->second = nullptr;
    }
  };
}

bool HvxSelector::scalarizeShuffle(ArrayRef<int> Mask, const SDLoc &dl,
                                   MVT ResTy, SDValue Va, SDValue Vb,
                                   SDNode *N) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  MVT ElemTy = ResTy.getVectorElementType();
  assert(ElemTy == MVT::i8);
  unsigned VecLen = Mask.size();
  bool HavePairs = (2*HwLen == VecLen);
  MVT SingleTy = getSingleVT(MVT::i8);

  // The prior attempts to handle this shuffle may have left a bunch of
  // dead nodes in the DAG (such as constants). These nodes will be added
  // at the end of DAG's node list, which at that point had already been
  // sorted topologically. In the main selection loop, the node list is
  // traversed backwards from the root node, which means that any new
  // nodes (from the end of the list) will not be visited.
  // Scalarization will replace the shuffle node with the scalarized
  // expression, and if that expression reused any if the leftoever (dead)
  // nodes, these nodes would not be selected (since the "local" selection
  // only visits nodes that are not in AllNodes).
  // To avoid this issue, remove all dead nodes from the DAG now.
  DAG.RemoveDeadNodes();
  DenseSet<SDNode*> AllNodes;
  for (SDNode &S : DAG.allnodes())
    AllNodes.insert(&S);

  Deleter DUA(DAG, AllNodes);

  SmallVector<SDValue,128> Ops;
  LLVMContext &Ctx = *DAG.getContext();
  MVT LegalTy = Lower.getTypeToTransformTo(Ctx, ElemTy).getSimpleVT();
  for (int I : Mask) {
    if (I < 0) {
      Ops.push_back(ISel.selectUndef(dl, LegalTy));
      continue;
    }
    SDValue Vec;
    unsigned M = I;
    if (M < VecLen) {
      Vec = Va;
    } else {
      Vec = Vb;
      M -= VecLen;
    }
    if (HavePairs) {
      if (M < HwLen) {
        Vec = DAG.getTargetExtractSubreg(Hexagon::vsub_lo, dl, SingleTy, Vec);
      } else {
        Vec = DAG.getTargetExtractSubreg(Hexagon::vsub_hi, dl, SingleTy, Vec);
        M -= HwLen;
      }
    }
    SDValue Idx = DAG.getConstant(M, dl, MVT::i32);
    SDValue Ex = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, LegalTy, {Vec, Idx});
    SDValue L = Lower.LowerOperation(Ex, DAG);
    assert(L.getNode());
    Ops.push_back(L);
  }

  SDValue LV;
  if (2*HwLen == VecLen) {
    SDValue B0 = DAG.getBuildVector(SingleTy, dl, {Ops.data(), HwLen});
    SDValue L0 = Lower.LowerOperation(B0, DAG);
    SDValue B1 = DAG.getBuildVector(SingleTy, dl, {Ops.data()+HwLen, HwLen});
    SDValue L1 = Lower.LowerOperation(B1, DAG);
    // XXX CONCAT_VECTORS is legal for HVX vectors. Legalizing (lowering)
    // functions may expect to be called only for illegal operations, so
    // make sure that they are not called for legal ones. Develop a better
    // mechanism for dealing with this.
    LV = DAG.getNode(ISD::CONCAT_VECTORS, dl, ResTy, {L0, L1});
  } else {
    SDValue BV = DAG.getBuildVector(ResTy, dl, Ops);
    LV = Lower.LowerOperation(BV, DAG);
  }

  assert(!N->use_empty());
  ISel.ReplaceNode(N, LV.getNode());

  if (AllNodes.count(LV.getNode())) {
    DAG.RemoveDeadNodes();
    return true;
  }

  // The lowered build-vector node will now need to be selected. It needs
  // to be done here because this node and its submodes are not included
  // in the main selection loop.
  // Implement essentially the same topological ordering algorithm as is
  // used in SelectionDAGISel.

  SetVector<SDNode*> SubNodes, TmpQ;
  std::map<SDNode*,unsigned> NumOps;

  SubNodes.insert(LV.getNode());
  for (unsigned I = 0; I != SubNodes.size(); ++I) {
    unsigned OpN = 0;
    SDNode *S = SubNodes[I];
    for (SDValue Op : S->ops()) {
      if (AllNodes.count(Op.getNode()))
        continue;
      SubNodes.insert(Op.getNode());
      ++OpN;
    }
    NumOps.insert({S, OpN});
    if (OpN == 0)
      TmpQ.insert(S);
  }

  for (unsigned I = 0; I != TmpQ.size(); ++I) {
    SDNode *S = TmpQ[I];
    for (SDNode *U : S->uses()) {
      if (!SubNodes.count(U))
        continue;
      auto F = NumOps.find(U);
      assert(F != NumOps.end());
      assert(F->second > 0);
      if (!--F->second)
        TmpQ.insert(F->first);
    }
  }
  assert(SubNodes.size() == TmpQ.size());
  NullifyingVector<decltype(TmpQ)::vector_type> Queue(TmpQ.takeVector());

  Deleter DUQ(DAG, Queue);
  for (SDNode *S : reverse(Queue))
    if (S != nullptr)
      ISel.Select(S);

  DAG.RemoveDeadNodes();
  return true;
}

OpRef HvxSelector::contracting(ShuffleMask SM, OpRef Va, OpRef Vb,
                               ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  if (!Va.isValid() || !Vb.isValid())
    return OpRef::fail();

  // Contracting shuffles, i.e. instructions that always discard some bytes
  // from the operand vectors.
  //
  // V6_vshuff{e,o}b
  // V6_vdealb4w
  // V6_vpack{e,o}{b,h}

  int VecLen = SM.Mask.size();
  std::pair<int,unsigned> Strip = findStrip(SM.Mask, 1, VecLen);
  MVT ResTy = getSingleVT(MVT::i8);

  // The following shuffles only work for bytes and halfwords. This requires
  // the strip length to be 1 or 2.
  if (Strip.second != 1 && Strip.second != 2)
    return OpRef::fail();

  // The patterns for the shuffles, in terms of the starting offsets of the
  // consecutive strips (L = length of the strip, N = VecLen):
  //
  // vpacke:    0, 2L, 4L ... N+0, N+2L, N+4L ...      L = 1 or 2
  // vpacko:    L, 3L, 5L ... N+L, N+3L, N+5L ...      L = 1 or 2
  //
  // vshuffe:   0, N+0, 2L, N+2L, 4L ...               L = 1 or 2
  // vshuffo:   L, N+L, 3L, N+3L, 5L ...               L = 1 or 2
  //
  // vdealb4w:  0, 4, 8 ... 2, 6, 10 ... N+0, N+4, N+8 ... N+2, N+6, N+10 ...

  // The value of the element in the mask following the strip will decide
  // what kind of a shuffle this can be.
  int NextInMask = SM.Mask[Strip.second];

  // Check if NextInMask could be 2L, 3L or 4, i.e. if it could be a mask
  // for vpack or vdealb4w. VecLen > 4, so NextInMask for vdealb4w would
  // satisfy this.
  if (NextInMask < VecLen) {
    // vpack{e,o} or vdealb4w
    if (Strip.first == 0 && Strip.second == 1 && NextInMask == 4) {
      int N = VecLen;
      // Check if this is vdealb4w (L=1).
      for (int I = 0; I != N/4; ++I)
        if (SM.Mask[I] != 4*I)
          return OpRef::fail();
      for (int I = 0; I != N/4; ++I)
        if (SM.Mask[I+N/4] != 2 + 4*I)
          return OpRef::fail();
      for (int I = 0; I != N/4; ++I)
        if (SM.Mask[I+N/2] != N + 4*I)
          return OpRef::fail();
      for (int I = 0; I != N/4; ++I)
        if (SM.Mask[I+3*N/4] != N+2 + 4*I)
          return OpRef::fail();
      // Matched mask for vdealb4w.
      Results.push(Hexagon::V6_vdealb4w, ResTy, {Vb, Va});
      return OpRef::res(Results.top());
    }

    // Check if this is vpack{e,o}.
    int N = VecLen;
    int L = Strip.second;
    // Check if the first strip starts at 0 or at L.
    if (Strip.first != 0 && Strip.first != L)
      return OpRef::fail();
    // Examine the rest of the mask.
    for (int I = L; I < N; I += L) {
      auto S = findStrip(SM.Mask.drop_front(I), 1, N-I);
      // Check whether the mask element at the beginning of each strip
      // increases by 2L each time.
      if (S.first - Strip.first != 2*I)
        return OpRef::fail();
      // Check whether each strip is of the same length.
      if (S.second != unsigned(L))
        return OpRef::fail();
    }

    // Strip.first == 0  =>  vpacke
    // Strip.first == L  =>  vpacko
    assert(Strip.first == 0 || Strip.first == L);
    using namespace Hexagon;
    NodeTemplate Res;
    Res.Opc = Strip.second == 1 // Number of bytes.
                  ? (Strip.first == 0 ? V6_vpackeb : V6_vpackob)
                  : (Strip.first == 0 ? V6_vpackeh : V6_vpackoh);
    Res.Ty = ResTy;
    Res.Ops = { Vb, Va };
    Results.push(Res);
    return OpRef::res(Results.top());
  }

  // Check if this is vshuff{e,o}.
  int N = VecLen;
  int L = Strip.second;
  std::pair<int,unsigned> PrevS = Strip;
  bool Flip = false;
  for (int I = L; I < N; I += L) {
    auto S = findStrip(SM.Mask.drop_front(I), 1, N-I);
    if (S.second != PrevS.second)
      return OpRef::fail();
    int Diff = Flip ? PrevS.first - S.first + 2*L
                    : S.first - PrevS.first;
    if (Diff != N)
      return OpRef::fail();
    Flip ^= true;
    PrevS = S;
  }
  // Strip.first == 0  =>  vshuffe
  // Strip.first == L  =>  vshuffo
  assert(Strip.first == 0 || Strip.first == L);
  using namespace Hexagon;
  NodeTemplate Res;
  Res.Opc = Strip.second == 1 // Number of bytes.
                ? (Strip.first == 0 ? V6_vshuffeb : V6_vshuffob)
                : (Strip.first == 0 ?  V6_vshufeh :  V6_vshufoh);
  Res.Ty = ResTy;
  Res.Ops = { Vb, Va };
  Results.push(Res);
  return OpRef::res(Results.top());
}

OpRef HvxSelector::expanding(ShuffleMask SM, OpRef Va, ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  // Expanding shuffles (using all elements and inserting into larger vector):
  //
  // V6_vunpacku{b,h} [*]
  //
  // [*] Only if the upper elements (filled with 0s) are "don't care" in Mask.
  //
  // Note: V6_vunpacko{b,h} are or-ing the high byte/half in the result, so
  // they are not shuffles.
  //
  // The argument is a single vector.

  int VecLen = SM.Mask.size();
  assert(2*HwLen == unsigned(VecLen) && "Expecting vector-pair type");

  std::pair<int,unsigned> Strip = findStrip(SM.Mask, 1, VecLen);

  // The patterns for the unpacks, in terms of the starting offsets of the
  // consecutive strips (L = length of the strip, N = VecLen):
  //
  // vunpacku:  0, -1, L, -1, 2L, -1 ...

  if (Strip.first != 0)
    return OpRef::fail();

  // The vunpackus only handle byte and half-word.
  if (Strip.second != 1 && Strip.second != 2)
    return OpRef::fail();

  int N = VecLen;
  int L = Strip.second;

  // First, check the non-ignored strips.
  for (int I = 2*L; I < 2*N; I += 2*L) {
    auto S = findStrip(SM.Mask.drop_front(I), 1, N-I);
    if (S.second != unsigned(L))
      return OpRef::fail();
    if (2*S.first != I)
      return OpRef::fail();
  }
  // Check the -1s.
  for (int I = L; I < 2*N; I += 2*L) {
    auto S = findStrip(SM.Mask.drop_front(I), 0, N-I);
    if (S.first != -1 || S.second != unsigned(L))
      return OpRef::fail();
  }

  unsigned Opc = Strip.second == 1 ? Hexagon::V6_vunpackub
                                   : Hexagon::V6_vunpackuh;
  Results.push(Opc, getPairVT(MVT::i8), {Va});
  return OpRef::res(Results.top());
}

OpRef HvxSelector::perfect(ShuffleMask SM, OpRef Va, ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  // V6_vdeal{b,h}
  // V6_vshuff{b,h}

  // V6_vshufoe{b,h}  those are quivalent to vshuffvdd(..,{1,2})
  // V6_vshuffvdd (V6_vshuff)
  // V6_dealvdd (V6_vdeal)

  int VecLen = SM.Mask.size();
  assert(isPowerOf2_32(VecLen) && Log2_32(VecLen) <= 8);
  unsigned LogLen = Log2_32(VecLen);
  unsigned HwLog = Log2_32(HwLen);
  // The result length must be the same as the length of a single vector,
  // or a vector pair.
  assert(LogLen == HwLog || LogLen == HwLog+1);
  bool Extend = (LogLen == HwLog);

  if (!isPermutation(SM.Mask))
    return OpRef::fail();

  SmallVector<unsigned,8> Perm(LogLen);

  // Check if this could be a perfect shuffle, or a combination of perfect
  // shuffles.
  //
  // Consider this permutation (using hex digits to make the ASCII diagrams
  // easier to read):
  //   { 0, 8, 1, 9, 2, A, 3, B, 4, C, 5, D, 6, E, 7, F }.
  // This is a "deal" operation: divide the input into two halves, and
  // create the output by picking elements by alternating between these two
  // halves:
  //   0 1 2 3 4 5 6 7    -->    0 8 1 9 2 A 3 B 4 C 5 D 6 E 7 F  [*]
  //   8 9 A B C D E F
  //
  // Aside from a few special explicit cases (V6_vdealb, etc.), HVX provides
  // a somwehat different mechanism that could be used to perform shuffle/
  // deal operations: a 2x2 transpose.
  // Consider the halves of inputs again, they can be interpreted as a 2x8
  // matrix. A 2x8 matrix can be looked at four 2x2 matrices concatenated
  // together. Now, when considering 2 elements at a time, it will be a 2x4
  // matrix (with elements 01, 23, 45, etc.), or two 2x2 matrices:
  //   01 23  45 67
  //   89 AB  CD EF
  // With groups of 4, this will become a single 2x2 matrix, and so on.
  //
  // The 2x2 transpose instruction works by transposing each of the 2x2
  // matrices (or "sub-matrices"), given a specific group size. For example,
  // if the group size is 1 (i.e. each element is its own group), there
  // will be four transposes of the four 2x2 matrices that form the 2x8.
  // For example, with the inputs as above, the result will be:
  //   0 8  2 A  4 C  6 E
  //   1 9  3 B  5 D  7 F
  // Now, this result can be tranposed again, but with the group size of 2:
  //   08 19  4C 5D
  //   2A 3B  6E 7F
  // If we then transpose that result, but with the group size of 4, we get:
  //   0819 2A3B
  //   4C5D 6E7F
  // If we concatenate these two rows, it will be
  //   0 8 1 9 2 A 3 B 4 C 5 D 6 E 7 F
  // which is the same as the "deal" [*] above.
  //
  // In general, a "deal" of individual elements is a series of 2x2 transposes,
  // with changing group size. HVX has two instructions:
  //   Vdd = V6_vdealvdd Vu, Vv, Rt
  //   Vdd = V6_shufvdd  Vu, Vv, Rt
  // that perform exactly that. The register Rt controls which transposes are
  // going to happen: a bit at position n (counting from 0) indicates that a
  // transpose with a group size of 2^n will take place. If multiple bits are
  // set, multiple transposes will happen: vdealvdd will perform them starting
  // with the largest group size, vshuffvdd will do them in the reverse order.
  //
  // The main observation is that each 2x2 transpose corresponds to swapping
  // columns of bits in the binary representation of the values.
  //
  // The numbers {3,2,1,0} and the log2 of the number of contiguous 1 bits
  // in a given column. The * denote the columns that will be swapped.
  // The transpose with the group size 2^n corresponds to swapping columns
  // 3 (the highest log) and log2(n):
  //
  //     3 2 1 0         0 2 1 3         0 2 3 1
  //     *     *             * *           * *
  //  0  0 0 0 0      0  0 0 0 0      0  0 0 0 0      0  0 0 0 0
  //  1  0 0 0 1      8  1 0 0 0      8  1 0 0 0      8  1 0 0 0
  //  2  0 0 1 0      2  0 0 1 0      1  0 0 0 1      1  0 0 0 1
  //  3  0 0 1 1      A  1 0 1 0      9  1 0 0 1      9  1 0 0 1
  //  4  0 1 0 0      4  0 1 0 0      4  0 1 0 0      2  0 0 1 0
  //  5  0 1 0 1      C  1 1 0 0      C  1 1 0 0      A  1 0 1 0
  //  6  0 1 1 0      6  0 1 1 0      5  0 1 0 1      3  0 0 1 1
  //  7  0 1 1 1      E  1 1 1 0      D  1 1 0 1      B  1 0 1 1
  //  8  1 0 0 0      1  0 0 0 1      2  0 0 1 0      4  0 1 0 0
  //  9  1 0 0 1      9  1 0 0 1      A  1 0 1 0      C  1 1 0 0
  //  A  1 0 1 0      3  0 0 1 1      3  0 0 1 1      5  0 1 0 1
  //  B  1 0 1 1      B  1 0 1 1      B  1 0 1 1      D  1 1 0 1
  //  C  1 1 0 0      5  0 1 0 1      6  0 1 1 0      6  0 1 1 0
  //  D  1 1 0 1      D  1 1 0 1      E  1 1 1 0      E  1 1 1 0
  //  E  1 1 1 0      7  0 1 1 1      7  0 1 1 1      7  0 1 1 1
  //  F  1 1 1 1      F  1 1 1 1      F  1 1 1 1      F  1 1 1 1

  auto XorPow2 = [] (ArrayRef<int> Mask, unsigned Num) {
    unsigned X = Mask[0] ^ Mask[Num/2];
    // Check that the first half has the X's bits clear.
    if ((Mask[0] & X) != 0)
      return 0u;
    for (unsigned I = 1; I != Num/2; ++I) {
      if (unsigned(Mask[I] ^ Mask[I+Num/2]) != X)
        return 0u;
      if ((Mask[I] & X) != 0)
        return 0u;
    }
    return X;
  };

  // Create a vector of log2's for each column: Perm[i] corresponds to
  // the i-th bit (lsb is 0).
  assert(VecLen > 2);
  for (unsigned I = VecLen; I >= 2; I >>= 1) {
    // Examine the initial segment of Mask of size I.
    unsigned X = XorPow2(SM.Mask, I);
    if (!isPowerOf2_32(X))
      return OpRef::fail();
    // Check the other segments of Mask.
    for (int J = I; J < VecLen; J += I) {
      if (XorPow2(SM.Mask.slice(J, I), I) != X)
        return OpRef::fail();
    }
    Perm[Log2_32(X)] = Log2_32(I)-1;
  }

  // Once we have Perm, represent it as cycles. Denote the maximum log2
  // (equal to log2(VecLen)-1) as M. The cycle containing M can then be
  // written as (M a1 a2 a3 ... an). That cycle can be broken up into
  // simple swaps as (M a1)(M a2)(M a3)...(M an), with the composition
  // order being from left to right. Any (contiguous) segment where the
  // values ai, ai+1...aj are either all increasing or all decreasing,
  // can be implemented via a single vshuffvdd/vdealvdd respectively.
  //
  // If there is a cycle (a1 a2 ... an) that does not involve M, it can
  // be written as (M an)(a1 a2 ... an)(M a1). The first two cycles can
  // then be folded to get (M a1 a2 ... an)(M a1), and the above procedure
  // can be used to generate a sequence of vshuffvdd/vdealvdd.
  //
  // Example:
  // Assume M = 4 and consider a permutation (0 1)(2 3). It can be written
  // as (4 0 1)(4 0) composed with (4 2 3)(4 2), or simply
  //   (4 0 1)(4 0)(4 2 3)(4 2).
  // It can then be expanded into swaps as
  //   (4 0)(4 1)(4 0)(4 2)(4 3)(4 2),
  // and broken up into "increasing" segments as
  //   [(4 0)(4 1)] [(4 0)(4 2)(4 3)] [(4 2)].
  // This is equivalent to
  //   (4 0 1)(4 0 2 3)(4 2),
  // which can be implemented as 3 vshufvdd instructions.

  using CycleType = SmallVector<unsigned,8>;
  std::set<CycleType> Cycles;
  std::set<unsigned> All;

  for (unsigned I : Perm)
    All.insert(I);

  // If the cycle contains LogLen-1, move it to the front of the cycle.
  // Otherwise, return the cycle unchanged.
  auto canonicalize = [LogLen](const CycleType &C) -> CycleType {
    unsigned LogPos, N = C.size();
    for (LogPos = 0; LogPos != N; ++LogPos)
      if (C[LogPos] == LogLen-1)
        break;
    if (LogPos == N)
      return C;

    CycleType NewC(C.begin()+LogPos, C.end());
    NewC.append(C.begin(), C.begin()+LogPos);
    return NewC;
  };

  auto pfs = [](const std::set<CycleType> &Cs, unsigned Len) {
    // Ordering: shuff: 5 0 1 2 3 4, deal: 5 4 3 2 1 0 (for Log=6),
    // for bytes zero is included, for halfwords is not.
    if (Cs.size() != 1)
      return 0u;
    const CycleType &C = *Cs.begin();
    if (C[0] != Len-1)
      return 0u;
    int D = Len - C.size();
    if (D != 0 && D != 1)
      return 0u;

    bool IsDeal = true, IsShuff = true;
    for (unsigned I = 1; I != Len-D; ++I) {
      if (C[I] != Len-1-I)
        IsDeal = false;
      if (C[I] != I-(1-D))  // I-1, I
        IsShuff = false;
    }
    // At most one, IsDeal or IsShuff, can be non-zero.
    assert(!(IsDeal || IsShuff) || IsDeal != IsShuff);
    static unsigned Deals[] = { Hexagon::V6_vdealb, Hexagon::V6_vdealh };
    static unsigned Shufs[] = { Hexagon::V6_vshuffb, Hexagon::V6_vshuffh };
    return IsDeal ? Deals[D] : (IsShuff ? Shufs[D] : 0);
  };

  while (!All.empty()) {
    unsigned A = *All.begin();
    All.erase(A);
    CycleType C;
    C.push_back(A);
    for (unsigned B = Perm[A]; B != A; B = Perm[B]) {
      C.push_back(B);
      All.erase(B);
    }
    if (C.size() <= 1)
      continue;
    Cycles.insert(canonicalize(C));
  }

  MVT SingleTy = getSingleVT(MVT::i8);
  MVT PairTy = getPairVT(MVT::i8);

  // Recognize patterns for V6_vdeal{b,h} and V6_vshuff{b,h}.
  if (unsigned(VecLen) == HwLen) {
    if (unsigned SingleOpc = pfs(Cycles, LogLen)) {
      Results.push(SingleOpc, SingleTy, {Va});
      return OpRef::res(Results.top());
    }
  }

  SmallVector<unsigned,8> SwapElems;
  if (HwLen == unsigned(VecLen))
    SwapElems.push_back(LogLen-1);

  for (const CycleType &C : Cycles) {
    unsigned First = (C[0] == LogLen-1) ? 1 : 0;
    SwapElems.append(C.begin()+First, C.end());
    if (First == 0)
      SwapElems.push_back(C[0]);
  }

  const SDLoc &dl(Results.InpNode);
  OpRef Arg = !Extend ? Va
                      : concat(Va, OpRef::undef(SingleTy), Results);

  for (unsigned I = 0, E = SwapElems.size(); I != E; ) {
    bool IsInc = I == E-1 || SwapElems[I] < SwapElems[I+1];
    unsigned S = (1u << SwapElems[I]);
    if (I < E-1) {
      while (++I < E-1 && IsInc == (SwapElems[I] < SwapElems[I+1]))
        S |= 1u << SwapElems[I];
      // The above loop will not add a bit for the final SwapElems[I+1],
      // so add it here.
      S |= 1u << SwapElems[I];
    }
    ++I;

    NodeTemplate Res;
    Results.push(Hexagon::A2_tfrsi, MVT::i32,
                 { DAG.getTargetConstant(S, dl, MVT::i32) });
    Res.Opc = IsInc ? Hexagon::V6_vshuffvdd : Hexagon::V6_vdealvdd;
    Res.Ty = PairTy;
    Res.Ops = { OpRef::hi(Arg), OpRef::lo(Arg), OpRef::res(-1) };
    Results.push(Res);
    Arg = OpRef::res(Results.top());
  }

  return !Extend ? Arg : OpRef::lo(Arg);
}

OpRef HvxSelector::butterfly(ShuffleMask SM, OpRef Va, ResultStack &Results) {
  DEBUG_WITH_TYPE("isel", {dbgs() << __func__ << '\n';});
  // Butterfly shuffles.
  //
  // V6_vdelta
  // V6_vrdelta
  // V6_vror

  // The assumption here is that all elements picked by Mask are in the
  // first operand to the vector_shuffle. This assumption is enforced
  // by the caller.

  MVT ResTy = getSingleVT(MVT::i8);
  PermNetwork::Controls FC, RC;
  const SDLoc &dl(Results.InpNode);
  int VecLen = SM.Mask.size();

  for (int M : SM.Mask) {
    if (M != -1 && M >= VecLen)
      return OpRef::fail();
  }

  // Try the deltas/benes for both single vectors and vector pairs.
  ForwardDeltaNetwork FN(SM.Mask);
  if (FN.run(FC)) {
    SDValue Ctl = getVectorConstant(FC, dl);
    Results.push(Hexagon::V6_vdelta, ResTy, {Va, OpRef(Ctl)});
    return OpRef::res(Results.top());
  }

  // Try reverse delta.
  ReverseDeltaNetwork RN(SM.Mask);
  if (RN.run(RC)) {
    SDValue Ctl = getVectorConstant(RC, dl);
    Results.push(Hexagon::V6_vrdelta, ResTy, {Va, OpRef(Ctl)});
    return OpRef::res(Results.top());
  }

  // Do Benes.
  BenesNetwork BN(SM.Mask);
  if (BN.run(FC, RC)) {
    SDValue CtlF = getVectorConstant(FC, dl);
    SDValue CtlR = getVectorConstant(RC, dl);
    Results.push(Hexagon::V6_vdelta, ResTy, {Va, OpRef(CtlF)});
    Results.push(Hexagon::V6_vrdelta, ResTy,
                 {OpRef::res(-1), OpRef(CtlR)});
    return OpRef::res(Results.top());
  }

  return OpRef::fail();
}

SDValue HvxSelector::getVectorConstant(ArrayRef<uint8_t> Data,
                                       const SDLoc &dl) {
  SmallVector<SDValue, 128> Elems;
  for (uint8_t C : Data)
    Elems.push_back(DAG.getConstant(C, dl, MVT::i8));
  MVT VecTy = MVT::getVectorVT(MVT::i8, Data.size());
  SDValue BV = DAG.getBuildVector(VecTy, dl, Elems);
  SDValue LV = Lower.LowerOperation(BV, DAG);
  DAG.RemoveDeadNode(BV.getNode());
  return LV;
}

void HvxSelector::selectShuffle(SDNode *N) {
  DEBUG_WITH_TYPE("isel", {
    dbgs() << "Starting " << __func__ << " on node:\n";
    N->dump(&DAG);
  });
  MVT ResTy = N->getValueType(0).getSimpleVT();
  // Assume that vector shuffles operate on vectors of bytes.
  assert(ResTy.isVector() && ResTy.getVectorElementType() == MVT::i8);

  auto *SN = cast<ShuffleVectorSDNode>(N);
  std::vector<int> Mask(SN->getMask().begin(), SN->getMask().end());
  // This shouldn't really be necessary. Is it?
  for (int &Idx : Mask)
    if (Idx != -1 && Idx < 0)
      Idx = -1;

  unsigned VecLen = Mask.size();
  bool HavePairs = (2*HwLen == VecLen);
  assert(ResTy.getSizeInBits() / 8 == VecLen);

  // Vd = vector_shuffle Va, Vb, Mask
  //

  bool UseLeft = false, UseRight = false;
  for (unsigned I = 0; I != VecLen; ++I) {
    if (Mask[I] == -1)
      continue;
    unsigned Idx = Mask[I];
    assert(Idx < 2*VecLen);
    if (Idx < VecLen)
      UseLeft = true;
    else
      UseRight = true;
  }

  DEBUG_WITH_TYPE("isel", {
    dbgs() << "VecLen=" << VecLen << " HwLen=" << HwLen << " UseLeft="
           << UseLeft << " UseRight=" << UseRight << " HavePairs="
           << HavePairs << '\n';
  });
  // If the mask is all -1's, generate "undef".
  if (!UseLeft && !UseRight) {
    ISel.ReplaceNode(N, ISel.selectUndef(SDLoc(SN), ResTy).getNode());
    return;
  }

  SDValue Vec0 = N->getOperand(0);
  SDValue Vec1 = N->getOperand(1);
  ResultStack Results(SN);
  Results.push(TargetOpcode::COPY, ResTy, {Vec0});
  Results.push(TargetOpcode::COPY, ResTy, {Vec1});
  OpRef Va = OpRef::res(Results.top()-1);
  OpRef Vb = OpRef::res(Results.top());

  OpRef Res = !HavePairs ? shuffs2(ShuffleMask(Mask), Va, Vb, Results)
                         : shuffp2(ShuffleMask(Mask), Va, Vb, Results);

  bool Done = Res.isValid();
  if (Done) {
    // Make sure that Res is on the stack before materializing.
    Results.push(TargetOpcode::COPY, ResTy, {Res});
    materialize(Results);
  } else {
    Done = scalarizeShuffle(Mask, SDLoc(N), ResTy, Vec0, Vec1, N);
  }

  if (!Done) {
#ifndef NDEBUG
    dbgs() << "Unhandled shuffle:\n";
    SN->dumpr(&DAG);
#endif
    llvm_unreachable("Failed to select vector shuffle");
  }
}

void HvxSelector::selectRor(SDNode *N) {
  // If this is a rotation by less than 8, use V6_valignbi.
  MVT Ty = N->getValueType(0).getSimpleVT();
  const SDLoc &dl(N);
  SDValue VecV = N->getOperand(0);
  SDValue RotV = N->getOperand(1);
  SDNode *NewN = nullptr;

  if (auto *CN = dyn_cast<ConstantSDNode>(RotV.getNode())) {
    unsigned S = CN->getZExtValue() % HST.getVectorLength();
    if (S == 0) {
      NewN = VecV.getNode();
    } else if (isUInt<3>(S)) {
      SDValue C = DAG.getTargetConstant(S, dl, MVT::i32);
      NewN = DAG.getMachineNode(Hexagon::V6_valignbi, dl, Ty,
                                {VecV, VecV, C});
    }
  }

  if (!NewN)
    NewN = DAG.getMachineNode(Hexagon::V6_vror, dl, Ty, {VecV, RotV});

  ISel.ReplaceNode(N, NewN);
}

void HvxSelector::selectVAlign(SDNode *N) {
  SDValue Vv = N->getOperand(0);
  SDValue Vu = N->getOperand(1);
  SDValue Rt = N->getOperand(2);
  SDNode *NewN = DAG.getMachineNode(Hexagon::V6_valignb, SDLoc(N),
                                    N->getValueType(0), {Vv, Vu, Rt});
  ISel.ReplaceNode(N, NewN);
  DAG.RemoveDeadNode(N);
}

void HexagonDAGToDAGISel::SelectHvxShuffle(SDNode *N) {
  HvxSelector(*this, *CurDAG).selectShuffle(N);
}

void HexagonDAGToDAGISel::SelectHvxRor(SDNode *N) {
  HvxSelector(*this, *CurDAG).selectRor(N);
}

void HexagonDAGToDAGISel::SelectHvxVAlign(SDNode *N) {
  HvxSelector(*this, *CurDAG).selectVAlign(N);
}

void HexagonDAGToDAGISel::SelectV65GatherPred(SDNode *N) {
  const SDLoc &dl(N);
  SDValue Chain = N->getOperand(0);
  SDValue Address = N->getOperand(2);
  SDValue Predicate = N->getOperand(3);
  SDValue Base = N->getOperand(4);
  SDValue Modifier = N->getOperand(5);
  SDValue Offset = N->getOperand(6);

  unsigned Opcode;
  unsigned IntNo = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
  switch (IntNo) {
  default:
    llvm_unreachable("Unexpected HVX gather intrinsic.");
  case Intrinsic::hexagon_V6_vgathermhq:
  case Intrinsic::hexagon_V6_vgathermhq_128B:
    Opcode = Hexagon::V6_vgathermhq_pseudo;
    break;
  case Intrinsic::hexagon_V6_vgathermwq:
  case Intrinsic::hexagon_V6_vgathermwq_128B:
    Opcode = Hexagon::V6_vgathermwq_pseudo;
    break;
  case Intrinsic::hexagon_V6_vgathermhwq:
  case Intrinsic::hexagon_V6_vgathermhwq_128B:
    Opcode = Hexagon::V6_vgathermhwq_pseudo;
    break;
  }

  SDVTList VTs = CurDAG->getVTList(MVT::Other);
  SDValue Ops[] = { Address, Predicate, Base, Modifier, Offset, Chain };
  SDNode *Result = CurDAG->getMachineNode(Opcode, dl, VTs, Ops);

  MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(Result), {MemOp});

  ReplaceNode(N, Result);
}

void HexagonDAGToDAGISel::SelectV65Gather(SDNode *N) {
  const SDLoc &dl(N);
  SDValue Chain = N->getOperand(0);
  SDValue Address = N->getOperand(2);
  SDValue Base = N->getOperand(3);
  SDValue Modifier = N->getOperand(4);
  SDValue Offset = N->getOperand(5);

  unsigned Opcode;
  unsigned IntNo = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
  switch (IntNo) {
  default:
    llvm_unreachable("Unexpected HVX gather intrinsic.");
  case Intrinsic::hexagon_V6_vgathermh:
  case Intrinsic::hexagon_V6_vgathermh_128B:
    Opcode = Hexagon::V6_vgathermh_pseudo;
    break;
  case Intrinsic::hexagon_V6_vgathermw:
  case Intrinsic::hexagon_V6_vgathermw_128B:
    Opcode = Hexagon::V6_vgathermw_pseudo;
    break;
  case Intrinsic::hexagon_V6_vgathermhw:
  case Intrinsic::hexagon_V6_vgathermhw_128B:
    Opcode = Hexagon::V6_vgathermhw_pseudo;
    break;
  }

  SDVTList VTs = CurDAG->getVTList(MVT::Other);
  SDValue Ops[] = { Address, Base, Modifier, Offset, Chain };
  SDNode *Result = CurDAG->getMachineNode(Opcode, dl, VTs, Ops);

  MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(Result), {MemOp});

  ReplaceNode(N, Result);
}

void HexagonDAGToDAGISel::SelectHVXDualOutput(SDNode *N) {
  unsigned IID = cast<ConstantSDNode>(N->getOperand(0))->getZExtValue();
  SDNode *Result;
  switch (IID) {
  case Intrinsic::hexagon_V6_vaddcarry: {
    SmallVector<SDValue, 3> Ops = { N->getOperand(1), N->getOperand(2),
                                    N->getOperand(3) };
    SDVTList VTs = CurDAG->getVTList(MVT::v16i32, MVT::v512i1);
    Result = CurDAG->getMachineNode(Hexagon::V6_vaddcarry, SDLoc(N), VTs, Ops);
    break;
  }
  case Intrinsic::hexagon_V6_vaddcarry_128B: {
    SmallVector<SDValue, 3> Ops = { N->getOperand(1), N->getOperand(2),
                                    N->getOperand(3) };
    SDVTList VTs = CurDAG->getVTList(MVT::v32i32, MVT::v1024i1);
    Result = CurDAG->getMachineNode(Hexagon::V6_vaddcarry, SDLoc(N), VTs, Ops);
    break;
  }
  case Intrinsic::hexagon_V6_vsubcarry: {
    SmallVector<SDValue, 3> Ops = { N->getOperand(1), N->getOperand(2),
                                    N->getOperand(3) };
    SDVTList VTs = CurDAG->getVTList(MVT::v16i32, MVT::v512i1);
    Result = CurDAG->getMachineNode(Hexagon::V6_vsubcarry, SDLoc(N), VTs, Ops);
    break;
  }
  case Intrinsic::hexagon_V6_vsubcarry_128B: {
    SmallVector<SDValue, 3> Ops = { N->getOperand(1), N->getOperand(2),
                                    N->getOperand(3) };
    SDVTList VTs = CurDAG->getVTList(MVT::v32i32, MVT::v1024i1);
    Result = CurDAG->getMachineNode(Hexagon::V6_vsubcarry, SDLoc(N), VTs, Ops);
    break;
  }
  default:
    llvm_unreachable("Unexpected HVX dual output intrinsic.");
  }
  ReplaceUses(N, Result);
  ReplaceUses(SDValue(N, 0), SDValue(Result, 0));
  ReplaceUses(SDValue(N, 1), SDValue(Result, 1));
  CurDAG->RemoveDeadNode(N);
}
