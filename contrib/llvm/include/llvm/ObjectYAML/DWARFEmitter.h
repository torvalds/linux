//===--- DWARFEmitter.h - ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// Common declarations for yaml2obj
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_DWARFEMITTER_H
#define LLVM_OBJECTYAML_DWARFEMITTER_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace llvm {

class raw_ostream;

namespace DWARFYAML {

struct Data;
struct PubSection;

void EmitDebugAbbrev(raw_ostream &OS, const Data &DI);
void EmitDebugStr(raw_ostream &OS, const Data &DI);

void EmitDebugAranges(raw_ostream &OS, const Data &DI);
void EmitPubSection(raw_ostream &OS, const PubSection &Sect,
                    bool IsLittleEndian);
void EmitDebugInfo(raw_ostream &OS, const Data &DI);
void EmitDebugLine(raw_ostream &OS, const Data &DI);

Expected<StringMap<std::unique_ptr<MemoryBuffer>>>
EmitDebugSections(StringRef YAMLString, bool ApplyFixups = false,
                  bool IsLittleEndian = sys::IsLittleEndianHost);
StringMap<std::unique_ptr<MemoryBuffer>>
EmitDebugSections(llvm::DWARFYAML::Data &DI, bool ApplyFixups);

} // end namespace DWARFYAML
} // end namespace llvm

#endif // LLVM_OBJECTYAML_DWARFEMITTER_H
