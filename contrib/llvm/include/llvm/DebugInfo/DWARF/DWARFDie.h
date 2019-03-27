//===- DWARFDie.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARFDIE_H
#define LLVM_DEBUGINFO_DWARFDIE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include "llvm/DebugInfo/DWARF/DWARFAttribute.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include <cassert>
#include <cstdint>
#include <iterator>

namespace llvm {

class DWARFUnit;
class raw_ostream;

//===----------------------------------------------------------------------===//
/// Utility class that carries the DWARF compile/type unit and the debug info
/// entry in an object.
///
/// When accessing information from a debug info entry we always need to DWARF
/// compile/type unit in order to extract the info correctly as some information
/// is relative to the compile/type unit. Prior to this class the DWARFUnit and
/// the DWARFDebugInfoEntry was passed around separately and there was the
/// possibility for error if the wrong DWARFUnit was used to extract a unit
/// relative offset. This class helps to ensure that this doesn't happen and
/// also simplifies the attribute extraction calls by not having to specify the
/// DWARFUnit for each call.
class DWARFDie {
  DWARFUnit *U = nullptr;
  const DWARFDebugInfoEntry *Die = nullptr;

public:
  DWARFDie() = default;
  DWARFDie(DWARFUnit *Unit, const DWARFDebugInfoEntry *D) : U(Unit), Die(D) {}

  bool isValid() const { return U && Die; }
  explicit operator bool() const { return isValid(); }
  const DWARFDebugInfoEntry *getDebugInfoEntry() const { return Die; }
  DWARFUnit *getDwarfUnit() const { return U; }

  /// Get the abbreviation declaration for this DIE.
  ///
  /// \returns the abbreviation declaration or NULL for null tags.
  const DWARFAbbreviationDeclaration *getAbbreviationDeclarationPtr() const {
    assert(isValid() && "must check validity prior to calling");
    return Die->getAbbreviationDeclarationPtr();
  }

  /// Get the absolute offset into the debug info or types section.
  ///
  /// \returns the DIE offset or -1U if invalid.
  uint32_t getOffset() const {
    assert(isValid() && "must check validity prior to calling");
    return Die->getOffset();
  }

  dwarf::Tag getTag() const {
    auto AbbrevDecl = getAbbreviationDeclarationPtr();
    if (AbbrevDecl)
      return AbbrevDecl->getTag();
    return dwarf::DW_TAG_null;
  }

  bool hasChildren() const {
    assert(isValid() && "must check validity prior to calling");
    return Die->hasChildren();
  }

  /// Returns true for a valid DIE that terminates a sibling chain.
  bool isNULL() const { return getAbbreviationDeclarationPtr() == nullptr; }

  /// Returns true if DIE represents a subprogram (not inlined).
  bool isSubprogramDIE() const;

  /// Returns true if DIE represents a subprogram or an inlined subroutine.
  bool isSubroutineDIE() const;

  /// Get the parent of this DIE object.
  ///
  /// \returns a valid DWARFDie instance if this object has a parent or an
  /// invalid DWARFDie instance if it doesn't.
  DWARFDie getParent() const;

  /// Get the sibling of this DIE object.
  ///
  /// \returns a valid DWARFDie instance if this object has a sibling or an
  /// invalid DWARFDie instance if it doesn't.
  DWARFDie getSibling() const;

  /// Get the previous sibling of this DIE object.
  ///
  /// \returns a valid DWARFDie instance if this object has a sibling or an
  /// invalid DWARFDie instance if it doesn't.
  DWARFDie getPreviousSibling() const;

  /// Get the first child of this DIE object.
  ///
  /// \returns a valid DWARFDie instance if this object has children or an
  /// invalid DWARFDie instance if it doesn't.
  DWARFDie getFirstChild() const;

  /// Get the last child of this DIE object.
  ///
  /// \returns a valid null DWARFDie instance if this object has children or an
  /// invalid DWARFDie instance if it doesn't.
  DWARFDie getLastChild() const;

  /// Dump the DIE and all of its attributes to the supplied stream.
  ///
  /// \param OS the stream to use for output.
  /// \param indent the number of characters to indent each line that is output.
  void dump(raw_ostream &OS, unsigned indent = 0,
            DIDumpOptions DumpOpts = DIDumpOptions()) const;

  /// Convenience zero-argument overload for debugging.
  LLVM_DUMP_METHOD void dump() const;

  /// Extract the specified attribute from this DIE.
  ///
  /// Extract an attribute value from this DIE only. This call doesn't look
  /// for the attribute value in any DW_AT_specification or
  /// DW_AT_abstract_origin referenced DIEs.
  ///
  /// \param Attr the attribute to extract.
  /// \returns an optional DWARFFormValue that will have the form value if the
  /// attribute was successfully extracted.
  Optional<DWARFFormValue> find(dwarf::Attribute Attr) const;

  /// Extract the first value of any attribute in Attrs from this DIE.
  ///
  /// Extract the first attribute that matches from this DIE only. This call
  /// doesn't look for the attribute value in any DW_AT_specification or
  /// DW_AT_abstract_origin referenced DIEs. The attributes will be searched
  /// linearly in the order they are specified within Attrs.
  ///
  /// \param Attrs an array of DWARF attribute to look for.
  /// \returns an optional that has a valid DWARFFormValue for the first
  /// matching attribute in Attrs, or None if none of the attributes in Attrs
  /// exist in this DIE.
  Optional<DWARFFormValue> find(ArrayRef<dwarf::Attribute> Attrs) const;

  /// Extract the first value of any attribute in Attrs from this DIE and
  /// recurse into any DW_AT_specification or DW_AT_abstract_origin referenced
  /// DIEs.
  ///
  /// \param Attrs an array of DWARF attribute to look for.
  /// \returns an optional that has a valid DWARFFormValue for the first
  /// matching attribute in Attrs, or None if none of the attributes in Attrs
  /// exist in this DIE or in any DW_AT_specification or DW_AT_abstract_origin
  /// DIEs.
  Optional<DWARFFormValue>
  findRecursively(ArrayRef<dwarf::Attribute> Attrs) const;

  /// Extract the specified attribute from this DIE as the referenced DIE.
  ///
  /// Regardless of the reference type, return the correct DWARFDie instance if
  /// the attribute exists. The returned DWARFDie object might be from another
  /// DWARFUnit, but that is all encapsulated in the new DWARFDie object.
  ///
  /// Extract an attribute value from this DIE only. This call doesn't look
  /// for the attribute value in any DW_AT_specification or
  /// DW_AT_abstract_origin referenced DIEs.
  ///
  /// \param Attr the attribute to extract.
  /// \returns a valid DWARFDie instance if the attribute exists, or an invalid
  /// DWARFDie object if it doesn't.
  DWARFDie getAttributeValueAsReferencedDie(dwarf::Attribute Attr) const;
  DWARFDie getAttributeValueAsReferencedDie(const DWARFFormValue &V) const;

  /// Extract the range base attribute from this DIE as absolute section offset.
  ///
  /// This is a utility function that checks for either the DW_AT_rnglists_base
  /// or DW_AT_GNU_ranges_base attribute.
  ///
  /// \returns anm optional absolute section offset value for the attribute.
  Optional<uint64_t> getRangesBaseAttribute() const;

  /// Get the DW_AT_high_pc attribute value as an address.
  ///
  /// In DWARF version 4 and later the high PC can be encoded as an offset from
  /// the DW_AT_low_pc. This function takes care of extracting the value as an
  /// address or offset and adds it to the low PC if needed and returns the
  /// value as an optional in case the DIE doesn't have a DW_AT_high_pc
  /// attribute.
  ///
  /// \param LowPC the low PC that might be needed to calculate the high PC.
  /// \returns an optional address value for the attribute.
  Optional<uint64_t> getHighPC(uint64_t LowPC) const;

  /// Retrieves DW_AT_low_pc and DW_AT_high_pc from CU.
  /// Returns true if both attributes are present.
  bool getLowAndHighPC(uint64_t &LowPC, uint64_t &HighPC,
                       uint64_t &SectionIndex) const;

  /// Get the address ranges for this DIE.
  ///
  /// Get the hi/low PC range if both attributes are available or exrtracts the
  /// non-contiguous address ranges from the DW_AT_ranges attribute.
  ///
  /// Extracts the range information from this DIE only. This call doesn't look
  /// for the range in any DW_AT_specification or DW_AT_abstract_origin DIEs.
  ///
  /// \returns a address range vector that might be empty if no address range
  /// information is available.
  Expected<DWARFAddressRangesVector> getAddressRanges() const;

  /// Get all address ranges for any DW_TAG_subprogram DIEs in this DIE or any
  /// of its children.
  ///
  /// Get the hi/low PC range if both attributes are available or exrtracts the
  /// non-contiguous address ranges from the DW_AT_ranges attribute for this DIE
  /// and all children.
  ///
  /// \param Ranges the addres range vector to fill in.
  void collectChildrenAddressRanges(DWARFAddressRangesVector &Ranges) const;

  bool addressRangeContainsAddress(const uint64_t Address) const;

  /// If a DIE represents a subprogram (or inlined subroutine), returns its
  /// mangled name (or short name, if mangled is missing). This name may be
  /// fetched from specification or abstract origin for this subprogram.
  /// Returns null if no name is found.
  const char *getSubroutineName(DINameKind Kind) const;

  /// Return the DIE name resolving DW_AT_sepcification or DW_AT_abstract_origin
  /// references if necessary. Returns null if no name is found.
  const char *getName(DINameKind Kind) const;

  /// Returns the declaration line (start line) for a DIE, assuming it specifies
  /// a subprogram. This may be fetched from specification or abstract origin
  /// for this subprogram by resolving DW_AT_sepcification or
  /// DW_AT_abstract_origin references if necessary.
  uint64_t getDeclLine() const;

  /// Retrieves values of DW_AT_call_file, DW_AT_call_line and DW_AT_call_column
  /// from DIE (or zeroes if they are missing). This function looks for
  /// DW_AT_call attributes in this DIE only, it will not resolve the attribute
  /// values in any DW_AT_specification or DW_AT_abstract_origin DIEs.
  /// \param CallFile filled in with non-zero if successful, zero if there is no
  /// DW_AT_call_file attribute in this DIE.
  /// \param CallLine filled in with non-zero if successful, zero if there is no
  /// DW_AT_call_line attribute in this DIE.
  /// \param CallColumn filled in with non-zero if successful, zero if there is
  /// no DW_AT_call_column attribute in this DIE.
  /// \param CallDiscriminator filled in with non-zero if successful, zero if
  /// there is no DW_AT_GNU_discriminator attribute in this DIE.
  void getCallerFrame(uint32_t &CallFile, uint32_t &CallLine,
                      uint32_t &CallColumn, uint32_t &CallDiscriminator) const;

  class attribute_iterator;

  /// Get an iterator range to all attributes in the current DIE only.
  ///
  /// \returns an iterator range for the attributes of the current DIE.
  iterator_range<attribute_iterator> attributes() const;

  class iterator;

  iterator begin() const;
  iterator end() const;

  std::reverse_iterator<iterator> rbegin() const;
  std::reverse_iterator<iterator> rend() const;

  iterator_range<iterator> children() const;
};

class DWARFDie::attribute_iterator
    : public iterator_facade_base<attribute_iterator, std::forward_iterator_tag,
                                  const DWARFAttribute> {
  /// The DWARF DIE we are extracting attributes from.
  DWARFDie Die;
  /// The value vended to clients via the operator*() or operator->().
  DWARFAttribute AttrValue;
  /// The attribute index within the abbreviation declaration in Die.
  uint32_t Index;

  friend bool operator==(const attribute_iterator &LHS,
                         const attribute_iterator &RHS);

  /// Update the attribute index and attempt to read the attribute value. If the
  /// attribute is able to be read, update AttrValue and the Index member
  /// variable. If the attribute value is not able to be read, an appropriate
  /// error will be set if the Err member variable is non-NULL and the iterator
  /// will be set to the end value so iteration stops.
  void updateForIndex(const DWARFAbbreviationDeclaration &AbbrDecl, uint32_t I);

public:
  attribute_iterator() = delete;
  explicit attribute_iterator(DWARFDie D, bool End);

  attribute_iterator &operator++();
  attribute_iterator &operator--();
  explicit operator bool() const { return AttrValue.isValid(); }
  const DWARFAttribute &operator*() const { return AttrValue; }
};

inline bool operator==(const DWARFDie::attribute_iterator &LHS,
                       const DWARFDie::attribute_iterator &RHS) {
  return LHS.Index == RHS.Index;
}

inline bool operator!=(const DWARFDie::attribute_iterator &LHS,
                       const DWARFDie::attribute_iterator &RHS) {
  return !(LHS == RHS);
}

inline bool operator==(const DWARFDie &LHS, const DWARFDie &RHS) {
  return LHS.getDebugInfoEntry() == RHS.getDebugInfoEntry() &&
         LHS.getDwarfUnit() == RHS.getDwarfUnit();
}

inline bool operator!=(const DWARFDie &LHS, const DWARFDie &RHS) {
  return !(LHS == RHS);
}

inline bool operator<(const DWARFDie &LHS, const DWARFDie &RHS) {
  return LHS.getOffset() < RHS.getOffset();
}

class DWARFDie::iterator
    : public iterator_facade_base<iterator, std::bidirectional_iterator_tag,
                                  const DWARFDie> {
  DWARFDie Die;

  friend std::reverse_iterator<llvm::DWARFDie::iterator>;
  friend bool operator==(const DWARFDie::iterator &LHS,
                         const DWARFDie::iterator &RHS);

public:
  iterator() = default;

  explicit iterator(DWARFDie D) : Die(D) {}

  iterator &operator++() {
    Die = Die.getSibling();
    return *this;
  }

  iterator &operator--() {
    Die = Die.getPreviousSibling();
    return *this;
  }

  const DWARFDie &operator*() const { return Die; }
};

inline bool operator==(const DWARFDie::iterator &LHS,
                       const DWARFDie::iterator &RHS) {
  return LHS.Die == RHS.Die;
}

inline bool operator!=(const DWARFDie::iterator &LHS,
                       const DWARFDie::iterator &RHS) {
  return !(LHS == RHS);
}

// These inline functions must follow the DWARFDie::iterator definition above
// as they use functions from that class.
inline DWARFDie::iterator DWARFDie::begin() const {
  return iterator(getFirstChild());
}

inline DWARFDie::iterator DWARFDie::end() const {
  return iterator(getLastChild());
}

inline iterator_range<DWARFDie::iterator> DWARFDie::children() const {
  return make_range(begin(), end());
}

} // end namespace llvm

namespace std {

template <>
class reverse_iterator<llvm::DWARFDie::iterator>
    : public llvm::iterator_facade_base<
          reverse_iterator<llvm::DWARFDie::iterator>,
          bidirectional_iterator_tag, const llvm::DWARFDie> {

private:
  llvm::DWARFDie Die;
  bool AtEnd;

public:
  reverse_iterator(llvm::DWARFDie::iterator It)
      : Die(It.Die), AtEnd(!It.Die.getPreviousSibling()) {
    if (!AtEnd)
      Die = Die.getPreviousSibling();
  }

  llvm::DWARFDie::iterator base() const {
    return llvm::DWARFDie::iterator(AtEnd ? Die : Die.getSibling());
  }

  reverse_iterator<llvm::DWARFDie::iterator> &operator++() {
    assert(!AtEnd && "Incrementing rend");
    llvm::DWARFDie D = Die.getPreviousSibling();
    if (D)
      Die = D;
    else
      AtEnd = true;
    return *this;
  }

  reverse_iterator<llvm::DWARFDie::iterator> &operator--() {
    if (AtEnd) {
      AtEnd = false;
      return *this;
    }
    Die = Die.getSibling();
    assert(!Die.isNULL() && "Decrementing rbegin");
    return *this;
  }

  const llvm::DWARFDie &operator*() const {
    assert(Die.isValid());
    return Die;
  }

  // FIXME: We should be able to specify the equals operator as a friend, but
  //        that causes the compiler to think the operator overload is ambiguous
  //        with the friend declaration and the actual definition as candidates.
  bool equals(const reverse_iterator<llvm::DWARFDie::iterator> &RHS) const {
    return Die == RHS.Die && AtEnd == RHS.AtEnd;
  }
};

} // namespace std

namespace llvm {

inline bool operator==(const std::reverse_iterator<DWARFDie::iterator> &LHS,
                       const std::reverse_iterator<DWARFDie::iterator> &RHS) {
  return LHS.equals(RHS);
}

inline bool operator!=(const std::reverse_iterator<DWARFDie::iterator> &LHS,
                       const std::reverse_iterator<DWARFDie::iterator> &RHS) {
  return !(LHS == RHS);
}

inline std::reverse_iterator<DWARFDie::iterator> DWARFDie::rbegin() const {
  return llvm::make_reverse_iterator(end());
}

inline std::reverse_iterator<DWARFDie::iterator> DWARFDie::rend() const {
  return llvm::make_reverse_iterator(begin());
}

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARFDIE_H
