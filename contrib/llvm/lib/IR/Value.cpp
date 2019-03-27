//===-- Value.cpp - Implement the Value class -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Value, ValueHandle, and User classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Value.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DerivedUser.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;

static cl::opt<unsigned> NonGlobalValueMaxNameSize(
    "non-global-value-max-name-size", cl::Hidden, cl::init(1024),
    cl::desc("Maximum size for the name of non-global values."));

//===----------------------------------------------------------------------===//
//                                Value Class
//===----------------------------------------------------------------------===//
static inline Type *checkType(Type *Ty) {
  assert(Ty && "Value defined with a null type: Error!");
  return Ty;
}

Value::Value(Type *ty, unsigned scid)
    : VTy(checkType(ty)), UseList(nullptr), SubclassID(scid),
      HasValueHandle(0), SubclassOptionalData(0), SubclassData(0),
      NumUserOperands(0), IsUsedByMD(false), HasName(false) {
  static_assert(ConstantFirstVal == 0, "!(SubclassID < ConstantFirstVal)");
  // FIXME: Why isn't this in the subclass gunk??
  // Note, we cannot call isa<CallInst> before the CallInst has been
  // constructed.
  if (SubclassID == Instruction::Call || SubclassID == Instruction::Invoke)
    assert((VTy->isFirstClassType() || VTy->isVoidTy() || VTy->isStructTy()) &&
           "invalid CallInst type!");
  else if (SubclassID != BasicBlockVal &&
           (/*SubclassID < ConstantFirstVal ||*/ SubclassID > ConstantLastVal))
    assert((VTy->isFirstClassType() || VTy->isVoidTy()) &&
           "Cannot create non-first-class values except for constants!");
  static_assert(sizeof(Value) == 2 * sizeof(void *) + 2 * sizeof(unsigned),
                "Value too big");
}

Value::~Value() {
  // Notify all ValueHandles (if present) that this value is going away.
  if (HasValueHandle)
    ValueHandleBase::ValueIsDeleted(this);
  if (isUsedByMetadata())
    ValueAsMetadata::handleDeletion(this);

#ifndef NDEBUG      // Only in -g mode...
  // Check to make sure that there are no uses of this value that are still
  // around when the value is destroyed.  If there are, then we have a dangling
  // reference and something is wrong.  This code is here to print out where
  // the value is still being referenced.
  //
  if (!use_empty()) {
    dbgs() << "While deleting: " << *VTy << " %" << getName() << "\n";
    for (auto *U : users())
      dbgs() << "Use still stuck around after Def is destroyed:" << *U << "\n";
  }
#endif
  assert(use_empty() && "Uses remain when a value is destroyed!");

  // If this value is named, destroy the name.  This should not be in a symtab
  // at this point.
  destroyValueName();
}

void Value::deleteValue() {
  switch (getValueID()) {
#define HANDLE_VALUE(Name)                                                     \
  case Value::Name##Val:                                                       \
    delete static_cast<Name *>(this);                                          \
    break;
#define HANDLE_MEMORY_VALUE(Name)                                              \
  case Value::Name##Val:                                                       \
    static_cast<DerivedUser *>(this)->DeleteValue(                             \
        static_cast<DerivedUser *>(this));                                     \
    break;
#define HANDLE_INSTRUCTION(Name)  /* nothing */
#include "llvm/IR/Value.def"

#define HANDLE_INST(N, OPC, CLASS)                                             \
  case Value::InstructionVal + Instruction::OPC:                               \
    delete static_cast<CLASS *>(this);                                         \
    break;
#define HANDLE_USER_INST(N, OPC, CLASS)
#include "llvm/IR/Instruction.def"

  default:
    llvm_unreachable("attempting to delete unknown value kind");
  }
}

void Value::destroyValueName() {
  ValueName *Name = getValueName();
  if (Name)
    Name->Destroy();
  setValueName(nullptr);
}

bool Value::hasNUses(unsigned N) const {
  return hasNItems(use_begin(), use_end(), N);
}

bool Value::hasNUsesOrMore(unsigned N) const {
  return hasNItemsOrMore(use_begin(), use_end(), N);
}

bool Value::isUsedInBasicBlock(const BasicBlock *BB) const {
  // This can be computed either by scanning the instructions in BB, or by
  // scanning the use list of this Value. Both lists can be very long, but
  // usually one is quite short.
  //
  // Scan both lists simultaneously until one is exhausted. This limits the
  // search to the shorter list.
  BasicBlock::const_iterator BI = BB->begin(), BE = BB->end();
  const_user_iterator UI = user_begin(), UE = user_end();
  for (; BI != BE && UI != UE; ++BI, ++UI) {
    // Scan basic block: Check if this Value is used by the instruction at BI.
    if (is_contained(BI->operands(), this))
      return true;
    // Scan use list: Check if the use at UI is in BB.
    const auto *User = dyn_cast<Instruction>(*UI);
    if (User && User->getParent() == BB)
      return true;
  }
  return false;
}

unsigned Value::getNumUses() const {
  return (unsigned)std::distance(use_begin(), use_end());
}

static bool getSymTab(Value *V, ValueSymbolTable *&ST) {
  ST = nullptr;
  if (Instruction *I = dyn_cast<Instruction>(V)) {
    if (BasicBlock *P = I->getParent())
      if (Function *PP = P->getParent())
        ST = PP->getValueSymbolTable();
  } else if (BasicBlock *BB = dyn_cast<BasicBlock>(V)) {
    if (Function *P = BB->getParent())
      ST = P->getValueSymbolTable();
  } else if (GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
    if (Module *P = GV->getParent())
      ST = &P->getValueSymbolTable();
  } else if (Argument *A = dyn_cast<Argument>(V)) {
    if (Function *P = A->getParent())
      ST = P->getValueSymbolTable();
  } else {
    assert(isa<Constant>(V) && "Unknown value type!");
    return true;  // no name is setable for this.
  }
  return false;
}

ValueName *Value::getValueName() const {
  if (!HasName) return nullptr;

  LLVMContext &Ctx = getContext();
  auto I = Ctx.pImpl->ValueNames.find(this);
  assert(I != Ctx.pImpl->ValueNames.end() &&
         "No name entry found!");

  return I->second;
}

void Value::setValueName(ValueName *VN) {
  LLVMContext &Ctx = getContext();

  assert(HasName == Ctx.pImpl->ValueNames.count(this) &&
         "HasName bit out of sync!");

  if (!VN) {
    if (HasName)
      Ctx.pImpl->ValueNames.erase(this);
    HasName = false;
    return;
  }

  HasName = true;
  Ctx.pImpl->ValueNames[this] = VN;
}

StringRef Value::getName() const {
  // Make sure the empty string is still a C string. For historical reasons,
  // some clients want to call .data() on the result and expect it to be null
  // terminated.
  if (!hasName())
    return StringRef("", 0);
  return getValueName()->getKey();
}

void Value::setNameImpl(const Twine &NewName) {
  // Fast-path: LLVMContext can be set to strip out non-GlobalValue names
  if (getContext().shouldDiscardValueNames() && !isa<GlobalValue>(this))
    return;

  // Fast path for common IRBuilder case of setName("") when there is no name.
  if (NewName.isTriviallyEmpty() && !hasName())
    return;

  SmallString<256> NameData;
  StringRef NameRef = NewName.toStringRef(NameData);
  assert(NameRef.find_first_of(0) == StringRef::npos &&
         "Null bytes are not allowed in names");

  // Name isn't changing?
  if (getName() == NameRef)
    return;

  // Cap the size of non-GlobalValue names.
  if (NameRef.size() > NonGlobalValueMaxNameSize && !isa<GlobalValue>(this))
    NameRef =
        NameRef.substr(0, std::max(1u, (unsigned)NonGlobalValueMaxNameSize));

  assert(!getType()->isVoidTy() && "Cannot assign a name to void values!");

  // Get the symbol table to update for this object.
  ValueSymbolTable *ST;
  if (getSymTab(this, ST))
    return;  // Cannot set a name on this value (e.g. constant).

  if (!ST) { // No symbol table to update?  Just do the change.
    if (NameRef.empty()) {
      // Free the name for this value.
      destroyValueName();
      return;
    }

    // NOTE: Could optimize for the case the name is shrinking to not deallocate
    // then reallocated.
    destroyValueName();

    // Create the new name.
    setValueName(ValueName::Create(NameRef));
    getValueName()->setValue(this);
    return;
  }

  // NOTE: Could optimize for the case the name is shrinking to not deallocate
  // then reallocated.
  if (hasName()) {
    // Remove old name.
    ST->removeValueName(getValueName());
    destroyValueName();

    if (NameRef.empty())
      return;
  }

  // Name is changing to something new.
  setValueName(ST->createValueName(NameRef, this));
}

void Value::setName(const Twine &NewName) {
  setNameImpl(NewName);
  if (Function *F = dyn_cast<Function>(this))
    F->recalculateIntrinsicID();
}

void Value::takeName(Value *V) {
  ValueSymbolTable *ST = nullptr;
  // If this value has a name, drop it.
  if (hasName()) {
    // Get the symtab this is in.
    if (getSymTab(this, ST)) {
      // We can't set a name on this value, but we need to clear V's name if
      // it has one.
      if (V->hasName()) V->setName("");
      return;  // Cannot set a name on this value (e.g. constant).
    }

    // Remove old name.
    if (ST)
      ST->removeValueName(getValueName());
    destroyValueName();
  }

  // Now we know that this has no name.

  // If V has no name either, we're done.
  if (!V->hasName()) return;

  // Get this's symtab if we didn't before.
  if (!ST) {
    if (getSymTab(this, ST)) {
      // Clear V's name.
      V->setName("");
      return;  // Cannot set a name on this value (e.g. constant).
    }
  }

  // Get V's ST, this should always succed, because V has a name.
  ValueSymbolTable *VST;
  bool Failure = getSymTab(V, VST);
  assert(!Failure && "V has a name, so it should have a ST!"); (void)Failure;

  // If these values are both in the same symtab, we can do this very fast.
  // This works even if both values have no symtab yet.
  if (ST == VST) {
    // Take the name!
    setValueName(V->getValueName());
    V->setValueName(nullptr);
    getValueName()->setValue(this);
    return;
  }

  // Otherwise, things are slightly more complex.  Remove V's name from VST and
  // then reinsert it into ST.

  if (VST)
    VST->removeValueName(V->getValueName());
  setValueName(V->getValueName());
  V->setValueName(nullptr);
  getValueName()->setValue(this);

  if (ST)
    ST->reinsertValue(this);
}

void Value::assertModuleIsMaterializedImpl() const {
#ifndef NDEBUG
  const GlobalValue *GV = dyn_cast<GlobalValue>(this);
  if (!GV)
    return;
  const Module *M = GV->getParent();
  if (!M)
    return;
  assert(M->isMaterialized());
#endif
}

#ifndef NDEBUG
static bool contains(SmallPtrSetImpl<ConstantExpr *> &Cache, ConstantExpr *Expr,
                     Constant *C) {
  if (!Cache.insert(Expr).second)
    return false;

  for (auto &O : Expr->operands()) {
    if (O == C)
      return true;
    auto *CE = dyn_cast<ConstantExpr>(O);
    if (!CE)
      continue;
    if (contains(Cache, CE, C))
      return true;
  }
  return false;
}

static bool contains(Value *Expr, Value *V) {
  if (Expr == V)
    return true;

  auto *C = dyn_cast<Constant>(V);
  if (!C)
    return false;

  auto *CE = dyn_cast<ConstantExpr>(Expr);
  if (!CE)
    return false;

  SmallPtrSet<ConstantExpr *, 4> Cache;
  return contains(Cache, CE, C);
}
#endif // NDEBUG

void Value::doRAUW(Value *New, ReplaceMetadataUses ReplaceMetaUses) {
  assert(New && "Value::replaceAllUsesWith(<null>) is invalid!");
  assert(!contains(New, this) &&
         "this->replaceAllUsesWith(expr(this)) is NOT valid!");
  assert(New->getType() == getType() &&
         "replaceAllUses of value with new value of different type!");

  // Notify all ValueHandles (if present) that this value is going away.
  if (HasValueHandle)
    ValueHandleBase::ValueIsRAUWd(this, New);
  if (ReplaceMetaUses == ReplaceMetadataUses::Yes && isUsedByMetadata())
    ValueAsMetadata::handleRAUW(this, New);

  while (!materialized_use_empty()) {
    Use &U = *UseList;
    // Must handle Constants specially, we cannot call replaceUsesOfWith on a
    // constant because they are uniqued.
    if (auto *C = dyn_cast<Constant>(U.getUser())) {
      if (!isa<GlobalValue>(C)) {
        C->handleOperandChange(this, New);
        continue;
      }
    }

    U.set(New);
  }

  if (BasicBlock *BB = dyn_cast<BasicBlock>(this))
    BB->replaceSuccessorsPhiUsesWith(cast<BasicBlock>(New));
}

void Value::replaceAllUsesWith(Value *New) {
  doRAUW(New, ReplaceMetadataUses::Yes);
}

void Value::replaceNonMetadataUsesWith(Value *New) {
  doRAUW(New, ReplaceMetadataUses::No);
}

// Like replaceAllUsesWith except it does not handle constants or basic blocks.
// This routine leaves uses within BB.
void Value::replaceUsesOutsideBlock(Value *New, BasicBlock *BB) {
  assert(New && "Value::replaceUsesOutsideBlock(<null>, BB) is invalid!");
  assert(!contains(New, this) &&
         "this->replaceUsesOutsideBlock(expr(this), BB) is NOT valid!");
  assert(New->getType() == getType() &&
         "replaceUses of value with new value of different type!");
  assert(BB && "Basic block that may contain a use of 'New' must be defined\n");

  use_iterator UI = use_begin(), E = use_end();
  for (; UI != E;) {
    Use &U = *UI;
    ++UI;
    auto *Usr = dyn_cast<Instruction>(U.getUser());
    if (Usr && Usr->getParent() == BB)
      continue;
    U.set(New);
  }
}

namespace {
// Various metrics for how much to strip off of pointers.
enum PointerStripKind {
  PSK_ZeroIndices,
  PSK_ZeroIndicesAndAliases,
  PSK_ZeroIndicesAndAliasesAndInvariantGroups,
  PSK_InBoundsConstantIndices,
  PSK_InBounds
};

template <PointerStripKind StripKind>
static const Value *stripPointerCastsAndOffsets(const Value *V) {
  if (!V->getType()->isPointerTy())
    return V;

  // Even though we don't look through PHI nodes, we could be called on an
  // instruction in an unreachable block, which may be on a cycle.
  SmallPtrSet<const Value *, 4> Visited;

  Visited.insert(V);
  do {
    if (auto *GEP = dyn_cast<GEPOperator>(V)) {
      switch (StripKind) {
      case PSK_ZeroIndicesAndAliases:
      case PSK_ZeroIndicesAndAliasesAndInvariantGroups:
      case PSK_ZeroIndices:
        if (!GEP->hasAllZeroIndices())
          return V;
        break;
      case PSK_InBoundsConstantIndices:
        if (!GEP->hasAllConstantIndices())
          return V;
        LLVM_FALLTHROUGH;
      case PSK_InBounds:
        if (!GEP->isInBounds())
          return V;
        break;
      }
      V = GEP->getPointerOperand();
    } else if (Operator::getOpcode(V) == Instruction::BitCast ||
               Operator::getOpcode(V) == Instruction::AddrSpaceCast) {
      V = cast<Operator>(V)->getOperand(0);
    } else if (auto *GA = dyn_cast<GlobalAlias>(V)) {
      if (StripKind == PSK_ZeroIndices || GA->isInterposable())
        return V;
      V = GA->getAliasee();
    } else {
      if (const auto *Call = dyn_cast<CallBase>(V)) {
        if (const Value *RV = Call->getReturnedArgOperand()) {
          V = RV;
          continue;
        }
        // The result of launder.invariant.group must alias it's argument,
        // but it can't be marked with returned attribute, that's why it needs
        // special case.
        if (StripKind == PSK_ZeroIndicesAndAliasesAndInvariantGroups &&
            (Call->getIntrinsicID() == Intrinsic::launder_invariant_group ||
             Call->getIntrinsicID() == Intrinsic::strip_invariant_group)) {
          V = Call->getArgOperand(0);
          continue;
        }
      }
      return V;
    }
    assert(V->getType()->isPointerTy() && "Unexpected operand type!");
  } while (Visited.insert(V).second);

  return V;
}
} // end anonymous namespace

const Value *Value::stripPointerCasts() const {
  return stripPointerCastsAndOffsets<PSK_ZeroIndicesAndAliases>(this);
}

const Value *Value::stripPointerCastsNoFollowAliases() const {
  return stripPointerCastsAndOffsets<PSK_ZeroIndices>(this);
}

const Value *Value::stripInBoundsConstantOffsets() const {
  return stripPointerCastsAndOffsets<PSK_InBoundsConstantIndices>(this);
}

const Value *Value::stripPointerCastsAndInvariantGroups() const {
  return stripPointerCastsAndOffsets<PSK_ZeroIndicesAndAliasesAndInvariantGroups>(
      this);
}

const Value *
Value::stripAndAccumulateInBoundsConstantOffsets(const DataLayout &DL,
                                                 APInt &Offset) const {
  if (!getType()->isPointerTy())
    return this;

  assert(Offset.getBitWidth() == DL.getIndexSizeInBits(cast<PointerType>(
                                     getType())->getAddressSpace()) &&
         "The offset bit width does not match the DL specification.");

  // Even though we don't look through PHI nodes, we could be called on an
  // instruction in an unreachable block, which may be on a cycle.
  SmallPtrSet<const Value *, 4> Visited;
  Visited.insert(this);
  const Value *V = this;
  do {
    if (auto *GEP = dyn_cast<GEPOperator>(V)) {
      if (!GEP->isInBounds())
        return V;
      APInt GEPOffset(Offset);
      if (!GEP->accumulateConstantOffset(DL, GEPOffset))
        return V;
      Offset = GEPOffset;
      V = GEP->getPointerOperand();
    } else if (Operator::getOpcode(V) == Instruction::BitCast) {
      V = cast<Operator>(V)->getOperand(0);
    } else if (auto *GA = dyn_cast<GlobalAlias>(V)) {
      V = GA->getAliasee();
    } else {
      if (const auto *Call = dyn_cast<CallBase>(V))
        if (const Value *RV = Call->getReturnedArgOperand()) {
          V = RV;
          continue;
        }

      return V;
    }
    assert(V->getType()->isPointerTy() && "Unexpected operand type!");
  } while (Visited.insert(V).second);

  return V;
}

const Value *Value::stripInBoundsOffsets() const {
  return stripPointerCastsAndOffsets<PSK_InBounds>(this);
}

uint64_t Value::getPointerDereferenceableBytes(const DataLayout &DL,
                                               bool &CanBeNull) const {
  assert(getType()->isPointerTy() && "must be pointer");

  uint64_t DerefBytes = 0;
  CanBeNull = false;
  if (const Argument *A = dyn_cast<Argument>(this)) {
    DerefBytes = A->getDereferenceableBytes();
    if (DerefBytes == 0 && (A->hasByValAttr() || A->hasStructRetAttr())) {
      Type *PT = cast<PointerType>(A->getType())->getElementType();
      if (PT->isSized())
        DerefBytes = DL.getTypeStoreSize(PT);
    }
    if (DerefBytes == 0) {
      DerefBytes = A->getDereferenceableOrNullBytes();
      CanBeNull = true;
    }
  } else if (const auto *Call = dyn_cast<CallBase>(this)) {
    DerefBytes = Call->getDereferenceableBytes(AttributeList::ReturnIndex);
    if (DerefBytes == 0) {
      DerefBytes =
          Call->getDereferenceableOrNullBytes(AttributeList::ReturnIndex);
      CanBeNull = true;
    }
  } else if (const LoadInst *LI = dyn_cast<LoadInst>(this)) {
    if (MDNode *MD = LI->getMetadata(LLVMContext::MD_dereferenceable)) {
      ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(0));
      DerefBytes = CI->getLimitedValue();
    }
    if (DerefBytes == 0) {
      if (MDNode *MD =
              LI->getMetadata(LLVMContext::MD_dereferenceable_or_null)) {
        ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(0));
        DerefBytes = CI->getLimitedValue();
      }
      CanBeNull = true;
    }
  } else if (auto *AI = dyn_cast<AllocaInst>(this)) {
    if (!AI->isArrayAllocation()) {
      DerefBytes = DL.getTypeStoreSize(AI->getAllocatedType());
      CanBeNull = false;
    }
  } else if (auto *GV = dyn_cast<GlobalVariable>(this)) {
    if (GV->getValueType()->isSized() && !GV->hasExternalWeakLinkage()) {
      // TODO: Don't outright reject hasExternalWeakLinkage but set the
      // CanBeNull flag.
      DerefBytes = DL.getTypeStoreSize(GV->getValueType());
      CanBeNull = false;
    }
  }
  return DerefBytes;
}

unsigned Value::getPointerAlignment(const DataLayout &DL) const {
  assert(getType()->isPointerTy() && "must be pointer");

  unsigned Align = 0;
  if (auto *GO = dyn_cast<GlobalObject>(this)) {
    // Don't make any assumptions about function pointer alignment. Some
    // targets use the LSBs to store additional information.
    if (isa<Function>(GO))
      return 0;
    Align = GO->getAlignment();
    if (Align == 0) {
      if (auto *GVar = dyn_cast<GlobalVariable>(GO)) {
        Type *ObjectType = GVar->getValueType();
        if (ObjectType->isSized()) {
          // If the object is defined in the current Module, we'll be giving
          // it the preferred alignment. Otherwise, we have to assume that it
          // may only have the minimum ABI alignment.
          if (GVar->isStrongDefinitionForLinker())
            Align = DL.getPreferredAlignment(GVar);
          else
            Align = DL.getABITypeAlignment(ObjectType);
        }
      }
    }
  } else if (const Argument *A = dyn_cast<Argument>(this)) {
    Align = A->getParamAlignment();

    if (!Align && A->hasStructRetAttr()) {
      // An sret parameter has at least the ABI alignment of the return type.
      Type *EltTy = cast<PointerType>(A->getType())->getElementType();
      if (EltTy->isSized())
        Align = DL.getABITypeAlignment(EltTy);
    }
  } else if (const AllocaInst *AI = dyn_cast<AllocaInst>(this)) {
    Align = AI->getAlignment();
    if (Align == 0) {
      Type *AllocatedType = AI->getAllocatedType();
      if (AllocatedType->isSized())
        Align = DL.getPrefTypeAlignment(AllocatedType);
    }
  } else if (const auto *Call = dyn_cast<CallBase>(this))
    Align = Call->getAttributes().getRetAlignment();
  else if (const LoadInst *LI = dyn_cast<LoadInst>(this))
    if (MDNode *MD = LI->getMetadata(LLVMContext::MD_align)) {
      ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(0));
      Align = CI->getLimitedValue();
    }

  return Align;
}

const Value *Value::DoPHITranslation(const BasicBlock *CurBB,
                                     const BasicBlock *PredBB) const {
  auto *PN = dyn_cast<PHINode>(this);
  if (PN && PN->getParent() == CurBB)
    return PN->getIncomingValueForBlock(PredBB);
  return this;
}

LLVMContext &Value::getContext() const { return VTy->getContext(); }

void Value::reverseUseList() {
  if (!UseList || !UseList->Next)
    // No need to reverse 0 or 1 uses.
    return;

  Use *Head = UseList;
  Use *Current = UseList->Next;
  Head->Next = nullptr;
  while (Current) {
    Use *Next = Current->Next;
    Current->Next = Head;
    Head->setPrev(&Current->Next);
    Head = Current;
    Current = Next;
  }
  UseList = Head;
  Head->setPrev(&UseList);
}

bool Value::isSwiftError() const {
  auto *Arg = dyn_cast<Argument>(this);
  if (Arg)
    return Arg->hasSwiftErrorAttr();
  auto *Alloca = dyn_cast<AllocaInst>(this);
  if (!Alloca)
    return false;
  return Alloca->isSwiftError();
}

//===----------------------------------------------------------------------===//
//                             ValueHandleBase Class
//===----------------------------------------------------------------------===//

void ValueHandleBase::AddToExistingUseList(ValueHandleBase **List) {
  assert(List && "Handle list is null?");

  // Splice ourselves into the list.
  Next = *List;
  *List = this;
  setPrevPtr(List);
  if (Next) {
    Next->setPrevPtr(&Next);
    assert(getValPtr() == Next->getValPtr() && "Added to wrong list?");
  }
}

void ValueHandleBase::AddToExistingUseListAfter(ValueHandleBase *List) {
  assert(List && "Must insert after existing node");

  Next = List->Next;
  setPrevPtr(&List->Next);
  List->Next = this;
  if (Next)
    Next->setPrevPtr(&Next);
}

void ValueHandleBase::AddToUseList() {
  assert(getValPtr() && "Null pointer doesn't have a use list!");

  LLVMContextImpl *pImpl = getValPtr()->getContext().pImpl;

  if (getValPtr()->HasValueHandle) {
    // If this value already has a ValueHandle, then it must be in the
    // ValueHandles map already.
    ValueHandleBase *&Entry = pImpl->ValueHandles[getValPtr()];
    assert(Entry && "Value doesn't have any handles?");
    AddToExistingUseList(&Entry);
    return;
  }

  // Ok, it doesn't have any handles yet, so we must insert it into the
  // DenseMap.  However, doing this insertion could cause the DenseMap to
  // reallocate itself, which would invalidate all of the PrevP pointers that
  // point into the old table.  Handle this by checking for reallocation and
  // updating the stale pointers only if needed.
  DenseMap<Value*, ValueHandleBase*> &Handles = pImpl->ValueHandles;
  const void *OldBucketPtr = Handles.getPointerIntoBucketsArray();

  ValueHandleBase *&Entry = Handles[getValPtr()];
  assert(!Entry && "Value really did already have handles?");
  AddToExistingUseList(&Entry);
  getValPtr()->HasValueHandle = true;

  // If reallocation didn't happen or if this was the first insertion, don't
  // walk the table.
  if (Handles.isPointerIntoBucketsArray(OldBucketPtr) ||
      Handles.size() == 1) {
    return;
  }

  // Okay, reallocation did happen.  Fix the Prev Pointers.
  for (DenseMap<Value*, ValueHandleBase*>::iterator I = Handles.begin(),
       E = Handles.end(); I != E; ++I) {
    assert(I->second && I->first == I->second->getValPtr() &&
           "List invariant broken!");
    I->second->setPrevPtr(&I->second);
  }
}

void ValueHandleBase::RemoveFromUseList() {
  assert(getValPtr() && getValPtr()->HasValueHandle &&
         "Pointer doesn't have a use list!");

  // Unlink this from its use list.
  ValueHandleBase **PrevPtr = getPrevPtr();
  assert(*PrevPtr == this && "List invariant broken");

  *PrevPtr = Next;
  if (Next) {
    assert(Next->getPrevPtr() == &Next && "List invariant broken");
    Next->setPrevPtr(PrevPtr);
    return;
  }

  // If the Next pointer was null, then it is possible that this was the last
  // ValueHandle watching VP.  If so, delete its entry from the ValueHandles
  // map.
  LLVMContextImpl *pImpl = getValPtr()->getContext().pImpl;
  DenseMap<Value*, ValueHandleBase*> &Handles = pImpl->ValueHandles;
  if (Handles.isPointerIntoBucketsArray(PrevPtr)) {
    Handles.erase(getValPtr());
    getValPtr()->HasValueHandle = false;
  }
}

void ValueHandleBase::ValueIsDeleted(Value *V) {
  assert(V->HasValueHandle && "Should only be called if ValueHandles present");

  // Get the linked list base, which is guaranteed to exist since the
  // HasValueHandle flag is set.
  LLVMContextImpl *pImpl = V->getContext().pImpl;
  ValueHandleBase *Entry = pImpl->ValueHandles[V];
  assert(Entry && "Value bit set but no entries exist");

  // We use a local ValueHandleBase as an iterator so that ValueHandles can add
  // and remove themselves from the list without breaking our iteration.  This
  // is not really an AssertingVH; we just have to give ValueHandleBase a kind.
  // Note that we deliberately do not the support the case when dropping a value
  // handle results in a new value handle being permanently added to the list
  // (as might occur in theory for CallbackVH's): the new value handle will not
  // be processed and the checking code will mete out righteous punishment if
  // the handle is still present once we have finished processing all the other
  // value handles (it is fine to momentarily add then remove a value handle).
  for (ValueHandleBase Iterator(Assert, *Entry); Entry; Entry = Iterator.Next) {
    Iterator.RemoveFromUseList();
    Iterator.AddToExistingUseListAfter(Entry);
    assert(Entry->Next == &Iterator && "Loop invariant broken.");

    switch (Entry->getKind()) {
    case Assert:
      break;
    case Weak:
    case WeakTracking:
      // WeakTracking and Weak just go to null, which unlinks them
      // from the list.
      Entry->operator=(nullptr);
      break;
    case Callback:
      // Forward to the subclass's implementation.
      static_cast<CallbackVH*>(Entry)->deleted();
      break;
    }
  }

  // All callbacks, weak references, and assertingVHs should be dropped by now.
  if (V->HasValueHandle) {
#ifndef NDEBUG      // Only in +Asserts mode...
    dbgs() << "While deleting: " << *V->getType() << " %" << V->getName()
           << "\n";
    if (pImpl->ValueHandles[V]->getKind() == Assert)
      llvm_unreachable("An asserting value handle still pointed to this"
                       " value!");

#endif
    llvm_unreachable("All references to V were not removed?");
  }
}

void ValueHandleBase::ValueIsRAUWd(Value *Old, Value *New) {
  assert(Old->HasValueHandle &&"Should only be called if ValueHandles present");
  assert(Old != New && "Changing value into itself!");
  assert(Old->getType() == New->getType() &&
         "replaceAllUses of value with new value of different type!");

  // Get the linked list base, which is guaranteed to exist since the
  // HasValueHandle flag is set.
  LLVMContextImpl *pImpl = Old->getContext().pImpl;
  ValueHandleBase *Entry = pImpl->ValueHandles[Old];

  assert(Entry && "Value bit set but no entries exist");

  // We use a local ValueHandleBase as an iterator so that
  // ValueHandles can add and remove themselves from the list without
  // breaking our iteration.  This is not really an AssertingVH; we
  // just have to give ValueHandleBase some kind.
  for (ValueHandleBase Iterator(Assert, *Entry); Entry; Entry = Iterator.Next) {
    Iterator.RemoveFromUseList();
    Iterator.AddToExistingUseListAfter(Entry);
    assert(Entry->Next == &Iterator && "Loop invariant broken.");

    switch (Entry->getKind()) {
    case Assert:
    case Weak:
      // Asserting and Weak handles do not follow RAUW implicitly.
      break;
    case WeakTracking:
      // Weak goes to the new value, which will unlink it from Old's list.
      Entry->operator=(New);
      break;
    case Callback:
      // Forward to the subclass's implementation.
      static_cast<CallbackVH*>(Entry)->allUsesReplacedWith(New);
      break;
    }
  }

#ifndef NDEBUG
  // If any new weak value handles were added while processing the
  // list, then complain about it now.
  if (Old->HasValueHandle)
    for (Entry = pImpl->ValueHandles[Old]; Entry; Entry = Entry->Next)
      switch (Entry->getKind()) {
      case WeakTracking:
        dbgs() << "After RAUW from " << *Old->getType() << " %"
               << Old->getName() << " to " << *New->getType() << " %"
               << New->getName() << "\n";
        llvm_unreachable(
            "A weak tracking value handle still pointed to the  old value!\n");
      default:
        break;
      }
#endif
}

// Pin the vtable to this file.
void CallbackVH::anchor() {}
