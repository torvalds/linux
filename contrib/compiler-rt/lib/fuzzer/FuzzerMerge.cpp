//===- FuzzerMerge.cpp - merging corpora ----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Merging corpora.
//===----------------------------------------------------------------------===//

#include "FuzzerCommand.h"
#include "FuzzerMerge.h"
#include "FuzzerIO.h"
#include "FuzzerInternal.h"
#include "FuzzerTracePC.h"
#include "FuzzerUtil.h"

#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

namespace fuzzer {

bool Merger::Parse(const std::string &Str, bool ParseCoverage) {
  std::istringstream SS(Str);
  return Parse(SS, ParseCoverage);
}

void Merger::ParseOrExit(std::istream &IS, bool ParseCoverage) {
  if (!Parse(IS, ParseCoverage)) {
    Printf("MERGE: failed to parse the control file (unexpected error)\n");
    exit(1);
  }
}

// The control file example:
//
// 3 # The number of inputs
// 1 # The number of inputs in the first corpus, <= the previous number
// file0
// file1
// file2  # One file name per line.
// STARTED 0 123  # FileID, file size
// DONE 0 1 4 6 8  # FileID COV1 COV2 ...
// STARTED 1 456  # If DONE is missing, the input crashed while processing.
// STARTED 2 567
// DONE 2 8 9
bool Merger::Parse(std::istream &IS, bool ParseCoverage) {
  LastFailure.clear();
  std::string Line;

  // Parse NumFiles.
  if (!std::getline(IS, Line, '\n')) return false;
  std::istringstream L1(Line);
  size_t NumFiles = 0;
  L1 >> NumFiles;
  if (NumFiles == 0 || NumFiles > 10000000) return false;

  // Parse NumFilesInFirstCorpus.
  if (!std::getline(IS, Line, '\n')) return false;
  std::istringstream L2(Line);
  NumFilesInFirstCorpus = NumFiles + 1;
  L2 >> NumFilesInFirstCorpus;
  if (NumFilesInFirstCorpus > NumFiles) return false;

  // Parse file names.
  Files.resize(NumFiles);
  for (size_t i = 0; i < NumFiles; i++)
    if (!std::getline(IS, Files[i].Name, '\n'))
      return false;

  // Parse STARTED and DONE lines.
  size_t ExpectedStartMarker = 0;
  const size_t kInvalidStartMarker = -1;
  size_t LastSeenStartMarker = kInvalidStartMarker;
  Vector<uint32_t> TmpFeatures;
  while (std::getline(IS, Line, '\n')) {
    std::istringstream ISS1(Line);
    std::string Marker;
    size_t N;
    ISS1 >> Marker;
    ISS1 >> N;
    if (Marker == "STARTED") {
      // STARTED FILE_ID FILE_SIZE
      if (ExpectedStartMarker != N)
        return false;
      ISS1 >> Files[ExpectedStartMarker].Size;
      LastSeenStartMarker = ExpectedStartMarker;
      assert(ExpectedStartMarker < Files.size());
      ExpectedStartMarker++;
    } else if (Marker == "DONE") {
      // DONE FILE_ID COV1 COV2 COV3 ...
      size_t CurrentFileIdx = N;
      if (CurrentFileIdx != LastSeenStartMarker)
        return false;
      LastSeenStartMarker = kInvalidStartMarker;
      if (ParseCoverage) {
        TmpFeatures.clear();  // use a vector from outer scope to avoid resizes.
        while (ISS1 >> std::hex >> N)
          TmpFeatures.push_back(N);
        std::sort(TmpFeatures.begin(), TmpFeatures.end());
        Files[CurrentFileIdx].Features = TmpFeatures;
      }
    } else {
      return false;
    }
  }
  if (LastSeenStartMarker != kInvalidStartMarker)
    LastFailure = Files[LastSeenStartMarker].Name;

  FirstNotProcessedFile = ExpectedStartMarker;
  return true;
}

size_t Merger::ApproximateMemoryConsumption() const  {
  size_t Res = 0;
  for (const auto &F: Files)
    Res += sizeof(F) + F.Features.size() * sizeof(F.Features[0]);
  return Res;
}

// Decides which files need to be merged (add thost to NewFiles).
// Returns the number of new features added.
size_t Merger::Merge(const Set<uint32_t> &InitialFeatures,
                     Vector<std::string> *NewFiles) {
  NewFiles->clear();
  assert(NumFilesInFirstCorpus <= Files.size());
  Set<uint32_t> AllFeatures(InitialFeatures);

  // What features are in the initial corpus?
  for (size_t i = 0; i < NumFilesInFirstCorpus; i++) {
    auto &Cur = Files[i].Features;
    AllFeatures.insert(Cur.begin(), Cur.end());
  }
  size_t InitialNumFeatures = AllFeatures.size();

  // Remove all features that we already know from all other inputs.
  for (size_t i = NumFilesInFirstCorpus; i < Files.size(); i++) {
    auto &Cur = Files[i].Features;
    Vector<uint32_t> Tmp;
    std::set_difference(Cur.begin(), Cur.end(), AllFeatures.begin(),
                        AllFeatures.end(), std::inserter(Tmp, Tmp.begin()));
    Cur.swap(Tmp);
  }

  // Sort. Give preference to
  //   * smaller files
  //   * files with more features.
  std::sort(Files.begin() + NumFilesInFirstCorpus, Files.end(),
            [&](const MergeFileInfo &a, const MergeFileInfo &b) -> bool {
              if (a.Size != b.Size)
                return a.Size < b.Size;
              return a.Features.size() > b.Features.size();
            });

  // One greedy pass: add the file's features to AllFeatures.
  // If new features were added, add this file to NewFiles.
  for (size_t i = NumFilesInFirstCorpus; i < Files.size(); i++) {
    auto &Cur = Files[i].Features;
    // Printf("%s -> sz %zd ft %zd\n", Files[i].Name.c_str(),
    //       Files[i].Size, Cur.size());
    size_t OldSize = AllFeatures.size();
    AllFeatures.insert(Cur.begin(), Cur.end());
    if (AllFeatures.size() > OldSize)
      NewFiles->push_back(Files[i].Name);
  }
  return AllFeatures.size() - InitialNumFeatures;
}

void Merger::PrintSummary(std::ostream &OS) {
  for (auto &File : Files) {
    OS << std::hex;
    OS << File.Name << " size: " << File.Size << " features: ";
    for (auto Feature : File.Features)
      OS << " " << Feature;
    OS << "\n";
  }
}

Set<uint32_t> Merger::AllFeatures() const {
  Set<uint32_t> S;
  for (auto &File : Files)
    S.insert(File.Features.begin(), File.Features.end());
  return S;
}

Set<uint32_t> Merger::ParseSummary(std::istream &IS) {
  std::string Line, Tmp;
  Set<uint32_t> Res;
  while (std::getline(IS, Line, '\n')) {
    size_t N;
    std::istringstream ISS1(Line);
    ISS1 >> Tmp;  // Name
    ISS1 >> Tmp;  // size:
    assert(Tmp == "size:" && "Corrupt summary file");
    ISS1 >> std::hex;
    ISS1 >> N;    // File Size
    ISS1 >> Tmp;  // features:
    assert(Tmp == "features:" && "Corrupt summary file");
    while (ISS1 >> std::hex >> N)
      Res.insert(N);
  }
  return Res;
}

// Inner process. May crash if the target crashes.
void Fuzzer::CrashResistantMergeInternalStep(const std::string &CFPath) {
  Printf("MERGE-INNER: using the control file '%s'\n", CFPath.c_str());
  Merger M;
  std::ifstream IF(CFPath);
  M.ParseOrExit(IF, false);
  IF.close();
  if (!M.LastFailure.empty())
    Printf("MERGE-INNER: '%s' caused a failure at the previous merge step\n",
           M.LastFailure.c_str());

  Printf("MERGE-INNER: %zd total files;"
         " %zd processed earlier; will process %zd files now\n",
         M.Files.size(), M.FirstNotProcessedFile,
         M.Files.size() - M.FirstNotProcessedFile);

  std::ofstream OF(CFPath, std::ofstream::out | std::ofstream::app);
  Set<size_t> AllFeatures;
  for (size_t i = M.FirstNotProcessedFile; i < M.Files.size(); i++) {
    MaybeExitGracefully();
    auto U = FileToVector(M.Files[i].Name);
    if (U.size() > MaxInputLen) {
      U.resize(MaxInputLen);
      U.shrink_to_fit();
    }
    std::ostringstream StartedLine;
    // Write the pre-run marker.
    OF << "STARTED " << std::dec << i << " " << U.size() << "\n";
    OF.flush();  // Flush is important since Command::Execute may crash.
    // Run.
    TPC.ResetMaps();
    ExecuteCallback(U.data(), U.size());
    // Collect coverage. We are iterating over the files in this order:
    // * First, files in the initial corpus ordered by size, smallest first.
    // * Then, all other files, smallest first.
    // So it makes no sense to record all features for all files, instead we
    // only record features that were not seen before.
    Set<size_t> UniqFeatures;
    TPC.CollectFeatures([&](size_t Feature) {
      if (AllFeatures.insert(Feature).second)
        UniqFeatures.insert(Feature);
    });
    // Show stats.
    if (!(TotalNumberOfRuns & (TotalNumberOfRuns - 1)))
      PrintStats("pulse ");
    // Write the post-run marker and the coverage.
    OF << "DONE " << i;
    for (size_t F : UniqFeatures)
      OF << " " << std::hex << F;
    OF << "\n";
    OF.flush();
  }
}

static void WriteNewControlFile(const std::string &CFPath,
                                const Vector<SizedFile> &AllFiles,
                                size_t NumFilesInFirstCorpus) {
  RemoveFile(CFPath);
  std::ofstream ControlFile(CFPath);
  ControlFile << AllFiles.size() << "\n";
  ControlFile << NumFilesInFirstCorpus << "\n";
  for (auto &SF: AllFiles)
    ControlFile << SF.File << "\n";
  if (!ControlFile) {
    Printf("MERGE-OUTER: failed to write to the control file: %s\n",
           CFPath.c_str());
    exit(1);
  }
}

// Outer process. Does not call the target code and thus sohuld not fail.
void Fuzzer::CrashResistantMerge(const Vector<std::string> &Args,
                                 const Vector<std::string> &Corpora,
                                 const char *CoverageSummaryInputPathOrNull,
                                 const char *CoverageSummaryOutputPathOrNull,
                                 const char *MergeControlFilePathOrNull) {
  if (Corpora.size() <= 1) {
    Printf("Merge requires two or more corpus dirs\n");
    return;
  }
  auto CFPath =
      MergeControlFilePathOrNull
          ? MergeControlFilePathOrNull
          : DirPlusFile(TmpDir(),
                        "libFuzzerTemp." + std::to_string(GetPid()) + ".txt");

  size_t NumAttempts = 0;
  if (MergeControlFilePathOrNull && FileSize(MergeControlFilePathOrNull)) {
    Printf("MERGE-OUTER: non-empty control file provided: '%s'\n",
           MergeControlFilePathOrNull);
    Merger M;
    std::ifstream IF(MergeControlFilePathOrNull);
    if (M.Parse(IF, /*ParseCoverage=*/false)) {
      Printf("MERGE-OUTER: control file ok, %zd files total,"
             " first not processed file %zd\n",
             M.Files.size(), M.FirstNotProcessedFile);
      if (!M.LastFailure.empty())
        Printf("MERGE-OUTER: '%s' will be skipped as unlucky "
               "(merge has stumbled on it the last time)\n",
               M.LastFailure.c_str());
      if (M.FirstNotProcessedFile >= M.Files.size()) {
        Printf("MERGE-OUTER: nothing to do, merge has been completed before\n");
        exit(0);
      }

      NumAttempts = M.Files.size() - M.FirstNotProcessedFile;
    } else {
      Printf("MERGE-OUTER: bad control file, will overwrite it\n");
    }
  }

  if (!NumAttempts) {
    // The supplied control file is empty or bad, create a fresh one.
    Vector<SizedFile> AllFiles;
    GetSizedFilesFromDir(Corpora[0], &AllFiles);
    size_t NumFilesInFirstCorpus = AllFiles.size();
    std::sort(AllFiles.begin(), AllFiles.end());
    for (size_t i = 1; i < Corpora.size(); i++)
      GetSizedFilesFromDir(Corpora[i], &AllFiles);
    std::sort(AllFiles.begin() + NumFilesInFirstCorpus, AllFiles.end());
    Printf("MERGE-OUTER: %zd files, %zd in the initial corpus\n",
           AllFiles.size(), NumFilesInFirstCorpus);
    WriteNewControlFile(CFPath, AllFiles, NumFilesInFirstCorpus);
    NumAttempts = AllFiles.size();
  }

  // Execute the inner process until it passes.
  // Every inner process should execute at least one input.
  Command BaseCmd(Args);
  BaseCmd.removeFlag("merge");
  bool Success = false;
  for (size_t Attempt = 1; Attempt <= NumAttempts; Attempt++) {
    MaybeExitGracefully();
    Printf("MERGE-OUTER: attempt %zd\n", Attempt);
    Command Cmd(BaseCmd);
    Cmd.addFlag("merge_control_file", CFPath);
    Cmd.addFlag("merge_inner", "1");
    auto ExitCode = ExecuteCommand(Cmd);
    if (!ExitCode) {
      Printf("MERGE-OUTER: succesfull in %zd attempt(s)\n", Attempt);
      Success = true;
      break;
    }
  }
  if (!Success) {
    Printf("MERGE-OUTER: zero succesfull attempts, exiting\n");
    exit(1);
  }
  // Read the control file and do the merge.
  Merger M;
  std::ifstream IF(CFPath);
  IF.seekg(0, IF.end);
  Printf("MERGE-OUTER: the control file has %zd bytes\n", (size_t)IF.tellg());
  IF.seekg(0, IF.beg);
  M.ParseOrExit(IF, true);
  IF.close();
  Printf("MERGE-OUTER: consumed %zdMb (%zdMb rss) to parse the control file\n",
         M.ApproximateMemoryConsumption() >> 20, GetPeakRSSMb());
  if (CoverageSummaryOutputPathOrNull) {
    Printf("MERGE-OUTER: writing coverage summary for %zd files to %s\n",
           M.Files.size(), CoverageSummaryOutputPathOrNull);
    std::ofstream SummaryOut(CoverageSummaryOutputPathOrNull);
    M.PrintSummary(SummaryOut);
  }
  Vector<std::string> NewFiles;
  Set<uint32_t> InitialFeatures;
  if (CoverageSummaryInputPathOrNull) {
    std::ifstream SummaryIn(CoverageSummaryInputPathOrNull);
    InitialFeatures = M.ParseSummary(SummaryIn);
    Printf("MERGE-OUTER: coverage summary loaded from %s, %zd features found\n",
           CoverageSummaryInputPathOrNull, InitialFeatures.size());
  }
  size_t NumNewFeatures = M.Merge(InitialFeatures, &NewFiles);
  Printf("MERGE-OUTER: %zd new files with %zd new features added\n",
         NewFiles.size(), NumNewFeatures);
  for (auto &F: NewFiles)
    WriteToOutputCorpus(FileToVector(F, MaxInputLen));
  // We are done, delete the control file if it was a temporary one.
  if (!MergeControlFilePathOrNull)
    RemoveFile(CFPath);
}

} // namespace fuzzer
