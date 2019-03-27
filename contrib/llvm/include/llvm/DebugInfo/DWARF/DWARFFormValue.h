//===- DWARFFormValue.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARFFORMVALUE_H
#define LLVM_DEBUGINFO_DWARFFORMVALUE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include <cstdint>

namespace llvm {

class DWARFContext;
class DWARFUnit;
class raw_ostream;

class DWARFFormValue {
public:
  enum FormClass {
    FC_Unknown,
    FC_Address,
    FC_Block,
    FC_Constant,
    FC_String,
    FC_Flag,
    FC_Reference,
    FC_Indirect,
    FC_SectionOffset,
    FC_Exprloc
  };

private:
  struct ValueType {
    ValueType() { uval = 0; }

    union {
      uint64_t uval;
      int64_t sval;
      const char *cstr;
    };
    const uint8_t *data = nullptr;
    uint64_t SectionIndex;      /// Section index for reference forms.
  };

  dwarf::Form Form;             /// Form for this value.
  ValueType Value;              /// Contains all data for the form.
  const DWARFUnit *U = nullptr; /// Remember the DWARFUnit at extract time.
  const DWARFContext *C = nullptr; /// Context for extract time.
public:
  DWARFFormValue(dwarf::Form F = dwarf::Form(0)) : Form(F) {}

  dwarf::Form getForm() const { return Form; }
  uint64_t getRawUValue() const { return Value.uval; }
  void setForm(dwarf::Form F) { Form = F; }
  void setUValue(uint64_t V) { Value.uval = V; }
  void setSValue(int64_t V) { Value.sval = V; }
  void setPValue(const char *V) { Value.cstr = V; }

  void setBlockValue(const ArrayRef<uint8_t> &Data) {
    Value.data = Data.data();
    setUValue(Data.size());
  }

  bool isFormClass(FormClass FC) const;
  const DWARFUnit *getUnit() const { return U; }
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts = DIDumpOptions()) const;
  void dumpSectionedAddress(raw_ostream &OS, DIDumpOptions DumpOpts,
                            SectionedAddress SA) const;
  static void dumpAddressSection(const DWARFObject &Obj, raw_ostream &OS,
                                 DIDumpOptions DumpOpts, uint64_t SectionIndex);

  /// Extracts a value in \p Data at offset \p *OffsetPtr. The information
  /// in \p FormParams is needed to interpret some forms. The optional
  /// \p Context and \p Unit allows extracting information if the form refers
  /// to other sections (e.g., .debug_str).
  bool extractValue(const DWARFDataExtractor &Data, uint32_t *OffsetPtr,
                    dwarf::FormParams FormParams,
                    const DWARFContext *Context = nullptr,
                    const DWARFUnit *Unit = nullptr);

  bool extractValue(const DWARFDataExtractor &Data, uint32_t *OffsetPtr,
                    dwarf::FormParams FormParams, const DWARFUnit *U) {
    return extractValue(Data, OffsetPtr, FormParams, nullptr, U);
  }

  bool isInlinedCStr() const {
    return Value.data != nullptr && Value.data == (const uint8_t *)Value.cstr;
  }

  /// getAsFoo functions below return the extracted value as Foo if only
  /// DWARFFormValue has form class is suitable for representing Foo.
  Optional<uint64_t> getAsReference() const;
  Optional<uint64_t> getAsUnsignedConstant() const;
  Optional<int64_t> getAsSignedConstant() const;
  Optional<const char *> getAsCString() const;
  Optional<uint64_t> getAsAddress() const;
  Optional<SectionedAddress> getAsSectionedAddress() const;
  Optional<uint64_t> getAsSectionOffset() const;
  Optional<ArrayRef<uint8_t>> getAsBlock() const;
  Optional<uint64_t> getAsCStringOffset() const;
  Optional<uint64_t> getAsReferenceUVal() const;

  /// Skip a form's value in \p DebugInfoData at the offset specified by
  /// \p OffsetPtr.
  ///
  /// Skips the bytes for the current form and updates the offset.
  ///
  /// \param DebugInfoData The data where we want to skip the value.
  /// \param OffsetPtr A reference to the offset that will be updated.
  /// \param Params DWARF parameters to help interpret forms.
  /// \returns true on success, false if the form was not skipped.
  bool skipValue(DataExtractor DebugInfoData, uint32_t *OffsetPtr,
                 const dwarf::FormParams Params) const {
    return DWARFFormValue::skipValue(Form, DebugInfoData, OffsetPtr, Params);
  }

  /// Skip a form's value in \p DebugInfoData at the offset specified by
  /// \p OffsetPtr.
  ///
  /// Skips the bytes for the specified form and updates the offset.
  ///
  /// \param Form The DW_FORM enumeration that indicates the form to skip.
  /// \param DebugInfoData The data where we want to skip the value.
  /// \param OffsetPtr A reference to the offset that will be updated.
  /// \param FormParams DWARF parameters to help interpret forms.
  /// \returns true on success, false if the form was not skipped.
  static bool skipValue(dwarf::Form Form, DataExtractor DebugInfoData,
                        uint32_t *OffsetPtr,
                        const dwarf::FormParams FormParams);

private:
  void dumpString(raw_ostream &OS) const;
};

namespace dwarf {

/// Take an optional DWARFFormValue and try to extract a string value from it.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and was a string.
inline Optional<const char *> toString(const Optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsCString();
  return None;
}

/// Take an optional DWARFFormValue and extract a string value from it.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the string value or Default if the V doesn't have a value or the
/// form value's encoding wasn't a string.
inline const char *toString(const Optional<DWARFFormValue> &V,
                            const char *Default) {
  return toString(V).getValueOr(Default);
}

/// Take an optional DWARFFormValue and try to extract an unsigned constant.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a unsigned constant form.
inline Optional<uint64_t> toUnsigned(const Optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsUnsignedConstant();
  return None;
}

/// Take an optional DWARFFormValue and extract a unsigned constant.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted unsigned value or Default if the V doesn't have a
/// value or the form value's encoding wasn't an unsigned constant form.
inline uint64_t toUnsigned(const Optional<DWARFFormValue> &V,
                           uint64_t Default) {
  return toUnsigned(V).getValueOr(Default);
}

/// Take an optional DWARFFormValue and try to extract an reference.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a reference form.
inline Optional<uint64_t> toReference(const Optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsReference();
  return None;
}

/// Take an optional DWARFFormValue and extract a reference.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted reference value or Default if the V doesn't have a
/// value or the form value's encoding wasn't a reference form.
inline uint64_t toReference(const Optional<DWARFFormValue> &V,
                            uint64_t Default) {
  return toReference(V).getValueOr(Default);
}

/// Take an optional DWARFFormValue and try to extract an signed constant.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a signed constant form.
inline Optional<int64_t> toSigned(const Optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSignedConstant();
  return None;
}

/// Take an optional DWARFFormValue and extract a signed integer.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted signed integer value or Default if the V doesn't
/// have a value or the form value's encoding wasn't a signed integer form.
inline int64_t toSigned(const Optional<DWARFFormValue> &V, int64_t Default) {
  return toSigned(V).getValueOr(Default);
}

/// Take an optional DWARFFormValue and try to extract an address.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a address form.
inline Optional<uint64_t> toAddress(const Optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsAddress();
  return None;
}

inline Optional<SectionedAddress>
toSectionedAddress(const Optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSectionedAddress();
  return None;
}

/// Take an optional DWARFFormValue and extract a address.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted address value or Default if the V doesn't have a
/// value or the form value's encoding wasn't an address form.
inline uint64_t toAddress(const Optional<DWARFFormValue> &V, uint64_t Default) {
  return toAddress(V).getValueOr(Default);
}

/// Take an optional DWARFFormValue and try to extract an section offset.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a section offset form.
inline Optional<uint64_t> toSectionOffset(const Optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsSectionOffset();
  return None;
}

/// Take an optional DWARFFormValue and extract a section offset.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \param Default the default value to return in case of failure.
/// \returns the extracted section offset value or Default if the V doesn't
/// have a value or the form value's encoding wasn't a section offset form.
inline uint64_t toSectionOffset(const Optional<DWARFFormValue> &V,
                                uint64_t Default) {
  return toSectionOffset(V).getValueOr(Default);
}

/// Take an optional DWARFFormValue and try to extract block data.
///
/// \param V and optional DWARFFormValue to attempt to extract the value from.
/// \returns an optional value that contains a value if the form value
/// was valid and has a block form.
inline Optional<ArrayRef<uint8_t>> toBlock(const Optional<DWARFFormValue> &V) {
  if (V)
    return V->getAsBlock();
  return None;
}

} // end namespace dwarf

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARFFORMVALUE_H
