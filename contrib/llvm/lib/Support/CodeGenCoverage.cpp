//===- lib/Support/CodeGenCoverage.cpp -------------------------------------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the CodeGenCoverage class.
//===----------------------------------------------------------------------===//

#include "llvm/Support/CodeGenCoverage.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/ToolOutputFile.h"

#if LLVM_ON_UNIX
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

using namespace llvm;

static sys::SmartMutex<true> OutputMutex;

CodeGenCoverage::CodeGenCoverage() {}

void CodeGenCoverage::setCovered(uint64_t RuleID) {
  if (RuleCoverage.size() <= RuleID)
    RuleCoverage.resize(RuleID + 1, 0);
  RuleCoverage[RuleID] = true;
}

bool CodeGenCoverage::isCovered(uint64_t RuleID) const {
  if (RuleCoverage.size() <= RuleID)
    return false;
  return RuleCoverage[RuleID];
}

iterator_range<CodeGenCoverage::const_covered_iterator>
CodeGenCoverage::covered() const {
  return RuleCoverage.set_bits();
}

bool CodeGenCoverage::parse(MemoryBuffer &Buffer, StringRef BackendName) {
  const char *CurPtr = Buffer.getBufferStart();

  while (CurPtr != Buffer.getBufferEnd()) {
    // Read the backend name from the input.
    const char *LexedBackendName = CurPtr;
    while (*CurPtr++ != 0)
      ;
    if (CurPtr == Buffer.getBufferEnd())
      return false; // Data is invalid, expected rule id's to follow.

    bool IsForThisBackend = BackendName.equals(LexedBackendName);
    while (CurPtr != Buffer.getBufferEnd()) {
      if (std::distance(CurPtr, Buffer.getBufferEnd()) < 8)
        return false; // Data is invalid. Not enough bytes for another rule id.

      uint64_t RuleID = support::endian::read64(CurPtr, support::native);
      CurPtr += 8;

      // ~0ull terminates the rule id list.
      if (RuleID == ~0ull)
        break;

      // Anything else, is recorded or ignored depending on whether it's
      // intended for the backend we're interested in.
      if (IsForThisBackend)
        setCovered(RuleID);
    }
  }

  return true;
}

bool CodeGenCoverage::emit(StringRef CoveragePrefix,
                           StringRef BackendName) const {
  if (!CoveragePrefix.empty() && !RuleCoverage.empty()) {
    sys::SmartScopedLock<true> Lock(OutputMutex);

    // We can handle locking within a process easily enough but we don't want to
    // manage it between multiple processes. Use the process ID to ensure no
    // more than one process is ever writing to the same file at the same time.
    std::string Pid =
#if LLVM_ON_UNIX
        llvm::to_string(::getpid());
#elif defined(_WIN32)
        llvm::to_string(::GetCurrentProcessId());
#else
        "";
#endif

    std::string CoverageFilename = (CoveragePrefix + Pid).str();

    std::error_code EC;
    sys::fs::OpenFlags OpenFlags = sys::fs::F_Append;
    std::unique_ptr<ToolOutputFile> CoverageFile =
        llvm::make_unique<ToolOutputFile>(CoverageFilename, EC, OpenFlags);
    if (EC)
      return false;

    uint64_t Zero = 0;
    uint64_t InvZero = ~0ull;
    CoverageFile->os() << BackendName;
    CoverageFile->os().write((const char *)&Zero, sizeof(unsigned char));
    for (uint64_t I : RuleCoverage.set_bits())
      CoverageFile->os().write((const char *)&I, sizeof(uint64_t));
    CoverageFile->os().write((const char *)&InvZero, sizeof(uint64_t));

    CoverageFile->keep();
  }

  return true;
}

void CodeGenCoverage::reset() { RuleCoverage.resize(0); }
