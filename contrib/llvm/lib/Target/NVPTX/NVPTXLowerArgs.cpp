//===-- NVPTXLowerArgs.cpp - Lower arguments ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
// Arguments to kernel and device functions are passed via param space,
// which imposes certain restrictions:
// http://docs.nvidia.com/cuda/parallel-thread-execution/#state-spaces
//
// Kernel parameters are read-only and accessible only via ld.param
// instruction, directly or via a pointer. Pointers to kernel
// arguments can't be converted to generic address space.
//
// Device function parameters are directly accessible via
// ld.param/st.param, but taking the address of one returns a pointer
// to a copy created in local space which *can't* be used with
// ld.param/st.param.
//
// Copying a byval struct into local memory in IR allows us to enforce
// the param space restrictions, gives the rest of IR a pointer w/o
// param space restrictions, and gives us an opportunity to eliminate
// the copy.
//
// Pointer arguments to kernel functions need more work to be lowered:
//
// 1. Convert non-byval pointer arguments of CUDA kernels to pointers in the
//    global address space. This allows later optimizations to emit
//    ld.global.*/st.global.* for accessing these pointer arguments. For
//    example,
//
//    define void @foo(float* %input) {
//      %v = load float, float* %input, align 4
//      ...
//    }
//
//    becomes
//
//    define void @foo(float* %input) {
//      %input2 = addrspacecast float* %input to float addrspace(1)*
//      %input3 = addrspacecast float addrspace(1)* %input2 to float*
//      %v = load float, float* %input3, align 4
//      ...
//    }
//
//    Later, NVPTXInferAddressSpaces will optimize it to
//
//    define void @foo(float* %input) {
//      %input2 = addrspacecast float* %input to float addrspace(1)*
//      %v = load float, float addrspace(1)* %input2, align 4
//      ...
//    }
//
// 2. Convert pointers in a byval kernel parameter to pointers in the global
//    address space. As #2, it allows NVPTX to emit more ld/st.global. E.g.,
//
//    struct S {
//      int *x;
//      int *y;
//    };
//    __global__ void foo(S s) {
//      int *b = s.y;
//      // use b
//    }
//
//    "b" points to the global address space. In the IR level,
//
//    define void @foo({i32*, i32*}* byval %input) {
//      %b_ptr = getelementptr {i32*, i32*}, {i32*, i32*}* %input, i64 0, i32 1
//      %b = load i32*, i32** %b_ptr
//      ; use %b
//    }
//
//    becomes
//
//    define void @foo({i32*, i32*}* byval %input) {
//      %b_ptr = getelementptr {i32*, i32*}, {i32*, i32*}* %input, i64 0, i32 1
//      %b = load i32*, i32** %b_ptr
//      %b_global = addrspacecast i32* %b to i32 addrspace(1)*
//      %b_generic = addrspacecast i32 addrspace(1)* %b_global to i32*
//      ; use %b_generic
//    }
//
// TODO: merge this pass with NVPTXInferAddressSpaces so that other passes don't
// cancel the addrspacecast pair this pass emits.
//===----------------------------------------------------------------------===//

#include "NVPTX.h"
#include "NVPTXTargetMachine.h"
#include "NVPTXUtilities.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace llvm {
void initializeNVPTXLowerArgsPass(PassRegistry &);
}

namespace {
class NVPTXLowerArgs : public FunctionPass {
  bool runOnFunction(Function &F) override;

  bool runOnKernelFunction(Function &F);
  bool runOnDeviceFunction(Function &F);

  // handle byval parameters
  void handleByValParam(Argument *Arg);
  // Knowing Ptr must point to the global address space, this function
  // addrspacecasts Ptr to global and then back to generic. This allows
  // NVPTXInferAddressSpaces to fold the global-to-generic cast into
  // loads/stores that appear later.
  void markPointerAsGlobal(Value *Ptr);

public:
  static char ID; // Pass identification, replacement for typeid
  NVPTXLowerArgs(const NVPTXTargetMachine *TM = nullptr)
      : FunctionPass(ID), TM(TM) {}
  StringRef getPassName() const override {
    return "Lower pointer arguments of CUDA kernels";
  }

private:
  const NVPTXTargetMachine *TM;
};
} // namespace

char NVPTXLowerArgs::ID = 1;

INITIALIZE_PASS(NVPTXLowerArgs, "nvptx-lower-args",
                "Lower arguments (NVPTX)", false, false)

// =============================================================================
// If the function had a byval struct ptr arg, say foo(%struct.x* byval %d),
// then add the following instructions to the first basic block:
//
// %temp = alloca %struct.x, align 8
// %tempd = addrspacecast %struct.x* %d to %struct.x addrspace(101)*
// %tv = load %struct.x addrspace(101)* %tempd
// store %struct.x %tv, %struct.x* %temp, align 8
//
// The above code allocates some space in the stack and copies the incoming
// struct from param space to local space.
// Then replace all occurrences of %d by %temp.
// =============================================================================
void NVPTXLowerArgs::handleByValParam(Argument *Arg) {
  Function *Func = Arg->getParent();
  Instruction *FirstInst = &(Func->getEntryBlock().front());
  PointerType *PType = dyn_cast<PointerType>(Arg->getType());

  assert(PType && "Expecting pointer type in handleByValParam");

  Type *StructType = PType->getElementType();
  unsigned AS = Func->getParent()->getDataLayout().getAllocaAddrSpace();
  AllocaInst *AllocA = new AllocaInst(StructType, AS, Arg->getName(), FirstInst);
  // Set the alignment to alignment of the byval parameter. This is because,
  // later load/stores assume that alignment, and we are going to replace
  // the use of the byval parameter with this alloca instruction.
  AllocA->setAlignment(Func->getParamAlignment(Arg->getArgNo()));
  Arg->replaceAllUsesWith(AllocA);

  Value *ArgInParam = new AddrSpaceCastInst(
      Arg, PointerType::get(StructType, ADDRESS_SPACE_PARAM), Arg->getName(),
      FirstInst);
  LoadInst *LI = new LoadInst(ArgInParam, Arg->getName(), FirstInst);
  new StoreInst(LI, AllocA, FirstInst);
}

void NVPTXLowerArgs::markPointerAsGlobal(Value *Ptr) {
  if (Ptr->getType()->getPointerAddressSpace() == ADDRESS_SPACE_GLOBAL)
    return;

  // Deciding where to emit the addrspacecast pair.
  BasicBlock::iterator InsertPt;
  if (Argument *Arg = dyn_cast<Argument>(Ptr)) {
    // Insert at the functon entry if Ptr is an argument.
    InsertPt = Arg->getParent()->getEntryBlock().begin();
  } else {
    // Insert right after Ptr if Ptr is an instruction.
    InsertPt = ++cast<Instruction>(Ptr)->getIterator();
    assert(InsertPt != InsertPt->getParent()->end() &&
           "We don't call this function with Ptr being a terminator.");
  }

  Instruction *PtrInGlobal = new AddrSpaceCastInst(
      Ptr, PointerType::get(Ptr->getType()->getPointerElementType(),
                            ADDRESS_SPACE_GLOBAL),
      Ptr->getName(), &*InsertPt);
  Value *PtrInGeneric = new AddrSpaceCastInst(PtrInGlobal, Ptr->getType(),
                                              Ptr->getName(), &*InsertPt);
  // Replace with PtrInGeneric all uses of Ptr except PtrInGlobal.
  Ptr->replaceAllUsesWith(PtrInGeneric);
  PtrInGlobal->setOperand(0, Ptr);
}

// =============================================================================
// Main function for this pass.
// =============================================================================
bool NVPTXLowerArgs::runOnKernelFunction(Function &F) {
  if (TM && TM->getDrvInterface() == NVPTX::CUDA) {
    // Mark pointers in byval structs as global.
    for (auto &B : F) {
      for (auto &I : B) {
        if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
          if (LI->getType()->isPointerTy()) {
            Value *UO = GetUnderlyingObject(LI->getPointerOperand(),
                                            F.getParent()->getDataLayout());
            if (Argument *Arg = dyn_cast<Argument>(UO)) {
              if (Arg->hasByValAttr()) {
                // LI is a load from a pointer within a byval kernel parameter.
                markPointerAsGlobal(LI);
              }
            }
          }
        }
      }
    }
  }

  for (Argument &Arg : F.args()) {
    if (Arg.getType()->isPointerTy()) {
      if (Arg.hasByValAttr())
        handleByValParam(&Arg);
      else if (TM && TM->getDrvInterface() == NVPTX::CUDA)
        markPointerAsGlobal(&Arg);
    }
  }
  return true;
}

// Device functions only need to copy byval args into local memory.
bool NVPTXLowerArgs::runOnDeviceFunction(Function &F) {
  for (Argument &Arg : F.args())
    if (Arg.getType()->isPointerTy() && Arg.hasByValAttr())
      handleByValParam(&Arg);
  return true;
}

bool NVPTXLowerArgs::runOnFunction(Function &F) {
  return isKernelFunction(F) ? runOnKernelFunction(F) : runOnDeviceFunction(F);
}

FunctionPass *
llvm::createNVPTXLowerArgsPass(const NVPTXTargetMachine *TM) {
  return new NVPTXLowerArgs(TM);
}
