//===-- CheckerRegistration.h - Checker Registration Function ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_FRONTEND_CHECKERREGISTRATION_H
#define LLVM_CLANG_STATICANALYZER_FRONTEND_CHECKERREGISTRATION_H

#include "clang/AST/ASTContext.h"
#include "clang/Basic/LLVM.h"
#include <functional>
#include <memory>
#include <string>

namespace clang {
  class AnalyzerOptions;
  class LangOptions;
  class DiagnosticsEngine;

namespace ento {
  class CheckerManager;
  class CheckerRegistry;

  std::unique_ptr<CheckerManager> createCheckerManager(
      ASTContext &context,
      AnalyzerOptions &opts,
      ArrayRef<std::string> plugins,
      ArrayRef<std::function<void(CheckerRegistry &)>> checkerRegistrationFns,
      DiagnosticsEngine &diags);

} // end ento namespace

} // end namespace clang

#endif
