//===-- sanitizer_symbolize.cc ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of weak hooks from sanitizer_symbolizer_posix_libcdep.cc.
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <string>

#include "llvm/DebugInfo/Symbolize/DIPrinter.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"

static llvm::symbolize::LLVMSymbolizer *getDefaultSymbolizer() {
  static llvm::symbolize::LLVMSymbolizer *DefaultSymbolizer =
      new llvm::symbolize::LLVMSymbolizer();
  return DefaultSymbolizer;
}

namespace __sanitizer {
int internal_snprintf(char *buffer, unsigned long length, const char *format,
                      ...);
}  // namespace __sanitizer

extern "C" {

typedef uint64_t u64;

bool __sanitizer_symbolize_code(const char *ModuleName, uint64_t ModuleOffset,
                                char *Buffer, int MaxLength) {
  std::string Result;
  {
    llvm::raw_string_ostream OS(Result);
    llvm::symbolize::DIPrinter Printer(OS);
    auto ResOrErr =
        getDefaultSymbolizer()->symbolizeInlinedCode(ModuleName, ModuleOffset);
    Printer << (ResOrErr ? ResOrErr.get() : llvm::DIInliningInfo());
  }
  return __sanitizer::internal_snprintf(Buffer, MaxLength, "%s",
                                        Result.c_str()) < MaxLength;
}

bool __sanitizer_symbolize_data(const char *ModuleName, uint64_t ModuleOffset,
                                char *Buffer, int MaxLength) {
  std::string Result;
  {
    llvm::raw_string_ostream OS(Result);
    llvm::symbolize::DIPrinter Printer(OS);
    auto ResOrErr =
        getDefaultSymbolizer()->symbolizeData(ModuleName, ModuleOffset);
    Printer << (ResOrErr ? ResOrErr.get() : llvm::DIGlobal());
  }
  return __sanitizer::internal_snprintf(Buffer, MaxLength, "%s",
                                        Result.c_str()) < MaxLength;
}

void __sanitizer_symbolize_flush() { getDefaultSymbolizer()->flush(); }

int __sanitizer_symbolize_demangle(const char *Name, char *Buffer,
                                   int MaxLength) {
  std::string Result =
      llvm::symbolize::LLVMSymbolizer::DemangleName(Name, nullptr);
  return __sanitizer::internal_snprintf(Buffer, MaxLength, "%s",
                                        Result.c_str()) < MaxLength
             ? static_cast<int>(Result.size() + 1)
             : 0;
}

}  // extern "C"
