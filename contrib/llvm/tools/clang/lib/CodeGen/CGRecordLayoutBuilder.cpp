//===--- CGRecordLayoutBuilder.cpp - CGRecordLayout builder  ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Builder implementation for CGRecordLayout objects.
//
//===----------------------------------------------------------------------===//

#include "CGRecordLayout.h"
#include "CGCXXABI.h"
#include "CodeGenTypes.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/CodeGenOptions.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;
using namespace CodeGen;

namespace {
/// The CGRecordLowering is responsible for lowering an ASTRecordLayout to an
/// llvm::Type.  Some of the lowering is straightforward, some is not.  Here we
/// detail some of the complexities and weirdnesses here.
/// * LLVM does not have unions - Unions can, in theory be represented by any
///   llvm::Type with correct size.  We choose a field via a specific heuristic
///   and add padding if necessary.
/// * LLVM does not have bitfields - Bitfields are collected into contiguous
///   runs and allocated as a single storage type for the run.  ASTRecordLayout
///   contains enough information to determine where the runs break.  Microsoft
///   and Itanium follow different rules and use different codepaths.
/// * It is desired that, when possible, bitfields use the appropriate iN type
///   when lowered to llvm types.  For example unsigned x : 24 gets lowered to
///   i24.  This isn't always possible because i24 has storage size of 32 bit
///   and if it is possible to use that extra byte of padding we must use
///   [i8 x 3] instead of i24.  The function clipTailPadding does this.
///   C++ examples that require clipping:
///   struct { int a : 24; char b; }; // a must be clipped, b goes at offset 3
///   struct A { int a : 24; }; // a must be clipped because a struct like B
//    could exist: struct B : A { char b; }; // b goes at offset 3
/// * Clang ignores 0 sized bitfields and 0 sized bases but *not* zero sized
///   fields.  The existing asserts suggest that LLVM assumes that *every* field
///   has an underlying storage type.  Therefore empty structures containing
///   zero sized subobjects such as empty records or zero sized arrays still get
///   a zero sized (empty struct) storage type.
/// * Clang reads the complete type rather than the base type when generating
///   code to access fields.  Bitfields in tail position with tail padding may
///   be clipped in the base class but not the complete class (we may discover
///   that the tail padding is not used in the complete class.) However,
///   because LLVM reads from the complete type it can generate incorrect code
///   if we do not clip the tail padding off of the bitfield in the complete
///   layout.  This introduces a somewhat awkward extra unnecessary clip stage.
///   The location of the clip is stored internally as a sentinel of type
///   SCISSOR.  If LLVM were updated to read base types (which it probably
///   should because locations of things such as VBases are bogus in the llvm
///   type anyway) then we could eliminate the SCISSOR.
/// * Itanium allows nearly empty primary virtual bases.  These bases don't get
///   get their own storage because they're laid out as part of another base
///   or at the beginning of the structure.  Determining if a VBase actually
///   gets storage awkwardly involves a walk of all bases.
/// * VFPtrs and VBPtrs do *not* make a record NotZeroInitializable.
struct CGRecordLowering {
  // MemberInfo is a helper structure that contains information about a record
  // member.  In additional to the standard member types, there exists a
  // sentinel member type that ensures correct rounding.
  struct MemberInfo {
    CharUnits Offset;
    enum InfoKind { VFPtr, VBPtr, Field, Base, VBase, Scissor } Kind;
    llvm::Type *Data;
    union {
      const FieldDecl *FD;
      const CXXRecordDecl *RD;
    };
    MemberInfo(CharUnits Offset, InfoKind Kind, llvm::Type *Data,
               const FieldDecl *FD = nullptr)
      : Offset(Offset), Kind(Kind), Data(Data), FD(FD) {}
    MemberInfo(CharUnits Offset, InfoKind Kind, llvm::Type *Data,
               const CXXRecordDecl *RD)
      : Offset(Offset), Kind(Kind), Data(Data), RD(RD) {}
    // MemberInfos are sorted so we define a < operator.
    bool operator <(const MemberInfo& a) const { return Offset < a.Offset; }
  };
  // The constructor.
  CGRecordLowering(CodeGenTypes &Types, const RecordDecl *D, bool Packed);
  // Short helper routines.
  /// Constructs a MemberInfo instance from an offset and llvm::Type *.
  MemberInfo StorageInfo(CharUnits Offset, llvm::Type *Data) {
    return MemberInfo(Offset, MemberInfo::Field, Data);
  }

  /// The Microsoft bitfield layout rule allocates discrete storage
  /// units of the field's formal type and only combines adjacent
  /// fields of the same formal type.  We want to emit a layout with
  /// these discrete storage units instead of combining them into a
  /// continuous run.
  bool isDiscreteBitFieldABI() {
    return Context.getTargetInfo().getCXXABI().isMicrosoft() ||
           D->isMsStruct(Context);
  }

  /// The Itanium base layout rule allows virtual bases to overlap
  /// other bases, which complicates layout in specific ways.
  ///
  /// Note specifically that the ms_struct attribute doesn't change this.
  bool isOverlappingVBaseABI() {
    return !Context.getTargetInfo().getCXXABI().isMicrosoft();
  }

  /// Wraps llvm::Type::getIntNTy with some implicit arguments.
  llvm::Type *getIntNType(uint64_t NumBits) {
    return llvm::Type::getIntNTy(Types.getLLVMContext(),
                                 (unsigned)llvm::alignTo(NumBits, 8));
  }
  /// Gets an llvm type of size NumBytes and alignment 1.
  llvm::Type *getByteArrayType(CharUnits NumBytes) {
    assert(!NumBytes.isZero() && "Empty byte arrays aren't allowed.");
    llvm::Type *Type = llvm::Type::getInt8Ty(Types.getLLVMContext());
    return NumBytes == CharUnits::One() ? Type :
        (llvm::Type *)llvm::ArrayType::get(Type, NumBytes.getQuantity());
  }
  /// Gets the storage type for a field decl and handles storage
  /// for itanium bitfields that are smaller than their declared type.
  llvm::Type *getStorageType(const FieldDecl *FD) {
    llvm::Type *Type = Types.ConvertTypeForMem(FD->getType());
    if (!FD->isBitField()) return Type;
    if (isDiscreteBitFieldABI()) return Type;
    return getIntNType(std::min(FD->getBitWidthValue(Context),
                             (unsigned)Context.toBits(getSize(Type))));
  }
  /// Gets the llvm Basesubobject type from a CXXRecordDecl.
  llvm::Type *getStorageType(const CXXRecordDecl *RD) {
    return Types.getCGRecordLayout(RD).getBaseSubobjectLLVMType();
  }
  CharUnits bitsToCharUnits(uint64_t BitOffset) {
    return Context.toCharUnitsFromBits(BitOffset);
  }
  CharUnits getSize(llvm::Type *Type) {
    return CharUnits::fromQuantity(DataLayout.getTypeAllocSize(Type));
  }
  CharUnits getAlignment(llvm::Type *Type) {
    return CharUnits::fromQuantity(DataLayout.getABITypeAlignment(Type));
  }
  bool isZeroInitializable(const FieldDecl *FD) {
    return Types.isZeroInitializable(FD->getType());
  }
  bool isZeroInitializable(const RecordDecl *RD) {
    return Types.isZeroInitializable(RD);
  }
  void appendPaddingBytes(CharUnits Size) {
    if (!Size.isZero())
      FieldTypes.push_back(getByteArrayType(Size));
  }
  uint64_t getFieldBitOffset(const FieldDecl *FD) {
    return Layout.getFieldOffset(FD->getFieldIndex());
  }
  // Layout routines.
  void setBitFieldInfo(const FieldDecl *FD, CharUnits StartOffset,
                       llvm::Type *StorageType);
  /// Lowers an ASTRecordLayout to a llvm type.
  void lower(bool NonVirtualBaseType);
  void lowerUnion();
  void accumulateFields();
  void accumulateBitFields(RecordDecl::field_iterator Field,
                        RecordDecl::field_iterator FieldEnd);
  void accumulateBases();
  void accumulateVPtrs();
  void accumulateVBases();
  /// Recursively searches all of the bases to find out if a vbase is
  /// not the primary vbase of some base class.
  bool hasOwnStorage(const CXXRecordDecl *Decl, const CXXRecordDecl *Query);
  void calculateZeroInit();
  /// Lowers bitfield storage types to I8 arrays for bitfields with tail
  /// padding that is or can potentially be used.
  void clipTailPadding();
  /// Determines if we need a packed llvm struct.
  void determinePacked(bool NVBaseType);
  /// Inserts padding everywhere it's needed.
  void insertPadding();
  /// Fills out the structures that are ultimately consumed.
  void fillOutputFields();
  // Input memoization fields.
  CodeGenTypes &Types;
  const ASTContext &Context;
  const RecordDecl *D;
  const CXXRecordDecl *RD;
  const ASTRecordLayout &Layout;
  const llvm::DataLayout &DataLayout;
  // Helpful intermediate data-structures.
  std::vector<MemberInfo> Members;
  // Output fields, consumed by CodeGenTypes::ComputeRecordLayout.
  SmallVector<llvm::Type *, 16> FieldTypes;
  llvm::DenseMap<const FieldDecl *, unsigned> Fields;
  llvm::DenseMap<const FieldDecl *, CGBitFieldInfo> BitFields;
  llvm::DenseMap<const CXXRecordDecl *, unsigned> NonVirtualBases;
  llvm::DenseMap<const CXXRecordDecl *, unsigned> VirtualBases;
  bool IsZeroInitializable : 1;
  bool IsZeroInitializableAsBase : 1;
  bool Packed : 1;
private:
  CGRecordLowering(const CGRecordLowering &) = delete;
  void operator =(const CGRecordLowering &) = delete;
};
} // namespace {

CGRecordLowering::CGRecordLowering(CodeGenTypes &Types, const RecordDecl *D,
                                   bool Packed)
    : Types(Types), Context(Types.getContext()), D(D),
      RD(dyn_cast<CXXRecordDecl>(D)),
      Layout(Types.getContext().getASTRecordLayout(D)),
      DataLayout(Types.getDataLayout()), IsZeroInitializable(true),
      IsZeroInitializableAsBase(true), Packed(Packed) {}

void CGRecordLowering::setBitFieldInfo(
    const FieldDecl *FD, CharUnits StartOffset, llvm::Type *StorageType) {
  CGBitFieldInfo &Info = BitFields[FD->getCanonicalDecl()];
  Info.IsSigned = FD->getType()->isSignedIntegerOrEnumerationType();
  Info.Offset = (unsigned)(getFieldBitOffset(FD) - Context.toBits(StartOffset));
  Info.Size = FD->getBitWidthValue(Context);
  Info.StorageSize = (unsigned)DataLayout.getTypeAllocSizeInBits(StorageType);
  Info.StorageOffset = StartOffset;
  if (Info.Size > Info.StorageSize)
    Info.Size = Info.StorageSize;
  // Reverse the bit offsets for big endian machines. Because we represent
  // a bitfield as a single large integer load, we can imagine the bits
  // counting from the most-significant-bit instead of the
  // least-significant-bit.
  if (DataLayout.isBigEndian())
    Info.Offset = Info.StorageSize - (Info.Offset + Info.Size);
}

void CGRecordLowering::lower(bool NVBaseType) {
  // The lowering process implemented in this function takes a variety of
  // carefully ordered phases.
  // 1) Store all members (fields and bases) in a list and sort them by offset.
  // 2) Add a 1-byte capstone member at the Size of the structure.
  // 3) Clip bitfield storages members if their tail padding is or might be
  //    used by another field or base.  The clipping process uses the capstone
  //    by treating it as another object that occurs after the record.
  // 4) Determine if the llvm-struct requires packing.  It's important that this
  //    phase occur after clipping, because clipping changes the llvm type.
  //    This phase reads the offset of the capstone when determining packedness
  //    and updates the alignment of the capstone to be equal of the alignment
  //    of the record after doing so.
  // 5) Insert padding everywhere it is needed.  This phase requires 'Packed' to
  //    have been computed and needs to know the alignment of the record in
  //    order to understand if explicit tail padding is needed.
  // 6) Remove the capstone, we don't need it anymore.
  // 7) Determine if this record can be zero-initialized.  This phase could have
  //    been placed anywhere after phase 1.
  // 8) Format the complete list of members in a way that can be consumed by
  //    CodeGenTypes::ComputeRecordLayout.
  CharUnits Size = NVBaseType ? Layout.getNonVirtualSize() : Layout.getSize();
  if (D->isUnion())
    return lowerUnion();
  accumulateFields();
  // RD implies C++.
  if (RD) {
    accumulateVPtrs();
    accumulateBases();
    if (Members.empty())
      return appendPaddingBytes(Size);
    if (!NVBaseType)
      accumulateVBases();
  }
  std::stable_sort(Members.begin(), Members.end());
  Members.push_back(StorageInfo(Size, getIntNType(8)));
  clipTailPadding();
  determinePacked(NVBaseType);
  insertPadding();
  Members.pop_back();
  calculateZeroInit();
  fillOutputFields();
}

void CGRecordLowering::lowerUnion() {
  CharUnits LayoutSize = Layout.getSize();
  llvm::Type *StorageType = nullptr;
  bool SeenNamedMember = false;
  // Iterate through the fields setting bitFieldInfo and the Fields array. Also
  // locate the "most appropriate" storage type.  The heuristic for finding the
  // storage type isn't necessary, the first (non-0-length-bitfield) field's
  // type would work fine and be simpler but would be different than what we've
  // been doing and cause lit tests to change.
  for (const auto *Field : D->fields()) {
    if (Field->isBitField()) {
      if (Field->isZeroLengthBitField(Context))
        continue;
      llvm::Type *FieldType = getStorageType(Field);
      if (LayoutSize < getSize(FieldType))
        FieldType = getByteArrayType(LayoutSize);
      setBitFieldInfo(Field, CharUnits::Zero(), FieldType);
    }
    Fields[Field->getCanonicalDecl()] = 0;
    llvm::Type *FieldType = getStorageType(Field);
    // Compute zero-initializable status.
    // This union might not be zero initialized: it may contain a pointer to
    // data member which might have some exotic initialization sequence.
    // If this is the case, then we aught not to try and come up with a "better"
    // type, it might not be very easy to come up with a Constant which
    // correctly initializes it.
    if (!SeenNamedMember) {
      SeenNamedMember = Field->getIdentifier();
      if (!SeenNamedMember)
        if (const auto *FieldRD = Field->getType()->getAsRecordDecl())
          SeenNamedMember = FieldRD->findFirstNamedDataMember();
      if (SeenNamedMember && !isZeroInitializable(Field)) {
        IsZeroInitializable = IsZeroInitializableAsBase = false;
        StorageType = FieldType;
      }
    }
    // Because our union isn't zero initializable, we won't be getting a better
    // storage type.
    if (!IsZeroInitializable)
      continue;
    // Conditionally update our storage type if we've got a new "better" one.
    if (!StorageType ||
        getAlignment(FieldType) >  getAlignment(StorageType) ||
        (getAlignment(FieldType) == getAlignment(StorageType) &&
        getSize(FieldType) > getSize(StorageType)))
      StorageType = FieldType;
  }
  // If we have no storage type just pad to the appropriate size and return.
  if (!StorageType)
    return appendPaddingBytes(LayoutSize);
  // If our storage size was bigger than our required size (can happen in the
  // case of packed bitfields on Itanium) then just use an I8 array.
  if (LayoutSize < getSize(StorageType))
    StorageType = getByteArrayType(LayoutSize);
  FieldTypes.push_back(StorageType);
  appendPaddingBytes(LayoutSize - getSize(StorageType));
  // Set packed if we need it.
  if (LayoutSize % getAlignment(StorageType))
    Packed = true;
}

void CGRecordLowering::accumulateFields() {
  for (RecordDecl::field_iterator Field = D->field_begin(),
                                  FieldEnd = D->field_end();
    Field != FieldEnd;)
    if (Field->isBitField()) {
      RecordDecl::field_iterator Start = Field;
      // Iterate to gather the list of bitfields.
      for (++Field; Field != FieldEnd && Field->isBitField(); ++Field);
      accumulateBitFields(Start, Field);
    } else {
      Members.push_back(MemberInfo(
          bitsToCharUnits(getFieldBitOffset(*Field)), MemberInfo::Field,
          getStorageType(*Field), *Field));
      ++Field;
    }
}

void
CGRecordLowering::accumulateBitFields(RecordDecl::field_iterator Field,
                                      RecordDecl::field_iterator FieldEnd) {
  // Run stores the first element of the current run of bitfields.  FieldEnd is
  // used as a special value to note that we don't have a current run.  A
  // bitfield run is a contiguous collection of bitfields that can be stored in
  // the same storage block.  Zero-sized bitfields and bitfields that would
  // cross an alignment boundary break a run and start a new one.
  RecordDecl::field_iterator Run = FieldEnd;
  // Tail is the offset of the first bit off the end of the current run.  It's
  // used to determine if the ASTRecordLayout is treating these two bitfields as
  // contiguous.  StartBitOffset is offset of the beginning of the Run.
  uint64_t StartBitOffset, Tail = 0;
  if (isDiscreteBitFieldABI()) {
    for (; Field != FieldEnd; ++Field) {
      uint64_t BitOffset = getFieldBitOffset(*Field);
      // Zero-width bitfields end runs.
      if (Field->isZeroLengthBitField(Context)) {
        Run = FieldEnd;
        continue;
      }
      llvm::Type *Type = Types.ConvertTypeForMem(Field->getType());
      // If we don't have a run yet, or don't live within the previous run's
      // allocated storage then we allocate some storage and start a new run.
      if (Run == FieldEnd || BitOffset >= Tail) {
        Run = Field;
        StartBitOffset = BitOffset;
        Tail = StartBitOffset + DataLayout.getTypeAllocSizeInBits(Type);
        // Add the storage member to the record.  This must be added to the
        // record before the bitfield members so that it gets laid out before
        // the bitfields it contains get laid out.
        Members.push_back(StorageInfo(bitsToCharUnits(StartBitOffset), Type));
      }
      // Bitfields get the offset of their storage but come afterward and remain
      // there after a stable sort.
      Members.push_back(MemberInfo(bitsToCharUnits(StartBitOffset),
                                   MemberInfo::Field, nullptr, *Field));
    }
    return;
  }

  // Check if OffsetInRecord is better as a single field run. When OffsetInRecord
  // has legal integer width, and its bitfield offset is naturally aligned, it
  // is better to make the bitfield a separate storage component so as it can be
  // accessed directly with lower cost.
  auto IsBetterAsSingleFieldRun = [&](uint64_t OffsetInRecord,
                                      uint64_t StartBitOffset) {
    if (!Types.getCodeGenOpts().FineGrainedBitfieldAccesses)
      return false;
    if (!DataLayout.isLegalInteger(OffsetInRecord))
      return false;
    // Make sure StartBitOffset is natually aligned if it is treated as an
    // IType integer.
     if (StartBitOffset %
            Context.toBits(getAlignment(getIntNType(OffsetInRecord))) !=
        0)
      return false;
    return true;
  };

  // The start field is better as a single field run.
  bool StartFieldAsSingleRun = false;
  for (;;) {
    // Check to see if we need to start a new run.
    if (Run == FieldEnd) {
      // If we're out of fields, return.
      if (Field == FieldEnd)
        break;
      // Any non-zero-length bitfield can start a new run.
      if (!Field->isZeroLengthBitField(Context)) {
        Run = Field;
        StartBitOffset = getFieldBitOffset(*Field);
        Tail = StartBitOffset + Field->getBitWidthValue(Context);
        StartFieldAsSingleRun = IsBetterAsSingleFieldRun(Tail - StartBitOffset,
                                                         StartBitOffset);
      }
      ++Field;
      continue;
    }

    // If the start field of a new run is better as a single run, or
    // if current field (or consecutive fields) is better as a single run, or
    // if current field has zero width bitfield and either
    // UseZeroLengthBitfieldAlignment or UseBitFieldTypeAlignment is set to
    // true, or
    // if the offset of current field is inconsistent with the offset of
    // previous field plus its offset,
    // skip the block below and go ahead to emit the storage.
    // Otherwise, try to add bitfields to the run.
    if (!StartFieldAsSingleRun && Field != FieldEnd &&
        !IsBetterAsSingleFieldRun(Tail - StartBitOffset, StartBitOffset) &&
        (!Field->isZeroLengthBitField(Context) ||
         (!Context.getTargetInfo().useZeroLengthBitfieldAlignment() &&
          !Context.getTargetInfo().useBitFieldTypeAlignment())) &&
        Tail == getFieldBitOffset(*Field)) {
      Tail += Field->getBitWidthValue(Context);
      ++Field;
      continue;
    }

    // We've hit a break-point in the run and need to emit a storage field.
    llvm::Type *Type = getIntNType(Tail - StartBitOffset);
    // Add the storage member to the record and set the bitfield info for all of
    // the bitfields in the run.  Bitfields get the offset of their storage but
    // come afterward and remain there after a stable sort.
    Members.push_back(StorageInfo(bitsToCharUnits(StartBitOffset), Type));
    for (; Run != Field; ++Run)
      Members.push_back(MemberInfo(bitsToCharUnits(StartBitOffset),
                                   MemberInfo::Field, nullptr, *Run));
    Run = FieldEnd;
    StartFieldAsSingleRun = false;
  }
}

void CGRecordLowering::accumulateBases() {
  // If we've got a primary virtual base, we need to add it with the bases.
  if (Layout.isPrimaryBaseVirtual()) {
    const CXXRecordDecl *BaseDecl = Layout.getPrimaryBase();
    Members.push_back(MemberInfo(CharUnits::Zero(), MemberInfo::Base,
                                 getStorageType(BaseDecl), BaseDecl));
  }
  // Accumulate the non-virtual bases.
  for (const auto &Base : RD->bases()) {
    if (Base.isVirtual())
      continue;

    // Bases can be zero-sized even if not technically empty if they
    // contain only a trailing array member.
    const CXXRecordDecl *BaseDecl = Base.getType()->getAsCXXRecordDecl();
    if (!BaseDecl->isEmpty() &&
        !Context.getASTRecordLayout(BaseDecl).getNonVirtualSize().isZero())
      Members.push_back(MemberInfo(Layout.getBaseClassOffset(BaseDecl),
          MemberInfo::Base, getStorageType(BaseDecl), BaseDecl));
  }
}

void CGRecordLowering::accumulateVPtrs() {
  if (Layout.hasOwnVFPtr())
    Members.push_back(MemberInfo(CharUnits::Zero(), MemberInfo::VFPtr,
        llvm::FunctionType::get(getIntNType(32), /*isVarArg=*/true)->
            getPointerTo()->getPointerTo()));
  if (Layout.hasOwnVBPtr())
    Members.push_back(MemberInfo(Layout.getVBPtrOffset(), MemberInfo::VBPtr,
        llvm::Type::getInt32PtrTy(Types.getLLVMContext())));
}

void CGRecordLowering::accumulateVBases() {
  CharUnits ScissorOffset = Layout.getNonVirtualSize();
  // In the itanium ABI, it's possible to place a vbase at a dsize that is
  // smaller than the nvsize.  Here we check to see if such a base is placed
  // before the nvsize and set the scissor offset to that, instead of the
  // nvsize.
  if (isOverlappingVBaseABI())
    for (const auto &Base : RD->vbases()) {
      const CXXRecordDecl *BaseDecl = Base.getType()->getAsCXXRecordDecl();
      if (BaseDecl->isEmpty())
        continue;
      // If the vbase is a primary virtual base of some base, then it doesn't
      // get its own storage location but instead lives inside of that base.
      if (Context.isNearlyEmpty(BaseDecl) && !hasOwnStorage(RD, BaseDecl))
        continue;
      ScissorOffset = std::min(ScissorOffset,
                               Layout.getVBaseClassOffset(BaseDecl));
    }
  Members.push_back(MemberInfo(ScissorOffset, MemberInfo::Scissor, nullptr,
                               RD));
  for (const auto &Base : RD->vbases()) {
    const CXXRecordDecl *BaseDecl = Base.getType()->getAsCXXRecordDecl();
    if (BaseDecl->isEmpty())
      continue;
    CharUnits Offset = Layout.getVBaseClassOffset(BaseDecl);
    // If the vbase is a primary virtual base of some base, then it doesn't
    // get its own storage location but instead lives inside of that base.
    if (isOverlappingVBaseABI() &&
        Context.isNearlyEmpty(BaseDecl) &&
        !hasOwnStorage(RD, BaseDecl)) {
      Members.push_back(MemberInfo(Offset, MemberInfo::VBase, nullptr,
                                   BaseDecl));
      continue;
    }
    // If we've got a vtordisp, add it as a storage type.
    if (Layout.getVBaseOffsetsMap().find(BaseDecl)->second.hasVtorDisp())
      Members.push_back(StorageInfo(Offset - CharUnits::fromQuantity(4),
                                    getIntNType(32)));
    Members.push_back(MemberInfo(Offset, MemberInfo::VBase,
                                 getStorageType(BaseDecl), BaseDecl));
  }
}

bool CGRecordLowering::hasOwnStorage(const CXXRecordDecl *Decl,
                                     const CXXRecordDecl *Query) {
  const ASTRecordLayout &DeclLayout = Context.getASTRecordLayout(Decl);
  if (DeclLayout.isPrimaryBaseVirtual() && DeclLayout.getPrimaryBase() == Query)
    return false;
  for (const auto &Base : Decl->bases())
    if (!hasOwnStorage(Base.getType()->getAsCXXRecordDecl(), Query))
      return false;
  return true;
}

void CGRecordLowering::calculateZeroInit() {
  for (std::vector<MemberInfo>::const_iterator Member = Members.begin(),
                                               MemberEnd = Members.end();
       IsZeroInitializableAsBase && Member != MemberEnd; ++Member) {
    if (Member->Kind == MemberInfo::Field) {
      if (!Member->FD || isZeroInitializable(Member->FD))
        continue;
      IsZeroInitializable = IsZeroInitializableAsBase = false;
    } else if (Member->Kind == MemberInfo::Base ||
               Member->Kind == MemberInfo::VBase) {
      if (isZeroInitializable(Member->RD))
        continue;
      IsZeroInitializable = false;
      if (Member->Kind == MemberInfo::Base)
        IsZeroInitializableAsBase = false;
    }
  }
}

void CGRecordLowering::clipTailPadding() {
  std::vector<MemberInfo>::iterator Prior = Members.begin();
  CharUnits Tail = getSize(Prior->Data);
  for (std::vector<MemberInfo>::iterator Member = Prior + 1,
                                         MemberEnd = Members.end();
       Member != MemberEnd; ++Member) {
    // Only members with data and the scissor can cut into tail padding.
    if (!Member->Data && Member->Kind != MemberInfo::Scissor)
      continue;
    if (Member->Offset < Tail) {
      assert(Prior->Kind == MemberInfo::Field && !Prior->FD &&
             "Only storage fields have tail padding!");
      Prior->Data = getByteArrayType(bitsToCharUnits(llvm::alignTo(
          cast<llvm::IntegerType>(Prior->Data)->getIntegerBitWidth(), 8)));
    }
    if (Member->Data)
      Prior = Member;
    Tail = Prior->Offset + getSize(Prior->Data);
  }
}

void CGRecordLowering::determinePacked(bool NVBaseType) {
  if (Packed)
    return;
  CharUnits Alignment = CharUnits::One();
  CharUnits NVAlignment = CharUnits::One();
  CharUnits NVSize =
      !NVBaseType && RD ? Layout.getNonVirtualSize() : CharUnits::Zero();
  for (std::vector<MemberInfo>::const_iterator Member = Members.begin(),
                                               MemberEnd = Members.end();
       Member != MemberEnd; ++Member) {
    if (!Member->Data)
      continue;
    // If any member falls at an offset that it not a multiple of its alignment,
    // then the entire record must be packed.
    if (Member->Offset % getAlignment(Member->Data))
      Packed = true;
    if (Member->Offset < NVSize)
      NVAlignment = std::max(NVAlignment, getAlignment(Member->Data));
    Alignment = std::max(Alignment, getAlignment(Member->Data));
  }
  // If the size of the record (the capstone's offset) is not a multiple of the
  // record's alignment, it must be packed.
  if (Members.back().Offset % Alignment)
    Packed = true;
  // If the non-virtual sub-object is not a multiple of the non-virtual
  // sub-object's alignment, it must be packed.  We cannot have a packed
  // non-virtual sub-object and an unpacked complete object or vise versa.
  if (NVSize % NVAlignment)
    Packed = true;
  // Update the alignment of the sentinel.
  if (!Packed)
    Members.back().Data = getIntNType(Context.toBits(Alignment));
}

void CGRecordLowering::insertPadding() {
  std::vector<std::pair<CharUnits, CharUnits> > Padding;
  CharUnits Size = CharUnits::Zero();
  for (std::vector<MemberInfo>::const_iterator Member = Members.begin(),
                                               MemberEnd = Members.end();
       Member != MemberEnd; ++Member) {
    if (!Member->Data)
      continue;
    CharUnits Offset = Member->Offset;
    assert(Offset >= Size);
    // Insert padding if we need to.
    if (Offset !=
        Size.alignTo(Packed ? CharUnits::One() : getAlignment(Member->Data)))
      Padding.push_back(std::make_pair(Size, Offset - Size));
    Size = Offset + getSize(Member->Data);
  }
  if (Padding.empty())
    return;
  // Add the padding to the Members list and sort it.
  for (std::vector<std::pair<CharUnits, CharUnits> >::const_iterator
        Pad = Padding.begin(), PadEnd = Padding.end();
        Pad != PadEnd; ++Pad)
    Members.push_back(StorageInfo(Pad->first, getByteArrayType(Pad->second)));
  std::stable_sort(Members.begin(), Members.end());
}

void CGRecordLowering::fillOutputFields() {
  for (std::vector<MemberInfo>::const_iterator Member = Members.begin(),
                                               MemberEnd = Members.end();
       Member != MemberEnd; ++Member) {
    if (Member->Data)
      FieldTypes.push_back(Member->Data);
    if (Member->Kind == MemberInfo::Field) {
      if (Member->FD)
        Fields[Member->FD->getCanonicalDecl()] = FieldTypes.size() - 1;
      // A field without storage must be a bitfield.
      if (!Member->Data)
        setBitFieldInfo(Member->FD, Member->Offset, FieldTypes.back());
    } else if (Member->Kind == MemberInfo::Base)
      NonVirtualBases[Member->RD] = FieldTypes.size() - 1;
    else if (Member->Kind == MemberInfo::VBase)
      VirtualBases[Member->RD] = FieldTypes.size() - 1;
  }
}

CGBitFieldInfo CGBitFieldInfo::MakeInfo(CodeGenTypes &Types,
                                        const FieldDecl *FD,
                                        uint64_t Offset, uint64_t Size,
                                        uint64_t StorageSize,
                                        CharUnits StorageOffset) {
  // This function is vestigial from CGRecordLayoutBuilder days but is still
  // used in GCObjCRuntime.cpp.  That usage has a "fixme" attached to it that
  // when addressed will allow for the removal of this function.
  llvm::Type *Ty = Types.ConvertTypeForMem(FD->getType());
  CharUnits TypeSizeInBytes =
    CharUnits::fromQuantity(Types.getDataLayout().getTypeAllocSize(Ty));
  uint64_t TypeSizeInBits = Types.getContext().toBits(TypeSizeInBytes);

  bool IsSigned = FD->getType()->isSignedIntegerOrEnumerationType();

  if (Size > TypeSizeInBits) {
    // We have a wide bit-field. The extra bits are only used for padding, so
    // if we have a bitfield of type T, with size N:
    //
    // T t : N;
    //
    // We can just assume that it's:
    //
    // T t : sizeof(T);
    //
    Size = TypeSizeInBits;
  }

  // Reverse the bit offsets for big endian machines. Because we represent
  // a bitfield as a single large integer load, we can imagine the bits
  // counting from the most-significant-bit instead of the
  // least-significant-bit.
  if (Types.getDataLayout().isBigEndian()) {
    Offset = StorageSize - (Offset + Size);
  }

  return CGBitFieldInfo(Offset, Size, IsSigned, StorageSize, StorageOffset);
}

CGRecordLayout *CodeGenTypes::ComputeRecordLayout(const RecordDecl *D,
                                                  llvm::StructType *Ty) {
  CGRecordLowering Builder(*this, D, /*Packed=*/false);

  Builder.lower(/*NonVirtualBaseType=*/false);

  // If we're in C++, compute the base subobject type.
  llvm::StructType *BaseTy = nullptr;
  if (isa<CXXRecordDecl>(D) && !D->isUnion() && !D->hasAttr<FinalAttr>()) {
    BaseTy = Ty;
    if (Builder.Layout.getNonVirtualSize() != Builder.Layout.getSize()) {
      CGRecordLowering BaseBuilder(*this, D, /*Packed=*/Builder.Packed);
      BaseBuilder.lower(/*NonVirtualBaseType=*/true);
      BaseTy = llvm::StructType::create(
          getLLVMContext(), BaseBuilder.FieldTypes, "", BaseBuilder.Packed);
      addRecordTypeName(D, BaseTy, ".base");
      // BaseTy and Ty must agree on their packedness for getLLVMFieldNo to work
      // on both of them with the same index.
      assert(Builder.Packed == BaseBuilder.Packed &&
             "Non-virtual and complete types must agree on packedness");
    }
  }

  // Fill in the struct *after* computing the base type.  Filling in the body
  // signifies that the type is no longer opaque and record layout is complete,
  // but we may need to recursively layout D while laying D out as a base type.
  Ty->setBody(Builder.FieldTypes, Builder.Packed);

  CGRecordLayout *RL =
    new CGRecordLayout(Ty, BaseTy, Builder.IsZeroInitializable,
                        Builder.IsZeroInitializableAsBase);

  RL->NonVirtualBases.swap(Builder.NonVirtualBases);
  RL->CompleteObjectVirtualBases.swap(Builder.VirtualBases);

  // Add all the field numbers.
  RL->FieldInfo.swap(Builder.Fields);

  // Add bitfield info.
  RL->BitFields.swap(Builder.BitFields);

  // Dump the layout, if requested.
  if (getContext().getLangOpts().DumpRecordLayouts) {
    llvm::outs() << "\n*** Dumping IRgen Record Layout\n";
    llvm::outs() << "Record: ";
    D->dump(llvm::outs());
    llvm::outs() << "\nLayout: ";
    RL->print(llvm::outs());
  }

#ifndef NDEBUG
  // Verify that the computed LLVM struct size matches the AST layout size.
  const ASTRecordLayout &Layout = getContext().getASTRecordLayout(D);

  uint64_t TypeSizeInBits = getContext().toBits(Layout.getSize());
  assert(TypeSizeInBits == getDataLayout().getTypeAllocSizeInBits(Ty) &&
         "Type size mismatch!");

  if (BaseTy) {
    CharUnits NonVirtualSize  = Layout.getNonVirtualSize();

    uint64_t AlignedNonVirtualTypeSizeInBits =
      getContext().toBits(NonVirtualSize);

    assert(AlignedNonVirtualTypeSizeInBits ==
           getDataLayout().getTypeAllocSizeInBits(BaseTy) &&
           "Type size mismatch!");
  }

  // Verify that the LLVM and AST field offsets agree.
  llvm::StructType *ST = RL->getLLVMType();
  const llvm::StructLayout *SL = getDataLayout().getStructLayout(ST);

  const ASTRecordLayout &AST_RL = getContext().getASTRecordLayout(D);
  RecordDecl::field_iterator it = D->field_begin();
  for (unsigned i = 0, e = AST_RL.getFieldCount(); i != e; ++i, ++it) {
    const FieldDecl *FD = *it;

    // For non-bit-fields, just check that the LLVM struct offset matches the
    // AST offset.
    if (!FD->isBitField()) {
      unsigned FieldNo = RL->getLLVMFieldNo(FD);
      assert(AST_RL.getFieldOffset(i) == SL->getElementOffsetInBits(FieldNo) &&
             "Invalid field offset!");
      continue;
    }

    // Ignore unnamed bit-fields.
    if (!FD->getDeclName())
      continue;

    // Don't inspect zero-length bitfields.
    if (FD->isZeroLengthBitField(getContext()))
      continue;

    const CGBitFieldInfo &Info = RL->getBitFieldInfo(FD);
    llvm::Type *ElementTy = ST->getTypeAtIndex(RL->getLLVMFieldNo(FD));

    // Unions have overlapping elements dictating their layout, but for
    // non-unions we can verify that this section of the layout is the exact
    // expected size.
    if (D->isUnion()) {
      // For unions we verify that the start is zero and the size
      // is in-bounds. However, on BE systems, the offset may be non-zero, but
      // the size + offset should match the storage size in that case as it
      // "starts" at the back.
      if (getDataLayout().isBigEndian())
        assert(static_cast<unsigned>(Info.Offset + Info.Size) ==
               Info.StorageSize &&
               "Big endian union bitfield does not end at the back");
      else
        assert(Info.Offset == 0 &&
               "Little endian union bitfield with a non-zero offset");
      assert(Info.StorageSize <= SL->getSizeInBits() &&
             "Union not large enough for bitfield storage");
    } else {
      assert(Info.StorageSize ==
             getDataLayout().getTypeAllocSizeInBits(ElementTy) &&
             "Storage size does not match the element type size");
    }
    assert(Info.Size > 0 && "Empty bitfield!");
    assert(static_cast<unsigned>(Info.Offset) + Info.Size <= Info.StorageSize &&
           "Bitfield outside of its allocated storage");
  }
#endif

  return RL;
}

void CGRecordLayout::print(raw_ostream &OS) const {
  OS << "<CGRecordLayout\n";
  OS << "  LLVMType:" << *CompleteObjectType << "\n";
  if (BaseSubobjectType)
    OS << "  NonVirtualBaseLLVMType:" << *BaseSubobjectType << "\n";
  OS << "  IsZeroInitializable:" << IsZeroInitializable << "\n";
  OS << "  BitFields:[\n";

  // Print bit-field infos in declaration order.
  std::vector<std::pair<unsigned, const CGBitFieldInfo*> > BFIs;
  for (llvm::DenseMap<const FieldDecl*, CGBitFieldInfo>::const_iterator
         it = BitFields.begin(), ie = BitFields.end();
       it != ie; ++it) {
    const RecordDecl *RD = it->first->getParent();
    unsigned Index = 0;
    for (RecordDecl::field_iterator
           it2 = RD->field_begin(); *it2 != it->first; ++it2)
      ++Index;
    BFIs.push_back(std::make_pair(Index, &it->second));
  }
  llvm::array_pod_sort(BFIs.begin(), BFIs.end());
  for (unsigned i = 0, e = BFIs.size(); i != e; ++i) {
    OS.indent(4);
    BFIs[i].second->print(OS);
    OS << "\n";
  }

  OS << "]>\n";
}

LLVM_DUMP_METHOD void CGRecordLayout::dump() const {
  print(llvm::errs());
}

void CGBitFieldInfo::print(raw_ostream &OS) const {
  OS << "<CGBitFieldInfo"
     << " Offset:" << Offset
     << " Size:" << Size
     << " IsSigned:" << IsSigned
     << " StorageSize:" << StorageSize
     << " StorageOffset:" << StorageOffset.getQuantity() << ">";
}

LLVM_DUMP_METHOD void CGBitFieldInfo::dump() const {
  print(llvm::errs());
}
