//===- CrashDebugger.cpp - Debug compilation crashes ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the bugpoint internals that narrow down compilation crashes
//
//===----------------------------------------------------------------------===//

#include "BugDriver.h"
#include "ListReducer.h"
#include "ToolRunner.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <set>
using namespace llvm;

namespace {
cl::opt<bool> KeepMain("keep-main",
                       cl::desc("Force function reduction to keep main"),
                       cl::init(false));
cl::opt<bool> NoGlobalRM("disable-global-remove",
                         cl::desc("Do not remove global variables"),
                         cl::init(false));

cl::opt<bool> ReplaceFuncsWithNull(
    "replace-funcs-with-null",
    cl::desc("When stubbing functions, replace all uses will null"),
    cl::init(false));
cl::opt<bool> DontReducePassList("disable-pass-list-reduction",
                                 cl::desc("Skip pass list reduction steps"),
                                 cl::init(false));

cl::opt<bool> NoNamedMDRM("disable-namedmd-remove",
                          cl::desc("Do not remove global named metadata"),
                          cl::init(false));
cl::opt<bool> NoStripDebugInfo("disable-strip-debuginfo",
                               cl::desc("Do not strip debug info metadata"),
                               cl::init(false));
cl::opt<bool> NoStripDebugTypeInfo("disable-strip-debug-types",
                               cl::desc("Do not strip debug type info metadata"),
                               cl::init(false));
cl::opt<bool> VerboseErrors("verbose-errors",
                            cl::desc("Print the output of crashing program"),
                            cl::init(false));
}

namespace llvm {
class ReducePassList : public ListReducer<std::string> {
  BugDriver &BD;

public:
  ReducePassList(BugDriver &bd) : BD(bd) {}

  // Return true iff running the "removed" passes succeeds, and running the
  // "Kept" passes fail when run on the output of the "removed" passes.  If we
  // return true, we update the current module of bugpoint.
  Expected<TestResult> doTest(std::vector<std::string> &Removed,
                              std::vector<std::string> &Kept) override;
};
}

Expected<ReducePassList::TestResult>
ReducePassList::doTest(std::vector<std::string> &Prefix,
                       std::vector<std::string> &Suffix) {
  std::string PrefixOutput;
  std::unique_ptr<Module> OrigProgram;
  if (!Prefix.empty()) {
    outs() << "Checking to see if these passes crash: "
           << getPassesString(Prefix) << ": ";
    if (BD.runPasses(BD.getProgram(), Prefix, PrefixOutput))
      return KeepPrefix;

    OrigProgram = std::move(BD.Program);

    BD.Program = parseInputFile(PrefixOutput, BD.getContext());
    if (BD.Program == nullptr) {
      errs() << BD.getToolName() << ": Error reading bitcode file '"
             << PrefixOutput << "'!\n";
      exit(1);
    }
    sys::fs::remove(PrefixOutput);
  }

  outs() << "Checking to see if these passes crash: " << getPassesString(Suffix)
         << ": ";

  if (BD.runPasses(BD.getProgram(), Suffix))
    return KeepSuffix; // The suffix crashes alone...

  // Nothing failed, restore state...
  if (OrigProgram)
    BD.Program = std::move(OrigProgram);
  return NoFailure;
}

using BugTester = bool (*)(const BugDriver &, Module *);

namespace {
/// ReduceCrashingGlobalInitializers - This works by removing global variable
/// initializers and seeing if the program still crashes. If it does, then we
/// keep that program and try again.
class ReduceCrashingGlobalInitializers : public ListReducer<GlobalVariable *> {
  BugDriver &BD;
  BugTester TestFn;

public:
  ReduceCrashingGlobalInitializers(BugDriver &bd, BugTester testFn)
      : BD(bd), TestFn(testFn) {}

  Expected<TestResult> doTest(std::vector<GlobalVariable *> &Prefix,
                              std::vector<GlobalVariable *> &Kept) override {
    if (!Kept.empty() && TestGlobalVariables(Kept))
      return KeepSuffix;
    if (!Prefix.empty() && TestGlobalVariables(Prefix))
      return KeepPrefix;
    return NoFailure;
  }

  bool TestGlobalVariables(std::vector<GlobalVariable *> &GVs);
};
}

bool ReduceCrashingGlobalInitializers::TestGlobalVariables(
    std::vector<GlobalVariable *> &GVs) {
  // Clone the program to try hacking it apart...
  ValueToValueMapTy VMap;
  std::unique_ptr<Module> M = CloneModule(BD.getProgram(), VMap);

  // Convert list to set for fast lookup...
  std::set<GlobalVariable *> GVSet;

  for (unsigned i = 0, e = GVs.size(); i != e; ++i) {
    GlobalVariable *CMGV = cast<GlobalVariable>(VMap[GVs[i]]);
    assert(CMGV && "Global Variable not in module?!");
    GVSet.insert(CMGV);
  }

  outs() << "Checking for crash with only these global variables: ";
  PrintGlobalVariableList(GVs);
  outs() << ": ";

  // Loop over and delete any global variables which we aren't supposed to be
  // playing with...
  for (GlobalVariable &I : M->globals())
    if (I.hasInitializer() && !GVSet.count(&I)) {
      DeleteGlobalInitializer(&I);
      I.setLinkage(GlobalValue::ExternalLinkage);
      I.setComdat(nullptr);
    }

  // Try running the hacked up program...
  if (TestFn(BD, M.get())) {
    BD.setNewProgram(std::move(M)); // It crashed, keep the trimmed version...

    // Make sure to use global variable pointers that point into the now-current
    // module.
    GVs.assign(GVSet.begin(), GVSet.end());
    return true;
  }

  return false;
}

namespace {
/// ReduceCrashingFunctions reducer - This works by removing functions and
/// seeing if the program still crashes. If it does, then keep the newer,
/// smaller program.
///
class ReduceCrashingFunctions : public ListReducer<Function *> {
  BugDriver &BD;
  BugTester TestFn;

public:
  ReduceCrashingFunctions(BugDriver &bd, BugTester testFn)
      : BD(bd), TestFn(testFn) {}

  Expected<TestResult> doTest(std::vector<Function *> &Prefix,
                              std::vector<Function *> &Kept) override {
    if (!Kept.empty() && TestFuncs(Kept))
      return KeepSuffix;
    if (!Prefix.empty() && TestFuncs(Prefix))
      return KeepPrefix;
    return NoFailure;
  }

  bool TestFuncs(std::vector<Function *> &Prefix);
};
}

static void RemoveFunctionReferences(Module *M, const char *Name) {
  auto *UsedVar = M->getGlobalVariable(Name, true);
  if (!UsedVar || !UsedVar->hasInitializer())
    return;
  if (isa<ConstantAggregateZero>(UsedVar->getInitializer())) {
    assert(UsedVar->use_empty());
    UsedVar->eraseFromParent();
    return;
  }
  auto *OldUsedVal = cast<ConstantArray>(UsedVar->getInitializer());
  std::vector<Constant *> Used;
  for (Value *V : OldUsedVal->operand_values()) {
    Constant *Op = cast<Constant>(V->stripPointerCasts());
    if (!Op->isNullValue()) {
      Used.push_back(cast<Constant>(V));
    }
  }
  auto *NewValElemTy = OldUsedVal->getType()->getElementType();
  auto *NewValTy = ArrayType::get(NewValElemTy, Used.size());
  auto *NewUsedVal = ConstantArray::get(NewValTy, Used);
  UsedVar->mutateType(NewUsedVal->getType()->getPointerTo());
  UsedVar->setInitializer(NewUsedVal);
}

bool ReduceCrashingFunctions::TestFuncs(std::vector<Function *> &Funcs) {
  // If main isn't present, claim there is no problem.
  if (KeepMain && !is_contained(Funcs, BD.getProgram().getFunction("main")))
    return false;

  // Clone the program to try hacking it apart...
  ValueToValueMapTy VMap;
  std::unique_ptr<Module> M = CloneModule(BD.getProgram(), VMap);

  // Convert list to set for fast lookup...
  std::set<Function *> Functions;
  for (unsigned i = 0, e = Funcs.size(); i != e; ++i) {
    Function *CMF = cast<Function>(VMap[Funcs[i]]);
    assert(CMF && "Function not in module?!");
    assert(CMF->getFunctionType() == Funcs[i]->getFunctionType() && "wrong ty");
    assert(CMF->getName() == Funcs[i]->getName() && "wrong name");
    Functions.insert(CMF);
  }

  outs() << "Checking for crash with only these functions: ";
  PrintFunctionList(Funcs);
  outs() << ": ";
  if (!ReplaceFuncsWithNull) {
    // Loop over and delete any functions which we aren't supposed to be playing
    // with...
    for (Function &I : *M)
      if (!I.isDeclaration() && !Functions.count(&I))
        DeleteFunctionBody(&I);
  } else {
    std::vector<GlobalValue *> ToRemove;
    // First, remove aliases to functions we're about to purge.
    for (GlobalAlias &Alias : M->aliases()) {
      GlobalObject *Root = Alias.getBaseObject();
      Function *F = dyn_cast_or_null<Function>(Root);
      if (F) {
        if (Functions.count(F))
          // We're keeping this function.
          continue;
      } else if (Root->isNullValue()) {
        // This referenced a globalalias that we've already replaced,
        // so we still need to replace this alias.
      } else if (!F) {
        // Not a function, therefore not something we mess with.
        continue;
      }

      PointerType *Ty = cast<PointerType>(Alias.getType());
      Constant *Replacement = ConstantPointerNull::get(Ty);
      Alias.replaceAllUsesWith(Replacement);
      ToRemove.push_back(&Alias);
    }

    for (Function &I : *M) {
      if (!I.isDeclaration() && !Functions.count(&I)) {
        PointerType *Ty = cast<PointerType>(I.getType());
        Constant *Replacement = ConstantPointerNull::get(Ty);
        I.replaceAllUsesWith(Replacement);
        ToRemove.push_back(&I);
      }
    }

    for (auto *F : ToRemove) {
      F->eraseFromParent();
    }

    // Finally, remove any null members from any global intrinsic.
    RemoveFunctionReferences(M.get(), "llvm.used");
    RemoveFunctionReferences(M.get(), "llvm.compiler.used");
  }
  // Try running the hacked up program...
  if (TestFn(BD, M.get())) {
    BD.setNewProgram(std::move(M)); // It crashed, keep the trimmed version...

    // Make sure to use function pointers that point into the now-current
    // module.
    Funcs.assign(Functions.begin(), Functions.end());
    return true;
  }
  return false;
}

namespace {
/// ReduceCrashingFunctionAttributes reducer - This works by removing
/// attributes on a particular function and seeing if the program still crashes.
/// If it does, then keep the newer, smaller program.
///
class ReduceCrashingFunctionAttributes : public ListReducer<Attribute> {
  BugDriver &BD;
  std::string FnName;
  BugTester TestFn;

public:
  ReduceCrashingFunctionAttributes(BugDriver &bd, const std::string &FnName,
                                   BugTester testFn)
      : BD(bd), FnName(FnName), TestFn(testFn) {}

  Expected<TestResult> doTest(std::vector<Attribute> &Prefix,
                              std::vector<Attribute> &Kept) override {
    if (!Kept.empty() && TestFuncAttrs(Kept))
      return KeepSuffix;
    if (!Prefix.empty() && TestFuncAttrs(Prefix))
      return KeepPrefix;
    return NoFailure;
  }

  bool TestFuncAttrs(std::vector<Attribute> &Attrs);
};
}

bool ReduceCrashingFunctionAttributes::TestFuncAttrs(
    std::vector<Attribute> &Attrs) {
  // Clone the program to try hacking it apart...
  std::unique_ptr<Module> M = CloneModule(BD.getProgram());
  Function *F = M->getFunction(FnName);

  // Build up an AttributeList from the attributes we've been given by the
  // reducer.
  AttrBuilder AB;
  for (auto A : Attrs)
    AB.addAttribute(A);
  AttributeList NewAttrs;
  NewAttrs =
      NewAttrs.addAttributes(BD.getContext(), AttributeList::FunctionIndex, AB);

  // Set this new list of attributes on the function.
  F->setAttributes(NewAttrs);

  // Try running on the hacked up program...
  if (TestFn(BD, M.get())) {
    BD.setNewProgram(std::move(M)); // It crashed, keep the trimmed version...

    // Pass along the set of attributes that caused the crash.
    Attrs.clear();
    for (Attribute A : NewAttrs.getFnAttributes()) {
      Attrs.push_back(A);
    }
    return true;
  }
  return false;
}

namespace {
/// Simplify the CFG without completely destroying it.
/// This is not well defined, but basically comes down to "try to eliminate
/// unreachable blocks and constant fold terminators without deciding that
/// certain undefined behavior cuts off the program at the legs".
void simpleSimplifyCfg(Function &F, SmallVectorImpl<BasicBlock *> &BBs) {
  if (F.empty())
    return;

  for (auto *BB : BBs) {
    ConstantFoldTerminator(BB);
    MergeBlockIntoPredecessor(BB);
  }

  // Remove unreachable blocks
  // removeUnreachableBlocks can't be used here, it will turn various
  // undefined behavior into unreachables, but bugpoint was the thing that
  // generated the undefined behavior, and we don't want it to kill the entire
  // program.
  SmallPtrSet<BasicBlock *, 16> Visited;
  for (auto *BB : depth_first(&F.getEntryBlock()))
    Visited.insert(BB);

  SmallVector<BasicBlock *, 16> Unreachable;
  for (auto &BB : F)
    if (!Visited.count(&BB))
      Unreachable.push_back(&BB);

  // The dead BB's may be in a dead cycle or otherwise have references to each
  // other.  Because of this, we have to drop all references first, then delete
  // them all at once.
  for (auto *BB : Unreachable) {
    for (BasicBlock *Successor : successors(&*BB))
      if (Visited.count(Successor))
        Successor->removePredecessor(&*BB);
    BB->dropAllReferences();
  }
  for (auto *BB : Unreachable)
    BB->eraseFromParent();
}
/// ReduceCrashingBlocks reducer - This works by setting the terminators of
/// all terminators except the specified basic blocks to a 'ret' instruction,
/// then running the simplify-cfg pass.  This has the effect of chopping up
/// the CFG really fast which can reduce large functions quickly.
///
class ReduceCrashingBlocks : public ListReducer<const BasicBlock *> {
  BugDriver &BD;
  BugTester TestFn;

public:
  ReduceCrashingBlocks(BugDriver &BD, BugTester testFn)
      : BD(BD), TestFn(testFn) {}

  Expected<TestResult> doTest(std::vector<const BasicBlock *> &Prefix,
                              std::vector<const BasicBlock *> &Kept) override {
    if (!Kept.empty() && TestBlocks(Kept))
      return KeepSuffix;
    if (!Prefix.empty() && TestBlocks(Prefix))
      return KeepPrefix;
    return NoFailure;
  }

  bool TestBlocks(std::vector<const BasicBlock *> &Prefix);
};
}

bool ReduceCrashingBlocks::TestBlocks(std::vector<const BasicBlock *> &BBs) {
  // Clone the program to try hacking it apart...
  ValueToValueMapTy VMap;
  std::unique_ptr<Module> M = CloneModule(BD.getProgram(), VMap);

  // Convert list to set for fast lookup...
  SmallPtrSet<BasicBlock *, 8> Blocks;
  for (unsigned i = 0, e = BBs.size(); i != e; ++i)
    Blocks.insert(cast<BasicBlock>(VMap[BBs[i]]));

  outs() << "Checking for crash with only these blocks:";
  unsigned NumPrint = Blocks.size();
  if (NumPrint > 10)
    NumPrint = 10;
  for (unsigned i = 0, e = NumPrint; i != e; ++i)
    outs() << " " << BBs[i]->getName();
  if (NumPrint < Blocks.size())
    outs() << "... <" << Blocks.size() << " total>";
  outs() << ": ";

  // Loop over and delete any hack up any blocks that are not listed...
  for (Function &F : M->functions()) {
    for (BasicBlock &BB : F) {
      if (!Blocks.count(&BB) && BB.getTerminator()->getNumSuccessors()) {
        // Loop over all of the successors of this block, deleting any PHI nodes
        // that might include it.
        for (BasicBlock *Succ : successors(&BB))
          Succ->removePredecessor(&BB);

        Instruction *BBTerm = BB.getTerminator();
        if (BBTerm->isEHPad() || BBTerm->getType()->isTokenTy())
          continue;
        if (!BBTerm->getType()->isVoidTy())
          BBTerm->replaceAllUsesWith(Constant::getNullValue(BBTerm->getType()));

        // Replace the old terminator instruction.
        BB.getInstList().pop_back();
        new UnreachableInst(BB.getContext(), &BB);
      }
    }
  }

  // The CFG Simplifier pass may delete one of the basic blocks we are
  // interested in.  If it does we need to take the block out of the list.  Make
  // a "persistent mapping" by turning basic blocks into <function, name> pairs.
  // This won't work well if blocks are unnamed, but that is just the risk we
  // have to take. FIXME: Can we just name the blocks?
  std::vector<std::pair<std::string, std::string>> BlockInfo;

  for (BasicBlock *BB : Blocks)
    BlockInfo.emplace_back(BB->getParent()->getName(), BB->getName());

  SmallVector<BasicBlock *, 16> ToProcess;
  for (auto &F : *M) {
    for (auto &BB : F)
      if (!Blocks.count(&BB))
        ToProcess.push_back(&BB);
    simpleSimplifyCfg(F, ToProcess);
    ToProcess.clear();
  }
  // Verify we didn't break anything
  std::vector<std::string> Passes;
  Passes.push_back("verify");
  std::unique_ptr<Module> New = BD.runPassesOn(M.get(), Passes);
  if (!New) {
    errs() << "verify failed!\n";
    exit(1);
  }
  M = std::move(New);

  // Try running on the hacked up program...
  if (TestFn(BD, M.get())) {
    BD.setNewProgram(std::move(M)); // It crashed, keep the trimmed version...

    // Make sure to use basic block pointers that point into the now-current
    // module, and that they don't include any deleted blocks.
    BBs.clear();
    const ValueSymbolTable &GST = BD.getProgram().getValueSymbolTable();
    for (const auto &BI : BlockInfo) {
      Function *F = cast<Function>(GST.lookup(BI.first));
      Value *V = F->getValueSymbolTable()->lookup(BI.second);
      if (V && V->getType() == Type::getLabelTy(V->getContext()))
        BBs.push_back(cast<BasicBlock>(V));
    }
    return true;
  }
  // It didn't crash, try something else.
  return false;
}

namespace {
/// ReduceCrashingConditionals reducer - This works by changing
/// conditional branches to unconditional ones, then simplifying the CFG
/// This has the effect of chopping up the CFG really fast which can reduce
/// large functions quickly.
///
class ReduceCrashingConditionals : public ListReducer<const BasicBlock *> {
  BugDriver &BD;
  BugTester TestFn;
  bool Direction;

public:
  ReduceCrashingConditionals(BugDriver &bd, BugTester testFn, bool Direction)
      : BD(bd), TestFn(testFn), Direction(Direction) {}

  Expected<TestResult> doTest(std::vector<const BasicBlock *> &Prefix,
                              std::vector<const BasicBlock *> &Kept) override {
    if (!Kept.empty() && TestBlocks(Kept))
      return KeepSuffix;
    if (!Prefix.empty() && TestBlocks(Prefix))
      return KeepPrefix;
    return NoFailure;
  }

  bool TestBlocks(std::vector<const BasicBlock *> &Prefix);
};
}

bool ReduceCrashingConditionals::TestBlocks(
    std::vector<const BasicBlock *> &BBs) {
  // Clone the program to try hacking it apart...
  ValueToValueMapTy VMap;
  std::unique_ptr<Module> M = CloneModule(BD.getProgram(), VMap);

  // Convert list to set for fast lookup...
  SmallPtrSet<const BasicBlock *, 8> Blocks;
  for (const auto *BB : BBs)
    Blocks.insert(cast<BasicBlock>(VMap[BB]));

  outs() << "Checking for crash with changing conditionals to always jump to "
         << (Direction ? "true" : "false") << ":";
  unsigned NumPrint = Blocks.size();
  if (NumPrint > 10)
    NumPrint = 10;
  for (unsigned i = 0, e = NumPrint; i != e; ++i)
    outs() << " " << BBs[i]->getName();
  if (NumPrint < Blocks.size())
    outs() << "... <" << Blocks.size() << " total>";
  outs() << ": ";

  // Loop over and delete any hack up any blocks that are not listed...
  for (auto &F : *M)
    for (auto &BB : F)
      if (!Blocks.count(&BB)) {
        auto *BR = dyn_cast<BranchInst>(BB.getTerminator());
        if (!BR || !BR->isConditional())
          continue;
        if (Direction)
          BR->setCondition(ConstantInt::getTrue(BR->getContext()));
        else
          BR->setCondition(ConstantInt::getFalse(BR->getContext()));
      }

  // The following may destroy some blocks, so we save them first
  std::vector<std::pair<std::string, std::string>> BlockInfo;

  for (const BasicBlock *BB : Blocks)
    BlockInfo.emplace_back(BB->getParent()->getName(), BB->getName());

  SmallVector<BasicBlock *, 16> ToProcess;
  for (auto &F : *M) {
    for (auto &BB : F)
      if (!Blocks.count(&BB))
        ToProcess.push_back(&BB);
    simpleSimplifyCfg(F, ToProcess);
    ToProcess.clear();
  }
  // Verify we didn't break anything
  std::vector<std::string> Passes;
  Passes.push_back("verify");
  std::unique_ptr<Module> New = BD.runPassesOn(M.get(), Passes);
  if (!New) {
    errs() << "verify failed!\n";
    exit(1);
  }
  M = std::move(New);

  // Try running on the hacked up program...
  if (TestFn(BD, M.get())) {
    BD.setNewProgram(std::move(M)); // It crashed, keep the trimmed version...

    // Make sure to use basic block pointers that point into the now-current
    // module, and that they don't include any deleted blocks.
    BBs.clear();
    const ValueSymbolTable &GST = BD.getProgram().getValueSymbolTable();
    for (auto &BI : BlockInfo) {
      auto *F = cast<Function>(GST.lookup(BI.first));
      Value *V = F->getValueSymbolTable()->lookup(BI.second);
      if (V && V->getType() == Type::getLabelTy(V->getContext()))
        BBs.push_back(cast<BasicBlock>(V));
    }
    return true;
  }
  // It didn't crash, try something else.
  return false;
}

namespace {
/// SimplifyCFG reducer - This works by calling SimplifyCFG on each basic block
/// in the program.

class ReduceSimplifyCFG : public ListReducer<const BasicBlock *> {
  BugDriver &BD;
  BugTester TestFn;
  TargetTransformInfo TTI;

public:
  ReduceSimplifyCFG(BugDriver &bd, BugTester testFn)
      : BD(bd), TestFn(testFn), TTI(bd.getProgram().getDataLayout()) {}

  Expected<TestResult> doTest(std::vector<const BasicBlock *> &Prefix,
                              std::vector<const BasicBlock *> &Kept) override {
    if (!Kept.empty() && TestBlocks(Kept))
      return KeepSuffix;
    if (!Prefix.empty() && TestBlocks(Prefix))
      return KeepPrefix;
    return NoFailure;
  }

  bool TestBlocks(std::vector<const BasicBlock *> &Prefix);
};
}

bool ReduceSimplifyCFG::TestBlocks(std::vector<const BasicBlock *> &BBs) {
  // Clone the program to try hacking it apart...
  ValueToValueMapTy VMap;
  std::unique_ptr<Module> M = CloneModule(BD.getProgram(), VMap);

  // Convert list to set for fast lookup...
  SmallPtrSet<const BasicBlock *, 8> Blocks;
  for (const auto *BB : BBs)
    Blocks.insert(cast<BasicBlock>(VMap[BB]));

  outs() << "Checking for crash with CFG simplifying:";
  unsigned NumPrint = Blocks.size();
  if (NumPrint > 10)
    NumPrint = 10;
  for (unsigned i = 0, e = NumPrint; i != e; ++i)
    outs() << " " << BBs[i]->getName();
  if (NumPrint < Blocks.size())
    outs() << "... <" << Blocks.size() << " total>";
  outs() << ": ";

  // The following may destroy some blocks, so we save them first
  std::vector<std::pair<std::string, std::string>> BlockInfo;

  for (const BasicBlock *BB : Blocks)
    BlockInfo.emplace_back(BB->getParent()->getName(), BB->getName());

  // Loop over and delete any hack up any blocks that are not listed...
  for (auto &F : *M)
    // Loop over all of the basic blocks and remove them if they are unneeded.
    for (Function::iterator BBIt = F.begin(); BBIt != F.end();) {
      if (!Blocks.count(&*BBIt)) {
        ++BBIt;
        continue;
      }
      simplifyCFG(&*BBIt++, TTI);
    }
  // Verify we didn't break anything
  std::vector<std::string> Passes;
  Passes.push_back("verify");
  std::unique_ptr<Module> New = BD.runPassesOn(M.get(), Passes);
  if (!New) {
    errs() << "verify failed!\n";
    exit(1);
  }
  M = std::move(New);

  // Try running on the hacked up program...
  if (TestFn(BD, M.get())) {
    BD.setNewProgram(std::move(M)); // It crashed, keep the trimmed version...

    // Make sure to use basic block pointers that point into the now-current
    // module, and that they don't include any deleted blocks.
    BBs.clear();
    const ValueSymbolTable &GST = BD.getProgram().getValueSymbolTable();
    for (auto &BI : BlockInfo) {
      auto *F = cast<Function>(GST.lookup(BI.first));
      Value *V = F->getValueSymbolTable()->lookup(BI.second);
      if (V && V->getType() == Type::getLabelTy(V->getContext()))
        BBs.push_back(cast<BasicBlock>(V));
    }
    return true;
  }
  // It didn't crash, try something else.
  return false;
}

namespace {
/// ReduceCrashingInstructions reducer - This works by removing the specified
/// non-terminator instructions and replacing them with undef.
///
class ReduceCrashingInstructions : public ListReducer<const Instruction *> {
  BugDriver &BD;
  BugTester TestFn;

public:
  ReduceCrashingInstructions(BugDriver &bd, BugTester testFn)
      : BD(bd), TestFn(testFn) {}

  Expected<TestResult> doTest(std::vector<const Instruction *> &Prefix,
                              std::vector<const Instruction *> &Kept) override {
    if (!Kept.empty() && TestInsts(Kept))
      return KeepSuffix;
    if (!Prefix.empty() && TestInsts(Prefix))
      return KeepPrefix;
    return NoFailure;
  }

  bool TestInsts(std::vector<const Instruction *> &Prefix);
};
}

bool ReduceCrashingInstructions::TestInsts(
    std::vector<const Instruction *> &Insts) {
  // Clone the program to try hacking it apart...
  ValueToValueMapTy VMap;
  std::unique_ptr<Module> M = CloneModule(BD.getProgram(), VMap);

  // Convert list to set for fast lookup...
  SmallPtrSet<Instruction *, 32> Instructions;
  for (unsigned i = 0, e = Insts.size(); i != e; ++i) {
    assert(!Insts[i]->isTerminator());
    Instructions.insert(cast<Instruction>(VMap[Insts[i]]));
  }

  outs() << "Checking for crash with only " << Instructions.size();
  if (Instructions.size() == 1)
    outs() << " instruction: ";
  else
    outs() << " instructions: ";

  for (Module::iterator MI = M->begin(), ME = M->end(); MI != ME; ++MI)
    for (Function::iterator FI = MI->begin(), FE = MI->end(); FI != FE; ++FI)
      for (BasicBlock::iterator I = FI->begin(), E = FI->end(); I != E;) {
        Instruction *Inst = &*I++;
        if (!Instructions.count(Inst) && !Inst->isTerminator() &&
            !Inst->isEHPad() && !Inst->getType()->isTokenTy() &&
            !Inst->isSwiftError()) {
          if (!Inst->getType()->isVoidTy())
            Inst->replaceAllUsesWith(UndefValue::get(Inst->getType()));
          Inst->eraseFromParent();
        }
      }

  // Verify that this is still valid.
  legacy::PassManager Passes;
  Passes.add(createVerifierPass(/*FatalErrors=*/false));
  Passes.run(*M);

  // Try running on the hacked up program...
  if (TestFn(BD, M.get())) {
    BD.setNewProgram(std::move(M)); // It crashed, keep the trimmed version...

    // Make sure to use instruction pointers that point into the now-current
    // module, and that they don't include any deleted blocks.
    Insts.clear();
    for (Instruction *Inst : Instructions)
      Insts.push_back(Inst);
    return true;
  }
  // It didn't crash, try something else.
  return false;
}

namespace {
// Reduce the list of Named Metadata nodes. We keep this as a list of
// names to avoid having to convert back and forth every time.
class ReduceCrashingNamedMD : public ListReducer<std::string> {
  BugDriver &BD;
  BugTester TestFn;

public:
  ReduceCrashingNamedMD(BugDriver &bd, BugTester testFn)
      : BD(bd), TestFn(testFn) {}

  Expected<TestResult> doTest(std::vector<std::string> &Prefix,
                              std::vector<std::string> &Kept) override {
    if (!Kept.empty() && TestNamedMDs(Kept))
      return KeepSuffix;
    if (!Prefix.empty() && TestNamedMDs(Prefix))
      return KeepPrefix;
    return NoFailure;
  }

  bool TestNamedMDs(std::vector<std::string> &NamedMDs);
};
}

bool ReduceCrashingNamedMD::TestNamedMDs(std::vector<std::string> &NamedMDs) {

  ValueToValueMapTy VMap;
  std::unique_ptr<Module> M = CloneModule(BD.getProgram(), VMap);

  outs() << "Checking for crash with only these named metadata nodes:";
  unsigned NumPrint = std::min<size_t>(NamedMDs.size(), 10);
  for (unsigned i = 0, e = NumPrint; i != e; ++i)
    outs() << " " << NamedMDs[i];
  if (NumPrint < NamedMDs.size())
    outs() << "... <" << NamedMDs.size() << " total>";
  outs() << ": ";

  // Make a StringMap for faster lookup
  StringSet<> Names;
  for (const std::string &Name : NamedMDs)
    Names.insert(Name);

  // First collect all the metadata to delete in a vector, then
  // delete them all at once to avoid invalidating the iterator
  std::vector<NamedMDNode *> ToDelete;
  ToDelete.reserve(M->named_metadata_size() - Names.size());
  for (auto &NamedMD : M->named_metadata())
    // Always keep a nonempty llvm.dbg.cu because the Verifier would complain.
    if (!Names.count(NamedMD.getName()) &&
        (!(NamedMD.getName() == "llvm.dbg.cu" && NamedMD.getNumOperands() > 0)))
      ToDelete.push_back(&NamedMD);

  for (auto *NamedMD : ToDelete)
    NamedMD->eraseFromParent();

  // Verify that this is still valid.
  legacy::PassManager Passes;
  Passes.add(createVerifierPass(/*FatalErrors=*/false));
  Passes.run(*M);

  // Try running on the hacked up program...
  if (TestFn(BD, M.get())) {
    BD.setNewProgram(std::move(M)); // It crashed, keep the trimmed version...
    return true;
  }
  return false;
}

namespace {
// Reduce the list of operands to named metadata nodes
class ReduceCrashingNamedMDOps : public ListReducer<const MDNode *> {
  BugDriver &BD;
  BugTester TestFn;

public:
  ReduceCrashingNamedMDOps(BugDriver &bd, BugTester testFn)
      : BD(bd), TestFn(testFn) {}

  Expected<TestResult> doTest(std::vector<const MDNode *> &Prefix,
                              std::vector<const MDNode *> &Kept) override {
    if (!Kept.empty() && TestNamedMDOps(Kept))
      return KeepSuffix;
    if (!Prefix.empty() && TestNamedMDOps(Prefix))
      return KeepPrefix;
    return NoFailure;
  }

  bool TestNamedMDOps(std::vector<const MDNode *> &NamedMDOps);
};
}

bool ReduceCrashingNamedMDOps::TestNamedMDOps(
    std::vector<const MDNode *> &NamedMDOps) {
  // Convert list to set for fast lookup...
  SmallPtrSet<const MDNode *, 32> OldMDNodeOps;
  for (unsigned i = 0, e = NamedMDOps.size(); i != e; ++i) {
    OldMDNodeOps.insert(NamedMDOps[i]);
  }

  outs() << "Checking for crash with only " << OldMDNodeOps.size();
  if (OldMDNodeOps.size() == 1)
    outs() << " named metadata operand: ";
  else
    outs() << " named metadata operands: ";

  ValueToValueMapTy VMap;
  std::unique_ptr<Module> M = CloneModule(BD.getProgram(), VMap);

  // This is a little wasteful. In the future it might be good if we could have
  // these dropped during cloning.
  for (auto &NamedMD : BD.getProgram().named_metadata()) {
    // Drop the old one and create a new one
    M->eraseNamedMetadata(M->getNamedMetadata(NamedMD.getName()));
    NamedMDNode *NewNamedMDNode =
        M->getOrInsertNamedMetadata(NamedMD.getName());
    for (MDNode *op : NamedMD.operands())
      if (OldMDNodeOps.count(op))
        NewNamedMDNode->addOperand(cast<MDNode>(MapMetadata(op, VMap)));
  }

  // Verify that this is still valid.
  legacy::PassManager Passes;
  Passes.add(createVerifierPass(/*FatalErrors=*/false));
  Passes.run(*M);

  // Try running on the hacked up program...
  if (TestFn(BD, M.get())) {
    // Make sure to use instruction pointers that point into the now-current
    // module, and that they don't include any deleted blocks.
    NamedMDOps.clear();
    for (const MDNode *Node : OldMDNodeOps)
      NamedMDOps.push_back(cast<MDNode>(*VMap.getMappedMD(Node)));

    BD.setNewProgram(std::move(M)); // It crashed, keep the trimmed version...
    return true;
  }
  // It didn't crash, try something else.
  return false;
}

/// Attempt to eliminate as many global initializers as possible.
static Error ReduceGlobalInitializers(BugDriver &BD, BugTester TestFn) {
  Module &OrigM = BD.getProgram();
  if (OrigM.global_empty())
    return Error::success();

  // Now try to reduce the number of global variable initializers in the
  // module to something small.
  std::unique_ptr<Module> M = CloneModule(OrigM);
  bool DeletedInit = false;

  for (GlobalVariable &GV : M->globals()) {
    if (GV.hasInitializer()) {
      DeleteGlobalInitializer(&GV);
      GV.setLinkage(GlobalValue::ExternalLinkage);
      GV.setComdat(nullptr);
      DeletedInit = true;
    }
  }

  if (!DeletedInit)
    return Error::success();

  // See if the program still causes a crash...
  outs() << "\nChecking to see if we can delete global inits: ";

  if (TestFn(BD, M.get())) { // Still crashes?
    BD.setNewProgram(std::move(M));
    outs() << "\n*** Able to remove all global initializers!\n";
    return Error::success();
  }

  // No longer crashes.
  outs() << "  - Removing all global inits hides problem!\n";

  std::vector<GlobalVariable *> GVs;
  for (GlobalVariable &GV : OrigM.globals())
    if (GV.hasInitializer())
      GVs.push_back(&GV);

  if (GVs.size() > 1 && !BugpointIsInterrupted) {
    outs() << "\n*** Attempting to reduce the number of global initializers "
           << "in the testcase\n";

    unsigned OldSize = GVs.size();
    Expected<bool> Result =
        ReduceCrashingGlobalInitializers(BD, TestFn).reduceList(GVs);
    if (Error E = Result.takeError())
      return E;

    if (GVs.size() < OldSize)
      BD.EmitProgressBitcode(BD.getProgram(), "reduced-global-variables");
  }
  return Error::success();
}

static Error ReduceInsts(BugDriver &BD, BugTester TestFn) {
  // Attempt to delete instructions using bisection. This should help out nasty
  // cases with large basic blocks where the problem is at one end.
  if (!BugpointIsInterrupted) {
    std::vector<const Instruction *> Insts;
    for (const Function &F : BD.getProgram())
      for (const BasicBlock &BB : F)
        for (const Instruction &I : BB)
          if (!I.isTerminator())
            Insts.push_back(&I);

    Expected<bool> Result =
        ReduceCrashingInstructions(BD, TestFn).reduceList(Insts);
    if (Error E = Result.takeError())
      return E;
  }

  unsigned Simplification = 2;
  do {
    if (BugpointIsInterrupted)
      // TODO: Should we distinguish this with an "interrupted error"?
      return Error::success();
    --Simplification;
    outs() << "\n*** Attempting to reduce testcase by deleting instruc"
           << "tions: Simplification Level #" << Simplification << '\n';

    // Now that we have deleted the functions that are unnecessary for the
    // program, try to remove instructions that are not necessary to cause the
    // crash.  To do this, we loop through all of the instructions in the
    // remaining functions, deleting them (replacing any values produced with
    // nulls), and then running ADCE and SimplifyCFG.  If the transformed input
    // still triggers failure, keep deleting until we cannot trigger failure
    // anymore.
    //
    unsigned InstructionsToSkipBeforeDeleting = 0;
  TryAgain:

    // Loop over all of the (non-terminator) instructions remaining in the
    // function, attempting to delete them.
    unsigned CurInstructionNum = 0;
    for (Module::const_iterator FI = BD.getProgram().begin(),
                                E = BD.getProgram().end();
         FI != E; ++FI)
      if (!FI->isDeclaration())
        for (Function::const_iterator BI = FI->begin(), E = FI->end(); BI != E;
             ++BI)
          for (BasicBlock::const_iterator I = BI->begin(), E = --BI->end();
               I != E; ++I, ++CurInstructionNum) {
            if (InstructionsToSkipBeforeDeleting) {
              --InstructionsToSkipBeforeDeleting;
            } else {
              if (BugpointIsInterrupted)
                // TODO: Should this be some kind of interrupted error?
                return Error::success();

              if (I->isEHPad() || I->getType()->isTokenTy() ||
                  I->isSwiftError())
                continue;

              outs() << "Checking instruction: " << *I;
              std::unique_ptr<Module> M =
                  BD.deleteInstructionFromProgram(&*I, Simplification);

              // Find out if the pass still crashes on this pass...
              if (TestFn(BD, M.get())) {
                // Yup, it does, we delete the old module, and continue trying
                // to reduce the testcase...
                BD.setNewProgram(std::move(M));
                InstructionsToSkipBeforeDeleting = CurInstructionNum;
                goto TryAgain; // I wish I had a multi-level break here!
              }
            }
          }

    if (InstructionsToSkipBeforeDeleting) {
      InstructionsToSkipBeforeDeleting = 0;
      goto TryAgain;
    }

  } while (Simplification);
  BD.EmitProgressBitcode(BD.getProgram(), "reduced-instructions");
  return Error::success();
}

/// DebugACrash - Given a predicate that determines whether a component crashes
/// on a program, try to destructively reduce the program while still keeping
/// the predicate true.
static Error DebugACrash(BugDriver &BD, BugTester TestFn) {
  // See if we can get away with nuking some of the global variable initializers
  // in the program...
  if (!NoGlobalRM)
    if (Error E = ReduceGlobalInitializers(BD, TestFn))
      return E;

  // Now try to reduce the number of functions in the module to something small.
  std::vector<Function *> Functions;
  for (Function &F : BD.getProgram())
    if (!F.isDeclaration())
      Functions.push_back(&F);

  if (Functions.size() > 1 && !BugpointIsInterrupted) {
    outs() << "\n*** Attempting to reduce the number of functions "
              "in the testcase\n";

    unsigned OldSize = Functions.size();
    Expected<bool> Result =
        ReduceCrashingFunctions(BD, TestFn).reduceList(Functions);
    if (Error E = Result.takeError())
      return E;

    if (Functions.size() < OldSize)
      BD.EmitProgressBitcode(BD.getProgram(), "reduced-function");
  }

  // For each remaining function, try to reduce that function's attributes.
  std::vector<std::string> FunctionNames;
  for (Function &F : BD.getProgram())
    FunctionNames.push_back(F.getName());

  if (!FunctionNames.empty() && !BugpointIsInterrupted) {
    outs() << "\n*** Attempting to reduce the number of function attributes in "
              "the testcase\n";

    unsigned OldSize = 0;
    unsigned NewSize = 0;
    for (std::string &Name : FunctionNames) {
      Function *Fn = BD.getProgram().getFunction(Name);
      assert(Fn && "Could not find funcion?");

      std::vector<Attribute> Attrs;
      for (Attribute A : Fn->getAttributes().getFnAttributes())
        Attrs.push_back(A);

      OldSize += Attrs.size();
      Expected<bool> Result =
          ReduceCrashingFunctionAttributes(BD, Name, TestFn).reduceList(Attrs);
      if (Error E = Result.takeError())
        return E;

      NewSize += Attrs.size();
    }

    if (OldSize < NewSize)
      BD.EmitProgressBitcode(BD.getProgram(), "reduced-function-attributes");
  }

  // Attempt to change conditional branches into unconditional branches to
  // eliminate blocks.
  if (!DisableSimplifyCFG && !BugpointIsInterrupted) {
    std::vector<const BasicBlock *> Blocks;
    for (Function &F : BD.getProgram())
      for (BasicBlock &BB : F)
        Blocks.push_back(&BB);
    unsigned OldSize = Blocks.size();
    Expected<bool> Result =
        ReduceCrashingConditionals(BD, TestFn, true).reduceList(Blocks);
    if (Error E = Result.takeError())
      return E;
    Result = ReduceCrashingConditionals(BD, TestFn, false).reduceList(Blocks);
    if (Error E = Result.takeError())
      return E;
    if (Blocks.size() < OldSize)
      BD.EmitProgressBitcode(BD.getProgram(), "reduced-conditionals");
  }

  // Attempt to delete entire basic blocks at a time to speed up
  // convergence... this actually works by setting the terminator of the blocks
  // to a return instruction then running simplifycfg, which can potentially
  // shrinks the code dramatically quickly
  //
  if (!DisableSimplifyCFG && !BugpointIsInterrupted) {
    std::vector<const BasicBlock *> Blocks;
    for (Function &F : BD.getProgram())
      for (BasicBlock &BB : F)
        Blocks.push_back(&BB);
    unsigned OldSize = Blocks.size();
    Expected<bool> Result = ReduceCrashingBlocks(BD, TestFn).reduceList(Blocks);
    if (Error E = Result.takeError())
      return E;
    if (Blocks.size() < OldSize)
      BD.EmitProgressBitcode(BD.getProgram(), "reduced-blocks");
  }

  if (!DisableSimplifyCFG && !BugpointIsInterrupted) {
    std::vector<const BasicBlock *> Blocks;
    for (Function &F : BD.getProgram())
      for (BasicBlock &BB : F)
        Blocks.push_back(&BB);
    unsigned OldSize = Blocks.size();
    Expected<bool> Result = ReduceSimplifyCFG(BD, TestFn).reduceList(Blocks);
    if (Error E = Result.takeError())
      return E;
    if (Blocks.size() < OldSize)
      BD.EmitProgressBitcode(BD.getProgram(), "reduced-simplifycfg");
  }

  // Attempt to delete instructions using bisection. This should help out nasty
  // cases with large basic blocks where the problem is at one end.
  if (!BugpointIsInterrupted)
    if (Error E = ReduceInsts(BD, TestFn))
      return E;

  // Attempt to strip debug info metadata.
  auto stripMetadata = [&](std::function<bool(Module &)> strip) {
    std::unique_ptr<Module> M = CloneModule(BD.getProgram());
    strip(*M);
    if (TestFn(BD, M.get()))
      BD.setNewProgram(std::move(M));
  };
  if (!NoStripDebugInfo && !BugpointIsInterrupted) {
    outs() << "\n*** Attempting to strip the debug info: ";
    stripMetadata(StripDebugInfo);
  }
  if (!NoStripDebugTypeInfo && !BugpointIsInterrupted) {
    outs() << "\n*** Attempting to strip the debug type info: ";
    stripMetadata(stripNonLineTableDebugInfo);
  }

  if (!NoNamedMDRM) {
    if (!BugpointIsInterrupted) {
      // Try to reduce the amount of global metadata (particularly debug info),
      // by dropping global named metadata that anchors them
      outs() << "\n*** Attempting to remove named metadata: ";
      std::vector<std::string> NamedMDNames;
      for (auto &NamedMD : BD.getProgram().named_metadata())
        NamedMDNames.push_back(NamedMD.getName().str());
      Expected<bool> Result =
          ReduceCrashingNamedMD(BD, TestFn).reduceList(NamedMDNames);
      if (Error E = Result.takeError())
        return E;
    }

    if (!BugpointIsInterrupted) {
      // Now that we quickly dropped all the named metadata that doesn't
      // contribute to the crash, bisect the operands of the remaining ones
      std::vector<const MDNode *> NamedMDOps;
      for (auto &NamedMD : BD.getProgram().named_metadata())
        for (auto op : NamedMD.operands())
          NamedMDOps.push_back(op);
      Expected<bool> Result =
          ReduceCrashingNamedMDOps(BD, TestFn).reduceList(NamedMDOps);
      if (Error E = Result.takeError())
        return E;
    }
    BD.EmitProgressBitcode(BD.getProgram(), "reduced-named-md");
  }

  // Try to clean up the testcase by running funcresolve and globaldce...
  if (!BugpointIsInterrupted) {
    outs() << "\n*** Attempting to perform final cleanups: ";
    std::unique_ptr<Module> M = CloneModule(BD.getProgram());
    M = BD.performFinalCleanups(std::move(M), true);

    // Find out if the pass still crashes on the cleaned up program...
    if (M && TestFn(BD, M.get()))
      BD.setNewProgram(
          std::move(M)); // Yup, it does, keep the reduced version...
  }

  BD.EmitProgressBitcode(BD.getProgram(), "reduced-simplified");

  return Error::success();
}

static bool TestForOptimizerCrash(const BugDriver &BD, Module *M) {
  return BD.runPasses(*M, BD.getPassesToRun());
}

/// debugOptimizerCrash - This method is called when some pass crashes on input.
/// It attempts to prune down the testcase to something reasonable, and figure
/// out exactly which pass is crashing.
///
Error BugDriver::debugOptimizerCrash(const std::string &ID) {
  outs() << "\n*** Debugging optimizer crash!\n";

  // Reduce the list of passes which causes the optimizer to crash...
  if (!BugpointIsInterrupted && !DontReducePassList) {
    Expected<bool> Result = ReducePassList(*this).reduceList(PassesToRun);
    if (Error E = Result.takeError())
      return E;
  }

  outs() << "\n*** Found crashing pass"
         << (PassesToRun.size() == 1 ? ": " : "es: ")
         << getPassesString(PassesToRun) << '\n';

  EmitProgressBitcode(*Program, ID);

  return DebugACrash(*this, TestForOptimizerCrash);
}

static bool TestForCodeGenCrash(const BugDriver &BD, Module *M) {
  if (Error E = BD.compileProgram(*M)) {
    if (VerboseErrors)
      errs() << toString(std::move(E)) << "\n";
    else {
      consumeError(std::move(E));
      errs() << "<crash>\n";
    }
    return true; // Tool is still crashing.
  }
  errs() << '\n';
  return false;
}

/// debugCodeGeneratorCrash - This method is called when the code generator
/// crashes on an input.  It attempts to reduce the input as much as possible
/// while still causing the code generator to crash.
Error BugDriver::debugCodeGeneratorCrash() {
  errs() << "*** Debugging code generator crash!\n";

  return DebugACrash(*this, TestForCodeGenCrash);
}
