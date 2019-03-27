//===----- ARMCodeGenPrepare.cpp ------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass inserts intrinsics to handle small types that would otherwise be
/// promoted during legalization. Here we can manually promote types or insert
/// intrinsics which can handle narrow types that aren't supported by the
/// register classes.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMSubtarget.h"
#include "ARMTargetMachine.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"

#define DEBUG_TYPE "arm-codegenprepare"

using namespace llvm;

static cl::opt<bool>
DisableCGP("arm-disable-cgp", cl::Hidden, cl::init(true),
           cl::desc("Disable ARM specific CodeGenPrepare pass"));

static cl::opt<bool>
EnableDSP("arm-enable-scalar-dsp", cl::Hidden, cl::init(false),
          cl::desc("Use DSP instructions for scalar operations"));

static cl::opt<bool>
EnableDSPWithImms("arm-enable-scalar-dsp-imms", cl::Hidden, cl::init(false),
                   cl::desc("Use DSP instructions for scalar operations\
                            with immediate operands"));

// The goal of this pass is to enable more efficient code generation for
// operations on narrow types (i.e. types with < 32-bits) and this is a
// motivating IR code example:
//
//   define hidden i32 @cmp(i8 zeroext) {
//     %2 = add i8 %0, -49
//     %3 = icmp ult i8 %2, 3
//     ..
//   }
//
// The issue here is that i8 is type-legalized to i32 because i8 is not a
// legal type. Thus, arithmetic is done in integer-precision, but then the
// byte value is masked out as follows:
//
//   t19: i32 = add t4, Constant:i32<-49>
//     t24: i32 = and t19, Constant:i32<255>
//
// Consequently, we generate code like this:
//
//   subs  r0, #49
//   uxtb  r1, r0
//   cmp r1, #3
//
// This shows that masking out the byte value results in generation of
// the UXTB instruction. This is not optimal as r0 already contains the byte
// value we need, and so instead we can just generate:
//
//   sub.w r1, r0, #49
//   cmp r1, #3
//
// We achieve this by type promoting the IR to i32 like so for this example:
//
//   define i32 @cmp(i8 zeroext %c) {
//     %0 = zext i8 %c to i32
//     %c.off = add i32 %0, -49
//     %1 = icmp ult i32 %c.off, 3
//     ..
//   }
//
// For this to be valid and legal, we need to prove that the i32 add is
// producing the same value as the i8 addition, and that e.g. no overflow
// happens.
//
// A brief sketch of the algorithm and some terminology.
// We pattern match interesting IR patterns:
// - which have "sources": instructions producing narrow values (i8, i16), and
// - they have "sinks": instructions consuming these narrow values.
//
// We collect all instruction connecting sources and sinks in a worklist, so
// that we can mutate these instruction and perform type promotion when it is
// legal to do so.

namespace {
class IRPromoter {
  SmallPtrSet<Value*, 8> NewInsts;
  SmallPtrSet<Instruction*, 4> InstsToRemove;
  DenseMap<Value*, SmallVector<Type*, 4>> TruncTysMap;
  SmallPtrSet<Value*, 8> Promoted;
  Module *M = nullptr;
  LLVMContext &Ctx;
  IntegerType *ExtTy = nullptr;
  IntegerType *OrigTy = nullptr;
  SmallPtrSetImpl<Value*> *Visited;
  SmallPtrSetImpl<Value*> *Sources;
  SmallPtrSetImpl<Instruction*> *Sinks;
  SmallPtrSetImpl<Instruction*> *SafeToPromote;

  void ReplaceAllUsersOfWith(Value *From, Value *To);
  void PrepareConstants(void);
  void ExtendSources(void);
  void ConvertTruncs(void);
  void PromoteTree(void);
  void TruncateSinks(void);
  void Cleanup(void);

public:
  IRPromoter(Module *M) : M(M), Ctx(M->getContext()),
                          ExtTy(Type::getInt32Ty(Ctx)) { }


  void Mutate(Type *OrigTy,
              SmallPtrSetImpl<Value*> &Visited,
              SmallPtrSetImpl<Value*> &Sources,
              SmallPtrSetImpl<Instruction*> &Sinks,
              SmallPtrSetImpl<Instruction*> &SafeToPromote);
};

class ARMCodeGenPrepare : public FunctionPass {
  const ARMSubtarget *ST = nullptr;
  IRPromoter *Promoter = nullptr;
  std::set<Value*> AllVisited;
  SmallPtrSet<Instruction*, 8> SafeToPromote;

  bool isSafeOverflow(Instruction *I);
  bool isSupportedValue(Value *V);
  bool isLegalToPromote(Value *V);
  bool TryToPromote(Value *V);

public:
  static char ID;
  static unsigned TypeSize;
  Type *OrigTy = nullptr;

  ARMCodeGenPrepare() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetPassConfig>();
  }

  StringRef getPassName() const override { return "ARM IR optimizations"; }

  bool doInitialization(Module &M) override;
  bool runOnFunction(Function &F) override;
  bool doFinalization(Module &M) override;
};

}

static bool generateSignBits(Value *V) {
  if (!isa<Instruction>(V))
    return false;

  unsigned Opc = cast<Instruction>(V)->getOpcode();
  return Opc == Instruction::AShr || Opc == Instruction::SDiv ||
         Opc == Instruction::SRem;
}

static bool EqualTypeSize(Value *V) {
  return V->getType()->getScalarSizeInBits() == ARMCodeGenPrepare::TypeSize;
}

static bool LessOrEqualTypeSize(Value *V) {
  return V->getType()->getScalarSizeInBits() <= ARMCodeGenPrepare::TypeSize;
}

static bool GreaterThanTypeSize(Value *V) {
  return V->getType()->getScalarSizeInBits() > ARMCodeGenPrepare::TypeSize;
}

static bool LessThanTypeSize(Value *V) {
  return V->getType()->getScalarSizeInBits() < ARMCodeGenPrepare::TypeSize;
}

/// Some instructions can use 8- and 16-bit operands, and we don't need to
/// promote anything larger. We disallow booleans to make life easier when
/// dealing with icmps but allow any other integer that is <= 16 bits. Void
/// types are accepted so we can handle switches.
static bool isSupportedType(Value *V) {
  Type *Ty = V->getType();

  // Allow voids and pointers, these won't be promoted.
  if (Ty->isVoidTy() || Ty->isPointerTy())
    return true;

  if (auto *Ld = dyn_cast<LoadInst>(V))
    Ty = cast<PointerType>(Ld->getPointerOperandType())->getElementType();

  if (!isa<IntegerType>(Ty) ||
      cast<IntegerType>(V->getType())->getBitWidth() == 1)
    return false;

  return LessOrEqualTypeSize(V);
}

/// Return true if the given value is a source in the use-def chain, producing
/// a narrow 'TypeSize' value. These values will be zext to start the promotion
/// of the tree to i32. We guarantee that these won't populate the upper bits
/// of the register. ZExt on the loads will be free, and the same for call
/// return values because we only accept ones that guarantee a zeroext ret val.
/// Many arguments will have the zeroext attribute too, so those would be free
/// too.
static bool isSource(Value *V) {
  if (!isa<IntegerType>(V->getType()))
    return false;

  // TODO Allow zext to be sources.
  if (isa<Argument>(V))
    return true;
  else if (isa<LoadInst>(V))
    return true;
  else if (isa<BitCastInst>(V))
    return true;
  else if (auto *Call = dyn_cast<CallInst>(V))
    return Call->hasRetAttr(Attribute::AttrKind::ZExt);
  else if (auto *Trunc = dyn_cast<TruncInst>(V))
    return EqualTypeSize(Trunc);
  return false;
}

/// Return true if V will require any promoted values to be truncated for the
/// the IR to remain valid. We can't mutate the value type of these
/// instructions.
static bool isSink(Value *V) {
  // TODO The truncate also isn't actually necessary because we would already
  // proved that the data value is kept within the range of the original data
  // type.

  // Sinks are:
  // - points where the value in the register is being observed, such as an
  //   icmp, switch or store.
  // - points where value types have to match, such as calls and returns.
  // - zext are included to ease the transformation and are generally removed
  //   later on.
  if (auto *Store = dyn_cast<StoreInst>(V))
    return LessOrEqualTypeSize(Store->getValueOperand());
  if (auto *Return = dyn_cast<ReturnInst>(V))
    return LessOrEqualTypeSize(Return->getReturnValue());
  if (auto *ZExt = dyn_cast<ZExtInst>(V))
    return GreaterThanTypeSize(ZExt);
  if (auto *Switch = dyn_cast<SwitchInst>(V))
    return LessThanTypeSize(Switch->getCondition());
  if (auto *ICmp = dyn_cast<ICmpInst>(V))
    return ICmp->isSigned() || LessThanTypeSize(ICmp->getOperand(0));

  return isa<CallInst>(V);
}

/// Return whether the instruction can be promoted within any modifications to
/// its operands or result.
bool ARMCodeGenPrepare::isSafeOverflow(Instruction *I) {
  // FIXME Do we need NSW too?
  if (isa<OverflowingBinaryOperator>(I) && I->hasNoUnsignedWrap())
    return true;

  // We can support a, potentially, overflowing instruction (I) if:
  // - It is only used by an unsigned icmp.
  // - The icmp uses a constant.
  // - The overflowing value (I) is decreasing, i.e would underflow - wrapping
  //   around zero to become a larger number than before.
  // - The underflowing instruction (I) also uses a constant.
  //
  // We can then use the two constants to calculate whether the result would
  // wrap in respect to itself in the original bitwidth. If it doesn't wrap,
  // just underflows the range, the icmp would give the same result whether the
  // result has been truncated or not. We calculate this by:
  // - Zero extending both constants, if needed, to 32-bits.
  // - Take the absolute value of I's constant, adding this to the icmp const.
  // - Check that this value is not out of range for small type. If it is, it
  //   means that it has underflowed enough to wrap around the icmp constant.
  //
  // For example:
  //
  // %sub = sub i8 %a, 2
  // %cmp = icmp ule i8 %sub, 254
  //
  // If %a = 0, %sub = -2 == FE == 254
  // But if this is evalulated as a i32
  // %sub = -2 == FF FF FF FE == 4294967294
  // So the unsigned compares (i8 and i32) would not yield the same result.
  //
  // Another way to look at it is:
  // %a - 2 <= 254
  // %a + 2 <= 254 + 2
  // %a <= 256
  // And we can't represent 256 in the i8 format, so we don't support it.
  //
  // Whereas:
  //
  // %sub i8 %a, 1
  // %cmp = icmp ule i8 %sub, 254
  //
  // If %a = 0, %sub = -1 == FF == 255
  // As i32:
  // %sub = -1 == FF FF FF FF == 4294967295
  //
  // In this case, the unsigned compare results would be the same and this
  // would also be true for ult, uge and ugt:
  // - (255 < 254) == (0xFFFFFFFF < 254) == false
  // - (255 <= 254) == (0xFFFFFFFF <= 254) == false
  // - (255 > 254) == (0xFFFFFFFF > 254) == true
  // - (255 >= 254) == (0xFFFFFFFF >= 254) == true
  //
  // To demonstrate why we can't handle increasing values:
  // 
  // %add = add i8 %a, 2
  // %cmp = icmp ult i8 %add, 127
  //
  // If %a = 254, %add = 256 == (i8 1)
  // As i32:
  // %add = 256
  //
  // (1 < 127) != (256 < 127)

  unsigned Opc = I->getOpcode();
  if (Opc != Instruction::Add && Opc != Instruction::Sub)
    return false;

  if (!I->hasOneUse() ||
      !isa<ICmpInst>(*I->user_begin()) ||
      !isa<ConstantInt>(I->getOperand(1)))
    return false;

  ConstantInt *OverflowConst = cast<ConstantInt>(I->getOperand(1));
  bool NegImm = OverflowConst->isNegative();
  bool IsDecreasing = ((Opc == Instruction::Sub) && !NegImm) ||
                       ((Opc == Instruction::Add) && NegImm);
  if (!IsDecreasing)
    return false;

  // Don't support an icmp that deals with sign bits.
  auto *CI = cast<ICmpInst>(*I->user_begin());
  if (CI->isSigned() || CI->isEquality())
    return false;

  ConstantInt *ICmpConst = nullptr;
  if (auto *Const = dyn_cast<ConstantInt>(CI->getOperand(0)))
    ICmpConst = Const;
  else if (auto *Const = dyn_cast<ConstantInt>(CI->getOperand(1)))
    ICmpConst = Const;
  else
    return false;

  // Now check that the result can't wrap on itself.
  APInt Total = ICmpConst->getValue().getBitWidth() < 32 ?
    ICmpConst->getValue().zext(32) : ICmpConst->getValue();

  Total += OverflowConst->getValue().getBitWidth() < 32 ?
    OverflowConst->getValue().abs().zext(32) : OverflowConst->getValue().abs();

  APInt Max = APInt::getAllOnesValue(ARMCodeGenPrepare::TypeSize);

  if (Total.getBitWidth() > Max.getBitWidth()) {
    if (Total.ugt(Max.zext(Total.getBitWidth())))
      return false;
  } else if (Max.getBitWidth() > Total.getBitWidth()) {
    if (Total.zext(Max.getBitWidth()).ugt(Max))
      return false;
  } else if (Total.ugt(Max))
    return false;

  LLVM_DEBUG(dbgs() << "ARM CGP: Allowing safe overflow for " << *I << "\n");
  return true;
}

static bool shouldPromote(Value *V) {
  if (!isa<IntegerType>(V->getType()) || isSink(V))
    return false;

  if (isSource(V))
    return true;

  auto *I = dyn_cast<Instruction>(V);
  if (!I)
    return false;

  if (isa<ICmpInst>(I))
    return false;

  return true;
}

/// Return whether we can safely mutate V's type to ExtTy without having to be
/// concerned with zero extending or truncation.
static bool isPromotedResultSafe(Value *V) {
  if (!isa<Instruction>(V))
    return true;

  if (generateSignBits(V))
    return false;

  return !isa<OverflowingBinaryOperator>(V);
}

/// Return the intrinsic for the instruction that can perform the same
/// operation but on a narrow type. This is using the parallel dsp intrinsics
/// on scalar values.
static Intrinsic::ID getNarrowIntrinsic(Instruction *I) {
  // Whether we use the signed or unsigned versions of these intrinsics
  // doesn't matter because we're not using the GE bits that they set in
  // the APSR.
  switch(I->getOpcode()) {
  default:
    break;
  case Instruction::Add:
    return ARMCodeGenPrepare::TypeSize == 16 ? Intrinsic::arm_uadd16 :
      Intrinsic::arm_uadd8;
  case Instruction::Sub:
    return ARMCodeGenPrepare::TypeSize == 16 ? Intrinsic::arm_usub16 :
      Intrinsic::arm_usub8;
  }
  llvm_unreachable("unhandled opcode for narrow intrinsic");
}

void IRPromoter::ReplaceAllUsersOfWith(Value *From, Value *To) {
  SmallVector<Instruction*, 4> Users;
  Instruction *InstTo = dyn_cast<Instruction>(To);
  bool ReplacedAll = true;

  LLVM_DEBUG(dbgs() << "ARM CGP: Replacing " << *From << " with " << *To
             << "\n");

  for (Use &U : From->uses()) {
    auto *User = cast<Instruction>(U.getUser());
    if (InstTo && User->isIdenticalTo(InstTo)) {
      ReplacedAll = false;
      continue;
    }
    Users.push_back(User);
  }

  for (auto *U : Users)
    U->replaceUsesOfWith(From, To);

  if (ReplacedAll)
    if (auto *I = dyn_cast<Instruction>(From))
      InstsToRemove.insert(I);
}

void IRPromoter::PrepareConstants() {
  IRBuilder<> Builder{Ctx};
  // First step is to prepare the instructions for mutation. Most constants
  // just need to be zero extended into their new type, but complications arise
  // because:
  // - For nuw binary operators, negative immediates would need sign extending;
  //   however, instead we'll change them to positive and zext them. We can do
  //   this because:
  //   > The operators that can wrap are: add, sub, mul and shl.
  //   > shl interprets its second operand as unsigned and if the first operand
  //     is an immediate, it will need zext to be nuw.
  //   > I'm assuming mul has to interpret immediates as unsigned for nuw.
  //   > Which leaves the nuw add and sub to be handled; as with shl, if an
  //     immediate is used as operand 0, it will need zext to be nuw.
  // - We also allow add and sub to safely overflow in certain circumstances
  //   and only when the value (operand 0) is being decreased.
  //
  // For adds and subs, that are either nuw or safely wrap and use a negative
  // immediate as operand 1, we create an equivalent instruction using a
  // positive immediate. That positive immediate can then be zext along with
  // all the other immediates later.
  for (auto *V : *Visited) {
    if (!isa<Instruction>(V))
      continue;

    auto *I = cast<Instruction>(V);
    if (SafeToPromote->count(I)) {

      if (!isa<OverflowingBinaryOperator>(I))
        continue;

      if (auto *Const = dyn_cast<ConstantInt>(I->getOperand(1))) {
        if (!Const->isNegative())
          break;

        unsigned Opc = I->getOpcode();
        if (Opc != Instruction::Add && Opc != Instruction::Sub)
          continue;

        LLVM_DEBUG(dbgs() << "ARM CGP: Adjusting " << *I << "\n");
        auto *NewConst = ConstantInt::get(Ctx, Const->getValue().abs());
        Builder.SetInsertPoint(I);
        Value *NewVal = Opc == Instruction::Sub ?
          Builder.CreateAdd(I->getOperand(0), NewConst) :
          Builder.CreateSub(I->getOperand(0), NewConst);
        LLVM_DEBUG(dbgs() << "ARM CGP: New equivalent: " << *NewVal << "\n");

        if (auto *NewInst = dyn_cast<Instruction>(NewVal)) {
          NewInst->copyIRFlags(I);
          NewInsts.insert(NewInst);
        }
        InstsToRemove.insert(I);
        I->replaceAllUsesWith(NewVal);
      }
    }
  }
  for (auto *I : NewInsts)
    Visited->insert(I);
}

void IRPromoter::ExtendSources() {
  IRBuilder<> Builder{Ctx};

  auto InsertZExt = [&](Value *V, Instruction *InsertPt) {
    assert(V->getType() != ExtTy && "zext already extends to i32");
    LLVM_DEBUG(dbgs() << "ARM CGP: Inserting ZExt for " << *V << "\n");
    Builder.SetInsertPoint(InsertPt);
    if (auto *I = dyn_cast<Instruction>(V))
      Builder.SetCurrentDebugLocation(I->getDebugLoc());

    Value *ZExt = Builder.CreateZExt(V, ExtTy);
    if (auto *I = dyn_cast<Instruction>(ZExt)) {
      if (isa<Argument>(V))
        I->moveBefore(InsertPt);
      else
        I->moveAfter(InsertPt);
      NewInsts.insert(I);
    }

    ReplaceAllUsersOfWith(V, ZExt);
  };

  // Now, insert extending instructions between the sources and their users.
  LLVM_DEBUG(dbgs() << "ARM CGP: Promoting sources:\n");
  for (auto V : *Sources) {
    LLVM_DEBUG(dbgs() << " - " << *V << "\n");
    if (auto *I = dyn_cast<Instruction>(V))
      InsertZExt(I, I);
    else if (auto *Arg = dyn_cast<Argument>(V)) {
      BasicBlock &BB = Arg->getParent()->front();
      InsertZExt(Arg, &*BB.getFirstInsertionPt());
    } else {
      llvm_unreachable("unhandled source that needs extending");
    }
    Promoted.insert(V);
  }
}

void IRPromoter::PromoteTree() {
  LLVM_DEBUG(dbgs() << "ARM CGP: Mutating the tree..\n");

  IRBuilder<> Builder{Ctx};

  // Mutate the types of the instructions within the tree. Here we handle
  // constant operands.
  for (auto *V : *Visited) {
    if (Sources->count(V))
      continue;

    auto *I = cast<Instruction>(V);
    if (Sinks->count(I))
      continue;

    for (unsigned i = 0, e = I->getNumOperands(); i < e; ++i) {
      Value *Op = I->getOperand(i);
      if ((Op->getType() == ExtTy) || !isa<IntegerType>(Op->getType()))
        continue;

      if (auto *Const = dyn_cast<ConstantInt>(Op)) {
        Constant *NewConst = ConstantExpr::getZExt(Const, ExtTy);
        I->setOperand(i, NewConst);
      } else if (isa<UndefValue>(Op))
        I->setOperand(i, UndefValue::get(ExtTy));
    }

    if (shouldPromote(I)) {
      I->mutateType(ExtTy);
      Promoted.insert(I);
    }
  }

  // Finally, any instructions that should be promoted but haven't yet been,
  // need to be handled using intrinsics.
  for (auto *V : *Visited) {
    auto *I = dyn_cast<Instruction>(V);
    if (!I)
      continue;

    if (Sources->count(I) || Sinks->count(I))
      continue;

    if (!shouldPromote(I) || SafeToPromote->count(I) || NewInsts.count(I))
      continue;
  
    assert(EnableDSP && "DSP intrinisc insertion not enabled!");

    // Replace unsafe instructions with appropriate intrinsic calls.
    LLVM_DEBUG(dbgs() << "ARM CGP: Inserting DSP intrinsic for "
               << *I << "\n");
    Function *DSPInst =
      Intrinsic::getDeclaration(M, getNarrowIntrinsic(I));
    Builder.SetInsertPoint(I);
    Builder.SetCurrentDebugLocation(I->getDebugLoc());
    Value *Args[] = { I->getOperand(0), I->getOperand(1) };
    CallInst *Call = Builder.CreateCall(DSPInst, Args);
    NewInsts.insert(Call);
    ReplaceAllUsersOfWith(I, Call);
  }
}

void IRPromoter::TruncateSinks() {
  LLVM_DEBUG(dbgs() << "ARM CGP: Fixing up the sinks:\n");

  IRBuilder<> Builder{Ctx};

  auto InsertTrunc = [&](Value *V, Type *TruncTy) -> Instruction* {
    if (!isa<Instruction>(V) || !isa<IntegerType>(V->getType()))
      return nullptr;

    if ((!Promoted.count(V) && !NewInsts.count(V)) || Sources->count(V))
      return nullptr;

    LLVM_DEBUG(dbgs() << "ARM CGP: Creating " << *TruncTy << " Trunc for "
               << *V << "\n");
    Builder.SetInsertPoint(cast<Instruction>(V));
    auto *Trunc = dyn_cast<Instruction>(Builder.CreateTrunc(V, TruncTy));
    if (Trunc)
      NewInsts.insert(Trunc);
    return Trunc;
  };

  // Fix up any stores or returns that use the results of the promoted
  // chain.
  for (auto I : *Sinks) {
    LLVM_DEBUG(dbgs() << "ARM CGP: For Sink: " << *I << "\n");

    // Handle calls separately as we need to iterate over arg operands.
    if (auto *Call = dyn_cast<CallInst>(I)) {
      for (unsigned i = 0; i < Call->getNumArgOperands(); ++i) {
        Value *Arg = Call->getArgOperand(i);
        Type *Ty = TruncTysMap[Call][i];
        if (Instruction *Trunc = InsertTrunc(Arg, Ty)) {
          Trunc->moveBefore(Call);
          Call->setArgOperand(i, Trunc);
        }
      }
      continue;
    }

    // Special case switches because we need to truncate the condition.
    if (auto *Switch = dyn_cast<SwitchInst>(I)) {
      Type *Ty = TruncTysMap[Switch][0];
      if (Instruction *Trunc = InsertTrunc(Switch->getCondition(), Ty)) {
        Trunc->moveBefore(Switch);
        Switch->setCondition(Trunc);
      }
      continue;
    }

    // Now handle the others.
    for (unsigned i = 0; i < I->getNumOperands(); ++i) {
      Type *Ty = TruncTysMap[I][i];
      if (Instruction *Trunc = InsertTrunc(I->getOperand(i), Ty)) {
        Trunc->moveBefore(I);
        I->setOperand(i, Trunc);
      }
    }
  }
}

void IRPromoter::Cleanup() {
  // Some zexts will now have become redundant, along with their trunc
  // operands, so remove them
  for (auto V : *Visited) {
    if (!isa<CastInst>(V))
      continue;

    auto ZExt = cast<CastInst>(V);
    if (ZExt->getDestTy() != ExtTy)
      continue;

    Value *Src = ZExt->getOperand(0);
    if (ZExt->getSrcTy() == ZExt->getDestTy()) {
      LLVM_DEBUG(dbgs() << "ARM CGP: Removing unnecessary cast: " << *ZExt
                 << "\n");
      ReplaceAllUsersOfWith(ZExt, Src);
      continue;
    }

    // For any truncs that we insert to handle zexts, we can replace the
    // result of the zext with the input to the trunc.
    if (NewInsts.count(Src) && isa<ZExtInst>(V) && isa<TruncInst>(Src)) {
      auto *Trunc = cast<TruncInst>(Src);
      assert(Trunc->getOperand(0)->getType() == ExtTy &&
             "expected inserted trunc to be operating on i32");
      ReplaceAllUsersOfWith(ZExt, Trunc->getOperand(0));
    }
  }

  for (auto *I : InstsToRemove) {
    LLVM_DEBUG(dbgs() << "ARM CGP: Removing " << *I << "\n");
    I->dropAllReferences();
    I->eraseFromParent();
  }

  InstsToRemove.clear();
  NewInsts.clear();
  TruncTysMap.clear();
  Promoted.clear();
}

void IRPromoter::ConvertTruncs() {
  IRBuilder<> Builder{Ctx};

  for (auto *V : *Visited) {
    if (!isa<TruncInst>(V) || Sources->count(V))
      continue;

    auto *Trunc = cast<TruncInst>(V);
    assert(LessThanTypeSize(Trunc) && "expected narrow trunc");

    Builder.SetInsertPoint(Trunc);
    unsigned NumBits =
      cast<IntegerType>(Trunc->getType())->getScalarSizeInBits();
    ConstantInt *Mask = ConstantInt::get(Ctx, APInt::getMaxValue(NumBits));
    Value *Masked = Builder.CreateAnd(Trunc->getOperand(0), Mask);

    if (auto *I = dyn_cast<Instruction>(Masked))
      NewInsts.insert(I);

    ReplaceAllUsersOfWith(Trunc, Masked);
  }
}

void IRPromoter::Mutate(Type *OrigTy,
                        SmallPtrSetImpl<Value*> &Visited,
                        SmallPtrSetImpl<Value*> &Sources,
                        SmallPtrSetImpl<Instruction*> &Sinks,
                        SmallPtrSetImpl<Instruction*> &SafeToPromote) {
  LLVM_DEBUG(dbgs() << "ARM CGP: Promoting use-def chains to from "
             << ARMCodeGenPrepare::TypeSize << " to 32-bits\n");

  assert(isa<IntegerType>(OrigTy) && "expected integer type");
  this->OrigTy = cast<IntegerType>(OrigTy);
  assert(OrigTy->getPrimitiveSizeInBits() < ExtTy->getPrimitiveSizeInBits() &&
         "original type not smaller than extended type");

  this->Visited = &Visited;
  this->Sources = &Sources;
  this->Sinks = &Sinks;
  this->SafeToPromote = &SafeToPromote;

  // Cache original types of the values that will likely need truncating
  for (auto *I : Sinks) {
    if (auto *Call = dyn_cast<CallInst>(I)) {
      for (unsigned i = 0; i < Call->getNumArgOperands(); ++i) {
        Value *Arg = Call->getArgOperand(i);
        TruncTysMap[Call].push_back(Arg->getType());
      }
    } else if (auto *Switch = dyn_cast<SwitchInst>(I))
      TruncTysMap[I].push_back(Switch->getCondition()->getType());
    else {
      for (unsigned i = 0; i < I->getNumOperands(); ++i)
        TruncTysMap[I].push_back(I->getOperand(i)->getType());
    }
  }

  // Convert adds and subs using negative immediates to equivalent instructions
  // that use positive constants.
  PrepareConstants();

  // Insert zext instructions between sources and their users.
  ExtendSources();

  // Convert any truncs, that aren't sources, into AND masks.
  ConvertTruncs();

  // Promote visited instructions, mutating their types in place. Also insert
  // DSP intrinsics, if enabled, for adds and subs which would be unsafe to
  // promote.
  PromoteTree();

  // Insert trunc instructions for use by calls, stores etc...
  TruncateSinks();

  // Finally, remove unecessary zexts and truncs, delete old instructions and
  // clear the data structures.
  Cleanup();

  LLVM_DEBUG(dbgs() << "ARM CGP: Mutation complete\n");
}

/// We accept most instructions, as well as Arguments and ConstantInsts. We
/// Disallow casts other than zext and truncs and only allow calls if their
/// return value is zeroext. We don't allow opcodes that can introduce sign
/// bits.
bool ARMCodeGenPrepare::isSupportedValue(Value *V) {
  if (auto *I = dyn_cast<ICmpInst>(V)) {
    // Now that we allow small types than TypeSize, only allow icmp of
    // TypeSize because they will require a trunc to be legalised.
    // TODO: Allow icmp of smaller types, and calculate at the end
    // whether the transform would be beneficial.
    if (isa<PointerType>(I->getOperand(0)->getType()))
      return true;
    return EqualTypeSize(I->getOperand(0));
  }

  // Memory instructions
  if (isa<StoreInst>(V) || isa<GetElementPtrInst>(V))
    return true;

  // Branches and targets.
  if( isa<BranchInst>(V) || isa<SwitchInst>(V) || isa<BasicBlock>(V))
    return true;

  // Non-instruction values that we can handle.
  if ((isa<Constant>(V) && !isa<ConstantExpr>(V)) || isa<Argument>(V))
    return isSupportedType(V);

  if (isa<PHINode>(V) || isa<SelectInst>(V) || isa<ReturnInst>(V) ||
      isa<LoadInst>(V))
    return isSupportedType(V);

  if (isa<SExtInst>(V))
    return false;

  if (auto *Cast = dyn_cast<CastInst>(V))
    return isSupportedType(Cast) || isSupportedType(Cast->getOperand(0));

  // Special cases for calls as we need to check for zeroext
  // TODO We should accept calls even if they don't have zeroext, as they can
  // still be sinks.
  if (auto *Call = dyn_cast<CallInst>(V))
    return isSupportedType(Call) &&
           Call->hasRetAttr(Attribute::AttrKind::ZExt);

  if (!isa<BinaryOperator>(V))
    return false;

  if (!isSupportedType(V))
    return false;

  if (generateSignBits(V)) {
    LLVM_DEBUG(dbgs() << "ARM CGP: No, instruction can generate sign bits.\n");
    return false;
  }
  return true;
}

/// Check that the type of V would be promoted and that the original type is
/// smaller than the targeted promoted type. Check that we're not trying to
/// promote something larger than our base 'TypeSize' type.
bool ARMCodeGenPrepare::isLegalToPromote(Value *V) {

  auto *I = dyn_cast<Instruction>(V);
  if (!I)
    return true;

  if (SafeToPromote.count(I))
   return true;

  if (isPromotedResultSafe(V) || isSafeOverflow(I)) {
    SafeToPromote.insert(I);
    return true;
  }

  if (I->getOpcode() != Instruction::Add && I->getOpcode() != Instruction::Sub)
    return false;

  // If promotion is not safe, can we use a DSP instruction to natively
  // handle the narrow type?
  if (!ST->hasDSP() || !EnableDSP || !isSupportedType(I))
    return false;

  if (ST->isThumb() && !ST->hasThumb2())
    return false;

  // TODO
  // Would it be profitable? For Thumb code, these parallel DSP instructions
  // are only Thumb-2, so we wouldn't be able to dual issue on Cortex-M33. For
  // Cortex-A, specifically Cortex-A72, the latency is double and throughput is
  // halved. They also do not take immediates as operands.
  for (auto &Op : I->operands()) {
    if (isa<Constant>(Op)) {
      if (!EnableDSPWithImms)
        return false;
    }
  }
  LLVM_DEBUG(dbgs() << "ARM CGP: Will use an intrinsic for: " << *I << "\n");
  return true;
}

bool ARMCodeGenPrepare::TryToPromote(Value *V) {
  OrigTy = V->getType();
  TypeSize = OrigTy->getPrimitiveSizeInBits();
  if (TypeSize > 16 || TypeSize < 8)
    return false;

  SafeToPromote.clear();

  if (!isSupportedValue(V) || !shouldPromote(V) || !isLegalToPromote(V))
    return false;

  LLVM_DEBUG(dbgs() << "ARM CGP: TryToPromote: " << *V << ", TypeSize = "
             << TypeSize << "\n");

  SetVector<Value*> WorkList;
  SmallPtrSet<Value*, 8> Sources;
  SmallPtrSet<Instruction*, 4> Sinks;
  SmallPtrSet<Value*, 16> CurrentVisited;
  WorkList.insert(V);

  // Return true if V was added to the worklist as a supported instruction,
  // if it was already visited, or if we don't need to explore it (e.g.
  // pointer values and GEPs), and false otherwise.
  auto AddLegalInst = [&](Value *V) {
    if (CurrentVisited.count(V))
      return true;

    // Ignore GEPs because they don't need promoting and the constant indices
    // will prevent the transformation.
    if (isa<GetElementPtrInst>(V))
      return true;

    if (!isSupportedValue(V) || (shouldPromote(V) && !isLegalToPromote(V))) {
      LLVM_DEBUG(dbgs() << "ARM CGP: Can't handle: " << *V << "\n");
      return false;
    }

    WorkList.insert(V);
    return true;
  };

  // Iterate through, and add to, a tree of operands and users in the use-def.
  while (!WorkList.empty()) {
    Value *V = WorkList.back();
    WorkList.pop_back();
    if (CurrentVisited.count(V))
      continue;

    // Ignore non-instructions, other than arguments.
    if (!isa<Instruction>(V) && !isSource(V))
      continue;

    // If we've already visited this value from somewhere, bail now because
    // the tree has already been explored.
    // TODO: This could limit the transform, ie if we try to promote something
    // from an i8 and fail first, before trying an i16.
    if (AllVisited.count(V))
      return false;

    CurrentVisited.insert(V);
    AllVisited.insert(V);

    // Calls can be both sources and sinks.
    if (isSink(V))
      Sinks.insert(cast<Instruction>(V));

    if (isSource(V))
      Sources.insert(V);

    if (!isSink(V) && !isSource(V)) {
      if (auto *I = dyn_cast<Instruction>(V)) {
        // Visit operands of any instruction visited.
        for (auto &U : I->operands()) {
          if (!AddLegalInst(U))
            return false;
        }
      }
    }

    // Don't visit users of a node which isn't going to be mutated unless its a
    // source.
    if (isSource(V) || shouldPromote(V)) {
      for (Use &U : V->uses()) {
        if (!AddLegalInst(U.getUser()))
          return false;
      }
    }
  }

  LLVM_DEBUG(dbgs() << "ARM CGP: Visited nodes:\n";
             for (auto *I : CurrentVisited)
               I->dump();
             );
  unsigned ToPromote = 0;
  for (auto *V : CurrentVisited) {
    if (Sources.count(V))
      continue;
    if (Sinks.count(cast<Instruction>(V)))
      continue;
    ++ToPromote;
  }

  if (ToPromote < 2)
    return false;

  Promoter->Mutate(OrigTy, CurrentVisited, Sources, Sinks, SafeToPromote);
  return true;
}

bool ARMCodeGenPrepare::doInitialization(Module &M) {
  Promoter = new IRPromoter(&M);
  return false;
}

bool ARMCodeGenPrepare::runOnFunction(Function &F) {
  if (skipFunction(F) || DisableCGP)
    return false;

  auto *TPC = &getAnalysis<TargetPassConfig>();
  if (!TPC)
    return false;

  const TargetMachine &TM = TPC->getTM<TargetMachine>();
  ST = &TM.getSubtarget<ARMSubtarget>(F);
  bool MadeChange = false;
  LLVM_DEBUG(dbgs() << "ARM CGP: Running on " << F.getName() << "\n");

  // Search up from icmps to try to promote their operands.
  for (BasicBlock &BB : F) {
    auto &Insts = BB.getInstList();
    for (auto &I : Insts) {
      if (AllVisited.count(&I))
        continue;

      if (isa<ICmpInst>(I)) {
        auto &CI = cast<ICmpInst>(I);

        // Skip signed or pointer compares
        if (CI.isSigned() || !isa<IntegerType>(CI.getOperand(0)->getType()))
          continue;

        LLVM_DEBUG(dbgs() << "ARM CGP: Searching from: " << CI << "\n");

        for (auto &Op : CI.operands()) {
          if (auto *I = dyn_cast<Instruction>(Op))
            MadeChange |= TryToPromote(I);
        }
      }
    }
    LLVM_DEBUG(if (verifyFunction(F, &dbgs())) {
                dbgs() << F;
                report_fatal_error("Broken function after type promotion");
               });
  }
  if (MadeChange)
    LLVM_DEBUG(dbgs() << "After ARMCodeGenPrepare: " << F << "\n");

  return MadeChange;
}

bool ARMCodeGenPrepare::doFinalization(Module &M) {
  delete Promoter;
  return false;
}

INITIALIZE_PASS_BEGIN(ARMCodeGenPrepare, DEBUG_TYPE,
                      "ARM IR optimizations", false, false)
INITIALIZE_PASS_END(ARMCodeGenPrepare, DEBUG_TYPE, "ARM IR optimizations",
                    false, false)

char ARMCodeGenPrepare::ID = 0;
unsigned ARMCodeGenPrepare::TypeSize = 0;

FunctionPass *llvm::createARMCodeGenPreparePass() {
  return new ARMCodeGenPrepare();
}
