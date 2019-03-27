//===- xray-converter.h - XRay Trace Conversion ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines the TraceConverter class for turning binary traces into
// human-readable text and vice versa.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TOOLS_LLVM_XRAY_XRAY_CONVERTER_H
#define LLVM_TOOLS_LLVM_XRAY_XRAY_CONVERTER_H

#include "func-id-helper.h"
#include "llvm/XRay/Trace.h"
#include "llvm/XRay/XRayRecord.h"

namespace llvm {
namespace xray {

class TraceConverter {
  FuncIdConversionHelper &FuncIdHelper;
  bool Symbolize;

public:
  TraceConverter(FuncIdConversionHelper &FuncIdHelper, bool Symbolize = false)
      : FuncIdHelper(FuncIdHelper), Symbolize(Symbolize) {}

  void exportAsYAML(const Trace &Records, raw_ostream &OS);
  void exportAsRAWv1(const Trace &Records, raw_ostream &OS);

  /// For this conversion, the Function records within each thread are expected
  /// to be in sorted TSC order. The trace event format encodes stack traces, so
  /// the linear history is essential for correct output.
  void exportAsChromeTraceEventFormat(const Trace &Records, raw_ostream &OS);
};

} // namespace xray
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_XRAY_XRAY_CONVERTER_H
