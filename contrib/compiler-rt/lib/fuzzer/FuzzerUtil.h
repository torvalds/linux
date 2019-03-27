//===- FuzzerUtil.h - Internal header for the Fuzzer Utils ------*- C++ -* ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Util functions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_UTIL_H
#define LLVM_FUZZER_UTIL_H

#include "FuzzerBuiltins.h"
#include "FuzzerBuiltinsMsvc.h"
#include "FuzzerCommand.h"
#include "FuzzerDefs.h"

namespace fuzzer {

void PrintHexArray(const Unit &U, const char *PrintAfter = "");

void PrintHexArray(const uint8_t *Data, size_t Size,
                   const char *PrintAfter = "");

void PrintASCII(const uint8_t *Data, size_t Size, const char *PrintAfter = "");

void PrintASCII(const Unit &U, const char *PrintAfter = "");

// Changes U to contain only ASCII (isprint+isspace) characters.
// Returns true iff U has been changed.
bool ToASCII(uint8_t *Data, size_t Size);

bool IsASCII(const Unit &U);

bool IsASCII(const uint8_t *Data, size_t Size);

std::string Base64(const Unit &U);

void PrintPC(const char *SymbolizedFMT, const char *FallbackFMT, uintptr_t PC);

std::string DescribePC(const char *SymbolizedFMT, uintptr_t PC);

void PrintStackTrace();

void PrintMemoryProfile();

unsigned NumberOfCpuCores();

// Platform specific functions.
void SetSignalHandler(const FuzzingOptions& Options);

void SleepSeconds(int Seconds);

unsigned long GetPid();

size_t GetPeakRSSMb();

int ExecuteCommand(const Command &Cmd);

FILE *OpenProcessPipe(const char *Command, const char *Mode);

const void *SearchMemory(const void *haystack, size_t haystacklen,
                         const void *needle, size_t needlelen);

std::string CloneArgsWithoutX(const Vector<std::string> &Args,
                              const char *X1, const char *X2);

inline std::string CloneArgsWithoutX(const Vector<std::string> &Args,
                                     const char *X) {
  return CloneArgsWithoutX(Args, X, X);
}

inline std::pair<std::string, std::string> SplitBefore(std::string X,
                                                       std::string S) {
  auto Pos = S.find(X);
  if (Pos == std::string::npos)
    return std::make_pair(S, "");
  return std::make_pair(S.substr(0, Pos), S.substr(Pos));
}

std::string DisassembleCmd(const std::string &FileName);

std::string SearchRegexCmd(const std::string &Regex);

size_t SimpleFastHash(const uint8_t *Data, size_t Size);

inline uint32_t Log(uint32_t X) { return 32 - Clz(X) - 1; }

}  // namespace fuzzer

#endif  // LLVM_FUZZER_UTIL_H
