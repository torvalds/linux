//===-- llvm/BinaryFormat/Dwarf.h ---Dwarf Constants-------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains constants used for implementing Dwarf
/// debug support.
///
/// For details on the Dwarf specfication see the latest DWARF Debugging
/// Information Format standard document on http://www.dwarfstd.org. This
/// file often includes support for non-released standard features.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_DWARF_H
#define LLVM_BINARYFORMAT_DWARF_H

#include "llvm/ADT/Optional.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadicDetails.h"
#include "llvm/ADT/Triple.h"

namespace llvm {
class StringRef;

namespace dwarf {

//===----------------------------------------------------------------------===//
// DWARF constants as gleaned from the DWARF Debugging Information Format V.5
// reference manual http://www.dwarfstd.org/.
//

// Do not mix the following two enumerations sets.  DW_TAG_invalid changes the
// enumeration base type.

enum LLVMConstants : uint32_t {
  // LLVM mock tags (see also llvm/BinaryFormat/Dwarf.def).
  DW_TAG_invalid = ~0U,        // Tag for invalid results.
  DW_VIRTUALITY_invalid = ~0U, // Virtuality for invalid results.
  DW_MACINFO_invalid = ~0U,    // Macinfo type for invalid results.

  // Other constants.
  DWARF_VERSION = 4,       // Default dwarf version we output.
  DW_PUBTYPES_VERSION = 2, // Section version number for .debug_pubtypes.
  DW_PUBNAMES_VERSION = 2, // Section version number for .debug_pubnames.
  DW_ARANGES_VERSION = 2,  // Section version number for .debug_aranges.
  // Identifiers we use to distinguish vendor extensions.
  DWARF_VENDOR_DWARF = 0, // Defined in v2 or later of the DWARF standard.
  DWARF_VENDOR_APPLE = 1,
  DWARF_VENDOR_BORLAND = 2,
  DWARF_VENDOR_GNU = 3,
  DWARF_VENDOR_GOOGLE = 4,
  DWARF_VENDOR_LLVM = 5,
  DWARF_VENDOR_MIPS = 6
};

/// Constants that define the DWARF format as 32 or 64 bit.
enum DwarfFormat : uint8_t { DWARF32, DWARF64 };

/// Special ID values that distinguish a CIE from a FDE in DWARF CFI.
/// Not inside an enum because a 64-bit value is needed.
/// @{
const uint32_t DW_CIE_ID = UINT32_MAX;
const uint64_t DW64_CIE_ID = UINT64_MAX;
/// @}

/// Identifier of an invalid DIE offset in the .debug_info section.
const uint32_t DW_INVALID_OFFSET = UINT32_MAX;

enum Tag : uint16_t {
#define HANDLE_DW_TAG(ID, NAME, VERSION, VENDOR) DW_TAG_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_TAG_lo_user = 0x4080,
  DW_TAG_hi_user = 0xffff,
  DW_TAG_user_base = 0x1000 ///< Recommended base for user tags.
};

inline bool isType(Tag T) {
  switch (T) {
  case DW_TAG_array_type:
  case DW_TAG_class_type:
  case DW_TAG_interface_type:
  case DW_TAG_enumeration_type:
  case DW_TAG_pointer_type:
  case DW_TAG_reference_type:
  case DW_TAG_rvalue_reference_type:
  case DW_TAG_string_type:
  case DW_TAG_structure_type:
  case DW_TAG_subroutine_type:
  case DW_TAG_union_type:
  case DW_TAG_ptr_to_member_type:
  case DW_TAG_set_type:
  case DW_TAG_subrange_type:
  case DW_TAG_base_type:
  case DW_TAG_const_type:
  case DW_TAG_file_type:
  case DW_TAG_packed_type:
  case DW_TAG_volatile_type:
  case DW_TAG_typedef:
    return true;
  default:
    return false;
  }
}

/// Attributes.
enum Attribute : uint16_t {
#define HANDLE_DW_AT(ID, NAME, VERSION, VENDOR) DW_AT_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_AT_lo_user = 0x2000,
  DW_AT_hi_user = 0x3fff,
};

enum Form : uint16_t {
#define HANDLE_DW_FORM(ID, NAME, VERSION, VENDOR) DW_FORM_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_FORM_lo_user = 0x1f00, ///< Not specified by DWARF.
};

enum LocationAtom {
#define HANDLE_DW_OP(ID, NAME, VERSION, VENDOR) DW_OP_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_OP_lo_user = 0xe0,
  DW_OP_hi_user = 0xff,
  DW_OP_LLVM_fragment = 0x1000 ///< Only used in LLVM metadata.
};

enum TypeKind : uint8_t {
#define HANDLE_DW_ATE(ID, NAME, VERSION, VENDOR) DW_ATE_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_ATE_lo_user = 0x80,
  DW_ATE_hi_user = 0xff
};

enum DecimalSignEncoding {
  // Decimal sign attribute values
  DW_DS_unsigned = 0x01,
  DW_DS_leading_overpunch = 0x02,
  DW_DS_trailing_overpunch = 0x03,
  DW_DS_leading_separate = 0x04,
  DW_DS_trailing_separate = 0x05
};

enum EndianityEncoding {
  // Endianity attribute values
#define HANDLE_DW_END(ID, NAME) DW_END_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_END_lo_user = 0x40,
  DW_END_hi_user = 0xff
};

enum AccessAttribute {
  // Accessibility codes
  DW_ACCESS_public = 0x01,
  DW_ACCESS_protected = 0x02,
  DW_ACCESS_private = 0x03
};

enum VisibilityAttribute {
  // Visibility codes
  DW_VIS_local = 0x01,
  DW_VIS_exported = 0x02,
  DW_VIS_qualified = 0x03
};

enum VirtualityAttribute {
#define HANDLE_DW_VIRTUALITY(ID, NAME) DW_VIRTUALITY_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_VIRTUALITY_max = 0x02
};

enum DefaultedMemberAttribute {
#define HANDLE_DW_DEFAULTED(ID, NAME) DW_DEFAULTED_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_DEFAULTED_max = 0x02
};

enum SourceLanguage {
#define HANDLE_DW_LANG(ID, NAME, LOWER_BOUND, VERSION, VENDOR)                 \
  DW_LANG_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_LANG_lo_user = 0x8000,
  DW_LANG_hi_user = 0xffff
};

enum CaseSensitivity {
  // Identifier case codes
  DW_ID_case_sensitive = 0x00,
  DW_ID_up_case = 0x01,
  DW_ID_down_case = 0x02,
  DW_ID_case_insensitive = 0x03
};

enum CallingConvention {
// Calling convention codes
#define HANDLE_DW_CC(ID, NAME) DW_CC_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_CC_lo_user = 0x40,
  DW_CC_hi_user = 0xff
};

enum InlineAttribute {
  // Inline codes
  DW_INL_not_inlined = 0x00,
  DW_INL_inlined = 0x01,
  DW_INL_declared_not_inlined = 0x02,
  DW_INL_declared_inlined = 0x03
};

enum ArrayDimensionOrdering {
  // Array ordering
  DW_ORD_row_major = 0x00,
  DW_ORD_col_major = 0x01
};

enum DiscriminantList {
  // Discriminant descriptor values
  DW_DSC_label = 0x00,
  DW_DSC_range = 0x01
};

/// Line Number Standard Opcode Encodings.
enum LineNumberOps : uint8_t {
#define HANDLE_DW_LNS(ID, NAME) DW_LNS_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// Line Number Extended Opcode Encodings.
enum LineNumberExtendedOps {
#define HANDLE_DW_LNE(ID, NAME) DW_LNE_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_LNE_lo_user = 0x80,
  DW_LNE_hi_user = 0xff
};

enum LineNumberEntryFormat {
#define HANDLE_DW_LNCT(ID, NAME) DW_LNCT_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_LNCT_lo_user = 0x2000,
  DW_LNCT_hi_user = 0x3fff,
};

enum MacinfoRecordType {
  // Macinfo Type Encodings
  DW_MACINFO_define = 0x01,
  DW_MACINFO_undef = 0x02,
  DW_MACINFO_start_file = 0x03,
  DW_MACINFO_end_file = 0x04,
  DW_MACINFO_vendor_ext = 0xff
};

/// DWARF v5 macro information entry type encodings.
enum MacroEntryType {
#define HANDLE_DW_MACRO(ID, NAME) DW_MACRO_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_MACRO_lo_user = 0xe0,
  DW_MACRO_hi_user = 0xff
};

/// DWARF v5 range list entry encoding values.
enum RangeListEntries {
#define HANDLE_DW_RLE(ID, NAME) DW_RLE_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// Call frame instruction encodings.
enum CallFrameInfo {
#define HANDLE_DW_CFA(ID, NAME) DW_CFA_##NAME = ID,
#define HANDLE_DW_CFA_PRED(ID, NAME, ARCH) DW_CFA_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_CFA_extended = 0x00,

  DW_CFA_lo_user = 0x1c,
  DW_CFA_hi_user = 0x3f
};

enum Constants {
  // Children flag
  DW_CHILDREN_no = 0x00,
  DW_CHILDREN_yes = 0x01,

  DW_EH_PE_absptr = 0x00,
  DW_EH_PE_omit = 0xff,
  DW_EH_PE_uleb128 = 0x01,
  DW_EH_PE_udata2 = 0x02,
  DW_EH_PE_udata4 = 0x03,
  DW_EH_PE_udata8 = 0x04,
  DW_EH_PE_sleb128 = 0x09,
  DW_EH_PE_sdata2 = 0x0A,
  DW_EH_PE_sdata4 = 0x0B,
  DW_EH_PE_sdata8 = 0x0C,
  DW_EH_PE_signed = 0x08,
  DW_EH_PE_pcrel = 0x10,
  DW_EH_PE_textrel = 0x20,
  DW_EH_PE_datarel = 0x30,
  DW_EH_PE_funcrel = 0x40,
  DW_EH_PE_aligned = 0x50,
  DW_EH_PE_indirect = 0x80
};

/// Constants for location lists in DWARF v5.
enum LocationListEntry : unsigned char {
  DW_LLE_end_of_list = 0x00,
  DW_LLE_base_addressx = 0x01,
  DW_LLE_startx_endx = 0x02,
  DW_LLE_startx_length = 0x03,
  DW_LLE_offset_pair = 0x04,
  DW_LLE_default_location = 0x05,
  DW_LLE_base_address = 0x06,
  DW_LLE_start_end = 0x07,
  DW_LLE_start_length = 0x08
};

/// Constants for the DW_APPLE_PROPERTY_attributes attribute.
/// Keep this list in sync with clang's DeclSpec.h ObjCPropertyAttributeKind!
enum ApplePropertyAttributes {
#define HANDLE_DW_APPLE_PROPERTY(ID, NAME) DW_APPLE_PROPERTY_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
};

/// Constants for unit types in DWARF v5.
enum UnitType : unsigned char {
#define HANDLE_DW_UT(ID, NAME) DW_UT_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_UT_lo_user = 0x80,
  DW_UT_hi_user = 0xff
};

enum Index {
#define HANDLE_DW_IDX(ID, NAME) DW_IDX_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_IDX_lo_user = 0x2000,
  DW_IDX_hi_user = 0x3fff
};

inline bool isUnitType(uint8_t UnitType) {
  switch (UnitType) {
  case DW_UT_compile:
  case DW_UT_type:
  case DW_UT_partial:
  case DW_UT_skeleton:
  case DW_UT_split_compile:
  case DW_UT_split_type:
    return true;
  default:
    return false;
  }
}

inline bool isUnitType(dwarf::Tag T) {
  switch (T) {
  case DW_TAG_compile_unit:
  case DW_TAG_type_unit:
  case DW_TAG_partial_unit:
  case DW_TAG_skeleton_unit:
    return true;
  default:
    return false;
  }
}

// Constants for the DWARF v5 Accelerator Table Proposal
enum AcceleratorTable {
  // Data layout descriptors.
  DW_ATOM_null = 0u,       ///  Marker as the end of a list of atoms.
  DW_ATOM_die_offset = 1u, // DIE offset in the debug_info section.
  DW_ATOM_cu_offset = 2u, // Offset of the compile unit header that contains the
                          // item in question.
  DW_ATOM_die_tag = 3u,   // A tag entry.
  DW_ATOM_type_flags = 4u, // Set of flags for a type.

  DW_ATOM_type_type_flags = 5u, // Dsymutil type extension.
  DW_ATOM_qual_name_hash = 6u,  // Dsymutil qualified hash extension.

  // DW_ATOM_type_flags values.

  // Always set for C++, only set for ObjC if this is the @implementation for a
  // class.
  DW_FLAG_type_implementation = 2u,

  // Hash functions.

  // Daniel J. Bernstein hash.
  DW_hash_function_djb = 0u
};

// Constants for the GNU pubnames/pubtypes extensions supporting gdb index.
enum GDBIndexEntryKind {
  GIEK_NONE,
  GIEK_TYPE,
  GIEK_VARIABLE,
  GIEK_FUNCTION,
  GIEK_OTHER,
  GIEK_UNUSED5,
  GIEK_UNUSED6,
  GIEK_UNUSED7
};

enum GDBIndexEntryLinkage { GIEL_EXTERNAL, GIEL_STATIC };

/// \defgroup DwarfConstantsDumping Dwarf constants dumping functions
///
/// All these functions map their argument's value back to the
/// corresponding enumerator name or return an empty StringRef if the value
/// isn't known.
///
/// @{
StringRef TagString(unsigned Tag);
StringRef ChildrenString(unsigned Children);
StringRef AttributeString(unsigned Attribute);
StringRef FormEncodingString(unsigned Encoding);
StringRef OperationEncodingString(unsigned Encoding);
StringRef AttributeEncodingString(unsigned Encoding);
StringRef DecimalSignString(unsigned Sign);
StringRef EndianityString(unsigned Endian);
StringRef AccessibilityString(unsigned Access);
StringRef VisibilityString(unsigned Visibility);
StringRef VirtualityString(unsigned Virtuality);
StringRef LanguageString(unsigned Language);
StringRef CaseString(unsigned Case);
StringRef ConventionString(unsigned Convention);
StringRef InlineCodeString(unsigned Code);
StringRef ArrayOrderString(unsigned Order);
StringRef LNStandardString(unsigned Standard);
StringRef LNExtendedString(unsigned Encoding);
StringRef MacinfoString(unsigned Encoding);
StringRef RangeListEncodingString(unsigned Encoding);
StringRef CallFrameString(unsigned Encoding, Triple::ArchType Arch);
StringRef ApplePropertyString(unsigned);
StringRef UnitTypeString(unsigned);
StringRef AtomTypeString(unsigned Atom);
StringRef GDBIndexEntryKindString(GDBIndexEntryKind Kind);
StringRef GDBIndexEntryLinkageString(GDBIndexEntryLinkage Linkage);
StringRef IndexString(unsigned Idx);
/// @}

/// \defgroup DwarfConstantsParsing Dwarf constants parsing functions
///
/// These functions map their strings back to the corresponding enumeration
/// value or return 0 if there is none, except for these exceptions:
///
/// \li \a getTag() returns \a DW_TAG_invalid on invalid input.
/// \li \a getVirtuality() returns \a DW_VIRTUALITY_invalid on invalid input.
/// \li \a getMacinfo() returns \a DW_MACINFO_invalid on invalid input.
///
/// @{
unsigned getTag(StringRef TagString);
unsigned getOperationEncoding(StringRef OperationEncodingString);
unsigned getVirtuality(StringRef VirtualityString);
unsigned getLanguage(StringRef LanguageString);
unsigned getCallingConvention(StringRef LanguageString);
unsigned getAttributeEncoding(StringRef EncodingString);
unsigned getMacinfo(StringRef MacinfoString);
/// @}

/// \defgroup DwarfConstantsVersioning Dwarf version for constants
///
/// For constants defined by DWARF, returns the DWARF version when the constant
/// was first defined. For vendor extensions, if there is a version-related
/// policy for when to emit it, returns a version number for that policy.
/// Otherwise returns 0.
///
/// @{
unsigned TagVersion(Tag T);
unsigned AttributeVersion(Attribute A);
unsigned FormVersion(Form F);
unsigned OperationVersion(LocationAtom O);
unsigned AttributeEncodingVersion(TypeKind E);
unsigned LanguageVersion(SourceLanguage L);
/// @}

/// \defgroup DwarfConstantsVendor Dwarf "vendor" for constants
///
/// These functions return an identifier describing "who" defined the constant,
/// either the DWARF standard itself or the vendor who defined the extension.
///
/// @{
unsigned TagVendor(Tag T);
unsigned AttributeVendor(Attribute A);
unsigned FormVendor(Form F);
unsigned OperationVendor(LocationAtom O);
unsigned AttributeEncodingVendor(TypeKind E);
unsigned LanguageVendor(SourceLanguage L);
/// @}

Optional<unsigned> LanguageLowerBound(SourceLanguage L);

/// A helper struct providing information about the byte size of DW_FORM
/// values that vary in size depending on the DWARF version, address byte
/// size, or DWARF32/DWARF64.
struct FormParams {
  uint16_t Version;
  uint8_t AddrSize;
  DwarfFormat Format;

  /// The definition of the size of form DW_FORM_ref_addr depends on the
  /// version. In DWARF v2 it's the size of an address; after that, it's the
  /// size of a reference.
  uint8_t getRefAddrByteSize() const {
    if (Version == 2)
      return AddrSize;
    return getDwarfOffsetByteSize();
  }

  /// The size of a reference is determined by the DWARF 32/64-bit format.
  uint8_t getDwarfOffsetByteSize() const {
    switch (Format) {
    case DwarfFormat::DWARF32:
      return 4;
    case DwarfFormat::DWARF64:
      return 8;
    }
    llvm_unreachable("Invalid Format value");
  }

  explicit operator bool() const { return Version && AddrSize; }
};

/// Get the fixed byte size for a given form.
///
/// If the form has a fixed byte size, then an Optional with a value will be
/// returned. If the form is always encoded using a variable length storage
/// format (ULEB or SLEB numbers or blocks) then None will be returned.
///
/// \param Form DWARF form to get the fixed byte size for.
/// \param Params DWARF parameters to help interpret forms.
/// \returns Optional<uint8_t> value with the fixed byte size or None if
/// \p Form doesn't have a fixed byte size.
Optional<uint8_t> getFixedFormByteSize(dwarf::Form Form, FormParams Params);

/// Tells whether the specified form is defined in the specified version,
/// or is an extension if extensions are allowed.
bool isValidFormForVersion(Form F, unsigned Version, bool ExtensionsOk = true);

/// Returns the symbolic string representing Val when used as a value
/// for attribute Attr.
StringRef AttributeValueString(uint16_t Attr, unsigned Val);

/// Returns the symbolic string representing Val when used as a value
/// for atom Atom.
StringRef AtomValueString(uint16_t Atom, unsigned Val);

/// Describes an entry of the various gnu_pub* debug sections.
///
/// The gnu_pub* kind looks like:
///
/// 0-3  reserved
/// 4-6  symbol kind
/// 7    0 == global, 1 == static
///
/// A gdb_index descriptor includes the above kind, shifted 24 bits up with the
/// offset of the cu within the debug_info section stored in those 24 bits.
struct PubIndexEntryDescriptor {
  GDBIndexEntryKind Kind;
  GDBIndexEntryLinkage Linkage;
  PubIndexEntryDescriptor(GDBIndexEntryKind Kind, GDBIndexEntryLinkage Linkage)
      : Kind(Kind), Linkage(Linkage) {}
  /* implicit */ PubIndexEntryDescriptor(GDBIndexEntryKind Kind)
      : Kind(Kind), Linkage(GIEL_EXTERNAL) {}
  explicit PubIndexEntryDescriptor(uint8_t Value)
      : Kind(
            static_cast<GDBIndexEntryKind>((Value & KIND_MASK) >> KIND_OFFSET)),
        Linkage(static_cast<GDBIndexEntryLinkage>((Value & LINKAGE_MASK) >>
                                                  LINKAGE_OFFSET)) {}
  uint8_t toBits() const {
    return Kind << KIND_OFFSET | Linkage << LINKAGE_OFFSET;
  }

private:
  enum {
    KIND_OFFSET = 4,
    KIND_MASK = 7 << KIND_OFFSET,
    LINKAGE_OFFSET = 7,
    LINKAGE_MASK = 1 << LINKAGE_OFFSET
  };
};

template <typename Enum> struct EnumTraits : public std::false_type {};

template <> struct EnumTraits<Attribute> : public std::true_type {
  static constexpr char Type[3] = "AT";
  static constexpr StringRef (*StringFn)(unsigned) = &AttributeString;
};

template <> struct EnumTraits<Form> : public std::true_type {
  static constexpr char Type[5] = "FORM";
  static constexpr StringRef (*StringFn)(unsigned) = &FormEncodingString;
};

template <> struct EnumTraits<Index> : public std::true_type {
  static constexpr char Type[4] = "IDX";
  static constexpr StringRef (*StringFn)(unsigned) = &IndexString;
};

template <> struct EnumTraits<Tag> : public std::true_type {
  static constexpr char Type[4] = "TAG";
  static constexpr StringRef (*StringFn)(unsigned) = &TagString;
};
} // End of namespace dwarf

/// Dwarf constants format_provider
///
/// Specialization of the format_provider template for dwarf enums. Unlike the
/// dumping functions above, these format unknown enumerator values as
/// DW_TYPE_unknown_1234 (e.g. DW_TAG_unknown_ffff).
template <typename Enum>
struct format_provider<
    Enum, typename std::enable_if<dwarf::EnumTraits<Enum>::value>::type> {
  static void format(const Enum &E, raw_ostream &OS, StringRef Style) {
    StringRef Str = dwarf::EnumTraits<Enum>::StringFn(E);
    if (Str.empty()) {
      OS << "DW_" << dwarf::EnumTraits<Enum>::Type << "_unknown_"
         << llvm::format("%x", E);
    } else
      OS << Str;
  }
};
} // End of namespace llvm

#endif
