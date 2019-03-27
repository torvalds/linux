//===-- AMDGPUCodeGenPrepare.cpp ------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass does misc. AMDGPU optimizations on IR before instruction
/// selection.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "AMDGPUTargetMachine.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LegacyDivergenceAnalysis.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <iterator>

#define DEBUG_TYPE "amdgpu-codegenprepare"

using namespace llvm;

namespace {

static cl::opt<bool> WidenLoads(
  "amdgpu-codegenprepare-widen-constant-loads",
  cl::desc("Widen sub-dword constant address space loads in AMDGPUCodeGenPrepare"),
  cl::ReallyHidden,
  cl::init(true));

class AMDGPUCodeGenPrepare : public FunctionPass,
                             public InstVisitor<AMDGPUCodeGenPrepare, bool> {
  const GCNSubtarget *ST = nullptr;
  AssumptionCache *AC = nullptr;
  LegacyDivergenceAnalysis *DA = nullptr;
  Module *Mod = nullptr;
  bool HasUnsafeFPMath = false;

  /// Copies exact/nsw/nuw flags (if any) from binary operation \p I to
  /// binary operation \p V.
  ///
  /// \returns Binary operation \p V.
  /// \returns \p T's base element bit width.
  unsigned getBaseElementBitWidth(const Type *T) const;

  /// \returns Equivalent 32 bit integer type for given type \p T. For example,
  /// if \p T is i7, then i32 is returned; if \p T is <3 x i12>, then <3 x i32>
  /// is returned.
  Type *getI32Ty(IRBuilder<> &B, const Type *T) const;

  /// \returns True if binary operation \p I is a signed binary operation, false
  /// otherwise.
  bool isSigned(const BinaryOperator &I) const;

  /// \returns True if the condition of 'select' operation \p I comes from a
  /// signed 'icmp' operation, false otherwise.
  bool isSigned(const SelectInst &I) const;

  /// \returns True if type \p T needs to be promoted to 32 bit integer type,
  /// false otherwise.
  bool needsPromotionToI32(const Type *T) const;

  /// Promotes uniform binary operation \p I to equivalent 32 bit binary
  /// operation.
  ///
  /// \details \p I's base element bit width must be greater than 1 and less
  /// than or equal 16. Promotion is done by sign or zero extending operands to
  /// 32 bits, replacing \p I with equivalent 32 bit binary operation, and
  /// truncating the result of 32 bit binary operation back to \p I's original
  /// type. Division operation is not promoted.
  ///
  /// \returns True if \p I is promoted to equivalent 32 bit binary operation,
  /// false otherwise.
  bool promoteUniformOpToI32(BinaryOperator &I) const;

  /// Promotes uniform 'icmp' operation \p I to 32 bit 'icmp' operation.
  ///
  /// \details \p I's base element bit width must be greater than 1 and less
  /// than or equal 16. Promotion is done by sign or zero extending operands to
  /// 32 bits, and replacing \p I with 32 bit 'icmp' operation.
  ///
  /// \returns True.
  bool promoteUniformOpToI32(ICmpInst &I) const;

  /// Promotes uniform 'select' operation \p I to 32 bit 'select'
  /// operation.
  ///
  /// \details \p I's base element bit width must be greater than 1 and less
  /// than or equal 16. Promotion is done by sign or zero extending operands to
  /// 32 bits, replacing \p I with 32 bit 'select' operation, and truncating the
  /// result of 32 bit 'select' operation back to \p I's original type.
  ///
  /// \returns True.
  bool promoteUniformOpToI32(SelectInst &I) const;

  /// Promotes uniform 'bitreverse' intrinsic \p I to 32 bit 'bitreverse'
  /// intrinsic.
  ///
  /// \details \p I's base element bit width must be greater than 1 and less
  /// than or equal 16. Promotion is done by zero extending the operand to 32
  /// bits, replacing \p I with 32 bit 'bitreverse' intrinsic, shifting the
  /// result of 32 bit 'bitreverse' intrinsic to the right with zero fill (the
  /// shift amount is 32 minus \p I's base element bit width), and truncating
  /// the result of the shift operation back to \p I's original type.
  ///
  /// \returns True.
  bool promoteUniformBitreverseToI32(IntrinsicInst &I) const;

  /// Expands 24 bit div or rem.
  Value* expandDivRem24(IRBuilder<> &Builder, BinaryOperator &I,
                        Value *Num, Value *Den,
                        bool IsDiv, bool IsSigned) const;

  /// Expands 32 bit div or rem.
  Value* expandDivRem32(IRBuilder<> &Builder, BinaryOperator &I,
                        Value *Num, Value *Den) const;

  /// Widen a scalar load.
  ///
  /// \details \p Widen scalar load for uniform, small type loads from constant
  //  memory / to a full 32-bits and then truncate the input to allow a scalar
  //  load instead of a vector load.
  //
  /// \returns True.

  bool canWidenScalarExtLoad(LoadInst &I) const;

public:
  static char ID;

  AMDGPUCodeGenPrepare() : FunctionPass(ID) {}

  bool visitFDiv(BinaryOperator &I);

  bool visitInstruction(Instruction &I) { return false; }
  bool visitBinaryOperator(BinaryOperator &I);
  bool visitLoadInst(LoadInst &I);
  bool visitICmpInst(ICmpInst &I);
  bool visitSelectInst(SelectInst &I);

  bool visitIntrinsicInst(IntrinsicInst &I);
  bool visitBitreverseIntrinsicInst(IntrinsicInst &I);

  bool doInitialization(Module &M) override;
  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override { return "AMDGPU IR optimizations"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<LegacyDivergenceAnalysis>();
    AU.setPreservesAll();
 }
};

} // end anonymous namespace

unsigned AMDGPUCodeGenPrepare::getBaseElementBitWidth(const Type *T) const {
  assert(needsPromotionToI32(T) && "T does not need promotion to i32");

  if (T->isIntegerTy())
    return T->getIntegerBitWidth();
  return cast<VectorType>(T)->getElementType()->getIntegerBitWidth();
}

Type *AMDGPUCodeGenPrepare::getI32Ty(IRBuilder<> &B, const Type *T) const {
  assert(needsPromotionToI32(T) && "T does not need promotion to i32");

  if (T->isIntegerTy())
    return B.getInt32Ty();
  return VectorType::get(B.getInt32Ty(), cast<VectorType>(T)->getNumElements());
}

bool AMDGPUCodeGenPrepare::isSigned(const BinaryOperator &I) const {
  return I.getOpcode() == Instruction::AShr ||
      I.getOpcode() == Instruction::SDiv || I.getOpcode() == Instruction::SRem;
}

bool AMDGPUCodeGenPrepare::isSigned(const SelectInst &I) const {
  return isa<ICmpInst>(I.getOperand(0)) ?
      cast<ICmpInst>(I.getOperand(0))->isSigned() : false;
}

bool AMDGPUCodeGenPrepare::needsPromotionToI32(const Type *T) const {
  const IntegerType *IntTy = dyn_cast<IntegerType>(T);
  if (IntTy && IntTy->getBitWidth() > 1 && IntTy->getBitWidth() <= 16)
    return true;

  if (const VectorType *VT = dyn_cast<VectorType>(T)) {
    // TODO: The set of packed operations is more limited, so may want to
    // promote some anyway.
    if (ST->hasVOP3PInsts())
      return false;

    return needsPromotionToI32(VT->getElementType());
  }

  return false;
}

// Return true if the op promoted to i32 should have nsw set.
static bool promotedOpIsNSW(const Instruction &I) {
  switch (I.getOpcode()) {
  case Instruction::Shl:
  case Instruction::Add:
  case Instruction::Sub:
    return true;
  case Instruction::Mul:
    return I.hasNoUnsignedWrap();
  default:
    return false;
  }
}

// Return true if the op promoted to i32 should have nuw set.
static bool promotedOpIsNUW(const Instruction &I) {
  switch (I.getOpcode()) {
  case Instruction::Shl:
  case Instruction::Add:
  case Instruction::Mul:
    return true;
  case Instruction::Sub:
    return I.hasNoUnsignedWrap();
  default:
    return false;
  }
}

bool AMDGPUCodeGenPrepare::canWidenScalarExtLoad(LoadInst &I) const {
  Type *Ty = I.getType();
  const DataLayout &DL = Mod->getDataLayout();
  int TySize = DL.getTypeSizeInBits(Ty);
  unsigned Align = I.getAlignment() ?
                   I.getAlignment() : DL.getABITypeAlignment(Ty);

  return I.isSimple() && TySize < 32 && Align >= 4 && DA->isUniform(&I);
}

bool AMDGPUCodeGenPrepare::promoteUniformOpToI32(BinaryOperator &I) const {
  assert(needsPromotionToI32(I.getType()) &&
         "I does not need promotion to i32");

  if (I.getOpcode() == Instruction::SDiv ||
      I.getOpcode() == Instruction::UDiv ||
      I.getOpcode() == Instruction::SRem ||
      I.getOpcode() == Instruction::URem)
    return false;

  IRBuilder<> Builder(&I);
  Builder.SetCurrentDebugLocation(I.getDebugLoc());

  Type *I32Ty = getI32Ty(Builder, I.getType());
  Value *ExtOp0 = nullptr;
  Value *ExtOp1 = nullptr;
  Value *ExtRes = nullptr;
  Value *TruncRes = nullptr;

  if (isSigned(I)) {
    ExtOp0 = Builder.CreateSExt(I.getOperand(0), I32Ty);
    ExtOp1 = Builder.CreateSExt(I.getOperand(1), I32Ty);
  } else {
    ExtOp0 = Builder.CreateZExt(I.getOperand(0), I32Ty);
    ExtOp1 = Builder.CreateZExt(I.getOperand(1), I32Ty);
  }

  ExtRes = Builder.CreateBinOp(I.getOpcode(), ExtOp0, ExtOp1);
  if (Instruction *Inst = dyn_cast<Instruction>(ExtRes)) {
    if (promotedOpIsNSW(cast<Instruction>(I)))
      Inst->setHasNoSignedWrap();

    if (promotedOpIsNUW(cast<Instruction>(I)))
      Inst->setHasNoUnsignedWrap();

    if (const auto *ExactOp = dyn_cast<PossiblyExactOperator>(&I))
      Inst->setIsExact(ExactOp->isExact());
  }

  TruncRes = Builder.CreateTrunc(ExtRes, I.getType());

  I.replaceAllUsesWith(TruncRes);
  I.eraseFromParent();

  return true;
}

bool AMDGPUCodeGenPrepare::promoteUniformOpToI32(ICmpInst &I) const {
  assert(needsPromotionToI32(I.getOperand(0)->getType()) &&
         "I does not need promotion to i32");

  IRBuilder<> Builder(&I);
  Builder.SetCurrentDebugLocation(I.getDebugLoc());

  Type *I32Ty = getI32Ty(Builder, I.getOperand(0)->getType());
  Value *ExtOp0 = nullptr;
  Value *ExtOp1 = nullptr;
  Value *NewICmp  = nullptr;

  if (I.isSigned()) {
    ExtOp0 = Builder.CreateSExt(I.getOperand(0), I32Ty);
    ExtOp1 = Builder.CreateSExt(I.getOperand(1), I32Ty);
  } else {
    ExtOp0 = Builder.CreateZExt(I.getOperand(0), I32Ty);
    ExtOp1 = Builder.CreateZExt(I.getOperand(1), I32Ty);
  }
  NewICmp = Builder.CreateICmp(I.getPredicate(), ExtOp0, ExtOp1);

  I.replaceAllUsesWith(NewICmp);
  I.eraseFromParent();

  return true;
}

bool AMDGPUCodeGenPrepare::promoteUniformOpToI32(SelectInst &I) const {
  assert(needsPromotionToI32(I.getType()) &&
         "I does not need promotion to i32");

  IRBuilder<> Builder(&I);
  Builder.SetCurrentDebugLocation(I.getDebugLoc());

  Type *I32Ty = getI32Ty(Builder, I.getType());
  Value *ExtOp1 = nullptr;
  Value *ExtOp2 = nullptr;
  Value *ExtRes = nullptr;
  Value *TruncRes = nullptr;

  if (isSigned(I)) {
    ExtOp1 = Builder.CreateSExt(I.getOperand(1), I32Ty);
    ExtOp2 = Builder.CreateSExt(I.getOperand(2), I32Ty);
  } else {
    ExtOp1 = Builder.CreateZExt(I.getOperand(1), I32Ty);
    ExtOp2 = Builder.CreateZExt(I.getOperand(2), I32Ty);
  }
  ExtRes = Builder.CreateSelect(I.getOperand(0), ExtOp1, ExtOp2);
  TruncRes = Builder.CreateTrunc(ExtRes, I.getType());

  I.replaceAllUsesWith(TruncRes);
  I.eraseFromParent();

  return true;
}

bool AMDGPUCodeGenPrepare::promoteUniformBitreverseToI32(
    IntrinsicInst &I) const {
  assert(I.getIntrinsicID() == Intrinsic::bitreverse &&
         "I must be bitreverse intrinsic");
  assert(needsPromotionToI32(I.getType()) &&
         "I does not need promotion to i32");

  IRBuilder<> Builder(&I);
  Builder.SetCurrentDebugLocation(I.getDebugLoc());

  Type *I32Ty = getI32Ty(Builder, I.getType());
  Function *I32 =
      Intrinsic::getDeclaration(Mod, Intrinsic::bitreverse, { I32Ty });
  Value *ExtOp = Builder.CreateZExt(I.getOperand(0), I32Ty);
  Value *ExtRes = Builder.CreateCall(I32, { ExtOp });
  Value *LShrOp =
      Builder.CreateLShr(ExtRes, 32 - getBaseElementBitWidth(I.getType()));
  Value *TruncRes =
      Builder.CreateTrunc(LShrOp, I.getType());

  I.replaceAllUsesWith(TruncRes);
  I.eraseFromParent();

  return true;
}

static bool shouldKeepFDivF32(Value *Num, bool UnsafeDiv, bool HasDenormals) {
  const ConstantFP *CNum = dyn_cast<ConstantFP>(Num);
  if (!CNum)
    return HasDenormals;

  if (UnsafeDiv)
    return true;

  bool IsOne = CNum->isExactlyValue(+1.0) || CNum->isExactlyValue(-1.0);

  // Reciprocal f32 is handled separately without denormals.
  return HasDenormals ^ IsOne;
}

// Insert an intrinsic for fast fdiv for safe math situations where we can
// reduce precision. Leave fdiv for situations where the generic node is
// expected to be optimized.
bool AMDGPUCodeGenPrepare::visitFDiv(BinaryOperator &FDiv) {
  Type *Ty = FDiv.getType();

  if (!Ty->getScalarType()->isFloatTy())
    return false;

  MDNode *FPMath = FDiv.getMetadata(LLVMContext::MD_fpmath);
  if (!FPMath)
    return false;

  const FPMathOperator *FPOp = cast<const FPMathOperator>(&FDiv);
  float ULP = FPOp->getFPAccuracy();
  if (ULP < 2.5f)
    return false;

  FastMathFlags FMF = FPOp->getFastMathFlags();
  bool UnsafeDiv = HasUnsafeFPMath || FMF.isFast() ||
                                      FMF.allowReciprocal();

  // With UnsafeDiv node will be optimized to just rcp and mul.
  if (UnsafeDiv)
    return false;

  IRBuilder<> Builder(FDiv.getParent(), std::next(FDiv.getIterator()), FPMath);
  Builder.setFastMathFlags(FMF);
  Builder.SetCurrentDebugLocation(FDiv.getDebugLoc());

  Function *Decl = Intrinsic::getDeclaration(Mod, Intrinsic::amdgcn_fdiv_fast);

  Value *Num = FDiv.getOperand(0);
  Value *Den = FDiv.getOperand(1);

  Value *NewFDiv = nullptr;

  bool HasDenormals = ST->hasFP32Denormals();
  if (VectorType *VT = dyn_cast<VectorType>(Ty)) {
    NewFDiv = UndefValue::get(VT);

    // FIXME: Doesn't do the right thing for cases where the vector is partially
    // constant. This works when the scalarizer pass is run first.
    for (unsigned I = 0, E = VT->getNumElements(); I != E; ++I) {
      Value *NumEltI = Builder.CreateExtractElement(Num, I);
      Value *DenEltI = Builder.CreateExtractElement(Den, I);
      Value *NewElt;

      if (shouldKeepFDivF32(NumEltI, UnsafeDiv, HasDenormals)) {
        NewElt = Builder.CreateFDiv(NumEltI, DenEltI);
      } else {
        NewElt = Builder.CreateCall(Decl, { NumEltI, DenEltI });
      }

      NewFDiv = Builder.CreateInsertElement(NewFDiv, NewElt, I);
    }
  } else {
    if (!shouldKeepFDivF32(Num, UnsafeDiv, HasDenormals))
      NewFDiv = Builder.CreateCall(Decl, { Num, Den });
  }

  if (NewFDiv) {
    FDiv.replaceAllUsesWith(NewFDiv);
    NewFDiv->takeName(&FDiv);
    FDiv.eraseFromParent();
  }

  return !!NewFDiv;
}

static bool hasUnsafeFPMath(const Function &F) {
  Attribute Attr = F.getFnAttribute("unsafe-fp-math");
  return Attr.getValueAsString() == "true";
}

static std::pair<Value*, Value*> getMul64(IRBuilder<> &Builder,
                                          Value *LHS, Value *RHS) {
  Type *I32Ty = Builder.getInt32Ty();
  Type *I64Ty = Builder.getInt64Ty();

  Value *LHS_EXT64 = Builder.CreateZExt(LHS, I64Ty);
  Value *RHS_EXT64 = Builder.CreateZExt(RHS, I64Ty);
  Value *MUL64 = Builder.CreateMul(LHS_EXT64, RHS_EXT64);
  Value *Lo = Builder.CreateTrunc(MUL64, I32Ty);
  Value *Hi = Builder.CreateLShr(MUL64, Builder.getInt64(32));
  Hi = Builder.CreateTrunc(Hi, I32Ty);
  return std::make_pair(Lo, Hi);
}

static Value* getMulHu(IRBuilder<> &Builder, Value *LHS, Value *RHS) {
  return getMul64(Builder, LHS, RHS).second;
}

// The fractional part of a float is enough to accurately represent up to
// a 24-bit signed integer.
Value* AMDGPUCodeGenPrepare::expandDivRem24(IRBuilder<> &Builder,
                                            BinaryOperator &I,
                                            Value *Num, Value *Den,
                                            bool IsDiv, bool IsSigned) const {
  assert(Num->getType()->isIntegerTy(32));

  const DataLayout &DL = Mod->getDataLayout();
  unsigned LHSSignBits = ComputeNumSignBits(Num, DL, 0, AC, &I);
  if (LHSSignBits < 9)
    return nullptr;

  unsigned RHSSignBits = ComputeNumSignBits(Den, DL, 0, AC, &I);
  if (RHSSignBits < 9)
    return nullptr;


  unsigned SignBits = std::min(LHSSignBits, RHSSignBits);
  unsigned DivBits = 32 - SignBits;
  if (IsSigned)
    ++DivBits;

  Type *Ty = Num->getType();
  Type *I32Ty = Builder.getInt32Ty();
  Type *F32Ty = Builder.getFloatTy();
  ConstantInt *One = Builder.getInt32(1);
  Value *JQ = One;

  if (IsSigned) {
    // char|short jq = ia ^ ib;
    JQ = Builder.CreateXor(Num, Den);

    // jq = jq >> (bitsize - 2)
    JQ = Builder.CreateAShr(JQ, Builder.getInt32(30));

    // jq = jq | 0x1
    JQ = Builder.CreateOr(JQ, One);
  }

  // int ia = (int)LHS;
  Value *IA = Num;

  // int ib, (int)RHS;
  Value *IB = Den;

  // float fa = (float)ia;
  Value *FA = IsSigned ? Builder.CreateSIToFP(IA, F32Ty)
                       : Builder.CreateUIToFP(IA, F32Ty);

  // float fb = (float)ib;
  Value *FB = IsSigned ? Builder.CreateSIToFP(IB,F32Ty)
                       : Builder.CreateUIToFP(IB,F32Ty);

  Value *RCP = Builder.CreateFDiv(ConstantFP::get(F32Ty, 1.0), FB);
  Value *FQM = Builder.CreateFMul(FA, RCP);

  // fq = trunc(fqm);
  CallInst *FQ = Builder.CreateUnaryIntrinsic(Intrinsic::trunc, FQM);
  FQ->copyFastMathFlags(Builder.getFastMathFlags());

  // float fqneg = -fq;
  Value *FQNeg = Builder.CreateFNeg(FQ);

  // float fr = mad(fqneg, fb, fa);
  Value *FR = Builder.CreateIntrinsic(Intrinsic::amdgcn_fmad_ftz,
                                      {FQNeg->getType()}, {FQNeg, FB, FA}, FQ);

  // int iq = (int)fq;
  Value *IQ = IsSigned ? Builder.CreateFPToSI(FQ, I32Ty)
                       : Builder.CreateFPToUI(FQ, I32Ty);

  // fr = fabs(fr);
  FR = Builder.CreateUnaryIntrinsic(Intrinsic::fabs, FR, FQ);

  // fb = fabs(fb);
  FB = Builder.CreateUnaryIntrinsic(Intrinsic::fabs, FB, FQ);

  // int cv = fr >= fb;
  Value *CV = Builder.CreateFCmpOGE(FR, FB);

  // jq = (cv ? jq : 0);
  JQ = Builder.CreateSelect(CV, JQ, Builder.getInt32(0));

  // dst = iq + jq;
  Value *Div = Builder.CreateAdd(IQ, JQ);

  Value *Res = Div;
  if (!IsDiv) {
    // Rem needs compensation, it's easier to recompute it
    Value *Rem = Builder.CreateMul(Div, Den);
    Res = Builder.CreateSub(Num, Rem);
  }

  // Truncate to number of bits this divide really is.
  if (IsSigned) {
    Res = Builder.CreateTrunc(Res, Builder.getIntNTy(DivBits));
    Res = Builder.CreateSExt(Res, Ty);
  } else {
    ConstantInt *TruncMask = Builder.getInt32((UINT64_C(1) << DivBits) - 1);
    Res = Builder.CreateAnd(Res, TruncMask);
  }

  return Res;
}

Value* AMDGPUCodeGenPrepare::expandDivRem32(IRBuilder<> &Builder,
                                            BinaryOperator &I,
                                            Value *Num, Value *Den) const {
  Instruction::BinaryOps Opc = I.getOpcode();
  assert(Opc == Instruction::URem || Opc == Instruction::UDiv ||
         Opc == Instruction::SRem || Opc == Instruction::SDiv);

  FastMathFlags FMF;
  FMF.setFast();
  Builder.setFastMathFlags(FMF);

  if (isa<Constant>(Den))
    return nullptr; // Keep it for optimization

  bool IsDiv = Opc == Instruction::UDiv || Opc == Instruction::SDiv;
  bool IsSigned = Opc == Instruction::SRem || Opc == Instruction::SDiv;

  Type *Ty = Num->getType();
  Type *I32Ty = Builder.getInt32Ty();
  Type *F32Ty = Builder.getFloatTy();

  if (Ty->getScalarSizeInBits() < 32) {
    if (IsSigned) {
      Num = Builder.CreateSExt(Num, I32Ty);
      Den = Builder.CreateSExt(Den, I32Ty);
    } else {
      Num = Builder.CreateZExt(Num, I32Ty);
      Den = Builder.CreateZExt(Den, I32Ty);
    }
  }

  if (Value *Res = expandDivRem24(Builder, I, Num, Den, IsDiv, IsSigned)) {
    Res = Builder.CreateTrunc(Res, Ty);
    return Res;
  }

  ConstantInt *Zero = Builder.getInt32(0);
  ConstantInt *One = Builder.getInt32(1);
  ConstantInt *MinusOne = Builder.getInt32(~0);

  Value *Sign = nullptr;
  if (IsSigned) {
    ConstantInt *K31 = Builder.getInt32(31);
    Value *LHSign = Builder.CreateAShr(Num, K31);
    Value *RHSign = Builder.CreateAShr(Den, K31);
    // Remainder sign is the same as LHS
    Sign = IsDiv ? Builder.CreateXor(LHSign, RHSign) : LHSign;

    Num = Builder.CreateAdd(Num, LHSign);
    Den = Builder.CreateAdd(Den, RHSign);

    Num = Builder.CreateXor(Num, LHSign);
    Den = Builder.CreateXor(Den, RHSign);
  }

  // RCP =  URECIP(Den) = 2^32 / Den + e
  // e is rounding error.
  Value *DEN_F32 = Builder.CreateUIToFP(Den, F32Ty);
  Value *RCP_F32 = Builder.CreateFDiv(ConstantFP::get(F32Ty, 1.0), DEN_F32);
  Constant *UINT_MAX_PLUS_1 = ConstantFP::get(F32Ty, BitsToFloat(0x4f800000));
  Value *RCP_SCALE = Builder.CreateFMul(RCP_F32, UINT_MAX_PLUS_1);
  Value *RCP = Builder.CreateFPToUI(RCP_SCALE, I32Ty);

  // RCP_LO, RCP_HI = mul(RCP, Den) */
  Value *RCP_LO, *RCP_HI;
  std::tie(RCP_LO, RCP_HI) = getMul64(Builder, RCP, Den);

  // NEG_RCP_LO = -RCP_LO
  Value *NEG_RCP_LO = Builder.CreateNeg(RCP_LO);

  // ABS_RCP_LO = (RCP_HI == 0 ? NEG_RCP_LO : RCP_LO)
  Value *RCP_HI_0_CC = Builder.CreateICmpEQ(RCP_HI, Zero);
  Value *ABS_RCP_LO = Builder.CreateSelect(RCP_HI_0_CC, NEG_RCP_LO, RCP_LO);

  // Calculate the rounding error from the URECIP instruction
  // E = mulhu(ABS_RCP_LO, RCP)
  Value *E = getMulHu(Builder, ABS_RCP_LO, RCP);

  // RCP_A_E = RCP + E
  Value *RCP_A_E = Builder.CreateAdd(RCP, E);

  // RCP_S_E = RCP - E
  Value *RCP_S_E = Builder.CreateSub(RCP, E);

  // Tmp0 = (RCP_HI == 0 ? RCP_A_E : RCP_SUB_E)
  Value *Tmp0 = Builder.CreateSelect(RCP_HI_0_CC, RCP_A_E, RCP_S_E);

  // Quotient = mulhu(Tmp0, Num)
  Value *Quotient = getMulHu(Builder, Tmp0, Num);

  // Num_S_Remainder = Quotient * Den
  Value *Num_S_Remainder = Builder.CreateMul(Quotient, Den);

  // Remainder = Num - Num_S_Remainder
  Value *Remainder = Builder.CreateSub(Num, Num_S_Remainder);

  // Remainder_GE_Den = (Remainder >= Den ? -1 : 0)
  Value *Rem_GE_Den_CC = Builder.CreateICmpUGE(Remainder, Den);
  Value *Remainder_GE_Den = Builder.CreateSelect(Rem_GE_Den_CC, MinusOne, Zero);

  // Remainder_GE_Zero = (Num >= Num_S_Remainder ? -1 : 0)
  Value *Num_GE_Num_S_Rem_CC = Builder.CreateICmpUGE(Num, Num_S_Remainder);
  Value *Remainder_GE_Zero = Builder.CreateSelect(Num_GE_Num_S_Rem_CC,
                                                  MinusOne, Zero);

  // Tmp1 = Remainder_GE_Den & Remainder_GE_Zero
  Value *Tmp1 = Builder.CreateAnd(Remainder_GE_Den, Remainder_GE_Zero);
  Value *Tmp1_0_CC = Builder.CreateICmpEQ(Tmp1, Zero);

  Value *Res;
  if (IsDiv) {
    // Quotient_A_One = Quotient + 1
    Value *Quotient_A_One = Builder.CreateAdd(Quotient, One);

    // Quotient_S_One = Quotient - 1
    Value *Quotient_S_One = Builder.CreateSub(Quotient, One);

    // Div = (Tmp1 == 0 ? Quotient : Quotient_A_One)
    Value *Div = Builder.CreateSelect(Tmp1_0_CC, Quotient, Quotient_A_One);

    // Div = (Remainder_GE_Zero == 0 ? Quotient_S_One : Div)
    Res = Builder.CreateSelect(Num_GE_Num_S_Rem_CC, Div, Quotient_S_One);
  } else {
    // Remainder_S_Den = Remainder - Den
    Value *Remainder_S_Den = Builder.CreateSub(Remainder, Den);

    // Remainder_A_Den = Remainder + Den
    Value *Remainder_A_Den = Builder.CreateAdd(Remainder, Den);

    // Rem = (Tmp1 == 0 ? Remainder : Remainder_S_Den)
    Value *Rem = Builder.CreateSelect(Tmp1_0_CC, Remainder, Remainder_S_Den);

    // Rem = (Remainder_GE_Zero == 0 ? Remainder_A_Den : Rem)
    Res = Builder.CreateSelect(Num_GE_Num_S_Rem_CC, Rem, Remainder_A_Den);
  }

  if (IsSigned) {
    Res = Builder.CreateXor(Res, Sign);
    Res = Builder.CreateSub(Res, Sign);
  }

  Res = Builder.CreateTrunc(Res, Ty);

  return Res;
}

bool AMDGPUCodeGenPrepare::visitBinaryOperator(BinaryOperator &I) {
  if (ST->has16BitInsts() && needsPromotionToI32(I.getType()) &&
      DA->isUniform(&I) && promoteUniformOpToI32(I))
    return true;

  bool Changed = false;
  Instruction::BinaryOps Opc = I.getOpcode();
  Type *Ty = I.getType();
  Value *NewDiv = nullptr;
  if ((Opc == Instruction::URem || Opc == Instruction::UDiv ||
       Opc == Instruction::SRem || Opc == Instruction::SDiv) &&
      Ty->getScalarSizeInBits() <= 32) {
    Value *Num = I.getOperand(0);
    Value *Den = I.getOperand(1);
    IRBuilder<> Builder(&I);
    Builder.SetCurrentDebugLocation(I.getDebugLoc());

    if (VectorType *VT = dyn_cast<VectorType>(Ty)) {
      NewDiv = UndefValue::get(VT);

      for (unsigned N = 0, E = VT->getNumElements(); N != E; ++N) {
        Value *NumEltN = Builder.CreateExtractElement(Num, N);
        Value *DenEltN = Builder.CreateExtractElement(Den, N);
        Value *NewElt = expandDivRem32(Builder, I, NumEltN, DenEltN);
        if (!NewElt)
          NewElt = Builder.CreateBinOp(Opc, NumEltN, DenEltN);
        NewDiv = Builder.CreateInsertElement(NewDiv, NewElt, N);
      }
    } else {
      NewDiv = expandDivRem32(Builder, I, Num, Den);
    }

    if (NewDiv) {
      I.replaceAllUsesWith(NewDiv);
      I.eraseFromParent();
      Changed = true;
    }
  }

  return Changed;
}

bool AMDGPUCodeGenPrepare::visitLoadInst(LoadInst &I) {
  if (!WidenLoads)
    return false;

  if ((I.getPointerAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS ||
       I.getPointerAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS_32BIT) &&
      canWidenScalarExtLoad(I)) {
    IRBuilder<> Builder(&I);
    Builder.SetCurrentDebugLocation(I.getDebugLoc());

    Type *I32Ty = Builder.getInt32Ty();
    Type *PT = PointerType::get(I32Ty, I.getPointerAddressSpace());
    Value *BitCast= Builder.CreateBitCast(I.getPointerOperand(), PT);
    LoadInst *WidenLoad = Builder.CreateLoad(BitCast);
    WidenLoad->copyMetadata(I);

    // If we have range metadata, we need to convert the type, and not make
    // assumptions about the high bits.
    if (auto *Range = WidenLoad->getMetadata(LLVMContext::MD_range)) {
      ConstantInt *Lower =
        mdconst::extract<ConstantInt>(Range->getOperand(0));

      if (Lower->getValue().isNullValue()) {
        WidenLoad->setMetadata(LLVMContext::MD_range, nullptr);
      } else {
        Metadata *LowAndHigh[] = {
          ConstantAsMetadata::get(ConstantInt::get(I32Ty, Lower->getValue().zext(32))),
          // Don't make assumptions about the high bits.
          ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0))
        };

        WidenLoad->setMetadata(LLVMContext::MD_range,
                               MDNode::get(Mod->getContext(), LowAndHigh));
      }
    }

    int TySize = Mod->getDataLayout().getTypeSizeInBits(I.getType());
    Type *IntNTy = Builder.getIntNTy(TySize);
    Value *ValTrunc = Builder.CreateTrunc(WidenLoad, IntNTy);
    Value *ValOrig = Builder.CreateBitCast(ValTrunc, I.getType());
    I.replaceAllUsesWith(ValOrig);
    I.eraseFromParent();
    return true;
  }

  return false;
}

bool AMDGPUCodeGenPrepare::visitICmpInst(ICmpInst &I) {
  bool Changed = false;

  if (ST->has16BitInsts() && needsPromotionToI32(I.getOperand(0)->getType()) &&
      DA->isUniform(&I))
    Changed |= promoteUniformOpToI32(I);

  return Changed;
}

bool AMDGPUCodeGenPrepare::visitSelectInst(SelectInst &I) {
  bool Changed = false;

  if (ST->has16BitInsts() && needsPromotionToI32(I.getType()) &&
      DA->isUniform(&I))
    Changed |= promoteUniformOpToI32(I);

  return Changed;
}

bool AMDGPUCodeGenPrepare::visitIntrinsicInst(IntrinsicInst &I) {
  switch (I.getIntrinsicID()) {
  case Intrinsic::bitreverse:
    return visitBitreverseIntrinsicInst(I);
  default:
    return false;
  }
}

bool AMDGPUCodeGenPrepare::visitBitreverseIntrinsicInst(IntrinsicInst &I) {
  bool Changed = false;

  if (ST->has16BitInsts() && needsPromotionToI32(I.getType()) &&
      DA->isUniform(&I))
    Changed |= promoteUniformBitreverseToI32(I);

  return Changed;
}

bool AMDGPUCodeGenPrepare::doInitialization(Module &M) {
  Mod = &M;
  return false;
}

bool AMDGPUCodeGenPrepare::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;

  const AMDGPUTargetMachine &TM = TPC->getTM<AMDGPUTargetMachine>();
  ST = &TM.getSubtarget<GCNSubtarget>(F);
  AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
  DA = &getAnalysis<LegacyDivergenceAnalysis>();
  HasUnsafeFPMath = hasUnsafeFPMath(F);

  bool MadeChange = false;

  for (BasicBlock &BB : F) {
    BasicBlock::iterator Next;
    for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; I = Next) {
      Next = std::next(I);
      MadeChange |= visit(*I);
    }
  }

  return MadeChange;
}

INITIALIZE_PASS_BEGIN(AMDGPUCodeGenPrepare, DEBUG_TYPE,
                      "AMDGPU IR optimizations", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(LegacyDivergenceAnalysis)
INITIALIZE_PASS_END(AMDGPUCodeGenPrepare, DEBUG_TYPE, "AMDGPU IR optimizations",
                    false, false)

char AMDGPUCodeGenPrepare::ID = 0;

FunctionPass *llvm::createAMDGPUCodeGenPreparePass() {
  return new AMDGPUCodeGenPrepare();
}
