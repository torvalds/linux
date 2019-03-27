//===- ValueMapper.cpp - Interface shared by lib/Transforms/Utils ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MapValue function, which is shared by various parts of
// the lib/Transforms/Utils library.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <limits>
#include <memory>
#include <utility>

using namespace llvm;

// Out of line method to get vtable etc for class.
void ValueMapTypeRemapper::anchor() {}
void ValueMaterializer::anchor() {}

namespace {

/// A basic block used in a BlockAddress whose function body is not yet
/// materialized.
struct DelayedBasicBlock {
  BasicBlock *OldBB;
  std::unique_ptr<BasicBlock> TempBB;

  DelayedBasicBlock(const BlockAddress &Old)
      : OldBB(Old.getBasicBlock()),
        TempBB(BasicBlock::Create(Old.getContext())) {}
};

struct WorklistEntry {
  enum EntryKind {
    MapGlobalInit,
    MapAppendingVar,
    MapGlobalAliasee,
    RemapFunction
  };
  struct GVInitTy {
    GlobalVariable *GV;
    Constant *Init;
  };
  struct AppendingGVTy {
    GlobalVariable *GV;
    Constant *InitPrefix;
  };
  struct GlobalAliaseeTy {
    GlobalAlias *GA;
    Constant *Aliasee;
  };

  unsigned Kind : 2;
  unsigned MCID : 29;
  unsigned AppendingGVIsOldCtorDtor : 1;
  unsigned AppendingGVNumNewMembers;
  union {
    GVInitTy GVInit;
    AppendingGVTy AppendingGV;
    GlobalAliaseeTy GlobalAliasee;
    Function *RemapF;
  } Data;
};

struct MappingContext {
  ValueToValueMapTy *VM;
  ValueMaterializer *Materializer = nullptr;

  /// Construct a MappingContext with a value map and materializer.
  explicit MappingContext(ValueToValueMapTy &VM,
                          ValueMaterializer *Materializer = nullptr)
      : VM(&VM), Materializer(Materializer) {}
};

class Mapper {
  friend class MDNodeMapper;

#ifndef NDEBUG
  DenseSet<GlobalValue *> AlreadyScheduled;
#endif

  RemapFlags Flags;
  ValueMapTypeRemapper *TypeMapper;
  unsigned CurrentMCID = 0;
  SmallVector<MappingContext, 2> MCs;
  SmallVector<WorklistEntry, 4> Worklist;
  SmallVector<DelayedBasicBlock, 1> DelayedBBs;
  SmallVector<Constant *, 16> AppendingInits;

public:
  Mapper(ValueToValueMapTy &VM, RemapFlags Flags,
         ValueMapTypeRemapper *TypeMapper, ValueMaterializer *Materializer)
      : Flags(Flags), TypeMapper(TypeMapper),
        MCs(1, MappingContext(VM, Materializer)) {}

  /// ValueMapper should explicitly call \a flush() before destruction.
  ~Mapper() { assert(!hasWorkToDo() && "Expected to be flushed"); }

  bool hasWorkToDo() const { return !Worklist.empty(); }

  unsigned
  registerAlternateMappingContext(ValueToValueMapTy &VM,
                                  ValueMaterializer *Materializer = nullptr) {
    MCs.push_back(MappingContext(VM, Materializer));
    return MCs.size() - 1;
  }

  void addFlags(RemapFlags Flags);

  void remapGlobalObjectMetadata(GlobalObject &GO);

  Value *mapValue(const Value *V);
  void remapInstruction(Instruction *I);
  void remapFunction(Function &F);

  Constant *mapConstant(const Constant *C) {
    return cast_or_null<Constant>(mapValue(C));
  }

  /// Map metadata.
  ///
  /// Find the mapping for MD.  Guarantees that the return will be resolved
  /// (not an MDNode, or MDNode::isResolved() returns true).
  Metadata *mapMetadata(const Metadata *MD);

  void scheduleMapGlobalInitializer(GlobalVariable &GV, Constant &Init,
                                    unsigned MCID);
  void scheduleMapAppendingVariable(GlobalVariable &GV, Constant *InitPrefix,
                                    bool IsOldCtorDtor,
                                    ArrayRef<Constant *> NewMembers,
                                    unsigned MCID);
  void scheduleMapGlobalAliasee(GlobalAlias &GA, Constant &Aliasee,
                                unsigned MCID);
  void scheduleRemapFunction(Function &F, unsigned MCID);

  void flush();

private:
  void mapGlobalInitializer(GlobalVariable &GV, Constant &Init);
  void mapAppendingVariable(GlobalVariable &GV, Constant *InitPrefix,
                            bool IsOldCtorDtor,
                            ArrayRef<Constant *> NewMembers);
  void mapGlobalAliasee(GlobalAlias &GA, Constant &Aliasee);
  void remapFunction(Function &F, ValueToValueMapTy &VM);

  ValueToValueMapTy &getVM() { return *MCs[CurrentMCID].VM; }
  ValueMaterializer *getMaterializer() { return MCs[CurrentMCID].Materializer; }

  Value *mapBlockAddress(const BlockAddress &BA);

  /// Map metadata that doesn't require visiting operands.
  Optional<Metadata *> mapSimpleMetadata(const Metadata *MD);

  Metadata *mapToMetadata(const Metadata *Key, Metadata *Val);
  Metadata *mapToSelf(const Metadata *MD);
};

class MDNodeMapper {
  Mapper &M;

  /// Data about a node in \a UniquedGraph.
  struct Data {
    bool HasChanged = false;
    unsigned ID = std::numeric_limits<unsigned>::max();
    TempMDNode Placeholder;
  };

  /// A graph of uniqued nodes.
  struct UniquedGraph {
    SmallDenseMap<const Metadata *, Data, 32> Info; // Node properties.
    SmallVector<MDNode *, 16> POT;                  // Post-order traversal.

    /// Propagate changed operands through the post-order traversal.
    ///
    /// Iteratively update \a Data::HasChanged for each node based on \a
    /// Data::HasChanged of its operands, until fixed point.
    void propagateChanges();

    /// Get a forward reference to a node to use as an operand.
    Metadata &getFwdReference(MDNode &Op);
  };

  /// Worklist of distinct nodes whose operands need to be remapped.
  SmallVector<MDNode *, 16> DistinctWorklist;

  // Storage for a UniquedGraph.
  SmallDenseMap<const Metadata *, Data, 32> InfoStorage;
  SmallVector<MDNode *, 16> POTStorage;

public:
  MDNodeMapper(Mapper &M) : M(M) {}

  /// Map a metadata node (and its transitive operands).
  ///
  /// Map all the (unmapped) nodes in the subgraph under \c N.  The iterative
  /// algorithm handles distinct nodes and uniqued node subgraphs using
  /// different strategies.
  ///
  /// Distinct nodes are immediately mapped and added to \a DistinctWorklist
  /// using \a mapDistinctNode().  Their mapping can always be computed
  /// immediately without visiting operands, even if their operands change.
  ///
  /// The mapping for uniqued nodes depends on whether their operands change.
  /// \a mapTopLevelUniquedNode() traverses the transitive uniqued subgraph of
  /// a node to calculate uniqued node mappings in bulk.  Distinct leafs are
  /// added to \a DistinctWorklist with \a mapDistinctNode().
  ///
  /// After mapping \c N itself, this function remaps the operands of the
  /// distinct nodes in \a DistinctWorklist until the entire subgraph under \c
  /// N has been mapped.
  Metadata *map(const MDNode &N);

private:
  /// Map a top-level uniqued node and the uniqued subgraph underneath it.
  ///
  /// This builds up a post-order traversal of the (unmapped) uniqued subgraph
  /// underneath \c FirstN and calculates the nodes' mapping.  Each node uses
  /// the identity mapping (\a Mapper::mapToSelf()) as long as all of its
  /// operands uses the identity mapping.
  ///
  /// The algorithm works as follows:
  ///
  ///  1. \a createPOT(): traverse the uniqued subgraph under \c FirstN and
  ///     save the post-order traversal in the given \a UniquedGraph, tracking
  ///     nodes' operands change.
  ///
  ///  2. \a UniquedGraph::propagateChanges(): propagate changed operands
  ///     through the \a UniquedGraph until fixed point, following the rule
  ///     that if a node changes, any node that references must also change.
  ///
  ///  3. \a mapNodesInPOT(): map the uniqued nodes, creating new uniqued nodes
  ///     (referencing new operands) where necessary.
  Metadata *mapTopLevelUniquedNode(const MDNode &FirstN);

  /// Try to map the operand of an \a MDNode.
  ///
  /// If \c Op is already mapped, return the mapping.  If it's not an \a
  /// MDNode, compute and return the mapping.  If it's a distinct \a MDNode,
  /// return the result of \a mapDistinctNode().
  ///
  /// \return None if \c Op is an unmapped uniqued \a MDNode.
  /// \post getMappedOp(Op) only returns None if this returns None.
  Optional<Metadata *> tryToMapOperand(const Metadata *Op);

  /// Map a distinct node.
  ///
  /// Return the mapping for the distinct node \c N, saving the result in \a
  /// DistinctWorklist for later remapping.
  ///
  /// \pre \c N is not yet mapped.
  /// \pre \c N.isDistinct().
  MDNode *mapDistinctNode(const MDNode &N);

  /// Get a previously mapped node.
  Optional<Metadata *> getMappedOp(const Metadata *Op) const;

  /// Create a post-order traversal of an unmapped uniqued node subgraph.
  ///
  /// This traverses the metadata graph deeply enough to map \c FirstN.  It
  /// uses \a tryToMapOperand() (via \a Mapper::mapSimplifiedNode()), so any
  /// metadata that has already been mapped will not be part of the POT.
  ///
  /// Each node that has a changed operand from outside the graph (e.g., a
  /// distinct node, an already-mapped uniqued node, or \a ConstantAsMetadata)
  /// is marked with \a Data::HasChanged.
  ///
  /// \return \c true if any nodes in \c G have \a Data::HasChanged.
  /// \post \c G.POT is a post-order traversal ending with \c FirstN.
  /// \post \a Data::hasChanged in \c G.Info indicates whether any node needs
  /// to change because of operands outside the graph.
  bool createPOT(UniquedGraph &G, const MDNode &FirstN);

  /// Visit the operands of a uniqued node in the POT.
  ///
  /// Visit the operands in the range from \c I to \c E, returning the first
  /// uniqued node we find that isn't yet in \c G.  \c I is always advanced to
  /// where to continue the loop through the operands.
  ///
  /// This sets \c HasChanged if any of the visited operands change.
  MDNode *visitOperands(UniquedGraph &G, MDNode::op_iterator &I,
                        MDNode::op_iterator E, bool &HasChanged);

  /// Map all the nodes in the given uniqued graph.
  ///
  /// This visits all the nodes in \c G in post-order, using the identity
  /// mapping or creating a new node depending on \a Data::HasChanged.
  ///
  /// \pre \a getMappedOp() returns None for nodes in \c G, but not for any of
  /// their operands outside of \c G.
  /// \pre \a Data::HasChanged is true for a node in \c G iff any of its
  /// operands have changed.
  /// \post \a getMappedOp() returns the mapped node for every node in \c G.
  void mapNodesInPOT(UniquedGraph &G);

  /// Remap a node's operands using the given functor.
  ///
  /// Iterate through the operands of \c N and update them in place using \c
  /// mapOperand.
  ///
  /// \pre N.isDistinct() or N.isTemporary().
  template <class OperandMapper>
  void remapOperands(MDNode &N, OperandMapper mapOperand);
};

} // end anonymous namespace

Value *Mapper::mapValue(const Value *V) {
  ValueToValueMapTy::iterator I = getVM().find(V);

  // If the value already exists in the map, use it.
  if (I != getVM().end()) {
    assert(I->second && "Unexpected null mapping");
    return I->second;
  }

  // If we have a materializer and it can materialize a value, use that.
  if (auto *Materializer = getMaterializer()) {
    if (Value *NewV = Materializer->materialize(const_cast<Value *>(V))) {
      getVM()[V] = NewV;
      return NewV;
    }
  }

  // Global values do not need to be seeded into the VM if they
  // are using the identity mapping.
  if (isa<GlobalValue>(V)) {
    if (Flags & RF_NullMapMissingGlobalValues)
      return nullptr;
    return getVM()[V] = const_cast<Value *>(V);
  }

  if (const InlineAsm *IA = dyn_cast<InlineAsm>(V)) {
    // Inline asm may need *type* remapping.
    FunctionType *NewTy = IA->getFunctionType();
    if (TypeMapper) {
      NewTy = cast<FunctionType>(TypeMapper->remapType(NewTy));

      if (NewTy != IA->getFunctionType())
        V = InlineAsm::get(NewTy, IA->getAsmString(), IA->getConstraintString(),
                           IA->hasSideEffects(), IA->isAlignStack());
    }

    return getVM()[V] = const_cast<Value *>(V);
  }

  if (const auto *MDV = dyn_cast<MetadataAsValue>(V)) {
    const Metadata *MD = MDV->getMetadata();

    if (auto *LAM = dyn_cast<LocalAsMetadata>(MD)) {
      // Look through to grab the local value.
      if (Value *LV = mapValue(LAM->getValue())) {
        if (V == LAM->getValue())
          return const_cast<Value *>(V);
        return MetadataAsValue::get(V->getContext(), ValueAsMetadata::get(LV));
      }

      // FIXME: always return nullptr once Verifier::verifyDominatesUse()
      // ensures metadata operands only reference defined SSA values.
      return (Flags & RF_IgnoreMissingLocals)
                 ? nullptr
                 : MetadataAsValue::get(V->getContext(),
                                        MDTuple::get(V->getContext(), None));
    }

    // If this is a module-level metadata and we know that nothing at the module
    // level is changing, then use an identity mapping.
    if (Flags & RF_NoModuleLevelChanges)
      return getVM()[V] = const_cast<Value *>(V);

    // Map the metadata and turn it into a value.
    auto *MappedMD = mapMetadata(MD);
    if (MD == MappedMD)
      return getVM()[V] = const_cast<Value *>(V);
    return getVM()[V] = MetadataAsValue::get(V->getContext(), MappedMD);
  }

  // Okay, this either must be a constant (which may or may not be mappable) or
  // is something that is not in the mapping table.
  Constant *C = const_cast<Constant*>(dyn_cast<Constant>(V));
  if (!C)
    return nullptr;

  if (BlockAddress *BA = dyn_cast<BlockAddress>(C))
    return mapBlockAddress(*BA);

  auto mapValueOrNull = [this](Value *V) {
    auto Mapped = mapValue(V);
    assert((Mapped || (Flags & RF_NullMapMissingGlobalValues)) &&
           "Unexpected null mapping for constant operand without "
           "NullMapMissingGlobalValues flag");
    return Mapped;
  };

  // Otherwise, we have some other constant to remap.  Start by checking to see
  // if all operands have an identity remapping.
  unsigned OpNo = 0, NumOperands = C->getNumOperands();
  Value *Mapped = nullptr;
  for (; OpNo != NumOperands; ++OpNo) {
    Value *Op = C->getOperand(OpNo);
    Mapped = mapValueOrNull(Op);
    if (!Mapped)
      return nullptr;
    if (Mapped != Op)
      break;
  }

  // See if the type mapper wants to remap the type as well.
  Type *NewTy = C->getType();
  if (TypeMapper)
    NewTy = TypeMapper->remapType(NewTy);

  // If the result type and all operands match up, then just insert an identity
  // mapping.
  if (OpNo == NumOperands && NewTy == C->getType())
    return getVM()[V] = C;

  // Okay, we need to create a new constant.  We've already processed some or
  // all of the operands, set them all up now.
  SmallVector<Constant*, 8> Ops;
  Ops.reserve(NumOperands);
  for (unsigned j = 0; j != OpNo; ++j)
    Ops.push_back(cast<Constant>(C->getOperand(j)));

  // If one of the operands mismatch, push it and the other mapped operands.
  if (OpNo != NumOperands) {
    Ops.push_back(cast<Constant>(Mapped));

    // Map the rest of the operands that aren't processed yet.
    for (++OpNo; OpNo != NumOperands; ++OpNo) {
      Mapped = mapValueOrNull(C->getOperand(OpNo));
      if (!Mapped)
        return nullptr;
      Ops.push_back(cast<Constant>(Mapped));
    }
  }
  Type *NewSrcTy = nullptr;
  if (TypeMapper)
    if (auto *GEPO = dyn_cast<GEPOperator>(C))
      NewSrcTy = TypeMapper->remapType(GEPO->getSourceElementType());

  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(C))
    return getVM()[V] = CE->getWithOperands(Ops, NewTy, false, NewSrcTy);
  if (isa<ConstantArray>(C))
    return getVM()[V] = ConstantArray::get(cast<ArrayType>(NewTy), Ops);
  if (isa<ConstantStruct>(C))
    return getVM()[V] = ConstantStruct::get(cast<StructType>(NewTy), Ops);
  if (isa<ConstantVector>(C))
    return getVM()[V] = ConstantVector::get(Ops);
  // If this is a no-operand constant, it must be because the type was remapped.
  if (isa<UndefValue>(C))
    return getVM()[V] = UndefValue::get(NewTy);
  if (isa<ConstantAggregateZero>(C))
    return getVM()[V] = ConstantAggregateZero::get(NewTy);
  assert(isa<ConstantPointerNull>(C));
  return getVM()[V] = ConstantPointerNull::get(cast<PointerType>(NewTy));
}

Value *Mapper::mapBlockAddress(const BlockAddress &BA) {
  Function *F = cast<Function>(mapValue(BA.getFunction()));

  // F may not have materialized its initializer.  In that case, create a
  // dummy basic block for now, and replace it once we've materialized all
  // the initializers.
  BasicBlock *BB;
  if (F->empty()) {
    DelayedBBs.push_back(DelayedBasicBlock(BA));
    BB = DelayedBBs.back().TempBB.get();
  } else {
    BB = cast_or_null<BasicBlock>(mapValue(BA.getBasicBlock()));
  }

  return getVM()[&BA] = BlockAddress::get(F, BB ? BB : BA.getBasicBlock());
}

Metadata *Mapper::mapToMetadata(const Metadata *Key, Metadata *Val) {
  getVM().MD()[Key].reset(Val);
  return Val;
}

Metadata *Mapper::mapToSelf(const Metadata *MD) {
  return mapToMetadata(MD, const_cast<Metadata *>(MD));
}

Optional<Metadata *> MDNodeMapper::tryToMapOperand(const Metadata *Op) {
  if (!Op)
    return nullptr;

  if (Optional<Metadata *> MappedOp = M.mapSimpleMetadata(Op)) {
#ifndef NDEBUG
    if (auto *CMD = dyn_cast<ConstantAsMetadata>(Op))
      assert((!*MappedOp || M.getVM().count(CMD->getValue()) ||
              M.getVM().getMappedMD(Op)) &&
             "Expected Value to be memoized");
    else
      assert((isa<MDString>(Op) || M.getVM().getMappedMD(Op)) &&
             "Expected result to be memoized");
#endif
    return *MappedOp;
  }

  const MDNode &N = *cast<MDNode>(Op);
  if (N.isDistinct())
    return mapDistinctNode(N);
  return None;
}

static Metadata *cloneOrBuildODR(const MDNode &N) {
  auto *CT = dyn_cast<DICompositeType>(&N);
  // If ODR type uniquing is enabled, we would have uniqued composite types
  // with identifiers during bitcode reading, so we can just use CT.
  if (CT && CT->getContext().isODRUniquingDebugTypes() &&
      CT->getIdentifier() != "")
    return const_cast<DICompositeType *>(CT);
  return MDNode::replaceWithDistinct(N.clone());
}

MDNode *MDNodeMapper::mapDistinctNode(const MDNode &N) {
  assert(N.isDistinct() && "Expected a distinct node");
  assert(!M.getVM().getMappedMD(&N) && "Expected an unmapped node");
  DistinctWorklist.push_back(
      cast<MDNode>((M.Flags & RF_MoveDistinctMDs)
                       ? M.mapToSelf(&N)
                       : M.mapToMetadata(&N, cloneOrBuildODR(N))));
  return DistinctWorklist.back();
}

static ConstantAsMetadata *wrapConstantAsMetadata(const ConstantAsMetadata &CMD,
                                                  Value *MappedV) {
  if (CMD.getValue() == MappedV)
    return const_cast<ConstantAsMetadata *>(&CMD);
  return MappedV ? ConstantAsMetadata::getConstant(MappedV) : nullptr;
}

Optional<Metadata *> MDNodeMapper::getMappedOp(const Metadata *Op) const {
  if (!Op)
    return nullptr;

  if (Optional<Metadata *> MappedOp = M.getVM().getMappedMD(Op))
    return *MappedOp;

  if (isa<MDString>(Op))
    return const_cast<Metadata *>(Op);

  if (auto *CMD = dyn_cast<ConstantAsMetadata>(Op))
    return wrapConstantAsMetadata(*CMD, M.getVM().lookup(CMD->getValue()));

  return None;
}

Metadata &MDNodeMapper::UniquedGraph::getFwdReference(MDNode &Op) {
  auto Where = Info.find(&Op);
  assert(Where != Info.end() && "Expected a valid reference");

  auto &OpD = Where->second;
  if (!OpD.HasChanged)
    return Op;

  // Lazily construct a temporary node.
  if (!OpD.Placeholder)
    OpD.Placeholder = Op.clone();

  return *OpD.Placeholder;
}

template <class OperandMapper>
void MDNodeMapper::remapOperands(MDNode &N, OperandMapper mapOperand) {
  assert(!N.isUniqued() && "Expected distinct or temporary nodes");
  for (unsigned I = 0, E = N.getNumOperands(); I != E; ++I) {
    Metadata *Old = N.getOperand(I);
    Metadata *New = mapOperand(Old);

    if (Old != New)
      N.replaceOperandWith(I, New);
  }
}

namespace {

/// An entry in the worklist for the post-order traversal.
struct POTWorklistEntry {
  MDNode *N;              ///< Current node.
  MDNode::op_iterator Op; ///< Current operand of \c N.

  /// Keep a flag of whether operands have changed in the worklist to avoid
  /// hitting the map in \a UniquedGraph.
  bool HasChanged = false;

  POTWorklistEntry(MDNode &N) : N(&N), Op(N.op_begin()) {}
};

} // end anonymous namespace

bool MDNodeMapper::createPOT(UniquedGraph &G, const MDNode &FirstN) {
  assert(G.Info.empty() && "Expected a fresh traversal");
  assert(FirstN.isUniqued() && "Expected uniqued node in POT");

  // Construct a post-order traversal of the uniqued subgraph under FirstN.
  bool AnyChanges = false;
  SmallVector<POTWorklistEntry, 16> Worklist;
  Worklist.push_back(POTWorklistEntry(const_cast<MDNode &>(FirstN)));
  (void)G.Info[&FirstN];
  while (!Worklist.empty()) {
    // Start or continue the traversal through the this node's operands.
    auto &WE = Worklist.back();
    if (MDNode *N = visitOperands(G, WE.Op, WE.N->op_end(), WE.HasChanged)) {
      // Push a new node to traverse first.
      Worklist.push_back(POTWorklistEntry(*N));
      continue;
    }

    // Push the node onto the POT.
    assert(WE.N->isUniqued() && "Expected only uniqued nodes");
    assert(WE.Op == WE.N->op_end() && "Expected to visit all operands");
    auto &D = G.Info[WE.N];
    AnyChanges |= D.HasChanged = WE.HasChanged;
    D.ID = G.POT.size();
    G.POT.push_back(WE.N);

    // Pop the node off the worklist.
    Worklist.pop_back();
  }
  return AnyChanges;
}

MDNode *MDNodeMapper::visitOperands(UniquedGraph &G, MDNode::op_iterator &I,
                                    MDNode::op_iterator E, bool &HasChanged) {
  while (I != E) {
    Metadata *Op = *I++; // Increment even on early return.
    if (Optional<Metadata *> MappedOp = tryToMapOperand(Op)) {
      // Check if the operand changes.
      HasChanged |= Op != *MappedOp;
      continue;
    }

    // A uniqued metadata node.
    MDNode &OpN = *cast<MDNode>(Op);
    assert(OpN.isUniqued() &&
           "Only uniqued operands cannot be mapped immediately");
    if (G.Info.insert(std::make_pair(&OpN, Data())).second)
      return &OpN; // This is a new one.  Return it.
  }
  return nullptr;
}

void MDNodeMapper::UniquedGraph::propagateChanges() {
  bool AnyChanges;
  do {
    AnyChanges = false;
    for (MDNode *N : POT) {
      auto &D = Info[N];
      if (D.HasChanged)
        continue;

      if (llvm::none_of(N->operands(), [&](const Metadata *Op) {
            auto Where = Info.find(Op);
            return Where != Info.end() && Where->second.HasChanged;
          }))
        continue;

      AnyChanges = D.HasChanged = true;
    }
  } while (AnyChanges);
}

void MDNodeMapper::mapNodesInPOT(UniquedGraph &G) {
  // Construct uniqued nodes, building forward references as necessary.
  SmallVector<MDNode *, 16> CyclicNodes;
  for (auto *N : G.POT) {
    auto &D = G.Info[N];
    if (!D.HasChanged) {
      // The node hasn't changed.
      M.mapToSelf(N);
      continue;
    }

    // Remember whether this node had a placeholder.
    bool HadPlaceholder(D.Placeholder);

    // Clone the uniqued node and remap the operands.
    TempMDNode ClonedN = D.Placeholder ? std::move(D.Placeholder) : N->clone();
    remapOperands(*ClonedN, [this, &D, &G](Metadata *Old) {
      if (Optional<Metadata *> MappedOp = getMappedOp(Old))
        return *MappedOp;
      (void)D;
      assert(G.Info[Old].ID > D.ID && "Expected a forward reference");
      return &G.getFwdReference(*cast<MDNode>(Old));
    });

    auto *NewN = MDNode::replaceWithUniqued(std::move(ClonedN));
    M.mapToMetadata(N, NewN);

    // Nodes that were referenced out of order in the POT are involved in a
    // uniquing cycle.
    if (HadPlaceholder)
      CyclicNodes.push_back(NewN);
  }

  // Resolve cycles.
  for (auto *N : CyclicNodes)
    if (!N->isResolved())
      N->resolveCycles();
}

Metadata *MDNodeMapper::map(const MDNode &N) {
  assert(DistinctWorklist.empty() && "MDNodeMapper::map is not recursive");
  assert(!(M.Flags & RF_NoModuleLevelChanges) &&
         "MDNodeMapper::map assumes module-level changes");

  // Require resolved nodes whenever metadata might be remapped.
  assert(N.isResolved() && "Unexpected unresolved node");

  Metadata *MappedN =
      N.isUniqued() ? mapTopLevelUniquedNode(N) : mapDistinctNode(N);
  while (!DistinctWorklist.empty())
    remapOperands(*DistinctWorklist.pop_back_val(), [this](Metadata *Old) {
      if (Optional<Metadata *> MappedOp = tryToMapOperand(Old))
        return *MappedOp;
      return mapTopLevelUniquedNode(*cast<MDNode>(Old));
    });
  return MappedN;
}

Metadata *MDNodeMapper::mapTopLevelUniquedNode(const MDNode &FirstN) {
  assert(FirstN.isUniqued() && "Expected uniqued node");

  // Create a post-order traversal of uniqued nodes under FirstN.
  UniquedGraph G;
  if (!createPOT(G, FirstN)) {
    // Return early if no nodes have changed.
    for (const MDNode *N : G.POT)
      M.mapToSelf(N);
    return &const_cast<MDNode &>(FirstN);
  }

  // Update graph with all nodes that have changed.
  G.propagateChanges();

  // Map all the nodes in the graph.
  mapNodesInPOT(G);

  // Return the original node, remapped.
  return *getMappedOp(&FirstN);
}

namespace {

struct MapMetadataDisabler {
  ValueToValueMapTy &VM;

  MapMetadataDisabler(ValueToValueMapTy &VM) : VM(VM) {
    VM.disableMapMetadata();
  }

  ~MapMetadataDisabler() { VM.enableMapMetadata(); }
};

} // end anonymous namespace

Optional<Metadata *> Mapper::mapSimpleMetadata(const Metadata *MD) {
  // If the value already exists in the map, use it.
  if (Optional<Metadata *> NewMD = getVM().getMappedMD(MD))
    return *NewMD;

  if (isa<MDString>(MD))
    return const_cast<Metadata *>(MD);

  // This is a module-level metadata.  If nothing at the module level is
  // changing, use an identity mapping.
  if ((Flags & RF_NoModuleLevelChanges))
    return const_cast<Metadata *>(MD);

  if (auto *CMD = dyn_cast<ConstantAsMetadata>(MD)) {
    // Disallow recursion into metadata mapping through mapValue.
    MapMetadataDisabler MMD(getVM());

    // Don't memoize ConstantAsMetadata.  Instead of lasting until the
    // LLVMContext is destroyed, they can be deleted when the GlobalValue they
    // reference is destructed.  These aren't super common, so the extra
    // indirection isn't that expensive.
    return wrapConstantAsMetadata(*CMD, mapValue(CMD->getValue()));
  }

  assert(isa<MDNode>(MD) && "Expected a metadata node");

  return None;
}

Metadata *Mapper::mapMetadata(const Metadata *MD) {
  assert(MD && "Expected valid metadata");
  assert(!isa<LocalAsMetadata>(MD) && "Unexpected local metadata");

  if (Optional<Metadata *> NewMD = mapSimpleMetadata(MD))
    return *NewMD;

  return MDNodeMapper(*this).map(*cast<MDNode>(MD));
}

void Mapper::flush() {
  // Flush out the worklist of global values.
  while (!Worklist.empty()) {
    WorklistEntry E = Worklist.pop_back_val();
    CurrentMCID = E.MCID;
    switch (E.Kind) {
    case WorklistEntry::MapGlobalInit:
      E.Data.GVInit.GV->setInitializer(mapConstant(E.Data.GVInit.Init));
      remapGlobalObjectMetadata(*E.Data.GVInit.GV);
      break;
    case WorklistEntry::MapAppendingVar: {
      unsigned PrefixSize = AppendingInits.size() - E.AppendingGVNumNewMembers;
      mapAppendingVariable(*E.Data.AppendingGV.GV,
                           E.Data.AppendingGV.InitPrefix,
                           E.AppendingGVIsOldCtorDtor,
                           makeArrayRef(AppendingInits).slice(PrefixSize));
      AppendingInits.resize(PrefixSize);
      break;
    }
    case WorklistEntry::MapGlobalAliasee:
      E.Data.GlobalAliasee.GA->setAliasee(
          mapConstant(E.Data.GlobalAliasee.Aliasee));
      break;
    case WorklistEntry::RemapFunction:
      remapFunction(*E.Data.RemapF);
      break;
    }
  }
  CurrentMCID = 0;

  // Finish logic for block addresses now that all global values have been
  // handled.
  while (!DelayedBBs.empty()) {
    DelayedBasicBlock DBB = DelayedBBs.pop_back_val();
    BasicBlock *BB = cast_or_null<BasicBlock>(mapValue(DBB.OldBB));
    DBB.TempBB->replaceAllUsesWith(BB ? BB : DBB.OldBB);
  }
}

void Mapper::remapInstruction(Instruction *I) {
  // Remap operands.
  for (Use &Op : I->operands()) {
    Value *V = mapValue(Op);
    // If we aren't ignoring missing entries, assert that something happened.
    if (V)
      Op = V;
    else
      assert((Flags & RF_IgnoreMissingLocals) &&
             "Referenced value not in value map!");
  }

  // Remap phi nodes' incoming blocks.
  if (PHINode *PN = dyn_cast<PHINode>(I)) {
    for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
      Value *V = mapValue(PN->getIncomingBlock(i));
      // If we aren't ignoring missing entries, assert that something happened.
      if (V)
        PN->setIncomingBlock(i, cast<BasicBlock>(V));
      else
        assert((Flags & RF_IgnoreMissingLocals) &&
               "Referenced block not in value map!");
    }
  }

  // Remap attached metadata.
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  I->getAllMetadata(MDs);
  for (const auto &MI : MDs) {
    MDNode *Old = MI.second;
    MDNode *New = cast_or_null<MDNode>(mapMetadata(Old));
    if (New != Old)
      I->setMetadata(MI.first, New);
  }

  if (!TypeMapper)
    return;

  // If the instruction's type is being remapped, do so now.
  if (auto CS = CallSite(I)) {
    SmallVector<Type *, 3> Tys;
    FunctionType *FTy = CS.getFunctionType();
    Tys.reserve(FTy->getNumParams());
    for (Type *Ty : FTy->params())
      Tys.push_back(TypeMapper->remapType(Ty));
    CS.mutateFunctionType(FunctionType::get(
        TypeMapper->remapType(I->getType()), Tys, FTy->isVarArg()));
    return;
  }
  if (auto *AI = dyn_cast<AllocaInst>(I))
    AI->setAllocatedType(TypeMapper->remapType(AI->getAllocatedType()));
  if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
    GEP->setSourceElementType(
        TypeMapper->remapType(GEP->getSourceElementType()));
    GEP->setResultElementType(
        TypeMapper->remapType(GEP->getResultElementType()));
  }
  I->mutateType(TypeMapper->remapType(I->getType()));
}

void Mapper::remapGlobalObjectMetadata(GlobalObject &GO) {
  SmallVector<std::pair<unsigned, MDNode *>, 8> MDs;
  GO.getAllMetadata(MDs);
  GO.clearMetadata();
  for (const auto &I : MDs)
    GO.addMetadata(I.first, *cast<MDNode>(mapMetadata(I.second)));
}

void Mapper::remapFunction(Function &F) {
  // Remap the operands.
  for (Use &Op : F.operands())
    if (Op)
      Op = mapValue(Op);

  // Remap the metadata attachments.
  remapGlobalObjectMetadata(F);

  // Remap the argument types.
  if (TypeMapper)
    for (Argument &A : F.args())
      A.mutateType(TypeMapper->remapType(A.getType()));

  // Remap the instructions.
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      remapInstruction(&I);
}

void Mapper::mapAppendingVariable(GlobalVariable &GV, Constant *InitPrefix,
                                  bool IsOldCtorDtor,
                                  ArrayRef<Constant *> NewMembers) {
  SmallVector<Constant *, 16> Elements;
  if (InitPrefix) {
    unsigned NumElements =
        cast<ArrayType>(InitPrefix->getType())->getNumElements();
    for (unsigned I = 0; I != NumElements; ++I)
      Elements.push_back(InitPrefix->getAggregateElement(I));
  }

  PointerType *VoidPtrTy;
  Type *EltTy;
  if (IsOldCtorDtor) {
    // FIXME: This upgrade is done during linking to support the C API.  See
    // also IRLinker::linkAppendingVarProto() in IRMover.cpp.
    VoidPtrTy = Type::getInt8Ty(GV.getContext())->getPointerTo();
    auto &ST = *cast<StructType>(NewMembers.front()->getType());
    Type *Tys[3] = {ST.getElementType(0), ST.getElementType(1), VoidPtrTy};
    EltTy = StructType::get(GV.getContext(), Tys, false);
  }

  for (auto *V : NewMembers) {
    Constant *NewV;
    if (IsOldCtorDtor) {
      auto *S = cast<ConstantStruct>(V);
      auto *E1 = cast<Constant>(mapValue(S->getOperand(0)));
      auto *E2 = cast<Constant>(mapValue(S->getOperand(1)));
      Constant *Null = Constant::getNullValue(VoidPtrTy);
      NewV = ConstantStruct::get(cast<StructType>(EltTy), E1, E2, Null);
    } else {
      NewV = cast_or_null<Constant>(mapValue(V));
    }
    Elements.push_back(NewV);
  }

  GV.setInitializer(ConstantArray::get(
      cast<ArrayType>(GV.getType()->getElementType()), Elements));
}

void Mapper::scheduleMapGlobalInitializer(GlobalVariable &GV, Constant &Init,
                                          unsigned MCID) {
  assert(AlreadyScheduled.insert(&GV).second && "Should not reschedule");
  assert(MCID < MCs.size() && "Invalid mapping context");

  WorklistEntry WE;
  WE.Kind = WorklistEntry::MapGlobalInit;
  WE.MCID = MCID;
  WE.Data.GVInit.GV = &GV;
  WE.Data.GVInit.Init = &Init;
  Worklist.push_back(WE);
}

void Mapper::scheduleMapAppendingVariable(GlobalVariable &GV,
                                          Constant *InitPrefix,
                                          bool IsOldCtorDtor,
                                          ArrayRef<Constant *> NewMembers,
                                          unsigned MCID) {
  assert(AlreadyScheduled.insert(&GV).second && "Should not reschedule");
  assert(MCID < MCs.size() && "Invalid mapping context");

  WorklistEntry WE;
  WE.Kind = WorklistEntry::MapAppendingVar;
  WE.MCID = MCID;
  WE.Data.AppendingGV.GV = &GV;
  WE.Data.AppendingGV.InitPrefix = InitPrefix;
  WE.AppendingGVIsOldCtorDtor = IsOldCtorDtor;
  WE.AppendingGVNumNewMembers = NewMembers.size();
  Worklist.push_back(WE);
  AppendingInits.append(NewMembers.begin(), NewMembers.end());
}

void Mapper::scheduleMapGlobalAliasee(GlobalAlias &GA, Constant &Aliasee,
                                      unsigned MCID) {
  assert(AlreadyScheduled.insert(&GA).second && "Should not reschedule");
  assert(MCID < MCs.size() && "Invalid mapping context");

  WorklistEntry WE;
  WE.Kind = WorklistEntry::MapGlobalAliasee;
  WE.MCID = MCID;
  WE.Data.GlobalAliasee.GA = &GA;
  WE.Data.GlobalAliasee.Aliasee = &Aliasee;
  Worklist.push_back(WE);
}

void Mapper::scheduleRemapFunction(Function &F, unsigned MCID) {
  assert(AlreadyScheduled.insert(&F).second && "Should not reschedule");
  assert(MCID < MCs.size() && "Invalid mapping context");

  WorklistEntry WE;
  WE.Kind = WorklistEntry::RemapFunction;
  WE.MCID = MCID;
  WE.Data.RemapF = &F;
  Worklist.push_back(WE);
}

void Mapper::addFlags(RemapFlags Flags) {
  assert(!hasWorkToDo() && "Expected to have flushed the worklist");
  this->Flags = this->Flags | Flags;
}

static Mapper *getAsMapper(void *pImpl) {
  return reinterpret_cast<Mapper *>(pImpl);
}

namespace {

class FlushingMapper {
  Mapper &M;

public:
  explicit FlushingMapper(void *pImpl) : M(*getAsMapper(pImpl)) {
    assert(!M.hasWorkToDo() && "Expected to be flushed");
  }

  ~FlushingMapper() { M.flush(); }

  Mapper *operator->() const { return &M; }
};

} // end anonymous namespace

ValueMapper::ValueMapper(ValueToValueMapTy &VM, RemapFlags Flags,
                         ValueMapTypeRemapper *TypeMapper,
                         ValueMaterializer *Materializer)
    : pImpl(new Mapper(VM, Flags, TypeMapper, Materializer)) {}

ValueMapper::~ValueMapper() { delete getAsMapper(pImpl); }

unsigned
ValueMapper::registerAlternateMappingContext(ValueToValueMapTy &VM,
                                             ValueMaterializer *Materializer) {
  return getAsMapper(pImpl)->registerAlternateMappingContext(VM, Materializer);
}

void ValueMapper::addFlags(RemapFlags Flags) {
  FlushingMapper(pImpl)->addFlags(Flags);
}

Value *ValueMapper::mapValue(const Value &V) {
  return FlushingMapper(pImpl)->mapValue(&V);
}

Constant *ValueMapper::mapConstant(const Constant &C) {
  return cast_or_null<Constant>(mapValue(C));
}

Metadata *ValueMapper::mapMetadata(const Metadata &MD) {
  return FlushingMapper(pImpl)->mapMetadata(&MD);
}

MDNode *ValueMapper::mapMDNode(const MDNode &N) {
  return cast_or_null<MDNode>(mapMetadata(N));
}

void ValueMapper::remapInstruction(Instruction &I) {
  FlushingMapper(pImpl)->remapInstruction(&I);
}

void ValueMapper::remapFunction(Function &F) {
  FlushingMapper(pImpl)->remapFunction(F);
}

void ValueMapper::scheduleMapGlobalInitializer(GlobalVariable &GV,
                                               Constant &Init,
                                               unsigned MCID) {
  getAsMapper(pImpl)->scheduleMapGlobalInitializer(GV, Init, MCID);
}

void ValueMapper::scheduleMapAppendingVariable(GlobalVariable &GV,
                                               Constant *InitPrefix,
                                               bool IsOldCtorDtor,
                                               ArrayRef<Constant *> NewMembers,
                                               unsigned MCID) {
  getAsMapper(pImpl)->scheduleMapAppendingVariable(
      GV, InitPrefix, IsOldCtorDtor, NewMembers, MCID);
}

void ValueMapper::scheduleMapGlobalAliasee(GlobalAlias &GA, Constant &Aliasee,
                                           unsigned MCID) {
  getAsMapper(pImpl)->scheduleMapGlobalAliasee(GA, Aliasee, MCID);
}

void ValueMapper::scheduleRemapFunction(Function &F, unsigned MCID) {
  getAsMapper(pImpl)->scheduleRemapFunction(F, MCID);
}
