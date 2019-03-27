//===- CoverageExporterLcov.h - Code coverage lcov exporter ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class implements a code coverage exporter for lcov trace file format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_COV_COVERAGEEXPORTERLCOV_H
#define LLVM_COV_COVERAGEEXPORTERLCOV_H

#include "CoverageExporter.h"

namespace llvm {

class CoverageExporterLcov : public CoverageExporter {
public:
  CoverageExporterLcov(const coverage::CoverageMapping &CoverageMapping,
                       const CoverageViewOptions &Options, raw_ostream &OS)
      : CoverageExporter(CoverageMapping, Options, OS) {}

  /// Render the CoverageMapping object.
  void renderRoot(const CoverageFilters &IgnoreFilters) override;

  /// Render the CoverageMapping object for specified source files.
  void renderRoot(ArrayRef<std::string> SourceFiles) override;
};

} // end namespace llvm

#endif // LLVM_COV_COVERAGEEXPORTERLCOV_H
