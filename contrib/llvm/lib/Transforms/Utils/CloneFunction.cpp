//===- CloneFunction.cpp - Clone a function into another function ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the CloneFunctionInto interface, which is used as the
// low-level function cloner.  This is used by the CloneFunction and function
// inliner to do the dirty work of copying the body of a function around.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DomTreeUpdater.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <map>
using namespace llvm;

/// See comments in Cloning.h.
BasicBlock *llvm::CloneBasicBlock(const BasicBlock *BB, ValueToValueMapTy &VMap,
                                  const Twine &NameSuffix, Function *F,
                                  ClonedCodeInfo *CodeInfo,
                                  DebugInfoFinder *DIFinder) {
  DenseMap<const MDNode *, MDNode *> Cache;
  BasicBlock *NewBB = BasicBlock::Create(BB->getContext(), "", F);
  if (BB->hasName())
    NewBB->setName(BB->getName() + NameSuffix);

  bool hasCalls = false, hasDynamicAllocas = false, hasStaticAllocas = false;
  Module *TheModule = F ? F->getParent() : nullptr;

  // Loop over all instructions, and copy them over.
  for (const Instruction &I : *BB) {
    if (DIFinder && TheModule)
      DIFinder->processInstruction(*TheModule, I);

    Instruction *NewInst = I.clone();
    if (I.hasName())
      NewInst->setName(I.getName() + NameSuffix);
    NewBB->getInstList().push_back(NewInst);
    VMap[&I] = NewInst; // Add instruction map to value.

    hasCalls |= (isa<CallInst>(I) && !isa<DbgInfoIntrinsic>(I));
    if (const AllocaInst *AI = dyn_cast<AllocaInst>(&I)) {
      if (isa<ConstantInt>(AI->getArraySize()))
        hasStaticAllocas = true;
      else
        hasDynamicAllocas = true;
    }
  }

  if (CodeInfo) {
    CodeInfo->ContainsCalls          |= hasCalls;
    CodeInfo->ContainsDynamicAllocas |= hasDynamicAllocas;
    CodeInfo->ContainsDynamicAllocas |= hasStaticAllocas &&
                                        BB != &BB->getParent()->getEntryBlock();
  }
  return NewBB;
}

// Clone OldFunc into NewFunc, transforming the old arguments into references to
// VMap values.
//
void llvm::CloneFunctionInto(Function *NewFunc, const Function *OldFunc,
                             ValueToValueMapTy &VMap,
                             bool ModuleLevelChanges,
                             SmallVectorImpl<ReturnInst*> &Returns,
                             const char *NameSuffix, ClonedCodeInfo *CodeInfo,
                             ValueMapTypeRemapper *TypeMapper,
                             ValueMaterializer *Materializer) {
  assert(NameSuffix && "NameSuffix cannot be null!");

#ifndef NDEBUG
  for (const Argument &I : OldFunc->args())
    assert(VMap.count(&I) && "No mapping from source argument specified!");
#endif

  // Copy all attributes other than those stored in the AttributeList.  We need
  // to remap the parameter indices of the AttributeList.
  AttributeList NewAttrs = NewFunc->getAttributes();
  NewFunc->copyAttributesFrom(OldFunc);
  NewFunc->setAttributes(NewAttrs);

  // Fix up the personality function that got copied over.
  if (OldFunc->hasPersonalityFn())
    NewFunc->setPersonalityFn(
        MapValue(OldFunc->getPersonalityFn(), VMap,
                 ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges,
                 TypeMapper, Materializer));

  SmallVector<AttributeSet, 4> NewArgAttrs(NewFunc->arg_size());
  AttributeList OldAttrs = OldFunc->getAttributes();

  // Clone any argument attributes that are present in the VMap.
  for (const Argument &OldArg : OldFunc->args()) {
    if (Argument *NewArg = dyn_cast<Argument>(VMap[&OldArg])) {
      NewArgAttrs[NewArg->getArgNo()] =
          OldAttrs.getParamAttributes(OldArg.getArgNo());
    }
  }

  NewFunc->setAttributes(
      AttributeList::get(NewFunc->getContext(), OldAttrs.getFnAttributes(),
                         OldAttrs.getRetAttributes(), NewArgAttrs));

  bool MustCloneSP =
      OldFunc->getParent() && OldFunc->getParent() == NewFunc->getParent();
  DISubprogram *SP = OldFunc->getSubprogram();
  if (SP) {
    assert(!MustCloneSP || ModuleLevelChanges);
    // Add mappings for some DebugInfo nodes that we don't want duplicated
    // even if they're distinct.
    auto &MD = VMap.MD();
    MD[SP->getUnit()].reset(SP->getUnit());
    MD[SP->getType()].reset(SP->getType());
    MD[SP->getFile()].reset(SP->getFile());
    // If we're not cloning into the same module, no need to clone the
    // subprogram
    if (!MustCloneSP)
      MD[SP].reset(SP);
  }

  SmallVector<std::pair<unsigned, MDNode *>, 1> MDs;
  OldFunc->getAllMetadata(MDs);
  for (auto MD : MDs) {
    NewFunc->addMetadata(
        MD.first,
        *MapMetadata(MD.second, VMap,
                     ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges,
                     TypeMapper, Materializer));
  }

  // When we remap instructions, we want to avoid duplicating inlined
  // DISubprograms, so record all subprograms we find as we duplicate
  // instructions and then freeze them in the MD map.
  // We also record information about dbg.value and dbg.declare to avoid
  // duplicating the types.
  DebugInfoFinder DIFinder;

  // Loop over all of the basic blocks in the function, cloning them as
  // appropriate.  Note that we save BE this way in order to handle cloning of
  // recursive functions into themselves.
  //
  for (Function::const_iterator BI = OldFunc->begin(), BE = OldFunc->end();
       BI != BE; ++BI) {
    const BasicBlock &BB = *BI;

    // Create a new basic block and copy instructions into it!
    BasicBlock *CBB = CloneBasicBlock(&BB, VMap, NameSuffix, NewFunc, CodeInfo,
                                      ModuleLevelChanges ? &DIFinder : nullptr);

    // Add basic block mapping.
    VMap[&BB] = CBB;

    // It is only legal to clone a function if a block address within that
    // function is never referenced outside of the function.  Given that, we
    // want to map block addresses from the old function to block addresses in
    // the clone. (This is different from the generic ValueMapper
    // implementation, which generates an invalid blockaddress when
    // cloning a function.)
    if (BB.hasAddressTaken()) {
      Constant *OldBBAddr = BlockAddress::get(const_cast<Function*>(OldFunc),
                                              const_cast<BasicBlock*>(&BB));
      VMap[OldBBAddr] = BlockAddress::get(NewFunc, CBB);
    }

    // Note return instructions for the caller.
    if (ReturnInst *RI = dyn_cast<ReturnInst>(CBB->getTerminator()))
      Returns.push_back(RI);
  }

  for (DISubprogram *ISP : DIFinder.subprograms())
    if (ISP != SP)
      VMap.MD()[ISP].reset(ISP);

  for (DICompileUnit *CU : DIFinder.compile_units())
    VMap.MD()[CU].reset(CU);

  for (DIType *Type : DIFinder.types())
    VMap.MD()[Type].reset(Type);

  // Loop over all of the instructions in the function, fixing up operand
  // references as we go.  This uses VMap to do all the hard work.
  for (Function::iterator BB =
           cast<BasicBlock>(VMap[&OldFunc->front()])->getIterator(),
                          BE = NewFunc->end();
       BB != BE; ++BB)
    // Loop over all instructions, fixing each one as we find it...
    for (Instruction &II : *BB)
      RemapInstruction(&II, VMap,
                       ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges,
                       TypeMapper, Materializer);
}

/// Return a copy of the specified function and add it to that function's
/// module.  Also, any references specified in the VMap are changed to refer to
/// their mapped value instead of the original one.  If any of the arguments to
/// the function are in the VMap, the arguments are deleted from the resultant
/// function.  The VMap is updated to include mappings from all of the
/// instructions and basicblocks in the function from their old to new values.
///
Function *llvm::CloneFunction(Function *F, ValueToValueMapTy &VMap,
                              ClonedCodeInfo *CodeInfo) {
  std::vector<Type*> ArgTypes;

  // The user might be deleting arguments to the function by specifying them in
  // the VMap.  If so, we need to not add the arguments to the arg ty vector
  //
  for (const Argument &I : F->args())
    if (VMap.count(&I) == 0) // Haven't mapped the argument to anything yet?
      ArgTypes.push_back(I.getType());

  // Create a new function type...
  FunctionType *FTy = FunctionType::get(F->getFunctionType()->getReturnType(),
                                    ArgTypes, F->getFunctionType()->isVarArg());

  // Create the new function...
  Function *NewF = Function::Create(FTy, F->getLinkage(), F->getAddressSpace(),
                                    F->getName(), F->getParent());

  // Loop over the arguments, copying the names of the mapped arguments over...
  Function::arg_iterator DestI = NewF->arg_begin();
  for (const Argument & I : F->args())
    if (VMap.count(&I) == 0) {     // Is this argument preserved?
      DestI->setName(I.getName()); // Copy the name over...
      VMap[&I] = &*DestI++;        // Add mapping to VMap
    }

  SmallVector<ReturnInst*, 8> Returns;  // Ignore returns cloned.
  CloneFunctionInto(NewF, F, VMap, F->getSubprogram() != nullptr, Returns, "",
                    CodeInfo);

  return NewF;
}



namespace {
  /// This is a private class used to implement CloneAndPruneFunctionInto.
  struct PruningFunctionCloner {
    Function *NewFunc;
    const Function *OldFunc;
    ValueToValueMapTy &VMap;
    bool ModuleLevelChanges;
    const char *NameSuffix;
    ClonedCodeInfo *CodeInfo;

  public:
    PruningFunctionCloner(Function *newFunc, const Function *oldFunc,
                          ValueToValueMapTy &valueMap, bool moduleLevelChanges,
                          const char *nameSuffix, ClonedCodeInfo *codeInfo)
        : NewFunc(newFunc), OldFunc(oldFunc), VMap(valueMap),
          ModuleLevelChanges(moduleLevelChanges), NameSuffix(nameSuffix),
          CodeInfo(codeInfo) {}

    /// The specified block is found to be reachable, clone it and
    /// anything that it can reach.
    void CloneBlock(const BasicBlock *BB,
                    BasicBlock::const_iterator StartingInst,
                    std::vector<const BasicBlock*> &ToClone);
  };
}

/// The specified block is found to be reachable, clone it and
/// anything that it can reach.
void PruningFunctionCloner::CloneBlock(const BasicBlock *BB,
                                       BasicBlock::const_iterator StartingInst,
                                       std::vector<const BasicBlock*> &ToClone){
  WeakTrackingVH &BBEntry = VMap[BB];

  // Have we already cloned this block?
  if (BBEntry) return;

  // Nope, clone it now.
  BasicBlock *NewBB;
  BBEntry = NewBB = BasicBlock::Create(BB->getContext());
  if (BB->hasName()) NewBB->setName(BB->getName()+NameSuffix);

  // It is only legal to clone a function if a block address within that
  // function is never referenced outside of the function.  Given that, we
  // want to map block addresses from the old function to block addresses in
  // the clone. (This is different from the generic ValueMapper
  // implementation, which generates an invalid blockaddress when
  // cloning a function.)
  //
  // Note that we don't need to fix the mapping for unreachable blocks;
  // the default mapping there is safe.
  if (BB->hasAddressTaken()) {
    Constant *OldBBAddr = BlockAddress::get(const_cast<Function*>(OldFunc),
                                            const_cast<BasicBlock*>(BB));
    VMap[OldBBAddr] = BlockAddress::get(NewFunc, NewBB);
  }

  bool hasCalls = false, hasDynamicAllocas = false, hasStaticAllocas = false;

  // Loop over all instructions, and copy them over, DCE'ing as we go.  This
  // loop doesn't include the terminator.
  for (BasicBlock::const_iterator II = StartingInst, IE = --BB->end();
       II != IE; ++II) {

    Instruction *NewInst = II->clone();

    // Eagerly remap operands to the newly cloned instruction, except for PHI
    // nodes for which we defer processing until we update the CFG.
    if (!isa<PHINode>(NewInst)) {
      RemapInstruction(NewInst, VMap,
                       ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges);

      // If we can simplify this instruction to some other value, simply add
      // a mapping to that value rather than inserting a new instruction into
      // the basic block.
      if (Value *V =
              SimplifyInstruction(NewInst, BB->getModule()->getDataLayout())) {
        // On the off-chance that this simplifies to an instruction in the old
        // function, map it back into the new function.
        if (NewFunc != OldFunc)
          if (Value *MappedV = VMap.lookup(V))
            V = MappedV;

        if (!NewInst->mayHaveSideEffects()) {
          VMap[&*II] = V;
          NewInst->deleteValue();
          continue;
        }
      }
    }

    if (II->hasName())
      NewInst->setName(II->getName()+NameSuffix);
    VMap[&*II] = NewInst; // Add instruction map to value.
    NewBB->getInstList().push_back(NewInst);
    hasCalls |= (isa<CallInst>(II) && !isa<DbgInfoIntrinsic>(II));

    if (CodeInfo)
      if (auto CS = ImmutableCallSite(&*II))
        if (CS.hasOperandBundles())
          CodeInfo->OperandBundleCallSites.push_back(NewInst);

    if (const AllocaInst *AI = dyn_cast<AllocaInst>(II)) {
      if (isa<ConstantInt>(AI->getArraySize()))
        hasStaticAllocas = true;
      else
        hasDynamicAllocas = true;
    }
  }

  // Finally, clone over the terminator.
  const Instruction *OldTI = BB->getTerminator();
  bool TerminatorDone = false;
  if (const BranchInst *BI = dyn_cast<BranchInst>(OldTI)) {
    if (BI->isConditional()) {
      // If the condition was a known constant in the callee...
      ConstantInt *Cond = dyn_cast<ConstantInt>(BI->getCondition());
      // Or is a known constant in the caller...
      if (!Cond) {
        Value *V = VMap.lookup(BI->getCondition());
        Cond = dyn_cast_or_null<ConstantInt>(V);
      }

      // Constant fold to uncond branch!
      if (Cond) {
        BasicBlock *Dest = BI->getSuccessor(!Cond->getZExtValue());
        VMap[OldTI] = BranchInst::Create(Dest, NewBB);
        ToClone.push_back(Dest);
        TerminatorDone = true;
      }
    }
  } else if (const SwitchInst *SI = dyn_cast<SwitchInst>(OldTI)) {
    // If switching on a value known constant in the caller.
    ConstantInt *Cond = dyn_cast<ConstantInt>(SI->getCondition());
    if (!Cond) { // Or known constant after constant prop in the callee...
      Value *V = VMap.lookup(SI->getCondition());
      Cond = dyn_cast_or_null<ConstantInt>(V);
    }
    if (Cond) {     // Constant fold to uncond branch!
      SwitchInst::ConstCaseHandle Case = *SI->findCaseValue(Cond);
      BasicBlock *Dest = const_cast<BasicBlock*>(Case.getCaseSuccessor());
      VMap[OldTI] = BranchInst::Create(Dest, NewBB);
      ToClone.push_back(Dest);
      TerminatorDone = true;
    }
  }

  if (!TerminatorDone) {
    Instruction *NewInst = OldTI->clone();
    if (OldTI->hasName())
      NewInst->setName(OldTI->getName()+NameSuffix);
    NewBB->getInstList().push_back(NewInst);
    VMap[OldTI] = NewInst;             // Add instruction map to value.

    if (CodeInfo)
      if (auto CS = ImmutableCallSite(OldTI))
        if (CS.hasOperandBundles())
          CodeInfo->OperandBundleCallSites.push_back(NewInst);

    // Recursively clone any reachable successor blocks.
    const Instruction *TI = BB->getTerminator();
    for (const BasicBlock *Succ : successors(TI))
      ToClone.push_back(Succ);
  }

  if (CodeInfo) {
    CodeInfo->ContainsCalls          |= hasCalls;
    CodeInfo->ContainsDynamicAllocas |= hasDynamicAllocas;
    CodeInfo->ContainsDynamicAllocas |= hasStaticAllocas &&
      BB != &BB->getParent()->front();
  }
}

/// This works like CloneAndPruneFunctionInto, except that it does not clone the
/// entire function. Instead it starts at an instruction provided by the caller
/// and copies (and prunes) only the code reachable from that instruction.
void llvm::CloneAndPruneIntoFromInst(Function *NewFunc, const Function *OldFunc,
                                     const Instruction *StartingInst,
                                     ValueToValueMapTy &VMap,
                                     bool ModuleLevelChanges,
                                     SmallVectorImpl<ReturnInst *> &Returns,
                                     const char *NameSuffix,
                                     ClonedCodeInfo *CodeInfo) {
  assert(NameSuffix && "NameSuffix cannot be null!");

  ValueMapTypeRemapper *TypeMapper = nullptr;
  ValueMaterializer *Materializer = nullptr;

#ifndef NDEBUG
  // If the cloning starts at the beginning of the function, verify that
  // the function arguments are mapped.
  if (!StartingInst)
    for (const Argument &II : OldFunc->args())
      assert(VMap.count(&II) && "No mapping from source argument specified!");
#endif

  PruningFunctionCloner PFC(NewFunc, OldFunc, VMap, ModuleLevelChanges,
                            NameSuffix, CodeInfo);
  const BasicBlock *StartingBB;
  if (StartingInst)
    StartingBB = StartingInst->getParent();
  else {
    StartingBB = &OldFunc->getEntryBlock();
    StartingInst = &StartingBB->front();
  }

  // Clone the entry block, and anything recursively reachable from it.
  std::vector<const BasicBlock*> CloneWorklist;
  PFC.CloneBlock(StartingBB, StartingInst->getIterator(), CloneWorklist);
  while (!CloneWorklist.empty()) {
    const BasicBlock *BB = CloneWorklist.back();
    CloneWorklist.pop_back();
    PFC.CloneBlock(BB, BB->begin(), CloneWorklist);
  }

  // Loop over all of the basic blocks in the old function.  If the block was
  // reachable, we have cloned it and the old block is now in the value map:
  // insert it into the new function in the right order.  If not, ignore it.
  //
  // Defer PHI resolution until rest of function is resolved.
  SmallVector<const PHINode*, 16> PHIToResolve;
  for (const BasicBlock &BI : *OldFunc) {
    Value *V = VMap.lookup(&BI);
    BasicBlock *NewBB = cast_or_null<BasicBlock>(V);
    if (!NewBB) continue;  // Dead block.

    // Add the new block to the new function.
    NewFunc->getBasicBlockList().push_back(NewBB);

    // Handle PHI nodes specially, as we have to remove references to dead
    // blocks.
    for (const PHINode &PN : BI.phis()) {
      // PHI nodes may have been remapped to non-PHI nodes by the caller or
      // during the cloning process.
      if (isa<PHINode>(VMap[&PN]))
        PHIToResolve.push_back(&PN);
      else
        break;
    }

    // Finally, remap the terminator instructions, as those can't be remapped
    // until all BBs are mapped.
    RemapInstruction(NewBB->getTerminator(), VMap,
                     ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges,
                     TypeMapper, Materializer);
  }

  // Defer PHI resolution until rest of function is resolved, PHI resolution
  // requires the CFG to be up-to-date.
  for (unsigned phino = 0, e = PHIToResolve.size(); phino != e; ) {
    const PHINode *OPN = PHIToResolve[phino];
    unsigned NumPreds = OPN->getNumIncomingValues();
    const BasicBlock *OldBB = OPN->getParent();
    BasicBlock *NewBB = cast<BasicBlock>(VMap[OldBB]);

    // Map operands for blocks that are live and remove operands for blocks
    // that are dead.
    for (; phino != PHIToResolve.size() &&
         PHIToResolve[phino]->getParent() == OldBB; ++phino) {
      OPN = PHIToResolve[phino];
      PHINode *PN = cast<PHINode>(VMap[OPN]);
      for (unsigned pred = 0, e = NumPreds; pred != e; ++pred) {
        Value *V = VMap.lookup(PN->getIncomingBlock(pred));
        if (BasicBlock *MappedBlock = cast_or_null<BasicBlock>(V)) {
          Value *InVal = MapValue(PN->getIncomingValue(pred),
                                  VMap,
                        ModuleLevelChanges ? RF_None : RF_NoModuleLevelChanges);
          assert(InVal && "Unknown input value?");
          PN->setIncomingValue(pred, InVal);
          PN->setIncomingBlock(pred, MappedBlock);
        } else {
          PN->removeIncomingValue(pred, false);
          --pred;  // Revisit the next entry.
          --e;
        }
      }
    }

    // The loop above has removed PHI entries for those blocks that are dead
    // and has updated others.  However, if a block is live (i.e. copied over)
    // but its terminator has been changed to not go to this block, then our
    // phi nodes will have invalid entries.  Update the PHI nodes in this
    // case.
    PHINode *PN = cast<PHINode>(NewBB->begin());
    NumPreds = pred_size(NewBB);
    if (NumPreds != PN->getNumIncomingValues()) {
      assert(NumPreds < PN->getNumIncomingValues());
      // Count how many times each predecessor comes to this block.
      std::map<BasicBlock*, unsigned> PredCount;
      for (pred_iterator PI = pred_begin(NewBB), E = pred_end(NewBB);
           PI != E; ++PI)
        --PredCount[*PI];

      // Figure out how many entries to remove from each PHI.
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
        ++PredCount[PN->getIncomingBlock(i)];

      // At this point, the excess predecessor entries are positive in the
      // map.  Loop over all of the PHIs and remove excess predecessor
      // entries.
      BasicBlock::iterator I = NewBB->begin();
      for (; (PN = dyn_cast<PHINode>(I)); ++I) {
        for (const auto &PCI : PredCount) {
          BasicBlock *Pred = PCI.first;
          for (unsigned NumToRemove = PCI.second; NumToRemove; --NumToRemove)
            PN->removeIncomingValue(Pred, false);
        }
      }
    }

    // If the loops above have made these phi nodes have 0 or 1 operand,
    // replace them with undef or the input value.  We must do this for
    // correctness, because 0-operand phis are not valid.
    PN = cast<PHINode>(NewBB->begin());
    if (PN->getNumIncomingValues() == 0) {
      BasicBlock::iterator I = NewBB->begin();
      BasicBlock::const_iterator OldI = OldBB->begin();
      while ((PN = dyn_cast<PHINode>(I++))) {
        Value *NV = UndefValue::get(PN->getType());
        PN->replaceAllUsesWith(NV);
        assert(VMap[&*OldI] == PN && "VMap mismatch");
        VMap[&*OldI] = NV;
        PN->eraseFromParent();
        ++OldI;
      }
    }
  }

  // Make a second pass over the PHINodes now that all of them have been
  // remapped into the new function, simplifying the PHINode and performing any
  // recursive simplifications exposed. This will transparently update the
  // WeakTrackingVH in the VMap. Notably, we rely on that so that if we coalesce
  // two PHINodes, the iteration over the old PHIs remains valid, and the
  // mapping will just map us to the new node (which may not even be a PHI
  // node).
  const DataLayout &DL = NewFunc->getParent()->getDataLayout();
  SmallSetVector<const Value *, 8> Worklist;
  for (unsigned Idx = 0, Size = PHIToResolve.size(); Idx != Size; ++Idx)
    if (isa<PHINode>(VMap[PHIToResolve[Idx]]))
      Worklist.insert(PHIToResolve[Idx]);

  // Note that we must test the size on each iteration, the worklist can grow.
  for (unsigned Idx = 0; Idx != Worklist.size(); ++Idx) {
    const Value *OrigV = Worklist[Idx];
    auto *I = dyn_cast_or_null<Instruction>(VMap.lookup(OrigV));
    if (!I)
      continue;

    // Skip over non-intrinsic callsites, we don't want to remove any nodes from
    // the CGSCC.
    CallSite CS = CallSite(I);
    if (CS && CS.getCalledFunction() && !CS.getCalledFunction()->isIntrinsic())
      continue;

    // See if this instruction simplifies.
    Value *SimpleV = SimplifyInstruction(I, DL);
    if (!SimpleV)
      continue;

    // Stash away all the uses of the old instruction so we can check them for
    // recursive simplifications after a RAUW. This is cheaper than checking all
    // uses of To on the recursive step in most cases.
    for (const User *U : OrigV->users())
      Worklist.insert(cast<Instruction>(U));

    // Replace the instruction with its simplified value.
    I->replaceAllUsesWith(SimpleV);

    // If the original instruction had no side effects, remove it.
    if (isInstructionTriviallyDead(I))
      I->eraseFromParent();
    else
      VMap[OrigV] = I;
  }

  // Now that the inlined function body has been fully constructed, go through
  // and zap unconditional fall-through branches. This happens all the time when
  // specializing code: code specialization turns conditional branches into
  // uncond branches, and this code folds them.
  Function::iterator Begin = cast<BasicBlock>(VMap[StartingBB])->getIterator();
  Function::iterator I = Begin;
  while (I != NewFunc->end()) {
    // We need to simplify conditional branches and switches with a constant
    // operand. We try to prune these out when cloning, but if the
    // simplification required looking through PHI nodes, those are only
    // available after forming the full basic block. That may leave some here,
    // and we still want to prune the dead code as early as possible.
    //
    // Do the folding before we check if the block is dead since we want code
    // like
    //  bb:
    //    br i1 undef, label %bb, label %bb
    // to be simplified to
    //  bb:
    //    br label %bb
    // before we call I->getSinglePredecessor().
    ConstantFoldTerminator(&*I);

    // Check if this block has become dead during inlining or other
    // simplifications. Note that the first block will appear dead, as it has
    // not yet been wired up properly.
    if (I != Begin && (pred_begin(&*I) == pred_end(&*I) ||
                       I->getSinglePredecessor() == &*I)) {
      BasicBlock *DeadBB = &*I++;
      DeleteDeadBlock(DeadBB);
      continue;
    }

    BranchInst *BI = dyn_cast<BranchInst>(I->getTerminator());
    if (!BI || BI->isConditional()) { ++I; continue; }

    BasicBlock *Dest = BI->getSuccessor(0);
    if (!Dest->getSinglePredecessor()) {
      ++I; continue;
    }

    // We shouldn't be able to get single-entry PHI nodes here, as instsimplify
    // above should have zapped all of them..
    assert(!isa<PHINode>(Dest->begin()));

    // We know all single-entry PHI nodes in the inlined function have been
    // removed, so we just need to splice the blocks.
    BI->eraseFromParent();

    // Make all PHI nodes that referred to Dest now refer to I as their source.
    Dest->replaceAllUsesWith(&*I);

    // Move all the instructions in the succ to the pred.
    I->getInstList().splice(I->end(), Dest->getInstList());

    // Remove the dest block.
    Dest->eraseFromParent();

    // Do not increment I, iteratively merge all things this block branches to.
  }

  // Make a final pass over the basic blocks from the old function to gather
  // any return instructions which survived folding. We have to do this here
  // because we can iteratively remove and merge returns above.
  for (Function::iterator I = cast<BasicBlock>(VMap[StartingBB])->getIterator(),
                          E = NewFunc->end();
       I != E; ++I)
    if (ReturnInst *RI = dyn_cast<ReturnInst>(I->getTerminator()))
      Returns.push_back(RI);
}


/// This works exactly like CloneFunctionInto,
/// except that it does some simple constant prop and DCE on the fly.  The
/// effect of this is to copy significantly less code in cases where (for
/// example) a function call with constant arguments is inlined, and those
/// constant arguments cause a significant amount of code in the callee to be
/// dead.  Since this doesn't produce an exact copy of the input, it can't be
/// used for things like CloneFunction or CloneModule.
void llvm::CloneAndPruneFunctionInto(Function *NewFunc, const Function *OldFunc,
                                     ValueToValueMapTy &VMap,
                                     bool ModuleLevelChanges,
                                     SmallVectorImpl<ReturnInst*> &Returns,
                                     const char *NameSuffix,
                                     ClonedCodeInfo *CodeInfo,
                                     Instruction *TheCall) {
  CloneAndPruneIntoFromInst(NewFunc, OldFunc, &OldFunc->front().front(), VMap,
                            ModuleLevelChanges, Returns, NameSuffix, CodeInfo);
}

/// Remaps instructions in \p Blocks using the mapping in \p VMap.
void llvm::remapInstructionsInBlocks(
    const SmallVectorImpl<BasicBlock *> &Blocks, ValueToValueMapTy &VMap) {
  // Rewrite the code to refer to itself.
  for (auto *BB : Blocks)
    for (auto &Inst : *BB)
      RemapInstruction(&Inst, VMap,
                       RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
}

/// Clones a loop \p OrigLoop.  Returns the loop and the blocks in \p
/// Blocks.
///
/// Updates LoopInfo and DominatorTree assuming the loop is dominated by block
/// \p LoopDomBB.  Insert the new blocks before block specified in \p Before.
Loop *llvm::cloneLoopWithPreheader(BasicBlock *Before, BasicBlock *LoopDomBB,
                                   Loop *OrigLoop, ValueToValueMapTy &VMap,
                                   const Twine &NameSuffix, LoopInfo *LI,
                                   DominatorTree *DT,
                                   SmallVectorImpl<BasicBlock *> &Blocks) {
  assert(OrigLoop->getSubLoops().empty() &&
         "Loop to be cloned cannot have inner loop");
  Function *F = OrigLoop->getHeader()->getParent();
  Loop *ParentLoop = OrigLoop->getParentLoop();

  Loop *NewLoop = LI->AllocateLoop();
  if (ParentLoop)
    ParentLoop->addChildLoop(NewLoop);
  else
    LI->addTopLevelLoop(NewLoop);

  BasicBlock *OrigPH = OrigLoop->getLoopPreheader();
  assert(OrigPH && "No preheader");
  BasicBlock *NewPH = CloneBasicBlock(OrigPH, VMap, NameSuffix, F);
  // To rename the loop PHIs.
  VMap[OrigPH] = NewPH;
  Blocks.push_back(NewPH);

  // Update LoopInfo.
  if (ParentLoop)
    ParentLoop->addBasicBlockToLoop(NewPH, *LI);

  // Update DominatorTree.
  DT->addNewBlock(NewPH, LoopDomBB);

  for (BasicBlock *BB : OrigLoop->getBlocks()) {
    BasicBlock *NewBB = CloneBasicBlock(BB, VMap, NameSuffix, F);
    VMap[BB] = NewBB;

    // Update LoopInfo.
    NewLoop->addBasicBlockToLoop(NewBB, *LI);

    // Add DominatorTree node. After seeing all blocks, update to correct IDom.
    DT->addNewBlock(NewBB, NewPH);

    Blocks.push_back(NewBB);
  }

  for (BasicBlock *BB : OrigLoop->getBlocks()) {
    // Update DominatorTree.
    BasicBlock *IDomBB = DT->getNode(BB)->getIDom()->getBlock();
    DT->changeImmediateDominator(cast<BasicBlock>(VMap[BB]),
                                 cast<BasicBlock>(VMap[IDomBB]));
  }

  // Move them physically from the end of the block list.
  F->getBasicBlockList().splice(Before->getIterator(), F->getBasicBlockList(),
                                NewPH);
  F->getBasicBlockList().splice(Before->getIterator(), F->getBasicBlockList(),
                                NewLoop->getHeader()->getIterator(), F->end());

  return NewLoop;
}

/// Duplicate non-Phi instructions from the beginning of block up to
/// StopAt instruction into a split block between BB and its predecessor.
BasicBlock *llvm::DuplicateInstructionsInSplitBetween(
    BasicBlock *BB, BasicBlock *PredBB, Instruction *StopAt,
    ValueToValueMapTy &ValueMapping, DomTreeUpdater &DTU) {

  assert(count(successors(PredBB), BB) == 1 &&
         "There must be a single edge between PredBB and BB!");
  // We are going to have to map operands from the original BB block to the new
  // copy of the block 'NewBB'.  If there are PHI nodes in BB, evaluate them to
  // account for entry from PredBB.
  BasicBlock::iterator BI = BB->begin();
  for (; PHINode *PN = dyn_cast<PHINode>(BI); ++BI)
    ValueMapping[PN] = PN->getIncomingValueForBlock(PredBB);

  BasicBlock *NewBB = SplitEdge(PredBB, BB);
  NewBB->setName(PredBB->getName() + ".split");
  Instruction *NewTerm = NewBB->getTerminator();

  // FIXME: SplitEdge does not yet take a DTU, so we include the split edge
  //        in the update set here.
  DTU.applyUpdates({{DominatorTree::Delete, PredBB, BB},
                    {DominatorTree::Insert, PredBB, NewBB},
                    {DominatorTree::Insert, NewBB, BB}});

  // Clone the non-phi instructions of BB into NewBB, keeping track of the
  // mapping and using it to remap operands in the cloned instructions.
  // Stop once we see the terminator too. This covers the case where BB's
  // terminator gets replaced and StopAt == BB's terminator.
  for (; StopAt != &*BI && BB->getTerminator() != &*BI; ++BI) {
    Instruction *New = BI->clone();
    New->setName(BI->getName());
    New->insertBefore(NewTerm);
    ValueMapping[&*BI] = New;

    // Remap operands to patch up intra-block references.
    for (unsigned i = 0, e = New->getNumOperands(); i != e; ++i)
      if (Instruction *Inst = dyn_cast<Instruction>(New->getOperand(i))) {
        auto I = ValueMapping.find(Inst);
        if (I != ValueMapping.end())
          New->setOperand(i, I->second);
      }
  }

  return NewBB;
}
