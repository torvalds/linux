//===-- UniqueDWARFASTType.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "UniqueDWARFASTType.h"

#include "lldb/Symbol/Declaration.h"

bool UniqueDWARFASTTypeList::Find(const DWARFDIE &die,
                                  const lldb_private::Declaration &decl,
                                  const int32_t byte_size,
                                  UniqueDWARFASTType &entry) const {
  for (const UniqueDWARFASTType &udt : m_collection) {
    // Make sure the tags match
    if (udt.m_die.Tag() == die.Tag()) {
      // Validate byte sizes of both types only if both are valid.
      if (udt.m_byte_size < 0 || byte_size < 0 ||
          udt.m_byte_size == byte_size) {
        // Make sure the file and line match
        if (udt.m_declaration == decl) {
          // The type has the same name, and was defined on the same file and
          // line. Now verify all of the parent DIEs match.
          DWARFDIE parent_arg_die = die.GetParent();
          DWARFDIE parent_pos_die = udt.m_die.GetParent();
          bool match = true;
          bool done = false;
          while (!done && match && parent_arg_die && parent_pos_die) {
            const dw_tag_t parent_arg_tag = parent_arg_die.Tag();
            const dw_tag_t parent_pos_tag = parent_pos_die.Tag();
            if (parent_arg_tag == parent_pos_tag) {
              switch (parent_arg_tag) {
              case DW_TAG_class_type:
              case DW_TAG_structure_type:
              case DW_TAG_union_type:
              case DW_TAG_namespace: {
                const char *parent_arg_die_name = parent_arg_die.GetName();
                if (parent_arg_die_name ==
                    NULL) // Anonymous (i.e. no-name) struct
                {
                  match = false;
                } else {
                  const char *parent_pos_die_name = parent_pos_die.GetName();
                  if (parent_pos_die_name == NULL ||
                      ((parent_arg_die_name != parent_pos_die_name) &&
                       strcmp(parent_arg_die_name, parent_pos_die_name)))
                    match = false;
                }
              } break;

              case DW_TAG_compile_unit:
              case DW_TAG_partial_unit:
                done = true;
                break;
              }
            }
            parent_arg_die = parent_arg_die.GetParent();
            parent_pos_die = parent_pos_die.GetParent();
          }

          if (match) {
            entry = udt;
            return true;
          }
        }
      }
    }
  }
  return false;
}
