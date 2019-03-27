//===- llvm/DataLayout.h - Data size & alignment info -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines layout properties related to datatype size/offset/alignment
// information.  It uses lazy annotations to cache information about how
// structure types are laid out and used.
//
// This structure should be created once, filled in if the defaults are not
// correct and then passed around by const&.  None of the members functions
// require modification to the object.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DATALAYOUT_H
#define LLVM_IR_DATALAYOUT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>
#include <string>

// This needs to be outside of the namespace, to avoid conflict with llvm-c
// decl.
using LLVMTargetDataRef = struct LLVMOpaqueTargetData *;

namespace llvm {

class GlobalVariable;
class LLVMContext;
class Module;
class StructLayout;
class Triple;
class Value;

/// Enum used to categorize the alignment types stored by LayoutAlignElem
enum AlignTypeEnum {
  INVALID_ALIGN = 0,
  INTEGER_ALIGN = 'i',
  VECTOR_ALIGN = 'v',
  FLOAT_ALIGN = 'f',
  AGGREGATE_ALIGN = 'a'
};

// FIXME: Currently the DataLayout string carries a "preferred alignment"
// for types. As the DataLayout is module/global, this should likely be
// sunk down to an FTTI element that is queried rather than a global
// preference.

/// Layout alignment element.
///
/// Stores the alignment data associated with a given alignment type (integer,
/// vector, float) and type bit width.
///
/// \note The unusual order of elements in the structure attempts to reduce
/// padding and make the structure slightly more cache friendly.
struct LayoutAlignElem {
  /// Alignment type from \c AlignTypeEnum
  unsigned AlignType : 8;
  unsigned TypeBitWidth : 24;
  unsigned ABIAlign : 16;
  unsigned PrefAlign : 16;

  static LayoutAlignElem get(AlignTypeEnum align_type, unsigned abi_align,
                             unsigned pref_align, uint32_t bit_width);

  bool operator==(const LayoutAlignElem &rhs) const;
};

/// Layout pointer alignment element.
///
/// Stores the alignment data associated with a given pointer and address space.
///
/// \note The unusual order of elements in the structure attempts to reduce
/// padding and make the structure slightly more cache friendly.
struct PointerAlignElem {
  unsigned ABIAlign;
  unsigned PrefAlign;
  uint32_t TypeByteWidth;
  uint32_t AddressSpace;
  uint32_t IndexWidth;

  /// Initializer
  static PointerAlignElem get(uint32_t AddressSpace, unsigned ABIAlign,
                              unsigned PrefAlign, uint32_t TypeByteWidth,
                              uint32_t IndexWidth);

  bool operator==(const PointerAlignElem &rhs) const;
};

/// A parsed version of the target data layout string in and methods for
/// querying it.
///
/// The target data layout string is specified *by the target* - a frontend
/// generating LLVM IR is required to generate the right target data for the
/// target being codegen'd to.
class DataLayout {
private:
  /// Defaults to false.
  bool BigEndian;

  unsigned AllocaAddrSpace;
  unsigned StackNaturalAlign;
  unsigned ProgramAddrSpace;

  enum ManglingModeT {
    MM_None,
    MM_ELF,
    MM_MachO,
    MM_WinCOFF,
    MM_WinCOFFX86,
    MM_Mips
  };
  ManglingModeT ManglingMode;

  SmallVector<unsigned char, 8> LegalIntWidths;

  /// Primitive type alignment data. This is sorted by type and bit
  /// width during construction.
  using AlignmentsTy = SmallVector<LayoutAlignElem, 16>;
  AlignmentsTy Alignments;

  AlignmentsTy::const_iterator
  findAlignmentLowerBound(AlignTypeEnum AlignType, uint32_t BitWidth) const {
    return const_cast<DataLayout *>(this)->findAlignmentLowerBound(AlignType,
                                                                   BitWidth);
  }

  AlignmentsTy::iterator
  findAlignmentLowerBound(AlignTypeEnum AlignType, uint32_t BitWidth);

  /// The string representation used to create this DataLayout
  std::string StringRepresentation;

  using PointersTy = SmallVector<PointerAlignElem, 8>;
  PointersTy Pointers;

  PointersTy::const_iterator
  findPointerLowerBound(uint32_t AddressSpace) const {
    return const_cast<DataLayout *>(this)->findPointerLowerBound(AddressSpace);
  }

  PointersTy::iterator findPointerLowerBound(uint32_t AddressSpace);

  // The StructType -> StructLayout map.
  mutable void *LayoutMap = nullptr;

  /// Pointers in these address spaces are non-integral, and don't have a
  /// well-defined bitwise representation.
  SmallVector<unsigned, 8> NonIntegralAddressSpaces;

  void setAlignment(AlignTypeEnum align_type, unsigned abi_align,
                    unsigned pref_align, uint32_t bit_width);
  unsigned getAlignmentInfo(AlignTypeEnum align_type, uint32_t bit_width,
                            bool ABIAlign, Type *Ty) const;
  void setPointerAlignment(uint32_t AddrSpace, unsigned ABIAlign,
                           unsigned PrefAlign, uint32_t TypeByteWidth,
                           uint32_t IndexWidth);

  /// Internal helper method that returns requested alignment for type.
  unsigned getAlignment(Type *Ty, bool abi_or_pref) const;

  /// Parses a target data specification string. Assert if the string is
  /// malformed.
  void parseSpecifier(StringRef LayoutDescription);

  // Free all internal data structures.
  void clear();

public:
  /// Constructs a DataLayout from a specification string. See reset().
  explicit DataLayout(StringRef LayoutDescription) {
    reset(LayoutDescription);
  }

  /// Initialize target data from properties stored in the module.
  explicit DataLayout(const Module *M);

  DataLayout(const DataLayout &DL) { *this = DL; }

  ~DataLayout(); // Not virtual, do not subclass this class

  DataLayout &operator=(const DataLayout &DL) {
    clear();
    StringRepresentation = DL.StringRepresentation;
    BigEndian = DL.isBigEndian();
    AllocaAddrSpace = DL.AllocaAddrSpace;
    StackNaturalAlign = DL.StackNaturalAlign;
    ProgramAddrSpace = DL.ProgramAddrSpace;
    ManglingMode = DL.ManglingMode;
    LegalIntWidths = DL.LegalIntWidths;
    Alignments = DL.Alignments;
    Pointers = DL.Pointers;
    NonIntegralAddressSpaces = DL.NonIntegralAddressSpaces;
    return *this;
  }

  bool operator==(const DataLayout &Other) const;
  bool operator!=(const DataLayout &Other) const { return !(*this == Other); }

  void init(const Module *M);

  /// Parse a data layout string (with fallback to default values).
  void reset(StringRef LayoutDescription);

  /// Layout endianness...
  bool isLittleEndian() const { return !BigEndian; }
  bool isBigEndian() const { return BigEndian; }

  /// Returns the string representation of the DataLayout.
  ///
  /// This representation is in the same format accepted by the string
  /// constructor above. This should not be used to compare two DataLayout as
  /// different string can represent the same layout.
  const std::string &getStringRepresentation() const {
    return StringRepresentation;
  }

  /// Test if the DataLayout was constructed from an empty string.
  bool isDefault() const { return StringRepresentation.empty(); }

  /// Returns true if the specified type is known to be a native integer
  /// type supported by the CPU.
  ///
  /// For example, i64 is not native on most 32-bit CPUs and i37 is not native
  /// on any known one. This returns false if the integer width is not legal.
  ///
  /// The width is specified in bits.
  bool isLegalInteger(uint64_t Width) const {
    for (unsigned LegalIntWidth : LegalIntWidths)
      if (LegalIntWidth == Width)
        return true;
    return false;
  }

  bool isIllegalInteger(uint64_t Width) const { return !isLegalInteger(Width); }

  /// Returns true if the given alignment exceeds the natural stack alignment.
  bool exceedsNaturalStackAlignment(unsigned Align) const {
    return (StackNaturalAlign != 0) && (Align > StackNaturalAlign);
  }

  unsigned getStackAlignment() const { return StackNaturalAlign; }
  unsigned getAllocaAddrSpace() const { return AllocaAddrSpace; }

  unsigned getProgramAddressSpace() const { return ProgramAddrSpace; }

  bool hasMicrosoftFastStdCallMangling() const {
    return ManglingMode == MM_WinCOFFX86;
  }

  /// Returns true if symbols with leading question marks should not receive IR
  /// mangling. True for Windows mangling modes.
  bool doNotMangleLeadingQuestionMark() const {
    return ManglingMode == MM_WinCOFF || ManglingMode == MM_WinCOFFX86;
  }

  bool hasLinkerPrivateGlobalPrefix() const { return ManglingMode == MM_MachO; }

  StringRef getLinkerPrivateGlobalPrefix() const {
    if (ManglingMode == MM_MachO)
      return "l";
    return "";
  }

  char getGlobalPrefix() const {
    switch (ManglingMode) {
    case MM_None:
    case MM_ELF:
    case MM_Mips:
    case MM_WinCOFF:
      return '\0';
    case MM_MachO:
    case MM_WinCOFFX86:
      return '_';
    }
    llvm_unreachable("invalid mangling mode");
  }

  StringRef getPrivateGlobalPrefix() const {
    switch (ManglingMode) {
    case MM_None:
      return "";
    case MM_ELF:
    case MM_WinCOFF:
      return ".L";
    case MM_Mips:
      return "$";
    case MM_MachO:
    case MM_WinCOFFX86:
      return "L";
    }
    llvm_unreachable("invalid mangling mode");
  }

  static const char *getManglingComponent(const Triple &T);

  /// Returns true if the specified type fits in a native integer type
  /// supported by the CPU.
  ///
  /// For example, if the CPU only supports i32 as a native integer type, then
  /// i27 fits in a legal integer type but i45 does not.
  bool fitsInLegalInteger(unsigned Width) const {
    for (unsigned LegalIntWidth : LegalIntWidths)
      if (Width <= LegalIntWidth)
        return true;
    return false;
  }

  /// Layout pointer alignment
  unsigned getPointerABIAlignment(unsigned AS) const;

  /// Return target's alignment for stack-based pointers
  /// FIXME: The defaults need to be removed once all of
  /// the backends/clients are updated.
  unsigned getPointerPrefAlignment(unsigned AS = 0) const;

  /// Layout pointer size
  /// FIXME: The defaults need to be removed once all of
  /// the backends/clients are updated.
  unsigned getPointerSize(unsigned AS = 0) const;

  /// Returns the maximum pointer size over all address spaces.
  unsigned getMaxPointerSize() const;

  // Index size used for address calculation.
  unsigned getIndexSize(unsigned AS) const;

  /// Return the address spaces containing non-integral pointers.  Pointers in
  /// this address space don't have a well-defined bitwise representation.
  ArrayRef<unsigned> getNonIntegralAddressSpaces() const {
    return NonIntegralAddressSpaces;
  }

  bool isNonIntegralPointerType(PointerType *PT) const {
    ArrayRef<unsigned> NonIntegralSpaces = getNonIntegralAddressSpaces();
    return find(NonIntegralSpaces, PT->getAddressSpace()) !=
           NonIntegralSpaces.end();
  }

  bool isNonIntegralPointerType(Type *Ty) const {
    auto *PTy = dyn_cast<PointerType>(Ty);
    return PTy && isNonIntegralPointerType(PTy);
  }

  /// Layout pointer size, in bits
  /// FIXME: The defaults need to be removed once all of
  /// the backends/clients are updated.
  unsigned getPointerSizeInBits(unsigned AS = 0) const {
    return getPointerSize(AS) * 8;
  }

  /// Returns the maximum pointer size over all address spaces.
  unsigned getMaxPointerSizeInBits() const {
    return getMaxPointerSize() * 8;
  }

  /// Size in bits of index used for address calculation in getelementptr.
  unsigned getIndexSizeInBits(unsigned AS) const {
    return getIndexSize(AS) * 8;
  }

  /// Layout pointer size, in bits, based on the type.  If this function is
  /// called with a pointer type, then the type size of the pointer is returned.
  /// If this function is called with a vector of pointers, then the type size
  /// of the pointer is returned.  This should only be called with a pointer or
  /// vector of pointers.
  unsigned getPointerTypeSizeInBits(Type *) const;

  /// Layout size of the index used in GEP calculation.
  /// The function should be called with pointer or vector of pointers type.
  unsigned getIndexTypeSizeInBits(Type *Ty) const;

  unsigned getPointerTypeSize(Type *Ty) const {
    return getPointerTypeSizeInBits(Ty) / 8;
  }

  /// Size examples:
  ///
  /// Type        SizeInBits  StoreSizeInBits  AllocSizeInBits[*]
  /// ----        ----------  ---------------  ---------------
  ///  i1            1           8                8
  ///  i8            8           8                8
  ///  i19          19          24               32
  ///  i32          32          32               32
  ///  i100        100         104              128
  ///  i128        128         128              128
  ///  Float        32          32               32
  ///  Double       64          64               64
  ///  X86_FP80     80          80               96
  ///
  /// [*] The alloc size depends on the alignment, and thus on the target.
  ///     These values are for x86-32 linux.

  /// Returns the number of bits necessary to hold the specified type.
  ///
  /// For example, returns 36 for i36 and 80 for x86_fp80. The type passed must
  /// have a size (Type::isSized() must return true).
  uint64_t getTypeSizeInBits(Type *Ty) const;

  /// Returns the maximum number of bytes that may be overwritten by
  /// storing the specified type.
  ///
  /// For example, returns 5 for i36 and 10 for x86_fp80.
  uint64_t getTypeStoreSize(Type *Ty) const {
    return (getTypeSizeInBits(Ty) + 7) / 8;
  }

  /// Returns the maximum number of bits that may be overwritten by
  /// storing the specified type; always a multiple of 8.
  ///
  /// For example, returns 40 for i36 and 80 for x86_fp80.
  uint64_t getTypeStoreSizeInBits(Type *Ty) const {
    return 8 * getTypeStoreSize(Ty);
  }

  /// Returns the offset in bytes between successive objects of the
  /// specified type, including alignment padding.
  ///
  /// This is the amount that alloca reserves for this type. For example,
  /// returns 12 or 16 for x86_fp80, depending on alignment.
  uint64_t getTypeAllocSize(Type *Ty) const {
    // Round up to the next alignment boundary.
    return alignTo(getTypeStoreSize(Ty), getABITypeAlignment(Ty));
  }

  /// Returns the offset in bits between successive objects of the
  /// specified type, including alignment padding; always a multiple of 8.
  ///
  /// This is the amount that alloca reserves for this type. For example,
  /// returns 96 or 128 for x86_fp80, depending on alignment.
  uint64_t getTypeAllocSizeInBits(Type *Ty) const {
    return 8 * getTypeAllocSize(Ty);
  }

  /// Returns the minimum ABI-required alignment for the specified type.
  unsigned getABITypeAlignment(Type *Ty) const;

  /// Returns the minimum ABI-required alignment for an integer type of
  /// the specified bitwidth.
  unsigned getABIIntegerTypeAlignment(unsigned BitWidth) const;

  /// Returns the preferred stack/global alignment for the specified
  /// type.
  ///
  /// This is always at least as good as the ABI alignment.
  unsigned getPrefTypeAlignment(Type *Ty) const;

  /// Returns the preferred alignment for the specified type, returned as
  /// log2 of the value (a shift amount).
  unsigned getPreferredTypeAlignmentShift(Type *Ty) const;

  /// Returns an integer type with size at least as big as that of a
  /// pointer in the given address space.
  IntegerType *getIntPtrType(LLVMContext &C, unsigned AddressSpace = 0) const;

  /// Returns an integer (vector of integer) type with size at least as
  /// big as that of a pointer of the given pointer (vector of pointer) type.
  Type *getIntPtrType(Type *) const;

  /// Returns the smallest integer type with size at least as big as
  /// Width bits.
  Type *getSmallestLegalIntType(LLVMContext &C, unsigned Width = 0) const;

  /// Returns the largest legal integer type, or null if none are set.
  Type *getLargestLegalIntType(LLVMContext &C) const {
    unsigned LargestSize = getLargestLegalIntTypeSizeInBits();
    return (LargestSize == 0) ? nullptr : Type::getIntNTy(C, LargestSize);
  }

  /// Returns the size of largest legal integer type size, or 0 if none
  /// are set.
  unsigned getLargestLegalIntTypeSizeInBits() const;

  /// Returns the type of a GEP index.
  /// If it was not specified explicitly, it will be the integer type of the
  /// pointer width - IntPtrType.
  Type *getIndexType(Type *PtrTy) const;

  /// Returns the offset from the beginning of the type for the specified
  /// indices.
  ///
  /// Note that this takes the element type, not the pointer type.
  /// This is used to implement getelementptr.
  int64_t getIndexedOffsetInType(Type *ElemTy, ArrayRef<Value *> Indices) const;

  /// Returns a StructLayout object, indicating the alignment of the
  /// struct, its size, and the offsets of its fields.
  ///
  /// Note that this information is lazily cached.
  const StructLayout *getStructLayout(StructType *Ty) const;

  /// Returns the preferred alignment of the specified global.
  ///
  /// This includes an explicitly requested alignment (if the global has one).
  unsigned getPreferredAlignment(const GlobalVariable *GV) const;

  /// Returns the preferred alignment of the specified global, returned
  /// in log form.
  ///
  /// This includes an explicitly requested alignment (if the global has one).
  unsigned getPreferredAlignmentLog(const GlobalVariable *GV) const;
};

inline DataLayout *unwrap(LLVMTargetDataRef P) {
  return reinterpret_cast<DataLayout *>(P);
}

inline LLVMTargetDataRef wrap(const DataLayout *P) {
  return reinterpret_cast<LLVMTargetDataRef>(const_cast<DataLayout *>(P));
}

/// Used to lazily calculate structure layout information for a target machine,
/// based on the DataLayout structure.
class StructLayout {
  uint64_t StructSize;
  unsigned StructAlignment;
  unsigned IsPadded : 1;
  unsigned NumElements : 31;
  uint64_t MemberOffsets[1]; // variable sized array!

public:
  uint64_t getSizeInBytes() const { return StructSize; }

  uint64_t getSizeInBits() const { return 8 * StructSize; }

  unsigned getAlignment() const { return StructAlignment; }

  /// Returns whether the struct has padding or not between its fields.
  /// NB: Padding in nested element is not taken into account.
  bool hasPadding() const { return IsPadded; }

  /// Given a valid byte offset into the structure, returns the structure
  /// index that contains it.
  unsigned getElementContainingOffset(uint64_t Offset) const;

  uint64_t getElementOffset(unsigned Idx) const {
    assert(Idx < NumElements && "Invalid element idx!");
    return MemberOffsets[Idx];
  }

  uint64_t getElementOffsetInBits(unsigned Idx) const {
    return getElementOffset(Idx) * 8;
  }

private:
  friend class DataLayout; // Only DataLayout can create this class

  StructLayout(StructType *ST, const DataLayout &DL);
};

// The implementation of this method is provided inline as it is particularly
// well suited to constant folding when called on a specific Type subclass.
inline uint64_t DataLayout::getTypeSizeInBits(Type *Ty) const {
  assert(Ty->isSized() && "Cannot getTypeInfo() on a type that is unsized!");
  switch (Ty->getTypeID()) {
  case Type::LabelTyID:
    return getPointerSizeInBits(0);
  case Type::PointerTyID:
    return getPointerSizeInBits(Ty->getPointerAddressSpace());
  case Type::ArrayTyID: {
    ArrayType *ATy = cast<ArrayType>(Ty);
    return ATy->getNumElements() *
           getTypeAllocSizeInBits(ATy->getElementType());
  }
  case Type::StructTyID:
    // Get the layout annotation... which is lazily created on demand.
    return getStructLayout(cast<StructType>(Ty))->getSizeInBits();
  case Type::IntegerTyID:
    return Ty->getIntegerBitWidth();
  case Type::HalfTyID:
    return 16;
  case Type::FloatTyID:
    return 32;
  case Type::DoubleTyID:
  case Type::X86_MMXTyID:
    return 64;
  case Type::PPC_FP128TyID:
  case Type::FP128TyID:
    return 128;
  // In memory objects this is always aligned to a higher boundary, but
  // only 80 bits contain information.
  case Type::X86_FP80TyID:
    return 80;
  case Type::VectorTyID: {
    VectorType *VTy = cast<VectorType>(Ty);
    return VTy->getNumElements() * getTypeSizeInBits(VTy->getElementType());
  }
  default:
    llvm_unreachable("DataLayout::getTypeSizeInBits(): Unsupported type");
  }
}

} // end namespace llvm

#endif // LLVM_IR_DATALAYOUT_H
