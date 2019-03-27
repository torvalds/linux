//===- Function.cpp - Implement the Global object classes -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Function class for the IR library.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Function.h"
#include "SymbolTableListTraitsImpl.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

using namespace llvm;
using ProfileCount = Function::ProfileCount;

// Explicit instantiations of SymbolTableListTraits since some of the methods
// are not in the public header file...
template class llvm::SymbolTableListTraits<BasicBlock>;

//===----------------------------------------------------------------------===//
// Argument Implementation
//===----------------------------------------------------------------------===//

Argument::Argument(Type *Ty, const Twine &Name, Function *Par, unsigned ArgNo)
    : Value(Ty, Value::ArgumentVal), Parent(Par), ArgNo(ArgNo) {
  setName(Name);
}

void Argument::setParent(Function *parent) {
  Parent = parent;
}

bool Argument::hasNonNullAttr() const {
  if (!getType()->isPointerTy()) return false;
  if (getParent()->hasParamAttribute(getArgNo(), Attribute::NonNull))
    return true;
  else if (getDereferenceableBytes() > 0 &&
           !NullPointerIsDefined(getParent(),
                                 getType()->getPointerAddressSpace()))
    return true;
  return false;
}

bool Argument::hasByValAttr() const {
  if (!getType()->isPointerTy()) return false;
  return hasAttribute(Attribute::ByVal);
}

bool Argument::hasSwiftSelfAttr() const {
  return getParent()->hasParamAttribute(getArgNo(), Attribute::SwiftSelf);
}

bool Argument::hasSwiftErrorAttr() const {
  return getParent()->hasParamAttribute(getArgNo(), Attribute::SwiftError);
}

bool Argument::hasInAllocaAttr() const {
  if (!getType()->isPointerTy()) return false;
  return hasAttribute(Attribute::InAlloca);
}

bool Argument::hasByValOrInAllocaAttr() const {
  if (!getType()->isPointerTy()) return false;
  AttributeList Attrs = getParent()->getAttributes();
  return Attrs.hasParamAttribute(getArgNo(), Attribute::ByVal) ||
         Attrs.hasParamAttribute(getArgNo(), Attribute::InAlloca);
}

unsigned Argument::getParamAlignment() const {
  assert(getType()->isPointerTy() && "Only pointers have alignments");
  return getParent()->getParamAlignment(getArgNo());
}

uint64_t Argument::getDereferenceableBytes() const {
  assert(getType()->isPointerTy() &&
         "Only pointers have dereferenceable bytes");
  return getParent()->getParamDereferenceableBytes(getArgNo());
}

uint64_t Argument::getDereferenceableOrNullBytes() const {
  assert(getType()->isPointerTy() &&
         "Only pointers have dereferenceable bytes");
  return getParent()->getParamDereferenceableOrNullBytes(getArgNo());
}

bool Argument::hasNestAttr() const {
  if (!getType()->isPointerTy()) return false;
  return hasAttribute(Attribute::Nest);
}

bool Argument::hasNoAliasAttr() const {
  if (!getType()->isPointerTy()) return false;
  return hasAttribute(Attribute::NoAlias);
}

bool Argument::hasNoCaptureAttr() const {
  if (!getType()->isPointerTy()) return false;
  return hasAttribute(Attribute::NoCapture);
}

bool Argument::hasStructRetAttr() const {
  if (!getType()->isPointerTy()) return false;
  return hasAttribute(Attribute::StructRet);
}

bool Argument::hasReturnedAttr() const {
  return hasAttribute(Attribute::Returned);
}

bool Argument::hasZExtAttr() const {
  return hasAttribute(Attribute::ZExt);
}

bool Argument::hasSExtAttr() const {
  return hasAttribute(Attribute::SExt);
}

bool Argument::onlyReadsMemory() const {
  AttributeList Attrs = getParent()->getAttributes();
  return Attrs.hasParamAttribute(getArgNo(), Attribute::ReadOnly) ||
         Attrs.hasParamAttribute(getArgNo(), Attribute::ReadNone);
}

void Argument::addAttrs(AttrBuilder &B) {
  AttributeList AL = getParent()->getAttributes();
  AL = AL.addParamAttributes(Parent->getContext(), getArgNo(), B);
  getParent()->setAttributes(AL);
}

void Argument::addAttr(Attribute::AttrKind Kind) {
  getParent()->addParamAttr(getArgNo(), Kind);
}

void Argument::addAttr(Attribute Attr) {
  getParent()->addParamAttr(getArgNo(), Attr);
}

void Argument::removeAttr(Attribute::AttrKind Kind) {
  getParent()->removeParamAttr(getArgNo(), Kind);
}

bool Argument::hasAttribute(Attribute::AttrKind Kind) const {
  return getParent()->hasParamAttribute(getArgNo(), Kind);
}

//===----------------------------------------------------------------------===//
// Helper Methods in Function
//===----------------------------------------------------------------------===//

LLVMContext &Function::getContext() const {
  return getType()->getContext();
}

unsigned Function::getInstructionCount() const {
  unsigned NumInstrs = 0;
  for (const BasicBlock &BB : BasicBlocks)
    NumInstrs += std::distance(BB.instructionsWithoutDebug().begin(),
                               BB.instructionsWithoutDebug().end());
  return NumInstrs;
}

Function *Function::Create(FunctionType *Ty, LinkageTypes Linkage,
                           const Twine &N, Module &M) {
  return Create(Ty, Linkage, M.getDataLayout().getProgramAddressSpace(), N, &M);
}

void Function::removeFromParent() {
  getParent()->getFunctionList().remove(getIterator());
}

void Function::eraseFromParent() {
  getParent()->getFunctionList().erase(getIterator());
}

//===----------------------------------------------------------------------===//
// Function Implementation
//===----------------------------------------------------------------------===//

static unsigned computeAddrSpace(unsigned AddrSpace, Module *M) {
  // If AS == -1 and we are passed a valid module pointer we place the function
  // in the program address space. Otherwise we default to AS0.
  if (AddrSpace == static_cast<unsigned>(-1))
    return M ? M->getDataLayout().getProgramAddressSpace() : 0;
  return AddrSpace;
}

Function::Function(FunctionType *Ty, LinkageTypes Linkage, unsigned AddrSpace,
                   const Twine &name, Module *ParentModule)
    : GlobalObject(Ty, Value::FunctionVal,
                   OperandTraits<Function>::op_begin(this), 0, Linkage, name,
                   computeAddrSpace(AddrSpace, ParentModule)),
      NumArgs(Ty->getNumParams()) {
  assert(FunctionType::isValidReturnType(getReturnType()) &&
         "invalid return type");
  setGlobalObjectSubClassData(0);

  // We only need a symbol table for a function if the context keeps value names
  if (!getContext().shouldDiscardValueNames())
    SymTab = make_unique<ValueSymbolTable>();

  // If the function has arguments, mark them as lazily built.
  if (Ty->getNumParams())
    setValueSubclassData(1);   // Set the "has lazy arguments" bit.

  if (ParentModule)
    ParentModule->getFunctionList().push_back(this);

  HasLLVMReservedName = getName().startswith("llvm.");
  // Ensure intrinsics have the right parameter attributes.
  // Note, the IntID field will have been set in Value::setName if this function
  // name is a valid intrinsic ID.
  if (IntID)
    setAttributes(Intrinsic::getAttributes(getContext(), IntID));
}

Function::~Function() {
  dropAllReferences();    // After this it is safe to delete instructions.

  // Delete all of the method arguments and unlink from symbol table...
  if (Arguments)
    clearArguments();

  // Remove the function from the on-the-side GC table.
  clearGC();
}

void Function::BuildLazyArguments() const {
  // Create the arguments vector, all arguments start out unnamed.
  auto *FT = getFunctionType();
  if (NumArgs > 0) {
    Arguments = std::allocator<Argument>().allocate(NumArgs);
    for (unsigned i = 0, e = NumArgs; i != e; ++i) {
      Type *ArgTy = FT->getParamType(i);
      assert(!ArgTy->isVoidTy() && "Cannot have void typed arguments!");
      new (Arguments + i) Argument(ArgTy, "", const_cast<Function *>(this), i);
    }
  }

  // Clear the lazy arguments bit.
  unsigned SDC = getSubclassDataFromValue();
  const_cast<Function*>(this)->setValueSubclassData(SDC &= ~(1<<0));
  assert(!hasLazyArguments());
}

static MutableArrayRef<Argument> makeArgArray(Argument *Args, size_t Count) {
  return MutableArrayRef<Argument>(Args, Count);
}

void Function::clearArguments() {
  for (Argument &A : makeArgArray(Arguments, NumArgs)) {
    A.setName("");
    A.~Argument();
  }
  std::allocator<Argument>().deallocate(Arguments, NumArgs);
  Arguments = nullptr;
}

void Function::stealArgumentListFrom(Function &Src) {
  assert(isDeclaration() && "Expected no references to current arguments");

  // Drop the current arguments, if any, and set the lazy argument bit.
  if (!hasLazyArguments()) {
    assert(llvm::all_of(makeArgArray(Arguments, NumArgs),
                        [](const Argument &A) { return A.use_empty(); }) &&
           "Expected arguments to be unused in declaration");
    clearArguments();
    setValueSubclassData(getSubclassDataFromValue() | (1 << 0));
  }

  // Nothing to steal if Src has lazy arguments.
  if (Src.hasLazyArguments())
    return;

  // Steal arguments from Src, and fix the lazy argument bits.
  assert(arg_size() == Src.arg_size());
  Arguments = Src.Arguments;
  Src.Arguments = nullptr;
  for (Argument &A : makeArgArray(Arguments, NumArgs)) {
    // FIXME: This does the work of transferNodesFromList inefficiently.
    SmallString<128> Name;
    if (A.hasName())
      Name = A.getName();
    if (!Name.empty())
      A.setName("");
    A.setParent(this);
    if (!Name.empty())
      A.setName(Name);
  }

  setValueSubclassData(getSubclassDataFromValue() & ~(1 << 0));
  assert(!hasLazyArguments());
  Src.setValueSubclassData(Src.getSubclassDataFromValue() | (1 << 0));
}

// dropAllReferences() - This function causes all the subinstructions to "let
// go" of all references that they are maintaining.  This allows one to
// 'delete' a whole class at a time, even though there may be circular
// references... first all references are dropped, and all use counts go to
// zero.  Then everything is deleted for real.  Note that no operations are
// valid on an object that has "dropped all references", except operator
// delete.
//
void Function::dropAllReferences() {
  setIsMaterializable(false);

  for (BasicBlock &BB : *this)
    BB.dropAllReferences();

  // Delete all basic blocks. They are now unused, except possibly by
  // blockaddresses, but BasicBlock's destructor takes care of those.
  while (!BasicBlocks.empty())
    BasicBlocks.begin()->eraseFromParent();

  // Drop uses of any optional data (real or placeholder).
  if (getNumOperands()) {
    User::dropAllReferences();
    setNumHungOffUseOperands(0);
    setValueSubclassData(getSubclassDataFromValue() & ~0xe);
  }

  // Metadata is stored in a side-table.
  clearMetadata();
}

void Function::addAttribute(unsigned i, Attribute::AttrKind Kind) {
  AttributeList PAL = getAttributes();
  PAL = PAL.addAttribute(getContext(), i, Kind);
  setAttributes(PAL);
}

void Function::addAttribute(unsigned i, Attribute Attr) {
  AttributeList PAL = getAttributes();
  PAL = PAL.addAttribute(getContext(), i, Attr);
  setAttributes(PAL);
}

void Function::addAttributes(unsigned i, const AttrBuilder &Attrs) {
  AttributeList PAL = getAttributes();
  PAL = PAL.addAttributes(getContext(), i, Attrs);
  setAttributes(PAL);
}

void Function::addParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) {
  AttributeList PAL = getAttributes();
  PAL = PAL.addParamAttribute(getContext(), ArgNo, Kind);
  setAttributes(PAL);
}

void Function::addParamAttr(unsigned ArgNo, Attribute Attr) {
  AttributeList PAL = getAttributes();
  PAL = PAL.addParamAttribute(getContext(), ArgNo, Attr);
  setAttributes(PAL);
}

void Function::addParamAttrs(unsigned ArgNo, const AttrBuilder &Attrs) {
  AttributeList PAL = getAttributes();
  PAL = PAL.addParamAttributes(getContext(), ArgNo, Attrs);
  setAttributes(PAL);
}

void Function::removeAttribute(unsigned i, Attribute::AttrKind Kind) {
  AttributeList PAL = getAttributes();
  PAL = PAL.removeAttribute(getContext(), i, Kind);
  setAttributes(PAL);
}

void Function::removeAttribute(unsigned i, StringRef Kind) {
  AttributeList PAL = getAttributes();
  PAL = PAL.removeAttribute(getContext(), i, Kind);
  setAttributes(PAL);
}

void Function::removeAttributes(unsigned i, const AttrBuilder &Attrs) {
  AttributeList PAL = getAttributes();
  PAL = PAL.removeAttributes(getContext(), i, Attrs);
  setAttributes(PAL);
}

void Function::removeParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) {
  AttributeList PAL = getAttributes();
  PAL = PAL.removeParamAttribute(getContext(), ArgNo, Kind);
  setAttributes(PAL);
}

void Function::removeParamAttr(unsigned ArgNo, StringRef Kind) {
  AttributeList PAL = getAttributes();
  PAL = PAL.removeParamAttribute(getContext(), ArgNo, Kind);
  setAttributes(PAL);
}

void Function::removeParamAttrs(unsigned ArgNo, const AttrBuilder &Attrs) {
  AttributeList PAL = getAttributes();
  PAL = PAL.removeParamAttributes(getContext(), ArgNo, Attrs);
  setAttributes(PAL);
}

void Function::addDereferenceableAttr(unsigned i, uint64_t Bytes) {
  AttributeList PAL = getAttributes();
  PAL = PAL.addDereferenceableAttr(getContext(), i, Bytes);
  setAttributes(PAL);
}

void Function::addDereferenceableParamAttr(unsigned ArgNo, uint64_t Bytes) {
  AttributeList PAL = getAttributes();
  PAL = PAL.addDereferenceableParamAttr(getContext(), ArgNo, Bytes);
  setAttributes(PAL);
}

void Function::addDereferenceableOrNullAttr(unsigned i, uint64_t Bytes) {
  AttributeList PAL = getAttributes();
  PAL = PAL.addDereferenceableOrNullAttr(getContext(), i, Bytes);
  setAttributes(PAL);
}

void Function::addDereferenceableOrNullParamAttr(unsigned ArgNo,
                                                 uint64_t Bytes) {
  AttributeList PAL = getAttributes();
  PAL = PAL.addDereferenceableOrNullParamAttr(getContext(), ArgNo, Bytes);
  setAttributes(PAL);
}

const std::string &Function::getGC() const {
  assert(hasGC() && "Function has no collector");
  return getContext().getGC(*this);
}

void Function::setGC(std::string Str) {
  setValueSubclassDataBit(14, !Str.empty());
  getContext().setGC(*this, std::move(Str));
}

void Function::clearGC() {
  if (!hasGC())
    return;
  getContext().deleteGC(*this);
  setValueSubclassDataBit(14, false);
}

/// Copy all additional attributes (those not needed to create a Function) from
/// the Function Src to this one.
void Function::copyAttributesFrom(const Function *Src) {
  GlobalObject::copyAttributesFrom(Src);
  setCallingConv(Src->getCallingConv());
  setAttributes(Src->getAttributes());
  if (Src->hasGC())
    setGC(Src->getGC());
  else
    clearGC();
  if (Src->hasPersonalityFn())
    setPersonalityFn(Src->getPersonalityFn());
  if (Src->hasPrefixData())
    setPrefixData(Src->getPrefixData());
  if (Src->hasPrologueData())
    setPrologueData(Src->getPrologueData());
}

/// Table of string intrinsic names indexed by enum value.
static const char * const IntrinsicNameTable[] = {
  "not_intrinsic",
#define GET_INTRINSIC_NAME_TABLE
#include "llvm/IR/IntrinsicImpl.inc"
#undef GET_INTRINSIC_NAME_TABLE
};

/// Table of per-target intrinsic name tables.
#define GET_INTRINSIC_TARGET_DATA
#include "llvm/IR/IntrinsicImpl.inc"
#undef GET_INTRINSIC_TARGET_DATA

/// Find the segment of \c IntrinsicNameTable for intrinsics with the same
/// target as \c Name, or the generic table if \c Name is not target specific.
///
/// Returns the relevant slice of \c IntrinsicNameTable
static ArrayRef<const char *> findTargetSubtable(StringRef Name) {
  assert(Name.startswith("llvm."));

  ArrayRef<IntrinsicTargetInfo> Targets(TargetInfos);
  // Drop "llvm." and take the first dotted component. That will be the target
  // if this is target specific.
  StringRef Target = Name.drop_front(5).split('.').first;
  auto It = std::lower_bound(Targets.begin(), Targets.end(), Target,
                             [](const IntrinsicTargetInfo &TI,
                                StringRef Target) { return TI.Name < Target; });
  // We've either found the target or just fall back to the generic set, which
  // is always first.
  const auto &TI = It != Targets.end() && It->Name == Target ? *It : Targets[0];
  return makeArrayRef(&IntrinsicNameTable[1] + TI.Offset, TI.Count);
}

/// This does the actual lookup of an intrinsic ID which
/// matches the given function name.
Intrinsic::ID Function::lookupIntrinsicID(StringRef Name) {
  ArrayRef<const char *> NameTable = findTargetSubtable(Name);
  int Idx = Intrinsic::lookupLLVMIntrinsicByName(NameTable, Name);
  if (Idx == -1)
    return Intrinsic::not_intrinsic;

  // Intrinsic IDs correspond to the location in IntrinsicNameTable, but we have
  // an index into a sub-table.
  int Adjust = NameTable.data() - IntrinsicNameTable;
  Intrinsic::ID ID = static_cast<Intrinsic::ID>(Idx + Adjust);

  // If the intrinsic is not overloaded, require an exact match. If it is
  // overloaded, require either exact or prefix match.
  const auto MatchSize = strlen(NameTable[Idx]);
  assert(Name.size() >= MatchSize && "Expected either exact or prefix match");
  bool IsExactMatch = Name.size() == MatchSize;
  return IsExactMatch || isOverloaded(ID) ? ID : Intrinsic::not_intrinsic;
}

void Function::recalculateIntrinsicID() {
  StringRef Name = getName();
  if (!Name.startswith("llvm.")) {
    HasLLVMReservedName = false;
    IntID = Intrinsic::not_intrinsic;
    return;
  }
  HasLLVMReservedName = true;
  IntID = lookupIntrinsicID(Name);
}

/// Returns a stable mangling for the type specified for use in the name
/// mangling scheme used by 'any' types in intrinsic signatures.  The mangling
/// of named types is simply their name.  Manglings for unnamed types consist
/// of a prefix ('p' for pointers, 'a' for arrays, 'f_' for functions)
/// combined with the mangling of their component types.  A vararg function
/// type will have a suffix of 'vararg'.  Since function types can contain
/// other function types, we close a function type mangling with suffix 'f'
/// which can't be confused with it's prefix.  This ensures we don't have
/// collisions between two unrelated function types. Otherwise, you might
/// parse ffXX as f(fXX) or f(fX)X.  (X is a placeholder for any other type.)
///
static std::string getMangledTypeStr(Type* Ty) {
  std::string Result;
  if (PointerType* PTyp = dyn_cast<PointerType>(Ty)) {
    Result += "p" + utostr(PTyp->getAddressSpace()) +
      getMangledTypeStr(PTyp->getElementType());
  } else if (ArrayType* ATyp = dyn_cast<ArrayType>(Ty)) {
    Result += "a" + utostr(ATyp->getNumElements()) +
      getMangledTypeStr(ATyp->getElementType());
  } else if (StructType *STyp = dyn_cast<StructType>(Ty)) {
    if (!STyp->isLiteral()) {
      Result += "s_";
      Result += STyp->getName();
    } else {
      Result += "sl_";
      for (auto Elem : STyp->elements())
        Result += getMangledTypeStr(Elem);
    }
    // Ensure nested structs are distinguishable.
    Result += "s";
  } else if (FunctionType *FT = dyn_cast<FunctionType>(Ty)) {
    Result += "f_" + getMangledTypeStr(FT->getReturnType());
    for (size_t i = 0; i < FT->getNumParams(); i++)
      Result += getMangledTypeStr(FT->getParamType(i));
    if (FT->isVarArg())
      Result += "vararg";
    // Ensure nested function types are distinguishable.
    Result += "f";
  } else if (isa<VectorType>(Ty)) {
    Result += "v" + utostr(Ty->getVectorNumElements()) +
      getMangledTypeStr(Ty->getVectorElementType());
  } else if (Ty) {
    switch (Ty->getTypeID()) {
    default: llvm_unreachable("Unhandled type");
    case Type::VoidTyID:      Result += "isVoid";   break;
    case Type::MetadataTyID:  Result += "Metadata"; break;
    case Type::HalfTyID:      Result += "f16";      break;
    case Type::FloatTyID:     Result += "f32";      break;
    case Type::DoubleTyID:    Result += "f64";      break;
    case Type::X86_FP80TyID:  Result += "f80";      break;
    case Type::FP128TyID:     Result += "f128";     break;
    case Type::PPC_FP128TyID: Result += "ppcf128";  break;
    case Type::X86_MMXTyID:   Result += "x86mmx";   break;
    case Type::IntegerTyID:
      Result += "i" + utostr(cast<IntegerType>(Ty)->getBitWidth());
      break;
    }
  }
  return Result;
}

StringRef Intrinsic::getName(ID id) {
  assert(id < num_intrinsics && "Invalid intrinsic ID!");
  assert(!isOverloaded(id) &&
         "This version of getName does not support overloading");
  return IntrinsicNameTable[id];
}

std::string Intrinsic::getName(ID id, ArrayRef<Type*> Tys) {
  assert(id < num_intrinsics && "Invalid intrinsic ID!");
  std::string Result(IntrinsicNameTable[id]);
  for (Type *Ty : Tys) {
    Result += "." + getMangledTypeStr(Ty);
  }
  return Result;
}

/// IIT_Info - These are enumerators that describe the entries returned by the
/// getIntrinsicInfoTableEntries function.
///
/// NOTE: This must be kept in synch with the copy in TblGen/IntrinsicEmitter!
enum IIT_Info {
  // Common values should be encoded with 0-15.
  IIT_Done = 0,
  IIT_I1   = 1,
  IIT_I8   = 2,
  IIT_I16  = 3,
  IIT_I32  = 4,
  IIT_I64  = 5,
  IIT_F16  = 6,
  IIT_F32  = 7,
  IIT_F64  = 8,
  IIT_V2   = 9,
  IIT_V4   = 10,
  IIT_V8   = 11,
  IIT_V16  = 12,
  IIT_V32  = 13,
  IIT_PTR  = 14,
  IIT_ARG  = 15,

  // Values from 16+ are only encodable with the inefficient encoding.
  IIT_V64  = 16,
  IIT_MMX  = 17,
  IIT_TOKEN = 18,
  IIT_METADATA = 19,
  IIT_EMPTYSTRUCT = 20,
  IIT_STRUCT2 = 21,
  IIT_STRUCT3 = 22,
  IIT_STRUCT4 = 23,
  IIT_STRUCT5 = 24,
  IIT_EXTEND_ARG = 25,
  IIT_TRUNC_ARG = 26,
  IIT_ANYPTR = 27,
  IIT_V1   = 28,
  IIT_VARARG = 29,
  IIT_HALF_VEC_ARG = 30,
  IIT_SAME_VEC_WIDTH_ARG = 31,
  IIT_PTR_TO_ARG = 32,
  IIT_PTR_TO_ELT = 33,
  IIT_VEC_OF_ANYPTRS_TO_ELT = 34,
  IIT_I128 = 35,
  IIT_V512 = 36,
  IIT_V1024 = 37,
  IIT_STRUCT6 = 38,
  IIT_STRUCT7 = 39,
  IIT_STRUCT8 = 40,
  IIT_F128 = 41
};

static void DecodeIITType(unsigned &NextElt, ArrayRef<unsigned char> Infos,
                      SmallVectorImpl<Intrinsic::IITDescriptor> &OutputTable) {
  using namespace Intrinsic;

  IIT_Info Info = IIT_Info(Infos[NextElt++]);
  unsigned StructElts = 2;

  switch (Info) {
  case IIT_Done:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Void, 0));
    return;
  case IIT_VARARG:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::VarArg, 0));
    return;
  case IIT_MMX:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::MMX, 0));
    return;
  case IIT_TOKEN:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Token, 0));
    return;
  case IIT_METADATA:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Metadata, 0));
    return;
  case IIT_F16:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Half, 0));
    return;
  case IIT_F32:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Float, 0));
    return;
  case IIT_F64:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Double, 0));
    return;
  case IIT_F128:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Quad, 0));
    return;
  case IIT_I1:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Integer, 1));
    return;
  case IIT_I8:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Integer, 8));
    return;
  case IIT_I16:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Integer,16));
    return;
  case IIT_I32:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Integer, 32));
    return;
  case IIT_I64:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Integer, 64));
    return;
  case IIT_I128:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Integer, 128));
    return;
  case IIT_V1:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Vector, 1));
    DecodeIITType(NextElt, Infos, OutputTable);
    return;
  case IIT_V2:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Vector, 2));
    DecodeIITType(NextElt, Infos, OutputTable);
    return;
  case IIT_V4:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Vector, 4));
    DecodeIITType(NextElt, Infos, OutputTable);
    return;
  case IIT_V8:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Vector, 8));
    DecodeIITType(NextElt, Infos, OutputTable);
    return;
  case IIT_V16:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Vector, 16));
    DecodeIITType(NextElt, Infos, OutputTable);
    return;
  case IIT_V32:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Vector, 32));
    DecodeIITType(NextElt, Infos, OutputTable);
    return;
  case IIT_V64:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Vector, 64));
    DecodeIITType(NextElt, Infos, OutputTable);
    return;
  case IIT_V512:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Vector, 512));
    DecodeIITType(NextElt, Infos, OutputTable);
    return;
  case IIT_V1024:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Vector, 1024));
    DecodeIITType(NextElt, Infos, OutputTable);
    return;
  case IIT_PTR:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Pointer, 0));
    DecodeIITType(NextElt, Infos, OutputTable);
    return;
  case IIT_ANYPTR: {  // [ANYPTR addrspace, subtype]
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Pointer,
                                             Infos[NextElt++]));
    DecodeIITType(NextElt, Infos, OutputTable);
    return;
  }
  case IIT_ARG: {
    unsigned ArgInfo = (NextElt == Infos.size() ? 0 : Infos[NextElt++]);
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Argument, ArgInfo));
    return;
  }
  case IIT_EXTEND_ARG: {
    unsigned ArgInfo = (NextElt == Infos.size() ? 0 : Infos[NextElt++]);
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::ExtendArgument,
                                             ArgInfo));
    return;
  }
  case IIT_TRUNC_ARG: {
    unsigned ArgInfo = (NextElt == Infos.size() ? 0 : Infos[NextElt++]);
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::TruncArgument,
                                             ArgInfo));
    return;
  }
  case IIT_HALF_VEC_ARG: {
    unsigned ArgInfo = (NextElt == Infos.size() ? 0 : Infos[NextElt++]);
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::HalfVecArgument,
                                             ArgInfo));
    return;
  }
  case IIT_SAME_VEC_WIDTH_ARG: {
    unsigned ArgInfo = (NextElt == Infos.size() ? 0 : Infos[NextElt++]);
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::SameVecWidthArgument,
                                             ArgInfo));
    return;
  }
  case IIT_PTR_TO_ARG: {
    unsigned ArgInfo = (NextElt == Infos.size() ? 0 : Infos[NextElt++]);
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::PtrToArgument,
                                             ArgInfo));
    return;
  }
  case IIT_PTR_TO_ELT: {
    unsigned ArgInfo = (NextElt == Infos.size() ? 0 : Infos[NextElt++]);
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::PtrToElt, ArgInfo));
    return;
  }
  case IIT_VEC_OF_ANYPTRS_TO_ELT: {
    unsigned short ArgNo = (NextElt == Infos.size() ? 0 : Infos[NextElt++]);
    unsigned short RefNo = (NextElt == Infos.size() ? 0 : Infos[NextElt++]);
    OutputTable.push_back(
        IITDescriptor::get(IITDescriptor::VecOfAnyPtrsToElt, ArgNo, RefNo));
    return;
  }
  case IIT_EMPTYSTRUCT:
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Struct, 0));
    return;
  case IIT_STRUCT8: ++StructElts; LLVM_FALLTHROUGH;
  case IIT_STRUCT7: ++StructElts; LLVM_FALLTHROUGH;
  case IIT_STRUCT6: ++StructElts; LLVM_FALLTHROUGH;
  case IIT_STRUCT5: ++StructElts; LLVM_FALLTHROUGH;
  case IIT_STRUCT4: ++StructElts; LLVM_FALLTHROUGH;
  case IIT_STRUCT3: ++StructElts; LLVM_FALLTHROUGH;
  case IIT_STRUCT2: {
    OutputTable.push_back(IITDescriptor::get(IITDescriptor::Struct,StructElts));

    for (unsigned i = 0; i != StructElts; ++i)
      DecodeIITType(NextElt, Infos, OutputTable);
    return;
  }
  }
  llvm_unreachable("unhandled");
}

#define GET_INTRINSIC_GENERATOR_GLOBAL
#include "llvm/IR/IntrinsicImpl.inc"
#undef GET_INTRINSIC_GENERATOR_GLOBAL

void Intrinsic::getIntrinsicInfoTableEntries(ID id,
                                             SmallVectorImpl<IITDescriptor> &T){
  // Check to see if the intrinsic's type was expressible by the table.
  unsigned TableVal = IIT_Table[id-1];

  // Decode the TableVal into an array of IITValues.
  SmallVector<unsigned char, 8> IITValues;
  ArrayRef<unsigned char> IITEntries;
  unsigned NextElt = 0;
  if ((TableVal >> 31) != 0) {
    // This is an offset into the IIT_LongEncodingTable.
    IITEntries = IIT_LongEncodingTable;

    // Strip sentinel bit.
    NextElt = (TableVal << 1) >> 1;
  } else {
    // Decode the TableVal into an array of IITValues.  If the entry was encoded
    // into a single word in the table itself, decode it now.
    do {
      IITValues.push_back(TableVal & 0xF);
      TableVal >>= 4;
    } while (TableVal);

    IITEntries = IITValues;
    NextElt = 0;
  }

  // Okay, decode the table into the output vector of IITDescriptors.
  DecodeIITType(NextElt, IITEntries, T);
  while (NextElt != IITEntries.size() && IITEntries[NextElt] != 0)
    DecodeIITType(NextElt, IITEntries, T);
}

static Type *DecodeFixedType(ArrayRef<Intrinsic::IITDescriptor> &Infos,
                             ArrayRef<Type*> Tys, LLVMContext &Context) {
  using namespace Intrinsic;

  IITDescriptor D = Infos.front();
  Infos = Infos.slice(1);

  switch (D.Kind) {
  case IITDescriptor::Void: return Type::getVoidTy(Context);
  case IITDescriptor::VarArg: return Type::getVoidTy(Context);
  case IITDescriptor::MMX: return Type::getX86_MMXTy(Context);
  case IITDescriptor::Token: return Type::getTokenTy(Context);
  case IITDescriptor::Metadata: return Type::getMetadataTy(Context);
  case IITDescriptor::Half: return Type::getHalfTy(Context);
  case IITDescriptor::Float: return Type::getFloatTy(Context);
  case IITDescriptor::Double: return Type::getDoubleTy(Context);
  case IITDescriptor::Quad: return Type::getFP128Ty(Context);

  case IITDescriptor::Integer:
    return IntegerType::get(Context, D.Integer_Width);
  case IITDescriptor::Vector:
    return VectorType::get(DecodeFixedType(Infos, Tys, Context),D.Vector_Width);
  case IITDescriptor::Pointer:
    return PointerType::get(DecodeFixedType(Infos, Tys, Context),
                            D.Pointer_AddressSpace);
  case IITDescriptor::Struct: {
    SmallVector<Type *, 8> Elts;
    for (unsigned i = 0, e = D.Struct_NumElements; i != e; ++i)
      Elts.push_back(DecodeFixedType(Infos, Tys, Context));
    return StructType::get(Context, Elts);
  }
  case IITDescriptor::Argument:
    return Tys[D.getArgumentNumber()];
  case IITDescriptor::ExtendArgument: {
    Type *Ty = Tys[D.getArgumentNumber()];
    if (VectorType *VTy = dyn_cast<VectorType>(Ty))
      return VectorType::getExtendedElementVectorType(VTy);

    return IntegerType::get(Context, 2 * cast<IntegerType>(Ty)->getBitWidth());
  }
  case IITDescriptor::TruncArgument: {
    Type *Ty = Tys[D.getArgumentNumber()];
    if (VectorType *VTy = dyn_cast<VectorType>(Ty))
      return VectorType::getTruncatedElementVectorType(VTy);

    IntegerType *ITy = cast<IntegerType>(Ty);
    assert(ITy->getBitWidth() % 2 == 0);
    return IntegerType::get(Context, ITy->getBitWidth() / 2);
  }
  case IITDescriptor::HalfVecArgument:
    return VectorType::getHalfElementsVectorType(cast<VectorType>(
                                                  Tys[D.getArgumentNumber()]));
  case IITDescriptor::SameVecWidthArgument: {
    Type *EltTy = DecodeFixedType(Infos, Tys, Context);
    Type *Ty = Tys[D.getArgumentNumber()];
    if (VectorType *VTy = dyn_cast<VectorType>(Ty)) {
      return VectorType::get(EltTy, VTy->getNumElements());
    }
    llvm_unreachable("unhandled");
  }
  case IITDescriptor::PtrToArgument: {
    Type *Ty = Tys[D.getArgumentNumber()];
    return PointerType::getUnqual(Ty);
  }
  case IITDescriptor::PtrToElt: {
    Type *Ty = Tys[D.getArgumentNumber()];
    VectorType *VTy = dyn_cast<VectorType>(Ty);
    if (!VTy)
      llvm_unreachable("Expected an argument of Vector Type");
    Type *EltTy = VTy->getVectorElementType();
    return PointerType::getUnqual(EltTy);
  }
  case IITDescriptor::VecOfAnyPtrsToElt:
    // Return the overloaded type (which determines the pointers address space)
    return Tys[D.getOverloadArgNumber()];
  }
  llvm_unreachable("unhandled");
}

FunctionType *Intrinsic::getType(LLVMContext &Context,
                                 ID id, ArrayRef<Type*> Tys) {
  SmallVector<IITDescriptor, 8> Table;
  getIntrinsicInfoTableEntries(id, Table);

  ArrayRef<IITDescriptor> TableRef = Table;
  Type *ResultTy = DecodeFixedType(TableRef, Tys, Context);

  SmallVector<Type*, 8> ArgTys;
  while (!TableRef.empty())
    ArgTys.push_back(DecodeFixedType(TableRef, Tys, Context));

  // DecodeFixedType returns Void for IITDescriptor::Void and IITDescriptor::VarArg
  // If we see void type as the type of the last argument, it is vararg intrinsic
  if (!ArgTys.empty() && ArgTys.back()->isVoidTy()) {
    ArgTys.pop_back();
    return FunctionType::get(ResultTy, ArgTys, true);
  }
  return FunctionType::get(ResultTy, ArgTys, false);
}

bool Intrinsic::isOverloaded(ID id) {
#define GET_INTRINSIC_OVERLOAD_TABLE
#include "llvm/IR/IntrinsicImpl.inc"
#undef GET_INTRINSIC_OVERLOAD_TABLE
}

bool Intrinsic::isLeaf(ID id) {
  switch (id) {
  default:
    return true;

  case Intrinsic::experimental_gc_statepoint:
  case Intrinsic::experimental_patchpoint_void:
  case Intrinsic::experimental_patchpoint_i64:
    return false;
  }
}

/// This defines the "Intrinsic::getAttributes(ID id)" method.
#define GET_INTRINSIC_ATTRIBUTES
#include "llvm/IR/IntrinsicImpl.inc"
#undef GET_INTRINSIC_ATTRIBUTES

Function *Intrinsic::getDeclaration(Module *M, ID id, ArrayRef<Type*> Tys) {
  // There can never be multiple globals with the same name of different types,
  // because intrinsics must be a specific type.
  return
    cast<Function>(M->getOrInsertFunction(getName(id, Tys),
                                          getType(M->getContext(), id, Tys)));
}

// This defines the "Intrinsic::getIntrinsicForGCCBuiltin()" method.
#define GET_LLVM_INTRINSIC_FOR_GCC_BUILTIN
#include "llvm/IR/IntrinsicImpl.inc"
#undef GET_LLVM_INTRINSIC_FOR_GCC_BUILTIN

// This defines the "Intrinsic::getIntrinsicForMSBuiltin()" method.
#define GET_LLVM_INTRINSIC_FOR_MS_BUILTIN
#include "llvm/IR/IntrinsicImpl.inc"
#undef GET_LLVM_INTRINSIC_FOR_MS_BUILTIN

bool Intrinsic::matchIntrinsicType(Type *Ty, ArrayRef<Intrinsic::IITDescriptor> &Infos,
                                   SmallVectorImpl<Type*> &ArgTys) {
  using namespace Intrinsic;

  // If we ran out of descriptors, there are too many arguments.
  if (Infos.empty()) return true;
  IITDescriptor D = Infos.front();
  Infos = Infos.slice(1);

  switch (D.Kind) {
    case IITDescriptor::Void: return !Ty->isVoidTy();
    case IITDescriptor::VarArg: return true;
    case IITDescriptor::MMX:  return !Ty->isX86_MMXTy();
    case IITDescriptor::Token: return !Ty->isTokenTy();
    case IITDescriptor::Metadata: return !Ty->isMetadataTy();
    case IITDescriptor::Half: return !Ty->isHalfTy();
    case IITDescriptor::Float: return !Ty->isFloatTy();
    case IITDescriptor::Double: return !Ty->isDoubleTy();
    case IITDescriptor::Quad: return !Ty->isFP128Ty();
    case IITDescriptor::Integer: return !Ty->isIntegerTy(D.Integer_Width);
    case IITDescriptor::Vector: {
      VectorType *VT = dyn_cast<VectorType>(Ty);
      return !VT || VT->getNumElements() != D.Vector_Width ||
             matchIntrinsicType(VT->getElementType(), Infos, ArgTys);
    }
    case IITDescriptor::Pointer: {
      PointerType *PT = dyn_cast<PointerType>(Ty);
      return !PT || PT->getAddressSpace() != D.Pointer_AddressSpace ||
             matchIntrinsicType(PT->getElementType(), Infos, ArgTys);
    }

    case IITDescriptor::Struct: {
      StructType *ST = dyn_cast<StructType>(Ty);
      if (!ST || ST->getNumElements() != D.Struct_NumElements)
        return true;

      for (unsigned i = 0, e = D.Struct_NumElements; i != e; ++i)
        if (matchIntrinsicType(ST->getElementType(i), Infos, ArgTys))
          return true;
      return false;
    }

    case IITDescriptor::Argument:
      // Two cases here - If this is the second occurrence of an argument, verify
      // that the later instance matches the previous instance.
      if (D.getArgumentNumber() < ArgTys.size())
        return Ty != ArgTys[D.getArgumentNumber()];

          // Otherwise, if this is the first instance of an argument, record it and
          // verify the "Any" kind.
          assert(D.getArgumentNumber() == ArgTys.size() && "Table consistency error");
          ArgTys.push_back(Ty);

          switch (D.getArgumentKind()) {
            case IITDescriptor::AK_Any:        return false; // Success
            case IITDescriptor::AK_AnyInteger: return !Ty->isIntOrIntVectorTy();
            case IITDescriptor::AK_AnyFloat:   return !Ty->isFPOrFPVectorTy();
            case IITDescriptor::AK_AnyVector:  return !isa<VectorType>(Ty);
            case IITDescriptor::AK_AnyPointer: return !isa<PointerType>(Ty);
          }
          llvm_unreachable("all argument kinds not covered");

    case IITDescriptor::ExtendArgument: {
      // This may only be used when referring to a previous vector argument.
      if (D.getArgumentNumber() >= ArgTys.size())
        return true;

      Type *NewTy = ArgTys[D.getArgumentNumber()];
      if (VectorType *VTy = dyn_cast<VectorType>(NewTy))
        NewTy = VectorType::getExtendedElementVectorType(VTy);
      else if (IntegerType *ITy = dyn_cast<IntegerType>(NewTy))
        NewTy = IntegerType::get(ITy->getContext(), 2 * ITy->getBitWidth());
      else
        return true;

      return Ty != NewTy;
    }
    case IITDescriptor::TruncArgument: {
      // This may only be used when referring to a previous vector argument.
      if (D.getArgumentNumber() >= ArgTys.size())
        return true;

      Type *NewTy = ArgTys[D.getArgumentNumber()];
      if (VectorType *VTy = dyn_cast<VectorType>(NewTy))
        NewTy = VectorType::getTruncatedElementVectorType(VTy);
      else if (IntegerType *ITy = dyn_cast<IntegerType>(NewTy))
        NewTy = IntegerType::get(ITy->getContext(), ITy->getBitWidth() / 2);
      else
        return true;

      return Ty != NewTy;
    }
    case IITDescriptor::HalfVecArgument:
      // This may only be used when referring to a previous vector argument.
      return D.getArgumentNumber() >= ArgTys.size() ||
             !isa<VectorType>(ArgTys[D.getArgumentNumber()]) ||
             VectorType::getHalfElementsVectorType(
                     cast<VectorType>(ArgTys[D.getArgumentNumber()])) != Ty;
    case IITDescriptor::SameVecWidthArgument: {
      if (D.getArgumentNumber() >= ArgTys.size())
        return true;
      VectorType * ReferenceType =
        dyn_cast<VectorType>(ArgTys[D.getArgumentNumber()]);
      VectorType *ThisArgType = dyn_cast<VectorType>(Ty);
      if (!ThisArgType || !ReferenceType ||
          (ReferenceType->getVectorNumElements() !=
           ThisArgType->getVectorNumElements()))
        return true;
      return matchIntrinsicType(ThisArgType->getVectorElementType(),
                                Infos, ArgTys);
    }
    case IITDescriptor::PtrToArgument: {
      if (D.getArgumentNumber() >= ArgTys.size())
        return true;
      Type * ReferenceType = ArgTys[D.getArgumentNumber()];
      PointerType *ThisArgType = dyn_cast<PointerType>(Ty);
      return (!ThisArgType || ThisArgType->getElementType() != ReferenceType);
    }
    case IITDescriptor::PtrToElt: {
      if (D.getArgumentNumber() >= ArgTys.size())
        return true;
      VectorType * ReferenceType =
        dyn_cast<VectorType> (ArgTys[D.getArgumentNumber()]);
      PointerType *ThisArgType = dyn_cast<PointerType>(Ty);

      return (!ThisArgType || !ReferenceType ||
              ThisArgType->getElementType() != ReferenceType->getElementType());
    }
    case IITDescriptor::VecOfAnyPtrsToElt: {
      unsigned RefArgNumber = D.getRefArgNumber();

      // This may only be used when referring to a previous argument.
      if (RefArgNumber >= ArgTys.size())
        return true;

      // Record the overloaded type
      assert(D.getOverloadArgNumber() == ArgTys.size() &&
             "Table consistency error");
      ArgTys.push_back(Ty);

      // Verify the overloaded type "matches" the Ref type.
      // i.e. Ty is a vector with the same width as Ref.
      // Composed of pointers to the same element type as Ref.
      VectorType *ReferenceType = dyn_cast<VectorType>(ArgTys[RefArgNumber]);
      VectorType *ThisArgVecTy = dyn_cast<VectorType>(Ty);
      if (!ThisArgVecTy || !ReferenceType ||
          (ReferenceType->getVectorNumElements() !=
           ThisArgVecTy->getVectorNumElements()))
        return true;
      PointerType *ThisArgEltTy =
              dyn_cast<PointerType>(ThisArgVecTy->getVectorElementType());
      if (!ThisArgEltTy)
        return true;
      return ThisArgEltTy->getElementType() !=
             ReferenceType->getVectorElementType();
    }
  }
  llvm_unreachable("unhandled");
}

bool
Intrinsic::matchIntrinsicVarArg(bool isVarArg,
                                ArrayRef<Intrinsic::IITDescriptor> &Infos) {
  // If there are no descriptors left, then it can't be a vararg.
  if (Infos.empty())
    return isVarArg;

  // There should be only one descriptor remaining at this point.
  if (Infos.size() != 1)
    return true;

  // Check and verify the descriptor.
  IITDescriptor D = Infos.front();
  Infos = Infos.slice(1);
  if (D.Kind == IITDescriptor::VarArg)
    return !isVarArg;

  return true;
}

Optional<Function*> Intrinsic::remangleIntrinsicFunction(Function *F) {
  Intrinsic::ID ID = F->getIntrinsicID();
  if (!ID)
    return None;

  FunctionType *FTy = F->getFunctionType();
  // Accumulate an array of overloaded types for the given intrinsic
  SmallVector<Type *, 4> ArgTys;
  {
    SmallVector<Intrinsic::IITDescriptor, 8> Table;
    getIntrinsicInfoTableEntries(ID, Table);
    ArrayRef<Intrinsic::IITDescriptor> TableRef = Table;

    // If we encounter any problems matching the signature with the descriptor
    // just give up remangling. It's up to verifier to report the discrepancy.
    if (Intrinsic::matchIntrinsicType(FTy->getReturnType(), TableRef, ArgTys))
      return None;
    for (auto Ty : FTy->params())
      if (Intrinsic::matchIntrinsicType(Ty, TableRef, ArgTys))
        return None;
    if (Intrinsic::matchIntrinsicVarArg(FTy->isVarArg(), TableRef))
      return None;
  }

  StringRef Name = F->getName();
  if (Name == Intrinsic::getName(ID, ArgTys))
    return None;

  auto NewDecl = Intrinsic::getDeclaration(F->getParent(), ID, ArgTys);
  NewDecl->setCallingConv(F->getCallingConv());
  assert(NewDecl->getFunctionType() == FTy && "Shouldn't change the signature");
  return NewDecl;
}

/// hasAddressTaken - returns true if there are any uses of this function
/// other than direct calls or invokes to it.
bool Function::hasAddressTaken(const User* *PutOffender) const {
  for (const Use &U : uses()) {
    const User *FU = U.getUser();
    if (isa<BlockAddress>(FU))
      continue;
    const auto *Call = dyn_cast<CallBase>(FU);
    if (!Call) {
      if (PutOffender)
        *PutOffender = FU;
      return true;
    }
    if (!Call->isCallee(&U)) {
      if (PutOffender)
        *PutOffender = FU;
      return true;
    }
  }
  return false;
}

bool Function::isDefTriviallyDead() const {
  // Check the linkage
  if (!hasLinkOnceLinkage() && !hasLocalLinkage() &&
      !hasAvailableExternallyLinkage())
    return false;

  // Check if the function is used by anything other than a blockaddress.
  for (const User *U : users())
    if (!isa<BlockAddress>(U))
      return false;

  return true;
}

/// callsFunctionThatReturnsTwice - Return true if the function has a call to
/// setjmp or other function that gcc recognizes as "returning twice".
bool Function::callsFunctionThatReturnsTwice() const {
  for (const Instruction &I : instructions(this))
    if (const auto *Call = dyn_cast<CallBase>(&I))
      if (Call->hasFnAttr(Attribute::ReturnsTwice))
        return true;

  return false;
}

Constant *Function::getPersonalityFn() const {
  assert(hasPersonalityFn() && getNumOperands());
  return cast<Constant>(Op<0>());
}

void Function::setPersonalityFn(Constant *Fn) {
  setHungoffOperand<0>(Fn);
  setValueSubclassDataBit(3, Fn != nullptr);
}

Constant *Function::getPrefixData() const {
  assert(hasPrefixData() && getNumOperands());
  return cast<Constant>(Op<1>());
}

void Function::setPrefixData(Constant *PrefixData) {
  setHungoffOperand<1>(PrefixData);
  setValueSubclassDataBit(1, PrefixData != nullptr);
}

Constant *Function::getPrologueData() const {
  assert(hasPrologueData() && getNumOperands());
  return cast<Constant>(Op<2>());
}

void Function::setPrologueData(Constant *PrologueData) {
  setHungoffOperand<2>(PrologueData);
  setValueSubclassDataBit(2, PrologueData != nullptr);
}

void Function::allocHungoffUselist() {
  // If we've already allocated a uselist, stop here.
  if (getNumOperands())
    return;

  allocHungoffUses(3, /*IsPhi=*/ false);
  setNumHungOffUseOperands(3);

  // Initialize the uselist with placeholder operands to allow traversal.
  auto *CPN = ConstantPointerNull::get(Type::getInt1PtrTy(getContext(), 0));
  Op<0>().set(CPN);
  Op<1>().set(CPN);
  Op<2>().set(CPN);
}

template <int Idx>
void Function::setHungoffOperand(Constant *C) {
  if (C) {
    allocHungoffUselist();
    Op<Idx>().set(C);
  } else if (getNumOperands()) {
    Op<Idx>().set(
        ConstantPointerNull::get(Type::getInt1PtrTy(getContext(), 0)));
  }
}

void Function::setValueSubclassDataBit(unsigned Bit, bool On) {
  assert(Bit < 16 && "SubclassData contains only 16 bits");
  if (On)
    setValueSubclassData(getSubclassDataFromValue() | (1 << Bit));
  else
    setValueSubclassData(getSubclassDataFromValue() & ~(1 << Bit));
}

void Function::setEntryCount(ProfileCount Count,
                             const DenseSet<GlobalValue::GUID> *S) {
  assert(Count.hasValue());
#if !defined(NDEBUG)
  auto PrevCount = getEntryCount();
  assert(!PrevCount.hasValue() || PrevCount.getType() == Count.getType());
#endif
  MDBuilder MDB(getContext());
  setMetadata(
      LLVMContext::MD_prof,
      MDB.createFunctionEntryCount(Count.getCount(), Count.isSynthetic(), S));
}

void Function::setEntryCount(uint64_t Count, Function::ProfileCountType Type,
                             const DenseSet<GlobalValue::GUID> *Imports) {
  setEntryCount(ProfileCount(Count, Type), Imports);
}

ProfileCount Function::getEntryCount() const {
  MDNode *MD = getMetadata(LLVMContext::MD_prof);
  if (MD && MD->getOperand(0))
    if (MDString *MDS = dyn_cast<MDString>(MD->getOperand(0))) {
      if (MDS->getString().equals("function_entry_count")) {
        ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(1));
        uint64_t Count = CI->getValue().getZExtValue();
        // A value of -1 is used for SamplePGO when there were no samples.
        // Treat this the same as unknown.
        if (Count == (uint64_t)-1)
          return ProfileCount::getInvalid();
        return ProfileCount(Count, PCT_Real);
      } else if (MDS->getString().equals("synthetic_function_entry_count")) {
        ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(1));
        uint64_t Count = CI->getValue().getZExtValue();
        return ProfileCount(Count, PCT_Synthetic);
      }
    }
  return ProfileCount::getInvalid();
}

DenseSet<GlobalValue::GUID> Function::getImportGUIDs() const {
  DenseSet<GlobalValue::GUID> R;
  if (MDNode *MD = getMetadata(LLVMContext::MD_prof))
    if (MDString *MDS = dyn_cast<MDString>(MD->getOperand(0)))
      if (MDS->getString().equals("function_entry_count"))
        for (unsigned i = 2; i < MD->getNumOperands(); i++)
          R.insert(mdconst::extract<ConstantInt>(MD->getOperand(i))
                       ->getValue()
                       .getZExtValue());
  return R;
}

void Function::setSectionPrefix(StringRef Prefix) {
  MDBuilder MDB(getContext());
  setMetadata(LLVMContext::MD_section_prefix,
              MDB.createFunctionSectionPrefix(Prefix));
}

Optional<StringRef> Function::getSectionPrefix() const {
  if (MDNode *MD = getMetadata(LLVMContext::MD_section_prefix)) {
    assert(cast<MDString>(MD->getOperand(0))
               ->getString()
               .equals("function_section_prefix") &&
           "Metadata not match");
    return cast<MDString>(MD->getOperand(1))->getString();
  }
  return None;
}

bool Function::nullPointerIsDefined() const {
  return getFnAttribute("null-pointer-is-valid")
          .getValueAsString()
          .equals("true");
}

bool llvm::NullPointerIsDefined(const Function *F, unsigned AS) {
  if (F && F->nullPointerIsDefined())
    return true;

  if (AS != 0)
    return true;

  return false;
}
