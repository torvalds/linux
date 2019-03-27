//===-- SBSymbol.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBSymbol.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

SBSymbol::SBSymbol() : m_opaque_ptr(NULL) {}

SBSymbol::SBSymbol(lldb_private::Symbol *lldb_object_ptr)
    : m_opaque_ptr(lldb_object_ptr) {}

SBSymbol::SBSymbol(const lldb::SBSymbol &rhs)
    : m_opaque_ptr(rhs.m_opaque_ptr) {}

const SBSymbol &SBSymbol::operator=(const SBSymbol &rhs) {
  m_opaque_ptr = rhs.m_opaque_ptr;
  return *this;
}

SBSymbol::~SBSymbol() { m_opaque_ptr = NULL; }

void SBSymbol::SetSymbol(lldb_private::Symbol *lldb_object_ptr) {
  m_opaque_ptr = lldb_object_ptr;
}

bool SBSymbol::IsValid() const { return m_opaque_ptr != NULL; }

const char *SBSymbol::GetName() const {
  const char *name = NULL;
  if (m_opaque_ptr)
    name = m_opaque_ptr->GetName().AsCString();

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBSymbol(%p)::GetName () => \"%s\"",
                static_cast<void *>(m_opaque_ptr), name ? name : "");
  return name;
}

const char *SBSymbol::GetDisplayName() const {
  const char *name = NULL;
  if (m_opaque_ptr)
    name = m_opaque_ptr->GetMangled()
               .GetDisplayDemangledName(m_opaque_ptr->GetLanguage())
               .AsCString();

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBSymbol(%p)::GetDisplayName () => \"%s\"",
                static_cast<void *>(m_opaque_ptr), name ? name : "");
  return name;
}

const char *SBSymbol::GetMangledName() const {
  const char *name = NULL;
  if (m_opaque_ptr)
    name = m_opaque_ptr->GetMangled().GetMangledName().AsCString();
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBSymbol(%p)::GetMangledName () => \"%s\"",
                static_cast<void *>(m_opaque_ptr), name ? name : "");

  return name;
}

bool SBSymbol::operator==(const SBSymbol &rhs) const {
  return m_opaque_ptr == rhs.m_opaque_ptr;
}

bool SBSymbol::operator!=(const SBSymbol &rhs) const {
  return m_opaque_ptr != rhs.m_opaque_ptr;
}

bool SBSymbol::GetDescription(SBStream &description) {
  Stream &strm = description.ref();

  if (m_opaque_ptr) {
    m_opaque_ptr->GetDescription(&strm, lldb::eDescriptionLevelFull, NULL);
  } else
    strm.PutCString("No value");

  return true;
}

SBInstructionList SBSymbol::GetInstructions(SBTarget target) {
  return GetInstructions(target, NULL);
}

SBInstructionList SBSymbol::GetInstructions(SBTarget target,
                                            const char *flavor_string) {
  SBInstructionList sb_instructions;
  if (m_opaque_ptr) {
    ExecutionContext exe_ctx;
    TargetSP target_sp(target.GetSP());
    std::unique_lock<std::recursive_mutex> lock;
    if (target_sp) {
      lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());

      target_sp->CalculateExecutionContext(exe_ctx);
    }
    if (m_opaque_ptr->ValueIsAddress()) {
      const Address &symbol_addr = m_opaque_ptr->GetAddressRef();
      ModuleSP module_sp = symbol_addr.GetModule();
      if (module_sp) {
        AddressRange symbol_range(symbol_addr, m_opaque_ptr->GetByteSize());
        const bool prefer_file_cache = false;
        sb_instructions.SetDisassembler(Disassembler::DisassembleRange(
            module_sp->GetArchitecture(), NULL, flavor_string, exe_ctx,
            symbol_range, prefer_file_cache));
      }
    }
  }
  return sb_instructions;
}

lldb_private::Symbol *SBSymbol::get() { return m_opaque_ptr; }

void SBSymbol::reset(lldb_private::Symbol *symbol) { m_opaque_ptr = symbol; }

SBAddress SBSymbol::GetStartAddress() {
  SBAddress addr;
  if (m_opaque_ptr && m_opaque_ptr->ValueIsAddress()) {
    addr.SetAddress(&m_opaque_ptr->GetAddressRef());
  }
  return addr;
}

SBAddress SBSymbol::GetEndAddress() {
  SBAddress addr;
  if (m_opaque_ptr && m_opaque_ptr->ValueIsAddress()) {
    lldb::addr_t range_size = m_opaque_ptr->GetByteSize();
    if (range_size > 0) {
      addr.SetAddress(&m_opaque_ptr->GetAddressRef());
      addr->Slide(m_opaque_ptr->GetByteSize());
    }
  }
  return addr;
}

uint32_t SBSymbol::GetPrologueByteSize() {
  if (m_opaque_ptr)
    return m_opaque_ptr->GetPrologueByteSize();
  return 0;
}

SymbolType SBSymbol::GetType() {
  if (m_opaque_ptr)
    return m_opaque_ptr->GetType();
  return eSymbolTypeInvalid;
}

bool SBSymbol::IsExternal() {
  if (m_opaque_ptr)
    return m_opaque_ptr->IsExternal();
  return false;
}

bool SBSymbol::IsSynthetic() {
  if (m_opaque_ptr)
    return m_opaque_ptr->IsSynthetic();
  return false;
}
