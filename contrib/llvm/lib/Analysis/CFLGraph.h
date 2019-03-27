//===- CFLGraph.h - Abstract stratified sets implementation. -----*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file defines CFLGraph, an auxiliary data structure used by CFL-based
/// alias analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_ANALYSIS_CFLGRAPH_H
#define LLVM_LIB_ANALYSIS_CFLGRAPH_H

#include "AliasAnalysisSummary.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <vector>

namespace llvm {
namespace cflaa {

/// The Program Expression Graph (PEG) of CFL analysis
/// CFLGraph is auxiliary data structure used by CFL-based alias analysis to
/// describe flow-insensitive pointer-related behaviors. Given an LLVM function,
/// the main purpose of this graph is to abstract away unrelated facts and
/// translate the rest into a form that can be easily digested by CFL analyses.
/// Each Node in the graph is an InstantiatedValue, and each edge represent a
/// pointer assignment between InstantiatedValue. Pointer
/// references/dereferences are not explicitly stored in the graph: we
/// implicitly assume that for each node (X, I) it has a dereference edge to (X,
/// I+1) and a reference edge to (X, I-1).
class CFLGraph {
public:
  using Node = InstantiatedValue;

  struct Edge {
    Node Other;
    int64_t Offset;
  };

  using EdgeList = std::vector<Edge>;

  struct NodeInfo {
    EdgeList Edges, ReverseEdges;
    AliasAttrs Attr;
  };

  class ValueInfo {
    std::vector<NodeInfo> Levels;

  public:
    bool addNodeToLevel(unsigned Level) {
      auto NumLevels = Levels.size();
      if (NumLevels > Level)
        return false;
      Levels.resize(Level + 1);
      return true;
    }

    NodeInfo &getNodeInfoAtLevel(unsigned Level) {
      assert(Level < Levels.size());
      return Levels[Level];
    }
    const NodeInfo &getNodeInfoAtLevel(unsigned Level) const {
      assert(Level < Levels.size());
      return Levels[Level];
    }

    unsigned getNumLevels() const { return Levels.size(); }
  };

private:
  using ValueMap = DenseMap<Value *, ValueInfo>;

  ValueMap ValueImpls;

  NodeInfo *getNode(Node N) {
    auto Itr = ValueImpls.find(N.Val);
    if (Itr == ValueImpls.end() || Itr->second.getNumLevels() <= N.DerefLevel)
      return nullptr;
    return &Itr->second.getNodeInfoAtLevel(N.DerefLevel);
  }

public:
  using const_value_iterator = ValueMap::const_iterator;

  bool addNode(Node N, AliasAttrs Attr = AliasAttrs()) {
    assert(N.Val != nullptr);
    auto &ValInfo = ValueImpls[N.Val];
    auto Changed = ValInfo.addNodeToLevel(N.DerefLevel);
    ValInfo.getNodeInfoAtLevel(N.DerefLevel).Attr |= Attr;
    return Changed;
  }

  void addAttr(Node N, AliasAttrs Attr) {
    auto *Info = getNode(N);
    assert(Info != nullptr);
    Info->Attr |= Attr;
  }

  void addEdge(Node From, Node To, int64_t Offset = 0) {
    auto *FromInfo = getNode(From);
    assert(FromInfo != nullptr);
    auto *ToInfo = getNode(To);
    assert(ToInfo != nullptr);

    FromInfo->Edges.push_back(Edge{To, Offset});
    ToInfo->ReverseEdges.push_back(Edge{From, Offset});
  }

  const NodeInfo *getNode(Node N) const {
    auto Itr = ValueImpls.find(N.Val);
    if (Itr == ValueImpls.end() || Itr->second.getNumLevels() <= N.DerefLevel)
      return nullptr;
    return &Itr->second.getNodeInfoAtLevel(N.DerefLevel);
  }

  AliasAttrs attrFor(Node N) const {
    auto *Info = getNode(N);
    assert(Info != nullptr);
    return Info->Attr;
  }

  iterator_range<const_value_iterator> value_mappings() const {
    return make_range<const_value_iterator>(ValueImpls.begin(),
                                            ValueImpls.end());
  }
};

///A builder class used to create CFLGraph instance from a given function
/// The CFL-AA that uses this builder must provide its own type as a template
/// argument. This is necessary for interprocedural processing: CFLGraphBuilder
/// needs a way of obtaining the summary of other functions when callinsts are
/// encountered.
/// As a result, we expect the said CFL-AA to expose a getAliasSummary() public
/// member function that takes a Function& and returns the corresponding summary
/// as a const AliasSummary*.
template <typename CFLAA> class CFLGraphBuilder {
  // Input of the builder
  CFLAA &Analysis;
  const TargetLibraryInfo &TLI;

  // Output of the builder
  CFLGraph Graph;
  SmallVector<Value *, 4> ReturnedValues;

  // Helper class
  /// Gets the edges our graph should have, based on an Instruction*
  class GetEdgesVisitor : public InstVisitor<GetEdgesVisitor, void> {
    CFLAA &AA;
    const DataLayout &DL;
    const TargetLibraryInfo &TLI;

    CFLGraph &Graph;
    SmallVectorImpl<Value *> &ReturnValues;

    static bool hasUsefulEdges(ConstantExpr *CE) {
      // ConstantExpr doesn't have terminators, invokes, or fences, so only
      // needs
      // to check for compares.
      return CE->getOpcode() != Instruction::ICmp &&
             CE->getOpcode() != Instruction::FCmp;
    }

    // Returns possible functions called by CS into the given SmallVectorImpl.
    // Returns true if targets found, false otherwise.
    static bool getPossibleTargets(CallSite CS,
                                   SmallVectorImpl<Function *> &Output) {
      if (auto *Fn = CS.getCalledFunction()) {
        Output.push_back(Fn);
        return true;
      }

      // TODO: If the call is indirect, we might be able to enumerate all
      // potential
      // targets of the call and return them, rather than just failing.
      return false;
    }

    void addNode(Value *Val, AliasAttrs Attr = AliasAttrs()) {
      assert(Val != nullptr && Val->getType()->isPointerTy());
      if (auto GVal = dyn_cast<GlobalValue>(Val)) {
        if (Graph.addNode(InstantiatedValue{GVal, 0},
                          getGlobalOrArgAttrFromValue(*GVal)))
          Graph.addNode(InstantiatedValue{GVal, 1}, getAttrUnknown());
      } else if (auto CExpr = dyn_cast<ConstantExpr>(Val)) {
        if (hasUsefulEdges(CExpr)) {
          if (Graph.addNode(InstantiatedValue{CExpr, 0}))
            visitConstantExpr(CExpr);
        }
      } else
        Graph.addNode(InstantiatedValue{Val, 0}, Attr);
    }

    void addAssignEdge(Value *From, Value *To, int64_t Offset = 0) {
      assert(From != nullptr && To != nullptr);
      if (!From->getType()->isPointerTy() || !To->getType()->isPointerTy())
        return;
      addNode(From);
      if (To != From) {
        addNode(To);
        Graph.addEdge(InstantiatedValue{From, 0}, InstantiatedValue{To, 0},
                      Offset);
      }
    }

    void addDerefEdge(Value *From, Value *To, bool IsRead) {
      assert(From != nullptr && To != nullptr);
      // FIXME: This is subtly broken, due to how we model some instructions
      // (e.g. extractvalue, extractelement) as loads. Since those take
      // non-pointer operands, we'll entirely skip adding edges for those.
      //
      // addAssignEdge seems to have a similar issue with insertvalue, etc.
      if (!From->getType()->isPointerTy() || !To->getType()->isPointerTy())
        return;
      addNode(From);
      addNode(To);
      if (IsRead) {
        Graph.addNode(InstantiatedValue{From, 1});
        Graph.addEdge(InstantiatedValue{From, 1}, InstantiatedValue{To, 0});
      } else {
        Graph.addNode(InstantiatedValue{To, 1});
        Graph.addEdge(InstantiatedValue{From, 0}, InstantiatedValue{To, 1});
      }
    }

    void addLoadEdge(Value *From, Value *To) { addDerefEdge(From, To, true); }
    void addStoreEdge(Value *From, Value *To) { addDerefEdge(From, To, false); }

  public:
    GetEdgesVisitor(CFLGraphBuilder &Builder, const DataLayout &DL)
        : AA(Builder.Analysis), DL(DL), TLI(Builder.TLI), Graph(Builder.Graph),
          ReturnValues(Builder.ReturnedValues) {}

    void visitInstruction(Instruction &) {
      llvm_unreachable("Unsupported instruction encountered");
    }

    void visitReturnInst(ReturnInst &Inst) {
      if (auto RetVal = Inst.getReturnValue()) {
        if (RetVal->getType()->isPointerTy()) {
          addNode(RetVal);
          ReturnValues.push_back(RetVal);
        }
      }
    }

    void visitPtrToIntInst(PtrToIntInst &Inst) {
      auto *Ptr = Inst.getOperand(0);
      addNode(Ptr, getAttrEscaped());
    }

    void visitIntToPtrInst(IntToPtrInst &Inst) {
      auto *Ptr = &Inst;
      addNode(Ptr, getAttrUnknown());
    }

    void visitCastInst(CastInst &Inst) {
      auto *Src = Inst.getOperand(0);
      addAssignEdge(Src, &Inst);
    }

    void visitBinaryOperator(BinaryOperator &Inst) {
      auto *Op1 = Inst.getOperand(0);
      auto *Op2 = Inst.getOperand(1);
      addAssignEdge(Op1, &Inst);
      addAssignEdge(Op2, &Inst);
    }

    void visitAtomicCmpXchgInst(AtomicCmpXchgInst &Inst) {
      auto *Ptr = Inst.getPointerOperand();
      auto *Val = Inst.getNewValOperand();
      addStoreEdge(Val, Ptr);
    }

    void visitAtomicRMWInst(AtomicRMWInst &Inst) {
      auto *Ptr = Inst.getPointerOperand();
      auto *Val = Inst.getValOperand();
      addStoreEdge(Val, Ptr);
    }

    void visitPHINode(PHINode &Inst) {
      for (Value *Val : Inst.incoming_values())
        addAssignEdge(Val, &Inst);
    }

    void visitGEP(GEPOperator &GEPOp) {
      uint64_t Offset = UnknownOffset;
      APInt APOffset(DL.getPointerSizeInBits(GEPOp.getPointerAddressSpace()),
                     0);
      if (GEPOp.accumulateConstantOffset(DL, APOffset))
        Offset = APOffset.getSExtValue();

      auto *Op = GEPOp.getPointerOperand();
      addAssignEdge(Op, &GEPOp, Offset);
    }

    void visitGetElementPtrInst(GetElementPtrInst &Inst) {
      auto *GEPOp = cast<GEPOperator>(&Inst);
      visitGEP(*GEPOp);
    }

    void visitSelectInst(SelectInst &Inst) {
      // Condition is not processed here (The actual statement producing
      // the condition result is processed elsewhere). For select, the
      // condition is evaluated, but not loaded, stored, or assigned
      // simply as a result of being the condition of a select.

      auto *TrueVal = Inst.getTrueValue();
      auto *FalseVal = Inst.getFalseValue();
      addAssignEdge(TrueVal, &Inst);
      addAssignEdge(FalseVal, &Inst);
    }

    void visitAllocaInst(AllocaInst &Inst) { addNode(&Inst); }

    void visitLoadInst(LoadInst &Inst) {
      auto *Ptr = Inst.getPointerOperand();
      auto *Val = &Inst;
      addLoadEdge(Ptr, Val);
    }

    void visitStoreInst(StoreInst &Inst) {
      auto *Ptr = Inst.getPointerOperand();
      auto *Val = Inst.getValueOperand();
      addStoreEdge(Val, Ptr);
    }

    void visitVAArgInst(VAArgInst &Inst) {
      // We can't fully model va_arg here. For *Ptr = Inst.getOperand(0), it
      // does
      // two things:
      //  1. Loads a value from *((T*)*Ptr).
      //  2. Increments (stores to) *Ptr by some target-specific amount.
      // For now, we'll handle this like a landingpad instruction (by placing
      // the
      // result in its own group, and having that group alias externals).
      if (Inst.getType()->isPointerTy())
        addNode(&Inst, getAttrUnknown());
    }

    static bool isFunctionExternal(Function *Fn) {
      return !Fn->hasExactDefinition();
    }

    bool tryInterproceduralAnalysis(CallSite CS,
                                    const SmallVectorImpl<Function *> &Fns) {
      assert(Fns.size() > 0);

      if (CS.arg_size() > MaxSupportedArgsInSummary)
        return false;

      // Exit early if we'll fail anyway
      for (auto *Fn : Fns) {
        if (isFunctionExternal(Fn) || Fn->isVarArg())
          return false;
        // Fail if the caller does not provide enough arguments
        assert(Fn->arg_size() <= CS.arg_size());
        if (!AA.getAliasSummary(*Fn))
          return false;
      }

      for (auto *Fn : Fns) {
        auto Summary = AA.getAliasSummary(*Fn);
        assert(Summary != nullptr);

        auto &RetParamRelations = Summary->RetParamRelations;
        for (auto &Relation : RetParamRelations) {
          auto IRelation = instantiateExternalRelation(Relation, CS);
          if (IRelation.hasValue()) {
            Graph.addNode(IRelation->From);
            Graph.addNode(IRelation->To);
            Graph.addEdge(IRelation->From, IRelation->To);
          }
        }

        auto &RetParamAttributes = Summary->RetParamAttributes;
        for (auto &Attribute : RetParamAttributes) {
          auto IAttr = instantiateExternalAttribute(Attribute, CS);
          if (IAttr.hasValue())
            Graph.addNode(IAttr->IValue, IAttr->Attr);
        }
      }

      return true;
    }

    void visitCallSite(CallSite CS) {
      auto Inst = CS.getInstruction();

      // Make sure all arguments and return value are added to the graph first
      for (Value *V : CS.args())
        if (V->getType()->isPointerTy())
          addNode(V);
      if (Inst->getType()->isPointerTy())
        addNode(Inst);

      // Check if Inst is a call to a library function that
      // allocates/deallocates on the heap. Those kinds of functions do not
      // introduce any aliases.
      // TODO: address other common library functions such as realloc(),
      // strdup(), etc.
      if (isMallocOrCallocLikeFn(Inst, &TLI) || isFreeCall(Inst, &TLI))
        return;

      // TODO: Add support for noalias args/all the other fun function
      // attributes that we can tack on.
      SmallVector<Function *, 4> Targets;
      if (getPossibleTargets(CS, Targets))
        if (tryInterproceduralAnalysis(CS, Targets))
          return;

      // Because the function is opaque, we need to note that anything
      // could have happened to the arguments (unless the function is marked
      // readonly or readnone), and that the result could alias just about
      // anything, too (unless the result is marked noalias).
      if (!CS.onlyReadsMemory())
        for (Value *V : CS.args()) {
          if (V->getType()->isPointerTy()) {
            // The argument itself escapes.
            Graph.addAttr(InstantiatedValue{V, 0}, getAttrEscaped());
            // The fate of argument memory is unknown. Note that since
            // AliasAttrs is transitive with respect to dereference, we only
            // need to specify it for the first-level memory.
            Graph.addNode(InstantiatedValue{V, 1}, getAttrUnknown());
          }
        }

      if (Inst->getType()->isPointerTy()) {
        auto *Fn = CS.getCalledFunction();
        if (Fn == nullptr || !Fn->returnDoesNotAlias())
          // No need to call addNode() since we've added Inst at the
          // beginning of this function and we know it is not a global.
          Graph.addAttr(InstantiatedValue{Inst, 0}, getAttrUnknown());
      }
    }

    /// Because vectors/aggregates are immutable and unaddressable, there's
    /// nothing we can do to coax a value out of them, other than calling
    /// Extract{Element,Value}. We can effectively treat them as pointers to
    /// arbitrary memory locations we can store in and load from.
    void visitExtractElementInst(ExtractElementInst &Inst) {
      auto *Ptr = Inst.getVectorOperand();
      auto *Val = &Inst;
      addLoadEdge(Ptr, Val);
    }

    void visitInsertElementInst(InsertElementInst &Inst) {
      auto *Vec = Inst.getOperand(0);
      auto *Val = Inst.getOperand(1);
      addAssignEdge(Vec, &Inst);
      addStoreEdge(Val, &Inst);
    }

    void visitLandingPadInst(LandingPadInst &Inst) {
      // Exceptions come from "nowhere", from our analysis' perspective.
      // So we place the instruction its own group, noting that said group may
      // alias externals
      if (Inst.getType()->isPointerTy())
        addNode(&Inst, getAttrUnknown());
    }

    void visitInsertValueInst(InsertValueInst &Inst) {
      auto *Agg = Inst.getOperand(0);
      auto *Val = Inst.getOperand(1);
      addAssignEdge(Agg, &Inst);
      addStoreEdge(Val, &Inst);
    }

    void visitExtractValueInst(ExtractValueInst &Inst) {
      auto *Ptr = Inst.getAggregateOperand();
      addLoadEdge(Ptr, &Inst);
    }

    void visitShuffleVectorInst(ShuffleVectorInst &Inst) {
      auto *From1 = Inst.getOperand(0);
      auto *From2 = Inst.getOperand(1);
      addAssignEdge(From1, &Inst);
      addAssignEdge(From2, &Inst);
    }

    void visitConstantExpr(ConstantExpr *CE) {
      switch (CE->getOpcode()) {
      case Instruction::GetElementPtr: {
        auto GEPOp = cast<GEPOperator>(CE);
        visitGEP(*GEPOp);
        break;
      }

      case Instruction::PtrToInt: {
        addNode(CE->getOperand(0), getAttrEscaped());
        break;
      }

      case Instruction::IntToPtr: {
        addNode(CE, getAttrUnknown());
        break;
      }

      case Instruction::BitCast:
      case Instruction::AddrSpaceCast:
      case Instruction::Trunc:
      case Instruction::ZExt:
      case Instruction::SExt:
      case Instruction::FPExt:
      case Instruction::FPTrunc:
      case Instruction::UIToFP:
      case Instruction::SIToFP:
      case Instruction::FPToUI:
      case Instruction::FPToSI: {
        addAssignEdge(CE->getOperand(0), CE);
        break;
      }

      case Instruction::Select: {
        addAssignEdge(CE->getOperand(1), CE);
        addAssignEdge(CE->getOperand(2), CE);
        break;
      }

      case Instruction::InsertElement:
      case Instruction::InsertValue: {
        addAssignEdge(CE->getOperand(0), CE);
        addStoreEdge(CE->getOperand(1), CE);
        break;
      }

      case Instruction::ExtractElement:
      case Instruction::ExtractValue: {
        addLoadEdge(CE->getOperand(0), CE);
        break;
      }

      case Instruction::Add:
      case Instruction::Sub:
      case Instruction::FSub:
      case Instruction::Mul:
      case Instruction::FMul:
      case Instruction::UDiv:
      case Instruction::SDiv:
      case Instruction::FDiv:
      case Instruction::URem:
      case Instruction::SRem:
      case Instruction::FRem:
      case Instruction::And:
      case Instruction::Or:
      case Instruction::Xor:
      case Instruction::Shl:
      case Instruction::LShr:
      case Instruction::AShr:
      case Instruction::ICmp:
      case Instruction::FCmp:
      case Instruction::ShuffleVector: {
        addAssignEdge(CE->getOperand(0), CE);
        addAssignEdge(CE->getOperand(1), CE);
        break;
      }

      default:
        llvm_unreachable("Unknown instruction type encountered!");
      }
    }
  };

  // Helper functions

  // Determines whether or not we an instruction is useless to us (e.g.
  // FenceInst)
  static bool hasUsefulEdges(Instruction *Inst) {
    bool IsNonInvokeRetTerminator = Inst->isTerminator() &&
                                    !isa<InvokeInst>(Inst) &&
                                    !isa<ReturnInst>(Inst);
    return !isa<CmpInst>(Inst) && !isa<FenceInst>(Inst) &&
           !IsNonInvokeRetTerminator;
  }

  void addArgumentToGraph(Argument &Arg) {
    if (Arg.getType()->isPointerTy()) {
      Graph.addNode(InstantiatedValue{&Arg, 0},
                    getGlobalOrArgAttrFromValue(Arg));
      // Pointees of a formal parameter is known to the caller
      Graph.addNode(InstantiatedValue{&Arg, 1}, getAttrCaller());
    }
  }

  // Given an Instruction, this will add it to the graph, along with any
  // Instructions that are potentially only available from said Instruction
  // For example, given the following line:
  //   %0 = load i16* getelementptr ([1 x i16]* @a, 0, 0), align 2
  // addInstructionToGraph would add both the `load` and `getelementptr`
  // instructions to the graph appropriately.
  void addInstructionToGraph(GetEdgesVisitor &Visitor, Instruction &Inst) {
    if (!hasUsefulEdges(&Inst))
      return;

    Visitor.visit(Inst);
  }

  // Builds the graph needed for constructing the StratifiedSets for the given
  // function
  void buildGraphFrom(Function &Fn) {
    GetEdgesVisitor Visitor(*this, Fn.getParent()->getDataLayout());

    for (auto &Bb : Fn.getBasicBlockList())
      for (auto &Inst : Bb.getInstList())
        addInstructionToGraph(Visitor, Inst);

    for (auto &Arg : Fn.args())
      addArgumentToGraph(Arg);
  }

public:
  CFLGraphBuilder(CFLAA &Analysis, const TargetLibraryInfo &TLI, Function &Fn)
      : Analysis(Analysis), TLI(TLI) {
    buildGraphFrom(Fn);
  }

  const CFLGraph &getCFLGraph() const { return Graph; }
  const SmallVector<Value *, 4> &getReturnValues() const {
    return ReturnedValues;
  }
};

} // end namespace cflaa
} // end namespace llvm

#endif // LLVM_LIB_ANALYSIS_CFLGRAPH_H
