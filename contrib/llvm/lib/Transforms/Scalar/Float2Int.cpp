//===- Float2Int.cpp - Demote floating point ops to work on integers ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Float2Int pass, which aims to demote floating
// point operations to work on integers, where that is losslessly possible.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "float2int"

#include "llvm/Transforms/Scalar/Float2Int.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include <deque>
#include <functional> // For std::function
using namespace llvm;

// The algorithm is simple. Start at instructions that convert from the
// float to the int domain: fptoui, fptosi and fcmp. Walk up the def-use
// graph, using an equivalence datastructure to unify graphs that interfere.
//
// Mappable instructions are those with an integer corrollary that, given
// integer domain inputs, produce an integer output; fadd, for example.
//
// If a non-mappable instruction is seen, this entire def-use graph is marked
// as non-transformable. If we see an instruction that converts from the
// integer domain to FP domain (uitofp,sitofp), we terminate our walk.

/// The largest integer type worth dealing with.
static cl::opt<unsigned>
MaxIntegerBW("float2int-max-integer-bw", cl::init(64), cl::Hidden,
             cl::desc("Max integer bitwidth to consider in float2int"
                      "(default=64)"));

namespace {
  struct Float2IntLegacyPass : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    Float2IntLegacyPass() : FunctionPass(ID) {
      initializeFloat2IntLegacyPassPass(*PassRegistry::getPassRegistry());
    }

    bool runOnFunction(Function &F) override {
      if (skipFunction(F))
        return false;

      return Impl.runImpl(F);
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      AU.addPreserved<GlobalsAAWrapperPass>();
    }

  private:
    Float2IntPass Impl;
  };
}

char Float2IntLegacyPass::ID = 0;
INITIALIZE_PASS(Float2IntLegacyPass, "float2int", "Float to int", false, false)

// Given a FCmp predicate, return a matching ICmp predicate if one
// exists, otherwise return BAD_ICMP_PREDICATE.
static CmpInst::Predicate mapFCmpPred(CmpInst::Predicate P) {
  switch (P) {
  case CmpInst::FCMP_OEQ:
  case CmpInst::FCMP_UEQ:
    return CmpInst::ICMP_EQ;
  case CmpInst::FCMP_OGT:
  case CmpInst::FCMP_UGT:
    return CmpInst::ICMP_SGT;
  case CmpInst::FCMP_OGE:
  case CmpInst::FCMP_UGE:
    return CmpInst::ICMP_SGE;
  case CmpInst::FCMP_OLT:
  case CmpInst::FCMP_ULT:
    return CmpInst::ICMP_SLT;
  case CmpInst::FCMP_OLE:
  case CmpInst::FCMP_ULE:
    return CmpInst::ICMP_SLE;
  case CmpInst::FCMP_ONE:
  case CmpInst::FCMP_UNE:
    return CmpInst::ICMP_NE;
  default:
    return CmpInst::BAD_ICMP_PREDICATE;
  }
}

// Given a floating point binary operator, return the matching
// integer version.
static Instruction::BinaryOps mapBinOpcode(unsigned Opcode) {
  switch (Opcode) {
  default: llvm_unreachable("Unhandled opcode!");
  case Instruction::FAdd: return Instruction::Add;
  case Instruction::FSub: return Instruction::Sub;
  case Instruction::FMul: return Instruction::Mul;
  }
}

// Find the roots - instructions that convert from the FP domain to
// integer domain.
void Float2IntPass::findRoots(Function &F, SmallPtrSet<Instruction*,8> &Roots) {
  for (auto &I : instructions(F)) {
    if (isa<VectorType>(I.getType()))
      continue;
    switch (I.getOpcode()) {
    default: break;
    case Instruction::FPToUI:
    case Instruction::FPToSI:
      Roots.insert(&I);
      break;
    case Instruction::FCmp:
      if (mapFCmpPred(cast<CmpInst>(&I)->getPredicate()) !=
          CmpInst::BAD_ICMP_PREDICATE)
        Roots.insert(&I);
      break;
    }
  }
}

// Helper - mark I as having been traversed, having range R.
void Float2IntPass::seen(Instruction *I, ConstantRange R) {
  LLVM_DEBUG(dbgs() << "F2I: " << *I << ":" << R << "\n");
  auto IT = SeenInsts.find(I);
  if (IT != SeenInsts.end())
    IT->second = std::move(R);
  else
    SeenInsts.insert(std::make_pair(I, std::move(R)));
}

// Helper - get a range representing a poison value.
ConstantRange Float2IntPass::badRange() {
  return ConstantRange(MaxIntegerBW + 1, true);
}
ConstantRange Float2IntPass::unknownRange() {
  return ConstantRange(MaxIntegerBW + 1, false);
}
ConstantRange Float2IntPass::validateRange(ConstantRange R) {
  if (R.getBitWidth() > MaxIntegerBW + 1)
    return badRange();
  return R;
}

// The most obvious way to structure the search is a depth-first, eager
// search from each root. However, that require direct recursion and so
// can only handle small instruction sequences. Instead, we split the search
// up into two phases:
//   - walkBackwards:  A breadth-first walk of the use-def graph starting from
//                     the roots. Populate "SeenInsts" with interesting
//                     instructions and poison values if they're obvious and
//                     cheap to compute. Calculate the equivalance set structure
//                     while we're here too.
//   - walkForwards:  Iterate over SeenInsts in reverse order, so we visit
//                     defs before their uses. Calculate the real range info.

// Breadth-first walk of the use-def graph; determine the set of nodes
// we care about and eagerly determine if some of them are poisonous.
void Float2IntPass::walkBackwards(const SmallPtrSetImpl<Instruction*> &Roots) {
  std::deque<Instruction*> Worklist(Roots.begin(), Roots.end());
  while (!Worklist.empty()) {
    Instruction *I = Worklist.back();
    Worklist.pop_back();

    if (SeenInsts.find(I) != SeenInsts.end())
      // Seen already.
      continue;

    switch (I->getOpcode()) {
      // FIXME: Handle select and phi nodes.
    default:
      // Path terminated uncleanly.
      seen(I, badRange());
      break;

    case Instruction::UIToFP:
    case Instruction::SIToFP: {
      // Path terminated cleanly - use the type of the integer input to seed
      // the analysis.
      unsigned BW = I->getOperand(0)->getType()->getPrimitiveSizeInBits();
      auto Input = ConstantRange(BW, true);
      auto CastOp = (Instruction::CastOps)I->getOpcode();
      seen(I, validateRange(Input.castOp(CastOp, MaxIntegerBW+1)));
      continue;
    }

    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::FCmp:
      seen(I, unknownRange());
      break;
    }

    for (Value *O : I->operands()) {
      if (Instruction *OI = dyn_cast<Instruction>(O)) {
        // Unify def-use chains if they interfere.
        ECs.unionSets(I, OI);
        if (SeenInsts.find(I)->second != badRange())
          Worklist.push_back(OI);
      } else if (!isa<ConstantFP>(O)) {
        // Not an instruction or ConstantFP? we can't do anything.
        seen(I, badRange());
      }
    }
  }
}

// Walk forwards down the list of seen instructions, so we visit defs before
// uses.
void Float2IntPass::walkForwards() {
  for (auto &It : reverse(SeenInsts)) {
    if (It.second != unknownRange())
      continue;

    Instruction *I = It.first;
    std::function<ConstantRange(ArrayRef<ConstantRange>)> Op;
    switch (I->getOpcode()) {
      // FIXME: Handle select and phi nodes.
    default:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
      llvm_unreachable("Should have been handled in walkForwards!");

    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
      Op = [I](ArrayRef<ConstantRange> Ops) {
        assert(Ops.size() == 2 && "its a binary operator!");
        auto BinOp = (Instruction::BinaryOps) I->getOpcode();
        return Ops[0].binaryOp(BinOp, Ops[1]);
      };
      break;

    //
    // Root-only instructions - we'll only see these if they're the
    //                          first node in a walk.
    //
    case Instruction::FPToUI:
    case Instruction::FPToSI:
      Op = [I](ArrayRef<ConstantRange> Ops) {
        assert(Ops.size() == 1 && "FPTo[US]I is a unary operator!");
        // Note: We're ignoring the casts output size here as that's what the
        // caller expects.
        auto CastOp = (Instruction::CastOps)I->getOpcode();
        return Ops[0].castOp(CastOp, MaxIntegerBW+1);
      };
      break;

    case Instruction::FCmp:
      Op = [](ArrayRef<ConstantRange> Ops) {
        assert(Ops.size() == 2 && "FCmp is a binary operator!");
        return Ops[0].unionWith(Ops[1]);
      };
      break;
    }

    bool Abort = false;
    SmallVector<ConstantRange,4> OpRanges;
    for (Value *O : I->operands()) {
      if (Instruction *OI = dyn_cast<Instruction>(O)) {
        assert(SeenInsts.find(OI) != SeenInsts.end() &&
               "def not seen before use!");
        OpRanges.push_back(SeenInsts.find(OI)->second);
      } else if (ConstantFP *CF = dyn_cast<ConstantFP>(O)) {
        // Work out if the floating point number can be losslessly represented
        // as an integer.
        // APFloat::convertToInteger(&Exact) purports to do what we want, but
        // the exactness can be too precise. For example, negative zero can
        // never be exactly converted to an integer.
        //
        // Instead, we ask APFloat to round itself to an integral value - this
        // preserves sign-of-zero - then compare the result with the original.
        //
        const APFloat &F = CF->getValueAPF();

        // First, weed out obviously incorrect values. Non-finite numbers
        // can't be represented and neither can negative zero, unless
        // we're in fast math mode.
        if (!F.isFinite() ||
            (F.isZero() && F.isNegative() && isa<FPMathOperator>(I) &&
             !I->hasNoSignedZeros())) {
          seen(I, badRange());
          Abort = true;
          break;
        }

        APFloat NewF = F;
        auto Res = NewF.roundToIntegral(APFloat::rmNearestTiesToEven);
        if (Res != APFloat::opOK || NewF.compare(F) != APFloat::cmpEqual) {
          seen(I, badRange());
          Abort = true;
          break;
        }
        // OK, it's representable. Now get it.
        APSInt Int(MaxIntegerBW+1, false);
        bool Exact;
        CF->getValueAPF().convertToInteger(Int,
                                           APFloat::rmNearestTiesToEven,
                                           &Exact);
        OpRanges.push_back(ConstantRange(Int));
      } else {
        llvm_unreachable("Should have already marked this as badRange!");
      }
    }

    // Reduce the operands' ranges to a single range and return.
    if (!Abort)
      seen(I, Op(OpRanges));
  }
}

// If there is a valid transform to be done, do it.
bool Float2IntPass::validateAndTransform() {
  bool MadeChange = false;

  // Iterate over every disjoint partition of the def-use graph.
  for (auto It = ECs.begin(), E = ECs.end(); It != E; ++It) {
    ConstantRange R(MaxIntegerBW + 1, false);
    bool Fail = false;
    Type *ConvertedToTy = nullptr;

    // For every member of the partition, union all the ranges together.
    for (auto MI = ECs.member_begin(It), ME = ECs.member_end();
         MI != ME; ++MI) {
      Instruction *I = *MI;
      auto SeenI = SeenInsts.find(I);
      if (SeenI == SeenInsts.end())
        continue;

      R = R.unionWith(SeenI->second);
      // We need to ensure I has no users that have not been seen.
      // If it does, transformation would be illegal.
      //
      // Don't count the roots, as they terminate the graphs.
      if (Roots.count(I) == 0) {
        // Set the type of the conversion while we're here.
        if (!ConvertedToTy)
          ConvertedToTy = I->getType();
        for (User *U : I->users()) {
          Instruction *UI = dyn_cast<Instruction>(U);
          if (!UI || SeenInsts.find(UI) == SeenInsts.end()) {
            LLVM_DEBUG(dbgs() << "F2I: Failing because of " << *U << "\n");
            Fail = true;
            break;
          }
        }
      }
      if (Fail)
        break;
    }

    // If the set was empty, or we failed, or the range is poisonous,
    // bail out.
    if (ECs.member_begin(It) == ECs.member_end() || Fail ||
        R.isFullSet() || R.isSignWrappedSet())
      continue;
    assert(ConvertedToTy && "Must have set the convertedtoty by this point!");

    // The number of bits required is the maximum of the upper and
    // lower limits, plus one so it can be signed.
    unsigned MinBW = std::max(R.getLower().getMinSignedBits(),
                              R.getUpper().getMinSignedBits()) + 1;
    LLVM_DEBUG(dbgs() << "F2I: MinBitwidth=" << MinBW << ", R: " << R << "\n");

    // If we've run off the realms of the exactly representable integers,
    // the floating point result will differ from an integer approximation.

    // Do we need more bits than are in the mantissa of the type we converted
    // to? semanticsPrecision returns the number of mantissa bits plus one
    // for the sign bit.
    unsigned MaxRepresentableBits
      = APFloat::semanticsPrecision(ConvertedToTy->getFltSemantics()) - 1;
    if (MinBW > MaxRepresentableBits) {
      LLVM_DEBUG(dbgs() << "F2I: Value not guaranteed to be representable!\n");
      continue;
    }
    if (MinBW > 64) {
      LLVM_DEBUG(
          dbgs() << "F2I: Value requires more than 64 bits to represent!\n");
      continue;
    }

    // OK, R is known to be representable. Now pick a type for it.
    // FIXME: Pick the smallest legal type that will fit.
    Type *Ty = (MinBW > 32) ? Type::getInt64Ty(*Ctx) : Type::getInt32Ty(*Ctx);

    for (auto MI = ECs.member_begin(It), ME = ECs.member_end();
         MI != ME; ++MI)
      convert(*MI, Ty);
    MadeChange = true;
  }

  return MadeChange;
}

Value *Float2IntPass::convert(Instruction *I, Type *ToTy) {
  if (ConvertedInsts.find(I) != ConvertedInsts.end())
    // Already converted this instruction.
    return ConvertedInsts[I];

  SmallVector<Value*,4> NewOperands;
  for (Value *V : I->operands()) {
    // Don't recurse if we're an instruction that terminates the path.
    if (I->getOpcode() == Instruction::UIToFP ||
        I->getOpcode() == Instruction::SIToFP) {
      NewOperands.push_back(V);
    } else if (Instruction *VI = dyn_cast<Instruction>(V)) {
      NewOperands.push_back(convert(VI, ToTy));
    } else if (ConstantFP *CF = dyn_cast<ConstantFP>(V)) {
      APSInt Val(ToTy->getPrimitiveSizeInBits(), /*IsUnsigned=*/false);
      bool Exact;
      CF->getValueAPF().convertToInteger(Val,
                                         APFloat::rmNearestTiesToEven,
                                         &Exact);
      NewOperands.push_back(ConstantInt::get(ToTy, Val));
    } else {
      llvm_unreachable("Unhandled operand type?");
    }
  }

  // Now create a new instruction.
  IRBuilder<> IRB(I);
  Value *NewV = nullptr;
  switch (I->getOpcode()) {
  default: llvm_unreachable("Unhandled instruction!");

  case Instruction::FPToUI:
    NewV = IRB.CreateZExtOrTrunc(NewOperands[0], I->getType());
    break;

  case Instruction::FPToSI:
    NewV = IRB.CreateSExtOrTrunc(NewOperands[0], I->getType());
    break;

  case Instruction::FCmp: {
    CmpInst::Predicate P = mapFCmpPred(cast<CmpInst>(I)->getPredicate());
    assert(P != CmpInst::BAD_ICMP_PREDICATE && "Unhandled predicate!");
    NewV = IRB.CreateICmp(P, NewOperands[0], NewOperands[1], I->getName());
    break;
  }

  case Instruction::UIToFP:
    NewV = IRB.CreateZExtOrTrunc(NewOperands[0], ToTy);
    break;

  case Instruction::SIToFP:
    NewV = IRB.CreateSExtOrTrunc(NewOperands[0], ToTy);
    break;

  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
    NewV = IRB.CreateBinOp(mapBinOpcode(I->getOpcode()),
                           NewOperands[0], NewOperands[1],
                           I->getName());
    break;
  }

  // If we're a root instruction, RAUW.
  if (Roots.count(I))
    I->replaceAllUsesWith(NewV);

  ConvertedInsts[I] = NewV;
  return NewV;
}

// Perform dead code elimination on the instructions we just modified.
void Float2IntPass::cleanup() {
  for (auto &I : reverse(ConvertedInsts))
    I.first->eraseFromParent();
}

bool Float2IntPass::runImpl(Function &F) {
  LLVM_DEBUG(dbgs() << "F2I: Looking at function " << F.getName() << "\n");
  // Clear out all state.
  ECs = EquivalenceClasses<Instruction*>();
  SeenInsts.clear();
  ConvertedInsts.clear();
  Roots.clear();

  Ctx = &F.getParent()->getContext();

  findRoots(F, Roots);

  walkBackwards(Roots);
  walkForwards();

  bool Modified = validateAndTransform();
  if (Modified)
    cleanup();
  return Modified;
}

namespace llvm {
FunctionPass *createFloat2IntPass() { return new Float2IntLegacyPass(); }

PreservedAnalyses Float2IntPass::run(Function &F, FunctionAnalysisManager &) {
  if (!runImpl(F))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<GlobalsAA>();
  return PA;
}
} // End namespace llvm
