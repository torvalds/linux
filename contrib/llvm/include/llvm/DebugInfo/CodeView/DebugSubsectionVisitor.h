//===- DebugSubsectionVisitor.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_MODULEDEBUGFRAGMENTVISITOR_H
#define LLVM_DEBUGINFO_CODEVIEW_MODULEDEBUGFRAGMENTVISITOR_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

namespace codeview {

class DebugChecksumsSubsectionRef;
class DebugSubsectionRecord;
class DebugInlineeLinesSubsectionRef;
class DebugCrossModuleExportsSubsectionRef;
class DebugCrossModuleImportsSubsectionRef;
class DebugFrameDataSubsectionRef;
class DebugLinesSubsectionRef;
class DebugStringTableSubsectionRef;
class DebugSymbolRVASubsectionRef;
class DebugSymbolsSubsectionRef;
class DebugUnknownSubsectionRef;
class StringsAndChecksumsRef;

class DebugSubsectionVisitor {
public:
  virtual ~DebugSubsectionVisitor() = default;

  virtual Error visitUnknown(DebugUnknownSubsectionRef &Unknown) {
    return Error::success();
  }
  virtual Error visitLines(DebugLinesSubsectionRef &Lines,
                           const StringsAndChecksumsRef &State) = 0;
  virtual Error visitFileChecksums(DebugChecksumsSubsectionRef &Checksums,
                                   const StringsAndChecksumsRef &State) = 0;
  virtual Error visitInlineeLines(DebugInlineeLinesSubsectionRef &Inlinees,
                                  const StringsAndChecksumsRef &State) = 0;
  virtual Error
  visitCrossModuleExports(DebugCrossModuleExportsSubsectionRef &CSE,
                          const StringsAndChecksumsRef &State) = 0;
  virtual Error
  visitCrossModuleImports(DebugCrossModuleImportsSubsectionRef &CSE,
                          const StringsAndChecksumsRef &State) = 0;

  virtual Error visitStringTable(DebugStringTableSubsectionRef &ST,
                                 const StringsAndChecksumsRef &State) = 0;

  virtual Error visitSymbols(DebugSymbolsSubsectionRef &CSE,
                             const StringsAndChecksumsRef &State) = 0;

  virtual Error visitFrameData(DebugFrameDataSubsectionRef &FD,
                               const StringsAndChecksumsRef &State) = 0;
  virtual Error visitCOFFSymbolRVAs(DebugSymbolRVASubsectionRef &RVAs,
                                    const StringsAndChecksumsRef &State) = 0;
};

Error visitDebugSubsection(const DebugSubsectionRecord &R,
                           DebugSubsectionVisitor &V,
                           const StringsAndChecksumsRef &State);

namespace detail {
template <typename T>
Error visitDebugSubsections(T &&FragmentRange, DebugSubsectionVisitor &V,
                            StringsAndChecksumsRef &State) {
  State.initialize(std::forward<T>(FragmentRange));

  for (const DebugSubsectionRecord &L : FragmentRange) {
    if (auto EC = visitDebugSubsection(L, V, State))
      return EC;
  }
  return Error::success();
}
} // namespace detail

template <typename T>
Error visitDebugSubsections(T &&FragmentRange, DebugSubsectionVisitor &V) {
  StringsAndChecksumsRef State;
  return detail::visitDebugSubsections(std::forward<T>(FragmentRange), V,
                                       State);
}

template <typename T>
Error visitDebugSubsections(T &&FragmentRange, DebugSubsectionVisitor &V,
                            const DebugStringTableSubsectionRef &Strings) {
  StringsAndChecksumsRef State(Strings);
  return detail::visitDebugSubsections(std::forward<T>(FragmentRange), V,
                                       State);
}

template <typename T>
Error visitDebugSubsections(T &&FragmentRange, DebugSubsectionVisitor &V,
                            const DebugStringTableSubsectionRef &Strings,
                            const DebugChecksumsSubsectionRef &Checksums) {
  StringsAndChecksumsRef State(Strings, Checksums);
  return detail::visitDebugSubsections(std::forward<T>(FragmentRange), V,
                                       State);
}

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_MODULEDEBUGFRAGMENTVISITOR_H
