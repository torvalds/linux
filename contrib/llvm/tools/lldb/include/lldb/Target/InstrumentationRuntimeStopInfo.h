//===-- InstrumentationRuntimeStopInfo.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_InstrumentationRuntimeStopInfo_h_
#define liblldb_InstrumentationRuntimeStopInfo_h_

#include <string>

#include "lldb/Target/StopInfo.h"
#include "lldb/Utility/StructuredData.h"

namespace lldb_private {

class InstrumentationRuntimeStopInfo : public StopInfo {
public:
  ~InstrumentationRuntimeStopInfo() override {}

  lldb::StopReason GetStopReason() const override {
    return lldb::eStopReasonInstrumentation;
  }

  const char *GetDescription() override;

  bool DoShouldNotify(Event *event_ptr) override { return true; }

  static lldb::StopInfoSP CreateStopReasonWithInstrumentationData(
      Thread &thread, std::string description,
      StructuredData::ObjectSP additional_data);

private:
  InstrumentationRuntimeStopInfo(Thread &thread, std::string description,
                                 StructuredData::ObjectSP additional_data);
};

} // namespace lldb_private

#endif // liblldb_InstrumentationRuntimeStopInfo_h_
