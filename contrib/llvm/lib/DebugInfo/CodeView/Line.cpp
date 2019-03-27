//===-- Line.cpp ----------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/Line.h"

using namespace llvm;
using namespace codeview;

LineInfo::LineInfo(uint32_t StartLine, uint32_t EndLine, bool IsStatement) {
  LineData = StartLine & StartLineMask;
  uint32_t LineDelta = EndLine - StartLine;
  LineData |= (LineDelta << EndLineDeltaShift) & EndLineDeltaMask;
  if (IsStatement) {
    LineData |= StatementFlag;
  }
}
