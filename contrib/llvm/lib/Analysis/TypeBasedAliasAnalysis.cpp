//===- TypeBasedAliasAnalysis.cpp - Type-Based Alias Analysis -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the TypeBasedAliasAnalysis pass, which implements
// metadata-based TBAA.
//
// In LLVM IR, memory does not have types, so LLVM's own type system is not
// suitable for doing TBAA. Instead, metadata is added to the IR to describe
// a type system of a higher level language. This can be used to implement
// typical C/C++ TBAA, but it can also be used to implement custom alias
// analysis behavior for other languages.
//
// We now support two types of metadata format: scalar TBAA and struct-path
// aware TBAA. After all testing cases are upgraded to use struct-path aware
// TBAA and we can auto-upgrade existing bc files, the support for scalar TBAA
// can be dropped.
//
// The scalar TBAA metadata format is very simple. TBAA MDNodes have up to
// three fields, e.g.:
//   !0 = !{ !"an example type tree" }
//   !1 = !{ !"int", !0 }
//   !2 = !{ !"float", !0 }
//   !3 = !{ !"const float", !2, i64 1 }
//
// The first field is an identity field. It can be any value, usually
// an MDString, which uniquely identifies the type. The most important
// name in the tree is the name of the root node. Two trees with
// different root node names are entirely disjoint, even if they
// have leaves with common names.
//
// The second field identifies the type's parent node in the tree, or
// is null or omitted for a root node. A type is considered to alias
// all of its descendants and all of its ancestors in the tree. Also,
// a type is considered to alias all types in other trees, so that
// bitcode produced from multiple front-ends is handled conservatively.
//
// If the third field is present, it's an integer which if equal to 1
// indicates that the type is "constant" (meaning pointsToConstantMemory
// should return true; see
// http://llvm.org/docs/AliasAnalysis.html#OtherItfs).
//
// With struct-path aware TBAA, the MDNodes attached to an instruction using
// "!tbaa" are called path tag nodes.
//
// The path tag node has 4 fields with the last field being optional.
//
// The first field is the base type node, it can be a struct type node
// or a scalar type node. The second field is the access type node, it
// must be a scalar type node. The third field is the offset into the base type.
// The last field has the same meaning as the last field of our scalar TBAA:
// it's an integer which if equal to 1 indicates that the access is "constant".
//
// The struct type node has a name and a list of pairs, one pair for each member
// of the struct. The first element of each pair is a type node (a struct type
// node or a scalar type node), specifying the type of the member, the second
// element of each pair is the offset of the member.
//
// Given an example
// typedef struct {
//   short s;
// } A;
// typedef struct {
//   uint16_t s;
//   A a;
// } B;
//
// For an access to B.a.s, we attach !5 (a path tag node) to the load/store
// instruction. The base type is !4 (struct B), the access type is !2 (scalar
// type short) and the offset is 4.
//
// !0 = !{!"Simple C/C++ TBAA"}
// !1 = !{!"omnipotent char", !0} // Scalar type node
// !2 = !{!"short", !1}           // Scalar type node
// !3 = !{!"A", !2, i64 0}        // Struct type node
// !4 = !{!"B", !2, i64 0, !3, i64 4}
//                                                           // Struct type node
// !5 = !{!4, !2, i64 4}          // Path tag node
//
// The struct type nodes and the scalar type nodes form a type DAG.
//         Root (!0)
//         char (!1)  -- edge to Root
//         short (!2) -- edge to char
//         A (!3) -- edge with offset 0 to short
//         B (!4) -- edge with offset 0 to short and edge with offset 4 to A
//
// To check if two tags (tagX and tagY) can alias, we start from the base type
// of tagX, follow the edge with the correct offset in the type DAG and adjust
// the offset until we reach the base type of tagY or until we reach the Root
// node.
// If we reach the base type of tagY, compare the adjusted offset with
// offset of tagY, return Alias if the offsets are the same, return NoAlias
// otherwise.
// If we reach the Root node, perform the above starting from base type of tagY
// to see if we reach base type of tagX.
//
// If they have different roots, they're part of different potentially
// unrelated type systems, so we return Alias to be conservative.
// If neither node is an ancestor of the other and they have the same root,
// then we say NoAlias.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

// A handy option for disabling TBAA functionality. The same effect can also be
// achieved by stripping the !tbaa tags from IR, but this option is sometimes
// more convenient.
static cl::opt<bool> EnableTBAA("enable-tbaa", cl::init(true), cl::Hidden);

namespace {

/// isNewFormatTypeNode - Return true iff the given type node is in the new
/// size-aware format.
static bool isNewFormatTypeNode(const MDNode *N) {
  if (N->getNumOperands() < 3)
    return false;
  // In the old format the first operand is a string.
  if (!isa<MDNode>(N->getOperand(0)))
    return false;
  return true;
}

/// This is a simple wrapper around an MDNode which provides a higher-level
/// interface by hiding the details of how alias analysis information is encoded
/// in its operands.
template<typename MDNodeTy>
class TBAANodeImpl {
  MDNodeTy *Node = nullptr;

public:
  TBAANodeImpl() = default;
  explicit TBAANodeImpl(MDNodeTy *N) : Node(N) {}

  /// getNode - Get the MDNode for this TBAANode.
  MDNodeTy *getNode() const { return Node; }

  /// isNewFormat - Return true iff the wrapped type node is in the new
  /// size-aware format.
  bool isNewFormat() const { return isNewFormatTypeNode(Node); }

  /// getParent - Get this TBAANode's Alias tree parent.
  TBAANodeImpl<MDNodeTy> getParent() const {
    if (isNewFormat())
      return TBAANodeImpl(cast<MDNodeTy>(Node->getOperand(0)));

    if (Node->getNumOperands() < 2)
      return TBAANodeImpl<MDNodeTy>();
    MDNodeTy *P = dyn_cast_or_null<MDNodeTy>(Node->getOperand(1));
    if (!P)
      return TBAANodeImpl<MDNodeTy>();
    // Ok, this node has a valid parent. Return it.
    return TBAANodeImpl<MDNodeTy>(P);
  }

  /// Test if this TBAANode represents a type for objects which are
  /// not modified (by any means) in the context where this
  /// AliasAnalysis is relevant.
  bool isTypeImmutable() const {
    if (Node->getNumOperands() < 3)
      return false;
    ConstantInt *CI = mdconst::dyn_extract<ConstantInt>(Node->getOperand(2));
    if (!CI)
      return false;
    return CI->getValue()[0];
  }
};

/// \name Specializations of \c TBAANodeImpl for const and non const qualified
/// \c MDNode.
/// @{
using TBAANode = TBAANodeImpl<const MDNode>;
using MutableTBAANode = TBAANodeImpl<MDNode>;
/// @}

/// This is a simple wrapper around an MDNode which provides a
/// higher-level interface by hiding the details of how alias analysis
/// information is encoded in its operands.
template<typename MDNodeTy>
class TBAAStructTagNodeImpl {
  /// This node should be created with createTBAAAccessTag().
  MDNodeTy *Node;

public:
  explicit TBAAStructTagNodeImpl(MDNodeTy *N) : Node(N) {}

  /// Get the MDNode for this TBAAStructTagNode.
  MDNodeTy *getNode() const { return Node; }

  /// isNewFormat - Return true iff the wrapped access tag is in the new
  /// size-aware format.
  bool isNewFormat() const {
    if (Node->getNumOperands() < 4)
      return false;
    if (MDNodeTy *AccessType = getAccessType())
      if (!TBAANodeImpl<MDNodeTy>(AccessType).isNewFormat())
        return false;
    return true;
  }

  MDNodeTy *getBaseType() const {
    return dyn_cast_or_null<MDNode>(Node->getOperand(0));
  }

  MDNodeTy *getAccessType() const {
    return dyn_cast_or_null<MDNode>(Node->getOperand(1));
  }

  uint64_t getOffset() const {
    return mdconst::extract<ConstantInt>(Node->getOperand(2))->getZExtValue();
  }

  uint64_t getSize() const {
    if (!isNewFormat())
      return UINT64_MAX;
    return mdconst::extract<ConstantInt>(Node->getOperand(3))->getZExtValue();
  }

  /// Test if this TBAAStructTagNode represents a type for objects
  /// which are not modified (by any means) in the context where this
  /// AliasAnalysis is relevant.
  bool isTypeImmutable() const {
    unsigned OpNo = isNewFormat() ? 4 : 3;
    if (Node->getNumOperands() < OpNo + 1)
      return false;
    ConstantInt *CI = mdconst::dyn_extract<ConstantInt>(Node->getOperand(OpNo));
    if (!CI)
      return false;
    return CI->getValue()[0];
  }
};

/// \name Specializations of \c TBAAStructTagNodeImpl for const and non const
/// qualified \c MDNods.
/// @{
using TBAAStructTagNode = TBAAStructTagNodeImpl<const MDNode>;
using MutableTBAAStructTagNode = TBAAStructTagNodeImpl<MDNode>;
/// @}

/// This is a simple wrapper around an MDNode which provides a
/// higher-level interface by hiding the details of how alias analysis
/// information is encoded in its operands.
class TBAAStructTypeNode {
  /// This node should be created with createTBAATypeNode().
  const MDNode *Node = nullptr;

public:
  TBAAStructTypeNode() = default;
  explicit TBAAStructTypeNode(const MDNode *N) : Node(N) {}

  /// Get the MDNode for this TBAAStructTypeNode.
  const MDNode *getNode() const { return Node; }

  /// isNewFormat - Return true iff the wrapped type node is in the new
  /// size-aware format.
  bool isNewFormat() const { return isNewFormatTypeNode(Node); }

  bool operator==(const TBAAStructTypeNode &Other) const {
    return getNode() == Other.getNode();
  }

  /// getId - Return type identifier.
  Metadata *getId() const {
    return Node->getOperand(isNewFormat() ? 2 : 0);
  }

  unsigned getNumFields() const {
    unsigned FirstFieldOpNo = isNewFormat() ? 3 : 1;
    unsigned NumOpsPerField = isNewFormat() ? 3 : 2;
    return (getNode()->getNumOperands() - FirstFieldOpNo) / NumOpsPerField;
  }

  TBAAStructTypeNode getFieldType(unsigned FieldIndex) const {
    unsigned FirstFieldOpNo = isNewFormat() ? 3 : 1;
    unsigned NumOpsPerField = isNewFormat() ? 3 : 2;
    unsigned OpIndex = FirstFieldOpNo + FieldIndex * NumOpsPerField;
    auto *TypeNode = cast<MDNode>(getNode()->getOperand(OpIndex));
    return TBAAStructTypeNode(TypeNode);
  }

  /// Get this TBAAStructTypeNode's field in the type DAG with
  /// given offset. Update the offset to be relative to the field type.
  TBAAStructTypeNode getField(uint64_t &Offset) const {
    bool NewFormat = isNewFormat();
    if (NewFormat) {
      // New-format root and scalar type nodes have no fields.
      if (Node->getNumOperands() < 6)
        return TBAAStructTypeNode();
    } else {
      // Parent can be omitted for the root node.
      if (Node->getNumOperands() < 2)
        return TBAAStructTypeNode();

      // Fast path for a scalar type node and a struct type node with a single
      // field.
      if (Node->getNumOperands() <= 3) {
        uint64_t Cur = Node->getNumOperands() == 2
                           ? 0
                           : mdconst::extract<ConstantInt>(Node->getOperand(2))
                                 ->getZExtValue();
        Offset -= Cur;
        MDNode *P = dyn_cast_or_null<MDNode>(Node->getOperand(1));
        if (!P)
          return TBAAStructTypeNode();
        return TBAAStructTypeNode(P);
      }
    }

    // Assume the offsets are in order. We return the previous field if
    // the current offset is bigger than the given offset.
    unsigned FirstFieldOpNo = NewFormat ? 3 : 1;
    unsigned NumOpsPerField = NewFormat ? 3 : 2;
    unsigned TheIdx = 0;
    for (unsigned Idx = FirstFieldOpNo; Idx < Node->getNumOperands();
         Idx += NumOpsPerField) {
      uint64_t Cur = mdconst::extract<ConstantInt>(Node->getOperand(Idx + 1))
                         ->getZExtValue();
      if (Cur > Offset) {
        assert(Idx >= FirstFieldOpNo + NumOpsPerField &&
               "TBAAStructTypeNode::getField should have an offset match!");
        TheIdx = Idx - NumOpsPerField;
        break;
      }
    }
    // Move along the last field.
    if (TheIdx == 0)
      TheIdx = Node->getNumOperands() - NumOpsPerField;
    uint64_t Cur = mdconst::extract<ConstantInt>(Node->getOperand(TheIdx + 1))
                       ->getZExtValue();
    Offset -= Cur;
    MDNode *P = dyn_cast_or_null<MDNode>(Node->getOperand(TheIdx));
    if (!P)
      return TBAAStructTypeNode();
    return TBAAStructTypeNode(P);
  }
};

} // end anonymous namespace

/// Check the first operand of the tbaa tag node, if it is a MDNode, we treat
/// it as struct-path aware TBAA format, otherwise, we treat it as scalar TBAA
/// format.
static bool isStructPathTBAA(const MDNode *MD) {
  // Anonymous TBAA root starts with a MDNode and dragonegg uses it as
  // a TBAA tag.
  return isa<MDNode>(MD->getOperand(0)) && MD->getNumOperands() >= 3;
}

AliasResult TypeBasedAAResult::alias(const MemoryLocation &LocA,
                                     const MemoryLocation &LocB) {
  if (!EnableTBAA)
    return AAResultBase::alias(LocA, LocB);

  // If accesses may alias, chain to the next AliasAnalysis.
  if (Aliases(LocA.AATags.TBAA, LocB.AATags.TBAA))
    return AAResultBase::alias(LocA, LocB);

  // Otherwise return a definitive result.
  return NoAlias;
}

bool TypeBasedAAResult::pointsToConstantMemory(const MemoryLocation &Loc,
                                               bool OrLocal) {
  if (!EnableTBAA)
    return AAResultBase::pointsToConstantMemory(Loc, OrLocal);

  const MDNode *M = Loc.AATags.TBAA;
  if (!M)
    return AAResultBase::pointsToConstantMemory(Loc, OrLocal);

  // If this is an "immutable" type, we can assume the pointer is pointing
  // to constant memory.
  if ((!isStructPathTBAA(M) && TBAANode(M).isTypeImmutable()) ||
      (isStructPathTBAA(M) && TBAAStructTagNode(M).isTypeImmutable()))
    return true;

  return AAResultBase::pointsToConstantMemory(Loc, OrLocal);
}

FunctionModRefBehavior
TypeBasedAAResult::getModRefBehavior(const CallBase *Call) {
  if (!EnableTBAA)
    return AAResultBase::getModRefBehavior(Call);

  FunctionModRefBehavior Min = FMRB_UnknownModRefBehavior;

  // If this is an "immutable" type, we can assume the call doesn't write
  // to memory.
  if (const MDNode *M = Call->getMetadata(LLVMContext::MD_tbaa))
    if ((!isStructPathTBAA(M) && TBAANode(M).isTypeImmutable()) ||
        (isStructPathTBAA(M) && TBAAStructTagNode(M).isTypeImmutable()))
      Min = FMRB_OnlyReadsMemory;

  return FunctionModRefBehavior(AAResultBase::getModRefBehavior(Call) & Min);
}

FunctionModRefBehavior TypeBasedAAResult::getModRefBehavior(const Function *F) {
  // Functions don't have metadata. Just chain to the next implementation.
  return AAResultBase::getModRefBehavior(F);
}

ModRefInfo TypeBasedAAResult::getModRefInfo(const CallBase *Call,
                                            const MemoryLocation &Loc) {
  if (!EnableTBAA)
    return AAResultBase::getModRefInfo(Call, Loc);

  if (const MDNode *L = Loc.AATags.TBAA)
    if (const MDNode *M = Call->getMetadata(LLVMContext::MD_tbaa))
      if (!Aliases(L, M))
        return ModRefInfo::NoModRef;

  return AAResultBase::getModRefInfo(Call, Loc);
}

ModRefInfo TypeBasedAAResult::getModRefInfo(const CallBase *Call1,
                                            const CallBase *Call2) {
  if (!EnableTBAA)
    return AAResultBase::getModRefInfo(Call1, Call2);

  if (const MDNode *M1 = Call1->getMetadata(LLVMContext::MD_tbaa))
    if (const MDNode *M2 = Call2->getMetadata(LLVMContext::MD_tbaa))
      if (!Aliases(M1, M2))
        return ModRefInfo::NoModRef;

  return AAResultBase::getModRefInfo(Call1, Call2);
}

bool MDNode::isTBAAVtableAccess() const {
  if (!isStructPathTBAA(this)) {
    if (getNumOperands() < 1)
      return false;
    if (MDString *Tag1 = dyn_cast<MDString>(getOperand(0))) {
      if (Tag1->getString() == "vtable pointer")
        return true;
    }
    return false;
  }

  // For struct-path aware TBAA, we use the access type of the tag.
  TBAAStructTagNode Tag(this);
  TBAAStructTypeNode AccessType(Tag.getAccessType());
  if(auto *Id = dyn_cast<MDString>(AccessType.getId()))
    if (Id->getString() == "vtable pointer")
      return true;
  return false;
}

static bool matchAccessTags(const MDNode *A, const MDNode *B,
                            const MDNode **GenericTag = nullptr);

MDNode *MDNode::getMostGenericTBAA(MDNode *A, MDNode *B) {
  const MDNode *GenericTag;
  matchAccessTags(A, B, &GenericTag);
  return const_cast<MDNode*>(GenericTag);
}

static const MDNode *getLeastCommonType(const MDNode *A, const MDNode *B) {
  if (!A || !B)
    return nullptr;

  if (A == B)
    return A;

  SmallSetVector<const MDNode *, 4> PathA;
  TBAANode TA(A);
  while (TA.getNode()) {
    if (PathA.count(TA.getNode()))
      report_fatal_error("Cycle found in TBAA metadata.");
    PathA.insert(TA.getNode());
    TA = TA.getParent();
  }

  SmallSetVector<const MDNode *, 4> PathB;
  TBAANode TB(B);
  while (TB.getNode()) {
    if (PathB.count(TB.getNode()))
      report_fatal_error("Cycle found in TBAA metadata.");
    PathB.insert(TB.getNode());
    TB = TB.getParent();
  }

  int IA = PathA.size() - 1;
  int IB = PathB.size() - 1;

  const MDNode *Ret = nullptr;
  while (IA >= 0 && IB >= 0) {
    if (PathA[IA] == PathB[IB])
      Ret = PathA[IA];
    else
      break;
    --IA;
    --IB;
  }

  return Ret;
}

void Instruction::getAAMetadata(AAMDNodes &N, bool Merge) const {
  if (Merge)
    N.TBAA =
        MDNode::getMostGenericTBAA(N.TBAA, getMetadata(LLVMContext::MD_tbaa));
  else
    N.TBAA = getMetadata(LLVMContext::MD_tbaa);

  if (Merge)
    N.Scope = MDNode::getMostGenericAliasScope(
        N.Scope, getMetadata(LLVMContext::MD_alias_scope));
  else
    N.Scope = getMetadata(LLVMContext::MD_alias_scope);

  if (Merge)
    N.NoAlias =
        MDNode::intersect(N.NoAlias, getMetadata(LLVMContext::MD_noalias));
  else
    N.NoAlias = getMetadata(LLVMContext::MD_noalias);
}

static const MDNode *createAccessTag(const MDNode *AccessType) {
  // If there is no access type or the access type is the root node, then
  // we don't have any useful access tag to return.
  if (!AccessType || AccessType->getNumOperands() < 2)
    return nullptr;

  Type *Int64 = IntegerType::get(AccessType->getContext(), 64);
  auto *OffsetNode = ConstantAsMetadata::get(ConstantInt::get(Int64, 0));

  if (TBAAStructTypeNode(AccessType).isNewFormat()) {
    // TODO: Take access ranges into account when matching access tags and
    // fix this code to generate actual access sizes for generic tags.
    uint64_t AccessSize = UINT64_MAX;
    auto *SizeNode =
        ConstantAsMetadata::get(ConstantInt::get(Int64, AccessSize));
    Metadata *Ops[] = {const_cast<MDNode*>(AccessType),
                       const_cast<MDNode*>(AccessType),
                       OffsetNode, SizeNode};
    return MDNode::get(AccessType->getContext(), Ops);
  }

  Metadata *Ops[] = {const_cast<MDNode*>(AccessType),
                     const_cast<MDNode*>(AccessType),
                     OffsetNode};
  return MDNode::get(AccessType->getContext(), Ops);
}

static bool hasField(TBAAStructTypeNode BaseType,
                     TBAAStructTypeNode FieldType) {
  for (unsigned I = 0, E = BaseType.getNumFields(); I != E; ++I) {
    TBAAStructTypeNode T = BaseType.getFieldType(I);
    if (T == FieldType || hasField(T, FieldType))
      return true;
  }
  return false;
}

/// Return true if for two given accesses, one of the accessed objects may be a
/// subobject of the other. The \p BaseTag and \p SubobjectTag parameters
/// describe the accesses to the base object and the subobject respectively.
/// \p CommonType must be the metadata node describing the common type of the
/// accessed objects. On return, \p MayAlias is set to true iff these accesses
/// may alias and \p Generic, if not null, points to the most generic access
/// tag for the given two.
static bool mayBeAccessToSubobjectOf(TBAAStructTagNode BaseTag,
                                     TBAAStructTagNode SubobjectTag,
                                     const MDNode *CommonType,
                                     const MDNode **GenericTag,
                                     bool &MayAlias) {
  // If the base object is of the least common type, then this may be an access
  // to its subobject.
  if (BaseTag.getAccessType() == BaseTag.getBaseType() &&
      BaseTag.getAccessType() == CommonType) {
    if (GenericTag)
      *GenericTag = createAccessTag(CommonType);
    MayAlias = true;
    return true;
  }

  // If the access to the base object is through a field of the subobject's
  // type, then this may be an access to that field. To check for that we start
  // from the base type, follow the edge with the correct offset in the type DAG
  // and adjust the offset until we reach the field type or until we reach the
  // access type.
  bool NewFormat = BaseTag.isNewFormat();
  TBAAStructTypeNode BaseType(BaseTag.getBaseType());
  uint64_t OffsetInBase = BaseTag.getOffset();

  for (;;) {
    // In the old format there is no distinction between fields and parent
    // types, so in this case we consider all nodes up to the root.
    if (!BaseType.getNode()) {
      assert(!NewFormat && "Did not see access type in access path!");
      break;
    }

    if (BaseType.getNode() == SubobjectTag.getBaseType()) {
      bool SameMemberAccess = OffsetInBase == SubobjectTag.getOffset();
      if (GenericTag) {
        *GenericTag = SameMemberAccess ? SubobjectTag.getNode() :
                                         createAccessTag(CommonType);
      }
      MayAlias = SameMemberAccess;
      return true;
    }

    // With new-format nodes we stop at the access type.
    if (NewFormat && BaseType.getNode() == BaseTag.getAccessType())
      break;

    // Follow the edge with the correct offset. Offset will be adjusted to
    // be relative to the field type.
    BaseType = BaseType.getField(OffsetInBase);
  }

  // If the base object has a direct or indirect field of the subobject's type,
  // then this may be an access to that field. We need this to check now that
  // we support aggregates as access types.
  if (NewFormat) {
    // TBAAStructTypeNode BaseAccessType(BaseTag.getAccessType());
    TBAAStructTypeNode FieldType(SubobjectTag.getBaseType());
    if (hasField(BaseType, FieldType)) {
      if (GenericTag)
        *GenericTag = createAccessTag(CommonType);
      MayAlias = true;
      return true;
    }
  }

  return false;
}

/// matchTags - Return true if the given couple of accesses are allowed to
/// overlap. If \arg GenericTag is not null, then on return it points to the
/// most generic access descriptor for the given two.
static bool matchAccessTags(const MDNode *A, const MDNode *B,
                            const MDNode **GenericTag) {
  if (A == B) {
    if (GenericTag)
      *GenericTag = A;
    return true;
  }

  // Accesses with no TBAA information may alias with any other accesses.
  if (!A || !B) {
    if (GenericTag)
      *GenericTag = nullptr;
    return true;
  }

  // Verify that both input nodes are struct-path aware.  Auto-upgrade should
  // have taken care of this.
  assert(isStructPathTBAA(A) && "Access A is not struct-path aware!");
  assert(isStructPathTBAA(B) && "Access B is not struct-path aware!");

  TBAAStructTagNode TagA(A), TagB(B);
  const MDNode *CommonType = getLeastCommonType(TagA.getAccessType(),
                                                TagB.getAccessType());

  // If the final access types have different roots, they're part of different
  // potentially unrelated type systems, so we must be conservative.
  if (!CommonType) {
    if (GenericTag)
      *GenericTag = nullptr;
    return true;
  }

  // If one of the accessed objects may be a subobject of the other, then such
  // accesses may alias.
  bool MayAlias;
  if (mayBeAccessToSubobjectOf(/* BaseTag= */ TagA, /* SubobjectTag= */ TagB,
                               CommonType, GenericTag, MayAlias) ||
      mayBeAccessToSubobjectOf(/* BaseTag= */ TagB, /* SubobjectTag= */ TagA,
                               CommonType, GenericTag, MayAlias))
    return MayAlias;

  // Otherwise, we've proved there's no alias.
  if (GenericTag)
    *GenericTag = createAccessTag(CommonType);
  return false;
}

/// Aliases - Test whether the access represented by tag A may alias the
/// access represented by tag B.
bool TypeBasedAAResult::Aliases(const MDNode *A, const MDNode *B) const {
  return matchAccessTags(A, B);
}

AnalysisKey TypeBasedAA::Key;

TypeBasedAAResult TypeBasedAA::run(Function &F, FunctionAnalysisManager &AM) {
  return TypeBasedAAResult();
}

char TypeBasedAAWrapperPass::ID = 0;
INITIALIZE_PASS(TypeBasedAAWrapperPass, "tbaa", "Type-Based Alias Analysis",
                false, true)

ImmutablePass *llvm::createTypeBasedAAWrapperPass() {
  return new TypeBasedAAWrapperPass();
}

TypeBasedAAWrapperPass::TypeBasedAAWrapperPass() : ImmutablePass(ID) {
  initializeTypeBasedAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

bool TypeBasedAAWrapperPass::doInitialization(Module &M) {
  Result.reset(new TypeBasedAAResult());
  return false;
}

bool TypeBasedAAWrapperPass::doFinalization(Module &M) {
  Result.reset();
  return false;
}

void TypeBasedAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}
