//===- ChainedDiagnosticConsumer.h - Chain Diagnostic Clients ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_CHAINEDDIAGNOSTICCONSUMER_H
#define LLVM_CLANG_FRONTEND_CHAINEDDIAGNOSTICCONSUMER_H

#include "clang/Basic/Diagnostic.h"
#include <memory>

namespace clang {
class LangOptions;

/// ChainedDiagnosticConsumer - Chain two diagnostic clients so that diagnostics
/// go to the first client and then the second. The first diagnostic client
/// should be the "primary" client, and will be used for computing whether the
/// diagnostics should be included in counts.
class ChainedDiagnosticConsumer : public DiagnosticConsumer {
  virtual void anchor();
  std::unique_ptr<DiagnosticConsumer> OwningPrimary;
  DiagnosticConsumer *Primary;
  std::unique_ptr<DiagnosticConsumer> Secondary;

public:
  ChainedDiagnosticConsumer(std::unique_ptr<DiagnosticConsumer> Primary,
                            std::unique_ptr<DiagnosticConsumer> Secondary)
      : OwningPrimary(std::move(Primary)), Primary(OwningPrimary.get()),
        Secondary(std::move(Secondary)) {}

  /// Construct without taking ownership of \c Primary.
  ChainedDiagnosticConsumer(DiagnosticConsumer *Primary,
                            std::unique_ptr<DiagnosticConsumer> Secondary)
      : Primary(Primary), Secondary(std::move(Secondary)) {}

  void BeginSourceFile(const LangOptions &LO,
                       const Preprocessor *PP) override {
    Primary->BeginSourceFile(LO, PP);
    Secondary->BeginSourceFile(LO, PP);
  }

  void EndSourceFile() override {
    Secondary->EndSourceFile();
    Primary->EndSourceFile();
  }

  void finish() override {
    Secondary->finish();
    Primary->finish();
  }

  bool IncludeInDiagnosticCounts() const override {
    return Primary->IncludeInDiagnosticCounts();
  }

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override {
    // Default implementation (Warnings/errors count).
    DiagnosticConsumer::HandleDiagnostic(DiagLevel, Info);

    Primary->HandleDiagnostic(DiagLevel, Info);
    Secondary->HandleDiagnostic(DiagLevel, Info);
  }
};

} // end namspace clang

#endif
