//===-- SBFunction.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBFunction.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

SBFunction::SBFunction() : m_opaque_ptr(NULL) {}

SBFunction::SBFunction(lldb_private::Function *lldb_object_ptr)
    : m_opaque_ptr(lldb_object_ptr) {}

SBFunction::SBFunction(const lldb::SBFunction &rhs)
    : m_opaque_ptr(rhs.m_opaque_ptr) {}

const SBFunction &SBFunction::operator=(const SBFunction &rhs) {
  m_opaque_ptr = rhs.m_opaque_ptr;
  return *this;
}

SBFunction::~SBFunction() { m_opaque_ptr = NULL; }

bool SBFunction::IsValid() const { return m_opaque_ptr != NULL; }

const char *SBFunction::GetName() const {
  const char *cstr = NULL;
  if (m_opaque_ptr)
    cstr = m_opaque_ptr->GetName().AsCString();

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (cstr)
      log->Printf("SBFunction(%p)::GetName () => \"%s\"",
                  static_cast<void *>(m_opaque_ptr), cstr);
    else
      log->Printf("SBFunction(%p)::GetName () => NULL",
                  static_cast<void *>(m_opaque_ptr));
  }
  return cstr;
}

const char *SBFunction::GetDisplayName() const {
  const char *cstr = NULL;
  if (m_opaque_ptr)
    cstr = m_opaque_ptr->GetMangled()
               .GetDisplayDemangledName(m_opaque_ptr->GetLanguage())
               .AsCString();

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (cstr)
      log->Printf("SBFunction(%p)::GetDisplayName () => \"%s\"",
                  static_cast<void *>(m_opaque_ptr), cstr);
    else
      log->Printf("SBFunction(%p)::GetDisplayName () => NULL",
                  static_cast<void *>(m_opaque_ptr));
  }
  return cstr;
}

const char *SBFunction::GetMangledName() const {
  const char *cstr = NULL;
  if (m_opaque_ptr)
    cstr = m_opaque_ptr->GetMangled().GetMangledName().AsCString();
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log) {
    if (cstr)
      log->Printf("SBFunction(%p)::GetMangledName () => \"%s\"",
                  static_cast<void *>(m_opaque_ptr), cstr);
    else
      log->Printf("SBFunction(%p)::GetMangledName () => NULL",
                  static_cast<void *>(m_opaque_ptr));
  }
  return cstr;
}

bool SBFunction::operator==(const SBFunction &rhs) const {
  return m_opaque_ptr == rhs.m_opaque_ptr;
}

bool SBFunction::operator!=(const SBFunction &rhs) const {
  return m_opaque_ptr != rhs.m_opaque_ptr;
}

bool SBFunction::GetDescription(SBStream &s) {
  if (m_opaque_ptr) {
    s.Printf("SBFunction: id = 0x%8.8" PRIx64 ", name = %s",
             m_opaque_ptr->GetID(), m_opaque_ptr->GetName().AsCString());
    Type *func_type = m_opaque_ptr->GetType();
    if (func_type)
      s.Printf(", type = %s", func_type->GetName().AsCString());
    return true;
  }
  s.Printf("No value");
  return false;
}

SBInstructionList SBFunction::GetInstructions(SBTarget target) {
  return GetInstructions(target, NULL);
}

SBInstructionList SBFunction::GetInstructions(SBTarget target,
                                              const char *flavor) {
  SBInstructionList sb_instructions;
  if (m_opaque_ptr) {
    ExecutionContext exe_ctx;
    TargetSP target_sp(target.GetSP());
    std::unique_lock<std::recursive_mutex> lock;
    if (target_sp) {
      lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());
      target_sp->CalculateExecutionContext(exe_ctx);
      exe_ctx.SetProcessSP(target_sp->GetProcessSP());
    }
    ModuleSP module_sp(
        m_opaque_ptr->GetAddressRange().GetBaseAddress().GetModule());
    if (module_sp) {
      const bool prefer_file_cache = false;
      sb_instructions.SetDisassembler(Disassembler::DisassembleRange(
          module_sp->GetArchitecture(), NULL, flavor, exe_ctx,
          m_opaque_ptr->GetAddressRange(), prefer_file_cache));
    }
  }
  return sb_instructions;
}

lldb_private::Function *SBFunction::get() { return m_opaque_ptr; }

void SBFunction::reset(lldb_private::Function *lldb_object_ptr) {
  m_opaque_ptr = lldb_object_ptr;
}

SBAddress SBFunction::GetStartAddress() {
  SBAddress addr;
  if (m_opaque_ptr)
    addr.SetAddress(&m_opaque_ptr->GetAddressRange().GetBaseAddress());
  return addr;
}

SBAddress SBFunction::GetEndAddress() {
  SBAddress addr;
  if (m_opaque_ptr) {
    addr_t byte_size = m_opaque_ptr->GetAddressRange().GetByteSize();
    if (byte_size > 0) {
      addr.SetAddress(&m_opaque_ptr->GetAddressRange().GetBaseAddress());
      addr->Slide(byte_size);
    }
  }
  return addr;
}

const char *SBFunction::GetArgumentName(uint32_t arg_idx) {
  if (m_opaque_ptr) {
    Block &block = m_opaque_ptr->GetBlock(true);
    VariableListSP variable_list_sp = block.GetBlockVariableList(true);
    if (variable_list_sp) {
      VariableList arguments;
      variable_list_sp->AppendVariablesWithScope(eValueTypeVariableArgument,
                                                 arguments, true);
      lldb::VariableSP variable_sp = arguments.GetVariableAtIndex(arg_idx);
      if (variable_sp)
        return variable_sp->GetName().GetCString();
    }
  }
  return nullptr;
}

uint32_t SBFunction::GetPrologueByteSize() {
  if (m_opaque_ptr)
    return m_opaque_ptr->GetPrologueByteSize();
  return 0;
}

SBType SBFunction::GetType() {
  SBType sb_type;
  if (m_opaque_ptr) {
    Type *function_type = m_opaque_ptr->GetType();
    if (function_type)
      sb_type.ref().SetType(function_type->shared_from_this());
  }
  return sb_type;
}

SBBlock SBFunction::GetBlock() {
  SBBlock sb_block;
  if (m_opaque_ptr)
    sb_block.SetPtr(&m_opaque_ptr->GetBlock(true));
  return sb_block;
}

lldb::LanguageType SBFunction::GetLanguage() {
  if (m_opaque_ptr) {
    if (m_opaque_ptr->GetCompileUnit())
      return m_opaque_ptr->GetCompileUnit()->GetLanguage();
  }
  return lldb::eLanguageTypeUnknown;
}

bool SBFunction::GetIsOptimized() {
  if (m_opaque_ptr) {
    if (m_opaque_ptr->GetCompileUnit())
      return m_opaque_ptr->GetCompileUnit()->GetIsOptimized();
  }
  return false;
}
