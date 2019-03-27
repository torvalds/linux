//===- DebugSubsectionVisitor.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/DebugSubsectionVisitor.h"

#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugCrossExSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugCrossImpSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugInlineeLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/DebugInfo/CodeView/DebugSymbolRVASubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSymbolsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugUnknownSubsection.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamRef.h"

using namespace llvm;
using namespace llvm::codeview;

Error llvm::codeview::visitDebugSubsection(
    const DebugSubsectionRecord &R, DebugSubsectionVisitor &V,
    const StringsAndChecksumsRef &State) {
  BinaryStreamReader Reader(R.getRecordData());
  switch (R.kind()) {
  case DebugSubsectionKind::Lines: {
    DebugLinesSubsectionRef Fragment;
    if (auto EC = Fragment.initialize(Reader))
      return EC;

    return V.visitLines(Fragment, State);
  }
  case DebugSubsectionKind::FileChecksums: {
    DebugChecksumsSubsectionRef Fragment;
    if (auto EC = Fragment.initialize(Reader))
      return EC;

    return V.visitFileChecksums(Fragment, State);
  }
  case DebugSubsectionKind::InlineeLines: {
    DebugInlineeLinesSubsectionRef Fragment;
    if (auto EC = Fragment.initialize(Reader))
      return EC;
    return V.visitInlineeLines(Fragment, State);
  }
  case DebugSubsectionKind::CrossScopeExports: {
    DebugCrossModuleExportsSubsectionRef Section;
    if (auto EC = Section.initialize(Reader))
      return EC;
    return V.visitCrossModuleExports(Section, State);
  }
  case DebugSubsectionKind::CrossScopeImports: {
    DebugCrossModuleImportsSubsectionRef Section;
    if (auto EC = Section.initialize(Reader))
      return EC;
    return V.visitCrossModuleImports(Section, State);
  }
  case DebugSubsectionKind::Symbols: {
    DebugSymbolsSubsectionRef Section;
    if (auto EC = Section.initialize(Reader))
      return EC;
    return V.visitSymbols(Section, State);
  }
  case DebugSubsectionKind::StringTable: {
    DebugStringTableSubsectionRef Section;
    if (auto EC = Section.initialize(Reader))
      return EC;
    return V.visitStringTable(Section, State);
  }
  case DebugSubsectionKind::FrameData: {
    DebugFrameDataSubsectionRef Section;
    if (auto EC = Section.initialize(Reader))
      return EC;
    return V.visitFrameData(Section, State);
  }
  case DebugSubsectionKind::CoffSymbolRVA: {
    DebugSymbolRVASubsectionRef Section;
    if (auto EC = Section.initialize(Reader))
      return EC;
    return V.visitCOFFSymbolRVAs(Section, State);
  }
  default: {
    DebugUnknownSubsectionRef Fragment(R.kind(), R.getRecordData());
    return V.visitUnknown(Fragment);
  }
  }
}
