//===- Record.cpp - Record implementation ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implement the tablegen record classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

static BumpPtrAllocator Allocator;

//===----------------------------------------------------------------------===//
//    Type implementations
//===----------------------------------------------------------------------===//

BitRecTy BitRecTy::Shared;
CodeRecTy CodeRecTy::Shared;
IntRecTy IntRecTy::Shared;
StringRecTy StringRecTy::Shared;
DagRecTy DagRecTy::Shared;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RecTy::dump() const { print(errs()); }
#endif

ListRecTy *RecTy::getListTy() {
  if (!ListTy)
    ListTy = new(Allocator) ListRecTy(this);
  return ListTy;
}

bool RecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  assert(RHS && "NULL pointer");
  return Kind == RHS->getRecTyKind();
}

bool RecTy::typeIsA(const RecTy *RHS) const { return this == RHS; }

bool BitRecTy::typeIsConvertibleTo(const RecTy *RHS) const{
  if (RecTy::typeIsConvertibleTo(RHS) || RHS->getRecTyKind() == IntRecTyKind)
    return true;
  if (const BitsRecTy *BitsTy = dyn_cast<BitsRecTy>(RHS))
    return BitsTy->getNumBits() == 1;
  return false;
}

BitsRecTy *BitsRecTy::get(unsigned Sz) {
  static std::vector<BitsRecTy*> Shared;
  if (Sz >= Shared.size())
    Shared.resize(Sz + 1);
  BitsRecTy *&Ty = Shared[Sz];
  if (!Ty)
    Ty = new(Allocator) BitsRecTy(Sz);
  return Ty;
}

std::string BitsRecTy::getAsString() const {
  return "bits<" + utostr(Size) + ">";
}

bool BitsRecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  if (RecTy::typeIsConvertibleTo(RHS)) //argument and the sender are same type
    return cast<BitsRecTy>(RHS)->Size == Size;
  RecTyKind kind = RHS->getRecTyKind();
  return (kind == BitRecTyKind && Size == 1) || (kind == IntRecTyKind);
}

bool BitsRecTy::typeIsA(const RecTy *RHS) const {
  if (const BitsRecTy *RHSb = dyn_cast<BitsRecTy>(RHS))
    return RHSb->Size == Size;
  return false;
}

bool IntRecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  RecTyKind kind = RHS->getRecTyKind();
  return kind==BitRecTyKind || kind==BitsRecTyKind || kind==IntRecTyKind;
}

bool CodeRecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  RecTyKind Kind = RHS->getRecTyKind();
  return Kind == CodeRecTyKind || Kind == StringRecTyKind;
}

std::string StringRecTy::getAsString() const {
  return "string";
}

bool StringRecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  RecTyKind Kind = RHS->getRecTyKind();
  return Kind == StringRecTyKind || Kind == CodeRecTyKind;
}

std::string ListRecTy::getAsString() const {
  return "list<" + Ty->getAsString() + ">";
}

bool ListRecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  if (const auto *ListTy = dyn_cast<ListRecTy>(RHS))
    return Ty->typeIsConvertibleTo(ListTy->getElementType());
  return false;
}

bool ListRecTy::typeIsA(const RecTy *RHS) const {
  if (const ListRecTy *RHSl = dyn_cast<ListRecTy>(RHS))
    return getElementType()->typeIsA(RHSl->getElementType());
  return false;
}

std::string DagRecTy::getAsString() const {
  return "dag";
}

static void ProfileRecordRecTy(FoldingSetNodeID &ID,
                               ArrayRef<Record *> Classes) {
  ID.AddInteger(Classes.size());
  for (Record *R : Classes)
    ID.AddPointer(R);
}

RecordRecTy *RecordRecTy::get(ArrayRef<Record *> UnsortedClasses) {
  if (UnsortedClasses.empty()) {
    static RecordRecTy AnyRecord(0);
    return &AnyRecord;
  }

  FoldingSet<RecordRecTy> &ThePool =
      UnsortedClasses[0]->getRecords().RecordTypePool;

  SmallVector<Record *, 4> Classes(UnsortedClasses.begin(),
                                   UnsortedClasses.end());
  llvm::sort(Classes, [](Record *LHS, Record *RHS) {
    return LHS->getNameInitAsString() < RHS->getNameInitAsString();
  });

  FoldingSetNodeID ID;
  ProfileRecordRecTy(ID, Classes);

  void *IP = nullptr;
  if (RecordRecTy *Ty = ThePool.FindNodeOrInsertPos(ID, IP))
    return Ty;

#ifndef NDEBUG
  // Check for redundancy.
  for (unsigned i = 0; i < Classes.size(); ++i) {
    for (unsigned j = 0; j < Classes.size(); ++j) {
      assert(i == j || !Classes[i]->isSubClassOf(Classes[j]));
    }
    assert(&Classes[0]->getRecords() == &Classes[i]->getRecords());
  }
#endif

  void *Mem = Allocator.Allocate(totalSizeToAlloc<Record *>(Classes.size()),
                                 alignof(RecordRecTy));
  RecordRecTy *Ty = new(Mem) RecordRecTy(Classes.size());
  std::uninitialized_copy(Classes.begin(), Classes.end(),
                          Ty->getTrailingObjects<Record *>());
  ThePool.InsertNode(Ty, IP);
  return Ty;
}

void RecordRecTy::Profile(FoldingSetNodeID &ID) const {
  ProfileRecordRecTy(ID, getClasses());
}

std::string RecordRecTy::getAsString() const {
  if (NumClasses == 1)
    return getClasses()[0]->getNameInitAsString();

  std::string Str = "{";
  bool First = true;
  for (Record *R : getClasses()) {
    if (!First)
      Str += ", ";
    First = false;
    Str += R->getNameInitAsString();
  }
  Str += "}";
  return Str;
}

bool RecordRecTy::isSubClassOf(Record *Class) const {
  return llvm::any_of(getClasses(), [Class](Record *MySuperClass) {
                                      return MySuperClass == Class ||
                                             MySuperClass->isSubClassOf(Class);
                                    });
}

bool RecordRecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  if (this == RHS)
    return true;

  const RecordRecTy *RTy = dyn_cast<RecordRecTy>(RHS);
  if (!RTy)
    return false;

  return llvm::all_of(RTy->getClasses(), [this](Record *TargetClass) {
                                           return isSubClassOf(TargetClass);
                                         });
}

bool RecordRecTy::typeIsA(const RecTy *RHS) const {
  return typeIsConvertibleTo(RHS);
}

static RecordRecTy *resolveRecordTypes(RecordRecTy *T1, RecordRecTy *T2) {
  SmallVector<Record *, 4> CommonSuperClasses;
  SmallVector<Record *, 4> Stack;

  Stack.insert(Stack.end(), T1->classes_begin(), T1->classes_end());

  while (!Stack.empty()) {
    Record *R = Stack.back();
    Stack.pop_back();

    if (T2->isSubClassOf(R)) {
      CommonSuperClasses.push_back(R);
    } else {
      R->getDirectSuperClasses(Stack);
    }
  }

  return RecordRecTy::get(CommonSuperClasses);
}

RecTy *llvm::resolveTypes(RecTy *T1, RecTy *T2) {
  if (T1 == T2)
    return T1;

  if (RecordRecTy *RecTy1 = dyn_cast<RecordRecTy>(T1)) {
    if (RecordRecTy *RecTy2 = dyn_cast<RecordRecTy>(T2))
      return resolveRecordTypes(RecTy1, RecTy2);
  }

  if (T1->typeIsConvertibleTo(T2))
    return T2;
  if (T2->typeIsConvertibleTo(T1))
    return T1;

  if (ListRecTy *ListTy1 = dyn_cast<ListRecTy>(T1)) {
    if (ListRecTy *ListTy2 = dyn_cast<ListRecTy>(T2)) {
      RecTy* NewType = resolveTypes(ListTy1->getElementType(),
                                    ListTy2->getElementType());
      if (NewType)
        return NewType->getListTy();
    }
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
//    Initializer implementations
//===----------------------------------------------------------------------===//

void Init::anchor() {}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void Init::dump() const { return print(errs()); }
#endif

UnsetInit *UnsetInit::get() {
  static UnsetInit TheInit;
  return &TheInit;
}

Init *UnsetInit::getCastTo(RecTy *Ty) const {
  return const_cast<UnsetInit *>(this);
}

Init *UnsetInit::convertInitializerTo(RecTy *Ty) const {
  return const_cast<UnsetInit *>(this);
}

BitInit *BitInit::get(bool V) {
  static BitInit True(true);
  static BitInit False(false);

  return V ? &True : &False;
}

Init *BitInit::convertInitializerTo(RecTy *Ty) const {
  if (isa<BitRecTy>(Ty))
    return const_cast<BitInit *>(this);

  if (isa<IntRecTy>(Ty))
    return IntInit::get(getValue());

  if (auto *BRT = dyn_cast<BitsRecTy>(Ty)) {
    // Can only convert single bit.
    if (BRT->getNumBits() == 1)
      return BitsInit::get(const_cast<BitInit *>(this));
  }

  return nullptr;
}

static void
ProfileBitsInit(FoldingSetNodeID &ID, ArrayRef<Init *> Range) {
  ID.AddInteger(Range.size());

  for (Init *I : Range)
    ID.AddPointer(I);
}

BitsInit *BitsInit::get(ArrayRef<Init *> Range) {
  static FoldingSet<BitsInit> ThePool;

  FoldingSetNodeID ID;
  ProfileBitsInit(ID, Range);

  void *IP = nullptr;
  if (BitsInit *I = ThePool.FindNodeOrInsertPos(ID, IP))
    return I;

  void *Mem = Allocator.Allocate(totalSizeToAlloc<Init *>(Range.size()),
                                 alignof(BitsInit));
  BitsInit *I = new(Mem) BitsInit(Range.size());
  std::uninitialized_copy(Range.begin(), Range.end(),
                          I->getTrailingObjects<Init *>());
  ThePool.InsertNode(I, IP);
  return I;
}

void BitsInit::Profile(FoldingSetNodeID &ID) const {
  ProfileBitsInit(ID, makeArrayRef(getTrailingObjects<Init *>(), NumBits));
}

Init *BitsInit::convertInitializerTo(RecTy *Ty) const {
  if (isa<BitRecTy>(Ty)) {
    if (getNumBits() != 1) return nullptr; // Only accept if just one bit!
    return getBit(0);
  }

  if (auto *BRT = dyn_cast<BitsRecTy>(Ty)) {
    // If the number of bits is right, return it.  Otherwise we need to expand
    // or truncate.
    if (getNumBits() != BRT->getNumBits()) return nullptr;
    return const_cast<BitsInit *>(this);
  }

  if (isa<IntRecTy>(Ty)) {
    int64_t Result = 0;
    for (unsigned i = 0, e = getNumBits(); i != e; ++i)
      if (auto *Bit = dyn_cast<BitInit>(getBit(i)))
        Result |= static_cast<int64_t>(Bit->getValue()) << i;
      else
        return nullptr;
    return IntInit::get(Result);
  }

  return nullptr;
}

Init *
BitsInit::convertInitializerBitRange(ArrayRef<unsigned> Bits) const {
  SmallVector<Init *, 16> NewBits(Bits.size());

  for (unsigned i = 0, e = Bits.size(); i != e; ++i) {
    if (Bits[i] >= getNumBits())
      return nullptr;
    NewBits[i] = getBit(Bits[i]);
  }
  return BitsInit::get(NewBits);
}

bool BitsInit::isConcrete() const {
  for (unsigned i = 0, e = getNumBits(); i != e; ++i) {
    if (!getBit(i)->isConcrete())
      return false;
  }
  return true;
}

std::string BitsInit::getAsString() const {
  std::string Result = "{ ";
  for (unsigned i = 0, e = getNumBits(); i != e; ++i) {
    if (i) Result += ", ";
    if (Init *Bit = getBit(e-i-1))
      Result += Bit->getAsString();
    else
      Result += "*";
  }
  return Result + " }";
}

// resolveReferences - If there are any field references that refer to fields
// that have been filled in, we can propagate the values now.
Init *BitsInit::resolveReferences(Resolver &R) const {
  bool Changed = false;
  SmallVector<Init *, 16> NewBits(getNumBits());

  Init *CachedBitVarRef = nullptr;
  Init *CachedBitVarResolved = nullptr;

  for (unsigned i = 0, e = getNumBits(); i != e; ++i) {
    Init *CurBit = getBit(i);
    Init *NewBit = CurBit;

    if (VarBitInit *CurBitVar = dyn_cast<VarBitInit>(CurBit)) {
      if (CurBitVar->getBitVar() != CachedBitVarRef) {
        CachedBitVarRef = CurBitVar->getBitVar();
        CachedBitVarResolved = CachedBitVarRef->resolveReferences(R);
      }

      NewBit = CachedBitVarResolved->getBit(CurBitVar->getBitNum());
    } else {
      // getBit(0) implicitly converts int and bits<1> values to bit.
      NewBit = CurBit->resolveReferences(R)->getBit(0);
    }

    if (isa<UnsetInit>(NewBit) && R.keepUnsetBits())
      NewBit = CurBit;
    NewBits[i] = NewBit;
    Changed |= CurBit != NewBit;
  }

  if (Changed)
    return BitsInit::get(NewBits);

  return const_cast<BitsInit *>(this);
}

IntInit *IntInit::get(int64_t V) {
  static DenseMap<int64_t, IntInit*> ThePool;

  IntInit *&I = ThePool[V];
  if (!I) I = new(Allocator) IntInit(V);
  return I;
}

std::string IntInit::getAsString() const {
  return itostr(Value);
}

static bool canFitInBitfield(int64_t Value, unsigned NumBits) {
  // For example, with NumBits == 4, we permit Values from [-7 .. 15].
  return (NumBits >= sizeof(Value) * 8) ||
         (Value >> NumBits == 0) || (Value >> (NumBits-1) == -1);
}

Init *IntInit::convertInitializerTo(RecTy *Ty) const {
  if (isa<IntRecTy>(Ty))
    return const_cast<IntInit *>(this);

  if (isa<BitRecTy>(Ty)) {
    int64_t Val = getValue();
    if (Val != 0 && Val != 1) return nullptr;  // Only accept 0 or 1 for a bit!
    return BitInit::get(Val != 0);
  }

  if (auto *BRT = dyn_cast<BitsRecTy>(Ty)) {
    int64_t Value = getValue();
    // Make sure this bitfield is large enough to hold the integer value.
    if (!canFitInBitfield(Value, BRT->getNumBits()))
      return nullptr;

    SmallVector<Init *, 16> NewBits(BRT->getNumBits());
    for (unsigned i = 0; i != BRT->getNumBits(); ++i)
      NewBits[i] = BitInit::get(Value & ((i < 64) ? (1LL << i) : 0));

    return BitsInit::get(NewBits);
  }

  return nullptr;
}

Init *
IntInit::convertInitializerBitRange(ArrayRef<unsigned> Bits) const {
  SmallVector<Init *, 16> NewBits(Bits.size());

  for (unsigned i = 0, e = Bits.size(); i != e; ++i) {
    if (Bits[i] >= 64)
      return nullptr;

    NewBits[i] = BitInit::get(Value & (INT64_C(1) << Bits[i]));
  }
  return BitsInit::get(NewBits);
}

CodeInit *CodeInit::get(StringRef V) {
  static StringMap<CodeInit*, BumpPtrAllocator &> ThePool(Allocator);

  auto &Entry = *ThePool.insert(std::make_pair(V, nullptr)).first;
  if (!Entry.second)
    Entry.second = new(Allocator) CodeInit(Entry.getKey());
  return Entry.second;
}

StringInit *StringInit::get(StringRef V) {
  static StringMap<StringInit*, BumpPtrAllocator &> ThePool(Allocator);

  auto &Entry = *ThePool.insert(std::make_pair(V, nullptr)).first;
  if (!Entry.second)
    Entry.second = new(Allocator) StringInit(Entry.getKey());
  return Entry.second;
}

Init *StringInit::convertInitializerTo(RecTy *Ty) const {
  if (isa<StringRecTy>(Ty))
    return const_cast<StringInit *>(this);
  if (isa<CodeRecTy>(Ty))
    return CodeInit::get(getValue());

  return nullptr;
}

Init *CodeInit::convertInitializerTo(RecTy *Ty) const {
  if (isa<CodeRecTy>(Ty))
    return const_cast<CodeInit *>(this);
  if (isa<StringRecTy>(Ty))
    return StringInit::get(getValue());

  return nullptr;
}

static void ProfileListInit(FoldingSetNodeID &ID,
                            ArrayRef<Init *> Range,
                            RecTy *EltTy) {
  ID.AddInteger(Range.size());
  ID.AddPointer(EltTy);

  for (Init *I : Range)
    ID.AddPointer(I);
}

ListInit *ListInit::get(ArrayRef<Init *> Range, RecTy *EltTy) {
  static FoldingSet<ListInit> ThePool;

  FoldingSetNodeID ID;
  ProfileListInit(ID, Range, EltTy);

  void *IP = nullptr;
  if (ListInit *I = ThePool.FindNodeOrInsertPos(ID, IP))
    return I;

  assert(Range.empty() || !isa<TypedInit>(Range[0]) ||
         cast<TypedInit>(Range[0])->getType()->typeIsConvertibleTo(EltTy));

  void *Mem = Allocator.Allocate(totalSizeToAlloc<Init *>(Range.size()),
                                 alignof(ListInit));
  ListInit *I = new(Mem) ListInit(Range.size(), EltTy);
  std::uninitialized_copy(Range.begin(), Range.end(),
                          I->getTrailingObjects<Init *>());
  ThePool.InsertNode(I, IP);
  return I;
}

void ListInit::Profile(FoldingSetNodeID &ID) const {
  RecTy *EltTy = cast<ListRecTy>(getType())->getElementType();

  ProfileListInit(ID, getValues(), EltTy);
}

Init *ListInit::convertInitializerTo(RecTy *Ty) const {
  if (getType() == Ty)
    return const_cast<ListInit*>(this);

  if (auto *LRT = dyn_cast<ListRecTy>(Ty)) {
    SmallVector<Init*, 8> Elements;
    Elements.reserve(getValues().size());

    // Verify that all of the elements of the list are subclasses of the
    // appropriate class!
    bool Changed = false;
    RecTy *ElementType = LRT->getElementType();
    for (Init *I : getValues())
      if (Init *CI = I->convertInitializerTo(ElementType)) {
        Elements.push_back(CI);
        if (CI != I)
          Changed = true;
      } else
        return nullptr;

    if (!Changed)
      return const_cast<ListInit*>(this);
    return ListInit::get(Elements, ElementType);
  }

  return nullptr;
}

Init *ListInit::convertInitListSlice(ArrayRef<unsigned> Elements) const {
  SmallVector<Init*, 8> Vals;
  Vals.reserve(Elements.size());
  for (unsigned Element : Elements) {
    if (Element >= size())
      return nullptr;
    Vals.push_back(getElement(Element));
  }
  return ListInit::get(Vals, getElementType());
}

Record *ListInit::getElementAsRecord(unsigned i) const {
  assert(i < NumValues && "List element index out of range!");
  DefInit *DI = dyn_cast<DefInit>(getElement(i));
  if (!DI)
    PrintFatalError("Expected record in list!");
  return DI->getDef();
}

Init *ListInit::resolveReferences(Resolver &R) const {
  SmallVector<Init*, 8> Resolved;
  Resolved.reserve(size());
  bool Changed = false;

  for (Init *CurElt : getValues()) {
    Init *E = CurElt->resolveReferences(R);
    Changed |= E != CurElt;
    Resolved.push_back(E);
  }

  if (Changed)
    return ListInit::get(Resolved, getElementType());
  return const_cast<ListInit *>(this);
}

bool ListInit::isConcrete() const {
  for (Init *Element : *this) {
    if (!Element->isConcrete())
      return false;
  }
  return true;
}

std::string ListInit::getAsString() const {
  std::string Result = "[";
  const char *sep = "";
  for (Init *Element : *this) {
    Result += sep;
    sep = ", ";
    Result += Element->getAsString();
  }
  return Result + "]";
}

Init *OpInit::getBit(unsigned Bit) const {
  if (getType() == BitRecTy::get())
    return const_cast<OpInit*>(this);
  return VarBitInit::get(const_cast<OpInit*>(this), Bit);
}

static void
ProfileUnOpInit(FoldingSetNodeID &ID, unsigned Opcode, Init *Op, RecTy *Type) {
  ID.AddInteger(Opcode);
  ID.AddPointer(Op);
  ID.AddPointer(Type);
}

UnOpInit *UnOpInit::get(UnaryOp Opc, Init *LHS, RecTy *Type) {
  static FoldingSet<UnOpInit> ThePool;

  FoldingSetNodeID ID;
  ProfileUnOpInit(ID, Opc, LHS, Type);

  void *IP = nullptr;
  if (UnOpInit *I = ThePool.FindNodeOrInsertPos(ID, IP))
    return I;

  UnOpInit *I = new(Allocator) UnOpInit(Opc, LHS, Type);
  ThePool.InsertNode(I, IP);
  return I;
}

void UnOpInit::Profile(FoldingSetNodeID &ID) const {
  ProfileUnOpInit(ID, getOpcode(), getOperand(), getType());
}

Init *UnOpInit::Fold(Record *CurRec, bool IsFinal) const {
  switch (getOpcode()) {
  case CAST:
    if (isa<StringRecTy>(getType())) {
      if (StringInit *LHSs = dyn_cast<StringInit>(LHS))
        return LHSs;

      if (DefInit *LHSd = dyn_cast<DefInit>(LHS))
        return StringInit::get(LHSd->getAsString());

      if (IntInit *LHSi = dyn_cast<IntInit>(LHS))
        return StringInit::get(LHSi->getAsString());
    } else if (isa<RecordRecTy>(getType())) {
      if (StringInit *Name = dyn_cast<StringInit>(LHS)) {
        if (!CurRec && !IsFinal)
          break;
        assert(CurRec && "NULL pointer");
        Record *D;

        // Self-references are allowed, but their resolution is delayed until
        // the final resolve to ensure that we get the correct type for them.
        if (Name == CurRec->getNameInit()) {
          if (!IsFinal)
            break;
          D = CurRec;
        } else {
          D = CurRec->getRecords().getDef(Name->getValue());
          if (!D) {
            if (IsFinal)
              PrintFatalError(CurRec->getLoc(),
                              Twine("Undefined reference to record: '") +
                              Name->getValue() + "'\n");
            break;
          }
        }

        DefInit *DI = DefInit::get(D);
        if (!DI->getType()->typeIsA(getType())) {
          PrintFatalError(CurRec->getLoc(),
                          Twine("Expected type '") +
                          getType()->getAsString() + "', got '" +
                          DI->getType()->getAsString() + "' in: " +
                          getAsString() + "\n");
        }
        return DI;
      }
    }

    if (Init *NewInit = LHS->convertInitializerTo(getType()))
      return NewInit;
    break;

  case HEAD:
    if (ListInit *LHSl = dyn_cast<ListInit>(LHS)) {
      assert(!LHSl->empty() && "Empty list in head");
      return LHSl->getElement(0);
    }
    break;

  case TAIL:
    if (ListInit *LHSl = dyn_cast<ListInit>(LHS)) {
      assert(!LHSl->empty() && "Empty list in tail");
      // Note the +1.  We can't just pass the result of getValues()
      // directly.
      return ListInit::get(LHSl->getValues().slice(1), LHSl->getElementType());
    }
    break;

  case SIZE:
    if (ListInit *LHSl = dyn_cast<ListInit>(LHS))
      return IntInit::get(LHSl->size());
    break;

  case EMPTY:
    if (ListInit *LHSl = dyn_cast<ListInit>(LHS))
      return IntInit::get(LHSl->empty());
    if (StringInit *LHSs = dyn_cast<StringInit>(LHS))
      return IntInit::get(LHSs->getValue().empty());
    break;
  }
  return const_cast<UnOpInit *>(this);
}

Init *UnOpInit::resolveReferences(Resolver &R) const {
  Init *lhs = LHS->resolveReferences(R);

  if (LHS != lhs || (R.isFinal() && getOpcode() == CAST))
    return (UnOpInit::get(getOpcode(), lhs, getType()))
        ->Fold(R.getCurrentRecord(), R.isFinal());
  return const_cast<UnOpInit *>(this);
}

std::string UnOpInit::getAsString() const {
  std::string Result;
  switch (getOpcode()) {
  case CAST: Result = "!cast<" + getType()->getAsString() + ">"; break;
  case HEAD: Result = "!head"; break;
  case TAIL: Result = "!tail"; break;
  case SIZE: Result = "!size"; break;
  case EMPTY: Result = "!empty"; break;
  }
  return Result + "(" + LHS->getAsString() + ")";
}

static void
ProfileBinOpInit(FoldingSetNodeID &ID, unsigned Opcode, Init *LHS, Init *RHS,
                 RecTy *Type) {
  ID.AddInteger(Opcode);
  ID.AddPointer(LHS);
  ID.AddPointer(RHS);
  ID.AddPointer(Type);
}

BinOpInit *BinOpInit::get(BinaryOp Opc, Init *LHS,
                          Init *RHS, RecTy *Type) {
  static FoldingSet<BinOpInit> ThePool;

  FoldingSetNodeID ID;
  ProfileBinOpInit(ID, Opc, LHS, RHS, Type);

  void *IP = nullptr;
  if (BinOpInit *I = ThePool.FindNodeOrInsertPos(ID, IP))
    return I;

  BinOpInit *I = new(Allocator) BinOpInit(Opc, LHS, RHS, Type);
  ThePool.InsertNode(I, IP);
  return I;
}

void BinOpInit::Profile(FoldingSetNodeID &ID) const {
  ProfileBinOpInit(ID, getOpcode(), getLHS(), getRHS(), getType());
}

static StringInit *ConcatStringInits(const StringInit *I0,
                                     const StringInit *I1) {
  SmallString<80> Concat(I0->getValue());
  Concat.append(I1->getValue());
  return StringInit::get(Concat);
}

Init *BinOpInit::getStrConcat(Init *I0, Init *I1) {
  // Shortcut for the common case of concatenating two strings.
  if (const StringInit *I0s = dyn_cast<StringInit>(I0))
    if (const StringInit *I1s = dyn_cast<StringInit>(I1))
      return ConcatStringInits(I0s, I1s);
  return BinOpInit::get(BinOpInit::STRCONCAT, I0, I1, StringRecTy::get());
}

Init *BinOpInit::Fold(Record *CurRec) const {
  switch (getOpcode()) {
  case CONCAT: {
    DagInit *LHSs = dyn_cast<DagInit>(LHS);
    DagInit *RHSs = dyn_cast<DagInit>(RHS);
    if (LHSs && RHSs) {
      DefInit *LOp = dyn_cast<DefInit>(LHSs->getOperator());
      DefInit *ROp = dyn_cast<DefInit>(RHSs->getOperator());
      if (!LOp || !ROp)
        break;
      if (LOp->getDef() != ROp->getDef()) {
        PrintFatalError(Twine("Concatenated Dag operators do not match: '") +
                        LHSs->getAsString() + "' vs. '" + RHSs->getAsString() +
                        "'");
      }
      SmallVector<Init*, 8> Args;
      SmallVector<StringInit*, 8> ArgNames;
      for (unsigned i = 0, e = LHSs->getNumArgs(); i != e; ++i) {
        Args.push_back(LHSs->getArg(i));
        ArgNames.push_back(LHSs->getArgName(i));
      }
      for (unsigned i = 0, e = RHSs->getNumArgs(); i != e; ++i) {
        Args.push_back(RHSs->getArg(i));
        ArgNames.push_back(RHSs->getArgName(i));
      }
      return DagInit::get(LHSs->getOperator(), nullptr, Args, ArgNames);
    }
    break;
  }
  case LISTCONCAT: {
    ListInit *LHSs = dyn_cast<ListInit>(LHS);
    ListInit *RHSs = dyn_cast<ListInit>(RHS);
    if (LHSs && RHSs) {
      SmallVector<Init *, 8> Args;
      Args.insert(Args.end(), LHSs->begin(), LHSs->end());
      Args.insert(Args.end(), RHSs->begin(), RHSs->end());
      return ListInit::get(Args, LHSs->getElementType());
    }
    break;
  }
  case STRCONCAT: {
    StringInit *LHSs = dyn_cast<StringInit>(LHS);
    StringInit *RHSs = dyn_cast<StringInit>(RHS);
    if (LHSs && RHSs)
      return ConcatStringInits(LHSs, RHSs);
    break;
  }
  case EQ:
  case NE:
  case LE:
  case LT:
  case GE:
  case GT: {
    // try to fold eq comparison for 'bit' and 'int', otherwise fallback
    // to string objects.
    IntInit *L =
        dyn_cast_or_null<IntInit>(LHS->convertInitializerTo(IntRecTy::get()));
    IntInit *R =
        dyn_cast_or_null<IntInit>(RHS->convertInitializerTo(IntRecTy::get()));

    if (L && R) {
      bool Result;
      switch (getOpcode()) {
      case EQ: Result = L->getValue() == R->getValue(); break;
      case NE: Result = L->getValue() != R->getValue(); break;
      case LE: Result = L->getValue() <= R->getValue(); break;
      case LT: Result = L->getValue() < R->getValue(); break;
      case GE: Result = L->getValue() >= R->getValue(); break;
      case GT: Result = L->getValue() > R->getValue(); break;
      default: llvm_unreachable("unhandled comparison");
      }
      return BitInit::get(Result);
    }

    if (getOpcode() == EQ || getOpcode() == NE) {
      StringInit *LHSs = dyn_cast<StringInit>(LHS);
      StringInit *RHSs = dyn_cast<StringInit>(RHS);

      // Make sure we've resolved
      if (LHSs && RHSs) {
        bool Equal = LHSs->getValue() == RHSs->getValue();
        return BitInit::get(getOpcode() == EQ ? Equal : !Equal);
      }
    }

    break;
  }
  case ADD:
  case AND:
  case OR:
  case SHL:
  case SRA:
  case SRL: {
    IntInit *LHSi =
      dyn_cast_or_null<IntInit>(LHS->convertInitializerTo(IntRecTy::get()));
    IntInit *RHSi =
      dyn_cast_or_null<IntInit>(RHS->convertInitializerTo(IntRecTy::get()));
    if (LHSi && RHSi) {
      int64_t LHSv = LHSi->getValue(), RHSv = RHSi->getValue();
      int64_t Result;
      switch (getOpcode()) {
      default: llvm_unreachable("Bad opcode!");
      case ADD: Result = LHSv +  RHSv; break;
      case AND: Result = LHSv &  RHSv; break;
      case OR: Result = LHSv | RHSv; break;
      case SHL: Result = LHSv << RHSv; break;
      case SRA: Result = LHSv >> RHSv; break;
      case SRL: Result = (uint64_t)LHSv >> (uint64_t)RHSv; break;
      }
      return IntInit::get(Result);
    }
    break;
  }
  }
  return const_cast<BinOpInit *>(this);
}

Init *BinOpInit::resolveReferences(Resolver &R) const {
  Init *lhs = LHS->resolveReferences(R);
  Init *rhs = RHS->resolveReferences(R);

  if (LHS != lhs || RHS != rhs)
    return (BinOpInit::get(getOpcode(), lhs, rhs, getType()))
        ->Fold(R.getCurrentRecord());
  return const_cast<BinOpInit *>(this);
}

std::string BinOpInit::getAsString() const {
  std::string Result;
  switch (getOpcode()) {
  case CONCAT: Result = "!con"; break;
  case ADD: Result = "!add"; break;
  case AND: Result = "!and"; break;
  case OR: Result = "!or"; break;
  case SHL: Result = "!shl"; break;
  case SRA: Result = "!sra"; break;
  case SRL: Result = "!srl"; break;
  case EQ: Result = "!eq"; break;
  case NE: Result = "!ne"; break;
  case LE: Result = "!le"; break;
  case LT: Result = "!lt"; break;
  case GE: Result = "!ge"; break;
  case GT: Result = "!gt"; break;
  case LISTCONCAT: Result = "!listconcat"; break;
  case STRCONCAT: Result = "!strconcat"; break;
  }
  return Result + "(" + LHS->getAsString() + ", " + RHS->getAsString() + ")";
}

static void
ProfileTernOpInit(FoldingSetNodeID &ID, unsigned Opcode, Init *LHS, Init *MHS,
                  Init *RHS, RecTy *Type) {
  ID.AddInteger(Opcode);
  ID.AddPointer(LHS);
  ID.AddPointer(MHS);
  ID.AddPointer(RHS);
  ID.AddPointer(Type);
}

TernOpInit *TernOpInit::get(TernaryOp Opc, Init *LHS, Init *MHS, Init *RHS,
                            RecTy *Type) {
  static FoldingSet<TernOpInit> ThePool;

  FoldingSetNodeID ID;
  ProfileTernOpInit(ID, Opc, LHS, MHS, RHS, Type);

  void *IP = nullptr;
  if (TernOpInit *I = ThePool.FindNodeOrInsertPos(ID, IP))
    return I;

  TernOpInit *I = new(Allocator) TernOpInit(Opc, LHS, MHS, RHS, Type);
  ThePool.InsertNode(I, IP);
  return I;
}

void TernOpInit::Profile(FoldingSetNodeID &ID) const {
  ProfileTernOpInit(ID, getOpcode(), getLHS(), getMHS(), getRHS(), getType());
}

static Init *ForeachApply(Init *LHS, Init *MHSe, Init *RHS, Record *CurRec) {
  MapResolver R(CurRec);
  R.set(LHS, MHSe);
  return RHS->resolveReferences(R);
}

static Init *ForeachDagApply(Init *LHS, DagInit *MHSd, Init *RHS,
                             Record *CurRec) {
  bool Change = false;
  Init *Val = ForeachApply(LHS, MHSd->getOperator(), RHS, CurRec);
  if (Val != MHSd->getOperator())
    Change = true;

  SmallVector<std::pair<Init *, StringInit *>, 8> NewArgs;
  for (unsigned int i = 0; i < MHSd->getNumArgs(); ++i) {
    Init *Arg = MHSd->getArg(i);
    Init *NewArg;
    StringInit *ArgName = MHSd->getArgName(i);

    if (DagInit *Argd = dyn_cast<DagInit>(Arg))
      NewArg = ForeachDagApply(LHS, Argd, RHS, CurRec);
    else
      NewArg = ForeachApply(LHS, Arg, RHS, CurRec);

    NewArgs.push_back(std::make_pair(NewArg, ArgName));
    if (Arg != NewArg)
      Change = true;
  }

  if (Change)
    return DagInit::get(Val, nullptr, NewArgs);
  return MHSd;
}

// Applies RHS to all elements of MHS, using LHS as a temp variable.
static Init *ForeachHelper(Init *LHS, Init *MHS, Init *RHS, RecTy *Type,
                           Record *CurRec) {
  if (DagInit *MHSd = dyn_cast<DagInit>(MHS))
    return ForeachDagApply(LHS, MHSd, RHS, CurRec);

  if (ListInit *MHSl = dyn_cast<ListInit>(MHS)) {
    SmallVector<Init *, 8> NewList(MHSl->begin(), MHSl->end());

    for (Init *&Item : NewList) {
      Init *NewItem = ForeachApply(LHS, Item, RHS, CurRec);
      if (NewItem != Item)
        Item = NewItem;
    }
    return ListInit::get(NewList, cast<ListRecTy>(Type)->getElementType());
  }

  return nullptr;
}

Init *TernOpInit::Fold(Record *CurRec) const {
  switch (getOpcode()) {
  case SUBST: {
    DefInit *LHSd = dyn_cast<DefInit>(LHS);
    VarInit *LHSv = dyn_cast<VarInit>(LHS);
    StringInit *LHSs = dyn_cast<StringInit>(LHS);

    DefInit *MHSd = dyn_cast<DefInit>(MHS);
    VarInit *MHSv = dyn_cast<VarInit>(MHS);
    StringInit *MHSs = dyn_cast<StringInit>(MHS);

    DefInit *RHSd = dyn_cast<DefInit>(RHS);
    VarInit *RHSv = dyn_cast<VarInit>(RHS);
    StringInit *RHSs = dyn_cast<StringInit>(RHS);

    if (LHSd && MHSd && RHSd) {
      Record *Val = RHSd->getDef();
      if (LHSd->getAsString() == RHSd->getAsString())
        Val = MHSd->getDef();
      return DefInit::get(Val);
    }
    if (LHSv && MHSv && RHSv) {
      std::string Val = RHSv->getName();
      if (LHSv->getAsString() == RHSv->getAsString())
        Val = MHSv->getName();
      return VarInit::get(Val, getType());
    }
    if (LHSs && MHSs && RHSs) {
      std::string Val = RHSs->getValue();

      std::string::size_type found;
      std::string::size_type idx = 0;
      while (true) {
        found = Val.find(LHSs->getValue(), idx);
        if (found == std::string::npos)
          break;
        Val.replace(found, LHSs->getValue().size(), MHSs->getValue());
        idx = found + MHSs->getValue().size();
      }

      return StringInit::get(Val);
    }
    break;
  }

  case FOREACH: {
    if (Init *Result = ForeachHelper(LHS, MHS, RHS, getType(), CurRec))
      return Result;
    break;
  }

  case IF: {
    if (IntInit *LHSi = dyn_cast_or_null<IntInit>(
                            LHS->convertInitializerTo(IntRecTy::get()))) {
      if (LHSi->getValue())
        return MHS;
      return RHS;
    }
    break;
  }

  case DAG: {
    ListInit *MHSl = dyn_cast<ListInit>(MHS);
    ListInit *RHSl = dyn_cast<ListInit>(RHS);
    bool MHSok = MHSl || isa<UnsetInit>(MHS);
    bool RHSok = RHSl || isa<UnsetInit>(RHS);

    if (isa<UnsetInit>(MHS) && isa<UnsetInit>(RHS))
      break; // Typically prevented by the parser, but might happen with template args

    if (MHSok && RHSok && (!MHSl || !RHSl || MHSl->size() == RHSl->size())) {
      SmallVector<std::pair<Init *, StringInit *>, 8> Children;
      unsigned Size = MHSl ? MHSl->size() : RHSl->size();
      for (unsigned i = 0; i != Size; ++i) {
        Init *Node = MHSl ? MHSl->getElement(i) : UnsetInit::get();
        Init *Name = RHSl ? RHSl->getElement(i) : UnsetInit::get();
        if (!isa<StringInit>(Name) && !isa<UnsetInit>(Name))
          return const_cast<TernOpInit *>(this);
        Children.emplace_back(Node, dyn_cast<StringInit>(Name));
      }
      return DagInit::get(LHS, nullptr, Children);
    }
    break;
  }
  }

  return const_cast<TernOpInit *>(this);
}

Init *TernOpInit::resolveReferences(Resolver &R) const {
  Init *lhs = LHS->resolveReferences(R);

  if (getOpcode() == IF && lhs != LHS) {
    if (IntInit *Value = dyn_cast_or_null<IntInit>(
                             lhs->convertInitializerTo(IntRecTy::get()))) {
      // Short-circuit
      if (Value->getValue())
        return MHS->resolveReferences(R);
      return RHS->resolveReferences(R);
    }
  }

  Init *mhs = MHS->resolveReferences(R);
  Init *rhs;

  if (getOpcode() == FOREACH) {
    ShadowResolver SR(R);
    SR.addShadow(lhs);
    rhs = RHS->resolveReferences(SR);
  } else {
    rhs = RHS->resolveReferences(R);
  }

  if (LHS != lhs || MHS != mhs || RHS != rhs)
    return (TernOpInit::get(getOpcode(), lhs, mhs, rhs, getType()))
        ->Fold(R.getCurrentRecord());
  return const_cast<TernOpInit *>(this);
}

std::string TernOpInit::getAsString() const {
  std::string Result;
  bool UnquotedLHS = false;
  switch (getOpcode()) {
  case SUBST: Result = "!subst"; break;
  case FOREACH: Result = "!foreach"; UnquotedLHS = true; break;
  case IF: Result = "!if"; break;
  case DAG: Result = "!dag"; break;
  }
  return (Result + "(" +
          (UnquotedLHS ? LHS->getAsUnquotedString() : LHS->getAsString()) +
          ", " + MHS->getAsString() + ", " + RHS->getAsString() + ")");
}

static void ProfileFoldOpInit(FoldingSetNodeID &ID, Init *A, Init *B,
                              Init *Start, Init *List, Init *Expr,
                              RecTy *Type) {
  ID.AddPointer(Start);
  ID.AddPointer(List);
  ID.AddPointer(A);
  ID.AddPointer(B);
  ID.AddPointer(Expr);
  ID.AddPointer(Type);
}

FoldOpInit *FoldOpInit::get(Init *Start, Init *List, Init *A, Init *B,
                            Init *Expr, RecTy *Type) {
  static FoldingSet<FoldOpInit> ThePool;

  FoldingSetNodeID ID;
  ProfileFoldOpInit(ID, Start, List, A, B, Expr, Type);

  void *IP = nullptr;
  if (FoldOpInit *I = ThePool.FindNodeOrInsertPos(ID, IP))
    return I;

  FoldOpInit *I = new (Allocator) FoldOpInit(Start, List, A, B, Expr, Type);
  ThePool.InsertNode(I, IP);
  return I;
}

void FoldOpInit::Profile(FoldingSetNodeID &ID) const {
  ProfileFoldOpInit(ID, Start, List, A, B, Expr, getType());
}

Init *FoldOpInit::Fold(Record *CurRec) const {
  if (ListInit *LI = dyn_cast<ListInit>(List)) {
    Init *Accum = Start;
    for (Init *Elt : *LI) {
      MapResolver R(CurRec);
      R.set(A, Accum);
      R.set(B, Elt);
      Accum = Expr->resolveReferences(R);
    }
    return Accum;
  }
  return const_cast<FoldOpInit *>(this);
}

Init *FoldOpInit::resolveReferences(Resolver &R) const {
  Init *NewStart = Start->resolveReferences(R);
  Init *NewList = List->resolveReferences(R);
  ShadowResolver SR(R);
  SR.addShadow(A);
  SR.addShadow(B);
  Init *NewExpr = Expr->resolveReferences(SR);

  if (Start == NewStart && List == NewList && Expr == NewExpr)
    return const_cast<FoldOpInit *>(this);

  return get(NewStart, NewList, A, B, NewExpr, getType())
      ->Fold(R.getCurrentRecord());
}

Init *FoldOpInit::getBit(unsigned Bit) const {
  return VarBitInit::get(const_cast<FoldOpInit *>(this), Bit);
}

std::string FoldOpInit::getAsString() const {
  return (Twine("!foldl(") + Start->getAsString() + ", " + List->getAsString() +
          ", " + A->getAsUnquotedString() + ", " + B->getAsUnquotedString() +
          ", " + Expr->getAsString() + ")")
      .str();
}

static void ProfileIsAOpInit(FoldingSetNodeID &ID, RecTy *CheckType,
                             Init *Expr) {
  ID.AddPointer(CheckType);
  ID.AddPointer(Expr);
}

IsAOpInit *IsAOpInit::get(RecTy *CheckType, Init *Expr) {
  static FoldingSet<IsAOpInit> ThePool;

  FoldingSetNodeID ID;
  ProfileIsAOpInit(ID, CheckType, Expr);

  void *IP = nullptr;
  if (IsAOpInit *I = ThePool.FindNodeOrInsertPos(ID, IP))
    return I;

  IsAOpInit *I = new (Allocator) IsAOpInit(CheckType, Expr);
  ThePool.InsertNode(I, IP);
  return I;
}

void IsAOpInit::Profile(FoldingSetNodeID &ID) const {
  ProfileIsAOpInit(ID, CheckType, Expr);
}

Init *IsAOpInit::Fold() const {
  if (TypedInit *TI = dyn_cast<TypedInit>(Expr)) {
    // Is the expression type known to be (a subclass of) the desired type?
    if (TI->getType()->typeIsConvertibleTo(CheckType))
      return IntInit::get(1);

    if (isa<RecordRecTy>(CheckType)) {
      // If the target type is not a subclass of the expression type, or if
      // the expression has fully resolved to a record, we know that it can't
      // be of the required type.
      if (!CheckType->typeIsConvertibleTo(TI->getType()) || isa<DefInit>(Expr))
        return IntInit::get(0);
    } else {
      // We treat non-record types as not castable.
      return IntInit::get(0);
    }
  }
  return const_cast<IsAOpInit *>(this);
}

Init *IsAOpInit::resolveReferences(Resolver &R) const {
  Init *NewExpr = Expr->resolveReferences(R);
  if (Expr != NewExpr)
    return get(CheckType, NewExpr)->Fold();
  return const_cast<IsAOpInit *>(this);
}

Init *IsAOpInit::getBit(unsigned Bit) const {
  return VarBitInit::get(const_cast<IsAOpInit *>(this), Bit);
}

std::string IsAOpInit::getAsString() const {
  return (Twine("!isa<") + CheckType->getAsString() + ">(" +
          Expr->getAsString() + ")")
      .str();
}

RecTy *TypedInit::getFieldType(StringInit *FieldName) const {
  if (RecordRecTy *RecordType = dyn_cast<RecordRecTy>(getType())) {
    for (Record *Rec : RecordType->getClasses()) {
      if (RecordVal *Field = Rec->getValue(FieldName))
        return Field->getType();
    }
  }
  return nullptr;
}

Init *
TypedInit::convertInitializerTo(RecTy *Ty) const {
  if (getType() == Ty || getType()->typeIsA(Ty))
    return const_cast<TypedInit *>(this);

  if (isa<BitRecTy>(getType()) && isa<BitsRecTy>(Ty) &&
      cast<BitsRecTy>(Ty)->getNumBits() == 1)
    return BitsInit::get({const_cast<TypedInit *>(this)});

  return nullptr;
}

Init *TypedInit::convertInitializerBitRange(ArrayRef<unsigned> Bits) const {
  BitsRecTy *T = dyn_cast<BitsRecTy>(getType());
  if (!T) return nullptr;  // Cannot subscript a non-bits variable.
  unsigned NumBits = T->getNumBits();

  SmallVector<Init *, 16> NewBits;
  NewBits.reserve(Bits.size());
  for (unsigned Bit : Bits) {
    if (Bit >= NumBits)
      return nullptr;

    NewBits.push_back(VarBitInit::get(const_cast<TypedInit *>(this), Bit));
  }
  return BitsInit::get(NewBits);
}

Init *TypedInit::getCastTo(RecTy *Ty) const {
  // Handle the common case quickly
  if (getType() == Ty || getType()->typeIsA(Ty))
    return const_cast<TypedInit *>(this);

  if (Init *Converted = convertInitializerTo(Ty)) {
    assert(!isa<TypedInit>(Converted) ||
           cast<TypedInit>(Converted)->getType()->typeIsA(Ty));
    return Converted;
  }

  if (!getType()->typeIsConvertibleTo(Ty))
    return nullptr;

  return UnOpInit::get(UnOpInit::CAST, const_cast<TypedInit *>(this), Ty)
      ->Fold(nullptr);
}

Init *TypedInit::convertInitListSlice(ArrayRef<unsigned> Elements) const {
  ListRecTy *T = dyn_cast<ListRecTy>(getType());
  if (!T) return nullptr;  // Cannot subscript a non-list variable.

  if (Elements.size() == 1)
    return VarListElementInit::get(const_cast<TypedInit *>(this), Elements[0]);

  SmallVector<Init*, 8> ListInits;
  ListInits.reserve(Elements.size());
  for (unsigned Element : Elements)
    ListInits.push_back(VarListElementInit::get(const_cast<TypedInit *>(this),
                                                Element));
  return ListInit::get(ListInits, T->getElementType());
}


VarInit *VarInit::get(StringRef VN, RecTy *T) {
  Init *Value = StringInit::get(VN);
  return VarInit::get(Value, T);
}

VarInit *VarInit::get(Init *VN, RecTy *T) {
  using Key = std::pair<RecTy *, Init *>;
  static DenseMap<Key, VarInit*> ThePool;

  Key TheKey(std::make_pair(T, VN));

  VarInit *&I = ThePool[TheKey];
  if (!I)
    I = new(Allocator) VarInit(VN, T);
  return I;
}

StringRef VarInit::getName() const {
  StringInit *NameString = cast<StringInit>(getNameInit());
  return NameString->getValue();
}

Init *VarInit::getBit(unsigned Bit) const {
  if (getType() == BitRecTy::get())
    return const_cast<VarInit*>(this);
  return VarBitInit::get(const_cast<VarInit*>(this), Bit);
}

Init *VarInit::resolveReferences(Resolver &R) const {
  if (Init *Val = R.resolve(VarName))
    return Val;
  return const_cast<VarInit *>(this);
}

VarBitInit *VarBitInit::get(TypedInit *T, unsigned B) {
  using Key = std::pair<TypedInit *, unsigned>;
  static DenseMap<Key, VarBitInit*> ThePool;

  Key TheKey(std::make_pair(T, B));

  VarBitInit *&I = ThePool[TheKey];
  if (!I)
    I = new(Allocator) VarBitInit(T, B);
  return I;
}

std::string VarBitInit::getAsString() const {
  return TI->getAsString() + "{" + utostr(Bit) + "}";
}

Init *VarBitInit::resolveReferences(Resolver &R) const {
  Init *I = TI->resolveReferences(R);
  if (TI != I)
    return I->getBit(getBitNum());

  return const_cast<VarBitInit*>(this);
}

VarListElementInit *VarListElementInit::get(TypedInit *T,
                                            unsigned E) {
  using Key = std::pair<TypedInit *, unsigned>;
  static DenseMap<Key, VarListElementInit*> ThePool;

  Key TheKey(std::make_pair(T, E));

  VarListElementInit *&I = ThePool[TheKey];
  if (!I) I = new(Allocator) VarListElementInit(T, E);
  return I;
}

std::string VarListElementInit::getAsString() const {
  return TI->getAsString() + "[" + utostr(Element) + "]";
}

Init *VarListElementInit::resolveReferences(Resolver &R) const {
  Init *NewTI = TI->resolveReferences(R);
  if (ListInit *List = dyn_cast<ListInit>(NewTI)) {
    // Leave out-of-bounds array references as-is. This can happen without
    // being an error, e.g. in the untaken "branch" of an !if expression.
    if (getElementNum() < List->size())
      return List->getElement(getElementNum());
  }
  if (NewTI != TI && isa<TypedInit>(NewTI))
    return VarListElementInit::get(cast<TypedInit>(NewTI), getElementNum());
  return const_cast<VarListElementInit *>(this);
}

Init *VarListElementInit::getBit(unsigned Bit) const {
  if (getType() == BitRecTy::get())
    return const_cast<VarListElementInit*>(this);
  return VarBitInit::get(const_cast<VarListElementInit*>(this), Bit);
}

DefInit::DefInit(Record *D)
    : TypedInit(IK_DefInit, D->getType()), Def(D) {}

DefInit *DefInit::get(Record *R) {
  return R->getDefInit();
}

Init *DefInit::convertInitializerTo(RecTy *Ty) const {
  if (auto *RRT = dyn_cast<RecordRecTy>(Ty))
    if (getType()->typeIsConvertibleTo(RRT))
      return const_cast<DefInit *>(this);
  return nullptr;
}

RecTy *DefInit::getFieldType(StringInit *FieldName) const {
  if (const RecordVal *RV = Def->getValue(FieldName))
    return RV->getType();
  return nullptr;
}

std::string DefInit::getAsString() const {
  return Def->getName();
}

static void ProfileVarDefInit(FoldingSetNodeID &ID,
                              Record *Class,
                              ArrayRef<Init *> Args) {
  ID.AddInteger(Args.size());
  ID.AddPointer(Class);

  for (Init *I : Args)
    ID.AddPointer(I);
}

VarDefInit *VarDefInit::get(Record *Class, ArrayRef<Init *> Args) {
  static FoldingSet<VarDefInit> ThePool;

  FoldingSetNodeID ID;
  ProfileVarDefInit(ID, Class, Args);

  void *IP = nullptr;
  if (VarDefInit *I = ThePool.FindNodeOrInsertPos(ID, IP))
    return I;

  void *Mem = Allocator.Allocate(totalSizeToAlloc<Init *>(Args.size()),
                                 alignof(VarDefInit));
  VarDefInit *I = new(Mem) VarDefInit(Class, Args.size());
  std::uninitialized_copy(Args.begin(), Args.end(),
                          I->getTrailingObjects<Init *>());
  ThePool.InsertNode(I, IP);
  return I;
}

void VarDefInit::Profile(FoldingSetNodeID &ID) const {
  ProfileVarDefInit(ID, Class, args());
}

DefInit *VarDefInit::instantiate() {
  if (!Def) {
    RecordKeeper &Records = Class->getRecords();
    auto NewRecOwner = make_unique<Record>(Records.getNewAnonymousName(),
                                           Class->getLoc(), Records,
                                           /*IsAnonymous=*/true);
    Record *NewRec = NewRecOwner.get();

    // Copy values from class to instance
    for (const RecordVal &Val : Class->getValues())
      NewRec->addValue(Val);

    // Substitute and resolve template arguments
    ArrayRef<Init *> TArgs = Class->getTemplateArgs();
    MapResolver R(NewRec);

    for (unsigned i = 0, e = TArgs.size(); i != e; ++i) {
      if (i < args_size())
        R.set(TArgs[i], getArg(i));
      else
        R.set(TArgs[i], NewRec->getValue(TArgs[i])->getValue());

      NewRec->removeValue(TArgs[i]);
    }

    NewRec->resolveReferences(R);

    // Add superclasses.
    ArrayRef<std::pair<Record *, SMRange>> SCs = Class->getSuperClasses();
    for (const auto &SCPair : SCs)
      NewRec->addSuperClass(SCPair.first, SCPair.second);

    NewRec->addSuperClass(Class,
                          SMRange(Class->getLoc().back(),
                                  Class->getLoc().back()));

    // Resolve internal references and store in record keeper
    NewRec->resolveReferences();
    Records.addDef(std::move(NewRecOwner));

    Def = DefInit::get(NewRec);
  }

  return Def;
}

Init *VarDefInit::resolveReferences(Resolver &R) const {
  TrackUnresolvedResolver UR(&R);
  bool Changed = false;
  SmallVector<Init *, 8> NewArgs;
  NewArgs.reserve(args_size());

  for (Init *Arg : args()) {
    Init *NewArg = Arg->resolveReferences(UR);
    NewArgs.push_back(NewArg);
    Changed |= NewArg != Arg;
  }

  if (Changed) {
    auto New = VarDefInit::get(Class, NewArgs);
    if (!UR.foundUnresolved())
      return New->instantiate();
    return New;
  }
  return const_cast<VarDefInit *>(this);
}

Init *VarDefInit::Fold() const {
  if (Def)
    return Def;

  TrackUnresolvedResolver R;
  for (Init *Arg : args())
    Arg->resolveReferences(R);

  if (!R.foundUnresolved())
    return const_cast<VarDefInit *>(this)->instantiate();
  return const_cast<VarDefInit *>(this);
}

std::string VarDefInit::getAsString() const {
  std::string Result = Class->getNameInitAsString() + "<";
  const char *sep = "";
  for (Init *Arg : args()) {
    Result += sep;
    sep = ", ";
    Result += Arg->getAsString();
  }
  return Result + ">";
}

FieldInit *FieldInit::get(Init *R, StringInit *FN) {
  using Key = std::pair<Init *, StringInit *>;
  static DenseMap<Key, FieldInit*> ThePool;

  Key TheKey(std::make_pair(R, FN));

  FieldInit *&I = ThePool[TheKey];
  if (!I) I = new(Allocator) FieldInit(R, FN);
  return I;
}

Init *FieldInit::getBit(unsigned Bit) const {
  if (getType() == BitRecTy::get())
    return const_cast<FieldInit*>(this);
  return VarBitInit::get(const_cast<FieldInit*>(this), Bit);
}

Init *FieldInit::resolveReferences(Resolver &R) const {
  Init *NewRec = Rec->resolveReferences(R);
  if (NewRec != Rec)
    return FieldInit::get(NewRec, FieldName)->Fold(R.getCurrentRecord());
  return const_cast<FieldInit *>(this);
}

Init *FieldInit::Fold(Record *CurRec) const {
  if (DefInit *DI = dyn_cast<DefInit>(Rec)) {
    Record *Def = DI->getDef();
    if (Def == CurRec)
      PrintFatalError(CurRec->getLoc(),
                      Twine("Attempting to access field '") +
                      FieldName->getAsUnquotedString() + "' of '" +
                      Rec->getAsString() + "' is a forbidden self-reference");
    Init *FieldVal = Def->getValue(FieldName)->getValue();
    if (FieldVal->isComplete())
      return FieldVal;
  }
  return const_cast<FieldInit *>(this);
}

static void ProfileDagInit(FoldingSetNodeID &ID, Init *V, StringInit *VN,
                           ArrayRef<Init *> ArgRange,
                           ArrayRef<StringInit *> NameRange) {
  ID.AddPointer(V);
  ID.AddPointer(VN);

  ArrayRef<Init *>::iterator Arg = ArgRange.begin();
  ArrayRef<StringInit *>::iterator Name = NameRange.begin();
  while (Arg != ArgRange.end()) {
    assert(Name != NameRange.end() && "Arg name underflow!");
    ID.AddPointer(*Arg++);
    ID.AddPointer(*Name++);
  }
  assert(Name == NameRange.end() && "Arg name overflow!");
}

DagInit *
DagInit::get(Init *V, StringInit *VN, ArrayRef<Init *> ArgRange,
             ArrayRef<StringInit *> NameRange) {
  static FoldingSet<DagInit> ThePool;

  FoldingSetNodeID ID;
  ProfileDagInit(ID, V, VN, ArgRange, NameRange);

  void *IP = nullptr;
  if (DagInit *I = ThePool.FindNodeOrInsertPos(ID, IP))
    return I;

  void *Mem = Allocator.Allocate(totalSizeToAlloc<Init *, StringInit *>(ArgRange.size(), NameRange.size()), alignof(BitsInit));
  DagInit *I = new(Mem) DagInit(V, VN, ArgRange.size(), NameRange.size());
  std::uninitialized_copy(ArgRange.begin(), ArgRange.end(),
                          I->getTrailingObjects<Init *>());
  std::uninitialized_copy(NameRange.begin(), NameRange.end(),
                          I->getTrailingObjects<StringInit *>());
  ThePool.InsertNode(I, IP);
  return I;
}

DagInit *
DagInit::get(Init *V, StringInit *VN,
             ArrayRef<std::pair<Init*, StringInit*>> args) {
  SmallVector<Init *, 8> Args;
  SmallVector<StringInit *, 8> Names;

  for (const auto &Arg : args) {
    Args.push_back(Arg.first);
    Names.push_back(Arg.second);
  }

  return DagInit::get(V, VN, Args, Names);
}

void DagInit::Profile(FoldingSetNodeID &ID) const {
  ProfileDagInit(ID, Val, ValName, makeArrayRef(getTrailingObjects<Init *>(), NumArgs), makeArrayRef(getTrailingObjects<StringInit *>(), NumArgNames));
}

Init *DagInit::resolveReferences(Resolver &R) const {
  SmallVector<Init*, 8> NewArgs;
  NewArgs.reserve(arg_size());
  bool ArgsChanged = false;
  for (const Init *Arg : getArgs()) {
    Init *NewArg = Arg->resolveReferences(R);
    NewArgs.push_back(NewArg);
    ArgsChanged |= NewArg != Arg;
  }

  Init *Op = Val->resolveReferences(R);
  if (Op != Val || ArgsChanged)
    return DagInit::get(Op, ValName, NewArgs, getArgNames());

  return const_cast<DagInit *>(this);
}

bool DagInit::isConcrete() const {
  if (!Val->isConcrete())
    return false;
  for (const Init *Elt : getArgs()) {
    if (!Elt->isConcrete())
      return false;
  }
  return true;
}

std::string DagInit::getAsString() const {
  std::string Result = "(" + Val->getAsString();
  if (ValName)
    Result += ":" + ValName->getAsUnquotedString();
  if (!arg_empty()) {
    Result += " " + getArg(0)->getAsString();
    if (getArgName(0)) Result += ":$" + getArgName(0)->getAsUnquotedString();
    for (unsigned i = 1, e = getNumArgs(); i != e; ++i) {
      Result += ", " + getArg(i)->getAsString();
      if (getArgName(i)) Result += ":$" + getArgName(i)->getAsUnquotedString();
    }
  }
  return Result + ")";
}

//===----------------------------------------------------------------------===//
//    Other implementations
//===----------------------------------------------------------------------===//

RecordVal::RecordVal(Init *N, RecTy *T, bool P)
  : Name(N), TyAndPrefix(T, P) {
  setValue(UnsetInit::get());
  assert(Value && "Cannot create unset value for current type!");
}

StringRef RecordVal::getName() const {
  return cast<StringInit>(getNameInit())->getValue();
}

bool RecordVal::setValue(Init *V) {
  if (V) {
    Value = V->getCastTo(getType());
    if (Value) {
      assert(!isa<TypedInit>(Value) ||
             cast<TypedInit>(Value)->getType()->typeIsA(getType()));
      if (BitsRecTy *BTy = dyn_cast<BitsRecTy>(getType())) {
        if (!isa<BitsInit>(Value)) {
          SmallVector<Init *, 64> Bits;
          Bits.reserve(BTy->getNumBits());
          for (unsigned i = 0, e = BTy->getNumBits(); i < e; ++i)
            Bits.push_back(Value->getBit(i));
          Value = BitsInit::get(Bits);
        }
      }
    }
    return Value == nullptr;
  }
  Value = nullptr;
  return false;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RecordVal::dump() const { errs() << *this; }
#endif

void RecordVal::print(raw_ostream &OS, bool PrintSem) const {
  if (getPrefix()) OS << "field ";
  OS << *getType() << " " << getNameInitAsString();

  if (getValue())
    OS << " = " << *getValue();

  if (PrintSem) OS << ";\n";
}

unsigned Record::LastID = 0;

void Record::checkName() {
  // Ensure the record name has string type.
  const TypedInit *TypedName = cast<const TypedInit>(Name);
  if (!isa<StringRecTy>(TypedName->getType()))
    PrintFatalError(getLoc(), Twine("Record name '") + Name->getAsString() +
                                  "' is not a string!");
}

RecordRecTy *Record::getType() {
  SmallVector<Record *, 4> DirectSCs;
  getDirectSuperClasses(DirectSCs);
  return RecordRecTy::get(DirectSCs);
}

DefInit *Record::getDefInit() {
  if (!TheInit)
    TheInit = new(Allocator) DefInit(this);
  return TheInit;
}

void Record::setName(Init *NewName) {
  Name = NewName;
  checkName();
  // DO NOT resolve record values to the name at this point because
  // there might be default values for arguments of this def.  Those
  // arguments might not have been resolved yet so we don't want to
  // prematurely assume values for those arguments were not passed to
  // this def.
  //
  // Nonetheless, it may be that some of this Record's values
  // reference the record name.  Indeed, the reason for having the
  // record name be an Init is to provide this flexibility.  The extra
  // resolve steps after completely instantiating defs takes care of
  // this.  See TGParser::ParseDef and TGParser::ParseDefm.
}

void Record::getDirectSuperClasses(SmallVectorImpl<Record *> &Classes) const {
  ArrayRef<std::pair<Record *, SMRange>> SCs = getSuperClasses();
  while (!SCs.empty()) {
    // Superclasses are in reverse preorder, so 'back' is a direct superclass,
    // and its transitive superclasses are directly preceding it.
    Record *SC = SCs.back().first;
    SCs = SCs.drop_back(1 + SC->getSuperClasses().size());
    Classes.push_back(SC);
  }
}

void Record::resolveReferences(Resolver &R, const RecordVal *SkipVal) {
  for (RecordVal &Value : Values) {
    if (SkipVal == &Value) // Skip resolve the same field as the given one
      continue;
    if (Init *V = Value.getValue()) {
      Init *VR = V->resolveReferences(R);
      if (Value.setValue(VR)) {
        std::string Type;
        if (TypedInit *VRT = dyn_cast<TypedInit>(VR))
          Type =
              (Twine("of type '") + VRT->getType()->getAsString() + "' ").str();
        PrintFatalError(getLoc(), Twine("Invalid value ") + Type +
                                      "is found when setting '" +
                                      Value.getNameInitAsString() +
                                      "' of type '" +
                                      Value.getType()->getAsString() +
                                      "' after resolving references: " +
                                      VR->getAsUnquotedString() + "\n");
      }
    }
  }
  Init *OldName = getNameInit();
  Init *NewName = Name->resolveReferences(R);
  if (NewName != OldName) {
    // Re-register with RecordKeeper.
    setName(NewName);
  }
}

void Record::resolveReferences() {
  RecordResolver R(*this);
  R.setFinal(true);
  resolveReferences(R);
}

void Record::resolveReferencesTo(const RecordVal *RV) {
  RecordValResolver R(*this, RV);
  resolveReferences(R, RV);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void Record::dump() const { errs() << *this; }
#endif

raw_ostream &llvm::operator<<(raw_ostream &OS, const Record &R) {
  OS << R.getNameInitAsString();

  ArrayRef<Init *> TArgs = R.getTemplateArgs();
  if (!TArgs.empty()) {
    OS << "<";
    bool NeedComma = false;
    for (const Init *TA : TArgs) {
      if (NeedComma) OS << ", ";
      NeedComma = true;
      const RecordVal *RV = R.getValue(TA);
      assert(RV && "Template argument record not found??");
      RV->print(OS, false);
    }
    OS << ">";
  }

  OS << " {";
  ArrayRef<std::pair<Record *, SMRange>> SC = R.getSuperClasses();
  if (!SC.empty()) {
    OS << "\t//";
    for (const auto &SuperPair : SC)
      OS << " " << SuperPair.first->getNameInitAsString();
  }
  OS << "\n";

  for (const RecordVal &Val : R.getValues())
    if (Val.getPrefix() && !R.isTemplateArg(Val.getNameInit()))
      OS << Val;
  for (const RecordVal &Val : R.getValues())
    if (!Val.getPrefix() && !R.isTemplateArg(Val.getNameInit()))
      OS << Val;

  return OS << "}\n";
}

Init *Record::getValueInit(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");
  return R->getValue();
}

StringRef Record::getValueAsString(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (StringInit *SI = dyn_cast<StringInit>(R->getValue()))
    return SI->getValue();
  if (CodeInit *CI = dyn_cast<CodeInit>(R->getValue()))
    return CI->getValue();

  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
    FieldName + "' does not have a string initializer!");
}

BitsInit *Record::getValueAsBitsInit(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (BitsInit *BI = dyn_cast<BitsInit>(R->getValue()))
    return BI;
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
    FieldName + "' does not have a BitsInit initializer!");
}

ListInit *Record::getValueAsListInit(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (ListInit *LI = dyn_cast<ListInit>(R->getValue()))
    return LI;
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
    FieldName + "' does not have a list initializer!");
}

std::vector<Record*>
Record::getValueAsListOfDefs(StringRef FieldName) const {
  ListInit *List = getValueAsListInit(FieldName);
  std::vector<Record*> Defs;
  for (Init *I : List->getValues()) {
    if (DefInit *DI = dyn_cast<DefInit>(I))
      Defs.push_back(DI->getDef());
    else
      PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
        FieldName + "' list is not entirely DefInit!");
  }
  return Defs;
}

int64_t Record::getValueAsInt(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (IntInit *II = dyn_cast<IntInit>(R->getValue()))
    return II->getValue();
  PrintFatalError(getLoc(), Twine("Record `") + getName() + "', field `" +
                                FieldName +
                                "' does not have an int initializer: " +
                                R->getValue()->getAsString());
}

std::vector<int64_t>
Record::getValueAsListOfInts(StringRef FieldName) const {
  ListInit *List = getValueAsListInit(FieldName);
  std::vector<int64_t> Ints;
  for (Init *I : List->getValues()) {
    if (IntInit *II = dyn_cast<IntInit>(I))
      Ints.push_back(II->getValue());
    else
      PrintFatalError(getLoc(),
                      Twine("Record `") + getName() + "', field `" + FieldName +
                          "' does not have a list of ints initializer: " +
                          I->getAsString());
  }
  return Ints;
}

std::vector<StringRef>
Record::getValueAsListOfStrings(StringRef FieldName) const {
  ListInit *List = getValueAsListInit(FieldName);
  std::vector<StringRef> Strings;
  for (Init *I : List->getValues()) {
    if (StringInit *SI = dyn_cast<StringInit>(I))
      Strings.push_back(SI->getValue());
    else
      PrintFatalError(getLoc(),
                      Twine("Record `") + getName() + "', field `" + FieldName +
                          "' does not have a list of strings initializer: " +
                          I->getAsString());
  }
  return Strings;
}

Record *Record::getValueAsDef(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (DefInit *DI = dyn_cast<DefInit>(R->getValue()))
    return DI->getDef();
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
    FieldName + "' does not have a def initializer!");
}

bool Record::getValueAsBit(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (BitInit *BI = dyn_cast<BitInit>(R->getValue()))
    return BI->getValue();
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
    FieldName + "' does not have a bit initializer!");
}

bool Record::getValueAsBitOrUnset(StringRef FieldName, bool &Unset) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName.str() + "'!\n");

  if (isa<UnsetInit>(R->getValue())) {
    Unset = true;
    return false;
  }
  Unset = false;
  if (BitInit *BI = dyn_cast<BitInit>(R->getValue()))
    return BI->getValue();
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
    FieldName + "' does not have a bit initializer!");
}

DagInit *Record::getValueAsDag(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (DagInit *DI = dyn_cast<DagInit>(R->getValue()))
    return DI;
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
    FieldName + "' does not have a dag initializer!");
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RecordKeeper::dump() const { errs() << *this; }
#endif

raw_ostream &llvm::operator<<(raw_ostream &OS, const RecordKeeper &RK) {
  OS << "------------- Classes -----------------\n";
  for (const auto &C : RK.getClasses())
    OS << "class " << *C.second;

  OS << "------------- Defs -----------------\n";
  for (const auto &D : RK.getDefs())
    OS << "def " << *D.second;
  return OS;
}

/// GetNewAnonymousName - Generate a unique anonymous name that can be used as
/// an identifier.
Init *RecordKeeper::getNewAnonymousName() {
  return StringInit::get("anonymous_" + utostr(AnonCounter++));
}

std::vector<Record *>
RecordKeeper::getAllDerivedDefinitions(StringRef ClassName) const {
  Record *Class = getClass(ClassName);
  if (!Class)
    PrintFatalError("ERROR: Couldn't find the `" + ClassName + "' class!\n");

  std::vector<Record*> Defs;
  for (const auto &D : getDefs())
    if (D.second->isSubClassOf(Class))
      Defs.push_back(D.second.get());

  return Defs;
}

Init *MapResolver::resolve(Init *VarName) {
  auto It = Map.find(VarName);
  if (It == Map.end())
    return nullptr;

  Init *I = It->second.V;

  if (!It->second.Resolved && Map.size() > 1) {
    // Resolve mutual references among the mapped variables, but prevent
    // infinite recursion.
    Map.erase(It);
    I = I->resolveReferences(*this);
    Map[VarName] = {I, true};
  }

  return I;
}

Init *RecordResolver::resolve(Init *VarName) {
  Init *Val = Cache.lookup(VarName);
  if (Val)
    return Val;

  for (Init *S : Stack) {
    if (S == VarName)
      return nullptr; // prevent infinite recursion
  }

  if (RecordVal *RV = getCurrentRecord()->getValue(VarName)) {
    if (!isa<UnsetInit>(RV->getValue())) {
      Val = RV->getValue();
      Stack.push_back(VarName);
      Val = Val->resolveReferences(*this);
      Stack.pop_back();
    }
  }

  Cache[VarName] = Val;
  return Val;
}

Init *TrackUnresolvedResolver::resolve(Init *VarName) {
  Init *I = nullptr;

  if (R) {
    I = R->resolve(VarName);
    if (I && !FoundUnresolved) {
      // Do not recurse into the resolved initializer, as that would change
      // the behavior of the resolver we're delegating, but do check to see
      // if there are unresolved variables remaining.
      TrackUnresolvedResolver Sub;
      I->resolveReferences(Sub);
      FoundUnresolved |= Sub.FoundUnresolved;
    }
  }

  if (!I)
    FoundUnresolved = true;
  return I;
}

Init *HasReferenceResolver::resolve(Init *VarName)
{
  if (VarName == VarNameToTrack)
    Found = true;
  return nullptr;
}
