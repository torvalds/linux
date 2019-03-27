//===- InferAddressSpace.cpp - --------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// CUDA C/C++ includes memory space designation as variable type qualifers (such
// as __global__ and __shared__). Knowing the space of a memory access allows
// CUDA compilers to emit faster PTX loads and stores. For example, a load from
// shared memory can be translated to `ld.shared` which is roughly 10% faster
// than a generic `ld` on an NVIDIA Tesla K40c.
//
// Unfortunately, type qualifiers only apply to variable declarations, so CUDA
// compilers must infer the memory space of an address expression from
// type-qualified variables.
//
// LLVM IR uses non-zero (so-called) specific address spaces to represent memory
// spaces (e.g. addrspace(3) means shared memory). The Clang frontend
// places only type-qualified variables in specific address spaces, and then
// conservatively `addrspacecast`s each type-qualified variable to addrspace(0)
// (so-called the generic address space) for other instructions to use.
//
// For example, the Clang translates the following CUDA code
//   __shared__ float a[10];
//   float v = a[i];
// to
//   %0 = addrspacecast [10 x float] addrspace(3)* @a to [10 x float]*
//   %1 = gep [10 x float], [10 x float]* %0, i64 0, i64 %i
//   %v = load float, float* %1 ; emits ld.f32
// @a is in addrspace(3) since it's type-qualified, but its use from %1 is
// redirected to %0 (the generic version of @a).
//
// The optimization implemented in this file propagates specific address spaces
// from type-qualified variable declarations to its users. For example, it
// optimizes the above IR to
//   %1 = gep [10 x float] addrspace(3)* @a, i64 0, i64 %i
//   %v = load float addrspace(3)* %1 ; emits ld.shared.f32
// propagating the addrspace(3) from @a to %1. As the result, the NVPTX
// codegen is able to emit ld.shared.f32 for %v.
//
// Address space inference works in two steps. First, it uses a data-flow
// analysis to infer as many generic pointers as possible to point to only one
// specific address space. In the above example, it can prove that %1 only
// points to addrspace(3). This algorithm was published in
//   CUDA: Compiling and optimizing for a GPU platform
//   Chakrabarti, Grover, Aarts, Kong, Kudlur, Lin, Marathe, Murphy, Wang
//   ICCS 2012
//
// Then, address space inference replaces all refinable generic pointers with
// equivalent specific pointers.
//
// The major challenge of implementing this optimization is handling PHINodes,
// which may create loops in the data flow graph. This brings two complications.
//
// First, the data flow analysis in Step 1 needs to be circular. For example,
//     %generic.input = addrspacecast float addrspace(3)* %input to float*
//   loop:
//     %y = phi [ %generic.input, %y2 ]
//     %y2 = getelementptr %y, 1
//     %v = load %y2
//     br ..., label %loop, ...
// proving %y specific requires proving both %generic.input and %y2 specific,
// but proving %y2 specific circles back to %y. To address this complication,
// the data flow analysis operates on a lattice:
//   uninitialized > specific address spaces > generic.
// All address expressions (our implementation only considers phi, bitcast,
// addrspacecast, and getelementptr) start with the uninitialized address space.
// The monotone transfer function moves the address space of a pointer down a
// lattice path from uninitialized to specific and then to generic. A join
// operation of two different specific address spaces pushes the expression down
// to the generic address space. The analysis completes once it reaches a fixed
// point.
//
// Second, IR rewriting in Step 2 also needs to be circular. For example,
// converting %y to addrspace(3) requires the compiler to know the converted
// %y2, but converting %y2 needs the converted %y. To address this complication,
// we break these cycles using "undef" placeholders. When converting an
// instruction `I` to a new address space, if its operand `Op` is not converted
// yet, we let `I` temporarily use `undef` and fix all the uses of undef later.
// For instance, our algorithm first converts %y to
//   %y' = phi float addrspace(3)* [ %input, undef ]
// Then, it converts %y2 to
//   %y2' = getelementptr %y', 1
// Finally, it fixes the undef in %y' so that
//   %y' = phi float addrspace(3)* [ %input, %y2' ]
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <cassert>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

#define DEBUG_TYPE "infer-address-spaces"

using namespace llvm;

static const unsigned UninitializedAddressSpace =
    std::numeric_limits<unsigned>::max();

namespace {

using ValueToAddrSpaceMapTy = DenseMap<const Value *, unsigned>;

/// InferAddressSpaces
class InferAddressSpaces : public FunctionPass {
  /// Target specific address space which uses of should be replaced if
  /// possible.
  unsigned FlatAddrSpace;

public:
  static char ID;

  InferAddressSpaces() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<TargetTransformInfoWrapperPass>();
  }

  bool runOnFunction(Function &F) override;

private:
  // Returns the new address space of V if updated; otherwise, returns None.
  Optional<unsigned>
  updateAddressSpace(const Value &V,
                     const ValueToAddrSpaceMapTy &InferredAddrSpace) const;

  // Tries to infer the specific address space of each address expression in
  // Postorder.
  void inferAddressSpaces(ArrayRef<WeakTrackingVH> Postorder,
                          ValueToAddrSpaceMapTy *InferredAddrSpace) const;

  bool isSafeToCastConstAddrSpace(Constant *C, unsigned NewAS) const;

  // Changes the flat address expressions in function F to point to specific
  // address spaces if InferredAddrSpace says so. Postorder is the postorder of
  // all flat expressions in the use-def graph of function F.
  bool rewriteWithNewAddressSpaces(
      const TargetTransformInfo &TTI, ArrayRef<WeakTrackingVH> Postorder,
      const ValueToAddrSpaceMapTy &InferredAddrSpace, Function *F) const;

  void appendsFlatAddressExpressionToPostorderStack(
    Value *V, std::vector<std::pair<Value *, bool>> &PostorderStack,
    DenseSet<Value *> &Visited) const;

  bool rewriteIntrinsicOperands(IntrinsicInst *II,
                                Value *OldV, Value *NewV) const;
  void collectRewritableIntrinsicOperands(
    IntrinsicInst *II,
    std::vector<std::pair<Value *, bool>> &PostorderStack,
    DenseSet<Value *> &Visited) const;

  std::vector<WeakTrackingVH> collectFlatAddressExpressions(Function &F) const;

  Value *cloneValueWithNewAddressSpace(
    Value *V, unsigned NewAddrSpace,
    const ValueToValueMapTy &ValueWithNewAddrSpace,
    SmallVectorImpl<const Use *> *UndefUsesToFix) const;
  unsigned joinAddressSpaces(unsigned AS1, unsigned AS2) const;
};

} // end anonymous namespace

char InferAddressSpaces::ID = 0;

namespace llvm {

void initializeInferAddressSpacesPass(PassRegistry &);

} // end namespace llvm

INITIALIZE_PASS(InferAddressSpaces, DEBUG_TYPE, "Infer address spaces",
                false, false)

// Returns true if V is an address expression.
// TODO: Currently, we consider only phi, bitcast, addrspacecast, and
// getelementptr operators.
static bool isAddressExpression(const Value &V) {
  if (!isa<Operator>(V))
    return false;

  switch (cast<Operator>(V).getOpcode()) {
  case Instruction::PHI:
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
  case Instruction::GetElementPtr:
  case Instruction::Select:
    return true;
  default:
    return false;
  }
}

// Returns the pointer operands of V.
//
// Precondition: V is an address expression.
static SmallVector<Value *, 2> getPointerOperands(const Value &V) {
  const Operator &Op = cast<Operator>(V);
  switch (Op.getOpcode()) {
  case Instruction::PHI: {
    auto IncomingValues = cast<PHINode>(Op).incoming_values();
    return SmallVector<Value *, 2>(IncomingValues.begin(),
                                   IncomingValues.end());
  }
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
  case Instruction::GetElementPtr:
    return {Op.getOperand(0)};
  case Instruction::Select:
    return {Op.getOperand(1), Op.getOperand(2)};
  default:
    llvm_unreachable("Unexpected instruction type.");
  }
}

// TODO: Move logic to TTI?
bool InferAddressSpaces::rewriteIntrinsicOperands(IntrinsicInst *II,
                                                  Value *OldV,
                                                  Value *NewV) const {
  Module *M = II->getParent()->getParent()->getParent();

  switch (II->getIntrinsicID()) {
  case Intrinsic::amdgcn_atomic_inc:
  case Intrinsic::amdgcn_atomic_dec:
  case Intrinsic::amdgcn_ds_fadd:
  case Intrinsic::amdgcn_ds_fmin:
  case Intrinsic::amdgcn_ds_fmax: {
    const ConstantInt *IsVolatile = dyn_cast<ConstantInt>(II->getArgOperand(4));
    if (!IsVolatile || !IsVolatile->isZero())
      return false;

    LLVM_FALLTHROUGH;
  }
  case Intrinsic::objectsize: {
    Type *DestTy = II->getType();
    Type *SrcTy = NewV->getType();
    Function *NewDecl =
        Intrinsic::getDeclaration(M, II->getIntrinsicID(), {DestTy, SrcTy});
    II->setArgOperand(0, NewV);
    II->setCalledFunction(NewDecl);
    return true;
  }
  default:
    return false;
  }
}

// TODO: Move logic to TTI?
void InferAddressSpaces::collectRewritableIntrinsicOperands(
    IntrinsicInst *II, std::vector<std::pair<Value *, bool>> &PostorderStack,
    DenseSet<Value *> &Visited) const {
  switch (II->getIntrinsicID()) {
  case Intrinsic::objectsize:
  case Intrinsic::amdgcn_atomic_inc:
  case Intrinsic::amdgcn_atomic_dec:
  case Intrinsic::amdgcn_ds_fadd:
  case Intrinsic::amdgcn_ds_fmin:
  case Intrinsic::amdgcn_ds_fmax:
    appendsFlatAddressExpressionToPostorderStack(II->getArgOperand(0),
                                                 PostorderStack, Visited);
    break;
  default:
    break;
  }
}

// Returns all flat address expressions in function F. The elements are
// If V is an unvisited flat address expression, appends V to PostorderStack
// and marks it as visited.
void InferAddressSpaces::appendsFlatAddressExpressionToPostorderStack(
    Value *V, std::vector<std::pair<Value *, bool>> &PostorderStack,
    DenseSet<Value *> &Visited) const {
  assert(V->getType()->isPointerTy());

  // Generic addressing expressions may be hidden in nested constant
  // expressions.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
    // TODO: Look in non-address parts, like icmp operands.
    if (isAddressExpression(*CE) && Visited.insert(CE).second)
      PostorderStack.push_back(std::make_pair(CE, false));

    return;
  }

  if (isAddressExpression(*V) &&
      V->getType()->getPointerAddressSpace() == FlatAddrSpace) {
    if (Visited.insert(V).second) {
      PostorderStack.push_back(std::make_pair(V, false));

      Operator *Op = cast<Operator>(V);
      for (unsigned I = 0, E = Op->getNumOperands(); I != E; ++I) {
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Op->getOperand(I))) {
          if (isAddressExpression(*CE) && Visited.insert(CE).second)
            PostorderStack.emplace_back(CE, false);
        }
      }
    }
  }
}

// Returns all flat address expressions in function F. The elements are ordered
// ordered in postorder.
std::vector<WeakTrackingVH>
InferAddressSpaces::collectFlatAddressExpressions(Function &F) const {
  // This function implements a non-recursive postorder traversal of a partial
  // use-def graph of function F.
  std::vector<std::pair<Value *, bool>> PostorderStack;
  // The set of visited expressions.
  DenseSet<Value *> Visited;

  auto PushPtrOperand = [&](Value *Ptr) {
    appendsFlatAddressExpressionToPostorderStack(Ptr, PostorderStack,
                                                 Visited);
  };

  // Look at operations that may be interesting accelerate by moving to a known
  // address space. We aim at generating after loads and stores, but pure
  // addressing calculations may also be faster.
  for (Instruction &I : instructions(F)) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
      if (!GEP->getType()->isVectorTy())
        PushPtrOperand(GEP->getPointerOperand());
    } else if (auto *LI = dyn_cast<LoadInst>(&I))
      PushPtrOperand(LI->getPointerOperand());
    else if (auto *SI = dyn_cast<StoreInst>(&I))
      PushPtrOperand(SI->getPointerOperand());
    else if (auto *RMW = dyn_cast<AtomicRMWInst>(&I))
      PushPtrOperand(RMW->getPointerOperand());
    else if (auto *CmpX = dyn_cast<AtomicCmpXchgInst>(&I))
      PushPtrOperand(CmpX->getPointerOperand());
    else if (auto *MI = dyn_cast<MemIntrinsic>(&I)) {
      // For memset/memcpy/memmove, any pointer operand can be replaced.
      PushPtrOperand(MI->getRawDest());

      // Handle 2nd operand for memcpy/memmove.
      if (auto *MTI = dyn_cast<MemTransferInst>(MI))
        PushPtrOperand(MTI->getRawSource());
    } else if (auto *II = dyn_cast<IntrinsicInst>(&I))
      collectRewritableIntrinsicOperands(II, PostorderStack, Visited);
    else if (ICmpInst *Cmp = dyn_cast<ICmpInst>(&I)) {
      // FIXME: Handle vectors of pointers
      if (Cmp->getOperand(0)->getType()->isPointerTy()) {
        PushPtrOperand(Cmp->getOperand(0));
        PushPtrOperand(Cmp->getOperand(1));
      }
    } else if (auto *ASC = dyn_cast<AddrSpaceCastInst>(&I)) {
      if (!ASC->getType()->isVectorTy())
        PushPtrOperand(ASC->getPointerOperand());
    }
  }

  std::vector<WeakTrackingVH> Postorder; // The resultant postorder.
  while (!PostorderStack.empty()) {
    Value *TopVal = PostorderStack.back().first;
    // If the operands of the expression on the top are already explored,
    // adds that expression to the resultant postorder.
    if (PostorderStack.back().second) {
      if (TopVal->getType()->getPointerAddressSpace() == FlatAddrSpace)
        Postorder.push_back(TopVal);
      PostorderStack.pop_back();
      continue;
    }
    // Otherwise, adds its operands to the stack and explores them.
    PostorderStack.back().second = true;
    for (Value *PtrOperand : getPointerOperands(*TopVal)) {
      appendsFlatAddressExpressionToPostorderStack(PtrOperand, PostorderStack,
                                                   Visited);
    }
  }
  return Postorder;
}

// A helper function for cloneInstructionWithNewAddressSpace. Returns the clone
// of OperandUse.get() in the new address space. If the clone is not ready yet,
// returns an undef in the new address space as a placeholder.
static Value *operandWithNewAddressSpaceOrCreateUndef(
    const Use &OperandUse, unsigned NewAddrSpace,
    const ValueToValueMapTy &ValueWithNewAddrSpace,
    SmallVectorImpl<const Use *> *UndefUsesToFix) {
  Value *Operand = OperandUse.get();

  Type *NewPtrTy =
      Operand->getType()->getPointerElementType()->getPointerTo(NewAddrSpace);

  if (Constant *C = dyn_cast<Constant>(Operand))
    return ConstantExpr::getAddrSpaceCast(C, NewPtrTy);

  if (Value *NewOperand = ValueWithNewAddrSpace.lookup(Operand))
    return NewOperand;

  UndefUsesToFix->push_back(&OperandUse);
  return UndefValue::get(NewPtrTy);
}

// Returns a clone of `I` with its operands converted to those specified in
// ValueWithNewAddrSpace. Due to potential cycles in the data flow graph, an
// operand whose address space needs to be modified might not exist in
// ValueWithNewAddrSpace. In that case, uses undef as a placeholder operand and
// adds that operand use to UndefUsesToFix so that caller can fix them later.
//
// Note that we do not necessarily clone `I`, e.g., if it is an addrspacecast
// from a pointer whose type already matches. Therefore, this function returns a
// Value* instead of an Instruction*.
static Value *cloneInstructionWithNewAddressSpace(
    Instruction *I, unsigned NewAddrSpace,
    const ValueToValueMapTy &ValueWithNewAddrSpace,
    SmallVectorImpl<const Use *> *UndefUsesToFix) {
  Type *NewPtrType =
      I->getType()->getPointerElementType()->getPointerTo(NewAddrSpace);

  if (I->getOpcode() == Instruction::AddrSpaceCast) {
    Value *Src = I->getOperand(0);
    // Because `I` is flat, the source address space must be specific.
    // Therefore, the inferred address space must be the source space, according
    // to our algorithm.
    assert(Src->getType()->getPointerAddressSpace() == NewAddrSpace);
    if (Src->getType() != NewPtrType)
      return new BitCastInst(Src, NewPtrType);
    return Src;
  }

  // Computes the converted pointer operands.
  SmallVector<Value *, 4> NewPointerOperands;
  for (const Use &OperandUse : I->operands()) {
    if (!OperandUse.get()->getType()->isPointerTy())
      NewPointerOperands.push_back(nullptr);
    else
      NewPointerOperands.push_back(operandWithNewAddressSpaceOrCreateUndef(
                                     OperandUse, NewAddrSpace, ValueWithNewAddrSpace, UndefUsesToFix));
  }

  switch (I->getOpcode()) {
  case Instruction::BitCast:
    return new BitCastInst(NewPointerOperands[0], NewPtrType);
  case Instruction::PHI: {
    assert(I->getType()->isPointerTy());
    PHINode *PHI = cast<PHINode>(I);
    PHINode *NewPHI = PHINode::Create(NewPtrType, PHI->getNumIncomingValues());
    for (unsigned Index = 0; Index < PHI->getNumIncomingValues(); ++Index) {
      unsigned OperandNo = PHINode::getOperandNumForIncomingValue(Index);
      NewPHI->addIncoming(NewPointerOperands[OperandNo],
                          PHI->getIncomingBlock(Index));
    }
    return NewPHI;
  }
  case Instruction::GetElementPtr: {
    GetElementPtrInst *GEP = cast<GetElementPtrInst>(I);
    GetElementPtrInst *NewGEP = GetElementPtrInst::Create(
        GEP->getSourceElementType(), NewPointerOperands[0],
        SmallVector<Value *, 4>(GEP->idx_begin(), GEP->idx_end()));
    NewGEP->setIsInBounds(GEP->isInBounds());
    return NewGEP;
  }
  case Instruction::Select:
    assert(I->getType()->isPointerTy());
    return SelectInst::Create(I->getOperand(0), NewPointerOperands[1],
                              NewPointerOperands[2], "", nullptr, I);
  default:
    llvm_unreachable("Unexpected opcode");
  }
}

// Similar to cloneInstructionWithNewAddressSpace, returns a clone of the
// constant expression `CE` with its operands replaced as specified in
// ValueWithNewAddrSpace.
static Value *cloneConstantExprWithNewAddressSpace(
  ConstantExpr *CE, unsigned NewAddrSpace,
  const ValueToValueMapTy &ValueWithNewAddrSpace) {
  Type *TargetType =
    CE->getType()->getPointerElementType()->getPointerTo(NewAddrSpace);

  if (CE->getOpcode() == Instruction::AddrSpaceCast) {
    // Because CE is flat, the source address space must be specific.
    // Therefore, the inferred address space must be the source space according
    // to our algorithm.
    assert(CE->getOperand(0)->getType()->getPointerAddressSpace() ==
           NewAddrSpace);
    return ConstantExpr::getBitCast(CE->getOperand(0), TargetType);
  }

  if (CE->getOpcode() == Instruction::BitCast) {
    if (Value *NewOperand = ValueWithNewAddrSpace.lookup(CE->getOperand(0)))
      return ConstantExpr::getBitCast(cast<Constant>(NewOperand), TargetType);
    return ConstantExpr::getAddrSpaceCast(CE, TargetType);
  }

  if (CE->getOpcode() == Instruction::Select) {
    Constant *Src0 = CE->getOperand(1);
    Constant *Src1 = CE->getOperand(2);
    if (Src0->getType()->getPointerAddressSpace() ==
        Src1->getType()->getPointerAddressSpace()) {

      return ConstantExpr::getSelect(
          CE->getOperand(0), ConstantExpr::getAddrSpaceCast(Src0, TargetType),
          ConstantExpr::getAddrSpaceCast(Src1, TargetType));
    }
  }

  // Computes the operands of the new constant expression.
  bool IsNew = false;
  SmallVector<Constant *, 4> NewOperands;
  for (unsigned Index = 0; Index < CE->getNumOperands(); ++Index) {
    Constant *Operand = CE->getOperand(Index);
    // If the address space of `Operand` needs to be modified, the new operand
    // with the new address space should already be in ValueWithNewAddrSpace
    // because (1) the constant expressions we consider (i.e. addrspacecast,
    // bitcast, and getelementptr) do not incur cycles in the data flow graph
    // and (2) this function is called on constant expressions in postorder.
    if (Value *NewOperand = ValueWithNewAddrSpace.lookup(Operand)) {
      IsNew = true;
      NewOperands.push_back(cast<Constant>(NewOperand));
    } else {
      // Otherwise, reuses the old operand.
      NewOperands.push_back(Operand);
    }
  }

  // If !IsNew, we will replace the Value with itself. However, replaced values
  // are assumed to wrapped in a addrspace cast later so drop it now.
  if (!IsNew)
    return nullptr;

  if (CE->getOpcode() == Instruction::GetElementPtr) {
    // Needs to specify the source type while constructing a getelementptr
    // constant expression.
    return CE->getWithOperands(
      NewOperands, TargetType, /*OnlyIfReduced=*/false,
      NewOperands[0]->getType()->getPointerElementType());
  }

  return CE->getWithOperands(NewOperands, TargetType);
}

// Returns a clone of the value `V`, with its operands replaced as specified in
// ValueWithNewAddrSpace. This function is called on every flat address
// expression whose address space needs to be modified, in postorder.
//
// See cloneInstructionWithNewAddressSpace for the meaning of UndefUsesToFix.
Value *InferAddressSpaces::cloneValueWithNewAddressSpace(
  Value *V, unsigned NewAddrSpace,
  const ValueToValueMapTy &ValueWithNewAddrSpace,
  SmallVectorImpl<const Use *> *UndefUsesToFix) const {
  // All values in Postorder are flat address expressions.
  assert(isAddressExpression(*V) &&
         V->getType()->getPointerAddressSpace() == FlatAddrSpace);

  if (Instruction *I = dyn_cast<Instruction>(V)) {
    Value *NewV = cloneInstructionWithNewAddressSpace(
      I, NewAddrSpace, ValueWithNewAddrSpace, UndefUsesToFix);
    if (Instruction *NewI = dyn_cast<Instruction>(NewV)) {
      if (NewI->getParent() == nullptr) {
        NewI->insertBefore(I);
        NewI->takeName(I);
      }
    }
    return NewV;
  }

  return cloneConstantExprWithNewAddressSpace(
    cast<ConstantExpr>(V), NewAddrSpace, ValueWithNewAddrSpace);
}

// Defines the join operation on the address space lattice (see the file header
// comments).
unsigned InferAddressSpaces::joinAddressSpaces(unsigned AS1,
                                               unsigned AS2) const {
  if (AS1 == FlatAddrSpace || AS2 == FlatAddrSpace)
    return FlatAddrSpace;

  if (AS1 == UninitializedAddressSpace)
    return AS2;
  if (AS2 == UninitializedAddressSpace)
    return AS1;

  // The join of two different specific address spaces is flat.
  return (AS1 == AS2) ? AS1 : FlatAddrSpace;
}

bool InferAddressSpaces::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  const TargetTransformInfo &TTI =
      getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
  FlatAddrSpace = TTI.getFlatAddressSpace();
  if (FlatAddrSpace == UninitializedAddressSpace)
    return false;

  // Collects all flat address expressions in postorder.
  std::vector<WeakTrackingVH> Postorder = collectFlatAddressExpressions(F);

  // Runs a data-flow analysis to refine the address spaces of every expression
  // in Postorder.
  ValueToAddrSpaceMapTy InferredAddrSpace;
  inferAddressSpaces(Postorder, &InferredAddrSpace);

  // Changes the address spaces of the flat address expressions who are inferred
  // to point to a specific address space.
  return rewriteWithNewAddressSpaces(TTI, Postorder, InferredAddrSpace, &F);
}

// Constants need to be tracked through RAUW to handle cases with nested
// constant expressions, so wrap values in WeakTrackingVH.
void InferAddressSpaces::inferAddressSpaces(
    ArrayRef<WeakTrackingVH> Postorder,
    ValueToAddrSpaceMapTy *InferredAddrSpace) const {
  SetVector<Value *> Worklist(Postorder.begin(), Postorder.end());
  // Initially, all expressions are in the uninitialized address space.
  for (Value *V : Postorder)
    (*InferredAddrSpace)[V] = UninitializedAddressSpace;

  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();

    // Tries to update the address space of the stack top according to the
    // address spaces of its operands.
    LLVM_DEBUG(dbgs() << "Updating the address space of\n  " << *V << '\n');
    Optional<unsigned> NewAS = updateAddressSpace(*V, *InferredAddrSpace);
    if (!NewAS.hasValue())
      continue;
    // If any updates are made, grabs its users to the worklist because
    // their address spaces can also be possibly updated.
    LLVM_DEBUG(dbgs() << "  to " << NewAS.getValue() << '\n');
    (*InferredAddrSpace)[V] = NewAS.getValue();

    for (Value *User : V->users()) {
      // Skip if User is already in the worklist.
      if (Worklist.count(User))
        continue;

      auto Pos = InferredAddrSpace->find(User);
      // Our algorithm only updates the address spaces of flat address
      // expressions, which are those in InferredAddrSpace.
      if (Pos == InferredAddrSpace->end())
        continue;

      // Function updateAddressSpace moves the address space down a lattice
      // path. Therefore, nothing to do if User is already inferred as flat (the
      // bottom element in the lattice).
      if (Pos->second == FlatAddrSpace)
        continue;

      Worklist.insert(User);
    }
  }
}

Optional<unsigned> InferAddressSpaces::updateAddressSpace(
    const Value &V, const ValueToAddrSpaceMapTy &InferredAddrSpace) const {
  assert(InferredAddrSpace.count(&V));

  // The new inferred address space equals the join of the address spaces
  // of all its pointer operands.
  unsigned NewAS = UninitializedAddressSpace;

  const Operator &Op = cast<Operator>(V);
  if (Op.getOpcode() == Instruction::Select) {
    Value *Src0 = Op.getOperand(1);
    Value *Src1 = Op.getOperand(2);

    auto I = InferredAddrSpace.find(Src0);
    unsigned Src0AS = (I != InferredAddrSpace.end()) ?
      I->second : Src0->getType()->getPointerAddressSpace();

    auto J = InferredAddrSpace.find(Src1);
    unsigned Src1AS = (J != InferredAddrSpace.end()) ?
      J->second : Src1->getType()->getPointerAddressSpace();

    auto *C0 = dyn_cast<Constant>(Src0);
    auto *C1 = dyn_cast<Constant>(Src1);

    // If one of the inputs is a constant, we may be able to do a constant
    // addrspacecast of it. Defer inferring the address space until the input
    // address space is known.
    if ((C1 && Src0AS == UninitializedAddressSpace) ||
        (C0 && Src1AS == UninitializedAddressSpace))
      return None;

    if (C0 && isSafeToCastConstAddrSpace(C0, Src1AS))
      NewAS = Src1AS;
    else if (C1 && isSafeToCastConstAddrSpace(C1, Src0AS))
      NewAS = Src0AS;
    else
      NewAS = joinAddressSpaces(Src0AS, Src1AS);
  } else {
    for (Value *PtrOperand : getPointerOperands(V)) {
      auto I = InferredAddrSpace.find(PtrOperand);
      unsigned OperandAS = I != InferredAddrSpace.end() ?
        I->second : PtrOperand->getType()->getPointerAddressSpace();

      // join(flat, *) = flat. So we can break if NewAS is already flat.
      NewAS = joinAddressSpaces(NewAS, OperandAS);
      if (NewAS == FlatAddrSpace)
        break;
    }
  }

  unsigned OldAS = InferredAddrSpace.lookup(&V);
  assert(OldAS != FlatAddrSpace);
  if (OldAS == NewAS)
    return None;
  return NewAS;
}

/// \p returns true if \p U is the pointer operand of a memory instruction with
/// a single pointer operand that can have its address space changed by simply
/// mutating the use to a new value. If the memory instruction is volatile,
/// return true only if the target allows the memory instruction to be volatile
/// in the new address space.
static bool isSimplePointerUseValidToReplace(const TargetTransformInfo &TTI,
                                             Use &U, unsigned AddrSpace) {
  User *Inst = U.getUser();
  unsigned OpNo = U.getOperandNo();
  bool VolatileIsAllowed = false;
  if (auto *I = dyn_cast<Instruction>(Inst))
    VolatileIsAllowed = TTI.hasVolatileVariant(I, AddrSpace);

  if (auto *LI = dyn_cast<LoadInst>(Inst))
    return OpNo == LoadInst::getPointerOperandIndex() &&
           (VolatileIsAllowed || !LI->isVolatile());

  if (auto *SI = dyn_cast<StoreInst>(Inst))
    return OpNo == StoreInst::getPointerOperandIndex() &&
           (VolatileIsAllowed || !SI->isVolatile());

  if (auto *RMW = dyn_cast<AtomicRMWInst>(Inst))
    return OpNo == AtomicRMWInst::getPointerOperandIndex() &&
           (VolatileIsAllowed || !RMW->isVolatile());

  if (auto *CmpX = dyn_cast<AtomicCmpXchgInst>(Inst))
    return OpNo == AtomicCmpXchgInst::getPointerOperandIndex() &&
           (VolatileIsAllowed || !CmpX->isVolatile());

  return false;
}

/// Update memory intrinsic uses that require more complex processing than
/// simple memory instructions. Thse require re-mangling and may have multiple
/// pointer operands.
static bool handleMemIntrinsicPtrUse(MemIntrinsic *MI, Value *OldV,
                                     Value *NewV) {
  IRBuilder<> B(MI);
  MDNode *TBAA = MI->getMetadata(LLVMContext::MD_tbaa);
  MDNode *ScopeMD = MI->getMetadata(LLVMContext::MD_alias_scope);
  MDNode *NoAliasMD = MI->getMetadata(LLVMContext::MD_noalias);

  if (auto *MSI = dyn_cast<MemSetInst>(MI)) {
    B.CreateMemSet(NewV, MSI->getValue(),
                   MSI->getLength(), MSI->getDestAlignment(),
                   false, // isVolatile
                   TBAA, ScopeMD, NoAliasMD);
  } else if (auto *MTI = dyn_cast<MemTransferInst>(MI)) {
    Value *Src = MTI->getRawSource();
    Value *Dest = MTI->getRawDest();

    // Be careful in case this is a self-to-self copy.
    if (Src == OldV)
      Src = NewV;

    if (Dest == OldV)
      Dest = NewV;

    if (isa<MemCpyInst>(MTI)) {
      MDNode *TBAAStruct = MTI->getMetadata(LLVMContext::MD_tbaa_struct);
      B.CreateMemCpy(Dest, MTI->getDestAlignment(),
                     Src, MTI->getSourceAlignment(),
                     MTI->getLength(),
                     false, // isVolatile
                     TBAA, TBAAStruct, ScopeMD, NoAliasMD);
    } else {
      assert(isa<MemMoveInst>(MTI));
      B.CreateMemMove(Dest, MTI->getDestAlignment(),
                      Src, MTI->getSourceAlignment(),
                      MTI->getLength(),
                      false, // isVolatile
                      TBAA, ScopeMD, NoAliasMD);
    }
  } else
    llvm_unreachable("unhandled MemIntrinsic");

  MI->eraseFromParent();
  return true;
}

// \p returns true if it is OK to change the address space of constant \p C with
// a ConstantExpr addrspacecast.
bool InferAddressSpaces::isSafeToCastConstAddrSpace(Constant *C, unsigned NewAS) const {
  assert(NewAS != UninitializedAddressSpace);

  unsigned SrcAS = C->getType()->getPointerAddressSpace();
  if (SrcAS == NewAS || isa<UndefValue>(C))
    return true;

  // Prevent illegal casts between different non-flat address spaces.
  if (SrcAS != FlatAddrSpace && NewAS != FlatAddrSpace)
    return false;

  if (isa<ConstantPointerNull>(C))
    return true;

  if (auto *Op = dyn_cast<Operator>(C)) {
    // If we already have a constant addrspacecast, it should be safe to cast it
    // off.
    if (Op->getOpcode() == Instruction::AddrSpaceCast)
      return isSafeToCastConstAddrSpace(cast<Constant>(Op->getOperand(0)), NewAS);

    if (Op->getOpcode() == Instruction::IntToPtr &&
        Op->getType()->getPointerAddressSpace() == FlatAddrSpace)
      return true;
  }

  return false;
}

static Value::use_iterator skipToNextUser(Value::use_iterator I,
                                          Value::use_iterator End) {
  User *CurUser = I->getUser();
  ++I;

  while (I != End && I->getUser() == CurUser)
    ++I;

  return I;
}

bool InferAddressSpaces::rewriteWithNewAddressSpaces(
    const TargetTransformInfo &TTI, ArrayRef<WeakTrackingVH> Postorder,
    const ValueToAddrSpaceMapTy &InferredAddrSpace, Function *F) const {
  // For each address expression to be modified, creates a clone of it with its
  // pointer operands converted to the new address space. Since the pointer
  // operands are converted, the clone is naturally in the new address space by
  // construction.
  ValueToValueMapTy ValueWithNewAddrSpace;
  SmallVector<const Use *, 32> UndefUsesToFix;
  for (Value* V : Postorder) {
    unsigned NewAddrSpace = InferredAddrSpace.lookup(V);
    if (V->getType()->getPointerAddressSpace() != NewAddrSpace) {
      ValueWithNewAddrSpace[V] = cloneValueWithNewAddressSpace(
        V, NewAddrSpace, ValueWithNewAddrSpace, &UndefUsesToFix);
    }
  }

  if (ValueWithNewAddrSpace.empty())
    return false;

  // Fixes all the undef uses generated by cloneInstructionWithNewAddressSpace.
  for (const Use *UndefUse : UndefUsesToFix) {
    User *V = UndefUse->getUser();
    User *NewV = cast<User>(ValueWithNewAddrSpace.lookup(V));
    unsigned OperandNo = UndefUse->getOperandNo();
    assert(isa<UndefValue>(NewV->getOperand(OperandNo)));
    NewV->setOperand(OperandNo, ValueWithNewAddrSpace.lookup(UndefUse->get()));
  }

  SmallVector<Instruction *, 16> DeadInstructions;

  // Replaces the uses of the old address expressions with the new ones.
  for (const WeakTrackingVH &WVH : Postorder) {
    assert(WVH && "value was unexpectedly deleted");
    Value *V = WVH;
    Value *NewV = ValueWithNewAddrSpace.lookup(V);
    if (NewV == nullptr)
      continue;

    LLVM_DEBUG(dbgs() << "Replacing the uses of " << *V << "\n  with\n  "
                      << *NewV << '\n');

    if (Constant *C = dyn_cast<Constant>(V)) {
      Constant *Replace = ConstantExpr::getAddrSpaceCast(cast<Constant>(NewV),
                                                         C->getType());
      if (C != Replace) {
        LLVM_DEBUG(dbgs() << "Inserting replacement const cast: " << Replace
                          << ": " << *Replace << '\n');
        C->replaceAllUsesWith(Replace);
        V = Replace;
      }
    }

    Value::use_iterator I, E, Next;
    for (I = V->use_begin(), E = V->use_end(); I != E; ) {
      Use &U = *I;

      // Some users may see the same pointer operand in multiple operands. Skip
      // to the next instruction.
      I = skipToNextUser(I, E);

      if (isSimplePointerUseValidToReplace(
              TTI, U, V->getType()->getPointerAddressSpace())) {
        // If V is used as the pointer operand of a compatible memory operation,
        // sets the pointer operand to NewV. This replacement does not change
        // the element type, so the resultant load/store is still valid.
        U.set(NewV);
        continue;
      }

      User *CurUser = U.getUser();
      // Handle more complex cases like intrinsic that need to be remangled.
      if (auto *MI = dyn_cast<MemIntrinsic>(CurUser)) {
        if (!MI->isVolatile() && handleMemIntrinsicPtrUse(MI, V, NewV))
          continue;
      }

      if (auto *II = dyn_cast<IntrinsicInst>(CurUser)) {
        if (rewriteIntrinsicOperands(II, V, NewV))
          continue;
      }

      if (isa<Instruction>(CurUser)) {
        if (ICmpInst *Cmp = dyn_cast<ICmpInst>(CurUser)) {
          // If we can infer that both pointers are in the same addrspace,
          // transform e.g.
          //   %cmp = icmp eq float* %p, %q
          // into
          //   %cmp = icmp eq float addrspace(3)* %new_p, %new_q

          unsigned NewAS = NewV->getType()->getPointerAddressSpace();
          int SrcIdx = U.getOperandNo();
          int OtherIdx = (SrcIdx == 0) ? 1 : 0;
          Value *OtherSrc = Cmp->getOperand(OtherIdx);

          if (Value *OtherNewV = ValueWithNewAddrSpace.lookup(OtherSrc)) {
            if (OtherNewV->getType()->getPointerAddressSpace() == NewAS) {
              Cmp->setOperand(OtherIdx, OtherNewV);
              Cmp->setOperand(SrcIdx, NewV);
              continue;
            }
          }

          // Even if the type mismatches, we can cast the constant.
          if (auto *KOtherSrc = dyn_cast<Constant>(OtherSrc)) {
            if (isSafeToCastConstAddrSpace(KOtherSrc, NewAS)) {
              Cmp->setOperand(SrcIdx, NewV);
              Cmp->setOperand(OtherIdx,
                ConstantExpr::getAddrSpaceCast(KOtherSrc, NewV->getType()));
              continue;
            }
          }
        }

        if (AddrSpaceCastInst *ASC = dyn_cast<AddrSpaceCastInst>(CurUser)) {
          unsigned NewAS = NewV->getType()->getPointerAddressSpace();
          if (ASC->getDestAddressSpace() == NewAS) {
            if (ASC->getType()->getPointerElementType() !=
                NewV->getType()->getPointerElementType()) {
              NewV = CastInst::Create(Instruction::BitCast, NewV,
                                      ASC->getType(), "", ASC);
            }
            ASC->replaceAllUsesWith(NewV);
            DeadInstructions.push_back(ASC);
            continue;
          }
        }

        // Otherwise, replaces the use with flat(NewV).
        if (Instruction *I = dyn_cast<Instruction>(V)) {
          BasicBlock::iterator InsertPos = std::next(I->getIterator());
          while (isa<PHINode>(InsertPos))
            ++InsertPos;
          U.set(new AddrSpaceCastInst(NewV, V->getType(), "", &*InsertPos));
        } else {
          U.set(ConstantExpr::getAddrSpaceCast(cast<Constant>(NewV),
                                               V->getType()));
        }
      }
    }

    if (V->use_empty()) {
      if (Instruction *I = dyn_cast<Instruction>(V))
        DeadInstructions.push_back(I);
    }
  }

  for (Instruction *I : DeadInstructions)
    RecursivelyDeleteTriviallyDeadInstructions(I);

  return true;
}

FunctionPass *llvm::createInferAddressSpacesPass() {
  return new InferAddressSpaces();
}
