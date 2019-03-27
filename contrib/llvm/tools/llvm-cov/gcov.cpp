//===- gcov.cpp - GCOV compatible LLVM coverage tool ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// llvm-cov is a command line tools to analyze and report coverage information.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/GCOV.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <system_error>
using namespace llvm;

static void reportCoverage(StringRef SourceFile, StringRef ObjectDir,
                           const std::string &InputGCNO,
                           const std::string &InputGCDA, bool DumpGCOV,
                           const GCOV::Options &Options) {
  SmallString<128> CoverageFileStem(ObjectDir);
  if (CoverageFileStem.empty()) {
    // If no directory was specified with -o, look next to the source file.
    CoverageFileStem = sys::path::parent_path(SourceFile);
    sys::path::append(CoverageFileStem, sys::path::stem(SourceFile));
  } else if (sys::fs::is_directory(ObjectDir))
    // A directory name was given. Use it and the source file name.
    sys::path::append(CoverageFileStem, sys::path::stem(SourceFile));
  else
    // A file was given. Ignore the source file and look next to this file.
    sys::path::replace_extension(CoverageFileStem, "");

  std::string GCNO = InputGCNO.empty()
                         ? std::string(CoverageFileStem.str()) + ".gcno"
                         : InputGCNO;
  std::string GCDA = InputGCDA.empty()
                         ? std::string(CoverageFileStem.str()) + ".gcda"
                         : InputGCDA;
  GCOVFile GF;

  ErrorOr<std::unique_ptr<MemoryBuffer>> GCNO_Buff =
      MemoryBuffer::getFileOrSTDIN(GCNO);
  if (std::error_code EC = GCNO_Buff.getError()) {
    errs() << GCNO << ": " << EC.message() << "\n";
    return;
  }
  GCOVBuffer GCNO_GB(GCNO_Buff.get().get());
  if (!GF.readGCNO(GCNO_GB)) {
    errs() << "Invalid .gcno File!\n";
    return;
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>> GCDA_Buff =
      MemoryBuffer::getFileOrSTDIN(GCDA);
  if (std::error_code EC = GCDA_Buff.getError()) {
    if (EC != errc::no_such_file_or_directory) {
      errs() << GCDA << ": " << EC.message() << "\n";
      return;
    }
    // Clear the filename to make it clear we didn't read anything.
    GCDA = "-";
  } else {
    GCOVBuffer GCDA_GB(GCDA_Buff.get().get());
    if (!GF.readGCDA(GCDA_GB)) {
      errs() << "Invalid .gcda File!\n";
      return;
    }
  }

  if (DumpGCOV)
    GF.print(errs());

  FileInfo FI(Options);
  GF.collectLineCounts(FI);
  FI.print(llvm::outs(), SourceFile, GCNO, GCDA);
}

int gcovMain(int argc, const char *argv[]) {
  cl::list<std::string> SourceFiles(cl::Positional, cl::OneOrMore,
                                    cl::desc("SOURCEFILE"));

  cl::opt<bool> AllBlocks("a", cl::Grouping, cl::init(false),
                          cl::desc("Display all basic blocks"));
  cl::alias AllBlocksA("all-blocks", cl::aliasopt(AllBlocks));

  cl::opt<bool> BranchProb("b", cl::Grouping, cl::init(false),
                           cl::desc("Display branch probabilities"));
  cl::alias BranchProbA("branch-probabilities", cl::aliasopt(BranchProb));

  cl::opt<bool> BranchCount("c", cl::Grouping, cl::init(false),
                            cl::desc("Display branch counts instead "
                                     "of percentages (requires -b)"));
  cl::alias BranchCountA("branch-counts", cl::aliasopt(BranchCount));

  cl::opt<bool> LongNames("l", cl::Grouping, cl::init(false),
                          cl::desc("Prefix filenames with the main file"));
  cl::alias LongNamesA("long-file-names", cl::aliasopt(LongNames));

  cl::opt<bool> FuncSummary("f", cl::Grouping, cl::init(false),
                            cl::desc("Show coverage for each function"));
  cl::alias FuncSummaryA("function-summaries", cl::aliasopt(FuncSummary));

  cl::opt<bool> NoOutput("n", cl::Grouping, cl::init(false),
                         cl::desc("Do not output any .gcov files"));
  cl::alias NoOutputA("no-output", cl::aliasopt(NoOutput));

  cl::opt<std::string> ObjectDir(
      "o", cl::value_desc("DIR|FILE"), cl::init(""),
      cl::desc("Find objects in DIR or based on FILE's path"));
  cl::alias ObjectDirA("object-directory", cl::aliasopt(ObjectDir));
  cl::alias ObjectDirB("object-file", cl::aliasopt(ObjectDir));

  cl::opt<bool> PreservePaths("p", cl::Grouping, cl::init(false),
                              cl::desc("Preserve path components"));
  cl::alias PreservePathsA("preserve-paths", cl::aliasopt(PreservePaths));

  cl::opt<bool> UncondBranch("u", cl::Grouping, cl::init(false),
                             cl::desc("Display unconditional branch info "
                                      "(requires -b)"));
  cl::alias UncondBranchA("unconditional-branches", cl::aliasopt(UncondBranch));

  cl::OptionCategory DebugCat("Internal and debugging options");
  cl::opt<bool> DumpGCOV("dump", cl::init(false), cl::cat(DebugCat),
                         cl::desc("Dump the gcov file to stderr"));
  cl::opt<std::string> InputGCNO("gcno", cl::cat(DebugCat), cl::init(""),
                                 cl::desc("Override inferred gcno file"));
  cl::opt<std::string> InputGCDA("gcda", cl::cat(DebugCat), cl::init(""),
                                 cl::desc("Override inferred gcda file"));

  cl::ParseCommandLineOptions(argc, argv, "LLVM code coverage tool\n");

  GCOV::Options Options(AllBlocks, BranchProb, BranchCount, FuncSummary,
                        PreservePaths, UncondBranch, LongNames, NoOutput);

  for (const auto &SourceFile : SourceFiles)
    reportCoverage(SourceFile, ObjectDir, InputGCNO, InputGCDA, DumpGCOV,
                   Options);
  return 0;
}
