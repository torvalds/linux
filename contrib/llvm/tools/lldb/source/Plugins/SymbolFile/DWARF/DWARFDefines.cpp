//===-- DWARFDefines.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFDefines.h"
#include "lldb/Utility/ConstString.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace lldb_private {

const char *DW_TAG_value_to_name(uint32_t val) {
  static char invalid[100];

  if (val == 0)
    return "NULL";

  llvm::StringRef llvmstr = llvm::dwarf::TagString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_TAG constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_CHILDREN_value_to_name(uint8_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::ChildrenString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_CHILDREN constant: 0x%x",
             val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_AT_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::AttributeString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_AT constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_FORM_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::FormEncodingString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_FORM constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_OP_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::OperationEncodingString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_OP constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

DRC_class DW_OP_value_to_class(uint32_t val) {
  switch (val) {
  case 0x03:
    return DRC_ONEOPERAND;
  case 0x06:
    return DRC_ZEROOPERANDS;
  case 0x08:
    return DRC_ONEOPERAND;
  case 0x09:
    return DRC_ONEOPERAND;
  case 0x0a:
    return DRC_ONEOPERAND;
  case 0x0b:
    return DRC_ONEOPERAND;
  case 0x0c:
    return DRC_ONEOPERAND;
  case 0x0d:
    return DRC_ONEOPERAND;
  case 0x0e:
    return DRC_ONEOPERAND;
  case 0x0f:
    return DRC_ONEOPERAND;
  case 0x10:
    return DRC_ONEOPERAND;
  case 0x11:
    return DRC_ONEOPERAND;
  case 0x12:
    return DRC_ZEROOPERANDS;
  case 0x13:
    return DRC_ZEROOPERANDS;
  case 0x14:
    return DRC_ZEROOPERANDS;
  case 0x15:
    return DRC_ONEOPERAND;
  case 0x16:
    return DRC_ZEROOPERANDS;
  case 0x17:
    return DRC_ZEROOPERANDS;
  case 0x18:
    return DRC_ZEROOPERANDS;
  case 0x19:
    return DRC_ZEROOPERANDS;
  case 0x1a:
    return DRC_ZEROOPERANDS;
  case 0x1b:
    return DRC_ZEROOPERANDS;
  case 0x1c:
    return DRC_ZEROOPERANDS;
  case 0x1d:
    return DRC_ZEROOPERANDS;
  case 0x1e:
    return DRC_ZEROOPERANDS;
  case 0x1f:
    return DRC_ZEROOPERANDS;
  case 0x20:
    return DRC_ZEROOPERANDS;
  case 0x21:
    return DRC_ZEROOPERANDS;
  case 0x22:
    return DRC_ZEROOPERANDS;
  case 0x23:
    return DRC_ONEOPERAND;
  case 0x24:
    return DRC_ZEROOPERANDS;
  case 0x25:
    return DRC_ZEROOPERANDS;
  case 0x26:
    return DRC_ZEROOPERANDS;
  case 0x27:
    return DRC_ZEROOPERANDS;
  case 0x2f:
    return DRC_ONEOPERAND;
  case 0x28:
    return DRC_ONEOPERAND;
  case 0x29:
    return DRC_ZEROOPERANDS;
  case 0x2a:
    return DRC_ZEROOPERANDS;
  case 0x2b:
    return DRC_ZEROOPERANDS;
  case 0x2c:
    return DRC_ZEROOPERANDS;
  case 0x2d:
    return DRC_ZEROOPERANDS;
  case 0x2e:
    return DRC_ZEROOPERANDS;
  case 0x30:
    return DRC_ZEROOPERANDS;
  case 0x31:
    return DRC_ZEROOPERANDS;
  case 0x32:
    return DRC_ZEROOPERANDS;
  case 0x33:
    return DRC_ZEROOPERANDS;
  case 0x34:
    return DRC_ZEROOPERANDS;
  case 0x35:
    return DRC_ZEROOPERANDS;
  case 0x36:
    return DRC_ZEROOPERANDS;
  case 0x37:
    return DRC_ZEROOPERANDS;
  case 0x38:
    return DRC_ZEROOPERANDS;
  case 0x39:
    return DRC_ZEROOPERANDS;
  case 0x3a:
    return DRC_ZEROOPERANDS;
  case 0x3b:
    return DRC_ZEROOPERANDS;
  case 0x3c:
    return DRC_ZEROOPERANDS;
  case 0x3d:
    return DRC_ZEROOPERANDS;
  case 0x3e:
    return DRC_ZEROOPERANDS;
  case 0x3f:
    return DRC_ZEROOPERANDS;
  case 0x40:
    return DRC_ZEROOPERANDS;
  case 0x41:
    return DRC_ZEROOPERANDS;
  case 0x42:
    return DRC_ZEROOPERANDS;
  case 0x43:
    return DRC_ZEROOPERANDS;
  case 0x44:
    return DRC_ZEROOPERANDS;
  case 0x45:
    return DRC_ZEROOPERANDS;
  case 0x46:
    return DRC_ZEROOPERANDS;
  case 0x47:
    return DRC_ZEROOPERANDS;
  case 0x48:
    return DRC_ZEROOPERANDS;
  case 0x49:
    return DRC_ZEROOPERANDS;
  case 0x4a:
    return DRC_ZEROOPERANDS;
  case 0x4b:
    return DRC_ZEROOPERANDS;
  case 0x4c:
    return DRC_ZEROOPERANDS;
  case 0x4d:
    return DRC_ZEROOPERANDS;
  case 0x4e:
    return DRC_ZEROOPERANDS;
  case 0x4f:
    return DRC_ZEROOPERANDS;
  case 0x50:
    return DRC_ZEROOPERANDS;
  case 0x51:
    return DRC_ZEROOPERANDS;
  case 0x52:
    return DRC_ZEROOPERANDS;
  case 0x53:
    return DRC_ZEROOPERANDS;
  case 0x54:
    return DRC_ZEROOPERANDS;
  case 0x55:
    return DRC_ZEROOPERANDS;
  case 0x56:
    return DRC_ZEROOPERANDS;
  case 0x57:
    return DRC_ZEROOPERANDS;
  case 0x58:
    return DRC_ZEROOPERANDS;
  case 0x59:
    return DRC_ZEROOPERANDS;
  case 0x5a:
    return DRC_ZEROOPERANDS;
  case 0x5b:
    return DRC_ZEROOPERANDS;
  case 0x5c:
    return DRC_ZEROOPERANDS;
  case 0x5d:
    return DRC_ZEROOPERANDS;
  case 0x5e:
    return DRC_ZEROOPERANDS;
  case 0x5f:
    return DRC_ZEROOPERANDS;
  case 0x60:
    return DRC_ZEROOPERANDS;
  case 0x61:
    return DRC_ZEROOPERANDS;
  case 0x62:
    return DRC_ZEROOPERANDS;
  case 0x63:
    return DRC_ZEROOPERANDS;
  case 0x64:
    return DRC_ZEROOPERANDS;
  case 0x65:
    return DRC_ZEROOPERANDS;
  case 0x66:
    return DRC_ZEROOPERANDS;
  case 0x67:
    return DRC_ZEROOPERANDS;
  case 0x68:
    return DRC_ZEROOPERANDS;
  case 0x69:
    return DRC_ZEROOPERANDS;
  case 0x6a:
    return DRC_ZEROOPERANDS;
  case 0x6b:
    return DRC_ZEROOPERANDS;
  case 0x6c:
    return DRC_ZEROOPERANDS;
  case 0x6d:
    return DRC_ZEROOPERANDS;
  case 0x6e:
    return DRC_ZEROOPERANDS;
  case 0x6f:
    return DRC_ZEROOPERANDS;
  case 0x70:
    return DRC_ONEOPERAND;
  case 0x71:
    return DRC_ONEOPERAND;
  case 0x72:
    return DRC_ONEOPERAND;
  case 0x73:
    return DRC_ONEOPERAND;
  case 0x74:
    return DRC_ONEOPERAND;
  case 0x75:
    return DRC_ONEOPERAND;
  case 0x76:
    return DRC_ONEOPERAND;
  case 0x77:
    return DRC_ONEOPERAND;
  case 0x78:
    return DRC_ONEOPERAND;
  case 0x79:
    return DRC_ONEOPERAND;
  case 0x7a:
    return DRC_ONEOPERAND;
  case 0x7b:
    return DRC_ONEOPERAND;
  case 0x7c:
    return DRC_ONEOPERAND;
  case 0x7d:
    return DRC_ONEOPERAND;
  case 0x7e:
    return DRC_ONEOPERAND;
  case 0x7f:
    return DRC_ONEOPERAND;
  case 0x80:
    return DRC_ONEOPERAND;
  case 0x81:
    return DRC_ONEOPERAND;
  case 0x82:
    return DRC_ONEOPERAND;
  case 0x83:
    return DRC_ONEOPERAND;
  case 0x84:
    return DRC_ONEOPERAND;
  case 0x85:
    return DRC_ONEOPERAND;
  case 0x86:
    return DRC_ONEOPERAND;
  case 0x87:
    return DRC_ONEOPERAND;
  case 0x88:
    return DRC_ONEOPERAND;
  case 0x89:
    return DRC_ONEOPERAND;
  case 0x8a:
    return DRC_ONEOPERAND;
  case 0x8b:
    return DRC_ONEOPERAND;
  case 0x8c:
    return DRC_ONEOPERAND;
  case 0x8d:
    return DRC_ONEOPERAND;
  case 0x8e:
    return DRC_ONEOPERAND;
  case 0x8f:
    return DRC_ONEOPERAND;
  case 0x90:
    return DRC_ONEOPERAND;
  case 0x91:
    return DRC_ONEOPERAND;
  case 0x92:
    return DRC_TWOOPERANDS;
  case 0x93:
    return DRC_ONEOPERAND;
  case 0x94:
    return DRC_ONEOPERAND;
  case 0x95:
    return DRC_ONEOPERAND;
  case 0x96:
    return DRC_ZEROOPERANDS;
  case 0x97:
    return DRC_DWARFv3 | DRC_ZEROOPERANDS;
  case 0x98:
    return DRC_DWARFv3 | DRC_ONEOPERAND;
  case 0x99:
    return DRC_DWARFv3 | DRC_ONEOPERAND;
  case 0x9a:
    return DRC_DWARFv3 | DRC_ONEOPERAND;
  case 0xf0:
    return DRC_ZEROOPERANDS; /* DW_OP_APPLE_uninit */
  case 0xe0:
    return 0;
  case 0xff:
    return 0;
  default:
    return 0;
  }
}

const char *DW_ATE_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::AttributeEncodingString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_ATE constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_ACCESS_value_to_name(uint32_t val) {
  static char invalid[100];

  llvm::StringRef llvmstr = llvm::dwarf::AccessibilityString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_ACCESS constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_VIS_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::VisibilityString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_VIS constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_VIRTUALITY_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::VirtualityString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_VIRTUALITY constant: 0x%x",
             val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_LANG_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::LanguageString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_LANG constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_ID_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::CaseString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_ID constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_CC_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::ConventionString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_CC constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_INL_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::InlineCodeString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_INL constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_ORD_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::ArrayOrderString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_ORD constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_LNS_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::LNStandardString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_LNS constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_LNE_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::LNExtendedString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_LNE constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_MACINFO_value_to_name(uint32_t val) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::MacinfoString(val);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_MACINFO constant: 0x%x",
             val);
    return invalid;
  }
  return llvmstr.data();
}

const char *DW_CFA_value_to_name(uint32_t val, llvm::Triple::ArchType Arch) {
  static char invalid[100];
  llvm::StringRef llvmstr = llvm::dwarf::CallFrameString(val, Arch);
  if (llvmstr.empty()) {
    snprintf(invalid, sizeof(invalid), "Unknown DW_CFA constant: 0x%x", val);
    return invalid;
  }
  return llvmstr.data();
}

DW_TAG_CategoryEnum get_tag_category(uint16_t tag) {
  switch (tag) {
  case DW_TAG_array_type:
    return TagCategoryType;
  case DW_TAG_class_type:
    return TagCategoryType;
  case DW_TAG_entry_point:
    return TagCategoryProgram;
  case DW_TAG_enumeration_type:
    return TagCategoryType;
  case DW_TAG_formal_parameter:
    return TagCategoryVariable;
  case DW_TAG_imported_declaration:
    return TagCategoryProgram;
  case DW_TAG_label:
    return TagCategoryProgram;
  case DW_TAG_lexical_block:
    return TagCategoryProgram;
  case DW_TAG_member:
    return TagCategoryType;
  case DW_TAG_pointer_type:
    return TagCategoryType;
  case DW_TAG_reference_type:
    return TagCategoryType;
  case DW_TAG_compile_unit:
    return TagCategoryProgram;
  case DW_TAG_string_type:
    return TagCategoryType;
  case DW_TAG_structure_type:
    return TagCategoryType;
  case DW_TAG_subroutine_type:
    return TagCategoryType;
  case DW_TAG_typedef:
    return TagCategoryType;
  case DW_TAG_union_type:
    return TagCategoryType;
  case DW_TAG_unspecified_parameters:
    return TagCategoryVariable;
  case DW_TAG_variant:
    return TagCategoryType;
  case DW_TAG_common_block:
    return TagCategoryProgram;
  case DW_TAG_common_inclusion:
    return TagCategoryProgram;
  case DW_TAG_inheritance:
    return TagCategoryType;
  case DW_TAG_inlined_subroutine:
    return TagCategoryProgram;
  case DW_TAG_module:
    return TagCategoryProgram;
  case DW_TAG_ptr_to_member_type:
    return TagCategoryType;
  case DW_TAG_set_type:
    return TagCategoryType;
  case DW_TAG_subrange_type:
    return TagCategoryType;
  case DW_TAG_with_stmt:
    return TagCategoryProgram;
  case DW_TAG_access_declaration:
    return TagCategoryProgram;
  case DW_TAG_base_type:
    return TagCategoryType;
  case DW_TAG_catch_block:
    return TagCategoryProgram;
  case DW_TAG_const_type:
    return TagCategoryType;
  case DW_TAG_constant:
    return TagCategoryVariable;
  case DW_TAG_enumerator:
    return TagCategoryType;
  case DW_TAG_file_type:
    return TagCategoryType;
  case DW_TAG_friend:
    return TagCategoryType;
  case DW_TAG_namelist:
    return TagCategoryVariable;
  case DW_TAG_namelist_item:
    return TagCategoryVariable;
  case DW_TAG_packed_type:
    return TagCategoryType;
  case DW_TAG_subprogram:
    return TagCategoryProgram;
  case DW_TAG_template_type_parameter:
    return TagCategoryType;
  case DW_TAG_template_value_parameter:
    return TagCategoryType;
  case DW_TAG_GNU_template_parameter_pack:
    return TagCategoryType;
  case DW_TAG_thrown_type:
    return TagCategoryType;
  case DW_TAG_try_block:
    return TagCategoryProgram;
  case DW_TAG_variant_part:
    return TagCategoryType;
  case DW_TAG_variable:
    return TagCategoryVariable;
  case DW_TAG_volatile_type:
    return TagCategoryType;
  case DW_TAG_dwarf_procedure:
    return TagCategoryProgram;
  case DW_TAG_restrict_type:
    return TagCategoryType;
  case DW_TAG_interface_type:
    return TagCategoryType;
  case DW_TAG_namespace:
    return TagCategoryProgram;
  case DW_TAG_imported_module:
    return TagCategoryProgram;
  case DW_TAG_unspecified_type:
    return TagCategoryType;
  case DW_TAG_partial_unit:
    return TagCategoryProgram;
  case DW_TAG_imported_unit:
    return TagCategoryProgram;
  case DW_TAG_shared_type:
    return TagCategoryType;
  default:
    break;
  }
  return TagCategoryProgram;
}

} // namespace lldb_private
