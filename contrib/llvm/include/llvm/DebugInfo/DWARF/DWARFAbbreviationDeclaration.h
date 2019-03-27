//===- DWARFAbbreviationDeclaration.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARFABBREVIATIONDECLARATION_H
#define LLVM_DEBUGINFO_DWARFABBREVIATIONDECLARATION_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/DataExtractor.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace llvm {

class DWARFFormValue;
class DWARFUnit;
class raw_ostream;

class DWARFAbbreviationDeclaration {
public:
  struct AttributeSpec {
    AttributeSpec(dwarf::Attribute A, dwarf::Form F, int64_t Value)
        : Attr(A), Form(F), Value(Value) {
      assert(isImplicitConst());
    }
    AttributeSpec(dwarf::Attribute A, dwarf::Form F, Optional<uint8_t> ByteSize)
        : Attr(A), Form(F) {
      assert(!isImplicitConst());
      this->ByteSize.HasByteSize = ByteSize.hasValue();
      if (this->ByteSize.HasByteSize)
        this->ByteSize.ByteSize = *ByteSize;
    }

    dwarf::Attribute Attr;
    dwarf::Form Form;

  private:
    /// The following field is used for ByteSize for non-implicit_const
    /// attributes and as value for implicit_const ones, indicated by
    /// Form == DW_FORM_implicit_const.
    /// The following cases are distinguished:
    /// * Form != DW_FORM_implicit_const and HasByteSize is true:
    ///     ByteSize contains the fixed size in bytes for the Form in this
    ///     object.
    /// * Form != DW_FORM_implicit_const and HasByteSize is false:
    ///     byte size of Form either varies according to the DWARFUnit
    ///     that it is contained in or the value size varies and must be
    ///     decoded from the debug information in order to determine its size.
    /// * Form == DW_FORM_implicit_const:
    ///     Value contains value for the implicit_const attribute.
    struct ByteSizeStorage {
      bool HasByteSize;
      uint8_t ByteSize;
    };
    union {
      ByteSizeStorage ByteSize;
      int64_t Value;
    };

  public:
    bool isImplicitConst() const {
      return Form == dwarf::DW_FORM_implicit_const;
    }

    int64_t getImplicitConstValue() const {
      assert(isImplicitConst());
      return Value;
    }

    /// Get the fixed byte size of this Form if possible. This function might
    /// use the DWARFUnit to calculate the size of the Form, like for
    /// DW_AT_address and DW_AT_ref_addr, so this isn't just an accessor for
    /// the ByteSize member.
    Optional<int64_t> getByteSize(const DWARFUnit &U) const;
  };
  using AttributeSpecVector = SmallVector<AttributeSpec, 8>;

  DWARFAbbreviationDeclaration();

  uint32_t getCode() const { return Code; }
  uint8_t getCodeByteSize() const { return CodeByteSize; }
  dwarf::Tag getTag() const { return Tag; }
  bool hasChildren() const { return HasChildren; }

  using attr_iterator_range =
      iterator_range<AttributeSpecVector::const_iterator>;

  attr_iterator_range attributes() const {
    return attr_iterator_range(AttributeSpecs.begin(), AttributeSpecs.end());
  }

  dwarf::Form getFormByIndex(uint32_t idx) const {
    assert(idx < AttributeSpecs.size());
    return AttributeSpecs[idx].Form;
  }

  size_t getNumAttributes() const {
    return AttributeSpecs.size();
  }

  dwarf::Attribute getAttrByIndex(uint32_t idx) const {
    assert(idx < AttributeSpecs.size());
    return AttributeSpecs[idx].Attr;
  }

  /// Get the index of the specified attribute.
  ///
  /// Searches the this abbreviation declaration for the index of the specified
  /// attribute.
  ///
  /// \param attr DWARF attribute to search for.
  /// \returns Optional index of the attribute if found, None otherwise.
  Optional<uint32_t> findAttributeIndex(dwarf::Attribute attr) const;

  /// Extract a DWARF form value from a DIE specified by DIE offset.
  ///
  /// Extract an attribute value for a DWARFUnit given the DIE offset and the
  /// attribute.
  ///
  /// \param DIEOffset the DIE offset that points to the ULEB128 abbreviation
  /// code in the .debug_info data.
  /// \param Attr DWARF attribute to search for.
  /// \param U the DWARFUnit the contains the DIE.
  /// \returns Optional DWARF form value if the attribute was extracted.
  Optional<DWARFFormValue> getAttributeValue(const uint32_t DIEOffset,
                                             const dwarf::Attribute Attr,
                                             const DWARFUnit &U) const;

  bool extract(DataExtractor Data, uint32_t* OffsetPtr);
  void dump(raw_ostream &OS) const;

  // Return an optional byte size of all attribute data in this abbreviation
  // if a constant byte size can be calculated given a DWARFUnit. This allows
  // DWARF parsing to be faster as many DWARF DIEs have a fixed byte size.
  Optional<size_t> getFixedAttributesByteSize(const DWARFUnit &U) const;

private:
  void clear();

  /// A helper structure that can quickly determine the size in bytes of an
  /// abbreviation declaration.
  struct FixedSizeInfo {
    /// The fixed byte size for fixed size forms.
    uint16_t NumBytes = 0;
    /// Number of DW_FORM_address forms in this abbrevation declaration.
    uint8_t NumAddrs = 0;
    /// Number of DW_FORM_ref_addr forms in this abbrevation declaration.
    uint8_t NumRefAddrs = 0;
    /// Number of 4 byte in DWARF32 and 8 byte in DWARF64 forms.
    uint8_t NumDwarfOffsets = 0;

    FixedSizeInfo() = default;

    /// Calculate the fixed size in bytes given a DWARFUnit.
    ///
    /// \param U the DWARFUnit to use when determing the byte size.
    /// \returns the size in bytes for all attribute data in this abbreviation.
    /// The returned size does not include bytes for the  ULEB128 abbreviation
    /// code
    size_t getByteSize(const DWARFUnit &U) const;
  };

  uint32_t Code;
  dwarf::Tag Tag;
  uint8_t CodeByteSize;
  bool HasChildren;
  AttributeSpecVector AttributeSpecs;
  /// If this abbreviation has a fixed byte size then FixedAttributeSize member
  /// variable below will have a value.
  Optional<FixedSizeInfo> FixedAttributeSize;
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARFABBREVIATIONDECLARATION_H
