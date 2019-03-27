//===- FuzzerDataFlowTrace.cpp - DataFlowTrace                ---*- C++ -* ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// fuzzer::DataFlowTrace
//===----------------------------------------------------------------------===//

#include "FuzzerDataFlowTrace.h"
#include "FuzzerIO.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace fuzzer {

void DataFlowTrace::Init(const std::string &DirPath,
                         const std::string &FocusFunction) {
  if (DirPath.empty()) return;
  const char *kFunctionsTxt = "functions.txt";
  Printf("INFO: DataFlowTrace: reading from '%s'\n", DirPath.c_str());
  Vector<SizedFile> Files;
  GetSizedFilesFromDir(DirPath, &Files);
  std::string L;

  // Read functions.txt
  std::ifstream IF(DirPlusFile(DirPath, kFunctionsTxt));
  size_t FocusFuncIdx = SIZE_MAX;
  size_t NumFunctions = 0;
  while (std::getline(IF, L, '\n')) {
    NumFunctions++;
    if (FocusFunction == L)
      FocusFuncIdx = NumFunctions - 1;
  }
  if (!NumFunctions || FocusFuncIdx == SIZE_MAX || Files.size() <= 1)
    return;
  // Read traces.
  size_t NumTraceFiles = 0;
  size_t NumTracesWithFocusFunction = 0;
  for (auto &SF : Files) {
    auto Name = Basename(SF.File);
    if (Name == kFunctionsTxt) continue;
    auto ParseError = [&](const char *Err) {
      Printf("DataFlowTrace: parse error: %s\n  File: %s\n  Line: %s\n", Err,
             Name.c_str(), L.c_str());
    };
    NumTraceFiles++;
    // Printf("=== %s\n", Name.c_str());
    std::ifstream IF(SF.File);
    while (std::getline(IF, L, '\n')) {
      size_t SpacePos = L.find(' ');
      if (SpacePos == std::string::npos)
        return ParseError("no space in the trace line");
      if (L.empty() || L[0] != 'F')
        return ParseError("the trace line doesn't start with 'F'");
      size_t N = std::atol(L.c_str() + 1);
      if (N >= NumFunctions)
        return ParseError("N is greater than the number of functions");
      if (N == FocusFuncIdx) {
        NumTracesWithFocusFunction++;
        const char *Beg = L.c_str() + SpacePos + 1;
        const char *End = L.c_str() + L.size();
        assert(Beg < End);
        size_t Len = End - Beg;
        Vector<uint8_t> V(Len);
        for (size_t I = 0; I < Len; I++) {
          if (Beg[I] != '0' && Beg[I] != '1')
            ParseError("the trace should contain only 0 or 1");
          V[I] = Beg[I] == '1';
        }
        Traces[Name] = V;
        // Print just a few small traces.
        if (NumTracesWithFocusFunction <= 3 && Len <= 16)
          Printf("%s => |%s|\n", Name.c_str(), L.c_str() + SpacePos + 1);
        break;  // No need to parse the following lines.
      }
    }
  }
  assert(NumTraceFiles == Files.size() - 1);
  Printf("INFO: DataFlowTrace: %zd trace files, %zd functions, "
         "%zd traces with focus function\n",
         NumTraceFiles, NumFunctions, NumTracesWithFocusFunction);
}

}  // namespace fuzzer

