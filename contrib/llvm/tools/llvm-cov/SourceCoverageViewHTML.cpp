//===- SourceCoverageViewHTML.cpp - A html code coverage view -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file This file implements the html coverage renderer.
///
//===----------------------------------------------------------------------===//

#include "CoverageReport.h"
#include "SourceCoverageViewHTML.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"

using namespace llvm;

namespace {

// Return a string with the special characters in \p Str escaped.
std::string escape(StringRef Str, const CoverageViewOptions &Opts) {
  std::string TabExpandedResult;
  unsigned ColNum = 0; // Record the column number.
  for (char C : Str) {
    if (C == '\t') {
      // Replace '\t' with up to TabSize spaces.
      unsigned NumSpaces = Opts.TabSize - (ColNum % Opts.TabSize);
      for (unsigned I = 0; I < NumSpaces; ++I)
        TabExpandedResult += ' ';
      ColNum += NumSpaces;
    } else {
      TabExpandedResult += C;
      if (C == '\n' || C == '\r')
        ColNum = 0;
      else
        ++ColNum;
    }
  }
  std::string EscapedHTML;
  {
    raw_string_ostream OS{EscapedHTML};
    printHTMLEscaped(TabExpandedResult, OS);
  }
  return EscapedHTML;
}

// Create a \p Name tag around \p Str, and optionally set its \p ClassName.
std::string tag(const std::string &Name, const std::string &Str,
                const std::string &ClassName = "") {
  std::string Tag = "<" + Name;
  if (!ClassName.empty())
    Tag += " class='" + ClassName + "'";
  return Tag + ">" + Str + "</" + Name + ">";
}

// Create an anchor to \p Link with the label \p Str.
std::string a(const std::string &Link, const std::string &Str,
              const std::string &TargetName = "") {
  std::string Name = TargetName.empty() ? "" : ("name='" + TargetName + "' ");
  return "<a " + Name + "href='" + Link + "'>" + Str + "</a>";
}

const char *BeginHeader =
  "<head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta charset='UTF-8'>";

const char *CSSForCoverage =
    R"(.red {
  background-color: #ffd0d0;
}
.cyan {
  background-color: cyan;
}
body {
  font-family: -apple-system, sans-serif;
}
pre {
  margin-top: 0px !important;
  margin-bottom: 0px !important;
}
.source-name-title {
  padding: 5px 10px;
  border-bottom: 1px solid #dbdbdb;
  background-color: #eee;
  line-height: 35px;
}
.centered {
  display: table;
  margin-left: left;
  margin-right: auto;
  border: 1px solid #dbdbdb;
  border-radius: 3px;
}
.expansion-view {
  background-color: rgba(0, 0, 0, 0);
  margin-left: 0px;
  margin-top: 5px;
  margin-right: 5px;
  margin-bottom: 5px;
  border: 1px solid #dbdbdb;
  border-radius: 3px;
}
table {
  border-collapse: collapse;
}
.light-row {
  background: #ffffff;
  border: 1px solid #dbdbdb;
}
.light-row-bold {
  background: #ffffff;
  border: 1px solid #dbdbdb;
  font-weight: bold;
}
.column-entry {
  text-align: left;
}
.column-entry-bold {
  font-weight: bold;
  text-align: left;
}
.column-entry-yellow {
  text-align: left;
  background-color: #ffffd0;
}
.column-entry-yellow:hover {
  background-color: #fffff0;
}
.column-entry-red {
  text-align: left;
  background-color: #ffd0d0;
}
.column-entry-red:hover {
  background-color: #fff0f0;
}
.column-entry-green {
  text-align: left;
  background-color: #d0ffd0;
}
.column-entry-green:hover {
  background-color: #f0fff0;
}
.line-number {
  text-align: right;
  color: #aaa;
}
.covered-line {
  text-align: right;
  color: #0080ff;
}
.uncovered-line {
  text-align: right;
  color: #ff3300;
}
.tooltip {
  position: relative;
  display: inline;
  background-color: #b3e6ff;
  text-decoration: none;
}
.tooltip span.tooltip-content {
  position: absolute;
  width: 100px;
  margin-left: -50px;
  color: #FFFFFF;
  background: #000000;
  height: 30px;
  line-height: 30px;
  text-align: center;
  visibility: hidden;
  border-radius: 6px;
}
.tooltip span.tooltip-content:after {
  content: '';
  position: absolute;
  top: 100%;
  left: 50%;
  margin-left: -8px;
  width: 0; height: 0;
  border-top: 8px solid #000000;
  border-right: 8px solid transparent;
  border-left: 8px solid transparent;
}
:hover.tooltip span.tooltip-content {
  visibility: visible;
  opacity: 0.8;
  bottom: 30px;
  left: 50%;
  z-index: 999;
}
th, td {
  vertical-align: top;
  padding: 2px 8px;
  border-collapse: collapse;
  border-right: solid 1px #eee;
  border-left: solid 1px #eee;
  text-align: left;
}
td pre {
  display: inline-block;
}
td:first-child {
  border-left: none;
}
td:last-child {
  border-right: none;
}
tr:hover {
  background-color: #f0f0f0;
}
)";

const char *EndHeader = "</head>";

const char *BeginCenteredDiv = "<div class='centered'>";

const char *EndCenteredDiv = "</div>";

const char *BeginSourceNameDiv = "<div class='source-name-title'>";

const char *EndSourceNameDiv = "</div>";

const char *BeginCodeTD = "<td class='code'>";

const char *EndCodeTD = "</td>";

const char *BeginPre = "<pre>";

const char *EndPre = "</pre>";

const char *BeginExpansionDiv = "<div class='expansion-view'>";

const char *EndExpansionDiv = "</div>";

const char *BeginTable = "<table>";

const char *EndTable = "</table>";

const char *ProjectTitleTag = "h1";

const char *ReportTitleTag = "h2";

const char *CreatedTimeTag = "h4";

std::string getPathToStyle(StringRef ViewPath) {
  std::string PathToStyle = "";
  std::string PathSep = sys::path::get_separator();
  unsigned NumSeps = ViewPath.count(PathSep);
  for (unsigned I = 0, E = NumSeps; I < E; ++I)
    PathToStyle += ".." + PathSep;
  return PathToStyle + "style.css";
}

void emitPrelude(raw_ostream &OS, const CoverageViewOptions &Opts,
                 const std::string &PathToStyle = "") {
  OS << "<!doctype html>"
        "<html>"
     << BeginHeader;

  // Link to a stylesheet if one is available. Otherwise, use the default style.
  if (PathToStyle.empty())
    OS << "<style>" << CSSForCoverage << "</style>";
  else
    OS << "<link rel='stylesheet' type='text/css' href='"
       << escape(PathToStyle, Opts) << "'>";

  OS << EndHeader << "<body>";
}

void emitEpilog(raw_ostream &OS) {
  OS << "</body>"
     << "</html>";
}

} // anonymous namespace

Expected<CoveragePrinter::OwnedStream>
CoveragePrinterHTML::createViewFile(StringRef Path, bool InToplevel) {
  auto OSOrErr = createOutputStream(Path, "html", InToplevel);
  if (!OSOrErr)
    return OSOrErr;

  OwnedStream OS = std::move(OSOrErr.get());

  if (!Opts.hasOutputDirectory()) {
    emitPrelude(*OS.get(), Opts);
  } else {
    std::string ViewPath = getOutputPath(Path, "html", InToplevel);
    emitPrelude(*OS.get(), Opts, getPathToStyle(ViewPath));
  }

  return std::move(OS);
}

void CoveragePrinterHTML::closeViewFile(OwnedStream OS) {
  emitEpilog(*OS.get());
}

/// Emit column labels for the table in the index.
static void emitColumnLabelsForIndex(raw_ostream &OS,
                                     const CoverageViewOptions &Opts) {
  SmallVector<std::string, 4> Columns;
  Columns.emplace_back(tag("td", "Filename", "column-entry-bold"));
  Columns.emplace_back(tag("td", "Function Coverage", "column-entry-bold"));
  if (Opts.ShowInstantiationSummary)
    Columns.emplace_back(
        tag("td", "Instantiation Coverage", "column-entry-bold"));
  Columns.emplace_back(tag("td", "Line Coverage", "column-entry-bold"));
  if (Opts.ShowRegionSummary)
    Columns.emplace_back(tag("td", "Region Coverage", "column-entry-bold"));
  OS << tag("tr", join(Columns.begin(), Columns.end(), ""));
}

std::string
CoveragePrinterHTML::buildLinkToFile(StringRef SF,
                                     const FileCoverageSummary &FCS) const {
  SmallString<128> LinkTextStr(sys::path::relative_path(FCS.Name));
  sys::path::remove_dots(LinkTextStr, /*remove_dot_dots=*/true);
  sys::path::native(LinkTextStr);
  std::string LinkText = escape(LinkTextStr, Opts);
  std::string LinkTarget =
      escape(getOutputPath(SF, "html", /*InToplevel=*/false), Opts);
  return a(LinkTarget, LinkText);
}

/// Render a file coverage summary (\p FCS) in a table row. If \p IsTotals is
/// false, link the summary to \p SF.
void CoveragePrinterHTML::emitFileSummary(raw_ostream &OS, StringRef SF,
                                          const FileCoverageSummary &FCS,
                                          bool IsTotals) const {
  SmallVector<std::string, 8> Columns;

  // Format a coverage triple and add the result to the list of columns.
  auto AddCoverageTripleToColumn = [&Columns](unsigned Hit, unsigned Total,
                                              float Pctg) {
    std::string S;
    {
      raw_string_ostream RSO{S};
      if (Total)
        RSO << format("%*.2f", 7, Pctg) << "% ";
      else
        RSO << "- ";
      RSO << '(' << Hit << '/' << Total << ')';
    }
    const char *CellClass = "column-entry-yellow";
    if (Hit == Total)
      CellClass = "column-entry-green";
    else if (Pctg < 80.0)
      CellClass = "column-entry-red";
    Columns.emplace_back(tag("td", tag("pre", S), CellClass));
  };

  // Simplify the display file path, and wrap it in a link if requested.
  std::string Filename;
  if (IsTotals) {
    Filename = SF;
  } else {
    Filename = buildLinkToFile(SF, FCS);
  }

  Columns.emplace_back(tag("td", tag("pre", Filename)));
  AddCoverageTripleToColumn(FCS.FunctionCoverage.getExecuted(),
                            FCS.FunctionCoverage.getNumFunctions(),
                            FCS.FunctionCoverage.getPercentCovered());
  if (Opts.ShowInstantiationSummary)
    AddCoverageTripleToColumn(FCS.InstantiationCoverage.getExecuted(),
                              FCS.InstantiationCoverage.getNumFunctions(),
                              FCS.InstantiationCoverage.getPercentCovered());
  AddCoverageTripleToColumn(FCS.LineCoverage.getCovered(),
                            FCS.LineCoverage.getNumLines(),
                            FCS.LineCoverage.getPercentCovered());
  if (Opts.ShowRegionSummary)
    AddCoverageTripleToColumn(FCS.RegionCoverage.getCovered(),
                              FCS.RegionCoverage.getNumRegions(),
                              FCS.RegionCoverage.getPercentCovered());

  if (IsTotals)
    OS << tag("tr", join(Columns.begin(), Columns.end(), ""), "light-row-bold");
  else
    OS << tag("tr", join(Columns.begin(), Columns.end(), ""), "light-row");
}

Error CoveragePrinterHTML::createIndexFile(
    ArrayRef<std::string> SourceFiles, const CoverageMapping &Coverage,
    const CoverageFiltersMatchAll &Filters) {
  // Emit the default stylesheet.
  auto CSSOrErr = createOutputStream("style", "css", /*InToplevel=*/true);
  if (Error E = CSSOrErr.takeError())
    return E;

  OwnedStream CSS = std::move(CSSOrErr.get());
  CSS->operator<<(CSSForCoverage);

  // Emit a file index along with some coverage statistics.
  auto OSOrErr = createOutputStream("index", "html", /*InToplevel=*/true);
  if (Error E = OSOrErr.takeError())
    return E;
  auto OS = std::move(OSOrErr.get());
  raw_ostream &OSRef = *OS.get();

  assert(Opts.hasOutputDirectory() && "No output directory for index file");
  emitPrelude(OSRef, Opts, getPathToStyle(""));

  // Emit some basic information about the coverage report.
  if (Opts.hasProjectTitle())
    OSRef << tag(ProjectTitleTag, escape(Opts.ProjectTitle, Opts));
  OSRef << tag(ReportTitleTag, "Coverage Report");
  if (Opts.hasCreatedTime())
    OSRef << tag(CreatedTimeTag, escape(Opts.CreatedTimeStr, Opts));

  // Emit a link to some documentation.
  OSRef << tag("p", "Click " +
                        a("http://clang.llvm.org/docs/"
                          "SourceBasedCodeCoverage.html#interpreting-reports",
                          "here") +
                        " for information about interpreting this report.");

  // Emit a table containing links to reports for each file in the covmapping.
  // Exclude files which don't contain any regions.
  OSRef << BeginCenteredDiv << BeginTable;
  emitColumnLabelsForIndex(OSRef, Opts);
  FileCoverageSummary Totals("TOTALS");
  auto FileReports = CoverageReport::prepareFileReports(
      Coverage, Totals, SourceFiles, Opts, Filters);
  bool EmptyFiles = false;
  for (unsigned I = 0, E = FileReports.size(); I < E; ++I) {
    if (FileReports[I].FunctionCoverage.getNumFunctions())
      emitFileSummary(OSRef, SourceFiles[I], FileReports[I]);
    else
      EmptyFiles = true;
  }
  emitFileSummary(OSRef, "Totals", Totals, /*IsTotals=*/true);
  OSRef << EndTable << EndCenteredDiv;

  // Emit links to files which don't contain any functions. These are normally
  // not very useful, but could be relevant for code which abuses the
  // preprocessor.
  if (EmptyFiles && Filters.empty()) {
    OSRef << tag("p", "Files which contain no functions. (These "
                      "files contain code pulled into other files "
                      "by the preprocessor.)\n");
    OSRef << BeginCenteredDiv << BeginTable;
    for (unsigned I = 0, E = FileReports.size(); I < E; ++I)
      if (!FileReports[I].FunctionCoverage.getNumFunctions()) {
        std::string Link = buildLinkToFile(SourceFiles[I], FileReports[I]);
        OSRef << tag("tr", tag("td", tag("pre", Link)), "light-row") << '\n';
      }
    OSRef << EndTable << EndCenteredDiv;
  }

  OSRef << tag("h5", escape(Opts.getLLVMVersionString(), Opts));
  emitEpilog(OSRef);

  return Error::success();
}

void SourceCoverageViewHTML::renderViewHeader(raw_ostream &OS) {
  OS << BeginCenteredDiv << BeginTable;
}

void SourceCoverageViewHTML::renderViewFooter(raw_ostream &OS) {
  OS << EndTable << EndCenteredDiv;
}

void SourceCoverageViewHTML::renderSourceName(raw_ostream &OS, bool WholeFile) {
  OS << BeginSourceNameDiv << tag("pre", escape(getSourceName(), getOptions()))
     << EndSourceNameDiv;
}

void SourceCoverageViewHTML::renderLinePrefix(raw_ostream &OS, unsigned) {
  OS << "<tr>";
}

void SourceCoverageViewHTML::renderLineSuffix(raw_ostream &OS, unsigned) {
  // If this view has sub-views, renderLine() cannot close the view's cell.
  // Take care of it here, after all sub-views have been rendered.
  if (hasSubViews())
    OS << EndCodeTD;
  OS << "</tr>";
}

void SourceCoverageViewHTML::renderViewDivider(raw_ostream &, unsigned) {
  // The table-based output makes view dividers unnecessary.
}

void SourceCoverageViewHTML::renderLine(raw_ostream &OS, LineRef L,
                                        const LineCoverageStats &LCS,
                                        unsigned ExpansionCol, unsigned) {
  StringRef Line = L.Line;
  unsigned LineNo = L.LineNo;

  // Steps for handling text-escaping, highlighting, and tooltip creation:
  //
  // 1. Split the line into N+1 snippets, where N = |Segments|. The first
  //    snippet starts from Col=1 and ends at the start of the first segment.
  //    The last snippet starts at the last mapped column in the line and ends
  //    at the end of the line. Both are required but may be empty.

  SmallVector<std::string, 8> Snippets;
  CoverageSegmentArray Segments = LCS.getLineSegments();

  unsigned LCol = 1;
  auto Snip = [&](unsigned Start, unsigned Len) {
    Snippets.push_back(Line.substr(Start, Len));
    LCol += Len;
  };

  Snip(LCol - 1, Segments.empty() ? 0 : (Segments.front()->Col - 1));

  for (unsigned I = 1, E = Segments.size(); I < E; ++I)
    Snip(LCol - 1, Segments[I]->Col - LCol);

  // |Line| + 1 is needed to avoid underflow when, e.g |Line| = 0 and LCol = 1.
  Snip(LCol - 1, Line.size() + 1 - LCol);

  // 2. Escape all of the snippets.

  for (unsigned I = 0, E = Snippets.size(); I < E; ++I)
    Snippets[I] = escape(Snippets[I], getOptions());

  // 3. Use \p WrappedSegment to set the highlight for snippet 0. Use segment
  //    1 to set the highlight for snippet 2, segment 2 to set the highlight for
  //    snippet 3, and so on.

  Optional<StringRef> Color;
  SmallVector<std::pair<unsigned, unsigned>, 2> HighlightedRanges;
  auto Highlight = [&](const std::string &Snippet, unsigned LC, unsigned RC) {
    if (getOptions().Debug)
      HighlightedRanges.emplace_back(LC, RC);
    return tag("span", Snippet, Color.getValue());
  };

  auto CheckIfUncovered = [&](const CoverageSegment *S) {
    return S && (!S->IsGapRegion || (Color && *Color == "red")) &&
           S->HasCount && S->Count == 0;
  };

  if (CheckIfUncovered(LCS.getWrappedSegment())) {
    Color = "red";
    if (!Snippets[0].empty())
      Snippets[0] = Highlight(Snippets[0], 1, 1 + Snippets[0].size());
  }

  for (unsigned I = 0, E = Segments.size(); I < E; ++I) {
    const auto *CurSeg = Segments[I];
    if (CheckIfUncovered(CurSeg))
      Color = "red";
    else if (CurSeg->Col == ExpansionCol)
      Color = "cyan";
    else
      Color = None;

    if (Color.hasValue())
      Snippets[I + 1] = Highlight(Snippets[I + 1], CurSeg->Col,
                                  CurSeg->Col + Snippets[I + 1].size());
  }

  if (Color.hasValue() && Segments.empty())
    Snippets.back() = Highlight(Snippets.back(), 1, 1 + Snippets.back().size());

  if (getOptions().Debug) {
    for (const auto &Range : HighlightedRanges) {
      errs() << "Highlighted line " << LineNo << ", " << Range.first << " -> ";
      if (Range.second == 0)
        errs() << "?";
      else
        errs() << Range.second;
      errs() << "\n";
    }
  }

  // 4. Snippets[1:N+1] correspond to \p Segments[0:N]: use these to generate
  //    sub-line region count tooltips if needed.

  if (shouldRenderRegionMarkers(LCS)) {
    // Just consider the segments which start *and* end on this line.
    for (unsigned I = 0, E = Segments.size() - 1; I < E; ++I) {
      const auto *CurSeg = Segments[I];
      if (!CurSeg->IsRegionEntry)
        continue;
      if (CurSeg->Count == LCS.getExecutionCount())
        continue;

      Snippets[I + 1] =
          tag("div", Snippets[I + 1] + tag("span", formatCount(CurSeg->Count),
                                           "tooltip-content"),
              "tooltip");

      if (getOptions().Debug)
        errs() << "Marker at " << CurSeg->Line << ":" << CurSeg->Col << " = "
               << formatCount(CurSeg->Count) << "\n";
    }
  }

  OS << BeginCodeTD;
  OS << BeginPre;
  for (const auto &Snippet : Snippets)
    OS << Snippet;
  OS << EndPre;

  // If there are no sub-views left to attach to this cell, end the cell.
  // Otherwise, end it after the sub-views are rendered (renderLineSuffix()).
  if (!hasSubViews())
    OS << EndCodeTD;
}

void SourceCoverageViewHTML::renderLineCoverageColumn(
    raw_ostream &OS, const LineCoverageStats &Line) {
  std::string Count = "";
  if (Line.isMapped())
    Count = tag("pre", formatCount(Line.getExecutionCount()));
  std::string CoverageClass =
      (Line.getExecutionCount() > 0) ? "covered-line" : "uncovered-line";
  OS << tag("td", Count, CoverageClass);
}

void SourceCoverageViewHTML::renderLineNumberColumn(raw_ostream &OS,
                                                    unsigned LineNo) {
  std::string LineNoStr = utostr(uint64_t(LineNo));
  std::string TargetName = "L" + LineNoStr;
  OS << tag("td", a("#" + TargetName, tag("pre", LineNoStr), TargetName),
            "line-number");
}

void SourceCoverageViewHTML::renderRegionMarkers(raw_ostream &,
                                                 const LineCoverageStats &Line,
                                                 unsigned) {
  // Region markers are rendered in-line using tooltips.
}

void SourceCoverageViewHTML::renderExpansionSite(raw_ostream &OS, LineRef L,
                                                 const LineCoverageStats &LCS,
                                                 unsigned ExpansionCol,
                                                 unsigned ViewDepth) {
  // Render the line containing the expansion site. No extra formatting needed.
  renderLine(OS, L, LCS, ExpansionCol, ViewDepth);
}

void SourceCoverageViewHTML::renderExpansionView(raw_ostream &OS,
                                                 ExpansionView &ESV,
                                                 unsigned ViewDepth) {
  OS << BeginExpansionDiv;
  ESV.View->print(OS, /*WholeFile=*/false, /*ShowSourceName=*/false,
                  /*ShowTitle=*/false, ViewDepth + 1);
  OS << EndExpansionDiv;
}

void SourceCoverageViewHTML::renderInstantiationView(raw_ostream &OS,
                                                     InstantiationView &ISV,
                                                     unsigned ViewDepth) {
  OS << BeginExpansionDiv;
  if (!ISV.View)
    OS << BeginSourceNameDiv
       << tag("pre",
              escape("Unexecuted instantiation: " + ISV.FunctionName.str(),
                     getOptions()))
       << EndSourceNameDiv;
  else
    ISV.View->print(OS, /*WholeFile=*/false, /*ShowSourceName=*/true,
                    /*ShowTitle=*/false, ViewDepth);
  OS << EndExpansionDiv;
}

void SourceCoverageViewHTML::renderTitle(raw_ostream &OS, StringRef Title) {
  if (getOptions().hasProjectTitle())
    OS << tag(ProjectTitleTag, escape(getOptions().ProjectTitle, getOptions()));
  OS << tag(ReportTitleTag, escape(Title, getOptions()));
  if (getOptions().hasCreatedTime())
    OS << tag(CreatedTimeTag,
              escape(getOptions().CreatedTimeStr, getOptions()));
}

void SourceCoverageViewHTML::renderTableHeader(raw_ostream &OS,
                                               unsigned FirstUncoveredLineNo,
                                               unsigned ViewDepth) {
  std::string SourceLabel;
  if (FirstUncoveredLineNo == 0) {
    SourceLabel = tag("td", tag("pre", "Source"));
  } else {
    std::string LinkTarget = "#L" + utostr(uint64_t(FirstUncoveredLineNo));
    SourceLabel =
        tag("td", tag("pre", "Source (" +
                                 a(LinkTarget, "jump to first uncovered line") +
                                 ")"));
  }

  renderLinePrefix(OS, ViewDepth);
  OS << tag("td", tag("pre", "Line")) << tag("td", tag("pre", "Count"))
     << SourceLabel;
  renderLineSuffix(OS, ViewDepth);
}
