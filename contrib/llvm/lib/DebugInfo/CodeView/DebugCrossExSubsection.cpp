//===- DebugCrossExSubsection.cpp -----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/DebugCrossExSubsection.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Error.h"
#include <cstdint>

using namespace llvm;
using namespace llvm::codeview;

Error DebugCrossModuleExportsSubsectionRef::initialize(
    BinaryStreamReader Reader) {
  if (Reader.bytesRemaining() % sizeof(CrossModuleExport) != 0)
    return make_error<CodeViewError>(
        cv_error_code::corrupt_record,
        "Cross Scope Exports section is an invalid size!");

  uint32_t Size = Reader.bytesRemaining() / sizeof(CrossModuleExport);
  return Reader.readArray(References, Size);
}

Error DebugCrossModuleExportsSubsectionRef::initialize(BinaryStreamRef Stream) {
  BinaryStreamReader Reader(Stream);
  return initialize(Reader);
}

void DebugCrossModuleExportsSubsection::addMapping(uint32_t Local,
                                                   uint32_t Global) {
  Mappings[Local] = Global;
}

uint32_t DebugCrossModuleExportsSubsection::calculateSerializedSize() const {
  return Mappings.size() * sizeof(CrossModuleExport);
}

Error DebugCrossModuleExportsSubsection::commit(
    BinaryStreamWriter &Writer) const {
  for (const auto &M : Mappings) {
    if (auto EC = Writer.writeInteger(M.first))
      return EC;
    if (auto EC = Writer.writeInteger(M.second))
      return EC;
  }
  return Error::success();
}
