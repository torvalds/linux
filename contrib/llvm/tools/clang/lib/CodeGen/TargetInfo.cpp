//===---- TargetInfo.cpp - Encapsulate target details -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// These classes wrap the information about a call or function
// definition used to handle ABI compliancy.
//
//===----------------------------------------------------------------------===//

#include "TargetInfo.h"
#include "ABIInfo.h"
#include "CGBlocks.h"
#include "CGCXXABI.h"
#include "CGValue.h"
#include "CodeGenFunction.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "clang/CodeGen/SwiftCallingConv.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>    // std::sort

using namespace clang;
using namespace CodeGen;

// Helper for coercing an aggregate argument or return value into an integer
// array of the same size (including padding) and alignment.  This alternate
// coercion happens only for the RenderScript ABI and can be removed after
// runtimes that rely on it are no longer supported.
//
// RenderScript assumes that the size of the argument / return value in the IR
// is the same as the size of the corresponding qualified type. This helper
// coerces the aggregate type into an array of the same size (including
// padding).  This coercion is used in lieu of expansion of struct members or
// other canonical coercions that return a coerced-type of larger size.
//
// Ty          - The argument / return value type
// Context     - The associated ASTContext
// LLVMContext - The associated LLVMContext
static ABIArgInfo coerceToIntArray(QualType Ty,
                                   ASTContext &Context,
                                   llvm::LLVMContext &LLVMContext) {
  // Alignment and Size are measured in bits.
  const uint64_t Size = Context.getTypeSize(Ty);
  const uint64_t Alignment = Context.getTypeAlign(Ty);
  llvm::Type *IntType = llvm::Type::getIntNTy(LLVMContext, Alignment);
  const uint64_t NumElements = (Size + Alignment - 1) / Alignment;
  return ABIArgInfo::getDirect(llvm::ArrayType::get(IntType, NumElements));
}

static void AssignToArrayRange(CodeGen::CGBuilderTy &Builder,
                               llvm::Value *Array,
                               llvm::Value *Value,
                               unsigned FirstIndex,
                               unsigned LastIndex) {
  // Alternatively, we could emit this as a loop in the source.
  for (unsigned I = FirstIndex; I <= LastIndex; ++I) {
    llvm::Value *Cell =
        Builder.CreateConstInBoundsGEP1_32(Builder.getInt8Ty(), Array, I);
    Builder.CreateAlignedStore(Value, Cell, CharUnits::One());
  }
}

static bool isAggregateTypeForABI(QualType T) {
  return !CodeGenFunction::hasScalarEvaluationKind(T) ||
         T->isMemberFunctionPointerType();
}

ABIArgInfo
ABIInfo::getNaturalAlignIndirect(QualType Ty, bool ByRef, bool Realign,
                                 llvm::Type *Padding) const {
  return ABIArgInfo::getIndirect(getContext().getTypeAlignInChars(Ty),
                                 ByRef, Realign, Padding);
}

ABIArgInfo
ABIInfo::getNaturalAlignIndirectInReg(QualType Ty, bool Realign) const {
  return ABIArgInfo::getIndirectInReg(getContext().getTypeAlignInChars(Ty),
                                      /*ByRef*/ false, Realign);
}

Address ABIInfo::EmitMSVAArg(CodeGenFunction &CGF, Address VAListAddr,
                             QualType Ty) const {
  return Address::invalid();
}

ABIInfo::~ABIInfo() {}

/// Does the given lowering require more than the given number of
/// registers when expanded?
///
/// This is intended to be the basis of a reasonable basic implementation
/// of should{Pass,Return}IndirectlyForSwift.
///
/// For most targets, a limit of four total registers is reasonable; this
/// limits the amount of code required in order to move around the value
/// in case it wasn't produced immediately prior to the call by the caller
/// (or wasn't produced in exactly the right registers) or isn't used
/// immediately within the callee.  But some targets may need to further
/// limit the register count due to an inability to support that many
/// return registers.
static bool occupiesMoreThan(CodeGenTypes &cgt,
                             ArrayRef<llvm::Type*> scalarTypes,
                             unsigned maxAllRegisters) {
  unsigned intCount = 0, fpCount = 0;
  for (llvm::Type *type : scalarTypes) {
    if (type->isPointerTy()) {
      intCount++;
    } else if (auto intTy = dyn_cast<llvm::IntegerType>(type)) {
      auto ptrWidth = cgt.getTarget().getPointerWidth(0);
      intCount += (intTy->getBitWidth() + ptrWidth - 1) / ptrWidth;
    } else {
      assert(type->isVectorTy() || type->isFloatingPointTy());
      fpCount++;
    }
  }

  return (intCount + fpCount > maxAllRegisters);
}

bool SwiftABIInfo::isLegalVectorTypeForSwift(CharUnits vectorSize,
                                             llvm::Type *eltTy,
                                             unsigned numElts) const {
  // The default implementation of this assumes that the target guarantees
  // 128-bit SIMD support but nothing more.
  return (vectorSize.getQuantity() > 8 && vectorSize.getQuantity() <= 16);
}

static CGCXXABI::RecordArgABI getRecordArgABI(const RecordType *RT,
                                              CGCXXABI &CXXABI) {
  const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(RT->getDecl());
  if (!RD) {
    if (!RT->getDecl()->canPassInRegisters())
      return CGCXXABI::RAA_Indirect;
    return CGCXXABI::RAA_Default;
  }
  return CXXABI.getRecordArgABI(RD);
}

static CGCXXABI::RecordArgABI getRecordArgABI(QualType T,
                                              CGCXXABI &CXXABI) {
  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return CGCXXABI::RAA_Default;
  return getRecordArgABI(RT, CXXABI);
}

static bool classifyReturnType(const CGCXXABI &CXXABI, CGFunctionInfo &FI,
                               const ABIInfo &Info) {
  QualType Ty = FI.getReturnType();

  if (const auto *RT = Ty->getAs<RecordType>())
    if (!isa<CXXRecordDecl>(RT->getDecl()) &&
        !RT->getDecl()->canPassInRegisters()) {
      FI.getReturnInfo() = Info.getNaturalAlignIndirect(Ty);
      return true;
    }

  return CXXABI.classifyReturnType(FI);
}

/// Pass transparent unions as if they were the type of the first element. Sema
/// should ensure that all elements of the union have the same "machine type".
static QualType useFirstFieldIfTransparentUnion(QualType Ty) {
  if (const RecordType *UT = Ty->getAsUnionType()) {
    const RecordDecl *UD = UT->getDecl();
    if (UD->hasAttr<TransparentUnionAttr>()) {
      assert(!UD->field_empty() && "sema created an empty transparent union");
      return UD->field_begin()->getType();
    }
  }
  return Ty;
}

CGCXXABI &ABIInfo::getCXXABI() const {
  return CGT.getCXXABI();
}

ASTContext &ABIInfo::getContext() const {
  return CGT.getContext();
}

llvm::LLVMContext &ABIInfo::getVMContext() const {
  return CGT.getLLVMContext();
}

const llvm::DataLayout &ABIInfo::getDataLayout() const {
  return CGT.getDataLayout();
}

const TargetInfo &ABIInfo::getTarget() const {
  return CGT.getTarget();
}

const CodeGenOptions &ABIInfo::getCodeGenOpts() const {
  return CGT.getCodeGenOpts();
}

bool ABIInfo::isAndroid() const { return getTarget().getTriple().isAndroid(); }

bool ABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  return false;
}

bool ABIInfo::isHomogeneousAggregateSmallEnough(const Type *Base,
                                                uint64_t Members) const {
  return false;
}

LLVM_DUMP_METHOD void ABIArgInfo::dump() const {
  raw_ostream &OS = llvm::errs();
  OS << "(ABIArgInfo Kind=";
  switch (TheKind) {
  case Direct:
    OS << "Direct Type=";
    if (llvm::Type *Ty = getCoerceToType())
      Ty->print(OS);
    else
      OS << "null";
    break;
  case Extend:
    OS << "Extend";
    break;
  case Ignore:
    OS << "Ignore";
    break;
  case InAlloca:
    OS << "InAlloca Offset=" << getInAllocaFieldIndex();
    break;
  case Indirect:
    OS << "Indirect Align=" << getIndirectAlign().getQuantity()
       << " ByVal=" << getIndirectByVal()
       << " Realign=" << getIndirectRealign();
    break;
  case Expand:
    OS << "Expand";
    break;
  case CoerceAndExpand:
    OS << "CoerceAndExpand Type=";
    getCoerceAndExpandType()->print(OS);
    break;
  }
  OS << ")\n";
}

// Dynamically round a pointer up to a multiple of the given alignment.
static llvm::Value *emitRoundPointerUpToAlignment(CodeGenFunction &CGF,
                                                  llvm::Value *Ptr,
                                                  CharUnits Align) {
  llvm::Value *PtrAsInt = Ptr;
  // OverflowArgArea = (OverflowArgArea + Align - 1) & -Align;
  PtrAsInt = CGF.Builder.CreatePtrToInt(PtrAsInt, CGF.IntPtrTy);
  PtrAsInt = CGF.Builder.CreateAdd(PtrAsInt,
        llvm::ConstantInt::get(CGF.IntPtrTy, Align.getQuantity() - 1));
  PtrAsInt = CGF.Builder.CreateAnd(PtrAsInt,
           llvm::ConstantInt::get(CGF.IntPtrTy, -Align.getQuantity()));
  PtrAsInt = CGF.Builder.CreateIntToPtr(PtrAsInt,
                                        Ptr->getType(),
                                        Ptr->getName() + ".aligned");
  return PtrAsInt;
}

/// Emit va_arg for a platform using the common void* representation,
/// where arguments are simply emitted in an array of slots on the stack.
///
/// This version implements the core direct-value passing rules.
///
/// \param SlotSize - The size and alignment of a stack slot.
///   Each argument will be allocated to a multiple of this number of
///   slots, and all the slots will be aligned to this value.
/// \param AllowHigherAlign - The slot alignment is not a cap;
///   an argument type with an alignment greater than the slot size
///   will be emitted on a higher-alignment address, potentially
///   leaving one or more empty slots behind as padding.  If this
///   is false, the returned address might be less-aligned than
///   DirectAlign.
static Address emitVoidPtrDirectVAArg(CodeGenFunction &CGF,
                                      Address VAListAddr,
                                      llvm::Type *DirectTy,
                                      CharUnits DirectSize,
                                      CharUnits DirectAlign,
                                      CharUnits SlotSize,
                                      bool AllowHigherAlign) {
  // Cast the element type to i8* if necessary.  Some platforms define
  // va_list as a struct containing an i8* instead of just an i8*.
  if (VAListAddr.getElementType() != CGF.Int8PtrTy)
    VAListAddr = CGF.Builder.CreateElementBitCast(VAListAddr, CGF.Int8PtrTy);

  llvm::Value *Ptr = CGF.Builder.CreateLoad(VAListAddr, "argp.cur");

  // If the CC aligns values higher than the slot size, do so if needed.
  Address Addr = Address::invalid();
  if (AllowHigherAlign && DirectAlign > SlotSize) {
    Addr = Address(emitRoundPointerUpToAlignment(CGF, Ptr, DirectAlign),
                                                 DirectAlign);
  } else {
    Addr = Address(Ptr, SlotSize);
  }

  // Advance the pointer past the argument, then store that back.
  CharUnits FullDirectSize = DirectSize.alignTo(SlotSize);
  llvm::Value *NextPtr =
    CGF.Builder.CreateConstInBoundsByteGEP(Addr.getPointer(), FullDirectSize,
                                           "argp.next");
  CGF.Builder.CreateStore(NextPtr, VAListAddr);

  // If the argument is smaller than a slot, and this is a big-endian
  // target, the argument will be right-adjusted in its slot.
  if (DirectSize < SlotSize && CGF.CGM.getDataLayout().isBigEndian() &&
      !DirectTy->isStructTy()) {
    Addr = CGF.Builder.CreateConstInBoundsByteGEP(Addr, SlotSize - DirectSize);
  }

  Addr = CGF.Builder.CreateElementBitCast(Addr, DirectTy);
  return Addr;
}

/// Emit va_arg for a platform using the common void* representation,
/// where arguments are simply emitted in an array of slots on the stack.
///
/// \param IsIndirect - Values of this type are passed indirectly.
/// \param ValueInfo - The size and alignment of this type, generally
///   computed with getContext().getTypeInfoInChars(ValueTy).
/// \param SlotSizeAndAlign - The size and alignment of a stack slot.
///   Each argument will be allocated to a multiple of this number of
///   slots, and all the slots will be aligned to this value.
/// \param AllowHigherAlign - The slot alignment is not a cap;
///   an argument type with an alignment greater than the slot size
///   will be emitted on a higher-alignment address, potentially
///   leaving one or more empty slots behind as padding.
static Address emitVoidPtrVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                QualType ValueTy, bool IsIndirect,
                                std::pair<CharUnits, CharUnits> ValueInfo,
                                CharUnits SlotSizeAndAlign,
                                bool AllowHigherAlign) {
  // The size and alignment of the value that was passed directly.
  CharUnits DirectSize, DirectAlign;
  if (IsIndirect) {
    DirectSize = CGF.getPointerSize();
    DirectAlign = CGF.getPointerAlign();
  } else {
    DirectSize = ValueInfo.first;
    DirectAlign = ValueInfo.second;
  }

  // Cast the address we've calculated to the right type.
  llvm::Type *DirectTy = CGF.ConvertTypeForMem(ValueTy);
  if (IsIndirect)
    DirectTy = DirectTy->getPointerTo(0);

  Address Addr = emitVoidPtrDirectVAArg(CGF, VAListAddr, DirectTy,
                                        DirectSize, DirectAlign,
                                        SlotSizeAndAlign,
                                        AllowHigherAlign);

  if (IsIndirect) {
    Addr = Address(CGF.Builder.CreateLoad(Addr), ValueInfo.second);
  }

  return Addr;

}

static Address emitMergePHI(CodeGenFunction &CGF,
                            Address Addr1, llvm::BasicBlock *Block1,
                            Address Addr2, llvm::BasicBlock *Block2,
                            const llvm::Twine &Name = "") {
  assert(Addr1.getType() == Addr2.getType());
  llvm::PHINode *PHI = CGF.Builder.CreatePHI(Addr1.getType(), 2, Name);
  PHI->addIncoming(Addr1.getPointer(), Block1);
  PHI->addIncoming(Addr2.getPointer(), Block2);
  CharUnits Align = std::min(Addr1.getAlignment(), Addr2.getAlignment());
  return Address(PHI, Align);
}

TargetCodeGenInfo::~TargetCodeGenInfo() { delete Info; }

// If someone can figure out a general rule for this, that would be great.
// It's probably just doomed to be platform-dependent, though.
unsigned TargetCodeGenInfo::getSizeOfUnwindException() const {
  // Verified for:
  //   x86-64     FreeBSD, Linux, Darwin
  //   x86-32     FreeBSD, Linux, Darwin
  //   PowerPC    Linux, Darwin
  //   ARM        Darwin (*not* EABI)
  //   AArch64    Linux
  return 32;
}

bool TargetCodeGenInfo::isNoProtoCallVariadic(const CallArgList &args,
                                     const FunctionNoProtoType *fnType) const {
  // The following conventions are known to require this to be false:
  //   x86_stdcall
  //   MIPS
  // For everything else, we just prefer false unless we opt out.
  return false;
}

void
TargetCodeGenInfo::getDependentLibraryOption(llvm::StringRef Lib,
                                             llvm::SmallString<24> &Opt) const {
  // This assumes the user is passing a library name like "rt" instead of a
  // filename like "librt.a/so", and that they don't care whether it's static or
  // dynamic.
  Opt = "-l";
  Opt += Lib;
}

unsigned TargetCodeGenInfo::getOpenCLKernelCallingConv() const {
  // OpenCL kernels are called via an explicit runtime API with arguments
  // set with clSetKernelArg(), not as normal sub-functions.
  // Return SPIR_KERNEL by default as the kernel calling convention to
  // ensure the fingerprint is fixed such way that each OpenCL argument
  // gets one matching argument in the produced kernel function argument
  // list to enable feasible implementation of clSetKernelArg() with
  // aggregates etc. In case we would use the default C calling conv here,
  // clSetKernelArg() might break depending on the target-specific
  // conventions; different targets might split structs passed as values
  // to multiple function arguments etc.
  return llvm::CallingConv::SPIR_KERNEL;
}

llvm::Constant *TargetCodeGenInfo::getNullPointer(const CodeGen::CodeGenModule &CGM,
    llvm::PointerType *T, QualType QT) const {
  return llvm::ConstantPointerNull::get(T);
}

LangAS TargetCodeGenInfo::getGlobalVarAddressSpace(CodeGenModule &CGM,
                                                   const VarDecl *D) const {
  assert(!CGM.getLangOpts().OpenCL &&
         !(CGM.getLangOpts().CUDA && CGM.getLangOpts().CUDAIsDevice) &&
         "Address space agnostic languages only");
  return D ? D->getType().getAddressSpace() : LangAS::Default;
}

llvm::Value *TargetCodeGenInfo::performAddrSpaceCast(
    CodeGen::CodeGenFunction &CGF, llvm::Value *Src, LangAS SrcAddr,
    LangAS DestAddr, llvm::Type *DestTy, bool isNonNull) const {
  // Since target may map different address spaces in AST to the same address
  // space, an address space conversion may end up as a bitcast.
  if (auto *C = dyn_cast<llvm::Constant>(Src))
    return performAddrSpaceCast(CGF.CGM, C, SrcAddr, DestAddr, DestTy);
  return CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(Src, DestTy);
}

llvm::Constant *
TargetCodeGenInfo::performAddrSpaceCast(CodeGenModule &CGM, llvm::Constant *Src,
                                        LangAS SrcAddr, LangAS DestAddr,
                                        llvm::Type *DestTy) const {
  // Since target may map different address spaces in AST to the same address
  // space, an address space conversion may end up as a bitcast.
  return llvm::ConstantExpr::getPointerCast(Src, DestTy);
}

llvm::SyncScope::ID
TargetCodeGenInfo::getLLVMSyncScopeID(SyncScope S, llvm::LLVMContext &C) const {
  return C.getOrInsertSyncScopeID(""); /* default sync scope */
}

static bool isEmptyRecord(ASTContext &Context, QualType T, bool AllowArrays);

/// isEmptyField - Return true iff a the field is "empty", that is it
/// is an unnamed bit-field or an (array of) empty record(s).
static bool isEmptyField(ASTContext &Context, const FieldDecl *FD,
                         bool AllowArrays) {
  if (FD->isUnnamedBitfield())
    return true;

  QualType FT = FD->getType();

  // Constant arrays of empty records count as empty, strip them off.
  // Constant arrays of zero length always count as empty.
  if (AllowArrays)
    while (const ConstantArrayType *AT = Context.getAsConstantArrayType(FT)) {
      if (AT->getSize() == 0)
        return true;
      FT = AT->getElementType();
    }

  const RecordType *RT = FT->getAs<RecordType>();
  if (!RT)
    return false;

  // C++ record fields are never empty, at least in the Itanium ABI.
  //
  // FIXME: We should use a predicate for whether this behavior is true in the
  // current ABI.
  if (isa<CXXRecordDecl>(RT->getDecl()))
    return false;

  return isEmptyRecord(Context, FT, AllowArrays);
}

/// isEmptyRecord - Return true iff a structure contains only empty
/// fields. Note that a structure with a flexible array member is not
/// considered empty.
static bool isEmptyRecord(ASTContext &Context, QualType T, bool AllowArrays) {
  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return false;
  const RecordDecl *RD = RT->getDecl();
  if (RD->hasFlexibleArrayMember())
    return false;

  // If this is a C++ record, check the bases first.
  if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD))
    for (const auto &I : CXXRD->bases())
      if (!isEmptyRecord(Context, I.getType(), true))
        return false;

  for (const auto *I : RD->fields())
    if (!isEmptyField(Context, I, AllowArrays))
      return false;
  return true;
}

/// isSingleElementStruct - Determine if a structure is a "single
/// element struct", i.e. it has exactly one non-empty field or
/// exactly one field which is itself a single element
/// struct. Structures with flexible array members are never
/// considered single element structs.
///
/// \return The field declaration for the single non-empty field, if
/// it exists.
static const Type *isSingleElementStruct(QualType T, ASTContext &Context) {
  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return nullptr;

  const RecordDecl *RD = RT->getDecl();
  if (RD->hasFlexibleArrayMember())
    return nullptr;

  const Type *Found = nullptr;

  // If this is a C++ record, check the bases first.
  if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
    for (const auto &I : CXXRD->bases()) {
      // Ignore empty records.
      if (isEmptyRecord(Context, I.getType(), true))
        continue;

      // If we already found an element then this isn't a single-element struct.
      if (Found)
        return nullptr;

      // If this is non-empty and not a single element struct, the composite
      // cannot be a single element struct.
      Found = isSingleElementStruct(I.getType(), Context);
      if (!Found)
        return nullptr;
    }
  }

  // Check for single element.
  for (const auto *FD : RD->fields()) {
    QualType FT = FD->getType();

    // Ignore empty fields.
    if (isEmptyField(Context, FD, true))
      continue;

    // If we already found an element then this isn't a single-element
    // struct.
    if (Found)
      return nullptr;

    // Treat single element arrays as the element.
    while (const ConstantArrayType *AT = Context.getAsConstantArrayType(FT)) {
      if (AT->getSize().getZExtValue() != 1)
        break;
      FT = AT->getElementType();
    }

    if (!isAggregateTypeForABI(FT)) {
      Found = FT.getTypePtr();
    } else {
      Found = isSingleElementStruct(FT, Context);
      if (!Found)
        return nullptr;
    }
  }

  // We don't consider a struct a single-element struct if it has
  // padding beyond the element type.
  if (Found && Context.getTypeSize(Found) != Context.getTypeSize(T))
    return nullptr;

  return Found;
}

namespace {
Address EmitVAArgInstr(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                       const ABIArgInfo &AI) {
  // This default implementation defers to the llvm backend's va_arg
  // instruction. It can handle only passing arguments directly
  // (typically only handled in the backend for primitive types), or
  // aggregates passed indirectly by pointer (NOTE: if the "byval"
  // flag has ABI impact in the callee, this implementation cannot
  // work.)

  // Only a few cases are covered here at the moment -- those needed
  // by the default abi.
  llvm::Value *Val;

  if (AI.isIndirect()) {
    assert(!AI.getPaddingType() &&
           "Unexpected PaddingType seen in arginfo in generic VAArg emitter!");
    assert(
        !AI.getIndirectRealign() &&
        "Unexpected IndirectRealign seen in arginfo in generic VAArg emitter!");

    auto TyInfo = CGF.getContext().getTypeInfoInChars(Ty);
    CharUnits TyAlignForABI = TyInfo.second;

    llvm::Type *BaseTy =
        llvm::PointerType::getUnqual(CGF.ConvertTypeForMem(Ty));
    llvm::Value *Addr =
        CGF.Builder.CreateVAArg(VAListAddr.getPointer(), BaseTy);
    return Address(Addr, TyAlignForABI);
  } else {
    assert((AI.isDirect() || AI.isExtend()) &&
           "Unexpected ArgInfo Kind in generic VAArg emitter!");

    assert(!AI.getInReg() &&
           "Unexpected InReg seen in arginfo in generic VAArg emitter!");
    assert(!AI.getPaddingType() &&
           "Unexpected PaddingType seen in arginfo in generic VAArg emitter!");
    assert(!AI.getDirectOffset() &&
           "Unexpected DirectOffset seen in arginfo in generic VAArg emitter!");
    assert(!AI.getCoerceToType() &&
           "Unexpected CoerceToType seen in arginfo in generic VAArg emitter!");

    Address Temp = CGF.CreateMemTemp(Ty, "varet");
    Val = CGF.Builder.CreateVAArg(VAListAddr.getPointer(), CGF.ConvertType(Ty));
    CGF.Builder.CreateStore(Val, Temp);
    return Temp;
  }
}

/// DefaultABIInfo - The default implementation for ABI specific
/// details. This implementation provides information which results in
/// self-consistent and sensible LLVM IR generation, but does not
/// conform to any particular ABI.
class DefaultABIInfo : public ABIInfo {
public:
  DefaultABIInfo(CodeGen::CodeGenTypes &CGT) : ABIInfo(CGT) {}

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType RetTy) const;

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type);
  }

  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override {
    return EmitVAArgInstr(CGF, VAListAddr, Ty, classifyArgumentType(Ty));
  }
};

class DefaultTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  DefaultTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT)
    : TargetCodeGenInfo(new DefaultABIInfo(CGT)) {}
};

ABIArgInfo DefaultABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (isAggregateTypeForABI(Ty)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // passed by value.
    if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

    return getNaturalAlignIndirect(Ty);
  }

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  return (Ty->isPromotableIntegerType() ? ABIArgInfo::getExtend(Ty)
                                        : ABIArgInfo::getDirect());
}

ABIArgInfo DefaultABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  if (isAggregateTypeForABI(RetTy))
    return getNaturalAlignIndirect(RetTy);

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
    RetTy = EnumTy->getDecl()->getIntegerType();

  return (RetTy->isPromotableIntegerType() ? ABIArgInfo::getExtend(RetTy)
                                           : ABIArgInfo::getDirect());
}

//===----------------------------------------------------------------------===//
// WebAssembly ABI Implementation
//
// This is a very simple ABI that relies a lot on DefaultABIInfo.
//===----------------------------------------------------------------------===//

class WebAssemblyABIInfo final : public SwiftABIInfo {
  DefaultABIInfo defaultInfo;

public:
  explicit WebAssemblyABIInfo(CodeGen::CodeGenTypes &CGT)
      : SwiftABIInfo(CGT), defaultInfo(CGT) {}

private:
  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType Ty) const;

  // DefaultABIInfo's classifyReturnType and classifyArgumentType are
  // non-virtual, but computeInfo and EmitVAArg are virtual, so we
  // overload them.
  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &Arg : FI.arguments())
      Arg.info = classifyArgumentType(Arg.type);
  }

  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;

  bool shouldPassIndirectlyForSwift(ArrayRef<llvm::Type*> scalars,
                                    bool asReturnValue) const override {
    return occupiesMoreThan(CGT, scalars, /*total*/ 4);
  }

  bool isSwiftErrorInRegister() const override {
    return false;
  }
};

class WebAssemblyTargetCodeGenInfo final : public TargetCodeGenInfo {
public:
  explicit WebAssemblyTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT)
      : TargetCodeGenInfo(new WebAssemblyABIInfo(CGT)) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    TargetCodeGenInfo::setTargetAttributes(D, GV, CGM);
    if (const auto *FD = dyn_cast_or_null<FunctionDecl>(D)) {
      if (const auto *Attr = FD->getAttr<WebAssemblyImportModuleAttr>()) {
        llvm::Function *Fn = cast<llvm::Function>(GV);
        llvm::AttrBuilder B;
        B.addAttribute("wasm-import-module", Attr->getImportModule());
        Fn->addAttributes(llvm::AttributeList::FunctionIndex, B);
      }
      if (const auto *Attr = FD->getAttr<WebAssemblyImportNameAttr>()) {
        llvm::Function *Fn = cast<llvm::Function>(GV);
        llvm::AttrBuilder B;
        B.addAttribute("wasm-import-name", Attr->getImportName());
        Fn->addAttributes(llvm::AttributeList::FunctionIndex, B);
      }
    }

    if (auto *FD = dyn_cast_or_null<FunctionDecl>(D)) {
      llvm::Function *Fn = cast<llvm::Function>(GV);
      if (!FD->doesThisDeclarationHaveABody() && !FD->hasPrototype())
        Fn->addFnAttr("no-prototype");
    }
  }
};

/// Classify argument of given type \p Ty.
ABIArgInfo WebAssemblyABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (isAggregateTypeForABI(Ty)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // passed by value.
    if (auto RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);
    // Ignore empty structs/unions.
    if (isEmptyRecord(getContext(), Ty, true))
      return ABIArgInfo::getIgnore();
    // Lower single-element structs to just pass a regular value. TODO: We
    // could do reasonable-size multiple-element structs too, using getExpand(),
    // though watch out for things like bitfields.
    if (const Type *SeltTy = isSingleElementStruct(Ty, getContext()))
      return ABIArgInfo::getDirect(CGT.ConvertType(QualType(SeltTy, 0)));
  }

  // Otherwise just do the default thing.
  return defaultInfo.classifyArgumentType(Ty);
}

ABIArgInfo WebAssemblyABIInfo::classifyReturnType(QualType RetTy) const {
  if (isAggregateTypeForABI(RetTy)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // returned by value.
    if (!getRecordArgABI(RetTy, getCXXABI())) {
      // Ignore empty structs/unions.
      if (isEmptyRecord(getContext(), RetTy, true))
        return ABIArgInfo::getIgnore();
      // Lower single-element structs to just return a regular value. TODO: We
      // could do reasonable-size multiple-element structs too, using
      // ABIArgInfo::getDirect().
      if (const Type *SeltTy = isSingleElementStruct(RetTy, getContext()))
        return ABIArgInfo::getDirect(CGT.ConvertType(QualType(SeltTy, 0)));
    }
  }

  // Otherwise just do the default thing.
  return defaultInfo.classifyReturnType(RetTy);
}

Address WebAssemblyABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                      QualType Ty) const {
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*Indirect=*/ false,
                          getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(4),
                          /*AllowHigherAlign=*/ true);
}

//===----------------------------------------------------------------------===//
// le32/PNaCl bitcode ABI Implementation
//
// This is a simplified version of the x86_32 ABI.  Arguments and return values
// are always passed on the stack.
//===----------------------------------------------------------------------===//

class PNaClABIInfo : public ABIInfo {
 public:
  PNaClABIInfo(CodeGen::CodeGenTypes &CGT) : ABIInfo(CGT) {}

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType RetTy) const;

  void computeInfo(CGFunctionInfo &FI) const override;
  Address EmitVAArg(CodeGenFunction &CGF,
                    Address VAListAddr, QualType Ty) const override;
};

class PNaClTargetCodeGenInfo : public TargetCodeGenInfo {
 public:
  PNaClTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT)
    : TargetCodeGenInfo(new PNaClABIInfo(CGT)) {}
};

void PNaClABIInfo::computeInfo(CGFunctionInfo &FI) const {
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

  for (auto &I : FI.arguments())
    I.info = classifyArgumentType(I.type);
}

Address PNaClABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                QualType Ty) const {
  // The PNaCL ABI is a bit odd, in that varargs don't use normal
  // function classification. Structs get passed directly for varargs
  // functions, through a rewriting transform in
  // pnacl-llvm/lib/Transforms/NaCl/ExpandVarArgs.cpp, which allows
  // this target to actually support a va_arg instructions with an
  // aggregate type, unlike other targets.
  return EmitVAArgInstr(CGF, VAListAddr, Ty, ABIArgInfo::getDirect());
}

/// Classify argument of given type \p Ty.
ABIArgInfo PNaClABIInfo::classifyArgumentType(QualType Ty) const {
  if (isAggregateTypeForABI(Ty)) {
    if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);
    return getNaturalAlignIndirect(Ty);
  } else if (const EnumType *EnumTy = Ty->getAs<EnumType>()) {
    // Treat an enum type as its underlying type.
    Ty = EnumTy->getDecl()->getIntegerType();
  } else if (Ty->isFloatingType()) {
    // Floating-point types don't go inreg.
    return ABIArgInfo::getDirect();
  }

  return (Ty->isPromotableIntegerType() ? ABIArgInfo::getExtend(Ty)
                                        : ABIArgInfo::getDirect());
}

ABIArgInfo PNaClABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  // In the PNaCl ABI we always return records/structures on the stack.
  if (isAggregateTypeForABI(RetTy))
    return getNaturalAlignIndirect(RetTy);

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
    RetTy = EnumTy->getDecl()->getIntegerType();

  return (RetTy->isPromotableIntegerType() ? ABIArgInfo::getExtend(RetTy)
                                           : ABIArgInfo::getDirect());
}

/// IsX86_MMXType - Return true if this is an MMX type.
bool IsX86_MMXType(llvm::Type *IRType) {
  // Return true if the type is an MMX type <2 x i32>, <4 x i16>, or <8 x i8>.
  return IRType->isVectorTy() && IRType->getPrimitiveSizeInBits() == 64 &&
    cast<llvm::VectorType>(IRType)->getElementType()->isIntegerTy() &&
    IRType->getScalarSizeInBits() != 64;
}

static llvm::Type* X86AdjustInlineAsmType(CodeGen::CodeGenFunction &CGF,
                                          StringRef Constraint,
                                          llvm::Type* Ty) {
  bool IsMMXCons = llvm::StringSwitch<bool>(Constraint)
                     .Cases("y", "&y", "^Ym", true)
                     .Default(false);
  if (IsMMXCons && Ty->isVectorTy()) {
    if (cast<llvm::VectorType>(Ty)->getBitWidth() != 64) {
      // Invalid MMX constraint
      return nullptr;
    }

    return llvm::Type::getX86_MMXTy(CGF.getLLVMContext());
  }

  // No operation needed
  return Ty;
}

/// Returns true if this type can be passed in SSE registers with the
/// X86_VectorCall calling convention. Shared between x86_32 and x86_64.
static bool isX86VectorTypeForVectorCall(ASTContext &Context, QualType Ty) {
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    if (BT->isFloatingPoint() && BT->getKind() != BuiltinType::Half) {
      if (BT->getKind() == BuiltinType::LongDouble) {
        if (&Context.getTargetInfo().getLongDoubleFormat() ==
            &llvm::APFloat::x87DoubleExtended())
          return false;
      }
      return true;
    }
  } else if (const VectorType *VT = Ty->getAs<VectorType>()) {
    // vectorcall can pass XMM, YMM, and ZMM vectors. We don't pass SSE1 MMX
    // registers specially.
    unsigned VecSize = Context.getTypeSize(VT);
    if (VecSize == 128 || VecSize == 256 || VecSize == 512)
      return true;
  }
  return false;
}

/// Returns true if this aggregate is small enough to be passed in SSE registers
/// in the X86_VectorCall calling convention. Shared between x86_32 and x86_64.
static bool isX86VectorCallAggregateSmallEnough(uint64_t NumMembers) {
  return NumMembers <= 4;
}

/// Returns a Homogeneous Vector Aggregate ABIArgInfo, used in X86.
static ABIArgInfo getDirectX86Hva(llvm::Type* T = nullptr) {
  auto AI = ABIArgInfo::getDirect(T);
  AI.setInReg(true);
  AI.setCanBeFlattened(false);
  return AI;
}

//===----------------------------------------------------------------------===//
// X86-32 ABI Implementation
//===----------------------------------------------------------------------===//

/// Similar to llvm::CCState, but for Clang.
struct CCState {
  CCState(unsigned CC) : CC(CC), FreeRegs(0), FreeSSERegs(0) {}

  unsigned CC;
  unsigned FreeRegs;
  unsigned FreeSSERegs;
};

enum {
  // Vectorcall only allows the first 6 parameters to be passed in registers.
  VectorcallMaxParamNumAsReg = 6
};

/// X86_32ABIInfo - The X86-32 ABI information.
class X86_32ABIInfo : public SwiftABIInfo {
  enum Class {
    Integer,
    Float
  };

  static const unsigned MinABIStackAlignInBytes = 4;

  bool IsDarwinVectorABI;
  bool IsRetSmallStructInRegABI;
  bool IsWin32StructABI;
  bool IsSoftFloatABI;
  bool IsMCUABI;
  unsigned DefaultNumRegisterParameters;

  static bool isRegisterSize(unsigned Size) {
    return (Size == 8 || Size == 16 || Size == 32 || Size == 64);
  }

  bool isHomogeneousAggregateBaseType(QualType Ty) const override {
    // FIXME: Assumes vectorcall is in use.
    return isX86VectorTypeForVectorCall(getContext(), Ty);
  }

  bool isHomogeneousAggregateSmallEnough(const Type *Ty,
                                         uint64_t NumMembers) const override {
    // FIXME: Assumes vectorcall is in use.
    return isX86VectorCallAggregateSmallEnough(NumMembers);
  }

  bool shouldReturnTypeInRegister(QualType Ty, ASTContext &Context) const;

  /// getIndirectResult - Give a source type \arg Ty, return a suitable result
  /// such that the argument will be passed in memory.
  ABIArgInfo getIndirectResult(QualType Ty, bool ByVal, CCState &State) const;

  ABIArgInfo getIndirectReturnResult(QualType Ty, CCState &State) const;

  /// Return the alignment to use for the given type on the stack.
  unsigned getTypeStackAlignInBytes(QualType Ty, unsigned Align) const;

  Class classify(QualType Ty) const;
  ABIArgInfo classifyReturnType(QualType RetTy, CCState &State) const;
  ABIArgInfo classifyArgumentType(QualType RetTy, CCState &State) const;

  /// Updates the number of available free registers, returns
  /// true if any registers were allocated.
  bool updateFreeRegs(QualType Ty, CCState &State) const;

  bool shouldAggregateUseDirect(QualType Ty, CCState &State, bool &InReg,
                                bool &NeedsPadding) const;
  bool shouldPrimitiveUseInReg(QualType Ty, CCState &State) const;

  bool canExpandIndirectArgument(QualType Ty) const;

  /// Rewrite the function info so that all memory arguments use
  /// inalloca.
  void rewriteWithInAlloca(CGFunctionInfo &FI) const;

  void addFieldToArgStruct(SmallVector<llvm::Type *, 6> &FrameFields,
                           CharUnits &StackOffset, ABIArgInfo &Info,
                           QualType Type) const;
  void computeVectorCallArgs(CGFunctionInfo &FI, CCState &State,
                             bool &UsedInAlloca) const;

public:

  void computeInfo(CGFunctionInfo &FI) const override;
  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;

  X86_32ABIInfo(CodeGen::CodeGenTypes &CGT, bool DarwinVectorABI,
                bool RetSmallStructInRegABI, bool Win32StructABI,
                unsigned NumRegisterParameters, bool SoftFloatABI)
    : SwiftABIInfo(CGT), IsDarwinVectorABI(DarwinVectorABI),
      IsRetSmallStructInRegABI(RetSmallStructInRegABI),
      IsWin32StructABI(Win32StructABI),
      IsSoftFloatABI(SoftFloatABI),
      IsMCUABI(CGT.getTarget().getTriple().isOSIAMCU()),
      DefaultNumRegisterParameters(NumRegisterParameters) {}

  bool shouldPassIndirectlyForSwift(ArrayRef<llvm::Type*> scalars,
                                    bool asReturnValue) const override {
    // LLVM's x86-32 lowering currently only assigns up to three
    // integer registers and three fp registers.  Oddly, it'll use up to
    // four vector registers for vectors, but those can overlap with the
    // scalar registers.
    return occupiesMoreThan(CGT, scalars, /*total*/ 3);
  }

  bool isSwiftErrorInRegister() const override {
    // x86-32 lowering does not support passing swifterror in a register.
    return false;
  }
};

class X86_32TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  X86_32TargetCodeGenInfo(CodeGen::CodeGenTypes &CGT, bool DarwinVectorABI,
                          bool RetSmallStructInRegABI, bool Win32StructABI,
                          unsigned NumRegisterParameters, bool SoftFloatABI)
      : TargetCodeGenInfo(new X86_32ABIInfo(
            CGT, DarwinVectorABI, RetSmallStructInRegABI, Win32StructABI,
            NumRegisterParameters, SoftFloatABI)) {}

  static bool isStructReturnInRegABI(
      const llvm::Triple &Triple, const CodeGenOptions &Opts);

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override;

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &CGM) const override {
    // Darwin uses different dwarf register numbers for EH.
    if (CGM.getTarget().getTriple().isOSDarwin()) return 5;
    return 4;
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override;

  llvm::Type* adjustInlineAsmType(CodeGen::CodeGenFunction &CGF,
                                  StringRef Constraint,
                                  llvm::Type* Ty) const override {
    return X86AdjustInlineAsmType(CGF, Constraint, Ty);
  }

  void addReturnRegisterOutputs(CodeGenFunction &CGF, LValue ReturnValue,
                                std::string &Constraints,
                                std::vector<llvm::Type *> &ResultRegTypes,
                                std::vector<llvm::Type *> &ResultTruncRegTypes,
                                std::vector<LValue> &ResultRegDests,
                                std::string &AsmString,
                                unsigned NumOutputs) const override;

  llvm::Constant *
  getUBSanFunctionSignature(CodeGen::CodeGenModule &CGM) const override {
    unsigned Sig = (0xeb << 0) |  // jmp rel8
                   (0x06 << 8) |  //           .+0x08
                   ('v' << 16) |
                   ('2' << 24);
    return llvm::ConstantInt::get(CGM.Int32Ty, Sig);
  }

  StringRef getARCRetainAutoreleasedReturnValueMarker() const override {
    return "movl\t%ebp, %ebp"
           "\t\t// marker for objc_retainAutoreleaseReturnValue";
  }
};

}

/// Rewrite input constraint references after adding some output constraints.
/// In the case where there is one output and one input and we add one output,
/// we need to replace all operand references greater than or equal to 1:
///     mov $0, $1
///     mov eax, $1
/// The result will be:
///     mov $0, $2
///     mov eax, $2
static void rewriteInputConstraintReferences(unsigned FirstIn,
                                             unsigned NumNewOuts,
                                             std::string &AsmString) {
  std::string Buf;
  llvm::raw_string_ostream OS(Buf);
  size_t Pos = 0;
  while (Pos < AsmString.size()) {
    size_t DollarStart = AsmString.find('$', Pos);
    if (DollarStart == std::string::npos)
      DollarStart = AsmString.size();
    size_t DollarEnd = AsmString.find_first_not_of('$', DollarStart);
    if (DollarEnd == std::string::npos)
      DollarEnd = AsmString.size();
    OS << StringRef(&AsmString[Pos], DollarEnd - Pos);
    Pos = DollarEnd;
    size_t NumDollars = DollarEnd - DollarStart;
    if (NumDollars % 2 != 0 && Pos < AsmString.size()) {
      // We have an operand reference.
      size_t DigitStart = Pos;
      size_t DigitEnd = AsmString.find_first_not_of("0123456789", DigitStart);
      if (DigitEnd == std::string::npos)
        DigitEnd = AsmString.size();
      StringRef OperandStr(&AsmString[DigitStart], DigitEnd - DigitStart);
      unsigned OperandIndex;
      if (!OperandStr.getAsInteger(10, OperandIndex)) {
        if (OperandIndex >= FirstIn)
          OperandIndex += NumNewOuts;
        OS << OperandIndex;
      } else {
        OS << OperandStr;
      }
      Pos = DigitEnd;
    }
  }
  AsmString = std::move(OS.str());
}

/// Add output constraints for EAX:EDX because they are return registers.
void X86_32TargetCodeGenInfo::addReturnRegisterOutputs(
    CodeGenFunction &CGF, LValue ReturnSlot, std::string &Constraints,
    std::vector<llvm::Type *> &ResultRegTypes,
    std::vector<llvm::Type *> &ResultTruncRegTypes,
    std::vector<LValue> &ResultRegDests, std::string &AsmString,
    unsigned NumOutputs) const {
  uint64_t RetWidth = CGF.getContext().getTypeSize(ReturnSlot.getType());

  // Use the EAX constraint if the width is 32 or smaller and EAX:EDX if it is
  // larger.
  if (!Constraints.empty())
    Constraints += ',';
  if (RetWidth <= 32) {
    Constraints += "={eax}";
    ResultRegTypes.push_back(CGF.Int32Ty);
  } else {
    // Use the 'A' constraint for EAX:EDX.
    Constraints += "=A";
    ResultRegTypes.push_back(CGF.Int64Ty);
  }

  // Truncate EAX or EAX:EDX to an integer of the appropriate size.
  llvm::Type *CoerceTy = llvm::IntegerType::get(CGF.getLLVMContext(), RetWidth);
  ResultTruncRegTypes.push_back(CoerceTy);

  // Coerce the integer by bitcasting the return slot pointer.
  ReturnSlot.setAddress(CGF.Builder.CreateBitCast(ReturnSlot.getAddress(),
                                                  CoerceTy->getPointerTo()));
  ResultRegDests.push_back(ReturnSlot);

  rewriteInputConstraintReferences(NumOutputs, 1, AsmString);
}

/// shouldReturnTypeInRegister - Determine if the given type should be
/// returned in a register (for the Darwin and MCU ABI).
bool X86_32ABIInfo::shouldReturnTypeInRegister(QualType Ty,
                                               ASTContext &Context) const {
  uint64_t Size = Context.getTypeSize(Ty);

  // For i386, type must be register sized.
  // For the MCU ABI, it only needs to be <= 8-byte
  if ((IsMCUABI && Size > 64) || (!IsMCUABI && !isRegisterSize(Size)))
   return false;

  if (Ty->isVectorType()) {
    // 64- and 128- bit vectors inside structures are not returned in
    // registers.
    if (Size == 64 || Size == 128)
      return false;

    return true;
  }

  // If this is a builtin, pointer, enum, complex type, member pointer, or
  // member function pointer it is ok.
  if (Ty->getAs<BuiltinType>() || Ty->hasPointerRepresentation() ||
      Ty->isAnyComplexType() || Ty->isEnumeralType() ||
      Ty->isBlockPointerType() || Ty->isMemberPointerType())
    return true;

  // Arrays are treated like records.
  if (const ConstantArrayType *AT = Context.getAsConstantArrayType(Ty))
    return shouldReturnTypeInRegister(AT->getElementType(), Context);

  // Otherwise, it must be a record type.
  const RecordType *RT = Ty->getAs<RecordType>();
  if (!RT) return false;

  // FIXME: Traverse bases here too.

  // Structure types are passed in register if all fields would be
  // passed in a register.
  for (const auto *FD : RT->getDecl()->fields()) {
    // Empty fields are ignored.
    if (isEmptyField(Context, FD, true))
      continue;

    // Check fields recursively.
    if (!shouldReturnTypeInRegister(FD->getType(), Context))
      return false;
  }
  return true;
}

static bool is32Or64BitBasicType(QualType Ty, ASTContext &Context) {
  // Treat complex types as the element type.
  if (const ComplexType *CTy = Ty->getAs<ComplexType>())
    Ty = CTy->getElementType();

  // Check for a type which we know has a simple scalar argument-passing
  // convention without any padding.  (We're specifically looking for 32
  // and 64-bit integer and integer-equivalents, float, and double.)
  if (!Ty->getAs<BuiltinType>() && !Ty->hasPointerRepresentation() &&
      !Ty->isEnumeralType() && !Ty->isBlockPointerType())
    return false;

  uint64_t Size = Context.getTypeSize(Ty);
  return Size == 32 || Size == 64;
}

static bool addFieldSizes(ASTContext &Context, const RecordDecl *RD,
                          uint64_t &Size) {
  for (const auto *FD : RD->fields()) {
    // Scalar arguments on the stack get 4 byte alignment on x86. If the
    // argument is smaller than 32-bits, expanding the struct will create
    // alignment padding.
    if (!is32Or64BitBasicType(FD->getType(), Context))
      return false;

    // FIXME: Reject bit-fields wholesale; there are two problems, we don't know
    // how to expand them yet, and the predicate for telling if a bitfield still
    // counts as "basic" is more complicated than what we were doing previously.
    if (FD->isBitField())
      return false;

    Size += Context.getTypeSize(FD->getType());
  }
  return true;
}

static bool addBaseAndFieldSizes(ASTContext &Context, const CXXRecordDecl *RD,
                                 uint64_t &Size) {
  // Don't do this if there are any non-empty bases.
  for (const CXXBaseSpecifier &Base : RD->bases()) {
    if (!addBaseAndFieldSizes(Context, Base.getType()->getAsCXXRecordDecl(),
                              Size))
      return false;
  }
  if (!addFieldSizes(Context, RD, Size))
    return false;
  return true;
}

/// Test whether an argument type which is to be passed indirectly (on the
/// stack) would have the equivalent layout if it was expanded into separate
/// arguments. If so, we prefer to do the latter to avoid inhibiting
/// optimizations.
bool X86_32ABIInfo::canExpandIndirectArgument(QualType Ty) const {
  // We can only expand structure types.
  const RecordType *RT = Ty->getAs<RecordType>();
  if (!RT)
    return false;
  const RecordDecl *RD = RT->getDecl();
  uint64_t Size = 0;
  if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
    if (!IsWin32StructABI) {
      // On non-Windows, we have to conservatively match our old bitcode
      // prototypes in order to be ABI-compatible at the bitcode level.
      if (!CXXRD->isCLike())
        return false;
    } else {
      // Don't do this for dynamic classes.
      if (CXXRD->isDynamicClass())
        return false;
    }
    if (!addBaseAndFieldSizes(getContext(), CXXRD, Size))
      return false;
  } else {
    if (!addFieldSizes(getContext(), RD, Size))
      return false;
  }

  // We can do this if there was no alignment padding.
  return Size == getContext().getTypeSize(Ty);
}

ABIArgInfo X86_32ABIInfo::getIndirectReturnResult(QualType RetTy, CCState &State) const {
  // If the return value is indirect, then the hidden argument is consuming one
  // integer register.
  if (State.FreeRegs) {
    --State.FreeRegs;
    if (!IsMCUABI)
      return getNaturalAlignIndirectInReg(RetTy);
  }
  return getNaturalAlignIndirect(RetTy, /*ByVal=*/false);
}

ABIArgInfo X86_32ABIInfo::classifyReturnType(QualType RetTy,
                                             CCState &State) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  const Type *Base = nullptr;
  uint64_t NumElts = 0;
  if ((State.CC == llvm::CallingConv::X86_VectorCall ||
       State.CC == llvm::CallingConv::X86_RegCall) &&
      isHomogeneousAggregate(RetTy, Base, NumElts)) {
    // The LLVM struct type for such an aggregate should lower properly.
    return ABIArgInfo::getDirect();
  }

  if (const VectorType *VT = RetTy->getAs<VectorType>()) {
    // On Darwin, some vectors are returned in registers.
    if (IsDarwinVectorABI) {
      uint64_t Size = getContext().getTypeSize(RetTy);

      // 128-bit vectors are a special case; they are returned in
      // registers and we need to make sure to pick a type the LLVM
      // backend will like.
      if (Size == 128)
        return ABIArgInfo::getDirect(llvm::VectorType::get(
                  llvm::Type::getInt64Ty(getVMContext()), 2));

      // Always return in register if it fits in a general purpose
      // register, or if it is 64 bits and has a single element.
      if ((Size == 8 || Size == 16 || Size == 32) ||
          (Size == 64 && VT->getNumElements() == 1))
        return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(),
                                                            Size));

      return getIndirectReturnResult(RetTy, State);
    }

    return ABIArgInfo::getDirect();
  }

  if (isAggregateTypeForABI(RetTy)) {
    if (const RecordType *RT = RetTy->getAs<RecordType>()) {
      // Structures with flexible arrays are always indirect.
      if (RT->getDecl()->hasFlexibleArrayMember())
        return getIndirectReturnResult(RetTy, State);
    }

    // If specified, structs and unions are always indirect.
    if (!IsRetSmallStructInRegABI && !RetTy->isAnyComplexType())
      return getIndirectReturnResult(RetTy, State);

    // Ignore empty structs/unions.
    if (isEmptyRecord(getContext(), RetTy, true))
      return ABIArgInfo::getIgnore();

    // Small structures which are register sized are generally returned
    // in a register.
    if (shouldReturnTypeInRegister(RetTy, getContext())) {
      uint64_t Size = getContext().getTypeSize(RetTy);

      // As a special-case, if the struct is a "single-element" struct, and
      // the field is of type "float" or "double", return it in a
      // floating-point register. (MSVC does not apply this special case.)
      // We apply a similar transformation for pointer types to improve the
      // quality of the generated IR.
      if (const Type *SeltTy = isSingleElementStruct(RetTy, getContext()))
        if ((!IsWin32StructABI && SeltTy->isRealFloatingType())
            || SeltTy->hasPointerRepresentation())
          return ABIArgInfo::getDirect(CGT.ConvertType(QualType(SeltTy, 0)));

      // FIXME: We should be able to narrow this integer in cases with dead
      // padding.
      return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(),Size));
    }

    return getIndirectReturnResult(RetTy, State);
  }

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
    RetTy = EnumTy->getDecl()->getIntegerType();

  return (RetTy->isPromotableIntegerType() ? ABIArgInfo::getExtend(RetTy)
                                           : ABIArgInfo::getDirect());
}

static bool isSSEVectorType(ASTContext &Context, QualType Ty) {
  return Ty->getAs<VectorType>() && Context.getTypeSize(Ty) == 128;
}

static bool isRecordWithSSEVectorType(ASTContext &Context, QualType Ty) {
  const RecordType *RT = Ty->getAs<RecordType>();
  if (!RT)
    return 0;
  const RecordDecl *RD = RT->getDecl();

  // If this is a C++ record, check the bases first.
  if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD))
    for (const auto &I : CXXRD->bases())
      if (!isRecordWithSSEVectorType(Context, I.getType()))
        return false;

  for (const auto *i : RD->fields()) {
    QualType FT = i->getType();

    if (isSSEVectorType(Context, FT))
      return true;

    if (isRecordWithSSEVectorType(Context, FT))
      return true;
  }

  return false;
}

unsigned X86_32ABIInfo::getTypeStackAlignInBytes(QualType Ty,
                                                 unsigned Align) const {
  // Otherwise, if the alignment is less than or equal to the minimum ABI
  // alignment, just use the default; the backend will handle this.
  if (Align <= MinABIStackAlignInBytes)
    return 0; // Use default alignment.

  // On non-Darwin, the stack type alignment is always 4.
  if (!IsDarwinVectorABI) {
    // Set explicit alignment, since we may need to realign the top.
    return MinABIStackAlignInBytes;
  }

  // Otherwise, if the type contains an SSE vector type, the alignment is 16.
  if (Align >= 16 && (isSSEVectorType(getContext(), Ty) ||
                      isRecordWithSSEVectorType(getContext(), Ty)))
    return 16;

  return MinABIStackAlignInBytes;
}

ABIArgInfo X86_32ABIInfo::getIndirectResult(QualType Ty, bool ByVal,
                                            CCState &State) const {
  if (!ByVal) {
    if (State.FreeRegs) {
      --State.FreeRegs; // Non-byval indirects just use one pointer.
      if (!IsMCUABI)
        return getNaturalAlignIndirectInReg(Ty);
    }
    return getNaturalAlignIndirect(Ty, false);
  }

  // Compute the byval alignment.
  unsigned TypeAlign = getContext().getTypeAlign(Ty) / 8;
  unsigned StackAlign = getTypeStackAlignInBytes(Ty, TypeAlign);
  if (StackAlign == 0)
    return ABIArgInfo::getIndirect(CharUnits::fromQuantity(4), /*ByVal=*/true);

  // If the stack alignment is less than the type alignment, realign the
  // argument.
  bool Realign = TypeAlign > StackAlign;
  return ABIArgInfo::getIndirect(CharUnits::fromQuantity(StackAlign),
                                 /*ByVal=*/true, Realign);
}

X86_32ABIInfo::Class X86_32ABIInfo::classify(QualType Ty) const {
  const Type *T = isSingleElementStruct(Ty, getContext());
  if (!T)
    T = Ty.getTypePtr();

  if (const BuiltinType *BT = T->getAs<BuiltinType>()) {
    BuiltinType::Kind K = BT->getKind();
    if (K == BuiltinType::Float || K == BuiltinType::Double)
      return Float;
  }
  return Integer;
}

bool X86_32ABIInfo::updateFreeRegs(QualType Ty, CCState &State) const {
  if (!IsSoftFloatABI) {
    Class C = classify(Ty);
    if (C == Float)
      return false;
  }

  unsigned Size = getContext().getTypeSize(Ty);
  unsigned SizeInRegs = (Size + 31) / 32;

  if (SizeInRegs == 0)
    return false;

  if (!IsMCUABI) {
    if (SizeInRegs > State.FreeRegs) {
      State.FreeRegs = 0;
      return false;
    }
  } else {
    // The MCU psABI allows passing parameters in-reg even if there are
    // earlier parameters that are passed on the stack. Also,
    // it does not allow passing >8-byte structs in-register,
    // even if there are 3 free registers available.
    if (SizeInRegs > State.FreeRegs || SizeInRegs > 2)
      return false;
  }

  State.FreeRegs -= SizeInRegs;
  return true;
}

bool X86_32ABIInfo::shouldAggregateUseDirect(QualType Ty, CCState &State,
                                             bool &InReg,
                                             bool &NeedsPadding) const {
  // On Windows, aggregates other than HFAs are never passed in registers, and
  // they do not consume register slots. Homogenous floating-point aggregates
  // (HFAs) have already been dealt with at this point.
  if (IsWin32StructABI && isAggregateTypeForABI(Ty))
    return false;

  NeedsPadding = false;
  InReg = !IsMCUABI;

  if (!updateFreeRegs(Ty, State))
    return false;

  if (IsMCUABI)
    return true;

  if (State.CC == llvm::CallingConv::X86_FastCall ||
      State.CC == llvm::CallingConv::X86_VectorCall ||
      State.CC == llvm::CallingConv::X86_RegCall) {
    if (getContext().getTypeSize(Ty) <= 32 && State.FreeRegs)
      NeedsPadding = true;

    return false;
  }

  return true;
}

bool X86_32ABIInfo::shouldPrimitiveUseInReg(QualType Ty, CCState &State) const {
  if (!updateFreeRegs(Ty, State))
    return false;

  if (IsMCUABI)
    return false;

  if (State.CC == llvm::CallingConv::X86_FastCall ||
      State.CC == llvm::CallingConv::X86_VectorCall ||
      State.CC == llvm::CallingConv::X86_RegCall) {
    if (getContext().getTypeSize(Ty) > 32)
      return false;

    return (Ty->isIntegralOrEnumerationType() || Ty->isPointerType() ||
        Ty->isReferenceType());
  }

  return true;
}

ABIArgInfo X86_32ABIInfo::classifyArgumentType(QualType Ty,
                                               CCState &State) const {
  // FIXME: Set alignment on indirect arguments.

  Ty = useFirstFieldIfTransparentUnion(Ty);

  // Check with the C++ ABI first.
  const RecordType *RT = Ty->getAs<RecordType>();
  if (RT) {
    CGCXXABI::RecordArgABI RAA = getRecordArgABI(RT, getCXXABI());
    if (RAA == CGCXXABI::RAA_Indirect) {
      return getIndirectResult(Ty, false, State);
    } else if (RAA == CGCXXABI::RAA_DirectInMemory) {
      // The field index doesn't matter, we'll fix it up later.
      return ABIArgInfo::getInAlloca(/*FieldIndex=*/0);
    }
  }

  // Regcall uses the concept of a homogenous vector aggregate, similar
  // to other targets.
  const Type *Base = nullptr;
  uint64_t NumElts = 0;
  if (State.CC == llvm::CallingConv::X86_RegCall &&
      isHomogeneousAggregate(Ty, Base, NumElts)) {

    if (State.FreeSSERegs >= NumElts) {
      State.FreeSSERegs -= NumElts;
      if (Ty->isBuiltinType() || Ty->isVectorType())
        return ABIArgInfo::getDirect();
      return ABIArgInfo::getExpand();
    }
    return getIndirectResult(Ty, /*ByVal=*/false, State);
  }

  if (isAggregateTypeForABI(Ty)) {
    // Structures with flexible arrays are always indirect.
    // FIXME: This should not be byval!
    if (RT && RT->getDecl()->hasFlexibleArrayMember())
      return getIndirectResult(Ty, true, State);

    // Ignore empty structs/unions on non-Windows.
    if (!IsWin32StructABI && isEmptyRecord(getContext(), Ty, true))
      return ABIArgInfo::getIgnore();

    llvm::LLVMContext &LLVMContext = getVMContext();
    llvm::IntegerType *Int32 = llvm::Type::getInt32Ty(LLVMContext);
    bool NeedsPadding = false;
    bool InReg;
    if (shouldAggregateUseDirect(Ty, State, InReg, NeedsPadding)) {
      unsigned SizeInRegs = (getContext().getTypeSize(Ty) + 31) / 32;
      SmallVector<llvm::Type*, 3> Elements(SizeInRegs, Int32);
      llvm::Type *Result = llvm::StructType::get(LLVMContext, Elements);
      if (InReg)
        return ABIArgInfo::getDirectInReg(Result);
      else
        return ABIArgInfo::getDirect(Result);
    }
    llvm::IntegerType *PaddingType = NeedsPadding ? Int32 : nullptr;

    // Expand small (<= 128-bit) record types when we know that the stack layout
    // of those arguments will match the struct. This is important because the
    // LLVM backend isn't smart enough to remove byval, which inhibits many
    // optimizations.
    // Don't do this for the MCU if there are still free integer registers
    // (see X86_64 ABI for full explanation).
    if (getContext().getTypeSize(Ty) <= 4 * 32 &&
        (!IsMCUABI || State.FreeRegs == 0) && canExpandIndirectArgument(Ty))
      return ABIArgInfo::getExpandWithPadding(
          State.CC == llvm::CallingConv::X86_FastCall ||
              State.CC == llvm::CallingConv::X86_VectorCall ||
              State.CC == llvm::CallingConv::X86_RegCall,
          PaddingType);

    return getIndirectResult(Ty, true, State);
  }

  if (const VectorType *VT = Ty->getAs<VectorType>()) {
    // On Darwin, some vectors are passed in memory, we handle this by passing
    // it as an i8/i16/i32/i64.
    if (IsDarwinVectorABI) {
      uint64_t Size = getContext().getTypeSize(Ty);
      if ((Size == 8 || Size == 16 || Size == 32) ||
          (Size == 64 && VT->getNumElements() == 1))
        return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(),
                                                            Size));
    }

    if (IsX86_MMXType(CGT.ConvertType(Ty)))
      return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(), 64));

    return ABIArgInfo::getDirect();
  }


  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  bool InReg = shouldPrimitiveUseInReg(Ty, State);

  if (Ty->isPromotableIntegerType()) {
    if (InReg)
      return ABIArgInfo::getExtendInReg(Ty);
    return ABIArgInfo::getExtend(Ty);
  }

  if (InReg)
    return ABIArgInfo::getDirectInReg();
  return ABIArgInfo::getDirect();
}

void X86_32ABIInfo::computeVectorCallArgs(CGFunctionInfo &FI, CCState &State,
                                          bool &UsedInAlloca) const {
  // Vectorcall x86 works subtly different than in x64, so the format is
  // a bit different than the x64 version.  First, all vector types (not HVAs)
  // are assigned, with the first 6 ending up in the YMM0-5 or XMM0-5 registers.
  // This differs from the x64 implementation, where the first 6 by INDEX get
  // registers.
  // After that, integers AND HVAs are assigned Left to Right in the same pass.
  // Integers are passed as ECX/EDX if one is available (in order).  HVAs will
  // first take up the remaining YMM/XMM registers. If insufficient registers
  // remain but an integer register (ECX/EDX) is available, it will be passed
  // in that, else, on the stack.
  for (auto &I : FI.arguments()) {
    // First pass do all the vector types.
    const Type *Base = nullptr;
    uint64_t NumElts = 0;
    const QualType& Ty = I.type;
    if ((Ty->isVectorType() || Ty->isBuiltinType()) &&
        isHomogeneousAggregate(Ty, Base, NumElts)) {
      if (State.FreeSSERegs >= NumElts) {
        State.FreeSSERegs -= NumElts;
        I.info = ABIArgInfo::getDirect();
      } else {
        I.info = classifyArgumentType(Ty, State);
      }
      UsedInAlloca |= (I.info.getKind() == ABIArgInfo::InAlloca);
    }
  }

  for (auto &I : FI.arguments()) {
    // Second pass, do the rest!
    const Type *Base = nullptr;
    uint64_t NumElts = 0;
    const QualType& Ty = I.type;
    bool IsHva = isHomogeneousAggregate(Ty, Base, NumElts);

    if (IsHva && !Ty->isVectorType() && !Ty->isBuiltinType()) {
      // Assign true HVAs (non vector/native FP types).
      if (State.FreeSSERegs >= NumElts) {
        State.FreeSSERegs -= NumElts;
        I.info = getDirectX86Hva();
      } else {
        I.info = getIndirectResult(Ty, /*ByVal=*/false, State);
      }
    } else if (!IsHva) {
      // Assign all Non-HVAs, so this will exclude Vector/FP args.
      I.info = classifyArgumentType(Ty, State);
      UsedInAlloca |= (I.info.getKind() == ABIArgInfo::InAlloca);
    }
  }
}

void X86_32ABIInfo::computeInfo(CGFunctionInfo &FI) const {
  CCState State(FI.getCallingConvention());
  if (IsMCUABI)
    State.FreeRegs = 3;
  else if (State.CC == llvm::CallingConv::X86_FastCall)
    State.FreeRegs = 2;
  else if (State.CC == llvm::CallingConv::X86_VectorCall) {
    State.FreeRegs = 2;
    State.FreeSSERegs = 6;
  } else if (FI.getHasRegParm())
    State.FreeRegs = FI.getRegParm();
  else if (State.CC == llvm::CallingConv::X86_RegCall) {
    State.FreeRegs = 5;
    State.FreeSSERegs = 8;
  } else
    State.FreeRegs = DefaultNumRegisterParameters;

  if (!::classifyReturnType(getCXXABI(), FI, *this)) {
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType(), State);
  } else if (FI.getReturnInfo().isIndirect()) {
    // The C++ ABI is not aware of register usage, so we have to check if the
    // return value was sret and put it in a register ourselves if appropriate.
    if (State.FreeRegs) {
      --State.FreeRegs;  // The sret parameter consumes a register.
      if (!IsMCUABI)
        FI.getReturnInfo().setInReg(true);
    }
  }

  // The chain argument effectively gives us another free register.
  if (FI.isChainCall())
    ++State.FreeRegs;

  bool UsedInAlloca = false;
  if (State.CC == llvm::CallingConv::X86_VectorCall) {
    computeVectorCallArgs(FI, State, UsedInAlloca);
  } else {
    // If not vectorcall, revert to normal behavior.
    for (auto &I : FI.arguments()) {
      I.info = classifyArgumentType(I.type, State);
      UsedInAlloca |= (I.info.getKind() == ABIArgInfo::InAlloca);
    }
  }

  // If we needed to use inalloca for any argument, do a second pass and rewrite
  // all the memory arguments to use inalloca.
  if (UsedInAlloca)
    rewriteWithInAlloca(FI);
}

void
X86_32ABIInfo::addFieldToArgStruct(SmallVector<llvm::Type *, 6> &FrameFields,
                                   CharUnits &StackOffset, ABIArgInfo &Info,
                                   QualType Type) const {
  // Arguments are always 4-byte-aligned.
  CharUnits FieldAlign = CharUnits::fromQuantity(4);

  assert(StackOffset.isMultipleOf(FieldAlign) && "unaligned inalloca struct");
  Info = ABIArgInfo::getInAlloca(FrameFields.size());
  FrameFields.push_back(CGT.ConvertTypeForMem(Type));
  StackOffset += getContext().getTypeSizeInChars(Type);

  // Insert padding bytes to respect alignment.
  CharUnits FieldEnd = StackOffset;
  StackOffset = FieldEnd.alignTo(FieldAlign);
  if (StackOffset != FieldEnd) {
    CharUnits NumBytes = StackOffset - FieldEnd;
    llvm::Type *Ty = llvm::Type::getInt8Ty(getVMContext());
    Ty = llvm::ArrayType::get(Ty, NumBytes.getQuantity());
    FrameFields.push_back(Ty);
  }
}

static bool isArgInAlloca(const ABIArgInfo &Info) {
  // Leave ignored and inreg arguments alone.
  switch (Info.getKind()) {
  case ABIArgInfo::InAlloca:
    return true;
  case ABIArgInfo::Indirect:
    assert(Info.getIndirectByVal());
    return true;
  case ABIArgInfo::Ignore:
    return false;
  case ABIArgInfo::Direct:
  case ABIArgInfo::Extend:
    if (Info.getInReg())
      return false;
    return true;
  case ABIArgInfo::Expand:
  case ABIArgInfo::CoerceAndExpand:
    // These are aggregate types which are never passed in registers when
    // inalloca is involved.
    return true;
  }
  llvm_unreachable("invalid enum");
}

void X86_32ABIInfo::rewriteWithInAlloca(CGFunctionInfo &FI) const {
  assert(IsWin32StructABI && "inalloca only supported on win32");

  // Build a packed struct type for all of the arguments in memory.
  SmallVector<llvm::Type *, 6> FrameFields;

  // The stack alignment is always 4.
  CharUnits StackAlign = CharUnits::fromQuantity(4);

  CharUnits StackOffset;
  CGFunctionInfo::arg_iterator I = FI.arg_begin(), E = FI.arg_end();

  // Put 'this' into the struct before 'sret', if necessary.
  bool IsThisCall =
      FI.getCallingConvention() == llvm::CallingConv::X86_ThisCall;
  ABIArgInfo &Ret = FI.getReturnInfo();
  if (Ret.isIndirect() && Ret.isSRetAfterThis() && !IsThisCall &&
      isArgInAlloca(I->info)) {
    addFieldToArgStruct(FrameFields, StackOffset, I->info, I->type);
    ++I;
  }

  // Put the sret parameter into the inalloca struct if it's in memory.
  if (Ret.isIndirect() && !Ret.getInReg()) {
    CanQualType PtrTy = getContext().getPointerType(FI.getReturnType());
    addFieldToArgStruct(FrameFields, StackOffset, Ret, PtrTy);
    // On Windows, the hidden sret parameter is always returned in eax.
    Ret.setInAllocaSRet(IsWin32StructABI);
  }

  // Skip the 'this' parameter in ecx.
  if (IsThisCall)
    ++I;

  // Put arguments passed in memory into the struct.
  for (; I != E; ++I) {
    if (isArgInAlloca(I->info))
      addFieldToArgStruct(FrameFields, StackOffset, I->info, I->type);
  }

  FI.setArgStruct(llvm::StructType::get(getVMContext(), FrameFields,
                                        /*isPacked=*/true),
                  StackAlign);
}

Address X86_32ABIInfo::EmitVAArg(CodeGenFunction &CGF,
                                 Address VAListAddr, QualType Ty) const {

  auto TypeInfo = getContext().getTypeInfoInChars(Ty);

  // x86-32 changes the alignment of certain arguments on the stack.
  //
  // Just messing with TypeInfo like this works because we never pass
  // anything indirectly.
  TypeInfo.second = CharUnits::fromQuantity(
                getTypeStackAlignInBytes(Ty, TypeInfo.second.getQuantity()));

  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*Indirect*/ false,
                          TypeInfo, CharUnits::fromQuantity(4),
                          /*AllowHigherAlign*/ true);
}

bool X86_32TargetCodeGenInfo::isStructReturnInRegABI(
    const llvm::Triple &Triple, const CodeGenOptions &Opts) {
  assert(Triple.getArch() == llvm::Triple::x86);

  switch (Opts.getStructReturnConvention()) {
  case CodeGenOptions::SRCK_Default:
    break;
  case CodeGenOptions::SRCK_OnStack:  // -fpcc-struct-return
    return false;
  case CodeGenOptions::SRCK_InRegs:  // -freg-struct-return
    return true;
  }

  if (Triple.isOSDarwin() || Triple.isOSIAMCU())
    return true;

  switch (Triple.getOS()) {
  case llvm::Triple::DragonFly:
  case llvm::Triple::FreeBSD:
  case llvm::Triple::OpenBSD:
  case llvm::Triple::Win32:
    return true;
  default:
    return false;
  }
}

void X86_32TargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &CGM) const {
  if (GV->isDeclaration())
    return;
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D)) {
    if (FD->hasAttr<X86ForceAlignArgPointerAttr>()) {
      llvm::Function *Fn = cast<llvm::Function>(GV);
      Fn->addFnAttr("stackrealign");
    }
    if (FD->hasAttr<AnyX86InterruptAttr>()) {
      llvm::Function *Fn = cast<llvm::Function>(GV);
      Fn->setCallingConv(llvm::CallingConv::X86_INTR);
    }
  }
}

bool X86_32TargetCodeGenInfo::initDwarfEHRegSizeTable(
                                               CodeGen::CodeGenFunction &CGF,
                                               llvm::Value *Address) const {
  CodeGen::CGBuilderTy &Builder = CGF.Builder;

  llvm::Value *Four8 = llvm::ConstantInt::get(CGF.Int8Ty, 4);

  // 0-7 are the eight integer registers;  the order is different
  //   on Darwin (for EH), but the range is the same.
  // 8 is %eip.
  AssignToArrayRange(Builder, Address, Four8, 0, 8);

  if (CGF.CGM.getTarget().getTriple().isOSDarwin()) {
    // 12-16 are st(0..4).  Not sure why we stop at 4.
    // These have size 16, which is sizeof(long double) on
    // platforms with 8-byte alignment for that type.
    llvm::Value *Sixteen8 = llvm::ConstantInt::get(CGF.Int8Ty, 16);
    AssignToArrayRange(Builder, Address, Sixteen8, 12, 16);

  } else {
    // 9 is %eflags, which doesn't get a size on Darwin for some
    // reason.
    Builder.CreateAlignedStore(
        Four8, Builder.CreateConstInBoundsGEP1_32(CGF.Int8Ty, Address, 9),
                               CharUnits::One());

    // 11-16 are st(0..5).  Not sure why we stop at 5.
    // These have size 12, which is sizeof(long double) on
    // platforms with 4-byte alignment for that type.
    llvm::Value *Twelve8 = llvm::ConstantInt::get(CGF.Int8Ty, 12);
    AssignToArrayRange(Builder, Address, Twelve8, 11, 16);
  }

  return false;
}

//===----------------------------------------------------------------------===//
// X86-64 ABI Implementation
//===----------------------------------------------------------------------===//


namespace {
/// The AVX ABI level for X86 targets.
enum class X86AVXABILevel {
  None,
  AVX,
  AVX512
};

/// \p returns the size in bits of the largest (native) vector for \p AVXLevel.
static unsigned getNativeVectorSizeForAVXABI(X86AVXABILevel AVXLevel) {
  switch (AVXLevel) {
  case X86AVXABILevel::AVX512:
    return 512;
  case X86AVXABILevel::AVX:
    return 256;
  case X86AVXABILevel::None:
    return 128;
  }
  llvm_unreachable("Unknown AVXLevel");
}

/// X86_64ABIInfo - The X86_64 ABI information.
class X86_64ABIInfo : public SwiftABIInfo {
  enum Class {
    Integer = 0,
    SSE,
    SSEUp,
    X87,
    X87Up,
    ComplexX87,
    NoClass,
    Memory
  };

  /// merge - Implement the X86_64 ABI merging algorithm.
  ///
  /// Merge an accumulating classification \arg Accum with a field
  /// classification \arg Field.
  ///
  /// \param Accum - The accumulating classification. This should
  /// always be either NoClass or the result of a previous merge
  /// call. In addition, this should never be Memory (the caller
  /// should just return Memory for the aggregate).
  static Class merge(Class Accum, Class Field);

  /// postMerge - Implement the X86_64 ABI post merging algorithm.
  ///
  /// Post merger cleanup, reduces a malformed Hi and Lo pair to
  /// final MEMORY or SSE classes when necessary.
  ///
  /// \param AggregateSize - The size of the current aggregate in
  /// the classification process.
  ///
  /// \param Lo - The classification for the parts of the type
  /// residing in the low word of the containing object.
  ///
  /// \param Hi - The classification for the parts of the type
  /// residing in the higher words of the containing object.
  ///
  void postMerge(unsigned AggregateSize, Class &Lo, Class &Hi) const;

  /// classify - Determine the x86_64 register classes in which the
  /// given type T should be passed.
  ///
  /// \param Lo - The classification for the parts of the type
  /// residing in the low word of the containing object.
  ///
  /// \param Hi - The classification for the parts of the type
  /// residing in the high word of the containing object.
  ///
  /// \param OffsetBase - The bit offset of this type in the
  /// containing object.  Some parameters are classified different
  /// depending on whether they straddle an eightbyte boundary.
  ///
  /// \param isNamedArg - Whether the argument in question is a "named"
  /// argument, as used in AMD64-ABI 3.5.7.
  ///
  /// If a word is unused its result will be NoClass; if a type should
  /// be passed in Memory then at least the classification of \arg Lo
  /// will be Memory.
  ///
  /// The \arg Lo class will be NoClass iff the argument is ignored.
  ///
  /// If the \arg Lo class is ComplexX87, then the \arg Hi class will
  /// also be ComplexX87.
  void classify(QualType T, uint64_t OffsetBase, Class &Lo, Class &Hi,
                bool isNamedArg) const;

  llvm::Type *GetByteVectorType(QualType Ty) const;
  llvm::Type *GetSSETypeAtOffset(llvm::Type *IRType,
                                 unsigned IROffset, QualType SourceTy,
                                 unsigned SourceOffset) const;
  llvm::Type *GetINTEGERTypeAtOffset(llvm::Type *IRType,
                                     unsigned IROffset, QualType SourceTy,
                                     unsigned SourceOffset) const;

  /// getIndirectResult - Give a source type \arg Ty, return a suitable result
  /// such that the argument will be returned in memory.
  ABIArgInfo getIndirectReturnResult(QualType Ty) const;

  /// getIndirectResult - Give a source type \arg Ty, return a suitable result
  /// such that the argument will be passed in memory.
  ///
  /// \param freeIntRegs - The number of free integer registers remaining
  /// available.
  ABIArgInfo getIndirectResult(QualType Ty, unsigned freeIntRegs) const;

  ABIArgInfo classifyReturnType(QualType RetTy) const;

  ABIArgInfo classifyArgumentType(QualType Ty, unsigned freeIntRegs,
                                  unsigned &neededInt, unsigned &neededSSE,
                                  bool isNamedArg) const;

  ABIArgInfo classifyRegCallStructType(QualType Ty, unsigned &NeededInt,
                                       unsigned &NeededSSE) const;

  ABIArgInfo classifyRegCallStructTypeImpl(QualType Ty, unsigned &NeededInt,
                                           unsigned &NeededSSE) const;

  bool IsIllegalVectorType(QualType Ty) const;

  /// The 0.98 ABI revision clarified a lot of ambiguities,
  /// unfortunately in ways that were not always consistent with
  /// certain previous compilers.  In particular, platforms which
  /// required strict binary compatibility with older versions of GCC
  /// may need to exempt themselves.
  bool honorsRevision0_98() const {
    return !getTarget().getTriple().isOSDarwin();
  }

  /// GCC classifies <1 x long long> as SSE but some platform ABIs choose to
  /// classify it as INTEGER (for compatibility with older clang compilers).
  bool classifyIntegerMMXAsSSE() const {
    // Clang <= 3.8 did not do this.
    if (getContext().getLangOpts().getClangABICompat() <=
        LangOptions::ClangABI::Ver3_8)
      return false;

    const llvm::Triple &Triple = getTarget().getTriple();
    if (Triple.isOSDarwin() || Triple.getOS() == llvm::Triple::PS4)
      return false;
    if (Triple.isOSFreeBSD() && Triple.getOSMajorVersion() >= 10)
      return false;
    return true;
  }

  X86AVXABILevel AVXLevel;
  // Some ABIs (e.g. X32 ABI and Native Client OS) use 32 bit pointers on
  // 64-bit hardware.
  bool Has64BitPointers;

public:
  X86_64ABIInfo(CodeGen::CodeGenTypes &CGT, X86AVXABILevel AVXLevel) :
      SwiftABIInfo(CGT), AVXLevel(AVXLevel),
      Has64BitPointers(CGT.getDataLayout().getPointerSize(0) == 8) {
  }

  bool isPassedUsingAVXType(QualType type) const {
    unsigned neededInt, neededSSE;
    // The freeIntRegs argument doesn't matter here.
    ABIArgInfo info = classifyArgumentType(type, 0, neededInt, neededSSE,
                                           /*isNamedArg*/true);
    if (info.isDirect()) {
      llvm::Type *ty = info.getCoerceToType();
      if (llvm::VectorType *vectorTy = dyn_cast_or_null<llvm::VectorType>(ty))
        return (vectorTy->getBitWidth() > 128);
    }
    return false;
  }

  void computeInfo(CGFunctionInfo &FI) const override;

  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;
  Address EmitMSVAArg(CodeGenFunction &CGF, Address VAListAddr,
                      QualType Ty) const override;

  bool has64BitPointers() const {
    return Has64BitPointers;
  }

  bool shouldPassIndirectlyForSwift(ArrayRef<llvm::Type*> scalars,
                                    bool asReturnValue) const override {
    return occupiesMoreThan(CGT, scalars, /*total*/ 4);
  }
  bool isSwiftErrorInRegister() const override {
    return true;
  }
};

/// WinX86_64ABIInfo - The Windows X86_64 ABI information.
class WinX86_64ABIInfo : public SwiftABIInfo {
public:
  WinX86_64ABIInfo(CodeGen::CodeGenTypes &CGT)
      : SwiftABIInfo(CGT),
        IsMingw64(getTarget().getTriple().isWindowsGNUEnvironment()) {}

  void computeInfo(CGFunctionInfo &FI) const override;

  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;

  bool isHomogeneousAggregateBaseType(QualType Ty) const override {
    // FIXME: Assumes vectorcall is in use.
    return isX86VectorTypeForVectorCall(getContext(), Ty);
  }

  bool isHomogeneousAggregateSmallEnough(const Type *Ty,
                                         uint64_t NumMembers) const override {
    // FIXME: Assumes vectorcall is in use.
    return isX86VectorCallAggregateSmallEnough(NumMembers);
  }

  bool shouldPassIndirectlyForSwift(ArrayRef<llvm::Type *> scalars,
                                    bool asReturnValue) const override {
    return occupiesMoreThan(CGT, scalars, /*total*/ 4);
  }

  bool isSwiftErrorInRegister() const override {
    return true;
  }

private:
  ABIArgInfo classify(QualType Ty, unsigned &FreeSSERegs, bool IsReturnType,
                      bool IsVectorCall, bool IsRegCall) const;
  ABIArgInfo reclassifyHvaArgType(QualType Ty, unsigned &FreeSSERegs,
                                      const ABIArgInfo &current) const;
  void computeVectorCallArgs(CGFunctionInfo &FI, unsigned FreeSSERegs,
                             bool IsVectorCall, bool IsRegCall) const;

    bool IsMingw64;
};

class X86_64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  X86_64TargetCodeGenInfo(CodeGen::CodeGenTypes &CGT, X86AVXABILevel AVXLevel)
      : TargetCodeGenInfo(new X86_64ABIInfo(CGT, AVXLevel)) {}

  const X86_64ABIInfo &getABIInfo() const {
    return static_cast<const X86_64ABIInfo&>(TargetCodeGenInfo::getABIInfo());
  }

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &CGM) const override {
    return 7;
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override {
    llvm::Value *Eight8 = llvm::ConstantInt::get(CGF.Int8Ty, 8);

    // 0-15 are the 16 integer registers.
    // 16 is %rip.
    AssignToArrayRange(CGF.Builder, Address, Eight8, 0, 16);
    return false;
  }

  llvm::Type* adjustInlineAsmType(CodeGen::CodeGenFunction &CGF,
                                  StringRef Constraint,
                                  llvm::Type* Ty) const override {
    return X86AdjustInlineAsmType(CGF, Constraint, Ty);
  }

  bool isNoProtoCallVariadic(const CallArgList &args,
                             const FunctionNoProtoType *fnType) const override {
    // The default CC on x86-64 sets %al to the number of SSA
    // registers used, and GCC sets this when calling an unprototyped
    // function, so we override the default behavior.  However, don't do
    // that when AVX types are involved: the ABI explicitly states it is
    // undefined, and it doesn't work in practice because of how the ABI
    // defines varargs anyway.
    if (fnType->getCallConv() == CC_C) {
      bool HasAVXType = false;
      for (CallArgList::const_iterator
             it = args.begin(), ie = args.end(); it != ie; ++it) {
        if (getABIInfo().isPassedUsingAVXType(it->Ty)) {
          HasAVXType = true;
          break;
        }
      }

      if (!HasAVXType)
        return true;
    }

    return TargetCodeGenInfo::isNoProtoCallVariadic(args, fnType);
  }

  llvm::Constant *
  getUBSanFunctionSignature(CodeGen::CodeGenModule &CGM) const override {
    unsigned Sig = (0xeb << 0) | // jmp rel8
                   (0x06 << 8) | //           .+0x08
                   ('v' << 16) |
                   ('2' << 24);
    return llvm::ConstantInt::get(CGM.Int32Ty, Sig);
  }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    if (GV->isDeclaration())
      return;
    if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D)) {
      if (FD->hasAttr<X86ForceAlignArgPointerAttr>()) {
        llvm::Function *Fn = cast<llvm::Function>(GV);
        Fn->addFnAttr("stackrealign");
      }
      if (FD->hasAttr<AnyX86InterruptAttr>()) {
        llvm::Function *Fn = cast<llvm::Function>(GV);
        Fn->setCallingConv(llvm::CallingConv::X86_INTR);
      }
    }
  }
};

class PS4TargetCodeGenInfo : public X86_64TargetCodeGenInfo {
public:
  PS4TargetCodeGenInfo(CodeGen::CodeGenTypes &CGT, X86AVXABILevel AVXLevel)
    : X86_64TargetCodeGenInfo(CGT, AVXLevel) {}

  void getDependentLibraryOption(llvm::StringRef Lib,
                                 llvm::SmallString<24> &Opt) const override {
    Opt = "\01";
    // If the argument contains a space, enclose it in quotes.
    if (Lib.find(" ") != StringRef::npos)
      Opt += "\"" + Lib.str() + "\"";
    else
      Opt += Lib;
  }
};

static std::string qualifyWindowsLibrary(llvm::StringRef Lib) {
  // If the argument does not end in .lib, automatically add the suffix.
  // If the argument contains a space, enclose it in quotes.
  // This matches the behavior of MSVC.
  bool Quote = (Lib.find(" ") != StringRef::npos);
  std::string ArgStr = Quote ? "\"" : "";
  ArgStr += Lib;
  if (!Lib.endswith_lower(".lib") && !Lib.endswith_lower(".a"))
    ArgStr += ".lib";
  ArgStr += Quote ? "\"" : "";
  return ArgStr;
}

class WinX86_32TargetCodeGenInfo : public X86_32TargetCodeGenInfo {
public:
  WinX86_32TargetCodeGenInfo(CodeGen::CodeGenTypes &CGT,
        bool DarwinVectorABI, bool RetSmallStructInRegABI, bool Win32StructABI,
        unsigned NumRegisterParameters)
    : X86_32TargetCodeGenInfo(CGT, DarwinVectorABI, RetSmallStructInRegABI,
        Win32StructABI, NumRegisterParameters, false) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override;

  void getDependentLibraryOption(llvm::StringRef Lib,
                                 llvm::SmallString<24> &Opt) const override {
    Opt = "/DEFAULTLIB:";
    Opt += qualifyWindowsLibrary(Lib);
  }

  void getDetectMismatchOption(llvm::StringRef Name,
                               llvm::StringRef Value,
                               llvm::SmallString<32> &Opt) const override {
    Opt = "/FAILIFMISMATCH:\"" + Name.str() + "=" + Value.str() + "\"";
  }
};

static void addStackProbeTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                                          CodeGen::CodeGenModule &CGM) {
  if (llvm::Function *Fn = dyn_cast_or_null<llvm::Function>(GV)) {

    if (CGM.getCodeGenOpts().StackProbeSize != 4096)
      Fn->addFnAttr("stack-probe-size",
                    llvm::utostr(CGM.getCodeGenOpts().StackProbeSize));
    if (CGM.getCodeGenOpts().NoStackArgProbe)
      Fn->addFnAttr("no-stack-arg-probe");
  }
}

void WinX86_32TargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &CGM) const {
  X86_32TargetCodeGenInfo::setTargetAttributes(D, GV, CGM);
  if (GV->isDeclaration())
    return;
  addStackProbeTargetAttributes(D, GV, CGM);
}

class WinX86_64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  WinX86_64TargetCodeGenInfo(CodeGen::CodeGenTypes &CGT,
                             X86AVXABILevel AVXLevel)
      : TargetCodeGenInfo(new WinX86_64ABIInfo(CGT)) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override;

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &CGM) const override {
    return 7;
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override {
    llvm::Value *Eight8 = llvm::ConstantInt::get(CGF.Int8Ty, 8);

    // 0-15 are the 16 integer registers.
    // 16 is %rip.
    AssignToArrayRange(CGF.Builder, Address, Eight8, 0, 16);
    return false;
  }

  void getDependentLibraryOption(llvm::StringRef Lib,
                                 llvm::SmallString<24> &Opt) const override {
    Opt = "/DEFAULTLIB:";
    Opt += qualifyWindowsLibrary(Lib);
  }

  void getDetectMismatchOption(llvm::StringRef Name,
                               llvm::StringRef Value,
                               llvm::SmallString<32> &Opt) const override {
    Opt = "/FAILIFMISMATCH:\"" + Name.str() + "=" + Value.str() + "\"";
  }
};

void WinX86_64TargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &CGM) const {
  TargetCodeGenInfo::setTargetAttributes(D, GV, CGM);
  if (GV->isDeclaration())
    return;
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D)) {
    if (FD->hasAttr<X86ForceAlignArgPointerAttr>()) {
      llvm::Function *Fn = cast<llvm::Function>(GV);
      Fn->addFnAttr("stackrealign");
    }
    if (FD->hasAttr<AnyX86InterruptAttr>()) {
      llvm::Function *Fn = cast<llvm::Function>(GV);
      Fn->setCallingConv(llvm::CallingConv::X86_INTR);
    }
  }

  addStackProbeTargetAttributes(D, GV, CGM);
}
}

void X86_64ABIInfo::postMerge(unsigned AggregateSize, Class &Lo,
                              Class &Hi) const {
  // AMD64-ABI 3.2.3p2: Rule 5. Then a post merger cleanup is done:
  //
  // (a) If one of the classes is Memory, the whole argument is passed in
  //     memory.
  //
  // (b) If X87UP is not preceded by X87, the whole argument is passed in
  //     memory.
  //
  // (c) If the size of the aggregate exceeds two eightbytes and the first
  //     eightbyte isn't SSE or any other eightbyte isn't SSEUP, the whole
  //     argument is passed in memory. NOTE: This is necessary to keep the
  //     ABI working for processors that don't support the __m256 type.
  //
  // (d) If SSEUP is not preceded by SSE or SSEUP, it is converted to SSE.
  //
  // Some of these are enforced by the merging logic.  Others can arise
  // only with unions; for example:
  //   union { _Complex double; unsigned; }
  //
  // Note that clauses (b) and (c) were added in 0.98.
  //
  if (Hi == Memory)
    Lo = Memory;
  if (Hi == X87Up && Lo != X87 && honorsRevision0_98())
    Lo = Memory;
  if (AggregateSize > 128 && (Lo != SSE || Hi != SSEUp))
    Lo = Memory;
  if (Hi == SSEUp && Lo != SSE)
    Hi = SSE;
}

X86_64ABIInfo::Class X86_64ABIInfo::merge(Class Accum, Class Field) {
  // AMD64-ABI 3.2.3p2: Rule 4. Each field of an object is
  // classified recursively so that always two fields are
  // considered. The resulting class is calculated according to
  // the classes of the fields in the eightbyte:
  //
  // (a) If both classes are equal, this is the resulting class.
  //
  // (b) If one of the classes is NO_CLASS, the resulting class is
  // the other class.
  //
  // (c) If one of the classes is MEMORY, the result is the MEMORY
  // class.
  //
  // (d) If one of the classes is INTEGER, the result is the
  // INTEGER.
  //
  // (e) If one of the classes is X87, X87UP, COMPLEX_X87 class,
  // MEMORY is used as class.
  //
  // (f) Otherwise class SSE is used.

  // Accum should never be memory (we should have returned) or
  // ComplexX87 (because this cannot be passed in a structure).
  assert((Accum != Memory && Accum != ComplexX87) &&
         "Invalid accumulated classification during merge.");
  if (Accum == Field || Field == NoClass)
    return Accum;
  if (Field == Memory)
    return Memory;
  if (Accum == NoClass)
    return Field;
  if (Accum == Integer || Field == Integer)
    return Integer;
  if (Field == X87 || Field == X87Up || Field == ComplexX87 ||
      Accum == X87 || Accum == X87Up)
    return Memory;
  return SSE;
}

void X86_64ABIInfo::classify(QualType Ty, uint64_t OffsetBase,
                             Class &Lo, Class &Hi, bool isNamedArg) const {
  // FIXME: This code can be simplified by introducing a simple value class for
  // Class pairs with appropriate constructor methods for the various
  // situations.

  // FIXME: Some of the split computations are wrong; unaligned vectors
  // shouldn't be passed in registers for example, so there is no chance they
  // can straddle an eightbyte. Verify & simplify.

  Lo = Hi = NoClass;

  Class &Current = OffsetBase < 64 ? Lo : Hi;
  Current = Memory;

  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    BuiltinType::Kind k = BT->getKind();

    if (k == BuiltinType::Void) {
      Current = NoClass;
    } else if (k == BuiltinType::Int128 || k == BuiltinType::UInt128) {
      Lo = Integer;
      Hi = Integer;
    } else if (k >= BuiltinType::Bool && k <= BuiltinType::LongLong) {
      Current = Integer;
    } else if (k == BuiltinType::Float || k == BuiltinType::Double) {
      Current = SSE;
    } else if (k == BuiltinType::LongDouble) {
      const llvm::fltSemantics *LDF = &getTarget().getLongDoubleFormat();
      if (LDF == &llvm::APFloat::IEEEquad()) {
        Lo = SSE;
        Hi = SSEUp;
      } else if (LDF == &llvm::APFloat::x87DoubleExtended()) {
        Lo = X87;
        Hi = X87Up;
      } else if (LDF == &llvm::APFloat::IEEEdouble()) {
        Current = SSE;
      } else
        llvm_unreachable("unexpected long double representation!");
    }
    // FIXME: _Decimal32 and _Decimal64 are SSE.
    // FIXME: _float128 and _Decimal128 are (SSE, SSEUp).
    return;
  }

  if (const EnumType *ET = Ty->getAs<EnumType>()) {
    // Classify the underlying integer type.
    classify(ET->getDecl()->getIntegerType(), OffsetBase, Lo, Hi, isNamedArg);
    return;
  }

  if (Ty->hasPointerRepresentation()) {
    Current = Integer;
    return;
  }

  if (Ty->isMemberPointerType()) {
    if (Ty->isMemberFunctionPointerType()) {
      if (Has64BitPointers) {
        // If Has64BitPointers, this is an {i64, i64}, so classify both
        // Lo and Hi now.
        Lo = Hi = Integer;
      } else {
        // Otherwise, with 32-bit pointers, this is an {i32, i32}. If that
        // straddles an eightbyte boundary, Hi should be classified as well.
        uint64_t EB_FuncPtr = (OffsetBase) / 64;
        uint64_t EB_ThisAdj = (OffsetBase + 64 - 1) / 64;
        if (EB_FuncPtr != EB_ThisAdj) {
          Lo = Hi = Integer;
        } else {
          Current = Integer;
        }
      }
    } else {
      Current = Integer;
    }
    return;
  }

  if (const VectorType *VT = Ty->getAs<VectorType>()) {
    uint64_t Size = getContext().getTypeSize(VT);
    if (Size == 1 || Size == 8 || Size == 16 || Size == 32) {
      // gcc passes the following as integer:
      // 4 bytes - <4 x char>, <2 x short>, <1 x int>, <1 x float>
      // 2 bytes - <2 x char>, <1 x short>
      // 1 byte  - <1 x char>
      Current = Integer;

      // If this type crosses an eightbyte boundary, it should be
      // split.
      uint64_t EB_Lo = (OffsetBase) / 64;
      uint64_t EB_Hi = (OffsetBase + Size - 1) / 64;
      if (EB_Lo != EB_Hi)
        Hi = Lo;
    } else if (Size == 64) {
      QualType ElementType = VT->getElementType();

      // gcc passes <1 x double> in memory. :(
      if (ElementType->isSpecificBuiltinType(BuiltinType::Double))
        return;

      // gcc passes <1 x long long> as SSE but clang used to unconditionally
      // pass them as integer.  For platforms where clang is the de facto
      // platform compiler, we must continue to use integer.
      if (!classifyIntegerMMXAsSSE() &&
          (ElementType->isSpecificBuiltinType(BuiltinType::LongLong) ||
           ElementType->isSpecificBuiltinType(BuiltinType::ULongLong) ||
           ElementType->isSpecificBuiltinType(BuiltinType::Long) ||
           ElementType->isSpecificBuiltinType(BuiltinType::ULong)))
        Current = Integer;
      else
        Current = SSE;

      // If this type crosses an eightbyte boundary, it should be
      // split.
      if (OffsetBase && OffsetBase != 64)
        Hi = Lo;
    } else if (Size == 128 ||
               (isNamedArg && Size <= getNativeVectorSizeForAVXABI(AVXLevel))) {
      // Arguments of 256-bits are split into four eightbyte chunks. The
      // least significant one belongs to class SSE and all the others to class
      // SSEUP. The original Lo and Hi design considers that types can't be
      // greater than 128-bits, so a 64-bit split in Hi and Lo makes sense.
      // This design isn't correct for 256-bits, but since there're no cases
      // where the upper parts would need to be inspected, avoid adding
      // complexity and just consider Hi to match the 64-256 part.
      //
      // Note that per 3.5.7 of AMD64-ABI, 256-bit args are only passed in
      // registers if they are "named", i.e. not part of the "..." of a
      // variadic function.
      //
      // Similarly, per 3.2.3. of the AVX512 draft, 512-bits ("named") args are
      // split into eight eightbyte chunks, one SSE and seven SSEUP.
      Lo = SSE;
      Hi = SSEUp;
    }
    return;
  }

  if (const ComplexType *CT = Ty->getAs<ComplexType>()) {
    QualType ET = getContext().getCanonicalType(CT->getElementType());

    uint64_t Size = getContext().getTypeSize(Ty);
    if (ET->isIntegralOrEnumerationType()) {
      if (Size <= 64)
        Current = Integer;
      else if (Size <= 128)
        Lo = Hi = Integer;
    } else if (ET == getContext().FloatTy) {
      Current = SSE;
    } else if (ET == getContext().DoubleTy) {
      Lo = Hi = SSE;
    } else if (ET == getContext().LongDoubleTy) {
      const llvm::fltSemantics *LDF = &getTarget().getLongDoubleFormat();
      if (LDF == &llvm::APFloat::IEEEquad())
        Current = Memory;
      else if (LDF == &llvm::APFloat::x87DoubleExtended())
        Current = ComplexX87;
      else if (LDF == &llvm::APFloat::IEEEdouble())
        Lo = Hi = SSE;
      else
        llvm_unreachable("unexpected long double representation!");
    }

    // If this complex type crosses an eightbyte boundary then it
    // should be split.
    uint64_t EB_Real = (OffsetBase) / 64;
    uint64_t EB_Imag = (OffsetBase + getContext().getTypeSize(ET)) / 64;
    if (Hi == NoClass && EB_Real != EB_Imag)
      Hi = Lo;

    return;
  }

  if (const ConstantArrayType *AT = getContext().getAsConstantArrayType(Ty)) {
    // Arrays are treated like structures.

    uint64_t Size = getContext().getTypeSize(Ty);

    // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger
    // than eight eightbytes, ..., it has class MEMORY.
    if (Size > 512)
      return;

    // AMD64-ABI 3.2.3p2: Rule 1. If ..., or it contains unaligned
    // fields, it has class MEMORY.
    //
    // Only need to check alignment of array base.
    if (OffsetBase % getContext().getTypeAlign(AT->getElementType()))
      return;

    // Otherwise implement simplified merge. We could be smarter about
    // this, but it isn't worth it and would be harder to verify.
    Current = NoClass;
    uint64_t EltSize = getContext().getTypeSize(AT->getElementType());
    uint64_t ArraySize = AT->getSize().getZExtValue();

    // The only case a 256-bit wide vector could be used is when the array
    // contains a single 256-bit element. Since Lo and Hi logic isn't extended
    // to work for sizes wider than 128, early check and fallback to memory.
    //
    if (Size > 128 &&
        (Size != EltSize || Size > getNativeVectorSizeForAVXABI(AVXLevel)))
      return;

    for (uint64_t i=0, Offset=OffsetBase; i<ArraySize; ++i, Offset += EltSize) {
      Class FieldLo, FieldHi;
      classify(AT->getElementType(), Offset, FieldLo, FieldHi, isNamedArg);
      Lo = merge(Lo, FieldLo);
      Hi = merge(Hi, FieldHi);
      if (Lo == Memory || Hi == Memory)
        break;
    }

    postMerge(Size, Lo, Hi);
    assert((Hi != SSEUp || Lo == SSE) && "Invalid SSEUp array classification.");
    return;
  }

  if (const RecordType *RT = Ty->getAs<RecordType>()) {
    uint64_t Size = getContext().getTypeSize(Ty);

    // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger
    // than eight eightbytes, ..., it has class MEMORY.
    if (Size > 512)
      return;

    // AMD64-ABI 3.2.3p2: Rule 2. If a C++ object has either a non-trivial
    // copy constructor or a non-trivial destructor, it is passed by invisible
    // reference.
    if (getRecordArgABI(RT, getCXXABI()))
      return;

    const RecordDecl *RD = RT->getDecl();

    // Assume variable sized types are passed in memory.
    if (RD->hasFlexibleArrayMember())
      return;

    const ASTRecordLayout &Layout = getContext().getASTRecordLayout(RD);

    // Reset Lo class, this will be recomputed.
    Current = NoClass;

    // If this is a C++ record, classify the bases first.
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      for (const auto &I : CXXRD->bases()) {
        assert(!I.isVirtual() && !I.getType()->isDependentType() &&
               "Unexpected base class!");
        const CXXRecordDecl *Base =
          cast<CXXRecordDecl>(I.getType()->getAs<RecordType>()->getDecl());

        // Classify this field.
        //
        // AMD64-ABI 3.2.3p2: Rule 3. If the size of the aggregate exceeds a
        // single eightbyte, each is classified separately. Each eightbyte gets
        // initialized to class NO_CLASS.
        Class FieldLo, FieldHi;
        uint64_t Offset =
          OffsetBase + getContext().toBits(Layout.getBaseClassOffset(Base));
        classify(I.getType(), Offset, FieldLo, FieldHi, isNamedArg);
        Lo = merge(Lo, FieldLo);
        Hi = merge(Hi, FieldHi);
        if (Lo == Memory || Hi == Memory) {
          postMerge(Size, Lo, Hi);
          return;
        }
      }
    }

    // Classify the fields one at a time, merging the results.
    unsigned idx = 0;
    for (RecordDecl::field_iterator i = RD->field_begin(), e = RD->field_end();
           i != e; ++i, ++idx) {
      uint64_t Offset = OffsetBase + Layout.getFieldOffset(idx);
      bool BitField = i->isBitField();

      // Ignore padding bit-fields.
      if (BitField && i->isUnnamedBitfield())
        continue;

      // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger than
      // four eightbytes, or it contains unaligned fields, it has class MEMORY.
      //
      // The only case a 256-bit wide vector could be used is when the struct
      // contains a single 256-bit element. Since Lo and Hi logic isn't extended
      // to work for sizes wider than 128, early check and fallback to memory.
      //
      if (Size > 128 && (Size != getContext().getTypeSize(i->getType()) ||
                         Size > getNativeVectorSizeForAVXABI(AVXLevel))) {
        Lo = Memory;
        postMerge(Size, Lo, Hi);
        return;
      }
      // Note, skip this test for bit-fields, see below.
      if (!BitField && Offset % getContext().getTypeAlign(i->getType())) {
        Lo = Memory;
        postMerge(Size, Lo, Hi);
        return;
      }

      // Classify this field.
      //
      // AMD64-ABI 3.2.3p2: Rule 3. If the size of the aggregate
      // exceeds a single eightbyte, each is classified
      // separately. Each eightbyte gets initialized to class
      // NO_CLASS.
      Class FieldLo, FieldHi;

      // Bit-fields require special handling, they do not force the
      // structure to be passed in memory even if unaligned, and
      // therefore they can straddle an eightbyte.
      if (BitField) {
        assert(!i->isUnnamedBitfield());
        uint64_t Offset = OffsetBase + Layout.getFieldOffset(idx);
        uint64_t Size = i->getBitWidthValue(getContext());

        uint64_t EB_Lo = Offset / 64;
        uint64_t EB_Hi = (Offset + Size - 1) / 64;

        if (EB_Lo) {
          assert(EB_Hi == EB_Lo && "Invalid classification, type > 16 bytes.");
          FieldLo = NoClass;
          FieldHi = Integer;
        } else {
          FieldLo = Integer;
          FieldHi = EB_Hi ? Integer : NoClass;
        }
      } else
        classify(i->getType(), Offset, FieldLo, FieldHi, isNamedArg);
      Lo = merge(Lo, FieldLo);
      Hi = merge(Hi, FieldHi);
      if (Lo == Memory || Hi == Memory)
        break;
    }

    postMerge(Size, Lo, Hi);
  }
}

ABIArgInfo X86_64ABIInfo::getIndirectReturnResult(QualType Ty) const {
  // If this is a scalar LLVM value then assume LLVM will pass it in the right
  // place naturally.
  if (!isAggregateTypeForABI(Ty)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    return (Ty->isPromotableIntegerType() ? ABIArgInfo::getExtend(Ty)
                                          : ABIArgInfo::getDirect());
  }

  return getNaturalAlignIndirect(Ty);
}

bool X86_64ABIInfo::IsIllegalVectorType(QualType Ty) const {
  if (const VectorType *VecTy = Ty->getAs<VectorType>()) {
    uint64_t Size = getContext().getTypeSize(VecTy);
    unsigned LargestVector = getNativeVectorSizeForAVXABI(AVXLevel);
    if (Size <= 64 || Size > LargestVector)
      return true;
  }

  return false;
}

ABIArgInfo X86_64ABIInfo::getIndirectResult(QualType Ty,
                                            unsigned freeIntRegs) const {
  // If this is a scalar LLVM value then assume LLVM will pass it in the right
  // place naturally.
  //
  // This assumption is optimistic, as there could be free registers available
  // when we need to pass this argument in memory, and LLVM could try to pass
  // the argument in the free register. This does not seem to happen currently,
  // but this code would be much safer if we could mark the argument with
  // 'onstack'. See PR12193.
  if (!isAggregateTypeForABI(Ty) && !IsIllegalVectorType(Ty)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    return (Ty->isPromotableIntegerType() ? ABIArgInfo::getExtend(Ty)
                                          : ABIArgInfo::getDirect());
  }

  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
    return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

  // Compute the byval alignment. We specify the alignment of the byval in all
  // cases so that the mid-level optimizer knows the alignment of the byval.
  unsigned Align = std::max(getContext().getTypeAlign(Ty) / 8, 8U);

  // Attempt to avoid passing indirect results using byval when possible. This
  // is important for good codegen.
  //
  // We do this by coercing the value into a scalar type which the backend can
  // handle naturally (i.e., without using byval).
  //
  // For simplicity, we currently only do this when we have exhausted all of the
  // free integer registers. Doing this when there are free integer registers
  // would require more care, as we would have to ensure that the coerced value
  // did not claim the unused register. That would require either reording the
  // arguments to the function (so that any subsequent inreg values came first),
  // or only doing this optimization when there were no following arguments that
  // might be inreg.
  //
  // We currently expect it to be rare (particularly in well written code) for
  // arguments to be passed on the stack when there are still free integer
  // registers available (this would typically imply large structs being passed
  // by value), so this seems like a fair tradeoff for now.
  //
  // We can revisit this if the backend grows support for 'onstack' parameter
  // attributes. See PR12193.
  if (freeIntRegs == 0) {
    uint64_t Size = getContext().getTypeSize(Ty);

    // If this type fits in an eightbyte, coerce it into the matching integral
    // type, which will end up on the stack (with alignment 8).
    if (Align == 8 && Size <= 64)
      return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(),
                                                          Size));
  }

  return ABIArgInfo::getIndirect(CharUnits::fromQuantity(Align));
}

/// The ABI specifies that a value should be passed in a full vector XMM/YMM
/// register. Pick an LLVM IR type that will be passed as a vector register.
llvm::Type *X86_64ABIInfo::GetByteVectorType(QualType Ty) const {
  // Wrapper structs/arrays that only contain vectors are passed just like
  // vectors; strip them off if present.
  if (const Type *InnerTy = isSingleElementStruct(Ty, getContext()))
    Ty = QualType(InnerTy, 0);

  llvm::Type *IRType = CGT.ConvertType(Ty);
  if (isa<llvm::VectorType>(IRType) ||
      IRType->getTypeID() == llvm::Type::FP128TyID)
    return IRType;

  // We couldn't find the preferred IR vector type for 'Ty'.
  uint64_t Size = getContext().getTypeSize(Ty);
  assert((Size == 128 || Size == 256 || Size == 512) && "Invalid type found!");

  // Return a LLVM IR vector type based on the size of 'Ty'.
  return llvm::VectorType::get(llvm::Type::getDoubleTy(getVMContext()),
                               Size / 64);
}

/// BitsContainNoUserData - Return true if the specified [start,end) bit range
/// is known to either be off the end of the specified type or being in
/// alignment padding.  The user type specified is known to be at most 128 bits
/// in size, and have passed through X86_64ABIInfo::classify with a successful
/// classification that put one of the two halves in the INTEGER class.
///
/// It is conservatively correct to return false.
static bool BitsContainNoUserData(QualType Ty, unsigned StartBit,
                                  unsigned EndBit, ASTContext &Context) {
  // If the bytes being queried are off the end of the type, there is no user
  // data hiding here.  This handles analysis of builtins, vectors and other
  // types that don't contain interesting padding.
  unsigned TySize = (unsigned)Context.getTypeSize(Ty);
  if (TySize <= StartBit)
    return true;

  if (const ConstantArrayType *AT = Context.getAsConstantArrayType(Ty)) {
    unsigned EltSize = (unsigned)Context.getTypeSize(AT->getElementType());
    unsigned NumElts = (unsigned)AT->getSize().getZExtValue();

    // Check each element to see if the element overlaps with the queried range.
    for (unsigned i = 0; i != NumElts; ++i) {
      // If the element is after the span we care about, then we're done..
      unsigned EltOffset = i*EltSize;
      if (EltOffset >= EndBit) break;

      unsigned EltStart = EltOffset < StartBit ? StartBit-EltOffset :0;
      if (!BitsContainNoUserData(AT->getElementType(), EltStart,
                                 EndBit-EltOffset, Context))
        return false;
    }
    // If it overlaps no elements, then it is safe to process as padding.
    return true;
  }

  if (const RecordType *RT = Ty->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);

    // If this is a C++ record, check the bases first.
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      for (const auto &I : CXXRD->bases()) {
        assert(!I.isVirtual() && !I.getType()->isDependentType() &&
               "Unexpected base class!");
        const CXXRecordDecl *Base =
          cast<CXXRecordDecl>(I.getType()->getAs<RecordType>()->getDecl());

        // If the base is after the span we care about, ignore it.
        unsigned BaseOffset = Context.toBits(Layout.getBaseClassOffset(Base));
        if (BaseOffset >= EndBit) continue;

        unsigned BaseStart = BaseOffset < StartBit ? StartBit-BaseOffset :0;
        if (!BitsContainNoUserData(I.getType(), BaseStart,
                                   EndBit-BaseOffset, Context))
          return false;
      }
    }

    // Verify that no field has data that overlaps the region of interest.  Yes
    // this could be sped up a lot by being smarter about queried fields,
    // however we're only looking at structs up to 16 bytes, so we don't care
    // much.
    unsigned idx = 0;
    for (RecordDecl::field_iterator i = RD->field_begin(), e = RD->field_end();
         i != e; ++i, ++idx) {
      unsigned FieldOffset = (unsigned)Layout.getFieldOffset(idx);

      // If we found a field after the region we care about, then we're done.
      if (FieldOffset >= EndBit) break;

      unsigned FieldStart = FieldOffset < StartBit ? StartBit-FieldOffset :0;
      if (!BitsContainNoUserData(i->getType(), FieldStart, EndBit-FieldOffset,
                                 Context))
        return false;
    }

    // If nothing in this record overlapped the area of interest, then we're
    // clean.
    return true;
  }

  return false;
}

/// ContainsFloatAtOffset - Return true if the specified LLVM IR type has a
/// float member at the specified offset.  For example, {int,{float}} has a
/// float at offset 4.  It is conservatively correct for this routine to return
/// false.
static bool ContainsFloatAtOffset(llvm::Type *IRType, unsigned IROffset,
                                  const llvm::DataLayout &TD) {
  // Base case if we find a float.
  if (IROffset == 0 && IRType->isFloatTy())
    return true;

  // If this is a struct, recurse into the field at the specified offset.
  if (llvm::StructType *STy = dyn_cast<llvm::StructType>(IRType)) {
    const llvm::StructLayout *SL = TD.getStructLayout(STy);
    unsigned Elt = SL->getElementContainingOffset(IROffset);
    IROffset -= SL->getElementOffset(Elt);
    return ContainsFloatAtOffset(STy->getElementType(Elt), IROffset, TD);
  }

  // If this is an array, recurse into the field at the specified offset.
  if (llvm::ArrayType *ATy = dyn_cast<llvm::ArrayType>(IRType)) {
    llvm::Type *EltTy = ATy->getElementType();
    unsigned EltSize = TD.getTypeAllocSize(EltTy);
    IROffset -= IROffset/EltSize*EltSize;
    return ContainsFloatAtOffset(EltTy, IROffset, TD);
  }

  return false;
}


/// GetSSETypeAtOffset - Return a type that will be passed by the backend in the
/// low 8 bytes of an XMM register, corresponding to the SSE class.
llvm::Type *X86_64ABIInfo::
GetSSETypeAtOffset(llvm::Type *IRType, unsigned IROffset,
                   QualType SourceTy, unsigned SourceOffset) const {
  // The only three choices we have are either double, <2 x float>, or float. We
  // pass as float if the last 4 bytes is just padding.  This happens for
  // structs that contain 3 floats.
  if (BitsContainNoUserData(SourceTy, SourceOffset*8+32,
                            SourceOffset*8+64, getContext()))
    return llvm::Type::getFloatTy(getVMContext());

  // We want to pass as <2 x float> if the LLVM IR type contains a float at
  // offset+0 and offset+4.  Walk the LLVM IR type to find out if this is the
  // case.
  if (ContainsFloatAtOffset(IRType, IROffset, getDataLayout()) &&
      ContainsFloatAtOffset(IRType, IROffset+4, getDataLayout()))
    return llvm::VectorType::get(llvm::Type::getFloatTy(getVMContext()), 2);

  return llvm::Type::getDoubleTy(getVMContext());
}


/// GetINTEGERTypeAtOffset - The ABI specifies that a value should be passed in
/// an 8-byte GPR.  This means that we either have a scalar or we are talking
/// about the high or low part of an up-to-16-byte struct.  This routine picks
/// the best LLVM IR type to represent this, which may be i64 or may be anything
/// else that the backend will pass in a GPR that works better (e.g. i8, %foo*,
/// etc).
///
/// PrefType is an LLVM IR type that corresponds to (part of) the IR type for
/// the source type.  IROffset is an offset in bytes into the LLVM IR type that
/// the 8-byte value references.  PrefType may be null.
///
/// SourceTy is the source-level type for the entire argument.  SourceOffset is
/// an offset into this that we're processing (which is always either 0 or 8).
///
llvm::Type *X86_64ABIInfo::
GetINTEGERTypeAtOffset(llvm::Type *IRType, unsigned IROffset,
                       QualType SourceTy, unsigned SourceOffset) const {
  // If we're dealing with an un-offset LLVM IR type, then it means that we're
  // returning an 8-byte unit starting with it.  See if we can safely use it.
  if (IROffset == 0) {
    // Pointers and int64's always fill the 8-byte unit.
    if ((isa<llvm::PointerType>(IRType) && Has64BitPointers) ||
        IRType->isIntegerTy(64))
      return IRType;

    // If we have a 1/2/4-byte integer, we can use it only if the rest of the
    // goodness in the source type is just tail padding.  This is allowed to
    // kick in for struct {double,int} on the int, but not on
    // struct{double,int,int} because we wouldn't return the second int.  We
    // have to do this analysis on the source type because we can't depend on
    // unions being lowered a specific way etc.
    if (IRType->isIntegerTy(8) || IRType->isIntegerTy(16) ||
        IRType->isIntegerTy(32) ||
        (isa<llvm::PointerType>(IRType) && !Has64BitPointers)) {
      unsigned BitWidth = isa<llvm::PointerType>(IRType) ? 32 :
          cast<llvm::IntegerType>(IRType)->getBitWidth();

      if (BitsContainNoUserData(SourceTy, SourceOffset*8+BitWidth,
                                SourceOffset*8+64, getContext()))
        return IRType;
    }
  }

  if (llvm::StructType *STy = dyn_cast<llvm::StructType>(IRType)) {
    // If this is a struct, recurse into the field at the specified offset.
    const llvm::StructLayout *SL = getDataLayout().getStructLayout(STy);
    if (IROffset < SL->getSizeInBytes()) {
      unsigned FieldIdx = SL->getElementContainingOffset(IROffset);
      IROffset -= SL->getElementOffset(FieldIdx);

      return GetINTEGERTypeAtOffset(STy->getElementType(FieldIdx), IROffset,
                                    SourceTy, SourceOffset);
    }
  }

  if (llvm::ArrayType *ATy = dyn_cast<llvm::ArrayType>(IRType)) {
    llvm::Type *EltTy = ATy->getElementType();
    unsigned EltSize = getDataLayout().getTypeAllocSize(EltTy);
    unsigned EltOffset = IROffset/EltSize*EltSize;
    return GetINTEGERTypeAtOffset(EltTy, IROffset-EltOffset, SourceTy,
                                  SourceOffset);
  }

  // Okay, we don't have any better idea of what to pass, so we pass this in an
  // integer register that isn't too big to fit the rest of the struct.
  unsigned TySizeInBytes =
    (unsigned)getContext().getTypeSizeInChars(SourceTy).getQuantity();

  assert(TySizeInBytes != SourceOffset && "Empty field?");

  // It is always safe to classify this as an integer type up to i64 that
  // isn't larger than the structure.
  return llvm::IntegerType::get(getVMContext(),
                                std::min(TySizeInBytes-SourceOffset, 8U)*8);
}


/// GetX86_64ByValArgumentPair - Given a high and low type that can ideally
/// be used as elements of a two register pair to pass or return, return a
/// first class aggregate to represent them.  For example, if the low part of
/// a by-value argument should be passed as i32* and the high part as float,
/// return {i32*, float}.
static llvm::Type *
GetX86_64ByValArgumentPair(llvm::Type *Lo, llvm::Type *Hi,
                           const llvm::DataLayout &TD) {
  // In order to correctly satisfy the ABI, we need to the high part to start
  // at offset 8.  If the high and low parts we inferred are both 4-byte types
  // (e.g. i32 and i32) then the resultant struct type ({i32,i32}) won't have
  // the second element at offset 8.  Check for this:
  unsigned LoSize = (unsigned)TD.getTypeAllocSize(Lo);
  unsigned HiAlign = TD.getABITypeAlignment(Hi);
  unsigned HiStart = llvm::alignTo(LoSize, HiAlign);
  assert(HiStart != 0 && HiStart <= 8 && "Invalid x86-64 argument pair!");

  // To handle this, we have to increase the size of the low part so that the
  // second element will start at an 8 byte offset.  We can't increase the size
  // of the second element because it might make us access off the end of the
  // struct.
  if (HiStart != 8) {
    // There are usually two sorts of types the ABI generation code can produce
    // for the low part of a pair that aren't 8 bytes in size: float or
    // i8/i16/i32.  This can also include pointers when they are 32-bit (X32 and
    // NaCl).
    // Promote these to a larger type.
    if (Lo->isFloatTy())
      Lo = llvm::Type::getDoubleTy(Lo->getContext());
    else {
      assert((Lo->isIntegerTy() || Lo->isPointerTy())
             && "Invalid/unknown lo type");
      Lo = llvm::Type::getInt64Ty(Lo->getContext());
    }
  }

  llvm::StructType *Result = llvm::StructType::get(Lo, Hi);

  // Verify that the second element is at an 8-byte offset.
  assert(TD.getStructLayout(Result)->getElementOffset(1) == 8 &&
         "Invalid x86-64 argument pair!");
  return Result;
}

ABIArgInfo X86_64ABIInfo::
classifyReturnType(QualType RetTy) const {
  // AMD64-ABI 3.2.3p4: Rule 1. Classify the return type with the
  // classification algorithm.
  X86_64ABIInfo::Class Lo, Hi;
  classify(RetTy, 0, Lo, Hi, /*isNamedArg*/ true);

  // Check some invariants.
  assert((Hi != Memory || Lo == Memory) && "Invalid memory classification.");
  assert((Hi != SSEUp || Lo == SSE) && "Invalid SSEUp classification.");

  llvm::Type *ResType = nullptr;
  switch (Lo) {
  case NoClass:
    if (Hi == NoClass)
      return ABIArgInfo::getIgnore();
    // If the low part is just padding, it takes no register, leave ResType
    // null.
    assert((Hi == SSE || Hi == Integer || Hi == X87Up) &&
           "Unknown missing lo part");
    break;

  case SSEUp:
  case X87Up:
    llvm_unreachable("Invalid classification for lo word.");

    // AMD64-ABI 3.2.3p4: Rule 2. Types of class memory are returned via
    // hidden argument.
  case Memory:
    return getIndirectReturnResult(RetTy);

    // AMD64-ABI 3.2.3p4: Rule 3. If the class is INTEGER, the next
    // available register of the sequence %rax, %rdx is used.
  case Integer:
    ResType = GetINTEGERTypeAtOffset(CGT.ConvertType(RetTy), 0, RetTy, 0);

    // If we have a sign or zero extended integer, make sure to return Extend
    // so that the parameter gets the right LLVM IR attributes.
    if (Hi == NoClass && isa<llvm::IntegerType>(ResType)) {
      // Treat an enum type as its underlying type.
      if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
        RetTy = EnumTy->getDecl()->getIntegerType();

      if (RetTy->isIntegralOrEnumerationType() &&
          RetTy->isPromotableIntegerType())
        return ABIArgInfo::getExtend(RetTy);
    }
    break;

    // AMD64-ABI 3.2.3p4: Rule 4. If the class is SSE, the next
    // available SSE register of the sequence %xmm0, %xmm1 is used.
  case SSE:
    ResType = GetSSETypeAtOffset(CGT.ConvertType(RetTy), 0, RetTy, 0);
    break;

    // AMD64-ABI 3.2.3p4: Rule 6. If the class is X87, the value is
    // returned on the X87 stack in %st0 as 80-bit x87 number.
  case X87:
    ResType = llvm::Type::getX86_FP80Ty(getVMContext());
    break;

    // AMD64-ABI 3.2.3p4: Rule 8. If the class is COMPLEX_X87, the real
    // part of the value is returned in %st0 and the imaginary part in
    // %st1.
  case ComplexX87:
    assert(Hi == ComplexX87 && "Unexpected ComplexX87 classification.");
    ResType = llvm::StructType::get(llvm::Type::getX86_FP80Ty(getVMContext()),
                                    llvm::Type::getX86_FP80Ty(getVMContext()));
    break;
  }

  llvm::Type *HighPart = nullptr;
  switch (Hi) {
    // Memory was handled previously and X87 should
    // never occur as a hi class.
  case Memory:
  case X87:
    llvm_unreachable("Invalid classification for hi word.");

  case ComplexX87: // Previously handled.
  case NoClass:
    break;

  case Integer:
    HighPart = GetINTEGERTypeAtOffset(CGT.ConvertType(RetTy), 8, RetTy, 8);
    if (Lo == NoClass)  // Return HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;
  case SSE:
    HighPart = GetSSETypeAtOffset(CGT.ConvertType(RetTy), 8, RetTy, 8);
    if (Lo == NoClass)  // Return HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;

    // AMD64-ABI 3.2.3p4: Rule 5. If the class is SSEUP, the eightbyte
    // is passed in the next available eightbyte chunk if the last used
    // vector register.
    //
    // SSEUP should always be preceded by SSE, just widen.
  case SSEUp:
    assert(Lo == SSE && "Unexpected SSEUp classification.");
    ResType = GetByteVectorType(RetTy);
    break;

    // AMD64-ABI 3.2.3p4: Rule 7. If the class is X87UP, the value is
    // returned together with the previous X87 value in %st0.
  case X87Up:
    // If X87Up is preceded by X87, we don't need to do
    // anything. However, in some cases with unions it may not be
    // preceded by X87. In such situations we follow gcc and pass the
    // extra bits in an SSE reg.
    if (Lo != X87) {
      HighPart = GetSSETypeAtOffset(CGT.ConvertType(RetTy), 8, RetTy, 8);
      if (Lo == NoClass)  // Return HighPart at offset 8 in memory.
        return ABIArgInfo::getDirect(HighPart, 8);
    }
    break;
  }

  // If a high part was specified, merge it together with the low part.  It is
  // known to pass in the high eightbyte of the result.  We do this by forming a
  // first class struct aggregate with the high and low part: {low, high}
  if (HighPart)
    ResType = GetX86_64ByValArgumentPair(ResType, HighPart, getDataLayout());

  return ABIArgInfo::getDirect(ResType);
}

ABIArgInfo X86_64ABIInfo::classifyArgumentType(
  QualType Ty, unsigned freeIntRegs, unsigned &neededInt, unsigned &neededSSE,
  bool isNamedArg)
  const
{
  Ty = useFirstFieldIfTransparentUnion(Ty);

  X86_64ABIInfo::Class Lo, Hi;
  classify(Ty, 0, Lo, Hi, isNamedArg);

  // Check some invariants.
  // FIXME: Enforce these by construction.
  assert((Hi != Memory || Lo == Memory) && "Invalid memory classification.");
  assert((Hi != SSEUp || Lo == SSE) && "Invalid SSEUp classification.");

  neededInt = 0;
  neededSSE = 0;
  llvm::Type *ResType = nullptr;
  switch (Lo) {
  case NoClass:
    if (Hi == NoClass)
      return ABIArgInfo::getIgnore();
    // If the low part is just padding, it takes no register, leave ResType
    // null.
    assert((Hi == SSE || Hi == Integer || Hi == X87Up) &&
           "Unknown missing lo part");
    break;

    // AMD64-ABI 3.2.3p3: Rule 1. If the class is MEMORY, pass the argument
    // on the stack.
  case Memory:

    // AMD64-ABI 3.2.3p3: Rule 5. If the class is X87, X87UP or
    // COMPLEX_X87, it is passed in memory.
  case X87:
  case ComplexX87:
    if (getRecordArgABI(Ty, getCXXABI()) == CGCXXABI::RAA_Indirect)
      ++neededInt;
    return getIndirectResult(Ty, freeIntRegs);

  case SSEUp:
  case X87Up:
    llvm_unreachable("Invalid classification for lo word.");

    // AMD64-ABI 3.2.3p3: Rule 2. If the class is INTEGER, the next
    // available register of the sequence %rdi, %rsi, %rdx, %rcx, %r8
    // and %r9 is used.
  case Integer:
    ++neededInt;

    // Pick an 8-byte type based on the preferred type.
    ResType = GetINTEGERTypeAtOffset(CGT.ConvertType(Ty), 0, Ty, 0);

    // If we have a sign or zero extended integer, make sure to return Extend
    // so that the parameter gets the right LLVM IR attributes.
    if (Hi == NoClass && isa<llvm::IntegerType>(ResType)) {
      // Treat an enum type as its underlying type.
      if (const EnumType *EnumTy = Ty->getAs<EnumType>())
        Ty = EnumTy->getDecl()->getIntegerType();

      if (Ty->isIntegralOrEnumerationType() &&
          Ty->isPromotableIntegerType())
        return ABIArgInfo::getExtend(Ty);
    }

    break;

    // AMD64-ABI 3.2.3p3: Rule 3. If the class is SSE, the next
    // available SSE register is used, the registers are taken in the
    // order from %xmm0 to %xmm7.
  case SSE: {
    llvm::Type *IRType = CGT.ConvertType(Ty);
    ResType = GetSSETypeAtOffset(IRType, 0, Ty, 0);
    ++neededSSE;
    break;
  }
  }

  llvm::Type *HighPart = nullptr;
  switch (Hi) {
    // Memory was handled previously, ComplexX87 and X87 should
    // never occur as hi classes, and X87Up must be preceded by X87,
    // which is passed in memory.
  case Memory:
  case X87:
  case ComplexX87:
    llvm_unreachable("Invalid classification for hi word.");

  case NoClass: break;

  case Integer:
    ++neededInt;
    // Pick an 8-byte type based on the preferred type.
    HighPart = GetINTEGERTypeAtOffset(CGT.ConvertType(Ty), 8, Ty, 8);

    if (Lo == NoClass)  // Pass HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;

    // X87Up generally doesn't occur here (long double is passed in
    // memory), except in situations involving unions.
  case X87Up:
  case SSE:
    HighPart = GetSSETypeAtOffset(CGT.ConvertType(Ty), 8, Ty, 8);

    if (Lo == NoClass)  // Pass HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);

    ++neededSSE;
    break;

    // AMD64-ABI 3.2.3p3: Rule 4. If the class is SSEUP, the
    // eightbyte is passed in the upper half of the last used SSE
    // register.  This only happens when 128-bit vectors are passed.
  case SSEUp:
    assert(Lo == SSE && "Unexpected SSEUp classification");
    ResType = GetByteVectorType(Ty);
    break;
  }

  // If a high part was specified, merge it together with the low part.  It is
  // known to pass in the high eightbyte of the result.  We do this by forming a
  // first class struct aggregate with the high and low part: {low, high}
  if (HighPart)
    ResType = GetX86_64ByValArgumentPair(ResType, HighPart, getDataLayout());

  return ABIArgInfo::getDirect(ResType);
}

ABIArgInfo
X86_64ABIInfo::classifyRegCallStructTypeImpl(QualType Ty, unsigned &NeededInt,
                                             unsigned &NeededSSE) const {
  auto RT = Ty->getAs<RecordType>();
  assert(RT && "classifyRegCallStructType only valid with struct types");

  if (RT->getDecl()->hasFlexibleArrayMember())
    return getIndirectReturnResult(Ty);

  // Sum up bases
  if (auto CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
    if (CXXRD->isDynamicClass()) {
      NeededInt = NeededSSE = 0;
      return getIndirectReturnResult(Ty);
    }

    for (const auto &I : CXXRD->bases())
      if (classifyRegCallStructTypeImpl(I.getType(), NeededInt, NeededSSE)
              .isIndirect()) {
        NeededInt = NeededSSE = 0;
        return getIndirectReturnResult(Ty);
      }
  }

  // Sum up members
  for (const auto *FD : RT->getDecl()->fields()) {
    if (FD->getType()->isRecordType() && !FD->getType()->isUnionType()) {
      if (classifyRegCallStructTypeImpl(FD->getType(), NeededInt, NeededSSE)
              .isIndirect()) {
        NeededInt = NeededSSE = 0;
        return getIndirectReturnResult(Ty);
      }
    } else {
      unsigned LocalNeededInt, LocalNeededSSE;
      if (classifyArgumentType(FD->getType(), UINT_MAX, LocalNeededInt,
                               LocalNeededSSE, true)
              .isIndirect()) {
        NeededInt = NeededSSE = 0;
        return getIndirectReturnResult(Ty);
      }
      NeededInt += LocalNeededInt;
      NeededSSE += LocalNeededSSE;
    }
  }

  return ABIArgInfo::getDirect();
}

ABIArgInfo X86_64ABIInfo::classifyRegCallStructType(QualType Ty,
                                                    unsigned &NeededInt,
                                                    unsigned &NeededSSE) const {

  NeededInt = 0;
  NeededSSE = 0;

  return classifyRegCallStructTypeImpl(Ty, NeededInt, NeededSSE);
}

void X86_64ABIInfo::computeInfo(CGFunctionInfo &FI) const {

  const unsigned CallingConv = FI.getCallingConvention();
  // It is possible to force Win64 calling convention on any x86_64 target by
  // using __attribute__((ms_abi)). In such case to correctly emit Win64
  // compatible code delegate this call to WinX86_64ABIInfo::computeInfo.
  if (CallingConv == llvm::CallingConv::Win64) {
    WinX86_64ABIInfo Win64ABIInfo(CGT);
    Win64ABIInfo.computeInfo(FI);
    return;
  }

  bool IsRegCall = CallingConv == llvm::CallingConv::X86_RegCall;

  // Keep track of the number of assigned registers.
  unsigned FreeIntRegs = IsRegCall ? 11 : 6;
  unsigned FreeSSERegs = IsRegCall ? 16 : 8;
  unsigned NeededInt, NeededSSE;

  if (!::classifyReturnType(getCXXABI(), FI, *this)) {
    if (IsRegCall && FI.getReturnType()->getTypePtr()->isRecordType() &&
        !FI.getReturnType()->getTypePtr()->isUnionType()) {
      FI.getReturnInfo() =
          classifyRegCallStructType(FI.getReturnType(), NeededInt, NeededSSE);
      if (FreeIntRegs >= NeededInt && FreeSSERegs >= NeededSSE) {
        FreeIntRegs -= NeededInt;
        FreeSSERegs -= NeededSSE;
      } else {
        FI.getReturnInfo() = getIndirectReturnResult(FI.getReturnType());
      }
    } else if (IsRegCall && FI.getReturnType()->getAs<ComplexType>()) {
      // Complex Long Double Type is passed in Memory when Regcall
      // calling convention is used.
      const ComplexType *CT = FI.getReturnType()->getAs<ComplexType>();
      if (getContext().getCanonicalType(CT->getElementType()) ==
          getContext().LongDoubleTy)
        FI.getReturnInfo() = getIndirectReturnResult(FI.getReturnType());
    } else
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  }

  // If the return value is indirect, then the hidden argument is consuming one
  // integer register.
  if (FI.getReturnInfo().isIndirect())
    --FreeIntRegs;

  // The chain argument effectively gives us another free register.
  if (FI.isChainCall())
    ++FreeIntRegs;

  unsigned NumRequiredArgs = FI.getNumRequiredArgs();
  // AMD64-ABI 3.2.3p3: Once arguments are classified, the registers
  // get assigned (in left-to-right order) for passing as follows...
  unsigned ArgNo = 0;
  for (CGFunctionInfo::arg_iterator it = FI.arg_begin(), ie = FI.arg_end();
       it != ie; ++it, ++ArgNo) {
    bool IsNamedArg = ArgNo < NumRequiredArgs;

    if (IsRegCall && it->type->isStructureOrClassType())
      it->info = classifyRegCallStructType(it->type, NeededInt, NeededSSE);
    else
      it->info = classifyArgumentType(it->type, FreeIntRegs, NeededInt,
                                      NeededSSE, IsNamedArg);

    // AMD64-ABI 3.2.3p3: If there are no registers available for any
    // eightbyte of an argument, the whole argument is passed on the
    // stack. If registers have already been assigned for some
    // eightbytes of such an argument, the assignments get reverted.
    if (FreeIntRegs >= NeededInt && FreeSSERegs >= NeededSSE) {
      FreeIntRegs -= NeededInt;
      FreeSSERegs -= NeededSSE;
    } else {
      it->info = getIndirectResult(it->type, FreeIntRegs);
    }
  }
}

static Address EmitX86_64VAArgFromMemory(CodeGenFunction &CGF,
                                         Address VAListAddr, QualType Ty) {
  Address overflow_arg_area_p = CGF.Builder.CreateStructGEP(
      VAListAddr, 2, CharUnits::fromQuantity(8), "overflow_arg_area_p");
  llvm::Value *overflow_arg_area =
    CGF.Builder.CreateLoad(overflow_arg_area_p, "overflow_arg_area");

  // AMD64-ABI 3.5.7p5: Step 7. Align l->overflow_arg_area upwards to a 16
  // byte boundary if alignment needed by type exceeds 8 byte boundary.
  // It isn't stated explicitly in the standard, but in practice we use
  // alignment greater than 16 where necessary.
  CharUnits Align = CGF.getContext().getTypeAlignInChars(Ty);
  if (Align > CharUnits::fromQuantity(8)) {
    overflow_arg_area = emitRoundPointerUpToAlignment(CGF, overflow_arg_area,
                                                      Align);
  }

  // AMD64-ABI 3.5.7p5: Step 8. Fetch type from l->overflow_arg_area.
  llvm::Type *LTy = CGF.ConvertTypeForMem(Ty);
  llvm::Value *Res =
    CGF.Builder.CreateBitCast(overflow_arg_area,
                              llvm::PointerType::getUnqual(LTy));

  // AMD64-ABI 3.5.7p5: Step 9. Set l->overflow_arg_area to:
  // l->overflow_arg_area + sizeof(type).
  // AMD64-ABI 3.5.7p5: Step 10. Align l->overflow_arg_area upwards to
  // an 8 byte boundary.

  uint64_t SizeInBytes = (CGF.getContext().getTypeSize(Ty) + 7) / 8;
  llvm::Value *Offset =
      llvm::ConstantInt::get(CGF.Int32Ty, (SizeInBytes + 7)  & ~7);
  overflow_arg_area = CGF.Builder.CreateGEP(overflow_arg_area, Offset,
                                            "overflow_arg_area.next");
  CGF.Builder.CreateStore(overflow_arg_area, overflow_arg_area_p);

  // AMD64-ABI 3.5.7p5: Step 11. Return the fetched type.
  return Address(Res, Align);
}

Address X86_64ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                 QualType Ty) const {
  // Assume that va_list type is correct; should be pointer to LLVM type:
  // struct {
  //   i32 gp_offset;
  //   i32 fp_offset;
  //   i8* overflow_arg_area;
  //   i8* reg_save_area;
  // };
  unsigned neededInt, neededSSE;

  Ty = getContext().getCanonicalType(Ty);
  ABIArgInfo AI = classifyArgumentType(Ty, 0, neededInt, neededSSE,
                                       /*isNamedArg*/false);

  // AMD64-ABI 3.5.7p5: Step 1. Determine whether type may be passed
  // in the registers. If not go to step 7.
  if (!neededInt && !neededSSE)
    return EmitX86_64VAArgFromMemory(CGF, VAListAddr, Ty);

  // AMD64-ABI 3.5.7p5: Step 2. Compute num_gp to hold the number of
  // general purpose registers needed to pass type and num_fp to hold
  // the number of floating point registers needed.

  // AMD64-ABI 3.5.7p5: Step 3. Verify whether arguments fit into
  // registers. In the case: l->gp_offset > 48 - num_gp * 8 or
  // l->fp_offset > 304 - num_fp * 16 go to step 7.
  //
  // NOTE: 304 is a typo, there are (6 * 8 + 8 * 16) = 176 bytes of
  // register save space).

  llvm::Value *InRegs = nullptr;
  Address gp_offset_p = Address::invalid(), fp_offset_p = Address::invalid();
  llvm::Value *gp_offset = nullptr, *fp_offset = nullptr;
  if (neededInt) {
    gp_offset_p =
        CGF.Builder.CreateStructGEP(VAListAddr, 0, CharUnits::Zero(),
                                    "gp_offset_p");
    gp_offset = CGF.Builder.CreateLoad(gp_offset_p, "gp_offset");
    InRegs = llvm::ConstantInt::get(CGF.Int32Ty, 48 - neededInt * 8);
    InRegs = CGF.Builder.CreateICmpULE(gp_offset, InRegs, "fits_in_gp");
  }

  if (neededSSE) {
    fp_offset_p =
        CGF.Builder.CreateStructGEP(VAListAddr, 1, CharUnits::fromQuantity(4),
                                    "fp_offset_p");
    fp_offset = CGF.Builder.CreateLoad(fp_offset_p, "fp_offset");
    llvm::Value *FitsInFP =
      llvm::ConstantInt::get(CGF.Int32Ty, 176 - neededSSE * 16);
    FitsInFP = CGF.Builder.CreateICmpULE(fp_offset, FitsInFP, "fits_in_fp");
    InRegs = InRegs ? CGF.Builder.CreateAnd(InRegs, FitsInFP) : FitsInFP;
  }

  llvm::BasicBlock *InRegBlock = CGF.createBasicBlock("vaarg.in_reg");
  llvm::BasicBlock *InMemBlock = CGF.createBasicBlock("vaarg.in_mem");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("vaarg.end");
  CGF.Builder.CreateCondBr(InRegs, InRegBlock, InMemBlock);

  // Emit code to load the value if it was passed in registers.

  CGF.EmitBlock(InRegBlock);

  // AMD64-ABI 3.5.7p5: Step 4. Fetch type from l->reg_save_area with
  // an offset of l->gp_offset and/or l->fp_offset. This may require
  // copying to a temporary location in case the parameter is passed
  // in different register classes or requires an alignment greater
  // than 8 for general purpose registers and 16 for XMM registers.
  //
  // FIXME: This really results in shameful code when we end up needing to
  // collect arguments from different places; often what should result in a
  // simple assembling of a structure from scattered addresses has many more
  // loads than necessary. Can we clean this up?
  llvm::Type *LTy = CGF.ConvertTypeForMem(Ty);
  llvm::Value *RegSaveArea = CGF.Builder.CreateLoad(
      CGF.Builder.CreateStructGEP(VAListAddr, 3, CharUnits::fromQuantity(16)),
                                  "reg_save_area");

  Address RegAddr = Address::invalid();
  if (neededInt && neededSSE) {
    // FIXME: Cleanup.
    assert(AI.isDirect() && "Unexpected ABI info for mixed regs");
    llvm::StructType *ST = cast<llvm::StructType>(AI.getCoerceToType());
    Address Tmp = CGF.CreateMemTemp(Ty);
    Tmp = CGF.Builder.CreateElementBitCast(Tmp, ST);
    assert(ST->getNumElements() == 2 && "Unexpected ABI info for mixed regs");
    llvm::Type *TyLo = ST->getElementType(0);
    llvm::Type *TyHi = ST->getElementType(1);
    assert((TyLo->isFPOrFPVectorTy() ^ TyHi->isFPOrFPVectorTy()) &&
           "Unexpected ABI info for mixed regs");
    llvm::Type *PTyLo = llvm::PointerType::getUnqual(TyLo);
    llvm::Type *PTyHi = llvm::PointerType::getUnqual(TyHi);
    llvm::Value *GPAddr = CGF.Builder.CreateGEP(RegSaveArea, gp_offset);
    llvm::Value *FPAddr = CGF.Builder.CreateGEP(RegSaveArea, fp_offset);
    llvm::Value *RegLoAddr = TyLo->isFPOrFPVectorTy() ? FPAddr : GPAddr;
    llvm::Value *RegHiAddr = TyLo->isFPOrFPVectorTy() ? GPAddr : FPAddr;

    // Copy the first element.
    // FIXME: Our choice of alignment here and below is probably pessimistic.
    llvm::Value *V = CGF.Builder.CreateAlignedLoad(
        TyLo, CGF.Builder.CreateBitCast(RegLoAddr, PTyLo),
        CharUnits::fromQuantity(getDataLayout().getABITypeAlignment(TyLo)));
    CGF.Builder.CreateStore(V,
                    CGF.Builder.CreateStructGEP(Tmp, 0, CharUnits::Zero()));

    // Copy the second element.
    V = CGF.Builder.CreateAlignedLoad(
        TyHi, CGF.Builder.CreateBitCast(RegHiAddr, PTyHi),
        CharUnits::fromQuantity(getDataLayout().getABITypeAlignment(TyHi)));
    CharUnits Offset = CharUnits::fromQuantity(
                   getDataLayout().getStructLayout(ST)->getElementOffset(1));
    CGF.Builder.CreateStore(V, CGF.Builder.CreateStructGEP(Tmp, 1, Offset));

    RegAddr = CGF.Builder.CreateElementBitCast(Tmp, LTy);
  } else if (neededInt) {
    RegAddr = Address(CGF.Builder.CreateGEP(RegSaveArea, gp_offset),
                      CharUnits::fromQuantity(8));
    RegAddr = CGF.Builder.CreateElementBitCast(RegAddr, LTy);

    // Copy to a temporary if necessary to ensure the appropriate alignment.
    std::pair<CharUnits, CharUnits> SizeAlign =
        getContext().getTypeInfoInChars(Ty);
    uint64_t TySize = SizeAlign.first.getQuantity();
    CharUnits TyAlign = SizeAlign.second;

    // Copy into a temporary if the type is more aligned than the
    // register save area.
    if (TyAlign.getQuantity() > 8) {
      Address Tmp = CGF.CreateMemTemp(Ty);
      CGF.Builder.CreateMemCpy(Tmp, RegAddr, TySize, false);
      RegAddr = Tmp;
    }

  } else if (neededSSE == 1) {
    RegAddr = Address(CGF.Builder.CreateGEP(RegSaveArea, fp_offset),
                      CharUnits::fromQuantity(16));
    RegAddr = CGF.Builder.CreateElementBitCast(RegAddr, LTy);
  } else {
    assert(neededSSE == 2 && "Invalid number of needed registers!");
    // SSE registers are spaced 16 bytes apart in the register save
    // area, we need to collect the two eightbytes together.
    // The ABI isn't explicit about this, but it seems reasonable
    // to assume that the slots are 16-byte aligned, since the stack is
    // naturally 16-byte aligned and the prologue is expected to store
    // all the SSE registers to the RSA.
    Address RegAddrLo = Address(CGF.Builder.CreateGEP(RegSaveArea, fp_offset),
                                CharUnits::fromQuantity(16));
    Address RegAddrHi =
      CGF.Builder.CreateConstInBoundsByteGEP(RegAddrLo,
                                             CharUnits::fromQuantity(16));
    llvm::Type *ST = AI.canHaveCoerceToType()
                         ? AI.getCoerceToType()
                         : llvm::StructType::get(CGF.DoubleTy, CGF.DoubleTy);
    llvm::Value *V;
    Address Tmp = CGF.CreateMemTemp(Ty);
    Tmp = CGF.Builder.CreateElementBitCast(Tmp, ST);
    V = CGF.Builder.CreateLoad(CGF.Builder.CreateElementBitCast(
        RegAddrLo, ST->getStructElementType(0)));
    CGF.Builder.CreateStore(V,
                   CGF.Builder.CreateStructGEP(Tmp, 0, CharUnits::Zero()));
    V = CGF.Builder.CreateLoad(CGF.Builder.CreateElementBitCast(
        RegAddrHi, ST->getStructElementType(1)));
    CGF.Builder.CreateStore(V,
          CGF.Builder.CreateStructGEP(Tmp, 1, CharUnits::fromQuantity(8)));

    RegAddr = CGF.Builder.CreateElementBitCast(Tmp, LTy);
  }

  // AMD64-ABI 3.5.7p5: Step 5. Set:
  // l->gp_offset = l->gp_offset + num_gp * 8
  // l->fp_offset = l->fp_offset + num_fp * 16.
  if (neededInt) {
    llvm::Value *Offset = llvm::ConstantInt::get(CGF.Int32Ty, neededInt * 8);
    CGF.Builder.CreateStore(CGF.Builder.CreateAdd(gp_offset, Offset),
                            gp_offset_p);
  }
  if (neededSSE) {
    llvm::Value *Offset = llvm::ConstantInt::get(CGF.Int32Ty, neededSSE * 16);
    CGF.Builder.CreateStore(CGF.Builder.CreateAdd(fp_offset, Offset),
                            fp_offset_p);
  }
  CGF.EmitBranch(ContBlock);

  // Emit code to load the value if it was passed in memory.

  CGF.EmitBlock(InMemBlock);
  Address MemAddr = EmitX86_64VAArgFromMemory(CGF, VAListAddr, Ty);

  // Return the appropriate result.

  CGF.EmitBlock(ContBlock);
  Address ResAddr = emitMergePHI(CGF, RegAddr, InRegBlock, MemAddr, InMemBlock,
                                 "vaarg.addr");
  return ResAddr;
}

Address X86_64ABIInfo::EmitMSVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                   QualType Ty) const {
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*indirect*/ false,
                          CGF.getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(8),
                          /*allowHigherAlign*/ false);
}

ABIArgInfo
WinX86_64ABIInfo::reclassifyHvaArgType(QualType Ty, unsigned &FreeSSERegs,
                                    const ABIArgInfo &current) const {
  // Assumes vectorCall calling convention.
  const Type *Base = nullptr;
  uint64_t NumElts = 0;

  if (!Ty->isBuiltinType() && !Ty->isVectorType() &&
      isHomogeneousAggregate(Ty, Base, NumElts) && FreeSSERegs >= NumElts) {
    FreeSSERegs -= NumElts;
    return getDirectX86Hva();
  }
  return current;
}

ABIArgInfo WinX86_64ABIInfo::classify(QualType Ty, unsigned &FreeSSERegs,
                                      bool IsReturnType, bool IsVectorCall,
                                      bool IsRegCall) const {

  if (Ty->isVoidType())
    return ABIArgInfo::getIgnore();

  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  TypeInfo Info = getContext().getTypeInfo(Ty);
  uint64_t Width = Info.Width;
  CharUnits Align = getContext().toCharUnitsFromBits(Info.Align);

  const RecordType *RT = Ty->getAs<RecordType>();
  if (RT) {
    if (!IsReturnType) {
      if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(RT, getCXXABI()))
        return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);
    }

    if (RT->getDecl()->hasFlexibleArrayMember())
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

  }

  const Type *Base = nullptr;
  uint64_t NumElts = 0;
  // vectorcall adds the concept of a homogenous vector aggregate, similar to
  // other targets.
  if ((IsVectorCall || IsRegCall) &&
      isHomogeneousAggregate(Ty, Base, NumElts)) {
    if (IsRegCall) {
      if (FreeSSERegs >= NumElts) {
        FreeSSERegs -= NumElts;
        if (IsReturnType || Ty->isBuiltinType() || Ty->isVectorType())
          return ABIArgInfo::getDirect();
        return ABIArgInfo::getExpand();
      }
      return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
    } else if (IsVectorCall) {
      if (FreeSSERegs >= NumElts &&
          (IsReturnType || Ty->isBuiltinType() || Ty->isVectorType())) {
        FreeSSERegs -= NumElts;
        return ABIArgInfo::getDirect();
      } else if (IsReturnType) {
        return ABIArgInfo::getExpand();
      } else if (!Ty->isBuiltinType() && !Ty->isVectorType()) {
        // HVAs are delayed and reclassified in the 2nd step.
        return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
      }
    }
  }

  if (Ty->isMemberPointerType()) {
    // If the member pointer is represented by an LLVM int or ptr, pass it
    // directly.
    llvm::Type *LLTy = CGT.ConvertType(Ty);
    if (LLTy->isPointerTy() || LLTy->isIntegerTy())
      return ABIArgInfo::getDirect();
  }

  if (RT || Ty->isAnyComplexType() || Ty->isMemberPointerType()) {
    // MS x64 ABI requirement: "Any argument that doesn't fit in 8 bytes, or is
    // not 1, 2, 4, or 8 bytes, must be passed by reference."
    if (Width > 64 || !llvm::isPowerOf2_64(Width))
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

    // Otherwise, coerce it to a small integer.
    return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(), Width));
  }

  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    switch (BT->getKind()) {
    case BuiltinType::Bool:
      // Bool type is always extended to the ABI, other builtin types are not
      // extended.
      return ABIArgInfo::getExtend(Ty);

    case BuiltinType::LongDouble:
      // Mingw64 GCC uses the old 80 bit extended precision floating point
      // unit. It passes them indirectly through memory.
      if (IsMingw64) {
        const llvm::fltSemantics *LDF = &getTarget().getLongDoubleFormat();
        if (LDF == &llvm::APFloat::x87DoubleExtended())
          return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
      }
      break;

    case BuiltinType::Int128:
    case BuiltinType::UInt128:
      // If it's a parameter type, the normal ABI rule is that arguments larger
      // than 8 bytes are passed indirectly. GCC follows it. We follow it too,
      // even though it isn't particularly efficient.
      if (!IsReturnType)
        return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);

      // Mingw64 GCC returns i128 in XMM0. Coerce to v2i64 to handle that.
      // Clang matches them for compatibility.
      return ABIArgInfo::getDirect(
          llvm::VectorType::get(llvm::Type::getInt64Ty(getVMContext()), 2));

    default:
      break;
    }
  }

  return ABIArgInfo::getDirect();
}

void WinX86_64ABIInfo::computeVectorCallArgs(CGFunctionInfo &FI,
                                             unsigned FreeSSERegs,
                                             bool IsVectorCall,
                                             bool IsRegCall) const {
  unsigned Count = 0;
  for (auto &I : FI.arguments()) {
    // Vectorcall in x64 only permits the first 6 arguments to be passed
    // as XMM/YMM registers.
    if (Count < VectorcallMaxParamNumAsReg)
      I.info = classify(I.type, FreeSSERegs, false, IsVectorCall, IsRegCall);
    else {
      // Since these cannot be passed in registers, pretend no registers
      // are left.
      unsigned ZeroSSERegsAvail = 0;
      I.info = classify(I.type, /*FreeSSERegs=*/ZeroSSERegsAvail, false,
                        IsVectorCall, IsRegCall);
    }
    ++Count;
  }

  for (auto &I : FI.arguments()) {
    I.info = reclassifyHvaArgType(I.type, FreeSSERegs, I.info);
  }
}

void WinX86_64ABIInfo::computeInfo(CGFunctionInfo &FI) const {
  bool IsVectorCall =
      FI.getCallingConvention() == llvm::CallingConv::X86_VectorCall;
  bool IsRegCall = FI.getCallingConvention() == llvm::CallingConv::X86_RegCall;

  unsigned FreeSSERegs = 0;
  if (IsVectorCall) {
    // We can use up to 4 SSE return registers with vectorcall.
    FreeSSERegs = 4;
  } else if (IsRegCall) {
    // RegCall gives us 16 SSE registers.
    FreeSSERegs = 16;
  }

  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classify(FI.getReturnType(), FreeSSERegs, true,
                                  IsVectorCall, IsRegCall);

  if (IsVectorCall) {
    // We can use up to 6 SSE register parameters with vectorcall.
    FreeSSERegs = 6;
  } else if (IsRegCall) {
    // RegCall gives us 16 SSE registers, we can reuse the return registers.
    FreeSSERegs = 16;
  }

  if (IsVectorCall) {
    computeVectorCallArgs(FI, FreeSSERegs, IsVectorCall, IsRegCall);
  } else {
    for (auto &I : FI.arguments())
      I.info = classify(I.type, FreeSSERegs, false, IsVectorCall, IsRegCall);
  }

}

Address WinX86_64ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                    QualType Ty) const {

  bool IsIndirect = false;

  // MS x64 ABI requirement: "Any argument that doesn't fit in 8 bytes, or is
  // not 1, 2, 4, or 8 bytes, must be passed by reference."
  if (isAggregateTypeForABI(Ty) || Ty->isMemberPointerType()) {
    uint64_t Width = getContext().getTypeSize(Ty);
    IsIndirect = Width > 64 || !llvm::isPowerOf2_64(Width);
  }

  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect,
                          CGF.getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(8),
                          /*allowHigherAlign*/ false);
}

// PowerPC-32
namespace {
/// PPC32_SVR4_ABIInfo - The 32-bit PowerPC ELF (SVR4) ABI information.
class PPC32_SVR4_ABIInfo : public DefaultABIInfo {
  bool IsSoftFloatABI;

  CharUnits getParamTypeAlignment(QualType Ty) const;

public:
  PPC32_SVR4_ABIInfo(CodeGen::CodeGenTypes &CGT, bool SoftFloatABI)
      : DefaultABIInfo(CGT), IsSoftFloatABI(SoftFloatABI) {}

  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;
};

class PPC32TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  PPC32TargetCodeGenInfo(CodeGenTypes &CGT, bool SoftFloatABI)
      : TargetCodeGenInfo(new PPC32_SVR4_ABIInfo(CGT, SoftFloatABI)) {}

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    // This is recovered from gcc output.
    return 1; // r1 is the dedicated stack pointer
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override;
};
}

CharUnits PPC32_SVR4_ABIInfo::getParamTypeAlignment(QualType Ty) const {
  // Complex types are passed just like their elements
  if (const ComplexType *CTy = Ty->getAs<ComplexType>())
    Ty = CTy->getElementType();

  if (Ty->isVectorType())
    return CharUnits::fromQuantity(getContext().getTypeSize(Ty) == 128 ? 16
                                                                       : 4);

  // For single-element float/vector structs, we consider the whole type
  // to have the same alignment requirements as its single element.
  const Type *AlignTy = nullptr;
  if (const Type *EltType = isSingleElementStruct(Ty, getContext())) {
    const BuiltinType *BT = EltType->getAs<BuiltinType>();
    if ((EltType->isVectorType() && getContext().getTypeSize(EltType) == 128) ||
        (BT && BT->isFloatingPoint()))
      AlignTy = EltType;
  }

  if (AlignTy)
    return CharUnits::fromQuantity(AlignTy->isVectorType() ? 16 : 4);
  return CharUnits::fromQuantity(4);
}

// TODO: this implementation is now likely redundant with
// DefaultABIInfo::EmitVAArg.
Address PPC32_SVR4_ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAList,
                                      QualType Ty) const {
  if (getTarget().getTriple().isOSDarwin()) {
    auto TI = getContext().getTypeInfoInChars(Ty);
    TI.second = getParamTypeAlignment(Ty);

    CharUnits SlotSize = CharUnits::fromQuantity(4);
    return emitVoidPtrVAArg(CGF, VAList, Ty,
                            classifyArgumentType(Ty).isIndirect(), TI, SlotSize,
                            /*AllowHigherAlign=*/true);
  }

  const unsigned OverflowLimit = 8;
  if (const ComplexType *CTy = Ty->getAs<ComplexType>()) {
    // TODO: Implement this. For now ignore.
    (void)CTy;
    return Address::invalid(); // FIXME?
  }

  // struct __va_list_tag {
  //   unsigned char gpr;
  //   unsigned char fpr;
  //   unsigned short reserved;
  //   void *overflow_arg_area;
  //   void *reg_save_area;
  // };

  bool isI64 = Ty->isIntegerType() && getContext().getTypeSize(Ty) == 64;
  bool isInt =
      Ty->isIntegerType() || Ty->isPointerType() || Ty->isAggregateType();
  bool isF64 = Ty->isFloatingType() && getContext().getTypeSize(Ty) == 64;

  // All aggregates are passed indirectly?  That doesn't seem consistent
  // with the argument-lowering code.
  bool isIndirect = Ty->isAggregateType();

  CGBuilderTy &Builder = CGF.Builder;

  // The calling convention either uses 1-2 GPRs or 1 FPR.
  Address NumRegsAddr = Address::invalid();
  if (isInt || IsSoftFloatABI) {
    NumRegsAddr = Builder.CreateStructGEP(VAList, 0, CharUnits::Zero(), "gpr");
  } else {
    NumRegsAddr = Builder.CreateStructGEP(VAList, 1, CharUnits::One(), "fpr");
  }

  llvm::Value *NumRegs = Builder.CreateLoad(NumRegsAddr, "numUsedRegs");

  // "Align" the register count when TY is i64.
  if (isI64 || (isF64 && IsSoftFloatABI)) {
    NumRegs = Builder.CreateAdd(NumRegs, Builder.getInt8(1));
    NumRegs = Builder.CreateAnd(NumRegs, Builder.getInt8((uint8_t) ~1U));
  }

  llvm::Value *CC =
      Builder.CreateICmpULT(NumRegs, Builder.getInt8(OverflowLimit), "cond");

  llvm::BasicBlock *UsingRegs = CGF.createBasicBlock("using_regs");
  llvm::BasicBlock *UsingOverflow = CGF.createBasicBlock("using_overflow");
  llvm::BasicBlock *Cont = CGF.createBasicBlock("cont");

  Builder.CreateCondBr(CC, UsingRegs, UsingOverflow);

  llvm::Type *DirectTy = CGF.ConvertType(Ty);
  if (isIndirect) DirectTy = DirectTy->getPointerTo(0);

  // Case 1: consume registers.
  Address RegAddr = Address::invalid();
  {
    CGF.EmitBlock(UsingRegs);

    Address RegSaveAreaPtr =
      Builder.CreateStructGEP(VAList, 4, CharUnits::fromQuantity(8));
    RegAddr = Address(Builder.CreateLoad(RegSaveAreaPtr),
                      CharUnits::fromQuantity(8));
    assert(RegAddr.getElementType() == CGF.Int8Ty);

    // Floating-point registers start after the general-purpose registers.
    if (!(isInt || IsSoftFloatABI)) {
      RegAddr = Builder.CreateConstInBoundsByteGEP(RegAddr,
                                                   CharUnits::fromQuantity(32));
    }

    // Get the address of the saved value by scaling the number of
    // registers we've used by the number of
    CharUnits RegSize = CharUnits::fromQuantity((isInt || IsSoftFloatABI) ? 4 : 8);
    llvm::Value *RegOffset =
      Builder.CreateMul(NumRegs, Builder.getInt8(RegSize.getQuantity()));
    RegAddr = Address(Builder.CreateInBoundsGEP(CGF.Int8Ty,
                                            RegAddr.getPointer(), RegOffset),
                      RegAddr.getAlignment().alignmentOfArrayElement(RegSize));
    RegAddr = Builder.CreateElementBitCast(RegAddr, DirectTy);

    // Increase the used-register count.
    NumRegs =
      Builder.CreateAdd(NumRegs,
                        Builder.getInt8((isI64 || (isF64 && IsSoftFloatABI)) ? 2 : 1));
    Builder.CreateStore(NumRegs, NumRegsAddr);

    CGF.EmitBranch(Cont);
  }

  // Case 2: consume space in the overflow area.
  Address MemAddr = Address::invalid();
  {
    CGF.EmitBlock(UsingOverflow);

    Builder.CreateStore(Builder.getInt8(OverflowLimit), NumRegsAddr);

    // Everything in the overflow area is rounded up to a size of at least 4.
    CharUnits OverflowAreaAlign = CharUnits::fromQuantity(4);

    CharUnits Size;
    if (!isIndirect) {
      auto TypeInfo = CGF.getContext().getTypeInfoInChars(Ty);
      Size = TypeInfo.first.alignTo(OverflowAreaAlign);
    } else {
      Size = CGF.getPointerSize();
    }

    Address OverflowAreaAddr =
      Builder.CreateStructGEP(VAList, 3, CharUnits::fromQuantity(4));
    Address OverflowArea(Builder.CreateLoad(OverflowAreaAddr, "argp.cur"),
                         OverflowAreaAlign);
    // Round up address of argument to alignment
    CharUnits Align = CGF.getContext().getTypeAlignInChars(Ty);
    if (Align > OverflowAreaAlign) {
      llvm::Value *Ptr = OverflowArea.getPointer();
      OverflowArea = Address(emitRoundPointerUpToAlignment(CGF, Ptr, Align),
                                                           Align);
    }

    MemAddr = Builder.CreateElementBitCast(OverflowArea, DirectTy);

    // Increase the overflow area.
    OverflowArea = Builder.CreateConstInBoundsByteGEP(OverflowArea, Size);
    Builder.CreateStore(OverflowArea.getPointer(), OverflowAreaAddr);
    CGF.EmitBranch(Cont);
  }

  CGF.EmitBlock(Cont);

  // Merge the cases with a phi.
  Address Result = emitMergePHI(CGF, RegAddr, UsingRegs, MemAddr, UsingOverflow,
                                "vaarg.addr");

  // Load the pointer if the argument was passed indirectly.
  if (isIndirect) {
    Result = Address(Builder.CreateLoad(Result, "aggr"),
                     getContext().getTypeAlignInChars(Ty));
  }

  return Result;
}

bool
PPC32TargetCodeGenInfo::initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                                                llvm::Value *Address) const {
  // This is calculated from the LLVM and GCC tables and verified
  // against gcc output.  AFAIK all ABIs use the same encoding.

  CodeGen::CGBuilderTy &Builder = CGF.Builder;

  llvm::IntegerType *i8 = CGF.Int8Ty;
  llvm::Value *Four8 = llvm::ConstantInt::get(i8, 4);
  llvm::Value *Eight8 = llvm::ConstantInt::get(i8, 8);
  llvm::Value *Sixteen8 = llvm::ConstantInt::get(i8, 16);

  // 0-31: r0-31, the 4-byte general-purpose registers
  AssignToArrayRange(Builder, Address, Four8, 0, 31);

  // 32-63: fp0-31, the 8-byte floating-point registers
  AssignToArrayRange(Builder, Address, Eight8, 32, 63);

  // 64-76 are various 4-byte special-purpose registers:
  // 64: mq
  // 65: lr
  // 66: ctr
  // 67: ap
  // 68-75 cr0-7
  // 76: xer
  AssignToArrayRange(Builder, Address, Four8, 64, 76);

  // 77-108: v0-31, the 16-byte vector registers
  AssignToArrayRange(Builder, Address, Sixteen8, 77, 108);

  // 109: vrsave
  // 110: vscr
  // 111: spe_acc
  // 112: spefscr
  // 113: sfp
  AssignToArrayRange(Builder, Address, Four8, 109, 113);

  return false;
}

// PowerPC-64

namespace {
/// PPC64_SVR4_ABIInfo - The 64-bit PowerPC ELF (SVR4) ABI information.
class PPC64_SVR4_ABIInfo : public SwiftABIInfo {
public:
  enum ABIKind {
    ELFv1 = 0,
    ELFv2
  };

private:
  static const unsigned GPRBits = 64;
  ABIKind Kind;
  bool HasQPX;
  bool IsSoftFloatABI;

  // A vector of float or double will be promoted to <4 x f32> or <4 x f64> and
  // will be passed in a QPX register.
  bool IsQPXVectorTy(const Type *Ty) const {
    if (!HasQPX)
      return false;

    if (const VectorType *VT = Ty->getAs<VectorType>()) {
      unsigned NumElements = VT->getNumElements();
      if (NumElements == 1)
        return false;

      if (VT->getElementType()->isSpecificBuiltinType(BuiltinType::Double)) {
        if (getContext().getTypeSize(Ty) <= 256)
          return true;
      } else if (VT->getElementType()->
                   isSpecificBuiltinType(BuiltinType::Float)) {
        if (getContext().getTypeSize(Ty) <= 128)
          return true;
      }
    }

    return false;
  }

  bool IsQPXVectorTy(QualType Ty) const {
    return IsQPXVectorTy(Ty.getTypePtr());
  }

public:
  PPC64_SVR4_ABIInfo(CodeGen::CodeGenTypes &CGT, ABIKind Kind, bool HasQPX,
                     bool SoftFloatABI)
      : SwiftABIInfo(CGT), Kind(Kind), HasQPX(HasQPX),
        IsSoftFloatABI(SoftFloatABI) {}

  bool isPromotableTypeForABI(QualType Ty) const;
  CharUnits getParamTypeAlignment(QualType Ty) const;

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType Ty) const;

  bool isHomogeneousAggregateBaseType(QualType Ty) const override;
  bool isHomogeneousAggregateSmallEnough(const Type *Ty,
                                         uint64_t Members) const override;

  // TODO: We can add more logic to computeInfo to improve performance.
  // Example: For aggregate arguments that fit in a register, we could
  // use getDirectInReg (as is done below for structs containing a single
  // floating-point value) to avoid pushing them to memory on function
  // entry.  This would require changing the logic in PPCISelLowering
  // when lowering the parameters in the caller and args in the callee.
  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments()) {
      // We rely on the default argument classification for the most part.
      // One exception:  An aggregate containing a single floating-point
      // or vector item must be passed in a register if one is available.
      const Type *T = isSingleElementStruct(I.type, getContext());
      if (T) {
        const BuiltinType *BT = T->getAs<BuiltinType>();
        if (IsQPXVectorTy(T) ||
            (T->isVectorType() && getContext().getTypeSize(T) == 128) ||
            (BT && BT->isFloatingPoint())) {
          QualType QT(T, 0);
          I.info = ABIArgInfo::getDirectInReg(CGT.ConvertType(QT));
          continue;
        }
      }
      I.info = classifyArgumentType(I.type);
    }
  }

  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;

  bool shouldPassIndirectlyForSwift(ArrayRef<llvm::Type*> scalars,
                                    bool asReturnValue) const override {
    return occupiesMoreThan(CGT, scalars, /*total*/ 4);
  }

  bool isSwiftErrorInRegister() const override {
    return false;
  }
};

class PPC64_SVR4_TargetCodeGenInfo : public TargetCodeGenInfo {

public:
  PPC64_SVR4_TargetCodeGenInfo(CodeGenTypes &CGT,
                               PPC64_SVR4_ABIInfo::ABIKind Kind, bool HasQPX,
                               bool SoftFloatABI)
      : TargetCodeGenInfo(new PPC64_SVR4_ABIInfo(CGT, Kind, HasQPX,
                                                 SoftFloatABI)) {}

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    // This is recovered from gcc output.
    return 1; // r1 is the dedicated stack pointer
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override;
};

class PPC64TargetCodeGenInfo : public DefaultTargetCodeGenInfo {
public:
  PPC64TargetCodeGenInfo(CodeGenTypes &CGT) : DefaultTargetCodeGenInfo(CGT) {}

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    // This is recovered from gcc output.
    return 1; // r1 is the dedicated stack pointer
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override;
};

}

// Return true if the ABI requires Ty to be passed sign- or zero-
// extended to 64 bits.
bool
PPC64_SVR4_ABIInfo::isPromotableTypeForABI(QualType Ty) const {
  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  // Promotable integer types are required to be promoted by the ABI.
  if (Ty->isPromotableIntegerType())
    return true;

  // In addition to the usual promotable integer types, we also need to
  // extend all 32-bit types, since the ABI requires promotion to 64 bits.
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>())
    switch (BT->getKind()) {
    case BuiltinType::Int:
    case BuiltinType::UInt:
      return true;
    default:
      break;
    }

  return false;
}

/// isAlignedParamType - Determine whether a type requires 16-byte or
/// higher alignment in the parameter area.  Always returns at least 8.
CharUnits PPC64_SVR4_ABIInfo::getParamTypeAlignment(QualType Ty) const {
  // Complex types are passed just like their elements.
  if (const ComplexType *CTy = Ty->getAs<ComplexType>())
    Ty = CTy->getElementType();

  // Only vector types of size 16 bytes need alignment (larger types are
  // passed via reference, smaller types are not aligned).
  if (IsQPXVectorTy(Ty)) {
    if (getContext().getTypeSize(Ty) > 128)
      return CharUnits::fromQuantity(32);

    return CharUnits::fromQuantity(16);
  } else if (Ty->isVectorType()) {
    return CharUnits::fromQuantity(getContext().getTypeSize(Ty) == 128 ? 16 : 8);
  }

  // For single-element float/vector structs, we consider the whole type
  // to have the same alignment requirements as its single element.
  const Type *AlignAsType = nullptr;
  const Type *EltType = isSingleElementStruct(Ty, getContext());
  if (EltType) {
    const BuiltinType *BT = EltType->getAs<BuiltinType>();
    if (IsQPXVectorTy(EltType) || (EltType->isVectorType() &&
         getContext().getTypeSize(EltType) == 128) ||
        (BT && BT->isFloatingPoint()))
      AlignAsType = EltType;
  }

  // Likewise for ELFv2 homogeneous aggregates.
  const Type *Base = nullptr;
  uint64_t Members = 0;
  if (!AlignAsType && Kind == ELFv2 &&
      isAggregateTypeForABI(Ty) && isHomogeneousAggregate(Ty, Base, Members))
    AlignAsType = Base;

  // With special case aggregates, only vector base types need alignment.
  if (AlignAsType && IsQPXVectorTy(AlignAsType)) {
    if (getContext().getTypeSize(AlignAsType) > 128)
      return CharUnits::fromQuantity(32);

    return CharUnits::fromQuantity(16);
  } else if (AlignAsType) {
    return CharUnits::fromQuantity(AlignAsType->isVectorType() ? 16 : 8);
  }

  // Otherwise, we only need alignment for any aggregate type that
  // has an alignment requirement of >= 16 bytes.
  if (isAggregateTypeForABI(Ty) && getContext().getTypeAlign(Ty) >= 128) {
    if (HasQPX && getContext().getTypeAlign(Ty) >= 256)
      return CharUnits::fromQuantity(32);
    return CharUnits::fromQuantity(16);
  }

  return CharUnits::fromQuantity(8);
}

/// isHomogeneousAggregate - Return true if a type is an ELFv2 homogeneous
/// aggregate.  Base is set to the base element type, and Members is set
/// to the number of base elements.
bool ABIInfo::isHomogeneousAggregate(QualType Ty, const Type *&Base,
                                     uint64_t &Members) const {
  if (const ConstantArrayType *AT = getContext().getAsConstantArrayType(Ty)) {
    uint64_t NElements = AT->getSize().getZExtValue();
    if (NElements == 0)
      return false;
    if (!isHomogeneousAggregate(AT->getElementType(), Base, Members))
      return false;
    Members *= NElements;
  } else if (const RecordType *RT = Ty->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    if (RD->hasFlexibleArrayMember())
      return false;

    Members = 0;

    // If this is a C++ record, check the bases first.
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      for (const auto &I : CXXRD->bases()) {
        // Ignore empty records.
        if (isEmptyRecord(getContext(), I.getType(), true))
          continue;

        uint64_t FldMembers;
        if (!isHomogeneousAggregate(I.getType(), Base, FldMembers))
          return false;

        Members += FldMembers;
      }
    }

    for (const auto *FD : RD->fields()) {
      // Ignore (non-zero arrays of) empty records.
      QualType FT = FD->getType();
      while (const ConstantArrayType *AT =
             getContext().getAsConstantArrayType(FT)) {
        if (AT->getSize().getZExtValue() == 0)
          return false;
        FT = AT->getElementType();
      }
      if (isEmptyRecord(getContext(), FT, true))
        continue;

      // For compatibility with GCC, ignore empty bitfields in C++ mode.
      if (getContext().getLangOpts().CPlusPlus &&
          FD->isZeroLengthBitField(getContext()))
        continue;

      uint64_t FldMembers;
      if (!isHomogeneousAggregate(FD->getType(), Base, FldMembers))
        return false;

      Members = (RD->isUnion() ?
                 std::max(Members, FldMembers) : Members + FldMembers);
    }

    if (!Base)
      return false;

    // Ensure there is no padding.
    if (getContext().getTypeSize(Base) * Members !=
        getContext().getTypeSize(Ty))
      return false;
  } else {
    Members = 1;
    if (const ComplexType *CT = Ty->getAs<ComplexType>()) {
      Members = 2;
      Ty = CT->getElementType();
    }

    // Most ABIs only support float, double, and some vector type widths.
    if (!isHomogeneousAggregateBaseType(Ty))
      return false;

    // The base type must be the same for all members.  Types that
    // agree in both total size and mode (float vs. vector) are
    // treated as being equivalent here.
    const Type *TyPtr = Ty.getTypePtr();
    if (!Base) {
      Base = TyPtr;
      // If it's a non-power-of-2 vector, its size is already a power-of-2,
      // so make sure to widen it explicitly.
      if (const VectorType *VT = Base->getAs<VectorType>()) {
        QualType EltTy = VT->getElementType();
        unsigned NumElements =
            getContext().getTypeSize(VT) / getContext().getTypeSize(EltTy);
        Base = getContext()
                   .getVectorType(EltTy, NumElements, VT->getVectorKind())
                   .getTypePtr();
      }
    }

    if (Base->isVectorType() != TyPtr->isVectorType() ||
        getContext().getTypeSize(Base) != getContext().getTypeSize(TyPtr))
      return false;
  }
  return Members > 0 && isHomogeneousAggregateSmallEnough(Base, Members);
}

bool PPC64_SVR4_ABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  // Homogeneous aggregates for ELFv2 must have base types of float,
  // double, long double, or 128-bit vectors.
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    if (BT->getKind() == BuiltinType::Float ||
        BT->getKind() == BuiltinType::Double ||
        BT->getKind() == BuiltinType::LongDouble ||
        (getContext().getTargetInfo().hasFloat128Type() &&
          (BT->getKind() == BuiltinType::Float128))) {
      if (IsSoftFloatABI)
        return false;
      return true;
    }
  }
  if (const VectorType *VT = Ty->getAs<VectorType>()) {
    if (getContext().getTypeSize(VT) == 128 || IsQPXVectorTy(Ty))
      return true;
  }
  return false;
}

bool PPC64_SVR4_ABIInfo::isHomogeneousAggregateSmallEnough(
    const Type *Base, uint64_t Members) const {
  // Vector and fp128 types require one register, other floating point types
  // require one or two registers depending on their size.
  uint32_t NumRegs =
      ((getContext().getTargetInfo().hasFloat128Type() &&
          Base->isFloat128Type()) ||
        Base->isVectorType()) ? 1
                              : (getContext().getTypeSize(Base) + 63) / 64;

  // Homogeneous Aggregates may occupy at most 8 registers.
  return Members * NumRegs <= 8;
}

ABIArgInfo
PPC64_SVR4_ABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (Ty->isAnyComplexType())
    return ABIArgInfo::getDirect();

  // Non-Altivec vector types are passed in GPRs (smaller than 16 bytes)
  // or via reference (larger than 16 bytes).
  if (Ty->isVectorType() && !IsQPXVectorTy(Ty)) {
    uint64_t Size = getContext().getTypeSize(Ty);
    if (Size > 128)
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
    else if (Size < 128) {
      llvm::Type *CoerceTy = llvm::IntegerType::get(getVMContext(), Size);
      return ABIArgInfo::getDirect(CoerceTy);
    }
  }

  if (isAggregateTypeForABI(Ty)) {
    if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

    uint64_t ABIAlign = getParamTypeAlignment(Ty).getQuantity();
    uint64_t TyAlign = getContext().getTypeAlignInChars(Ty).getQuantity();

    // ELFv2 homogeneous aggregates are passed as array types.
    const Type *Base = nullptr;
    uint64_t Members = 0;
    if (Kind == ELFv2 &&
        isHomogeneousAggregate(Ty, Base, Members)) {
      llvm::Type *BaseTy = CGT.ConvertType(QualType(Base, 0));
      llvm::Type *CoerceTy = llvm::ArrayType::get(BaseTy, Members);
      return ABIArgInfo::getDirect(CoerceTy);
    }

    // If an aggregate may end up fully in registers, we do not
    // use the ByVal method, but pass the aggregate as array.
    // This is usually beneficial since we avoid forcing the
    // back-end to store the argument to memory.
    uint64_t Bits = getContext().getTypeSize(Ty);
    if (Bits > 0 && Bits <= 8 * GPRBits) {
      llvm::Type *CoerceTy;

      // Types up to 8 bytes are passed as integer type (which will be
      // properly aligned in the argument save area doubleword).
      if (Bits <= GPRBits)
        CoerceTy =
            llvm::IntegerType::get(getVMContext(), llvm::alignTo(Bits, 8));
      // Larger types are passed as arrays, with the base type selected
      // according to the required alignment in the save area.
      else {
        uint64_t RegBits = ABIAlign * 8;
        uint64_t NumRegs = llvm::alignTo(Bits, RegBits) / RegBits;
        llvm::Type *RegTy = llvm::IntegerType::get(getVMContext(), RegBits);
        CoerceTy = llvm::ArrayType::get(RegTy, NumRegs);
      }

      return ABIArgInfo::getDirect(CoerceTy);
    }

    // All other aggregates are passed ByVal.
    return ABIArgInfo::getIndirect(CharUnits::fromQuantity(ABIAlign),
                                   /*ByVal=*/true,
                                   /*Realign=*/TyAlign > ABIAlign);
  }

  return (isPromotableTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                     : ABIArgInfo::getDirect());
}

ABIArgInfo
PPC64_SVR4_ABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  if (RetTy->isAnyComplexType())
    return ABIArgInfo::getDirect();

  // Non-Altivec vector types are returned in GPRs (smaller than 16 bytes)
  // or via reference (larger than 16 bytes).
  if (RetTy->isVectorType() && !IsQPXVectorTy(RetTy)) {
    uint64_t Size = getContext().getTypeSize(RetTy);
    if (Size > 128)
      return getNaturalAlignIndirect(RetTy);
    else if (Size < 128) {
      llvm::Type *CoerceTy = llvm::IntegerType::get(getVMContext(), Size);
      return ABIArgInfo::getDirect(CoerceTy);
    }
  }

  if (isAggregateTypeForABI(RetTy)) {
    // ELFv2 homogeneous aggregates are returned as array types.
    const Type *Base = nullptr;
    uint64_t Members = 0;
    if (Kind == ELFv2 &&
        isHomogeneousAggregate(RetTy, Base, Members)) {
      llvm::Type *BaseTy = CGT.ConvertType(QualType(Base, 0));
      llvm::Type *CoerceTy = llvm::ArrayType::get(BaseTy, Members);
      return ABIArgInfo::getDirect(CoerceTy);
    }

    // ELFv2 small aggregates are returned in up to two registers.
    uint64_t Bits = getContext().getTypeSize(RetTy);
    if (Kind == ELFv2 && Bits <= 2 * GPRBits) {
      if (Bits == 0)
        return ABIArgInfo::getIgnore();

      llvm::Type *CoerceTy;
      if (Bits > GPRBits) {
        CoerceTy = llvm::IntegerType::get(getVMContext(), GPRBits);
        CoerceTy = llvm::StructType::get(CoerceTy, CoerceTy);
      } else
        CoerceTy =
            llvm::IntegerType::get(getVMContext(), llvm::alignTo(Bits, 8));
      return ABIArgInfo::getDirect(CoerceTy);
    }

    // All other aggregates are returned indirectly.
    return getNaturalAlignIndirect(RetTy);
  }

  return (isPromotableTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                        : ABIArgInfo::getDirect());
}

// Based on ARMABIInfo::EmitVAArg, adjusted for 64-bit machine.
Address PPC64_SVR4_ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                      QualType Ty) const {
  auto TypeInfo = getContext().getTypeInfoInChars(Ty);
  TypeInfo.second = getParamTypeAlignment(Ty);

  CharUnits SlotSize = CharUnits::fromQuantity(8);

  // If we have a complex type and the base type is smaller than 8 bytes,
  // the ABI calls for the real and imaginary parts to be right-adjusted
  // in separate doublewords.  However, Clang expects us to produce a
  // pointer to a structure with the two parts packed tightly.  So generate
  // loads of the real and imaginary parts relative to the va_list pointer,
  // and store them to a temporary structure.
  if (const ComplexType *CTy = Ty->getAs<ComplexType>()) {
    CharUnits EltSize = TypeInfo.first / 2;
    if (EltSize < SlotSize) {
      Address Addr = emitVoidPtrDirectVAArg(CGF, VAListAddr, CGF.Int8Ty,
                                            SlotSize * 2, SlotSize,
                                            SlotSize, /*AllowHigher*/ true);

      Address RealAddr = Addr;
      Address ImagAddr = RealAddr;
      if (CGF.CGM.getDataLayout().isBigEndian()) {
        RealAddr = CGF.Builder.CreateConstInBoundsByteGEP(RealAddr,
                                                          SlotSize - EltSize);
        ImagAddr = CGF.Builder.CreateConstInBoundsByteGEP(ImagAddr,
                                                      2 * SlotSize - EltSize);
      } else {
        ImagAddr = CGF.Builder.CreateConstInBoundsByteGEP(RealAddr, SlotSize);
      }

      llvm::Type *EltTy = CGF.ConvertTypeForMem(CTy->getElementType());
      RealAddr = CGF.Builder.CreateElementBitCast(RealAddr, EltTy);
      ImagAddr = CGF.Builder.CreateElementBitCast(ImagAddr, EltTy);
      llvm::Value *Real = CGF.Builder.CreateLoad(RealAddr, ".vareal");
      llvm::Value *Imag = CGF.Builder.CreateLoad(ImagAddr, ".vaimag");

      Address Temp = CGF.CreateMemTemp(Ty, "vacplx");
      CGF.EmitStoreOfComplex({Real, Imag}, CGF.MakeAddrLValue(Temp, Ty),
                             /*init*/ true);
      return Temp;
    }
  }

  // Otherwise, just use the general rule.
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*Indirect*/ false,
                          TypeInfo, SlotSize, /*AllowHigher*/ true);
}

static bool
PPC64_initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                              llvm::Value *Address) {
  // This is calculated from the LLVM and GCC tables and verified
  // against gcc output.  AFAIK all ABIs use the same encoding.

  CodeGen::CGBuilderTy &Builder = CGF.Builder;

  llvm::IntegerType *i8 = CGF.Int8Ty;
  llvm::Value *Four8 = llvm::ConstantInt::get(i8, 4);
  llvm::Value *Eight8 = llvm::ConstantInt::get(i8, 8);
  llvm::Value *Sixteen8 = llvm::ConstantInt::get(i8, 16);

  // 0-31: r0-31, the 8-byte general-purpose registers
  AssignToArrayRange(Builder, Address, Eight8, 0, 31);

  // 32-63: fp0-31, the 8-byte floating-point registers
  AssignToArrayRange(Builder, Address, Eight8, 32, 63);

  // 64-67 are various 8-byte special-purpose registers:
  // 64: mq
  // 65: lr
  // 66: ctr
  // 67: ap
  AssignToArrayRange(Builder, Address, Eight8, 64, 67);

  // 68-76 are various 4-byte special-purpose registers:
  // 68-75 cr0-7
  // 76: xer
  AssignToArrayRange(Builder, Address, Four8, 68, 76);

  // 77-108: v0-31, the 16-byte vector registers
  AssignToArrayRange(Builder, Address, Sixteen8, 77, 108);

  // 109: vrsave
  // 110: vscr
  // 111: spe_acc
  // 112: spefscr
  // 113: sfp
  // 114: tfhar
  // 115: tfiar
  // 116: texasr
  AssignToArrayRange(Builder, Address, Eight8, 109, 116);

  return false;
}

bool
PPC64_SVR4_TargetCodeGenInfo::initDwarfEHRegSizeTable(
  CodeGen::CodeGenFunction &CGF,
  llvm::Value *Address) const {

  return PPC64_initDwarfEHRegSizeTable(CGF, Address);
}

bool
PPC64TargetCodeGenInfo::initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                                                llvm::Value *Address) const {

  return PPC64_initDwarfEHRegSizeTable(CGF, Address);
}

//===----------------------------------------------------------------------===//
// AArch64 ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class AArch64ABIInfo : public SwiftABIInfo {
public:
  enum ABIKind {
    AAPCS = 0,
    DarwinPCS,
    Win64
  };

private:
  ABIKind Kind;

public:
  AArch64ABIInfo(CodeGenTypes &CGT, ABIKind Kind)
    : SwiftABIInfo(CGT), Kind(Kind) {}

private:
  ABIKind getABIKind() const { return Kind; }
  bool isDarwinPCS() const { return Kind == DarwinPCS; }

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType RetTy) const;
  bool isHomogeneousAggregateBaseType(QualType Ty) const override;
  bool isHomogeneousAggregateSmallEnough(const Type *Ty,
                                         uint64_t Members) const override;

  bool isIllegalVectorType(QualType Ty) const;

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!::classifyReturnType(getCXXABI(), FI, *this))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

    for (auto &it : FI.arguments())
      it.info = classifyArgumentType(it.type);
  }

  Address EmitDarwinVAArg(Address VAListAddr, QualType Ty,
                          CodeGenFunction &CGF) const;

  Address EmitAAPCSVAArg(Address VAListAddr, QualType Ty,
                         CodeGenFunction &CGF) const;

  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override {
    return Kind == Win64 ? EmitMSVAArg(CGF, VAListAddr, Ty)
                         : isDarwinPCS() ? EmitDarwinVAArg(VAListAddr, Ty, CGF)
                                         : EmitAAPCSVAArg(VAListAddr, Ty, CGF);
  }

  Address EmitMSVAArg(CodeGenFunction &CGF, Address VAListAddr,
                      QualType Ty) const override;

  bool shouldPassIndirectlyForSwift(ArrayRef<llvm::Type*> scalars,
                                    bool asReturnValue) const override {
    return occupiesMoreThan(CGT, scalars, /*total*/ 4);
  }
  bool isSwiftErrorInRegister() const override {
    return true;
  }

  bool isLegalVectorTypeForSwift(CharUnits totalSize, llvm::Type *eltTy,
                                 unsigned elts) const override;
};

class AArch64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  AArch64TargetCodeGenInfo(CodeGenTypes &CGT, AArch64ABIInfo::ABIKind Kind)
      : TargetCodeGenInfo(new AArch64ABIInfo(CGT, Kind)) {}

  StringRef getARCRetainAutoreleasedReturnValueMarker() const override {
    return "mov\tfp, fp\t\t// marker for objc_retainAutoreleaseReturnValue";
  }

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    return 31;
  }

  bool doesReturnSlotInterfereWithArgs() const override { return false; }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD)
      return;
    llvm::Function *Fn = cast<llvm::Function>(GV);

    auto Kind = CGM.getCodeGenOpts().getSignReturnAddress();
    if (Kind != CodeGenOptions::SignReturnAddressScope::None) {
      Fn->addFnAttr("sign-return-address",
                    Kind == CodeGenOptions::SignReturnAddressScope::All
                        ? "all"
                        : "non-leaf");

      auto Key = CGM.getCodeGenOpts().getSignReturnAddressKey();
      Fn->addFnAttr("sign-return-address-key",
                    Key == CodeGenOptions::SignReturnAddressKeyValue::AKey
                        ? "a_key"
                        : "b_key");
    }

    if (CGM.getCodeGenOpts().BranchTargetEnforcement)
      Fn->addFnAttr("branch-target-enforcement");
  }
};

class WindowsAArch64TargetCodeGenInfo : public AArch64TargetCodeGenInfo {
public:
  WindowsAArch64TargetCodeGenInfo(CodeGenTypes &CGT, AArch64ABIInfo::ABIKind K)
      : AArch64TargetCodeGenInfo(CGT, K) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override;

  void getDependentLibraryOption(llvm::StringRef Lib,
                                 llvm::SmallString<24> &Opt) const override {
    Opt = "/DEFAULTLIB:" + qualifyWindowsLibrary(Lib);
  }

  void getDetectMismatchOption(llvm::StringRef Name, llvm::StringRef Value,
                               llvm::SmallString<32> &Opt) const override {
    Opt = "/FAILIFMISMATCH:\"" + Name.str() + "=" + Value.str() + "\"";
  }
};

void WindowsAArch64TargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &CGM) const {
  AArch64TargetCodeGenInfo::setTargetAttributes(D, GV, CGM);
  if (GV->isDeclaration())
    return;
  addStackProbeTargetAttributes(D, GV, CGM);
}
}

ABIArgInfo AArch64ABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  // Handle illegal vector types here.
  if (isIllegalVectorType(Ty)) {
    uint64_t Size = getContext().getTypeSize(Ty);
    // Android promotes <2 x i8> to i16, not i32
    if (isAndroid() && (Size <= 16)) {
      llvm::Type *ResType = llvm::Type::getInt16Ty(getVMContext());
      return ABIArgInfo::getDirect(ResType);
    }
    if (Size <= 32) {
      llvm::Type *ResType = llvm::Type::getInt32Ty(getVMContext());
      return ABIArgInfo::getDirect(ResType);
    }
    if (Size == 64) {
      llvm::Type *ResType =
          llvm::VectorType::get(llvm::Type::getInt32Ty(getVMContext()), 2);
      return ABIArgInfo::getDirect(ResType);
    }
    if (Size == 128) {
      llvm::Type *ResType =
          llvm::VectorType::get(llvm::Type::getInt32Ty(getVMContext()), 4);
      return ABIArgInfo::getDirect(ResType);
    }
    return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
  }

  if (!isAggregateTypeForABI(Ty)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    return (Ty->isPromotableIntegerType() && isDarwinPCS()
                ? ABIArgInfo::getExtend(Ty)
                : ABIArgInfo::getDirect());
  }

  // Structures with either a non-trivial destructor or a non-trivial
  // copy constructor are always indirect.
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI())) {
    return getNaturalAlignIndirect(Ty, /*ByVal=*/RAA ==
                                     CGCXXABI::RAA_DirectInMemory);
  }

  // Empty records are always ignored on Darwin, but actually passed in C++ mode
  // elsewhere for GNU compatibility.
  uint64_t Size = getContext().getTypeSize(Ty);
  bool IsEmpty = isEmptyRecord(getContext(), Ty, true);
  if (IsEmpty || Size == 0) {
    if (!getContext().getLangOpts().CPlusPlus || isDarwinPCS())
      return ABIArgInfo::getIgnore();

    // GNU C mode. The only argument that gets ignored is an empty one with size
    // 0.
    if (IsEmpty && Size == 0)
      return ABIArgInfo::getIgnore();
    return ABIArgInfo::getDirect(llvm::Type::getInt8Ty(getVMContext()));
  }

  // Homogeneous Floating-point Aggregates (HFAs) need to be expanded.
  const Type *Base = nullptr;
  uint64_t Members = 0;
  if (isHomogeneousAggregate(Ty, Base, Members)) {
    return ABIArgInfo::getDirect(
        llvm::ArrayType::get(CGT.ConvertType(QualType(Base, 0)), Members));
  }

  // Aggregates <= 16 bytes are passed directly in registers or on the stack.
  if (Size <= 128) {
    // On RenderScript, coerce Aggregates <= 16 bytes to an integer array of
    // same size and alignment.
    if (getTarget().isRenderScriptTarget()) {
      return coerceToIntArray(Ty, getContext(), getVMContext());
    }
    unsigned Alignment;
    if (Kind == AArch64ABIInfo::AAPCS) {
      Alignment = getContext().getTypeUnadjustedAlign(Ty);
      Alignment = Alignment < 128 ? 64 : 128;
    } else {
      Alignment = getContext().getTypeAlign(Ty);
    }
    Size = llvm::alignTo(Size, 64); // round up to multiple of 8 bytes

    // We use a pair of i64 for 16-byte aggregate with 8-byte alignment.
    // For aggregates with 16-byte alignment, we use i128.
    if (Alignment < 128 && Size == 128) {
      llvm::Type *BaseTy = llvm::Type::getInt64Ty(getVMContext());
      return ABIArgInfo::getDirect(llvm::ArrayType::get(BaseTy, Size / 64));
    }
    return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(), Size));
  }

  return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
}

ABIArgInfo AArch64ABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  // Large vector types should be returned via memory.
  if (RetTy->isVectorType() && getContext().getTypeSize(RetTy) > 128)
    return getNaturalAlignIndirect(RetTy);

  if (!isAggregateTypeForABI(RetTy)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
      RetTy = EnumTy->getDecl()->getIntegerType();

    return (RetTy->isPromotableIntegerType() && isDarwinPCS()
                ? ABIArgInfo::getExtend(RetTy)
                : ABIArgInfo::getDirect());
  }

  uint64_t Size = getContext().getTypeSize(RetTy);
  if (isEmptyRecord(getContext(), RetTy, true) || Size == 0)
    return ABIArgInfo::getIgnore();

  const Type *Base = nullptr;
  uint64_t Members = 0;
  if (isHomogeneousAggregate(RetTy, Base, Members))
    // Homogeneous Floating-point Aggregates (HFAs) are returned directly.
    return ABIArgInfo::getDirect();

  // Aggregates <= 16 bytes are returned directly in registers or on the stack.
  if (Size <= 128) {
    // On RenderScript, coerce Aggregates <= 16 bytes to an integer array of
    // same size and alignment.
    if (getTarget().isRenderScriptTarget()) {
      return coerceToIntArray(RetTy, getContext(), getVMContext());
    }
    unsigned Alignment = getContext().getTypeAlign(RetTy);
    Size = llvm::alignTo(Size, 64); // round up to multiple of 8 bytes

    // We use a pair of i64 for 16-byte aggregate with 8-byte alignment.
    // For aggregates with 16-byte alignment, we use i128.
    if (Alignment < 128 && Size == 128) {
      llvm::Type *BaseTy = llvm::Type::getInt64Ty(getVMContext());
      return ABIArgInfo::getDirect(llvm::ArrayType::get(BaseTy, Size / 64));
    }
    return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(), Size));
  }

  return getNaturalAlignIndirect(RetTy);
}

/// isIllegalVectorType - check whether the vector type is legal for AArch64.
bool AArch64ABIInfo::isIllegalVectorType(QualType Ty) const {
  if (const VectorType *VT = Ty->getAs<VectorType>()) {
    // Check whether VT is legal.
    unsigned NumElements = VT->getNumElements();
    uint64_t Size = getContext().getTypeSize(VT);
    // NumElements should be power of 2.
    if (!llvm::isPowerOf2_32(NumElements))
      return true;
    return Size != 64 && (Size != 128 || NumElements == 1);
  }
  return false;
}

bool AArch64ABIInfo::isLegalVectorTypeForSwift(CharUnits totalSize,
                                               llvm::Type *eltTy,
                                               unsigned elts) const {
  if (!llvm::isPowerOf2_32(elts))
    return false;
  if (totalSize.getQuantity() != 8 &&
      (totalSize.getQuantity() != 16 || elts == 1))
    return false;
  return true;
}

bool AArch64ABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  // Homogeneous aggregates for AAPCS64 must have base types of a floating
  // point type or a short-vector type. This is the same as the 32-bit ABI,
  // but with the difference that any floating-point type is allowed,
  // including __fp16.
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    if (BT->isFloatingPoint())
      return true;
  } else if (const VectorType *VT = Ty->getAs<VectorType>()) {
    unsigned VecSize = getContext().getTypeSize(VT);
    if (VecSize == 64 || VecSize == 128)
      return true;
  }
  return false;
}

bool AArch64ABIInfo::isHomogeneousAggregateSmallEnough(const Type *Base,
                                                       uint64_t Members) const {
  return Members <= 4;
}

Address AArch64ABIInfo::EmitAAPCSVAArg(Address VAListAddr,
                                            QualType Ty,
                                            CodeGenFunction &CGF) const {
  ABIArgInfo AI = classifyArgumentType(Ty);
  bool IsIndirect = AI.isIndirect();

  llvm::Type *BaseTy = CGF.ConvertType(Ty);
  if (IsIndirect)
    BaseTy = llvm::PointerType::getUnqual(BaseTy);
  else if (AI.getCoerceToType())
    BaseTy = AI.getCoerceToType();

  unsigned NumRegs = 1;
  if (llvm::ArrayType *ArrTy = dyn_cast<llvm::ArrayType>(BaseTy)) {
    BaseTy = ArrTy->getElementType();
    NumRegs = ArrTy->getNumElements();
  }
  bool IsFPR = BaseTy->isFloatingPointTy() || BaseTy->isVectorTy();

  // The AArch64 va_list type and handling is specified in the Procedure Call
  // Standard, section B.4:
  //
  // struct {
  //   void *__stack;
  //   void *__gr_top;
  //   void *__vr_top;
  //   int __gr_offs;
  //   int __vr_offs;
  // };

  llvm::BasicBlock *MaybeRegBlock = CGF.createBasicBlock("vaarg.maybe_reg");
  llvm::BasicBlock *InRegBlock = CGF.createBasicBlock("vaarg.in_reg");
  llvm::BasicBlock *OnStackBlock = CGF.createBasicBlock("vaarg.on_stack");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("vaarg.end");

  auto TyInfo = getContext().getTypeInfoInChars(Ty);
  CharUnits TyAlign = TyInfo.second;

  Address reg_offs_p = Address::invalid();
  llvm::Value *reg_offs = nullptr;
  int reg_top_index;
  CharUnits reg_top_offset;
  int RegSize = IsIndirect ? 8 : TyInfo.first.getQuantity();
  if (!IsFPR) {
    // 3 is the field number of __gr_offs
    reg_offs_p =
        CGF.Builder.CreateStructGEP(VAListAddr, 3, CharUnits::fromQuantity(24),
                                    "gr_offs_p");
    reg_offs = CGF.Builder.CreateLoad(reg_offs_p, "gr_offs");
    reg_top_index = 1; // field number for __gr_top
    reg_top_offset = CharUnits::fromQuantity(8);
    RegSize = llvm::alignTo(RegSize, 8);
  } else {
    // 4 is the field number of __vr_offs.
    reg_offs_p =
        CGF.Builder.CreateStructGEP(VAListAddr, 4, CharUnits::fromQuantity(28),
                                    "vr_offs_p");
    reg_offs = CGF.Builder.CreateLoad(reg_offs_p, "vr_offs");
    reg_top_index = 2; // field number for __vr_top
    reg_top_offset = CharUnits::fromQuantity(16);
    RegSize = 16 * NumRegs;
  }

  //=======================================
  // Find out where argument was passed
  //=======================================

  // If reg_offs >= 0 we're already using the stack for this type of
  // argument. We don't want to keep updating reg_offs (in case it overflows,
  // though anyone passing 2GB of arguments, each at most 16 bytes, deserves
  // whatever they get).
  llvm::Value *UsingStack = nullptr;
  UsingStack = CGF.Builder.CreateICmpSGE(
      reg_offs, llvm::ConstantInt::get(CGF.Int32Ty, 0));

  CGF.Builder.CreateCondBr(UsingStack, OnStackBlock, MaybeRegBlock);

  // Otherwise, at least some kind of argument could go in these registers, the
  // question is whether this particular type is too big.
  CGF.EmitBlock(MaybeRegBlock);

  // Integer arguments may need to correct register alignment (for example a
  // "struct { __int128 a; };" gets passed in x_2N, x_{2N+1}). In this case we
  // align __gr_offs to calculate the potential address.
  if (!IsFPR && !IsIndirect && TyAlign.getQuantity() > 8) {
    int Align = TyAlign.getQuantity();

    reg_offs = CGF.Builder.CreateAdd(
        reg_offs, llvm::ConstantInt::get(CGF.Int32Ty, Align - 1),
        "align_regoffs");
    reg_offs = CGF.Builder.CreateAnd(
        reg_offs, llvm::ConstantInt::get(CGF.Int32Ty, -Align),
        "aligned_regoffs");
  }

  // Update the gr_offs/vr_offs pointer for next call to va_arg on this va_list.
  // The fact that this is done unconditionally reflects the fact that
  // allocating an argument to the stack also uses up all the remaining
  // registers of the appropriate kind.
  llvm::Value *NewOffset = nullptr;
  NewOffset = CGF.Builder.CreateAdd(
      reg_offs, llvm::ConstantInt::get(CGF.Int32Ty, RegSize), "new_reg_offs");
  CGF.Builder.CreateStore(NewOffset, reg_offs_p);

  // Now we're in a position to decide whether this argument really was in
  // registers or not.
  llvm::Value *InRegs = nullptr;
  InRegs = CGF.Builder.CreateICmpSLE(
      NewOffset, llvm::ConstantInt::get(CGF.Int32Ty, 0), "inreg");

  CGF.Builder.CreateCondBr(InRegs, InRegBlock, OnStackBlock);

  //=======================================
  // Argument was in registers
  //=======================================

  // Now we emit the code for if the argument was originally passed in
  // registers. First start the appropriate block:
  CGF.EmitBlock(InRegBlock);

  llvm::Value *reg_top = nullptr;
  Address reg_top_p = CGF.Builder.CreateStructGEP(VAListAddr, reg_top_index,
                                                  reg_top_offset, "reg_top_p");
  reg_top = CGF.Builder.CreateLoad(reg_top_p, "reg_top");
  Address BaseAddr(CGF.Builder.CreateInBoundsGEP(reg_top, reg_offs),
                   CharUnits::fromQuantity(IsFPR ? 16 : 8));
  Address RegAddr = Address::invalid();
  llvm::Type *MemTy = CGF.ConvertTypeForMem(Ty);

  if (IsIndirect) {
    // If it's been passed indirectly (actually a struct), whatever we find from
    // stored registers or on the stack will actually be a struct **.
    MemTy = llvm::PointerType::getUnqual(MemTy);
  }

  const Type *Base = nullptr;
  uint64_t NumMembers = 0;
  bool IsHFA = isHomogeneousAggregate(Ty, Base, NumMembers);
  if (IsHFA && NumMembers > 1) {
    // Homogeneous aggregates passed in registers will have their elements split
    // and stored 16-bytes apart regardless of size (they're notionally in qN,
    // qN+1, ...). We reload and store into a temporary local variable
    // contiguously.
    assert(!IsIndirect && "Homogeneous aggregates should be passed directly");
    auto BaseTyInfo = getContext().getTypeInfoInChars(QualType(Base, 0));
    llvm::Type *BaseTy = CGF.ConvertType(QualType(Base, 0));
    llvm::Type *HFATy = llvm::ArrayType::get(BaseTy, NumMembers);
    Address Tmp = CGF.CreateTempAlloca(HFATy,
                                       std::max(TyAlign, BaseTyInfo.second));

    // On big-endian platforms, the value will be right-aligned in its slot.
    int Offset = 0;
    if (CGF.CGM.getDataLayout().isBigEndian() &&
        BaseTyInfo.first.getQuantity() < 16)
      Offset = 16 - BaseTyInfo.first.getQuantity();

    for (unsigned i = 0; i < NumMembers; ++i) {
      CharUnits BaseOffset = CharUnits::fromQuantity(16 * i + Offset);
      Address LoadAddr =
        CGF.Builder.CreateConstInBoundsByteGEP(BaseAddr, BaseOffset);
      LoadAddr = CGF.Builder.CreateElementBitCast(LoadAddr, BaseTy);

      Address StoreAddr =
        CGF.Builder.CreateConstArrayGEP(Tmp, i, BaseTyInfo.first);

      llvm::Value *Elem = CGF.Builder.CreateLoad(LoadAddr);
      CGF.Builder.CreateStore(Elem, StoreAddr);
    }

    RegAddr = CGF.Builder.CreateElementBitCast(Tmp, MemTy);
  } else {
    // Otherwise the object is contiguous in memory.

    // It might be right-aligned in its slot.
    CharUnits SlotSize = BaseAddr.getAlignment();
    if (CGF.CGM.getDataLayout().isBigEndian() && !IsIndirect &&
        (IsHFA || !isAggregateTypeForABI(Ty)) &&
        TyInfo.first < SlotSize) {
      CharUnits Offset = SlotSize - TyInfo.first;
      BaseAddr = CGF.Builder.CreateConstInBoundsByteGEP(BaseAddr, Offset);
    }

    RegAddr = CGF.Builder.CreateElementBitCast(BaseAddr, MemTy);
  }

  CGF.EmitBranch(ContBlock);

  //=======================================
  // Argument was on the stack
  //=======================================
  CGF.EmitBlock(OnStackBlock);

  Address stack_p = CGF.Builder.CreateStructGEP(VAListAddr, 0,
                                                CharUnits::Zero(), "stack_p");
  llvm::Value *OnStackPtr = CGF.Builder.CreateLoad(stack_p, "stack");

  // Again, stack arguments may need realignment. In this case both integer and
  // floating-point ones might be affected.
  if (!IsIndirect && TyAlign.getQuantity() > 8) {
    int Align = TyAlign.getQuantity();

    OnStackPtr = CGF.Builder.CreatePtrToInt(OnStackPtr, CGF.Int64Ty);

    OnStackPtr = CGF.Builder.CreateAdd(
        OnStackPtr, llvm::ConstantInt::get(CGF.Int64Ty, Align - 1),
        "align_stack");
    OnStackPtr = CGF.Builder.CreateAnd(
        OnStackPtr, llvm::ConstantInt::get(CGF.Int64Ty, -Align),
        "align_stack");

    OnStackPtr = CGF.Builder.CreateIntToPtr(OnStackPtr, CGF.Int8PtrTy);
  }
  Address OnStackAddr(OnStackPtr,
                      std::max(CharUnits::fromQuantity(8), TyAlign));

  // All stack slots are multiples of 8 bytes.
  CharUnits StackSlotSize = CharUnits::fromQuantity(8);
  CharUnits StackSize;
  if (IsIndirect)
    StackSize = StackSlotSize;
  else
    StackSize = TyInfo.first.alignTo(StackSlotSize);

  llvm::Value *StackSizeC = CGF.Builder.getSize(StackSize);
  llvm::Value *NewStack =
      CGF.Builder.CreateInBoundsGEP(OnStackPtr, StackSizeC, "new_stack");

  // Write the new value of __stack for the next call to va_arg
  CGF.Builder.CreateStore(NewStack, stack_p);

  if (CGF.CGM.getDataLayout().isBigEndian() && !isAggregateTypeForABI(Ty) &&
      TyInfo.first < StackSlotSize) {
    CharUnits Offset = StackSlotSize - TyInfo.first;
    OnStackAddr = CGF.Builder.CreateConstInBoundsByteGEP(OnStackAddr, Offset);
  }

  OnStackAddr = CGF.Builder.CreateElementBitCast(OnStackAddr, MemTy);

  CGF.EmitBranch(ContBlock);

  //=======================================
  // Tidy up
  //=======================================
  CGF.EmitBlock(ContBlock);

  Address ResAddr = emitMergePHI(CGF, RegAddr, InRegBlock,
                                 OnStackAddr, OnStackBlock, "vaargs.addr");

  if (IsIndirect)
    return Address(CGF.Builder.CreateLoad(ResAddr, "vaarg.addr"),
                   TyInfo.second);

  return ResAddr;
}

Address AArch64ABIInfo::EmitDarwinVAArg(Address VAListAddr, QualType Ty,
                                        CodeGenFunction &CGF) const {
  // The backend's lowering doesn't support va_arg for aggregates or
  // illegal vector types.  Lower VAArg here for these cases and use
  // the LLVM va_arg instruction for everything else.
  if (!isAggregateTypeForABI(Ty) && !isIllegalVectorType(Ty))
    return EmitVAArgInstr(CGF, VAListAddr, Ty, ABIArgInfo::getDirect());

  CharUnits SlotSize = CharUnits::fromQuantity(8);

  // Empty records are ignored for parameter passing purposes.
  if (isEmptyRecord(getContext(), Ty, true)) {
    Address Addr(CGF.Builder.CreateLoad(VAListAddr, "ap.cur"), SlotSize);
    Addr = CGF.Builder.CreateElementBitCast(Addr, CGF.ConvertTypeForMem(Ty));
    return Addr;
  }

  // The size of the actual thing passed, which might end up just
  // being a pointer for indirect types.
  auto TyInfo = getContext().getTypeInfoInChars(Ty);

  // Arguments bigger than 16 bytes which aren't homogeneous
  // aggregates should be passed indirectly.
  bool IsIndirect = false;
  if (TyInfo.first.getQuantity() > 16) {
    const Type *Base = nullptr;
    uint64_t Members = 0;
    IsIndirect = !isHomogeneousAggregate(Ty, Base, Members);
  }

  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect,
                          TyInfo, SlotSize, /*AllowHigherAlign*/ true);
}

Address AArch64ABIInfo::EmitMSVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                    QualType Ty) const {
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*indirect*/ false,
                          CGF.getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(8),
                          /*allowHigherAlign*/ false);
}

//===----------------------------------------------------------------------===//
// ARM ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class ARMABIInfo : public SwiftABIInfo {
public:
  enum ABIKind {
    APCS = 0,
    AAPCS = 1,
    AAPCS_VFP = 2,
    AAPCS16_VFP = 3,
  };

private:
  ABIKind Kind;

public:
  ARMABIInfo(CodeGenTypes &CGT, ABIKind _Kind)
      : SwiftABIInfo(CGT), Kind(_Kind) {
    setCCs();
  }

  bool isEABI() const {
    switch (getTarget().getTriple().getEnvironment()) {
    case llvm::Triple::Android:
    case llvm::Triple::EABI:
    case llvm::Triple::EABIHF:
    case llvm::Triple::GNUEABI:
    case llvm::Triple::GNUEABIHF:
    case llvm::Triple::MuslEABI:
    case llvm::Triple::MuslEABIHF:
      return true;
    default:
      return false;
    }
  }

  bool isEABIHF() const {
    switch (getTarget().getTriple().getEnvironment()) {
    case llvm::Triple::EABIHF:
    case llvm::Triple::GNUEABIHF:
    case llvm::Triple::MuslEABIHF:
      return true;
    default:
      return false;
    }
  }

  ABIKind getABIKind() const { return Kind; }

private:
  ABIArgInfo classifyReturnType(QualType RetTy, bool isVariadic) const;
  ABIArgInfo classifyArgumentType(QualType RetTy, bool isVariadic) const;
  ABIArgInfo classifyHomogeneousAggregate(QualType Ty, const Type *Base,
                                          uint64_t Members) const;
  ABIArgInfo coerceIllegalVector(QualType Ty) const;
  bool isIllegalVectorType(QualType Ty) const;

  bool isHomogeneousAggregateBaseType(QualType Ty) const override;
  bool isHomogeneousAggregateSmallEnough(const Type *Ty,
                                         uint64_t Members) const override;

  void computeInfo(CGFunctionInfo &FI) const override;

  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;

  llvm::CallingConv::ID getLLVMDefaultCC() const;
  llvm::CallingConv::ID getABIDefaultCC() const;
  void setCCs();

  bool shouldPassIndirectlyForSwift(ArrayRef<llvm::Type*> scalars,
                                    bool asReturnValue) const override {
    return occupiesMoreThan(CGT, scalars, /*total*/ 4);
  }
  bool isSwiftErrorInRegister() const override {
    return true;
  }
  bool isLegalVectorTypeForSwift(CharUnits totalSize, llvm::Type *eltTy,
                                 unsigned elts) const override;
};

class ARMTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  ARMTargetCodeGenInfo(CodeGenTypes &CGT, ARMABIInfo::ABIKind K)
    :TargetCodeGenInfo(new ARMABIInfo(CGT, K)) {}

  const ARMABIInfo &getABIInfo() const {
    return static_cast<const ARMABIInfo&>(TargetCodeGenInfo::getABIInfo());
  }

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    return 13;
  }

  StringRef getARCRetainAutoreleasedReturnValueMarker() const override {
    return "mov\tr7, r7\t\t// marker for objc_retainAutoreleaseReturnValue";
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override {
    llvm::Value *Four8 = llvm::ConstantInt::get(CGF.Int8Ty, 4);

    // 0-15 are the 16 integer registers.
    AssignToArrayRange(CGF.Builder, Address, Four8, 0, 15);
    return false;
  }

  unsigned getSizeOfUnwindException() const override {
    if (getABIInfo().isEABI()) return 88;
    return TargetCodeGenInfo::getSizeOfUnwindException();
  }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    if (GV->isDeclaration())
      return;
    const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD)
      return;

    const ARMInterruptAttr *Attr = FD->getAttr<ARMInterruptAttr>();
    if (!Attr)
      return;

    const char *Kind;
    switch (Attr->getInterrupt()) {
    case ARMInterruptAttr::Generic: Kind = ""; break;
    case ARMInterruptAttr::IRQ:     Kind = "IRQ"; break;
    case ARMInterruptAttr::FIQ:     Kind = "FIQ"; break;
    case ARMInterruptAttr::SWI:     Kind = "SWI"; break;
    case ARMInterruptAttr::ABORT:   Kind = "ABORT"; break;
    case ARMInterruptAttr::UNDEF:   Kind = "UNDEF"; break;
    }

    llvm::Function *Fn = cast<llvm::Function>(GV);

    Fn->addFnAttr("interrupt", Kind);

    ARMABIInfo::ABIKind ABI = cast<ARMABIInfo>(getABIInfo()).getABIKind();
    if (ABI == ARMABIInfo::APCS)
      return;

    // AAPCS guarantees that sp will be 8-byte aligned on any public interface,
    // however this is not necessarily true on taking any interrupt. Instruct
    // the backend to perform a realignment as part of the function prologue.
    llvm::AttrBuilder B;
    B.addStackAlignmentAttr(8);
    Fn->addAttributes(llvm::AttributeList::FunctionIndex, B);
  }
};

class WindowsARMTargetCodeGenInfo : public ARMTargetCodeGenInfo {
public:
  WindowsARMTargetCodeGenInfo(CodeGenTypes &CGT, ARMABIInfo::ABIKind K)
      : ARMTargetCodeGenInfo(CGT, K) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override;

  void getDependentLibraryOption(llvm::StringRef Lib,
                                 llvm::SmallString<24> &Opt) const override {
    Opt = "/DEFAULTLIB:" + qualifyWindowsLibrary(Lib);
  }

  void getDetectMismatchOption(llvm::StringRef Name, llvm::StringRef Value,
                               llvm::SmallString<32> &Opt) const override {
    Opt = "/FAILIFMISMATCH:\"" + Name.str() + "=" + Value.str() + "\"";
  }
};

void WindowsARMTargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &CGM) const {
  ARMTargetCodeGenInfo::setTargetAttributes(D, GV, CGM);
  if (GV->isDeclaration())
    return;
  addStackProbeTargetAttributes(D, GV, CGM);
}
}

void ARMABIInfo::computeInfo(CGFunctionInfo &FI) const {
  if (!::classifyReturnType(getCXXABI(), FI, *this))
    FI.getReturnInfo() =
        classifyReturnType(FI.getReturnType(), FI.isVariadic());

  for (auto &I : FI.arguments())
    I.info = classifyArgumentType(I.type, FI.isVariadic());

  // Always honor user-specified calling convention.
  if (FI.getCallingConvention() != llvm::CallingConv::C)
    return;

  llvm::CallingConv::ID cc = getRuntimeCC();
  if (cc != llvm::CallingConv::C)
    FI.setEffectiveCallingConvention(cc);
}

/// Return the default calling convention that LLVM will use.
llvm::CallingConv::ID ARMABIInfo::getLLVMDefaultCC() const {
  // The default calling convention that LLVM will infer.
  if (isEABIHF() || getTarget().getTriple().isWatchABI())
    return llvm::CallingConv::ARM_AAPCS_VFP;
  else if (isEABI())
    return llvm::CallingConv::ARM_AAPCS;
  else
    return llvm::CallingConv::ARM_APCS;
}

/// Return the calling convention that our ABI would like us to use
/// as the C calling convention.
llvm::CallingConv::ID ARMABIInfo::getABIDefaultCC() const {
  switch (getABIKind()) {
  case APCS: return llvm::CallingConv::ARM_APCS;
  case AAPCS: return llvm::CallingConv::ARM_AAPCS;
  case AAPCS_VFP: return llvm::CallingConv::ARM_AAPCS_VFP;
  case AAPCS16_VFP: return llvm::CallingConv::ARM_AAPCS_VFP;
  }
  llvm_unreachable("bad ABI kind");
}

void ARMABIInfo::setCCs() {
  assert(getRuntimeCC() == llvm::CallingConv::C);

  // Don't muddy up the IR with a ton of explicit annotations if
  // they'd just match what LLVM will infer from the triple.
  llvm::CallingConv::ID abiCC = getABIDefaultCC();
  if (abiCC != getLLVMDefaultCC())
    RuntimeCC = abiCC;
}

ABIArgInfo ARMABIInfo::coerceIllegalVector(QualType Ty) const {
  uint64_t Size = getContext().getTypeSize(Ty);
  if (Size <= 32) {
    llvm::Type *ResType =
        llvm::Type::getInt32Ty(getVMContext());
    return ABIArgInfo::getDirect(ResType);
  }
  if (Size == 64 || Size == 128) {
    llvm::Type *ResType = llvm::VectorType::get(
        llvm::Type::getInt32Ty(getVMContext()), Size / 32);
    return ABIArgInfo::getDirect(ResType);
  }
  return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
}

ABIArgInfo ARMABIInfo::classifyHomogeneousAggregate(QualType Ty,
                                                    const Type *Base,
                                                    uint64_t Members) const {
  assert(Base && "Base class should be set for homogeneous aggregate");
  // Base can be a floating-point or a vector.
  if (const VectorType *VT = Base->getAs<VectorType>()) {
    // FP16 vectors should be converted to integer vectors
    if (!getTarget().hasLegalHalfType() &&
        (VT->getElementType()->isFloat16Type() ||
          VT->getElementType()->isHalfType())) {
      uint64_t Size = getContext().getTypeSize(VT);
      llvm::Type *NewVecTy = llvm::VectorType::get(
          llvm::Type::getInt32Ty(getVMContext()), Size / 32);
      llvm::Type *Ty = llvm::ArrayType::get(NewVecTy, Members);
      return ABIArgInfo::getDirect(Ty, 0, nullptr, false);
    }
  }
  return ABIArgInfo::getDirect(nullptr, 0, nullptr, false);
}

ABIArgInfo ARMABIInfo::classifyArgumentType(QualType Ty,
                                            bool isVariadic) const {
  // 6.1.2.1 The following argument types are VFP CPRCs:
  //   A single-precision floating-point type (including promoted
  //   half-precision types); A double-precision floating-point type;
  //   A 64-bit or 128-bit containerized vector type; Homogeneous Aggregate
  //   with a Base Type of a single- or double-precision floating-point type,
  //   64-bit containerized vectors or 128-bit containerized vectors with one
  //   to four Elements.
  bool IsEffectivelyAAPCS_VFP = getABIKind() == AAPCS_VFP && !isVariadic;

  Ty = useFirstFieldIfTransparentUnion(Ty);

  // Handle illegal vector types here.
  if (isIllegalVectorType(Ty))
    return coerceIllegalVector(Ty);

  // _Float16 and __fp16 get passed as if it were an int or float, but with
  // the top 16 bits unspecified. This is not done for OpenCL as it handles the
  // half type natively, and does not need to interwork with AAPCS code.
  if ((Ty->isFloat16Type() || Ty->isHalfType()) &&
      !getContext().getLangOpts().NativeHalfArgsAndReturns) {
    llvm::Type *ResType = IsEffectivelyAAPCS_VFP ?
      llvm::Type::getFloatTy(getVMContext()) :
      llvm::Type::getInt32Ty(getVMContext());
    return ABIArgInfo::getDirect(ResType);
  }

  if (!isAggregateTypeForABI(Ty)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>()) {
      Ty = EnumTy->getDecl()->getIntegerType();
    }

    return (Ty->isPromotableIntegerType() ? ABIArgInfo::getExtend(Ty)
                                          : ABIArgInfo::getDirect());
  }

  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI())) {
    return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);
  }

  // Ignore empty records.
  if (isEmptyRecord(getContext(), Ty, true))
    return ABIArgInfo::getIgnore();

  if (IsEffectivelyAAPCS_VFP) {
    // Homogeneous Aggregates need to be expanded when we can fit the aggregate
    // into VFP registers.
    const Type *Base = nullptr;
    uint64_t Members = 0;
    if (isHomogeneousAggregate(Ty, Base, Members))
      return classifyHomogeneousAggregate(Ty, Base, Members);
  } else if (getABIKind() == ARMABIInfo::AAPCS16_VFP) {
    // WatchOS does have homogeneous aggregates. Note that we intentionally use
    // this convention even for a variadic function: the backend will use GPRs
    // if needed.
    const Type *Base = nullptr;
    uint64_t Members = 0;
    if (isHomogeneousAggregate(Ty, Base, Members)) {
      assert(Base && Members <= 4 && "unexpected homogeneous aggregate");
      llvm::Type *Ty =
        llvm::ArrayType::get(CGT.ConvertType(QualType(Base, 0)), Members);
      return ABIArgInfo::getDirect(Ty, 0, nullptr, false);
    }
  }

  if (getABIKind() == ARMABIInfo::AAPCS16_VFP &&
      getContext().getTypeSizeInChars(Ty) > CharUnits::fromQuantity(16)) {
    // WatchOS is adopting the 64-bit AAPCS rule on composite types: if they're
    // bigger than 128-bits, they get placed in space allocated by the caller,
    // and a pointer is passed.
    return ABIArgInfo::getIndirect(
        CharUnits::fromQuantity(getContext().getTypeAlign(Ty) / 8), false);
  }

  // Support byval for ARM.
  // The ABI alignment for APCS is 4-byte and for AAPCS at least 4-byte and at
  // most 8-byte. We realign the indirect argument if type alignment is bigger
  // than ABI alignment.
  uint64_t ABIAlign = 4;
  uint64_t TyAlign;
  if (getABIKind() == ARMABIInfo::AAPCS_VFP ||
      getABIKind() == ARMABIInfo::AAPCS) {
    TyAlign = getContext().getTypeUnadjustedAlignInChars(Ty).getQuantity();
    ABIAlign = std::min(std::max(TyAlign, (uint64_t)4), (uint64_t)8);
  } else {
    TyAlign = getContext().getTypeAlignInChars(Ty).getQuantity();
  }
  if (getContext().getTypeSizeInChars(Ty) > CharUnits::fromQuantity(64)) {
    assert(getABIKind() != ARMABIInfo::AAPCS16_VFP && "unexpected byval");
    return ABIArgInfo::getIndirect(CharUnits::fromQuantity(ABIAlign),
                                   /*ByVal=*/true,
                                   /*Realign=*/TyAlign > ABIAlign);
  }

  // On RenderScript, coerce Aggregates <= 64 bytes to an integer array of
  // same size and alignment.
  if (getTarget().isRenderScriptTarget()) {
    return coerceToIntArray(Ty, getContext(), getVMContext());
  }

  // Otherwise, pass by coercing to a structure of the appropriate size.
  llvm::Type* ElemTy;
  unsigned SizeRegs;
  // FIXME: Try to match the types of the arguments more accurately where
  // we can.
  if (TyAlign <= 4) {
    ElemTy = llvm::Type::getInt32Ty(getVMContext());
    SizeRegs = (getContext().getTypeSize(Ty) + 31) / 32;
  } else {
    ElemTy = llvm::Type::getInt64Ty(getVMContext());
    SizeRegs = (getContext().getTypeSize(Ty) + 63) / 64;
  }

  return ABIArgInfo::getDirect(llvm::ArrayType::get(ElemTy, SizeRegs));
}

static bool isIntegerLikeType(QualType Ty, ASTContext &Context,
                              llvm::LLVMContext &VMContext) {
  // APCS, C Language Calling Conventions, Non-Simple Return Values: A structure
  // is called integer-like if its size is less than or equal to one word, and
  // the offset of each of its addressable sub-fields is zero.

  uint64_t Size = Context.getTypeSize(Ty);

  // Check that the type fits in a word.
  if (Size > 32)
    return false;

  // FIXME: Handle vector types!
  if (Ty->isVectorType())
    return false;

  // Float types are never treated as "integer like".
  if (Ty->isRealFloatingType())
    return false;

  // If this is a builtin or pointer type then it is ok.
  if (Ty->getAs<BuiltinType>() || Ty->isPointerType())
    return true;

  // Small complex integer types are "integer like".
  if (const ComplexType *CT = Ty->getAs<ComplexType>())
    return isIntegerLikeType(CT->getElementType(), Context, VMContext);

  // Single element and zero sized arrays should be allowed, by the definition
  // above, but they are not.

  // Otherwise, it must be a record type.
  const RecordType *RT = Ty->getAs<RecordType>();
  if (!RT) return false;

  // Ignore records with flexible arrays.
  const RecordDecl *RD = RT->getDecl();
  if (RD->hasFlexibleArrayMember())
    return false;

  // Check that all sub-fields are at offset 0, and are themselves "integer
  // like".
  const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);

  bool HadField = false;
  unsigned idx = 0;
  for (RecordDecl::field_iterator i = RD->field_begin(), e = RD->field_end();
       i != e; ++i, ++idx) {
    const FieldDecl *FD = *i;

    // Bit-fields are not addressable, we only need to verify they are "integer
    // like". We still have to disallow a subsequent non-bitfield, for example:
    //   struct { int : 0; int x }
    // is non-integer like according to gcc.
    if (FD->isBitField()) {
      if (!RD->isUnion())
        HadField = true;

      if (!isIntegerLikeType(FD->getType(), Context, VMContext))
        return false;

      continue;
    }

    // Check if this field is at offset 0.
    if (Layout.getFieldOffset(idx) != 0)
      return false;

    if (!isIntegerLikeType(FD->getType(), Context, VMContext))
      return false;

    // Only allow at most one field in a structure. This doesn't match the
    // wording above, but follows gcc in situations with a field following an
    // empty structure.
    if (!RD->isUnion()) {
      if (HadField)
        return false;

      HadField = true;
    }
  }

  return true;
}

ABIArgInfo ARMABIInfo::classifyReturnType(QualType RetTy,
                                          bool isVariadic) const {
  bool IsEffectivelyAAPCS_VFP =
      (getABIKind() == AAPCS_VFP || getABIKind() == AAPCS16_VFP) && !isVariadic;

  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  if (const VectorType *VT = RetTy->getAs<VectorType>()) {
    // Large vector types should be returned via memory.
    if (getContext().getTypeSize(RetTy) > 128)
      return getNaturalAlignIndirect(RetTy);
    // FP16 vectors should be converted to integer vectors
    if (!getTarget().hasLegalHalfType() &&
        (VT->getElementType()->isFloat16Type() ||
         VT->getElementType()->isHalfType()))
      return coerceIllegalVector(RetTy);
  }

  // _Float16 and __fp16 get returned as if it were an int or float, but with
  // the top 16 bits unspecified. This is not done for OpenCL as it handles the
  // half type natively, and does not need to interwork with AAPCS code.
  if ((RetTy->isFloat16Type() || RetTy->isHalfType()) &&
      !getContext().getLangOpts().NativeHalfArgsAndReturns) {
    llvm::Type *ResType = IsEffectivelyAAPCS_VFP ?
      llvm::Type::getFloatTy(getVMContext()) :
      llvm::Type::getInt32Ty(getVMContext());
    return ABIArgInfo::getDirect(ResType);
  }

  if (!isAggregateTypeForABI(RetTy)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
      RetTy = EnumTy->getDecl()->getIntegerType();

    return RetTy->isPromotableIntegerType() ? ABIArgInfo::getExtend(RetTy)
                                            : ABIArgInfo::getDirect();
  }

  // Are we following APCS?
  if (getABIKind() == APCS) {
    if (isEmptyRecord(getContext(), RetTy, false))
      return ABIArgInfo::getIgnore();

    // Complex types are all returned as packed integers.
    //
    // FIXME: Consider using 2 x vector types if the back end handles them
    // correctly.
    if (RetTy->isAnyComplexType())
      return ABIArgInfo::getDirect(llvm::IntegerType::get(
          getVMContext(), getContext().getTypeSize(RetTy)));

    // Integer like structures are returned in r0.
    if (isIntegerLikeType(RetTy, getContext(), getVMContext())) {
      // Return in the smallest viable integer type.
      uint64_t Size = getContext().getTypeSize(RetTy);
      if (Size <= 8)
        return ABIArgInfo::getDirect(llvm::Type::getInt8Ty(getVMContext()));
      if (Size <= 16)
        return ABIArgInfo::getDirect(llvm::Type::getInt16Ty(getVMContext()));
      return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));
    }

    // Otherwise return in memory.
    return getNaturalAlignIndirect(RetTy);
  }

  // Otherwise this is an AAPCS variant.

  if (isEmptyRecord(getContext(), RetTy, true))
    return ABIArgInfo::getIgnore();

  // Check for homogeneous aggregates with AAPCS-VFP.
  if (IsEffectivelyAAPCS_VFP) {
    const Type *Base = nullptr;
    uint64_t Members = 0;
    if (isHomogeneousAggregate(RetTy, Base, Members))
      return classifyHomogeneousAggregate(RetTy, Base, Members);
  }

  // Aggregates <= 4 bytes are returned in r0; other aggregates
  // are returned indirectly.
  uint64_t Size = getContext().getTypeSize(RetTy);
  if (Size <= 32) {
    // On RenderScript, coerce Aggregates <= 4 bytes to an integer array of
    // same size and alignment.
    if (getTarget().isRenderScriptTarget()) {
      return coerceToIntArray(RetTy, getContext(), getVMContext());
    }
    if (getDataLayout().isBigEndian())
      // Return in 32 bit integer integer type (as if loaded by LDR, AAPCS 5.4)
      return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));

    // Return in the smallest viable integer type.
    if (Size <= 8)
      return ABIArgInfo::getDirect(llvm::Type::getInt8Ty(getVMContext()));
    if (Size <= 16)
      return ABIArgInfo::getDirect(llvm::Type::getInt16Ty(getVMContext()));
    return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));
  } else if (Size <= 128 && getABIKind() == AAPCS16_VFP) {
    llvm::Type *Int32Ty = llvm::Type::getInt32Ty(getVMContext());
    llvm::Type *CoerceTy =
        llvm::ArrayType::get(Int32Ty, llvm::alignTo(Size, 32) / 32);
    return ABIArgInfo::getDirect(CoerceTy);
  }

  return getNaturalAlignIndirect(RetTy);
}

/// isIllegalVector - check whether Ty is an illegal vector type.
bool ARMABIInfo::isIllegalVectorType(QualType Ty) const {
  if (const VectorType *VT = Ty->getAs<VectorType> ()) {
    // On targets that don't support FP16, FP16 is expanded into float, and we
    // don't want the ABI to depend on whether or not FP16 is supported in
    // hardware. Thus return false to coerce FP16 vectors into integer vectors.
    if (!getTarget().hasLegalHalfType() &&
        (VT->getElementType()->isFloat16Type() ||
         VT->getElementType()->isHalfType()))
      return true;
    if (isAndroid()) {
      // Android shipped using Clang 3.1, which supported a slightly different
      // vector ABI. The primary differences were that 3-element vector types
      // were legal, and so were sub 32-bit vectors (i.e. <2 x i8>). This path
      // accepts that legacy behavior for Android only.
      // Check whether VT is legal.
      unsigned NumElements = VT->getNumElements();
      // NumElements should be power of 2 or equal to 3.
      if (!llvm::isPowerOf2_32(NumElements) && NumElements != 3)
        return true;
    } else {
      // Check whether VT is legal.
      unsigned NumElements = VT->getNumElements();
      uint64_t Size = getContext().getTypeSize(VT);
      // NumElements should be power of 2.
      if (!llvm::isPowerOf2_32(NumElements))
        return true;
      // Size should be greater than 32 bits.
      return Size <= 32;
    }
  }
  return false;
}

bool ARMABIInfo::isLegalVectorTypeForSwift(CharUnits vectorSize,
                                           llvm::Type *eltTy,
                                           unsigned numElts) const {
  if (!llvm::isPowerOf2_32(numElts))
    return false;
  unsigned size = getDataLayout().getTypeStoreSizeInBits(eltTy);
  if (size > 64)
    return false;
  if (vectorSize.getQuantity() != 8 &&
      (vectorSize.getQuantity() != 16 || numElts == 1))
    return false;
  return true;
}

bool ARMABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  // Homogeneous aggregates for AAPCS-VFP must have base types of float,
  // double, or 64-bit or 128-bit vectors.
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    if (BT->getKind() == BuiltinType::Float ||
        BT->getKind() == BuiltinType::Double ||
        BT->getKind() == BuiltinType::LongDouble)
      return true;
  } else if (const VectorType *VT = Ty->getAs<VectorType>()) {
    unsigned VecSize = getContext().getTypeSize(VT);
    if (VecSize == 64 || VecSize == 128)
      return true;
  }
  return false;
}

bool ARMABIInfo::isHomogeneousAggregateSmallEnough(const Type *Base,
                                                   uint64_t Members) const {
  return Members <= 4;
}

Address ARMABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                              QualType Ty) const {
  CharUnits SlotSize = CharUnits::fromQuantity(4);

  // Empty records are ignored for parameter passing purposes.
  if (isEmptyRecord(getContext(), Ty, true)) {
    Address Addr(CGF.Builder.CreateLoad(VAListAddr), SlotSize);
    Addr = CGF.Builder.CreateElementBitCast(Addr, CGF.ConvertTypeForMem(Ty));
    return Addr;
  }

  auto TyInfo = getContext().getTypeInfoInChars(Ty);
  CharUnits TyAlignForABI = TyInfo.second;

  // Use indirect if size of the illegal vector is bigger than 16 bytes.
  bool IsIndirect = false;
  const Type *Base = nullptr;
  uint64_t Members = 0;
  if (TyInfo.first > CharUnits::fromQuantity(16) && isIllegalVectorType(Ty)) {
    IsIndirect = true;

  // ARMv7k passes structs bigger than 16 bytes indirectly, in space
  // allocated by the caller.
  } else if (TyInfo.first > CharUnits::fromQuantity(16) &&
             getABIKind() == ARMABIInfo::AAPCS16_VFP &&
             !isHomogeneousAggregate(Ty, Base, Members)) {
    IsIndirect = true;

  // Otherwise, bound the type's ABI alignment.
  // The ABI alignment for 64-bit or 128-bit vectors is 8 for AAPCS and 4 for
  // APCS. For AAPCS, the ABI alignment is at least 4-byte and at most 8-byte.
  // Our callers should be prepared to handle an under-aligned address.
  } else if (getABIKind() == ARMABIInfo::AAPCS_VFP ||
             getABIKind() == ARMABIInfo::AAPCS) {
    TyAlignForABI = std::max(TyAlignForABI, CharUnits::fromQuantity(4));
    TyAlignForABI = std::min(TyAlignForABI, CharUnits::fromQuantity(8));
  } else if (getABIKind() == ARMABIInfo::AAPCS16_VFP) {
    // ARMv7k allows type alignment up to 16 bytes.
    TyAlignForABI = std::max(TyAlignForABI, CharUnits::fromQuantity(4));
    TyAlignForABI = std::min(TyAlignForABI, CharUnits::fromQuantity(16));
  } else {
    TyAlignForABI = CharUnits::fromQuantity(4);
  }
  TyInfo.second = TyAlignForABI;

  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect, TyInfo,
                          SlotSize, /*AllowHigherAlign*/ true);
}

//===----------------------------------------------------------------------===//
// NVPTX ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class NVPTXABIInfo : public ABIInfo {
public:
  NVPTXABIInfo(CodeGenTypes &CGT) : ABIInfo(CGT) {}

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType Ty) const;

  void computeInfo(CGFunctionInfo &FI) const override;
  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;
};

class NVPTXTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  NVPTXTargetCodeGenInfo(CodeGenTypes &CGT)
    : TargetCodeGenInfo(new NVPTXABIInfo(CGT)) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override;
  bool shouldEmitStaticExternCAliases() const override;

private:
  // Adds a NamedMDNode with F, Name, and Operand as operands, and adds the
  // resulting MDNode to the nvvm.annotations MDNode.
  static void addNVVMMetadata(llvm::Function *F, StringRef Name, int Operand);
};

ABIArgInfo NVPTXABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  // note: this is different from default ABI
  if (!RetTy->isScalarType())
    return ABIArgInfo::getDirect();

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
    RetTy = EnumTy->getDecl()->getIntegerType();

  return (RetTy->isPromotableIntegerType() ? ABIArgInfo::getExtend(RetTy)
                                           : ABIArgInfo::getDirect());
}

ABIArgInfo NVPTXABIInfo::classifyArgumentType(QualType Ty) const {
  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  // Return aggregates type as indirect by value
  if (isAggregateTypeForABI(Ty))
    return getNaturalAlignIndirect(Ty, /* byval */ true);

  return (Ty->isPromotableIntegerType() ? ABIArgInfo::getExtend(Ty)
                                        : ABIArgInfo::getDirect());
}

void NVPTXABIInfo::computeInfo(CGFunctionInfo &FI) const {
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  for (auto &I : FI.arguments())
    I.info = classifyArgumentType(I.type);

  // Always honor user-specified calling convention.
  if (FI.getCallingConvention() != llvm::CallingConv::C)
    return;

  FI.setEffectiveCallingConvention(getRuntimeCC());
}

Address NVPTXABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                QualType Ty) const {
  llvm_unreachable("NVPTX does not support varargs");
}

void NVPTXTargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &M) const {
  if (GV->isDeclaration())
    return;
  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (!FD) return;

  llvm::Function *F = cast<llvm::Function>(GV);

  // Perform special handling in OpenCL mode
  if (M.getLangOpts().OpenCL) {
    // Use OpenCL function attributes to check for kernel functions
    // By default, all functions are device functions
    if (FD->hasAttr<OpenCLKernelAttr>()) {
      // OpenCL __kernel functions get kernel metadata
      // Create !{<func-ref>, metadata !"kernel", i32 1} node
      addNVVMMetadata(F, "kernel", 1);
      // And kernel functions are not subject to inlining
      F->addFnAttr(llvm::Attribute::NoInline);
    }
  }

  // Perform special handling in CUDA mode.
  if (M.getLangOpts().CUDA) {
    // CUDA __global__ functions get a kernel metadata entry.  Since
    // __global__ functions cannot be called from the device, we do not
    // need to set the noinline attribute.
    if (FD->hasAttr<CUDAGlobalAttr>()) {
      // Create !{<func-ref>, metadata !"kernel", i32 1} node
      addNVVMMetadata(F, "kernel", 1);
    }
    if (CUDALaunchBoundsAttr *Attr = FD->getAttr<CUDALaunchBoundsAttr>()) {
      // Create !{<func-ref>, metadata !"maxntidx", i32 <val>} node
      llvm::APSInt MaxThreads(32);
      MaxThreads = Attr->getMaxThreads()->EvaluateKnownConstInt(M.getContext());
      if (MaxThreads > 0)
        addNVVMMetadata(F, "maxntidx", MaxThreads.getExtValue());

      // min blocks is an optional argument for CUDALaunchBoundsAttr. If it was
      // not specified in __launch_bounds__ or if the user specified a 0 value,
      // we don't have to add a PTX directive.
      if (Attr->getMinBlocks()) {
        llvm::APSInt MinBlocks(32);
        MinBlocks = Attr->getMinBlocks()->EvaluateKnownConstInt(M.getContext());
        if (MinBlocks > 0)
          // Create !{<func-ref>, metadata !"minctasm", i32 <val>} node
          addNVVMMetadata(F, "minctasm", MinBlocks.getExtValue());
      }
    }
  }
}

void NVPTXTargetCodeGenInfo::addNVVMMetadata(llvm::Function *F, StringRef Name,
                                             int Operand) {
  llvm::Module *M = F->getParent();
  llvm::LLVMContext &Ctx = M->getContext();

  // Get "nvvm.annotations" metadata node
  llvm::NamedMDNode *MD = M->getOrInsertNamedMetadata("nvvm.annotations");

  llvm::Metadata *MDVals[] = {
      llvm::ConstantAsMetadata::get(F), llvm::MDString::get(Ctx, Name),
      llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), Operand))};
  // Append metadata to nvvm.annotations
  MD->addOperand(llvm::MDNode::get(Ctx, MDVals));
}

bool NVPTXTargetCodeGenInfo::shouldEmitStaticExternCAliases() const {
  return false;
}
}

//===----------------------------------------------------------------------===//
// SystemZ ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class SystemZABIInfo : public SwiftABIInfo {
  bool HasVector;

public:
  SystemZABIInfo(CodeGenTypes &CGT, bool HV)
    : SwiftABIInfo(CGT), HasVector(HV) {}

  bool isPromotableIntegerType(QualType Ty) const;
  bool isCompoundType(QualType Ty) const;
  bool isVectorArgumentType(QualType Ty) const;
  bool isFPArgumentType(QualType Ty) const;
  QualType GetSingleElementType(QualType Ty) const;

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType ArgTy) const;

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type);
  }

  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;

  bool shouldPassIndirectlyForSwift(ArrayRef<llvm::Type*> scalars,
                                    bool asReturnValue) const override {
    return occupiesMoreThan(CGT, scalars, /*total*/ 4);
  }
  bool isSwiftErrorInRegister() const override {
    return false;
  }
};

class SystemZTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  SystemZTargetCodeGenInfo(CodeGenTypes &CGT, bool HasVector)
    : TargetCodeGenInfo(new SystemZABIInfo(CGT, HasVector)) {}
};

}

bool SystemZABIInfo::isPromotableIntegerType(QualType Ty) const {
  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  // Promotable integer types are required to be promoted by the ABI.
  if (Ty->isPromotableIntegerType())
    return true;

  // 32-bit values must also be promoted.
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>())
    switch (BT->getKind()) {
    case BuiltinType::Int:
    case BuiltinType::UInt:
      return true;
    default:
      return false;
    }
  return false;
}

bool SystemZABIInfo::isCompoundType(QualType Ty) const {
  return (Ty->isAnyComplexType() ||
          Ty->isVectorType() ||
          isAggregateTypeForABI(Ty));
}

bool SystemZABIInfo::isVectorArgumentType(QualType Ty) const {
  return (HasVector &&
          Ty->isVectorType() &&
          getContext().getTypeSize(Ty) <= 128);
}

bool SystemZABIInfo::isFPArgumentType(QualType Ty) const {
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>())
    switch (BT->getKind()) {
    case BuiltinType::Float:
    case BuiltinType::Double:
      return true;
    default:
      return false;
    }

  return false;
}

QualType SystemZABIInfo::GetSingleElementType(QualType Ty) const {
  if (const RecordType *RT = Ty->getAsStructureType()) {
    const RecordDecl *RD = RT->getDecl();
    QualType Found;

    // If this is a C++ record, check the bases first.
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD))
      for (const auto &I : CXXRD->bases()) {
        QualType Base = I.getType();

        // Empty bases don't affect things either way.
        if (isEmptyRecord(getContext(), Base, true))
          continue;

        if (!Found.isNull())
          return Ty;
        Found = GetSingleElementType(Base);
      }

    // Check the fields.
    for (const auto *FD : RD->fields()) {
      // For compatibility with GCC, ignore empty bitfields in C++ mode.
      // Unlike isSingleElementStruct(), empty structure and array fields
      // do count.  So do anonymous bitfields that aren't zero-sized.
      if (getContext().getLangOpts().CPlusPlus &&
          FD->isZeroLengthBitField(getContext()))
        continue;

      // Unlike isSingleElementStruct(), arrays do not count.
      // Nested structures still do though.
      if (!Found.isNull())
        return Ty;
      Found = GetSingleElementType(FD->getType());
    }

    // Unlike isSingleElementStruct(), trailing padding is allowed.
    // An 8-byte aligned struct s { float f; } is passed as a double.
    if (!Found.isNull())
      return Found;
  }

  return Ty;
}

Address SystemZABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                  QualType Ty) const {
  // Assume that va_list type is correct; should be pointer to LLVM type:
  // struct {
  //   i64 __gpr;
  //   i64 __fpr;
  //   i8 *__overflow_arg_area;
  //   i8 *__reg_save_area;
  // };

  // Every non-vector argument occupies 8 bytes and is passed by preference
  // in either GPRs or FPRs.  Vector arguments occupy 8 or 16 bytes and are
  // always passed on the stack.
  Ty = getContext().getCanonicalType(Ty);
  auto TyInfo = getContext().getTypeInfoInChars(Ty);
  llvm::Type *ArgTy = CGF.ConvertTypeForMem(Ty);
  llvm::Type *DirectTy = ArgTy;
  ABIArgInfo AI = classifyArgumentType(Ty);
  bool IsIndirect = AI.isIndirect();
  bool InFPRs = false;
  bool IsVector = false;
  CharUnits UnpaddedSize;
  CharUnits DirectAlign;
  if (IsIndirect) {
    DirectTy = llvm::PointerType::getUnqual(DirectTy);
    UnpaddedSize = DirectAlign = CharUnits::fromQuantity(8);
  } else {
    if (AI.getCoerceToType())
      ArgTy = AI.getCoerceToType();
    InFPRs = ArgTy->isFloatTy() || ArgTy->isDoubleTy();
    IsVector = ArgTy->isVectorTy();
    UnpaddedSize = TyInfo.first;
    DirectAlign = TyInfo.second;
  }
  CharUnits PaddedSize = CharUnits::fromQuantity(8);
  if (IsVector && UnpaddedSize > PaddedSize)
    PaddedSize = CharUnits::fromQuantity(16);
  assert((UnpaddedSize <= PaddedSize) && "Invalid argument size.");

  CharUnits Padding = (PaddedSize - UnpaddedSize);

  llvm::Type *IndexTy = CGF.Int64Ty;
  llvm::Value *PaddedSizeV =
    llvm::ConstantInt::get(IndexTy, PaddedSize.getQuantity());

  if (IsVector) {
    // Work out the address of a vector argument on the stack.
    // Vector arguments are always passed in the high bits of a
    // single (8 byte) or double (16 byte) stack slot.
    Address OverflowArgAreaPtr =
      CGF.Builder.CreateStructGEP(VAListAddr, 2, CharUnits::fromQuantity(16),
                                  "overflow_arg_area_ptr");
    Address OverflowArgArea =
      Address(CGF.Builder.CreateLoad(OverflowArgAreaPtr, "overflow_arg_area"),
              TyInfo.second);
    Address MemAddr =
      CGF.Builder.CreateElementBitCast(OverflowArgArea, DirectTy, "mem_addr");

    // Update overflow_arg_area_ptr pointer
    llvm::Value *NewOverflowArgArea =
      CGF.Builder.CreateGEP(OverflowArgArea.getPointer(), PaddedSizeV,
                            "overflow_arg_area");
    CGF.Builder.CreateStore(NewOverflowArgArea, OverflowArgAreaPtr);

    return MemAddr;
  }

  assert(PaddedSize.getQuantity() == 8);

  unsigned MaxRegs, RegCountField, RegSaveIndex;
  CharUnits RegPadding;
  if (InFPRs) {
    MaxRegs = 4; // Maximum of 4 FPR arguments
    RegCountField = 1; // __fpr
    RegSaveIndex = 16; // save offset for f0
    RegPadding = CharUnits(); // floats are passed in the high bits of an FPR
  } else {
    MaxRegs = 5; // Maximum of 5 GPR arguments
    RegCountField = 0; // __gpr
    RegSaveIndex = 2; // save offset for r2
    RegPadding = Padding; // values are passed in the low bits of a GPR
  }

  Address RegCountPtr = CGF.Builder.CreateStructGEP(
      VAListAddr, RegCountField, RegCountField * CharUnits::fromQuantity(8),
      "reg_count_ptr");
  llvm::Value *RegCount = CGF.Builder.CreateLoad(RegCountPtr, "reg_count");
  llvm::Value *MaxRegsV = llvm::ConstantInt::get(IndexTy, MaxRegs);
  llvm::Value *InRegs = CGF.Builder.CreateICmpULT(RegCount, MaxRegsV,
                                                 "fits_in_regs");

  llvm::BasicBlock *InRegBlock = CGF.createBasicBlock("vaarg.in_reg");
  llvm::BasicBlock *InMemBlock = CGF.createBasicBlock("vaarg.in_mem");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("vaarg.end");
  CGF.Builder.CreateCondBr(InRegs, InRegBlock, InMemBlock);

  // Emit code to load the value if it was passed in registers.
  CGF.EmitBlock(InRegBlock);

  // Work out the address of an argument register.
  llvm::Value *ScaledRegCount =
    CGF.Builder.CreateMul(RegCount, PaddedSizeV, "scaled_reg_count");
  llvm::Value *RegBase =
    llvm::ConstantInt::get(IndexTy, RegSaveIndex * PaddedSize.getQuantity()
                                      + RegPadding.getQuantity());
  llvm::Value *RegOffset =
    CGF.Builder.CreateAdd(ScaledRegCount, RegBase, "reg_offset");
  Address RegSaveAreaPtr =
      CGF.Builder.CreateStructGEP(VAListAddr, 3, CharUnits::fromQuantity(24),
                                  "reg_save_area_ptr");
  llvm::Value *RegSaveArea =
    CGF.Builder.CreateLoad(RegSaveAreaPtr, "reg_save_area");
  Address RawRegAddr(CGF.Builder.CreateGEP(RegSaveArea, RegOffset,
                                           "raw_reg_addr"),
                     PaddedSize);
  Address RegAddr =
    CGF.Builder.CreateElementBitCast(RawRegAddr, DirectTy, "reg_addr");

  // Update the register count
  llvm::Value *One = llvm::ConstantInt::get(IndexTy, 1);
  llvm::Value *NewRegCount =
    CGF.Builder.CreateAdd(RegCount, One, "reg_count");
  CGF.Builder.CreateStore(NewRegCount, RegCountPtr);
  CGF.EmitBranch(ContBlock);

  // Emit code to load the value if it was passed in memory.
  CGF.EmitBlock(InMemBlock);

  // Work out the address of a stack argument.
  Address OverflowArgAreaPtr = CGF.Builder.CreateStructGEP(
      VAListAddr, 2, CharUnits::fromQuantity(16), "overflow_arg_area_ptr");
  Address OverflowArgArea =
    Address(CGF.Builder.CreateLoad(OverflowArgAreaPtr, "overflow_arg_area"),
            PaddedSize);
  Address RawMemAddr =
    CGF.Builder.CreateConstByteGEP(OverflowArgArea, Padding, "raw_mem_addr");
  Address MemAddr =
    CGF.Builder.CreateElementBitCast(RawMemAddr, DirectTy, "mem_addr");

  // Update overflow_arg_area_ptr pointer
  llvm::Value *NewOverflowArgArea =
    CGF.Builder.CreateGEP(OverflowArgArea.getPointer(), PaddedSizeV,
                          "overflow_arg_area");
  CGF.Builder.CreateStore(NewOverflowArgArea, OverflowArgAreaPtr);
  CGF.EmitBranch(ContBlock);

  // Return the appropriate result.
  CGF.EmitBlock(ContBlock);
  Address ResAddr = emitMergePHI(CGF, RegAddr, InRegBlock,
                                 MemAddr, InMemBlock, "va_arg.addr");

  if (IsIndirect)
    ResAddr = Address(CGF.Builder.CreateLoad(ResAddr, "indirect_arg"),
                      TyInfo.second);

  return ResAddr;
}

ABIArgInfo SystemZABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();
  if (isVectorArgumentType(RetTy))
    return ABIArgInfo::getDirect();
  if (isCompoundType(RetTy) || getContext().getTypeSize(RetTy) > 64)
    return getNaturalAlignIndirect(RetTy);
  return (isPromotableIntegerType(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                         : ABIArgInfo::getDirect());
}

ABIArgInfo SystemZABIInfo::classifyArgumentType(QualType Ty) const {
  // Handle the generic C++ ABI.
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
    return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

  // Integers and enums are extended to full register width.
  if (isPromotableIntegerType(Ty))
    return ABIArgInfo::getExtend(Ty);

  // Handle vector types and vector-like structure types.  Note that
  // as opposed to float-like structure types, we do not allow any
  // padding for vector-like structures, so verify the sizes match.
  uint64_t Size = getContext().getTypeSize(Ty);
  QualType SingleElementTy = GetSingleElementType(Ty);
  if (isVectorArgumentType(SingleElementTy) &&
      getContext().getTypeSize(SingleElementTy) == Size)
    return ABIArgInfo::getDirect(CGT.ConvertType(SingleElementTy));

  // Values that are not 1, 2, 4 or 8 bytes in size are passed indirectly.
  if (Size != 8 && Size != 16 && Size != 32 && Size != 64)
    return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

  // Handle small structures.
  if (const RecordType *RT = Ty->getAs<RecordType>()) {
    // Structures with flexible arrays have variable length, so really
    // fail the size test above.
    const RecordDecl *RD = RT->getDecl();
    if (RD->hasFlexibleArrayMember())
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

    // The structure is passed as an unextended integer, a float, or a double.
    llvm::Type *PassTy;
    if (isFPArgumentType(SingleElementTy)) {
      assert(Size == 32 || Size == 64);
      if (Size == 32)
        PassTy = llvm::Type::getFloatTy(getVMContext());
      else
        PassTy = llvm::Type::getDoubleTy(getVMContext());
    } else
      PassTy = llvm::IntegerType::get(getVMContext(), Size);
    return ABIArgInfo::getDirect(PassTy);
  }

  // Non-structure compounds are passed indirectly.
  if (isCompoundType(Ty))
    return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

  return ABIArgInfo::getDirect(nullptr);
}

//===----------------------------------------------------------------------===//
// MSP430 ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class MSP430TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  MSP430TargetCodeGenInfo(CodeGenTypes &CGT)
    : TargetCodeGenInfo(new DefaultABIInfo(CGT)) {}
  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override;
};

}

void MSP430TargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &M) const {
  if (GV->isDeclaration())
    return;
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D)) {
    const auto *InterruptAttr = FD->getAttr<MSP430InterruptAttr>();
    if (!InterruptAttr)
      return;

    // Handle 'interrupt' attribute:
    llvm::Function *F = cast<llvm::Function>(GV);

    // Step 1: Set ISR calling convention.
    F->setCallingConv(llvm::CallingConv::MSP430_INTR);

    // Step 2: Add attributes goodness.
    F->addFnAttr(llvm::Attribute::NoInline);
    F->addFnAttr("interrupt", llvm::utostr(InterruptAttr->getNumber()));
  }
}

//===----------------------------------------------------------------------===//
// MIPS ABI Implementation.  This works for both little-endian and
// big-endian variants.
//===----------------------------------------------------------------------===//

namespace {
class MipsABIInfo : public ABIInfo {
  bool IsO32;
  unsigned MinABIStackAlignInBytes, StackAlignInBytes;
  void CoerceToIntArgs(uint64_t TySize,
                       SmallVectorImpl<llvm::Type *> &ArgList) const;
  llvm::Type* HandleAggregates(QualType Ty, uint64_t TySize) const;
  llvm::Type* returnAggregateInRegs(QualType RetTy, uint64_t Size) const;
  llvm::Type* getPaddingType(uint64_t Align, uint64_t Offset) const;
public:
  MipsABIInfo(CodeGenTypes &CGT, bool _IsO32) :
    ABIInfo(CGT), IsO32(_IsO32), MinABIStackAlignInBytes(IsO32 ? 4 : 8),
    StackAlignInBytes(IsO32 ? 8 : 16) {}

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType RetTy, uint64_t &Offset) const;
  void computeInfo(CGFunctionInfo &FI) const override;
  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;
  ABIArgInfo extendType(QualType Ty) const;
};

class MIPSTargetCodeGenInfo : public TargetCodeGenInfo {
  unsigned SizeOfUnwindException;
public:
  MIPSTargetCodeGenInfo(CodeGenTypes &CGT, bool IsO32)
    : TargetCodeGenInfo(new MipsABIInfo(CGT, IsO32)),
      SizeOfUnwindException(IsO32 ? 24 : 32) {}

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &CGM) const override {
    return 29;
  }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD) return;
    llvm::Function *Fn = cast<llvm::Function>(GV);

    if (FD->hasAttr<MipsLongCallAttr>())
      Fn->addFnAttr("long-call");
    else if (FD->hasAttr<MipsShortCallAttr>())
      Fn->addFnAttr("short-call");

    // Other attributes do not have a meaning for declarations.
    if (GV->isDeclaration())
      return;

    if (FD->hasAttr<Mips16Attr>()) {
      Fn->addFnAttr("mips16");
    }
    else if (FD->hasAttr<NoMips16Attr>()) {
      Fn->addFnAttr("nomips16");
    }

    if (FD->hasAttr<MicroMipsAttr>())
      Fn->addFnAttr("micromips");
    else if (FD->hasAttr<NoMicroMipsAttr>())
      Fn->addFnAttr("nomicromips");

    const MipsInterruptAttr *Attr = FD->getAttr<MipsInterruptAttr>();
    if (!Attr)
      return;

    const char *Kind;
    switch (Attr->getInterrupt()) {
    case MipsInterruptAttr::eic:     Kind = "eic"; break;
    case MipsInterruptAttr::sw0:     Kind = "sw0"; break;
    case MipsInterruptAttr::sw1:     Kind = "sw1"; break;
    case MipsInterruptAttr::hw0:     Kind = "hw0"; break;
    case MipsInterruptAttr::hw1:     Kind = "hw1"; break;
    case MipsInterruptAttr::hw2:     Kind = "hw2"; break;
    case MipsInterruptAttr::hw3:     Kind = "hw3"; break;
    case MipsInterruptAttr::hw4:     Kind = "hw4"; break;
    case MipsInterruptAttr::hw5:     Kind = "hw5"; break;
    }

    Fn->addFnAttr("interrupt", Kind);

  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override;

  unsigned getSizeOfUnwindException() const override {
    return SizeOfUnwindException;
  }
};
}

void MipsABIInfo::CoerceToIntArgs(
    uint64_t TySize, SmallVectorImpl<llvm::Type *> &ArgList) const {
  llvm::IntegerType *IntTy =
    llvm::IntegerType::get(getVMContext(), MinABIStackAlignInBytes * 8);

  // Add (TySize / MinABIStackAlignInBytes) args of IntTy.
  for (unsigned N = TySize / (MinABIStackAlignInBytes * 8); N; --N)
    ArgList.push_back(IntTy);

  // If necessary, add one more integer type to ArgList.
  unsigned R = TySize % (MinABIStackAlignInBytes * 8);

  if (R)
    ArgList.push_back(llvm::IntegerType::get(getVMContext(), R));
}

// In N32/64, an aligned double precision floating point field is passed in
// a register.
llvm::Type* MipsABIInfo::HandleAggregates(QualType Ty, uint64_t TySize) const {
  SmallVector<llvm::Type*, 8> ArgList, IntArgList;

  if (IsO32) {
    CoerceToIntArgs(TySize, ArgList);
    return llvm::StructType::get(getVMContext(), ArgList);
  }

  if (Ty->isComplexType())
    return CGT.ConvertType(Ty);

  const RecordType *RT = Ty->getAs<RecordType>();

  // Unions/vectors are passed in integer registers.
  if (!RT || !RT->isStructureOrClassType()) {
    CoerceToIntArgs(TySize, ArgList);
    return llvm::StructType::get(getVMContext(), ArgList);
  }

  const RecordDecl *RD = RT->getDecl();
  const ASTRecordLayout &Layout = getContext().getASTRecordLayout(RD);
  assert(!(TySize % 8) && "Size of structure must be multiple of 8.");

  uint64_t LastOffset = 0;
  unsigned idx = 0;
  llvm::IntegerType *I64 = llvm::IntegerType::get(getVMContext(), 64);

  // Iterate over fields in the struct/class and check if there are any aligned
  // double fields.
  for (RecordDecl::field_iterator i = RD->field_begin(), e = RD->field_end();
       i != e; ++i, ++idx) {
    const QualType Ty = i->getType();
    const BuiltinType *BT = Ty->getAs<BuiltinType>();

    if (!BT || BT->getKind() != BuiltinType::Double)
      continue;

    uint64_t Offset = Layout.getFieldOffset(idx);
    if (Offset % 64) // Ignore doubles that are not aligned.
      continue;

    // Add ((Offset - LastOffset) / 64) args of type i64.
    for (unsigned j = (Offset - LastOffset) / 64; j > 0; --j)
      ArgList.push_back(I64);

    // Add double type.
    ArgList.push_back(llvm::Type::getDoubleTy(getVMContext()));
    LastOffset = Offset + 64;
  }

  CoerceToIntArgs(TySize - LastOffset, IntArgList);
  ArgList.append(IntArgList.begin(), IntArgList.end());

  return llvm::StructType::get(getVMContext(), ArgList);
}

llvm::Type *MipsABIInfo::getPaddingType(uint64_t OrigOffset,
                                        uint64_t Offset) const {
  if (OrigOffset + MinABIStackAlignInBytes > Offset)
    return nullptr;

  return llvm::IntegerType::get(getVMContext(), (Offset - OrigOffset) * 8);
}

ABIArgInfo
MipsABIInfo::classifyArgumentType(QualType Ty, uint64_t &Offset) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  uint64_t OrigOffset = Offset;
  uint64_t TySize = getContext().getTypeSize(Ty);
  uint64_t Align = getContext().getTypeAlign(Ty) / 8;

  Align = std::min(std::max(Align, (uint64_t)MinABIStackAlignInBytes),
                   (uint64_t)StackAlignInBytes);
  unsigned CurrOffset = llvm::alignTo(Offset, Align);
  Offset = CurrOffset + llvm::alignTo(TySize, Align * 8) / 8;

  if (isAggregateTypeForABI(Ty) || Ty->isVectorType()) {
    // Ignore empty aggregates.
    if (TySize == 0)
      return ABIArgInfo::getIgnore();

    if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI())) {
      Offset = OrigOffset + MinABIStackAlignInBytes;
      return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);
    }

    // If we have reached here, aggregates are passed directly by coercing to
    // another structure type. Padding is inserted if the offset of the
    // aggregate is unaligned.
    ABIArgInfo ArgInfo =
        ABIArgInfo::getDirect(HandleAggregates(Ty, TySize), 0,
                              getPaddingType(OrigOffset, CurrOffset));
    ArgInfo.setInReg(true);
    return ArgInfo;
  }

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  // All integral types are promoted to the GPR width.
  if (Ty->isIntegralOrEnumerationType())
    return extendType(Ty);

  return ABIArgInfo::getDirect(
      nullptr, 0, IsO32 ? nullptr : getPaddingType(OrigOffset, CurrOffset));
}

llvm::Type*
MipsABIInfo::returnAggregateInRegs(QualType RetTy, uint64_t Size) const {
  const RecordType *RT = RetTy->getAs<RecordType>();
  SmallVector<llvm::Type*, 8> RTList;

  if (RT && RT->isStructureOrClassType()) {
    const RecordDecl *RD = RT->getDecl();
    const ASTRecordLayout &Layout = getContext().getASTRecordLayout(RD);
    unsigned FieldCnt = Layout.getFieldCount();

    // N32/64 returns struct/classes in floating point registers if the
    // following conditions are met:
    // 1. The size of the struct/class is no larger than 128-bit.
    // 2. The struct/class has one or two fields all of which are floating
    //    point types.
    // 3. The offset of the first field is zero (this follows what gcc does).
    //
    // Any other composite results are returned in integer registers.
    //
    if (FieldCnt && (FieldCnt <= 2) && !Layout.getFieldOffset(0)) {
      RecordDecl::field_iterator b = RD->field_begin(), e = RD->field_end();
      for (; b != e; ++b) {
        const BuiltinType *BT = b->getType()->getAs<BuiltinType>();

        if (!BT || !BT->isFloatingPoint())
          break;

        RTList.push_back(CGT.ConvertType(b->getType()));
      }

      if (b == e)
        return llvm::StructType::get(getVMContext(), RTList,
                                     RD->hasAttr<PackedAttr>());

      RTList.clear();
    }
  }

  CoerceToIntArgs(Size, RTList);
  return llvm::StructType::get(getVMContext(), RTList);
}

ABIArgInfo MipsABIInfo::classifyReturnType(QualType RetTy) const {
  uint64_t Size = getContext().getTypeSize(RetTy);

  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  // O32 doesn't treat zero-sized structs differently from other structs.
  // However, N32/N64 ignores zero sized return values.
  if (!IsO32 && Size == 0)
    return ABIArgInfo::getIgnore();

  if (isAggregateTypeForABI(RetTy) || RetTy->isVectorType()) {
    if (Size <= 128) {
      if (RetTy->isAnyComplexType())
        return ABIArgInfo::getDirect();

      // O32 returns integer vectors in registers and N32/N64 returns all small
      // aggregates in registers.
      if (!IsO32 ||
          (RetTy->isVectorType() && !RetTy->hasFloatingRepresentation())) {
        ABIArgInfo ArgInfo =
            ABIArgInfo::getDirect(returnAggregateInRegs(RetTy, Size));
        ArgInfo.setInReg(true);
        return ArgInfo;
      }
    }

    return getNaturalAlignIndirect(RetTy);
  }

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
    RetTy = EnumTy->getDecl()->getIntegerType();

  if (RetTy->isPromotableIntegerType())
    return ABIArgInfo::getExtend(RetTy);

  if ((RetTy->isUnsignedIntegerOrEnumerationType() ||
      RetTy->isSignedIntegerOrEnumerationType()) && Size == 32 && !IsO32)
    return ABIArgInfo::getSignExtend(RetTy);

  return ABIArgInfo::getDirect();
}

void MipsABIInfo::computeInfo(CGFunctionInfo &FI) const {
  ABIArgInfo &RetInfo = FI.getReturnInfo();
  if (!getCXXABI().classifyReturnType(FI))
    RetInfo = classifyReturnType(FI.getReturnType());

  // Check if a pointer to an aggregate is passed as a hidden argument.
  uint64_t Offset = RetInfo.isIndirect() ? MinABIStackAlignInBytes : 0;

  for (auto &I : FI.arguments())
    I.info = classifyArgumentType(I.type, Offset);
}

Address MipsABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                               QualType OrigTy) const {
  QualType Ty = OrigTy;

  // Integer arguments are promoted to 32-bit on O32 and 64-bit on N32/N64.
  // Pointers are also promoted in the same way but this only matters for N32.
  unsigned SlotSizeInBits = IsO32 ? 32 : 64;
  unsigned PtrWidth = getTarget().getPointerWidth(0);
  bool DidPromote = false;
  if ((Ty->isIntegerType() &&
          getContext().getIntWidth(Ty) < SlotSizeInBits) ||
      (Ty->isPointerType() && PtrWidth < SlotSizeInBits)) {
    DidPromote = true;
    Ty = getContext().getIntTypeForBitwidth(SlotSizeInBits,
                                            Ty->isSignedIntegerType());
  }

  auto TyInfo = getContext().getTypeInfoInChars(Ty);

  // The alignment of things in the argument area is never larger than
  // StackAlignInBytes.
  TyInfo.second =
    std::min(TyInfo.second, CharUnits::fromQuantity(StackAlignInBytes));

  // MinABIStackAlignInBytes is the size of argument slots on the stack.
  CharUnits ArgSlotSize = CharUnits::fromQuantity(MinABIStackAlignInBytes);

  Address Addr = emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*indirect*/ false,
                          TyInfo, ArgSlotSize, /*AllowHigherAlign*/ true);


  // If there was a promotion, "unpromote" into a temporary.
  // TODO: can we just use a pointer into a subset of the original slot?
  if (DidPromote) {
    Address Temp = CGF.CreateMemTemp(OrigTy, "vaarg.promotion-temp");
    llvm::Value *Promoted = CGF.Builder.CreateLoad(Addr);

    // Truncate down to the right width.
    llvm::Type *IntTy = (OrigTy->isIntegerType() ? Temp.getElementType()
                                                 : CGF.IntPtrTy);
    llvm::Value *V = CGF.Builder.CreateTrunc(Promoted, IntTy);
    if (OrigTy->isPointerType())
      V = CGF.Builder.CreateIntToPtr(V, Temp.getElementType());

    CGF.Builder.CreateStore(V, Temp);
    Addr = Temp;
  }

  return Addr;
}

ABIArgInfo MipsABIInfo::extendType(QualType Ty) const {
  int TySize = getContext().getTypeSize(Ty);

  // MIPS64 ABI requires unsigned 32 bit integers to be sign extended.
  if (Ty->isUnsignedIntegerOrEnumerationType() && TySize == 32)
    return ABIArgInfo::getSignExtend(Ty);

  return ABIArgInfo::getExtend(Ty);
}

bool
MIPSTargetCodeGenInfo::initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                                               llvm::Value *Address) const {
  // This information comes from gcc's implementation, which seems to
  // as canonical as it gets.

  // Everything on MIPS is 4 bytes.  Double-precision FP registers
  // are aliased to pairs of single-precision FP registers.
  llvm::Value *Four8 = llvm::ConstantInt::get(CGF.Int8Ty, 4);

  // 0-31 are the general purpose registers, $0 - $31.
  // 32-63 are the floating-point registers, $f0 - $f31.
  // 64 and 65 are the multiply/divide registers, $hi and $lo.
  // 66 is the (notional, I think) register for signal-handler return.
  AssignToArrayRange(CGF.Builder, Address, Four8, 0, 65);

  // 67-74 are the floating-point status registers, $fcc0 - $fcc7.
  // They are one bit wide and ignored here.

  // 80-111 are the coprocessor 0 registers, $c0r0 - $c0r31.
  // (coprocessor 1 is the FP unit)
  // 112-143 are the coprocessor 2 registers, $c2r0 - $c2r31.
  // 144-175 are the coprocessor 3 registers, $c3r0 - $c3r31.
  // 176-181 are the DSP accumulator registers.
  AssignToArrayRange(CGF.Builder, Address, Four8, 80, 181);
  return false;
}

//===----------------------------------------------------------------------===//
// AVR ABI Implementation.
//===----------------------------------------------------------------------===//

namespace {
class AVRTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  AVRTargetCodeGenInfo(CodeGenTypes &CGT)
    : TargetCodeGenInfo(new DefaultABIInfo(CGT)) { }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    if (GV->isDeclaration())
      return;
    const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD) return;
    auto *Fn = cast<llvm::Function>(GV);

    if (FD->getAttr<AVRInterruptAttr>())
      Fn->addFnAttr("interrupt");

    if (FD->getAttr<AVRSignalAttr>())
      Fn->addFnAttr("signal");
  }
};
}

//===----------------------------------------------------------------------===//
// TCE ABI Implementation (see http://tce.cs.tut.fi). Uses mostly the defaults.
// Currently subclassed only to implement custom OpenCL C function attribute
// handling.
//===----------------------------------------------------------------------===//

namespace {

class TCETargetCodeGenInfo : public DefaultTargetCodeGenInfo {
public:
  TCETargetCodeGenInfo(CodeGenTypes &CGT)
    : DefaultTargetCodeGenInfo(CGT) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override;
};

void TCETargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &M) const {
  if (GV->isDeclaration())
    return;
  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (!FD) return;

  llvm::Function *F = cast<llvm::Function>(GV);

  if (M.getLangOpts().OpenCL) {
    if (FD->hasAttr<OpenCLKernelAttr>()) {
      // OpenCL C Kernel functions are not subject to inlining
      F->addFnAttr(llvm::Attribute::NoInline);
      const ReqdWorkGroupSizeAttr *Attr = FD->getAttr<ReqdWorkGroupSizeAttr>();
      if (Attr) {
        // Convert the reqd_work_group_size() attributes to metadata.
        llvm::LLVMContext &Context = F->getContext();
        llvm::NamedMDNode *OpenCLMetadata =
            M.getModule().getOrInsertNamedMetadata(
                "opencl.kernel_wg_size_info");

        SmallVector<llvm::Metadata *, 5> Operands;
        Operands.push_back(llvm::ConstantAsMetadata::get(F));

        Operands.push_back(
            llvm::ConstantAsMetadata::get(llvm::Constant::getIntegerValue(
                M.Int32Ty, llvm::APInt(32, Attr->getXDim()))));
        Operands.push_back(
            llvm::ConstantAsMetadata::get(llvm::Constant::getIntegerValue(
                M.Int32Ty, llvm::APInt(32, Attr->getYDim()))));
        Operands.push_back(
            llvm::ConstantAsMetadata::get(llvm::Constant::getIntegerValue(
                M.Int32Ty, llvm::APInt(32, Attr->getZDim()))));

        // Add a boolean constant operand for "required" (true) or "hint"
        // (false) for implementing the work_group_size_hint attr later.
        // Currently always true as the hint is not yet implemented.
        Operands.push_back(
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::getTrue(Context)));
        OpenCLMetadata->addOperand(llvm::MDNode::get(Context, Operands));
      }
    }
  }
}

}

//===----------------------------------------------------------------------===//
// Hexagon ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class HexagonABIInfo : public ABIInfo {


public:
  HexagonABIInfo(CodeGenTypes &CGT) : ABIInfo(CGT) {}

private:

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType RetTy) const;

  void computeInfo(CGFunctionInfo &FI) const override;

  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;
};

class HexagonTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  HexagonTargetCodeGenInfo(CodeGenTypes &CGT)
    :TargetCodeGenInfo(new HexagonABIInfo(CGT)) {}

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    return 29;
  }
};

}

void HexagonABIInfo::computeInfo(CGFunctionInfo &FI) const {
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  for (auto &I : FI.arguments())
    I.info = classifyArgumentType(I.type);
}

ABIArgInfo HexagonABIInfo::classifyArgumentType(QualType Ty) const {
  if (!isAggregateTypeForABI(Ty)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    return (Ty->isPromotableIntegerType() ? ABIArgInfo::getExtend(Ty)
                                          : ABIArgInfo::getDirect());
  }

  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
    return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

  // Ignore empty records.
  if (isEmptyRecord(getContext(), Ty, true))
    return ABIArgInfo::getIgnore();

  uint64_t Size = getContext().getTypeSize(Ty);
  if (Size > 64)
    return getNaturalAlignIndirect(Ty, /*ByVal=*/true);
    // Pass in the smallest viable integer type.
  else if (Size > 32)
      return ABIArgInfo::getDirect(llvm::Type::getInt64Ty(getVMContext()));
  else if (Size > 16)
      return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));
  else if (Size > 8)
      return ABIArgInfo::getDirect(llvm::Type::getInt16Ty(getVMContext()));
  else
      return ABIArgInfo::getDirect(llvm::Type::getInt8Ty(getVMContext()));
}

ABIArgInfo HexagonABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  // Large vector types should be returned via memory.
  if (RetTy->isVectorType() && getContext().getTypeSize(RetTy) > 64)
    return getNaturalAlignIndirect(RetTy);

  if (!isAggregateTypeForABI(RetTy)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
      RetTy = EnumTy->getDecl()->getIntegerType();

    return (RetTy->isPromotableIntegerType() ? ABIArgInfo::getExtend(RetTy)
                                             : ABIArgInfo::getDirect());
  }

  if (isEmptyRecord(getContext(), RetTy, true))
    return ABIArgInfo::getIgnore();

  // Aggregates <= 8 bytes are returned in r0; other aggregates
  // are returned indirectly.
  uint64_t Size = getContext().getTypeSize(RetTy);
  if (Size <= 64) {
    // Return in the smallest viable integer type.
    if (Size <= 8)
      return ABIArgInfo::getDirect(llvm::Type::getInt8Ty(getVMContext()));
    if (Size <= 16)
      return ABIArgInfo::getDirect(llvm::Type::getInt16Ty(getVMContext()));
    if (Size <= 32)
      return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));
    return ABIArgInfo::getDirect(llvm::Type::getInt64Ty(getVMContext()));
  }

  return getNaturalAlignIndirect(RetTy, /*ByVal=*/true);
}

Address HexagonABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                  QualType Ty) const {
  // FIXME: Someone needs to audit that this handle alignment correctly.
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*indirect*/ false,
                          getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(4),
                          /*AllowHigherAlign*/ true);
}

//===----------------------------------------------------------------------===//
// Lanai ABI Implementation
//===----------------------------------------------------------------------===//

namespace {
class LanaiABIInfo : public DefaultABIInfo {
public:
  LanaiABIInfo(CodeGen::CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  bool shouldUseInReg(QualType Ty, CCState &State) const;

  void computeInfo(CGFunctionInfo &FI) const override {
    CCState State(FI.getCallingConvention());
    // Lanai uses 4 registers to pass arguments unless the function has the
    // regparm attribute set.
    if (FI.getHasRegParm()) {
      State.FreeRegs = FI.getRegParm();
    } else {
      State.FreeRegs = 4;
    }

    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type, State);
  }

  ABIArgInfo getIndirectResult(QualType Ty, bool ByVal, CCState &State) const;
  ABIArgInfo classifyArgumentType(QualType RetTy, CCState &State) const;
};
} // end anonymous namespace

bool LanaiABIInfo::shouldUseInReg(QualType Ty, CCState &State) const {
  unsigned Size = getContext().getTypeSize(Ty);
  unsigned SizeInRegs = llvm::alignTo(Size, 32U) / 32U;

  if (SizeInRegs == 0)
    return false;

  if (SizeInRegs > State.FreeRegs) {
    State.FreeRegs = 0;
    return false;
  }

  State.FreeRegs -= SizeInRegs;

  return true;
}

ABIArgInfo LanaiABIInfo::getIndirectResult(QualType Ty, bool ByVal,
                                           CCState &State) const {
  if (!ByVal) {
    if (State.FreeRegs) {
      --State.FreeRegs; // Non-byval indirects just use one pointer.
      return getNaturalAlignIndirectInReg(Ty);
    }
    return getNaturalAlignIndirect(Ty, false);
  }

  // Compute the byval alignment.
  const unsigned MinABIStackAlignInBytes = 4;
  unsigned TypeAlign = getContext().getTypeAlign(Ty) / 8;
  return ABIArgInfo::getIndirect(CharUnits::fromQuantity(4), /*ByVal=*/true,
                                 /*Realign=*/TypeAlign >
                                     MinABIStackAlignInBytes);
}

ABIArgInfo LanaiABIInfo::classifyArgumentType(QualType Ty,
                                              CCState &State) const {
  // Check with the C++ ABI first.
  const RecordType *RT = Ty->getAs<RecordType>();
  if (RT) {
    CGCXXABI::RecordArgABI RAA = getRecordArgABI(RT, getCXXABI());
    if (RAA == CGCXXABI::RAA_Indirect) {
      return getIndirectResult(Ty, /*ByVal=*/false, State);
    } else if (RAA == CGCXXABI::RAA_DirectInMemory) {
      return getNaturalAlignIndirect(Ty, /*ByRef=*/true);
    }
  }

  if (isAggregateTypeForABI(Ty)) {
    // Structures with flexible arrays are always indirect.
    if (RT && RT->getDecl()->hasFlexibleArrayMember())
      return getIndirectResult(Ty, /*ByVal=*/true, State);

    // Ignore empty structs/unions.
    if (isEmptyRecord(getContext(), Ty, true))
      return ABIArgInfo::getIgnore();

    llvm::LLVMContext &LLVMContext = getVMContext();
    unsigned SizeInRegs = (getContext().getTypeSize(Ty) + 31) / 32;
    if (SizeInRegs <= State.FreeRegs) {
      llvm::IntegerType *Int32 = llvm::Type::getInt32Ty(LLVMContext);
      SmallVector<llvm::Type *, 3> Elements(SizeInRegs, Int32);
      llvm::Type *Result = llvm::StructType::get(LLVMContext, Elements);
      State.FreeRegs -= SizeInRegs;
      return ABIArgInfo::getDirectInReg(Result);
    } else {
      State.FreeRegs = 0;
    }
    return getIndirectResult(Ty, true, State);
  }

  // Treat an enum type as its underlying type.
  if (const auto *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  bool InReg = shouldUseInReg(Ty, State);
  if (Ty->isPromotableIntegerType()) {
    if (InReg)
      return ABIArgInfo::getDirectInReg();
    return ABIArgInfo::getExtend(Ty);
  }
  if (InReg)
    return ABIArgInfo::getDirectInReg();
  return ABIArgInfo::getDirect();
}

namespace {
class LanaiTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  LanaiTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT)
      : TargetCodeGenInfo(new LanaiABIInfo(CGT)) {}
};
}

//===----------------------------------------------------------------------===//
// AMDGPU ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class AMDGPUABIInfo final : public DefaultABIInfo {
private:
  static const unsigned MaxNumRegsForArgsRet = 16;

  unsigned numRegsForType(QualType Ty) const;

  bool isHomogeneousAggregateBaseType(QualType Ty) const override;
  bool isHomogeneousAggregateSmallEnough(const Type *Base,
                                         uint64_t Members) const override;

public:
  explicit AMDGPUABIInfo(CodeGen::CodeGenTypes &CGT) :
    DefaultABIInfo(CGT) {}

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyKernelArgumentType(QualType Ty) const;
  ABIArgInfo classifyArgumentType(QualType Ty, unsigned &NumRegsLeft) const;

  void computeInfo(CGFunctionInfo &FI) const override;
};

bool AMDGPUABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  return true;
}

bool AMDGPUABIInfo::isHomogeneousAggregateSmallEnough(
  const Type *Base, uint64_t Members) const {
  uint32_t NumRegs = (getContext().getTypeSize(Base) + 31) / 32;

  // Homogeneous Aggregates may occupy at most 16 registers.
  return Members * NumRegs <= MaxNumRegsForArgsRet;
}

/// Estimate number of registers the type will use when passed in registers.
unsigned AMDGPUABIInfo::numRegsForType(QualType Ty) const {
  unsigned NumRegs = 0;

  if (const VectorType *VT = Ty->getAs<VectorType>()) {
    // Compute from the number of elements. The reported size is based on the
    // in-memory size, which includes the padding 4th element for 3-vectors.
    QualType EltTy = VT->getElementType();
    unsigned EltSize = getContext().getTypeSize(EltTy);

    // 16-bit element vectors should be passed as packed.
    if (EltSize == 16)
      return (VT->getNumElements() + 1) / 2;

    unsigned EltNumRegs = (EltSize + 31) / 32;
    return EltNumRegs * VT->getNumElements();
  }

  if (const RecordType *RT = Ty->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    assert(!RD->hasFlexibleArrayMember());

    for (const FieldDecl *Field : RD->fields()) {
      QualType FieldTy = Field->getType();
      NumRegs += numRegsForType(FieldTy);
    }

    return NumRegs;
  }

  return (getContext().getTypeSize(Ty) + 31) / 32;
}

void AMDGPUABIInfo::computeInfo(CGFunctionInfo &FI) const {
  llvm::CallingConv::ID CC = FI.getCallingConvention();

  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

  unsigned NumRegsLeft = MaxNumRegsForArgsRet;
  for (auto &Arg : FI.arguments()) {
    if (CC == llvm::CallingConv::AMDGPU_KERNEL) {
      Arg.info = classifyKernelArgumentType(Arg.type);
    } else {
      Arg.info = classifyArgumentType(Arg.type, NumRegsLeft);
    }
  }
}

ABIArgInfo AMDGPUABIInfo::classifyReturnType(QualType RetTy) const {
  if (isAggregateTypeForABI(RetTy)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // returned by value.
    if (!getRecordArgABI(RetTy, getCXXABI())) {
      // Ignore empty structs/unions.
      if (isEmptyRecord(getContext(), RetTy, true))
        return ABIArgInfo::getIgnore();

      // Lower single-element structs to just return a regular value.
      if (const Type *SeltTy = isSingleElementStruct(RetTy, getContext()))
        return ABIArgInfo::getDirect(CGT.ConvertType(QualType(SeltTy, 0)));

      if (const RecordType *RT = RetTy->getAs<RecordType>()) {
        const RecordDecl *RD = RT->getDecl();
        if (RD->hasFlexibleArrayMember())
          return DefaultABIInfo::classifyReturnType(RetTy);
      }

      // Pack aggregates <= 4 bytes into single VGPR or pair.
      uint64_t Size = getContext().getTypeSize(RetTy);
      if (Size <= 16)
        return ABIArgInfo::getDirect(llvm::Type::getInt16Ty(getVMContext()));

      if (Size <= 32)
        return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));

      if (Size <= 64) {
        llvm::Type *I32Ty = llvm::Type::getInt32Ty(getVMContext());
        return ABIArgInfo::getDirect(llvm::ArrayType::get(I32Ty, 2));
      }

      if (numRegsForType(RetTy) <= MaxNumRegsForArgsRet)
        return ABIArgInfo::getDirect();
    }
  }

  // Otherwise just do the default thing.
  return DefaultABIInfo::classifyReturnType(RetTy);
}

/// For kernels all parameters are really passed in a special buffer. It doesn't
/// make sense to pass anything byval, so everything must be direct.
ABIArgInfo AMDGPUABIInfo::classifyKernelArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  // TODO: Can we omit empty structs?

  // Coerce single element structs to its element.
  if (const Type *SeltTy = isSingleElementStruct(Ty, getContext()))
    return ABIArgInfo::getDirect(CGT.ConvertType(QualType(SeltTy, 0)));

  // If we set CanBeFlattened to true, CodeGen will expand the struct to its
  // individual elements, which confuses the Clover OpenCL backend; therefore we
  // have to set it to false here. Other args of getDirect() are just defaults.
  return ABIArgInfo::getDirect(nullptr, 0, nullptr, false);
}

ABIArgInfo AMDGPUABIInfo::classifyArgumentType(QualType Ty,
                                               unsigned &NumRegsLeft) const {
  assert(NumRegsLeft <= MaxNumRegsForArgsRet && "register estimate underflow");

  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (isAggregateTypeForABI(Ty)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // passed by value.
    if (auto RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

    // Ignore empty structs/unions.
    if (isEmptyRecord(getContext(), Ty, true))
      return ABIArgInfo::getIgnore();

    // Lower single-element structs to just pass a regular value. TODO: We
    // could do reasonable-size multiple-element structs too, using getExpand(),
    // though watch out for things like bitfields.
    if (const Type *SeltTy = isSingleElementStruct(Ty, getContext()))
      return ABIArgInfo::getDirect(CGT.ConvertType(QualType(SeltTy, 0)));

    if (const RecordType *RT = Ty->getAs<RecordType>()) {
      const RecordDecl *RD = RT->getDecl();
      if (RD->hasFlexibleArrayMember())
        return DefaultABIInfo::classifyArgumentType(Ty);
    }

    // Pack aggregates <= 8 bytes into single VGPR or pair.
    uint64_t Size = getContext().getTypeSize(Ty);
    if (Size <= 64) {
      unsigned NumRegs = (Size + 31) / 32;
      NumRegsLeft -= std::min(NumRegsLeft, NumRegs);

      if (Size <= 16)
        return ABIArgInfo::getDirect(llvm::Type::getInt16Ty(getVMContext()));

      if (Size <= 32)
        return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));

      // XXX: Should this be i64 instead, and should the limit increase?
      llvm::Type *I32Ty = llvm::Type::getInt32Ty(getVMContext());
      return ABIArgInfo::getDirect(llvm::ArrayType::get(I32Ty, 2));
    }

    if (NumRegsLeft > 0) {
      unsigned NumRegs = numRegsForType(Ty);
      if (NumRegsLeft >= NumRegs) {
        NumRegsLeft -= NumRegs;
        return ABIArgInfo::getDirect();
      }
    }
  }

  // Otherwise just do the default thing.
  ABIArgInfo ArgInfo = DefaultABIInfo::classifyArgumentType(Ty);
  if (!ArgInfo.isIndirect()) {
    unsigned NumRegs = numRegsForType(Ty);
    NumRegsLeft -= std::min(NumRegs, NumRegsLeft);
  }

  return ArgInfo;
}

class AMDGPUTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  AMDGPUTargetCodeGenInfo(CodeGenTypes &CGT)
    : TargetCodeGenInfo(new AMDGPUABIInfo(CGT)) {}
  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override;
  unsigned getOpenCLKernelCallingConv() const override;

  llvm::Constant *getNullPointer(const CodeGen::CodeGenModule &CGM,
      llvm::PointerType *T, QualType QT) const override;

  LangAS getASTAllocaAddressSpace() const override {
    return getLangASFromTargetAS(
        getABIInfo().getDataLayout().getAllocaAddrSpace());
  }
  LangAS getGlobalVarAddressSpace(CodeGenModule &CGM,
                                  const VarDecl *D) const override;
  llvm::SyncScope::ID getLLVMSyncScopeID(SyncScope S,
                                         llvm::LLVMContext &C) const override;
  llvm::Function *
  createEnqueuedBlockKernel(CodeGenFunction &CGF,
                            llvm::Function *BlockInvokeFunc,
                            llvm::Value *BlockLiteral) const override;
  bool shouldEmitStaticExternCAliases() const override;
  void setCUDAKernelCallingConvention(const FunctionType *&FT) const override;
};
}

void AMDGPUTargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &M) const {
  if (GV->isDeclaration())
    return;
  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (!FD)
    return;

  llvm::Function *F = cast<llvm::Function>(GV);

  const auto *ReqdWGS = M.getLangOpts().OpenCL ?
    FD->getAttr<ReqdWorkGroupSizeAttr>() : nullptr;

  if (M.getLangOpts().OpenCL && FD->hasAttr<OpenCLKernelAttr>() &&
      (M.getTriple().getOS() == llvm::Triple::AMDHSA))
    F->addFnAttr("amdgpu-implicitarg-num-bytes", "48");

  const auto *FlatWGS = FD->getAttr<AMDGPUFlatWorkGroupSizeAttr>();
  if (ReqdWGS || FlatWGS) {
    unsigned Min = FlatWGS ? FlatWGS->getMin() : 0;
    unsigned Max = FlatWGS ? FlatWGS->getMax() : 0;
    if (ReqdWGS && Min == 0 && Max == 0)
      Min = Max = ReqdWGS->getXDim() * ReqdWGS->getYDim() * ReqdWGS->getZDim();

    if (Min != 0) {
      assert(Min <= Max && "Min must be less than or equal Max");

      std::string AttrVal = llvm::utostr(Min) + "," + llvm::utostr(Max);
      F->addFnAttr("amdgpu-flat-work-group-size", AttrVal);
    } else
      assert(Max == 0 && "Max must be zero");
  }

  if (const auto *Attr = FD->getAttr<AMDGPUWavesPerEUAttr>()) {
    unsigned Min = Attr->getMin();
    unsigned Max = Attr->getMax();

    if (Min != 0) {
      assert((Max == 0 || Min <= Max) && "Min must be less than or equal Max");

      std::string AttrVal = llvm::utostr(Min);
      if (Max != 0)
        AttrVal = AttrVal + "," + llvm::utostr(Max);
      F->addFnAttr("amdgpu-waves-per-eu", AttrVal);
    } else
      assert(Max == 0 && "Max must be zero");
  }

  if (const auto *Attr = FD->getAttr<AMDGPUNumSGPRAttr>()) {
    unsigned NumSGPR = Attr->getNumSGPR();

    if (NumSGPR != 0)
      F->addFnAttr("amdgpu-num-sgpr", llvm::utostr(NumSGPR));
  }

  if (const auto *Attr = FD->getAttr<AMDGPUNumVGPRAttr>()) {
    uint32_t NumVGPR = Attr->getNumVGPR();

    if (NumVGPR != 0)
      F->addFnAttr("amdgpu-num-vgpr", llvm::utostr(NumVGPR));
  }
}

unsigned AMDGPUTargetCodeGenInfo::getOpenCLKernelCallingConv() const {
  return llvm::CallingConv::AMDGPU_KERNEL;
}

// Currently LLVM assumes null pointers always have value 0,
// which results in incorrectly transformed IR. Therefore, instead of
// emitting null pointers in private and local address spaces, a null
// pointer in generic address space is emitted which is casted to a
// pointer in local or private address space.
llvm::Constant *AMDGPUTargetCodeGenInfo::getNullPointer(
    const CodeGen::CodeGenModule &CGM, llvm::PointerType *PT,
    QualType QT) const {
  if (CGM.getContext().getTargetNullPointerValue(QT) == 0)
    return llvm::ConstantPointerNull::get(PT);

  auto &Ctx = CGM.getContext();
  auto NPT = llvm::PointerType::get(PT->getElementType(),
      Ctx.getTargetAddressSpace(LangAS::opencl_generic));
  return llvm::ConstantExpr::getAddrSpaceCast(
      llvm::ConstantPointerNull::get(NPT), PT);
}

LangAS
AMDGPUTargetCodeGenInfo::getGlobalVarAddressSpace(CodeGenModule &CGM,
                                                  const VarDecl *D) const {
  assert(!CGM.getLangOpts().OpenCL &&
         !(CGM.getLangOpts().CUDA && CGM.getLangOpts().CUDAIsDevice) &&
         "Address space agnostic languages only");
  LangAS DefaultGlobalAS = getLangASFromTargetAS(
      CGM.getContext().getTargetAddressSpace(LangAS::opencl_global));
  if (!D)
    return DefaultGlobalAS;

  LangAS AddrSpace = D->getType().getAddressSpace();
  assert(AddrSpace == LangAS::Default || isTargetAddressSpace(AddrSpace));
  if (AddrSpace != LangAS::Default)
    return AddrSpace;

  if (CGM.isTypeConstant(D->getType(), false)) {
    if (auto ConstAS = CGM.getTarget().getConstantAddressSpace())
      return ConstAS.getValue();
  }
  return DefaultGlobalAS;
}

llvm::SyncScope::ID
AMDGPUTargetCodeGenInfo::getLLVMSyncScopeID(SyncScope S,
                                            llvm::LLVMContext &C) const {
  StringRef Name;
  switch (S) {
  case SyncScope::OpenCLWorkGroup:
    Name = "workgroup";
    break;
  case SyncScope::OpenCLDevice:
    Name = "agent";
    break;
  case SyncScope::OpenCLAllSVMDevices:
    Name = "";
    break;
  case SyncScope::OpenCLSubGroup:
    Name = "subgroup";
  }
  return C.getOrInsertSyncScopeID(Name);
}

bool AMDGPUTargetCodeGenInfo::shouldEmitStaticExternCAliases() const {
  return false;
}

void AMDGPUTargetCodeGenInfo::setCUDAKernelCallingConvention(
    const FunctionType *&FT) const {
  FT = getABIInfo().getContext().adjustFunctionType(
      FT, FT->getExtInfo().withCallingConv(CC_OpenCLKernel));
}

//===----------------------------------------------------------------------===//
// SPARC v8 ABI Implementation.
// Based on the SPARC Compliance Definition version 2.4.1.
//
// Ensures that complex values are passed in registers.
//
namespace {
class SparcV8ABIInfo : public DefaultABIInfo {
public:
  SparcV8ABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

private:
  ABIArgInfo classifyReturnType(QualType RetTy) const;
  void computeInfo(CGFunctionInfo &FI) const override;
};
} // end anonymous namespace


ABIArgInfo
SparcV8ABIInfo::classifyReturnType(QualType Ty) const {
  if (Ty->isAnyComplexType()) {
    return ABIArgInfo::getDirect();
  }
  else {
    return DefaultABIInfo::classifyReturnType(Ty);
  }
}

void SparcV8ABIInfo::computeInfo(CGFunctionInfo &FI) const {

  FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  for (auto &Arg : FI.arguments())
    Arg.info = classifyArgumentType(Arg.type);
}

namespace {
class SparcV8TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  SparcV8TargetCodeGenInfo(CodeGenTypes &CGT)
    : TargetCodeGenInfo(new SparcV8ABIInfo(CGT)) {}
};
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// SPARC v9 ABI Implementation.
// Based on the SPARC Compliance Definition version 2.4.1.
//
// Function arguments a mapped to a nominal "parameter array" and promoted to
// registers depending on their type. Each argument occupies 8 or 16 bytes in
// the array, structs larger than 16 bytes are passed indirectly.
//
// One case requires special care:
//
//   struct mixed {
//     int i;
//     float f;
//   };
//
// When a struct mixed is passed by value, it only occupies 8 bytes in the
// parameter array, but the int is passed in an integer register, and the float
// is passed in a floating point register. This is represented as two arguments
// with the LLVM IR inreg attribute:
//
//   declare void f(i32 inreg %i, float inreg %f)
//
// The code generator will only allocate 4 bytes from the parameter array for
// the inreg arguments. All other arguments are allocated a multiple of 8
// bytes.
//
namespace {
class SparcV9ABIInfo : public ABIInfo {
public:
  SparcV9ABIInfo(CodeGenTypes &CGT) : ABIInfo(CGT) {}

private:
  ABIArgInfo classifyType(QualType RetTy, unsigned SizeLimit) const;
  void computeInfo(CGFunctionInfo &FI) const override;
  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;

  // Coercion type builder for structs passed in registers. The coercion type
  // serves two purposes:
  //
  // 1. Pad structs to a multiple of 64 bits, so they are passed 'left-aligned'
  //    in registers.
  // 2. Expose aligned floating point elements as first-level elements, so the
  //    code generator knows to pass them in floating point registers.
  //
  // We also compute the InReg flag which indicates that the struct contains
  // aligned 32-bit floats.
  //
  struct CoerceBuilder {
    llvm::LLVMContext &Context;
    const llvm::DataLayout &DL;
    SmallVector<llvm::Type*, 8> Elems;
    uint64_t Size;
    bool InReg;

    CoerceBuilder(llvm::LLVMContext &c, const llvm::DataLayout &dl)
      : Context(c), DL(dl), Size(0), InReg(false) {}

    // Pad Elems with integers until Size is ToSize.
    void pad(uint64_t ToSize) {
      assert(ToSize >= Size && "Cannot remove elements");
      if (ToSize == Size)
        return;

      // Finish the current 64-bit word.
      uint64_t Aligned = llvm::alignTo(Size, 64);
      if (Aligned > Size && Aligned <= ToSize) {
        Elems.push_back(llvm::IntegerType::get(Context, Aligned - Size));
        Size = Aligned;
      }

      // Add whole 64-bit words.
      while (Size + 64 <= ToSize) {
        Elems.push_back(llvm::Type::getInt64Ty(Context));
        Size += 64;
      }

      // Final in-word padding.
      if (Size < ToSize) {
        Elems.push_back(llvm::IntegerType::get(Context, ToSize - Size));
        Size = ToSize;
      }
    }

    // Add a floating point element at Offset.
    void addFloat(uint64_t Offset, llvm::Type *Ty, unsigned Bits) {
      // Unaligned floats are treated as integers.
      if (Offset % Bits)
        return;
      // The InReg flag is only required if there are any floats < 64 bits.
      if (Bits < 64)
        InReg = true;
      pad(Offset);
      Elems.push_back(Ty);
      Size = Offset + Bits;
    }

    // Add a struct type to the coercion type, starting at Offset (in bits).
    void addStruct(uint64_t Offset, llvm::StructType *StrTy) {
      const llvm::StructLayout *Layout = DL.getStructLayout(StrTy);
      for (unsigned i = 0, e = StrTy->getNumElements(); i != e; ++i) {
        llvm::Type *ElemTy = StrTy->getElementType(i);
        uint64_t ElemOffset = Offset + Layout->getElementOffsetInBits(i);
        switch (ElemTy->getTypeID()) {
        case llvm::Type::StructTyID:
          addStruct(ElemOffset, cast<llvm::StructType>(ElemTy));
          break;
        case llvm::Type::FloatTyID:
          addFloat(ElemOffset, ElemTy, 32);
          break;
        case llvm::Type::DoubleTyID:
          addFloat(ElemOffset, ElemTy, 64);
          break;
        case llvm::Type::FP128TyID:
          addFloat(ElemOffset, ElemTy, 128);
          break;
        case llvm::Type::PointerTyID:
          if (ElemOffset % 64 == 0) {
            pad(ElemOffset);
            Elems.push_back(ElemTy);
            Size += 64;
          }
          break;
        default:
          break;
        }
      }
    }

    // Check if Ty is a usable substitute for the coercion type.
    bool isUsableType(llvm::StructType *Ty) const {
      return llvm::makeArrayRef(Elems) == Ty->elements();
    }

    // Get the coercion type as a literal struct type.
    llvm::Type *getType() const {
      if (Elems.size() == 1)
        return Elems.front();
      else
        return llvm::StructType::get(Context, Elems);
    }
  };
};
} // end anonymous namespace

ABIArgInfo
SparcV9ABIInfo::classifyType(QualType Ty, unsigned SizeLimit) const {
  if (Ty->isVoidType())
    return ABIArgInfo::getIgnore();

  uint64_t Size = getContext().getTypeSize(Ty);

  // Anything too big to fit in registers is passed with an explicit indirect
  // pointer / sret pointer.
  if (Size > SizeLimit)
    return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  // Integer types smaller than a register are extended.
  if (Size < 64 && Ty->isIntegerType())
    return ABIArgInfo::getExtend(Ty);

  // Other non-aggregates go in registers.
  if (!isAggregateTypeForABI(Ty))
    return ABIArgInfo::getDirect();

  // If a C++ object has either a non-trivial copy constructor or a non-trivial
  // destructor, it is passed with an explicit indirect pointer / sret pointer.
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
    return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

  // This is a small aggregate type that should be passed in registers.
  // Build a coercion type from the LLVM struct type.
  llvm::StructType *StrTy = dyn_cast<llvm::StructType>(CGT.ConvertType(Ty));
  if (!StrTy)
    return ABIArgInfo::getDirect();

  CoerceBuilder CB(getVMContext(), getDataLayout());
  CB.addStruct(0, StrTy);
  CB.pad(llvm::alignTo(CB.DL.getTypeSizeInBits(StrTy), 64));

  // Try to use the original type for coercion.
  llvm::Type *CoerceTy = CB.isUsableType(StrTy) ? StrTy : CB.getType();

  if (CB.InReg)
    return ABIArgInfo::getDirectInReg(CoerceTy);
  else
    return ABIArgInfo::getDirect(CoerceTy);
}

Address SparcV9ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                  QualType Ty) const {
  ABIArgInfo AI = classifyType(Ty, 16 * 8);
  llvm::Type *ArgTy = CGT.ConvertType(Ty);
  if (AI.canHaveCoerceToType() && !AI.getCoerceToType())
    AI.setCoerceToType(ArgTy);

  CharUnits SlotSize = CharUnits::fromQuantity(8);

  CGBuilderTy &Builder = CGF.Builder;
  Address Addr(Builder.CreateLoad(VAListAddr, "ap.cur"), SlotSize);
  llvm::Type *ArgPtrTy = llvm::PointerType::getUnqual(ArgTy);

  auto TypeInfo = getContext().getTypeInfoInChars(Ty);

  Address ArgAddr = Address::invalid();
  CharUnits Stride;
  switch (AI.getKind()) {
  case ABIArgInfo::Expand:
  case ABIArgInfo::CoerceAndExpand:
  case ABIArgInfo::InAlloca:
    llvm_unreachable("Unsupported ABI kind for va_arg");

  case ABIArgInfo::Extend: {
    Stride = SlotSize;
    CharUnits Offset = SlotSize - TypeInfo.first;
    ArgAddr = Builder.CreateConstInBoundsByteGEP(Addr, Offset, "extend");
    break;
  }

  case ABIArgInfo::Direct: {
    auto AllocSize = getDataLayout().getTypeAllocSize(AI.getCoerceToType());
    Stride = CharUnits::fromQuantity(AllocSize).alignTo(SlotSize);
    ArgAddr = Addr;
    break;
  }

  case ABIArgInfo::Indirect:
    Stride = SlotSize;
    ArgAddr = Builder.CreateElementBitCast(Addr, ArgPtrTy, "indirect");
    ArgAddr = Address(Builder.CreateLoad(ArgAddr, "indirect.arg"),
                      TypeInfo.second);
    break;

  case ABIArgInfo::Ignore:
    return Address(llvm::UndefValue::get(ArgPtrTy), TypeInfo.second);
  }

  // Update VAList.
  llvm::Value *NextPtr =
    Builder.CreateConstInBoundsByteGEP(Addr.getPointer(), Stride, "ap.next");
  Builder.CreateStore(NextPtr, VAListAddr);

  return Builder.CreateBitCast(ArgAddr, ArgPtrTy, "arg.addr");
}

void SparcV9ABIInfo::computeInfo(CGFunctionInfo &FI) const {
  FI.getReturnInfo() = classifyType(FI.getReturnType(), 32 * 8);
  for (auto &I : FI.arguments())
    I.info = classifyType(I.type, 16 * 8);
}

namespace {
class SparcV9TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  SparcV9TargetCodeGenInfo(CodeGenTypes &CGT)
    : TargetCodeGenInfo(new SparcV9ABIInfo(CGT)) {}

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    return 14;
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override;
};
} // end anonymous namespace

bool
SparcV9TargetCodeGenInfo::initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                                                llvm::Value *Address) const {
  // This is calculated from the LLVM and GCC tables and verified
  // against gcc output.  AFAIK all ABIs use the same encoding.

  CodeGen::CGBuilderTy &Builder = CGF.Builder;

  llvm::IntegerType *i8 = CGF.Int8Ty;
  llvm::Value *Four8 = llvm::ConstantInt::get(i8, 4);
  llvm::Value *Eight8 = llvm::ConstantInt::get(i8, 8);

  // 0-31: the 8-byte general-purpose registers
  AssignToArrayRange(Builder, Address, Eight8, 0, 31);

  // 32-63: f0-31, the 4-byte floating-point registers
  AssignToArrayRange(Builder, Address, Four8, 32, 63);

  //   Y   = 64
  //   PSR = 65
  //   WIM = 66
  //   TBR = 67
  //   PC  = 68
  //   NPC = 69
  //   FSR = 70
  //   CSR = 71
  AssignToArrayRange(Builder, Address, Eight8, 64, 71);

  // 72-87: d0-15, the 8-byte floating-point registers
  AssignToArrayRange(Builder, Address, Eight8, 72, 87);

  return false;
}

// ARC ABI implementation.
namespace {

class ARCABIInfo : public DefaultABIInfo {
public:
  using DefaultABIInfo::DefaultABIInfo;

private:
  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;

  void updateState(const ABIArgInfo &Info, QualType Ty, CCState &State) const {
    if (!State.FreeRegs)
      return;
    if (Info.isIndirect() && Info.getInReg())
      State.FreeRegs--;
    else if (Info.isDirect() && Info.getInReg()) {
      unsigned sz = (getContext().getTypeSize(Ty) + 31) / 32;
      if (sz < State.FreeRegs)
        State.FreeRegs -= sz;
      else
        State.FreeRegs = 0;
    }
  }

  void computeInfo(CGFunctionInfo &FI) const override {
    CCState State(FI.getCallingConvention());
    // ARC uses 8 registers to pass arguments.
    State.FreeRegs = 8;

    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    updateState(FI.getReturnInfo(), FI.getReturnType(), State);
    for (auto &I : FI.arguments()) {
      I.info = classifyArgumentType(I.type, State.FreeRegs);
      updateState(I.info, I.type, State);
    }
  }

  ABIArgInfo getIndirectByRef(QualType Ty, bool HasFreeRegs) const;
  ABIArgInfo getIndirectByValue(QualType Ty) const;
  ABIArgInfo classifyArgumentType(QualType Ty, uint8_t FreeRegs) const;
  ABIArgInfo classifyReturnType(QualType RetTy) const;
};

class ARCTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  ARCTargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(new ARCABIInfo(CGT)) {}
};


ABIArgInfo ARCABIInfo::getIndirectByRef(QualType Ty, bool HasFreeRegs) const {
  return HasFreeRegs ? getNaturalAlignIndirectInReg(Ty) :
                       getNaturalAlignIndirect(Ty, false);
}

ABIArgInfo ARCABIInfo::getIndirectByValue(QualType Ty) const {
  // Compute the byval alignment.
  const unsigned MinABIStackAlignInBytes = 4;
  unsigned TypeAlign = getContext().getTypeAlign(Ty) / 8;
  return ABIArgInfo::getIndirect(CharUnits::fromQuantity(4), /*ByVal=*/true,
                                 TypeAlign > MinABIStackAlignInBytes);
}

Address ARCABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                              QualType Ty) const {
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*indirect*/ false,
                          getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(4), true);
}

ABIArgInfo ARCABIInfo::classifyArgumentType(QualType Ty,
                                            uint8_t FreeRegs) const {
  // Handle the generic C++ ABI.
  const RecordType *RT = Ty->getAs<RecordType>();
  if (RT) {
    CGCXXABI::RecordArgABI RAA = getRecordArgABI(RT, getCXXABI());
    if (RAA == CGCXXABI::RAA_Indirect)
      return getIndirectByRef(Ty, FreeRegs > 0);

    if (RAA == CGCXXABI::RAA_DirectInMemory)
      return getIndirectByValue(Ty);
  }

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  auto SizeInRegs = llvm::alignTo(getContext().getTypeSize(Ty), 32) / 32;

  if (isAggregateTypeForABI(Ty)) {
    // Structures with flexible arrays are always indirect.
    if (RT && RT->getDecl()->hasFlexibleArrayMember())
      return getIndirectByValue(Ty);

    // Ignore empty structs/unions.
    if (isEmptyRecord(getContext(), Ty, true))
      return ABIArgInfo::getIgnore();

    llvm::LLVMContext &LLVMContext = getVMContext();

    llvm::IntegerType *Int32 = llvm::Type::getInt32Ty(LLVMContext);
    SmallVector<llvm::Type *, 3> Elements(SizeInRegs, Int32);
    llvm::Type *Result = llvm::StructType::get(LLVMContext, Elements);

    return FreeRegs >= SizeInRegs ?
        ABIArgInfo::getDirectInReg(Result) :
        ABIArgInfo::getDirect(Result, 0, nullptr, false);
  }

  return Ty->isPromotableIntegerType() ?
      (FreeRegs >= SizeInRegs ? ABIArgInfo::getExtendInReg(Ty) :
                                ABIArgInfo::getExtend(Ty)) :
      (FreeRegs >= SizeInRegs ? ABIArgInfo::getDirectInReg() :
                                ABIArgInfo::getDirect());
}

ABIArgInfo ARCABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isAnyComplexType())
    return ABIArgInfo::getDirectInReg();

  // Arguments of size > 4 registers are indirect.
  auto RetSize = llvm::alignTo(getContext().getTypeSize(RetTy), 32) / 32;
  if (RetSize > 4)
    return getIndirectByRef(RetTy, /*HasFreeRegs*/ true);

  return DefaultABIInfo::classifyReturnType(RetTy);
}

} // End anonymous namespace.

//===----------------------------------------------------------------------===//
// XCore ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

/// A SmallStringEnc instance is used to build up the TypeString by passing
/// it by reference between functions that append to it.
typedef llvm::SmallString<128> SmallStringEnc;

/// TypeStringCache caches the meta encodings of Types.
///
/// The reason for caching TypeStrings is two fold:
///   1. To cache a type's encoding for later uses;
///   2. As a means to break recursive member type inclusion.
///
/// A cache Entry can have a Status of:
///   NonRecursive:   The type encoding is not recursive;
///   Recursive:      The type encoding is recursive;
///   Incomplete:     An incomplete TypeString;
///   IncompleteUsed: An incomplete TypeString that has been used in a
///                   Recursive type encoding.
///
/// A NonRecursive entry will have all of its sub-members expanded as fully
/// as possible. Whilst it may contain types which are recursive, the type
/// itself is not recursive and thus its encoding may be safely used whenever
/// the type is encountered.
///
/// A Recursive entry will have all of its sub-members expanded as fully as
/// possible. The type itself is recursive and it may contain other types which
/// are recursive. The Recursive encoding must not be used during the expansion
/// of a recursive type's recursive branch. For simplicity the code uses
/// IncompleteCount to reject all usage of Recursive encodings for member types.
///
/// An Incomplete entry is always a RecordType and only encodes its
/// identifier e.g. "s(S){}". Incomplete 'StubEnc' entries are ephemeral and
/// are placed into the cache during type expansion as a means to identify and
/// handle recursive inclusion of types as sub-members. If there is recursion
/// the entry becomes IncompleteUsed.
///
/// During the expansion of a RecordType's members:
///
///   If the cache contains a NonRecursive encoding for the member type, the
///   cached encoding is used;
///
///   If the cache contains a Recursive encoding for the member type, the
///   cached encoding is 'Swapped' out, as it may be incorrect, and...
///
///   If the member is a RecordType, an Incomplete encoding is placed into the
///   cache to break potential recursive inclusion of itself as a sub-member;
///
///   Once a member RecordType has been expanded, its temporary incomplete
///   entry is removed from the cache. If a Recursive encoding was swapped out
///   it is swapped back in;
///
///   If an incomplete entry is used to expand a sub-member, the incomplete
///   entry is marked as IncompleteUsed. The cache keeps count of how many
///   IncompleteUsed entries it currently contains in IncompleteUsedCount;
///
///   If a member's encoding is found to be a NonRecursive or Recursive viz:
///   IncompleteUsedCount==0, the member's encoding is added to the cache.
///   Else the member is part of a recursive type and thus the recursion has
///   been exited too soon for the encoding to be correct for the member.
///
class TypeStringCache {
  enum Status {NonRecursive, Recursive, Incomplete, IncompleteUsed};
  struct Entry {
    std::string Str;     // The encoded TypeString for the type.
    enum Status State;   // Information about the encoding in 'Str'.
    std::string Swapped; // A temporary place holder for a Recursive encoding
                         // during the expansion of RecordType's members.
  };
  std::map<const IdentifierInfo *, struct Entry> Map;
  unsigned IncompleteCount;     // Number of Incomplete entries in the Map.
  unsigned IncompleteUsedCount; // Number of IncompleteUsed entries in the Map.
public:
  TypeStringCache() : IncompleteCount(0), IncompleteUsedCount(0) {}
  void addIncomplete(const IdentifierInfo *ID, std::string StubEnc);
  bool removeIncomplete(const IdentifierInfo *ID);
  void addIfComplete(const IdentifierInfo *ID, StringRef Str,
                     bool IsRecursive);
  StringRef lookupStr(const IdentifierInfo *ID);
};

/// TypeString encodings for enum & union fields must be order.
/// FieldEncoding is a helper for this ordering process.
class FieldEncoding {
  bool HasName;
  std::string Enc;
public:
  FieldEncoding(bool b, SmallStringEnc &e) : HasName(b), Enc(e.c_str()) {}
  StringRef str() { return Enc; }
  bool operator<(const FieldEncoding &rhs) const {
    if (HasName != rhs.HasName) return HasName;
    return Enc < rhs.Enc;
  }
};

class XCoreABIInfo : public DefaultABIInfo {
public:
  XCoreABIInfo(CodeGen::CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}
  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;
};

class XCoreTargetCodeGenInfo : public TargetCodeGenInfo {
  mutable TypeStringCache TSC;
public:
  XCoreTargetCodeGenInfo(CodeGenTypes &CGT)
    :TargetCodeGenInfo(new XCoreABIInfo(CGT)) {}
  void emitTargetMD(const Decl *D, llvm::GlobalValue *GV,
                    CodeGen::CodeGenModule &M) const override;
};

} // End anonymous namespace.

// TODO: this implementation is likely now redundant with the default
// EmitVAArg.
Address XCoreABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                QualType Ty) const {
  CGBuilderTy &Builder = CGF.Builder;

  // Get the VAList.
  CharUnits SlotSize = CharUnits::fromQuantity(4);
  Address AP(Builder.CreateLoad(VAListAddr), SlotSize);

  // Handle the argument.
  ABIArgInfo AI = classifyArgumentType(Ty);
  CharUnits TypeAlign = getContext().getTypeAlignInChars(Ty);
  llvm::Type *ArgTy = CGT.ConvertType(Ty);
  if (AI.canHaveCoerceToType() && !AI.getCoerceToType())
    AI.setCoerceToType(ArgTy);
  llvm::Type *ArgPtrTy = llvm::PointerType::getUnqual(ArgTy);

  Address Val = Address::invalid();
  CharUnits ArgSize = CharUnits::Zero();
  switch (AI.getKind()) {
  case ABIArgInfo::Expand:
  case ABIArgInfo::CoerceAndExpand:
  case ABIArgInfo::InAlloca:
    llvm_unreachable("Unsupported ABI kind for va_arg");
  case ABIArgInfo::Ignore:
    Val = Address(llvm::UndefValue::get(ArgPtrTy), TypeAlign);
    ArgSize = CharUnits::Zero();
    break;
  case ABIArgInfo::Extend:
  case ABIArgInfo::Direct:
    Val = Builder.CreateBitCast(AP, ArgPtrTy);
    ArgSize = CharUnits::fromQuantity(
                       getDataLayout().getTypeAllocSize(AI.getCoerceToType()));
    ArgSize = ArgSize.alignTo(SlotSize);
    break;
  case ABIArgInfo::Indirect:
    Val = Builder.CreateElementBitCast(AP, ArgPtrTy);
    Val = Address(Builder.CreateLoad(Val), TypeAlign);
    ArgSize = SlotSize;
    break;
  }

  // Increment the VAList.
  if (!ArgSize.isZero()) {
    llvm::Value *APN =
      Builder.CreateConstInBoundsByteGEP(AP.getPointer(), ArgSize);
    Builder.CreateStore(APN, VAListAddr);
  }

  return Val;
}

/// During the expansion of a RecordType, an incomplete TypeString is placed
/// into the cache as a means to identify and break recursion.
/// If there is a Recursive encoding in the cache, it is swapped out and will
/// be reinserted by removeIncomplete().
/// All other types of encoding should have been used rather than arriving here.
void TypeStringCache::addIncomplete(const IdentifierInfo *ID,
                                    std::string StubEnc) {
  if (!ID)
    return;
  Entry &E = Map[ID];
  assert( (E.Str.empty() || E.State == Recursive) &&
         "Incorrectly use of addIncomplete");
  assert(!StubEnc.empty() && "Passing an empty string to addIncomplete()");
  E.Swapped.swap(E.Str); // swap out the Recursive
  E.Str.swap(StubEnc);
  E.State = Incomplete;
  ++IncompleteCount;
}

/// Once the RecordType has been expanded, the temporary incomplete TypeString
/// must be removed from the cache.
/// If a Recursive was swapped out by addIncomplete(), it will be replaced.
/// Returns true if the RecordType was defined recursively.
bool TypeStringCache::removeIncomplete(const IdentifierInfo *ID) {
  if (!ID)
    return false;
  auto I = Map.find(ID);
  assert(I != Map.end() && "Entry not present");
  Entry &E = I->second;
  assert( (E.State == Incomplete ||
           E.State == IncompleteUsed) &&
         "Entry must be an incomplete type");
  bool IsRecursive = false;
  if (E.State == IncompleteUsed) {
    // We made use of our Incomplete encoding, thus we are recursive.
    IsRecursive = true;
    --IncompleteUsedCount;
  }
  if (E.Swapped.empty())
    Map.erase(I);
  else {
    // Swap the Recursive back.
    E.Swapped.swap(E.Str);
    E.Swapped.clear();
    E.State = Recursive;
  }
  --IncompleteCount;
  return IsRecursive;
}

/// Add the encoded TypeString to the cache only if it is NonRecursive or
/// Recursive (viz: all sub-members were expanded as fully as possible).
void TypeStringCache::addIfComplete(const IdentifierInfo *ID, StringRef Str,
                                    bool IsRecursive) {
  if (!ID || IncompleteUsedCount)
    return; // No key or it is is an incomplete sub-type so don't add.
  Entry &E = Map[ID];
  if (IsRecursive && !E.Str.empty()) {
    assert(E.State==Recursive && E.Str.size() == Str.size() &&
           "This is not the same Recursive entry");
    // The parent container was not recursive after all, so we could have used
    // this Recursive sub-member entry after all, but we assumed the worse when
    // we started viz: IncompleteCount!=0.
    return;
  }
  assert(E.Str.empty() && "Entry already present");
  E.Str = Str.str();
  E.State = IsRecursive? Recursive : NonRecursive;
}

/// Return a cached TypeString encoding for the ID. If there isn't one, or we
/// are recursively expanding a type (IncompleteCount != 0) and the cached
/// encoding is Recursive, return an empty StringRef.
StringRef TypeStringCache::lookupStr(const IdentifierInfo *ID) {
  if (!ID)
    return StringRef();   // We have no key.
  auto I = Map.find(ID);
  if (I == Map.end())
    return StringRef();   // We have no encoding.
  Entry &E = I->second;
  if (E.State == Recursive && IncompleteCount)
    return StringRef();   // We don't use Recursive encodings for member types.

  if (E.State == Incomplete) {
    // The incomplete type is being used to break out of recursion.
    E.State = IncompleteUsed;
    ++IncompleteUsedCount;
  }
  return E.Str;
}

/// The XCore ABI includes a type information section that communicates symbol
/// type information to the linker. The linker uses this information to verify
/// safety/correctness of things such as array bound and pointers et al.
/// The ABI only requires C (and XC) language modules to emit TypeStrings.
/// This type information (TypeString) is emitted into meta data for all global
/// symbols: definitions, declarations, functions & variables.
///
/// The TypeString carries type, qualifier, name, size & value details.
/// Please see 'Tools Development Guide' section 2.16.2 for format details:
/// https://www.xmos.com/download/public/Tools-Development-Guide%28X9114A%29.pdf
/// The output is tested by test/CodeGen/xcore-stringtype.c.
///
static bool getTypeString(SmallStringEnc &Enc, const Decl *D,
                          CodeGen::CodeGenModule &CGM, TypeStringCache &TSC);

/// XCore uses emitTargetMD to emit TypeString metadata for global symbols.
void XCoreTargetCodeGenInfo::emitTargetMD(const Decl *D, llvm::GlobalValue *GV,
                                          CodeGen::CodeGenModule &CGM) const {
  SmallStringEnc Enc;
  if (getTypeString(Enc, D, CGM, TSC)) {
    llvm::LLVMContext &Ctx = CGM.getModule().getContext();
    llvm::Metadata *MDVals[] = {llvm::ConstantAsMetadata::get(GV),
                                llvm::MDString::get(Ctx, Enc.str())};
    llvm::NamedMDNode *MD =
      CGM.getModule().getOrInsertNamedMetadata("xcore.typestrings");
    MD->addOperand(llvm::MDNode::get(Ctx, MDVals));
  }
}

//===----------------------------------------------------------------------===//
// SPIR ABI Implementation
//===----------------------------------------------------------------------===//

namespace {
class SPIRTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  SPIRTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT)
    : TargetCodeGenInfo(new DefaultABIInfo(CGT)) {}
  unsigned getOpenCLKernelCallingConv() const override;
};

} // End anonymous namespace.

namespace clang {
namespace CodeGen {
void computeSPIRKernelABIInfo(CodeGenModule &CGM, CGFunctionInfo &FI) {
  DefaultABIInfo SPIRABI(CGM.getTypes());
  SPIRABI.computeInfo(FI);
}
}
}

unsigned SPIRTargetCodeGenInfo::getOpenCLKernelCallingConv() const {
  return llvm::CallingConv::SPIR_KERNEL;
}

static bool appendType(SmallStringEnc &Enc, QualType QType,
                       const CodeGen::CodeGenModule &CGM,
                       TypeStringCache &TSC);

/// Helper function for appendRecordType().
/// Builds a SmallVector containing the encoded field types in declaration
/// order.
static bool extractFieldType(SmallVectorImpl<FieldEncoding> &FE,
                             const RecordDecl *RD,
                             const CodeGen::CodeGenModule &CGM,
                             TypeStringCache &TSC) {
  for (const auto *Field : RD->fields()) {
    SmallStringEnc Enc;
    Enc += "m(";
    Enc += Field->getName();
    Enc += "){";
    if (Field->isBitField()) {
      Enc += "b(";
      llvm::raw_svector_ostream OS(Enc);
      OS << Field->getBitWidthValue(CGM.getContext());
      Enc += ':';
    }
    if (!appendType(Enc, Field->getType(), CGM, TSC))
      return false;
    if (Field->isBitField())
      Enc += ')';
    Enc += '}';
    FE.emplace_back(!Field->getName().empty(), Enc);
  }
  return true;
}

/// Appends structure and union types to Enc and adds encoding to cache.
/// Recursively calls appendType (via extractFieldType) for each field.
/// Union types have their fields ordered according to the ABI.
static bool appendRecordType(SmallStringEnc &Enc, const RecordType *RT,
                             const CodeGen::CodeGenModule &CGM,
                             TypeStringCache &TSC, const IdentifierInfo *ID) {
  // Append the cached TypeString if we have one.
  StringRef TypeString = TSC.lookupStr(ID);
  if (!TypeString.empty()) {
    Enc += TypeString;
    return true;
  }

  // Start to emit an incomplete TypeString.
  size_t Start = Enc.size();
  Enc += (RT->isUnionType()? 'u' : 's');
  Enc += '(';
  if (ID)
    Enc += ID->getName();
  Enc += "){";

  // We collect all encoded fields and order as necessary.
  bool IsRecursive = false;
  const RecordDecl *RD = RT->getDecl()->getDefinition();
  if (RD && !RD->field_empty()) {
    // An incomplete TypeString stub is placed in the cache for this RecordType
    // so that recursive calls to this RecordType will use it whilst building a
    // complete TypeString for this RecordType.
    SmallVector<FieldEncoding, 16> FE;
    std::string StubEnc(Enc.substr(Start).str());
    StubEnc += '}';  // StubEnc now holds a valid incomplete TypeString.
    TSC.addIncomplete(ID, std::move(StubEnc));
    if (!extractFieldType(FE, RD, CGM, TSC)) {
      (void) TSC.removeIncomplete(ID);
      return false;
    }
    IsRecursive = TSC.removeIncomplete(ID);
    // The ABI requires unions to be sorted but not structures.
    // See FieldEncoding::operator< for sort algorithm.
    if (RT->isUnionType())
      llvm::sort(FE);
    // We can now complete the TypeString.
    unsigned E = FE.size();
    for (unsigned I = 0; I != E; ++I) {
      if (I)
        Enc += ',';
      Enc += FE[I].str();
    }
  }
  Enc += '}';
  TSC.addIfComplete(ID, Enc.substr(Start), IsRecursive);
  return true;
}

/// Appends enum types to Enc and adds the encoding to the cache.
static bool appendEnumType(SmallStringEnc &Enc, const EnumType *ET,
                           TypeStringCache &TSC,
                           const IdentifierInfo *ID) {
  // Append the cached TypeString if we have one.
  StringRef TypeString = TSC.lookupStr(ID);
  if (!TypeString.empty()) {
    Enc += TypeString;
    return true;
  }

  size_t Start = Enc.size();
  Enc += "e(";
  if (ID)
    Enc += ID->getName();
  Enc += "){";

  // We collect all encoded enumerations and order them alphanumerically.
  if (const EnumDecl *ED = ET->getDecl()->getDefinition()) {
    SmallVector<FieldEncoding, 16> FE;
    for (auto I = ED->enumerator_begin(), E = ED->enumerator_end(); I != E;
         ++I) {
      SmallStringEnc EnumEnc;
      EnumEnc += "m(";
      EnumEnc += I->getName();
      EnumEnc += "){";
      I->getInitVal().toString(EnumEnc);
      EnumEnc += '}';
      FE.push_back(FieldEncoding(!I->getName().empty(), EnumEnc));
    }
    llvm::sort(FE);
    unsigned E = FE.size();
    for (unsigned I = 0; I != E; ++I) {
      if (I)
        Enc += ',';
      Enc += FE[I].str();
    }
  }
  Enc += '}';
  TSC.addIfComplete(ID, Enc.substr(Start), false);
  return true;
}

/// Appends type's qualifier to Enc.
/// This is done prior to appending the type's encoding.
static void appendQualifier(SmallStringEnc &Enc, QualType QT) {
  // Qualifiers are emitted in alphabetical order.
  static const char *const Table[]={"","c:","r:","cr:","v:","cv:","rv:","crv:"};
  int Lookup = 0;
  if (QT.isConstQualified())
    Lookup += 1<<0;
  if (QT.isRestrictQualified())
    Lookup += 1<<1;
  if (QT.isVolatileQualified())
    Lookup += 1<<2;
  Enc += Table[Lookup];
}

/// Appends built-in types to Enc.
static bool appendBuiltinType(SmallStringEnc &Enc, const BuiltinType *BT) {
  const char *EncType;
  switch (BT->getKind()) {
    case BuiltinType::Void:
      EncType = "0";
      break;
    case BuiltinType::Bool:
      EncType = "b";
      break;
    case BuiltinType::Char_U:
      EncType = "uc";
      break;
    case BuiltinType::UChar:
      EncType = "uc";
      break;
    case BuiltinType::SChar:
      EncType = "sc";
      break;
    case BuiltinType::UShort:
      EncType = "us";
      break;
    case BuiltinType::Short:
      EncType = "ss";
      break;
    case BuiltinType::UInt:
      EncType = "ui";
      break;
    case BuiltinType::Int:
      EncType = "si";
      break;
    case BuiltinType::ULong:
      EncType = "ul";
      break;
    case BuiltinType::Long:
      EncType = "sl";
      break;
    case BuiltinType::ULongLong:
      EncType = "ull";
      break;
    case BuiltinType::LongLong:
      EncType = "sll";
      break;
    case BuiltinType::Float:
      EncType = "ft";
      break;
    case BuiltinType::Double:
      EncType = "d";
      break;
    case BuiltinType::LongDouble:
      EncType = "ld";
      break;
    default:
      return false;
  }
  Enc += EncType;
  return true;
}

/// Appends a pointer encoding to Enc before calling appendType for the pointee.
static bool appendPointerType(SmallStringEnc &Enc, const PointerType *PT,
                              const CodeGen::CodeGenModule &CGM,
                              TypeStringCache &TSC) {
  Enc += "p(";
  if (!appendType(Enc, PT->getPointeeType(), CGM, TSC))
    return false;
  Enc += ')';
  return true;
}

/// Appends array encoding to Enc before calling appendType for the element.
static bool appendArrayType(SmallStringEnc &Enc, QualType QT,
                            const ArrayType *AT,
                            const CodeGen::CodeGenModule &CGM,
                            TypeStringCache &TSC, StringRef NoSizeEnc) {
  if (AT->getSizeModifier() != ArrayType::Normal)
    return false;
  Enc += "a(";
  if (const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(AT))
    CAT->getSize().toStringUnsigned(Enc);
  else
    Enc += NoSizeEnc; // Global arrays use "*", otherwise it is "".
  Enc += ':';
  // The Qualifiers should be attached to the type rather than the array.
  appendQualifier(Enc, QT);
  if (!appendType(Enc, AT->getElementType(), CGM, TSC))
    return false;
  Enc += ')';
  return true;
}

/// Appends a function encoding to Enc, calling appendType for the return type
/// and the arguments.
static bool appendFunctionType(SmallStringEnc &Enc, const FunctionType *FT,
                             const CodeGen::CodeGenModule &CGM,
                             TypeStringCache &TSC) {
  Enc += "f{";
  if (!appendType(Enc, FT->getReturnType(), CGM, TSC))
    return false;
  Enc += "}(";
  if (const FunctionProtoType *FPT = FT->getAs<FunctionProtoType>()) {
    // N.B. we are only interested in the adjusted param types.
    auto I = FPT->param_type_begin();
    auto E = FPT->param_type_end();
    if (I != E) {
      do {
        if (!appendType(Enc, *I, CGM, TSC))
          return false;
        ++I;
        if (I != E)
          Enc += ',';
      } while (I != E);
      if (FPT->isVariadic())
        Enc += ",va";
    } else {
      if (FPT->isVariadic())
        Enc += "va";
      else
        Enc += '0';
    }
  }
  Enc += ')';
  return true;
}

/// Handles the type's qualifier before dispatching a call to handle specific
/// type encodings.
static bool appendType(SmallStringEnc &Enc, QualType QType,
                       const CodeGen::CodeGenModule &CGM,
                       TypeStringCache &TSC) {

  QualType QT = QType.getCanonicalType();

  if (const ArrayType *AT = QT->getAsArrayTypeUnsafe())
    // The Qualifiers should be attached to the type rather than the array.
    // Thus we don't call appendQualifier() here.
    return appendArrayType(Enc, QT, AT, CGM, TSC, "");

  appendQualifier(Enc, QT);

  if (const BuiltinType *BT = QT->getAs<BuiltinType>())
    return appendBuiltinType(Enc, BT);

  if (const PointerType *PT = QT->getAs<PointerType>())
    return appendPointerType(Enc, PT, CGM, TSC);

  if (const EnumType *ET = QT->getAs<EnumType>())
    return appendEnumType(Enc, ET, TSC, QT.getBaseTypeIdentifier());

  if (const RecordType *RT = QT->getAsStructureType())
    return appendRecordType(Enc, RT, CGM, TSC, QT.getBaseTypeIdentifier());

  if (const RecordType *RT = QT->getAsUnionType())
    return appendRecordType(Enc, RT, CGM, TSC, QT.getBaseTypeIdentifier());

  if (const FunctionType *FT = QT->getAs<FunctionType>())
    return appendFunctionType(Enc, FT, CGM, TSC);

  return false;
}

static bool getTypeString(SmallStringEnc &Enc, const Decl *D,
                          CodeGen::CodeGenModule &CGM, TypeStringCache &TSC) {
  if (!D)
    return false;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->getLanguageLinkage() != CLanguageLinkage)
      return false;
    return appendType(Enc, FD->getType(), CGM, TSC);
  }

  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    if (VD->getLanguageLinkage() != CLanguageLinkage)
      return false;
    QualType QT = VD->getType().getCanonicalType();
    if (const ArrayType *AT = QT->getAsArrayTypeUnsafe()) {
      // Global ArrayTypes are given a size of '*' if the size is unknown.
      // The Qualifiers should be attached to the type rather than the array.
      // Thus we don't call appendQualifier() here.
      return appendArrayType(Enc, QT, AT, CGM, TSC, "*");
    }
    return appendType(Enc, QT, CGM, TSC);
  }
  return false;
}

//===----------------------------------------------------------------------===//
// RISCV ABI Implementation
//===----------------------------------------------------------------------===//

namespace {
class RISCVABIInfo : public DefaultABIInfo {
private:
  unsigned XLen; // Size of the integer ('x') registers in bits.
  static const int NumArgGPRs = 8;

public:
  RISCVABIInfo(CodeGen::CodeGenTypes &CGT, unsigned XLen)
      : DefaultABIInfo(CGT), XLen(XLen) {}

  // DefaultABIInfo's classifyReturnType and classifyArgumentType are
  // non-virtual, but computeInfo is virtual, so we overload it.
  void computeInfo(CGFunctionInfo &FI) const override;

  ABIArgInfo classifyArgumentType(QualType Ty, bool IsFixed,
                                  int &ArgGPRsLeft) const;
  ABIArgInfo classifyReturnType(QualType RetTy) const;

  Address EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                    QualType Ty) const override;

  ABIArgInfo extendType(QualType Ty) const;
};
} // end anonymous namespace

void RISCVABIInfo::computeInfo(CGFunctionInfo &FI) const {
  QualType RetTy = FI.getReturnType();
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(RetTy);

  // IsRetIndirect is true if classifyArgumentType indicated the value should
  // be passed indirect or if the type size is greater than 2*xlen. e.g. fp128
  // is passed direct in LLVM IR, relying on the backend lowering code to
  // rewrite the argument list and pass indirectly on RV32.
  bool IsRetIndirect = FI.getReturnInfo().getKind() == ABIArgInfo::Indirect ||
                       getContext().getTypeSize(RetTy) > (2 * XLen);

  // We must track the number of GPRs used in order to conform to the RISC-V
  // ABI, as integer scalars passed in registers should have signext/zeroext
  // when promoted, but are anyext if passed on the stack. As GPR usage is
  // different for variadic arguments, we must also track whether we are
  // examining a vararg or not.
  int ArgGPRsLeft = IsRetIndirect ? NumArgGPRs - 1 : NumArgGPRs;
  int NumFixedArgs = FI.getNumRequiredArgs();

  int ArgNum = 0;
  for (auto &ArgInfo : FI.arguments()) {
    bool IsFixed = ArgNum < NumFixedArgs;
    ArgInfo.info = classifyArgumentType(ArgInfo.type, IsFixed, ArgGPRsLeft);
    ArgNum++;
  }
}

ABIArgInfo RISCVABIInfo::classifyArgumentType(QualType Ty, bool IsFixed,
                                              int &ArgGPRsLeft) const {
  assert(ArgGPRsLeft <= NumArgGPRs && "Arg GPR tracking underflow");
  Ty = useFirstFieldIfTransparentUnion(Ty);

  // Structures with either a non-trivial destructor or a non-trivial
  // copy constructor are always passed indirectly.
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI())) {
    if (ArgGPRsLeft)
      ArgGPRsLeft -= 1;
    return getNaturalAlignIndirect(Ty, /*ByVal=*/RAA ==
                                           CGCXXABI::RAA_DirectInMemory);
  }

  // Ignore empty structs/unions.
  if (isEmptyRecord(getContext(), Ty, true))
    return ABIArgInfo::getIgnore();

  uint64_t Size = getContext().getTypeSize(Ty);
  uint64_t NeededAlign = getContext().getTypeAlign(Ty);
  bool MustUseStack = false;
  // Determine the number of GPRs needed to pass the current argument
  // according to the ABI. 2*XLen-aligned varargs are passed in "aligned"
  // register pairs, so may consume 3 registers.
  int NeededArgGPRs = 1;
  if (!IsFixed && NeededAlign == 2 * XLen)
    NeededArgGPRs = 2 + (ArgGPRsLeft % 2);
  else if (Size > XLen && Size <= 2 * XLen)
    NeededArgGPRs = 2;

  if (NeededArgGPRs > ArgGPRsLeft) {
    MustUseStack = true;
    NeededArgGPRs = ArgGPRsLeft;
  }

  ArgGPRsLeft -= NeededArgGPRs;

  if (!isAggregateTypeForABI(Ty) && !Ty->isVectorType()) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    // All integral types are promoted to XLen width, unless passed on the
    // stack.
    if (Size < XLen && Ty->isIntegralOrEnumerationType() && !MustUseStack) {
      return extendType(Ty);
    }

    return ABIArgInfo::getDirect();
  }

  // Aggregates which are <= 2*XLen will be passed in registers if possible,
  // so coerce to integers.
  if (Size <= 2 * XLen) {
    unsigned Alignment = getContext().getTypeAlign(Ty);

    // Use a single XLen int if possible, 2*XLen if 2*XLen alignment is
    // required, and a 2-element XLen array if only XLen alignment is required.
    if (Size <= XLen) {
      return ABIArgInfo::getDirect(
          llvm::IntegerType::get(getVMContext(), XLen));
    } else if (Alignment == 2 * XLen) {
      return ABIArgInfo::getDirect(
          llvm::IntegerType::get(getVMContext(), 2 * XLen));
    } else {
      return ABIArgInfo::getDirect(llvm::ArrayType::get(
          llvm::IntegerType::get(getVMContext(), XLen), 2));
    }
  }
  return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
}

ABIArgInfo RISCVABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  int ArgGPRsLeft = 2;

  // The rules for return and argument types are the same, so defer to
  // classifyArgumentType.
  return classifyArgumentType(RetTy, /*IsFixed=*/true, ArgGPRsLeft);
}

Address RISCVABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                QualType Ty) const {
  CharUnits SlotSize = CharUnits::fromQuantity(XLen / 8);

  // Empty records are ignored for parameter passing purposes.
  if (isEmptyRecord(getContext(), Ty, true)) {
    Address Addr(CGF.Builder.CreateLoad(VAListAddr), SlotSize);
    Addr = CGF.Builder.CreateElementBitCast(Addr, CGF.ConvertTypeForMem(Ty));
    return Addr;
  }

  std::pair<CharUnits, CharUnits> SizeAndAlign =
      getContext().getTypeInfoInChars(Ty);

  // Arguments bigger than 2*Xlen bytes are passed indirectly.
  bool IsIndirect = SizeAndAlign.first > 2 * SlotSize;

  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect, SizeAndAlign,
                          SlotSize, /*AllowHigherAlign=*/true);
}

ABIArgInfo RISCVABIInfo::extendType(QualType Ty) const {
  int TySize = getContext().getTypeSize(Ty);
  // RV64 ABI requires unsigned 32 bit integers to be sign extended.
  if (XLen == 64 && Ty->isUnsignedIntegerOrEnumerationType() && TySize == 32)
    return ABIArgInfo::getSignExtend(Ty);
  return ABIArgInfo::getExtend(Ty);
}

namespace {
class RISCVTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  RISCVTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT, unsigned XLen)
      : TargetCodeGenInfo(new RISCVABIInfo(CGT, XLen)) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD) return;

    const auto *Attr = FD->getAttr<RISCVInterruptAttr>();
    if (!Attr)
      return;

    const char *Kind;
    switch (Attr->getInterrupt()) {
    case RISCVInterruptAttr::user: Kind = "user"; break;
    case RISCVInterruptAttr::supervisor: Kind = "supervisor"; break;
    case RISCVInterruptAttr::machine: Kind = "machine"; break;
    }

    auto *Fn = cast<llvm::Function>(GV);

    Fn->addFnAttr("interrupt", Kind);
  }
};
} // namespace

//===----------------------------------------------------------------------===//
// Driver code
//===----------------------------------------------------------------------===//

bool CodeGenModule::supportsCOMDAT() const {
  return getTriple().supportsCOMDAT();
}

const TargetCodeGenInfo &CodeGenModule::getTargetCodeGenInfo() {
  if (TheTargetCodeGenInfo)
    return *TheTargetCodeGenInfo;

  // Helper to set the unique_ptr while still keeping the return value.
  auto SetCGInfo = [&](TargetCodeGenInfo *P) -> const TargetCodeGenInfo & {
    this->TheTargetCodeGenInfo.reset(P);
    return *P;
  };

  const llvm::Triple &Triple = getTarget().getTriple();
  switch (Triple.getArch()) {
  default:
    return SetCGInfo(new DefaultTargetCodeGenInfo(Types));

  case llvm::Triple::le32:
    return SetCGInfo(new PNaClTargetCodeGenInfo(Types));
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
    if (Triple.getOS() == llvm::Triple::NaCl)
      return SetCGInfo(new PNaClTargetCodeGenInfo(Types));
    return SetCGInfo(new MIPSTargetCodeGenInfo(Types, true));

  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    return SetCGInfo(new MIPSTargetCodeGenInfo(Types, false));

  case llvm::Triple::avr:
    return SetCGInfo(new AVRTargetCodeGenInfo(Types));

  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be: {
    AArch64ABIInfo::ABIKind Kind = AArch64ABIInfo::AAPCS;
    if (getTarget().getABI() == "darwinpcs")
      Kind = AArch64ABIInfo::DarwinPCS;
    else if (Triple.isOSWindows())
      return SetCGInfo(
          new WindowsAArch64TargetCodeGenInfo(Types, AArch64ABIInfo::Win64));

    return SetCGInfo(new AArch64TargetCodeGenInfo(Types, Kind));
  }

  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    return SetCGInfo(new WebAssemblyTargetCodeGenInfo(Types));

  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb: {
    if (Triple.getOS() == llvm::Triple::Win32) {
      return SetCGInfo(
          new WindowsARMTargetCodeGenInfo(Types, ARMABIInfo::AAPCS_VFP));
    }

    ARMABIInfo::ABIKind Kind = ARMABIInfo::AAPCS;
    StringRef ABIStr = getTarget().getABI();
    if (ABIStr == "apcs-gnu")
      Kind = ARMABIInfo::APCS;
    else if (ABIStr == "aapcs16")
      Kind = ARMABIInfo::AAPCS16_VFP;
    else if (CodeGenOpts.FloatABI == "hard" ||
             (CodeGenOpts.FloatABI != "soft" &&
              (Triple.getEnvironment() == llvm::Triple::GNUEABIHF ||
               Triple.getEnvironment() == llvm::Triple::MuslEABIHF ||
               Triple.getEnvironment() == llvm::Triple::EABIHF)))
      Kind = ARMABIInfo::AAPCS_VFP;

    return SetCGInfo(new ARMTargetCodeGenInfo(Types, Kind));
  }

  case llvm::Triple::ppc:
    return SetCGInfo(
        new PPC32TargetCodeGenInfo(Types, CodeGenOpts.FloatABI == "soft"));
  case llvm::Triple::ppc64:
    if (Triple.isOSBinFormatELF()) {
      PPC64_SVR4_ABIInfo::ABIKind Kind = PPC64_SVR4_ABIInfo::ELFv1;
      if (getTarget().getABI() == "elfv2")
        Kind = PPC64_SVR4_ABIInfo::ELFv2;
      bool HasQPX = getTarget().getABI() == "elfv1-qpx";
      bool IsSoftFloat = CodeGenOpts.FloatABI == "soft";

      return SetCGInfo(new PPC64_SVR4_TargetCodeGenInfo(Types, Kind, HasQPX,
                                                        IsSoftFloat));
    } else
      return SetCGInfo(new PPC64TargetCodeGenInfo(Types));
  case llvm::Triple::ppc64le: {
    assert(Triple.isOSBinFormatELF() && "PPC64 LE non-ELF not supported!");
    PPC64_SVR4_ABIInfo::ABIKind Kind = PPC64_SVR4_ABIInfo::ELFv2;
    if (getTarget().getABI() == "elfv1" || getTarget().getABI() == "elfv1-qpx")
      Kind = PPC64_SVR4_ABIInfo::ELFv1;
    bool HasQPX = getTarget().getABI() == "elfv1-qpx";
    bool IsSoftFloat = CodeGenOpts.FloatABI == "soft";

    return SetCGInfo(new PPC64_SVR4_TargetCodeGenInfo(Types, Kind, HasQPX,
                                                      IsSoftFloat));
  }

  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64:
    return SetCGInfo(new NVPTXTargetCodeGenInfo(Types));

  case llvm::Triple::msp430:
    return SetCGInfo(new MSP430TargetCodeGenInfo(Types));

  case llvm::Triple::riscv32:
    return SetCGInfo(new RISCVTargetCodeGenInfo(Types, 32));
  case llvm::Triple::riscv64:
    return SetCGInfo(new RISCVTargetCodeGenInfo(Types, 64));

  case llvm::Triple::systemz: {
    bool HasVector = getTarget().getABI() == "vector";
    return SetCGInfo(new SystemZTargetCodeGenInfo(Types, HasVector));
  }

  case llvm::Triple::tce:
  case llvm::Triple::tcele:
    return SetCGInfo(new TCETargetCodeGenInfo(Types));

  case llvm::Triple::x86: {
    bool IsDarwinVectorABI = Triple.isOSDarwin();
    bool RetSmallStructInRegABI =
        X86_32TargetCodeGenInfo::isStructReturnInRegABI(Triple, CodeGenOpts);
    bool IsWin32FloatStructABI = Triple.isOSWindows() && !Triple.isOSCygMing();

    if (Triple.getOS() == llvm::Triple::Win32) {
      return SetCGInfo(new WinX86_32TargetCodeGenInfo(
          Types, IsDarwinVectorABI, RetSmallStructInRegABI,
          IsWin32FloatStructABI, CodeGenOpts.NumRegisterParameters));
    } else {
      return SetCGInfo(new X86_32TargetCodeGenInfo(
          Types, IsDarwinVectorABI, RetSmallStructInRegABI,
          IsWin32FloatStructABI, CodeGenOpts.NumRegisterParameters,
          CodeGenOpts.FloatABI == "soft"));
    }
  }

  case llvm::Triple::x86_64: {
    StringRef ABI = getTarget().getABI();
    X86AVXABILevel AVXLevel =
        (ABI == "avx512"
             ? X86AVXABILevel::AVX512
             : ABI == "avx" ? X86AVXABILevel::AVX : X86AVXABILevel::None);

    switch (Triple.getOS()) {
    case llvm::Triple::Win32:
      return SetCGInfo(new WinX86_64TargetCodeGenInfo(Types, AVXLevel));
    case llvm::Triple::PS4:
      return SetCGInfo(new PS4TargetCodeGenInfo(Types, AVXLevel));
    default:
      return SetCGInfo(new X86_64TargetCodeGenInfo(Types, AVXLevel));
    }
  }
  case llvm::Triple::hexagon:
    return SetCGInfo(new HexagonTargetCodeGenInfo(Types));
  case llvm::Triple::lanai:
    return SetCGInfo(new LanaiTargetCodeGenInfo(Types));
  case llvm::Triple::r600:
    return SetCGInfo(new AMDGPUTargetCodeGenInfo(Types));
  case llvm::Triple::amdgcn:
    return SetCGInfo(new AMDGPUTargetCodeGenInfo(Types));
  case llvm::Triple::sparc:
    return SetCGInfo(new SparcV8TargetCodeGenInfo(Types));
  case llvm::Triple::sparcv9:
    return SetCGInfo(new SparcV9TargetCodeGenInfo(Types));
  case llvm::Triple::xcore:
    return SetCGInfo(new XCoreTargetCodeGenInfo(Types));
  case llvm::Triple::arc:
    return SetCGInfo(new ARCTargetCodeGenInfo(Types));
  case llvm::Triple::spir:
  case llvm::Triple::spir64:
    return SetCGInfo(new SPIRTargetCodeGenInfo(Types));
  }
}

/// Create an OpenCL kernel for an enqueued block.
///
/// The kernel has the same function type as the block invoke function. Its
/// name is the name of the block invoke function postfixed with "_kernel".
/// It simply calls the block invoke function then returns.
llvm::Function *
TargetCodeGenInfo::createEnqueuedBlockKernel(CodeGenFunction &CGF,
                                             llvm::Function *Invoke,
                                             llvm::Value *BlockLiteral) const {
  auto *InvokeFT = Invoke->getFunctionType();
  llvm::SmallVector<llvm::Type *, 2> ArgTys;
  for (auto &P : InvokeFT->params())
    ArgTys.push_back(P);
  auto &C = CGF.getLLVMContext();
  std::string Name = Invoke->getName().str() + "_kernel";
  auto *FT = llvm::FunctionType::get(llvm::Type::getVoidTy(C), ArgTys, false);
  auto *F = llvm::Function::Create(FT, llvm::GlobalValue::InternalLinkage, Name,
                                   &CGF.CGM.getModule());
  auto IP = CGF.Builder.saveIP();
  auto *BB = llvm::BasicBlock::Create(C, "entry", F);
  auto &Builder = CGF.Builder;
  Builder.SetInsertPoint(BB);
  llvm::SmallVector<llvm::Value *, 2> Args;
  for (auto &A : F->args())
    Args.push_back(&A);
  Builder.CreateCall(Invoke, Args);
  Builder.CreateRetVoid();
  Builder.restoreIP(IP);
  return F;
}

/// Create an OpenCL kernel for an enqueued block.
///
/// The type of the first argument (the block literal) is the struct type
/// of the block literal instead of a pointer type. The first argument
/// (block literal) is passed directly by value to the kernel. The kernel
/// allocates the same type of struct on stack and stores the block literal
/// to it and passes its pointer to the block invoke function. The kernel
/// has "enqueued-block" function attribute and kernel argument metadata.
llvm::Function *AMDGPUTargetCodeGenInfo::createEnqueuedBlockKernel(
    CodeGenFunction &CGF, llvm::Function *Invoke,
    llvm::Value *BlockLiteral) const {
  auto &Builder = CGF.Builder;
  auto &C = CGF.getLLVMContext();

  auto *BlockTy = BlockLiteral->getType()->getPointerElementType();
  auto *InvokeFT = Invoke->getFunctionType();
  llvm::SmallVector<llvm::Type *, 2> ArgTys;
  llvm::SmallVector<llvm::Metadata *, 8> AddressQuals;
  llvm::SmallVector<llvm::Metadata *, 8> AccessQuals;
  llvm::SmallVector<llvm::Metadata *, 8> ArgTypeNames;
  llvm::SmallVector<llvm::Metadata *, 8> ArgBaseTypeNames;
  llvm::SmallVector<llvm::Metadata *, 8> ArgTypeQuals;
  llvm::SmallVector<llvm::Metadata *, 8> ArgNames;

  ArgTys.push_back(BlockTy);
  ArgTypeNames.push_back(llvm::MDString::get(C, "__block_literal"));
  AddressQuals.push_back(llvm::ConstantAsMetadata::get(Builder.getInt32(0)));
  ArgBaseTypeNames.push_back(llvm::MDString::get(C, "__block_literal"));
  ArgTypeQuals.push_back(llvm::MDString::get(C, ""));
  AccessQuals.push_back(llvm::MDString::get(C, "none"));
  ArgNames.push_back(llvm::MDString::get(C, "block_literal"));
  for (unsigned I = 1, E = InvokeFT->getNumParams(); I < E; ++I) {
    ArgTys.push_back(InvokeFT->getParamType(I));
    ArgTypeNames.push_back(llvm::MDString::get(C, "void*"));
    AddressQuals.push_back(llvm::ConstantAsMetadata::get(Builder.getInt32(3)));
    AccessQuals.push_back(llvm::MDString::get(C, "none"));
    ArgBaseTypeNames.push_back(llvm::MDString::get(C, "void*"));
    ArgTypeQuals.push_back(llvm::MDString::get(C, ""));
    ArgNames.push_back(
        llvm::MDString::get(C, (Twine("local_arg") + Twine(I)).str()));
  }
  std::string Name = Invoke->getName().str() + "_kernel";
  auto *FT = llvm::FunctionType::get(llvm::Type::getVoidTy(C), ArgTys, false);
  auto *F = llvm::Function::Create(FT, llvm::GlobalValue::InternalLinkage, Name,
                                   &CGF.CGM.getModule());
  F->addFnAttr("enqueued-block");
  auto IP = CGF.Builder.saveIP();
  auto *BB = llvm::BasicBlock::Create(C, "entry", F);
  Builder.SetInsertPoint(BB);
  unsigned BlockAlign = CGF.CGM.getDataLayout().getPrefTypeAlignment(BlockTy);
  auto *BlockPtr = Builder.CreateAlloca(BlockTy, nullptr);
  BlockPtr->setAlignment(BlockAlign);
  Builder.CreateAlignedStore(F->arg_begin(), BlockPtr, BlockAlign);
  auto *Cast = Builder.CreatePointerCast(BlockPtr, InvokeFT->getParamType(0));
  llvm::SmallVector<llvm::Value *, 2> Args;
  Args.push_back(Cast);
  for (auto I = F->arg_begin() + 1, E = F->arg_end(); I != E; ++I)
    Args.push_back(I);
  Builder.CreateCall(Invoke, Args);
  Builder.CreateRetVoid();
  Builder.restoreIP(IP);

  F->setMetadata("kernel_arg_addr_space", llvm::MDNode::get(C, AddressQuals));
  F->setMetadata("kernel_arg_access_qual", llvm::MDNode::get(C, AccessQuals));
  F->setMetadata("kernel_arg_type", llvm::MDNode::get(C, ArgTypeNames));
  F->setMetadata("kernel_arg_base_type",
                 llvm::MDNode::get(C, ArgBaseTypeNames));
  F->setMetadata("kernel_arg_type_qual", llvm::MDNode::get(C, ArgTypeQuals));
  if (CGF.CGM.getCodeGenOpts().EmitOpenCLArgMetadata)
    F->setMetadata("kernel_arg_name", llvm::MDNode::get(C, ArgNames));

  return F;
}
