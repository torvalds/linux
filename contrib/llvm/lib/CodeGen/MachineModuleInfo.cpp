//===-- llvm/CodeGen/MachineModuleInfo.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::dwarf;

// Handle the Pass registration stuff necessary to use DataLayout's.
INITIALIZE_PASS(MachineModuleInfo, "machinemoduleinfo",
                "Machine Module Information", false, false)
char MachineModuleInfo::ID = 0;

// Out of line virtual method.
MachineModuleInfoImpl::~MachineModuleInfoImpl() = default;

namespace llvm {

class MMIAddrLabelMapCallbackPtr final : CallbackVH {
  MMIAddrLabelMap *Map = nullptr;

public:
  MMIAddrLabelMapCallbackPtr() = default;
  MMIAddrLabelMapCallbackPtr(Value *V) : CallbackVH(V) {}

  void setPtr(BasicBlock *BB) {
    ValueHandleBase::operator=(BB);
  }

  void setMap(MMIAddrLabelMap *map) { Map = map; }

  void deleted() override;
  void allUsesReplacedWith(Value *V2) override;
};

class MMIAddrLabelMap {
  MCContext &Context;
  struct AddrLabelSymEntry {
    /// The symbols for the label.
    TinyPtrVector<MCSymbol *> Symbols;

    Function *Fn;   // The containing function of the BasicBlock.
    unsigned Index; // The index in BBCallbacks for the BasicBlock.
  };

  DenseMap<AssertingVH<BasicBlock>, AddrLabelSymEntry> AddrLabelSymbols;

  /// Callbacks for the BasicBlock's that we have entries for.  We use this so
  /// we get notified if a block is deleted or RAUWd.
  std::vector<MMIAddrLabelMapCallbackPtr> BBCallbacks;

  /// This is a per-function list of symbols whose corresponding BasicBlock got
  /// deleted.  These symbols need to be emitted at some point in the file, so
  /// AsmPrinter emits them after the function body.
  DenseMap<AssertingVH<Function>, std::vector<MCSymbol*>>
    DeletedAddrLabelsNeedingEmission;

public:
  MMIAddrLabelMap(MCContext &context) : Context(context) {}

  ~MMIAddrLabelMap() {
    assert(DeletedAddrLabelsNeedingEmission.empty() &&
           "Some labels for deleted blocks never got emitted");
  }

  ArrayRef<MCSymbol *> getAddrLabelSymbolToEmit(BasicBlock *BB);

  void takeDeletedSymbolsForFunction(Function *F,
                                     std::vector<MCSymbol*> &Result);

  void UpdateForDeletedBlock(BasicBlock *BB);
  void UpdateForRAUWBlock(BasicBlock *Old, BasicBlock *New);
};

} // end namespace llvm

ArrayRef<MCSymbol *> MMIAddrLabelMap::getAddrLabelSymbolToEmit(BasicBlock *BB) {
  assert(BB->hasAddressTaken() &&
         "Shouldn't get label for block without address taken");
  AddrLabelSymEntry &Entry = AddrLabelSymbols[BB];

  // If we already had an entry for this block, just return it.
  if (!Entry.Symbols.empty()) {
    assert(BB->getParent() == Entry.Fn && "Parent changed");
    return Entry.Symbols;
  }

  // Otherwise, this is a new entry, create a new symbol for it and add an
  // entry to BBCallbacks so we can be notified if the BB is deleted or RAUWd.
  BBCallbacks.emplace_back(BB);
  BBCallbacks.back().setMap(this);
  Entry.Index = BBCallbacks.size() - 1;
  Entry.Fn = BB->getParent();
  Entry.Symbols.push_back(Context.createTempSymbol());
  return Entry.Symbols;
}

/// If we have any deleted symbols for F, return them.
void MMIAddrLabelMap::
takeDeletedSymbolsForFunction(Function *F, std::vector<MCSymbol*> &Result) {
  DenseMap<AssertingVH<Function>, std::vector<MCSymbol*>>::iterator I =
    DeletedAddrLabelsNeedingEmission.find(F);

  // If there are no entries for the function, just return.
  if (I == DeletedAddrLabelsNeedingEmission.end()) return;

  // Otherwise, take the list.
  std::swap(Result, I->second);
  DeletedAddrLabelsNeedingEmission.erase(I);
}

void MMIAddrLabelMap::UpdateForDeletedBlock(BasicBlock *BB) {
  // If the block got deleted, there is no need for the symbol.  If the symbol
  // was already emitted, we can just forget about it, otherwise we need to
  // queue it up for later emission when the function is output.
  AddrLabelSymEntry Entry = std::move(AddrLabelSymbols[BB]);
  AddrLabelSymbols.erase(BB);
  assert(!Entry.Symbols.empty() && "Didn't have a symbol, why a callback?");
  BBCallbacks[Entry.Index] = nullptr;  // Clear the callback.

  assert((BB->getParent() == nullptr || BB->getParent() == Entry.Fn) &&
         "Block/parent mismatch");

  for (MCSymbol *Sym : Entry.Symbols) {
    if (Sym->isDefined())
      return;

    // If the block is not yet defined, we need to emit it at the end of the
    // function.  Add the symbol to the DeletedAddrLabelsNeedingEmission list
    // for the containing Function.  Since the block is being deleted, its
    // parent may already be removed, we have to get the function from 'Entry'.
    DeletedAddrLabelsNeedingEmission[Entry.Fn].push_back(Sym);
  }
}

void MMIAddrLabelMap::UpdateForRAUWBlock(BasicBlock *Old, BasicBlock *New) {
  // Get the entry for the RAUW'd block and remove it from our map.
  AddrLabelSymEntry OldEntry = std::move(AddrLabelSymbols[Old]);
  AddrLabelSymbols.erase(Old);
  assert(!OldEntry.Symbols.empty() && "Didn't have a symbol, why a callback?");

  AddrLabelSymEntry &NewEntry = AddrLabelSymbols[New];

  // If New is not address taken, just move our symbol over to it.
  if (NewEntry.Symbols.empty()) {
    BBCallbacks[OldEntry.Index].setPtr(New);    // Update the callback.
    NewEntry = std::move(OldEntry);             // Set New's entry.
    return;
  }

  BBCallbacks[OldEntry.Index] = nullptr;    // Update the callback.

  // Otherwise, we need to add the old symbols to the new block's set.
  NewEntry.Symbols.insert(NewEntry.Symbols.end(), OldEntry.Symbols.begin(),
                          OldEntry.Symbols.end());
}

void MMIAddrLabelMapCallbackPtr::deleted() {
  Map->UpdateForDeletedBlock(cast<BasicBlock>(getValPtr()));
}

void MMIAddrLabelMapCallbackPtr::allUsesReplacedWith(Value *V2) {
  Map->UpdateForRAUWBlock(cast<BasicBlock>(getValPtr()), cast<BasicBlock>(V2));
}

MachineModuleInfo::MachineModuleInfo(const LLVMTargetMachine *TM)
  : ImmutablePass(ID), TM(*TM),
    Context(TM->getMCAsmInfo(), TM->getMCRegisterInfo(),
            TM->getObjFileLowering(), nullptr, false) {
  initializeMachineModuleInfoPass(*PassRegistry::getPassRegistry());
}

MachineModuleInfo::~MachineModuleInfo() = default;

bool MachineModuleInfo::doInitialization(Module &M) {
  ObjFileMMI = nullptr;
  CurCallSite = 0;
  UsesVAFloatArgument = UsesMorestackAddr = false;
  HasSplitStack = HasNosplitStack = false;
  AddrLabelSymbols = nullptr;
  TheModule = &M;
  DbgInfoAvailable = !empty(M.debug_compile_units());
  return false;
}

bool MachineModuleInfo::doFinalization(Module &M) {
  Personalities.clear();

  delete AddrLabelSymbols;
  AddrLabelSymbols = nullptr;

  Context.reset();

  delete ObjFileMMI;
  ObjFileMMI = nullptr;

  return false;
}

//===- Address of Block Management ----------------------------------------===//

ArrayRef<MCSymbol *>
MachineModuleInfo::getAddrLabelSymbolToEmit(const BasicBlock *BB) {
  // Lazily create AddrLabelSymbols.
  if (!AddrLabelSymbols)
    AddrLabelSymbols = new MMIAddrLabelMap(Context);
 return AddrLabelSymbols->getAddrLabelSymbolToEmit(const_cast<BasicBlock*>(BB));
}

void MachineModuleInfo::
takeDeletedSymbolsForFunction(const Function *F,
                              std::vector<MCSymbol*> &Result) {
  // If no blocks have had their addresses taken, we're done.
  if (!AddrLabelSymbols) return;
  return AddrLabelSymbols->
     takeDeletedSymbolsForFunction(const_cast<Function*>(F), Result);
}

/// \name Exception Handling
/// \{

void MachineModuleInfo::addPersonality(const Function *Personality) {
  for (unsigned i = 0; i < Personalities.size(); ++i)
    if (Personalities[i] == Personality)
      return;
  Personalities.push_back(Personality);
}

/// \}

MachineFunction *
MachineModuleInfo::getMachineFunction(const Function &F) const {
  auto I = MachineFunctions.find(&F);
  return I != MachineFunctions.end() ? I->second.get() : nullptr;
}

MachineFunction &
MachineModuleInfo::getOrCreateMachineFunction(const Function &F) {
  // Shortcut for the common case where a sequence of MachineFunctionPasses
  // all query for the same Function.
  if (LastRequest == &F)
    return *LastResult;

  auto I = MachineFunctions.insert(
      std::make_pair(&F, std::unique_ptr<MachineFunction>()));
  MachineFunction *MF;
  if (I.second) {
    // No pre-existing machine function, create a new one.
    const TargetSubtargetInfo &STI = *TM.getSubtargetImpl(F);
    MF = new MachineFunction(F, TM, STI, NextFnNum++, *this);
    // Update the set entry.
    I.first->second.reset(MF);
  } else {
    MF = I.first->second.get();
  }

  LastRequest = &F;
  LastResult = MF;
  return *MF;
}

void MachineModuleInfo::deleteMachineFunctionFor(Function &F) {
  MachineFunctions.erase(&F);
  LastRequest = nullptr;
  LastResult = nullptr;
}

namespace {

/// This pass frees the MachineFunction object associated with a Function.
class FreeMachineFunction : public FunctionPass {
public:
  static char ID;

  FreeMachineFunction() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfo>();
    AU.addPreserved<MachineModuleInfo>();
  }

  bool runOnFunction(Function &F) override {
    MachineModuleInfo &MMI = getAnalysis<MachineModuleInfo>();
    MMI.deleteMachineFunctionFor(F);
    return true;
  }

  StringRef getPassName() const override {
    return "Free MachineFunction";
  }
};

} // end anonymous namespace

char FreeMachineFunction::ID;

FunctionPass *llvm::createFreeMachineFunctionPass() {
  return new FreeMachineFunction();
}

//===- MMI building helpers -----------------------------------------------===//

void llvm::computeUsesVAFloatArgument(const CallInst &I,
                                      MachineModuleInfo &MMI) {
  FunctionType *FT =
      cast<FunctionType>(I.getCalledValue()->getType()->getContainedType(0));
  if (FT->isVarArg() && !MMI.usesVAFloatArgument()) {
    for (unsigned i = 0, e = I.getNumArgOperands(); i != e; ++i) {
      Type *T = I.getArgOperand(i)->getType();
      for (auto i : post_order(T)) {
        if (i->isFloatingPointTy()) {
          MMI.setUsesVAFloatArgument(true);
          return;
        }
      }
    }
  }
}
