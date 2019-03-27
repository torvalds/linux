//===- CoverageExporterLcov.cpp - Code coverage export --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements export of code coverage data to lcov trace file format.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//
// The trace file code coverage export follows the following format (see also
// https://linux.die.net/man/1/geninfo). Each quoted string appears on its own
// line; the indentation shown here is only for documentation purposes.
//
// - for each source file:
//   - "SF:<absolute path to source file>"
//   - for each function:
//     - "FN:<line number of function start>,<function name>"
//   - for each function:
//     - "FNDA:<execution count>,<function name>"
//   - "FNF:<number of functions found>"
//   - "FNH:<number of functions hit>"
//   - for each instrumented line:
//     - "DA:<line number>,<execution count>[,<checksum>]
//   - "LH:<number of lines with non-zero execution count>"
//   - "LF:<nubmer of instrumented lines>"
//   - "end_of_record"
//
// If the user is exporting summary information only, then the FN, FNDA, and DA
// lines will not be present.
//
//===----------------------------------------------------------------------===//

#include "CoverageExporterLcov.h"
#include "CoverageReport.h"

using namespace llvm;

namespace {

void renderFunctionSummary(raw_ostream &OS,
                           const FileCoverageSummary &Summary) {
  OS << "FNF:" << Summary.FunctionCoverage.getNumFunctions() << '\n'
     << "FNH:" << Summary.FunctionCoverage.getExecuted() << '\n';
}

void renderFunctions(
    raw_ostream &OS,
    const iterator_range<coverage::FunctionRecordIterator> &Functions) {
  for (const auto &F : Functions) {
    auto StartLine = F.CountedRegions.front().LineStart;
    OS << "FN:" << StartLine << ',' << F.Name << '\n';
  }
  for (const auto &F : Functions)
    OS << "FNDA:" << F.ExecutionCount << ',' << F.Name << '\n';
}

void renderLineExecutionCounts(raw_ostream &OS,
                               const coverage::CoverageData &FileCoverage) {
  coverage::LineCoverageIterator LCI{FileCoverage, 1};
  coverage::LineCoverageIterator LCIEnd = LCI.getEnd();
  for (; LCI != LCIEnd; ++LCI) {
    const coverage::LineCoverageStats &LCS = *LCI;
    if (LCS.isMapped()) {
      OS << "DA:" << LCS.getLine() << ',' << LCS.getExecutionCount() << '\n';
    }
  }
}

void renderLineSummary(raw_ostream &OS, const FileCoverageSummary &Summary) {
  OS << "LF:" << Summary.LineCoverage.getNumLines() << '\n'
     << "LH:" << Summary.LineCoverage.getCovered() << '\n';
}

void renderFile(raw_ostream &OS, const coverage::CoverageMapping &Coverage,
                const std::string &Filename,
                const FileCoverageSummary &FileReport, bool ExportSummaryOnly) {
  OS << "SF:" << Filename << '\n';

  if (!ExportSummaryOnly) {
    renderFunctions(OS, Coverage.getCoveredFunctions());
  }
  renderFunctionSummary(OS, FileReport);

  if (!ExportSummaryOnly) {
    // Calculate and render detailed coverage information for given file.
    auto FileCoverage = Coverage.getCoverageForFile(Filename);
    renderLineExecutionCounts(OS, FileCoverage);
  }
  renderLineSummary(OS, FileReport);

  OS << "end_of_record\n";
}

void renderFiles(raw_ostream &OS, const coverage::CoverageMapping &Coverage,
                 ArrayRef<std::string> SourceFiles,
                 ArrayRef<FileCoverageSummary> FileReports,
                 bool ExportSummaryOnly) {
  for (unsigned I = 0, E = SourceFiles.size(); I < E; ++I)
    renderFile(OS, Coverage, SourceFiles[I], FileReports[I], ExportSummaryOnly);
}

} // end anonymous namespace

void CoverageExporterLcov::renderRoot(const CoverageFilters &IgnoreFilters) {
  std::vector<std::string> SourceFiles;
  for (StringRef SF : Coverage.getUniqueSourceFiles()) {
    if (!IgnoreFilters.matchesFilename(SF))
      SourceFiles.emplace_back(SF);
  }
  renderRoot(SourceFiles);
}

void CoverageExporterLcov::renderRoot(ArrayRef<std::string> SourceFiles) {
  FileCoverageSummary Totals = FileCoverageSummary("Totals");
  auto FileReports = CoverageReport::prepareFileReports(Coverage, Totals,
                                                        SourceFiles, Options);
  renderFiles(OS, Coverage, SourceFiles, FileReports,
              Options.ExportSummaryOnly);
}
