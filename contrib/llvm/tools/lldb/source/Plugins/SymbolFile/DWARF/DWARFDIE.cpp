//===-- DWARFDIE.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFDIE.h"

#include "DWARFASTParser.h"
#include "DWARFDIECollection.h"
#include "DWARFDebugInfo.h"
#include "DWARFDebugInfoEntry.h"
#include "DWARFDeclContext.h"
#include "DWARFUnit.h"

using namespace lldb_private;

void DWARFDIE::ElaboratingDIEIterator::Next() {
  assert(!m_worklist.empty() && "Incrementing end iterator?");

  // Pop the current item from the list.
  DWARFDIE die = m_worklist.back();
  m_worklist.pop_back();

  // And add back any items that elaborate it.
  for (dw_attr_t attr : {DW_AT_specification, DW_AT_abstract_origin}) {
    if (DWARFDIE d = die.GetReferencedDIE(attr))
      if (m_seen.insert(die.GetID()).second)
        m_worklist.push_back(d);
  }
}

DWARFDIE
DWARFDIE::GetParent() const {
  if (IsValid())
    return DWARFDIE(m_cu, m_die->GetParent());
  else
    return DWARFDIE();
}

DWARFDIE
DWARFDIE::GetFirstChild() const {
  if (IsValid())
    return DWARFDIE(m_cu, m_die->GetFirstChild());
  else
    return DWARFDIE();
}

DWARFDIE
DWARFDIE::GetSibling() const {
  if (IsValid())
    return DWARFDIE(m_cu, m_die->GetSibling());
  else
    return DWARFDIE();
}

DWARFDIE
DWARFDIE::GetReferencedDIE(const dw_attr_t attr) const {
  const dw_offset_t die_offset =
      GetAttributeValueAsReference(attr, DW_INVALID_OFFSET);
  if (die_offset != DW_INVALID_OFFSET)
    return GetDIE(die_offset);
  else
    return DWARFDIE();
}

DWARFDIE
DWARFDIE::GetDIE(dw_offset_t die_offset) const {
  if (IsValid())
    return m_cu->GetDIE(die_offset);
  else
    return DWARFDIE();
}

DWARFDIE
DWARFDIE::GetAttributeValueAsReferenceDIE(const dw_attr_t attr) const {
  if (IsValid()) {
    DWARFUnit *cu = GetCU();
    SymbolFileDWARF *dwarf = cu->GetSymbolFileDWARF();
    const bool check_specification_or_abstract_origin = true;
    DWARFFormValue form_value;
    if (m_die->GetAttributeValue(dwarf, cu, attr, form_value, nullptr,
                                 check_specification_or_abstract_origin))
      return dwarf->GetDIE(DIERef(form_value));
  }
  return DWARFDIE();
}

DWARFDIE
DWARFDIE::LookupDeepestBlock(lldb::addr_t file_addr) const {
  if (IsValid()) {
    SymbolFileDWARF *dwarf = GetDWARF();
    DWARFUnit *cu = GetCU();
    DWARFDebugInfoEntry *function_die = nullptr;
    DWARFDebugInfoEntry *block_die = nullptr;
    if (m_die->LookupAddress(file_addr, dwarf, cu, &function_die, &block_die)) {
      if (block_die && block_die != function_die) {
        if (cu->ContainsDIEOffset(block_die->GetOffset()))
          return DWARFDIE(cu, block_die);
        else
          return DWARFDIE(dwarf->DebugInfo()->GetCompileUnit(
                              DIERef(cu->GetOffset(), block_die->GetOffset())),
                          block_die);
      }
    }
  }
  return DWARFDIE();
}

const char *DWARFDIE::GetMangledName() const {
  if (IsValid())
    return m_die->GetMangledName(GetDWARF(), m_cu);
  else
    return nullptr;
}

const char *DWARFDIE::GetPubname() const {
  if (IsValid())
    return m_die->GetPubname(GetDWARF(), m_cu);
  else
    return nullptr;
}

const char *DWARFDIE::GetQualifiedName(std::string &storage) const {
  if (IsValid())
    return m_die->GetQualifiedName(GetDWARF(), m_cu, storage);
  else
    return nullptr;
}

lldb_private::Type *DWARFDIE::ResolveType() const {
  if (IsValid())
    return GetDWARF()->ResolveType(*this, true);
  else
    return nullptr;
}

lldb_private::Type *DWARFDIE::ResolveTypeUID(const DIERef &die_ref) const {
  SymbolFileDWARF *dwarf = GetDWARF();
  if (dwarf)
    return dwarf->ResolveTypeUID(dwarf->GetDIE(die_ref), true);
  else
    return nullptr;
}

void DWARFDIE::GetDeclContextDIEs(DWARFDIECollection &decl_context_dies) const {
  if (IsValid()) {
    DWARFDIE parent_decl_ctx_die =
        m_die->GetParentDeclContextDIE(GetDWARF(), GetCU());
    if (parent_decl_ctx_die && parent_decl_ctx_die.GetDIE() != GetDIE()) {
      decl_context_dies.Append(parent_decl_ctx_die);
      parent_decl_ctx_die.GetDeclContextDIEs(decl_context_dies);
    }
  }
}

void DWARFDIE::GetDWARFDeclContext(DWARFDeclContext &dwarf_decl_ctx) const {
  if (IsValid()) {
    dwarf_decl_ctx.SetLanguage(GetLanguage());
    m_die->GetDWARFDeclContext(GetDWARF(), GetCU(), dwarf_decl_ctx);
  } else {
    dwarf_decl_ctx.Clear();
  }
}

void DWARFDIE::GetDeclContext(std::vector<CompilerContext> &context) const {
  const dw_tag_t tag = Tag();
  if (tag == DW_TAG_compile_unit || tag == DW_TAG_partial_unit)
    return;
  DWARFDIE parent = GetParent();
  if (parent)
    parent.GetDeclContext(context);
  switch (tag) {
  case DW_TAG_module:
    context.push_back(
        CompilerContext(CompilerContextKind::Module, ConstString(GetName())));
    break;
  case DW_TAG_namespace:
    context.push_back(CompilerContext(CompilerContextKind::Namespace,
                                      ConstString(GetName())));
    break;
  case DW_TAG_structure_type:
    context.push_back(CompilerContext(CompilerContextKind::Structure,
                                      ConstString(GetName())));
    break;
  case DW_TAG_union_type:
    context.push_back(
        CompilerContext(CompilerContextKind::Union, ConstString(GetName())));
    break;
  case DW_TAG_class_type:
    context.push_back(
        CompilerContext(CompilerContextKind::Class, ConstString(GetName())));
    break;
  case DW_TAG_enumeration_type:
    context.push_back(CompilerContext(CompilerContextKind::Enumeration,
                                      ConstString(GetName())));
    break;
  case DW_TAG_subprogram:
    context.push_back(CompilerContext(CompilerContextKind::Function,
                                      ConstString(GetPubname())));
    break;
  case DW_TAG_variable:
    context.push_back(CompilerContext(CompilerContextKind::Variable,
                                      ConstString(GetPubname())));
    break;
  case DW_TAG_typedef:
    context.push_back(
        CompilerContext(CompilerContextKind::Typedef, ConstString(GetName())));
    break;
  default:
    break;
  }
}

DWARFDIE
DWARFDIE::GetParentDeclContextDIE() const {
  if (IsValid())
    return m_die->GetParentDeclContextDIE(GetDWARF(), m_cu);
  else
    return DWARFDIE();
}

bool DWARFDIE::IsStructUnionOrClass() const {
  const dw_tag_t tag = Tag();
  return tag == DW_TAG_class_type || tag == DW_TAG_structure_type ||
         tag == DW_TAG_union_type;
}

bool DWARFDIE::IsMethod() const {
  for (DWARFDIE d: elaborating_dies())
    if (d.GetParent().IsStructUnionOrClass())
      return true;
  return false;
}

DWARFDIE
DWARFDIE::GetContainingDWOModuleDIE() const {
  if (IsValid()) {
    DWARFDIE top_module_die;
    // Now make sure this DIE is scoped in a DW_TAG_module tag and return true
    // if so
    for (DWARFDIE parent = GetParent(); parent.IsValid();
         parent = parent.GetParent()) {
      const dw_tag_t tag = parent.Tag();
      if (tag == DW_TAG_module)
        top_module_die = parent;
      else if (tag == DW_TAG_compile_unit || tag == DW_TAG_partial_unit)
        break;
    }

    return top_module_die;
  }
  return DWARFDIE();
}

lldb::ModuleSP DWARFDIE::GetContainingDWOModule() const {
  if (IsValid()) {
    DWARFDIE dwo_module_die = GetContainingDWOModuleDIE();

    if (dwo_module_die) {
      const char *module_name = dwo_module_die.GetName();
      if (module_name)
        return GetDWARF()->GetDWOModule(lldb_private::ConstString(module_name));
    }
  }
  return lldb::ModuleSP();
}

bool DWARFDIE::GetDIENamesAndRanges(
    const char *&name, const char *&mangled, DWARFRangeList &ranges,
    int &decl_file, int &decl_line, int &decl_column, int &call_file,
    int &call_line, int &call_column,
    lldb_private::DWARFExpression *frame_base) const {
  if (IsValid()) {
    return m_die->GetDIENamesAndRanges(
        GetDWARF(), GetCU(), name, mangled, ranges, decl_file, decl_line,
        decl_column, call_file, call_line, call_column, frame_base);
  } else
    return false;
}

CompilerDecl DWARFDIE::GetDecl() const {
  DWARFASTParser *dwarf_ast = GetDWARFParser();
  if (dwarf_ast)
    return dwarf_ast->GetDeclForUIDFromDWARF(*this);
  else
    return CompilerDecl();
}

CompilerDeclContext DWARFDIE::GetDeclContext() const {
  DWARFASTParser *dwarf_ast = GetDWARFParser();
  if (dwarf_ast)
    return dwarf_ast->GetDeclContextForUIDFromDWARF(*this);
  else
    return CompilerDeclContext();
}

CompilerDeclContext DWARFDIE::GetContainingDeclContext() const {
  DWARFASTParser *dwarf_ast = GetDWARFParser();
  if (dwarf_ast)
    return dwarf_ast->GetDeclContextContainingUIDFromDWARF(*this);
  else
    return CompilerDeclContext();
}
