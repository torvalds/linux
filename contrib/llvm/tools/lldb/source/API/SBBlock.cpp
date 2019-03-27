//===-- SBBlock.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBBlock.h"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBValue.h"
#include "lldb/Core/AddressRange.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

SBBlock::SBBlock() : m_opaque_ptr(NULL) {}

SBBlock::SBBlock(lldb_private::Block *lldb_object_ptr)
    : m_opaque_ptr(lldb_object_ptr) {}

SBBlock::SBBlock(const SBBlock &rhs) : m_opaque_ptr(rhs.m_opaque_ptr) {}

const SBBlock &SBBlock::operator=(const SBBlock &rhs) {
  m_opaque_ptr = rhs.m_opaque_ptr;
  return *this;
}

SBBlock::~SBBlock() { m_opaque_ptr = NULL; }

bool SBBlock::IsValid() const { return m_opaque_ptr != NULL; }

bool SBBlock::IsInlined() const {
  if (m_opaque_ptr)
    return m_opaque_ptr->GetInlinedFunctionInfo() != NULL;
  return false;
}

const char *SBBlock::GetInlinedName() const {
  if (m_opaque_ptr) {
    const InlineFunctionInfo *inlined_info =
        m_opaque_ptr->GetInlinedFunctionInfo();
    if (inlined_info) {
      Function *function = m_opaque_ptr->CalculateSymbolContextFunction();
      LanguageType language;
      if (function)
        language = function->GetLanguage();
      else
        language = lldb::eLanguageTypeUnknown;
      return inlined_info->GetName(language).AsCString(NULL);
    }
  }
  return NULL;
}

SBFileSpec SBBlock::GetInlinedCallSiteFile() const {
  SBFileSpec sb_file;
  if (m_opaque_ptr) {
    const InlineFunctionInfo *inlined_info =
        m_opaque_ptr->GetInlinedFunctionInfo();
    if (inlined_info)
      sb_file.SetFileSpec(inlined_info->GetCallSite().GetFile());
  }
  return sb_file;
}

uint32_t SBBlock::GetInlinedCallSiteLine() const {
  if (m_opaque_ptr) {
    const InlineFunctionInfo *inlined_info =
        m_opaque_ptr->GetInlinedFunctionInfo();
    if (inlined_info)
      return inlined_info->GetCallSite().GetLine();
  }
  return 0;
}

uint32_t SBBlock::GetInlinedCallSiteColumn() const {
  if (m_opaque_ptr) {
    const InlineFunctionInfo *inlined_info =
        m_opaque_ptr->GetInlinedFunctionInfo();
    if (inlined_info)
      return inlined_info->GetCallSite().GetColumn();
  }
  return 0;
}

void SBBlock::AppendVariables(bool can_create, bool get_parent_variables,
                              lldb_private::VariableList *var_list) {
  if (IsValid()) {
    bool show_inline = true;
    m_opaque_ptr->AppendVariables(can_create, get_parent_variables, show_inline,
                                  [](Variable *) { return true; }, var_list);
  }
}

SBBlock SBBlock::GetParent() {
  SBBlock sb_block;
  if (m_opaque_ptr)
    sb_block.m_opaque_ptr = m_opaque_ptr->GetParent();
  return sb_block;
}

lldb::SBBlock SBBlock::GetContainingInlinedBlock() {
  SBBlock sb_block;
  if (m_opaque_ptr)
    sb_block.m_opaque_ptr = m_opaque_ptr->GetContainingInlinedBlock();
  return sb_block;
}

SBBlock SBBlock::GetSibling() {
  SBBlock sb_block;
  if (m_opaque_ptr)
    sb_block.m_opaque_ptr = m_opaque_ptr->GetSibling();
  return sb_block;
}

SBBlock SBBlock::GetFirstChild() {
  SBBlock sb_block;
  if (m_opaque_ptr)
    sb_block.m_opaque_ptr = m_opaque_ptr->GetFirstChild();
  return sb_block;
}

lldb_private::Block *SBBlock::GetPtr() { return m_opaque_ptr; }

void SBBlock::SetPtr(lldb_private::Block *block) { m_opaque_ptr = block; }

bool SBBlock::GetDescription(SBStream &description) {
  Stream &strm = description.ref();

  if (m_opaque_ptr) {
    lldb::user_id_t id = m_opaque_ptr->GetID();
    strm.Printf("Block: {id: %" PRIu64 "} ", id);
    if (IsInlined()) {
      strm.Printf(" (inlined, '%s') ", GetInlinedName());
    }
    lldb_private::SymbolContext sc;
    m_opaque_ptr->CalculateSymbolContext(&sc);
    if (sc.function) {
      m_opaque_ptr->DumpAddressRanges(
          &strm,
          sc.function->GetAddressRange().GetBaseAddress().GetFileAddress());
    }
  } else
    strm.PutCString("No value");

  return true;
}

uint32_t SBBlock::GetNumRanges() {
  if (m_opaque_ptr)
    return m_opaque_ptr->GetNumRanges();
  return 0;
}

lldb::SBAddress SBBlock::GetRangeStartAddress(uint32_t idx) {
  lldb::SBAddress sb_addr;
  if (m_opaque_ptr) {
    AddressRange range;
    if (m_opaque_ptr->GetRangeAtIndex(idx, range)) {
      sb_addr.ref() = range.GetBaseAddress();
    }
  }
  return sb_addr;
}

lldb::SBAddress SBBlock::GetRangeEndAddress(uint32_t idx) {
  lldb::SBAddress sb_addr;
  if (m_opaque_ptr) {
    AddressRange range;
    if (m_opaque_ptr->GetRangeAtIndex(idx, range)) {
      sb_addr.ref() = range.GetBaseAddress();
      sb_addr.ref().Slide(range.GetByteSize());
    }
  }
  return sb_addr;
}

uint32_t SBBlock::GetRangeIndexForBlockAddress(lldb::SBAddress block_addr) {
  if (m_opaque_ptr && block_addr.IsValid()) {
    return m_opaque_ptr->GetRangeIndexContainingAddress(block_addr.ref());
  }

  return UINT32_MAX;
}

lldb::SBValueList SBBlock::GetVariables(lldb::SBFrame &frame, bool arguments,
                                        bool locals, bool statics,
                                        lldb::DynamicValueType use_dynamic) {
  Block *block = GetPtr();
  SBValueList value_list;
  if (block) {
    StackFrameSP frame_sp(frame.GetFrameSP());
    VariableListSP variable_list_sp(block->GetBlockVariableList(true));

    if (variable_list_sp) {
      const size_t num_variables = variable_list_sp->GetSize();
      if (num_variables) {
        for (size_t i = 0; i < num_variables; ++i) {
          VariableSP variable_sp(variable_list_sp->GetVariableAtIndex(i));
          if (variable_sp) {
            bool add_variable = false;
            switch (variable_sp->GetScope()) {
            case eValueTypeVariableGlobal:
            case eValueTypeVariableStatic:
            case eValueTypeVariableThreadLocal:
              add_variable = statics;
              break;

            case eValueTypeVariableArgument:
              add_variable = arguments;
              break;

            case eValueTypeVariableLocal:
              add_variable = locals;
              break;

            default:
              break;
            }
            if (add_variable) {
              if (frame_sp) {
                lldb::ValueObjectSP valobj_sp(
                    frame_sp->GetValueObjectForFrameVariable(variable_sp,
                                                             eNoDynamicValues));
                SBValue value_sb;
                value_sb.SetSP(valobj_sp, use_dynamic);
                value_list.Append(value_sb);
              }
            }
          }
        }
      }
    }
  }
  return value_list;
}

lldb::SBValueList SBBlock::GetVariables(lldb::SBTarget &target, bool arguments,
                                        bool locals, bool statics) {
  Block *block = GetPtr();

  SBValueList value_list;
  if (block) {
    TargetSP target_sp(target.GetSP());

    VariableListSP variable_list_sp(block->GetBlockVariableList(true));

    if (variable_list_sp) {
      const size_t num_variables = variable_list_sp->GetSize();
      if (num_variables) {
        for (size_t i = 0; i < num_variables; ++i) {
          VariableSP variable_sp(variable_list_sp->GetVariableAtIndex(i));
          if (variable_sp) {
            bool add_variable = false;
            switch (variable_sp->GetScope()) {
            case eValueTypeVariableGlobal:
            case eValueTypeVariableStatic:
            case eValueTypeVariableThreadLocal:
              add_variable = statics;
              break;

            case eValueTypeVariableArgument:
              add_variable = arguments;
              break;

            case eValueTypeVariableLocal:
              add_variable = locals;
              break;

            default:
              break;
            }
            if (add_variable) {
              if (target_sp)
                value_list.Append(
                    ValueObjectVariable::Create(target_sp.get(), variable_sp));
            }
          }
        }
      }
    }
  }
  return value_list;
}
