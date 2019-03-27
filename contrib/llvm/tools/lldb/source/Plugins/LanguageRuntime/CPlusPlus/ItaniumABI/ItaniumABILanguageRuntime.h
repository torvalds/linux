//===-- ItaniumABILanguageRuntime.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ItaniumABILanguageRuntime_h_
#define liblldb_ItaniumABILanguageRuntime_h_

#include <map>
#include <mutex>
#include <vector>

#include "lldb/Breakpoint/BreakpointResolver.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/CPPLanguageRuntime.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class ItaniumABILanguageRuntime : public lldb_private::CPPLanguageRuntime {
public:
  ~ItaniumABILanguageRuntime() override = default;

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static lldb_private::LanguageRuntime *
  CreateInstance(Process *process, lldb::LanguageType language);

  static lldb_private::ConstString GetPluginNameStatic();

  bool IsVTableName(const char *name) override;

  bool GetDynamicTypeAndAddress(ValueObject &in_value,
                                lldb::DynamicValueType use_dynamic,
                                TypeAndOrName &class_type_or_name,
                                Address &address,
                                Value::ValueType &value_type) override;

  TypeAndOrName FixUpDynamicType(const TypeAndOrName &type_and_or_name,
                                 ValueObject &static_value) override;

  bool CouldHaveDynamicValue(ValueObject &in_value) override;

  void SetExceptionBreakpoints() override;

  void ClearExceptionBreakpoints() override;

  bool ExceptionBreakpointsAreSet() override;

  bool ExceptionBreakpointsExplainStop(lldb::StopInfoSP stop_reason) override;

  lldb::BreakpointResolverSP CreateExceptionResolver(Breakpoint *bkpt,
                                                     bool catch_bp,
                                                     bool throw_bp) override;

  lldb::SearchFilterSP CreateExceptionSearchFilter() override;
  
  lldb::ValueObjectSP GetExceptionObjectForThread(
      lldb::ThreadSP thread_sp) override;

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

protected:
  lldb::BreakpointResolverSP CreateExceptionResolver(Breakpoint *bkpt,
                                                     bool catch_bp,
                                                     bool throw_bp,
                                                     bool for_expressions);

  lldb::BreakpointSP CreateExceptionBreakpoint(bool catch_bp, bool throw_bp,
                                               bool for_expressions,
                                               bool is_internal);

private:
  typedef std::map<lldb_private::Address, TypeAndOrName> DynamicTypeCache;

  ItaniumABILanguageRuntime(Process *process)
      : // Call CreateInstance instead.
        lldb_private::CPPLanguageRuntime(process),
        m_cxx_exception_bp_sp(), m_dynamic_type_map(),
        m_dynamic_type_map_mutex() {}

  lldb::BreakpointSP m_cxx_exception_bp_sp;
  DynamicTypeCache m_dynamic_type_map;
  std::mutex m_dynamic_type_map_mutex;

  TypeAndOrName GetTypeInfoFromVTableAddress(ValueObject &in_value,
                                             lldb::addr_t original_ptr,
                                             lldb::addr_t vtable_addr);

  TypeAndOrName GetDynamicTypeInfo(const lldb_private::Address &vtable_addr);

  void SetDynamicTypeInfo(const lldb_private::Address &vtable_addr,
                          const TypeAndOrName &type_info);
};

} // namespace lldb_private

#endif // liblldb_ItaniumABILanguageRuntime_h_
