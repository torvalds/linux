//===- GlobalsModRef.cpp - Simple Mod/Ref Analysis for Globals ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This simple pass provides alias and mod/ref information for global values
// that do not have their address taken, and keeps track of whether functions
// read or write memory (are "pure").  For this simple (but very common) case,
// we can provide pretty accurate and useful information.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
using namespace llvm;

#define DEBUG_TYPE "globalsmodref-aa"

STATISTIC(NumNonAddrTakenGlobalVars,
          "Number of global vars without address taken");
STATISTIC(NumNonAddrTakenFunctions,"Number of functions without address taken");
STATISTIC(NumNoMemFunctions, "Number of functions that do not access memory");
STATISTIC(NumReadMemFunctions, "Number of functions that only read memory");
STATISTIC(NumIndirectGlobalVars, "Number of indirect global objects");

// An option to enable unsafe alias results from the GlobalsModRef analysis.
// When enabled, GlobalsModRef will provide no-alias results which in extremely
// rare cases may not be conservatively correct. In particular, in the face of
// transforms which cause assymetry between how effective GetUnderlyingObject
// is for two pointers, it may produce incorrect results.
//
// These unsafe results have been returned by GMR for many years without
// causing significant issues in the wild and so we provide a mechanism to
// re-enable them for users of LLVM that have a particular performance
// sensitivity and no known issues. The option also makes it easy to evaluate
// the performance impact of these results.
static cl::opt<bool> EnableUnsafeGlobalsModRefAliasResults(
    "enable-unsafe-globalsmodref-alias-results", cl::init(false), cl::Hidden);

/// The mod/ref information collected for a particular function.
///
/// We collect information about mod/ref behavior of a function here, both in
/// general and as pertains to specific globals. We only have this detailed
/// information when we know *something* useful about the behavior. If we
/// saturate to fully general mod/ref, we remove the info for the function.
class GlobalsAAResult::FunctionInfo {
  typedef SmallDenseMap<const GlobalValue *, ModRefInfo, 16> GlobalInfoMapType;

  /// Build a wrapper struct that has 8-byte alignment. All heap allocations
  /// should provide this much alignment at least, but this makes it clear we
  /// specifically rely on this amount of alignment.
  struct alignas(8) AlignedMap {
    AlignedMap() {}
    AlignedMap(const AlignedMap &Arg) : Map(Arg.Map) {}
    GlobalInfoMapType Map;
  };

  /// Pointer traits for our aligned map.
  struct AlignedMapPointerTraits {
    static inline void *getAsVoidPointer(AlignedMap *P) { return P; }
    static inline AlignedMap *getFromVoidPointer(void *P) {
      return (AlignedMap *)P;
    }
    enum { NumLowBitsAvailable = 3 };
    static_assert(alignof(AlignedMap) >= (1 << NumLowBitsAvailable),
                  "AlignedMap insufficiently aligned to have enough low bits.");
  };

  /// The bit that flags that this function may read any global. This is
  /// chosen to mix together with ModRefInfo bits.
  /// FIXME: This assumes ModRefInfo lattice will remain 4 bits!
  /// It overlaps with ModRefInfo::Must bit!
  /// FunctionInfo.getModRefInfo() masks out everything except ModRef so
  /// this remains correct, but the Must info is lost.
  enum { MayReadAnyGlobal = 4 };

  /// Checks to document the invariants of the bit packing here.
  static_assert((MayReadAnyGlobal & static_cast<int>(ModRefInfo::MustModRef)) ==
                    0,
                "ModRef and the MayReadAnyGlobal flag bits overlap.");
  static_assert(((MayReadAnyGlobal |
                  static_cast<int>(ModRefInfo::MustModRef)) >>
                 AlignedMapPointerTraits::NumLowBitsAvailable) == 0,
                "Insufficient low bits to store our flag and ModRef info.");

public:
  FunctionInfo() : Info() {}
  ~FunctionInfo() {
    delete Info.getPointer();
  }
  // Spell out the copy ond move constructors and assignment operators to get
  // deep copy semantics and correct move semantics in the face of the
  // pointer-int pair.
  FunctionInfo(const FunctionInfo &Arg)
      : Info(nullptr, Arg.Info.getInt()) {
    if (const auto *ArgPtr = Arg.Info.getPointer())
      Info.setPointer(new AlignedMap(*ArgPtr));
  }
  FunctionInfo(FunctionInfo &&Arg)
      : Info(Arg.Info.getPointer(), Arg.Info.getInt()) {
    Arg.Info.setPointerAndInt(nullptr, 0);
  }
  FunctionInfo &operator=(const FunctionInfo &RHS) {
    delete Info.getPointer();
    Info.setPointerAndInt(nullptr, RHS.Info.getInt());
    if (const auto *RHSPtr = RHS.Info.getPointer())
      Info.setPointer(new AlignedMap(*RHSPtr));
    return *this;
  }
  FunctionInfo &operator=(FunctionInfo &&RHS) {
    delete Info.getPointer();
    Info.setPointerAndInt(RHS.Info.getPointer(), RHS.Info.getInt());
    RHS.Info.setPointerAndInt(nullptr, 0);
    return *this;
  }

  /// This method clears MayReadAnyGlobal bit added by GlobalsAAResult to return
  /// the corresponding ModRefInfo. It must align in functionality with
  /// clearMust().
  ModRefInfo globalClearMayReadAnyGlobal(int I) const {
    return ModRefInfo((I & static_cast<int>(ModRefInfo::ModRef)) |
                      static_cast<int>(ModRefInfo::NoModRef));
  }

  /// Returns the \c ModRefInfo info for this function.
  ModRefInfo getModRefInfo() const {
    return globalClearMayReadAnyGlobal(Info.getInt());
  }

  /// Adds new \c ModRefInfo for this function to its state.
  void addModRefInfo(ModRefInfo NewMRI) {
    Info.setInt(Info.getInt() | static_cast<int>(setMust(NewMRI)));
  }

  /// Returns whether this function may read any global variable, and we don't
  /// know which global.
  bool mayReadAnyGlobal() const { return Info.getInt() & MayReadAnyGlobal; }

  /// Sets this function as potentially reading from any global.
  void setMayReadAnyGlobal() { Info.setInt(Info.getInt() | MayReadAnyGlobal); }

  /// Returns the \c ModRefInfo info for this function w.r.t. a particular
  /// global, which may be more precise than the general information above.
  ModRefInfo getModRefInfoForGlobal(const GlobalValue &GV) const {
    ModRefInfo GlobalMRI =
        mayReadAnyGlobal() ? ModRefInfo::Ref : ModRefInfo::NoModRef;
    if (AlignedMap *P = Info.getPointer()) {
      auto I = P->Map.find(&GV);
      if (I != P->Map.end())
        GlobalMRI = unionModRef(GlobalMRI, I->second);
    }
    return GlobalMRI;
  }

  /// Add mod/ref info from another function into ours, saturating towards
  /// ModRef.
  void addFunctionInfo(const FunctionInfo &FI) {
    addModRefInfo(FI.getModRefInfo());

    if (FI.mayReadAnyGlobal())
      setMayReadAnyGlobal();

    if (AlignedMap *P = FI.Info.getPointer())
      for (const auto &G : P->Map)
        addModRefInfoForGlobal(*G.first, G.second);
  }

  void addModRefInfoForGlobal(const GlobalValue &GV, ModRefInfo NewMRI) {
    AlignedMap *P = Info.getPointer();
    if (!P) {
      P = new AlignedMap();
      Info.setPointer(P);
    }
    auto &GlobalMRI = P->Map[&GV];
    GlobalMRI = unionModRef(GlobalMRI, NewMRI);
  }

  /// Clear a global's ModRef info. Should be used when a global is being
  /// deleted.
  void eraseModRefInfoForGlobal(const GlobalValue &GV) {
    if (AlignedMap *P = Info.getPointer())
      P->Map.erase(&GV);
  }

private:
  /// All of the information is encoded into a single pointer, with a three bit
  /// integer in the low three bits. The high bit provides a flag for when this
  /// function may read any global. The low two bits are the ModRefInfo. And
  /// the pointer, when non-null, points to a map from GlobalValue to
  /// ModRefInfo specific to that GlobalValue.
  PointerIntPair<AlignedMap *, 3, unsigned, AlignedMapPointerTraits> Info;
};

void GlobalsAAResult::DeletionCallbackHandle::deleted() {
  Value *V = getValPtr();
  if (auto *F = dyn_cast<Function>(V))
    GAR->FunctionInfos.erase(F);

  if (GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
    if (GAR->NonAddressTakenGlobals.erase(GV)) {
      // This global might be an indirect global.  If so, remove it and
      // remove any AllocRelatedValues for it.
      if (GAR->IndirectGlobals.erase(GV)) {
        // Remove any entries in AllocsForIndirectGlobals for this global.
        for (auto I = GAR->AllocsForIndirectGlobals.begin(),
                  E = GAR->AllocsForIndirectGlobals.end();
             I != E; ++I)
          if (I->second == GV)
            GAR->AllocsForIndirectGlobals.erase(I);
      }

      // Scan the function info we have collected and remove this global
      // from all of them.
      for (auto &FIPair : GAR->FunctionInfos)
        FIPair.second.eraseModRefInfoForGlobal(*GV);
    }
  }

  // If this is an allocation related to an indirect global, remove it.
  GAR->AllocsForIndirectGlobals.erase(V);

  // And clear out the handle.
  setValPtr(nullptr);
  GAR->Handles.erase(I);
  // This object is now destroyed!
}

FunctionModRefBehavior GlobalsAAResult::getModRefBehavior(const Function *F) {
  FunctionModRefBehavior Min = FMRB_UnknownModRefBehavior;

  if (FunctionInfo *FI = getFunctionInfo(F)) {
    if (!isModOrRefSet(FI->getModRefInfo()))
      Min = FMRB_DoesNotAccessMemory;
    else if (!isModSet(FI->getModRefInfo()))
      Min = FMRB_OnlyReadsMemory;
  }

  return FunctionModRefBehavior(AAResultBase::getModRefBehavior(F) & Min);
}

FunctionModRefBehavior
GlobalsAAResult::getModRefBehavior(const CallBase *Call) {
  FunctionModRefBehavior Min = FMRB_UnknownModRefBehavior;

  if (!Call->hasOperandBundles())
    if (const Function *F = Call->getCalledFunction())
      if (FunctionInfo *FI = getFunctionInfo(F)) {
        if (!isModOrRefSet(FI->getModRefInfo()))
          Min = FMRB_DoesNotAccessMemory;
        else if (!isModSet(FI->getModRefInfo()))
          Min = FMRB_OnlyReadsMemory;
      }

  return FunctionModRefBehavior(AAResultBase::getModRefBehavior(Call) & Min);
}

/// Returns the function info for the function, or null if we don't have
/// anything useful to say about it.
GlobalsAAResult::FunctionInfo *
GlobalsAAResult::getFunctionInfo(const Function *F) {
  auto I = FunctionInfos.find(F);
  if (I != FunctionInfos.end())
    return &I->second;
  return nullptr;
}

/// AnalyzeGlobals - Scan through the users of all of the internal
/// GlobalValue's in the program.  If none of them have their "address taken"
/// (really, their address passed to something nontrivial), record this fact,
/// and record the functions that they are used directly in.
void GlobalsAAResult::AnalyzeGlobals(Module &M) {
  SmallPtrSet<Function *, 32> TrackedFunctions;
  for (Function &F : M)
    if (F.hasLocalLinkage())
      if (!AnalyzeUsesOfPointer(&F)) {
        // Remember that we are tracking this global.
        NonAddressTakenGlobals.insert(&F);
        TrackedFunctions.insert(&F);
        Handles.emplace_front(*this, &F);
        Handles.front().I = Handles.begin();
        ++NumNonAddrTakenFunctions;
      }

  SmallPtrSet<Function *, 16> Readers, Writers;
  for (GlobalVariable &GV : M.globals())
    if (GV.hasLocalLinkage()) {
      if (!AnalyzeUsesOfPointer(&GV, &Readers,
                                GV.isConstant() ? nullptr : &Writers)) {
        // Remember that we are tracking this global, and the mod/ref fns
        NonAddressTakenGlobals.insert(&GV);
        Handles.emplace_front(*this, &GV);
        Handles.front().I = Handles.begin();

        for (Function *Reader : Readers) {
          if (TrackedFunctions.insert(Reader).second) {
            Handles.emplace_front(*this, Reader);
            Handles.front().I = Handles.begin();
          }
          FunctionInfos[Reader].addModRefInfoForGlobal(GV, ModRefInfo::Ref);
        }

        if (!GV.isConstant()) // No need to keep track of writers to constants
          for (Function *Writer : Writers) {
            if (TrackedFunctions.insert(Writer).second) {
              Handles.emplace_front(*this, Writer);
              Handles.front().I = Handles.begin();
            }
            FunctionInfos[Writer].addModRefInfoForGlobal(GV, ModRefInfo::Mod);
          }
        ++NumNonAddrTakenGlobalVars;

        // If this global holds a pointer type, see if it is an indirect global.
        if (GV.getValueType()->isPointerTy() &&
            AnalyzeIndirectGlobalMemory(&GV))
          ++NumIndirectGlobalVars;
      }
      Readers.clear();
      Writers.clear();
    }
}

/// AnalyzeUsesOfPointer - Look at all of the users of the specified pointer.
/// If this is used by anything complex (i.e., the address escapes), return
/// true.  Also, while we are at it, keep track of those functions that read and
/// write to the value.
///
/// If OkayStoreDest is non-null, stores into this global are allowed.
bool GlobalsAAResult::AnalyzeUsesOfPointer(Value *V,
                                           SmallPtrSetImpl<Function *> *Readers,
                                           SmallPtrSetImpl<Function *> *Writers,
                                           GlobalValue *OkayStoreDest) {
  if (!V->getType()->isPointerTy())
    return true;

  for (Use &U : V->uses()) {
    User *I = U.getUser();
    if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
      if (Readers)
        Readers->insert(LI->getParent()->getParent());
    } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
      if (V == SI->getOperand(1)) {
        if (Writers)
          Writers->insert(SI->getParent()->getParent());
      } else if (SI->getOperand(1) != OkayStoreDest) {
        return true; // Storing the pointer
      }
    } else if (Operator::getOpcode(I) == Instruction::GetElementPtr) {
      if (AnalyzeUsesOfPointer(I, Readers, Writers))
        return true;
    } else if (Operator::getOpcode(I) == Instruction::BitCast) {
      if (AnalyzeUsesOfPointer(I, Readers, Writers, OkayStoreDest))
        return true;
    } else if (auto *Call = dyn_cast<CallBase>(I)) {
      // Make sure that this is just the function being called, not that it is
      // passing into the function.
      if (Call->isDataOperand(&U)) {
        // Detect calls to free.
        if (Call->isArgOperand(&U) && isFreeCall(I, &TLI)) {
          if (Writers)
            Writers->insert(Call->getParent()->getParent());
        } else {
          return true; // Argument of an unknown call.
        }
      }
    } else if (ICmpInst *ICI = dyn_cast<ICmpInst>(I)) {
      if (!isa<ConstantPointerNull>(ICI->getOperand(1)))
        return true; // Allow comparison against null.
    } else if (Constant *C = dyn_cast<Constant>(I)) {
      // Ignore constants which don't have any live uses.
      if (isa<GlobalValue>(C) || C->isConstantUsed())
        return true;
    } else {
      return true;
    }
  }

  return false;
}

/// AnalyzeIndirectGlobalMemory - We found an non-address-taken global variable
/// which holds a pointer type.  See if the global always points to non-aliased
/// heap memory: that is, all initializers of the globals are allocations, and
/// those allocations have no use other than initialization of the global.
/// Further, all loads out of GV must directly use the memory, not store the
/// pointer somewhere.  If this is true, we consider the memory pointed to by
/// GV to be owned by GV and can disambiguate other pointers from it.
bool GlobalsAAResult::AnalyzeIndirectGlobalMemory(GlobalVariable *GV) {
  // Keep track of values related to the allocation of the memory, f.e. the
  // value produced by the malloc call and any casts.
  std::vector<Value *> AllocRelatedValues;

  // If the initializer is a valid pointer, bail.
  if (Constant *C = GV->getInitializer())
    if (!C->isNullValue())
      return false;

  // Walk the user list of the global.  If we find anything other than a direct
  // load or store, bail out.
  for (User *U : GV->users()) {
    if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
      // The pointer loaded from the global can only be used in simple ways:
      // we allow addressing of it and loading storing to it.  We do *not* allow
      // storing the loaded pointer somewhere else or passing to a function.
      if (AnalyzeUsesOfPointer(LI))
        return false; // Loaded pointer escapes.
      // TODO: Could try some IP mod/ref of the loaded pointer.
    } else if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
      // Storing the global itself.
      if (SI->getOperand(0) == GV)
        return false;

      // If storing the null pointer, ignore it.
      if (isa<ConstantPointerNull>(SI->getOperand(0)))
        continue;

      // Check the value being stored.
      Value *Ptr = GetUnderlyingObject(SI->getOperand(0),
                                       GV->getParent()->getDataLayout());

      if (!isAllocLikeFn(Ptr, &TLI))
        return false; // Too hard to analyze.

      // Analyze all uses of the allocation.  If any of them are used in a
      // non-simple way (e.g. stored to another global) bail out.
      if (AnalyzeUsesOfPointer(Ptr, /*Readers*/ nullptr, /*Writers*/ nullptr,
                               GV))
        return false; // Loaded pointer escapes.

      // Remember that this allocation is related to the indirect global.
      AllocRelatedValues.push_back(Ptr);
    } else {
      // Something complex, bail out.
      return false;
    }
  }

  // Okay, this is an indirect global.  Remember all of the allocations for
  // this global in AllocsForIndirectGlobals.
  while (!AllocRelatedValues.empty()) {
    AllocsForIndirectGlobals[AllocRelatedValues.back()] = GV;
    Handles.emplace_front(*this, AllocRelatedValues.back());
    Handles.front().I = Handles.begin();
    AllocRelatedValues.pop_back();
  }
  IndirectGlobals.insert(GV);
  Handles.emplace_front(*this, GV);
  Handles.front().I = Handles.begin();
  return true;
}

void GlobalsAAResult::CollectSCCMembership(CallGraph &CG) {
  // We do a bottom-up SCC traversal of the call graph.  In other words, we
  // visit all callees before callers (leaf-first).
  unsigned SCCID = 0;
  for (scc_iterator<CallGraph *> I = scc_begin(&CG); !I.isAtEnd(); ++I) {
    const std::vector<CallGraphNode *> &SCC = *I;
    assert(!SCC.empty() && "SCC with no functions?");

    for (auto *CGN : SCC)
      if (Function *F = CGN->getFunction())
        FunctionToSCCMap[F] = SCCID;
    ++SCCID;
  }
}

/// AnalyzeCallGraph - At this point, we know the functions where globals are
/// immediately stored to and read from.  Propagate this information up the call
/// graph to all callers and compute the mod/ref info for all memory for each
/// function.
void GlobalsAAResult::AnalyzeCallGraph(CallGraph &CG, Module &M) {
  // We do a bottom-up SCC traversal of the call graph.  In other words, we
  // visit all callees before callers (leaf-first).
  for (scc_iterator<CallGraph *> I = scc_begin(&CG); !I.isAtEnd(); ++I) {
    const std::vector<CallGraphNode *> &SCC = *I;
    assert(!SCC.empty() && "SCC with no functions?");

    Function *F = SCC[0]->getFunction();

    if (!F || !F->isDefinitionExact()) {
      // Calls externally or not exact - can't say anything useful. Remove any
      // existing function records (may have been created when scanning
      // globals).
      for (auto *Node : SCC)
        FunctionInfos.erase(Node->getFunction());
      continue;
    }

    FunctionInfo &FI = FunctionInfos[F];
    Handles.emplace_front(*this, F);
    Handles.front().I = Handles.begin();
    bool KnowNothing = false;

    // Collect the mod/ref properties due to called functions.  We only compute
    // one mod-ref set.
    for (unsigned i = 0, e = SCC.size(); i != e && !KnowNothing; ++i) {
      if (!F) {
        KnowNothing = true;
        break;
      }

      if (F->isDeclaration() || F->hasFnAttribute(Attribute::OptimizeNone)) {
        // Try to get mod/ref behaviour from function attributes.
        if (F->doesNotAccessMemory()) {
          // Can't do better than that!
        } else if (F->onlyReadsMemory()) {
          FI.addModRefInfo(ModRefInfo::Ref);
          if (!F->isIntrinsic() && !F->onlyAccessesArgMemory())
            // This function might call back into the module and read a global -
            // consider every global as possibly being read by this function.
            FI.setMayReadAnyGlobal();
        } else {
          FI.addModRefInfo(ModRefInfo::ModRef);
          // Can't say anything useful unless it's an intrinsic - they don't
          // read or write global variables of the kind considered here.
          KnowNothing = !F->isIntrinsic();
        }
        continue;
      }

      for (CallGraphNode::iterator CI = SCC[i]->begin(), E = SCC[i]->end();
           CI != E && !KnowNothing; ++CI)
        if (Function *Callee = CI->second->getFunction()) {
          if (FunctionInfo *CalleeFI = getFunctionInfo(Callee)) {
            // Propagate function effect up.
            FI.addFunctionInfo(*CalleeFI);
          } else {
            // Can't say anything about it.  However, if it is inside our SCC,
            // then nothing needs to be done.
            CallGraphNode *CalleeNode = CG[Callee];
            if (!is_contained(SCC, CalleeNode))
              KnowNothing = true;
          }
        } else {
          KnowNothing = true;
        }
    }

    // If we can't say anything useful about this SCC, remove all SCC functions
    // from the FunctionInfos map.
    if (KnowNothing) {
      for (auto *Node : SCC)
        FunctionInfos.erase(Node->getFunction());
      continue;
    }

    // Scan the function bodies for explicit loads or stores.
    for (auto *Node : SCC) {
      if (isModAndRefSet(FI.getModRefInfo()))
        break; // The mod/ref lattice saturates here.

      // Don't prove any properties based on the implementation of an optnone
      // function. Function attributes were already used as a best approximation
      // above.
      if (Node->getFunction()->hasFnAttribute(Attribute::OptimizeNone))
        continue;

      for (Instruction &I : instructions(Node->getFunction())) {
        if (isModAndRefSet(FI.getModRefInfo()))
          break; // The mod/ref lattice saturates here.

        // We handle calls specially because the graph-relevant aspects are
        // handled above.
        if (auto *Call = dyn_cast<CallBase>(&I)) {
          if (isAllocationFn(Call, &TLI) || isFreeCall(Call, &TLI)) {
            // FIXME: It is completely unclear why this is necessary and not
            // handled by the above graph code.
            FI.addModRefInfo(ModRefInfo::ModRef);
          } else if (Function *Callee = Call->getCalledFunction()) {
            // The callgraph doesn't include intrinsic calls.
            if (Callee->isIntrinsic()) {
              if (isa<DbgInfoIntrinsic>(Call))
                // Don't let dbg intrinsics affect alias info.
                continue;

              FunctionModRefBehavior Behaviour =
                  AAResultBase::getModRefBehavior(Callee);
              FI.addModRefInfo(createModRefInfo(Behaviour));
            }
          }
          continue;
        }

        // All non-call instructions we use the primary predicates for whether
        // thay read or write memory.
        if (I.mayReadFromMemory())
          FI.addModRefInfo(ModRefInfo::Ref);
        if (I.mayWriteToMemory())
          FI.addModRefInfo(ModRefInfo::Mod);
      }
    }

    if (!isModSet(FI.getModRefInfo()))
      ++NumReadMemFunctions;
    if (!isModOrRefSet(FI.getModRefInfo()))
      ++NumNoMemFunctions;

    // Finally, now that we know the full effect on this SCC, clone the
    // information to each function in the SCC.
    // FI is a reference into FunctionInfos, so copy it now so that it doesn't
    // get invalidated if DenseMap decides to re-hash.
    FunctionInfo CachedFI = FI;
    for (unsigned i = 1, e = SCC.size(); i != e; ++i)
      FunctionInfos[SCC[i]->getFunction()] = CachedFI;
  }
}

// GV is a non-escaping global. V is a pointer address that has been loaded from.
// If we can prove that V must escape, we can conclude that a load from V cannot
// alias GV.
static bool isNonEscapingGlobalNoAliasWithLoad(const GlobalValue *GV,
                                               const Value *V,
                                               int &Depth,
                                               const DataLayout &DL) {
  SmallPtrSet<const Value *, 8> Visited;
  SmallVector<const Value *, 8> Inputs;
  Visited.insert(V);
  Inputs.push_back(V);
  do {
    const Value *Input = Inputs.pop_back_val();

    if (isa<GlobalValue>(Input) || isa<Argument>(Input) || isa<CallInst>(Input) ||
        isa<InvokeInst>(Input))
      // Arguments to functions or returns from functions are inherently
      // escaping, so we can immediately classify those as not aliasing any
      // non-addr-taken globals.
      //
      // (Transitive) loads from a global are also safe - if this aliased
      // another global, its address would escape, so no alias.
      continue;

    // Recurse through a limited number of selects, loads and PHIs. This is an
    // arbitrary depth of 4, lower numbers could be used to fix compile time
    // issues if needed, but this is generally expected to be only be important
    // for small depths.
    if (++Depth > 4)
      return false;

    if (auto *LI = dyn_cast<LoadInst>(Input)) {
      Inputs.push_back(GetUnderlyingObject(LI->getPointerOperand(), DL));
      continue;
    }
    if (auto *SI = dyn_cast<SelectInst>(Input)) {
      const Value *LHS = GetUnderlyingObject(SI->getTrueValue(), DL);
      const Value *RHS = GetUnderlyingObject(SI->getFalseValue(), DL);
      if (Visited.insert(LHS).second)
        Inputs.push_back(LHS);
      if (Visited.insert(RHS).second)
        Inputs.push_back(RHS);
      continue;
    }
    if (auto *PN = dyn_cast<PHINode>(Input)) {
      for (const Value *Op : PN->incoming_values()) {
        Op = GetUnderlyingObject(Op, DL);
        if (Visited.insert(Op).second)
          Inputs.push_back(Op);
      }
      continue;
    }

    return false;
  } while (!Inputs.empty());

  // All inputs were known to be no-alias.
  return true;
}

// There are particular cases where we can conclude no-alias between
// a non-addr-taken global and some other underlying object. Specifically,
// a non-addr-taken global is known to not be escaped from any function. It is
// also incorrect for a transformation to introduce an escape of a global in
// a way that is observable when it was not there previously. One function
// being transformed to introduce an escape which could possibly be observed
// (via loading from a global or the return value for example) within another
// function is never safe. If the observation is made through non-atomic
// operations on different threads, it is a data-race and UB. If the
// observation is well defined, by being observed the transformation would have
// changed program behavior by introducing the observed escape, making it an
// invalid transform.
//
// This property does require that transformations which *temporarily* escape
// a global that was not previously escaped, prior to restoring it, cannot rely
// on the results of GMR::alias. This seems a reasonable restriction, although
// currently there is no way to enforce it. There is also no realistic
// optimization pass that would make this mistake. The closest example is
// a transformation pass which does reg2mem of SSA values but stores them into
// global variables temporarily before restoring the global variable's value.
// This could be useful to expose "benign" races for example. However, it seems
// reasonable to require that a pass which introduces escapes of global
// variables in this way to either not trust AA results while the escape is
// active, or to be forced to operate as a module pass that cannot co-exist
// with an alias analysis such as GMR.
bool GlobalsAAResult::isNonEscapingGlobalNoAlias(const GlobalValue *GV,
                                                 const Value *V) {
  // In order to know that the underlying object cannot alias the
  // non-addr-taken global, we must know that it would have to be an escape.
  // Thus if the underlying object is a function argument, a load from
  // a global, or the return of a function, it cannot alias. We can also
  // recurse through PHI nodes and select nodes provided all of their inputs
  // resolve to one of these known-escaping roots.
  SmallPtrSet<const Value *, 8> Visited;
  SmallVector<const Value *, 8> Inputs;
  Visited.insert(V);
  Inputs.push_back(V);
  int Depth = 0;
  do {
    const Value *Input = Inputs.pop_back_val();

    if (auto *InputGV = dyn_cast<GlobalValue>(Input)) {
      // If one input is the very global we're querying against, then we can't
      // conclude no-alias.
      if (InputGV == GV)
        return false;

      // Distinct GlobalVariables never alias, unless overriden or zero-sized.
      // FIXME: The condition can be refined, but be conservative for now.
      auto *GVar = dyn_cast<GlobalVariable>(GV);
      auto *InputGVar = dyn_cast<GlobalVariable>(InputGV);
      if (GVar && InputGVar &&
          !GVar->isDeclaration() && !InputGVar->isDeclaration() &&
          !GVar->isInterposable() && !InputGVar->isInterposable()) {
        Type *GVType = GVar->getInitializer()->getType();
        Type *InputGVType = InputGVar->getInitializer()->getType();
        if (GVType->isSized() && InputGVType->isSized() &&
            (DL.getTypeAllocSize(GVType) > 0) &&
            (DL.getTypeAllocSize(InputGVType) > 0))
          continue;
      }

      // Conservatively return false, even though we could be smarter
      // (e.g. look through GlobalAliases).
      return false;
    }

    if (isa<Argument>(Input) || isa<CallInst>(Input) ||
        isa<InvokeInst>(Input)) {
      // Arguments to functions or returns from functions are inherently
      // escaping, so we can immediately classify those as not aliasing any
      // non-addr-taken globals.
      continue;
    }

    // Recurse through a limited number of selects, loads and PHIs. This is an
    // arbitrary depth of 4, lower numbers could be used to fix compile time
    // issues if needed, but this is generally expected to be only be important
    // for small depths.
    if (++Depth > 4)
      return false;

    if (auto *LI = dyn_cast<LoadInst>(Input)) {
      // A pointer loaded from a global would have been captured, and we know
      // that the global is non-escaping, so no alias.
      const Value *Ptr = GetUnderlyingObject(LI->getPointerOperand(), DL);
      if (isNonEscapingGlobalNoAliasWithLoad(GV, Ptr, Depth, DL))
        // The load does not alias with GV.
        continue;
      // Otherwise, a load could come from anywhere, so bail.
      return false;
    }
    if (auto *SI = dyn_cast<SelectInst>(Input)) {
      const Value *LHS = GetUnderlyingObject(SI->getTrueValue(), DL);
      const Value *RHS = GetUnderlyingObject(SI->getFalseValue(), DL);
      if (Visited.insert(LHS).second)
        Inputs.push_back(LHS);
      if (Visited.insert(RHS).second)
        Inputs.push_back(RHS);
      continue;
    }
    if (auto *PN = dyn_cast<PHINode>(Input)) {
      for (const Value *Op : PN->incoming_values()) {
        Op = GetUnderlyingObject(Op, DL);
        if (Visited.insert(Op).second)
          Inputs.push_back(Op);
      }
      continue;
    }

    // FIXME: It would be good to handle other obvious no-alias cases here, but
    // it isn't clear how to do so reasonbly without building a small version
    // of BasicAA into this code. We could recurse into AAResultBase::alias
    // here but that seems likely to go poorly as we're inside the
    // implementation of such a query. Until then, just conservatievly retun
    // false.
    return false;
  } while (!Inputs.empty());

  // If all the inputs to V were definitively no-alias, then V is no-alias.
  return true;
}

/// alias - If one of the pointers is to a global that we are tracking, and the
/// other is some random pointer, we know there cannot be an alias, because the
/// address of the global isn't taken.
AliasResult GlobalsAAResult::alias(const MemoryLocation &LocA,
                                   const MemoryLocation &LocB) {
  // Get the base object these pointers point to.
  const Value *UV1 = GetUnderlyingObject(LocA.Ptr, DL);
  const Value *UV2 = GetUnderlyingObject(LocB.Ptr, DL);

  // If either of the underlying values is a global, they may be non-addr-taken
  // globals, which we can answer queries about.
  const GlobalValue *GV1 = dyn_cast<GlobalValue>(UV1);
  const GlobalValue *GV2 = dyn_cast<GlobalValue>(UV2);
  if (GV1 || GV2) {
    // If the global's address is taken, pretend we don't know it's a pointer to
    // the global.
    if (GV1 && !NonAddressTakenGlobals.count(GV1))
      GV1 = nullptr;
    if (GV2 && !NonAddressTakenGlobals.count(GV2))
      GV2 = nullptr;

    // If the two pointers are derived from two different non-addr-taken
    // globals we know these can't alias.
    if (GV1 && GV2 && GV1 != GV2)
      return NoAlias;

    // If one is and the other isn't, it isn't strictly safe but we can fake
    // this result if necessary for performance. This does not appear to be
    // a common problem in practice.
    if (EnableUnsafeGlobalsModRefAliasResults)
      if ((GV1 || GV2) && GV1 != GV2)
        return NoAlias;

    // Check for a special case where a non-escaping global can be used to
    // conclude no-alias.
    if ((GV1 || GV2) && GV1 != GV2) {
      const GlobalValue *GV = GV1 ? GV1 : GV2;
      const Value *UV = GV1 ? UV2 : UV1;
      if (isNonEscapingGlobalNoAlias(GV, UV))
        return NoAlias;
    }

    // Otherwise if they are both derived from the same addr-taken global, we
    // can't know the two accesses don't overlap.
  }

  // These pointers may be based on the memory owned by an indirect global.  If
  // so, we may be able to handle this.  First check to see if the base pointer
  // is a direct load from an indirect global.
  GV1 = GV2 = nullptr;
  if (const LoadInst *LI = dyn_cast<LoadInst>(UV1))
    if (GlobalVariable *GV = dyn_cast<GlobalVariable>(LI->getOperand(0)))
      if (IndirectGlobals.count(GV))
        GV1 = GV;
  if (const LoadInst *LI = dyn_cast<LoadInst>(UV2))
    if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(LI->getOperand(0)))
      if (IndirectGlobals.count(GV))
        GV2 = GV;

  // These pointers may also be from an allocation for the indirect global.  If
  // so, also handle them.
  if (!GV1)
    GV1 = AllocsForIndirectGlobals.lookup(UV1);
  if (!GV2)
    GV2 = AllocsForIndirectGlobals.lookup(UV2);

  // Now that we know whether the two pointers are related to indirect globals,
  // use this to disambiguate the pointers. If the pointers are based on
  // different indirect globals they cannot alias.
  if (GV1 && GV2 && GV1 != GV2)
    return NoAlias;

  // If one is based on an indirect global and the other isn't, it isn't
  // strictly safe but we can fake this result if necessary for performance.
  // This does not appear to be a common problem in practice.
  if (EnableUnsafeGlobalsModRefAliasResults)
    if ((GV1 || GV2) && GV1 != GV2)
      return NoAlias;

  return AAResultBase::alias(LocA, LocB);
}

ModRefInfo GlobalsAAResult::getModRefInfoForArgument(const CallBase *Call,
                                                     const GlobalValue *GV) {
  if (Call->doesNotAccessMemory())
    return ModRefInfo::NoModRef;
  ModRefInfo ConservativeResult =
      Call->onlyReadsMemory() ? ModRefInfo::Ref : ModRefInfo::ModRef;

  // Iterate through all the arguments to the called function. If any argument
  // is based on GV, return the conservative result.
  for (auto &A : Call->args()) {
    SmallVector<Value*, 4> Objects;
    GetUnderlyingObjects(A, Objects, DL);

    // All objects must be identified.
    if (!all_of(Objects, isIdentifiedObject) &&
        // Try ::alias to see if all objects are known not to alias GV.
        !all_of(Objects, [&](Value *V) {
          return this->alias(MemoryLocation(V), MemoryLocation(GV)) == NoAlias;
        }))
      return ConservativeResult;

    if (is_contained(Objects, GV))
      return ConservativeResult;
  }

  // We identified all objects in the argument list, and none of them were GV.
  return ModRefInfo::NoModRef;
}

ModRefInfo GlobalsAAResult::getModRefInfo(const CallBase *Call,
                                          const MemoryLocation &Loc) {
  ModRefInfo Known = ModRefInfo::ModRef;

  // If we are asking for mod/ref info of a direct call with a pointer to a
  // global we are tracking, return information if we have it.
  if (const GlobalValue *GV =
          dyn_cast<GlobalValue>(GetUnderlyingObject(Loc.Ptr, DL)))
    if (GV->hasLocalLinkage())
      if (const Function *F = Call->getCalledFunction())
        if (NonAddressTakenGlobals.count(GV))
          if (const FunctionInfo *FI = getFunctionInfo(F))
            Known = unionModRef(FI->getModRefInfoForGlobal(*GV),
                                getModRefInfoForArgument(Call, GV));

  if (!isModOrRefSet(Known))
    return ModRefInfo::NoModRef; // No need to query other mod/ref analyses
  return intersectModRef(Known, AAResultBase::getModRefInfo(Call, Loc));
}

GlobalsAAResult::GlobalsAAResult(const DataLayout &DL,
                                 const TargetLibraryInfo &TLI)
    : AAResultBase(), DL(DL), TLI(TLI) {}

GlobalsAAResult::GlobalsAAResult(GlobalsAAResult &&Arg)
    : AAResultBase(std::move(Arg)), DL(Arg.DL), TLI(Arg.TLI),
      NonAddressTakenGlobals(std::move(Arg.NonAddressTakenGlobals)),
      IndirectGlobals(std::move(Arg.IndirectGlobals)),
      AllocsForIndirectGlobals(std::move(Arg.AllocsForIndirectGlobals)),
      FunctionInfos(std::move(Arg.FunctionInfos)),
      Handles(std::move(Arg.Handles)) {
  // Update the parent for each DeletionCallbackHandle.
  for (auto &H : Handles) {
    assert(H.GAR == &Arg);
    H.GAR = this;
  }
}

GlobalsAAResult::~GlobalsAAResult() {}

/*static*/ GlobalsAAResult
GlobalsAAResult::analyzeModule(Module &M, const TargetLibraryInfo &TLI,
                               CallGraph &CG) {
  GlobalsAAResult Result(M.getDataLayout(), TLI);

  // Discover which functions aren't recursive, to feed into AnalyzeGlobals.
  Result.CollectSCCMembership(CG);

  // Find non-addr taken globals.
  Result.AnalyzeGlobals(M);

  // Propagate on CG.
  Result.AnalyzeCallGraph(CG, M);

  return Result;
}

AnalysisKey GlobalsAA::Key;

GlobalsAAResult GlobalsAA::run(Module &M, ModuleAnalysisManager &AM) {
  return GlobalsAAResult::analyzeModule(M,
                                        AM.getResult<TargetLibraryAnalysis>(M),
                                        AM.getResult<CallGraphAnalysis>(M));
}

char GlobalsAAWrapperPass::ID = 0;
INITIALIZE_PASS_BEGIN(GlobalsAAWrapperPass, "globals-aa",
                      "Globals Alias Analysis", false, true)
INITIALIZE_PASS_DEPENDENCY(CallGraphWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(GlobalsAAWrapperPass, "globals-aa",
                    "Globals Alias Analysis", false, true)

ModulePass *llvm::createGlobalsAAWrapperPass() {
  return new GlobalsAAWrapperPass();
}

GlobalsAAWrapperPass::GlobalsAAWrapperPass() : ModulePass(ID) {
  initializeGlobalsAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

bool GlobalsAAWrapperPass::runOnModule(Module &M) {
  Result.reset(new GlobalsAAResult(GlobalsAAResult::analyzeModule(
      M, getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(),
      getAnalysis<CallGraphWrapperPass>().getCallGraph())));
  return false;
}

bool GlobalsAAWrapperPass::doFinalization(Module &M) {
  Result.reset();
  return false;
}

void GlobalsAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<CallGraphWrapperPass>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}
