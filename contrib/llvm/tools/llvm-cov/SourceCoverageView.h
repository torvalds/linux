//===- SourceCoverageView.h - Code coverage view for source code ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file This class implements rendering for code coverage of source code.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_COV_SOURCECOVERAGEVIEW_H
#define LLVM_COV_SOURCECOVERAGEVIEW_H

#include "CoverageViewOptions.h"
#include "CoverageSummaryInfo.h"
#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include "llvm/Support/MemoryBuffer.h"
#include <vector>

namespace llvm {

using namespace coverage;

class CoverageFiltersMatchAll;
class SourceCoverageView;

/// A view that represents a macro or include expansion.
struct ExpansionView {
  CounterMappingRegion Region;
  std::unique_ptr<SourceCoverageView> View;

  ExpansionView(const CounterMappingRegion &Region,
                std::unique_ptr<SourceCoverageView> View)
      : Region(Region), View(std::move(View)) {}
  ExpansionView(ExpansionView &&RHS)
      : Region(std::move(RHS.Region)), View(std::move(RHS.View)) {}
  ExpansionView &operator=(ExpansionView &&RHS) {
    Region = std::move(RHS.Region);
    View = std::move(RHS.View);
    return *this;
  }

  unsigned getLine() const { return Region.LineStart; }
  unsigned getStartCol() const { return Region.ColumnStart; }
  unsigned getEndCol() const { return Region.ColumnEnd; }

  friend bool operator<(const ExpansionView &LHS, const ExpansionView &RHS) {
    return LHS.Region.startLoc() < RHS.Region.startLoc();
  }
};

/// A view that represents a function instantiation.
struct InstantiationView {
  StringRef FunctionName;
  unsigned Line;
  std::unique_ptr<SourceCoverageView> View;

  InstantiationView(StringRef FunctionName, unsigned Line,
                    std::unique_ptr<SourceCoverageView> View)
      : FunctionName(FunctionName), Line(Line), View(std::move(View)) {}

  friend bool operator<(const InstantiationView &LHS,
                        const InstantiationView &RHS) {
    return LHS.Line < RHS.Line;
  }
};

/// A file manager that handles format-aware file creation.
class CoveragePrinter {
public:
  struct StreamDestructor {
    void operator()(raw_ostream *OS) const;
  };

  using OwnedStream = std::unique_ptr<raw_ostream, StreamDestructor>;

protected:
  const CoverageViewOptions &Opts;

  CoveragePrinter(const CoverageViewOptions &Opts) : Opts(Opts) {}

  /// Return `OutputDir/ToplevelDir/Path.Extension`. If \p InToplevel is
  /// false, skip the ToplevelDir component. If \p Relative is false, skip the
  /// OutputDir component.
  std::string getOutputPath(StringRef Path, StringRef Extension,
                            bool InToplevel, bool Relative = true) const;

  /// If directory output is enabled, create a file in that directory
  /// at the path given by getOutputPath(). Otherwise, return stdout.
  Expected<OwnedStream> createOutputStream(StringRef Path, StringRef Extension,
                                           bool InToplevel) const;

  /// Return the sub-directory name for file coverage reports.
  static StringRef getCoverageDir() { return "coverage"; }

public:
  static std::unique_ptr<CoveragePrinter>
  create(const CoverageViewOptions &Opts);

  virtual ~CoveragePrinter() {}

  /// @name File Creation Interface
  /// @{

  /// Create a file to print a coverage view into.
  virtual Expected<OwnedStream> createViewFile(StringRef Path,
                                               bool InToplevel) = 0;

  /// Close a file which has been used to print a coverage view.
  virtual void closeViewFile(OwnedStream OS) = 0;

  /// Create an index which lists reports for the given source files.
  virtual Error createIndexFile(ArrayRef<std::string> SourceFiles,
                                const CoverageMapping &Coverage,
                                const CoverageFiltersMatchAll &Filters) = 0;

  /// @}
};

/// A code coverage view of a source file or function.
///
/// A source coverage view and its nested sub-views form a file-oriented
/// representation of code coverage data. This view can be printed out by a
/// renderer which implements the Rendering Interface.
class SourceCoverageView {
  /// A function or file name.
  StringRef SourceName;

  /// A memory buffer backing the source on display.
  const MemoryBuffer &File;

  /// Various options to guide the coverage renderer.
  const CoverageViewOptions &Options;

  /// Complete coverage information about the source on display.
  CoverageData CoverageInfo;

  /// A container for all expansions (e.g macros) in the source on display.
  std::vector<ExpansionView> ExpansionSubViews;

  /// A container for all instantiations (e.g template functions) in the source
  /// on display.
  std::vector<InstantiationView> InstantiationSubViews;

  /// Get the first uncovered line number for the source file.
  unsigned getFirstUncoveredLineNo();

protected:
  struct LineRef {
    StringRef Line;
    int64_t LineNo;

    LineRef(StringRef Line, int64_t LineNo) : Line(Line), LineNo(LineNo) {}
  };

  using CoverageSegmentArray = ArrayRef<const CoverageSegment *>;

  /// @name Rendering Interface
  /// @{

  /// Render a header for the view.
  virtual void renderViewHeader(raw_ostream &OS) = 0;

  /// Render a footer for the view.
  virtual void renderViewFooter(raw_ostream &OS) = 0;

  /// Render the source name for the view.
  virtual void renderSourceName(raw_ostream &OS, bool WholeFile) = 0;

  /// Render the line prefix at the given \p ViewDepth.
  virtual void renderLinePrefix(raw_ostream &OS, unsigned ViewDepth) = 0;

  /// Render the line suffix at the given \p ViewDepth.
  virtual void renderLineSuffix(raw_ostream &OS, unsigned ViewDepth) = 0;

  /// Render a view divider at the given \p ViewDepth.
  virtual void renderViewDivider(raw_ostream &OS, unsigned ViewDepth) = 0;

  /// Render a source line with highlighting.
  virtual void renderLine(raw_ostream &OS, LineRef L,
                          const LineCoverageStats &LCS, unsigned ExpansionCol,
                          unsigned ViewDepth) = 0;

  /// Render the line's execution count column.
  virtual void renderLineCoverageColumn(raw_ostream &OS,
                                        const LineCoverageStats &Line) = 0;

  /// Render the line number column.
  virtual void renderLineNumberColumn(raw_ostream &OS, unsigned LineNo) = 0;

  /// Render all the region's execution counts on a line.
  virtual void renderRegionMarkers(raw_ostream &OS,
                                   const LineCoverageStats &Line,
                                   unsigned ViewDepth) = 0;

  /// Render the site of an expansion.
  virtual void renderExpansionSite(raw_ostream &OS, LineRef L,
                                   const LineCoverageStats &LCS,
                                   unsigned ExpansionCol,
                                   unsigned ViewDepth) = 0;

  /// Render an expansion view and any nested views.
  virtual void renderExpansionView(raw_ostream &OS, ExpansionView &ESV,
                                   unsigned ViewDepth) = 0;

  /// Render an instantiation view and any nested views.
  virtual void renderInstantiationView(raw_ostream &OS, InstantiationView &ISV,
                                       unsigned ViewDepth) = 0;

  /// Render \p Title, a project title if one is available, and the
  /// created time.
  virtual void renderTitle(raw_ostream &OS, StringRef CellText) = 0;

  /// Render the table header for a given source file.
  virtual void renderTableHeader(raw_ostream &OS, unsigned FirstUncoveredLineNo,
                                 unsigned IndentLevel) = 0;

  /// @}

  /// Format a count using engineering notation with 3 significant
  /// digits.
  static std::string formatCount(uint64_t N);

  /// Check if region marker output is expected for a line.
  bool shouldRenderRegionMarkers(const LineCoverageStats &LCS) const;

  /// Check if there are any sub-views attached to this view.
  bool hasSubViews() const;

  SourceCoverageView(StringRef SourceName, const MemoryBuffer &File,
                     const CoverageViewOptions &Options,
                     CoverageData &&CoverageInfo)
      : SourceName(SourceName), File(File), Options(Options),
        CoverageInfo(std::move(CoverageInfo)) {}

public:
  static std::unique_ptr<SourceCoverageView>
  create(StringRef SourceName, const MemoryBuffer &File,
         const CoverageViewOptions &Options, CoverageData &&CoverageInfo);

  virtual ~SourceCoverageView() {}

  /// Return the source name formatted for the host OS.
  std::string getSourceName() const;

  const CoverageViewOptions &getOptions() const { return Options; }

  /// Add an expansion subview to this view.
  void addExpansion(const CounterMappingRegion &Region,
                    std::unique_ptr<SourceCoverageView> View);

  /// Add a function instantiation subview to this view.
  void addInstantiation(StringRef FunctionName, unsigned Line,
                        std::unique_ptr<SourceCoverageView> View);

  /// Print the code coverage information for a specific portion of a
  /// source file to the output stream.
  void print(raw_ostream &OS, bool WholeFile, bool ShowSourceName,
             bool ShowTitle, unsigned ViewDepth = 0);
};

} // namespace llvm

#endif // LLVM_COV_SOURCECOVERAGEVIEW_H
