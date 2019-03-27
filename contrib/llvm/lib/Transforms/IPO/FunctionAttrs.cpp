//===- FunctionAttrs.cpp - Pass which marks functions attributes ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements interprocedural passes which walk the
/// call-graph deducing and/or propagating function attributes.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include <cassert>
#include <iterator>
#include <map>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "functionattrs"

STATISTIC(NumReadNone, "Number of functions marked readnone");
STATISTIC(NumReadOnly, "Number of functions marked readonly");
STATISTIC(NumWriteOnly, "Number of functions marked writeonly");
STATISTIC(NumNoCapture, "Number of arguments marked nocapture");
STATISTIC(NumReturned, "Number of arguments marked returned");
STATISTIC(NumReadNoneArg, "Number of arguments marked readnone");
STATISTIC(NumReadOnlyArg, "Number of arguments marked readonly");
STATISTIC(NumNoAlias, "Number of function returns marked noalias");
STATISTIC(NumNonNullReturn, "Number of function returns marked nonnull");
STATISTIC(NumNoRecurse, "Number of functions marked as norecurse");
STATISTIC(NumNoUnwind, "Number of functions marked as nounwind");

// FIXME: This is disabled by default to avoid exposing security vulnerabilities
// in C/C++ code compiled by clang:
// http://lists.llvm.org/pipermail/cfe-dev/2017-January/052066.html
static cl::opt<bool> EnableNonnullArgPropagation(
    "enable-nonnull-arg-prop", cl::Hidden,
    cl::desc("Try to propagate nonnull argument attributes from callsites to "
             "caller functions."));

static cl::opt<bool> DisableNoUnwindInference(
    "disable-nounwind-inference", cl::Hidden,
    cl::desc("Stop inferring nounwind attribute during function-attrs pass"));

namespace {

using SCCNodeSet = SmallSetVector<Function *, 8>;

} // end anonymous namespace

/// Returns the memory access attribute for function F using AAR for AA results,
/// where SCCNodes is the current SCC.
///
/// If ThisBody is true, this function may examine the function body and will
/// return a result pertaining to this copy of the function. If it is false, the
/// result will be based only on AA results for the function declaration; it
/// will be assumed that some other (perhaps less optimized) version of the
/// function may be selected at link time.
static MemoryAccessKind checkFunctionMemoryAccess(Function &F, bool ThisBody,
                                                  AAResults &AAR,
                                                  const SCCNodeSet &SCCNodes) {
  FunctionModRefBehavior MRB = AAR.getModRefBehavior(&F);
  if (MRB == FMRB_DoesNotAccessMemory)
    // Already perfect!
    return MAK_ReadNone;

  if (!ThisBody) {
    if (AliasAnalysis::onlyReadsMemory(MRB))
      return MAK_ReadOnly;

    if (AliasAnalysis::doesNotReadMemory(MRB))
      return MAK_WriteOnly;

    // Conservatively assume it reads and writes to memory.
    return MAK_MayWrite;
  }

  // Scan the function body for instructions that may read or write memory.
  bool ReadsMemory = false;
  bool WritesMemory = false;
  for (inst_iterator II = inst_begin(F), E = inst_end(F); II != E; ++II) {
    Instruction *I = &*II;

    // Some instructions can be ignored even if they read or write memory.
    // Detect these now, skipping to the next instruction if one is found.
    if (auto *Call = dyn_cast<CallBase>(I)) {
      // Ignore calls to functions in the same SCC, as long as the call sites
      // don't have operand bundles.  Calls with operand bundles are allowed to
      // have memory effects not described by the memory effects of the call
      // target.
      if (!Call->hasOperandBundles() && Call->getCalledFunction() &&
          SCCNodes.count(Call->getCalledFunction()))
        continue;
      FunctionModRefBehavior MRB = AAR.getModRefBehavior(Call);
      ModRefInfo MRI = createModRefInfo(MRB);

      // If the call doesn't access memory, we're done.
      if (isNoModRef(MRI))
        continue;

      if (!AliasAnalysis::onlyAccessesArgPointees(MRB)) {
        // The call could access any memory. If that includes writes, note it.
        if (isModSet(MRI))
          WritesMemory = true;
        // If it reads, note it.
        if (isRefSet(MRI))
          ReadsMemory = true;
        continue;
      }

      // Check whether all pointer arguments point to local memory, and
      // ignore calls that only access local memory.
      for (CallSite::arg_iterator CI = Call->arg_begin(), CE = Call->arg_end();
           CI != CE; ++CI) {
        Value *Arg = *CI;
        if (!Arg->getType()->isPtrOrPtrVectorTy())
          continue;

        AAMDNodes AAInfo;
        I->getAAMetadata(AAInfo);
        MemoryLocation Loc(Arg, LocationSize::unknown(), AAInfo);

        // Skip accesses to local or constant memory as they don't impact the
        // externally visible mod/ref behavior.
        if (AAR.pointsToConstantMemory(Loc, /*OrLocal=*/true))
          continue;

        if (isModSet(MRI))
          // Writes non-local memory.
          WritesMemory = true;
        if (isRefSet(MRI))
          // Ok, it reads non-local memory.
          ReadsMemory = true;
      }
      continue;
    } else if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
      // Ignore non-volatile loads from local memory. (Atomic is okay here.)
      if (!LI->isVolatile()) {
        MemoryLocation Loc = MemoryLocation::get(LI);
        if (AAR.pointsToConstantMemory(Loc, /*OrLocal=*/true))
          continue;
      }
    } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
      // Ignore non-volatile stores to local memory. (Atomic is okay here.)
      if (!SI->isVolatile()) {
        MemoryLocation Loc = MemoryLocation::get(SI);
        if (AAR.pointsToConstantMemory(Loc, /*OrLocal=*/true))
          continue;
      }
    } else if (VAArgInst *VI = dyn_cast<VAArgInst>(I)) {
      // Ignore vaargs on local memory.
      MemoryLocation Loc = MemoryLocation::get(VI);
      if (AAR.pointsToConstantMemory(Loc, /*OrLocal=*/true))
        continue;
    }

    // Any remaining instructions need to be taken seriously!  Check if they
    // read or write memory.
    //
    // Writes memory, remember that.
    WritesMemory |= I->mayWriteToMemory();

    // If this instruction may read memory, remember that.
    ReadsMemory |= I->mayReadFromMemory();
  }

  if (WritesMemory) { 
    if (!ReadsMemory)
      return MAK_WriteOnly;
    else
      return MAK_MayWrite;
  }

  return ReadsMemory ? MAK_ReadOnly : MAK_ReadNone;
}

MemoryAccessKind llvm::computeFunctionBodyMemoryAccess(Function &F,
                                                       AAResults &AAR) {
  return checkFunctionMemoryAccess(F, /*ThisBody=*/true, AAR, {});
}

/// Deduce readonly/readnone attributes for the SCC.
template <typename AARGetterT>
static bool addReadAttrs(const SCCNodeSet &SCCNodes, AARGetterT &&AARGetter) {
  // Check if any of the functions in the SCC read or write memory.  If they
  // write memory then they can't be marked readnone or readonly.
  bool ReadsMemory = false;
  bool WritesMemory = false;
  for (Function *F : SCCNodes) {
    // Call the callable parameter to look up AA results for this function.
    AAResults &AAR = AARGetter(*F);

    // Non-exact function definitions may not be selected at link time, and an
    // alternative version that writes to memory may be selected.  See the
    // comment on GlobalValue::isDefinitionExact for more details.
    switch (checkFunctionMemoryAccess(*F, F->hasExactDefinition(),
                                      AAR, SCCNodes)) {
    case MAK_MayWrite:
      return false;
    case MAK_ReadOnly:
      ReadsMemory = true;
      break;
    case MAK_WriteOnly:
      WritesMemory = true;
      break;
    case MAK_ReadNone:
      // Nothing to do!
      break;
    }
  }

  // Success!  Functions in this SCC do not access memory, or only read memory.
  // Give them the appropriate attribute.
  bool MadeChange = false;

  assert(!(ReadsMemory && WritesMemory) &&
          "Function marked read-only and write-only");
  for (Function *F : SCCNodes) {
    if (F->doesNotAccessMemory())
      // Already perfect!
      continue;

    if (F->onlyReadsMemory() && ReadsMemory)
      // No change.
      continue;

    if (F->doesNotReadMemory() && WritesMemory)
      continue;

    MadeChange = true;

    // Clear out any existing attributes.
    F->removeFnAttr(Attribute::ReadOnly);
    F->removeFnAttr(Attribute::ReadNone);
    F->removeFnAttr(Attribute::WriteOnly);

    if (!WritesMemory && !ReadsMemory) {
      // Clear out any "access range attributes" if readnone was deduced.
      F->removeFnAttr(Attribute::ArgMemOnly);
      F->removeFnAttr(Attribute::InaccessibleMemOnly);
      F->removeFnAttr(Attribute::InaccessibleMemOrArgMemOnly);
    }

    // Add in the new attribute.
    if (WritesMemory && !ReadsMemory)
      F->addFnAttr(Attribute::WriteOnly);
    else
      F->addFnAttr(ReadsMemory ? Attribute::ReadOnly : Attribute::ReadNone);

    if (WritesMemory && !ReadsMemory)
      ++NumWriteOnly;
    else if (ReadsMemory)
      ++NumReadOnly;
    else
      ++NumReadNone;
  }

  return MadeChange;
}

namespace {

/// For a given pointer Argument, this retains a list of Arguments of functions
/// in the same SCC that the pointer data flows into. We use this to build an
/// SCC of the arguments.
struct ArgumentGraphNode {
  Argument *Definition;
  SmallVector<ArgumentGraphNode *, 4> Uses;
};

class ArgumentGraph {
  // We store pointers to ArgumentGraphNode objects, so it's important that
  // that they not move around upon insert.
  using ArgumentMapTy = std::map<Argument *, ArgumentGraphNode>;

  ArgumentMapTy ArgumentMap;

  // There is no root node for the argument graph, in fact:
  //   void f(int *x, int *y) { if (...) f(x, y); }
  // is an example where the graph is disconnected. The SCCIterator requires a
  // single entry point, so we maintain a fake ("synthetic") root node that
  // uses every node. Because the graph is directed and nothing points into
  // the root, it will not participate in any SCCs (except for its own).
  ArgumentGraphNode SyntheticRoot;

public:
  ArgumentGraph() { SyntheticRoot.Definition = nullptr; }

  using iterator = SmallVectorImpl<ArgumentGraphNode *>::iterator;

  iterator begin() { return SyntheticRoot.Uses.begin(); }
  iterator end() { return SyntheticRoot.Uses.end(); }
  ArgumentGraphNode *getEntryNode() { return &SyntheticRoot; }

  ArgumentGraphNode *operator[](Argument *A) {
    ArgumentGraphNode &Node = ArgumentMap[A];
    Node.Definition = A;
    SyntheticRoot.Uses.push_back(&Node);
    return &Node;
  }
};

/// This tracker checks whether callees are in the SCC, and if so it does not
/// consider that a capture, instead adding it to the "Uses" list and
/// continuing with the analysis.
struct ArgumentUsesTracker : public CaptureTracker {
  ArgumentUsesTracker(const SCCNodeSet &SCCNodes) : SCCNodes(SCCNodes) {}

  void tooManyUses() override { Captured = true; }

  bool captured(const Use *U) override {
    CallSite CS(U->getUser());
    if (!CS.getInstruction()) {
      Captured = true;
      return true;
    }

    Function *F = CS.getCalledFunction();
    if (!F || !F->hasExactDefinition() || !SCCNodes.count(F)) {
      Captured = true;
      return true;
    }

    // Note: the callee and the two successor blocks *follow* the argument
    // operands.  This means there is no need to adjust UseIndex to account for
    // these.

    unsigned UseIndex =
        std::distance(const_cast<const Use *>(CS.arg_begin()), U);

    assert(UseIndex < CS.data_operands_size() &&
           "Indirect function calls should have been filtered above!");

    if (UseIndex >= CS.getNumArgOperands()) {
      // Data operand, but not a argument operand -- must be a bundle operand
      assert(CS.hasOperandBundles() && "Must be!");

      // CaptureTracking told us that we're being captured by an operand bundle
      // use.  In this case it does not matter if the callee is within our SCC
      // or not -- we've been captured in some unknown way, and we have to be
      // conservative.
      Captured = true;
      return true;
    }

    if (UseIndex >= F->arg_size()) {
      assert(F->isVarArg() && "More params than args in non-varargs call");
      Captured = true;
      return true;
    }

    Uses.push_back(&*std::next(F->arg_begin(), UseIndex));
    return false;
  }

  // True only if certainly captured (used outside our SCC).
  bool Captured = false;

  // Uses within our SCC.
  SmallVector<Argument *, 4> Uses;

  const SCCNodeSet &SCCNodes;
};

} // end anonymous namespace

namespace llvm {

template <> struct GraphTraits<ArgumentGraphNode *> {
  using NodeRef = ArgumentGraphNode *;
  using ChildIteratorType = SmallVectorImpl<ArgumentGraphNode *>::iterator;

  static NodeRef getEntryNode(NodeRef A) { return A; }
  static ChildIteratorType child_begin(NodeRef N) { return N->Uses.begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->Uses.end(); }
};

template <>
struct GraphTraits<ArgumentGraph *> : public GraphTraits<ArgumentGraphNode *> {
  static NodeRef getEntryNode(ArgumentGraph *AG) { return AG->getEntryNode(); }

  static ChildIteratorType nodes_begin(ArgumentGraph *AG) {
    return AG->begin();
  }

  static ChildIteratorType nodes_end(ArgumentGraph *AG) { return AG->end(); }
};

} // end namespace llvm

/// Returns Attribute::None, Attribute::ReadOnly or Attribute::ReadNone.
static Attribute::AttrKind
determinePointerReadAttrs(Argument *A,
                          const SmallPtrSet<Argument *, 8> &SCCNodes) {
  SmallVector<Use *, 32> Worklist;
  SmallPtrSet<Use *, 32> Visited;

  // inalloca arguments are always clobbered by the call.
  if (A->hasInAllocaAttr())
    return Attribute::None;

  bool IsRead = false;
  // We don't need to track IsWritten. If A is written to, return immediately.

  for (Use &U : A->uses()) {
    Visited.insert(&U);
    Worklist.push_back(&U);
  }

  while (!Worklist.empty()) {
    Use *U = Worklist.pop_back_val();
    Instruction *I = cast<Instruction>(U->getUser());

    switch (I->getOpcode()) {
    case Instruction::BitCast:
    case Instruction::GetElementPtr:
    case Instruction::PHI:
    case Instruction::Select:
    case Instruction::AddrSpaceCast:
      // The original value is not read/written via this if the new value isn't.
      for (Use &UU : I->uses())
        if (Visited.insert(&UU).second)
          Worklist.push_back(&UU);
      break;

    case Instruction::Call:
    case Instruction::Invoke: {
      bool Captures = true;

      if (I->getType()->isVoidTy())
        Captures = false;

      auto AddUsersToWorklistIfCapturing = [&] {
        if (Captures)
          for (Use &UU : I->uses())
            if (Visited.insert(&UU).second)
              Worklist.push_back(&UU);
      };

      CallSite CS(I);
      if (CS.doesNotAccessMemory()) {
        AddUsersToWorklistIfCapturing();
        continue;
      }

      Function *F = CS.getCalledFunction();
      if (!F) {
        if (CS.onlyReadsMemory()) {
          IsRead = true;
          AddUsersToWorklistIfCapturing();
          continue;
        }
        return Attribute::None;
      }

      // Note: the callee and the two successor blocks *follow* the argument
      // operands.  This means there is no need to adjust UseIndex to account
      // for these.

      unsigned UseIndex = std::distance(CS.arg_begin(), U);

      // U cannot be the callee operand use: since we're exploring the
      // transitive uses of an Argument, having such a use be a callee would
      // imply the CallSite is an indirect call or invoke; and we'd take the
      // early exit above.
      assert(UseIndex < CS.data_operands_size() &&
             "Data operand use expected!");

      bool IsOperandBundleUse = UseIndex >= CS.getNumArgOperands();

      if (UseIndex >= F->arg_size() && !IsOperandBundleUse) {
        assert(F->isVarArg() && "More params than args in non-varargs call");
        return Attribute::None;
      }

      Captures &= !CS.doesNotCapture(UseIndex);

      // Since the optimizer (by design) cannot see the data flow corresponding
      // to a operand bundle use, these cannot participate in the optimistic SCC
      // analysis.  Instead, we model the operand bundle uses as arguments in
      // call to a function external to the SCC.
      if (IsOperandBundleUse ||
          !SCCNodes.count(&*std::next(F->arg_begin(), UseIndex))) {

        // The accessors used on CallSite here do the right thing for calls and
        // invokes with operand bundles.

        if (!CS.onlyReadsMemory() && !CS.onlyReadsMemory(UseIndex))
          return Attribute::None;
        if (!CS.doesNotAccessMemory(UseIndex))
          IsRead = true;
      }

      AddUsersToWorklistIfCapturing();
      break;
    }

    case Instruction::Load:
      // A volatile load has side effects beyond what readonly can be relied
      // upon.
      if (cast<LoadInst>(I)->isVolatile())
        return Attribute::None;

      IsRead = true;
      break;

    case Instruction::ICmp:
    case Instruction::Ret:
      break;

    default:
      return Attribute::None;
    }
  }

  return IsRead ? Attribute::ReadOnly : Attribute::ReadNone;
}

/// Deduce returned attributes for the SCC.
static bool addArgumentReturnedAttrs(const SCCNodeSet &SCCNodes) {
  bool Changed = false;

  // Check each function in turn, determining if an argument is always returned.
  for (Function *F : SCCNodes) {
    // We can infer and propagate function attributes only when we know that the
    // definition we'll get at link time is *exactly* the definition we see now.
    // For more details, see GlobalValue::mayBeDerefined.
    if (!F->hasExactDefinition())
      continue;

    if (F->getReturnType()->isVoidTy())
      continue;

    // There is nothing to do if an argument is already marked as 'returned'.
    if (llvm::any_of(F->args(),
                     [](const Argument &Arg) { return Arg.hasReturnedAttr(); }))
      continue;

    auto FindRetArg = [&]() -> Value * {
      Value *RetArg = nullptr;
      for (BasicBlock &BB : *F)
        if (auto *Ret = dyn_cast<ReturnInst>(BB.getTerminator())) {
          // Note that stripPointerCasts should look through functions with
          // returned arguments.
          Value *RetVal = Ret->getReturnValue()->stripPointerCasts();
          if (!isa<Argument>(RetVal) || RetVal->getType() != F->getReturnType())
            return nullptr;

          if (!RetArg)
            RetArg = RetVal;
          else if (RetArg != RetVal)
            return nullptr;
        }

      return RetArg;
    };

    if (Value *RetArg = FindRetArg()) {
      auto *A = cast<Argument>(RetArg);
      A->addAttr(Attribute::Returned);
      ++NumReturned;
      Changed = true;
    }
  }

  return Changed;
}

/// If a callsite has arguments that are also arguments to the parent function,
/// try to propagate attributes from the callsite's arguments to the parent's
/// arguments. This may be important because inlining can cause information loss
/// when attribute knowledge disappears with the inlined call.
static bool addArgumentAttrsFromCallsites(Function &F) {
  if (!EnableNonnullArgPropagation)
    return false;

  bool Changed = false;

  // For an argument attribute to transfer from a callsite to the parent, the
  // call must be guaranteed to execute every time the parent is called.
  // Conservatively, just check for calls in the entry block that are guaranteed
  // to execute.
  // TODO: This could be enhanced by testing if the callsite post-dominates the
  // entry block or by doing simple forward walks or backward walks to the
  // callsite.
  BasicBlock &Entry = F.getEntryBlock();
  for (Instruction &I : Entry) {
    if (auto CS = CallSite(&I)) {
      if (auto *CalledFunc = CS.getCalledFunction()) {
        for (auto &CSArg : CalledFunc->args()) {
          if (!CSArg.hasNonNullAttr())
            continue;

          // If the non-null callsite argument operand is an argument to 'F'
          // (the caller) and the call is guaranteed to execute, then the value
          // must be non-null throughout 'F'.
          auto *FArg = dyn_cast<Argument>(CS.getArgOperand(CSArg.getArgNo()));
          if (FArg && !FArg->hasNonNullAttr()) {
            FArg->addAttr(Attribute::NonNull);
            Changed = true;
          }
        }
      }
    }
    if (!isGuaranteedToTransferExecutionToSuccessor(&I))
      break;
  }

  return Changed;
}

/// Deduce nocapture attributes for the SCC.
static bool addArgumentAttrs(const SCCNodeSet &SCCNodes) {
  bool Changed = false;

  ArgumentGraph AG;

  // Check each function in turn, determining which pointer arguments are not
  // captured.
  for (Function *F : SCCNodes) {
    // We can infer and propagate function attributes only when we know that the
    // definition we'll get at link time is *exactly* the definition we see now.
    // For more details, see GlobalValue::mayBeDerefined.
    if (!F->hasExactDefinition())
      continue;

    Changed |= addArgumentAttrsFromCallsites(*F);

    // Functions that are readonly (or readnone) and nounwind and don't return
    // a value can't capture arguments. Don't analyze them.
    if (F->onlyReadsMemory() && F->doesNotThrow() &&
        F->getReturnType()->isVoidTy()) {
      for (Function::arg_iterator A = F->arg_begin(), E = F->arg_end(); A != E;
           ++A) {
        if (A->getType()->isPointerTy() && !A->hasNoCaptureAttr()) {
          A->addAttr(Attribute::NoCapture);
          ++NumNoCapture;
          Changed = true;
        }
      }
      continue;
    }

    for (Function::arg_iterator A = F->arg_begin(), E = F->arg_end(); A != E;
         ++A) {
      if (!A->getType()->isPointerTy())
        continue;
      bool HasNonLocalUses = false;
      if (!A->hasNoCaptureAttr()) {
        ArgumentUsesTracker Tracker(SCCNodes);
        PointerMayBeCaptured(&*A, &Tracker);
        if (!Tracker.Captured) {
          if (Tracker.Uses.empty()) {
            // If it's trivially not captured, mark it nocapture now.
            A->addAttr(Attribute::NoCapture);
            ++NumNoCapture;
            Changed = true;
          } else {
            // If it's not trivially captured and not trivially not captured,
            // then it must be calling into another function in our SCC. Save
            // its particulars for Argument-SCC analysis later.
            ArgumentGraphNode *Node = AG[&*A];
            for (Argument *Use : Tracker.Uses) {
              Node->Uses.push_back(AG[Use]);
              if (Use != &*A)
                HasNonLocalUses = true;
            }
          }
        }
        // Otherwise, it's captured. Don't bother doing SCC analysis on it.
      }
      if (!HasNonLocalUses && !A->onlyReadsMemory()) {
        // Can we determine that it's readonly/readnone without doing an SCC?
        // Note that we don't allow any calls at all here, or else our result
        // will be dependent on the iteration order through the functions in the
        // SCC.
        SmallPtrSet<Argument *, 8> Self;
        Self.insert(&*A);
        Attribute::AttrKind R = determinePointerReadAttrs(&*A, Self);
        if (R != Attribute::None) {
          A->addAttr(R);
          Changed = true;
          R == Attribute::ReadOnly ? ++NumReadOnlyArg : ++NumReadNoneArg;
        }
      }
    }
  }

  // The graph we've collected is partial because we stopped scanning for
  // argument uses once we solved the argument trivially. These partial nodes
  // show up as ArgumentGraphNode objects with an empty Uses list, and for
  // these nodes the final decision about whether they capture has already been
  // made.  If the definition doesn't have a 'nocapture' attribute by now, it
  // captures.

  for (scc_iterator<ArgumentGraph *> I = scc_begin(&AG); !I.isAtEnd(); ++I) {
    const std::vector<ArgumentGraphNode *> &ArgumentSCC = *I;
    if (ArgumentSCC.size() == 1) {
      if (!ArgumentSCC[0]->Definition)
        continue; // synthetic root node

      // eg. "void f(int* x) { if (...) f(x); }"
      if (ArgumentSCC[0]->Uses.size() == 1 &&
          ArgumentSCC[0]->Uses[0] == ArgumentSCC[0]) {
        Argument *A = ArgumentSCC[0]->Definition;
        A->addAttr(Attribute::NoCapture);
        ++NumNoCapture;
        Changed = true;
      }
      continue;
    }

    bool SCCCaptured = false;
    for (auto I = ArgumentSCC.begin(), E = ArgumentSCC.end();
         I != E && !SCCCaptured; ++I) {
      ArgumentGraphNode *Node = *I;
      if (Node->Uses.empty()) {
        if (!Node->Definition->hasNoCaptureAttr())
          SCCCaptured = true;
      }
    }
    if (SCCCaptured)
      continue;

    SmallPtrSet<Argument *, 8> ArgumentSCCNodes;
    // Fill ArgumentSCCNodes with the elements of the ArgumentSCC.  Used for
    // quickly looking up whether a given Argument is in this ArgumentSCC.
    for (ArgumentGraphNode *I : ArgumentSCC) {
      ArgumentSCCNodes.insert(I->Definition);
    }

    for (auto I = ArgumentSCC.begin(), E = ArgumentSCC.end();
         I != E && !SCCCaptured; ++I) {
      ArgumentGraphNode *N = *I;
      for (ArgumentGraphNode *Use : N->Uses) {
        Argument *A = Use->Definition;
        if (A->hasNoCaptureAttr() || ArgumentSCCNodes.count(A))
          continue;
        SCCCaptured = true;
        break;
      }
    }
    if (SCCCaptured)
      continue;

    for (unsigned i = 0, e = ArgumentSCC.size(); i != e; ++i) {
      Argument *A = ArgumentSCC[i]->Definition;
      A->addAttr(Attribute::NoCapture);
      ++NumNoCapture;
      Changed = true;
    }

    // We also want to compute readonly/readnone. With a small number of false
    // negatives, we can assume that any pointer which is captured isn't going
    // to be provably readonly or readnone, since by definition we can't
    // analyze all uses of a captured pointer.
    //
    // The false negatives happen when the pointer is captured by a function
    // that promises readonly/readnone behaviour on the pointer, then the
    // pointer's lifetime ends before anything that writes to arbitrary memory.
    // Also, a readonly/readnone pointer may be returned, but returning a
    // pointer is capturing it.

    Attribute::AttrKind ReadAttr = Attribute::ReadNone;
    for (unsigned i = 0, e = ArgumentSCC.size(); i != e; ++i) {
      Argument *A = ArgumentSCC[i]->Definition;
      Attribute::AttrKind K = determinePointerReadAttrs(A, ArgumentSCCNodes);
      if (K == Attribute::ReadNone)
        continue;
      if (K == Attribute::ReadOnly) {
        ReadAttr = Attribute::ReadOnly;
        continue;
      }
      ReadAttr = K;
      break;
    }

    if (ReadAttr != Attribute::None) {
      for (unsigned i = 0, e = ArgumentSCC.size(); i != e; ++i) {
        Argument *A = ArgumentSCC[i]->Definition;
        // Clear out existing readonly/readnone attributes
        A->removeAttr(Attribute::ReadOnly);
        A->removeAttr(Attribute::ReadNone);
        A->addAttr(ReadAttr);
        ReadAttr == Attribute::ReadOnly ? ++NumReadOnlyArg : ++NumReadNoneArg;
        Changed = true;
      }
    }
  }

  return Changed;
}

/// Tests whether a function is "malloc-like".
///
/// A function is "malloc-like" if it returns either null or a pointer that
/// doesn't alias any other pointer visible to the caller.
static bool isFunctionMallocLike(Function *F, const SCCNodeSet &SCCNodes) {
  SmallSetVector<Value *, 8> FlowsToReturn;
  for (BasicBlock &BB : *F)
    if (ReturnInst *Ret = dyn_cast<ReturnInst>(BB.getTerminator()))
      FlowsToReturn.insert(Ret->getReturnValue());

  for (unsigned i = 0; i != FlowsToReturn.size(); ++i) {
    Value *RetVal = FlowsToReturn[i];

    if (Constant *C = dyn_cast<Constant>(RetVal)) {
      if (!C->isNullValue() && !isa<UndefValue>(C))
        return false;

      continue;
    }

    if (isa<Argument>(RetVal))
      return false;

    if (Instruction *RVI = dyn_cast<Instruction>(RetVal))
      switch (RVI->getOpcode()) {
      // Extend the analysis by looking upwards.
      case Instruction::BitCast:
      case Instruction::GetElementPtr:
      case Instruction::AddrSpaceCast:
        FlowsToReturn.insert(RVI->getOperand(0));
        continue;
      case Instruction::Select: {
        SelectInst *SI = cast<SelectInst>(RVI);
        FlowsToReturn.insert(SI->getTrueValue());
        FlowsToReturn.insert(SI->getFalseValue());
        continue;
      }
      case Instruction::PHI: {
        PHINode *PN = cast<PHINode>(RVI);
        for (Value *IncValue : PN->incoming_values())
          FlowsToReturn.insert(IncValue);
        continue;
      }

      // Check whether the pointer came from an allocation.
      case Instruction::Alloca:
        break;
      case Instruction::Call:
      case Instruction::Invoke: {
        CallSite CS(RVI);
        if (CS.hasRetAttr(Attribute::NoAlias))
          break;
        if (CS.getCalledFunction() && SCCNodes.count(CS.getCalledFunction()))
          break;
        LLVM_FALLTHROUGH;
      }
      default:
        return false; // Did not come from an allocation.
      }

    if (PointerMayBeCaptured(RetVal, false, /*StoreCaptures=*/false))
      return false;
  }

  return true;
}

/// Deduce noalias attributes for the SCC.
static bool addNoAliasAttrs(const SCCNodeSet &SCCNodes) {
  // Check each function in turn, determining which functions return noalias
  // pointers.
  for (Function *F : SCCNodes) {
    // Already noalias.
    if (F->returnDoesNotAlias())
      continue;

    // We can infer and propagate function attributes only when we know that the
    // definition we'll get at link time is *exactly* the definition we see now.
    // For more details, see GlobalValue::mayBeDerefined.
    if (!F->hasExactDefinition())
      return false;

    // We annotate noalias return values, which are only applicable to
    // pointer types.
    if (!F->getReturnType()->isPointerTy())
      continue;

    if (!isFunctionMallocLike(F, SCCNodes))
      return false;
  }

  bool MadeChange = false;
  for (Function *F : SCCNodes) {
    if (F->returnDoesNotAlias() ||
        !F->getReturnType()->isPointerTy())
      continue;

    F->setReturnDoesNotAlias();
    ++NumNoAlias;
    MadeChange = true;
  }

  return MadeChange;
}

/// Tests whether this function is known to not return null.
///
/// Requires that the function returns a pointer.
///
/// Returns true if it believes the function will not return a null, and sets
/// \p Speculative based on whether the returned conclusion is a speculative
/// conclusion due to SCC calls.
static bool isReturnNonNull(Function *F, const SCCNodeSet &SCCNodes,
                            bool &Speculative) {
  assert(F->getReturnType()->isPointerTy() &&
         "nonnull only meaningful on pointer types");
  Speculative = false;

  SmallSetVector<Value *, 8> FlowsToReturn;
  for (BasicBlock &BB : *F)
    if (auto *Ret = dyn_cast<ReturnInst>(BB.getTerminator()))
      FlowsToReturn.insert(Ret->getReturnValue());

  auto &DL = F->getParent()->getDataLayout();

  for (unsigned i = 0; i != FlowsToReturn.size(); ++i) {
    Value *RetVal = FlowsToReturn[i];

    // If this value is locally known to be non-null, we're good
    if (isKnownNonZero(RetVal, DL))
      continue;

    // Otherwise, we need to look upwards since we can't make any local
    // conclusions.
    Instruction *RVI = dyn_cast<Instruction>(RetVal);
    if (!RVI)
      return false;
    switch (RVI->getOpcode()) {
    // Extend the analysis by looking upwards.
    case Instruction::BitCast:
    case Instruction::GetElementPtr:
    case Instruction::AddrSpaceCast:
      FlowsToReturn.insert(RVI->getOperand(0));
      continue;
    case Instruction::Select: {
      SelectInst *SI = cast<SelectInst>(RVI);
      FlowsToReturn.insert(SI->getTrueValue());
      FlowsToReturn.insert(SI->getFalseValue());
      continue;
    }
    case Instruction::PHI: {
      PHINode *PN = cast<PHINode>(RVI);
      for (int i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
        FlowsToReturn.insert(PN->getIncomingValue(i));
      continue;
    }
    case Instruction::Call:
    case Instruction::Invoke: {
      CallSite CS(RVI);
      Function *Callee = CS.getCalledFunction();
      // A call to a node within the SCC is assumed to return null until
      // proven otherwise
      if (Callee && SCCNodes.count(Callee)) {
        Speculative = true;
        continue;
      }
      return false;
    }
    default:
      return false; // Unknown source, may be null
    };
    llvm_unreachable("should have either continued or returned");
  }

  return true;
}

/// Deduce nonnull attributes for the SCC.
static bool addNonNullAttrs(const SCCNodeSet &SCCNodes) {
  // Speculative that all functions in the SCC return only nonnull
  // pointers.  We may refute this as we analyze functions.
  bool SCCReturnsNonNull = true;

  bool MadeChange = false;

  // Check each function in turn, determining which functions return nonnull
  // pointers.
  for (Function *F : SCCNodes) {
    // Already nonnull.
    if (F->getAttributes().hasAttribute(AttributeList::ReturnIndex,
                                        Attribute::NonNull))
      continue;

    // We can infer and propagate function attributes only when we know that the
    // definition we'll get at link time is *exactly* the definition we see now.
    // For more details, see GlobalValue::mayBeDerefined.
    if (!F->hasExactDefinition())
      return false;

    // We annotate nonnull return values, which are only applicable to
    // pointer types.
    if (!F->getReturnType()->isPointerTy())
      continue;

    bool Speculative = false;
    if (isReturnNonNull(F, SCCNodes, Speculative)) {
      if (!Speculative) {
        // Mark the function eagerly since we may discover a function
        // which prevents us from speculating about the entire SCC
        LLVM_DEBUG(dbgs() << "Eagerly marking " << F->getName()
                          << " as nonnull\n");
        F->addAttribute(AttributeList::ReturnIndex, Attribute::NonNull);
        ++NumNonNullReturn;
        MadeChange = true;
      }
      continue;
    }
    // At least one function returns something which could be null, can't
    // speculate any more.
    SCCReturnsNonNull = false;
  }

  if (SCCReturnsNonNull) {
    for (Function *F : SCCNodes) {
      if (F->getAttributes().hasAttribute(AttributeList::ReturnIndex,
                                          Attribute::NonNull) ||
          !F->getReturnType()->isPointerTy())
        continue;

      LLVM_DEBUG(dbgs() << "SCC marking " << F->getName() << " as nonnull\n");
      F->addAttribute(AttributeList::ReturnIndex, Attribute::NonNull);
      ++NumNonNullReturn;
      MadeChange = true;
    }
  }

  return MadeChange;
}

namespace {

/// Collects a set of attribute inference requests and performs them all in one
/// go on a single SCC Node. Inference involves scanning function bodies
/// looking for instructions that violate attribute assumptions.
/// As soon as all the bodies are fine we are free to set the attribute.
/// Customization of inference for individual attributes is performed by
/// providing a handful of predicates for each attribute.
class AttributeInferer {
public:
  /// Describes a request for inference of a single attribute.
  struct InferenceDescriptor {

    /// Returns true if this function does not have to be handled.
    /// General intent for this predicate is to provide an optimization
    /// for functions that do not need this attribute inference at all
    /// (say, for functions that already have the attribute).
    std::function<bool(const Function &)> SkipFunction;

    /// Returns true if this instruction violates attribute assumptions.
    std::function<bool(Instruction &)> InstrBreaksAttribute;

    /// Sets the inferred attribute for this function.
    std::function<void(Function &)> SetAttribute;

    /// Attribute we derive.
    Attribute::AttrKind AKind;

    /// If true, only "exact" definitions can be used to infer this attribute.
    /// See GlobalValue::isDefinitionExact.
    bool RequiresExactDefinition;

    InferenceDescriptor(Attribute::AttrKind AK,
                        std::function<bool(const Function &)> SkipFunc,
                        std::function<bool(Instruction &)> InstrScan,
                        std::function<void(Function &)> SetAttr,
                        bool ReqExactDef)
        : SkipFunction(SkipFunc), InstrBreaksAttribute(InstrScan),
          SetAttribute(SetAttr), AKind(AK),
          RequiresExactDefinition(ReqExactDef) {}
  };

private:
  SmallVector<InferenceDescriptor, 4> InferenceDescriptors;

public:
  void registerAttrInference(InferenceDescriptor AttrInference) {
    InferenceDescriptors.push_back(AttrInference);
  }

  bool run(const SCCNodeSet &SCCNodes);
};

/// Perform all the requested attribute inference actions according to the
/// attribute predicates stored before.
bool AttributeInferer::run(const SCCNodeSet &SCCNodes) {
  SmallVector<InferenceDescriptor, 4> InferInSCC = InferenceDescriptors;
  // Go through all the functions in SCC and check corresponding attribute
  // assumptions for each of them. Attributes that are invalid for this SCC
  // will be removed from InferInSCC.
  for (Function *F : SCCNodes) {

    // No attributes whose assumptions are still valid - done.
    if (InferInSCC.empty())
      return false;

    // Check if our attributes ever need scanning/can be scanned.
    llvm::erase_if(InferInSCC, [F](const InferenceDescriptor &ID) {
      if (ID.SkipFunction(*F))
        return false;

      // Remove from further inference (invalidate) when visiting a function
      // that has no instructions to scan/has an unsuitable definition.
      return F->isDeclaration() ||
             (ID.RequiresExactDefinition && !F->hasExactDefinition());
    });

    // For each attribute still in InferInSCC that doesn't explicitly skip F,
    // set up the F instructions scan to verify assumptions of the attribute.
    SmallVector<InferenceDescriptor, 4> InferInThisFunc;
    llvm::copy_if(
        InferInSCC, std::back_inserter(InferInThisFunc),
        [F](const InferenceDescriptor &ID) { return !ID.SkipFunction(*F); });

    if (InferInThisFunc.empty())
      continue;

    // Start instruction scan.
    for (Instruction &I : instructions(*F)) {
      llvm::erase_if(InferInThisFunc, [&](const InferenceDescriptor &ID) {
        if (!ID.InstrBreaksAttribute(I))
          return false;
        // Remove attribute from further inference on any other functions
        // because attribute assumptions have just been violated.
        llvm::erase_if(InferInSCC, [&ID](const InferenceDescriptor &D) {
          return D.AKind == ID.AKind;
        });
        // Remove attribute from the rest of current instruction scan.
        return true;
      });

      if (InferInThisFunc.empty())
        break;
    }
  }

  if (InferInSCC.empty())
    return false;

  bool Changed = false;
  for (Function *F : SCCNodes)
    // At this point InferInSCC contains only functions that were either:
    //   - explicitly skipped from scan/inference, or
    //   - verified to have no instructions that break attribute assumptions.
    // Hence we just go and force the attribute for all non-skipped functions.
    for (auto &ID : InferInSCC) {
      if (ID.SkipFunction(*F))
        continue;
      Changed = true;
      ID.SetAttribute(*F);
    }
  return Changed;
}

} // end anonymous namespace

/// Helper for non-Convergent inference predicate InstrBreaksAttribute.
static bool InstrBreaksNonConvergent(Instruction &I,
                                     const SCCNodeSet &SCCNodes) {
  const CallSite CS(&I);
  // Breaks non-convergent assumption if CS is a convergent call to a function
  // not in the SCC.
  return CS && CS.isConvergent() && SCCNodes.count(CS.getCalledFunction()) == 0;
}

/// Helper for NoUnwind inference predicate InstrBreaksAttribute.
static bool InstrBreaksNonThrowing(Instruction &I, const SCCNodeSet &SCCNodes) {
  if (!I.mayThrow())
    return false;
  if (const auto *CI = dyn_cast<CallInst>(&I)) {
    if (Function *Callee = CI->getCalledFunction()) {
      // I is a may-throw call to a function inside our SCC. This doesn't
      // invalidate our current working assumption that the SCC is no-throw; we
      // just have to scan that other function.
      if (SCCNodes.count(Callee) > 0)
        return false;
    }
  }
  return true;
}

/// Infer attributes from all functions in the SCC by scanning every
/// instruction for compliance to the attribute assumptions. Currently it
/// does:
///   - removal of Convergent attribute
///   - addition of NoUnwind attribute
///
/// Returns true if any changes to function attributes were made.
static bool inferAttrsFromFunctionBodies(const SCCNodeSet &SCCNodes) {

  AttributeInferer AI;

  // Request to remove the convergent attribute from all functions in the SCC
  // if every callsite within the SCC is not convergent (except for calls
  // to functions within the SCC).
  // Note: Removal of the attr from the callsites will happen in
  // InstCombineCalls separately.
  AI.registerAttrInference(AttributeInferer::InferenceDescriptor{
      Attribute::Convergent,
      // Skip non-convergent functions.
      [](const Function &F) { return !F.isConvergent(); },
      // Instructions that break non-convergent assumption.
      [SCCNodes](Instruction &I) {
        return InstrBreaksNonConvergent(I, SCCNodes);
      },
      [](Function &F) {
        LLVM_DEBUG(dbgs() << "Removing convergent attr from fn " << F.getName()
                          << "\n");
        F.setNotConvergent();
      },
      /* RequiresExactDefinition= */ false});

  if (!DisableNoUnwindInference)
    // Request to infer nounwind attribute for all the functions in the SCC if
    // every callsite within the SCC is not throwing (except for calls to
    // functions within the SCC). Note that nounwind attribute suffers from
    // derefinement - results may change depending on how functions are
    // optimized. Thus it can be inferred only from exact definitions.
    AI.registerAttrInference(AttributeInferer::InferenceDescriptor{
        Attribute::NoUnwind,
        // Skip non-throwing functions.
        [](const Function &F) { return F.doesNotThrow(); },
        // Instructions that break non-throwing assumption.
        [SCCNodes](Instruction &I) {
          return InstrBreaksNonThrowing(I, SCCNodes);
        },
        [](Function &F) {
          LLVM_DEBUG(dbgs()
                     << "Adding nounwind attr to fn " << F.getName() << "\n");
          F.setDoesNotThrow();
          ++NumNoUnwind;
        },
        /* RequiresExactDefinition= */ true});

  // Perform all the requested attribute inference actions.
  return AI.run(SCCNodes);
}

static bool setDoesNotRecurse(Function &F) {
  if (F.doesNotRecurse())
    return false;
  F.setDoesNotRecurse();
  ++NumNoRecurse;
  return true;
}

static bool addNoRecurseAttrs(const SCCNodeSet &SCCNodes) {
  // Try and identify functions that do not recurse.

  // If the SCC contains multiple nodes we know for sure there is recursion.
  if (SCCNodes.size() != 1)
    return false;

  Function *F = *SCCNodes.begin();
  if (!F || F->isDeclaration() || F->doesNotRecurse())
    return false;

  // If all of the calls in F are identifiable and are to norecurse functions, F
  // is norecurse. This check also detects self-recursion as F is not currently
  // marked norecurse, so any called from F to F will not be marked norecurse.
  for (auto &BB : *F)
    for (auto &I : BB.instructionsWithoutDebug())
      if (auto CS = CallSite(&I)) {
        Function *Callee = CS.getCalledFunction();
        if (!Callee || Callee == F || !Callee->doesNotRecurse())
          // Function calls a potentially recursive function.
          return false;
      }

  // Every call was to a non-recursive function other than this function, and
  // we have no indirect recursion as the SCC size is one. This function cannot
  // recurse.
  return setDoesNotRecurse(*F);
}

template <typename AARGetterT>
static bool deriveAttrsInPostOrder(SCCNodeSet &SCCNodes, AARGetterT &&AARGetter,
                                   bool HasUnknownCall) {
  bool Changed = false;

  // Bail if the SCC only contains optnone functions.
  if (SCCNodes.empty())
    return Changed;

  Changed |= addArgumentReturnedAttrs(SCCNodes);
  Changed |= addReadAttrs(SCCNodes, AARGetter);
  Changed |= addArgumentAttrs(SCCNodes);

  // If we have no external nodes participating in the SCC, we can deduce some
  // more precise attributes as well.
  if (!HasUnknownCall) {
    Changed |= addNoAliasAttrs(SCCNodes);
    Changed |= addNonNullAttrs(SCCNodes);
    Changed |= inferAttrsFromFunctionBodies(SCCNodes);
    Changed |= addNoRecurseAttrs(SCCNodes);
  }

  return Changed;
}

PreservedAnalyses PostOrderFunctionAttrsPass::run(LazyCallGraph::SCC &C,
                                                  CGSCCAnalysisManager &AM,
                                                  LazyCallGraph &CG,
                                                  CGSCCUpdateResult &) {
  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerCGSCCProxy>(C, CG).getManager();

  // We pass a lambda into functions to wire them up to the analysis manager
  // for getting function analyses.
  auto AARGetter = [&](Function &F) -> AAResults & {
    return FAM.getResult<AAManager>(F);
  };

  // Fill SCCNodes with the elements of the SCC. Also track whether there are
  // any external or opt-none nodes that will prevent us from optimizing any
  // part of the SCC.
  SCCNodeSet SCCNodes;
  bool HasUnknownCall = false;
  for (LazyCallGraph::Node &N : C) {
    Function &F = N.getFunction();
    if (F.hasFnAttribute(Attribute::OptimizeNone) ||
        F.hasFnAttribute(Attribute::Naked)) {
      // Treat any function we're trying not to optimize as if it were an
      // indirect call and omit it from the node set used below.
      HasUnknownCall = true;
      continue;
    }
    // Track whether any functions in this SCC have an unknown call edge.
    // Note: if this is ever a performance hit, we can common it with
    // subsequent routines which also do scans over the instructions of the
    // function.
    if (!HasUnknownCall)
      for (Instruction &I : instructions(F))
        if (auto CS = CallSite(&I))
          if (!CS.getCalledFunction()) {
            HasUnknownCall = true;
            break;
          }

    SCCNodes.insert(&F);
  }

  if (deriveAttrsInPostOrder(SCCNodes, AARGetter, HasUnknownCall))
    return PreservedAnalyses::none();

  return PreservedAnalyses::all();
}

namespace {

struct PostOrderFunctionAttrsLegacyPass : public CallGraphSCCPass {
  // Pass identification, replacement for typeid
  static char ID;

  PostOrderFunctionAttrsLegacyPass() : CallGraphSCCPass(ID) {
    initializePostOrderFunctionAttrsLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnSCC(CallGraphSCC &SCC) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<AssumptionCacheTracker>();
    getAAResultsAnalysisUsage(AU);
    CallGraphSCCPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char PostOrderFunctionAttrsLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(PostOrderFunctionAttrsLegacyPass, "functionattrs",
                      "Deduce function attributes", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(CallGraphWrapperPass)
INITIALIZE_PASS_END(PostOrderFunctionAttrsLegacyPass, "functionattrs",
                    "Deduce function attributes", false, false)

Pass *llvm::createPostOrderFunctionAttrsLegacyPass() {
  return new PostOrderFunctionAttrsLegacyPass();
}

template <typename AARGetterT>
static bool runImpl(CallGraphSCC &SCC, AARGetterT AARGetter) {

  // Fill SCCNodes with the elements of the SCC. Used for quickly looking up
  // whether a given CallGraphNode is in this SCC. Also track whether there are
  // any external or opt-none nodes that will prevent us from optimizing any
  // part of the SCC.
  SCCNodeSet SCCNodes;
  bool ExternalNode = false;
  for (CallGraphNode *I : SCC) {
    Function *F = I->getFunction();
    if (!F || F->hasFnAttribute(Attribute::OptimizeNone) ||
        F->hasFnAttribute(Attribute::Naked)) {
      // External node or function we're trying not to optimize - we both avoid
      // transform them and avoid leveraging information they provide.
      ExternalNode = true;
      continue;
    }

    SCCNodes.insert(F);
  }

  return deriveAttrsInPostOrder(SCCNodes, AARGetter, ExternalNode);
}

bool PostOrderFunctionAttrsLegacyPass::runOnSCC(CallGraphSCC &SCC) {
  if (skipSCC(SCC))
    return false;
  return runImpl(SCC, LegacyAARGetter(*this));
}

namespace {

struct ReversePostOrderFunctionAttrsLegacyPass : public ModulePass {
  // Pass identification, replacement for typeid
  static char ID;

  ReversePostOrderFunctionAttrsLegacyPass() : ModulePass(ID) {
    initializeReversePostOrderFunctionAttrsLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<CallGraphWrapperPass>();
    AU.addPreserved<CallGraphWrapperPass>();
  }
};

} // end anonymous namespace

char ReversePostOrderFunctionAttrsLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(ReversePostOrderFunctionAttrsLegacyPass, "rpo-functionattrs",
                      "Deduce function attributes in RPO", false, false)
INITIALIZE_PASS_DEPENDENCY(CallGraphWrapperPass)
INITIALIZE_PASS_END(ReversePostOrderFunctionAttrsLegacyPass, "rpo-functionattrs",
                    "Deduce function attributes in RPO", false, false)

Pass *llvm::createReversePostOrderFunctionAttrsPass() {
  return new ReversePostOrderFunctionAttrsLegacyPass();
}

static bool addNoRecurseAttrsTopDown(Function &F) {
  // We check the preconditions for the function prior to calling this to avoid
  // the cost of building up a reversible post-order list. We assert them here
  // to make sure none of the invariants this relies on were violated.
  assert(!F.isDeclaration() && "Cannot deduce norecurse without a definition!");
  assert(!F.doesNotRecurse() &&
         "This function has already been deduced as norecurs!");
  assert(F.hasInternalLinkage() &&
         "Can only do top-down deduction for internal linkage functions!");

  // If F is internal and all of its uses are calls from a non-recursive
  // functions, then none of its calls could in fact recurse without going
  // through a function marked norecurse, and so we can mark this function too
  // as norecurse. Note that the uses must actually be calls -- otherwise
  // a pointer to this function could be returned from a norecurse function but
  // this function could be recursively (indirectly) called. Note that this
  // also detects if F is directly recursive as F is not yet marked as
  // a norecurse function.
  for (auto *U : F.users()) {
    auto *I = dyn_cast<Instruction>(U);
    if (!I)
      return false;
    CallSite CS(I);
    if (!CS || !CS.getParent()->getParent()->doesNotRecurse())
      return false;
  }
  return setDoesNotRecurse(F);
}

static bool deduceFunctionAttributeInRPO(Module &M, CallGraph &CG) {
  // We only have a post-order SCC traversal (because SCCs are inherently
  // discovered in post-order), so we accumulate them in a vector and then walk
  // it in reverse. This is simpler than using the RPO iterator infrastructure
  // because we need to combine SCC detection and the PO walk of the call
  // graph. We can also cheat egregiously because we're primarily interested in
  // synthesizing norecurse and so we can only save the singular SCCs as SCCs
  // with multiple functions in them will clearly be recursive.
  SmallVector<Function *, 16> Worklist;
  for (scc_iterator<CallGraph *> I = scc_begin(&CG); !I.isAtEnd(); ++I) {
    if (I->size() != 1)
      continue;

    Function *F = I->front()->getFunction();
    if (F && !F->isDeclaration() && !F->doesNotRecurse() &&
        F->hasInternalLinkage())
      Worklist.push_back(F);
  }

  bool Changed = false;
  for (auto *F : llvm::reverse(Worklist))
    Changed |= addNoRecurseAttrsTopDown(*F);

  return Changed;
}

bool ReversePostOrderFunctionAttrsLegacyPass::runOnModule(Module &M) {
  if (skipModule(M))
    return false;

  auto &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();

  return deduceFunctionAttributeInRPO(M, CG);
}

PreservedAnalyses
ReversePostOrderFunctionAttrsPass::run(Module &M, ModuleAnalysisManager &AM) {
  auto &CG = AM.getResult<CallGraphAnalysis>(M);

  if (!deduceFunctionAttributeInRPO(M, CG))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserve<CallGraphAnalysis>();
  return PA;
}
