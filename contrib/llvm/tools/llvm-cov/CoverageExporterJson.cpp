//===- CoverageExporterJson.cpp - Code coverage export --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements export of code coverage data to JSON.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//
// The json code coverage export follows the following format
// Root: dict => Root Element containing metadata
// -- Data: array => Homogeneous array of one or more export objects
//   -- Export: dict => Json representation of one CoverageMapping
//     -- Files: array => List of objects describing coverage for files
//       -- File: dict => Coverage for a single file
//         -- Segments: array => List of Segments contained in the file
//           -- Segment: dict => Describes a segment of the file with a counter
//         -- Expansions: array => List of expansion records
//           -- Expansion: dict => Object that descibes a single expansion
//             -- CountedRegion: dict => The region to be expanded
//             -- TargetRegions: array => List of Regions in the expansion
//               -- CountedRegion: dict => Single Region in the expansion
//         -- Summary: dict => Object summarizing the coverage for this file
//           -- LineCoverage: dict => Object summarizing line coverage
//           -- FunctionCoverage: dict => Object summarizing function coverage
//           -- RegionCoverage: dict => Object summarizing region coverage
//     -- Functions: array => List of objects describing coverage for functions
//       -- Function: dict => Coverage info for a single function
//         -- Filenames: array => List of filenames that the function relates to
//   -- Summary: dict => Object summarizing the coverage for the entire binary
//     -- LineCoverage: dict => Object summarizing line coverage
//     -- FunctionCoverage: dict => Object summarizing function coverage
//     -- InstantiationCoverage: dict => Object summarizing inst. coverage
//     -- RegionCoverage: dict => Object summarizing region coverage
//
//===----------------------------------------------------------------------===//

#include "CoverageExporterJson.h"
#include "CoverageReport.h"
#include "llvm/Support/JSON.h"

/// The semantic version combined as a string.
#define LLVM_COVERAGE_EXPORT_JSON_STR "2.0.0"

/// Unique type identifier for JSON coverage export.
#define LLVM_COVERAGE_EXPORT_JSON_TYPE_STR "llvm.coverage.json.export"

using namespace llvm;

namespace {

json::Array renderSegment(const coverage::CoverageSegment &Segment) {
  return json::Array({Segment.Line, Segment.Col, int64_t(Segment.Count),
                      Segment.HasCount, Segment.IsRegionEntry});
}

json::Array renderRegion(const coverage::CountedRegion &Region) {
  return json::Array({Region.LineStart, Region.ColumnStart, Region.LineEnd,
                      Region.ColumnEnd, int64_t(Region.ExecutionCount),
                      Region.FileID, Region.ExpandedFileID,
                      int64_t(Region.Kind)});
}

json::Array renderRegions(ArrayRef<coverage::CountedRegion> Regions) {
  json::Array RegionArray;
  for (const auto &Region : Regions)
    RegionArray.push_back(renderRegion(Region));
  return RegionArray;
}

json::Object renderExpansion(const coverage::ExpansionRecord &Expansion) {
  return json::Object(
      {{"filenames", json::Array(Expansion.Function.Filenames)},
       // Mark the beginning and end of this expansion in the source file.
       {"source_region", renderRegion(Expansion.Region)},
       // Enumerate the coverage information for the expansion.
       {"target_regions", renderRegions(Expansion.Function.CountedRegions)}});
}

json::Object renderSummary(const FileCoverageSummary &Summary) {
  return json::Object(
      {{"lines",
        json::Object({{"count", int64_t(Summary.LineCoverage.getNumLines())},
                      {"covered", int64_t(Summary.LineCoverage.getCovered())},
                      {"percent", Summary.LineCoverage.getPercentCovered()}})},
       {"functions",
        json::Object(
            {{"count", int64_t(Summary.FunctionCoverage.getNumFunctions())},
             {"covered", int64_t(Summary.FunctionCoverage.getExecuted())},
             {"percent", Summary.FunctionCoverage.getPercentCovered()}})},
       {"instantiations",
        json::Object(
            {{"count",
              int64_t(Summary.InstantiationCoverage.getNumFunctions())},
             {"covered", int64_t(Summary.InstantiationCoverage.getExecuted())},
             {"percent", Summary.InstantiationCoverage.getPercentCovered()}})},
       {"regions",
        json::Object(
            {{"count", int64_t(Summary.RegionCoverage.getNumRegions())},
             {"covered", int64_t(Summary.RegionCoverage.getCovered())},
             {"notcovered", int64_t(Summary.RegionCoverage.getNumRegions() -
                                    Summary.RegionCoverage.getCovered())},
             {"percent", Summary.RegionCoverage.getPercentCovered()}})}});
}

json::Array renderFileExpansions(const coverage::CoverageData &FileCoverage,
                                 const FileCoverageSummary &FileReport) {
  json::Array ExpansionArray;
  for (const auto &Expansion : FileCoverage.getExpansions())
    ExpansionArray.push_back(renderExpansion(Expansion));
  return ExpansionArray;
}

json::Array renderFileSegments(const coverage::CoverageData &FileCoverage,
                               const FileCoverageSummary &FileReport) {
  json::Array SegmentArray;
  for (const auto &Segment : FileCoverage)
    SegmentArray.push_back(renderSegment(Segment));
  return SegmentArray;
}

json::Object renderFile(const coverage::CoverageMapping &Coverage,
                        const std::string &Filename,
                        const FileCoverageSummary &FileReport,
                        bool ExportSummaryOnly) {
  json::Object File({{"filename", Filename}});
  if (!ExportSummaryOnly) {
    // Calculate and render detailed coverage information for given file.
    auto FileCoverage = Coverage.getCoverageForFile(Filename);
    File["segments"] = renderFileSegments(FileCoverage, FileReport);
    File["expansions"] = renderFileExpansions(FileCoverage, FileReport);
  }
  File["summary"] = renderSummary(FileReport);
  return File;
}

json::Array renderFiles(const coverage::CoverageMapping &Coverage,
                        ArrayRef<std::string> SourceFiles,
                        ArrayRef<FileCoverageSummary> FileReports,
                        bool ExportSummaryOnly) {
  json::Array FileArray;
  for (unsigned I = 0, E = SourceFiles.size(); I < E; ++I)
    FileArray.push_back(renderFile(Coverage, SourceFiles[I], FileReports[I],
                                   ExportSummaryOnly));
  return FileArray;
}

json::Array renderFunctions(
    const iterator_range<coverage::FunctionRecordIterator> &Functions) {
  json::Array FunctionArray;
  for (const auto &F : Functions)
    FunctionArray.push_back(
        json::Object({{"name", F.Name},
                      {"count", int64_t(F.ExecutionCount)},
                      {"regions", renderRegions(F.CountedRegions)},
                      {"filenames", json::Array(F.Filenames)}}));
  return FunctionArray;
}

} // end anonymous namespace

void CoverageExporterJson::renderRoot(const CoverageFilters &IgnoreFilters) {
  std::vector<std::string> SourceFiles;
  for (StringRef SF : Coverage.getUniqueSourceFiles()) {
    if (!IgnoreFilters.matchesFilename(SF))
      SourceFiles.emplace_back(SF);
  }
  renderRoot(SourceFiles);
}

void CoverageExporterJson::renderRoot(ArrayRef<std::string> SourceFiles) {
  FileCoverageSummary Totals = FileCoverageSummary("Totals");
  auto FileReports = CoverageReport::prepareFileReports(Coverage, Totals,
                                                        SourceFiles, Options);
  auto Export =
      json::Object({{"files", renderFiles(Coverage, SourceFiles, FileReports,
                                          Options.ExportSummaryOnly)},
                    {"totals", renderSummary(Totals)}});
  // Skip functions-level information for summary-only export mode.
  if (!Options.ExportSummaryOnly)
    Export["functions"] = renderFunctions(Coverage.getCoveredFunctions());

  auto ExportArray = json::Array({std::move(Export)});

  OS << json::Object({{"version", LLVM_COVERAGE_EXPORT_JSON_STR},
                      {"type", LLVM_COVERAGE_EXPORT_JSON_TYPE_STR},
                      {"data", std::move(ExportArray)}});
}
