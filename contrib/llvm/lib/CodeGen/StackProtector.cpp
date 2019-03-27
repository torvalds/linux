//===- StackProtector.cpp - Stack Protector Insertion ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass inserts stack protectors into functions which need them. A variable
// with a random value in it is stored onto the stack before the local variables
// are allocated. Upon exiting the block, the stored value is checked. If it's
// changed, then there was some sort of violation and the program aborts.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/StackProtector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "stack-protector"

STATISTIC(NumFunProtected, "Number of functions protected");
STATISTIC(NumAddrTaken, "Number of local variables that have their address"
                        " taken.");

static cl::opt<bool> EnableSelectionDAGSP("enable-selectiondag-sp",
                                          cl::init(true), cl::Hidden);

char StackProtector::ID = 0;

INITIALIZE_PASS_BEGIN(StackProtector, DEBUG_TYPE,
                      "Insert stack protectors", false, true)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(StackProtector, DEBUG_TYPE,
                    "Insert stack protectors", false, true)

FunctionPass *llvm::createStackProtectorPass() { return new StackProtector(); }

void StackProtector::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.addPreserved<DominatorTreeWrapperPass>();
}

bool StackProtector::runOnFunction(Function &Fn) {
  F = &Fn;
  M = F->getParent();
  DominatorTreeWrapperPass *DTWP =
      getAnalysisIfAvailable<DominatorTreeWrapperPass>();
  DT = DTWP ? &DTWP->getDomTree() : nullptr;
  TM = &getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
  Trip = TM->getTargetTriple();
  TLI = TM->getSubtargetImpl(Fn)->getTargetLowering();
  HasPrologue = false;
  HasIRCheck = false;

  Attribute Attr = Fn.getFnAttribute("stack-protector-buffer-size");
  if (Attr.isStringAttribute() &&
      Attr.getValueAsString().getAsInteger(10, SSPBufferSize))
    return false; // Invalid integer string

  if (!RequiresStackProtector())
    return false;

  // TODO(etienneb): Functions with funclets are not correctly supported now.
  // Do nothing if this is funclet-based personality.
  if (Fn.hasPersonalityFn()) {
    EHPersonality Personality = classifyEHPersonality(Fn.getPersonalityFn());
    if (isFuncletEHPersonality(Personality))
      return false;
  }

  ++NumFunProtected;
  return InsertStackProtectors();
}

/// \param [out] IsLarge is set to true if a protectable array is found and
/// it is "large" ( >= ssp-buffer-size).  In the case of a structure with
/// multiple arrays, this gets set if any of them is large.
bool StackProtector::ContainsProtectableArray(Type *Ty, bool &IsLarge,
                                              bool Strong,
                                              bool InStruct) const {
  if (!Ty)
    return false;
  if (ArrayType *AT = dyn_cast<ArrayType>(Ty)) {
    if (!AT->getElementType()->isIntegerTy(8)) {
      // If we're on a non-Darwin platform or we're inside of a structure, don't
      // add stack protectors unless the array is a character array.
      // However, in strong mode any array, regardless of type and size,
      // triggers a protector.
      if (!Strong && (InStruct || !Trip.isOSDarwin()))
        return false;
    }

    // If an array has more than SSPBufferSize bytes of allocated space, then we
    // emit stack protectors.
    if (SSPBufferSize <= M->getDataLayout().getTypeAllocSize(AT)) {
      IsLarge = true;
      return true;
    }

    if (Strong)
      // Require a protector for all arrays in strong mode
      return true;
  }

  const StructType *ST = dyn_cast<StructType>(Ty);
  if (!ST)
    return false;

  bool NeedsProtector = false;
  for (StructType::element_iterator I = ST->element_begin(),
                                    E = ST->element_end();
       I != E; ++I)
    if (ContainsProtectableArray(*I, IsLarge, Strong, true)) {
      // If the element is a protectable array and is large (>= SSPBufferSize)
      // then we are done.  If the protectable array is not large, then
      // keep looking in case a subsequent element is a large array.
      if (IsLarge)
        return true;
      NeedsProtector = true;
    }

  return NeedsProtector;
}

bool StackProtector::HasAddressTaken(const Instruction *AI) {
  for (const User *U : AI->users()) {
    if (const StoreInst *SI = dyn_cast<StoreInst>(U)) {
      if (AI == SI->getValueOperand())
        return true;
    } else if (const PtrToIntInst *SI = dyn_cast<PtrToIntInst>(U)) {
      if (AI == SI->getOperand(0))
        return true;
    } else if (const CallInst *CI = dyn_cast<CallInst>(U)) {
      // Ignore intrinsics that are not calls. TODO: Use isLoweredToCall().
      if (!isa<DbgInfoIntrinsic>(CI) && !CI->isLifetimeStartOrEnd())
        return true;
    } else if (isa<InvokeInst>(U)) {
      return true;
    } else if (const SelectInst *SI = dyn_cast<SelectInst>(U)) {
      if (HasAddressTaken(SI))
        return true;
    } else if (const PHINode *PN = dyn_cast<PHINode>(U)) {
      // Keep track of what PHI nodes we have already visited to ensure
      // they are only visited once.
      if (VisitedPHIs.insert(PN).second)
        if (HasAddressTaken(PN))
          return true;
    } else if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(U)) {
      if (HasAddressTaken(GEP))
        return true;
    } else if (const BitCastInst *BI = dyn_cast<BitCastInst>(U)) {
      if (HasAddressTaken(BI))
        return true;
    }
  }
  return false;
}

/// Search for the first call to the llvm.stackprotector intrinsic and return it
/// if present.
static const CallInst *findStackProtectorIntrinsic(Function &F) {
  for (const BasicBlock &BB : F)
    for (const Instruction &I : BB)
      if (const CallInst *CI = dyn_cast<CallInst>(&I))
        if (CI->getCalledFunction() ==
            Intrinsic::getDeclaration(F.getParent(), Intrinsic::stackprotector))
          return CI;
  return nullptr;
}

/// Check whether or not this function needs a stack protector based
/// upon the stack protector level.
///
/// We use two heuristics: a standard (ssp) and strong (sspstrong).
/// The standard heuristic which will add a guard variable to functions that
/// call alloca with a either a variable size or a size >= SSPBufferSize,
/// functions with character buffers larger than SSPBufferSize, and functions
/// with aggregates containing character buffers larger than SSPBufferSize. The
/// strong heuristic will add a guard variables to functions that call alloca
/// regardless of size, functions with any buffer regardless of type and size,
/// functions with aggregates that contain any buffer regardless of type and
/// size, and functions that contain stack-based variables that have had their
/// address taken.
bool StackProtector::RequiresStackProtector() {
  bool Strong = false;
  bool NeedsProtector = false;
  HasPrologue = findStackProtectorIntrinsic(*F);

  if (F->hasFnAttribute(Attribute::SafeStack))
    return false;

  // We are constructing the OptimizationRemarkEmitter on the fly rather than
  // using the analysis pass to avoid building DominatorTree and LoopInfo which
  // are not available this late in the IR pipeline.
  OptimizationRemarkEmitter ORE(F);

  if (F->hasFnAttribute(Attribute::StackProtectReq)) {
    ORE.emit([&]() {
      return OptimizationRemark(DEBUG_TYPE, "StackProtectorRequested", F)
             << "Stack protection applied to function "
             << ore::NV("Function", F)
             << " due to a function attribute or command-line switch";
    });
    NeedsProtector = true;
    Strong = true; // Use the same heuristic as strong to determine SSPLayout
  } else if (F->hasFnAttribute(Attribute::StackProtectStrong))
    Strong = true;
  else if (HasPrologue)
    NeedsProtector = true;
  else if (!F->hasFnAttribute(Attribute::StackProtect))
    return false;

  for (const BasicBlock &BB : *F) {
    for (const Instruction &I : BB) {
      if (const AllocaInst *AI = dyn_cast<AllocaInst>(&I)) {
        if (AI->isArrayAllocation()) {
          auto RemarkBuilder = [&]() {
            return OptimizationRemark(DEBUG_TYPE, "StackProtectorAllocaOrArray",
                                      &I)
                   << "Stack protection applied to function "
                   << ore::NV("Function", F)
                   << " due to a call to alloca or use of a variable length "
                      "array";
          };
          if (const auto *CI = dyn_cast<ConstantInt>(AI->getArraySize())) {
            if (CI->getLimitedValue(SSPBufferSize) >= SSPBufferSize) {
              // A call to alloca with size >= SSPBufferSize requires
              // stack protectors.
              Layout.insert(std::make_pair(AI,
                                           MachineFrameInfo::SSPLK_LargeArray));
              ORE.emit(RemarkBuilder);
              NeedsProtector = true;
            } else if (Strong) {
              // Require protectors for all alloca calls in strong mode.
              Layout.insert(std::make_pair(AI,
                                           MachineFrameInfo::SSPLK_SmallArray));
              ORE.emit(RemarkBuilder);
              NeedsProtector = true;
            }
          } else {
            // A call to alloca with a variable size requires protectors.
            Layout.insert(std::make_pair(AI,
                                         MachineFrameInfo::SSPLK_LargeArray));
            ORE.emit(RemarkBuilder);
            NeedsProtector = true;
          }
          continue;
        }

        bool IsLarge = false;
        if (ContainsProtectableArray(AI->getAllocatedType(), IsLarge, Strong)) {
          Layout.insert(std::make_pair(AI, IsLarge
                                       ? MachineFrameInfo::SSPLK_LargeArray
                                       : MachineFrameInfo::SSPLK_SmallArray));
          ORE.emit([&]() {
            return OptimizationRemark(DEBUG_TYPE, "StackProtectorBuffer", &I)
                   << "Stack protection applied to function "
                   << ore::NV("Function", F)
                   << " due to a stack allocated buffer or struct containing a "
                      "buffer";
          });
          NeedsProtector = true;
          continue;
        }

        if (Strong && HasAddressTaken(AI)) {
          ++NumAddrTaken;
          Layout.insert(std::make_pair(AI, MachineFrameInfo::SSPLK_AddrOf));
          ORE.emit([&]() {
            return OptimizationRemark(DEBUG_TYPE, "StackProtectorAddressTaken",
                                      &I)
                   << "Stack protection applied to function "
                   << ore::NV("Function", F)
                   << " due to the address of a local variable being taken";
          });
          NeedsProtector = true;
        }
      }
    }
  }

  return NeedsProtector;
}

/// Create a stack guard loading and populate whether SelectionDAG SSP is
/// supported.
static Value *getStackGuard(const TargetLoweringBase *TLI, Module *M,
                            IRBuilder<> &B,
                            bool *SupportsSelectionDAGSP = nullptr) {
  if (Value *Guard = TLI->getIRStackGuard(B))
    return B.CreateLoad(Guard, true, "StackGuard");

  // Use SelectionDAG SSP handling, since there isn't an IR guard.
  //
  // This is more or less weird, since we optionally output whether we
  // should perform a SelectionDAG SP here. The reason is that it's strictly
  // defined as !TLI->getIRStackGuard(B), where getIRStackGuard is also
  // mutating. There is no way to get this bit without mutating the IR, so
  // getting this bit has to happen in this right time.
  //
  // We could have define a new function TLI::supportsSelectionDAGSP(), but that
  // will put more burden on the backends' overriding work, especially when it
  // actually conveys the same information getIRStackGuard() already gives.
  if (SupportsSelectionDAGSP)
    *SupportsSelectionDAGSP = true;
  TLI->insertSSPDeclarations(*M);
  return B.CreateCall(Intrinsic::getDeclaration(M, Intrinsic::stackguard));
}

/// Insert code into the entry block that stores the stack guard
/// variable onto the stack:
///
///   entry:
///     StackGuardSlot = alloca i8*
///     StackGuard = <stack guard>
///     call void @llvm.stackprotector(StackGuard, StackGuardSlot)
///
/// Returns true if the platform/triple supports the stackprotectorcreate pseudo
/// node.
static bool CreatePrologue(Function *F, Module *M, ReturnInst *RI,
                           const TargetLoweringBase *TLI, AllocaInst *&AI) {
  bool SupportsSelectionDAGSP = false;
  IRBuilder<> B(&F->getEntryBlock().front());
  PointerType *PtrTy = Type::getInt8PtrTy(RI->getContext());
  AI = B.CreateAlloca(PtrTy, nullptr, "StackGuardSlot");

  Value *GuardSlot = getStackGuard(TLI, M, B, &SupportsSelectionDAGSP);
  B.CreateCall(Intrinsic::getDeclaration(M, Intrinsic::stackprotector),
               {GuardSlot, AI});
  return SupportsSelectionDAGSP;
}

/// InsertStackProtectors - Insert code into the prologue and epilogue of the
/// function.
///
///  - The prologue code loads and stores the stack guard onto the stack.
///  - The epilogue checks the value stored in the prologue against the original
///    value. It calls __stack_chk_fail if they differ.
bool StackProtector::InsertStackProtectors() {
  // If the target wants to XOR the frame pointer into the guard value, it's
  // impossible to emit the check in IR, so the target *must* support stack
  // protection in SDAG.
  bool SupportsSelectionDAGSP =
      TLI->useStackGuardXorFP() ||
      (EnableSelectionDAGSP && !TM->Options.EnableFastISel &&
       !TM->Options.EnableGlobalISel);
  AllocaInst *AI = nullptr;       // Place on stack that stores the stack guard.

  for (Function::iterator I = F->begin(), E = F->end(); I != E;) {
    BasicBlock *BB = &*I++;
    ReturnInst *RI = dyn_cast<ReturnInst>(BB->getTerminator());
    if (!RI)
      continue;

    // Generate prologue instrumentation if not already generated.
    if (!HasPrologue) {
      HasPrologue = true;
      SupportsSelectionDAGSP &= CreatePrologue(F, M, RI, TLI, AI);
    }

    // SelectionDAG based code generation. Nothing else needs to be done here.
    // The epilogue instrumentation is postponed to SelectionDAG.
    if (SupportsSelectionDAGSP)
      break;

    // Find the stack guard slot if the prologue was not created by this pass
    // itself via a previous call to CreatePrologue().
    if (!AI) {
      const CallInst *SPCall = findStackProtectorIntrinsic(*F);
      assert(SPCall && "Call to llvm.stackprotector is missing");
      AI = cast<AllocaInst>(SPCall->getArgOperand(1));
    }

    // Set HasIRCheck to true, so that SelectionDAG will not generate its own
    // version. SelectionDAG called 'shouldEmitSDCheck' to check whether
    // instrumentation has already been generated.
    HasIRCheck = true;

    // Generate epilogue instrumentation. The epilogue intrumentation can be
    // function-based or inlined depending on which mechanism the target is
    // providing.
    if (Value* GuardCheck = TLI->getSSPStackGuardCheck(*M)) {
      // Generate the function-based epilogue instrumentation.
      // The target provides a guard check function, generate a call to it.
      IRBuilder<> B(RI);
      LoadInst *Guard = B.CreateLoad(AI, true, "Guard");
      CallInst *Call = B.CreateCall(GuardCheck, {Guard});
      llvm::Function *Function = cast<llvm::Function>(GuardCheck);
      Call->setAttributes(Function->getAttributes());
      Call->setCallingConv(Function->getCallingConv());
    } else {
      // Generate the epilogue with inline instrumentation.
      // If we do not support SelectionDAG based tail calls, generate IR level
      // tail calls.
      //
      // For each block with a return instruction, convert this:
      //
      //   return:
      //     ...
      //     ret ...
      //
      // into this:
      //
      //   return:
      //     ...
      //     %1 = <stack guard>
      //     %2 = load StackGuardSlot
      //     %3 = cmp i1 %1, %2
      //     br i1 %3, label %SP_return, label %CallStackCheckFailBlk
      //
      //   SP_return:
      //     ret ...
      //
      //   CallStackCheckFailBlk:
      //     call void @__stack_chk_fail()
      //     unreachable

      // Create the FailBB. We duplicate the BB every time since the MI tail
      // merge pass will merge together all of the various BB into one including
      // fail BB generated by the stack protector pseudo instruction.
      BasicBlock *FailBB = CreateFailBB();

      // Split the basic block before the return instruction.
      BasicBlock *NewBB = BB->splitBasicBlock(RI->getIterator(), "SP_return");

      // Update the dominator tree if we need to.
      if (DT && DT->isReachableFromEntry(BB)) {
        DT->addNewBlock(NewBB, BB);
        DT->addNewBlock(FailBB, BB);
      }

      // Remove default branch instruction to the new BB.
      BB->getTerminator()->eraseFromParent();

      // Move the newly created basic block to the point right after the old
      // basic block so that it's in the "fall through" position.
      NewBB->moveAfter(BB);

      // Generate the stack protector instructions in the old basic block.
      IRBuilder<> B(BB);
      Value *Guard = getStackGuard(TLI, M, B);
      LoadInst *LI2 = B.CreateLoad(AI, true);
      Value *Cmp = B.CreateICmpEQ(Guard, LI2);
      auto SuccessProb =
          BranchProbabilityInfo::getBranchProbStackProtector(true);
      auto FailureProb =
          BranchProbabilityInfo::getBranchProbStackProtector(false);
      MDNode *Weights = MDBuilder(F->getContext())
                            .createBranchWeights(SuccessProb.getNumerator(),
                                                 FailureProb.getNumerator());
      B.CreateCondBr(Cmp, NewBB, FailBB, Weights);
    }
  }

  // Return if we didn't modify any basic blocks. i.e., there are no return
  // statements in the function.
  return HasPrologue;
}

/// CreateFailBB - Create a basic block to jump to when the stack protector
/// check fails.
BasicBlock *StackProtector::CreateFailBB() {
  LLVMContext &Context = F->getContext();
  BasicBlock *FailBB = BasicBlock::Create(Context, "CallStackCheckFailBlk", F);
  IRBuilder<> B(FailBB);
  B.SetCurrentDebugLocation(DebugLoc::get(0, 0, F->getSubprogram()));
  if (Trip.isOSOpenBSD()) {
    Constant *StackChkFail =
        M->getOrInsertFunction("__stack_smash_handler",
                               Type::getVoidTy(Context),
                               Type::getInt8PtrTy(Context));

    B.CreateCall(StackChkFail, B.CreateGlobalStringPtr(F->getName(), "SSH"));
  } else {
    Constant *StackChkFail =
        M->getOrInsertFunction("__stack_chk_fail", Type::getVoidTy(Context));

    B.CreateCall(StackChkFail, {});
  }
  B.CreateUnreachable();
  return FailBB;
}

bool StackProtector::shouldEmitSDCheck(const BasicBlock &BB) const {
  return HasPrologue && !HasIRCheck && dyn_cast<ReturnInst>(BB.getTerminator());
}

void StackProtector::copyToMachineFrameInfo(MachineFrameInfo &MFI) const {
  if (Layout.empty())
    return;

  for (int I = 0, E = MFI.getObjectIndexEnd(); I != E; ++I) {
    if (MFI.isDeadObjectIndex(I))
      continue;

    const AllocaInst *AI = MFI.getObjectAllocation(I);
    if (!AI)
      continue;

    SSPLayoutMap::const_iterator LI = Layout.find(AI);
    if (LI == Layout.end())
      continue;

    MFI.setObjectSSPLayout(I, LI->second);
  }
}
