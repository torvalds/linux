//===-- ExpressionVariable.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Expression/IRExecutionUnit.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"

using namespace lldb_private;

ExpressionVariable::~ExpressionVariable() {}

uint8_t *ExpressionVariable::GetValueBytes() {
  const size_t byte_size = m_frozen_sp->GetByteSize();
  if (byte_size > 0) {
    if (m_frozen_sp->GetDataExtractor().GetByteSize() < byte_size) {
      m_frozen_sp->GetValue().ResizeData(byte_size);
      m_frozen_sp->GetValue().GetData(m_frozen_sp->GetDataExtractor());
    }
    return const_cast<uint8_t *>(
        m_frozen_sp->GetDataExtractor().GetDataStart());
  }
  return NULL;
}

PersistentExpressionState::~PersistentExpressionState() {}

lldb::addr_t PersistentExpressionState::LookupSymbol(const ConstString &name) {
  SymbolMap::iterator si = m_symbol_map.find(name.GetCString());

  if (si != m_symbol_map.end())
    return si->second;
  else
    return LLDB_INVALID_ADDRESS;
}

void PersistentExpressionState::RegisterExecutionUnit(
    lldb::IRExecutionUnitSP &execution_unit_sp) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  m_execution_units.insert(execution_unit_sp);

  if (log)
    log->Printf("Registering JITted Functions:\n");

  for (const IRExecutionUnit::JittedFunction &jitted_function :
       execution_unit_sp->GetJittedFunctions()) {
    if (jitted_function.m_external &&
        jitted_function.m_name != execution_unit_sp->GetFunctionName() &&
        jitted_function.m_remote_addr != LLDB_INVALID_ADDRESS) {
      m_symbol_map[jitted_function.m_name.GetCString()] =
          jitted_function.m_remote_addr;
      if (log)
        log->Printf("  Function: %s at 0x%" PRIx64 ".",
                    jitted_function.m_name.GetCString(),
                    jitted_function.m_remote_addr);
    }
  }

  if (log)
    log->Printf("Registering JIIted Symbols:\n");

  for (const IRExecutionUnit::JittedGlobalVariable &global_var :
       execution_unit_sp->GetJittedGlobalVariables()) {
    if (global_var.m_remote_addr != LLDB_INVALID_ADDRESS) {
      // Demangle the name before inserting it, so that lookups by the ConstStr
      // of the demangled name will find the mangled one (needed for looking up
      // metadata pointers.)
      Mangled mangler(global_var.m_name);
      mangler.GetDemangledName(lldb::eLanguageTypeUnknown);
      m_symbol_map[global_var.m_name.GetCString()] = global_var.m_remote_addr;
      if (log)
        log->Printf("  Symbol: %s at 0x%" PRIx64 ".",
                    global_var.m_name.GetCString(), global_var.m_remote_addr);
    }
  }
}

ConstString PersistentExpressionState::GetNextPersistentVariableName(
    Target &target, llvm::StringRef Prefix) {
  llvm::SmallString<64> name;
  {
    llvm::raw_svector_ostream os(name);
    os << Prefix << target.GetNextPersistentVariableIndex();
  }
  return ConstString(name);
}
