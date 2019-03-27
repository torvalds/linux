//===- LLVMContextImpl.h - The LLVMContextImpl opaque class -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file declares LLVMContextImpl, the opaque implementation
//  of LLVMContext.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_IR_LLVMCONTEXTIMPL_H
#define LLVM_LIB_IR_LLVMCONTEXTIMPL_H

#include "AttributeImpl.h"
#include "ConstantsContext.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/TrackingMDRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/YAMLTraits.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class ConstantFP;
class ConstantInt;
class Type;
class Value;
class ValueHandleBase;

struct DenseMapAPIntKeyInfo {
  static inline APInt getEmptyKey() {
    APInt V(nullptr, 0);
    V.U.VAL = 0;
    return V;
  }

  static inline APInt getTombstoneKey() {
    APInt V(nullptr, 0);
    V.U.VAL = 1;
    return V;
  }

  static unsigned getHashValue(const APInt &Key) {
    return static_cast<unsigned>(hash_value(Key));
  }

  static bool isEqual(const APInt &LHS, const APInt &RHS) {
    return LHS.getBitWidth() == RHS.getBitWidth() && LHS == RHS;
  }
};

struct DenseMapAPFloatKeyInfo {
  static inline APFloat getEmptyKey() { return APFloat(APFloat::Bogus(), 1); }
  static inline APFloat getTombstoneKey() { return APFloat(APFloat::Bogus(), 2); }

  static unsigned getHashValue(const APFloat &Key) {
    return static_cast<unsigned>(hash_value(Key));
  }

  static bool isEqual(const APFloat &LHS, const APFloat &RHS) {
    return LHS.bitwiseIsEqual(RHS);
  }
};

struct AnonStructTypeKeyInfo {
  struct KeyTy {
    ArrayRef<Type*> ETypes;
    bool isPacked;

    KeyTy(const ArrayRef<Type*>& E, bool P) :
      ETypes(E), isPacked(P) {}

    KeyTy(const StructType *ST)
        : ETypes(ST->elements()), isPacked(ST->isPacked()) {}

    bool operator==(const KeyTy& that) const {
      if (isPacked != that.isPacked)
        return false;
      if (ETypes != that.ETypes)
        return false;
      return true;
    }
    bool operator!=(const KeyTy& that) const {
      return !this->operator==(that);
    }
  };

  static inline StructType* getEmptyKey() {
    return DenseMapInfo<StructType*>::getEmptyKey();
  }

  static inline StructType* getTombstoneKey() {
    return DenseMapInfo<StructType*>::getTombstoneKey();
  }

  static unsigned getHashValue(const KeyTy& Key) {
    return hash_combine(hash_combine_range(Key.ETypes.begin(),
                                           Key.ETypes.end()),
                        Key.isPacked);
  }

  static unsigned getHashValue(const StructType *ST) {
    return getHashValue(KeyTy(ST));
  }

  static bool isEqual(const KeyTy& LHS, const StructType *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return LHS == KeyTy(RHS);
  }

  static bool isEqual(const StructType *LHS, const StructType *RHS) {
    return LHS == RHS;
  }
};

struct FunctionTypeKeyInfo {
  struct KeyTy {
    const Type *ReturnType;
    ArrayRef<Type*> Params;
    bool isVarArg;

    KeyTy(const Type* R, const ArrayRef<Type*>& P, bool V) :
      ReturnType(R), Params(P), isVarArg(V) {}
    KeyTy(const FunctionType *FT)
        : ReturnType(FT->getReturnType()), Params(FT->params()),
          isVarArg(FT->isVarArg()) {}

    bool operator==(const KeyTy& that) const {
      if (ReturnType != that.ReturnType)
        return false;
      if (isVarArg != that.isVarArg)
        return false;
      if (Params != that.Params)
        return false;
      return true;
    }
    bool operator!=(const KeyTy& that) const {
      return !this->operator==(that);
    }
  };

  static inline FunctionType* getEmptyKey() {
    return DenseMapInfo<FunctionType*>::getEmptyKey();
  }

  static inline FunctionType* getTombstoneKey() {
    return DenseMapInfo<FunctionType*>::getTombstoneKey();
  }

  static unsigned getHashValue(const KeyTy& Key) {
    return hash_combine(Key.ReturnType,
                        hash_combine_range(Key.Params.begin(),
                                           Key.Params.end()),
                        Key.isVarArg);
  }

  static unsigned getHashValue(const FunctionType *FT) {
    return getHashValue(KeyTy(FT));
  }

  static bool isEqual(const KeyTy& LHS, const FunctionType *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return LHS == KeyTy(RHS);
  }

  static bool isEqual(const FunctionType *LHS, const FunctionType *RHS) {
    return LHS == RHS;
  }
};

/// Structure for hashing arbitrary MDNode operands.
class MDNodeOpsKey {
  ArrayRef<Metadata *> RawOps;
  ArrayRef<MDOperand> Ops;
  unsigned Hash;

protected:
  MDNodeOpsKey(ArrayRef<Metadata *> Ops)
      : RawOps(Ops), Hash(calculateHash(Ops)) {}

  template <class NodeTy>
  MDNodeOpsKey(const NodeTy *N, unsigned Offset = 0)
      : Ops(N->op_begin() + Offset, N->op_end()), Hash(N->getHash()) {}

  template <class NodeTy>
  bool compareOps(const NodeTy *RHS, unsigned Offset = 0) const {
    if (getHash() != RHS->getHash())
      return false;

    assert((RawOps.empty() || Ops.empty()) && "Two sets of operands?");
    return RawOps.empty() ? compareOps(Ops, RHS, Offset)
                          : compareOps(RawOps, RHS, Offset);
  }

  static unsigned calculateHash(MDNode *N, unsigned Offset = 0);

private:
  template <class T>
  static bool compareOps(ArrayRef<T> Ops, const MDNode *RHS, unsigned Offset) {
    if (Ops.size() != RHS->getNumOperands() - Offset)
      return false;
    return std::equal(Ops.begin(), Ops.end(), RHS->op_begin() + Offset);
  }

  static unsigned calculateHash(ArrayRef<Metadata *> Ops);

public:
  unsigned getHash() const { return Hash; }
};

template <class NodeTy> struct MDNodeKeyImpl;

/// Configuration point for MDNodeInfo::isEqual().
template <class NodeTy> struct MDNodeSubsetEqualImpl {
  using KeyTy = MDNodeKeyImpl<NodeTy>;

  static bool isSubsetEqual(const KeyTy &LHS, const NodeTy *RHS) {
    return false;
  }

  static bool isSubsetEqual(const NodeTy *LHS, const NodeTy *RHS) {
    return false;
  }
};

/// DenseMapInfo for MDTuple.
///
/// Note that we don't need the is-function-local bit, since that's implicit in
/// the operands.
template <> struct MDNodeKeyImpl<MDTuple> : MDNodeOpsKey {
  MDNodeKeyImpl(ArrayRef<Metadata *> Ops) : MDNodeOpsKey(Ops) {}
  MDNodeKeyImpl(const MDTuple *N) : MDNodeOpsKey(N) {}

  bool isKeyOf(const MDTuple *RHS) const { return compareOps(RHS); }

  unsigned getHashValue() const { return getHash(); }

  static unsigned calculateHash(MDTuple *N) {
    return MDNodeOpsKey::calculateHash(N);
  }
};

/// DenseMapInfo for DILocation.
template <> struct MDNodeKeyImpl<DILocation> {
  unsigned Line;
  unsigned Column;
  Metadata *Scope;
  Metadata *InlinedAt;
  bool ImplicitCode;

  MDNodeKeyImpl(unsigned Line, unsigned Column, Metadata *Scope,
                Metadata *InlinedAt, bool ImplicitCode)
      : Line(Line), Column(Column), Scope(Scope), InlinedAt(InlinedAt),
        ImplicitCode(ImplicitCode) {}
  MDNodeKeyImpl(const DILocation *L)
      : Line(L->getLine()), Column(L->getColumn()), Scope(L->getRawScope()),
        InlinedAt(L->getRawInlinedAt()), ImplicitCode(L->isImplicitCode()) {}

  bool isKeyOf(const DILocation *RHS) const {
    return Line == RHS->getLine() && Column == RHS->getColumn() &&
           Scope == RHS->getRawScope() && InlinedAt == RHS->getRawInlinedAt() &&
           ImplicitCode == RHS->isImplicitCode();
  }

  unsigned getHashValue() const {
    return hash_combine(Line, Column, Scope, InlinedAt, ImplicitCode);
  }
};

/// DenseMapInfo for GenericDINode.
template <> struct MDNodeKeyImpl<GenericDINode> : MDNodeOpsKey {
  unsigned Tag;
  MDString *Header;

  MDNodeKeyImpl(unsigned Tag, MDString *Header, ArrayRef<Metadata *> DwarfOps)
      : MDNodeOpsKey(DwarfOps), Tag(Tag), Header(Header) {}
  MDNodeKeyImpl(const GenericDINode *N)
      : MDNodeOpsKey(N, 1), Tag(N->getTag()), Header(N->getRawHeader()) {}

  bool isKeyOf(const GenericDINode *RHS) const {
    return Tag == RHS->getTag() && Header == RHS->getRawHeader() &&
           compareOps(RHS, 1);
  }

  unsigned getHashValue() const { return hash_combine(getHash(), Tag, Header); }

  static unsigned calculateHash(GenericDINode *N) {
    return MDNodeOpsKey::calculateHash(N, 1);
  }
};

template <> struct MDNodeKeyImpl<DISubrange> {
  Metadata *CountNode;
  int64_t LowerBound;

  MDNodeKeyImpl(Metadata *CountNode, int64_t LowerBound)
      : CountNode(CountNode), LowerBound(LowerBound) {}
  MDNodeKeyImpl(const DISubrange *N)
      : CountNode(N->getRawCountNode()),
        LowerBound(N->getLowerBound()) {}

  bool isKeyOf(const DISubrange *RHS) const {
    if (LowerBound != RHS->getLowerBound())
      return false;

    if (auto *RHSCount = RHS->getCount().dyn_cast<ConstantInt*>())
      if (auto *MD = dyn_cast<ConstantAsMetadata>(CountNode))
        if (RHSCount->getSExtValue() ==
            cast<ConstantInt>(MD->getValue())->getSExtValue())
          return true;

    return CountNode == RHS->getRawCountNode();
  }

  unsigned getHashValue() const {
    if (auto *MD = dyn_cast<ConstantAsMetadata>(CountNode))
      return hash_combine(cast<ConstantInt>(MD->getValue())->getSExtValue(),
                          LowerBound);
    return hash_combine(CountNode, LowerBound);
  }
};

template <> struct MDNodeKeyImpl<DIEnumerator> {
  int64_t Value;
  MDString *Name;
  bool IsUnsigned;

  MDNodeKeyImpl(int64_t Value, bool IsUnsigned, MDString *Name)
      : Value(Value), Name(Name), IsUnsigned(IsUnsigned) {}
  MDNodeKeyImpl(const DIEnumerator *N)
      : Value(N->getValue()), Name(N->getRawName()),
        IsUnsigned(N->isUnsigned()) {}

  bool isKeyOf(const DIEnumerator *RHS) const {
    return Value == RHS->getValue() && IsUnsigned == RHS->isUnsigned() &&
           Name == RHS->getRawName();
  }

  unsigned getHashValue() const { return hash_combine(Value, Name); }
};

template <> struct MDNodeKeyImpl<DIBasicType> {
  unsigned Tag;
  MDString *Name;
  uint64_t SizeInBits;
  uint32_t AlignInBits;
  unsigned Encoding;
  unsigned Flags;

  MDNodeKeyImpl(unsigned Tag, MDString *Name, uint64_t SizeInBits,
                uint32_t AlignInBits, unsigned Encoding, unsigned Flags)
      : Tag(Tag), Name(Name), SizeInBits(SizeInBits), AlignInBits(AlignInBits),
        Encoding(Encoding), Flags(Flags) {}
  MDNodeKeyImpl(const DIBasicType *N)
      : Tag(N->getTag()), Name(N->getRawName()), SizeInBits(N->getSizeInBits()),
        AlignInBits(N->getAlignInBits()), Encoding(N->getEncoding()), Flags(N->getFlags()) {}

  bool isKeyOf(const DIBasicType *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getRawName() &&
           SizeInBits == RHS->getSizeInBits() &&
           AlignInBits == RHS->getAlignInBits() &&
           Encoding == RHS->getEncoding() &&
           Flags == RHS->getFlags();
  }

  unsigned getHashValue() const {
    return hash_combine(Tag, Name, SizeInBits, AlignInBits, Encoding);
  }
};

template <> struct MDNodeKeyImpl<DIDerivedType> {
  unsigned Tag;
  MDString *Name;
  Metadata *File;
  unsigned Line;
  Metadata *Scope;
  Metadata *BaseType;
  uint64_t SizeInBits;
  uint64_t OffsetInBits;
  uint32_t AlignInBits;
  Optional<unsigned> DWARFAddressSpace;
  unsigned Flags;
  Metadata *ExtraData;

  MDNodeKeyImpl(unsigned Tag, MDString *Name, Metadata *File, unsigned Line,
                Metadata *Scope, Metadata *BaseType, uint64_t SizeInBits,
                uint32_t AlignInBits, uint64_t OffsetInBits,
                Optional<unsigned> DWARFAddressSpace, unsigned Flags,
                Metadata *ExtraData)
      : Tag(Tag), Name(Name), File(File), Line(Line), Scope(Scope),
        BaseType(BaseType), SizeInBits(SizeInBits), OffsetInBits(OffsetInBits),
        AlignInBits(AlignInBits), DWARFAddressSpace(DWARFAddressSpace),
        Flags(Flags), ExtraData(ExtraData) {}
  MDNodeKeyImpl(const DIDerivedType *N)
      : Tag(N->getTag()), Name(N->getRawName()), File(N->getRawFile()),
        Line(N->getLine()), Scope(N->getRawScope()),
        BaseType(N->getRawBaseType()), SizeInBits(N->getSizeInBits()),
        OffsetInBits(N->getOffsetInBits()), AlignInBits(N->getAlignInBits()),
        DWARFAddressSpace(N->getDWARFAddressSpace()), Flags(N->getFlags()),
        ExtraData(N->getRawExtraData()) {}

  bool isKeyOf(const DIDerivedType *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getRawName() &&
           File == RHS->getRawFile() && Line == RHS->getLine() &&
           Scope == RHS->getRawScope() && BaseType == RHS->getRawBaseType() &&
           SizeInBits == RHS->getSizeInBits() &&
           AlignInBits == RHS->getAlignInBits() &&
           OffsetInBits == RHS->getOffsetInBits() &&
           DWARFAddressSpace == RHS->getDWARFAddressSpace() &&
           Flags == RHS->getFlags() &&
           ExtraData == RHS->getRawExtraData();
  }

  unsigned getHashValue() const {
    // If this is a member inside an ODR type, only hash the type and the name.
    // Otherwise the hash will be stronger than
    // MDNodeSubsetEqualImpl::isODRMember().
    if (Tag == dwarf::DW_TAG_member && Name)
      if (auto *CT = dyn_cast_or_null<DICompositeType>(Scope))
        if (CT->getRawIdentifier())
          return hash_combine(Name, Scope);

    // Intentionally computes the hash on a subset of the operands for
    // performance reason. The subset has to be significant enough to avoid
    // collision "most of the time". There is no correctness issue in case of
    // collision because of the full check above.
    return hash_combine(Tag, Name, File, Line, Scope, BaseType, Flags);
  }
};

template <> struct MDNodeSubsetEqualImpl<DIDerivedType> {
  using KeyTy = MDNodeKeyImpl<DIDerivedType>;

  static bool isSubsetEqual(const KeyTy &LHS, const DIDerivedType *RHS) {
    return isODRMember(LHS.Tag, LHS.Scope, LHS.Name, RHS);
  }

  static bool isSubsetEqual(const DIDerivedType *LHS, const DIDerivedType *RHS) {
    return isODRMember(LHS->getTag(), LHS->getRawScope(), LHS->getRawName(),
                       RHS);
  }

  /// Subprograms compare equal if they declare the same function in an ODR
  /// type.
  static bool isODRMember(unsigned Tag, const Metadata *Scope,
                          const MDString *Name, const DIDerivedType *RHS) {
    // Check whether the LHS is eligible.
    if (Tag != dwarf::DW_TAG_member || !Name)
      return false;

    auto *CT = dyn_cast_or_null<DICompositeType>(Scope);
    if (!CT || !CT->getRawIdentifier())
      return false;

    // Compare to the RHS.
    return Tag == RHS->getTag() && Name == RHS->getRawName() &&
           Scope == RHS->getRawScope();
  }
};

template <> struct MDNodeKeyImpl<DICompositeType> {
  unsigned Tag;
  MDString *Name;
  Metadata *File;
  unsigned Line;
  Metadata *Scope;
  Metadata *BaseType;
  uint64_t SizeInBits;
  uint64_t OffsetInBits;
  uint32_t AlignInBits;
  unsigned Flags;
  Metadata *Elements;
  unsigned RuntimeLang;
  Metadata *VTableHolder;
  Metadata *TemplateParams;
  MDString *Identifier;
  Metadata *Discriminator;

  MDNodeKeyImpl(unsigned Tag, MDString *Name, Metadata *File, unsigned Line,
                Metadata *Scope, Metadata *BaseType, uint64_t SizeInBits,
                uint32_t AlignInBits, uint64_t OffsetInBits, unsigned Flags,
                Metadata *Elements, unsigned RuntimeLang,
                Metadata *VTableHolder, Metadata *TemplateParams,
                MDString *Identifier, Metadata *Discriminator)
      : Tag(Tag), Name(Name), File(File), Line(Line), Scope(Scope),
        BaseType(BaseType), SizeInBits(SizeInBits), OffsetInBits(OffsetInBits),
        AlignInBits(AlignInBits), Flags(Flags), Elements(Elements),
        RuntimeLang(RuntimeLang), VTableHolder(VTableHolder),
        TemplateParams(TemplateParams), Identifier(Identifier),
        Discriminator(Discriminator) {}
  MDNodeKeyImpl(const DICompositeType *N)
      : Tag(N->getTag()), Name(N->getRawName()), File(N->getRawFile()),
        Line(N->getLine()), Scope(N->getRawScope()),
        BaseType(N->getRawBaseType()), SizeInBits(N->getSizeInBits()),
        OffsetInBits(N->getOffsetInBits()), AlignInBits(N->getAlignInBits()),
        Flags(N->getFlags()), Elements(N->getRawElements()),
        RuntimeLang(N->getRuntimeLang()), VTableHolder(N->getRawVTableHolder()),
        TemplateParams(N->getRawTemplateParams()),
        Identifier(N->getRawIdentifier()),
        Discriminator(N->getRawDiscriminator()) {}

  bool isKeyOf(const DICompositeType *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getRawName() &&
           File == RHS->getRawFile() && Line == RHS->getLine() &&
           Scope == RHS->getRawScope() && BaseType == RHS->getRawBaseType() &&
           SizeInBits == RHS->getSizeInBits() &&
           AlignInBits == RHS->getAlignInBits() &&
           OffsetInBits == RHS->getOffsetInBits() && Flags == RHS->getFlags() &&
           Elements == RHS->getRawElements() &&
           RuntimeLang == RHS->getRuntimeLang() &&
           VTableHolder == RHS->getRawVTableHolder() &&
           TemplateParams == RHS->getRawTemplateParams() &&
           Identifier == RHS->getRawIdentifier() &&
           Discriminator == RHS->getRawDiscriminator();
  }

  unsigned getHashValue() const {
    // Intentionally computes the hash on a subset of the operands for
    // performance reason. The subset has to be significant enough to avoid
    // collision "most of the time". There is no correctness issue in case of
    // collision because of the full check above.
    return hash_combine(Name, File, Line, BaseType, Scope, Elements,
                        TemplateParams);
  }
};

template <> struct MDNodeKeyImpl<DISubroutineType> {
  unsigned Flags;
  uint8_t CC;
  Metadata *TypeArray;

  MDNodeKeyImpl(unsigned Flags, uint8_t CC, Metadata *TypeArray)
      : Flags(Flags), CC(CC), TypeArray(TypeArray) {}
  MDNodeKeyImpl(const DISubroutineType *N)
      : Flags(N->getFlags()), CC(N->getCC()), TypeArray(N->getRawTypeArray()) {}

  bool isKeyOf(const DISubroutineType *RHS) const {
    return Flags == RHS->getFlags() && CC == RHS->getCC() &&
           TypeArray == RHS->getRawTypeArray();
  }

  unsigned getHashValue() const { return hash_combine(Flags, CC, TypeArray); }
};

template <> struct MDNodeKeyImpl<DIFile> {
  MDString *Filename;
  MDString *Directory;
  Optional<DIFile::ChecksumInfo<MDString *>> Checksum;
  Optional<MDString *> Source;

  MDNodeKeyImpl(MDString *Filename, MDString *Directory,
                Optional<DIFile::ChecksumInfo<MDString *>> Checksum,
                Optional<MDString *> Source)
      : Filename(Filename), Directory(Directory), Checksum(Checksum),
        Source(Source) {}
  MDNodeKeyImpl(const DIFile *N)
      : Filename(N->getRawFilename()), Directory(N->getRawDirectory()),
        Checksum(N->getRawChecksum()), Source(N->getRawSource()) {}

  bool isKeyOf(const DIFile *RHS) const {
    return Filename == RHS->getRawFilename() &&
           Directory == RHS->getRawDirectory() &&
           Checksum == RHS->getRawChecksum() &&
           Source == RHS->getRawSource();
  }

  unsigned getHashValue() const {
    return hash_combine(
        Filename, Directory, Checksum ? Checksum->Kind : 0,
        Checksum ? Checksum->Value : nullptr, Source.getValueOr(nullptr));
  }
};

template <> struct MDNodeKeyImpl<DISubprogram> {
  Metadata *Scope;
  MDString *Name;
  MDString *LinkageName;
  Metadata *File;
  unsigned Line;
  Metadata *Type;
  unsigned ScopeLine;
  Metadata *ContainingType;
  unsigned VirtualIndex;
  int ThisAdjustment;
  unsigned Flags;
  unsigned SPFlags;
  Metadata *Unit;
  Metadata *TemplateParams;
  Metadata *Declaration;
  Metadata *RetainedNodes;
  Metadata *ThrownTypes;

  MDNodeKeyImpl(Metadata *Scope, MDString *Name, MDString *LinkageName,
                Metadata *File, unsigned Line, Metadata *Type,
                unsigned ScopeLine, Metadata *ContainingType,
                unsigned VirtualIndex, int ThisAdjustment, unsigned Flags,
                unsigned SPFlags, Metadata *Unit, Metadata *TemplateParams,
                Metadata *Declaration, Metadata *RetainedNodes,
                Metadata *ThrownTypes)
      : Scope(Scope), Name(Name), LinkageName(LinkageName), File(File),
        Line(Line), Type(Type), ScopeLine(ScopeLine),
        ContainingType(ContainingType), VirtualIndex(VirtualIndex),
        ThisAdjustment(ThisAdjustment), Flags(Flags), SPFlags(SPFlags),
        Unit(Unit), TemplateParams(TemplateParams), Declaration(Declaration),
        RetainedNodes(RetainedNodes), ThrownTypes(ThrownTypes) {}
  MDNodeKeyImpl(const DISubprogram *N)
      : Scope(N->getRawScope()), Name(N->getRawName()),
        LinkageName(N->getRawLinkageName()), File(N->getRawFile()),
        Line(N->getLine()), Type(N->getRawType()), ScopeLine(N->getScopeLine()),
        ContainingType(N->getRawContainingType()),
        VirtualIndex(N->getVirtualIndex()),
        ThisAdjustment(N->getThisAdjustment()), Flags(N->getFlags()),
        SPFlags(N->getSPFlags()), Unit(N->getRawUnit()),
        TemplateParams(N->getRawTemplateParams()),
        Declaration(N->getRawDeclaration()),
        RetainedNodes(N->getRawRetainedNodes()),
        ThrownTypes(N->getRawThrownTypes()) {}

  bool isKeyOf(const DISubprogram *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getRawName() &&
           LinkageName == RHS->getRawLinkageName() &&
           File == RHS->getRawFile() && Line == RHS->getLine() &&
           Type == RHS->getRawType() && ScopeLine == RHS->getScopeLine() &&
           ContainingType == RHS->getRawContainingType() &&
           VirtualIndex == RHS->getVirtualIndex() &&
           ThisAdjustment == RHS->getThisAdjustment() &&
           Flags == RHS->getFlags() && SPFlags == RHS->getSPFlags() &&
           Unit == RHS->getUnit() &&
           TemplateParams == RHS->getRawTemplateParams() &&
           Declaration == RHS->getRawDeclaration() &&
           RetainedNodes == RHS->getRawRetainedNodes() &&
           ThrownTypes == RHS->getRawThrownTypes();
  }

  bool isDefinition() const { return SPFlags & DISubprogram::SPFlagDefinition; }

  unsigned getHashValue() const {
    // If this is a declaration inside an ODR type, only hash the type and the
    // name.  Otherwise the hash will be stronger than
    // MDNodeSubsetEqualImpl::isDeclarationOfODRMember().
    if (!isDefinition() && LinkageName)
      if (auto *CT = dyn_cast_or_null<DICompositeType>(Scope))
        if (CT->getRawIdentifier())
          return hash_combine(LinkageName, Scope);

    // Intentionally computes the hash on a subset of the operands for
    // performance reason. The subset has to be significant enough to avoid
    // collision "most of the time". There is no correctness issue in case of
    // collision because of the full check above.
    return hash_combine(Name, Scope, File, Type, Line);
  }
};

template <> struct MDNodeSubsetEqualImpl<DISubprogram> {
  using KeyTy = MDNodeKeyImpl<DISubprogram>;

  static bool isSubsetEqual(const KeyTy &LHS, const DISubprogram *RHS) {
    return isDeclarationOfODRMember(LHS.isDefinition(), LHS.Scope,
                                    LHS.LinkageName, LHS.TemplateParams, RHS);
  }

  static bool isSubsetEqual(const DISubprogram *LHS, const DISubprogram *RHS) {
    return isDeclarationOfODRMember(LHS->isDefinition(), LHS->getRawScope(),
                                    LHS->getRawLinkageName(),
                                    LHS->getRawTemplateParams(), RHS);
  }

  /// Subprograms compare equal if they declare the same function in an ODR
  /// type.
  static bool isDeclarationOfODRMember(bool IsDefinition, const Metadata *Scope,
                                       const MDString *LinkageName,
                                       const Metadata *TemplateParams,
                                       const DISubprogram *RHS) {
    // Check whether the LHS is eligible.
    if (IsDefinition || !Scope || !LinkageName)
      return false;

    auto *CT = dyn_cast_or_null<DICompositeType>(Scope);
    if (!CT || !CT->getRawIdentifier())
      return false;

    // Compare to the RHS.
    // FIXME: We need to compare template parameters here to avoid incorrect
    // collisions in mapMetadata when RF_MoveDistinctMDs and a ODR-DISubprogram
    // has a non-ODR template parameter (i.e., a DICompositeType that does not
    // have an identifier). Eventually we should decouple ODR logic from
    // uniquing logic.
    return IsDefinition == RHS->isDefinition() && Scope == RHS->getRawScope() &&
           LinkageName == RHS->getRawLinkageName() &&
           TemplateParams == RHS->getRawTemplateParams();
  }
};

template <> struct MDNodeKeyImpl<DILexicalBlock> {
  Metadata *Scope;
  Metadata *File;
  unsigned Line;
  unsigned Column;

  MDNodeKeyImpl(Metadata *Scope, Metadata *File, unsigned Line, unsigned Column)
      : Scope(Scope), File(File), Line(Line), Column(Column) {}
  MDNodeKeyImpl(const DILexicalBlock *N)
      : Scope(N->getRawScope()), File(N->getRawFile()), Line(N->getLine()),
        Column(N->getColumn()) {}

  bool isKeyOf(const DILexicalBlock *RHS) const {
    return Scope == RHS->getRawScope() && File == RHS->getRawFile() &&
           Line == RHS->getLine() && Column == RHS->getColumn();
  }

  unsigned getHashValue() const {
    return hash_combine(Scope, File, Line, Column);
  }
};

template <> struct MDNodeKeyImpl<DILexicalBlockFile> {
  Metadata *Scope;
  Metadata *File;
  unsigned Discriminator;

  MDNodeKeyImpl(Metadata *Scope, Metadata *File, unsigned Discriminator)
      : Scope(Scope), File(File), Discriminator(Discriminator) {}
  MDNodeKeyImpl(const DILexicalBlockFile *N)
      : Scope(N->getRawScope()), File(N->getRawFile()),
        Discriminator(N->getDiscriminator()) {}

  bool isKeyOf(const DILexicalBlockFile *RHS) const {
    return Scope == RHS->getRawScope() && File == RHS->getRawFile() &&
           Discriminator == RHS->getDiscriminator();
  }

  unsigned getHashValue() const {
    return hash_combine(Scope, File, Discriminator);
  }
};

template <> struct MDNodeKeyImpl<DINamespace> {
  Metadata *Scope;
  MDString *Name;
  bool ExportSymbols;

  MDNodeKeyImpl(Metadata *Scope, MDString *Name, bool ExportSymbols)
      : Scope(Scope), Name(Name), ExportSymbols(ExportSymbols) {}
  MDNodeKeyImpl(const DINamespace *N)
      : Scope(N->getRawScope()), Name(N->getRawName()),
        ExportSymbols(N->getExportSymbols()) {}

  bool isKeyOf(const DINamespace *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getRawName() &&
           ExportSymbols == RHS->getExportSymbols();
  }

  unsigned getHashValue() const {
    return hash_combine(Scope, Name);
  }
};

template <> struct MDNodeKeyImpl<DIModule> {
  Metadata *Scope;
  MDString *Name;
  MDString *ConfigurationMacros;
  MDString *IncludePath;
  MDString *ISysRoot;

  MDNodeKeyImpl(Metadata *Scope, MDString *Name, MDString *ConfigurationMacros,
                MDString *IncludePath, MDString *ISysRoot)
      : Scope(Scope), Name(Name), ConfigurationMacros(ConfigurationMacros),
        IncludePath(IncludePath), ISysRoot(ISysRoot) {}
  MDNodeKeyImpl(const DIModule *N)
      : Scope(N->getRawScope()), Name(N->getRawName()),
        ConfigurationMacros(N->getRawConfigurationMacros()),
        IncludePath(N->getRawIncludePath()), ISysRoot(N->getRawISysRoot()) {}

  bool isKeyOf(const DIModule *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getRawName() &&
           ConfigurationMacros == RHS->getRawConfigurationMacros() &&
           IncludePath == RHS->getRawIncludePath() &&
           ISysRoot == RHS->getRawISysRoot();
  }

  unsigned getHashValue() const {
    return hash_combine(Scope, Name,
                        ConfigurationMacros, IncludePath, ISysRoot);
  }
};

template <> struct MDNodeKeyImpl<DITemplateTypeParameter> {
  MDString *Name;
  Metadata *Type;

  MDNodeKeyImpl(MDString *Name, Metadata *Type) : Name(Name), Type(Type) {}
  MDNodeKeyImpl(const DITemplateTypeParameter *N)
      : Name(N->getRawName()), Type(N->getRawType()) {}

  bool isKeyOf(const DITemplateTypeParameter *RHS) const {
    return Name == RHS->getRawName() && Type == RHS->getRawType();
  }

  unsigned getHashValue() const { return hash_combine(Name, Type); }
};

template <> struct MDNodeKeyImpl<DITemplateValueParameter> {
  unsigned Tag;
  MDString *Name;
  Metadata *Type;
  Metadata *Value;

  MDNodeKeyImpl(unsigned Tag, MDString *Name, Metadata *Type, Metadata *Value)
      : Tag(Tag), Name(Name), Type(Type), Value(Value) {}
  MDNodeKeyImpl(const DITemplateValueParameter *N)
      : Tag(N->getTag()), Name(N->getRawName()), Type(N->getRawType()),
        Value(N->getValue()) {}

  bool isKeyOf(const DITemplateValueParameter *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getRawName() &&
           Type == RHS->getRawType() && Value == RHS->getValue();
  }

  unsigned getHashValue() const { return hash_combine(Tag, Name, Type, Value); }
};

template <> struct MDNodeKeyImpl<DIGlobalVariable> {
  Metadata *Scope;
  MDString *Name;
  MDString *LinkageName;
  Metadata *File;
  unsigned Line;
  Metadata *Type;
  bool IsLocalToUnit;
  bool IsDefinition;
  Metadata *StaticDataMemberDeclaration;
  Metadata *TemplateParams;
  uint32_t AlignInBits;

  MDNodeKeyImpl(Metadata *Scope, MDString *Name, MDString *LinkageName,
                Metadata *File, unsigned Line, Metadata *Type,
                bool IsLocalToUnit, bool IsDefinition,
                Metadata *StaticDataMemberDeclaration, Metadata *TemplateParams,
                uint32_t AlignInBits)
      : Scope(Scope), Name(Name), LinkageName(LinkageName), File(File),
        Line(Line), Type(Type), IsLocalToUnit(IsLocalToUnit),
        IsDefinition(IsDefinition),
        StaticDataMemberDeclaration(StaticDataMemberDeclaration),
        TemplateParams(TemplateParams), AlignInBits(AlignInBits) {}
  MDNodeKeyImpl(const DIGlobalVariable *N)
      : Scope(N->getRawScope()), Name(N->getRawName()),
        LinkageName(N->getRawLinkageName()), File(N->getRawFile()),
        Line(N->getLine()), Type(N->getRawType()),
        IsLocalToUnit(N->isLocalToUnit()), IsDefinition(N->isDefinition()),
        StaticDataMemberDeclaration(N->getRawStaticDataMemberDeclaration()),
        TemplateParams(N->getRawTemplateParams()),
        AlignInBits(N->getAlignInBits()) {}

  bool isKeyOf(const DIGlobalVariable *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getRawName() &&
           LinkageName == RHS->getRawLinkageName() &&
           File == RHS->getRawFile() && Line == RHS->getLine() &&
           Type == RHS->getRawType() && IsLocalToUnit == RHS->isLocalToUnit() &&
           IsDefinition == RHS->isDefinition() &&
           StaticDataMemberDeclaration ==
               RHS->getRawStaticDataMemberDeclaration() &&
           TemplateParams == RHS->getRawTemplateParams() &&
           AlignInBits == RHS->getAlignInBits();
  }

  unsigned getHashValue() const {
    // We do not use AlignInBits in hashing function here on purpose:
    // in most cases this param for local variable is zero (for function param
    // it is always zero). This leads to lots of hash collisions and errors on
    // cases with lots of similar variables.
    // clang/test/CodeGen/debug-info-257-args.c is an example of this problem,
    // generated IR is random for each run and test fails with Align included.
    // TODO: make hashing work fine with such situations
    return hash_combine(Scope, Name, LinkageName, File, Line, Type,
                        IsLocalToUnit, IsDefinition, /* AlignInBits, */
                        StaticDataMemberDeclaration);
  }
};

template <> struct MDNodeKeyImpl<DILocalVariable> {
  Metadata *Scope;
  MDString *Name;
  Metadata *File;
  unsigned Line;
  Metadata *Type;
  unsigned Arg;
  unsigned Flags;
  uint32_t AlignInBits;

  MDNodeKeyImpl(Metadata *Scope, MDString *Name, Metadata *File, unsigned Line,
                Metadata *Type, unsigned Arg, unsigned Flags,
                uint32_t AlignInBits)
      : Scope(Scope), Name(Name), File(File), Line(Line), Type(Type), Arg(Arg),
        Flags(Flags), AlignInBits(AlignInBits) {}
  MDNodeKeyImpl(const DILocalVariable *N)
      : Scope(N->getRawScope()), Name(N->getRawName()), File(N->getRawFile()),
        Line(N->getLine()), Type(N->getRawType()), Arg(N->getArg()),
        Flags(N->getFlags()), AlignInBits(N->getAlignInBits()) {}

  bool isKeyOf(const DILocalVariable *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getRawName() &&
           File == RHS->getRawFile() && Line == RHS->getLine() &&
           Type == RHS->getRawType() && Arg == RHS->getArg() &&
           Flags == RHS->getFlags() && AlignInBits == RHS->getAlignInBits();
  }

  unsigned getHashValue() const {
    // We do not use AlignInBits in hashing function here on purpose:
    // in most cases this param for local variable is zero (for function param
    // it is always zero). This leads to lots of hash collisions and errors on
    // cases with lots of similar variables.
    // clang/test/CodeGen/debug-info-257-args.c is an example of this problem,
    // generated IR is random for each run and test fails with Align included.
    // TODO: make hashing work fine with such situations
    return hash_combine(Scope, Name, File, Line, Type, Arg, Flags);
  }
};

template <> struct MDNodeKeyImpl<DILabel> {
  Metadata *Scope;
  MDString *Name;
  Metadata *File;
  unsigned Line;

  MDNodeKeyImpl(Metadata *Scope, MDString *Name, Metadata *File, unsigned Line)
      : Scope(Scope), Name(Name), File(File), Line(Line) {}
  MDNodeKeyImpl(const DILabel *N)
      : Scope(N->getRawScope()), Name(N->getRawName()), File(N->getRawFile()),
        Line(N->getLine()) {}

  bool isKeyOf(const DILabel *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getRawName() &&
           File == RHS->getRawFile() && Line == RHS->getLine();
  }

  /// Using name and line to get hash value. It should already be mostly unique.
  unsigned getHashValue() const {
    return hash_combine(Scope, Name, Line);
  }
};

template <> struct MDNodeKeyImpl<DIExpression> {
  ArrayRef<uint64_t> Elements;

  MDNodeKeyImpl(ArrayRef<uint64_t> Elements) : Elements(Elements) {}
  MDNodeKeyImpl(const DIExpression *N) : Elements(N->getElements()) {}

  bool isKeyOf(const DIExpression *RHS) const {
    return Elements == RHS->getElements();
  }

  unsigned getHashValue() const {
    return hash_combine_range(Elements.begin(), Elements.end());
  }
};

template <> struct MDNodeKeyImpl<DIGlobalVariableExpression> {
  Metadata *Variable;
  Metadata *Expression;

  MDNodeKeyImpl(Metadata *Variable, Metadata *Expression)
      : Variable(Variable), Expression(Expression) {}
  MDNodeKeyImpl(const DIGlobalVariableExpression *N)
      : Variable(N->getRawVariable()), Expression(N->getRawExpression()) {}

  bool isKeyOf(const DIGlobalVariableExpression *RHS) const {
    return Variable == RHS->getRawVariable() &&
           Expression == RHS->getRawExpression();
  }

  unsigned getHashValue() const { return hash_combine(Variable, Expression); }
};

template <> struct MDNodeKeyImpl<DIObjCProperty> {
  MDString *Name;
  Metadata *File;
  unsigned Line;
  MDString *GetterName;
  MDString *SetterName;
  unsigned Attributes;
  Metadata *Type;

  MDNodeKeyImpl(MDString *Name, Metadata *File, unsigned Line,
                MDString *GetterName, MDString *SetterName, unsigned Attributes,
                Metadata *Type)
      : Name(Name), File(File), Line(Line), GetterName(GetterName),
        SetterName(SetterName), Attributes(Attributes), Type(Type) {}
  MDNodeKeyImpl(const DIObjCProperty *N)
      : Name(N->getRawName()), File(N->getRawFile()), Line(N->getLine()),
        GetterName(N->getRawGetterName()), SetterName(N->getRawSetterName()),
        Attributes(N->getAttributes()), Type(N->getRawType()) {}

  bool isKeyOf(const DIObjCProperty *RHS) const {
    return Name == RHS->getRawName() && File == RHS->getRawFile() &&
           Line == RHS->getLine() && GetterName == RHS->getRawGetterName() &&
           SetterName == RHS->getRawSetterName() &&
           Attributes == RHS->getAttributes() && Type == RHS->getRawType();
  }

  unsigned getHashValue() const {
    return hash_combine(Name, File, Line, GetterName, SetterName, Attributes,
                        Type);
  }
};

template <> struct MDNodeKeyImpl<DIImportedEntity> {
  unsigned Tag;
  Metadata *Scope;
  Metadata *Entity;
  Metadata *File;
  unsigned Line;
  MDString *Name;

  MDNodeKeyImpl(unsigned Tag, Metadata *Scope, Metadata *Entity, Metadata *File,
                unsigned Line, MDString *Name)
      : Tag(Tag), Scope(Scope), Entity(Entity), File(File), Line(Line),
        Name(Name) {}
  MDNodeKeyImpl(const DIImportedEntity *N)
      : Tag(N->getTag()), Scope(N->getRawScope()), Entity(N->getRawEntity()),
        File(N->getRawFile()), Line(N->getLine()), Name(N->getRawName()) {}

  bool isKeyOf(const DIImportedEntity *RHS) const {
    return Tag == RHS->getTag() && Scope == RHS->getRawScope() &&
           Entity == RHS->getRawEntity() && File == RHS->getFile() &&
           Line == RHS->getLine() && Name == RHS->getRawName();
  }

  unsigned getHashValue() const {
    return hash_combine(Tag, Scope, Entity, File, Line, Name);
  }
};

template <> struct MDNodeKeyImpl<DIMacro> {
  unsigned MIType;
  unsigned Line;
  MDString *Name;
  MDString *Value;

  MDNodeKeyImpl(unsigned MIType, unsigned Line, MDString *Name, MDString *Value)
      : MIType(MIType), Line(Line), Name(Name), Value(Value) {}
  MDNodeKeyImpl(const DIMacro *N)
      : MIType(N->getMacinfoType()), Line(N->getLine()), Name(N->getRawName()),
        Value(N->getRawValue()) {}

  bool isKeyOf(const DIMacro *RHS) const {
    return MIType == RHS->getMacinfoType() && Line == RHS->getLine() &&
           Name == RHS->getRawName() && Value == RHS->getRawValue();
  }

  unsigned getHashValue() const {
    return hash_combine(MIType, Line, Name, Value);
  }
};

template <> struct MDNodeKeyImpl<DIMacroFile> {
  unsigned MIType;
  unsigned Line;
  Metadata *File;
  Metadata *Elements;

  MDNodeKeyImpl(unsigned MIType, unsigned Line, Metadata *File,
                Metadata *Elements)
      : MIType(MIType), Line(Line), File(File), Elements(Elements) {}
  MDNodeKeyImpl(const DIMacroFile *N)
      : MIType(N->getMacinfoType()), Line(N->getLine()), File(N->getRawFile()),
        Elements(N->getRawElements()) {}

  bool isKeyOf(const DIMacroFile *RHS) const {
    return MIType == RHS->getMacinfoType() && Line == RHS->getLine() &&
           File == RHS->getRawFile() && Elements == RHS->getRawElements();
  }

  unsigned getHashValue() const {
    return hash_combine(MIType, Line, File, Elements);
  }
};

/// DenseMapInfo for MDNode subclasses.
template <class NodeTy> struct MDNodeInfo {
  using KeyTy = MDNodeKeyImpl<NodeTy>;
  using SubsetEqualTy = MDNodeSubsetEqualImpl<NodeTy>;

  static inline NodeTy *getEmptyKey() {
    return DenseMapInfo<NodeTy *>::getEmptyKey();
  }

  static inline NodeTy *getTombstoneKey() {
    return DenseMapInfo<NodeTy *>::getTombstoneKey();
  }

  static unsigned getHashValue(const KeyTy &Key) { return Key.getHashValue(); }

  static unsigned getHashValue(const NodeTy *N) {
    return KeyTy(N).getHashValue();
  }

  static bool isEqual(const KeyTy &LHS, const NodeTy *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return SubsetEqualTy::isSubsetEqual(LHS, RHS) || LHS.isKeyOf(RHS);
  }

  static bool isEqual(const NodeTy *LHS, const NodeTy *RHS) {
    if (LHS == RHS)
      return true;
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return SubsetEqualTy::isSubsetEqual(LHS, RHS);
  }
};

#define HANDLE_MDNODE_LEAF(CLASS) using CLASS##Info = MDNodeInfo<CLASS>;
#include "llvm/IR/Metadata.def"

/// Map-like storage for metadata attachments.
class MDAttachmentMap {
  SmallVector<std::pair<unsigned, TrackingMDNodeRef>, 2> Attachments;

public:
  bool empty() const { return Attachments.empty(); }
  size_t size() const { return Attachments.size(); }

  /// Get a particular attachment (if any).
  MDNode *lookup(unsigned ID) const;

  /// Set an attachment to a particular node.
  ///
  /// Set the \c ID attachment to \c MD, replacing the current attachment at \c
  /// ID (if anyway).
  void set(unsigned ID, MDNode &MD);

  /// Remove an attachment.
  ///
  /// Remove the attachment at \c ID, if any.
  bool erase(unsigned ID);

  /// Copy out all the attachments.
  ///
  /// Copies all the current attachments into \c Result, sorting by attachment
  /// ID.  This function does \em not clear \c Result.
  void getAll(SmallVectorImpl<std::pair<unsigned, MDNode *>> &Result) const;

  /// Erase matching attachments.
  ///
  /// Erases all attachments matching the \c shouldRemove predicate.
  template <class PredTy> void remove_if(PredTy shouldRemove) {
    Attachments.erase(llvm::remove_if(Attachments, shouldRemove),
                      Attachments.end());
  }
};

/// Multimap-like storage for metadata attachments for globals. This differs
/// from MDAttachmentMap in that it allows multiple attachments per metadata
/// kind.
class MDGlobalAttachmentMap {
  struct Attachment {
    unsigned MDKind;
    TrackingMDNodeRef Node;
  };
  SmallVector<Attachment, 1> Attachments;

public:
  bool empty() const { return Attachments.empty(); }

  /// Appends all attachments with the given ID to \c Result in insertion order.
  /// If the global has no attachments with the given ID, or if ID is invalid,
  /// leaves Result unchanged.
  void get(unsigned ID, SmallVectorImpl<MDNode *> &Result) const;

  /// Returns the first attachment with the given ID or nullptr if no such
  /// attachment exists.
  MDNode *lookup(unsigned ID) const;

  void insert(unsigned ID, MDNode &MD);
  bool erase(unsigned ID);

  /// Appends all attachments for the global to \c Result, sorting by attachment
  /// ID. Attachments with the same ID appear in insertion order. This function
  /// does \em not clear \c Result.
  void getAll(SmallVectorImpl<std::pair<unsigned, MDNode *>> &Result) const;
};

class LLVMContextImpl {
public:
  /// OwnedModules - The set of modules instantiated in this context, and which
  /// will be automatically deleted if this context is deleted.
  SmallPtrSet<Module*, 4> OwnedModules;

  LLVMContext::InlineAsmDiagHandlerTy InlineAsmDiagHandler = nullptr;
  void *InlineAsmDiagContext = nullptr;

  std::unique_ptr<DiagnosticHandler> DiagHandler;
  bool RespectDiagnosticFilters = false;
  bool DiagnosticsHotnessRequested = false;
  uint64_t DiagnosticsHotnessThreshold = 0;
  std::unique_ptr<yaml::Output> DiagnosticsOutputFile;

  LLVMContext::YieldCallbackTy YieldCallback = nullptr;
  void *YieldOpaqueHandle = nullptr;

  using IntMapTy =
      DenseMap<APInt, std::unique_ptr<ConstantInt>, DenseMapAPIntKeyInfo>;
  IntMapTy IntConstants;

  using FPMapTy =
      DenseMap<APFloat, std::unique_ptr<ConstantFP>, DenseMapAPFloatKeyInfo>;
  FPMapTy FPConstants;

  FoldingSet<AttributeImpl> AttrsSet;
  FoldingSet<AttributeListImpl> AttrsLists;
  FoldingSet<AttributeSetNode> AttrsSetNodes;

  StringMap<MDString, BumpPtrAllocator> MDStringCache;
  DenseMap<Value *, ValueAsMetadata *> ValuesAsMetadata;
  DenseMap<Metadata *, MetadataAsValue *> MetadataAsValues;

  DenseMap<const Value*, ValueName*> ValueNames;

#define HANDLE_MDNODE_LEAF_UNIQUABLE(CLASS)                                    \
  DenseSet<CLASS *, CLASS##Info> CLASS##s;
#include "llvm/IR/Metadata.def"

  // Optional map for looking up composite types by identifier.
  Optional<DenseMap<const MDString *, DICompositeType *>> DITypeMap;

  // MDNodes may be uniqued or not uniqued.  When they're not uniqued, they
  // aren't in the MDNodeSet, but they're still shared between objects, so no
  // one object can destroy them.  Keep track of them here so we can delete
  // them on context teardown.
  std::vector<MDNode *> DistinctMDNodes;

  DenseMap<Type *, std::unique_ptr<ConstantAggregateZero>> CAZConstants;

  using ArrayConstantsTy = ConstantUniqueMap<ConstantArray>;
  ArrayConstantsTy ArrayConstants;

  using StructConstantsTy = ConstantUniqueMap<ConstantStruct>;
  StructConstantsTy StructConstants;

  using VectorConstantsTy = ConstantUniqueMap<ConstantVector>;
  VectorConstantsTy VectorConstants;

  DenseMap<PointerType *, std::unique_ptr<ConstantPointerNull>> CPNConstants;

  DenseMap<Type *, std::unique_ptr<UndefValue>> UVConstants;

  StringMap<ConstantDataSequential*> CDSConstants;

  DenseMap<std::pair<const Function *, const BasicBlock *>, BlockAddress *>
    BlockAddresses;
  ConstantUniqueMap<ConstantExpr> ExprConstants;

  ConstantUniqueMap<InlineAsm> InlineAsms;

  ConstantInt *TheTrueVal = nullptr;
  ConstantInt *TheFalseVal = nullptr;

  std::unique_ptr<ConstantTokenNone> TheNoneToken;

  // Basic type instances.
  Type VoidTy, LabelTy, HalfTy, FloatTy, DoubleTy, MetadataTy, TokenTy;
  Type X86_FP80Ty, FP128Ty, PPC_FP128Ty, X86_MMXTy;
  IntegerType Int1Ty, Int8Ty, Int16Ty, Int32Ty, Int64Ty, Int128Ty;

  /// TypeAllocator - All dynamically allocated types are allocated from this.
  /// They live forever until the context is torn down.
  BumpPtrAllocator TypeAllocator;

  DenseMap<unsigned, IntegerType*> IntegerTypes;

  using FunctionTypeSet = DenseSet<FunctionType *, FunctionTypeKeyInfo>;
  FunctionTypeSet FunctionTypes;
  using StructTypeSet = DenseSet<StructType *, AnonStructTypeKeyInfo>;
  StructTypeSet AnonStructTypes;
  StringMap<StructType*> NamedStructTypes;
  unsigned NamedStructTypesUniqueID = 0;

  DenseMap<std::pair<Type *, uint64_t>, ArrayType*> ArrayTypes;
  DenseMap<std::pair<Type *, unsigned>, VectorType*> VectorTypes;
  DenseMap<Type*, PointerType*> PointerTypes;  // Pointers in AddrSpace = 0
  DenseMap<std::pair<Type*, unsigned>, PointerType*> ASPointerTypes;

  /// ValueHandles - This map keeps track of all of the value handles that are
  /// watching a Value*.  The Value::HasValueHandle bit is used to know
  /// whether or not a value has an entry in this map.
  using ValueHandlesTy = DenseMap<Value *, ValueHandleBase *>;
  ValueHandlesTy ValueHandles;

  /// CustomMDKindNames - Map to hold the metadata string to ID mapping.
  StringMap<unsigned> CustomMDKindNames;

  /// Collection of per-instruction metadata used in this context.
  DenseMap<const Instruction *, MDAttachmentMap> InstructionMetadata;

  /// Collection of per-GlobalObject metadata used in this context.
  DenseMap<const GlobalObject *, MDGlobalAttachmentMap> GlobalObjectMetadata;

  /// Collection of per-GlobalObject sections used in this context.
  DenseMap<const GlobalObject *, StringRef> GlobalObjectSections;

  /// Stable collection of section strings.
  StringSet<> SectionStrings;

  /// DiscriminatorTable - This table maps file:line locations to an
  /// integer representing the next DWARF path discriminator to assign to
  /// instructions in different blocks at the same location.
  DenseMap<std::pair<const char *, unsigned>, unsigned> DiscriminatorTable;

  int getOrAddScopeRecordIdxEntry(MDNode *N, int ExistingIdx);
  int getOrAddScopeInlinedAtIdxEntry(MDNode *Scope, MDNode *IA,int ExistingIdx);

  /// A set of interned tags for operand bundles.  The StringMap maps
  /// bundle tags to their IDs.
  ///
  /// \see LLVMContext::getOperandBundleTagID
  StringMap<uint32_t> BundleTagCache;

  StringMapEntry<uint32_t> *getOrInsertBundleTag(StringRef Tag);
  void getOperandBundleTags(SmallVectorImpl<StringRef> &Tags) const;
  uint32_t getOperandBundleTagID(StringRef Tag) const;

  /// A set of interned synchronization scopes.  The StringMap maps
  /// synchronization scope names to their respective synchronization scope IDs.
  StringMap<SyncScope::ID> SSC;

  /// getOrInsertSyncScopeID - Maps synchronization scope name to
  /// synchronization scope ID.  Every synchronization scope registered with
  /// LLVMContext has unique ID except pre-defined ones.
  SyncScope::ID getOrInsertSyncScopeID(StringRef SSN);

  /// getSyncScopeNames - Populates client supplied SmallVector with
  /// synchronization scope names registered with LLVMContext.  Synchronization
  /// scope names are ordered by increasing synchronization scope IDs.
  void getSyncScopeNames(SmallVectorImpl<StringRef> &SSNs) const;

  /// Maintain the GC name for each function.
  ///
  /// This saves allocating an additional word in Function for programs which
  /// do not use GC (i.e., most programs) at the cost of increased overhead for
  /// clients which do use GC.
  DenseMap<const Function*, std::string> GCNames;

  /// Flag to indicate if Value (other than GlobalValue) retains their name or
  /// not.
  bool DiscardValueNames = false;

  LLVMContextImpl(LLVMContext &C);
  ~LLVMContextImpl();

  /// Destroy the ConstantArrays if they are not used.
  void dropTriviallyDeadConstantArrays();

  mutable OptPassGate *OPG = nullptr;

  /// Access the object which can disable optional passes and individual
  /// optimizations at compile time.
  OptPassGate &getOptPassGate() const;

  /// Set the object which can disable optional passes and individual
  /// optimizations at compile time.
  ///
  /// The lifetime of the object must be guaranteed to extend as long as the
  /// LLVMContext is used by compilation.
  void setOptPassGate(OptPassGate&);
};

} // end namespace llvm

#endif // LLVM_LIB_IR_LLVMCONTEXTIMPL_H
