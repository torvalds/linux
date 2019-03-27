//===- RDFGraph.cpp -------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Target-independent, SSA-based data flow graph for register data flow (RDF).
//
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
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <set>
#include <utility>
#include <vector>

using namespace llvm;
using namespace rdf;

// Printing functions. Have them here first, so that the rest of the code
// can use them.
namespace llvm {
namespace rdf {

raw_ostream &operator<< (raw_ostream &OS, const PrintLaneMaskOpt &P) {
  if (!P.Mask.all())
    OS << ':' << PrintLaneMask(P.Mask);
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS, const Print<RegisterRef> &P) {
  auto &TRI = P.G.getTRI();
  if (P.Obj.Reg > 0 && P.Obj.Reg < TRI.getNumRegs())
    OS << TRI.getName(P.Obj.Reg);
  else
    OS << '#' << P.Obj.Reg;
  OS << PrintLaneMaskOpt(P.Obj.Mask);
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS, const Print<NodeId> &P) {
  auto NA = P.G.addr<NodeBase*>(P.Obj);
  uint16_t Attrs = NA.Addr->getAttrs();
  uint16_t Kind = NodeAttrs::kind(Attrs);
  uint16_t Flags = NodeAttrs::flags(Attrs);
  switch (NodeAttrs::type(Attrs)) {
    case NodeAttrs::Code:
      switch (Kind) {
        case NodeAttrs::Func:   OS << 'f'; break;
        case NodeAttrs::Block:  OS << 'b'; break;
        case NodeAttrs::Stmt:   OS << 's'; break;
        case NodeAttrs::Phi:    OS << 'p'; break;
        default:                OS << "c?"; break;
      }
      break;
    case NodeAttrs::Ref:
      if (Flags & NodeAttrs::Undef)
        OS << '/';
      if (Flags & NodeAttrs::Dead)
        OS << '\\';
      if (Flags & NodeAttrs::Preserving)
        OS << '+';
      if (Flags & NodeAttrs::Clobbering)
        OS << '~';
      switch (Kind) {
        case NodeAttrs::Use:    OS << 'u'; break;
        case NodeAttrs::Def:    OS << 'd'; break;
        case NodeAttrs::Block:  OS << 'b'; break;
        default:                OS << "r?"; break;
      }
      break;
    default:
      OS << '?';
      break;
  }
  OS << P.Obj;
  if (Flags & NodeAttrs::Shadow)
    OS << '"';
  return OS;
}

static void printRefHeader(raw_ostream &OS, const NodeAddr<RefNode*> RA,
                const DataFlowGraph &G) {
  OS << Print<NodeId>(RA.Id, G) << '<'
     << Print<RegisterRef>(RA.Addr->getRegRef(G), G) << '>';
  if (RA.Addr->getFlags() & NodeAttrs::Fixed)
    OS << '!';
}

template<>
raw_ostream &operator<< (raw_ostream &OS, const Print<NodeAddr<DefNode*>> &P) {
  printRefHeader(OS, P.Obj, P.G);
  OS << '(';
  if (NodeId N = P.Obj.Addr->getReachingDef())
    OS << Print<NodeId>(N, P.G);
  OS << ',';
  if (NodeId N = P.Obj.Addr->getReachedDef())
    OS << Print<NodeId>(N, P.G);
  OS << ',';
  if (NodeId N = P.Obj.Addr->getReachedUse())
    OS << Print<NodeId>(N, P.G);
  OS << "):";
  if (NodeId N = P.Obj.Addr->getSibling())
    OS << Print<NodeId>(N, P.G);
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS, const Print<NodeAddr<UseNode*>> &P) {
  printRefHeader(OS, P.Obj, P.G);
  OS << '(';
  if (NodeId N = P.Obj.Addr->getReachingDef())
    OS << Print<NodeId>(N, P.G);
  OS << "):";
  if (NodeId N = P.Obj.Addr->getSibling())
    OS << Print<NodeId>(N, P.G);
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS,
      const Print<NodeAddr<PhiUseNode*>> &P) {
  printRefHeader(OS, P.Obj, P.G);
  OS << '(';
  if (NodeId N = P.Obj.Addr->getReachingDef())
    OS << Print<NodeId>(N, P.G);
  OS << ',';
  if (NodeId N = P.Obj.Addr->getPredecessor())
    OS << Print<NodeId>(N, P.G);
  OS << "):";
  if (NodeId N = P.Obj.Addr->getSibling())
    OS << Print<NodeId>(N, P.G);
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS, const Print<NodeAddr<RefNode*>> &P) {
  switch (P.Obj.Addr->getKind()) {
    case NodeAttrs::Def:
      OS << PrintNode<DefNode*>(P.Obj, P.G);
      break;
    case NodeAttrs::Use:
      if (P.Obj.Addr->getFlags() & NodeAttrs::PhiRef)
        OS << PrintNode<PhiUseNode*>(P.Obj, P.G);
      else
        OS << PrintNode<UseNode*>(P.Obj, P.G);
      break;
  }
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS, const Print<NodeList> &P) {
  unsigned N = P.Obj.size();
  for (auto I : P.Obj) {
    OS << Print<NodeId>(I.Id, P.G);
    if (--N)
      OS << ' ';
  }
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS, const Print<NodeSet> &P) {
  unsigned N = P.Obj.size();
  for (auto I : P.Obj) {
    OS << Print<NodeId>(I, P.G);
    if (--N)
      OS << ' ';
  }
  return OS;
}

namespace {

  template <typename T>
  struct PrintListV {
    PrintListV(const NodeList &L, const DataFlowGraph &G) : List(L), G(G) {}

    using Type = T;
    const NodeList &List;
    const DataFlowGraph &G;
  };

  template <typename T>
  raw_ostream &operator<< (raw_ostream &OS, const PrintListV<T> &P) {
    unsigned N = P.List.size();
    for (NodeAddr<T> A : P.List) {
      OS << PrintNode<T>(A, P.G);
      if (--N)
        OS << ", ";
    }
    return OS;
  }

} // end anonymous namespace

template<>
raw_ostream &operator<< (raw_ostream &OS, const Print<NodeAddr<PhiNode*>> &P) {
  OS << Print<NodeId>(P.Obj.Id, P.G) << ": phi ["
     << PrintListV<RefNode*>(P.Obj.Addr->members(P.G), P.G) << ']';
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS,
      const Print<NodeAddr<StmtNode*>> &P) {
  const MachineInstr &MI = *P.Obj.Addr->getCode();
  unsigned Opc = MI.getOpcode();
  OS << Print<NodeId>(P.Obj.Id, P.G) << ": " << P.G.getTII().getName(Opc);
  // Print the target for calls and branches (for readability).
  if (MI.isCall() || MI.isBranch()) {
    MachineInstr::const_mop_iterator T =
          llvm::find_if(MI.operands(),
                        [] (const MachineOperand &Op) -> bool {
                          return Op.isMBB() || Op.isGlobal() || Op.isSymbol();
                        });
    if (T != MI.operands_end()) {
      OS << ' ';
      if (T->isMBB())
        OS << printMBBReference(*T->getMBB());
      else if (T->isGlobal())
        OS << T->getGlobal()->getName();
      else if (T->isSymbol())
        OS << T->getSymbolName();
    }
  }
  OS << " [" << PrintListV<RefNode*>(P.Obj.Addr->members(P.G), P.G) << ']';
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS,
      const Print<NodeAddr<InstrNode*>> &P) {
  switch (P.Obj.Addr->getKind()) {
    case NodeAttrs::Phi:
      OS << PrintNode<PhiNode*>(P.Obj, P.G);
      break;
    case NodeAttrs::Stmt:
      OS << PrintNode<StmtNode*>(P.Obj, P.G);
      break;
    default:
      OS << "instr? " << Print<NodeId>(P.Obj.Id, P.G);
      break;
  }
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS,
      const Print<NodeAddr<BlockNode*>> &P) {
  MachineBasicBlock *BB = P.Obj.Addr->getCode();
  unsigned NP = BB->pred_size();
  std::vector<int> Ns;
  auto PrintBBs = [&OS] (std::vector<int> Ns) -> void {
    unsigned N = Ns.size();
    for (int I : Ns) {
      OS << "%bb." << I;
      if (--N)
        OS << ", ";
    }
  };

  OS << Print<NodeId>(P.Obj.Id, P.G) << ": --- " << printMBBReference(*BB)
     << " --- preds(" << NP << "): ";
  for (MachineBasicBlock *B : BB->predecessors())
    Ns.push_back(B->getNumber());
  PrintBBs(Ns);

  unsigned NS = BB->succ_size();
  OS << "  succs(" << NS << "): ";
  Ns.clear();
  for (MachineBasicBlock *B : BB->successors())
    Ns.push_back(B->getNumber());
  PrintBBs(Ns);
  OS << '\n';

  for (auto I : P.Obj.Addr->members(P.G))
    OS << PrintNode<InstrNode*>(I, P.G) << '\n';
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS,
      const Print<NodeAddr<FuncNode*>> &P) {
  OS << "DFG dump:[\n" << Print<NodeId>(P.Obj.Id, P.G) << ": Function: "
     << P.Obj.Addr->getCode()->getName() << '\n';
  for (auto I : P.Obj.Addr->members(P.G))
    OS << PrintNode<BlockNode*>(I, P.G) << '\n';
  OS << "]\n";
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS, const Print<RegisterSet> &P) {
  OS << '{';
  for (auto I : P.Obj)
    OS << ' ' << Print<RegisterRef>(I, P.G);
  OS << " }";
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS, const Print<RegisterAggr> &P) {
  P.Obj.print(OS);
  return OS;
}

template<>
raw_ostream &operator<< (raw_ostream &OS,
      const Print<DataFlowGraph::DefStack> &P) {
  for (auto I = P.Obj.top(), E = P.Obj.bottom(); I != E; ) {
    OS << Print<NodeId>(I->Id, P.G)
       << '<' << Print<RegisterRef>(I->Addr->getRegRef(P.G), P.G) << '>';
    I.down();
    if (I != E)
      OS << ' ';
  }
  return OS;
}

} // end namespace rdf
} // end namespace llvm

// Node allocation functions.
//
// Node allocator is like a slab memory allocator: it allocates blocks of
// memory in sizes that are multiples of the size of a node. Each block has
// the same size. Nodes are allocated from the currently active block, and
// when it becomes full, a new one is created.
// There is a mapping scheme between node id and its location in a block,
// and within that block is described in the header file.
//
void NodeAllocator::startNewBlock() {
  void *T = MemPool.Allocate(NodesPerBlock*NodeMemSize, NodeMemSize);
  char *P = static_cast<char*>(T);
  Blocks.push_back(P);
  // Check if the block index is still within the allowed range, i.e. less
  // than 2^N, where N is the number of bits in NodeId for the block index.
  // BitsPerIndex is the number of bits per node index.
  assert((Blocks.size() < ((size_t)1 << (8*sizeof(NodeId)-BitsPerIndex))) &&
         "Out of bits for block index");
  ActiveEnd = P;
}

bool NodeAllocator::needNewBlock() {
  if (Blocks.empty())
    return true;

  char *ActiveBegin = Blocks.back();
  uint32_t Index = (ActiveEnd-ActiveBegin)/NodeMemSize;
  return Index >= NodesPerBlock;
}

NodeAddr<NodeBase*> NodeAllocator::New() {
  if (needNewBlock())
    startNewBlock();

  uint32_t ActiveB = Blocks.size()-1;
  uint32_t Index = (ActiveEnd - Blocks[ActiveB])/NodeMemSize;
  NodeAddr<NodeBase*> NA = { reinterpret_cast<NodeBase*>(ActiveEnd),
                             makeId(ActiveB, Index) };
  ActiveEnd += NodeMemSize;
  return NA;
}

NodeId NodeAllocator::id(const NodeBase *P) const {
  uintptr_t A = reinterpret_cast<uintptr_t>(P);
  for (unsigned i = 0, n = Blocks.size(); i != n; ++i) {
    uintptr_t B = reinterpret_cast<uintptr_t>(Blocks[i]);
    if (A < B || A >= B + NodesPerBlock*NodeMemSize)
      continue;
    uint32_t Idx = (A-B)/NodeMemSize;
    return makeId(i, Idx);
  }
  llvm_unreachable("Invalid node address");
}

void NodeAllocator::clear() {
  MemPool.Reset();
  Blocks.clear();
  ActiveEnd = nullptr;
}

// Insert node NA after "this" in the circular chain.
void NodeBase::append(NodeAddr<NodeBase*> NA) {
  NodeId Nx = Next;
  // If NA is already "next", do nothing.
  if (Next != NA.Id) {
    Next = NA.Id;
    NA.Addr->Next = Nx;
  }
}

// Fundamental node manipulator functions.

// Obtain the register reference from a reference node.
RegisterRef RefNode::getRegRef(const DataFlowGraph &G) const {
  assert(NodeAttrs::type(Attrs) == NodeAttrs::Ref);
  if (NodeAttrs::flags(Attrs) & NodeAttrs::PhiRef)
    return G.unpack(Ref.PR);
  assert(Ref.Op != nullptr);
  return G.makeRegRef(*Ref.Op);
}

// Set the register reference in the reference node directly (for references
// in phi nodes).
void RefNode::setRegRef(RegisterRef RR, DataFlowGraph &G) {
  assert(NodeAttrs::type(Attrs) == NodeAttrs::Ref);
  assert(NodeAttrs::flags(Attrs) & NodeAttrs::PhiRef);
  Ref.PR = G.pack(RR);
}

// Set the register reference in the reference node based on a machine
// operand (for references in statement nodes).
void RefNode::setRegRef(MachineOperand *Op, DataFlowGraph &G) {
  assert(NodeAttrs::type(Attrs) == NodeAttrs::Ref);
  assert(!(NodeAttrs::flags(Attrs) & NodeAttrs::PhiRef));
  (void)G;
  Ref.Op = Op;
}

// Get the owner of a given reference node.
NodeAddr<NodeBase*> RefNode::getOwner(const DataFlowGraph &G) {
  NodeAddr<NodeBase*> NA = G.addr<NodeBase*>(getNext());

  while (NA.Addr != this) {
    if (NA.Addr->getType() == NodeAttrs::Code)
      return NA;
    NA = G.addr<NodeBase*>(NA.Addr->getNext());
  }
  llvm_unreachable("No owner in circular list");
}

// Connect the def node to the reaching def node.
void DefNode::linkToDef(NodeId Self, NodeAddr<DefNode*> DA) {
  Ref.RD = DA.Id;
  Ref.Sib = DA.Addr->getReachedDef();
  DA.Addr->setReachedDef(Self);
}

// Connect the use node to the reaching def node.
void UseNode::linkToDef(NodeId Self, NodeAddr<DefNode*> DA) {
  Ref.RD = DA.Id;
  Ref.Sib = DA.Addr->getReachedUse();
  DA.Addr->setReachedUse(Self);
}

// Get the first member of the code node.
NodeAddr<NodeBase*> CodeNode::getFirstMember(const DataFlowGraph &G) const {
  if (Code.FirstM == 0)
    return NodeAddr<NodeBase*>();
  return G.addr<NodeBase*>(Code.FirstM);
}

// Get the last member of the code node.
NodeAddr<NodeBase*> CodeNode::getLastMember(const DataFlowGraph &G) const {
  if (Code.LastM == 0)
    return NodeAddr<NodeBase*>();
  return G.addr<NodeBase*>(Code.LastM);
}

// Add node NA at the end of the member list of the given code node.
void CodeNode::addMember(NodeAddr<NodeBase*> NA, const DataFlowGraph &G) {
  NodeAddr<NodeBase*> ML = getLastMember(G);
  if (ML.Id != 0) {
    ML.Addr->append(NA);
  } else {
    Code.FirstM = NA.Id;
    NodeId Self = G.id(this);
    NA.Addr->setNext(Self);
  }
  Code.LastM = NA.Id;
}

// Add node NA after member node MA in the given code node.
void CodeNode::addMemberAfter(NodeAddr<NodeBase*> MA, NodeAddr<NodeBase*> NA,
      const DataFlowGraph &G) {
  MA.Addr->append(NA);
  if (Code.LastM == MA.Id)
    Code.LastM = NA.Id;
}

// Remove member node NA from the given code node.
void CodeNode::removeMember(NodeAddr<NodeBase*> NA, const DataFlowGraph &G) {
  NodeAddr<NodeBase*> MA = getFirstMember(G);
  assert(MA.Id != 0);

  // Special handling if the member to remove is the first member.
  if (MA.Id == NA.Id) {
    if (Code.LastM == MA.Id) {
      // If it is the only member, set both first and last to 0.
      Code.FirstM = Code.LastM = 0;
    } else {
      // Otherwise, advance the first member.
      Code.FirstM = MA.Addr->getNext();
    }
    return;
  }

  while (MA.Addr != this) {
    NodeId MX = MA.Addr->getNext();
    if (MX == NA.Id) {
      MA.Addr->setNext(NA.Addr->getNext());
      // If the member to remove happens to be the last one, update the
      // LastM indicator.
      if (Code.LastM == NA.Id)
        Code.LastM = MA.Id;
      return;
    }
    MA = G.addr<NodeBase*>(MX);
  }
  llvm_unreachable("No such member");
}

// Return the list of all members of the code node.
NodeList CodeNode::members(const DataFlowGraph &G) const {
  static auto True = [] (NodeAddr<NodeBase*>) -> bool { return true; };
  return members_if(True, G);
}

// Return the owner of the given instr node.
NodeAddr<NodeBase*> InstrNode::getOwner(const DataFlowGraph &G) {
  NodeAddr<NodeBase*> NA = G.addr<NodeBase*>(getNext());

  while (NA.Addr != this) {
    assert(NA.Addr->getType() == NodeAttrs::Code);
    if (NA.Addr->getKind() == NodeAttrs::Block)
      return NA;
    NA = G.addr<NodeBase*>(NA.Addr->getNext());
  }
  llvm_unreachable("No owner in circular list");
}

// Add the phi node PA to the given block node.
void BlockNode::addPhi(NodeAddr<PhiNode*> PA, const DataFlowGraph &G) {
  NodeAddr<NodeBase*> M = getFirstMember(G);
  if (M.Id == 0) {
    addMember(PA, G);
    return;
  }

  assert(M.Addr->getType() == NodeAttrs::Code);
  if (M.Addr->getKind() == NodeAttrs::Stmt) {
    // If the first member of the block is a statement, insert the phi as
    // the first member.
    Code.FirstM = PA.Id;
    PA.Addr->setNext(M.Id);
  } else {
    // If the first member is a phi, find the last phi, and append PA to it.
    assert(M.Addr->getKind() == NodeAttrs::Phi);
    NodeAddr<NodeBase*> MN = M;
    do {
      M = MN;
      MN = G.addr<NodeBase*>(M.Addr->getNext());
      assert(MN.Addr->getType() == NodeAttrs::Code);
    } while (MN.Addr->getKind() == NodeAttrs::Phi);

    // M is the last phi.
    addMemberAfter(M, PA, G);
  }
}

// Find the block node corresponding to the machine basic block BB in the
// given func node.
NodeAddr<BlockNode*> FuncNode::findBlock(const MachineBasicBlock *BB,
      const DataFlowGraph &G) const {
  auto EqBB = [BB] (NodeAddr<NodeBase*> NA) -> bool {
    return NodeAddr<BlockNode*>(NA).Addr->getCode() == BB;
  };
  NodeList Ms = members_if(EqBB, G);
  if (!Ms.empty())
    return Ms[0];
  return NodeAddr<BlockNode*>();
}

// Get the block node for the entry block in the given function.
NodeAddr<BlockNode*> FuncNode::getEntryBlock(const DataFlowGraph &G) {
  MachineBasicBlock *EntryB = &getCode()->front();
  return findBlock(EntryB, G);
}

// Target operand information.
//

// For a given instruction, check if there are any bits of RR that can remain
// unchanged across this def.
bool TargetOperandInfo::isPreserving(const MachineInstr &In, unsigned OpNum)
      const {
  return TII.isPredicated(In);
}

// Check if the definition of RR produces an unspecified value.
bool TargetOperandInfo::isClobbering(const MachineInstr &In, unsigned OpNum)
      const {
  const MachineOperand &Op = In.getOperand(OpNum);
  if (Op.isRegMask())
    return true;
  assert(Op.isReg());
  if (In.isCall())
    if (Op.isDef() && Op.isDead())
      return true;
  return false;
}

// Check if the given instruction specifically requires
bool TargetOperandInfo::isFixedReg(const MachineInstr &In, unsigned OpNum)
      const {
  if (In.isCall() || In.isReturn() || In.isInlineAsm())
    return true;
  // Check for a tail call.
  if (In.isBranch())
    for (const MachineOperand &O : In.operands())
      if (O.isGlobal() || O.isSymbol())
        return true;

  const MCInstrDesc &D = In.getDesc();
  if (!D.getImplicitDefs() && !D.getImplicitUses())
    return false;
  const MachineOperand &Op = In.getOperand(OpNum);
  // If there is a sub-register, treat the operand as non-fixed. Currently,
  // fixed registers are those that are listed in the descriptor as implicit
  // uses or defs, and those lists do not allow sub-registers.
  if (Op.getSubReg() != 0)
    return false;
  RegisterId Reg = Op.getReg();
  const MCPhysReg *ImpR = Op.isDef() ? D.getImplicitDefs()
                                     : D.getImplicitUses();
  if (!ImpR)
    return false;
  while (*ImpR)
    if (*ImpR++ == Reg)
      return true;
  return false;
}

//
// The data flow graph construction.
//

DataFlowGraph::DataFlowGraph(MachineFunction &mf, const TargetInstrInfo &tii,
      const TargetRegisterInfo &tri, const MachineDominatorTree &mdt,
      const MachineDominanceFrontier &mdf, const TargetOperandInfo &toi)
    : MF(mf), TII(tii), TRI(tri), PRI(tri, mf), MDT(mdt), MDF(mdf), TOI(toi),
      LiveIns(PRI) {
}

// The implementation of the definition stack.
// Each register reference has its own definition stack. In particular,
// for a register references "Reg" and "Reg:subreg" will each have their
// own definition stacks.

// Construct a stack iterator.
DataFlowGraph::DefStack::Iterator::Iterator(const DataFlowGraph::DefStack &S,
      bool Top) : DS(S) {
  if (!Top) {
    // Initialize to bottom.
    Pos = 0;
    return;
  }
  // Initialize to the top, i.e. top-most non-delimiter (or 0, if empty).
  Pos = DS.Stack.size();
  while (Pos > 0 && DS.isDelimiter(DS.Stack[Pos-1]))
    Pos--;
}

// Return the size of the stack, including block delimiters.
unsigned DataFlowGraph::DefStack::size() const {
  unsigned S = 0;
  for (auto I = top(), E = bottom(); I != E; I.down())
    S++;
  return S;
}

// Remove the top entry from the stack. Remove all intervening delimiters
// so that after this, the stack is either empty, or the top of the stack
// is a non-delimiter.
void DataFlowGraph::DefStack::pop() {
  assert(!empty());
  unsigned P = nextDown(Stack.size());
  Stack.resize(P);
}

// Push a delimiter for block node N on the stack.
void DataFlowGraph::DefStack::start_block(NodeId N) {
  assert(N != 0);
  Stack.push_back(NodeAddr<DefNode*>(nullptr, N));
}

// Remove all nodes from the top of the stack, until the delimited for
// block node N is encountered. Remove the delimiter as well. In effect,
// this will remove from the stack all definitions from block N.
void DataFlowGraph::DefStack::clear_block(NodeId N) {
  assert(N != 0);
  unsigned P = Stack.size();
  while (P > 0) {
    bool Found = isDelimiter(Stack[P-1], N);
    P--;
    if (Found)
      break;
  }
  // This will also remove the delimiter, if found.
  Stack.resize(P);
}

// Move the stack iterator up by one.
unsigned DataFlowGraph::DefStack::nextUp(unsigned P) const {
  // Get the next valid position after P (skipping all delimiters).
  // The input position P does not have to point to a non-delimiter.
  unsigned SS = Stack.size();
  bool IsDelim;
  assert(P < SS);
  do {
    P++;
    IsDelim = isDelimiter(Stack[P-1]);
  } while (P < SS && IsDelim);
  assert(!IsDelim);
  return P;
}

// Move the stack iterator down by one.
unsigned DataFlowGraph::DefStack::nextDown(unsigned P) const {
  // Get the preceding valid position before P (skipping all delimiters).
  // The input position P does not have to point to a non-delimiter.
  assert(P > 0 && P <= Stack.size());
  bool IsDelim = isDelimiter(Stack[P-1]);
  do {
    if (--P == 0)
      break;
    IsDelim = isDelimiter(Stack[P-1]);
  } while (P > 0 && IsDelim);
  assert(!IsDelim);
  return P;
}

// Register information.

RegisterSet DataFlowGraph::getLandingPadLiveIns() const {
  RegisterSet LR;
  const Function &F = MF.getFunction();
  const Constant *PF = F.hasPersonalityFn() ? F.getPersonalityFn()
                                            : nullptr;
  const TargetLowering &TLI = *MF.getSubtarget().getTargetLowering();
  if (RegisterId R = TLI.getExceptionPointerRegister(PF))
    LR.insert(RegisterRef(R));
  if (RegisterId R = TLI.getExceptionSelectorRegister(PF))
    LR.insert(RegisterRef(R));
  return LR;
}

// Node management functions.

// Get the pointer to the node with the id N.
NodeBase *DataFlowGraph::ptr(NodeId N) const {
  if (N == 0)
    return nullptr;
  return Memory.ptr(N);
}

// Get the id of the node at the address P.
NodeId DataFlowGraph::id(const NodeBase *P) const {
  if (P == nullptr)
    return 0;
  return Memory.id(P);
}

// Allocate a new node and set the attributes to Attrs.
NodeAddr<NodeBase*> DataFlowGraph::newNode(uint16_t Attrs) {
  NodeAddr<NodeBase*> P = Memory.New();
  P.Addr->init();
  P.Addr->setAttrs(Attrs);
  return P;
}

// Make a copy of the given node B, except for the data-flow links, which
// are set to 0.
NodeAddr<NodeBase*> DataFlowGraph::cloneNode(const NodeAddr<NodeBase*> B) {
  NodeAddr<NodeBase*> NA = newNode(0);
  memcpy(NA.Addr, B.Addr, sizeof(NodeBase));
  // Ref nodes need to have the data-flow links reset.
  if (NA.Addr->getType() == NodeAttrs::Ref) {
    NodeAddr<RefNode*> RA = NA;
    RA.Addr->setReachingDef(0);
    RA.Addr->setSibling(0);
    if (NA.Addr->getKind() == NodeAttrs::Def) {
      NodeAddr<DefNode*> DA = NA;
      DA.Addr->setReachedDef(0);
      DA.Addr->setReachedUse(0);
    }
  }
  return NA;
}

// Allocation routines for specific node types/kinds.

NodeAddr<UseNode*> DataFlowGraph::newUse(NodeAddr<InstrNode*> Owner,
      MachineOperand &Op, uint16_t Flags) {
  NodeAddr<UseNode*> UA = newNode(NodeAttrs::Ref | NodeAttrs::Use | Flags);
  UA.Addr->setRegRef(&Op, *this);
  return UA;
}

NodeAddr<PhiUseNode*> DataFlowGraph::newPhiUse(NodeAddr<PhiNode*> Owner,
      RegisterRef RR, NodeAddr<BlockNode*> PredB, uint16_t Flags) {
  NodeAddr<PhiUseNode*> PUA = newNode(NodeAttrs::Ref | NodeAttrs::Use | Flags);
  assert(Flags & NodeAttrs::PhiRef);
  PUA.Addr->setRegRef(RR, *this);
  PUA.Addr->setPredecessor(PredB.Id);
  return PUA;
}

NodeAddr<DefNode*> DataFlowGraph::newDef(NodeAddr<InstrNode*> Owner,
      MachineOperand &Op, uint16_t Flags) {
  NodeAddr<DefNode*> DA = newNode(NodeAttrs::Ref | NodeAttrs::Def | Flags);
  DA.Addr->setRegRef(&Op, *this);
  return DA;
}

NodeAddr<DefNode*> DataFlowGraph::newDef(NodeAddr<InstrNode*> Owner,
      RegisterRef RR, uint16_t Flags) {
  NodeAddr<DefNode*> DA = newNode(NodeAttrs::Ref | NodeAttrs::Def | Flags);
  assert(Flags & NodeAttrs::PhiRef);
  DA.Addr->setRegRef(RR, *this);
  return DA;
}

NodeAddr<PhiNode*> DataFlowGraph::newPhi(NodeAddr<BlockNode*> Owner) {
  NodeAddr<PhiNode*> PA = newNode(NodeAttrs::Code | NodeAttrs::Phi);
  Owner.Addr->addPhi(PA, *this);
  return PA;
}

NodeAddr<StmtNode*> DataFlowGraph::newStmt(NodeAddr<BlockNode*> Owner,
      MachineInstr *MI) {
  NodeAddr<StmtNode*> SA = newNode(NodeAttrs::Code | NodeAttrs::Stmt);
  SA.Addr->setCode(MI);
  Owner.Addr->addMember(SA, *this);
  return SA;
}

NodeAddr<BlockNode*> DataFlowGraph::newBlock(NodeAddr<FuncNode*> Owner,
      MachineBasicBlock *BB) {
  NodeAddr<BlockNode*> BA = newNode(NodeAttrs::Code | NodeAttrs::Block);
  BA.Addr->setCode(BB);
  Owner.Addr->addMember(BA, *this);
  return BA;
}

NodeAddr<FuncNode*> DataFlowGraph::newFunc(MachineFunction *MF) {
  NodeAddr<FuncNode*> FA = newNode(NodeAttrs::Code | NodeAttrs::Func);
  FA.Addr->setCode(MF);
  return FA;
}

// Build the data flow graph.
void DataFlowGraph::build(unsigned Options) {
  reset();
  Func = newFunc(&MF);

  if (MF.empty())
    return;

  for (MachineBasicBlock &B : MF) {
    NodeAddr<BlockNode*> BA = newBlock(Func, &B);
    BlockNodes.insert(std::make_pair(&B, BA));
    for (MachineInstr &I : B) {
      if (I.isDebugInstr())
        continue;
      buildStmt(BA, I);
    }
  }

  NodeAddr<BlockNode*> EA = Func.Addr->getEntryBlock(*this);
  NodeList Blocks = Func.Addr->members(*this);

  // Collect information about block references.
  RegisterSet AllRefs;
  for (NodeAddr<BlockNode*> BA : Blocks)
    for (NodeAddr<InstrNode*> IA : BA.Addr->members(*this))
      for (NodeAddr<RefNode*> RA : IA.Addr->members(*this))
        AllRefs.insert(RA.Addr->getRegRef(*this));

  // Collect function live-ins and entry block live-ins.
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineBasicBlock &EntryB = *EA.Addr->getCode();
  assert(EntryB.pred_empty() && "Function entry block has predecessors");
  for (std::pair<unsigned,unsigned> P : MRI.liveins())
    LiveIns.insert(RegisterRef(P.first));
  if (MRI.tracksLiveness()) {
    for (auto I : EntryB.liveins())
      LiveIns.insert(RegisterRef(I.PhysReg, I.LaneMask));
  }

  // Add function-entry phi nodes for the live-in registers.
  //for (std::pair<RegisterId,LaneBitmask> P : LiveIns) {
  for (auto I = LiveIns.rr_begin(), E = LiveIns.rr_end(); I != E; ++I) {
    RegisterRef RR = *I;
    NodeAddr<PhiNode*> PA = newPhi(EA);
    uint16_t PhiFlags = NodeAttrs::PhiRef | NodeAttrs::Preserving;
    NodeAddr<DefNode*> DA = newDef(PA, RR, PhiFlags);
    PA.Addr->addMember(DA, *this);
  }

  // Add phis for landing pads.
  // Landing pads, unlike usual backs blocks, are not entered through
  // branches in the program, or fall-throughs from other blocks. They
  // are entered from the exception handling runtime and target's ABI
  // may define certain registers as defined on entry to such a block.
  RegisterSet EHRegs = getLandingPadLiveIns();
  if (!EHRegs.empty()) {
    for (NodeAddr<BlockNode*> BA : Blocks) {
      const MachineBasicBlock &B = *BA.Addr->getCode();
      if (!B.isEHPad())
        continue;

      // Prepare a list of NodeIds of the block's predecessors.
      NodeList Preds;
      for (MachineBasicBlock *PB : B.predecessors())
        Preds.push_back(findBlock(PB));

      // Build phi nodes for each live-in.
      for (RegisterRef RR : EHRegs) {
        NodeAddr<PhiNode*> PA = newPhi(BA);
        uint16_t PhiFlags = NodeAttrs::PhiRef | NodeAttrs::Preserving;
        // Add def:
        NodeAddr<DefNode*> DA = newDef(PA, RR, PhiFlags);
        PA.Addr->addMember(DA, *this);
        // Add uses (no reaching defs for phi uses):
        for (NodeAddr<BlockNode*> PBA : Preds) {
          NodeAddr<PhiUseNode*> PUA = newPhiUse(PA, RR, PBA);
          PA.Addr->addMember(PUA, *this);
        }
      }
    }
  }

  // Build a map "PhiM" which will contain, for each block, the set
  // of references that will require phi definitions in that block.
  BlockRefsMap PhiM;
  for (NodeAddr<BlockNode*> BA : Blocks)
    recordDefsForDF(PhiM, BA);
  for (NodeAddr<BlockNode*> BA : Blocks)
    buildPhis(PhiM, AllRefs, BA);

  // Link all the refs. This will recursively traverse the dominator tree.
  DefStackMap DM;
  linkBlockRefs(DM, EA);

  // Finally, remove all unused phi nodes.
  if (!(Options & BuildOptions::KeepDeadPhis))
    removeUnusedPhis();
}

RegisterRef DataFlowGraph::makeRegRef(unsigned Reg, unsigned Sub) const {
  assert(PhysicalRegisterInfo::isRegMaskId(Reg) ||
         TargetRegisterInfo::isPhysicalRegister(Reg));
  assert(Reg != 0);
  if (Sub != 0)
    Reg = TRI.getSubReg(Reg, Sub);
  return RegisterRef(Reg);
}

RegisterRef DataFlowGraph::makeRegRef(const MachineOperand &Op) const {
  assert(Op.isReg() || Op.isRegMask());
  if (Op.isReg())
    return makeRegRef(Op.getReg(), Op.getSubReg());
  return RegisterRef(PRI.getRegMaskId(Op.getRegMask()), LaneBitmask::getAll());
}

RegisterRef DataFlowGraph::restrictRef(RegisterRef AR, RegisterRef BR) const {
  if (AR.Reg == BR.Reg) {
    LaneBitmask M = AR.Mask & BR.Mask;
    return M.any() ? RegisterRef(AR.Reg, M) : RegisterRef();
  }
#ifndef NDEBUG
//  RegisterRef NAR = PRI.normalize(AR);
//  RegisterRef NBR = PRI.normalize(BR);
//  assert(NAR.Reg != NBR.Reg);
#endif
  // This isn't strictly correct, because the overlap may happen in the
  // part masked out.
  if (PRI.alias(AR, BR))
    return AR;
  return RegisterRef();
}

// For each stack in the map DefM, push the delimiter for block B on it.
void DataFlowGraph::markBlock(NodeId B, DefStackMap &DefM) {
  // Push block delimiters.
  for (auto I = DefM.begin(), E = DefM.end(); I != E; ++I)
    I->second.start_block(B);
}

// Remove all definitions coming from block B from each stack in DefM.
void DataFlowGraph::releaseBlock(NodeId B, DefStackMap &DefM) {
  // Pop all defs from this block from the definition stack. Defs that were
  // added to the map during the traversal of instructions will not have a
  // delimiter, but for those, the whole stack will be emptied.
  for (auto I = DefM.begin(), E = DefM.end(); I != E; ++I)
    I->second.clear_block(B);

  // Finally, remove empty stacks from the map.
  for (auto I = DefM.begin(), E = DefM.end(), NextI = I; I != E; I = NextI) {
    NextI = std::next(I);
    // This preserves the validity of iterators other than I.
    if (I->second.empty())
      DefM.erase(I);
  }
}

// Push all definitions from the instruction node IA to an appropriate
// stack in DefM.
void DataFlowGraph::pushAllDefs(NodeAddr<InstrNode*> IA, DefStackMap &DefM) {
  pushClobbers(IA, DefM);
  pushDefs(IA, DefM);
}

// Push all definitions from the instruction node IA to an appropriate
// stack in DefM.
void DataFlowGraph::pushClobbers(NodeAddr<InstrNode*> IA, DefStackMap &DefM) {
  NodeSet Visited;
  std::set<RegisterId> Defined;

  // The important objectives of this function are:
  // - to be able to handle instructions both while the graph is being
  //   constructed, and after the graph has been constructed, and
  // - maintain proper ordering of definitions on the stack for each
  //   register reference:
  //   - if there are two or more related defs in IA (i.e. coming from
  //     the same machine operand), then only push one def on the stack,
  //   - if there are multiple unrelated defs of non-overlapping
  //     subregisters of S, then the stack for S will have both (in an
  //     unspecified order), but the order does not matter from the data-
  //     -flow perspective.

  for (NodeAddr<DefNode*> DA : IA.Addr->members_if(IsDef, *this)) {
    if (Visited.count(DA.Id))
      continue;
    if (!(DA.Addr->getFlags() & NodeAttrs::Clobbering))
      continue;

    NodeList Rel = getRelatedRefs(IA, DA);
    NodeAddr<DefNode*> PDA = Rel.front();
    RegisterRef RR = PDA.Addr->getRegRef(*this);

    // Push the definition on the stack for the register and all aliases.
    // The def stack traversal in linkNodeUp will check the exact aliasing.
    DefM[RR.Reg].push(DA);
    Defined.insert(RR.Reg);
    for (RegisterId A : PRI.getAliasSet(RR.Reg)) {
      // Check that we don't push the same def twice.
      assert(A != RR.Reg);
      if (!Defined.count(A))
        DefM[A].push(DA);
    }
    // Mark all the related defs as visited.
    for (NodeAddr<NodeBase*> T : Rel)
      Visited.insert(T.Id);
  }
}

// Push all definitions from the instruction node IA to an appropriate
// stack in DefM.
void DataFlowGraph::pushDefs(NodeAddr<InstrNode*> IA, DefStackMap &DefM) {
  NodeSet Visited;
#ifndef NDEBUG
  std::set<RegisterId> Defined;
#endif

  // The important objectives of this function are:
  // - to be able to handle instructions both while the graph is being
  //   constructed, and after the graph has been constructed, and
  // - maintain proper ordering of definitions on the stack for each
  //   register reference:
  //   - if there are two or more related defs in IA (i.e. coming from
  //     the same machine operand), then only push one def on the stack,
  //   - if there are multiple unrelated defs of non-overlapping
  //     subregisters of S, then the stack for S will have both (in an
  //     unspecified order), but the order does not matter from the data-
  //     -flow perspective.

  for (NodeAddr<DefNode*> DA : IA.Addr->members_if(IsDef, *this)) {
    if (Visited.count(DA.Id))
      continue;
    if (DA.Addr->getFlags() & NodeAttrs::Clobbering)
      continue;

    NodeList Rel = getRelatedRefs(IA, DA);
    NodeAddr<DefNode*> PDA = Rel.front();
    RegisterRef RR = PDA.Addr->getRegRef(*this);
#ifndef NDEBUG
    // Assert if the register is defined in two or more unrelated defs.
    // This could happen if there are two or more def operands defining it.
    if (!Defined.insert(RR.Reg).second) {
      MachineInstr *MI = NodeAddr<StmtNode*>(IA).Addr->getCode();
      dbgs() << "Multiple definitions of register: "
             << Print<RegisterRef>(RR, *this) << " in\n  " << *MI << "in "
             << printMBBReference(*MI->getParent()) << '\n';
      llvm_unreachable(nullptr);
    }
#endif
    // Push the definition on the stack for the register and all aliases.
    // The def stack traversal in linkNodeUp will check the exact aliasing.
    DefM[RR.Reg].push(DA);
    for (RegisterId A : PRI.getAliasSet(RR.Reg)) {
      // Check that we don't push the same def twice.
      assert(A != RR.Reg);
      DefM[A].push(DA);
    }
    // Mark all the related defs as visited.
    for (NodeAddr<NodeBase*> T : Rel)
      Visited.insert(T.Id);
  }
}

// Return the list of all reference nodes related to RA, including RA itself.
// See "getNextRelated" for the meaning of a "related reference".
NodeList DataFlowGraph::getRelatedRefs(NodeAddr<InstrNode*> IA,
      NodeAddr<RefNode*> RA) const {
  assert(IA.Id != 0 && RA.Id != 0);

  NodeList Refs;
  NodeId Start = RA.Id;
  do {
    Refs.push_back(RA);
    RA = getNextRelated(IA, RA);
  } while (RA.Id != 0 && RA.Id != Start);
  return Refs;
}

// Clear all information in the graph.
void DataFlowGraph::reset() {
  Memory.clear();
  BlockNodes.clear();
  Func = NodeAddr<FuncNode*>();
}

// Return the next reference node in the instruction node IA that is related
// to RA. Conceptually, two reference nodes are related if they refer to the
// same instance of a register access, but differ in flags or other minor
// characteristics. Specific examples of related nodes are shadow reference
// nodes.
// Return the equivalent of nullptr if there are no more related references.
NodeAddr<RefNode*> DataFlowGraph::getNextRelated(NodeAddr<InstrNode*> IA,
      NodeAddr<RefNode*> RA) const {
  assert(IA.Id != 0 && RA.Id != 0);

  auto Related = [this,RA](NodeAddr<RefNode*> TA) -> bool {
    if (TA.Addr->getKind() != RA.Addr->getKind())
      return false;
    if (TA.Addr->getRegRef(*this) != RA.Addr->getRegRef(*this))
      return false;
    return true;
  };
  auto RelatedStmt = [&Related,RA](NodeAddr<RefNode*> TA) -> bool {
    return Related(TA) &&
           &RA.Addr->getOp() == &TA.Addr->getOp();
  };
  auto RelatedPhi = [&Related,RA](NodeAddr<RefNode*> TA) -> bool {
    if (!Related(TA))
      return false;
    if (TA.Addr->getKind() != NodeAttrs::Use)
      return true;
    // For phi uses, compare predecessor blocks.
    const NodeAddr<const PhiUseNode*> TUA = TA;
    const NodeAddr<const PhiUseNode*> RUA = RA;
    return TUA.Addr->getPredecessor() == RUA.Addr->getPredecessor();
  };

  RegisterRef RR = RA.Addr->getRegRef(*this);
  if (IA.Addr->getKind() == NodeAttrs::Stmt)
    return RA.Addr->getNextRef(RR, RelatedStmt, true, *this);
  return RA.Addr->getNextRef(RR, RelatedPhi, true, *this);
}

// Find the next node related to RA in IA that satisfies condition P.
// If such a node was found, return a pair where the second element is the
// located node. If such a node does not exist, return a pair where the
// first element is the element after which such a node should be inserted,
// and the second element is a null-address.
template <typename Predicate>
std::pair<NodeAddr<RefNode*>,NodeAddr<RefNode*>>
DataFlowGraph::locateNextRef(NodeAddr<InstrNode*> IA, NodeAddr<RefNode*> RA,
      Predicate P) const {
  assert(IA.Id != 0 && RA.Id != 0);

  NodeAddr<RefNode*> NA;
  NodeId Start = RA.Id;
  while (true) {
    NA = getNextRelated(IA, RA);
    if (NA.Id == 0 || NA.Id == Start)
      break;
    if (P(NA))
      break;
    RA = NA;
  }

  if (NA.Id != 0 && NA.Id != Start)
    return std::make_pair(RA, NA);
  return std::make_pair(RA, NodeAddr<RefNode*>());
}

// Get the next shadow node in IA corresponding to RA, and optionally create
// such a node if it does not exist.
NodeAddr<RefNode*> DataFlowGraph::getNextShadow(NodeAddr<InstrNode*> IA,
      NodeAddr<RefNode*> RA, bool Create) {
  assert(IA.Id != 0 && RA.Id != 0);

  uint16_t Flags = RA.Addr->getFlags() | NodeAttrs::Shadow;
  auto IsShadow = [Flags] (NodeAddr<RefNode*> TA) -> bool {
    return TA.Addr->getFlags() == Flags;
  };
  auto Loc = locateNextRef(IA, RA, IsShadow);
  if (Loc.second.Id != 0 || !Create)
    return Loc.second;

  // Create a copy of RA and mark is as shadow.
  NodeAddr<RefNode*> NA = cloneNode(RA);
  NA.Addr->setFlags(Flags | NodeAttrs::Shadow);
  IA.Addr->addMemberAfter(Loc.first, NA, *this);
  return NA;
}

// Get the next shadow node in IA corresponding to RA. Return null-address
// if such a node does not exist.
NodeAddr<RefNode*> DataFlowGraph::getNextShadow(NodeAddr<InstrNode*> IA,
      NodeAddr<RefNode*> RA) const {
  assert(IA.Id != 0 && RA.Id != 0);
  uint16_t Flags = RA.Addr->getFlags() | NodeAttrs::Shadow;
  auto IsShadow = [Flags] (NodeAddr<RefNode*> TA) -> bool {
    return TA.Addr->getFlags() == Flags;
  };
  return locateNextRef(IA, RA, IsShadow).second;
}

// Create a new statement node in the block node BA that corresponds to
// the machine instruction MI.
void DataFlowGraph::buildStmt(NodeAddr<BlockNode*> BA, MachineInstr &In) {
  NodeAddr<StmtNode*> SA = newStmt(BA, &In);

  auto isCall = [] (const MachineInstr &In) -> bool {
    if (In.isCall())
      return true;
    // Is tail call?
    if (In.isBranch()) {
      for (const MachineOperand &Op : In.operands())
        if (Op.isGlobal() || Op.isSymbol())
          return true;
      // Assume indirect branches are calls. This is for the purpose of
      // keeping implicit operands, and so it won't hurt on intra-function
      // indirect branches.
      if (In.isIndirectBranch())
        return true;
    }
    return false;
  };

  auto isDefUndef = [this] (const MachineInstr &In, RegisterRef DR) -> bool {
    // This instruction defines DR. Check if there is a use operand that
    // would make DR live on entry to the instruction.
    for (const MachineOperand &Op : In.operands()) {
      if (!Op.isReg() || Op.getReg() == 0 || !Op.isUse() || Op.isUndef())
        continue;
      RegisterRef UR = makeRegRef(Op);
      if (PRI.alias(DR, UR))
        return false;
    }
    return true;
  };

  bool IsCall = isCall(In);
  unsigned NumOps = In.getNumOperands();

  // Avoid duplicate implicit defs. This will not detect cases of implicit
  // defs that define registers that overlap, but it is not clear how to
  // interpret that in the absence of explicit defs. Overlapping explicit
  // defs are likely illegal already.
  BitVector DoneDefs(TRI.getNumRegs());
  // Process explicit defs first.
  for (unsigned OpN = 0; OpN < NumOps; ++OpN) {
    MachineOperand &Op = In.getOperand(OpN);
    if (!Op.isReg() || !Op.isDef() || Op.isImplicit())
      continue;
    unsigned R = Op.getReg();
    if (!R || !TargetRegisterInfo::isPhysicalRegister(R))
      continue;
    uint16_t Flags = NodeAttrs::None;
    if (TOI.isPreserving(In, OpN)) {
      Flags |= NodeAttrs::Preserving;
      // If the def is preserving, check if it is also undefined.
      if (isDefUndef(In, makeRegRef(Op)))
        Flags |= NodeAttrs::Undef;
    }
    if (TOI.isClobbering(In, OpN))
      Flags |= NodeAttrs::Clobbering;
    if (TOI.isFixedReg(In, OpN))
      Flags |= NodeAttrs::Fixed;
    if (IsCall && Op.isDead())
      Flags |= NodeAttrs::Dead;
    NodeAddr<DefNode*> DA = newDef(SA, Op, Flags);
    SA.Addr->addMember(DA, *this);
    assert(!DoneDefs.test(R));
    DoneDefs.set(R);
  }

  // Process reg-masks (as clobbers).
  BitVector DoneClobbers(TRI.getNumRegs());
  for (unsigned OpN = 0; OpN < NumOps; ++OpN) {
    MachineOperand &Op = In.getOperand(OpN);
    if (!Op.isRegMask())
      continue;
    uint16_t Flags = NodeAttrs::Clobbering | NodeAttrs::Fixed |
                     NodeAttrs::Dead;
    NodeAddr<DefNode*> DA = newDef(SA, Op, Flags);
    SA.Addr->addMember(DA, *this);
    // Record all clobbered registers in DoneDefs.
    const uint32_t *RM = Op.getRegMask();
    for (unsigned i = 1, e = TRI.getNumRegs(); i != e; ++i)
      if (!(RM[i/32] & (1u << (i%32))))
        DoneClobbers.set(i);
  }

  // Process implicit defs, skipping those that have already been added
  // as explicit.
  for (unsigned OpN = 0; OpN < NumOps; ++OpN) {
    MachineOperand &Op = In.getOperand(OpN);
    if (!Op.isReg() || !Op.isDef() || !Op.isImplicit())
      continue;
    unsigned R = Op.getReg();
    if (!R || !TargetRegisterInfo::isPhysicalRegister(R) || DoneDefs.test(R))
      continue;
    RegisterRef RR = makeRegRef(Op);
    uint16_t Flags = NodeAttrs::None;
    if (TOI.isPreserving(In, OpN)) {
      Flags |= NodeAttrs::Preserving;
      // If the def is preserving, check if it is also undefined.
      if (isDefUndef(In, RR))
        Flags |= NodeAttrs::Undef;
    }
    if (TOI.isClobbering(In, OpN))
      Flags |= NodeAttrs::Clobbering;
    if (TOI.isFixedReg(In, OpN))
      Flags |= NodeAttrs::Fixed;
    if (IsCall && Op.isDead()) {
      if (DoneClobbers.test(R))
        continue;
      Flags |= NodeAttrs::Dead;
    }
    NodeAddr<DefNode*> DA = newDef(SA, Op, Flags);
    SA.Addr->addMember(DA, *this);
    DoneDefs.set(R);
  }

  for (unsigned OpN = 0; OpN < NumOps; ++OpN) {
    MachineOperand &Op = In.getOperand(OpN);
    if (!Op.isReg() || !Op.isUse())
      continue;
    unsigned R = Op.getReg();
    if (!R || !TargetRegisterInfo::isPhysicalRegister(R))
      continue;
    uint16_t Flags = NodeAttrs::None;
    if (Op.isUndef())
      Flags |= NodeAttrs::Undef;
    if (TOI.isFixedReg(In, OpN))
      Flags |= NodeAttrs::Fixed;
    NodeAddr<UseNode*> UA = newUse(SA, Op, Flags);
    SA.Addr->addMember(UA, *this);
  }
}

// Scan all defs in the block node BA and record in PhiM the locations of
// phi nodes corresponding to these defs.
void DataFlowGraph::recordDefsForDF(BlockRefsMap &PhiM,
      NodeAddr<BlockNode*> BA) {
  // Check all defs from block BA and record them in each block in BA's
  // iterated dominance frontier. This information will later be used to
  // create phi nodes.
  MachineBasicBlock *BB = BA.Addr->getCode();
  assert(BB);
  auto DFLoc = MDF.find(BB);
  if (DFLoc == MDF.end() || DFLoc->second.empty())
    return;

  // Traverse all instructions in the block and collect the set of all
  // defined references. For each reference there will be a phi created
  // in the block's iterated dominance frontier.
  // This is done to make sure that each defined reference gets only one
  // phi node, even if it is defined multiple times.
  RegisterSet Defs;
  for (NodeAddr<InstrNode*> IA : BA.Addr->members(*this))
    for (NodeAddr<RefNode*> RA : IA.Addr->members_if(IsDef, *this))
      Defs.insert(RA.Addr->getRegRef(*this));

  // Calculate the iterated dominance frontier of BB.
  const MachineDominanceFrontier::DomSetType &DF = DFLoc->second;
  SetVector<MachineBasicBlock*> IDF(DF.begin(), DF.end());
  for (unsigned i = 0; i < IDF.size(); ++i) {
    auto F = MDF.find(IDF[i]);
    if (F != MDF.end())
      IDF.insert(F->second.begin(), F->second.end());
  }

  // Finally, add the set of defs to each block in the iterated dominance
  // frontier.
  for (auto DB : IDF) {
    NodeAddr<BlockNode*> DBA = findBlock(DB);
    PhiM[DBA.Id].insert(Defs.begin(), Defs.end());
  }
}

// Given the locations of phi nodes in the map PhiM, create the phi nodes
// that are located in the block node BA.
void DataFlowGraph::buildPhis(BlockRefsMap &PhiM, RegisterSet &AllRefs,
      NodeAddr<BlockNode*> BA) {
  // Check if this blocks has any DF defs, i.e. if there are any defs
  // that this block is in the iterated dominance frontier of.
  auto HasDF = PhiM.find(BA.Id);
  if (HasDF == PhiM.end() || HasDF->second.empty())
    return;

  // First, remove all R in Refs in such that there exists T in Refs
  // such that T covers R. In other words, only leave those refs that
  // are not covered by another ref (i.e. maximal with respect to covering).

  auto MaxCoverIn = [this] (RegisterRef RR, RegisterSet &RRs) -> RegisterRef {
    for (RegisterRef I : RRs)
      if (I != RR && RegisterAggr::isCoverOf(I, RR, PRI))
        RR = I;
    return RR;
  };

  RegisterSet MaxDF;
  for (RegisterRef I : HasDF->second)
    MaxDF.insert(MaxCoverIn(I, HasDF->second));

  std::vector<RegisterRef> MaxRefs;
  for (RegisterRef I : MaxDF)
    MaxRefs.push_back(MaxCoverIn(I, AllRefs));

  // Now, for each R in MaxRefs, get the alias closure of R. If the closure
  // only has R in it, create a phi a def for R. Otherwise, create a phi,
  // and add a def for each S in the closure.

  // Sort the refs so that the phis will be created in a deterministic order.
  llvm::sort(MaxRefs);
  // Remove duplicates.
  auto NewEnd = std::unique(MaxRefs.begin(), MaxRefs.end());
  MaxRefs.erase(NewEnd, MaxRefs.end());

  auto Aliased = [this,&MaxRefs](RegisterRef RR,
                                 std::vector<unsigned> &Closure) -> bool {
    for (unsigned I : Closure)
      if (PRI.alias(RR, MaxRefs[I]))
        return true;
    return false;
  };

  // Prepare a list of NodeIds of the block's predecessors.
  NodeList Preds;
  const MachineBasicBlock *MBB = BA.Addr->getCode();
  for (MachineBasicBlock *PB : MBB->predecessors())
    Preds.push_back(findBlock(PB));

  while (!MaxRefs.empty()) {
    // Put the first element in the closure, and then add all subsequent
    // elements from MaxRefs to it, if they alias at least one element
    // already in the closure.
    // ClosureIdx: vector of indices in MaxRefs of members of the closure.
    std::vector<unsigned> ClosureIdx = { 0 };
    for (unsigned i = 1; i != MaxRefs.size(); ++i)
      if (Aliased(MaxRefs[i], ClosureIdx))
        ClosureIdx.push_back(i);

    // Build a phi for the closure.
    unsigned CS = ClosureIdx.size();
    NodeAddr<PhiNode*> PA = newPhi(BA);

    // Add defs.
    for (unsigned X = 0; X != CS; ++X) {
      RegisterRef RR = MaxRefs[ClosureIdx[X]];
      uint16_t PhiFlags = NodeAttrs::PhiRef | NodeAttrs::Preserving;
      NodeAddr<DefNode*> DA = newDef(PA, RR, PhiFlags);
      PA.Addr->addMember(DA, *this);
    }
    // Add phi uses.
    for (NodeAddr<BlockNode*> PBA : Preds) {
      for (unsigned X = 0; X != CS; ++X) {
        RegisterRef RR = MaxRefs[ClosureIdx[X]];
        NodeAddr<PhiUseNode*> PUA = newPhiUse(PA, RR, PBA);
        PA.Addr->addMember(PUA, *this);
      }
    }

    // Erase from MaxRefs all elements in the closure.
    auto Begin = MaxRefs.begin();
    for (unsigned i = ClosureIdx.size(); i != 0; --i)
      MaxRefs.erase(Begin + ClosureIdx[i-1]);
  }
}

// Remove any unneeded phi nodes that were created during the build process.
void DataFlowGraph::removeUnusedPhis() {
  // This will remove unused phis, i.e. phis where each def does not reach
  // any uses or other defs. This will not detect or remove circular phi
  // chains that are otherwise dead. Unused/dead phis are created during
  // the build process and this function is intended to remove these cases
  // that are easily determinable to be unnecessary.

  SetVector<NodeId> PhiQ;
  for (NodeAddr<BlockNode*> BA : Func.Addr->members(*this)) {
    for (auto P : BA.Addr->members_if(IsPhi, *this))
      PhiQ.insert(P.Id);
  }

  static auto HasUsedDef = [](NodeList &Ms) -> bool {
    for (NodeAddr<NodeBase*> M : Ms) {
      if (M.Addr->getKind() != NodeAttrs::Def)
        continue;
      NodeAddr<DefNode*> DA = M;
      if (DA.Addr->getReachedDef() != 0 || DA.Addr->getReachedUse() != 0)
        return true;
    }
    return false;
  };

  // Any phi, if it is removed, may affect other phis (make them dead).
  // For each removed phi, collect the potentially affected phis and add
  // them back to the queue.
  while (!PhiQ.empty()) {
    auto PA = addr<PhiNode*>(PhiQ[0]);
    PhiQ.remove(PA.Id);
    NodeList Refs = PA.Addr->members(*this);
    if (HasUsedDef(Refs))
      continue;
    for (NodeAddr<RefNode*> RA : Refs) {
      if (NodeId RD = RA.Addr->getReachingDef()) {
        auto RDA = addr<DefNode*>(RD);
        NodeAddr<InstrNode*> OA = RDA.Addr->getOwner(*this);
        if (IsPhi(OA))
          PhiQ.insert(OA.Id);
      }
      if (RA.Addr->isDef())
        unlinkDef(RA, true);
      else
        unlinkUse(RA, true);
    }
    NodeAddr<BlockNode*> BA = PA.Addr->getOwner(*this);
    BA.Addr->removeMember(PA, *this);
  }
}

// For a given reference node TA in an instruction node IA, connect the
// reaching def of TA to the appropriate def node. Create any shadow nodes
// as appropriate.
template <typename T>
void DataFlowGraph::linkRefUp(NodeAddr<InstrNode*> IA, NodeAddr<T> TA,
      DefStack &DS) {
  if (DS.empty())
    return;
  RegisterRef RR = TA.Addr->getRegRef(*this);
  NodeAddr<T> TAP;

  // References from the def stack that have been examined so far.
  RegisterAggr Defs(PRI);

  for (auto I = DS.top(), E = DS.bottom(); I != E; I.down()) {
    RegisterRef QR = I->Addr->getRegRef(*this);

    // Skip all defs that are aliased to any of the defs that we have already
    // seen. If this completes a cover of RR, stop the stack traversal.
    bool Alias = Defs.hasAliasOf(QR);
    bool Cover = Defs.insert(QR).hasCoverOf(RR);
    if (Alias) {
      if (Cover)
        break;
      continue;
    }

    // The reaching def.
    NodeAddr<DefNode*> RDA = *I;

    // Pick the reached node.
    if (TAP.Id == 0) {
      TAP = TA;
    } else {
      // Mark the existing ref as "shadow" and create a new shadow.
      TAP.Addr->setFlags(TAP.Addr->getFlags() | NodeAttrs::Shadow);
      TAP = getNextShadow(IA, TAP, true);
    }

    // Create the link.
    TAP.Addr->linkToDef(TAP.Id, RDA);

    if (Cover)
      break;
  }
}

// Create data-flow links for all reference nodes in the statement node SA.
template <typename Predicate>
void DataFlowGraph::linkStmtRefs(DefStackMap &DefM, NodeAddr<StmtNode*> SA,
      Predicate P) {
#ifndef NDEBUG
  RegisterSet Defs;
#endif

  // Link all nodes (upwards in the data-flow) with their reaching defs.
  for (NodeAddr<RefNode*> RA : SA.Addr->members_if(P, *this)) {
    uint16_t Kind = RA.Addr->getKind();
    assert(Kind == NodeAttrs::Def || Kind == NodeAttrs::Use);
    RegisterRef RR = RA.Addr->getRegRef(*this);
#ifndef NDEBUG
    // Do not expect multiple defs of the same reference.
    assert(Kind != NodeAttrs::Def || !Defs.count(RR));
    Defs.insert(RR);
#endif

    auto F = DefM.find(RR.Reg);
    if (F == DefM.end())
      continue;
    DefStack &DS = F->second;
    if (Kind == NodeAttrs::Use)
      linkRefUp<UseNode*>(SA, RA, DS);
    else if (Kind == NodeAttrs::Def)
      linkRefUp<DefNode*>(SA, RA, DS);
    else
      llvm_unreachable("Unexpected node in instruction");
  }
}

// Create data-flow links for all instructions in the block node BA. This
// will include updating any phi nodes in BA.
void DataFlowGraph::linkBlockRefs(DefStackMap &DefM, NodeAddr<BlockNode*> BA) {
  // Push block delimiters.
  markBlock(BA.Id, DefM);

  auto IsClobber = [] (NodeAddr<RefNode*> RA) -> bool {
    return IsDef(RA) && (RA.Addr->getFlags() & NodeAttrs::Clobbering);
  };
  auto IsNoClobber = [] (NodeAddr<RefNode*> RA) -> bool {
    return IsDef(RA) && !(RA.Addr->getFlags() & NodeAttrs::Clobbering);
  };

  assert(BA.Addr && "block node address is needed to create a data-flow link");
  // For each non-phi instruction in the block, link all the defs and uses
  // to their reaching defs. For any member of the block (including phis),
  // push the defs on the corresponding stacks.
  for (NodeAddr<InstrNode*> IA : BA.Addr->members(*this)) {
    // Ignore phi nodes here. They will be linked part by part from the
    // predecessors.
    if (IA.Addr->getKind() == NodeAttrs::Stmt) {
      linkStmtRefs(DefM, IA, IsUse);
      linkStmtRefs(DefM, IA, IsClobber);
    }

    // Push the definitions on the stack.
    pushClobbers(IA, DefM);

    if (IA.Addr->getKind() == NodeAttrs::Stmt)
      linkStmtRefs(DefM, IA, IsNoClobber);

    pushDefs(IA, DefM);
  }

  // Recursively process all children in the dominator tree.
  MachineDomTreeNode *N = MDT.getNode(BA.Addr->getCode());
  for (auto I : *N) {
    MachineBasicBlock *SB = I->getBlock();
    NodeAddr<BlockNode*> SBA = findBlock(SB);
    linkBlockRefs(DefM, SBA);
  }

  // Link the phi uses from the successor blocks.
  auto IsUseForBA = [BA](NodeAddr<NodeBase*> NA) -> bool {
    if (NA.Addr->getKind() != NodeAttrs::Use)
      return false;
    assert(NA.Addr->getFlags() & NodeAttrs::PhiRef);
    NodeAddr<PhiUseNode*> PUA = NA;
    return PUA.Addr->getPredecessor() == BA.Id;
  };

  RegisterSet EHLiveIns = getLandingPadLiveIns();
  MachineBasicBlock *MBB = BA.Addr->getCode();

  for (MachineBasicBlock *SB : MBB->successors()) {
    bool IsEHPad = SB->isEHPad();
    NodeAddr<BlockNode*> SBA = findBlock(SB);
    for (NodeAddr<InstrNode*> IA : SBA.Addr->members_if(IsPhi, *this)) {
      // Do not link phi uses for landing pad live-ins.
      if (IsEHPad) {
        // Find what register this phi is for.
        NodeAddr<RefNode*> RA = IA.Addr->getFirstMember(*this);
        assert(RA.Id != 0);
        if (EHLiveIns.count(RA.Addr->getRegRef(*this)))
          continue;
      }
      // Go over each phi use associated with MBB, and link it.
      for (auto U : IA.Addr->members_if(IsUseForBA, *this)) {
        NodeAddr<PhiUseNode*> PUA = U;
        RegisterRef RR = PUA.Addr->getRegRef(*this);
        linkRefUp<UseNode*>(IA, PUA, DefM[RR.Reg]);
      }
    }
  }

  // Pop all defs from this block from the definition stacks.
  releaseBlock(BA.Id, DefM);
}

// Remove the use node UA from any data-flow and structural links.
void DataFlowGraph::unlinkUseDF(NodeAddr<UseNode*> UA) {
  NodeId RD = UA.Addr->getReachingDef();
  NodeId Sib = UA.Addr->getSibling();

  if (RD == 0) {
    assert(Sib == 0);
    return;
  }

  auto RDA = addr<DefNode*>(RD);
  auto TA = addr<UseNode*>(RDA.Addr->getReachedUse());
  if (TA.Id == UA.Id) {
    RDA.Addr->setReachedUse(Sib);
    return;
  }

  while (TA.Id != 0) {
    NodeId S = TA.Addr->getSibling();
    if (S == UA.Id) {
      TA.Addr->setSibling(UA.Addr->getSibling());
      return;
    }
    TA = addr<UseNode*>(S);
  }
}

// Remove the def node DA from any data-flow and structural links.
void DataFlowGraph::unlinkDefDF(NodeAddr<DefNode*> DA) {
  //
  //         RD
  //         | reached
  //         | def
  //         :
  //         .
  //        +----+
  // ... -- | DA | -- ... -- 0  : sibling chain of DA
  //        +----+
  //         |  | reached
  //         |  : def
  //         |  .
  //         | ...  : Siblings (defs)
  //         |
  //         : reached
  //         . use
  //        ... : sibling chain of reached uses

  NodeId RD = DA.Addr->getReachingDef();

  // Visit all siblings of the reached def and reset their reaching defs.
  // Also, defs reached by DA are now "promoted" to being reached by RD,
  // so all of them will need to be spliced into the sibling chain where
  // DA belongs.
  auto getAllNodes = [this] (NodeId N) -> NodeList {
    NodeList Res;
    while (N) {
      auto RA = addr<RefNode*>(N);
      // Keep the nodes in the exact sibling order.
      Res.push_back(RA);
      N = RA.Addr->getSibling();
    }
    return Res;
  };
  NodeList ReachedDefs = getAllNodes(DA.Addr->getReachedDef());
  NodeList ReachedUses = getAllNodes(DA.Addr->getReachedUse());

  if (RD == 0) {
    for (NodeAddr<RefNode*> I : ReachedDefs)
      I.Addr->setSibling(0);
    for (NodeAddr<RefNode*> I : ReachedUses)
      I.Addr->setSibling(0);
  }
  for (NodeAddr<DefNode*> I : ReachedDefs)
    I.Addr->setReachingDef(RD);
  for (NodeAddr<UseNode*> I : ReachedUses)
    I.Addr->setReachingDef(RD);

  NodeId Sib = DA.Addr->getSibling();
  if (RD == 0) {
    assert(Sib == 0);
    return;
  }

  // Update the reaching def node and remove DA from the sibling list.
  auto RDA = addr<DefNode*>(RD);
  auto TA = addr<DefNode*>(RDA.Addr->getReachedDef());
  if (TA.Id == DA.Id) {
    // If DA is the first reached def, just update the RD's reached def
    // to the DA's sibling.
    RDA.Addr->setReachedDef(Sib);
  } else {
    // Otherwise, traverse the sibling list of the reached defs and remove
    // DA from it.
    while (TA.Id != 0) {
      NodeId S = TA.Addr->getSibling();
      if (S == DA.Id) {
        TA.Addr->setSibling(Sib);
        break;
      }
      TA = addr<DefNode*>(S);
    }
  }

  // Splice the DA's reached defs into the RDA's reached def chain.
  if (!ReachedDefs.empty()) {
    auto Last = NodeAddr<DefNode*>(ReachedDefs.back());
    Last.Addr->setSibling(RDA.Addr->getReachedDef());
    RDA.Addr->setReachedDef(ReachedDefs.front().Id);
  }
  // Splice the DA's reached uses into the RDA's reached use chain.
  if (!ReachedUses.empty()) {
    auto Last = NodeAddr<UseNode*>(ReachedUses.back());
    Last.Addr->setSibling(RDA.Addr->getReachedUse());
    RDA.Addr->setReachedUse(ReachedUses.front().Id);
  }
}
