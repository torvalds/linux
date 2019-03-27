//===- llvm/IR/DebugInfoMetadata.h - Debug info metadata --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Declarations for metadata specific to debug info.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DEBUGINFOMETADATA_H
#define LLVM_IR_DEBUGINFOMETADATA_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <vector>

// Helper macros for defining get() overrides.
#define DEFINE_MDNODE_GET_UNPACK_IMPL(...) __VA_ARGS__
#define DEFINE_MDNODE_GET_UNPACK(ARGS) DEFINE_MDNODE_GET_UNPACK_IMPL ARGS
#define DEFINE_MDNODE_GET_DISTINCT_TEMPORARY(CLASS, FORMAL, ARGS)              \
  static CLASS *getDistinct(LLVMContext &Context,                              \
                            DEFINE_MDNODE_GET_UNPACK(FORMAL)) {                \
    return getImpl(Context, DEFINE_MDNODE_GET_UNPACK(ARGS), Distinct);         \
  }                                                                            \
  static Temp##CLASS getTemporary(LLVMContext &Context,                        \
                                  DEFINE_MDNODE_GET_UNPACK(FORMAL)) {          \
    return Temp##CLASS(                                                        \
        getImpl(Context, DEFINE_MDNODE_GET_UNPACK(ARGS), Temporary));          \
  }
#define DEFINE_MDNODE_GET(CLASS, FORMAL, ARGS)                                 \
  static CLASS *get(LLVMContext &Context, DEFINE_MDNODE_GET_UNPACK(FORMAL)) {  \
    return getImpl(Context, DEFINE_MDNODE_GET_UNPACK(ARGS), Uniqued);          \
  }                                                                            \
  static CLASS *getIfExists(LLVMContext &Context,                              \
                            DEFINE_MDNODE_GET_UNPACK(FORMAL)) {                \
    return getImpl(Context, DEFINE_MDNODE_GET_UNPACK(ARGS), Uniqued,           \
                   /* ShouldCreate */ false);                                  \
  }                                                                            \
  DEFINE_MDNODE_GET_DISTINCT_TEMPORARY(CLASS, FORMAL, ARGS)

namespace llvm {

/// Holds a subclass of DINode.
///
/// FIXME: This class doesn't currently make much sense.  Previously it was a
/// union beteen MDString (for ODR-uniqued types) and things like DIType.  To
/// support CodeView work, it wasn't deleted outright when MDString-based type
/// references were deleted; we'll soon need a similar concept for CodeView
/// DITypeIndex.
template <class T> class TypedDINodeRef {
  const Metadata *MD = nullptr;

public:
  TypedDINodeRef() = default;
  TypedDINodeRef(std::nullptr_t) {}
  TypedDINodeRef(const T *MD) : MD(MD) {}

  explicit TypedDINodeRef(const Metadata *MD) : MD(MD) {
    assert((!MD || isa<T>(MD)) && "Expected valid type ref");
  }

  template <class U>
  TypedDINodeRef(
      const TypedDINodeRef<U> &X,
      typename std::enable_if<std::is_convertible<U *, T *>::value>::type * =
          nullptr)
      : MD(X) {}

  operator Metadata *() const { return const_cast<Metadata *>(MD); }

  T *resolve() const { return const_cast<T *>(cast_or_null<T>(MD)); }

  bool operator==(const TypedDINodeRef<T> &X) const { return MD == X.MD; }
  bool operator!=(const TypedDINodeRef<T> &X) const { return MD != X.MD; }
};

using DINodeRef = TypedDINodeRef<DINode>;
using DIScopeRef = TypedDINodeRef<DIScope>;
using DITypeRef = TypedDINodeRef<DIType>;

class DITypeRefArray {
  const MDTuple *N = nullptr;

public:
  DITypeRefArray() = default;
  DITypeRefArray(const MDTuple *N) : N(N) {}

  explicit operator bool() const { return get(); }
  explicit operator MDTuple *() const { return get(); }

  MDTuple *get() const { return const_cast<MDTuple *>(N); }
  MDTuple *operator->() const { return get(); }
  MDTuple &operator*() const { return *get(); }

  // FIXME: Fix callers and remove condition on N.
  unsigned size() const { return N ? N->getNumOperands() : 0u; }
  DITypeRef operator[](unsigned I) const { return DITypeRef(N->getOperand(I)); }

  class iterator : std::iterator<std::input_iterator_tag, DITypeRef,
                                 std::ptrdiff_t, void, DITypeRef> {
    MDNode::op_iterator I = nullptr;

  public:
    iterator() = default;
    explicit iterator(MDNode::op_iterator I) : I(I) {}

    DITypeRef operator*() const { return DITypeRef(*I); }

    iterator &operator++() {
      ++I;
      return *this;
    }

    iterator operator++(int) {
      iterator Temp(*this);
      ++I;
      return Temp;
    }

    bool operator==(const iterator &X) const { return I == X.I; }
    bool operator!=(const iterator &X) const { return I != X.I; }
  };

  // FIXME: Fix callers and remove condition on N.
  iterator begin() const { return N ? iterator(N->op_begin()) : iterator(); }
  iterator end() const { return N ? iterator(N->op_end()) : iterator(); }
};

/// Tagged DWARF-like metadata node.
///
/// A metadata node with a DWARF tag (i.e., a constant named \c DW_TAG_*,
/// defined in llvm/BinaryFormat/Dwarf.h).  Called \a DINode because it's
/// potentially used for non-DWARF output.
class DINode : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

protected:
  DINode(LLVMContext &C, unsigned ID, StorageType Storage, unsigned Tag,
         ArrayRef<Metadata *> Ops1, ArrayRef<Metadata *> Ops2 = None)
      : MDNode(C, ID, Storage, Ops1, Ops2) {
    assert(Tag < 1u << 16);
    SubclassData16 = Tag;
  }
  ~DINode() = default;

  template <class Ty> Ty *getOperandAs(unsigned I) const {
    return cast_or_null<Ty>(getOperand(I));
  }

  StringRef getStringOperand(unsigned I) const {
    if (auto *S = getOperandAs<MDString>(I))
      return S->getString();
    return StringRef();
  }

  static MDString *getCanonicalMDString(LLVMContext &Context, StringRef S) {
    if (S.empty())
      return nullptr;
    return MDString::get(Context, S);
  }

  /// Allow subclasses to mutate the tag.
  void setTag(unsigned Tag) { SubclassData16 = Tag; }

public:
  unsigned getTag() const { return SubclassData16; }

  /// Debug info flags.
  ///
  /// The three accessibility flags are mutually exclusive and rolled together
  /// in the first two bits.
  enum DIFlags : uint32_t {
#define HANDLE_DI_FLAG(ID, NAME) Flag##NAME = ID,
#define DI_FLAG_LARGEST_NEEDED
#include "llvm/IR/DebugInfoFlags.def"
    FlagAccessibility = FlagPrivate | FlagProtected | FlagPublic,
    FlagPtrToMemberRep = FlagSingleInheritance | FlagMultipleInheritance |
                         FlagVirtualInheritance,
    LLVM_MARK_AS_BITMASK_ENUM(FlagLargest)
  };

  static DIFlags getFlag(StringRef Flag);
  static StringRef getFlagString(DIFlags Flag);

  /// Split up a flags bitfield.
  ///
  /// Split \c Flags into \c SplitFlags, a vector of its components.  Returns
  /// any remaining (unrecognized) bits.
  static DIFlags splitFlags(DIFlags Flags,
                            SmallVectorImpl<DIFlags> &SplitFlags);

  static bool classof(const Metadata *MD) {
    switch (MD->getMetadataID()) {
    default:
      return false;
    case GenericDINodeKind:
    case DISubrangeKind:
    case DIEnumeratorKind:
    case DIBasicTypeKind:
    case DIDerivedTypeKind:
    case DICompositeTypeKind:
    case DISubroutineTypeKind:
    case DIFileKind:
    case DICompileUnitKind:
    case DISubprogramKind:
    case DILexicalBlockKind:
    case DILexicalBlockFileKind:
    case DINamespaceKind:
    case DITemplateTypeParameterKind:
    case DITemplateValueParameterKind:
    case DIGlobalVariableKind:
    case DILocalVariableKind:
    case DILabelKind:
    case DIObjCPropertyKind:
    case DIImportedEntityKind:
    case DIModuleKind:
      return true;
    }
  }
};

template <class T> struct simplify_type<const TypedDINodeRef<T>> {
  using SimpleType = Metadata *;

  static SimpleType getSimplifiedValue(const TypedDINodeRef<T> &MD) {
    return MD;
  }
};

template <class T>
struct simplify_type<TypedDINodeRef<T>>
    : simplify_type<const TypedDINodeRef<T>> {};

/// Generic tagged DWARF-like metadata node.
///
/// An un-specialized DWARF-like metadata node.  The first operand is a
/// (possibly empty) null-separated \a MDString header that contains arbitrary
/// fields.  The remaining operands are \a dwarf_operands(), and are pointers
/// to other metadata.
class GenericDINode : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  GenericDINode(LLVMContext &C, StorageType Storage, unsigned Hash,
                unsigned Tag, ArrayRef<Metadata *> Ops1,
                ArrayRef<Metadata *> Ops2)
      : DINode(C, GenericDINodeKind, Storage, Tag, Ops1, Ops2) {
    setHash(Hash);
  }
  ~GenericDINode() { dropAllReferences(); }

  void setHash(unsigned Hash) { SubclassData32 = Hash; }
  void recalculateHash();

  static GenericDINode *getImpl(LLVMContext &Context, unsigned Tag,
                                StringRef Header, ArrayRef<Metadata *> DwarfOps,
                                StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Tag, getCanonicalMDString(Context, Header),
                   DwarfOps, Storage, ShouldCreate);
  }

  static GenericDINode *getImpl(LLVMContext &Context, unsigned Tag,
                                MDString *Header, ArrayRef<Metadata *> DwarfOps,
                                StorageType Storage, bool ShouldCreate = true);

  TempGenericDINode cloneImpl() const {
    return getTemporary(
        getContext(), getTag(), getHeader(),
        SmallVector<Metadata *, 4>(dwarf_op_begin(), dwarf_op_end()));
  }

public:
  unsigned getHash() const { return SubclassData32; }

  DEFINE_MDNODE_GET(GenericDINode, (unsigned Tag, StringRef Header,
                                    ArrayRef<Metadata *> DwarfOps),
                    (Tag, Header, DwarfOps))
  DEFINE_MDNODE_GET(GenericDINode, (unsigned Tag, MDString *Header,
                                    ArrayRef<Metadata *> DwarfOps),
                    (Tag, Header, DwarfOps))

  /// Return a (temporary) clone of this.
  TempGenericDINode clone() const { return cloneImpl(); }

  unsigned getTag() const { return SubclassData16; }
  StringRef getHeader() const { return getStringOperand(0); }
  MDString *getRawHeader() const { return getOperandAs<MDString>(0); }

  op_iterator dwarf_op_begin() const { return op_begin() + 1; }
  op_iterator dwarf_op_end() const { return op_end(); }
  op_range dwarf_operands() const {
    return op_range(dwarf_op_begin(), dwarf_op_end());
  }

  unsigned getNumDwarfOperands() const { return getNumOperands() - 1; }
  const MDOperand &getDwarfOperand(unsigned I) const {
    return getOperand(I + 1);
  }
  void replaceDwarfOperandWith(unsigned I, Metadata *New) {
    replaceOperandWith(I + 1, New);
  }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == GenericDINodeKind;
  }
};

/// Array subrange.
///
/// TODO: Merge into node for DW_TAG_array_type, which should have a custom
/// type.
class DISubrange : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  int64_t LowerBound;

  DISubrange(LLVMContext &C, StorageType Storage, Metadata *Node,
             int64_t LowerBound, ArrayRef<Metadata *> Ops)
      : DINode(C, DISubrangeKind, Storage, dwarf::DW_TAG_subrange_type, Ops),
        LowerBound(LowerBound) {}

  ~DISubrange() = default;

  static DISubrange *getImpl(LLVMContext &Context, int64_t Count,
                             int64_t LowerBound, StorageType Storage,
                             bool ShouldCreate = true);

  static DISubrange *getImpl(LLVMContext &Context, Metadata *CountNode,
                             int64_t LowerBound, StorageType Storage,
                             bool ShouldCreate = true);

  TempDISubrange cloneImpl() const {
    return getTemporary(getContext(), getRawCountNode(), getLowerBound());
  }

public:
  DEFINE_MDNODE_GET(DISubrange, (int64_t Count, int64_t LowerBound = 0),
                    (Count, LowerBound))

  DEFINE_MDNODE_GET(DISubrange, (Metadata *CountNode, int64_t LowerBound = 0),
                    (CountNode, LowerBound))

  TempDISubrange clone() const { return cloneImpl(); }

  int64_t getLowerBound() const { return LowerBound; }

  Metadata *getRawCountNode() const {
    return getOperand(0).get();
  }

  typedef PointerUnion<ConstantInt*, DIVariable*> CountType;

  CountType getCount() const {
    if (auto *MD = dyn_cast<ConstantAsMetadata>(getRawCountNode()))
      return CountType(cast<ConstantInt>(MD->getValue()));

    if (auto *DV = dyn_cast<DIVariable>(getRawCountNode()))
      return CountType(DV);

    return CountType();
  }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DISubrangeKind;
  }
};

/// Enumeration value.
///
/// TODO: Add a pointer to the context (DW_TAG_enumeration_type) once that no
/// longer creates a type cycle.
class DIEnumerator : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  int64_t Value;
  DIEnumerator(LLVMContext &C, StorageType Storage, int64_t Value,
               bool IsUnsigned, ArrayRef<Metadata *> Ops)
      : DINode(C, DIEnumeratorKind, Storage, dwarf::DW_TAG_enumerator, Ops),
        Value(Value) {
    SubclassData32 = IsUnsigned;
  }
  ~DIEnumerator() = default;

  static DIEnumerator *getImpl(LLVMContext &Context, int64_t Value,
                               bool IsUnsigned, StringRef Name,
                               StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Value, IsUnsigned,
                   getCanonicalMDString(Context, Name), Storage, ShouldCreate);
  }
  static DIEnumerator *getImpl(LLVMContext &Context, int64_t Value,
                               bool IsUnsigned, MDString *Name,
                               StorageType Storage, bool ShouldCreate = true);

  TempDIEnumerator cloneImpl() const {
    return getTemporary(getContext(), getValue(), isUnsigned(), getName());
  }

public:
  DEFINE_MDNODE_GET(DIEnumerator, (int64_t Value, bool IsUnsigned, StringRef Name),
                    (Value, IsUnsigned, Name))
  DEFINE_MDNODE_GET(DIEnumerator, (int64_t Value, bool IsUnsigned, MDString *Name),
                    (Value, IsUnsigned, Name))

  TempDIEnumerator clone() const { return cloneImpl(); }

  int64_t getValue() const { return Value; }
  bool isUnsigned() const { return SubclassData32; }
  StringRef getName() const { return getStringOperand(0); }

  MDString *getRawName() const { return getOperandAs<MDString>(0); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIEnumeratorKind;
  }
};

/// Base class for scope-like contexts.
///
/// Base class for lexical scopes and types (which are also declaration
/// contexts).
///
/// TODO: Separate the concepts of declaration contexts and lexical scopes.
class DIScope : public DINode {
protected:
  DIScope(LLVMContext &C, unsigned ID, StorageType Storage, unsigned Tag,
          ArrayRef<Metadata *> Ops)
      : DINode(C, ID, Storage, Tag, Ops) {}
  ~DIScope() = default;

public:
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }

  inline StringRef getFilename() const;
  inline StringRef getDirectory() const;
  inline Optional<StringRef> getSource() const;

  StringRef getName() const;
  DIScopeRef getScope() const;

  /// Return the raw underlying file.
  ///
  /// A \a DIFile is a \a DIScope, but it doesn't point at a separate file (it
  /// \em is the file).  If \c this is an \a DIFile, we need to return \c this.
  /// Otherwise, return the first operand, which is where all other subclasses
  /// store their file pointer.
  Metadata *getRawFile() const {
    return isa<DIFile>(this) ? const_cast<DIScope *>(this)
                             : static_cast<Metadata *>(getOperand(0));
  }

  static bool classof(const Metadata *MD) {
    switch (MD->getMetadataID()) {
    default:
      return false;
    case DIBasicTypeKind:
    case DIDerivedTypeKind:
    case DICompositeTypeKind:
    case DISubroutineTypeKind:
    case DIFileKind:
    case DICompileUnitKind:
    case DISubprogramKind:
    case DILexicalBlockKind:
    case DILexicalBlockFileKind:
    case DINamespaceKind:
    case DIModuleKind:
      return true;
    }
  }
};

/// File.
///
/// TODO: Merge with directory/file node (including users).
/// TODO: Canonicalize paths on creation.
class DIFile : public DIScope {
  friend class LLVMContextImpl;
  friend class MDNode;

public:
  /// Which algorithm (e.g. MD5) a checksum was generated with.
  ///
  /// The encoding is explicit because it is used directly in Bitcode. The
  /// value 0 is reserved to indicate the absence of a checksum in Bitcode.
  enum ChecksumKind {
    // The first variant was originally CSK_None, encoded as 0. The new
    // internal representation removes the need for this by wrapping the
    // ChecksumInfo in an Optional, but to preserve Bitcode compatibility the 0
    // encoding is reserved.
    CSK_MD5 = 1,
    CSK_SHA1 = 2,
    CSK_Last = CSK_SHA1 // Should be last enumeration.
  };

  /// A single checksum, represented by a \a Kind and a \a Value (a string).
  template <typename T>
  struct ChecksumInfo {
    /// The kind of checksum which \a Value encodes.
    ChecksumKind Kind;
    /// The string value of the checksum.
    T Value;

    ChecksumInfo(ChecksumKind Kind, T Value) : Kind(Kind), Value(Value) { }
    ~ChecksumInfo() = default;
    bool operator==(const ChecksumInfo<T> &X) const {
      return Kind == X.Kind && Value == X.Value;
    }
    bool operator!=(const ChecksumInfo<T> &X) const { return !(*this == X); }
    StringRef getKindAsString() const { return getChecksumKindAsString(Kind); }
  };

private:
  Optional<ChecksumInfo<MDString *>> Checksum;
  Optional<MDString *> Source;

  DIFile(LLVMContext &C, StorageType Storage,
         Optional<ChecksumInfo<MDString *>> CS, Optional<MDString *> Src,
         ArrayRef<Metadata *> Ops)
      : DIScope(C, DIFileKind, Storage, dwarf::DW_TAG_file_type, Ops),
        Checksum(CS), Source(Src) {}
  ~DIFile() = default;

  static DIFile *getImpl(LLVMContext &Context, StringRef Filename,
                         StringRef Directory,
                         Optional<ChecksumInfo<StringRef>> CS,
                         Optional<StringRef> Source,
                         StorageType Storage, bool ShouldCreate = true) {
    Optional<ChecksumInfo<MDString *>> MDChecksum;
    if (CS)
      MDChecksum.emplace(CS->Kind, getCanonicalMDString(Context, CS->Value));
    return getImpl(Context, getCanonicalMDString(Context, Filename),
                   getCanonicalMDString(Context, Directory), MDChecksum,
                   Source ? Optional<MDString *>(getCanonicalMDString(Context, *Source)) : None,
                   Storage, ShouldCreate);
  }
  static DIFile *getImpl(LLVMContext &Context, MDString *Filename,
                         MDString *Directory,
                         Optional<ChecksumInfo<MDString *>> CS,
                         Optional<MDString *> Source, StorageType Storage,
                         bool ShouldCreate = true);

  TempDIFile cloneImpl() const {
    return getTemporary(getContext(), getFilename(), getDirectory(),
                        getChecksum(), getSource());
  }

public:
  DEFINE_MDNODE_GET(DIFile, (StringRef Filename, StringRef Directory,
                             Optional<ChecksumInfo<StringRef>> CS = None,
                             Optional<StringRef> Source = None),
                    (Filename, Directory, CS, Source))
  DEFINE_MDNODE_GET(DIFile, (MDString * Filename, MDString *Directory,
                             Optional<ChecksumInfo<MDString *>> CS = None,
                             Optional<MDString *> Source = None),
                    (Filename, Directory, CS, Source))

  TempDIFile clone() const { return cloneImpl(); }

  StringRef getFilename() const { return getStringOperand(0); }
  StringRef getDirectory() const { return getStringOperand(1); }
  Optional<ChecksumInfo<StringRef>> getChecksum() const {
    Optional<ChecksumInfo<StringRef>> StringRefChecksum;
    if (Checksum)
      StringRefChecksum.emplace(Checksum->Kind, Checksum->Value->getString());
    return StringRefChecksum;
  }
  Optional<StringRef> getSource() const {
    return Source ? Optional<StringRef>((*Source)->getString()) : None;
  }

  MDString *getRawFilename() const { return getOperandAs<MDString>(0); }
  MDString *getRawDirectory() const { return getOperandAs<MDString>(1); }
  Optional<ChecksumInfo<MDString *>> getRawChecksum() const { return Checksum; }
  Optional<MDString *> getRawSource() const { return Source; }

  static StringRef getChecksumKindAsString(ChecksumKind CSKind);
  static Optional<ChecksumKind> getChecksumKind(StringRef CSKindStr);

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIFileKind;
  }
};

StringRef DIScope::getFilename() const {
  if (auto *F = getFile())
    return F->getFilename();
  return "";
}

StringRef DIScope::getDirectory() const {
  if (auto *F = getFile())
    return F->getDirectory();
  return "";
}

Optional<StringRef> DIScope::getSource() const {
  if (auto *F = getFile())
    return F->getSource();
  return None;
}

/// Base class for types.
///
/// TODO: Remove the hardcoded name and context, since many types don't use
/// them.
/// TODO: Split up flags.
class DIType : public DIScope {
  unsigned Line;
  DIFlags Flags;
  uint64_t SizeInBits;
  uint64_t OffsetInBits;
  uint32_t AlignInBits;

protected:
  DIType(LLVMContext &C, unsigned ID, StorageType Storage, unsigned Tag,
         unsigned Line, uint64_t SizeInBits, uint32_t AlignInBits,
         uint64_t OffsetInBits, DIFlags Flags, ArrayRef<Metadata *> Ops)
      : DIScope(C, ID, Storage, Tag, Ops) {
    init(Line, SizeInBits, AlignInBits, OffsetInBits, Flags);
  }
  ~DIType() = default;

  void init(unsigned Line, uint64_t SizeInBits, uint32_t AlignInBits,
            uint64_t OffsetInBits, DIFlags Flags) {
    this->Line = Line;
    this->Flags = Flags;
    this->SizeInBits = SizeInBits;
    this->AlignInBits = AlignInBits;
    this->OffsetInBits = OffsetInBits;
  }

  /// Change fields in place.
  void mutate(unsigned Tag, unsigned Line, uint64_t SizeInBits,
              uint32_t AlignInBits, uint64_t OffsetInBits, DIFlags Flags) {
    assert(isDistinct() && "Only distinct nodes can mutate");
    setTag(Tag);
    init(Line, SizeInBits, AlignInBits, OffsetInBits, Flags);
  }

public:
  TempDIType clone() const {
    return TempDIType(cast<DIType>(MDNode::clone().release()));
  }

  unsigned getLine() const { return Line; }
  uint64_t getSizeInBits() const { return SizeInBits; }
  uint32_t getAlignInBits() const { return AlignInBits; }
  uint32_t getAlignInBytes() const { return getAlignInBits() / CHAR_BIT; }
  uint64_t getOffsetInBits() const { return OffsetInBits; }
  DIFlags getFlags() const { return Flags; }

  DIScopeRef getScope() const { return DIScopeRef(getRawScope()); }
  StringRef getName() const { return getStringOperand(2); }


  Metadata *getRawScope() const { return getOperand(1); }
  MDString *getRawName() const { return getOperandAs<MDString>(2); }

  /// Returns a new temporary DIType with updated Flags
  TempDIType cloneWithFlags(DIFlags NewFlags) const {
    auto NewTy = clone();
    NewTy->Flags = NewFlags;
    return NewTy;
  }

  bool isPrivate() const {
    return (getFlags() & FlagAccessibility) == FlagPrivate;
  }
  bool isProtected() const {
    return (getFlags() & FlagAccessibility) == FlagProtected;
  }
  bool isPublic() const {
    return (getFlags() & FlagAccessibility) == FlagPublic;
  }
  bool isForwardDecl() const { return getFlags() & FlagFwdDecl; }
  bool isAppleBlockExtension() const { return getFlags() & FlagAppleBlock; }
  bool isBlockByrefStruct() const { return getFlags() & FlagBlockByrefStruct; }
  bool isVirtual() const { return getFlags() & FlagVirtual; }
  bool isArtificial() const { return getFlags() & FlagArtificial; }
  bool isObjectPointer() const { return getFlags() & FlagObjectPointer; }
  bool isObjcClassComplete() const {
    return getFlags() & FlagObjcClassComplete;
  }
  bool isVector() const { return getFlags() & FlagVector; }
  bool isBitField() const { return getFlags() & FlagBitField; }
  bool isStaticMember() const { return getFlags() & FlagStaticMember; }
  bool isLValueReference() const { return getFlags() & FlagLValueReference; }
  bool isRValueReference() const { return getFlags() & FlagRValueReference; }
  bool isTypePassByValue() const { return getFlags() & FlagTypePassByValue; }
  bool isTypePassByReference() const {
    return getFlags() & FlagTypePassByReference;
  }
  bool isBigEndian() const { return getFlags() & FlagBigEndian; }
  bool isLittleEndian() const { return getFlags() & FlagLittleEndian; }

  static bool classof(const Metadata *MD) {
    switch (MD->getMetadataID()) {
    default:
      return false;
    case DIBasicTypeKind:
    case DIDerivedTypeKind:
    case DICompositeTypeKind:
    case DISubroutineTypeKind:
      return true;
    }
  }
};

/// Basic type, like 'int' or 'float'.
///
/// TODO: Split out DW_TAG_unspecified_type.
/// TODO: Drop unused accessors.
class DIBasicType : public DIType {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Encoding;

  DIBasicType(LLVMContext &C, StorageType Storage, unsigned Tag,
              uint64_t SizeInBits, uint32_t AlignInBits, unsigned Encoding,
              DIFlags Flags, ArrayRef<Metadata *> Ops)
      : DIType(C, DIBasicTypeKind, Storage, Tag, 0, SizeInBits, AlignInBits, 0,
               Flags, Ops),
        Encoding(Encoding) {}
  ~DIBasicType() = default;

  static DIBasicType *getImpl(LLVMContext &Context, unsigned Tag,
                              StringRef Name, uint64_t SizeInBits,
                              uint32_t AlignInBits, unsigned Encoding,
                              DIFlags Flags, StorageType Storage,
                              bool ShouldCreate = true) {
    return getImpl(Context, Tag, getCanonicalMDString(Context, Name),
                   SizeInBits, AlignInBits, Encoding, Flags, Storage,
                   ShouldCreate);
  }
  static DIBasicType *getImpl(LLVMContext &Context, unsigned Tag,
                              MDString *Name, uint64_t SizeInBits,
                              uint32_t AlignInBits, unsigned Encoding,
                              DIFlags Flags, StorageType Storage,
                              bool ShouldCreate = true);

  TempDIBasicType cloneImpl() const {
    return getTemporary(getContext(), getTag(), getName(), getSizeInBits(),
                        getAlignInBits(), getEncoding(), getFlags());
  }

public:
  DEFINE_MDNODE_GET(DIBasicType, (unsigned Tag, StringRef Name),
                    (Tag, Name, 0, 0, 0, FlagZero))
  DEFINE_MDNODE_GET(DIBasicType,
                    (unsigned Tag, StringRef Name, uint64_t SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding, DIFlags Flags),
                    (Tag, Name, SizeInBits, AlignInBits, Encoding, Flags))
  DEFINE_MDNODE_GET(DIBasicType,
                    (unsigned Tag, MDString *Name, uint64_t SizeInBits,
                     uint32_t AlignInBits, unsigned Encoding, DIFlags Flags),
                    (Tag, Name, SizeInBits, AlignInBits, Encoding, Flags))

  TempDIBasicType clone() const { return cloneImpl(); }

  unsigned getEncoding() const { return Encoding; }

  enum class Signedness { Signed, Unsigned };

  /// Return the signedness of this type, or None if this type is neither
  /// signed nor unsigned.
  Optional<Signedness> getSignedness() const;

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIBasicTypeKind;
  }
};

/// Derived types.
///
/// This includes qualified types, pointers, references, friends, typedefs, and
/// class members.
///
/// TODO: Split out members (inheritance, fields, methods, etc.).
class DIDerivedType : public DIType {
  friend class LLVMContextImpl;
  friend class MDNode;

  /// The DWARF address space of the memory pointed to or referenced by a
  /// pointer or reference type respectively.
  Optional<unsigned> DWARFAddressSpace;

  DIDerivedType(LLVMContext &C, StorageType Storage, unsigned Tag,
                unsigned Line, uint64_t SizeInBits, uint32_t AlignInBits,
                uint64_t OffsetInBits, Optional<unsigned> DWARFAddressSpace,
                DIFlags Flags, ArrayRef<Metadata *> Ops)
      : DIType(C, DIDerivedTypeKind, Storage, Tag, Line, SizeInBits,
               AlignInBits, OffsetInBits, Flags, Ops),
        DWARFAddressSpace(DWARFAddressSpace) {}
  ~DIDerivedType() = default;

  static DIDerivedType *getImpl(LLVMContext &Context, unsigned Tag,
                                StringRef Name, DIFile *File, unsigned Line,
                                DIScopeRef Scope, DITypeRef BaseType,
                                uint64_t SizeInBits, uint32_t AlignInBits,
                                uint64_t OffsetInBits,
                                Optional<unsigned> DWARFAddressSpace,
                                DIFlags Flags, Metadata *ExtraData,
                                StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Tag, getCanonicalMDString(Context, Name), File,
                   Line, Scope, BaseType, SizeInBits, AlignInBits, OffsetInBits,
                   DWARFAddressSpace, Flags, ExtraData, Storage, ShouldCreate);
  }
  static DIDerivedType *getImpl(LLVMContext &Context, unsigned Tag,
                                MDString *Name, Metadata *File, unsigned Line,
                                Metadata *Scope, Metadata *BaseType,
                                uint64_t SizeInBits, uint32_t AlignInBits,
                                uint64_t OffsetInBits,
                                Optional<unsigned> DWARFAddressSpace,
                                DIFlags Flags, Metadata *ExtraData,
                                StorageType Storage, bool ShouldCreate = true);

  TempDIDerivedType cloneImpl() const {
    return getTemporary(getContext(), getTag(), getName(), getFile(), getLine(),
                        getScope(), getBaseType(), getSizeInBits(),
                        getAlignInBits(), getOffsetInBits(),
                        getDWARFAddressSpace(), getFlags(), getExtraData());
  }

public:
  DEFINE_MDNODE_GET(DIDerivedType,
                    (unsigned Tag, MDString *Name, Metadata *File,
                     unsigned Line, Metadata *Scope, Metadata *BaseType,
                     uint64_t SizeInBits, uint32_t AlignInBits,
                     uint64_t OffsetInBits,
                     Optional<unsigned> DWARFAddressSpace, DIFlags Flags,
                     Metadata *ExtraData = nullptr),
                    (Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                     AlignInBits, OffsetInBits, DWARFAddressSpace, Flags,
                     ExtraData))
  DEFINE_MDNODE_GET(DIDerivedType,
                    (unsigned Tag, StringRef Name, DIFile *File, unsigned Line,
                     DIScopeRef Scope, DITypeRef BaseType, uint64_t SizeInBits,
                     uint32_t AlignInBits, uint64_t OffsetInBits,
                     Optional<unsigned> DWARFAddressSpace, DIFlags Flags,
                     Metadata *ExtraData = nullptr),
                    (Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                     AlignInBits, OffsetInBits, DWARFAddressSpace, Flags,
                     ExtraData))

  TempDIDerivedType clone() const { return cloneImpl(); }

  /// Get the base type this is derived from.
  DITypeRef getBaseType() const { return DITypeRef(getRawBaseType()); }
  Metadata *getRawBaseType() const { return getOperand(3); }

  /// \returns The DWARF address space of the memory pointed to or referenced by
  /// a pointer or reference type respectively.
  Optional<unsigned> getDWARFAddressSpace() const { return DWARFAddressSpace; }

  /// Get extra data associated with this derived type.
  ///
  /// Class type for pointer-to-members, objective-c property node for ivars,
  /// global constant wrapper for static members, or virtual base pointer offset
  /// for inheritance.
  ///
  /// TODO: Separate out types that need this extra operand: pointer-to-member
  /// types and member fields (static members and ivars).
  Metadata *getExtraData() const { return getRawExtraData(); }
  Metadata *getRawExtraData() const { return getOperand(4); }

  /// Get casted version of extra data.
  /// @{
  DITypeRef getClassType() const {
    assert(getTag() == dwarf::DW_TAG_ptr_to_member_type);
    return DITypeRef(getExtraData());
  }

  DIObjCProperty *getObjCProperty() const {
    return dyn_cast_or_null<DIObjCProperty>(getExtraData());
  }

  uint32_t getVBPtrOffset() const {
    assert(getTag() == dwarf::DW_TAG_inheritance);
    if (auto *CM = cast_or_null<ConstantAsMetadata>(getExtraData()))
      if (auto *CI = dyn_cast_or_null<ConstantInt>(CM->getValue()))
        return static_cast<uint32_t>(CI->getZExtValue());
    return 0;
  }

  Constant *getStorageOffsetInBits() const {
    assert(getTag() == dwarf::DW_TAG_member && isBitField());
    if (auto *C = cast_or_null<ConstantAsMetadata>(getExtraData()))
      return C->getValue();
    return nullptr;
  }

  Constant *getConstant() const {
    assert(getTag() == dwarf::DW_TAG_member && isStaticMember());
    if (auto *C = cast_or_null<ConstantAsMetadata>(getExtraData()))
      return C->getValue();
    return nullptr;
  }
  Constant *getDiscriminantValue() const {
    assert(getTag() == dwarf::DW_TAG_member && !isStaticMember());
    if (auto *C = cast_or_null<ConstantAsMetadata>(getExtraData()))
      return C->getValue();
    return nullptr;
  }
  /// @}

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIDerivedTypeKind;
  }
};

/// Composite types.
///
/// TODO: Detach from DerivedTypeBase (split out MDEnumType?).
/// TODO: Create a custom, unrelated node for DW_TAG_array_type.
class DICompositeType : public DIType {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned RuntimeLang;

  DICompositeType(LLVMContext &C, StorageType Storage, unsigned Tag,
                  unsigned Line, unsigned RuntimeLang, uint64_t SizeInBits,
                  uint32_t AlignInBits, uint64_t OffsetInBits, DIFlags Flags,
                  ArrayRef<Metadata *> Ops)
      : DIType(C, DICompositeTypeKind, Storage, Tag, Line, SizeInBits,
               AlignInBits, OffsetInBits, Flags, Ops),
        RuntimeLang(RuntimeLang) {}
  ~DICompositeType() = default;

  /// Change fields in place.
  void mutate(unsigned Tag, unsigned Line, unsigned RuntimeLang,
              uint64_t SizeInBits, uint32_t AlignInBits,
              uint64_t OffsetInBits, DIFlags Flags) {
    assert(isDistinct() && "Only distinct nodes can mutate");
    assert(getRawIdentifier() && "Only ODR-uniqued nodes should mutate");
    this->RuntimeLang = RuntimeLang;
    DIType::mutate(Tag, Line, SizeInBits, AlignInBits, OffsetInBits, Flags);
  }

  static DICompositeType *
  getImpl(LLVMContext &Context, unsigned Tag, StringRef Name, Metadata *File,
          unsigned Line, DIScopeRef Scope, DITypeRef BaseType,
          uint64_t SizeInBits, uint32_t AlignInBits, uint64_t OffsetInBits,
          DIFlags Flags, DINodeArray Elements, unsigned RuntimeLang,
          DITypeRef VTableHolder, DITemplateParameterArray TemplateParams,
          StringRef Identifier, DIDerivedType *Discriminator,
          StorageType Storage, bool ShouldCreate = true) {
    return getImpl(
        Context, Tag, getCanonicalMDString(Context, Name), File, Line, Scope,
        BaseType, SizeInBits, AlignInBits, OffsetInBits, Flags, Elements.get(),
        RuntimeLang, VTableHolder, TemplateParams.get(),
        getCanonicalMDString(Context, Identifier), Discriminator, Storage, ShouldCreate);
  }
  static DICompositeType *
  getImpl(LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *File,
          unsigned Line, Metadata *Scope, Metadata *BaseType,
          uint64_t SizeInBits, uint32_t AlignInBits, uint64_t OffsetInBits,
          DIFlags Flags, Metadata *Elements, unsigned RuntimeLang,
          Metadata *VTableHolder, Metadata *TemplateParams,
          MDString *Identifier, Metadata *Discriminator,
          StorageType Storage, bool ShouldCreate = true);

  TempDICompositeType cloneImpl() const {
    return getTemporary(getContext(), getTag(), getName(), getFile(), getLine(),
                        getScope(), getBaseType(), getSizeInBits(),
                        getAlignInBits(), getOffsetInBits(), getFlags(),
                        getElements(), getRuntimeLang(), getVTableHolder(),
                        getTemplateParams(), getIdentifier(), getDiscriminator());
  }

public:
  DEFINE_MDNODE_GET(DICompositeType,
                    (unsigned Tag, StringRef Name, DIFile *File, unsigned Line,
                     DIScopeRef Scope, DITypeRef BaseType, uint64_t SizeInBits,
                     uint32_t AlignInBits, uint64_t OffsetInBits,
                     DIFlags Flags, DINodeArray Elements, unsigned RuntimeLang,
                     DITypeRef VTableHolder,
                     DITemplateParameterArray TemplateParams = nullptr,
                     StringRef Identifier = "", DIDerivedType *Discriminator = nullptr),
                    (Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                     AlignInBits, OffsetInBits, Flags, Elements, RuntimeLang,
                     VTableHolder, TemplateParams, Identifier, Discriminator))
  DEFINE_MDNODE_GET(DICompositeType,
                    (unsigned Tag, MDString *Name, Metadata *File,
                     unsigned Line, Metadata *Scope, Metadata *BaseType,
                     uint64_t SizeInBits, uint32_t AlignInBits,
                     uint64_t OffsetInBits, DIFlags Flags, Metadata *Elements,
                     unsigned RuntimeLang, Metadata *VTableHolder,
                     Metadata *TemplateParams = nullptr,
                     MDString *Identifier = nullptr,
                     Metadata *Discriminator = nullptr),
                    (Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                     AlignInBits, OffsetInBits, Flags, Elements, RuntimeLang,
                     VTableHolder, TemplateParams, Identifier, Discriminator))

  TempDICompositeType clone() const { return cloneImpl(); }

  /// Get a DICompositeType with the given ODR identifier.
  ///
  /// If \a LLVMContext::isODRUniquingDebugTypes(), gets the mapped
  /// DICompositeType for the given ODR \c Identifier.  If none exists, creates
  /// a new node.
  ///
  /// Else, returns \c nullptr.
  static DICompositeType *
  getODRType(LLVMContext &Context, MDString &Identifier, unsigned Tag,
             MDString *Name, Metadata *File, unsigned Line, Metadata *Scope,
             Metadata *BaseType, uint64_t SizeInBits, uint32_t AlignInBits,
             uint64_t OffsetInBits, DIFlags Flags, Metadata *Elements,
             unsigned RuntimeLang, Metadata *VTableHolder,
             Metadata *TemplateParams, Metadata *Discriminator);
  static DICompositeType *getODRTypeIfExists(LLVMContext &Context,
                                             MDString &Identifier);

  /// Build a DICompositeType with the given ODR identifier.
  ///
  /// Looks up the mapped DICompositeType for the given ODR \c Identifier.  If
  /// it doesn't exist, creates a new one.  If it does exist and \a
  /// isForwardDecl(), and the new arguments would be a definition, mutates the
  /// the type in place.  In either case, returns the type.
  ///
  /// If not \a LLVMContext::isODRUniquingDebugTypes(), this function returns
  /// nullptr.
  static DICompositeType *
  buildODRType(LLVMContext &Context, MDString &Identifier, unsigned Tag,
               MDString *Name, Metadata *File, unsigned Line, Metadata *Scope,
               Metadata *BaseType, uint64_t SizeInBits, uint32_t AlignInBits,
               uint64_t OffsetInBits, DIFlags Flags, Metadata *Elements,
               unsigned RuntimeLang, Metadata *VTableHolder,
               Metadata *TemplateParams, Metadata *Discriminator);

  DITypeRef getBaseType() const { return DITypeRef(getRawBaseType()); }
  DINodeArray getElements() const {
    return cast_or_null<MDTuple>(getRawElements());
  }
  DITypeRef getVTableHolder() const { return DITypeRef(getRawVTableHolder()); }
  DITemplateParameterArray getTemplateParams() const {
    return cast_or_null<MDTuple>(getRawTemplateParams());
  }
  StringRef getIdentifier() const { return getStringOperand(7); }
  unsigned getRuntimeLang() const { return RuntimeLang; }

  Metadata *getRawBaseType() const { return getOperand(3); }
  Metadata *getRawElements() const { return getOperand(4); }
  Metadata *getRawVTableHolder() const { return getOperand(5); }
  Metadata *getRawTemplateParams() const { return getOperand(6); }
  MDString *getRawIdentifier() const { return getOperandAs<MDString>(7); }
  Metadata *getRawDiscriminator() const { return getOperand(8); }
  DIDerivedType *getDiscriminator() const { return getOperandAs<DIDerivedType>(8); }

  /// Replace operands.
  ///
  /// If this \a isUniqued() and not \a isResolved(), on a uniquing collision
  /// this will be RAUW'ed and deleted.  Use a \a TrackingMDRef to keep track
  /// of its movement if necessary.
  /// @{
  void replaceElements(DINodeArray Elements) {
#ifndef NDEBUG
    for (DINode *Op : getElements())
      assert(is_contained(Elements->operands(), Op) &&
             "Lost a member during member list replacement");
#endif
    replaceOperandWith(4, Elements.get());
  }

  void replaceVTableHolder(DITypeRef VTableHolder) {
    replaceOperandWith(5, VTableHolder);
  }

  void replaceTemplateParams(DITemplateParameterArray TemplateParams) {
    replaceOperandWith(6, TemplateParams.get());
  }
  /// @}

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DICompositeTypeKind;
  }
};

/// Type array for a subprogram.
///
/// TODO: Fold the array of types in directly as operands.
class DISubroutineType : public DIType {
  friend class LLVMContextImpl;
  friend class MDNode;

  /// The calling convention used with DW_AT_calling_convention. Actually of
  /// type dwarf::CallingConvention.
  uint8_t CC;

  DISubroutineType(LLVMContext &C, StorageType Storage, DIFlags Flags,
                   uint8_t CC, ArrayRef<Metadata *> Ops)
      : DIType(C, DISubroutineTypeKind, Storage, dwarf::DW_TAG_subroutine_type,
               0, 0, 0, 0, Flags, Ops),
        CC(CC) {}
  ~DISubroutineType() = default;

  static DISubroutineType *getImpl(LLVMContext &Context, DIFlags Flags,
                                   uint8_t CC, DITypeRefArray TypeArray,
                                   StorageType Storage,
                                   bool ShouldCreate = true) {
    return getImpl(Context, Flags, CC, TypeArray.get(), Storage, ShouldCreate);
  }
  static DISubroutineType *getImpl(LLVMContext &Context, DIFlags Flags,
                                   uint8_t CC, Metadata *TypeArray,
                                   StorageType Storage,
                                   bool ShouldCreate = true);

  TempDISubroutineType cloneImpl() const {
    return getTemporary(getContext(), getFlags(), getCC(), getTypeArray());
  }

public:
  DEFINE_MDNODE_GET(DISubroutineType,
                    (DIFlags Flags, uint8_t CC, DITypeRefArray TypeArray),
                    (Flags, CC, TypeArray))
  DEFINE_MDNODE_GET(DISubroutineType,
                    (DIFlags Flags, uint8_t CC, Metadata *TypeArray),
                    (Flags, CC, TypeArray))

  TempDISubroutineType clone() const { return cloneImpl(); }

  uint8_t getCC() const { return CC; }

  DITypeRefArray getTypeArray() const {
    return cast_or_null<MDTuple>(getRawTypeArray());
  }

  Metadata *getRawTypeArray() const { return getOperand(3); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DISubroutineTypeKind;
  }
};

/// Compile unit.
class DICompileUnit : public DIScope {
  friend class LLVMContextImpl;
  friend class MDNode;

public:
  enum DebugEmissionKind : unsigned {
    NoDebug = 0,
    FullDebug,
    LineTablesOnly,
    DebugDirectivesOnly,
    LastEmissionKind = DebugDirectivesOnly
  };

  enum class DebugNameTableKind : unsigned {
    Default = 0,
    GNU = 1,
    None = 2,
    LastDebugNameTableKind = None
  };

  static Optional<DebugEmissionKind> getEmissionKind(StringRef Str);
  static const char *emissionKindString(DebugEmissionKind EK);
  static Optional<DebugNameTableKind> getNameTableKind(StringRef Str);
  static const char *nameTableKindString(DebugNameTableKind PK);

private:
  unsigned SourceLanguage;
  bool IsOptimized;
  unsigned RuntimeVersion;
  unsigned EmissionKind;
  uint64_t DWOId;
  bool SplitDebugInlining;
  bool DebugInfoForProfiling;
  unsigned NameTableKind;
  bool RangesBaseAddress;

  DICompileUnit(LLVMContext &C, StorageType Storage, unsigned SourceLanguage,
                bool IsOptimized, unsigned RuntimeVersion,
                unsigned EmissionKind, uint64_t DWOId, bool SplitDebugInlining,
                bool DebugInfoForProfiling, unsigned NameTableKind,
                bool RangesBaseAddress, ArrayRef<Metadata *> Ops)
      : DIScope(C, DICompileUnitKind, Storage, dwarf::DW_TAG_compile_unit, Ops),
        SourceLanguage(SourceLanguage), IsOptimized(IsOptimized),
        RuntimeVersion(RuntimeVersion), EmissionKind(EmissionKind),
        DWOId(DWOId), SplitDebugInlining(SplitDebugInlining),
        DebugInfoForProfiling(DebugInfoForProfiling),
        NameTableKind(NameTableKind), RangesBaseAddress(RangesBaseAddress) {
    assert(Storage != Uniqued);
  }
  ~DICompileUnit() = default;

  static DICompileUnit *
  getImpl(LLVMContext &Context, unsigned SourceLanguage, DIFile *File,
          StringRef Producer, bool IsOptimized, StringRef Flags,
          unsigned RuntimeVersion, StringRef SplitDebugFilename,
          unsigned EmissionKind, DICompositeTypeArray EnumTypes,
          DIScopeArray RetainedTypes,
          DIGlobalVariableExpressionArray GlobalVariables,
          DIImportedEntityArray ImportedEntities, DIMacroNodeArray Macros,
          uint64_t DWOId, bool SplitDebugInlining, bool DebugInfoForProfiling,
          unsigned NameTableKind, bool RangesBaseAddress, StorageType Storage,
          bool ShouldCreate = true) {
    return getImpl(Context, SourceLanguage, File,
                   getCanonicalMDString(Context, Producer), IsOptimized,
                   getCanonicalMDString(Context, Flags), RuntimeVersion,
                   getCanonicalMDString(Context, SplitDebugFilename),
                   EmissionKind, EnumTypes.get(), RetainedTypes.get(),
                   GlobalVariables.get(), ImportedEntities.get(), Macros.get(),
                   DWOId, SplitDebugInlining, DebugInfoForProfiling,
                   NameTableKind, RangesBaseAddress, Storage, ShouldCreate);
  }
  static DICompileUnit *
  getImpl(LLVMContext &Context, unsigned SourceLanguage, Metadata *File,
          MDString *Producer, bool IsOptimized, MDString *Flags,
          unsigned RuntimeVersion, MDString *SplitDebugFilename,
          unsigned EmissionKind, Metadata *EnumTypes, Metadata *RetainedTypes,
          Metadata *GlobalVariables, Metadata *ImportedEntities,
          Metadata *Macros, uint64_t DWOId, bool SplitDebugInlining,
          bool DebugInfoForProfiling, unsigned NameTableKind,
          bool RangesBaseAddress, StorageType Storage, bool ShouldCreate = true);

  TempDICompileUnit cloneImpl() const {
    return getTemporary(
        getContext(), getSourceLanguage(), getFile(), getProducer(),
        isOptimized(), getFlags(), getRuntimeVersion(), getSplitDebugFilename(),
        getEmissionKind(), getEnumTypes(), getRetainedTypes(),
        getGlobalVariables(), getImportedEntities(), getMacros(), DWOId,
        getSplitDebugInlining(), getDebugInfoForProfiling(), getNameTableKind(),
        getRangesBaseAddress());
  }

public:
  static void get() = delete;
  static void getIfExists() = delete;

  DEFINE_MDNODE_GET_DISTINCT_TEMPORARY(
      DICompileUnit,
      (unsigned SourceLanguage, DIFile *File, StringRef Producer,
       bool IsOptimized, StringRef Flags, unsigned RuntimeVersion,
       StringRef SplitDebugFilename, DebugEmissionKind EmissionKind,
       DICompositeTypeArray EnumTypes, DIScopeArray RetainedTypes,
       DIGlobalVariableExpressionArray GlobalVariables,
       DIImportedEntityArray ImportedEntities, DIMacroNodeArray Macros,
       uint64_t DWOId, bool SplitDebugInlining, bool DebugInfoForProfiling,
       DebugNameTableKind NameTableKind, bool RangesBaseAddress),
      (SourceLanguage, File, Producer, IsOptimized, Flags, RuntimeVersion,
       SplitDebugFilename, EmissionKind, EnumTypes, RetainedTypes,
       GlobalVariables, ImportedEntities, Macros, DWOId, SplitDebugInlining,
       DebugInfoForProfiling, (unsigned)NameTableKind, RangesBaseAddress))
  DEFINE_MDNODE_GET_DISTINCT_TEMPORARY(
      DICompileUnit,
      (unsigned SourceLanguage, Metadata *File, MDString *Producer,
       bool IsOptimized, MDString *Flags, unsigned RuntimeVersion,
       MDString *SplitDebugFilename, unsigned EmissionKind, Metadata *EnumTypes,
       Metadata *RetainedTypes, Metadata *GlobalVariables,
       Metadata *ImportedEntities, Metadata *Macros, uint64_t DWOId,
       bool SplitDebugInlining, bool DebugInfoForProfiling,
       unsigned NameTableKind, bool RangesBaseAddress),
      (SourceLanguage, File, Producer, IsOptimized, Flags, RuntimeVersion,
       SplitDebugFilename, EmissionKind, EnumTypes, RetainedTypes,
       GlobalVariables, ImportedEntities, Macros, DWOId, SplitDebugInlining,
       DebugInfoForProfiling, NameTableKind, RangesBaseAddress))

  TempDICompileUnit clone() const { return cloneImpl(); }

  unsigned getSourceLanguage() const { return SourceLanguage; }
  bool isOptimized() const { return IsOptimized; }
  unsigned getRuntimeVersion() const { return RuntimeVersion; }
  DebugEmissionKind getEmissionKind() const {
    return (DebugEmissionKind)EmissionKind;
  }
  bool isDebugDirectivesOnly() const {
    return EmissionKind == DebugDirectivesOnly;
  }
  bool getDebugInfoForProfiling() const { return DebugInfoForProfiling; }
  DebugNameTableKind getNameTableKind() const {
    return (DebugNameTableKind)NameTableKind;
  }
  bool getRangesBaseAddress() const {
    return RangesBaseAddress; }
  StringRef getProducer() const {
    return getStringOperand(1); }
  StringRef getFlags() const {
    return getStringOperand(2); }
  StringRef getSplitDebugFilename() const {
    return getStringOperand(3); }
  DICompositeTypeArray getEnumTypes() const {
    return cast_or_null<MDTuple>(getRawEnumTypes());
  }
  DIScopeArray getRetainedTypes() const {
    return cast_or_null<MDTuple>(getRawRetainedTypes());
  }
  DIGlobalVariableExpressionArray getGlobalVariables() const {
    return cast_or_null<MDTuple>(getRawGlobalVariables());
  }
  DIImportedEntityArray getImportedEntities() const {
    return cast_or_null<MDTuple>(getRawImportedEntities());
  }
  DIMacroNodeArray getMacros() const {
    return cast_or_null<MDTuple>(getRawMacros());
  }
  uint64_t getDWOId() const { return DWOId; }
  void setDWOId(uint64_t DwoId) { DWOId = DwoId; }
  bool getSplitDebugInlining() const { return SplitDebugInlining; }
  void setSplitDebugInlining(bool SplitDebugInlining) {
    this->SplitDebugInlining = SplitDebugInlining;
  }

  MDString *getRawProducer() const { return getOperandAs<MDString>(1); }
  MDString *getRawFlags() const { return getOperandAs<MDString>(2); }
  MDString *getRawSplitDebugFilename() const {
    return getOperandAs<MDString>(3);
  }
  Metadata *getRawEnumTypes() const { return getOperand(4); }
  Metadata *getRawRetainedTypes() const { return getOperand(5); }
  Metadata *getRawGlobalVariables() const { return getOperand(6); }
  Metadata *getRawImportedEntities() const { return getOperand(7); }
  Metadata *getRawMacros() const { return getOperand(8); }

  /// Replace arrays.
  ///
  /// If this \a isUniqued() and not \a isResolved(), it will be RAUW'ed and
  /// deleted on a uniquing collision.  In practice, uniquing collisions on \a
  /// DICompileUnit should be fairly rare.
  /// @{
  void replaceEnumTypes(DICompositeTypeArray N) {
    replaceOperandWith(4, N.get());
  }
  void replaceRetainedTypes(DITypeArray N) {
    replaceOperandWith(5, N.get());
  }
  void replaceGlobalVariables(DIGlobalVariableExpressionArray N) {
    replaceOperandWith(6, N.get());
  }
  void replaceImportedEntities(DIImportedEntityArray N) {
    replaceOperandWith(7, N.get());
  }
  void replaceMacros(DIMacroNodeArray N) { replaceOperandWith(8, N.get()); }
  /// @}

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DICompileUnitKind;
  }
};

/// A scope for locals.
///
/// A legal scope for lexical blocks, local variables, and debug info
/// locations.  Subclasses are \a DISubprogram, \a DILexicalBlock, and \a
/// DILexicalBlockFile.
class DILocalScope : public DIScope {
protected:
  DILocalScope(LLVMContext &C, unsigned ID, StorageType Storage, unsigned Tag,
               ArrayRef<Metadata *> Ops)
      : DIScope(C, ID, Storage, Tag, Ops) {}
  ~DILocalScope() = default;

public:
  /// Get the subprogram for this scope.
  ///
  /// Return this if it's an \a DISubprogram; otherwise, look up the scope
  /// chain.
  DISubprogram *getSubprogram() const;

  /// Get the first non DILexicalBlockFile scope of this scope.
  ///
  /// Return this if it's not a \a DILexicalBlockFIle; otherwise, look up the
  /// scope chain.
  DILocalScope *getNonLexicalBlockFileScope() const;

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DISubprogramKind ||
           MD->getMetadataID() == DILexicalBlockKind ||
           MD->getMetadataID() == DILexicalBlockFileKind;
  }
};

/// Debug location.
///
/// A debug location in source code, used for debug info and otherwise.
class DILocation : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  DILocation(LLVMContext &C, StorageType Storage, unsigned Line,
             unsigned Column, ArrayRef<Metadata *> MDs, bool ImplicitCode);
  ~DILocation() { dropAllReferences(); }

  static DILocation *getImpl(LLVMContext &Context, unsigned Line,
                             unsigned Column, Metadata *Scope,
                             Metadata *InlinedAt, bool ImplicitCode,
                             StorageType Storage, bool ShouldCreate = true);
  static DILocation *getImpl(LLVMContext &Context, unsigned Line,
                             unsigned Column, DILocalScope *Scope,
                             DILocation *InlinedAt, bool ImplicitCode,
                             StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Line, Column, static_cast<Metadata *>(Scope),
                   static_cast<Metadata *>(InlinedAt), ImplicitCode, Storage,
                   ShouldCreate);
  }

  /// With a given unsigned int \p U, use up to 13 bits to represent it.
  /// old_bit 1~5  --> new_bit 1~5
  /// old_bit 6~12 --> new_bit 7~13
  /// new_bit_6 is 0 if higher bits (7~13) are all 0
  static unsigned getPrefixEncodingFromUnsigned(unsigned U) {
    U &= 0xfff;
    return U > 0x1f ? (((U & 0xfe0) << 1) | (U & 0x1f) | 0x20) : U;
  }

  /// Reverse transformation as getPrefixEncodingFromUnsigned.
  static unsigned getUnsignedFromPrefixEncoding(unsigned U) {
    if (U & 1)
      return 0;
    U >>= 1;
    return (U & 0x20) ? (((U >> 1) & 0xfe0) | (U & 0x1f)) : (U & 0x1f);
  }

  /// Returns the next component stored in discriminator.
  static unsigned getNextComponentInDiscriminator(unsigned D) {
    if ((D & 1) == 0)
      return D >> ((D & 0x40) ? 14 : 7);
    else
      return D >> 1;
  }

  TempDILocation cloneImpl() const {
    // Get the raw scope/inlinedAt since it is possible to invoke this on
    // a DILocation containing temporary metadata.
    return getTemporary(getContext(), getLine(), getColumn(), getRawScope(),
                        getRawInlinedAt(), isImplicitCode());
  }

  static unsigned encodeComponent(unsigned C) {
    return (C == 0) ? 1U : (getPrefixEncodingFromUnsigned(C) << 1);
  }

  static unsigned encodingBits(unsigned C) {
    return (C == 0) ? 1 : (C > 0x1f ? 14 : 7);
  }

public:
  // Disallow replacing operands.
  void replaceOperandWith(unsigned I, Metadata *New) = delete;

  DEFINE_MDNODE_GET(DILocation,
                    (unsigned Line, unsigned Column, Metadata *Scope,
                     Metadata *InlinedAt = nullptr, bool ImplicitCode = false),
                    (Line, Column, Scope, InlinedAt, ImplicitCode))
  DEFINE_MDNODE_GET(DILocation,
                    (unsigned Line, unsigned Column, DILocalScope *Scope,
                     DILocation *InlinedAt = nullptr,
                     bool ImplicitCode = false),
                    (Line, Column, Scope, InlinedAt, ImplicitCode))

  /// Return a (temporary) clone of this.
  TempDILocation clone() const { return cloneImpl(); }

  unsigned getLine() const { return SubclassData32; }
  unsigned getColumn() const { return SubclassData16; }
  DILocalScope *getScope() const { return cast<DILocalScope>(getRawScope()); }

  DILocation *getInlinedAt() const {
    return cast_or_null<DILocation>(getRawInlinedAt());
  }

  /// Check if the location corresponds to an implicit code.
  /// When the ImplicitCode flag is true, it means that the Instruction
  /// with this DILocation has been added by the front-end but it hasn't been
  /// written explicitly by the user (e.g. cleanup stuff in C++ put on a closing
  /// bracket). It's useful for code coverage to not show a counter on "empty"
  /// lines.
  bool isImplicitCode() const { return ImplicitCode; }
  void setImplicitCode(bool ImplicitCode) { this->ImplicitCode = ImplicitCode; }

  DIFile *getFile() const { return getScope()->getFile(); }
  StringRef getFilename() const { return getScope()->getFilename(); }
  StringRef getDirectory() const { return getScope()->getDirectory(); }
  Optional<StringRef> getSource() const { return getScope()->getSource(); }

  /// Get the scope where this is inlined.
  ///
  /// Walk through \a getInlinedAt() and return \a getScope() from the deepest
  /// location.
  DILocalScope *getInlinedAtScope() const {
    if (auto *IA = getInlinedAt())
      return IA->getInlinedAtScope();
    return getScope();
  }

  /// Get the DWARF discriminator.
  ///
  /// DWARF discriminators distinguish identical file locations between
  /// instructions that are on different basic blocks.
  ///
  /// There are 3 components stored in discriminator, from lower bits:
  ///
  /// Base discriminator: assigned by AddDiscriminators pass to identify IRs
  ///                     that are defined by the same source line, but
  ///                     different basic blocks.
  /// Duplication factor: assigned by optimizations that will scale down
  ///                     the execution frequency of the original IR.
  /// Copy Identifier: assigned by optimizations that clones the IR.
  ///                  Each copy of the IR will be assigned an identifier.
  ///
  /// Encoding:
  ///
  /// The above 3 components are encoded into a 32bit unsigned integer in
  /// order. If the lowest bit is 1, the current component is empty, and the
  /// next component will start in the next bit. Otherwise, the current
  /// component is non-empty, and its content starts in the next bit. The
  /// value of each components is either 5 bit or 12 bit: if the 7th bit
  /// is 0, the bit 2~6 (5 bits) are used to represent the component; if the
  /// 7th bit is 1, the bit 2~6 (5 bits) and 8~14 (7 bits) are combined to
  /// represent the component. Thus, the number of bits used for a component
  /// is either 0 (if it and all the next components are empty); 1 - if it is
  /// empty; 7 - if its value is up to and including 0x1f (lsb and msb are both
  /// 0); or 14, if its value is up to and including 0x1ff. Note that the last
  /// component is also capped at 0x1ff, even in the case when both first
  /// components are 0, and we'd technically have 29 bits available.
  ///
  /// For precise control over the data being encoded in the discriminator,
  /// use encodeDiscriminator/decodeDiscriminator.
  ///
  /// Use {get|set}BaseDiscriminator and cloneWithDuplicationFactor after reading
  /// their documentation, as their behavior has side-effects.

  inline unsigned getDiscriminator() const;

  /// Returns a new DILocation with updated \p Discriminator.
  inline const DILocation *cloneWithDiscriminator(unsigned Discriminator) const;

  /// Returns a new DILocation with updated base discriminator \p BD. Only the
  /// base discriminator is set in the new DILocation, the other encoded values
  /// are elided.
  /// If the discriminator cannot be encoded, the function returns None.
  inline Optional<const DILocation *> setBaseDiscriminator(unsigned BD) const;

  /// Returns the duplication factor stored in the discriminator, or 1 if no
  /// duplication factor (or 0) is encoded.
  inline unsigned getDuplicationFactor() const;

  /// Returns the copy identifier stored in the discriminator.
  inline unsigned getCopyIdentifier() const;

  /// Returns the base discriminator stored in the discriminator.
  inline unsigned getBaseDiscriminator() const;

  /// Returns a new DILocation with duplication factor \p DF * current
  /// duplication factor encoded in the discriminator. The current duplication
  /// factor is as defined by getDuplicationFactor().
  /// Returns None if encoding failed.
  inline Optional<const DILocation *> cloneWithDuplicationFactor(unsigned DF) const;

  /// When two instructions are combined into a single instruction we also
  /// need to combine the original locations into a single location.
  ///
  /// When the locations are the same we can use either location. When they
  /// differ, we need a third location which is distinct from either. If they
  /// have the same file/line but have a different discriminator we could
  /// create a location with a new discriminator. If they are from different
  /// files/lines the location is ambiguous and can't be represented in a line
  /// entry. In this case, if \p GenerateLocation is true, we will set the
  /// merged debug location as line 0 of the nearest common scope where the two
  /// locations are inlined from.
  ///
  /// \p GenerateLocation: Whether the merged location can be generated when
  /// \p LocA and \p LocB differ.
  static const DILocation *getMergedLocation(const DILocation *LocA,
                                             const DILocation *LocB);

  /// Returns the base discriminator for a given encoded discriminator \p D.
  static unsigned getBaseDiscriminatorFromDiscriminator(unsigned D) {
    return getUnsignedFromPrefixEncoding(D);
  }

  /// Raw encoding of the discriminator. APIs such as setBaseDiscriminator or
  /// cloneWithDuplicationFactor have certain side-effects. This API, in
  /// conjunction with cloneWithDiscriminator, may be used to encode precisely
  /// the values provided. \p BD: base discriminator \p DF: duplication factor
  /// \p CI: copy index
  /// The return is None if the values cannot be encoded in 32 bits - for
  /// example, values for BD or DF larger than 12 bits. Otherwise, the return
  /// is the encoded value.
  static Optional<unsigned> encodeDiscriminator(unsigned BD, unsigned DF, unsigned CI);

  /// Raw decoder for values in an encoded discriminator D.
  static void decodeDiscriminator(unsigned D, unsigned &BD, unsigned &DF,
                                  unsigned &CI);

  /// Returns the duplication factor for a given encoded discriminator \p D, or
  /// 1 if no value or 0 is encoded.
  static unsigned getDuplicationFactorFromDiscriminator(unsigned D) {
    D = getNextComponentInDiscriminator(D);
    unsigned Ret = getUnsignedFromPrefixEncoding(D);
    if (Ret == 0)
      return 1;
    return Ret;
  }

  /// Returns the copy identifier for a given encoded discriminator \p D.
  static unsigned getCopyIdentifierFromDiscriminator(unsigned D) {
    return getUnsignedFromPrefixEncoding(getNextComponentInDiscriminator(
        getNextComponentInDiscriminator(D)));
  }


  Metadata *getRawScope() const { return getOperand(0); }
  Metadata *getRawInlinedAt() const {
    if (getNumOperands() == 2)
      return getOperand(1);
    return nullptr;
  }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILocationKind;
  }
};

/// Subprogram description.
///
/// TODO: Remove DisplayName.  It's always equal to Name.
/// TODO: Split up flags.
class DISubprogram : public DILocalScope {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Line;
  unsigned ScopeLine;
  unsigned VirtualIndex;

  /// In the MS ABI, the implicit 'this' parameter is adjusted in the prologue
  /// of method overrides from secondary bases by this amount. It may be
  /// negative.
  int ThisAdjustment;

public:
  /// Debug info subprogram flags.
  enum DISPFlags : uint32_t {
#define HANDLE_DISP_FLAG(ID, NAME) SPFlag##NAME = ID,
#define DISP_FLAG_LARGEST_NEEDED
#include "llvm/IR/DebugInfoFlags.def"
    SPFlagNonvirtual = SPFlagZero,
    SPFlagVirtuality = SPFlagVirtual | SPFlagPureVirtual,
    LLVM_MARK_AS_BITMASK_ENUM(SPFlagLargest)
  };

  static DISPFlags getFlag(StringRef Flag);
  static StringRef getFlagString(DISPFlags Flag);

  /// Split up a flags bitfield for easier printing.
  ///
  /// Split \c Flags into \c SplitFlags, a vector of its components.  Returns
  /// any remaining (unrecognized) bits.
  static DISPFlags splitFlags(DISPFlags Flags,
                              SmallVectorImpl<DISPFlags> &SplitFlags);

  // Helper for converting old bitfields to new flags word.
  static DISPFlags toSPFlags(bool IsLocalToUnit, bool IsDefinition,
                             bool IsOptimized,
                             unsigned Virtuality = SPFlagNonvirtual) {
    // We're assuming virtuality is the low-order field.
    static_assert(
        int(SPFlagVirtual) == int(dwarf::DW_VIRTUALITY_virtual) &&
            int(SPFlagPureVirtual) == int(dwarf::DW_VIRTUALITY_pure_virtual),
        "Virtuality constant mismatch");
    return static_cast<DISPFlags>(
        (Virtuality & SPFlagVirtuality) |
        (IsLocalToUnit ? SPFlagLocalToUnit : SPFlagZero) |
        (IsDefinition ? SPFlagDefinition : SPFlagZero) |
        (IsOptimized ? SPFlagOptimized : SPFlagZero));
  }

private:
  DIFlags Flags;
  DISPFlags SPFlags;

  DISubprogram(LLVMContext &C, StorageType Storage, unsigned Line,
               unsigned ScopeLine, unsigned VirtualIndex, int ThisAdjustment,
               DIFlags Flags, DISPFlags SPFlags, ArrayRef<Metadata *> Ops)
      : DILocalScope(C, DISubprogramKind, Storage, dwarf::DW_TAG_subprogram,
                     Ops),
        Line(Line), ScopeLine(ScopeLine), VirtualIndex(VirtualIndex),
        ThisAdjustment(ThisAdjustment), Flags(Flags), SPFlags(SPFlags) {
    static_assert(dwarf::DW_VIRTUALITY_max < 4, "Virtuality out of range");
  }
  ~DISubprogram() = default;

  static DISubprogram *
  getImpl(LLVMContext &Context, DIScopeRef Scope, StringRef Name,
          StringRef LinkageName, DIFile *File, unsigned Line,
          DISubroutineType *Type, unsigned ScopeLine, DITypeRef ContainingType,
          unsigned VirtualIndex, int ThisAdjustment, DIFlags Flags,
          DISPFlags SPFlags, DICompileUnit *Unit,
          DITemplateParameterArray TemplateParams, DISubprogram *Declaration,
          DINodeArray RetainedNodes, DITypeArray ThrownTypes,
          StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Scope, getCanonicalMDString(Context, Name),
                   getCanonicalMDString(Context, LinkageName), File, Line, Type,
                   ScopeLine, ContainingType, VirtualIndex, ThisAdjustment,
                   Flags, SPFlags, Unit, TemplateParams.get(), Declaration,
                   RetainedNodes.get(), ThrownTypes.get(), Storage,
                   ShouldCreate);
  }
  static DISubprogram *getImpl(LLVMContext &Context, Metadata *Scope,
                               MDString *Name, MDString *LinkageName,
                               Metadata *File, unsigned Line, Metadata *Type,
                               unsigned ScopeLine, Metadata *ContainingType,
                               unsigned VirtualIndex, int ThisAdjustment,
                               DIFlags Flags, DISPFlags SPFlags, Metadata *Unit,
                               Metadata *TemplateParams, Metadata *Declaration,
                               Metadata *RetainedNodes, Metadata *ThrownTypes,
                               StorageType Storage, bool ShouldCreate = true);

  TempDISubprogram cloneImpl() const {
    return getTemporary(getContext(), getScope(), getName(), getLinkageName(),
                        getFile(), getLine(), getType(), getScopeLine(),
                        getContainingType(), getVirtualIndex(),
                        getThisAdjustment(), getFlags(), getSPFlags(),
                        getUnit(), getTemplateParams(), getDeclaration(),
                        getRetainedNodes(), getThrownTypes());
  }

public:
  DEFINE_MDNODE_GET(
      DISubprogram,
      (DIScopeRef Scope, StringRef Name, StringRef LinkageName, DIFile *File,
       unsigned Line, DISubroutineType *Type, unsigned ScopeLine,
       DITypeRef ContainingType, unsigned VirtualIndex, int ThisAdjustment,
       DIFlags Flags, DISPFlags SPFlags, DICompileUnit *Unit,
       DITemplateParameterArray TemplateParams = nullptr,
       DISubprogram *Declaration = nullptr, DINodeArray RetainedNodes = nullptr,
       DITypeArray ThrownTypes = nullptr),
      (Scope, Name, LinkageName, File, Line, Type, ScopeLine, ContainingType,
       VirtualIndex, ThisAdjustment, Flags, SPFlags, Unit, TemplateParams,
       Declaration, RetainedNodes, ThrownTypes))

  DEFINE_MDNODE_GET(
      DISubprogram,
      (Metadata * Scope, MDString *Name, MDString *LinkageName, Metadata *File,
       unsigned Line, Metadata *Type, unsigned ScopeLine,
       Metadata *ContainingType, unsigned VirtualIndex, int ThisAdjustment,
       DIFlags Flags, DISPFlags SPFlags, Metadata *Unit,
       Metadata *TemplateParams = nullptr, Metadata *Declaration = nullptr,
       Metadata *RetainedNodes = nullptr, Metadata *ThrownTypes = nullptr),
      (Scope, Name, LinkageName, File, Line, Type, ScopeLine, ContainingType,
       VirtualIndex, ThisAdjustment, Flags, SPFlags, Unit, TemplateParams,
       Declaration, RetainedNodes, ThrownTypes))

  TempDISubprogram clone() const { return cloneImpl(); }

  /// Returns a new temporary DISubprogram with updated Flags
  TempDISubprogram cloneWithFlags(DIFlags NewFlags) const {
    auto NewSP = clone();
    NewSP->Flags = NewFlags;
    return NewSP;
  }

public:
  unsigned getLine() const { return Line; }
  unsigned getVirtuality() const { return getSPFlags() & SPFlagVirtuality; }
  unsigned getVirtualIndex() const { return VirtualIndex; }
  int getThisAdjustment() const { return ThisAdjustment; }
  unsigned getScopeLine() const { return ScopeLine; }
  DIFlags getFlags() const { return Flags; }
  DISPFlags getSPFlags() const { return SPFlags; }
  bool isLocalToUnit() const { return getSPFlags() & SPFlagLocalToUnit; }
  bool isDefinition() const { return getSPFlags() & SPFlagDefinition; }
  bool isOptimized() const { return getSPFlags() & SPFlagOptimized; }

  bool isArtificial() const { return getFlags() & FlagArtificial; }
  bool isPrivate() const {
    return (getFlags() & FlagAccessibility) == FlagPrivate;
  }
  bool isProtected() const {
    return (getFlags() & FlagAccessibility) == FlagProtected;
  }
  bool isPublic() const {
    return (getFlags() & FlagAccessibility) == FlagPublic;
  }
  bool isExplicit() const { return getFlags() & FlagExplicit; }
  bool isPrototyped() const { return getFlags() & FlagPrototyped; }
  bool areAllCallsDescribed() const {
    return getFlags() & FlagAllCallsDescribed;
  }
  bool isMainSubprogram() const { return getFlags() & FlagMainSubprogram; }

  /// Check if this is reference-qualified.
  ///
  /// Return true if this subprogram is a C++11 reference-qualified non-static
  /// member function (void foo() &).
  bool isLValueReference() const { return getFlags() & FlagLValueReference; }

  /// Check if this is rvalue-reference-qualified.
  ///
  /// Return true if this subprogram is a C++11 rvalue-reference-qualified
  /// non-static member function (void foo() &&).
  bool isRValueReference() const { return getFlags() & FlagRValueReference; }

  /// Check if this is marked as noreturn.
  ///
  /// Return true if this subprogram is C++11 noreturn or C11 _Noreturn
  bool isNoReturn() const { return getFlags() & FlagNoReturn; }

  // Check if this routine is a compiler-generated thunk.
  //
  // Returns true if this subprogram is a thunk generated by the compiler.
  bool isThunk() const { return getFlags() & FlagThunk; }

  DIScopeRef getScope() const { return DIScopeRef(getRawScope()); }

  StringRef getName() const { return getStringOperand(2); }
  StringRef getLinkageName() const { return getStringOperand(3); }

  DISubroutineType *getType() const {
    return cast_or_null<DISubroutineType>(getRawType());
  }
  DITypeRef getContainingType() const {
    return DITypeRef(getRawContainingType());
  }

  DICompileUnit *getUnit() const {
    return cast_or_null<DICompileUnit>(getRawUnit());
  }
  void replaceUnit(DICompileUnit *CU) { replaceOperandWith(5, CU); }
  DITemplateParameterArray getTemplateParams() const {
    return cast_or_null<MDTuple>(getRawTemplateParams());
  }
  DISubprogram *getDeclaration() const {
    return cast_or_null<DISubprogram>(getRawDeclaration());
  }
  DINodeArray getRetainedNodes() const {
    return cast_or_null<MDTuple>(getRawRetainedNodes());
  }
  DITypeArray getThrownTypes() const {
    return cast_or_null<MDTuple>(getRawThrownTypes());
  }

  Metadata *getRawScope() const { return getOperand(1); }
  MDString *getRawName() const { return getOperandAs<MDString>(2); }
  MDString *getRawLinkageName() const { return getOperandAs<MDString>(3); }
  Metadata *getRawType() const { return getOperand(4); }
  Metadata *getRawUnit() const { return getOperand(5); }
  Metadata *getRawDeclaration() const { return getOperand(6); }
  Metadata *getRawRetainedNodes() const { return getOperand(7); }
  Metadata *getRawContainingType() const {
    return getNumOperands() > 8 ? getOperandAs<Metadata>(8) : nullptr;
  }
  Metadata *getRawTemplateParams() const {
    return getNumOperands() > 9 ? getOperandAs<Metadata>(9) : nullptr;
  }
  Metadata *getRawThrownTypes() const {
    return getNumOperands() > 10 ? getOperandAs<Metadata>(10) : nullptr;
  }

  /// Check if this subprogram describes the given function.
  ///
  /// FIXME: Should this be looking through bitcasts?
  bool describes(const Function *F) const;

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DISubprogramKind;
  }
};

class DILexicalBlockBase : public DILocalScope {
protected:
  DILexicalBlockBase(LLVMContext &C, unsigned ID, StorageType Storage,
                     ArrayRef<Metadata *> Ops)
      : DILocalScope(C, ID, Storage, dwarf::DW_TAG_lexical_block, Ops) {}
  ~DILexicalBlockBase() = default;

public:
  DILocalScope *getScope() const { return cast<DILocalScope>(getRawScope()); }

  Metadata *getRawScope() const { return getOperand(1); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILexicalBlockKind ||
           MD->getMetadataID() == DILexicalBlockFileKind;
  }
};

class DILexicalBlock : public DILexicalBlockBase {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Line;
  uint16_t Column;

  DILexicalBlock(LLVMContext &C, StorageType Storage, unsigned Line,
                 unsigned Column, ArrayRef<Metadata *> Ops)
      : DILexicalBlockBase(C, DILexicalBlockKind, Storage, Ops), Line(Line),
        Column(Column) {
    assert(Column < (1u << 16) && "Expected 16-bit column");
  }
  ~DILexicalBlock() = default;

  static DILexicalBlock *getImpl(LLVMContext &Context, DILocalScope *Scope,
                                 DIFile *File, unsigned Line, unsigned Column,
                                 StorageType Storage,
                                 bool ShouldCreate = true) {
    return getImpl(Context, static_cast<Metadata *>(Scope),
                   static_cast<Metadata *>(File), Line, Column, Storage,
                   ShouldCreate);
  }

  static DILexicalBlock *getImpl(LLVMContext &Context, Metadata *Scope,
                                 Metadata *File, unsigned Line, unsigned Column,
                                 StorageType Storage, bool ShouldCreate = true);

  TempDILexicalBlock cloneImpl() const {
    return getTemporary(getContext(), getScope(), getFile(), getLine(),
                        getColumn());
  }

public:
  DEFINE_MDNODE_GET(DILexicalBlock, (DILocalScope * Scope, DIFile *File,
                                     unsigned Line, unsigned Column),
                    (Scope, File, Line, Column))
  DEFINE_MDNODE_GET(DILexicalBlock, (Metadata * Scope, Metadata *File,
                                     unsigned Line, unsigned Column),
                    (Scope, File, Line, Column))

  TempDILexicalBlock clone() const { return cloneImpl(); }

  unsigned getLine() const { return Line; }
  unsigned getColumn() const { return Column; }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILexicalBlockKind;
  }
};

class DILexicalBlockFile : public DILexicalBlockBase {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Discriminator;

  DILexicalBlockFile(LLVMContext &C, StorageType Storage,
                     unsigned Discriminator, ArrayRef<Metadata *> Ops)
      : DILexicalBlockBase(C, DILexicalBlockFileKind, Storage, Ops),
        Discriminator(Discriminator) {}
  ~DILexicalBlockFile() = default;

  static DILexicalBlockFile *getImpl(LLVMContext &Context, DILocalScope *Scope,
                                     DIFile *File, unsigned Discriminator,
                                     StorageType Storage,
                                     bool ShouldCreate = true) {
    return getImpl(Context, static_cast<Metadata *>(Scope),
                   static_cast<Metadata *>(File), Discriminator, Storage,
                   ShouldCreate);
  }

  static DILexicalBlockFile *getImpl(LLVMContext &Context, Metadata *Scope,
                                     Metadata *File, unsigned Discriminator,
                                     StorageType Storage,
                                     bool ShouldCreate = true);

  TempDILexicalBlockFile cloneImpl() const {
    return getTemporary(getContext(), getScope(), getFile(),
                        getDiscriminator());
  }

public:
  DEFINE_MDNODE_GET(DILexicalBlockFile, (DILocalScope * Scope, DIFile *File,
                                         unsigned Discriminator),
                    (Scope, File, Discriminator))
  DEFINE_MDNODE_GET(DILexicalBlockFile,
                    (Metadata * Scope, Metadata *File, unsigned Discriminator),
                    (Scope, File, Discriminator))

  TempDILexicalBlockFile clone() const { return cloneImpl(); }

  // TODO: Remove these once they're gone from DILexicalBlockBase.
  unsigned getLine() const = delete;
  unsigned getColumn() const = delete;

  unsigned getDiscriminator() const { return Discriminator; }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILexicalBlockFileKind;
  }
};

unsigned DILocation::getDiscriminator() const {
  if (auto *F = dyn_cast<DILexicalBlockFile>(getScope()))
    return F->getDiscriminator();
  return 0;
}

const DILocation *
DILocation::cloneWithDiscriminator(unsigned Discriminator) const {
  DIScope *Scope = getScope();
  // Skip all parent DILexicalBlockFile that already have a discriminator
  // assigned. We do not want to have nested DILexicalBlockFiles that have
  // mutliple discriminators because only the leaf DILexicalBlockFile's
  // dominator will be used.
  for (auto *LBF = dyn_cast<DILexicalBlockFile>(Scope);
       LBF && LBF->getDiscriminator() != 0;
       LBF = dyn_cast<DILexicalBlockFile>(Scope))
    Scope = LBF->getScope();
  DILexicalBlockFile *NewScope =
      DILexicalBlockFile::get(getContext(), Scope, getFile(), Discriminator);
  return DILocation::get(getContext(), getLine(), getColumn(), NewScope,
                         getInlinedAt());
}

unsigned DILocation::getBaseDiscriminator() const {
  return getBaseDiscriminatorFromDiscriminator(getDiscriminator());
}

unsigned DILocation::getDuplicationFactor() const {
  return getDuplicationFactorFromDiscriminator(getDiscriminator());
}

unsigned DILocation::getCopyIdentifier() const {
  return getCopyIdentifierFromDiscriminator(getDiscriminator());
}

Optional<const DILocation *> DILocation::setBaseDiscriminator(unsigned D) const {
  if (D == 0)
    return this;
  if (D > 0xfff)
    return None;
  return cloneWithDiscriminator(encodeComponent(D));
}

Optional<const DILocation *> DILocation::cloneWithDuplicationFactor(unsigned DF) const {
  DF *= getDuplicationFactor();
  if (DF <= 1)
    return this;

  unsigned BD = getBaseDiscriminator();
  unsigned CI = getCopyIdentifier();
  if (Optional<unsigned> D = encodeDiscriminator(BD, DF, CI))
    return cloneWithDiscriminator(*D);
  return None;
}

class DINamespace : public DIScope {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned ExportSymbols : 1;

  DINamespace(LLVMContext &Context, StorageType Storage, bool ExportSymbols,
              ArrayRef<Metadata *> Ops)
      : DIScope(Context, DINamespaceKind, Storage, dwarf::DW_TAG_namespace,
                Ops),
        ExportSymbols(ExportSymbols) {}
  ~DINamespace() = default;

  static DINamespace *getImpl(LLVMContext &Context, DIScope *Scope,
                              StringRef Name, bool ExportSymbols,
                              StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Scope, getCanonicalMDString(Context, Name),
                   ExportSymbols, Storage, ShouldCreate);
  }
  static DINamespace *getImpl(LLVMContext &Context, Metadata *Scope,
                              MDString *Name, bool ExportSymbols,
                              StorageType Storage, bool ShouldCreate = true);

  TempDINamespace cloneImpl() const {
    return getTemporary(getContext(), getScope(), getName(),
                        getExportSymbols());
  }

public:
  DEFINE_MDNODE_GET(DINamespace,
                    (DIScope *Scope, StringRef Name, bool ExportSymbols),
                    (Scope, Name, ExportSymbols))
  DEFINE_MDNODE_GET(DINamespace,
                    (Metadata *Scope, MDString *Name, bool ExportSymbols),
                    (Scope, Name, ExportSymbols))

  TempDINamespace clone() const { return cloneImpl(); }

  bool getExportSymbols() const { return ExportSymbols; }
  DIScope *getScope() const { return cast_or_null<DIScope>(getRawScope()); }
  StringRef getName() const { return getStringOperand(2); }

  Metadata *getRawScope() const { return getOperand(1); }
  MDString *getRawName() const { return getOperandAs<MDString>(2); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DINamespaceKind;
  }
};

/// A (clang) module that has been imported by the compile unit.
///
class DIModule : public DIScope {
  friend class LLVMContextImpl;
  friend class MDNode;

  DIModule(LLVMContext &Context, StorageType Storage, ArrayRef<Metadata *> Ops)
      : DIScope(Context, DIModuleKind, Storage, dwarf::DW_TAG_module, Ops) {}
  ~DIModule() = default;

  static DIModule *getImpl(LLVMContext &Context, DIScope *Scope,
                           StringRef Name, StringRef ConfigurationMacros,
                           StringRef IncludePath, StringRef ISysRoot,
                           StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Scope, getCanonicalMDString(Context, Name),
                   getCanonicalMDString(Context, ConfigurationMacros),
                   getCanonicalMDString(Context, IncludePath),
                   getCanonicalMDString(Context, ISysRoot),
                   Storage, ShouldCreate);
  }
  static DIModule *getImpl(LLVMContext &Context, Metadata *Scope,
                           MDString *Name, MDString *ConfigurationMacros,
                           MDString *IncludePath, MDString *ISysRoot,
                           StorageType Storage, bool ShouldCreate = true);

  TempDIModule cloneImpl() const {
    return getTemporary(getContext(), getScope(), getName(),
                        getConfigurationMacros(), getIncludePath(),
                        getISysRoot());
  }

public:
  DEFINE_MDNODE_GET(DIModule, (DIScope *Scope, StringRef Name,
                               StringRef ConfigurationMacros, StringRef IncludePath,
                               StringRef ISysRoot),
                    (Scope, Name, ConfigurationMacros, IncludePath, ISysRoot))
  DEFINE_MDNODE_GET(DIModule,
                    (Metadata *Scope, MDString *Name, MDString *ConfigurationMacros,
                     MDString *IncludePath, MDString *ISysRoot),
                    (Scope, Name, ConfigurationMacros, IncludePath, ISysRoot))

  TempDIModule clone() const { return cloneImpl(); }

  DIScope *getScope() const { return cast_or_null<DIScope>(getRawScope()); }
  StringRef getName() const { return getStringOperand(1); }
  StringRef getConfigurationMacros() const { return getStringOperand(2); }
  StringRef getIncludePath() const { return getStringOperand(3); }
  StringRef getISysRoot() const { return getStringOperand(4); }

  Metadata *getRawScope() const { return getOperand(0); }
  MDString *getRawName() const { return getOperandAs<MDString>(1); }
  MDString *getRawConfigurationMacros() const { return getOperandAs<MDString>(2); }
  MDString *getRawIncludePath() const { return getOperandAs<MDString>(3); }
  MDString *getRawISysRoot() const { return getOperandAs<MDString>(4); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIModuleKind;
  }
};

/// Base class for template parameters.
class DITemplateParameter : public DINode {
protected:
  DITemplateParameter(LLVMContext &Context, unsigned ID, StorageType Storage,
                      unsigned Tag, ArrayRef<Metadata *> Ops)
      : DINode(Context, ID, Storage, Tag, Ops) {}
  ~DITemplateParameter() = default;

public:
  StringRef getName() const { return getStringOperand(0); }
  DITypeRef getType() const { return DITypeRef(getRawType()); }

  MDString *getRawName() const { return getOperandAs<MDString>(0); }
  Metadata *getRawType() const { return getOperand(1); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DITemplateTypeParameterKind ||
           MD->getMetadataID() == DITemplateValueParameterKind;
  }
};

class DITemplateTypeParameter : public DITemplateParameter {
  friend class LLVMContextImpl;
  friend class MDNode;

  DITemplateTypeParameter(LLVMContext &Context, StorageType Storage,
                          ArrayRef<Metadata *> Ops)
      : DITemplateParameter(Context, DITemplateTypeParameterKind, Storage,
                            dwarf::DW_TAG_template_type_parameter, Ops) {}
  ~DITemplateTypeParameter() = default;

  static DITemplateTypeParameter *getImpl(LLVMContext &Context, StringRef Name,
                                          DITypeRef Type, StorageType Storage,
                                          bool ShouldCreate = true) {
    return getImpl(Context, getCanonicalMDString(Context, Name), Type, Storage,
                   ShouldCreate);
  }
  static DITemplateTypeParameter *getImpl(LLVMContext &Context, MDString *Name,
                                          Metadata *Type, StorageType Storage,
                                          bool ShouldCreate = true);

  TempDITemplateTypeParameter cloneImpl() const {
    return getTemporary(getContext(), getName(), getType());
  }

public:
  DEFINE_MDNODE_GET(DITemplateTypeParameter, (StringRef Name, DITypeRef Type),
                    (Name, Type))
  DEFINE_MDNODE_GET(DITemplateTypeParameter, (MDString * Name, Metadata *Type),
                    (Name, Type))

  TempDITemplateTypeParameter clone() const { return cloneImpl(); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DITemplateTypeParameterKind;
  }
};

class DITemplateValueParameter : public DITemplateParameter {
  friend class LLVMContextImpl;
  friend class MDNode;

  DITemplateValueParameter(LLVMContext &Context, StorageType Storage,
                           unsigned Tag, ArrayRef<Metadata *> Ops)
      : DITemplateParameter(Context, DITemplateValueParameterKind, Storage, Tag,
                            Ops) {}
  ~DITemplateValueParameter() = default;

  static DITemplateValueParameter *getImpl(LLVMContext &Context, unsigned Tag,
                                           StringRef Name, DITypeRef Type,
                                           Metadata *Value, StorageType Storage,
                                           bool ShouldCreate = true) {
    return getImpl(Context, Tag, getCanonicalMDString(Context, Name), Type,
                   Value, Storage, ShouldCreate);
  }
  static DITemplateValueParameter *getImpl(LLVMContext &Context, unsigned Tag,
                                           MDString *Name, Metadata *Type,
                                           Metadata *Value, StorageType Storage,
                                           bool ShouldCreate = true);

  TempDITemplateValueParameter cloneImpl() const {
    return getTemporary(getContext(), getTag(), getName(), getType(),
                        getValue());
  }

public:
  DEFINE_MDNODE_GET(DITemplateValueParameter, (unsigned Tag, StringRef Name,
                                               DITypeRef Type, Metadata *Value),
                    (Tag, Name, Type, Value))
  DEFINE_MDNODE_GET(DITemplateValueParameter, (unsigned Tag, MDString *Name,
                                               Metadata *Type, Metadata *Value),
                    (Tag, Name, Type, Value))

  TempDITemplateValueParameter clone() const { return cloneImpl(); }

  Metadata *getValue() const { return getOperand(2); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DITemplateValueParameterKind;
  }
};

/// Base class for variables.
class DIVariable : public DINode {
  unsigned Line;
  uint32_t AlignInBits;

protected:
  DIVariable(LLVMContext &C, unsigned ID, StorageType Storage, unsigned Line,
             ArrayRef<Metadata *> Ops, uint32_t AlignInBits = 0)
      : DINode(C, ID, Storage, dwarf::DW_TAG_variable, Ops), Line(Line),
        AlignInBits(AlignInBits) {}
  ~DIVariable() = default;

public:
  unsigned getLine() const { return Line; }
  DIScope *getScope() const { return cast_or_null<DIScope>(getRawScope()); }
  StringRef getName() const { return getStringOperand(1); }
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }
  DITypeRef getType() const { return DITypeRef(getRawType()); }
  uint32_t getAlignInBits() const { return AlignInBits; }
  uint32_t getAlignInBytes() const { return getAlignInBits() / CHAR_BIT; }
  /// Determines the size of the variable's type.
  Optional<uint64_t> getSizeInBits() const;

  /// Return the signedness of this variable's type, or None if this type is
  /// neither signed nor unsigned.
  Optional<DIBasicType::Signedness> getSignedness() const {
    if (auto *BT = dyn_cast<DIBasicType>(getType().resolve()))
      return BT->getSignedness();
    return None;
  }

  StringRef getFilename() const {
    if (auto *F = getFile())
      return F->getFilename();
    return "";
  }

  StringRef getDirectory() const {
    if (auto *F = getFile())
      return F->getDirectory();
    return "";
  }

  Optional<StringRef> getSource() const {
    if (auto *F = getFile())
      return F->getSource();
    return None;
  }

  Metadata *getRawScope() const { return getOperand(0); }
  MDString *getRawName() const { return getOperandAs<MDString>(1); }
  Metadata *getRawFile() const { return getOperand(2); }
  Metadata *getRawType() const { return getOperand(3); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILocalVariableKind ||
           MD->getMetadataID() == DIGlobalVariableKind;
  }
};

/// DWARF expression.
///
/// This is (almost) a DWARF expression that modifies the location of a
/// variable, or the location of a single piece of a variable, or (when using
/// DW_OP_stack_value) is the constant variable value.
///
/// TODO: Co-allocate the expression elements.
/// TODO: Separate from MDNode, or otherwise drop Distinct and Temporary
/// storage types.
class DIExpression : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  std::vector<uint64_t> Elements;

  DIExpression(LLVMContext &C, StorageType Storage, ArrayRef<uint64_t> Elements)
      : MDNode(C, DIExpressionKind, Storage, None),
        Elements(Elements.begin(), Elements.end()) {}
  ~DIExpression() = default;

  static DIExpression *getImpl(LLVMContext &Context,
                               ArrayRef<uint64_t> Elements, StorageType Storage,
                               bool ShouldCreate = true);

  TempDIExpression cloneImpl() const {
    return getTemporary(getContext(), getElements());
  }

public:
  DEFINE_MDNODE_GET(DIExpression, (ArrayRef<uint64_t> Elements), (Elements))

  TempDIExpression clone() const { return cloneImpl(); }

  ArrayRef<uint64_t> getElements() const { return Elements; }

  unsigned getNumElements() const { return Elements.size(); }

  uint64_t getElement(unsigned I) const {
    assert(I < Elements.size() && "Index out of range");
    return Elements[I];
  }

  /// Determine whether this represents a standalone constant value.
  bool isConstant() const;

  using element_iterator = ArrayRef<uint64_t>::iterator;

  element_iterator elements_begin() const { return getElements().begin(); }
  element_iterator elements_end() const { return getElements().end(); }

  /// A lightweight wrapper around an expression operand.
  ///
  /// TODO: Store arguments directly and change \a DIExpression to store a
  /// range of these.
  class ExprOperand {
    const uint64_t *Op = nullptr;

  public:
    ExprOperand() = default;
    explicit ExprOperand(const uint64_t *Op) : Op(Op) {}

    const uint64_t *get() const { return Op; }

    /// Get the operand code.
    uint64_t getOp() const { return *Op; }

    /// Get an argument to the operand.
    ///
    /// Never returns the operand itself.
    uint64_t getArg(unsigned I) const { return Op[I + 1]; }

    unsigned getNumArgs() const { return getSize() - 1; }

    /// Return the size of the operand.
    ///
    /// Return the number of elements in the operand (1 + args).
    unsigned getSize() const;

    /// Append the elements of this operand to \p V.
    void appendToVector(SmallVectorImpl<uint64_t> &V) const {
      V.append(get(), get() + getSize());
    }
  };

  /// An iterator for expression operands.
  class expr_op_iterator
      : public std::iterator<std::input_iterator_tag, ExprOperand> {
    ExprOperand Op;

  public:
    expr_op_iterator() = default;
    explicit expr_op_iterator(element_iterator I) : Op(I) {}

    element_iterator getBase() const { return Op.get(); }
    const ExprOperand &operator*() const { return Op; }
    const ExprOperand *operator->() const { return &Op; }

    expr_op_iterator &operator++() {
      increment();
      return *this;
    }
    expr_op_iterator operator++(int) {
      expr_op_iterator T(*this);
      increment();
      return T;
    }

    /// Get the next iterator.
    ///
    /// \a std::next() doesn't work because this is technically an
    /// input_iterator, but it's a perfectly valid operation.  This is an
    /// accessor to provide the same functionality.
    expr_op_iterator getNext() const { return ++expr_op_iterator(*this); }

    bool operator==(const expr_op_iterator &X) const {
      return getBase() == X.getBase();
    }
    bool operator!=(const expr_op_iterator &X) const {
      return getBase() != X.getBase();
    }

  private:
    void increment() { Op = ExprOperand(getBase() + Op.getSize()); }
  };

  /// Visit the elements via ExprOperand wrappers.
  ///
  /// These range iterators visit elements through \a ExprOperand wrappers.
  /// This is not guaranteed to be a valid range unless \a isValid() gives \c
  /// true.
  ///
  /// \pre \a isValid() gives \c true.
  /// @{
  expr_op_iterator expr_op_begin() const {
    return expr_op_iterator(elements_begin());
  }
  expr_op_iterator expr_op_end() const {
    return expr_op_iterator(elements_end());
  }
  iterator_range<expr_op_iterator> expr_ops() const {
    return {expr_op_begin(), expr_op_end()};
  }
  /// @}

  bool isValid() const;

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIExpressionKind;
  }

  /// Return whether the first element a DW_OP_deref.
  bool startsWithDeref() const {
    return getNumElements() > 0 && getElement(0) == dwarf::DW_OP_deref;
  }

  /// Holds the characteristics of one fragment of a larger variable.
  struct FragmentInfo {
    uint64_t SizeInBits;
    uint64_t OffsetInBits;
  };

  /// Retrieve the details of this fragment expression.
  static Optional<FragmentInfo> getFragmentInfo(expr_op_iterator Start,
                                                expr_op_iterator End);

  /// Retrieve the details of this fragment expression.
  Optional<FragmentInfo> getFragmentInfo() const {
    return getFragmentInfo(expr_op_begin(), expr_op_end());
  }

  /// Return whether this is a piece of an aggregate variable.
  bool isFragment() const { return getFragmentInfo().hasValue(); }

  /// Append \p Ops with operations to apply the \p Offset.
  static void appendOffset(SmallVectorImpl<uint64_t> &Ops, int64_t Offset);

  /// If this is a constant offset, extract it. If there is no expression,
  /// return true with an offset of zero.
  bool extractIfOffset(int64_t &Offset) const;

  /// Constants for DIExpression::prepend.
  enum { NoDeref = false, WithDeref = true, WithStackValue = true };

  /// Prepend \p DIExpr with a deref and offset operation and optionally turn it
  /// into a stack value.
  static DIExpression *prepend(const DIExpression *Expr, bool DerefBefore,
                               int64_t Offset = 0, bool DerefAfter = false,
                               bool StackValue = false);

  /// Prepend \p DIExpr with the given opcodes and optionally turn it into a
  /// stack value.
  static DIExpression *prependOpcodes(const DIExpression *Expr,
                                      SmallVectorImpl<uint64_t> &Ops,
                                      bool StackValue = false);

  /// Append the opcodes \p Ops to \p DIExpr. Unlike \ref appendToStack, the
  /// returned expression is a stack value only if \p DIExpr is a stack value.
  /// If \p DIExpr describes a fragment, the returned expression will describe
  /// the same fragment.
  static DIExpression *append(const DIExpression *Expr, ArrayRef<uint64_t> Ops);

  /// Convert \p DIExpr into a stack value if it isn't one already by appending
  /// DW_OP_deref if needed, and appending \p Ops to the resulting expression.
  /// If \p DIExpr describes a fragment, the returned expression will describe
  /// the same fragment.
  static DIExpression *appendToStack(const DIExpression *Expr,
                                     ArrayRef<uint64_t> Ops);

  /// Create a DIExpression to describe one part of an aggregate variable that
  /// is fragmented across multiple Values. The DW_OP_LLVM_fragment operation
  /// will be appended to the elements of \c Expr. If \c Expr already contains
  /// a \c DW_OP_LLVM_fragment \c OffsetInBits is interpreted as an offset
  /// into the existing fragment.
  ///
  /// \param OffsetInBits Offset of the piece in bits.
  /// \param SizeInBits   Size of the piece in bits.
  /// \return             Creating a fragment expression may fail if \c Expr
  ///                     contains arithmetic operations that would be truncated.
  static Optional<DIExpression *>
  createFragmentExpression(const DIExpression *Expr, unsigned OffsetInBits,
                           unsigned SizeInBits);

  /// Determine the relative position of the fragments described by this
  /// DIExpression and \p Other.
  /// Returns -1 if this is entirely before Other, 0 if this and Other overlap,
  /// 1 if this is entirely after Other.
  int fragmentCmp(const DIExpression *Other) const {
    auto Fragment1 = *getFragmentInfo();
    auto Fragment2 = *Other->getFragmentInfo();
    unsigned l1 = Fragment1.OffsetInBits;
    unsigned l2 = Fragment2.OffsetInBits;
    unsigned r1 = l1 + Fragment1.SizeInBits;
    unsigned r2 = l2 + Fragment2.SizeInBits;
    if (r1 <= l2)
      return -1;
    else if (r2 <= l1)
      return 1;
    else
      return 0;
  }

  /// Check if fragments overlap between this DIExpression and \p Other.
  bool fragmentsOverlap(const DIExpression *Other) const {
    if (!isFragment() || !Other->isFragment())
      return true;
    return fragmentCmp(Other) == 0;
  }
};

/// Global variables.
///
/// TODO: Remove DisplayName.  It's always equal to Name.
class DIGlobalVariable : public DIVariable {
  friend class LLVMContextImpl;
  friend class MDNode;

  bool IsLocalToUnit;
  bool IsDefinition;

  DIGlobalVariable(LLVMContext &C, StorageType Storage, unsigned Line,
                   bool IsLocalToUnit, bool IsDefinition, uint32_t AlignInBits,
                   ArrayRef<Metadata *> Ops)
      : DIVariable(C, DIGlobalVariableKind, Storage, Line, Ops, AlignInBits),
        IsLocalToUnit(IsLocalToUnit), IsDefinition(IsDefinition) {}
  ~DIGlobalVariable() = default;

  static DIGlobalVariable *
  getImpl(LLVMContext &Context, DIScope *Scope, StringRef Name,
          StringRef LinkageName, DIFile *File, unsigned Line, DITypeRef Type,
          bool IsLocalToUnit, bool IsDefinition,
          DIDerivedType *StaticDataMemberDeclaration, MDTuple *TemplateParams,
          uint32_t AlignInBits, StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, Scope, getCanonicalMDString(Context, Name),
                   getCanonicalMDString(Context, LinkageName), File, Line, Type,
                   IsLocalToUnit, IsDefinition, StaticDataMemberDeclaration,
                   cast_or_null<Metadata>(TemplateParams), AlignInBits, Storage,
                   ShouldCreate);
  }
  static DIGlobalVariable *
  getImpl(LLVMContext &Context, Metadata *Scope, MDString *Name,
          MDString *LinkageName, Metadata *File, unsigned Line, Metadata *Type,
          bool IsLocalToUnit, bool IsDefinition,
          Metadata *StaticDataMemberDeclaration, Metadata *TemplateParams,
          uint32_t AlignInBits, StorageType Storage, bool ShouldCreate = true);

  TempDIGlobalVariable cloneImpl() const {
    return getTemporary(getContext(), getScope(), getName(), getLinkageName(),
                        getFile(), getLine(), getType(), isLocalToUnit(),
                        isDefinition(), getStaticDataMemberDeclaration(),
                        getTemplateParams(), getAlignInBits());
  }

public:
  DEFINE_MDNODE_GET(DIGlobalVariable,
                    (DIScope * Scope, StringRef Name, StringRef LinkageName,
                     DIFile *File, unsigned Line, DITypeRef Type,
                     bool IsLocalToUnit, bool IsDefinition,
                     DIDerivedType *StaticDataMemberDeclaration,
                     MDTuple *TemplateParams, uint32_t AlignInBits),
                    (Scope, Name, LinkageName, File, Line, Type, IsLocalToUnit,
                     IsDefinition, StaticDataMemberDeclaration, TemplateParams,
                     AlignInBits))
  DEFINE_MDNODE_GET(DIGlobalVariable,
                    (Metadata * Scope, MDString *Name, MDString *LinkageName,
                     Metadata *File, unsigned Line, Metadata *Type,
                     bool IsLocalToUnit, bool IsDefinition,
                     Metadata *StaticDataMemberDeclaration,
                     Metadata *TemplateParams, uint32_t AlignInBits),
                    (Scope, Name, LinkageName, File, Line, Type, IsLocalToUnit,
                     IsDefinition, StaticDataMemberDeclaration, TemplateParams,
                     AlignInBits))

  TempDIGlobalVariable clone() const { return cloneImpl(); }

  bool isLocalToUnit() const { return IsLocalToUnit; }
  bool isDefinition() const { return IsDefinition; }
  StringRef getDisplayName() const { return getStringOperand(4); }
  StringRef getLinkageName() const { return getStringOperand(5); }
  DIDerivedType *getStaticDataMemberDeclaration() const {
    return cast_or_null<DIDerivedType>(getRawStaticDataMemberDeclaration());
  }

  MDString *getRawLinkageName() const { return getOperandAs<MDString>(5); }
  Metadata *getRawStaticDataMemberDeclaration() const { return getOperand(6); }
  Metadata *getRawTemplateParams() const { return getOperand(7); }
  MDTuple *getTemplateParams() const { return getOperandAs<MDTuple>(7); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIGlobalVariableKind;
  }
};

/// Local variable.
///
/// TODO: Split up flags.
class DILocalVariable : public DIVariable {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Arg : 16;
  DIFlags Flags;

  DILocalVariable(LLVMContext &C, StorageType Storage, unsigned Line,
                  unsigned Arg, DIFlags Flags, uint32_t AlignInBits,
                  ArrayRef<Metadata *> Ops)
      : DIVariable(C, DILocalVariableKind, Storage, Line, Ops, AlignInBits),
        Arg(Arg), Flags(Flags) {
    assert(Arg < (1 << 16) && "DILocalVariable: Arg out of range");
  }
  ~DILocalVariable() = default;

  static DILocalVariable *getImpl(LLVMContext &Context, DIScope *Scope,
                                  StringRef Name, DIFile *File, unsigned Line,
                                  DITypeRef Type, unsigned Arg, DIFlags Flags,
                                  uint32_t AlignInBits, StorageType Storage,
                                  bool ShouldCreate = true) {
    return getImpl(Context, Scope, getCanonicalMDString(Context, Name), File,
                   Line, Type, Arg, Flags, AlignInBits, Storage, ShouldCreate);
  }
  static DILocalVariable *getImpl(LLVMContext &Context, Metadata *Scope,
                                  MDString *Name, Metadata *File, unsigned Line,
                                  Metadata *Type, unsigned Arg, DIFlags Flags,
                                  uint32_t AlignInBits, StorageType Storage,
                                  bool ShouldCreate = true);

  TempDILocalVariable cloneImpl() const {
    return getTemporary(getContext(), getScope(), getName(), getFile(),
                        getLine(), getType(), getArg(), getFlags(),
                        getAlignInBits());
  }

public:
  DEFINE_MDNODE_GET(DILocalVariable,
                    (DILocalScope * Scope, StringRef Name, DIFile *File,
                     unsigned Line, DITypeRef Type, unsigned Arg,
                     DIFlags Flags, uint32_t AlignInBits),
                    (Scope, Name, File, Line, Type, Arg, Flags, AlignInBits))
  DEFINE_MDNODE_GET(DILocalVariable,
                    (Metadata * Scope, MDString *Name, Metadata *File,
                     unsigned Line, Metadata *Type, unsigned Arg,
                     DIFlags Flags, uint32_t AlignInBits),
                    (Scope, Name, File, Line, Type, Arg, Flags, AlignInBits))

  TempDILocalVariable clone() const { return cloneImpl(); }

  /// Get the local scope for this variable.
  ///
  /// Variables must be defined in a local scope.
  DILocalScope *getScope() const {
    return cast<DILocalScope>(DIVariable::getScope());
  }

  bool isParameter() const { return Arg; }
  unsigned getArg() const { return Arg; }
  DIFlags getFlags() const { return Flags; }

  bool isArtificial() const { return getFlags() & FlagArtificial; }
  bool isObjectPointer() const { return getFlags() & FlagObjectPointer; }

  /// Check that a location is valid for this variable.
  ///
  /// Check that \c DL exists, is in the same subprogram, and has the same
  /// inlined-at location as \c this.  (Otherwise, it's not a valid attachment
  /// to a \a DbgInfoIntrinsic.)
  bool isValidLocationForIntrinsic(const DILocation *DL) const {
    return DL && getScope()->getSubprogram() == DL->getScope()->getSubprogram();
  }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILocalVariableKind;
  }
};

/// Label.
///
class DILabel : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Line;

  DILabel(LLVMContext &C, StorageType Storage, unsigned Line,
          ArrayRef<Metadata *> Ops)
      : DINode(C, DILabelKind, Storage, dwarf::DW_TAG_label, Ops), Line(Line) {}
  ~DILabel() = default;

  static DILabel *getImpl(LLVMContext &Context, DIScope *Scope,
                          StringRef Name, DIFile *File, unsigned Line,
                          StorageType Storage,
                          bool ShouldCreate = true) {
    return getImpl(Context, Scope, getCanonicalMDString(Context, Name), File,
                   Line, Storage, ShouldCreate);
  }
  static DILabel *getImpl(LLVMContext &Context, Metadata *Scope,
                          MDString *Name, Metadata *File, unsigned Line,
                          StorageType Storage,
                          bool ShouldCreate = true);

  TempDILabel cloneImpl() const {
    return getTemporary(getContext(), getScope(), getName(), getFile(),
                        getLine());
  }

public:
  DEFINE_MDNODE_GET(DILabel,
                    (DILocalScope * Scope, StringRef Name, DIFile *File,
                     unsigned Line),
                    (Scope, Name, File, Line))
  DEFINE_MDNODE_GET(DILabel,
                    (Metadata * Scope, MDString *Name, Metadata *File,
                     unsigned Line),
                    (Scope, Name, File, Line))

  TempDILabel clone() const { return cloneImpl(); }

  /// Get the local scope for this label.
  ///
  /// Labels must be defined in a local scope.
  DILocalScope *getScope() const {
    return cast_or_null<DILocalScope>(getRawScope());
  }
  unsigned getLine() const { return Line; }
  StringRef getName() const { return getStringOperand(1); }
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }

  Metadata *getRawScope() const { return getOperand(0); }
  MDString *getRawName() const { return getOperandAs<MDString>(1); }
  Metadata *getRawFile() const { return getOperand(2); }

  /// Check that a location is valid for this label.
  ///
  /// Check that \c DL exists, is in the same subprogram, and has the same
  /// inlined-at location as \c this.  (Otherwise, it's not a valid attachment
  /// to a \a DbgInfoIntrinsic.)
  bool isValidLocationForIntrinsic(const DILocation *DL) const {
    return DL && getScope()->getSubprogram() == DL->getScope()->getSubprogram();
  }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DILabelKind;
  }
};

class DIObjCProperty : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Line;
  unsigned Attributes;

  DIObjCProperty(LLVMContext &C, StorageType Storage, unsigned Line,
                 unsigned Attributes, ArrayRef<Metadata *> Ops)
      : DINode(C, DIObjCPropertyKind, Storage, dwarf::DW_TAG_APPLE_property,
               Ops),
        Line(Line), Attributes(Attributes) {}
  ~DIObjCProperty() = default;

  static DIObjCProperty *
  getImpl(LLVMContext &Context, StringRef Name, DIFile *File, unsigned Line,
          StringRef GetterName, StringRef SetterName, unsigned Attributes,
          DITypeRef Type, StorageType Storage, bool ShouldCreate = true) {
    return getImpl(Context, getCanonicalMDString(Context, Name), File, Line,
                   getCanonicalMDString(Context, GetterName),
                   getCanonicalMDString(Context, SetterName), Attributes, Type,
                   Storage, ShouldCreate);
  }
  static DIObjCProperty *getImpl(LLVMContext &Context, MDString *Name,
                                 Metadata *File, unsigned Line,
                                 MDString *GetterName, MDString *SetterName,
                                 unsigned Attributes, Metadata *Type,
                                 StorageType Storage, bool ShouldCreate = true);

  TempDIObjCProperty cloneImpl() const {
    return getTemporary(getContext(), getName(), getFile(), getLine(),
                        getGetterName(), getSetterName(), getAttributes(),
                        getType());
  }

public:
  DEFINE_MDNODE_GET(DIObjCProperty,
                    (StringRef Name, DIFile *File, unsigned Line,
                     StringRef GetterName, StringRef SetterName,
                     unsigned Attributes, DITypeRef Type),
                    (Name, File, Line, GetterName, SetterName, Attributes,
                     Type))
  DEFINE_MDNODE_GET(DIObjCProperty,
                    (MDString * Name, Metadata *File, unsigned Line,
                     MDString *GetterName, MDString *SetterName,
                     unsigned Attributes, Metadata *Type),
                    (Name, File, Line, GetterName, SetterName, Attributes,
                     Type))

  TempDIObjCProperty clone() const { return cloneImpl(); }

  unsigned getLine() const { return Line; }
  unsigned getAttributes() const { return Attributes; }
  StringRef getName() const { return getStringOperand(0); }
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }
  StringRef getGetterName() const { return getStringOperand(2); }
  StringRef getSetterName() const { return getStringOperand(3); }
  DITypeRef getType() const { return DITypeRef(getRawType()); }

  StringRef getFilename() const {
    if (auto *F = getFile())
      return F->getFilename();
    return "";
  }

  StringRef getDirectory() const {
    if (auto *F = getFile())
      return F->getDirectory();
    return "";
  }

  Optional<StringRef> getSource() const {
    if (auto *F = getFile())
      return F->getSource();
    return None;
  }

  MDString *getRawName() const { return getOperandAs<MDString>(0); }
  Metadata *getRawFile() const { return getOperand(1); }
  MDString *getRawGetterName() const { return getOperandAs<MDString>(2); }
  MDString *getRawSetterName() const { return getOperandAs<MDString>(3); }
  Metadata *getRawType() const { return getOperand(4); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIObjCPropertyKind;
  }
};

/// An imported module (C++ using directive or similar).
class DIImportedEntity : public DINode {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Line;

  DIImportedEntity(LLVMContext &C, StorageType Storage, unsigned Tag,
                   unsigned Line, ArrayRef<Metadata *> Ops)
      : DINode(C, DIImportedEntityKind, Storage, Tag, Ops), Line(Line) {}
  ~DIImportedEntity() = default;

  static DIImportedEntity *getImpl(LLVMContext &Context, unsigned Tag,
                                   DIScope *Scope, DINodeRef Entity,
                                   DIFile *File, unsigned Line, StringRef Name,
                                   StorageType Storage,
                                   bool ShouldCreate = true) {
    return getImpl(Context, Tag, Scope, Entity, File, Line,
                   getCanonicalMDString(Context, Name), Storage, ShouldCreate);
  }
  static DIImportedEntity *getImpl(LLVMContext &Context, unsigned Tag,
                                   Metadata *Scope, Metadata *Entity,
                                   Metadata *File, unsigned Line,
                                   MDString *Name, StorageType Storage,
                                   bool ShouldCreate = true);

  TempDIImportedEntity cloneImpl() const {
    return getTemporary(getContext(), getTag(), getScope(), getEntity(),
                        getFile(), getLine(), getName());
  }

public:
  DEFINE_MDNODE_GET(DIImportedEntity,
                    (unsigned Tag, DIScope *Scope, DINodeRef Entity,
                     DIFile *File, unsigned Line, StringRef Name = ""),
                    (Tag, Scope, Entity, File, Line, Name))
  DEFINE_MDNODE_GET(DIImportedEntity,
                    (unsigned Tag, Metadata *Scope, Metadata *Entity,
                     Metadata *File, unsigned Line, MDString *Name),
                    (Tag, Scope, Entity, File, Line, Name))

  TempDIImportedEntity clone() const { return cloneImpl(); }

  unsigned getLine() const { return Line; }
  DIScope *getScope() const { return cast_or_null<DIScope>(getRawScope()); }
  DINodeRef getEntity() const { return DINodeRef(getRawEntity()); }
  StringRef getName() const { return getStringOperand(2); }
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }

  Metadata *getRawScope() const { return getOperand(0); }
  Metadata *getRawEntity() const { return getOperand(1); }
  MDString *getRawName() const { return getOperandAs<MDString>(2); }
  Metadata *getRawFile() const { return getOperand(3); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIImportedEntityKind;
  }
};

/// A pair of DIGlobalVariable and DIExpression.
class DIGlobalVariableExpression : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  DIGlobalVariableExpression(LLVMContext &C, StorageType Storage,
                             ArrayRef<Metadata *> Ops)
      : MDNode(C, DIGlobalVariableExpressionKind, Storage, Ops) {}
  ~DIGlobalVariableExpression() = default;

  static DIGlobalVariableExpression *
  getImpl(LLVMContext &Context, Metadata *Variable, Metadata *Expression,
          StorageType Storage, bool ShouldCreate = true);

  TempDIGlobalVariableExpression cloneImpl() const {
    return getTemporary(getContext(), getVariable(), getExpression());
  }

public:
  DEFINE_MDNODE_GET(DIGlobalVariableExpression,
                    (Metadata * Variable, Metadata *Expression),
                    (Variable, Expression))

  TempDIGlobalVariableExpression clone() const { return cloneImpl(); }

  Metadata *getRawVariable() const { return getOperand(0); }

  DIGlobalVariable *getVariable() const {
    return cast_or_null<DIGlobalVariable>(getRawVariable());
  }

  Metadata *getRawExpression() const { return getOperand(1); }

  DIExpression *getExpression() const {
    return cast<DIExpression>(getRawExpression());
  }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIGlobalVariableExpressionKind;
  }
};

/// Macro Info DWARF-like metadata node.
///
/// A metadata node with a DWARF macro info (i.e., a constant named
/// \c DW_MACINFO_*, defined in llvm/BinaryFormat/Dwarf.h).  Called \a
/// DIMacroNode
/// because it's potentially used for non-DWARF output.
class DIMacroNode : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

protected:
  DIMacroNode(LLVMContext &C, unsigned ID, StorageType Storage, unsigned MIType,
              ArrayRef<Metadata *> Ops1, ArrayRef<Metadata *> Ops2 = None)
      : MDNode(C, ID, Storage, Ops1, Ops2) {
    assert(MIType < 1u << 16);
    SubclassData16 = MIType;
  }
  ~DIMacroNode() = default;

  template <class Ty> Ty *getOperandAs(unsigned I) const {
    return cast_or_null<Ty>(getOperand(I));
  }

  StringRef getStringOperand(unsigned I) const {
    if (auto *S = getOperandAs<MDString>(I))
      return S->getString();
    return StringRef();
  }

  static MDString *getCanonicalMDString(LLVMContext &Context, StringRef S) {
    if (S.empty())
      return nullptr;
    return MDString::get(Context, S);
  }

public:
  unsigned getMacinfoType() const { return SubclassData16; }

  static bool classof(const Metadata *MD) {
    switch (MD->getMetadataID()) {
    default:
      return false;
    case DIMacroKind:
    case DIMacroFileKind:
      return true;
    }
  }
};

class DIMacro : public DIMacroNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Line;

  DIMacro(LLVMContext &C, StorageType Storage, unsigned MIType, unsigned Line,
          ArrayRef<Metadata *> Ops)
      : DIMacroNode(C, DIMacroKind, Storage, MIType, Ops), Line(Line) {}
  ~DIMacro() = default;

  static DIMacro *getImpl(LLVMContext &Context, unsigned MIType, unsigned Line,
                          StringRef Name, StringRef Value, StorageType Storage,
                          bool ShouldCreate = true) {
    return getImpl(Context, MIType, Line, getCanonicalMDString(Context, Name),
                   getCanonicalMDString(Context, Value), Storage, ShouldCreate);
  }
  static DIMacro *getImpl(LLVMContext &Context, unsigned MIType, unsigned Line,
                          MDString *Name, MDString *Value, StorageType Storage,
                          bool ShouldCreate = true);

  TempDIMacro cloneImpl() const {
    return getTemporary(getContext(), getMacinfoType(), getLine(), getName(),
                        getValue());
  }

public:
  DEFINE_MDNODE_GET(DIMacro, (unsigned MIType, unsigned Line, StringRef Name,
                              StringRef Value = ""),
                    (MIType, Line, Name, Value))
  DEFINE_MDNODE_GET(DIMacro, (unsigned MIType, unsigned Line, MDString *Name,
                              MDString *Value),
                    (MIType, Line, Name, Value))

  TempDIMacro clone() const { return cloneImpl(); }

  unsigned getLine() const { return Line; }

  StringRef getName() const { return getStringOperand(0); }
  StringRef getValue() const { return getStringOperand(1); }

  MDString *getRawName() const { return getOperandAs<MDString>(0); }
  MDString *getRawValue() const { return getOperandAs<MDString>(1); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIMacroKind;
  }
};

class DIMacroFile : public DIMacroNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  unsigned Line;

  DIMacroFile(LLVMContext &C, StorageType Storage, unsigned MIType,
              unsigned Line, ArrayRef<Metadata *> Ops)
      : DIMacroNode(C, DIMacroFileKind, Storage, MIType, Ops), Line(Line) {}
  ~DIMacroFile() = default;

  static DIMacroFile *getImpl(LLVMContext &Context, unsigned MIType,
                              unsigned Line, DIFile *File,
                              DIMacroNodeArray Elements, StorageType Storage,
                              bool ShouldCreate = true) {
    return getImpl(Context, MIType, Line, static_cast<Metadata *>(File),
                   Elements.get(), Storage, ShouldCreate);
  }

  static DIMacroFile *getImpl(LLVMContext &Context, unsigned MIType,
                              unsigned Line, Metadata *File, Metadata *Elements,
                              StorageType Storage, bool ShouldCreate = true);

  TempDIMacroFile cloneImpl() const {
    return getTemporary(getContext(), getMacinfoType(), getLine(), getFile(),
                        getElements());
  }

public:
  DEFINE_MDNODE_GET(DIMacroFile, (unsigned MIType, unsigned Line, DIFile *File,
                                  DIMacroNodeArray Elements),
                    (MIType, Line, File, Elements))
  DEFINE_MDNODE_GET(DIMacroFile, (unsigned MIType, unsigned Line,
                                  Metadata *File, Metadata *Elements),
                    (MIType, Line, File, Elements))

  TempDIMacroFile clone() const { return cloneImpl(); }

  void replaceElements(DIMacroNodeArray Elements) {
#ifndef NDEBUG
    for (DIMacroNode *Op : getElements())
      assert(is_contained(Elements->operands(), Op) &&
             "Lost a macro node during macro node list replacement");
#endif
    replaceOperandWith(1, Elements.get());
  }

  unsigned getLine() const { return Line; }
  DIFile *getFile() const { return cast_or_null<DIFile>(getRawFile()); }

  DIMacroNodeArray getElements() const {
    return cast_or_null<MDTuple>(getRawElements());
  }

  Metadata *getRawFile() const { return getOperand(0); }
  Metadata *getRawElements() const { return getOperand(1); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == DIMacroFileKind;
  }
};

} // end namespace llvm

#undef DEFINE_MDNODE_GET_UNPACK_IMPL
#undef DEFINE_MDNODE_GET_UNPACK
#undef DEFINE_MDNODE_GET

#endif // LLVM_IR_DEBUGINFOMETADATA_H
