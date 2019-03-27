//===- ExtractFunction.cpp - Extract a function from Program --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements several methods that are used to extract functions,
// loops, or portions of a module from the rest of the module.
//
//===----------------------------------------------------------------------===//

#include "BugDriver.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include <set>
using namespace llvm;

#define DEBUG_TYPE "bugpoint"

namespace llvm {
bool DisableSimplifyCFG = false;
extern cl::opt<std::string> OutputPrefix;
} // End llvm namespace

namespace {
cl::opt<bool> NoDCE("disable-dce",
                    cl::desc("Do not use the -dce pass to reduce testcases"));
cl::opt<bool, true>
    NoSCFG("disable-simplifycfg", cl::location(DisableSimplifyCFG),
           cl::desc("Do not use the -simplifycfg pass to reduce testcases"));

Function *globalInitUsesExternalBA(GlobalVariable *GV) {
  if (!GV->hasInitializer())
    return nullptr;

  Constant *I = GV->getInitializer();

  // walk the values used by the initializer
  // (and recurse into things like ConstantExpr)
  std::vector<Constant *> Todo;
  std::set<Constant *> Done;
  Todo.push_back(I);

  while (!Todo.empty()) {
    Constant *V = Todo.back();
    Todo.pop_back();
    Done.insert(V);

    if (BlockAddress *BA = dyn_cast<BlockAddress>(V)) {
      Function *F = BA->getFunction();
      if (F->isDeclaration())
        return F;
    }

    for (User::op_iterator i = V->op_begin(), e = V->op_end(); i != e; ++i) {
      Constant *C = dyn_cast<Constant>(*i);
      if (C && !isa<GlobalValue>(C) && !Done.count(C))
        Todo.push_back(C);
    }
  }
  return nullptr;
}
} // end anonymous namespace

std::unique_ptr<Module>
BugDriver::deleteInstructionFromProgram(const Instruction *I,
                                        unsigned Simplification) {
  // FIXME, use vmap?
  std::unique_ptr<Module> Clone = CloneModule(*Program);

  const BasicBlock *PBB = I->getParent();
  const Function *PF = PBB->getParent();

  Module::iterator RFI = Clone->begin(); // Get iterator to corresponding fn
  std::advance(
      RFI, std::distance(PF->getParent()->begin(), Module::const_iterator(PF)));

  Function::iterator RBI = RFI->begin(); // Get iterator to corresponding BB
  std::advance(RBI, std::distance(PF->begin(), Function::const_iterator(PBB)));

  BasicBlock::iterator RI = RBI->begin(); // Get iterator to corresponding inst
  std::advance(RI, std::distance(PBB->begin(), BasicBlock::const_iterator(I)));
  Instruction *TheInst = &*RI; // Got the corresponding instruction!

  // If this instruction produces a value, replace any users with null values
  if (!TheInst->getType()->isVoidTy())
    TheInst->replaceAllUsesWith(Constant::getNullValue(TheInst->getType()));

  // Remove the instruction from the program.
  TheInst->getParent()->getInstList().erase(TheInst);

  // Spiff up the output a little bit.
  std::vector<std::string> Passes;

  /// Can we get rid of the -disable-* options?
  if (Simplification > 1 && !NoDCE)
    Passes.push_back("dce");
  if (Simplification && !DisableSimplifyCFG)
    Passes.push_back("simplifycfg"); // Delete dead control flow

  Passes.push_back("verify");
  std::unique_ptr<Module> New = runPassesOn(Clone.get(), Passes);
  if (!New) {
    errs() << "Instruction removal failed.  Sorry. :(  Please report a bug!\n";
    exit(1);
  }
  return New;
}

std::unique_ptr<Module>
BugDriver::performFinalCleanups(std::unique_ptr<Module> M,
                                bool MayModifySemantics) {
  // Make all functions external, so GlobalDCE doesn't delete them...
  for (Module::iterator I = M->begin(), E = M->end(); I != E; ++I)
    I->setLinkage(GlobalValue::ExternalLinkage);

  std::vector<std::string> CleanupPasses;
  CleanupPasses.push_back("globaldce");

  if (MayModifySemantics)
    CleanupPasses.push_back("deadarghaX0r");
  else
    CleanupPasses.push_back("deadargelim");

  std::unique_ptr<Module> New = runPassesOn(M.get(), CleanupPasses);
  if (!New) {
    errs() << "Final cleanups failed.  Sorry. :(  Please report a bug!\n";
    return nullptr;
  }
  return New;
}

std::unique_ptr<Module> BugDriver::extractLoop(Module *M) {
  std::vector<std::string> LoopExtractPasses;
  LoopExtractPasses.push_back("loop-extract-single");

  std::unique_ptr<Module> NewM = runPassesOn(M, LoopExtractPasses);
  if (!NewM) {
    outs() << "*** Loop extraction failed: ";
    EmitProgressBitcode(*M, "loopextraction", true);
    outs() << "*** Sorry. :(  Please report a bug!\n";
    return nullptr;
  }

  // Check to see if we created any new functions.  If not, no loops were
  // extracted and we should return null.  Limit the number of loops we extract
  // to avoid taking forever.
  static unsigned NumExtracted = 32;
  if (M->size() == NewM->size() || --NumExtracted == 0) {
    return nullptr;
  } else {
    assert(M->size() < NewM->size() && "Loop extract removed functions?");
    Module::iterator MI = NewM->begin();
    for (unsigned i = 0, e = M->size(); i != e; ++i)
      ++MI;
  }

  return NewM;
}

static void eliminateAliases(GlobalValue *GV) {
  // First, check whether a GlobalAlias references this definition.
  // GlobalAlias MAY NOT reference declarations.
  for (;;) {
    // 1. Find aliases
    SmallVector<GlobalAlias *, 1> aliases;
    Module *M = GV->getParent();
    for (Module::alias_iterator I = M->alias_begin(), E = M->alias_end();
         I != E; ++I)
      if (I->getAliasee()->stripPointerCasts() == GV)
        aliases.push_back(&*I);
    if (aliases.empty())
      break;
    // 2. Resolve aliases
    for (unsigned i = 0, e = aliases.size(); i < e; ++i) {
      aliases[i]->replaceAllUsesWith(aliases[i]->getAliasee());
      aliases[i]->eraseFromParent();
    }
    // 3. Repeat until no more aliases found; there might
    // be an alias to an alias...
  }
}

//
// DeleteGlobalInitializer - "Remove" the global variable by deleting its
// initializer,
// making it external.
//
void llvm::DeleteGlobalInitializer(GlobalVariable *GV) {
  eliminateAliases(GV);
  GV->setInitializer(nullptr);
  GV->setComdat(nullptr);
}

// DeleteFunctionBody - "Remove" the function by deleting all of its basic
// blocks, making it external.
//
void llvm::DeleteFunctionBody(Function *F) {
  eliminateAliases(F);
  // Function declarations can't have comdats.
  F->setComdat(nullptr);

  // delete the body of the function...
  F->deleteBody();
  assert(F->isDeclaration() && "This didn't make the function external!");
}

/// GetTorInit - Given a list of entries for static ctors/dtors, return them
/// as a constant array.
static Constant *GetTorInit(std::vector<std::pair<Function *, int>> &TorList) {
  assert(!TorList.empty() && "Don't create empty tor list!");
  std::vector<Constant *> ArrayElts;
  Type *Int32Ty = Type::getInt32Ty(TorList[0].first->getContext());

  StructType *STy = StructType::get(Int32Ty, TorList[0].first->getType());
  for (unsigned i = 0, e = TorList.size(); i != e; ++i) {
    Constant *Elts[] = {ConstantInt::get(Int32Ty, TorList[i].second),
                        TorList[i].first};
    ArrayElts.push_back(ConstantStruct::get(STy, Elts));
  }
  return ConstantArray::get(
      ArrayType::get(ArrayElts[0]->getType(), ArrayElts.size()), ArrayElts);
}

/// SplitStaticCtorDtor - A module was recently split into two parts, M1/M2, and
/// M1 has all of the global variables.  If M2 contains any functions that are
/// static ctors/dtors, we need to add an llvm.global_[cd]tors global to M2, and
/// prune appropriate entries out of M1s list.
static void SplitStaticCtorDtor(const char *GlobalName, Module *M1, Module *M2,
                                ValueToValueMapTy &VMap) {
  GlobalVariable *GV = M1->getNamedGlobal(GlobalName);
  if (!GV || GV->isDeclaration() || GV->hasLocalLinkage() || !GV->use_empty())
    return;

  std::vector<std::pair<Function *, int>> M1Tors, M2Tors;
  ConstantArray *InitList = dyn_cast<ConstantArray>(GV->getInitializer());
  if (!InitList)
    return;

  for (unsigned i = 0, e = InitList->getNumOperands(); i != e; ++i) {
    if (ConstantStruct *CS =
            dyn_cast<ConstantStruct>(InitList->getOperand(i))) {
      if (CS->getNumOperands() != 2)
        return; // Not array of 2-element structs.

      if (CS->getOperand(1)->isNullValue())
        break; // Found a null terminator, stop here.

      ConstantInt *CI = dyn_cast<ConstantInt>(CS->getOperand(0));
      int Priority = CI ? CI->getSExtValue() : 0;

      Constant *FP = CS->getOperand(1);
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(FP))
        if (CE->isCast())
          FP = CE->getOperand(0);
      if (Function *F = dyn_cast<Function>(FP)) {
        if (!F->isDeclaration())
          M1Tors.push_back(std::make_pair(F, Priority));
        else {
          // Map to M2's version of the function.
          F = cast<Function>(VMap[F]);
          M2Tors.push_back(std::make_pair(F, Priority));
        }
      }
    }
  }

  GV->eraseFromParent();
  if (!M1Tors.empty()) {
    Constant *M1Init = GetTorInit(M1Tors);
    new GlobalVariable(*M1, M1Init->getType(), false,
                       GlobalValue::AppendingLinkage, M1Init, GlobalName);
  }

  GV = M2->getNamedGlobal(GlobalName);
  assert(GV && "Not a clone of M1?");
  assert(GV->use_empty() && "llvm.ctors shouldn't have uses!");

  GV->eraseFromParent();
  if (!M2Tors.empty()) {
    Constant *M2Init = GetTorInit(M2Tors);
    new GlobalVariable(*M2, M2Init->getType(), false,
                       GlobalValue::AppendingLinkage, M2Init, GlobalName);
  }
}

std::unique_ptr<Module>
llvm::SplitFunctionsOutOfModule(Module *M, const std::vector<Function *> &F,
                                ValueToValueMapTy &VMap) {
  // Make sure functions & globals are all external so that linkage
  // between the two modules will work.
  for (Module::iterator I = M->begin(), E = M->end(); I != E; ++I)
    I->setLinkage(GlobalValue::ExternalLinkage);
  for (Module::global_iterator I = M->global_begin(), E = M->global_end();
       I != E; ++I) {
    if (I->hasName() && I->getName()[0] == '\01')
      I->setName(I->getName().substr(1));
    I->setLinkage(GlobalValue::ExternalLinkage);
  }

  ValueToValueMapTy NewVMap;
  std::unique_ptr<Module> New = CloneModule(*M, NewVMap);

  // Remove the Test functions from the Safe module
  std::set<Function *> TestFunctions;
  for (unsigned i = 0, e = F.size(); i != e; ++i) {
    Function *TNOF = cast<Function>(VMap[F[i]]);
    LLVM_DEBUG(errs() << "Removing function ");
    LLVM_DEBUG(TNOF->printAsOperand(errs(), false));
    LLVM_DEBUG(errs() << "\n");
    TestFunctions.insert(cast<Function>(NewVMap[TNOF]));
    DeleteFunctionBody(TNOF); // Function is now external in this module!
  }

  // Remove the Safe functions from the Test module
  for (Function &I : *New)
    if (!TestFunctions.count(&I))
      DeleteFunctionBody(&I);

  // Try to split the global initializers evenly
  for (GlobalVariable &I : M->globals()) {
    GlobalVariable *GV = cast<GlobalVariable>(NewVMap[&I]);
    if (Function *TestFn = globalInitUsesExternalBA(&I)) {
      if (Function *SafeFn = globalInitUsesExternalBA(GV)) {
        errs() << "*** Error: when reducing functions, encountered "
                  "the global '";
        GV->printAsOperand(errs(), false);
        errs() << "' with an initializer that references blockaddresses "
                  "from safe function '"
               << SafeFn->getName() << "' and from test function '"
               << TestFn->getName() << "'.\n";
        exit(1);
      }
      DeleteGlobalInitializer(&I); // Delete the initializer to make it external
    } else {
      // If we keep it in the safe module, then delete it in the test module
      DeleteGlobalInitializer(GV);
    }
  }

  // Make sure that there is a global ctor/dtor array in both halves of the
  // module if they both have static ctor/dtor functions.
  SplitStaticCtorDtor("llvm.global_ctors", M, New.get(), NewVMap);
  SplitStaticCtorDtor("llvm.global_dtors", M, New.get(), NewVMap);

  return New;
}

//===----------------------------------------------------------------------===//
// Basic Block Extraction Code
//===----------------------------------------------------------------------===//

std::unique_ptr<Module>
BugDriver::extractMappedBlocksFromModule(const std::vector<BasicBlock *> &BBs,
                                         Module *M) {
  auto Temp = sys::fs::TempFile::create(OutputPrefix + "-extractblocks%%%%%%%");
  if (!Temp) {
    outs() << "*** Basic Block extraction failed!\n";
    errs() << "Error creating temporary file: " << toString(Temp.takeError())
           << "\n";
    EmitProgressBitcode(*M, "basicblockextractfail", true);
    return nullptr;
  }
  DiscardTemp Discard{*Temp};

  // Extract all of the blocks except the ones in BBs.
  SmallVector<BasicBlock *, 32> BlocksToExtract;
  for (Function &F : *M)
    for (BasicBlock &BB : F)
      // Check if this block is going to be extracted.
      if (std::find(BBs.begin(), BBs.end(), &BB) == BBs.end())
        BlocksToExtract.push_back(&BB);

  raw_fd_ostream OS(Temp->FD, /*shouldClose*/ false);
  for (BasicBlock *BB : BBs) {
    // If the BB doesn't have a name, give it one so we have something to key
    // off of.
    if (!BB->hasName())
      BB->setName("tmpbb");
    OS << BB->getParent()->getName() << " " << BB->getName() << "\n";
  }
  OS.flush();
  if (OS.has_error()) {
    errs() << "Error writing list of blocks to not extract\n";
    EmitProgressBitcode(*M, "basicblockextractfail", true);
    OS.clear_error();
    return nullptr;
  }

  std::string uniqueFN = "--extract-blocks-file=";
  uniqueFN += Temp->TmpName;
  const char *ExtraArg = uniqueFN.c_str();

  std::vector<std::string> PI;
  PI.push_back("extract-blocks");
  std::unique_ptr<Module> Ret = runPassesOn(M, PI, 1, &ExtraArg);

  if (!Ret) {
    outs() << "*** Basic Block extraction failed, please report a bug!\n";
    EmitProgressBitcode(*M, "basicblockextractfail", true);
  }
  return Ret;
}
