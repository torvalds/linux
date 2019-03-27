//===-- InstrumentationRuntimeStopInfo.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/InstrumentationRuntimeStopInfo.h"

#include "lldb/Target/InstrumentationRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/lldb-private.h"

using namespace lldb;
using namespace lldb_private;

InstrumentationRuntimeStopInfo::InstrumentationRuntimeStopInfo(
    Thread &thread, std::string description,
    StructuredData::ObjectSP additional_data)
    : StopInfo(thread, 0) {
  m_extended_info = additional_data;
  m_description = description;
}

const char *InstrumentationRuntimeStopInfo::GetDescription() {
  return m_description.c_str();
}

StopInfoSP
InstrumentationRuntimeStopInfo::CreateStopReasonWithInstrumentationData(
    Thread &thread, std::string description,
    StructuredData::ObjectSP additionalData) {
  return StopInfoSP(
      new InstrumentationRuntimeStopInfo(thread, description, additionalData));
}
