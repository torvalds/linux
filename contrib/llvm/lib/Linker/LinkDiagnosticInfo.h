//===- LinkDiagnosticInfo.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_LINKER_LINK_DIAGNOSTIC_INFO_H
#define LLVM_LIB_LINKER_LINK_DIAGNOSTIC_INFO_H

#include "llvm/IR/DiagnosticInfo.h"

namespace llvm {
class LinkDiagnosticInfo : public DiagnosticInfo {
  const Twine &Msg;

public:
  LinkDiagnosticInfo(DiagnosticSeverity Severity, const Twine &Msg);
  void print(DiagnosticPrinter &DP) const override;
};
}

#endif
