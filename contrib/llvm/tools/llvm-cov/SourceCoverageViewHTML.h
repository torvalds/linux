//===- SourceCoverageViewHTML.h - A html code coverage view ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file This file defines the interface to the html coverage renderer.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_COV_SOURCECOVERAGEVIEWHTML_H
#define LLVM_COV_SOURCECOVERAGEVIEWHTML_H

#include "SourceCoverageView.h"

namespace llvm {

using namespace coverage;

struct FileCoverageSummary;

/// A coverage printer for html output.
class CoveragePrinterHTML : public CoveragePrinter {
public:
  Expected<OwnedStream> createViewFile(StringRef Path,
                                       bool InToplevel) override;

  void closeViewFile(OwnedStream OS) override;

  Error createIndexFile(ArrayRef<std::string> SourceFiles,
                        const coverage::CoverageMapping &Coverage,
                        const CoverageFiltersMatchAll &Filters) override;

  CoveragePrinterHTML(const CoverageViewOptions &Opts)
      : CoveragePrinter(Opts) {}

private:
  void emitFileSummary(raw_ostream &OS, StringRef SF,
                       const FileCoverageSummary &FCS,
                       bool IsTotals = false) const;
  std::string buildLinkToFile(StringRef SF,
                              const FileCoverageSummary &FCS) const;
};

/// A code coverage view which supports html-based rendering.
class SourceCoverageViewHTML : public SourceCoverageView {
  void renderViewHeader(raw_ostream &OS) override;

  void renderViewFooter(raw_ostream &OS) override;

  void renderSourceName(raw_ostream &OS, bool WholeFile) override;

  void renderLinePrefix(raw_ostream &OS, unsigned ViewDepth) override;

  void renderLineSuffix(raw_ostream &OS, unsigned ViewDepth) override;

  void renderViewDivider(raw_ostream &OS, unsigned ViewDepth) override;

  void renderLine(raw_ostream &OS, LineRef L, const LineCoverageStats &LCS,
                  unsigned ExpansionCol, unsigned ViewDepth) override;

  void renderExpansionSite(raw_ostream &OS, LineRef L,
                           const LineCoverageStats &LCS, unsigned ExpansionCol,
                           unsigned ViewDepth) override;

  void renderExpansionView(raw_ostream &OS, ExpansionView &ESV,
                           unsigned ViewDepth) override;

  void renderInstantiationView(raw_ostream &OS, InstantiationView &ISV,
                               unsigned ViewDepth) override;

  void renderLineCoverageColumn(raw_ostream &OS,
                                const LineCoverageStats &Line) override;

  void renderLineNumberColumn(raw_ostream &OS, unsigned LineNo) override;

  void renderRegionMarkers(raw_ostream &OS, const LineCoverageStats &Line,
                           unsigned ViewDepth) override;

  void renderTitle(raw_ostream &OS, StringRef Title) override;

  void renderTableHeader(raw_ostream &OS, unsigned FirstUncoveredLineNo,
                         unsigned IndentLevel) override;

public:
  SourceCoverageViewHTML(StringRef SourceName, const MemoryBuffer &File,
                         const CoverageViewOptions &Options,
                         coverage::CoverageData &&CoverageInfo)
      : SourceCoverageView(SourceName, File, Options, std::move(CoverageInfo)) {
  }
};

} // namespace llvm

#endif // LLVM_COV_SOURCECOVERAGEVIEWHTML_H
