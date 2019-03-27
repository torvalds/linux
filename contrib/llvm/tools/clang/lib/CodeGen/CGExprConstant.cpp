//===--- CGExprConstant.cpp - Emit LLVM Code from Constant Expressions ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Constant Expr nodes as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CGCXXABI.h"
#include "CGObjCRuntime.h"
#include "CGRecordLayout.h"
#include "CodeGenModule.h"
#include "ConstantEmitter.h"
#include "TargetInfo.h"
#include "clang/AST/APValue.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/Builtins.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
using namespace clang;
using namespace CodeGen;

//===----------------------------------------------------------------------===//
//                            ConstStructBuilder
//===----------------------------------------------------------------------===//

namespace {
class ConstExprEmitter;
class ConstStructBuilder {
  CodeGenModule &CGM;
  ConstantEmitter &Emitter;

  bool Packed;
  CharUnits NextFieldOffsetInChars;
  CharUnits LLVMStructAlignment;
  SmallVector<llvm::Constant *, 32> Elements;
public:
  static llvm::Constant *BuildStruct(ConstantEmitter &Emitter,
                                     ConstExprEmitter *ExprEmitter,
                                     llvm::Constant *Base,
                                     InitListExpr *Updater,
                                     QualType ValTy);
  static llvm::Constant *BuildStruct(ConstantEmitter &Emitter,
                                     InitListExpr *ILE, QualType StructTy);
  static llvm::Constant *BuildStruct(ConstantEmitter &Emitter,
                                     const APValue &Value, QualType ValTy);

private:
  ConstStructBuilder(ConstantEmitter &emitter)
    : CGM(emitter.CGM), Emitter(emitter), Packed(false),
    NextFieldOffsetInChars(CharUnits::Zero()),
    LLVMStructAlignment(CharUnits::One()) { }

  void AppendField(const FieldDecl *Field, uint64_t FieldOffset,
                   llvm::Constant *InitExpr);

  void AppendBytes(CharUnits FieldOffsetInChars, llvm::Constant *InitCst);

  void AppendBitField(const FieldDecl *Field, uint64_t FieldOffset,
                      llvm::ConstantInt *InitExpr);

  void AppendPadding(CharUnits PadSize);

  void AppendTailPadding(CharUnits RecordSize);

  void ConvertStructToPacked();

  bool Build(InitListExpr *ILE);
  bool Build(ConstExprEmitter *Emitter, llvm::Constant *Base,
             InitListExpr *Updater);
  bool Build(const APValue &Val, const RecordDecl *RD, bool IsPrimaryBase,
             const CXXRecordDecl *VTableClass, CharUnits BaseOffset);
  llvm::Constant *Finalize(QualType Ty);

  CharUnits getAlignment(const llvm::Constant *C) const {
    if (Packed)  return CharUnits::One();
    return CharUnits::fromQuantity(
        CGM.getDataLayout().getABITypeAlignment(C->getType()));
  }

  CharUnits getSizeInChars(const llvm::Constant *C) const {
    return CharUnits::fromQuantity(
        CGM.getDataLayout().getTypeAllocSize(C->getType()));
  }
};

void ConstStructBuilder::
AppendField(const FieldDecl *Field, uint64_t FieldOffset,
            llvm::Constant *InitCst) {
  const ASTContext &Context = CGM.getContext();

  CharUnits FieldOffsetInChars = Context.toCharUnitsFromBits(FieldOffset);

  AppendBytes(FieldOffsetInChars, InitCst);
}

void ConstStructBuilder::
AppendBytes(CharUnits FieldOffsetInChars, llvm::Constant *InitCst) {

  assert(NextFieldOffsetInChars <= FieldOffsetInChars
         && "Field offset mismatch!");

  CharUnits FieldAlignment = getAlignment(InitCst);

  // Round up the field offset to the alignment of the field type.
  CharUnits AlignedNextFieldOffsetInChars =
      NextFieldOffsetInChars.alignTo(FieldAlignment);

  if (AlignedNextFieldOffsetInChars < FieldOffsetInChars) {
    // We need to append padding.
    AppendPadding(FieldOffsetInChars - NextFieldOffsetInChars);

    assert(NextFieldOffsetInChars == FieldOffsetInChars &&
           "Did not add enough padding!");

    AlignedNextFieldOffsetInChars =
        NextFieldOffsetInChars.alignTo(FieldAlignment);
  }

  if (AlignedNextFieldOffsetInChars > FieldOffsetInChars) {
    assert(!Packed && "Alignment is wrong even with a packed struct!");

    // Convert the struct to a packed struct.
    ConvertStructToPacked();

    // After we pack the struct, we may need to insert padding.
    if (NextFieldOffsetInChars < FieldOffsetInChars) {
      // We need to append padding.
      AppendPadding(FieldOffsetInChars - NextFieldOffsetInChars);

      assert(NextFieldOffsetInChars == FieldOffsetInChars &&
             "Did not add enough padding!");
    }
    AlignedNextFieldOffsetInChars = NextFieldOffsetInChars;
  }

  // Add the field.
  Elements.push_back(InitCst);
  NextFieldOffsetInChars = AlignedNextFieldOffsetInChars +
                           getSizeInChars(InitCst);

  if (Packed)
    assert(LLVMStructAlignment == CharUnits::One() &&
           "Packed struct not byte-aligned!");
  else
    LLVMStructAlignment = std::max(LLVMStructAlignment, FieldAlignment);
}

void ConstStructBuilder::AppendBitField(const FieldDecl *Field,
                                        uint64_t FieldOffset,
                                        llvm::ConstantInt *CI) {
  const ASTContext &Context = CGM.getContext();
  const uint64_t CharWidth = Context.getCharWidth();
  uint64_t NextFieldOffsetInBits = Context.toBits(NextFieldOffsetInChars);
  if (FieldOffset > NextFieldOffsetInBits) {
    // We need to add padding.
    CharUnits PadSize = Context.toCharUnitsFromBits(
        llvm::alignTo(FieldOffset - NextFieldOffsetInBits,
                      Context.getTargetInfo().getCharAlign()));

    AppendPadding(PadSize);
  }

  uint64_t FieldSize = Field->getBitWidthValue(Context);

  llvm::APInt FieldValue = CI->getValue();

  // Promote the size of FieldValue if necessary
  // FIXME: This should never occur, but currently it can because initializer
  // constants are cast to bool, and because clang is not enforcing bitfield
  // width limits.
  if (FieldSize > FieldValue.getBitWidth())
    FieldValue = FieldValue.zext(FieldSize);

  // Truncate the size of FieldValue to the bit field size.
  if (FieldSize < FieldValue.getBitWidth())
    FieldValue = FieldValue.trunc(FieldSize);

  NextFieldOffsetInBits = Context.toBits(NextFieldOffsetInChars);
  if (FieldOffset < NextFieldOffsetInBits) {
    // Either part of the field or the entire field can go into the previous
    // byte.
    assert(!Elements.empty() && "Elements can't be empty!");

    unsigned BitsInPreviousByte = NextFieldOffsetInBits - FieldOffset;

    bool FitsCompletelyInPreviousByte =
      BitsInPreviousByte >= FieldValue.getBitWidth();

    llvm::APInt Tmp = FieldValue;

    if (!FitsCompletelyInPreviousByte) {
      unsigned NewFieldWidth = FieldSize - BitsInPreviousByte;

      if (CGM.getDataLayout().isBigEndian()) {
        Tmp.lshrInPlace(NewFieldWidth);
        Tmp = Tmp.trunc(BitsInPreviousByte);

        // We want the remaining high bits.
        FieldValue = FieldValue.trunc(NewFieldWidth);
      } else {
        Tmp = Tmp.trunc(BitsInPreviousByte);

        // We want the remaining low bits.
        FieldValue.lshrInPlace(BitsInPreviousByte);
        FieldValue = FieldValue.trunc(NewFieldWidth);
      }
    }

    Tmp = Tmp.zext(CharWidth);
    if (CGM.getDataLayout().isBigEndian()) {
      if (FitsCompletelyInPreviousByte)
        Tmp = Tmp.shl(BitsInPreviousByte - FieldValue.getBitWidth());
    } else {
      Tmp = Tmp.shl(CharWidth - BitsInPreviousByte);
    }

    // 'or' in the bits that go into the previous byte.
    llvm::Value *LastElt = Elements.back();
    if (llvm::ConstantInt *Val = dyn_cast<llvm::ConstantInt>(LastElt))
      Tmp |= Val->getValue();
    else {
      assert(isa<llvm::UndefValue>(LastElt));
      // If there is an undef field that we're adding to, it can either be a
      // scalar undef (in which case, we just replace it with our field) or it
      // is an array.  If it is an array, we have to pull one byte off the
      // array so that the other undef bytes stay around.
      if (!isa<llvm::IntegerType>(LastElt->getType())) {
        // The undef padding will be a multibyte array, create a new smaller
        // padding and then an hole for our i8 to get plopped into.
        assert(isa<llvm::ArrayType>(LastElt->getType()) &&
               "Expected array padding of undefs");
        llvm::ArrayType *AT = cast<llvm::ArrayType>(LastElt->getType());
        assert(AT->getElementType()->isIntegerTy(CharWidth) &&
               AT->getNumElements() != 0 &&
               "Expected non-empty array padding of undefs");

        // Remove the padding array.
        NextFieldOffsetInChars -= CharUnits::fromQuantity(AT->getNumElements());
        Elements.pop_back();

        // Add the padding back in two chunks.
        AppendPadding(CharUnits::fromQuantity(AT->getNumElements()-1));
        AppendPadding(CharUnits::One());
        assert(isa<llvm::UndefValue>(Elements.back()) &&
               Elements.back()->getType()->isIntegerTy(CharWidth) &&
               "Padding addition didn't work right");
      }
    }

    Elements.back() = llvm::ConstantInt::get(CGM.getLLVMContext(), Tmp);

    if (FitsCompletelyInPreviousByte)
      return;
  }

  while (FieldValue.getBitWidth() > CharWidth) {
    llvm::APInt Tmp;

    if (CGM.getDataLayout().isBigEndian()) {
      // We want the high bits.
      Tmp =
        FieldValue.lshr(FieldValue.getBitWidth() - CharWidth).trunc(CharWidth);
    } else {
      // We want the low bits.
      Tmp = FieldValue.trunc(CharWidth);

      FieldValue.lshrInPlace(CharWidth);
    }

    Elements.push_back(llvm::ConstantInt::get(CGM.getLLVMContext(), Tmp));
    ++NextFieldOffsetInChars;

    FieldValue = FieldValue.trunc(FieldValue.getBitWidth() - CharWidth);
  }

  assert(FieldValue.getBitWidth() > 0 &&
         "Should have at least one bit left!");
  assert(FieldValue.getBitWidth() <= CharWidth &&
         "Should not have more than a byte left!");

  if (FieldValue.getBitWidth() < CharWidth) {
    if (CGM.getDataLayout().isBigEndian()) {
      unsigned BitWidth = FieldValue.getBitWidth();

      FieldValue = FieldValue.zext(CharWidth) << (CharWidth - BitWidth);
    } else
      FieldValue = FieldValue.zext(CharWidth);
  }

  // Append the last element.
  Elements.push_back(llvm::ConstantInt::get(CGM.getLLVMContext(),
                                            FieldValue));
  ++NextFieldOffsetInChars;
}

void ConstStructBuilder::AppendPadding(CharUnits PadSize) {
  if (PadSize.isZero())
    return;

  llvm::Type *Ty = CGM.Int8Ty;
  if (PadSize > CharUnits::One())
    Ty = llvm::ArrayType::get(Ty, PadSize.getQuantity());

  llvm::Constant *C = llvm::UndefValue::get(Ty);
  Elements.push_back(C);
  assert(getAlignment(C) == CharUnits::One() &&
         "Padding must have 1 byte alignment!");

  NextFieldOffsetInChars += getSizeInChars(C);
}

void ConstStructBuilder::AppendTailPadding(CharUnits RecordSize) {
  assert(NextFieldOffsetInChars <= RecordSize &&
         "Size mismatch!");

  AppendPadding(RecordSize - NextFieldOffsetInChars);
}

void ConstStructBuilder::ConvertStructToPacked() {
  SmallVector<llvm::Constant *, 16> PackedElements;
  CharUnits ElementOffsetInChars = CharUnits::Zero();

  for (unsigned i = 0, e = Elements.size(); i != e; ++i) {
    llvm::Constant *C = Elements[i];

    CharUnits ElementAlign = CharUnits::fromQuantity(
      CGM.getDataLayout().getABITypeAlignment(C->getType()));
    CharUnits AlignedElementOffsetInChars =
        ElementOffsetInChars.alignTo(ElementAlign);

    if (AlignedElementOffsetInChars > ElementOffsetInChars) {
      // We need some padding.
      CharUnits NumChars =
        AlignedElementOffsetInChars - ElementOffsetInChars;

      llvm::Type *Ty = CGM.Int8Ty;
      if (NumChars > CharUnits::One())
        Ty = llvm::ArrayType::get(Ty, NumChars.getQuantity());

      llvm::Constant *Padding = llvm::UndefValue::get(Ty);
      PackedElements.push_back(Padding);
      ElementOffsetInChars += getSizeInChars(Padding);
    }

    PackedElements.push_back(C);
    ElementOffsetInChars += getSizeInChars(C);
  }

  assert(ElementOffsetInChars == NextFieldOffsetInChars &&
         "Packing the struct changed its size!");

  Elements.swap(PackedElements);
  LLVMStructAlignment = CharUnits::One();
  Packed = true;
}

bool ConstStructBuilder::Build(InitListExpr *ILE) {
  RecordDecl *RD = ILE->getType()->getAs<RecordType>()->getDecl();
  const ASTRecordLayout &Layout = CGM.getContext().getASTRecordLayout(RD);

  unsigned FieldNo = 0;
  unsigned ElementNo = 0;

  // Bail out if we have base classes. We could support these, but they only
  // arise in C++1z where we will have already constant folded most interesting
  // cases. FIXME: There are still a few more cases we can handle this way.
  if (auto *CXXRD = dyn_cast<CXXRecordDecl>(RD))
    if (CXXRD->getNumBases())
      return false;

  for (RecordDecl::field_iterator Field = RD->field_begin(),
       FieldEnd = RD->field_end(); Field != FieldEnd; ++Field, ++FieldNo) {
    // If this is a union, skip all the fields that aren't being initialized.
    if (RD->isUnion() && ILE->getInitializedFieldInUnion() != *Field)
      continue;

    // Don't emit anonymous bitfields, they just affect layout.
    if (Field->isUnnamedBitfield())
      continue;

    // Get the initializer.  A struct can include fields without initializers,
    // we just use explicit null values for them.
    llvm::Constant *EltInit;
    if (ElementNo < ILE->getNumInits())
      EltInit = Emitter.tryEmitPrivateForMemory(ILE->getInit(ElementNo++),
                                                Field->getType());
    else
      EltInit = Emitter.emitNullForMemory(Field->getType());

    if (!EltInit)
      return false;

    if (!Field->isBitField()) {
      // Handle non-bitfield members.
      AppendField(*Field, Layout.getFieldOffset(FieldNo), EltInit);
    } else {
      // Otherwise we have a bitfield.
      if (auto *CI = dyn_cast<llvm::ConstantInt>(EltInit)) {
        AppendBitField(*Field, Layout.getFieldOffset(FieldNo), CI);
      } else {
        // We are trying to initialize a bitfield with a non-trivial constant,
        // this must require run-time code.
        return false;
      }
    }
  }

  return true;
}

namespace {
struct BaseInfo {
  BaseInfo(const CXXRecordDecl *Decl, CharUnits Offset, unsigned Index)
    : Decl(Decl), Offset(Offset), Index(Index) {
  }

  const CXXRecordDecl *Decl;
  CharUnits Offset;
  unsigned Index;

  bool operator<(const BaseInfo &O) const { return Offset < O.Offset; }
};
}

bool ConstStructBuilder::Build(const APValue &Val, const RecordDecl *RD,
                               bool IsPrimaryBase,
                               const CXXRecordDecl *VTableClass,
                               CharUnits Offset) {
  const ASTRecordLayout &Layout = CGM.getContext().getASTRecordLayout(RD);

  if (const CXXRecordDecl *CD = dyn_cast<CXXRecordDecl>(RD)) {
    // Add a vtable pointer, if we need one and it hasn't already been added.
    if (CD->isDynamicClass() && !IsPrimaryBase) {
      llvm::Constant *VTableAddressPoint =
          CGM.getCXXABI().getVTableAddressPointForConstExpr(
              BaseSubobject(CD, Offset), VTableClass);
      AppendBytes(Offset, VTableAddressPoint);
    }

    // Accumulate and sort bases, in order to visit them in address order, which
    // may not be the same as declaration order.
    SmallVector<BaseInfo, 8> Bases;
    Bases.reserve(CD->getNumBases());
    unsigned BaseNo = 0;
    for (CXXRecordDecl::base_class_const_iterator Base = CD->bases_begin(),
         BaseEnd = CD->bases_end(); Base != BaseEnd; ++Base, ++BaseNo) {
      assert(!Base->isVirtual() && "should not have virtual bases here");
      const CXXRecordDecl *BD = Base->getType()->getAsCXXRecordDecl();
      CharUnits BaseOffset = Layout.getBaseClassOffset(BD);
      Bases.push_back(BaseInfo(BD, BaseOffset, BaseNo));
    }
    std::stable_sort(Bases.begin(), Bases.end());

    for (unsigned I = 0, N = Bases.size(); I != N; ++I) {
      BaseInfo &Base = Bases[I];

      bool IsPrimaryBase = Layout.getPrimaryBase() == Base.Decl;
      Build(Val.getStructBase(Base.Index), Base.Decl, IsPrimaryBase,
            VTableClass, Offset + Base.Offset);
    }
  }

  unsigned FieldNo = 0;
  uint64_t OffsetBits = CGM.getContext().toBits(Offset);

  for (RecordDecl::field_iterator Field = RD->field_begin(),
       FieldEnd = RD->field_end(); Field != FieldEnd; ++Field, ++FieldNo) {
    // If this is a union, skip all the fields that aren't being initialized.
    if (RD->isUnion() && Val.getUnionField() != *Field)
      continue;

    // Don't emit anonymous bitfields, they just affect layout.
    if (Field->isUnnamedBitfield())
      continue;

    // Emit the value of the initializer.
    const APValue &FieldValue =
      RD->isUnion() ? Val.getUnionValue() : Val.getStructField(FieldNo);
    llvm::Constant *EltInit =
      Emitter.tryEmitPrivateForMemory(FieldValue, Field->getType());
    if (!EltInit)
      return false;

    if (!Field->isBitField()) {
      // Handle non-bitfield members.
      AppendField(*Field, Layout.getFieldOffset(FieldNo) + OffsetBits, EltInit);
    } else {
      // Otherwise we have a bitfield.
      AppendBitField(*Field, Layout.getFieldOffset(FieldNo) + OffsetBits,
                     cast<llvm::ConstantInt>(EltInit));
    }
  }

  return true;
}

llvm::Constant *ConstStructBuilder::Finalize(QualType Ty) {
  RecordDecl *RD = Ty->getAs<RecordType>()->getDecl();
  const ASTRecordLayout &Layout = CGM.getContext().getASTRecordLayout(RD);

  CharUnits LayoutSizeInChars = Layout.getSize();

  if (NextFieldOffsetInChars > LayoutSizeInChars) {
    // If the struct is bigger than the size of the record type,
    // we must have a flexible array member at the end.
    assert(RD->hasFlexibleArrayMember() &&
           "Must have flexible array member if struct is bigger than type!");

    // No tail padding is necessary.
  } else {
    // Append tail padding if necessary.
    CharUnits LLVMSizeInChars =
        NextFieldOffsetInChars.alignTo(LLVMStructAlignment);

    if (LLVMSizeInChars != LayoutSizeInChars)
      AppendTailPadding(LayoutSizeInChars);

    LLVMSizeInChars = NextFieldOffsetInChars.alignTo(LLVMStructAlignment);

    // Check if we need to convert the struct to a packed struct.
    if (NextFieldOffsetInChars <= LayoutSizeInChars &&
        LLVMSizeInChars > LayoutSizeInChars) {
      assert(!Packed && "Size mismatch!");

      ConvertStructToPacked();
      assert(NextFieldOffsetInChars <= LayoutSizeInChars &&
             "Converting to packed did not help!");
    }

    LLVMSizeInChars = NextFieldOffsetInChars.alignTo(LLVMStructAlignment);

    assert(LayoutSizeInChars == LLVMSizeInChars &&
           "Tail padding mismatch!");
  }

  // Pick the type to use.  If the type is layout identical to the ConvertType
  // type then use it, otherwise use whatever the builder produced for us.
  llvm::StructType *STy =
      llvm::ConstantStruct::getTypeForElements(CGM.getLLVMContext(),
                                               Elements, Packed);
  llvm::Type *ValTy = CGM.getTypes().ConvertType(Ty);
  if (llvm::StructType *ValSTy = dyn_cast<llvm::StructType>(ValTy)) {
    if (ValSTy->isLayoutIdentical(STy))
      STy = ValSTy;
  }

  llvm::Constant *Result = llvm::ConstantStruct::get(STy, Elements);

  assert(NextFieldOffsetInChars.alignTo(getAlignment(Result)) ==
             getSizeInChars(Result) &&
         "Size mismatch!");

  return Result;
}

llvm::Constant *ConstStructBuilder::BuildStruct(ConstantEmitter &Emitter,
                                                ConstExprEmitter *ExprEmitter,
                                                llvm::Constant *Base,
                                                InitListExpr *Updater,
                                                QualType ValTy) {
  ConstStructBuilder Builder(Emitter);
  if (!Builder.Build(ExprEmitter, Base, Updater))
    return nullptr;
  return Builder.Finalize(ValTy);
}

llvm::Constant *ConstStructBuilder::BuildStruct(ConstantEmitter &Emitter,
                                                InitListExpr *ILE,
                                                QualType ValTy) {
  ConstStructBuilder Builder(Emitter);

  if (!Builder.Build(ILE))
    return nullptr;

  return Builder.Finalize(ValTy);
}

llvm::Constant *ConstStructBuilder::BuildStruct(ConstantEmitter &Emitter,
                                                const APValue &Val,
                                                QualType ValTy) {
  ConstStructBuilder Builder(Emitter);

  const RecordDecl *RD = ValTy->castAs<RecordType>()->getDecl();
  const CXXRecordDecl *CD = dyn_cast<CXXRecordDecl>(RD);
  if (!Builder.Build(Val, RD, false, CD, CharUnits::Zero()))
    return nullptr;

  return Builder.Finalize(ValTy);
}


//===----------------------------------------------------------------------===//
//                             ConstExprEmitter
//===----------------------------------------------------------------------===//

static ConstantAddress tryEmitGlobalCompoundLiteral(CodeGenModule &CGM,
                                                    CodeGenFunction *CGF,
                                              const CompoundLiteralExpr *E) {
  CharUnits Align = CGM.getContext().getTypeAlignInChars(E->getType());
  if (llvm::GlobalVariable *Addr =
          CGM.getAddrOfConstantCompoundLiteralIfEmitted(E))
    return ConstantAddress(Addr, Align);

  LangAS addressSpace = E->getType().getAddressSpace();

  ConstantEmitter emitter(CGM, CGF);
  llvm::Constant *C = emitter.tryEmitForInitializer(E->getInitializer(),
                                                    addressSpace, E->getType());
  if (!C) {
    assert(!E->isFileScope() &&
           "file-scope compound literal did not have constant initializer!");
    return ConstantAddress::invalid();
  }

  auto GV = new llvm::GlobalVariable(CGM.getModule(), C->getType(),
                                     CGM.isTypeConstant(E->getType(), true),
                                     llvm::GlobalValue::InternalLinkage,
                                     C, ".compoundliteral", nullptr,
                                     llvm::GlobalVariable::NotThreadLocal,
                    CGM.getContext().getTargetAddressSpace(addressSpace));
  emitter.finalize(GV);
  GV->setAlignment(Align.getQuantity());
  CGM.setAddrOfConstantCompoundLiteral(E, GV);
  return ConstantAddress(GV, Align);
}

static llvm::Constant *
EmitArrayConstant(CodeGenModule &CGM, const ConstantArrayType *DestType,
                  llvm::Type *CommonElementType, unsigned ArrayBound,
                  SmallVectorImpl<llvm::Constant *> &Elements,
                  llvm::Constant *Filler) {
  // Figure out how long the initial prefix of non-zero elements is.
  unsigned NonzeroLength = ArrayBound;
  if (Elements.size() < NonzeroLength && Filler->isNullValue())
    NonzeroLength = Elements.size();
  if (NonzeroLength == Elements.size()) {
    while (NonzeroLength > 0 && Elements[NonzeroLength - 1]->isNullValue())
      --NonzeroLength;
  }

  if (NonzeroLength == 0) {
    return llvm::ConstantAggregateZero::get(
        CGM.getTypes().ConvertType(QualType(DestType, 0)));
  }

  // Add a zeroinitializer array filler if we have lots of trailing zeroes.
  unsigned TrailingZeroes = ArrayBound - NonzeroLength;
  if (TrailingZeroes >= 8) {
    assert(Elements.size() >= NonzeroLength &&
           "missing initializer for non-zero element");

    // If all the elements had the same type up to the trailing zeroes, emit a
    // struct of two arrays (the nonzero data and the zeroinitializer).
    if (CommonElementType && NonzeroLength >= 8) {
      llvm::Constant *Initial = llvm::ConstantArray::get(
          llvm::ArrayType::get(CommonElementType, NonzeroLength),
          makeArrayRef(Elements).take_front(NonzeroLength));
      Elements.resize(2);
      Elements[0] = Initial;
    } else {
      Elements.resize(NonzeroLength + 1);
    }

    auto *FillerType =
        CommonElementType
            ? CommonElementType
            : CGM.getTypes().ConvertType(DestType->getElementType());
    FillerType = llvm::ArrayType::get(FillerType, TrailingZeroes);
    Elements.back() = llvm::ConstantAggregateZero::get(FillerType);
    CommonElementType = nullptr;
  } else if (Elements.size() != ArrayBound) {
    // Otherwise pad to the right size with the filler if necessary.
    Elements.resize(ArrayBound, Filler);
    if (Filler->getType() != CommonElementType)
      CommonElementType = nullptr;
  }

  // If all elements have the same type, just emit an array constant.
  if (CommonElementType)
    return llvm::ConstantArray::get(
        llvm::ArrayType::get(CommonElementType, ArrayBound), Elements);

  // We have mixed types. Use a packed struct.
  llvm::SmallVector<llvm::Type *, 16> Types;
  Types.reserve(Elements.size());
  for (llvm::Constant *Elt : Elements)
    Types.push_back(Elt->getType());
  llvm::StructType *SType =
      llvm::StructType::get(CGM.getLLVMContext(), Types, true);
  return llvm::ConstantStruct::get(SType, Elements);
}

/// This class only needs to handle two cases:
/// 1) Literals (this is used by APValue emission to emit literals).
/// 2) Arrays, structs and unions (outside C++11 mode, we don't currently
///    constant fold these types).
class ConstExprEmitter :
  public StmtVisitor<ConstExprEmitter, llvm::Constant*, QualType> {
  CodeGenModule &CGM;
  ConstantEmitter &Emitter;
  llvm::LLVMContext &VMContext;
public:
  ConstExprEmitter(ConstantEmitter &emitter)
    : CGM(emitter.CGM), Emitter(emitter), VMContext(CGM.getLLVMContext()) {
  }

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  llvm::Constant *VisitStmt(Stmt *S, QualType T) {
    return nullptr;
  }

  llvm::Constant *VisitConstantExpr(ConstantExpr *CE, QualType T) {
    return Visit(CE->getSubExpr(), T);
  }

  llvm::Constant *VisitParenExpr(ParenExpr *PE, QualType T) {
    return Visit(PE->getSubExpr(), T);
  }

  llvm::Constant *
  VisitSubstNonTypeTemplateParmExpr(SubstNonTypeTemplateParmExpr *PE,
                                    QualType T) {
    return Visit(PE->getReplacement(), T);
  }

  llvm::Constant *VisitGenericSelectionExpr(GenericSelectionExpr *GE,
                                            QualType T) {
    return Visit(GE->getResultExpr(), T);
  }

  llvm::Constant *VisitChooseExpr(ChooseExpr *CE, QualType T) {
    return Visit(CE->getChosenSubExpr(), T);
  }

  llvm::Constant *VisitCompoundLiteralExpr(CompoundLiteralExpr *E, QualType T) {
    return Visit(E->getInitializer(), T);
  }

  llvm::Constant *VisitCastExpr(CastExpr *E, QualType destType) {
    if (const auto *ECE = dyn_cast<ExplicitCastExpr>(E))
      CGM.EmitExplicitCastExprType(ECE, Emitter.CGF);
    Expr *subExpr = E->getSubExpr();

    switch (E->getCastKind()) {
    case CK_ToUnion: {
      // GCC cast to union extension
      assert(E->getType()->isUnionType() &&
             "Destination type is not union type!");

      auto field = E->getTargetUnionField();

      auto C = Emitter.tryEmitPrivateForMemory(subExpr, field->getType());
      if (!C) return nullptr;

      auto destTy = ConvertType(destType);
      if (C->getType() == destTy) return C;

      // Build a struct with the union sub-element as the first member,
      // and padded to the appropriate size.
      SmallVector<llvm::Constant*, 2> Elts;
      SmallVector<llvm::Type*, 2> Types;
      Elts.push_back(C);
      Types.push_back(C->getType());
      unsigned CurSize = CGM.getDataLayout().getTypeAllocSize(C->getType());
      unsigned TotalSize = CGM.getDataLayout().getTypeAllocSize(destTy);

      assert(CurSize <= TotalSize && "Union size mismatch!");
      if (unsigned NumPadBytes = TotalSize - CurSize) {
        llvm::Type *Ty = CGM.Int8Ty;
        if (NumPadBytes > 1)
          Ty = llvm::ArrayType::get(Ty, NumPadBytes);

        Elts.push_back(llvm::UndefValue::get(Ty));
        Types.push_back(Ty);
      }

      llvm::StructType *STy = llvm::StructType::get(VMContext, Types, false);
      return llvm::ConstantStruct::get(STy, Elts);
    }

    case CK_AddressSpaceConversion: {
      auto C = Emitter.tryEmitPrivate(subExpr, subExpr->getType());
      if (!C) return nullptr;
      LangAS destAS = E->getType()->getPointeeType().getAddressSpace();
      LangAS srcAS = subExpr->getType()->getPointeeType().getAddressSpace();
      llvm::Type *destTy = ConvertType(E->getType());
      return CGM.getTargetCodeGenInfo().performAddrSpaceCast(CGM, C, srcAS,
                                                             destAS, destTy);
    }

    case CK_LValueToRValue:
    case CK_AtomicToNonAtomic:
    case CK_NonAtomicToAtomic:
    case CK_NoOp:
    case CK_ConstructorConversion:
      return Visit(subExpr, destType);

    case CK_IntToOCLSampler:
      llvm_unreachable("global sampler variables are not generated");

    case CK_Dependent: llvm_unreachable("saw dependent cast!");

    case CK_BuiltinFnToFnPtr:
      llvm_unreachable("builtin functions are handled elsewhere");

    case CK_ReinterpretMemberPointer:
    case CK_DerivedToBaseMemberPointer:
    case CK_BaseToDerivedMemberPointer: {
      auto C = Emitter.tryEmitPrivate(subExpr, subExpr->getType());
      if (!C) return nullptr;
      return CGM.getCXXABI().EmitMemberPointerConversion(E, C);
    }

    // These will never be supported.
    case CK_ObjCObjectLValueCast:
    case CK_ARCProduceObject:
    case CK_ARCConsumeObject:
    case CK_ARCReclaimReturnedObject:
    case CK_ARCExtendBlockObject:
    case CK_CopyAndAutoreleaseBlockObject:
      return nullptr;

    // These don't need to be handled here because Evaluate knows how to
    // evaluate them in the cases where they can be folded.
    case CK_BitCast:
    case CK_ToVoid:
    case CK_Dynamic:
    case CK_LValueBitCast:
    case CK_NullToMemberPointer:
    case CK_UserDefinedConversion:
    case CK_CPointerToObjCPointerCast:
    case CK_BlockPointerToObjCPointerCast:
    case CK_AnyPointerToBlockPointerCast:
    case CK_ArrayToPointerDecay:
    case CK_FunctionToPointerDecay:
    case CK_BaseToDerived:
    case CK_DerivedToBase:
    case CK_UncheckedDerivedToBase:
    case CK_MemberPointerToBoolean:
    case CK_VectorSplat:
    case CK_FloatingRealToComplex:
    case CK_FloatingComplexToReal:
    case CK_FloatingComplexToBoolean:
    case CK_FloatingComplexCast:
    case CK_FloatingComplexToIntegralComplex:
    case CK_IntegralRealToComplex:
    case CK_IntegralComplexToReal:
    case CK_IntegralComplexToBoolean:
    case CK_IntegralComplexCast:
    case CK_IntegralComplexToFloatingComplex:
    case CK_PointerToIntegral:
    case CK_PointerToBoolean:
    case CK_NullToPointer:
    case CK_IntegralCast:
    case CK_BooleanToSignedIntegral:
    case CK_IntegralToPointer:
    case CK_IntegralToBoolean:
    case CK_IntegralToFloating:
    case CK_FloatingToIntegral:
    case CK_FloatingToBoolean:
    case CK_FloatingCast:
    case CK_FixedPointCast:
    case CK_FixedPointToBoolean:
    case CK_ZeroToOCLOpaqueType:
      return nullptr;
    }
    llvm_unreachable("Invalid CastKind");
  }

  llvm::Constant *VisitCXXDefaultArgExpr(CXXDefaultArgExpr *DAE, QualType T) {
    return Visit(DAE->getExpr(), T);
  }

  llvm::Constant *VisitCXXDefaultInitExpr(CXXDefaultInitExpr *DIE, QualType T) {
    // No need for a DefaultInitExprScope: we don't handle 'this' in a
    // constant expression.
    return Visit(DIE->getExpr(), T);
  }

  llvm::Constant *VisitExprWithCleanups(ExprWithCleanups *E, QualType T) {
    if (!E->cleanupsHaveSideEffects())
      return Visit(E->getSubExpr(), T);
    return nullptr;
  }

  llvm::Constant *VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *E,
                                                QualType T) {
    return Visit(E->GetTemporaryExpr(), T);
  }

  llvm::Constant *EmitArrayInitialization(InitListExpr *ILE, QualType T) {
    auto *CAT = CGM.getContext().getAsConstantArrayType(ILE->getType());
    assert(CAT && "can't emit array init for non-constant-bound array");
    unsigned NumInitElements = ILE->getNumInits();
    unsigned NumElements = CAT->getSize().getZExtValue();

    // Initialising an array requires us to automatically
    // initialise any elements that have not been initialised explicitly
    unsigned NumInitableElts = std::min(NumInitElements, NumElements);

    QualType EltType = CAT->getElementType();

    // Initialize remaining array elements.
    llvm::Constant *fillC = nullptr;
    if (Expr *filler = ILE->getArrayFiller()) {
      fillC = Emitter.tryEmitAbstractForMemory(filler, EltType);
      if (!fillC)
        return nullptr;
    }

    // Copy initializer elements.
    SmallVector<llvm::Constant*, 16> Elts;
    if (fillC && fillC->isNullValue())
      Elts.reserve(NumInitableElts + 1);
    else
      Elts.reserve(NumElements);

    llvm::Type *CommonElementType = nullptr;
    for (unsigned i = 0; i < NumInitableElts; ++i) {
      Expr *Init = ILE->getInit(i);
      llvm::Constant *C = Emitter.tryEmitPrivateForMemory(Init, EltType);
      if (!C)
        return nullptr;
      if (i == 0)
        CommonElementType = C->getType();
      else if (C->getType() != CommonElementType)
        CommonElementType = nullptr;
      Elts.push_back(C);
    }

    return EmitArrayConstant(CGM, CAT, CommonElementType, NumElements, Elts,
                             fillC);
  }

  llvm::Constant *EmitRecordInitialization(InitListExpr *ILE, QualType T) {
    return ConstStructBuilder::BuildStruct(Emitter, ILE, T);
  }

  llvm::Constant *VisitImplicitValueInitExpr(ImplicitValueInitExpr* E,
                                             QualType T) {
    return CGM.EmitNullConstant(T);
  }

  llvm::Constant *VisitInitListExpr(InitListExpr *ILE, QualType T) {
    if (ILE->isTransparent())
      return Visit(ILE->getInit(0), T);

    if (ILE->getType()->isArrayType())
      return EmitArrayInitialization(ILE, T);

    if (ILE->getType()->isRecordType())
      return EmitRecordInitialization(ILE, T);

    return nullptr;
  }

  llvm::Constant *EmitDesignatedInitUpdater(llvm::Constant *Base,
                                            InitListExpr *Updater,
                                            QualType destType) {
    if (auto destAT = CGM.getContext().getAsArrayType(destType)) {
      llvm::ArrayType *AType = cast<llvm::ArrayType>(ConvertType(destType));
      llvm::Type *ElemType = AType->getElementType();

      unsigned NumInitElements = Updater->getNumInits();
      unsigned NumElements = AType->getNumElements();

      std::vector<llvm::Constant *> Elts;
      Elts.reserve(NumElements);

      QualType destElemType = destAT->getElementType();

      if (auto DataArray = dyn_cast<llvm::ConstantDataArray>(Base))
        for (unsigned i = 0; i != NumElements; ++i)
          Elts.push_back(DataArray->getElementAsConstant(i));
      else if (auto Array = dyn_cast<llvm::ConstantArray>(Base))
        for (unsigned i = 0; i != NumElements; ++i)
          Elts.push_back(Array->getOperand(i));
      else
        return nullptr; // FIXME: other array types not implemented

      llvm::Constant *fillC = nullptr;
      if (Expr *filler = Updater->getArrayFiller())
        if (!isa<NoInitExpr>(filler))
          fillC = Emitter.tryEmitAbstractForMemory(filler, destElemType);
      bool RewriteType = (fillC && fillC->getType() != ElemType);

      for (unsigned i = 0; i != NumElements; ++i) {
        Expr *Init = nullptr;
        if (i < NumInitElements)
          Init = Updater->getInit(i);

        if (!Init && fillC)
          Elts[i] = fillC;
        else if (!Init || isa<NoInitExpr>(Init))
          ; // Do nothing.
        else if (InitListExpr *ChildILE = dyn_cast<InitListExpr>(Init))
          Elts[i] = EmitDesignatedInitUpdater(Elts[i], ChildILE, destElemType);
        else
          Elts[i] = Emitter.tryEmitPrivateForMemory(Init, destElemType);

       if (!Elts[i])
          return nullptr;
        RewriteType |= (Elts[i]->getType() != ElemType);
      }

      if (RewriteType) {
        std::vector<llvm::Type *> Types;
        Types.reserve(NumElements);
        for (unsigned i = 0; i != NumElements; ++i)
          Types.push_back(Elts[i]->getType());
        llvm::StructType *SType = llvm::StructType::get(AType->getContext(),
                                                        Types, true);
        return llvm::ConstantStruct::get(SType, Elts);
      }

      return llvm::ConstantArray::get(AType, Elts);
    }

    if (destType->isRecordType())
      return ConstStructBuilder::BuildStruct(Emitter, this, Base, Updater,
                                             destType);

    return nullptr;
  }

  llvm::Constant *VisitDesignatedInitUpdateExpr(DesignatedInitUpdateExpr *E,
                                                QualType destType) {
    auto C = Visit(E->getBase(), destType);
    if (!C) return nullptr;
    return EmitDesignatedInitUpdater(C, E->getUpdater(), destType);
  }

  llvm::Constant *VisitCXXConstructExpr(CXXConstructExpr *E, QualType Ty) {
    if (!E->getConstructor()->isTrivial())
      return nullptr;

    // FIXME: We should not have to call getBaseElementType here.
    const RecordType *RT =
      CGM.getContext().getBaseElementType(Ty)->getAs<RecordType>();
    const CXXRecordDecl *RD = cast<CXXRecordDecl>(RT->getDecl());

    // If the class doesn't have a trivial destructor, we can't emit it as a
    // constant expr.
    if (!RD->hasTrivialDestructor())
      return nullptr;

    // Only copy and default constructors can be trivial.


    if (E->getNumArgs()) {
      assert(E->getNumArgs() == 1 && "trivial ctor with > 1 argument");
      assert(E->getConstructor()->isCopyOrMoveConstructor() &&
             "trivial ctor has argument but isn't a copy/move ctor");

      Expr *Arg = E->getArg(0);
      assert(CGM.getContext().hasSameUnqualifiedType(Ty, Arg->getType()) &&
             "argument to copy ctor is of wrong type");

      return Visit(Arg, Ty);
    }

    return CGM.EmitNullConstant(Ty);
  }

  llvm::Constant *VisitStringLiteral(StringLiteral *E, QualType T) {
    return CGM.GetConstantArrayFromStringLiteral(E);
  }

  llvm::Constant *VisitObjCEncodeExpr(ObjCEncodeExpr *E, QualType T) {
    // This must be an @encode initializing an array in a static initializer.
    // Don't emit it as the address of the string, emit the string data itself
    // as an inline array.
    std::string Str;
    CGM.getContext().getObjCEncodingForType(E->getEncodedType(), Str);
    const ConstantArrayType *CAT = CGM.getContext().getAsConstantArrayType(T);

    // Resize the string to the right size, adding zeros at the end, or
    // truncating as needed.
    Str.resize(CAT->getSize().getZExtValue(), '\0');
    return llvm::ConstantDataArray::getString(VMContext, Str, false);
  }

  llvm::Constant *VisitUnaryExtension(const UnaryOperator *E, QualType T) {
    return Visit(E->getSubExpr(), T);
  }

  // Utility methods
  llvm::Type *ConvertType(QualType T) {
    return CGM.getTypes().ConvertType(T);
  }
};

}  // end anonymous namespace.

bool ConstStructBuilder::Build(ConstExprEmitter *ExprEmitter,
                               llvm::Constant *Base,
                               InitListExpr *Updater) {
  assert(Base && "base expression should not be empty");

  QualType ExprType = Updater->getType();
  RecordDecl *RD = ExprType->getAs<RecordType>()->getDecl();
  const ASTRecordLayout &Layout = CGM.getContext().getASTRecordLayout(RD);
  const llvm::StructLayout *BaseLayout = CGM.getDataLayout().getStructLayout(
      cast<llvm::StructType>(Base->getType()));
  unsigned FieldNo = -1;
  unsigned ElementNo = 0;

  // Bail out if we have base classes. We could support these, but they only
  // arise in C++1z where we will have already constant folded most interesting
  // cases. FIXME: There are still a few more cases we can handle this way.
  if (auto *CXXRD = dyn_cast<CXXRecordDecl>(RD))
    if (CXXRD->getNumBases())
      return false;

  for (FieldDecl *Field : RD->fields()) {
    ++FieldNo;

    if (RD->isUnion() && Updater->getInitializedFieldInUnion() != Field)
      continue;

    // Skip anonymous bitfields.
    if (Field->isUnnamedBitfield())
      continue;

    llvm::Constant *EltInit = Base->getAggregateElement(ElementNo);

    // Bail out if the type of the ConstantStruct does not have the same layout
    // as the type of the InitListExpr.
    if (CGM.getTypes().ConvertType(Field->getType()) != EltInit->getType() ||
        Layout.getFieldOffset(ElementNo) !=
          BaseLayout->getElementOffsetInBits(ElementNo))
      return false;

    // Get the initializer. If we encounter an empty field or a NoInitExpr,
    // we use values from the base expression.
    Expr *Init = nullptr;
    if (ElementNo < Updater->getNumInits())
      Init = Updater->getInit(ElementNo);

    if (!Init || isa<NoInitExpr>(Init))
      ; // Do nothing.
    else if (InitListExpr *ChildILE = dyn_cast<InitListExpr>(Init))
      EltInit = ExprEmitter->EmitDesignatedInitUpdater(EltInit, ChildILE,
                                                       Field->getType());
    else
      EltInit = Emitter.tryEmitPrivateForMemory(Init, Field->getType());

    ++ElementNo;

    if (!EltInit)
      return false;

    if (!Field->isBitField())
      AppendField(Field, Layout.getFieldOffset(FieldNo), EltInit);
    else if (llvm::ConstantInt *CI = dyn_cast<llvm::ConstantInt>(EltInit))
      AppendBitField(Field, Layout.getFieldOffset(FieldNo), CI);
    else
      // Initializing a bitfield with a non-trivial constant?
      return false;
  }

  return true;
}

llvm::Constant *ConstantEmitter::validateAndPopAbstract(llvm::Constant *C,
                                                        AbstractState saved) {
  Abstract = saved.OldValue;

  assert(saved.OldPlaceholdersSize == PlaceholderAddresses.size() &&
         "created a placeholder while doing an abstract emission?");

  // No validation necessary for now.
  // No cleanup to do for now.
  return C;
}

llvm::Constant *
ConstantEmitter::tryEmitAbstractForInitializer(const VarDecl &D) {
  auto state = pushAbstract();
  auto C = tryEmitPrivateForVarInit(D);
  return validateAndPopAbstract(C, state);
}

llvm::Constant *
ConstantEmitter::tryEmitAbstract(const Expr *E, QualType destType) {
  auto state = pushAbstract();
  auto C = tryEmitPrivate(E, destType);
  return validateAndPopAbstract(C, state);
}

llvm::Constant *
ConstantEmitter::tryEmitAbstract(const APValue &value, QualType destType) {
  auto state = pushAbstract();
  auto C = tryEmitPrivate(value, destType);
  return validateAndPopAbstract(C, state);
}

llvm::Constant *
ConstantEmitter::emitAbstract(const Expr *E, QualType destType) {
  auto state = pushAbstract();
  auto C = tryEmitPrivate(E, destType);
  C = validateAndPopAbstract(C, state);
  if (!C) {
    CGM.Error(E->getExprLoc(),
              "internal error: could not emit constant value \"abstractly\"");
    C = CGM.EmitNullConstant(destType);
  }
  return C;
}

llvm::Constant *
ConstantEmitter::emitAbstract(SourceLocation loc, const APValue &value,
                              QualType destType) {
  auto state = pushAbstract();
  auto C = tryEmitPrivate(value, destType);
  C = validateAndPopAbstract(C, state);
  if (!C) {
    CGM.Error(loc,
              "internal error: could not emit constant value \"abstractly\"");
    C = CGM.EmitNullConstant(destType);
  }
  return C;
}

llvm::Constant *ConstantEmitter::tryEmitForInitializer(const VarDecl &D) {
  initializeNonAbstract(D.getType().getAddressSpace());
  return markIfFailed(tryEmitPrivateForVarInit(D));
}

llvm::Constant *ConstantEmitter::tryEmitForInitializer(const Expr *E,
                                                       LangAS destAddrSpace,
                                                       QualType destType) {
  initializeNonAbstract(destAddrSpace);
  return markIfFailed(tryEmitPrivateForMemory(E, destType));
}

llvm::Constant *ConstantEmitter::emitForInitializer(const APValue &value,
                                                    LangAS destAddrSpace,
                                                    QualType destType) {
  initializeNonAbstract(destAddrSpace);
  auto C = tryEmitPrivateForMemory(value, destType);
  assert(C && "couldn't emit constant value non-abstractly?");
  return C;
}

llvm::GlobalValue *ConstantEmitter::getCurrentAddrPrivate() {
  assert(!Abstract && "cannot get current address for abstract constant");



  // Make an obviously ill-formed global that should blow up compilation
  // if it survives.
  auto global = new llvm::GlobalVariable(CGM.getModule(), CGM.Int8Ty, true,
                                         llvm::GlobalValue::PrivateLinkage,
                                         /*init*/ nullptr,
                                         /*name*/ "",
                                         /*before*/ nullptr,
                                         llvm::GlobalVariable::NotThreadLocal,
                                         CGM.getContext().getTargetAddressSpace(DestAddressSpace));

  PlaceholderAddresses.push_back(std::make_pair(nullptr, global));

  return global;
}

void ConstantEmitter::registerCurrentAddrPrivate(llvm::Constant *signal,
                                           llvm::GlobalValue *placeholder) {
  assert(!PlaceholderAddresses.empty());
  assert(PlaceholderAddresses.back().first == nullptr);
  assert(PlaceholderAddresses.back().second == placeholder);
  PlaceholderAddresses.back().first = signal;
}

namespace {
  struct ReplacePlaceholders {
    CodeGenModule &CGM;

    /// The base address of the global.
    llvm::Constant *Base;
    llvm::Type *BaseValueTy = nullptr;

    /// The placeholder addresses that were registered during emission.
    llvm::DenseMap<llvm::Constant*, llvm::GlobalVariable*> PlaceholderAddresses;

    /// The locations of the placeholder signals.
    llvm::DenseMap<llvm::GlobalVariable*, llvm::Constant*> Locations;

    /// The current index stack.  We use a simple unsigned stack because
    /// we assume that placeholders will be relatively sparse in the
    /// initializer, but we cache the index values we find just in case.
    llvm::SmallVector<unsigned, 8> Indices;
    llvm::SmallVector<llvm::Constant*, 8> IndexValues;

    ReplacePlaceholders(CodeGenModule &CGM, llvm::Constant *base,
                        ArrayRef<std::pair<llvm::Constant*,
                                           llvm::GlobalVariable*>> addresses)
        : CGM(CGM), Base(base),
          PlaceholderAddresses(addresses.begin(), addresses.end()) {
    }

    void replaceInInitializer(llvm::Constant *init) {
      // Remember the type of the top-most initializer.
      BaseValueTy = init->getType();

      // Initialize the stack.
      Indices.push_back(0);
      IndexValues.push_back(nullptr);

      // Recurse into the initializer.
      findLocations(init);

      // Check invariants.
      assert(IndexValues.size() == Indices.size() && "mismatch");
      assert(Indices.size() == 1 && "didn't pop all indices");

      // Do the replacement; this basically invalidates 'init'.
      assert(Locations.size() == PlaceholderAddresses.size() &&
             "missed a placeholder?");

      // We're iterating over a hashtable, so this would be a source of
      // non-determinism in compiler output *except* that we're just
      // messing around with llvm::Constant structures, which never itself
      // does anything that should be visible in compiler output.
      for (auto &entry : Locations) {
        assert(entry.first->getParent() == nullptr && "not a placeholder!");
        entry.first->replaceAllUsesWith(entry.second);
        entry.first->eraseFromParent();
      }
    }

  private:
    void findLocations(llvm::Constant *init) {
      // Recurse into aggregates.
      if (auto agg = dyn_cast<llvm::ConstantAggregate>(init)) {
        for (unsigned i = 0, e = agg->getNumOperands(); i != e; ++i) {
          Indices.push_back(i);
          IndexValues.push_back(nullptr);

          findLocations(agg->getOperand(i));

          IndexValues.pop_back();
          Indices.pop_back();
        }
        return;
      }

      // Otherwise, check for registered constants.
      while (true) {
        auto it = PlaceholderAddresses.find(init);
        if (it != PlaceholderAddresses.end()) {
          setLocation(it->second);
          break;
        }

        // Look through bitcasts or other expressions.
        if (auto expr = dyn_cast<llvm::ConstantExpr>(init)) {
          init = expr->getOperand(0);
        } else {
          break;
        }
      }
    }

    void setLocation(llvm::GlobalVariable *placeholder) {
      assert(Locations.find(placeholder) == Locations.end() &&
             "already found location for placeholder!");

      // Lazily fill in IndexValues with the values from Indices.
      // We do this in reverse because we should always have a strict
      // prefix of indices from the start.
      assert(Indices.size() == IndexValues.size());
      for (size_t i = Indices.size() - 1; i != size_t(-1); --i) {
        if (IndexValues[i]) {
#ifndef NDEBUG
          for (size_t j = 0; j != i + 1; ++j) {
            assert(IndexValues[j] &&
                   isa<llvm::ConstantInt>(IndexValues[j]) &&
                   cast<llvm::ConstantInt>(IndexValues[j])->getZExtValue()
                     == Indices[j]);
          }
#endif
          break;
        }

        IndexValues[i] = llvm::ConstantInt::get(CGM.Int32Ty, Indices[i]);
      }

      // Form a GEP and then bitcast to the placeholder type so that the
      // replacement will succeed.
      llvm::Constant *location =
        llvm::ConstantExpr::getInBoundsGetElementPtr(BaseValueTy,
                                                     Base, IndexValues);
      location = llvm::ConstantExpr::getBitCast(location,
                                                placeholder->getType());

      Locations.insert({placeholder, location});
    }
  };
}

void ConstantEmitter::finalize(llvm::GlobalVariable *global) {
  assert(InitializedNonAbstract &&
         "finalizing emitter that was used for abstract emission?");
  assert(!Finalized && "finalizing emitter multiple times");
  assert(global->getInitializer());

  // Note that we might also be Failed.
  Finalized = true;

  if (!PlaceholderAddresses.empty()) {
    ReplacePlaceholders(CGM, global, PlaceholderAddresses)
      .replaceInInitializer(global->getInitializer());
    PlaceholderAddresses.clear(); // satisfy
  }
}

ConstantEmitter::~ConstantEmitter() {
  assert((!InitializedNonAbstract || Finalized || Failed) &&
         "not finalized after being initialized for non-abstract emission");
  assert(PlaceholderAddresses.empty() && "unhandled placeholders");
}

static QualType getNonMemoryType(CodeGenModule &CGM, QualType type) {
  if (auto AT = type->getAs<AtomicType>()) {
    return CGM.getContext().getQualifiedType(AT->getValueType(),
                                             type.getQualifiers());
  }
  return type;
}

llvm::Constant *ConstantEmitter::tryEmitPrivateForVarInit(const VarDecl &D) {
  // Make a quick check if variable can be default NULL initialized
  // and avoid going through rest of code which may do, for c++11,
  // initialization of memory to all NULLs.
  if (!D.hasLocalStorage()) {
    QualType Ty = CGM.getContext().getBaseElementType(D.getType());
    if (Ty->isRecordType())
      if (const CXXConstructExpr *E =
          dyn_cast_or_null<CXXConstructExpr>(D.getInit())) {
        const CXXConstructorDecl *CD = E->getConstructor();
        if (CD->isTrivial() && CD->isDefaultConstructor())
          return CGM.EmitNullConstant(D.getType());
      }
    InConstantContext = true;
  }

  QualType destType = D.getType();

  // Try to emit the initializer.  Note that this can allow some things that
  // are not allowed by tryEmitPrivateForMemory alone.
  if (auto value = D.evaluateValue()) {
    return tryEmitPrivateForMemory(*value, destType);
  }

  // FIXME: Implement C++11 [basic.start.init]p2: if the initializer of a
  // reference is a constant expression, and the reference binds to a temporary,
  // then constant initialization is performed. ConstExprEmitter will
  // incorrectly emit a prvalue constant in this case, and the calling code
  // interprets that as the (pointer) value of the reference, rather than the
  // desired value of the referee.
  if (destType->isReferenceType())
    return nullptr;

  const Expr *E = D.getInit();
  assert(E && "No initializer to emit");

  auto nonMemoryDestType = getNonMemoryType(CGM, destType);
  auto C =
    ConstExprEmitter(*this).Visit(const_cast<Expr*>(E), nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

llvm::Constant *
ConstantEmitter::tryEmitAbstractForMemory(const Expr *E, QualType destType) {
  auto nonMemoryDestType = getNonMemoryType(CGM, destType);
  auto C = tryEmitAbstract(E, nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

llvm::Constant *
ConstantEmitter::tryEmitAbstractForMemory(const APValue &value,
                                          QualType destType) {
  auto nonMemoryDestType = getNonMemoryType(CGM, destType);
  auto C = tryEmitAbstract(value, nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

llvm::Constant *ConstantEmitter::tryEmitPrivateForMemory(const Expr *E,
                                                         QualType destType) {
  auto nonMemoryDestType = getNonMemoryType(CGM, destType);
  llvm::Constant *C = tryEmitPrivate(E, nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

llvm::Constant *ConstantEmitter::tryEmitPrivateForMemory(const APValue &value,
                                                         QualType destType) {
  auto nonMemoryDestType = getNonMemoryType(CGM, destType);
  auto C = tryEmitPrivate(value, nonMemoryDestType);
  return (C ? emitForMemory(C, destType) : nullptr);
}

llvm::Constant *ConstantEmitter::emitForMemory(CodeGenModule &CGM,
                                               llvm::Constant *C,
                                               QualType destType) {
  // For an _Atomic-qualified constant, we may need to add tail padding.
  if (auto AT = destType->getAs<AtomicType>()) {
    QualType destValueType = AT->getValueType();
    C = emitForMemory(CGM, C, destValueType);

    uint64_t innerSize = CGM.getContext().getTypeSize(destValueType);
    uint64_t outerSize = CGM.getContext().getTypeSize(destType);
    if (innerSize == outerSize)
      return C;

    assert(innerSize < outerSize && "emitted over-large constant for atomic");
    llvm::Constant *elts[] = {
      C,
      llvm::ConstantAggregateZero::get(
          llvm::ArrayType::get(CGM.Int8Ty, (outerSize - innerSize) / 8))
    };
    return llvm::ConstantStruct::getAnon(elts);
  }

  // Zero-extend bool.
  if (C->getType()->isIntegerTy(1)) {
    llvm::Type *boolTy = CGM.getTypes().ConvertTypeForMem(destType);
    return llvm::ConstantExpr::getZExt(C, boolTy);
  }

  return C;
}

llvm::Constant *ConstantEmitter::tryEmitPrivate(const Expr *E,
                                                QualType destType) {
  Expr::EvalResult Result;

  bool Success = false;

  if (destType->isReferenceType())
    Success = E->EvaluateAsLValue(Result, CGM.getContext());
  else
    Success = E->EvaluateAsRValue(Result, CGM.getContext(), InConstantContext);

  llvm::Constant *C;
  if (Success && !Result.HasSideEffects)
    C = tryEmitPrivate(Result.Val, destType);
  else
    C = ConstExprEmitter(*this).Visit(const_cast<Expr*>(E), destType);

  return C;
}

llvm::Constant *CodeGenModule::getNullPointer(llvm::PointerType *T, QualType QT) {
  return getTargetCodeGenInfo().getNullPointer(*this, T, QT);
}

namespace {
/// A struct which can be used to peephole certain kinds of finalization
/// that normally happen during l-value emission.
struct ConstantLValue {
  llvm::Constant *Value;
  bool HasOffsetApplied;

  /*implicit*/ ConstantLValue(llvm::Constant *value,
                              bool hasOffsetApplied = false)
    : Value(value), HasOffsetApplied(false) {}

  /*implicit*/ ConstantLValue(ConstantAddress address)
    : ConstantLValue(address.getPointer()) {}
};

/// A helper class for emitting constant l-values.
class ConstantLValueEmitter : public ConstStmtVisitor<ConstantLValueEmitter,
                                                      ConstantLValue> {
  CodeGenModule &CGM;
  ConstantEmitter &Emitter;
  const APValue &Value;
  QualType DestType;

  // Befriend StmtVisitorBase so that we don't have to expose Visit*.
  friend StmtVisitorBase;

public:
  ConstantLValueEmitter(ConstantEmitter &emitter, const APValue &value,
                        QualType destType)
    : CGM(emitter.CGM), Emitter(emitter), Value(value), DestType(destType) {}

  llvm::Constant *tryEmit();

private:
  llvm::Constant *tryEmitAbsolute(llvm::Type *destTy);
  ConstantLValue tryEmitBase(const APValue::LValueBase &base);

  ConstantLValue VisitStmt(const Stmt *S) { return nullptr; }
  ConstantLValue VisitConstantExpr(const ConstantExpr *E);
  ConstantLValue VisitCompoundLiteralExpr(const CompoundLiteralExpr *E);
  ConstantLValue VisitStringLiteral(const StringLiteral *E);
  ConstantLValue VisitObjCEncodeExpr(const ObjCEncodeExpr *E);
  ConstantLValue VisitObjCStringLiteral(const ObjCStringLiteral *E);
  ConstantLValue VisitPredefinedExpr(const PredefinedExpr *E);
  ConstantLValue VisitAddrLabelExpr(const AddrLabelExpr *E);
  ConstantLValue VisitCallExpr(const CallExpr *E);
  ConstantLValue VisitBlockExpr(const BlockExpr *E);
  ConstantLValue VisitCXXTypeidExpr(const CXXTypeidExpr *E);
  ConstantLValue VisitCXXUuidofExpr(const CXXUuidofExpr *E);
  ConstantLValue VisitMaterializeTemporaryExpr(
                                         const MaterializeTemporaryExpr *E);

  bool hasNonZeroOffset() const {
    return !Value.getLValueOffset().isZero();
  }

  /// Return the value offset.
  llvm::Constant *getOffset() {
    return llvm::ConstantInt::get(CGM.Int64Ty,
                                  Value.getLValueOffset().getQuantity());
  }

  /// Apply the value offset to the given constant.
  llvm::Constant *applyOffset(llvm::Constant *C) {
    if (!hasNonZeroOffset())
      return C;

    llvm::Type *origPtrTy = C->getType();
    unsigned AS = origPtrTy->getPointerAddressSpace();
    llvm::Type *charPtrTy = CGM.Int8Ty->getPointerTo(AS);
    C = llvm::ConstantExpr::getBitCast(C, charPtrTy);
    C = llvm::ConstantExpr::getGetElementPtr(CGM.Int8Ty, C, getOffset());
    C = llvm::ConstantExpr::getPointerCast(C, origPtrTy);
    return C;
  }
};

}

llvm::Constant *ConstantLValueEmitter::tryEmit() {
  const APValue::LValueBase &base = Value.getLValueBase();

  // Certain special array initializers are represented in APValue
  // as l-values referring to the base expression which generates the
  // array.  This happens with e.g. string literals.  These should
  // probably just get their own representation kind in APValue.
  if (DestType->isArrayType()) {
    assert(!hasNonZeroOffset() && "offset on array initializer");
    auto expr = const_cast<Expr*>(base.get<const Expr*>());
    return ConstExprEmitter(Emitter).Visit(expr, DestType);
  }

  // Otherwise, the destination type should be a pointer or reference
  // type, but it might also be a cast thereof.
  //
  // FIXME: the chain of casts required should be reflected in the APValue.
  // We need this in order to correctly handle things like a ptrtoint of a
  // non-zero null pointer and addrspace casts that aren't trivially
  // represented in LLVM IR.
  auto destTy = CGM.getTypes().ConvertTypeForMem(DestType);
  assert(isa<llvm::IntegerType>(destTy) || isa<llvm::PointerType>(destTy));

  // If there's no base at all, this is a null or absolute pointer,
  // possibly cast back to an integer type.
  if (!base) {
    return tryEmitAbsolute(destTy);
  }

  // Otherwise, try to emit the base.
  ConstantLValue result = tryEmitBase(base);

  // If that failed, we're done.
  llvm::Constant *value = result.Value;
  if (!value) return nullptr;

  // Apply the offset if necessary and not already done.
  if (!result.HasOffsetApplied) {
    value = applyOffset(value);
  }

  // Convert to the appropriate type; this could be an lvalue for
  // an integer.  FIXME: performAddrSpaceCast
  if (isa<llvm::PointerType>(destTy))
    return llvm::ConstantExpr::getPointerCast(value, destTy);

  return llvm::ConstantExpr::getPtrToInt(value, destTy);
}

/// Try to emit an absolute l-value, such as a null pointer or an integer
/// bitcast to pointer type.
llvm::Constant *
ConstantLValueEmitter::tryEmitAbsolute(llvm::Type *destTy) {
  auto offset = getOffset();

  // If we're producing a pointer, this is easy.
  if (auto destPtrTy = cast<llvm::PointerType>(destTy)) {
    if (Value.isNullPointer()) {
      // FIXME: integer offsets from non-zero null pointers.
      return CGM.getNullPointer(destPtrTy, DestType);
    }

    // Convert the integer to a pointer-sized integer before converting it
    // to a pointer.
    // FIXME: signedness depends on the original integer type.
    auto intptrTy = CGM.getDataLayout().getIntPtrType(destPtrTy);
    llvm::Constant *C = offset;
    C = llvm::ConstantExpr::getIntegerCast(getOffset(), intptrTy,
                                           /*isSigned*/ false);
    C = llvm::ConstantExpr::getIntToPtr(C, destPtrTy);
    return C;
  }

  // Otherwise, we're basically returning an integer constant.

  // FIXME: this does the wrong thing with ptrtoint of a null pointer,
  // but since we don't know the original pointer type, there's not much
  // we can do about it.

  auto C = getOffset();
  C = llvm::ConstantExpr::getIntegerCast(C, destTy, /*isSigned*/ false);
  return C;
}

ConstantLValue
ConstantLValueEmitter::tryEmitBase(const APValue::LValueBase &base) {
  // Handle values.
  if (const ValueDecl *D = base.dyn_cast<const ValueDecl*>()) {
    if (D->hasAttr<WeakRefAttr>())
      return CGM.GetWeakRefReference(D).getPointer();

    if (auto FD = dyn_cast<FunctionDecl>(D))
      return CGM.GetAddrOfFunction(FD);

    if (auto VD = dyn_cast<VarDecl>(D)) {
      // We can never refer to a variable with local storage.
      if (!VD->hasLocalStorage()) {
        if (VD->isFileVarDecl() || VD->hasExternalStorage())
          return CGM.GetAddrOfGlobalVar(VD);

        if (VD->isLocalVarDecl()) {
          return CGM.getOrCreateStaticVarDecl(
              *VD, CGM.getLLVMLinkageVarDefinition(VD, /*isConstant=*/false));
        }
      }
    }

    return nullptr;
  }

  // Otherwise, it must be an expression.
  return Visit(base.get<const Expr*>());
}

ConstantLValue
ConstantLValueEmitter::VisitConstantExpr(const ConstantExpr *E) {
  return Visit(E->getSubExpr());
}

ConstantLValue
ConstantLValueEmitter::VisitCompoundLiteralExpr(const CompoundLiteralExpr *E) {
  return tryEmitGlobalCompoundLiteral(CGM, Emitter.CGF, E);
}

ConstantLValue
ConstantLValueEmitter::VisitStringLiteral(const StringLiteral *E) {
  return CGM.GetAddrOfConstantStringFromLiteral(E);
}

ConstantLValue
ConstantLValueEmitter::VisitObjCEncodeExpr(const ObjCEncodeExpr *E) {
  return CGM.GetAddrOfConstantStringFromObjCEncode(E);
}

ConstantLValue
ConstantLValueEmitter::VisitObjCStringLiteral(const ObjCStringLiteral *E) {
  auto C = CGM.getObjCRuntime().GenerateConstantString(E->getString());
  return C.getElementBitCast(CGM.getTypes().ConvertTypeForMem(E->getType()));
}

ConstantLValue
ConstantLValueEmitter::VisitPredefinedExpr(const PredefinedExpr *E) {
  if (auto CGF = Emitter.CGF) {
    LValue Res = CGF->EmitPredefinedLValue(E);
    return cast<ConstantAddress>(Res.getAddress());
  }

  auto kind = E->getIdentKind();
  if (kind == PredefinedExpr::PrettyFunction) {
    return CGM.GetAddrOfConstantCString("top level", ".tmp");
  }

  return CGM.GetAddrOfConstantCString("", ".tmp");
}

ConstantLValue
ConstantLValueEmitter::VisitAddrLabelExpr(const AddrLabelExpr *E) {
  assert(Emitter.CGF && "Invalid address of label expression outside function");
  llvm::Constant *Ptr = Emitter.CGF->GetAddrOfLabel(E->getLabel());
  Ptr = llvm::ConstantExpr::getBitCast(Ptr,
                                   CGM.getTypes().ConvertType(E->getType()));
  return Ptr;
}

ConstantLValue
ConstantLValueEmitter::VisitCallExpr(const CallExpr *E) {
  unsigned builtin = E->getBuiltinCallee();
  if (builtin != Builtin::BI__builtin___CFStringMakeConstantString &&
      builtin != Builtin::BI__builtin___NSStringMakeConstantString)
    return nullptr;

  auto literal = cast<StringLiteral>(E->getArg(0)->IgnoreParenCasts());
  if (builtin == Builtin::BI__builtin___NSStringMakeConstantString) {
    return CGM.getObjCRuntime().GenerateConstantString(literal);
  } else {
    // FIXME: need to deal with UCN conversion issues.
    return CGM.GetAddrOfConstantCFString(literal);
  }
}

ConstantLValue
ConstantLValueEmitter::VisitBlockExpr(const BlockExpr *E) {
  StringRef functionName;
  if (auto CGF = Emitter.CGF)
    functionName = CGF->CurFn->getName();
  else
    functionName = "global";

  return CGM.GetAddrOfGlobalBlock(E, functionName);
}

ConstantLValue
ConstantLValueEmitter::VisitCXXTypeidExpr(const CXXTypeidExpr *E) {
  QualType T;
  if (E->isTypeOperand())
    T = E->getTypeOperand(CGM.getContext());
  else
    T = E->getExprOperand()->getType();
  return CGM.GetAddrOfRTTIDescriptor(T);
}

ConstantLValue
ConstantLValueEmitter::VisitCXXUuidofExpr(const CXXUuidofExpr *E) {
  return CGM.GetAddrOfUuidDescriptor(E);
}

ConstantLValue
ConstantLValueEmitter::VisitMaterializeTemporaryExpr(
                                            const MaterializeTemporaryExpr *E) {
  assert(E->getStorageDuration() == SD_Static);
  SmallVector<const Expr *, 2> CommaLHSs;
  SmallVector<SubobjectAdjustment, 2> Adjustments;
  const Expr *Inner = E->GetTemporaryExpr()
      ->skipRValueSubobjectAdjustments(CommaLHSs, Adjustments);
  return CGM.GetAddrOfGlobalTemporary(E, Inner);
}

llvm::Constant *ConstantEmitter::tryEmitPrivate(const APValue &Value,
                                                QualType DestType) {
  switch (Value.getKind()) {
  case APValue::Uninitialized:
    llvm_unreachable("Constant expressions should be initialized.");
  case APValue::LValue:
    return ConstantLValueEmitter(*this, Value, DestType).tryEmit();
  case APValue::Int:
    return llvm::ConstantInt::get(CGM.getLLVMContext(), Value.getInt());
  case APValue::ComplexInt: {
    llvm::Constant *Complex[2];

    Complex[0] = llvm::ConstantInt::get(CGM.getLLVMContext(),
                                        Value.getComplexIntReal());
    Complex[1] = llvm::ConstantInt::get(CGM.getLLVMContext(),
                                        Value.getComplexIntImag());

    // FIXME: the target may want to specify that this is packed.
    llvm::StructType *STy =
        llvm::StructType::get(Complex[0]->getType(), Complex[1]->getType());
    return llvm::ConstantStruct::get(STy, Complex);
  }
  case APValue::Float: {
    const llvm::APFloat &Init = Value.getFloat();
    if (&Init.getSemantics() == &llvm::APFloat::IEEEhalf() &&
        !CGM.getContext().getLangOpts().NativeHalfType &&
        CGM.getContext().getTargetInfo().useFP16ConversionIntrinsics())
      return llvm::ConstantInt::get(CGM.getLLVMContext(),
                                    Init.bitcastToAPInt());
    else
      return llvm::ConstantFP::get(CGM.getLLVMContext(), Init);
  }
  case APValue::ComplexFloat: {
    llvm::Constant *Complex[2];

    Complex[0] = llvm::ConstantFP::get(CGM.getLLVMContext(),
                                       Value.getComplexFloatReal());
    Complex[1] = llvm::ConstantFP::get(CGM.getLLVMContext(),
                                       Value.getComplexFloatImag());

    // FIXME: the target may want to specify that this is packed.
    llvm::StructType *STy =
        llvm::StructType::get(Complex[0]->getType(), Complex[1]->getType());
    return llvm::ConstantStruct::get(STy, Complex);
  }
  case APValue::Vector: {
    unsigned NumElts = Value.getVectorLength();
    SmallVector<llvm::Constant *, 4> Inits(NumElts);

    for (unsigned I = 0; I != NumElts; ++I) {
      const APValue &Elt = Value.getVectorElt(I);
      if (Elt.isInt())
        Inits[I] = llvm::ConstantInt::get(CGM.getLLVMContext(), Elt.getInt());
      else if (Elt.isFloat())
        Inits[I] = llvm::ConstantFP::get(CGM.getLLVMContext(), Elt.getFloat());
      else
        llvm_unreachable("unsupported vector element type");
    }
    return llvm::ConstantVector::get(Inits);
  }
  case APValue::AddrLabelDiff: {
    const AddrLabelExpr *LHSExpr = Value.getAddrLabelDiffLHS();
    const AddrLabelExpr *RHSExpr = Value.getAddrLabelDiffRHS();
    llvm::Constant *LHS = tryEmitPrivate(LHSExpr, LHSExpr->getType());
    llvm::Constant *RHS = tryEmitPrivate(RHSExpr, RHSExpr->getType());
    if (!LHS || !RHS) return nullptr;

    // Compute difference
    llvm::Type *ResultType = CGM.getTypes().ConvertType(DestType);
    LHS = llvm::ConstantExpr::getPtrToInt(LHS, CGM.IntPtrTy);
    RHS = llvm::ConstantExpr::getPtrToInt(RHS, CGM.IntPtrTy);
    llvm::Constant *AddrLabelDiff = llvm::ConstantExpr::getSub(LHS, RHS);

    // LLVM is a bit sensitive about the exact format of the
    // address-of-label difference; make sure to truncate after
    // the subtraction.
    return llvm::ConstantExpr::getTruncOrBitCast(AddrLabelDiff, ResultType);
  }
  case APValue::Struct:
  case APValue::Union:
    return ConstStructBuilder::BuildStruct(*this, Value, DestType);
  case APValue::Array: {
    const ConstantArrayType *CAT =
        CGM.getContext().getAsConstantArrayType(DestType);
    unsigned NumElements = Value.getArraySize();
    unsigned NumInitElts = Value.getArrayInitializedElts();

    // Emit array filler, if there is one.
    llvm::Constant *Filler = nullptr;
    if (Value.hasArrayFiller()) {
      Filler = tryEmitAbstractForMemory(Value.getArrayFiller(),
                                        CAT->getElementType());
      if (!Filler)
        return nullptr;
    }

    // Emit initializer elements.
    SmallVector<llvm::Constant*, 16> Elts;
    if (Filler && Filler->isNullValue())
      Elts.reserve(NumInitElts + 1);
    else
      Elts.reserve(NumElements);

    llvm::Type *CommonElementType = nullptr;
    for (unsigned I = 0; I < NumInitElts; ++I) {
      llvm::Constant *C = tryEmitPrivateForMemory(
          Value.getArrayInitializedElt(I), CAT->getElementType());
      if (!C) return nullptr;

      if (I == 0)
        CommonElementType = C->getType();
      else if (C->getType() != CommonElementType)
        CommonElementType = nullptr;
      Elts.push_back(C);
    }

    // This means that the array type is probably "IncompleteType" or some
    // type that is not ConstantArray.
    if (CAT == nullptr && CommonElementType == nullptr && !NumInitElts) {
      const ArrayType *AT = CGM.getContext().getAsArrayType(DestType);
      CommonElementType = CGM.getTypes().ConvertType(AT->getElementType());
      llvm::ArrayType *AType = llvm::ArrayType::get(CommonElementType,
                                                    NumElements);
      return llvm::ConstantAggregateZero::get(AType);
    }

    return EmitArrayConstant(CGM, CAT, CommonElementType, NumElements, Elts,
                             Filler);
  }
  case APValue::MemberPointer:
    return CGM.getCXXABI().EmitMemberPointer(Value, DestType);
  }
  llvm_unreachable("Unknown APValue kind");
}

llvm::GlobalVariable *CodeGenModule::getAddrOfConstantCompoundLiteralIfEmitted(
    const CompoundLiteralExpr *E) {
  return EmittedCompoundLiterals.lookup(E);
}

void CodeGenModule::setAddrOfConstantCompoundLiteral(
    const CompoundLiteralExpr *CLE, llvm::GlobalVariable *GV) {
  bool Ok = EmittedCompoundLiterals.insert(std::make_pair(CLE, GV)).second;
  (void)Ok;
  assert(Ok && "CLE has already been emitted!");
}

ConstantAddress
CodeGenModule::GetAddrOfConstantCompoundLiteral(const CompoundLiteralExpr *E) {
  assert(E->isFileScope() && "not a file-scope compound literal expr");
  return tryEmitGlobalCompoundLiteral(*this, nullptr, E);
}

llvm::Constant *
CodeGenModule::getMemberPointerConstant(const UnaryOperator *uo) {
  // Member pointer constants always have a very particular form.
  const MemberPointerType *type = cast<MemberPointerType>(uo->getType());
  const ValueDecl *decl = cast<DeclRefExpr>(uo->getSubExpr())->getDecl();

  // A member function pointer.
  if (const CXXMethodDecl *method = dyn_cast<CXXMethodDecl>(decl))
    return getCXXABI().EmitMemberFunctionPointer(method);

  // Otherwise, a member data pointer.
  uint64_t fieldOffset = getContext().getFieldOffset(decl);
  CharUnits chars = getContext().toCharUnitsFromBits((int64_t) fieldOffset);
  return getCXXABI().EmitMemberDataPointer(type, chars);
}

static llvm::Constant *EmitNullConstantForBase(CodeGenModule &CGM,
                                               llvm::Type *baseType,
                                               const CXXRecordDecl *base);

static llvm::Constant *EmitNullConstant(CodeGenModule &CGM,
                                        const RecordDecl *record,
                                        bool asCompleteObject) {
  const CGRecordLayout &layout = CGM.getTypes().getCGRecordLayout(record);
  llvm::StructType *structure =
    (asCompleteObject ? layout.getLLVMType()
                      : layout.getBaseSubobjectLLVMType());

  unsigned numElements = structure->getNumElements();
  std::vector<llvm::Constant *> elements(numElements);

  auto CXXR = dyn_cast<CXXRecordDecl>(record);
  // Fill in all the bases.
  if (CXXR) {
    for (const auto &I : CXXR->bases()) {
      if (I.isVirtual()) {
        // Ignore virtual bases; if we're laying out for a complete
        // object, we'll lay these out later.
        continue;
      }

      const CXXRecordDecl *base =
        cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());

      // Ignore empty bases.
      if (base->isEmpty() ||
          CGM.getContext().getASTRecordLayout(base).getNonVirtualSize()
              .isZero())
        continue;

      unsigned fieldIndex = layout.getNonVirtualBaseLLVMFieldNo(base);
      llvm::Type *baseType = structure->getElementType(fieldIndex);
      elements[fieldIndex] = EmitNullConstantForBase(CGM, baseType, base);
    }
  }

  // Fill in all the fields.
  for (const auto *Field : record->fields()) {
    // Fill in non-bitfields. (Bitfields always use a zero pattern, which we
    // will fill in later.)
    if (!Field->isBitField()) {
      unsigned fieldIndex = layout.getLLVMFieldNo(Field);
      elements[fieldIndex] = CGM.EmitNullConstant(Field->getType());
    }

    // For unions, stop after the first named field.
    if (record->isUnion()) {
      if (Field->getIdentifier())
        break;
      if (const auto *FieldRD = Field->getType()->getAsRecordDecl())
        if (FieldRD->findFirstNamedDataMember())
          break;
    }
  }

  // Fill in the virtual bases, if we're working with the complete object.
  if (CXXR && asCompleteObject) {
    for (const auto &I : CXXR->vbases()) {
      const CXXRecordDecl *base =
        cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());

      // Ignore empty bases.
      if (base->isEmpty())
        continue;

      unsigned fieldIndex = layout.getVirtualBaseIndex(base);

      // We might have already laid this field out.
      if (elements[fieldIndex]) continue;

      llvm::Type *baseType = structure->getElementType(fieldIndex);
      elements[fieldIndex] = EmitNullConstantForBase(CGM, baseType, base);
    }
  }

  // Now go through all other fields and zero them out.
  for (unsigned i = 0; i != numElements; ++i) {
    if (!elements[i])
      elements[i] = llvm::Constant::getNullValue(structure->getElementType(i));
  }

  return llvm::ConstantStruct::get(structure, elements);
}

/// Emit the null constant for a base subobject.
static llvm::Constant *EmitNullConstantForBase(CodeGenModule &CGM,
                                               llvm::Type *baseType,
                                               const CXXRecordDecl *base) {
  const CGRecordLayout &baseLayout = CGM.getTypes().getCGRecordLayout(base);

  // Just zero out bases that don't have any pointer to data members.
  if (baseLayout.isZeroInitializableAsBase())
    return llvm::Constant::getNullValue(baseType);

  // Otherwise, we can just use its null constant.
  return EmitNullConstant(CGM, base, /*asCompleteObject=*/false);
}

llvm::Constant *ConstantEmitter::emitNullForMemory(CodeGenModule &CGM,
                                                   QualType T) {
  return emitForMemory(CGM, CGM.EmitNullConstant(T), T);
}

llvm::Constant *CodeGenModule::EmitNullConstant(QualType T) {
  if (T->getAs<PointerType>())
    return getNullPointer(
        cast<llvm::PointerType>(getTypes().ConvertTypeForMem(T)), T);

  if (getTypes().isZeroInitializable(T))
    return llvm::Constant::getNullValue(getTypes().ConvertTypeForMem(T));

  if (const ConstantArrayType *CAT = Context.getAsConstantArrayType(T)) {
    llvm::ArrayType *ATy =
      cast<llvm::ArrayType>(getTypes().ConvertTypeForMem(T));

    QualType ElementTy = CAT->getElementType();

    llvm::Constant *Element =
      ConstantEmitter::emitNullForMemory(*this, ElementTy);
    unsigned NumElements = CAT->getSize().getZExtValue();
    SmallVector<llvm::Constant *, 8> Array(NumElements, Element);
    return llvm::ConstantArray::get(ATy, Array);
  }

  if (const RecordType *RT = T->getAs<RecordType>())
    return ::EmitNullConstant(*this, RT->getDecl(), /*complete object*/ true);

  assert(T->isMemberDataPointerType() &&
         "Should only see pointers to data members here!");

  return getCXXABI().EmitNullMemberPointer(T->castAs<MemberPointerType>());
}

llvm::Constant *
CodeGenModule::EmitNullConstantForBase(const CXXRecordDecl *Record) {
  return ::EmitNullConstant(*this, Record, false);
}
