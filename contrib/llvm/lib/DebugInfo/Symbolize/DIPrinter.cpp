//===- lib/DebugInfo/Symbolize/DIPrinter.cpp ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the DIPrinter class, which is responsible for printing
// structures defined in DebugInfo/DIContext.h
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/Symbolize/DIPrinter.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace llvm {
namespace symbolize {

// By default, DILineInfo contains "<invalid>" for function/filename it
// cannot fetch. We replace it to "??" to make our output closer to addr2line.
static const char kDILineInfoBadString[] = "<invalid>";
static const char kBadString[] = "??";

// Prints source code around in the FileName the Line.
void DIPrinter::printContext(const std::string &FileName, int64_t Line) {
  if (PrintSourceContext <= 0)
    return;

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFile(FileName);
  if (!BufOrErr)
    return;

  std::unique_ptr<MemoryBuffer> Buf = std::move(BufOrErr.get());
  int64_t FirstLine =
      std::max(static_cast<int64_t>(1), Line - PrintSourceContext / 2);
  int64_t LastLine = FirstLine + PrintSourceContext;
  size_t MaxLineNumberWidth = std::ceil(std::log10(LastLine));

  for (line_iterator I = line_iterator(*Buf, false);
       !I.is_at_eof() && I.line_number() <= LastLine; ++I) {
    int64_t L = I.line_number();
    if (L >= FirstLine && L <= LastLine) {
      OS << format_decimal(L, MaxLineNumberWidth);
      if (L == Line)
        OS << " >: ";
      else
        OS << "  : ";
      OS << *I << "\n";
    }
  }
}

void DIPrinter::print(const DILineInfo &Info, bool Inlined) {
  if (PrintFunctionNames) {
    std::string FunctionName = Info.FunctionName;
    if (FunctionName == kDILineInfoBadString)
      FunctionName = kBadString;

    StringRef Delimiter = PrintPretty ? " at " : "\n";
    StringRef Prefix = (PrintPretty && Inlined) ? " (inlined by) " : "";
    OS << Prefix << FunctionName << Delimiter;
  }
  std::string Filename = Info.FileName;
  if (Filename == kDILineInfoBadString)
    Filename = kBadString;
  if (!Verbose) {
    OS << Filename << ":" << Info.Line << ":" << Info.Column << "\n";
    printContext(Filename, Info.Line);
    return;
  }
  OS << "  Filename: " << Filename << "\n";
  if (Info.StartLine)
    OS << "Function start line: " << Info.StartLine << "\n";
  OS << "  Line: " << Info.Line << "\n";
  OS << "  Column: " << Info.Column << "\n";
  if (Info.Discriminator)
    OS << "  Discriminator: " << Info.Discriminator << "\n";
}

DIPrinter &DIPrinter::operator<<(const DILineInfo &Info) {
  print(Info, false);
  return *this;
}

DIPrinter &DIPrinter::operator<<(const DIInliningInfo &Info) {
  uint32_t FramesNum = Info.getNumberOfFrames();
  if (FramesNum == 0) {
    print(DILineInfo(), false);
    return *this;
  }
  for (uint32_t i = 0; i < FramesNum; i++)
    print(Info.getFrame(i), i > 0);
  return *this;
}

DIPrinter &DIPrinter::operator<<(const DIGlobal &Global) {
  std::string Name = Global.Name;
  if (Name == kDILineInfoBadString)
    Name = kBadString;
  OS << Name << "\n";
  OS << Global.Start << " " << Global.Size << "\n";
  return *this;
}

} // end namespace symbolize
} // end namespace llvm
