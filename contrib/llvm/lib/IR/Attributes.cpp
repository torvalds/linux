//===- Attributes.cpp - Implement AttributesList --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// \file
// This file implements the Attribute, AttributeImpl, AttrBuilder,
// AttributeListImpl, and AttributeList classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Attributes.h"
#include "AttributeImpl.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

//===----------------------------------------------------------------------===//
// Attribute Construction Methods
//===----------------------------------------------------------------------===//

// allocsize has two integer arguments, but because they're both 32 bits, we can
// pack them into one 64-bit value, at the cost of making said value
// nonsensical.
//
// In order to do this, we need to reserve one value of the second (optional)
// allocsize argument to signify "not present."
static const unsigned AllocSizeNumElemsNotPresent = -1;

static uint64_t packAllocSizeArgs(unsigned ElemSizeArg,
                                  const Optional<unsigned> &NumElemsArg) {
  assert((!NumElemsArg.hasValue() ||
          *NumElemsArg != AllocSizeNumElemsNotPresent) &&
         "Attempting to pack a reserved value");

  return uint64_t(ElemSizeArg) << 32 |
         NumElemsArg.getValueOr(AllocSizeNumElemsNotPresent);
}

static std::pair<unsigned, Optional<unsigned>>
unpackAllocSizeArgs(uint64_t Num) {
  unsigned NumElems = Num & std::numeric_limits<unsigned>::max();
  unsigned ElemSizeArg = Num >> 32;

  Optional<unsigned> NumElemsArg;
  if (NumElems != AllocSizeNumElemsNotPresent)
    NumElemsArg = NumElems;
  return std::make_pair(ElemSizeArg, NumElemsArg);
}

Attribute Attribute::get(LLVMContext &Context, Attribute::AttrKind Kind,
                         uint64_t Val) {
  LLVMContextImpl *pImpl = Context.pImpl;
  FoldingSetNodeID ID;
  ID.AddInteger(Kind);
  if (Val) ID.AddInteger(Val);

  void *InsertPoint;
  AttributeImpl *PA = pImpl->AttrsSet.FindNodeOrInsertPos(ID, InsertPoint);

  if (!PA) {
    // If we didn't find any existing attributes of the same shape then create a
    // new one and insert it.
    if (!Val)
      PA = new EnumAttributeImpl(Kind);
    else
      PA = new IntAttributeImpl(Kind, Val);
    pImpl->AttrsSet.InsertNode(PA, InsertPoint);
  }

  // Return the Attribute that we found or created.
  return Attribute(PA);
}

Attribute Attribute::get(LLVMContext &Context, StringRef Kind, StringRef Val) {
  LLVMContextImpl *pImpl = Context.pImpl;
  FoldingSetNodeID ID;
  ID.AddString(Kind);
  if (!Val.empty()) ID.AddString(Val);

  void *InsertPoint;
  AttributeImpl *PA = pImpl->AttrsSet.FindNodeOrInsertPos(ID, InsertPoint);

  if (!PA) {
    // If we didn't find any existing attributes of the same shape then create a
    // new one and insert it.
    PA = new StringAttributeImpl(Kind, Val);
    pImpl->AttrsSet.InsertNode(PA, InsertPoint);
  }

  // Return the Attribute that we found or created.
  return Attribute(PA);
}

Attribute Attribute::getWithAlignment(LLVMContext &Context, uint64_t Align) {
  assert(isPowerOf2_32(Align) && "Alignment must be a power of two.");
  assert(Align <= 0x40000000 && "Alignment too large.");
  return get(Context, Alignment, Align);
}

Attribute Attribute::getWithStackAlignment(LLVMContext &Context,
                                           uint64_t Align) {
  assert(isPowerOf2_32(Align) && "Alignment must be a power of two.");
  assert(Align <= 0x100 && "Alignment too large.");
  return get(Context, StackAlignment, Align);
}

Attribute Attribute::getWithDereferenceableBytes(LLVMContext &Context,
                                                uint64_t Bytes) {
  assert(Bytes && "Bytes must be non-zero.");
  return get(Context, Dereferenceable, Bytes);
}

Attribute Attribute::getWithDereferenceableOrNullBytes(LLVMContext &Context,
                                                       uint64_t Bytes) {
  assert(Bytes && "Bytes must be non-zero.");
  return get(Context, DereferenceableOrNull, Bytes);
}

Attribute
Attribute::getWithAllocSizeArgs(LLVMContext &Context, unsigned ElemSizeArg,
                                const Optional<unsigned> &NumElemsArg) {
  assert(!(ElemSizeArg == 0 && NumElemsArg && *NumElemsArg == 0) &&
         "Invalid allocsize arguments -- given allocsize(0, 0)");
  return get(Context, AllocSize, packAllocSizeArgs(ElemSizeArg, NumElemsArg));
}

//===----------------------------------------------------------------------===//
// Attribute Accessor Methods
//===----------------------------------------------------------------------===//

bool Attribute::isEnumAttribute() const {
  return pImpl && pImpl->isEnumAttribute();
}

bool Attribute::isIntAttribute() const {
  return pImpl && pImpl->isIntAttribute();
}

bool Attribute::isStringAttribute() const {
  return pImpl && pImpl->isStringAttribute();
}

Attribute::AttrKind Attribute::getKindAsEnum() const {
  if (!pImpl) return None;
  assert((isEnumAttribute() || isIntAttribute()) &&
         "Invalid attribute type to get the kind as an enum!");
  return pImpl->getKindAsEnum();
}

uint64_t Attribute::getValueAsInt() const {
  if (!pImpl) return 0;
  assert(isIntAttribute() &&
         "Expected the attribute to be an integer attribute!");
  return pImpl->getValueAsInt();
}

StringRef Attribute::getKindAsString() const {
  if (!pImpl) return {};
  assert(isStringAttribute() &&
         "Invalid attribute type to get the kind as a string!");
  return pImpl->getKindAsString();
}

StringRef Attribute::getValueAsString() const {
  if (!pImpl) return {};
  assert(isStringAttribute() &&
         "Invalid attribute type to get the value as a string!");
  return pImpl->getValueAsString();
}

bool Attribute::hasAttribute(AttrKind Kind) const {
  return (pImpl && pImpl->hasAttribute(Kind)) || (!pImpl && Kind == None);
}

bool Attribute::hasAttribute(StringRef Kind) const {
  if (!isStringAttribute()) return false;
  return pImpl && pImpl->hasAttribute(Kind);
}

unsigned Attribute::getAlignment() const {
  assert(hasAttribute(Attribute::Alignment) &&
         "Trying to get alignment from non-alignment attribute!");
  return pImpl->getValueAsInt();
}

unsigned Attribute::getStackAlignment() const {
  assert(hasAttribute(Attribute::StackAlignment) &&
         "Trying to get alignment from non-alignment attribute!");
  return pImpl->getValueAsInt();
}

uint64_t Attribute::getDereferenceableBytes() const {
  assert(hasAttribute(Attribute::Dereferenceable) &&
         "Trying to get dereferenceable bytes from "
         "non-dereferenceable attribute!");
  return pImpl->getValueAsInt();
}

uint64_t Attribute::getDereferenceableOrNullBytes() const {
  assert(hasAttribute(Attribute::DereferenceableOrNull) &&
         "Trying to get dereferenceable bytes from "
         "non-dereferenceable attribute!");
  return pImpl->getValueAsInt();
}

std::pair<unsigned, Optional<unsigned>> Attribute::getAllocSizeArgs() const {
  assert(hasAttribute(Attribute::AllocSize) &&
         "Trying to get allocsize args from non-allocsize attribute");
  return unpackAllocSizeArgs(pImpl->getValueAsInt());
}

std::string Attribute::getAsString(bool InAttrGrp) const {
  if (!pImpl) return {};

  if (hasAttribute(Attribute::SanitizeAddress))
    return "sanitize_address";
  if (hasAttribute(Attribute::SanitizeHWAddress))
    return "sanitize_hwaddress";
  if (hasAttribute(Attribute::AlwaysInline))
    return "alwaysinline";
  if (hasAttribute(Attribute::ArgMemOnly))
    return "argmemonly";
  if (hasAttribute(Attribute::Builtin))
    return "builtin";
  if (hasAttribute(Attribute::ByVal))
    return "byval";
  if (hasAttribute(Attribute::Convergent))
    return "convergent";
  if (hasAttribute(Attribute::SwiftError))
    return "swifterror";
  if (hasAttribute(Attribute::SwiftSelf))
    return "swiftself";
  if (hasAttribute(Attribute::InaccessibleMemOnly))
    return "inaccessiblememonly";
  if (hasAttribute(Attribute::InaccessibleMemOrArgMemOnly))
    return "inaccessiblemem_or_argmemonly";
  if (hasAttribute(Attribute::InAlloca))
    return "inalloca";
  if (hasAttribute(Attribute::InlineHint))
    return "inlinehint";
  if (hasAttribute(Attribute::InReg))
    return "inreg";
  if (hasAttribute(Attribute::JumpTable))
    return "jumptable";
  if (hasAttribute(Attribute::MinSize))
    return "minsize";
  if (hasAttribute(Attribute::Naked))
    return "naked";
  if (hasAttribute(Attribute::Nest))
    return "nest";
  if (hasAttribute(Attribute::NoAlias))
    return "noalias";
  if (hasAttribute(Attribute::NoBuiltin))
    return "nobuiltin";
  if (hasAttribute(Attribute::NoCapture))
    return "nocapture";
  if (hasAttribute(Attribute::NoDuplicate))
    return "noduplicate";
  if (hasAttribute(Attribute::NoImplicitFloat))
    return "noimplicitfloat";
  if (hasAttribute(Attribute::NoInline))
    return "noinline";
  if (hasAttribute(Attribute::NonLazyBind))
    return "nonlazybind";
  if (hasAttribute(Attribute::NonNull))
    return "nonnull";
  if (hasAttribute(Attribute::NoRedZone))
    return "noredzone";
  if (hasAttribute(Attribute::NoReturn))
    return "noreturn";
  if (hasAttribute(Attribute::NoCfCheck))
    return "nocf_check";
  if (hasAttribute(Attribute::NoRecurse))
    return "norecurse";
  if (hasAttribute(Attribute::NoUnwind))
    return "nounwind";
  if (hasAttribute(Attribute::OptForFuzzing))
    return "optforfuzzing";
  if (hasAttribute(Attribute::OptimizeNone))
    return "optnone";
  if (hasAttribute(Attribute::OptimizeForSize))
    return "optsize";
  if (hasAttribute(Attribute::ReadNone))
    return "readnone";
  if (hasAttribute(Attribute::ReadOnly))
    return "readonly";
  if (hasAttribute(Attribute::WriteOnly))
    return "writeonly";
  if (hasAttribute(Attribute::Returned))
    return "returned";
  if (hasAttribute(Attribute::ReturnsTwice))
    return "returns_twice";
  if (hasAttribute(Attribute::SExt))
    return "signext";
  if (hasAttribute(Attribute::SpeculativeLoadHardening))
    return "speculative_load_hardening";
  if (hasAttribute(Attribute::Speculatable))
    return "speculatable";
  if (hasAttribute(Attribute::StackProtect))
    return "ssp";
  if (hasAttribute(Attribute::StackProtectReq))
    return "sspreq";
  if (hasAttribute(Attribute::StackProtectStrong))
    return "sspstrong";
  if (hasAttribute(Attribute::SafeStack))
    return "safestack";
  if (hasAttribute(Attribute::ShadowCallStack))
    return "shadowcallstack";
  if (hasAttribute(Attribute::StrictFP))
    return "strictfp";
  if (hasAttribute(Attribute::StructRet))
    return "sret";
  if (hasAttribute(Attribute::SanitizeThread))
    return "sanitize_thread";
  if (hasAttribute(Attribute::SanitizeMemory))
    return "sanitize_memory";
  if (hasAttribute(Attribute::UWTable))
    return "uwtable";
  if (hasAttribute(Attribute::ZExt))
    return "zeroext";
  if (hasAttribute(Attribute::Cold))
    return "cold";

  // FIXME: These should be output like this:
  //
  //   align=4
  //   alignstack=8
  //
  if (hasAttribute(Attribute::Alignment)) {
    std::string Result;
    Result += "align";
    Result += (InAttrGrp) ? "=" : " ";
    Result += utostr(getValueAsInt());
    return Result;
  }

  auto AttrWithBytesToString = [&](const char *Name) {
    std::string Result;
    Result += Name;
    if (InAttrGrp) {
      Result += "=";
      Result += utostr(getValueAsInt());
    } else {
      Result += "(";
      Result += utostr(getValueAsInt());
      Result += ")";
    }
    return Result;
  };

  if (hasAttribute(Attribute::StackAlignment))
    return AttrWithBytesToString("alignstack");

  if (hasAttribute(Attribute::Dereferenceable))
    return AttrWithBytesToString("dereferenceable");

  if (hasAttribute(Attribute::DereferenceableOrNull))
    return AttrWithBytesToString("dereferenceable_or_null");

  if (hasAttribute(Attribute::AllocSize)) {
    unsigned ElemSize;
    Optional<unsigned> NumElems;
    std::tie(ElemSize, NumElems) = getAllocSizeArgs();

    std::string Result = "allocsize(";
    Result += utostr(ElemSize);
    if (NumElems.hasValue()) {
      Result += ',';
      Result += utostr(*NumElems);
    }
    Result += ')';
    return Result;
  }

  // Convert target-dependent attributes to strings of the form:
  //
  //   "kind"
  //   "kind" = "value"
  //
  if (isStringAttribute()) {
    std::string Result;
    Result += (Twine('"') + getKindAsString() + Twine('"')).str();

    std::string AttrVal = pImpl->getValueAsString();
    if (AttrVal.empty()) return Result;

    // Since some attribute strings contain special characters that cannot be
    // printable, those have to be escaped to make the attribute value printable
    // as is.  e.g. "\01__gnu_mcount_nc"
    {
      raw_string_ostream OS(Result);
      OS << "=\"";
      printEscapedString(AttrVal, OS);
      OS << "\"";
    }
    return Result;
  }

  llvm_unreachable("Unknown attribute");
}

bool Attribute::operator<(Attribute A) const {
  if (!pImpl && !A.pImpl) return false;
  if (!pImpl) return true;
  if (!A.pImpl) return false;
  return *pImpl < *A.pImpl;
}

//===----------------------------------------------------------------------===//
// AttributeImpl Definition
//===----------------------------------------------------------------------===//

// Pin the vtables to this file.
AttributeImpl::~AttributeImpl() = default;

void EnumAttributeImpl::anchor() {}

void IntAttributeImpl::anchor() {}

void StringAttributeImpl::anchor() {}

bool AttributeImpl::hasAttribute(Attribute::AttrKind A) const {
  if (isStringAttribute()) return false;
  return getKindAsEnum() == A;
}

bool AttributeImpl::hasAttribute(StringRef Kind) const {
  if (!isStringAttribute()) return false;
  return getKindAsString() == Kind;
}

Attribute::AttrKind AttributeImpl::getKindAsEnum() const {
  assert(isEnumAttribute() || isIntAttribute());
  return static_cast<const EnumAttributeImpl *>(this)->getEnumKind();
}

uint64_t AttributeImpl::getValueAsInt() const {
  assert(isIntAttribute());
  return static_cast<const IntAttributeImpl *>(this)->getValue();
}

StringRef AttributeImpl::getKindAsString() const {
  assert(isStringAttribute());
  return static_cast<const StringAttributeImpl *>(this)->getStringKind();
}

StringRef AttributeImpl::getValueAsString() const {
  assert(isStringAttribute());
  return static_cast<const StringAttributeImpl *>(this)->getStringValue();
}

bool AttributeImpl::operator<(const AttributeImpl &AI) const {
  // This sorts the attributes with Attribute::AttrKinds coming first (sorted
  // relative to their enum value) and then strings.
  if (isEnumAttribute()) {
    if (AI.isEnumAttribute()) return getKindAsEnum() < AI.getKindAsEnum();
    if (AI.isIntAttribute()) return true;
    if (AI.isStringAttribute()) return true;
  }

  if (isIntAttribute()) {
    if (AI.isEnumAttribute()) return false;
    if (AI.isIntAttribute()) {
      if (getKindAsEnum() == AI.getKindAsEnum())
        return getValueAsInt() < AI.getValueAsInt();
      return getKindAsEnum() < AI.getKindAsEnum();
    }
    if (AI.isStringAttribute()) return true;
  }

  if (AI.isEnumAttribute()) return false;
  if (AI.isIntAttribute()) return false;
  if (getKindAsString() == AI.getKindAsString())
    return getValueAsString() < AI.getValueAsString();
  return getKindAsString() < AI.getKindAsString();
}

//===----------------------------------------------------------------------===//
// AttributeSet Definition
//===----------------------------------------------------------------------===//

AttributeSet AttributeSet::get(LLVMContext &C, const AttrBuilder &B) {
  return AttributeSet(AttributeSetNode::get(C, B));
}

AttributeSet AttributeSet::get(LLVMContext &C, ArrayRef<Attribute> Attrs) {
  return AttributeSet(AttributeSetNode::get(C, Attrs));
}

AttributeSet AttributeSet::addAttribute(LLVMContext &C,
                                        Attribute::AttrKind Kind) const {
  if (hasAttribute(Kind)) return *this;
  AttrBuilder B;
  B.addAttribute(Kind);
  return addAttributes(C, AttributeSet::get(C, B));
}

AttributeSet AttributeSet::addAttribute(LLVMContext &C, StringRef Kind,
                                        StringRef Value) const {
  AttrBuilder B;
  B.addAttribute(Kind, Value);
  return addAttributes(C, AttributeSet::get(C, B));
}

AttributeSet AttributeSet::addAttributes(LLVMContext &C,
                                         const AttributeSet AS) const {
  if (!hasAttributes())
    return AS;

  if (!AS.hasAttributes())
    return *this;

  AttrBuilder B(AS);
  for (const auto I : *this)
    B.addAttribute(I);

 return get(C, B);
}

AttributeSet AttributeSet::removeAttribute(LLVMContext &C,
                                             Attribute::AttrKind Kind) const {
  if (!hasAttribute(Kind)) return *this;
  AttrBuilder B(*this);
  B.removeAttribute(Kind);
  return get(C, B);
}

AttributeSet AttributeSet::removeAttribute(LLVMContext &C,
                                             StringRef Kind) const {
  if (!hasAttribute(Kind)) return *this;
  AttrBuilder B(*this);
  B.removeAttribute(Kind);
  return get(C, B);
}

AttributeSet AttributeSet::removeAttributes(LLVMContext &C,
                                              const AttrBuilder &Attrs) const {
  AttrBuilder B(*this);
  B.remove(Attrs);
  return get(C, B);
}

unsigned AttributeSet::getNumAttributes() const {
  return SetNode ? SetNode->getNumAttributes() : 0;
}

bool AttributeSet::hasAttribute(Attribute::AttrKind Kind) const {
  return SetNode ? SetNode->hasAttribute(Kind) : false;
}

bool AttributeSet::hasAttribute(StringRef Kind) const {
  return SetNode ? SetNode->hasAttribute(Kind) : false;
}

Attribute AttributeSet::getAttribute(Attribute::AttrKind Kind) const {
  return SetNode ? SetNode->getAttribute(Kind) : Attribute();
}

Attribute AttributeSet::getAttribute(StringRef Kind) const {
  return SetNode ? SetNode->getAttribute(Kind) : Attribute();
}

unsigned AttributeSet::getAlignment() const {
  return SetNode ? SetNode->getAlignment() : 0;
}

unsigned AttributeSet::getStackAlignment() const {
  return SetNode ? SetNode->getStackAlignment() : 0;
}

uint64_t AttributeSet::getDereferenceableBytes() const {
  return SetNode ? SetNode->getDereferenceableBytes() : 0;
}

uint64_t AttributeSet::getDereferenceableOrNullBytes() const {
  return SetNode ? SetNode->getDereferenceableOrNullBytes() : 0;
}

std::pair<unsigned, Optional<unsigned>> AttributeSet::getAllocSizeArgs() const {
  return SetNode ? SetNode->getAllocSizeArgs()
                 : std::pair<unsigned, Optional<unsigned>>(0, 0);
}

std::string AttributeSet::getAsString(bool InAttrGrp) const {
  return SetNode ? SetNode->getAsString(InAttrGrp) : "";
}

AttributeSet::iterator AttributeSet::begin() const {
  return SetNode ? SetNode->begin() : nullptr;
}

AttributeSet::iterator AttributeSet::end() const {
  return SetNode ? SetNode->end() : nullptr;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void AttributeSet::dump() const {
  dbgs() << "AS =\n";
    dbgs() << "  { ";
    dbgs() << getAsString(true) << " }\n";
}
#endif

//===----------------------------------------------------------------------===//
// AttributeSetNode Definition
//===----------------------------------------------------------------------===//

AttributeSetNode::AttributeSetNode(ArrayRef<Attribute> Attrs)
    : AvailableAttrs(0), NumAttrs(Attrs.size()) {
  // There's memory after the node where we can store the entries in.
  llvm::copy(Attrs, getTrailingObjects<Attribute>());

  for (const auto I : *this) {
    if (!I.isStringAttribute()) {
      AvailableAttrs |= ((uint64_t)1) << I.getKindAsEnum();
    }
  }
}

AttributeSetNode *AttributeSetNode::get(LLVMContext &C,
                                        ArrayRef<Attribute> Attrs) {
  if (Attrs.empty())
    return nullptr;

  // Otherwise, build a key to look up the existing attributes.
  LLVMContextImpl *pImpl = C.pImpl;
  FoldingSetNodeID ID;

  SmallVector<Attribute, 8> SortedAttrs(Attrs.begin(), Attrs.end());
  llvm::sort(SortedAttrs);

  for (const auto Attr : SortedAttrs)
    Attr.Profile(ID);

  void *InsertPoint;
  AttributeSetNode *PA =
    pImpl->AttrsSetNodes.FindNodeOrInsertPos(ID, InsertPoint);

  // If we didn't find any existing attributes of the same shape then create a
  // new one and insert it.
  if (!PA) {
    // Coallocate entries after the AttributeSetNode itself.
    void *Mem = ::operator new(totalSizeToAlloc<Attribute>(SortedAttrs.size()));
    PA = new (Mem) AttributeSetNode(SortedAttrs);
    pImpl->AttrsSetNodes.InsertNode(PA, InsertPoint);
  }

  // Return the AttributeSetNode that we found or created.
  return PA;
}

AttributeSetNode *AttributeSetNode::get(LLVMContext &C, const AttrBuilder &B) {
  // Add target-independent attributes.
  SmallVector<Attribute, 8> Attrs;
  for (Attribute::AttrKind Kind = Attribute::None;
       Kind != Attribute::EndAttrKinds; Kind = Attribute::AttrKind(Kind + 1)) {
    if (!B.contains(Kind))
      continue;

    Attribute Attr;
    switch (Kind) {
    case Attribute::Alignment:
      Attr = Attribute::getWithAlignment(C, B.getAlignment());
      break;
    case Attribute::StackAlignment:
      Attr = Attribute::getWithStackAlignment(C, B.getStackAlignment());
      break;
    case Attribute::Dereferenceable:
      Attr = Attribute::getWithDereferenceableBytes(
          C, B.getDereferenceableBytes());
      break;
    case Attribute::DereferenceableOrNull:
      Attr = Attribute::getWithDereferenceableOrNullBytes(
          C, B.getDereferenceableOrNullBytes());
      break;
    case Attribute::AllocSize: {
      auto A = B.getAllocSizeArgs();
      Attr = Attribute::getWithAllocSizeArgs(C, A.first, A.second);
      break;
    }
    default:
      Attr = Attribute::get(C, Kind);
    }
    Attrs.push_back(Attr);
  }

  // Add target-dependent (string) attributes.
  for (const auto &TDA : B.td_attrs())
    Attrs.emplace_back(Attribute::get(C, TDA.first, TDA.second));

  return get(C, Attrs);
}

bool AttributeSetNode::hasAttribute(StringRef Kind) const {
  for (const auto I : *this)
    if (I.hasAttribute(Kind))
      return true;
  return false;
}

Attribute AttributeSetNode::getAttribute(Attribute::AttrKind Kind) const {
  if (hasAttribute(Kind)) {
    for (const auto I : *this)
      if (I.hasAttribute(Kind))
        return I;
  }
  return {};
}

Attribute AttributeSetNode::getAttribute(StringRef Kind) const {
  for (const auto I : *this)
    if (I.hasAttribute(Kind))
      return I;
  return {};
}

unsigned AttributeSetNode::getAlignment() const {
  for (const auto I : *this)
    if (I.hasAttribute(Attribute::Alignment))
      return I.getAlignment();
  return 0;
}

unsigned AttributeSetNode::getStackAlignment() const {
  for (const auto I : *this)
    if (I.hasAttribute(Attribute::StackAlignment))
      return I.getStackAlignment();
  return 0;
}

uint64_t AttributeSetNode::getDereferenceableBytes() const {
  for (const auto I : *this)
    if (I.hasAttribute(Attribute::Dereferenceable))
      return I.getDereferenceableBytes();
  return 0;
}

uint64_t AttributeSetNode::getDereferenceableOrNullBytes() const {
  for (const auto I : *this)
    if (I.hasAttribute(Attribute::DereferenceableOrNull))
      return I.getDereferenceableOrNullBytes();
  return 0;
}

std::pair<unsigned, Optional<unsigned>>
AttributeSetNode::getAllocSizeArgs() const {
  for (const auto I : *this)
    if (I.hasAttribute(Attribute::AllocSize))
      return I.getAllocSizeArgs();
  return std::make_pair(0, 0);
}

std::string AttributeSetNode::getAsString(bool InAttrGrp) const {
  std::string Str;
  for (iterator I = begin(), E = end(); I != E; ++I) {
    if (I != begin())
      Str += ' ';
    Str += I->getAsString(InAttrGrp);
  }
  return Str;
}

//===----------------------------------------------------------------------===//
// AttributeListImpl Definition
//===----------------------------------------------------------------------===//

/// Map from AttributeList index to the internal array index. Adding one happens
/// to work, but it relies on unsigned integer wrapping. MSVC warns about
/// unsigned wrapping in constexpr functions, so write out the conditional. LLVM
/// folds it to add anyway.
static constexpr unsigned attrIdxToArrayIdx(unsigned Index) {
  return Index == AttributeList::FunctionIndex ? 0 : Index + 1;
}

AttributeListImpl::AttributeListImpl(LLVMContext &C,
                                     ArrayRef<AttributeSet> Sets)
    : AvailableFunctionAttrs(0), Context(C), NumAttrSets(Sets.size()) {
  assert(!Sets.empty() && "pointless AttributeListImpl");

  // There's memory after the node where we can store the entries in.
  llvm::copy(Sets, getTrailingObjects<AttributeSet>());

  // Initialize AvailableFunctionAttrs summary bitset.
  static_assert(Attribute::EndAttrKinds <=
                    sizeof(AvailableFunctionAttrs) * CHAR_BIT,
                "Too many attributes");
  static_assert(attrIdxToArrayIdx(AttributeList::FunctionIndex) == 0U,
                "function should be stored in slot 0");
  for (const auto I : Sets[0]) {
    if (!I.isStringAttribute())
      AvailableFunctionAttrs |= 1ULL << I.getKindAsEnum();
  }
}

void AttributeListImpl::Profile(FoldingSetNodeID &ID) const {
  Profile(ID, makeArrayRef(begin(), end()));
}

void AttributeListImpl::Profile(FoldingSetNodeID &ID,
                                ArrayRef<AttributeSet> Sets) {
  for (const auto &Set : Sets)
    ID.AddPointer(Set.SetNode);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void AttributeListImpl::dump() const {
  AttributeList(const_cast<AttributeListImpl *>(this)).dump();
}
#endif

//===----------------------------------------------------------------------===//
// AttributeList Construction and Mutation Methods
//===----------------------------------------------------------------------===//

AttributeList AttributeList::getImpl(LLVMContext &C,
                                     ArrayRef<AttributeSet> AttrSets) {
  assert(!AttrSets.empty() && "pointless AttributeListImpl");

  LLVMContextImpl *pImpl = C.pImpl;
  FoldingSetNodeID ID;
  AttributeListImpl::Profile(ID, AttrSets);

  void *InsertPoint;
  AttributeListImpl *PA =
      pImpl->AttrsLists.FindNodeOrInsertPos(ID, InsertPoint);

  // If we didn't find any existing attributes of the same shape then
  // create a new one and insert it.
  if (!PA) {
    // Coallocate entries after the AttributeListImpl itself.
    void *Mem = ::operator new(
        AttributeListImpl::totalSizeToAlloc<AttributeSet>(AttrSets.size()));
    PA = new (Mem) AttributeListImpl(C, AttrSets);
    pImpl->AttrsLists.InsertNode(PA, InsertPoint);
  }

  // Return the AttributesList that we found or created.
  return AttributeList(PA);
}

AttributeList
AttributeList::get(LLVMContext &C,
                   ArrayRef<std::pair<unsigned, Attribute>> Attrs) {
  // If there are no attributes then return a null AttributesList pointer.
  if (Attrs.empty())
    return {};

  assert(std::is_sorted(Attrs.begin(), Attrs.end(),
                        [](const std::pair<unsigned, Attribute> &LHS,
                           const std::pair<unsigned, Attribute> &RHS) {
                          return LHS.first < RHS.first;
                        }) && "Misordered Attributes list!");
  assert(llvm::none_of(Attrs,
                       [](const std::pair<unsigned, Attribute> &Pair) {
                         return Pair.second.hasAttribute(Attribute::None);
                       }) &&
         "Pointless attribute!");

  // Create a vector if (unsigned, AttributeSetNode*) pairs from the attributes
  // list.
  SmallVector<std::pair<unsigned, AttributeSet>, 8> AttrPairVec;
  for (ArrayRef<std::pair<unsigned, Attribute>>::iterator I = Attrs.begin(),
         E = Attrs.end(); I != E; ) {
    unsigned Index = I->first;
    SmallVector<Attribute, 4> AttrVec;
    while (I != E && I->first == Index) {
      AttrVec.push_back(I->second);
      ++I;
    }

    AttrPairVec.emplace_back(Index, AttributeSet::get(C, AttrVec));
  }

  return get(C, AttrPairVec);
}

AttributeList
AttributeList::get(LLVMContext &C,
                   ArrayRef<std::pair<unsigned, AttributeSet>> Attrs) {
  // If there are no attributes then return a null AttributesList pointer.
  if (Attrs.empty())
    return {};

  assert(std::is_sorted(Attrs.begin(), Attrs.end(),
                        [](const std::pair<unsigned, AttributeSet> &LHS,
                           const std::pair<unsigned, AttributeSet> &RHS) {
                          return LHS.first < RHS.first;
                        }) &&
         "Misordered Attributes list!");
  assert(llvm::none_of(Attrs,
                       [](const std::pair<unsigned, AttributeSet> &Pair) {
                         return !Pair.second.hasAttributes();
                       }) &&
         "Pointless attribute!");

  unsigned MaxIndex = Attrs.back().first;
  // If the MaxIndex is FunctionIndex and there are other indices in front
  // of it, we need to use the largest of those to get the right size.
  if (MaxIndex == FunctionIndex && Attrs.size() > 1)
    MaxIndex = Attrs[Attrs.size() - 2].first;

  SmallVector<AttributeSet, 4> AttrVec(attrIdxToArrayIdx(MaxIndex) + 1);
  for (const auto Pair : Attrs)
    AttrVec[attrIdxToArrayIdx(Pair.first)] = Pair.second;

  return getImpl(C, AttrVec);
}

AttributeList AttributeList::get(LLVMContext &C, AttributeSet FnAttrs,
                                 AttributeSet RetAttrs,
                                 ArrayRef<AttributeSet> ArgAttrs) {
  // Scan from the end to find the last argument with attributes.  Most
  // arguments don't have attributes, so it's nice if we can have fewer unique
  // AttributeListImpls by dropping empty attribute sets at the end of the list.
  unsigned NumSets = 0;
  for (size_t I = ArgAttrs.size(); I != 0; --I) {
    if (ArgAttrs[I - 1].hasAttributes()) {
      NumSets = I + 2;
      break;
    }
  }
  if (NumSets == 0) {
    // Check function and return attributes if we didn't have argument
    // attributes.
    if (RetAttrs.hasAttributes())
      NumSets = 2;
    else if (FnAttrs.hasAttributes())
      NumSets = 1;
  }

  // If all attribute sets were empty, we can use the empty attribute list.
  if (NumSets == 0)
    return {};

  SmallVector<AttributeSet, 8> AttrSets;
  AttrSets.reserve(NumSets);
  // If we have any attributes, we always have function attributes.
  AttrSets.push_back(FnAttrs);
  if (NumSets > 1)
    AttrSets.push_back(RetAttrs);
  if (NumSets > 2) {
    // Drop the empty argument attribute sets at the end.
    ArgAttrs = ArgAttrs.take_front(NumSets - 2);
    AttrSets.insert(AttrSets.end(), ArgAttrs.begin(), ArgAttrs.end());
  }

  return getImpl(C, AttrSets);
}

AttributeList AttributeList::get(LLVMContext &C, unsigned Index,
                                 const AttrBuilder &B) {
  if (!B.hasAttributes())
    return {};
  Index = attrIdxToArrayIdx(Index);
  SmallVector<AttributeSet, 8> AttrSets(Index + 1);
  AttrSets[Index] = AttributeSet::get(C, B);
  return getImpl(C, AttrSets);
}

AttributeList AttributeList::get(LLVMContext &C, unsigned Index,
                                 ArrayRef<Attribute::AttrKind> Kinds) {
  SmallVector<std::pair<unsigned, Attribute>, 8> Attrs;
  for (const auto K : Kinds)
    Attrs.emplace_back(Index, Attribute::get(C, K));
  return get(C, Attrs);
}

AttributeList AttributeList::get(LLVMContext &C, unsigned Index,
                                 ArrayRef<StringRef> Kinds) {
  SmallVector<std::pair<unsigned, Attribute>, 8> Attrs;
  for (const auto K : Kinds)
    Attrs.emplace_back(Index, Attribute::get(C, K));
  return get(C, Attrs);
}

AttributeList AttributeList::get(LLVMContext &C,
                                 ArrayRef<AttributeList> Attrs) {
  if (Attrs.empty())
    return {};
  if (Attrs.size() == 1)
    return Attrs[0];

  unsigned MaxSize = 0;
  for (const auto List : Attrs)
    MaxSize = std::max(MaxSize, List.getNumAttrSets());

  // If every list was empty, there is no point in merging the lists.
  if (MaxSize == 0)
    return {};

  SmallVector<AttributeSet, 8> NewAttrSets(MaxSize);
  for (unsigned I = 0; I < MaxSize; ++I) {
    AttrBuilder CurBuilder;
    for (const auto List : Attrs)
      CurBuilder.merge(List.getAttributes(I - 1));
    NewAttrSets[I] = AttributeSet::get(C, CurBuilder);
  }

  return getImpl(C, NewAttrSets);
}

AttributeList AttributeList::addAttribute(LLVMContext &C, unsigned Index,
                                          Attribute::AttrKind Kind) const {
  if (hasAttribute(Index, Kind)) return *this;
  AttrBuilder B;
  B.addAttribute(Kind);
  return addAttributes(C, Index, B);
}

AttributeList AttributeList::addAttribute(LLVMContext &C, unsigned Index,
                                          StringRef Kind,
                                          StringRef Value) const {
  AttrBuilder B;
  B.addAttribute(Kind, Value);
  return addAttributes(C, Index, B);
}

AttributeList AttributeList::addAttribute(LLVMContext &C, unsigned Index,
                                          Attribute A) const {
  AttrBuilder B;
  B.addAttribute(A);
  return addAttributes(C, Index, B);
}

AttributeList AttributeList::addAttributes(LLVMContext &C, unsigned Index,
                                           const AttrBuilder &B) const {
  if (!B.hasAttributes())
    return *this;

  if (!pImpl)
    return AttributeList::get(C, {{Index, AttributeSet::get(C, B)}});

#ifndef NDEBUG
  // FIXME it is not obvious how this should work for alignment. For now, say
  // we can't change a known alignment.
  unsigned OldAlign = getAttributes(Index).getAlignment();
  unsigned NewAlign = B.getAlignment();
  assert((!OldAlign || !NewAlign || OldAlign == NewAlign) &&
         "Attempt to change alignment!");
#endif

  Index = attrIdxToArrayIdx(Index);
  SmallVector<AttributeSet, 4> AttrSets(this->begin(), this->end());
  if (Index >= AttrSets.size())
    AttrSets.resize(Index + 1);

  AttrBuilder Merged(AttrSets[Index]);
  Merged.merge(B);
  AttrSets[Index] = AttributeSet::get(C, Merged);

  return getImpl(C, AttrSets);
}

AttributeList AttributeList::addParamAttribute(LLVMContext &C,
                                               ArrayRef<unsigned> ArgNos,
                                               Attribute A) const {
  assert(std::is_sorted(ArgNos.begin(), ArgNos.end()));

  SmallVector<AttributeSet, 4> AttrSets(this->begin(), this->end());
  unsigned MaxIndex = attrIdxToArrayIdx(ArgNos.back() + FirstArgIndex);
  if (MaxIndex >= AttrSets.size())
    AttrSets.resize(MaxIndex + 1);

  for (unsigned ArgNo : ArgNos) {
    unsigned Index = attrIdxToArrayIdx(ArgNo + FirstArgIndex);
    AttrBuilder B(AttrSets[Index]);
    B.addAttribute(A);
    AttrSets[Index] = AttributeSet::get(C, B);
  }

  return getImpl(C, AttrSets);
}

AttributeList AttributeList::removeAttribute(LLVMContext &C, unsigned Index,
                                             Attribute::AttrKind Kind) const {
  if (!hasAttribute(Index, Kind)) return *this;

  Index = attrIdxToArrayIdx(Index);
  SmallVector<AttributeSet, 4> AttrSets(this->begin(), this->end());
  assert(Index < AttrSets.size());

  AttrSets[Index] = AttrSets[Index].removeAttribute(C, Kind);

  return getImpl(C, AttrSets);
}

AttributeList AttributeList::removeAttribute(LLVMContext &C, unsigned Index,
                                             StringRef Kind) const {
  if (!hasAttribute(Index, Kind)) return *this;

  Index = attrIdxToArrayIdx(Index);
  SmallVector<AttributeSet, 4> AttrSets(this->begin(), this->end());
  assert(Index < AttrSets.size());

  AttrSets[Index] = AttrSets[Index].removeAttribute(C, Kind);

  return getImpl(C, AttrSets);
}

AttributeList
AttributeList::removeAttributes(LLVMContext &C, unsigned Index,
                                const AttrBuilder &AttrsToRemove) const {
  if (!pImpl)
    return {};

  Index = attrIdxToArrayIdx(Index);
  SmallVector<AttributeSet, 4> AttrSets(this->begin(), this->end());
  if (Index >= AttrSets.size())
    AttrSets.resize(Index + 1);

  AttrSets[Index] = AttrSets[Index].removeAttributes(C, AttrsToRemove);

  return getImpl(C, AttrSets);
}

AttributeList AttributeList::removeAttributes(LLVMContext &C,
                                              unsigned WithoutIndex) const {
  if (!pImpl)
    return {};
  WithoutIndex = attrIdxToArrayIdx(WithoutIndex);
  if (WithoutIndex >= getNumAttrSets())
    return *this;
  SmallVector<AttributeSet, 4> AttrSets(this->begin(), this->end());
  AttrSets[WithoutIndex] = AttributeSet();
  return getImpl(C, AttrSets);
}

AttributeList AttributeList::addDereferenceableAttr(LLVMContext &C,
                                                    unsigned Index,
                                                    uint64_t Bytes) const {
  AttrBuilder B;
  B.addDereferenceableAttr(Bytes);
  return addAttributes(C, Index, B);
}

AttributeList
AttributeList::addDereferenceableOrNullAttr(LLVMContext &C, unsigned Index,
                                            uint64_t Bytes) const {
  AttrBuilder B;
  B.addDereferenceableOrNullAttr(Bytes);
  return addAttributes(C, Index, B);
}

AttributeList
AttributeList::addAllocSizeAttr(LLVMContext &C, unsigned Index,
                                unsigned ElemSizeArg,
                                const Optional<unsigned> &NumElemsArg) {
  AttrBuilder B;
  B.addAllocSizeAttr(ElemSizeArg, NumElemsArg);
  return addAttributes(C, Index, B);
}

//===----------------------------------------------------------------------===//
// AttributeList Accessor Methods
//===----------------------------------------------------------------------===//

LLVMContext &AttributeList::getContext() const { return pImpl->getContext(); }

AttributeSet AttributeList::getParamAttributes(unsigned ArgNo) const {
  return getAttributes(ArgNo + FirstArgIndex);
}

AttributeSet AttributeList::getRetAttributes() const {
  return getAttributes(ReturnIndex);
}

AttributeSet AttributeList::getFnAttributes() const {
  return getAttributes(FunctionIndex);
}

bool AttributeList::hasAttribute(unsigned Index,
                                 Attribute::AttrKind Kind) const {
  return getAttributes(Index).hasAttribute(Kind);
}

bool AttributeList::hasAttribute(unsigned Index, StringRef Kind) const {
  return getAttributes(Index).hasAttribute(Kind);
}

bool AttributeList::hasAttributes(unsigned Index) const {
  return getAttributes(Index).hasAttributes();
}

bool AttributeList::hasFnAttribute(Attribute::AttrKind Kind) const {
  return pImpl && pImpl->hasFnAttribute(Kind);
}

bool AttributeList::hasFnAttribute(StringRef Kind) const {
  return hasAttribute(AttributeList::FunctionIndex, Kind);
}

bool AttributeList::hasParamAttribute(unsigned ArgNo,
                                      Attribute::AttrKind Kind) const {
  return hasAttribute(ArgNo + FirstArgIndex, Kind);
}

bool AttributeList::hasAttrSomewhere(Attribute::AttrKind Attr,
                                     unsigned *Index) const {
  if (!pImpl) return false;

  for (unsigned I = index_begin(), E = index_end(); I != E; ++I) {
    if (hasAttribute(I, Attr)) {
      if (Index)
        *Index = I;
      return true;
    }
  }

  return false;
}

Attribute AttributeList::getAttribute(unsigned Index,
                                      Attribute::AttrKind Kind) const {
  return getAttributes(Index).getAttribute(Kind);
}

Attribute AttributeList::getAttribute(unsigned Index, StringRef Kind) const {
  return getAttributes(Index).getAttribute(Kind);
}

unsigned AttributeList::getRetAlignment() const {
  return getAttributes(ReturnIndex).getAlignment();
}

unsigned AttributeList::getParamAlignment(unsigned ArgNo) const {
  return getAttributes(ArgNo + FirstArgIndex).getAlignment();
}

unsigned AttributeList::getStackAlignment(unsigned Index) const {
  return getAttributes(Index).getStackAlignment();
}

uint64_t AttributeList::getDereferenceableBytes(unsigned Index) const {
  return getAttributes(Index).getDereferenceableBytes();
}

uint64_t AttributeList::getDereferenceableOrNullBytes(unsigned Index) const {
  return getAttributes(Index).getDereferenceableOrNullBytes();
}

std::pair<unsigned, Optional<unsigned>>
AttributeList::getAllocSizeArgs(unsigned Index) const {
  return getAttributes(Index).getAllocSizeArgs();
}

std::string AttributeList::getAsString(unsigned Index, bool InAttrGrp) const {
  return getAttributes(Index).getAsString(InAttrGrp);
}

AttributeSet AttributeList::getAttributes(unsigned Index) const {
  Index = attrIdxToArrayIdx(Index);
  if (!pImpl || Index >= getNumAttrSets())
    return {};
  return pImpl->begin()[Index];
}

AttributeList::iterator AttributeList::begin() const {
  return pImpl ? pImpl->begin() : nullptr;
}

AttributeList::iterator AttributeList::end() const {
  return pImpl ? pImpl->end() : nullptr;
}

//===----------------------------------------------------------------------===//
// AttributeList Introspection Methods
//===----------------------------------------------------------------------===//

unsigned AttributeList::getNumAttrSets() const {
  return pImpl ? pImpl->NumAttrSets : 0;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void AttributeList::dump() const {
  dbgs() << "PAL[\n";

  for (unsigned i = index_begin(), e = index_end(); i != e; ++i) {
    if (getAttributes(i).hasAttributes())
      dbgs() << "  { " << i << " => " << getAsString(i) << " }\n";
  }

  dbgs() << "]\n";
}
#endif

//===----------------------------------------------------------------------===//
// AttrBuilder Method Implementations
//===----------------------------------------------------------------------===//

// FIXME: Remove this ctor, use AttributeSet.
AttrBuilder::AttrBuilder(AttributeList AL, unsigned Index) {
  AttributeSet AS = AL.getAttributes(Index);
  for (const auto &A : AS)
    addAttribute(A);
}

AttrBuilder::AttrBuilder(AttributeSet AS) {
  for (const auto &A : AS)
    addAttribute(A);
}

void AttrBuilder::clear() {
  Attrs.reset();
  TargetDepAttrs.clear();
  Alignment = StackAlignment = DerefBytes = DerefOrNullBytes = 0;
  AllocSizeArgs = 0;
}

AttrBuilder &AttrBuilder::addAttribute(Attribute::AttrKind Val) {
  assert((unsigned)Val < Attribute::EndAttrKinds && "Attribute out of range!");
  assert(Val != Attribute::Alignment && Val != Attribute::StackAlignment &&
         Val != Attribute::Dereferenceable && Val != Attribute::AllocSize &&
         "Adding integer attribute without adding a value!");
  Attrs[Val] = true;
  return *this;
}

AttrBuilder &AttrBuilder::addAttribute(Attribute Attr) {
  if (Attr.isStringAttribute()) {
    addAttribute(Attr.getKindAsString(), Attr.getValueAsString());
    return *this;
  }

  Attribute::AttrKind Kind = Attr.getKindAsEnum();
  Attrs[Kind] = true;

  if (Kind == Attribute::Alignment)
    Alignment = Attr.getAlignment();
  else if (Kind == Attribute::StackAlignment)
    StackAlignment = Attr.getStackAlignment();
  else if (Kind == Attribute::Dereferenceable)
    DerefBytes = Attr.getDereferenceableBytes();
  else if (Kind == Attribute::DereferenceableOrNull)
    DerefOrNullBytes = Attr.getDereferenceableOrNullBytes();
  else if (Kind == Attribute::AllocSize)
    AllocSizeArgs = Attr.getValueAsInt();
  return *this;
}

AttrBuilder &AttrBuilder::addAttribute(StringRef A, StringRef V) {
  TargetDepAttrs[A] = V;
  return *this;
}

AttrBuilder &AttrBuilder::removeAttribute(Attribute::AttrKind Val) {
  assert((unsigned)Val < Attribute::EndAttrKinds && "Attribute out of range!");
  Attrs[Val] = false;

  if (Val == Attribute::Alignment)
    Alignment = 0;
  else if (Val == Attribute::StackAlignment)
    StackAlignment = 0;
  else if (Val == Attribute::Dereferenceable)
    DerefBytes = 0;
  else if (Val == Attribute::DereferenceableOrNull)
    DerefOrNullBytes = 0;
  else if (Val == Attribute::AllocSize)
    AllocSizeArgs = 0;

  return *this;
}

AttrBuilder &AttrBuilder::removeAttributes(AttributeList A, uint64_t Index) {
  remove(A.getAttributes(Index));
  return *this;
}

AttrBuilder &AttrBuilder::removeAttribute(StringRef A) {
  auto I = TargetDepAttrs.find(A);
  if (I != TargetDepAttrs.end())
    TargetDepAttrs.erase(I);
  return *this;
}

std::pair<unsigned, Optional<unsigned>> AttrBuilder::getAllocSizeArgs() const {
  return unpackAllocSizeArgs(AllocSizeArgs);
}

AttrBuilder &AttrBuilder::addAlignmentAttr(unsigned Align) {
  if (Align == 0) return *this;

  assert(isPowerOf2_32(Align) && "Alignment must be a power of two.");
  assert(Align <= 0x40000000 && "Alignment too large.");

  Attrs[Attribute::Alignment] = true;
  Alignment = Align;
  return *this;
}

AttrBuilder &AttrBuilder::addStackAlignmentAttr(unsigned Align) {
  // Default alignment, allow the target to define how to align it.
  if (Align == 0) return *this;

  assert(isPowerOf2_32(Align) && "Alignment must be a power of two.");
  assert(Align <= 0x100 && "Alignment too large.");

  Attrs[Attribute::StackAlignment] = true;
  StackAlignment = Align;
  return *this;
}

AttrBuilder &AttrBuilder::addDereferenceableAttr(uint64_t Bytes) {
  if (Bytes == 0) return *this;

  Attrs[Attribute::Dereferenceable] = true;
  DerefBytes = Bytes;
  return *this;
}

AttrBuilder &AttrBuilder::addDereferenceableOrNullAttr(uint64_t Bytes) {
  if (Bytes == 0)
    return *this;

  Attrs[Attribute::DereferenceableOrNull] = true;
  DerefOrNullBytes = Bytes;
  return *this;
}

AttrBuilder &AttrBuilder::addAllocSizeAttr(unsigned ElemSize,
                                           const Optional<unsigned> &NumElems) {
  return addAllocSizeAttrFromRawRepr(packAllocSizeArgs(ElemSize, NumElems));
}

AttrBuilder &AttrBuilder::addAllocSizeAttrFromRawRepr(uint64_t RawArgs) {
  // (0, 0) is our "not present" value, so we need to check for it here.
  assert(RawArgs && "Invalid allocsize arguments -- given allocsize(0, 0)");

  Attrs[Attribute::AllocSize] = true;
  // Reuse existing machinery to store this as a single 64-bit integer so we can
  // save a few bytes over using a pair<unsigned, Optional<unsigned>>.
  AllocSizeArgs = RawArgs;
  return *this;
}

AttrBuilder &AttrBuilder::merge(const AttrBuilder &B) {
  // FIXME: What if both have alignments, but they don't match?!
  if (!Alignment)
    Alignment = B.Alignment;

  if (!StackAlignment)
    StackAlignment = B.StackAlignment;

  if (!DerefBytes)
    DerefBytes = B.DerefBytes;

  if (!DerefOrNullBytes)
    DerefOrNullBytes = B.DerefOrNullBytes;

  if (!AllocSizeArgs)
    AllocSizeArgs = B.AllocSizeArgs;

  Attrs |= B.Attrs;

  for (auto I : B.td_attrs())
    TargetDepAttrs[I.first] = I.second;

  return *this;
}

AttrBuilder &AttrBuilder::remove(const AttrBuilder &B) {
  // FIXME: What if both have alignments, but they don't match?!
  if (B.Alignment)
    Alignment = 0;

  if (B.StackAlignment)
    StackAlignment = 0;

  if (B.DerefBytes)
    DerefBytes = 0;

  if (B.DerefOrNullBytes)
    DerefOrNullBytes = 0;

  if (B.AllocSizeArgs)
    AllocSizeArgs = 0;

  Attrs &= ~B.Attrs;

  for (auto I : B.td_attrs())
    TargetDepAttrs.erase(I.first);

  return *this;
}

bool AttrBuilder::overlaps(const AttrBuilder &B) const {
  // First check if any of the target independent attributes overlap.
  if ((Attrs & B.Attrs).any())
    return true;

  // Then check if any target dependent ones do.
  for (const auto &I : td_attrs())
    if (B.contains(I.first))
      return true;

  return false;
}

bool AttrBuilder::contains(StringRef A) const {
  return TargetDepAttrs.find(A) != TargetDepAttrs.end();
}

bool AttrBuilder::hasAttributes() const {
  return !Attrs.none() || !TargetDepAttrs.empty();
}

bool AttrBuilder::hasAttributes(AttributeList AL, uint64_t Index) const {
  AttributeSet AS = AL.getAttributes(Index);

  for (const auto Attr : AS) {
    if (Attr.isEnumAttribute() || Attr.isIntAttribute()) {
      if (contains(Attr.getKindAsEnum()))
        return true;
    } else {
      assert(Attr.isStringAttribute() && "Invalid attribute kind!");
      return contains(Attr.getKindAsString());
    }
  }

  return false;
}

bool AttrBuilder::hasAlignmentAttr() const {
  return Alignment != 0;
}

bool AttrBuilder::operator==(const AttrBuilder &B) {
  if (Attrs != B.Attrs)
    return false;

  for (td_const_iterator I = TargetDepAttrs.begin(),
         E = TargetDepAttrs.end(); I != E; ++I)
    if (B.TargetDepAttrs.find(I->first) == B.TargetDepAttrs.end())
      return false;

  return Alignment == B.Alignment && StackAlignment == B.StackAlignment &&
         DerefBytes == B.DerefBytes;
}

//===----------------------------------------------------------------------===//
// AttributeFuncs Function Defintions
//===----------------------------------------------------------------------===//

/// Which attributes cannot be applied to a type.
AttrBuilder AttributeFuncs::typeIncompatible(Type *Ty) {
  AttrBuilder Incompatible;

  if (!Ty->isIntegerTy())
    // Attribute that only apply to integers.
    Incompatible.addAttribute(Attribute::SExt)
      .addAttribute(Attribute::ZExt);

  if (!Ty->isPointerTy())
    // Attribute that only apply to pointers.
    Incompatible.addAttribute(Attribute::ByVal)
      .addAttribute(Attribute::Nest)
      .addAttribute(Attribute::NoAlias)
      .addAttribute(Attribute::NoCapture)
      .addAttribute(Attribute::NonNull)
      .addDereferenceableAttr(1) // the int here is ignored
      .addDereferenceableOrNullAttr(1) // the int here is ignored
      .addAttribute(Attribute::ReadNone)
      .addAttribute(Attribute::ReadOnly)
      .addAttribute(Attribute::StructRet)
      .addAttribute(Attribute::InAlloca);

  return Incompatible;
}

template<typename AttrClass>
static bool isEqual(const Function &Caller, const Function &Callee) {
  return Caller.getFnAttribute(AttrClass::getKind()) ==
         Callee.getFnAttribute(AttrClass::getKind());
}

/// Compute the logical AND of the attributes of the caller and the
/// callee.
///
/// This function sets the caller's attribute to false if the callee's attribute
/// is false.
template<typename AttrClass>
static void setAND(Function &Caller, const Function &Callee) {
  if (AttrClass::isSet(Caller, AttrClass::getKind()) &&
      !AttrClass::isSet(Callee, AttrClass::getKind()))
    AttrClass::set(Caller, AttrClass::getKind(), false);
}

/// Compute the logical OR of the attributes of the caller and the
/// callee.
///
/// This function sets the caller's attribute to true if the callee's attribute
/// is true.
template<typename AttrClass>
static void setOR(Function &Caller, const Function &Callee) {
  if (!AttrClass::isSet(Caller, AttrClass::getKind()) &&
      AttrClass::isSet(Callee, AttrClass::getKind()))
    AttrClass::set(Caller, AttrClass::getKind(), true);
}

/// If the inlined function had a higher stack protection level than the
/// calling function, then bump up the caller's stack protection level.
static void adjustCallerSSPLevel(Function &Caller, const Function &Callee) {
  // If upgrading the SSP attribute, clear out the old SSP Attributes first.
  // Having multiple SSP attributes doesn't actually hurt, but it adds useless
  // clutter to the IR.
  AttrBuilder OldSSPAttr;
  OldSSPAttr.addAttribute(Attribute::StackProtect)
      .addAttribute(Attribute::StackProtectStrong)
      .addAttribute(Attribute::StackProtectReq);

  if (Callee.hasFnAttribute(Attribute::StackProtectReq)) {
    Caller.removeAttributes(AttributeList::FunctionIndex, OldSSPAttr);
    Caller.addFnAttr(Attribute::StackProtectReq);
  } else if (Callee.hasFnAttribute(Attribute::StackProtectStrong) &&
             !Caller.hasFnAttribute(Attribute::StackProtectReq)) {
    Caller.removeAttributes(AttributeList::FunctionIndex, OldSSPAttr);
    Caller.addFnAttr(Attribute::StackProtectStrong);
  } else if (Callee.hasFnAttribute(Attribute::StackProtect) &&
             !Caller.hasFnAttribute(Attribute::StackProtectReq) &&
             !Caller.hasFnAttribute(Attribute::StackProtectStrong))
    Caller.addFnAttr(Attribute::StackProtect);
}

/// If the inlined function required stack probes, then ensure that
/// the calling function has those too.
static void adjustCallerStackProbes(Function &Caller, const Function &Callee) {
  if (!Caller.hasFnAttribute("probe-stack") &&
      Callee.hasFnAttribute("probe-stack")) {
    Caller.addFnAttr(Callee.getFnAttribute("probe-stack"));
  }
}

/// If the inlined function defines the size of guard region
/// on the stack, then ensure that the calling function defines a guard region
/// that is no larger.
static void
adjustCallerStackProbeSize(Function &Caller, const Function &Callee) {
  if (Callee.hasFnAttribute("stack-probe-size")) {
    uint64_t CalleeStackProbeSize;
    Callee.getFnAttribute("stack-probe-size")
          .getValueAsString()
          .getAsInteger(0, CalleeStackProbeSize);
    if (Caller.hasFnAttribute("stack-probe-size")) {
      uint64_t CallerStackProbeSize;
      Caller.getFnAttribute("stack-probe-size")
            .getValueAsString()
            .getAsInteger(0, CallerStackProbeSize);
      if (CallerStackProbeSize > CalleeStackProbeSize) {
        Caller.addFnAttr(Callee.getFnAttribute("stack-probe-size"));
      }
    } else {
      Caller.addFnAttr(Callee.getFnAttribute("stack-probe-size"));
    }
  }
}

/// If the inlined function defines a min legal vector width, then ensure
/// the calling function has the same or larger min legal vector width. If the
/// caller has the attribute, but the callee doesn't, we need to remove the
/// attribute from the caller since we can't make any guarantees about the
/// caller's requirements.
/// This function is called after the inlining decision has been made so we have
/// to merge the attribute this way. Heuristics that would use
/// min-legal-vector-width to determine inline compatibility would need to be
/// handled as part of inline cost analysis.
static void
adjustMinLegalVectorWidth(Function &Caller, const Function &Callee) {
  if (Caller.hasFnAttribute("min-legal-vector-width")) {
    if (Callee.hasFnAttribute("min-legal-vector-width")) {
      uint64_t CallerVectorWidth;
      Caller.getFnAttribute("min-legal-vector-width")
            .getValueAsString()
            .getAsInteger(0, CallerVectorWidth);
      uint64_t CalleeVectorWidth;
      Callee.getFnAttribute("min-legal-vector-width")
            .getValueAsString()
            .getAsInteger(0, CalleeVectorWidth);
      if (CallerVectorWidth < CalleeVectorWidth)
        Caller.addFnAttr(Callee.getFnAttribute("min-legal-vector-width"));
    } else {
      // If the callee doesn't have the attribute then we don't know anything
      // and must drop the attribute from the caller.
      Caller.removeFnAttr("min-legal-vector-width");
    }
  }
}

/// If the inlined function has "null-pointer-is-valid=true" attribute,
/// set this attribute in the caller post inlining.
static void
adjustNullPointerValidAttr(Function &Caller, const Function &Callee) {
  if (Callee.nullPointerIsDefined() && !Caller.nullPointerIsDefined()) {
    Caller.addFnAttr(Callee.getFnAttribute("null-pointer-is-valid"));
  }
}

#define GET_ATTR_COMPAT_FUNC
#include "AttributesCompatFunc.inc"

bool AttributeFuncs::areInlineCompatible(const Function &Caller,
                                         const Function &Callee) {
  return hasCompatibleFnAttrs(Caller, Callee);
}

void AttributeFuncs::mergeAttributesForInlining(Function &Caller,
                                                const Function &Callee) {
  mergeFnAttrs(Caller, Callee);
}
