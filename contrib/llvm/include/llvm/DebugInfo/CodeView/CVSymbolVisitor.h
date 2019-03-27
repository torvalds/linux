//===- CVSymbolVisitor.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CVSYMBOLVISITOR_H
#define LLVM_DEBUGINFO_CODEVIEW_CVSYMBOLVISITOR_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorDelegate.h"
#include "llvm/Support/ErrorOr.h"

namespace llvm {
namespace codeview {
class SymbolVisitorCallbacks;

class CVSymbolVisitor {
public:
  CVSymbolVisitor(SymbolVisitorCallbacks &Callbacks);

  Error visitSymbolRecord(CVSymbol &Record);
  Error visitSymbolRecord(CVSymbol &Record, uint32_t Offset);
  Error visitSymbolStream(const CVSymbolArray &Symbols);
  Error visitSymbolStream(const CVSymbolArray &Symbols, uint32_t InitialOffset);

private:
  SymbolVisitorCallbacks &Callbacks;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_CVSYMBOLVISITOR_H
