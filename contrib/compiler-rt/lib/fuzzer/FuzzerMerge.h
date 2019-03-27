//===- FuzzerMerge.h - merging corpa ----------------------------*- C++ -* ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Merging Corpora.
//
// The task:
//   Take the existing corpus (possibly empty) and merge new inputs into
//   it so that only inputs with new coverage ('features') are added.
//   The process should tolerate the crashes, OOMs, leaks, etc.
//
// Algorithm:
//   The outter process collects the set of files and writes their names
//   into a temporary "control" file, then repeatedly launches the inner
//   process until all inputs are processed.
//   The outer process does not actually execute the target code.
//
//   The inner process reads the control file and sees a) list of all the inputs
//   and b) the last processed input. Then it starts processing the inputs one
//   by one. Before processing every input it writes one line to control file:
//   STARTED INPUT_ID INPUT_SIZE
//   After processing an input it write another line:
//   DONE INPUT_ID Feature1 Feature2 Feature3 ...
//   If a crash happens while processing an input the last line in the control
//   file will be "STARTED INPUT_ID" and so the next process will know
//   where to resume.
//
//   Once all inputs are processed by the innner process(es) the outer process
//   reads the control files and does the merge based entirely on the contents
//   of control file.
//   It uses a single pass greedy algorithm choosing first the smallest inputs
//   within the same size the inputs that have more new features.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_MERGE_H
#define LLVM_FUZZER_MERGE_H

#include "FuzzerDefs.h"

#include <istream>
#include <ostream>
#include <set>
#include <vector>

namespace fuzzer {

struct MergeFileInfo {
  std::string Name;
  size_t Size = 0;
  Vector<uint32_t> Features;
};

struct Merger {
  Vector<MergeFileInfo> Files;
  size_t NumFilesInFirstCorpus = 0;
  size_t FirstNotProcessedFile = 0;
  std::string LastFailure;

  bool Parse(std::istream &IS, bool ParseCoverage);
  bool Parse(const std::string &Str, bool ParseCoverage);
  void ParseOrExit(std::istream &IS, bool ParseCoverage);
  void PrintSummary(std::ostream &OS);
  Set<uint32_t> ParseSummary(std::istream &IS);
  size_t Merge(const Set<uint32_t> &InitialFeatures,
               Vector<std::string> *NewFiles);
  size_t Merge(Vector<std::string> *NewFiles) {
    return Merge(Set<uint32_t>{}, NewFiles);
  }
  size_t ApproximateMemoryConsumption() const;
  Set<uint32_t> AllFeatures() const;
};

}  // namespace fuzzer

#endif  // LLVM_FUZZER_MERGE_H
