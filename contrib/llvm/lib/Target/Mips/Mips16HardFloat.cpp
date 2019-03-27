//===- Mips16HardFloat.cpp for Mips16 Hard Float --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass needed for Mips16 Hard Float
//
//===----------------------------------------------------------------------===//

#include "MipsTargetMachine.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "mips16-hard-float"

namespace {

  class Mips16HardFloat : public ModulePass {
  public:
    static char ID;

    Mips16HardFloat() : ModulePass(ID) {}

    StringRef getPassName() const override { return "MIPS16 Hard Float Pass"; }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<TargetPassConfig>();
      ModulePass::getAnalysisUsage(AU);
    }

    bool runOnModule(Module &M) override;
  };

} // end anonymous namespace

static void EmitInlineAsm(LLVMContext &C, BasicBlock *BB, StringRef AsmText) {
  std::vector<Type *> AsmArgTypes;
  std::vector<Value *> AsmArgs;

  FunctionType *AsmFTy =
      FunctionType::get(Type::getVoidTy(C), AsmArgTypes, false);
  InlineAsm *IA = InlineAsm::get(AsmFTy, AsmText, "", true,
                                 /* IsAlignStack */ false, InlineAsm::AD_ATT);
  CallInst::Create(IA, AsmArgs, "", BB);
}

char Mips16HardFloat::ID = 0;

//
// Return types that matter for hard float are:
// float, double, complex float, and complex double
//
enum FPReturnVariant {
  FRet, DRet, CFRet, CDRet, NoFPRet
};

//
// Determine which FP return type this function has
//
static FPReturnVariant whichFPReturnVariant(Type *T) {
  switch (T->getTypeID()) {
  case Type::FloatTyID:
    return FRet;
  case Type::DoubleTyID:
    return DRet;
  case Type::StructTyID: {
    StructType *ST = cast<StructType>(T);
    if (ST->getNumElements() != 2)
      break;
    if ((ST->getElementType(0)->isFloatTy()) &&
        (ST->getElementType(1)->isFloatTy()))
      return CFRet;
    if ((ST->getElementType(0)->isDoubleTy()) &&
        (ST->getElementType(1)->isDoubleTy()))
      return CDRet;
    break;
  }
  default:
    break;
  }
  return NoFPRet;
}

// Parameter type that matter are float, (float, float), (float, double),
// double, (double, double), (double, float)
enum FPParamVariant {
  FSig, FFSig, FDSig,
  DSig, DDSig, DFSig, NoSig
};

// which floating point parameter signature variant we are dealing with
using TypeID = Type::TypeID;
const Type::TypeID FloatTyID = Type::FloatTyID;
const Type::TypeID DoubleTyID = Type::DoubleTyID;

static FPParamVariant whichFPParamVariantNeeded(Function &F) {
  switch (F.arg_size()) {
  case 0:
    return NoSig;
  case 1:{
    TypeID ArgTypeID = F.getFunctionType()->getParamType(0)->getTypeID();
    switch (ArgTypeID) {
    case FloatTyID:
      return FSig;
    case DoubleTyID:
      return DSig;
    default:
      return NoSig;
    }
  }
  default: {
    TypeID ArgTypeID0 = F.getFunctionType()->getParamType(0)->getTypeID();
    TypeID ArgTypeID1 = F.getFunctionType()->getParamType(1)->getTypeID();
    switch(ArgTypeID0) {
    case FloatTyID: {
      switch (ArgTypeID1) {
      case FloatTyID:
        return FFSig;
      case DoubleTyID:
        return FDSig;
      default:
        return FSig;
      }
    }
    case DoubleTyID: {
      switch (ArgTypeID1) {
      case FloatTyID:
        return DFSig;
      case DoubleTyID:
        return DDSig;
      default:
        return DSig;
      }
    }
    default:
      return NoSig;
    }
  }
  }
  llvm_unreachable("can't get here");
}

// Figure out if we need float point based on the function parameters.
// We need to move variables in and/or out of floating point
// registers because of the ABI
static bool needsFPStubFromParams(Function &F) {
  if (F.arg_size() >=1) {
    Type *ArgType = F.getFunctionType()->getParamType(0);
    switch (ArgType->getTypeID()) {
    case Type::FloatTyID:
    case Type::DoubleTyID:
      return true;
    default:
      break;
    }
  }
  return false;
}

static bool needsFPReturnHelper(Function &F) {
  Type* RetType = F.getReturnType();
  return whichFPReturnVariant(RetType) != NoFPRet;
}

static bool needsFPReturnHelper(FunctionType &FT) {
  Type* RetType = FT.getReturnType();
  return whichFPReturnVariant(RetType) != NoFPRet;
}

static bool needsFPHelperFromSig(Function &F) {
  return needsFPStubFromParams(F) || needsFPReturnHelper(F);
}

// We swap between FP and Integer registers to allow Mips16 and Mips32 to
// interoperate
static std::string swapFPIntParams(FPParamVariant PV, Module *M, bool LE,
                                   bool ToFP) {
  std::string MI = ToFP ? "mtc1 ": "mfc1 ";
  std::string AsmText;

  switch (PV) {
  case FSig:
    AsmText += MI + "$$4, $$f12\n";
    break;

  case FFSig:
    AsmText += MI + "$$4, $$f12\n";
    AsmText += MI + "$$5, $$f14\n";
    break;

  case FDSig:
    AsmText += MI + "$$4, $$f12\n";
    if (LE) {
      AsmText += MI + "$$6, $$f14\n";
      AsmText += MI + "$$7, $$f15\n";
    } else {
      AsmText += MI + "$$7, $$f14\n";
      AsmText += MI + "$$6, $$f15\n";
    }
    break;

  case DSig:
    if (LE) {
      AsmText += MI + "$$4, $$f12\n";
      AsmText += MI + "$$5, $$f13\n";
    } else {
      AsmText += MI + "$$5, $$f12\n";
      AsmText += MI + "$$4, $$f13\n";
    }
    break;

  case DDSig:
    if (LE) {
      AsmText += MI + "$$4, $$f12\n";
      AsmText += MI + "$$5, $$f13\n";
      AsmText += MI + "$$6, $$f14\n";
      AsmText += MI + "$$7, $$f15\n";
    } else {
      AsmText += MI + "$$5, $$f12\n";
      AsmText += MI + "$$4, $$f13\n";
      AsmText += MI + "$$7, $$f14\n";
      AsmText += MI + "$$6, $$f15\n";
    }
    break;

  case DFSig:
    if (LE) {
      AsmText += MI + "$$4, $$f12\n";
      AsmText += MI + "$$5, $$f13\n";
    } else {
      AsmText += MI + "$$5, $$f12\n";
      AsmText += MI + "$$4, $$f13\n";
    }
    AsmText += MI + "$$6, $$f14\n";
    break;

  case NoSig:
    break;
  }

  return AsmText;
}

// Make sure that we know we already need a stub for this function.
// Having called needsFPHelperFromSig
static void assureFPCallStub(Function &F, Module *M,
                             const MipsTargetMachine &TM) {
  // for now we only need them for static relocation
  if (TM.isPositionIndependent())
    return;
  LLVMContext &Context = M->getContext();
  bool LE = TM.isLittleEndian();
  std::string Name = F.getName();
  std::string SectionName = ".mips16.call.fp." + Name;
  std::string StubName = "__call_stub_fp_" + Name;
  //
  // see if we already have the stub
  //
  Function *FStub = M->getFunction(StubName);
  if (FStub && !FStub->isDeclaration()) return;
  FStub = Function::Create(F.getFunctionType(),
                           Function::InternalLinkage, StubName, M);
  FStub->addFnAttr("mips16_fp_stub");
  FStub->addFnAttr(Attribute::Naked);
  FStub->addFnAttr(Attribute::NoInline);
  FStub->addFnAttr(Attribute::NoUnwind);
  FStub->addFnAttr("nomips16");
  FStub->setSection(SectionName);
  BasicBlock *BB = BasicBlock::Create(Context, "entry", FStub);
  FPReturnVariant RV = whichFPReturnVariant(FStub->getReturnType());
  FPParamVariant PV = whichFPParamVariantNeeded(F);

  std::string AsmText;
  AsmText += ".set reorder\n";
  AsmText += swapFPIntParams(PV, M, LE, true);
  if (RV != NoFPRet) {
    AsmText += "move $$18, $$31\n";
    AsmText += "jal " + Name + "\n";
  } else {
    AsmText += "lui  $$25, %hi(" + Name + ")\n";
    AsmText += "addiu  $$25, $$25, %lo(" + Name + ")\n";
  }

  switch (RV) {
  case FRet:
    AsmText += "mfc1 $$2, $$f0\n";
    break;

  case DRet:
    if (LE) {
      AsmText += "mfc1 $$2, $$f0\n";
      AsmText += "mfc1 $$3, $$f1\n";
    } else {
      AsmText += "mfc1 $$3, $$f0\n";
      AsmText += "mfc1 $$2, $$f1\n";
    }
    break;

  case CFRet:
    if (LE) {
      AsmText += "mfc1 $$2, $$f0\n";
      AsmText += "mfc1 $$3, $$f2\n";
    } else {
      AsmText += "mfc1 $$3, $$f0\n";
      AsmText += "mfc1 $$3, $$f2\n";
    }
    break;

  case CDRet:
    if (LE) {
      AsmText += "mfc1 $$4, $$f2\n";
      AsmText += "mfc1 $$5, $$f3\n";
      AsmText += "mfc1 $$2, $$f0\n";
      AsmText += "mfc1 $$3, $$f1\n";

    } else {
      AsmText += "mfc1 $$5, $$f2\n";
      AsmText += "mfc1 $$4, $$f3\n";
      AsmText += "mfc1 $$3, $$f0\n";
      AsmText += "mfc1 $$2, $$f1\n";
    }
    break;

  case NoFPRet:
    break;
  }

  if (RV != NoFPRet)
    AsmText += "jr $$18\n";
  else
    AsmText += "jr $$25\n";
  EmitInlineAsm(Context, BB, AsmText);

  new UnreachableInst(Context, BB);
}

// Functions that are llvm intrinsics and don't need helpers.
static const char *const IntrinsicInline[] = {
  "fabs", "fabsf",
  "llvm.ceil.f32", "llvm.ceil.f64",
  "llvm.copysign.f32", "llvm.copysign.f64",
  "llvm.cos.f32", "llvm.cos.f64",
  "llvm.exp.f32", "llvm.exp.f64",
  "llvm.exp2.f32", "llvm.exp2.f64",
  "llvm.fabs.f32", "llvm.fabs.f64",
  "llvm.floor.f32", "llvm.floor.f64",
  "llvm.fma.f32", "llvm.fma.f64",
  "llvm.log.f32", "llvm.log.f64",
  "llvm.log10.f32", "llvm.log10.f64",
  "llvm.nearbyint.f32", "llvm.nearbyint.f64",
  "llvm.pow.f32", "llvm.pow.f64",
  "llvm.powi.f32", "llvm.powi.f64",
  "llvm.rint.f32", "llvm.rint.f64",
  "llvm.round.f32", "llvm.round.f64",
  "llvm.sin.f32", "llvm.sin.f64",
  "llvm.sqrt.f32", "llvm.sqrt.f64",
  "llvm.trunc.f32", "llvm.trunc.f64",
};

static bool isIntrinsicInline(Function *F) {
  return std::binary_search(std::begin(IntrinsicInline),
                            std::end(IntrinsicInline), F->getName());
}

// Returns of float, double and complex need to be handled with a helper
// function.
static bool fixupFPReturnAndCall(Function &F, Module *M,
                                 const MipsTargetMachine &TM) {
  bool Modified = false;
  LLVMContext &C = M->getContext();
  Type *MyVoid = Type::getVoidTy(C);
  for (auto &BB: F)
    for (auto &I: BB) {
      if (const ReturnInst *RI = dyn_cast<ReturnInst>(&I)) {
        Value *RVal = RI->getReturnValue();
        if (!RVal) continue;
        //
        // If there is a return value and it needs a helper function,
        // figure out which one and add a call before the actual
        // return to this helper. The purpose of the helper is to move
        // floating point values from their soft float return mapping to
        // where they would have been mapped to in floating point registers.
        //
        Type *T = RVal->getType();
        FPReturnVariant RV = whichFPReturnVariant(T);
        if (RV == NoFPRet) continue;
        static const char *const Helper[NoFPRet] = {
          "__mips16_ret_sf", "__mips16_ret_df", "__mips16_ret_sc",
          "__mips16_ret_dc"
        };
        const char *Name = Helper[RV];
        AttributeList A;
        Value *Params[] = {RVal};
        Modified = true;
        //
        // These helper functions have a different calling ABI so
        // this __Mips16RetHelper indicates that so that later
        // during call setup, the proper call lowering to the helper
        // functions will take place.
        //
        A = A.addAttribute(C, AttributeList::FunctionIndex,
                           "__Mips16RetHelper");
        A = A.addAttribute(C, AttributeList::FunctionIndex,
                           Attribute::ReadNone);
        A = A.addAttribute(C, AttributeList::FunctionIndex,
                           Attribute::NoInline);
        Value *F = (M->getOrInsertFunction(Name, A, MyVoid, T));
        CallInst::Create(F, Params, "", &I);
      } else if (const CallInst *CI = dyn_cast<CallInst>(&I)) {
        FunctionType *FT = CI->getFunctionType();
        Function *F_ =  CI->getCalledFunction();
        if (needsFPReturnHelper(*FT) &&
            !(F_ && isIntrinsicInline(F_))) {
          Modified=true;
          F.addFnAttr("saveS2");
        }
        if (F_ && !isIntrinsicInline(F_)) {
          // pic mode calls are handled by already defined
          // helper functions
          if (needsFPReturnHelper(*F_)) {
            Modified=true;
            F.addFnAttr("saveS2");
          }
          if (!TM.isPositionIndependent()) {
            if (needsFPHelperFromSig(*F_)) {
              assureFPCallStub(*F_, M, TM);
              Modified=true;
            }
          }
        }
      }
    }
  return Modified;
}

static void createFPFnStub(Function *F, Module *M, FPParamVariant PV,
                           const MipsTargetMachine &TM) {
  bool PicMode = TM.isPositionIndependent();
  bool LE = TM.isLittleEndian();
  LLVMContext &Context = M->getContext();
  std::string Name = F->getName();
  std::string SectionName = ".mips16.fn." + Name;
  std::string StubName = "__fn_stub_" + Name;
  std::string LocalName = "$$__fn_local_" + Name;
  Function *FStub = Function::Create
    (F->getFunctionType(),
     Function::InternalLinkage, StubName, M);
  FStub->addFnAttr("mips16_fp_stub");
  FStub->addFnAttr(Attribute::Naked);
  FStub->addFnAttr(Attribute::NoUnwind);
  FStub->addFnAttr(Attribute::NoInline);
  FStub->addFnAttr("nomips16");
  FStub->setSection(SectionName);
  BasicBlock *BB = BasicBlock::Create(Context, "entry", FStub);

  std::string AsmText;
  if (PicMode) {
    AsmText += ".set noreorder\n";
    AsmText += ".cpload $$25\n";
    AsmText += ".set reorder\n";
    AsmText += ".reloc 0, R_MIPS_NONE, " + Name + "\n";
    AsmText += "la $$25, " + LocalName + "\n";
  } else
    AsmText += "la $$25, " + Name + "\n";
  AsmText += swapFPIntParams(PV, M, LE, false);
  AsmText += "jr $$25\n";
  AsmText += LocalName + " = " + Name + "\n";
  EmitInlineAsm(Context, BB, AsmText);

  new UnreachableInst(FStub->getContext(), BB);
}

// remove the use-soft-float attribute
static void removeUseSoftFloat(Function &F) {
  AttrBuilder B;
  LLVM_DEBUG(errs() << "removing -use-soft-float\n");
  B.addAttribute("use-soft-float", "false");
  F.removeAttributes(AttributeList::FunctionIndex, B);
  if (F.hasFnAttribute("use-soft-float")) {
    LLVM_DEBUG(errs() << "still has -use-soft-float\n");
  }
  F.addAttributes(AttributeList::FunctionIndex, B);
}

// This pass only makes sense when the underlying chip has floating point but
// we are compiling as mips16.
// For all mips16 functions (that are not stubs we have already generated), or
// declared via attributes as nomips16, we must:
//    1) fixup all returns of float, double, single and double complex
//       by calling a helper function before the actual return.
//    2) generate helper functions (stubs) that can be called by mips32
//       functions that will move parameters passed normally passed in
//       floating point
//       registers the soft float equivalents.
//    3) in the case of static relocation, generate helper functions so that
//       mips16 functions can call extern functions of unknown type (mips16 or
//       mips32).
//    4) TBD. For pic, calls to extern functions of unknown type are handled by
//       predefined helper functions in libc but this work is currently done
//       during call lowering but it should be moved here in the future.
bool Mips16HardFloat::runOnModule(Module &M) {
  auto &TM = static_cast<const MipsTargetMachine &>(
      getAnalysis<TargetPassConfig>().getTM<TargetMachine>());
  LLVM_DEBUG(errs() << "Run on Module Mips16HardFloat\n");
  bool Modified = false;
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (F->hasFnAttribute("nomips16") &&
        F->hasFnAttribute("use-soft-float")) {
      removeUseSoftFloat(*F);
      continue;
    }
    if (F->isDeclaration() || F->hasFnAttribute("mips16_fp_stub") ||
        F->hasFnAttribute("nomips16")) continue;
    Modified |= fixupFPReturnAndCall(*F, &M, TM);
    FPParamVariant V = whichFPParamVariantNeeded(*F);
    if (V != NoSig) {
      Modified = true;
      createFPFnStub(&*F, &M, V, TM);
    }
  }
  return Modified;
}

ModulePass *llvm::createMips16HardFloatPass() {
  return new Mips16HardFloat();
}
