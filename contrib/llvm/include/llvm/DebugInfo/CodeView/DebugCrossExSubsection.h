//===- DebugCrossExSubsection.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGCROSSEXSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGCROSSEXSUBSECTION_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <map>

namespace llvm {
namespace codeview {

class DebugCrossModuleExportsSubsectionRef final : public DebugSubsectionRef {
  using ReferenceArray = FixedStreamArray<CrossModuleExport>;
  using Iterator = ReferenceArray::Iterator;

public:
  DebugCrossModuleExportsSubsectionRef()
      : DebugSubsectionRef(DebugSubsectionKind::CrossScopeExports) {}

  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::CrossScopeExports;
  }

  Error initialize(BinaryStreamReader Reader);
  Error initialize(BinaryStreamRef Stream);

  Iterator begin() const { return References.begin(); }
  Iterator end() const { return References.end(); }

private:
  FixedStreamArray<CrossModuleExport> References;
};

class DebugCrossModuleExportsSubsection final : public DebugSubsection {
public:
  DebugCrossModuleExportsSubsection()
      : DebugSubsection(DebugSubsectionKind::CrossScopeExports) {}

  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::CrossScopeExports;
  }

  void addMapping(uint32_t Local, uint32_t Global);

  uint32_t calculateSerializedSize() const override;
  Error commit(BinaryStreamWriter &Writer) const override;

private:
  std::map<uint32_t, uint32_t> Mappings;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGCROSSEXSUBSECTION_H
