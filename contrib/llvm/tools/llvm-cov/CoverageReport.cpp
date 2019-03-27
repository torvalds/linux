//===- CoverageReport.cpp - Code coverage report -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class implements rendering of a code coverage report.
//
//===----------------------------------------------------------------------===//

#include "CoverageReport.h"
#include "RenderingSupport.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include <numeric>

using namespace llvm;

namespace {

/// Helper struct which prints trimmed and aligned columns.
struct Column {
  enum TrimKind { NoTrim, WidthTrim, RightTrim };

  enum AlignmentKind { LeftAlignment, RightAlignment };

  StringRef Str;
  unsigned Width;
  TrimKind Trim;
  AlignmentKind Alignment;

  Column(StringRef Str, unsigned Width)
      : Str(Str), Width(Width), Trim(WidthTrim), Alignment(LeftAlignment) {}

  Column &set(TrimKind Value) {
    Trim = Value;
    return *this;
  }

  Column &set(AlignmentKind Value) {
    Alignment = Value;
    return *this;
  }

  void render(raw_ostream &OS) const {
    if (Str.size() <= Width) {
      if (Alignment == RightAlignment) {
        OS.indent(Width - Str.size());
        OS << Str;
        return;
      }
      OS << Str;
      OS.indent(Width - Str.size());
      return;
    }

    switch (Trim) {
    case NoTrim:
      OS << Str;
      break;
    case WidthTrim:
      OS << Str.substr(0, Width);
      break;
    case RightTrim:
      OS << Str.substr(0, Width - 3) << "...";
      break;
    }
  }
};

raw_ostream &operator<<(raw_ostream &OS, const Column &Value) {
  Value.render(OS);
  return OS;
}

Column column(StringRef Str, unsigned Width) { return Column(Str, Width); }

template <typename T>
Column column(StringRef Str, unsigned Width, const T &Value) {
  return Column(Str, Width).set(Value);
}

// Specify the default column widths.
size_t FileReportColumns[] = {25, 12, 18, 10, 12, 18, 10,
                              16, 16, 10, 12, 18, 10};
size_t FunctionReportColumns[] = {25, 10, 8, 8, 10, 8, 8};

/// Adjust column widths to fit long file paths and function names.
void adjustColumnWidths(ArrayRef<StringRef> Files,
                        ArrayRef<StringRef> Functions) {
  for (StringRef Filename : Files)
    FileReportColumns[0] = std::max(FileReportColumns[0], Filename.size());
  for (StringRef Funcname : Functions)
    FunctionReportColumns[0] =
        std::max(FunctionReportColumns[0], Funcname.size());
}

/// Prints a horizontal divider long enough to cover the given column
/// widths.
void renderDivider(ArrayRef<size_t> ColumnWidths, raw_ostream &OS) {
  size_t Length = std::accumulate(ColumnWidths.begin(), ColumnWidths.end(), 0);
  for (size_t I = 0; I < Length; ++I)
    OS << '-';
}

/// Return the color which correponds to the coverage percentage of a
/// certain metric.
template <typename T>
raw_ostream::Colors determineCoveragePercentageColor(const T &Info) {
  if (Info.isFullyCovered())
    return raw_ostream::GREEN;
  return Info.getPercentCovered() >= 80.0 ? raw_ostream::YELLOW
                                          : raw_ostream::RED;
}

/// Get the number of redundant path components in each path in \p Paths.
unsigned getNumRedundantPathComponents(ArrayRef<std::string> Paths) {
  // To start, set the number of redundant path components to the maximum
  // possible value.
  SmallVector<StringRef, 8> FirstPathComponents{sys::path::begin(Paths[0]),
                                                sys::path::end(Paths[0])};
  unsigned NumRedundant = FirstPathComponents.size();

  for (unsigned I = 1, E = Paths.size(); NumRedundant > 0 && I < E; ++I) {
    StringRef Path = Paths[I];
    for (const auto &Component :
         enumerate(make_range(sys::path::begin(Path), sys::path::end(Path)))) {
      // Do not increase the number of redundant components: that would remove
      // useful parts of already-visited paths.
      if (Component.index() >= NumRedundant)
        break;

      // Lower the number of redundant components when there's a mismatch
      // between the first path, and the path under consideration.
      if (FirstPathComponents[Component.index()] != Component.value()) {
        NumRedundant = Component.index();
        break;
      }
    }
  }

  return NumRedundant;
}

/// Determine the length of the longest redundant prefix of the paths in
/// \p Paths.
unsigned getRedundantPrefixLen(ArrayRef<std::string> Paths) {
  // If there's at most one path, no path components are redundant.
  if (Paths.size() <= 1)
    return 0;

  unsigned PrefixLen = 0;
  unsigned NumRedundant = getNumRedundantPathComponents(Paths);
  auto Component = sys::path::begin(Paths[0]);
  for (unsigned I = 0; I < NumRedundant; ++I) {
    auto LastComponent = Component;
    ++Component;
    PrefixLen += Component - LastComponent;
  }
  return PrefixLen;
}

} // end anonymous namespace

namespace llvm {

void CoverageReport::render(const FileCoverageSummary &File,
                            raw_ostream &OS) const {
  auto FileCoverageColor =
      determineCoveragePercentageColor(File.RegionCoverage);
  auto FuncCoverageColor =
      determineCoveragePercentageColor(File.FunctionCoverage);
  auto InstantiationCoverageColor =
      determineCoveragePercentageColor(File.InstantiationCoverage);
  auto LineCoverageColor = determineCoveragePercentageColor(File.LineCoverage);
  SmallString<256> FileName = File.Name;
  sys::path::remove_dots(FileName, /*remove_dot_dots=*/true);
  sys::path::native(FileName);
  OS << column(FileName, FileReportColumns[0], Column::NoTrim);

  if (Options.ShowRegionSummary) {
    OS << format("%*u", FileReportColumns[1],
                 (unsigned)File.RegionCoverage.getNumRegions());
    Options.colored_ostream(OS, FileCoverageColor)
        << format("%*u", FileReportColumns[2],
                  (unsigned)(File.RegionCoverage.getNumRegions() -
                             File.RegionCoverage.getCovered()));
    if (File.RegionCoverage.getNumRegions())
      Options.colored_ostream(OS, FileCoverageColor)
          << format("%*.2f", FileReportColumns[3] - 1,
                    File.RegionCoverage.getPercentCovered())
          << '%';
    else
      OS << column("-", FileReportColumns[3], Column::RightAlignment);
  }

  OS << format("%*u", FileReportColumns[4],
               (unsigned)File.FunctionCoverage.getNumFunctions());
  OS << format("%*u", FileReportColumns[5],
               (unsigned)(File.FunctionCoverage.getNumFunctions() -
                          File.FunctionCoverage.getExecuted()));
  if (File.FunctionCoverage.getNumFunctions())
    Options.colored_ostream(OS, FuncCoverageColor)
        << format("%*.2f", FileReportColumns[6] - 1,
                  File.FunctionCoverage.getPercentCovered())
        << '%';
  else
    OS << column("-", FileReportColumns[6], Column::RightAlignment);

  if (Options.ShowInstantiationSummary) {
    OS << format("%*u", FileReportColumns[7],
                 (unsigned)File.InstantiationCoverage.getNumFunctions());
    OS << format("%*u", FileReportColumns[8],
                 (unsigned)(File.InstantiationCoverage.getNumFunctions() -
                            File.InstantiationCoverage.getExecuted()));
    if (File.InstantiationCoverage.getNumFunctions())
      Options.colored_ostream(OS, InstantiationCoverageColor)
          << format("%*.2f", FileReportColumns[9] - 1,
                    File.InstantiationCoverage.getPercentCovered())
          << '%';
    else
      OS << column("-", FileReportColumns[9], Column::RightAlignment);
  }

  OS << format("%*u", FileReportColumns[10],
               (unsigned)File.LineCoverage.getNumLines());
  Options.colored_ostream(OS, LineCoverageColor) << format(
      "%*u", FileReportColumns[11], (unsigned)(File.LineCoverage.getNumLines() -
                                               File.LineCoverage.getCovered()));
  if (File.LineCoverage.getNumLines())
    Options.colored_ostream(OS, LineCoverageColor)
        << format("%*.2f", FileReportColumns[12] - 1,
                  File.LineCoverage.getPercentCovered())
        << '%';
  else
    OS << column("-", FileReportColumns[12], Column::RightAlignment);
  OS << "\n";
}

void CoverageReport::render(const FunctionCoverageSummary &Function,
                            const DemangleCache &DC,
                            raw_ostream &OS) const {
  auto FuncCoverageColor =
      determineCoveragePercentageColor(Function.RegionCoverage);
  auto LineCoverageColor =
      determineCoveragePercentageColor(Function.LineCoverage);
  OS << column(DC.demangle(Function.Name), FunctionReportColumns[0],
               Column::RightTrim)
     << format("%*u", FunctionReportColumns[1],
               (unsigned)Function.RegionCoverage.getNumRegions());
  Options.colored_ostream(OS, FuncCoverageColor)
      << format("%*u", FunctionReportColumns[2],
                (unsigned)(Function.RegionCoverage.getNumRegions() -
                           Function.RegionCoverage.getCovered()));
  Options.colored_ostream(
      OS, determineCoveragePercentageColor(Function.RegionCoverage))
      << format("%*.2f", FunctionReportColumns[3] - 1,
                Function.RegionCoverage.getPercentCovered())
      << '%';
  OS << format("%*u", FunctionReportColumns[4],
               (unsigned)Function.LineCoverage.getNumLines());
  Options.colored_ostream(OS, LineCoverageColor)
      << format("%*u", FunctionReportColumns[5],
                (unsigned)(Function.LineCoverage.getNumLines() -
                           Function.LineCoverage.getCovered()));
  Options.colored_ostream(
      OS, determineCoveragePercentageColor(Function.LineCoverage))
      << format("%*.2f", FunctionReportColumns[6] - 1,
                Function.LineCoverage.getPercentCovered())
      << '%';
  OS << "\n";
}

void CoverageReport::renderFunctionReports(ArrayRef<std::string> Files,
                                           const DemangleCache &DC,
                                           raw_ostream &OS) {
  bool isFirst = true;
  for (StringRef Filename : Files) {
    auto Functions = Coverage.getCoveredFunctions(Filename);

    if (isFirst)
      isFirst = false;
    else
      OS << "\n";

    std::vector<StringRef> Funcnames;
    for (const auto &F : Functions)
      Funcnames.emplace_back(DC.demangle(F.Name));
    adjustColumnWidths({}, Funcnames);

    OS << "File '" << Filename << "':\n";
    OS << column("Name", FunctionReportColumns[0])
       << column("Regions", FunctionReportColumns[1], Column::RightAlignment)
       << column("Miss", FunctionReportColumns[2], Column::RightAlignment)
       << column("Cover", FunctionReportColumns[3], Column::RightAlignment)
       << column("Lines", FunctionReportColumns[4], Column::RightAlignment)
       << column("Miss", FunctionReportColumns[5], Column::RightAlignment)
       << column("Cover", FunctionReportColumns[6], Column::RightAlignment);
    OS << "\n";
    renderDivider(FunctionReportColumns, OS);
    OS << "\n";
    FunctionCoverageSummary Totals("TOTAL");
    for (const auto &F : Functions) {
      auto Function = FunctionCoverageSummary::get(Coverage, F);
      ++Totals.ExecutionCount;
      Totals.RegionCoverage += Function.RegionCoverage;
      Totals.LineCoverage += Function.LineCoverage;
      render(Function, DC, OS);
    }
    if (Totals.ExecutionCount) {
      renderDivider(FunctionReportColumns, OS);
      OS << "\n";
      render(Totals, DC, OS);
    }
  }
}

void CoverageReport::prepareSingleFileReport(const StringRef Filename,
    const coverage::CoverageMapping *Coverage,
    const CoverageViewOptions &Options, const unsigned LCP,
    FileCoverageSummary *FileReport, const CoverageFilter *Filters) {
  for (const auto &Group : Coverage->getInstantiationGroups(Filename)) {
    std::vector<FunctionCoverageSummary> InstantiationSummaries;
    for (const coverage::FunctionRecord *F : Group.getInstantiations()) {
      if (!Filters->matches(*Coverage, *F))
        continue;
      auto InstantiationSummary = FunctionCoverageSummary::get(*Coverage, *F);
      FileReport->addInstantiation(InstantiationSummary);
      InstantiationSummaries.push_back(InstantiationSummary);
    }
    if (InstantiationSummaries.empty())
      continue;

    auto GroupSummary =
        FunctionCoverageSummary::get(Group, InstantiationSummaries);

    if (Options.Debug)
      outs() << "InstantiationGroup: " << GroupSummary.Name << " with "
             << "size = " << Group.size() << "\n";

    FileReport->addFunction(GroupSummary);
  }
}

std::vector<FileCoverageSummary> CoverageReport::prepareFileReports(
    const coverage::CoverageMapping &Coverage, FileCoverageSummary &Totals,
    ArrayRef<std::string> Files, const CoverageViewOptions &Options,
    const CoverageFilter &Filters) {
  unsigned LCP = getRedundantPrefixLen(Files);
  auto NumThreads = Options.NumThreads;

  // If NumThreads is not specified, auto-detect a good default.
  if (NumThreads == 0)
    NumThreads =
        std::max(1U, std::min(llvm::heavyweight_hardware_concurrency(),
                              unsigned(Files.size())));

  ThreadPool Pool(NumThreads);

  std::vector<FileCoverageSummary> FileReports;
  FileReports.reserve(Files.size());

  for (StringRef Filename : Files) {
    FileReports.emplace_back(Filename.drop_front(LCP));
    Pool.async(&CoverageReport::prepareSingleFileReport, Filename,
               &Coverage, Options, LCP, &FileReports.back(), &Filters);
  }
  Pool.wait();

  for (const auto &FileReport : FileReports)
    Totals += FileReport;

  return FileReports;
}

void CoverageReport::renderFileReports(
    raw_ostream &OS, const CoverageFilters &IgnoreFilenameFilters) const {
  std::vector<std::string> UniqueSourceFiles;
  for (StringRef SF : Coverage.getUniqueSourceFiles()) {
    // Apply ignore source files filters.
    if (!IgnoreFilenameFilters.matchesFilename(SF))
      UniqueSourceFiles.emplace_back(SF.str());
  }
  renderFileReports(OS, UniqueSourceFiles);
}

void CoverageReport::renderFileReports(
    raw_ostream &OS, ArrayRef<std::string> Files) const {
  renderFileReports(OS, Files, CoverageFiltersMatchAll());
}

void CoverageReport::renderFileReports(
    raw_ostream &OS, ArrayRef<std::string> Files,
    const CoverageFiltersMatchAll &Filters) const {
  FileCoverageSummary Totals("TOTAL");
  auto FileReports =
      prepareFileReports(Coverage, Totals, Files, Options, Filters);

  std::vector<StringRef> Filenames;
  for (const FileCoverageSummary &FCS : FileReports)
    Filenames.emplace_back(FCS.Name);
  adjustColumnWidths(Filenames, {});

  OS << column("Filename", FileReportColumns[0]);
  if (Options.ShowRegionSummary)
    OS << column("Regions", FileReportColumns[1], Column::RightAlignment)
       << column("Missed Regions", FileReportColumns[2], Column::RightAlignment)
       << column("Cover", FileReportColumns[3], Column::RightAlignment);
  OS << column("Functions", FileReportColumns[4], Column::RightAlignment)
     << column("Missed Functions", FileReportColumns[5], Column::RightAlignment)
     << column("Executed", FileReportColumns[6], Column::RightAlignment);
  if (Options.ShowInstantiationSummary)
    OS << column("Instantiations", FileReportColumns[7], Column::RightAlignment)
       << column("Missed Insts.", FileReportColumns[8], Column::RightAlignment)
       << column("Executed", FileReportColumns[9], Column::RightAlignment);
  OS << column("Lines", FileReportColumns[10], Column::RightAlignment)
     << column("Missed Lines", FileReportColumns[11], Column::RightAlignment)
     << column("Cover", FileReportColumns[12], Column::RightAlignment) << "\n";
  renderDivider(FileReportColumns, OS);
  OS << "\n";

  bool EmptyFiles = false;
  for (const FileCoverageSummary &FCS : FileReports) {
    if (FCS.FunctionCoverage.getNumFunctions())
      render(FCS, OS);
    else
      EmptyFiles = true;
  }

  if (EmptyFiles && Filters.empty()) {
    OS << "\n"
       << "Files which contain no functions:\n";

    for (const FileCoverageSummary &FCS : FileReports)
      if (!FCS.FunctionCoverage.getNumFunctions())
        render(FCS, OS);
  }

  renderDivider(FileReportColumns, OS);
  OS << "\n";
  render(Totals, OS);
}

} // end namespace llvm
