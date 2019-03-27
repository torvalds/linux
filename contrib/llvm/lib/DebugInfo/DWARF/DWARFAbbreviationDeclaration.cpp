//===- DWARFAbbreviationDeclaration.cpp -----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"

#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <cstdint>

using namespace llvm;
using namespace dwarf;

void DWARFAbbreviationDeclaration::clear() {
  Code = 0;
  Tag = DW_TAG_null;
  CodeByteSize = 0;
  HasChildren = false;
  AttributeSpecs.clear();
  FixedAttributeSize.reset();
}

DWARFAbbreviationDeclaration::DWARFAbbreviationDeclaration() {
  clear();
}

bool
DWARFAbbreviationDeclaration::extract(DataExtractor Data,
                                      uint32_t* OffsetPtr) {
  clear();
  const uint32_t Offset = *OffsetPtr;
  Code = Data.getULEB128(OffsetPtr);
  if (Code == 0) {
    return false;
  }
  CodeByteSize = *OffsetPtr - Offset;
  Tag = static_cast<llvm::dwarf::Tag>(Data.getULEB128(OffsetPtr));
  if (Tag == DW_TAG_null) {
    clear();
    return false;
  }
  uint8_t ChildrenByte = Data.getU8(OffsetPtr);
  HasChildren = (ChildrenByte == DW_CHILDREN_yes);
  // Assign a value to our optional FixedAttributeSize member variable. If
  // this member variable still has a value after the while loop below, then
  // all attribute data in this abbreviation declaration has a fixed byte size.
  FixedAttributeSize = FixedSizeInfo();

  // Read all of the abbreviation attributes and forms.
  while (true) {
    auto A = static_cast<Attribute>(Data.getULEB128(OffsetPtr));
    auto F = static_cast<Form>(Data.getULEB128(OffsetPtr));
    if (A && F) {
      bool IsImplicitConst = (F == DW_FORM_implicit_const);
      if (IsImplicitConst) {
        int64_t V = Data.getSLEB128(OffsetPtr);
        AttributeSpecs.push_back(AttributeSpec(A, F, V));
        continue;
      }
      Optional<uint8_t> ByteSize;
      // If this abbrevation still has a fixed byte size, then update the
      // FixedAttributeSize as needed.
      switch (F) {
      case DW_FORM_addr:
        if (FixedAttributeSize)
          ++FixedAttributeSize->NumAddrs;
        break;

      case DW_FORM_ref_addr:
        if (FixedAttributeSize)
          ++FixedAttributeSize->NumRefAddrs;
        break;

      case DW_FORM_strp:
      case DW_FORM_GNU_ref_alt:
      case DW_FORM_GNU_strp_alt:
      case DW_FORM_line_strp:
      case DW_FORM_sec_offset:
      case DW_FORM_strp_sup:
        if (FixedAttributeSize)
          ++FixedAttributeSize->NumDwarfOffsets;
        break;

      default:
        // The form has a byte size that doesn't depend on Params.
        // If it's a fixed size, keep track of it.
        if ((ByteSize = dwarf::getFixedFormByteSize(F, dwarf::FormParams()))) {
          if (FixedAttributeSize)
            FixedAttributeSize->NumBytes += *ByteSize;
          break;
        }
        // Indicate we no longer have a fixed byte size for this
        // abbreviation by clearing the FixedAttributeSize optional value
        // so it doesn't have a value.
        FixedAttributeSize.reset();
        break;
      }
      // Record this attribute and its fixed size if it has one.
      AttributeSpecs.push_back(AttributeSpec(A, F, ByteSize));
    } else if (A == 0 && F == 0) {
      // We successfully reached the end of this abbreviation declaration
      // since both attribute and form are zero.
      break;
    } else {
      // Attribute and form pairs must either both be non-zero, in which case
      // they are added to the abbreviation declaration, or both be zero to
      // terminate the abbrevation declaration. In this case only one was
      // zero which is an error.
      clear();
      return false;
    }
  }
  return true;
}

void DWARFAbbreviationDeclaration::dump(raw_ostream &OS) const {
  OS << '[' << getCode() << "] ";
  OS << formatv("{0}", getTag());
  OS << "\tDW_CHILDREN_" << (hasChildren() ? "yes" : "no") << '\n';
  for (const AttributeSpec &Spec : AttributeSpecs) {
    OS << formatv("\t{0}\t{1}", Spec.Attr, Spec.Form);
    if (Spec.isImplicitConst())
      OS << '\t' << Spec.getImplicitConstValue();
    OS << '\n';
  }
  OS << '\n';
}

Optional<uint32_t>
DWARFAbbreviationDeclaration::findAttributeIndex(dwarf::Attribute Attr) const {
  for (uint32_t i = 0, e = AttributeSpecs.size(); i != e; ++i) {
    if (AttributeSpecs[i].Attr == Attr)
      return i;
  }
  return None;
}

Optional<DWARFFormValue> DWARFAbbreviationDeclaration::getAttributeValue(
    const uint32_t DIEOffset, const dwarf::Attribute Attr,
    const DWARFUnit &U) const {
  Optional<uint32_t> MatchAttrIndex = findAttributeIndex(Attr);
  if (!MatchAttrIndex)
    return None;

  auto DebugInfoData = U.getDebugInfoExtractor();

  // Add the byte size of ULEB that for the abbrev Code so we can start
  // skipping the attribute data.
  uint32_t Offset = DIEOffset + CodeByteSize;
  uint32_t AttrIndex = 0;
  for (const auto &Spec : AttributeSpecs) {
    if (*MatchAttrIndex == AttrIndex) {
      // We have arrived at the attribute to extract, extract if from Offset.
      DWARFFormValue FormValue(Spec.Form);
      if (Spec.isImplicitConst()) {
        FormValue.setSValue(Spec.getImplicitConstValue());
        return FormValue;
      }
      if (FormValue.extractValue(DebugInfoData, &Offset, U.getFormParams(), &U))
        return FormValue;
    }
    // March Offset along until we get to the attribute we want.
    if (auto FixedSize = Spec.getByteSize(U))
      Offset += *FixedSize;
    else
      DWARFFormValue::skipValue(Spec.Form, DebugInfoData, &Offset,
                                U.getFormParams());
    ++AttrIndex;
  }
  return None;
}

size_t DWARFAbbreviationDeclaration::FixedSizeInfo::getByteSize(
    const DWARFUnit &U) const {
  size_t ByteSize = NumBytes;
  if (NumAddrs)
    ByteSize += NumAddrs * U.getAddressByteSize();
  if (NumRefAddrs)
    ByteSize += NumRefAddrs * U.getRefAddrByteSize();
  if (NumDwarfOffsets)
    ByteSize += NumDwarfOffsets * U.getDwarfOffsetByteSize();
  return ByteSize;
}

Optional<int64_t> DWARFAbbreviationDeclaration::AttributeSpec::getByteSize(
    const DWARFUnit &U) const {
  if (isImplicitConst())
    return 0;
  if (ByteSize.HasByteSize)
    return ByteSize.ByteSize;
  Optional<int64_t> S;
  auto FixedByteSize = dwarf::getFixedFormByteSize(Form, U.getFormParams());
  if (FixedByteSize)
    S = *FixedByteSize;
  return S;
}

Optional<size_t> DWARFAbbreviationDeclaration::getFixedAttributesByteSize(
    const DWARFUnit &U) const {
  if (FixedAttributeSize)
    return FixedAttributeSize->getByteSize(U);
  return None;
}
