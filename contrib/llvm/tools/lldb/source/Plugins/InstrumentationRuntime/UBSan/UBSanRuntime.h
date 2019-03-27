//===-- UndefinedBehaviorSanitizerRuntime.h ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_UndefinedBehaviorSanitizerRuntime_h_
#define liblldb_UndefinedBehaviorSanitizerRuntime_h_

#include "lldb/Target/ABI.h"
#include "lldb/Target/InstrumentationRuntime.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class UndefinedBehaviorSanitizerRuntime
    : public lldb_private::InstrumentationRuntime {
public:
  ~UndefinedBehaviorSanitizerRuntime() override;

  static lldb::InstrumentationRuntimeSP
  CreateInstance(const lldb::ProcessSP &process_sp);

  static void Initialize();

  static void Terminate();

  static lldb_private::ConstString GetPluginNameStatic();

  static lldb::InstrumentationRuntimeType GetTypeStatic();

  lldb_private::ConstString GetPluginName() override {
    return GetPluginNameStatic();
  }

  virtual lldb::InstrumentationRuntimeType GetType() { return GetTypeStatic(); }

  uint32_t GetPluginVersion() override { return 1; }

  lldb::ThreadCollectionSP
  GetBacktracesFromExtendedStopInfo(StructuredData::ObjectSP info) override;

private:
  UndefinedBehaviorSanitizerRuntime(const lldb::ProcessSP &process_sp)
      : lldb_private::InstrumentationRuntime(process_sp) {}

  const RegularExpression &GetPatternForRuntimeLibrary() override;

  bool CheckIfRuntimeIsValid(const lldb::ModuleSP module_sp) override;

  void Activate() override;

  void Deactivate();

  static bool NotifyBreakpointHit(void *baton,
                                  StoppointCallbackContext *context,
                                  lldb::user_id_t break_id,
                                  lldb::user_id_t break_loc_id);

  StructuredData::ObjectSP RetrieveReportData(ExecutionContextRef exe_ctx_ref);
};

} // namespace lldb_private

#endif // liblldb_UndefinedBehaviorSanitizerRuntime_h_
