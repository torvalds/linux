//===--- PathDiagnosticConsumers.h - Path Diagnostic Clients ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the interface to create different path diagostic clients.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHDIAGNOSTICCONSUMERS_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHDIAGNOSTICCONSUMERS_H

#include <string>
#include <vector>

namespace clang {

class AnalyzerOptions;
class Preprocessor;

namespace ento {

class PathDiagnosticConsumer;
typedef std::vector<PathDiagnosticConsumer*> PathDiagnosticConsumers;

#define ANALYSIS_DIAGNOSTICS(NAME, CMDFLAG, DESC, CREATEFN)\
void CREATEFN(AnalyzerOptions &AnalyzerOpts,\
              PathDiagnosticConsumers &C,\
              const std::string &Prefix,\
              const Preprocessor &PP);
#include "clang/StaticAnalyzer/Core/Analyses.def"

} // end 'ento' namespace
} // end 'clang' namespace

#endif
