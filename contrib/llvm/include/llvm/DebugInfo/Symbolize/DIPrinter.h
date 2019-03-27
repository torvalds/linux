//===- llvm/DebugInfo/Symbolize/DIPrinter.h ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the DIPrinter class, which is responsible for printing
// structures defined in DebugInfo/DIContext.h
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_SYMBOLIZE_DIPRINTER_H
#define LLVM_DEBUGINFO_SYMBOLIZE_DIPRINTER_H

#include "llvm/Support/raw_ostream.h"

namespace llvm {
struct DILineInfo;
class DIInliningInfo;
struct DIGlobal;

namespace symbolize {

class DIPrinter {
  raw_ostream &OS;
  bool PrintFunctionNames;
  bool PrintPretty;
  int PrintSourceContext;
  bool Verbose;

  void print(const DILineInfo &Info, bool Inlined);
  void printContext(const std::string &FileName, int64_t Line);

public:
  DIPrinter(raw_ostream &OS, bool PrintFunctionNames = true,
            bool PrintPretty = false, int PrintSourceContext = 0,
            bool Verbose = false)
      : OS(OS), PrintFunctionNames(PrintFunctionNames),
        PrintPretty(PrintPretty), PrintSourceContext(PrintSourceContext),
        Verbose(Verbose) {}

  DIPrinter &operator<<(const DILineInfo &Info);
  DIPrinter &operator<<(const DIInliningInfo &Info);
  DIPrinter &operator<<(const DIGlobal &Global);
};
}
}

#endif

