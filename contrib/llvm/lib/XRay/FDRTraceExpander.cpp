//===- FDRTraceExpander.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/FDRTraceExpander.h"

namespace llvm {
namespace xray {

void TraceExpander::resetCurrentRecord() {
  if (BuildingRecord)
    C(CurrentRecord);
  BuildingRecord = false;
  CurrentRecord.CallArgs.clear();
  CurrentRecord.Data.clear();
}

Error TraceExpander::visit(BufferExtents &) {
  resetCurrentRecord();
  return Error::success();
}

Error TraceExpander::visit(WallclockRecord &) { return Error::success(); }

Error TraceExpander::visit(NewCPUIDRecord &R) {
  CPUId = R.cpuid();
  BaseTSC = R.tsc();
  return Error::success();
}

Error TraceExpander::visit(TSCWrapRecord &R) {
  BaseTSC = R.tsc();
  return Error::success();
}

Error TraceExpander::visit(CustomEventRecord &R) {
  resetCurrentRecord();
  if (!IgnoringRecords) {
    CurrentRecord.TSC = R.tsc();
    CurrentRecord.CPU = R.cpu();
    CurrentRecord.PId = PID;
    CurrentRecord.TId = TID;
    CurrentRecord.Type = RecordTypes::CUSTOM_EVENT;
    CurrentRecord.Data = R.data();
    BuildingRecord = true;
  }
  return Error::success();
}

Error TraceExpander::visit(CustomEventRecordV5 &R) {
  resetCurrentRecord();
  if (!IgnoringRecords) {
    BaseTSC += R.delta();
    CurrentRecord.TSC = BaseTSC;
    CurrentRecord.CPU = CPUId;
    CurrentRecord.PId = PID;
    CurrentRecord.TId = TID;
    CurrentRecord.Type = RecordTypes::CUSTOM_EVENT;
    CurrentRecord.Data = R.data();
    BuildingRecord = true;
  }
  return Error::success();
}

Error TraceExpander::visit(TypedEventRecord &R) {
  resetCurrentRecord();
  if (!IgnoringRecords) {
    BaseTSC += R.delta();
    CurrentRecord.TSC = BaseTSC;
    CurrentRecord.CPU = CPUId;
    CurrentRecord.PId = PID;
    CurrentRecord.TId = TID;
    CurrentRecord.RecordType = R.eventType();
    CurrentRecord.Type = RecordTypes::TYPED_EVENT;
    CurrentRecord.Data = R.data();
    BuildingRecord = true;
  }
  return Error::success();
}

Error TraceExpander::visit(CallArgRecord &R) {
  CurrentRecord.CallArgs.push_back(R.arg());
  CurrentRecord.Type = RecordTypes::ENTER_ARG;
  return Error::success();
}

Error TraceExpander::visit(PIDRecord &R) {
  PID = R.pid();
  return Error::success();
}

Error TraceExpander::visit(NewBufferRecord &R) {
  if (IgnoringRecords)
    IgnoringRecords = false;
  TID = R.tid();
  if (LogVersion == 2)
    PID = R.tid();
  return Error::success();
}

Error TraceExpander::visit(EndBufferRecord &) {
  IgnoringRecords = true;
  resetCurrentRecord();
  return Error::success();
}

Error TraceExpander::visit(FunctionRecord &R) {
  resetCurrentRecord();
  if (!IgnoringRecords) {
    BaseTSC += R.delta();
    CurrentRecord.Type = R.recordType();
    CurrentRecord.FuncId = R.functionId();
    CurrentRecord.TSC = BaseTSC;
    CurrentRecord.PId = PID;
    CurrentRecord.TId = TID;
    CurrentRecord.CPU = CPUId;
    BuildingRecord = true;
  }
  return Error::success();
}

Error TraceExpander::flush() {
  resetCurrentRecord();
  return Error::success();
}

} // namespace xray
} // namespace llvm
