//===--- AnalysisConsumer.h - Front-end Analysis Engine Hooks ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header contains the functions necessary for a front-end to run various
// analyses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_FRONTEND_ANALYSISCONSUMER_H
#define LLVM_CLANG_STATICANALYZER_FRONTEND_ANALYSISCONSUMER_H

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/LLVM.h"
#include <functional>
#include <memory>

namespace clang {

class Preprocessor;
class DiagnosticsEngine;
class CodeInjector;
class CompilerInstance;

namespace ento {
class PathDiagnosticConsumer;
class CheckerManager;
class CheckerRegistry;

class AnalysisASTConsumer : public ASTConsumer {
public:
  virtual void AddDiagnosticConsumer(PathDiagnosticConsumer *Consumer) = 0;

  /// This method allows registering statically linked custom checkers that are
  /// not a part of the Clang tree. It employs the same mechanism that is used
  /// by plugins.
  ///
  /// Example:
  ///
  ///   Consumer->AddCheckerRegistrationFn([] (CheckerRegistry& Registry) {
  ///     Registry.addChecker<MyCustomChecker>("example.MyCustomChecker",
  ///                                          "Description");
  ///   });
  virtual void
  AddCheckerRegistrationFn(std::function<void(CheckerRegistry &)> Fn) = 0;
};

/// CreateAnalysisConsumer - Creates an ASTConsumer to run various code
/// analysis passes.  (The set of analyses run is controlled by command-line
/// options.)
std::unique_ptr<AnalysisASTConsumer>
CreateAnalysisConsumer(CompilerInstance &CI);

} // end GR namespace

} // end clang namespace

#endif
