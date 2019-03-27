//===-- ResourceProcessor.h -------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_INCLUDE_LLVM_SUPPORT_WINDOWS_RESOURCE_PROCESSOR_H
#define LLVM_INCLUDE_LLVM_SUPPORT_WINDOWS_RESOURCE_PROCESSOR_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <vector>


namespace llvm {

class WindowsResourceProcessor {
public:
  using PathType = SmallVector<char, 64>;

  WindowsResourceProcessor() {}

  void addDefine(StringRef Key, StringRef Value = StringRef()) {
    PreprocessorDefines.emplace_back(Key, Value);
  }
  void addInclude(const PathType &IncludePath) {
    IncludeList.push_back(IncludePath);
  }
  void setVerbose(bool Verbose) { IsVerbose = Verbose; }
  void setNullAtEnd(bool NullAtEnd) { AppendNull = NullAtEnd; }

  Error process(StringRef InputData,
    std::unique_ptr<raw_fd_ostream> OutputStream);

private:
  StringRef InputData;
  std::vector<PathType> IncludeList;
  std::vector<std::pair<StringRef, StringRef>> PreprocessorDefines;
  bool IsVerbose, AppendNull;
};

}

#endif
