//===- EnumTables.h - Enum to string conversion tables ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_ENUMTABLES_H
#define LLVM_DEBUGINFO_CODEVIEW_ENUMTABLES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/ScopedPrinter.h"
#include <cstdint>

namespace llvm {
namespace codeview {

ArrayRef<EnumEntry<SymbolKind>> getSymbolTypeNames();
ArrayRef<EnumEntry<TypeLeafKind>> getTypeLeafNames();
ArrayRef<EnumEntry<uint16_t>> getRegisterNames();
ArrayRef<EnumEntry<uint32_t>> getPublicSymFlagNames();
ArrayRef<EnumEntry<uint8_t>> getProcSymFlagNames();
ArrayRef<EnumEntry<uint16_t>> getLocalFlagNames();
ArrayRef<EnumEntry<uint8_t>> getFrameCookieKindNames();
ArrayRef<EnumEntry<SourceLanguage>> getSourceLanguageNames();
ArrayRef<EnumEntry<uint32_t>> getCompileSym2FlagNames();
ArrayRef<EnumEntry<uint32_t>> getCompileSym3FlagNames();
ArrayRef<EnumEntry<uint32_t>> getFileChecksumNames();
ArrayRef<EnumEntry<unsigned>> getCPUTypeNames();
ArrayRef<EnumEntry<uint32_t>> getFrameProcSymFlagNames();
ArrayRef<EnumEntry<uint16_t>> getExportSymFlagNames();
ArrayRef<EnumEntry<uint32_t>> getModuleSubstreamKindNames();
ArrayRef<EnumEntry<uint8_t>> getThunkOrdinalNames();
ArrayRef<EnumEntry<uint16_t>> getTrampolineNames();
ArrayRef<EnumEntry<COFF::SectionCharacteristics>>
getImageSectionCharacteristicNames();

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_ENUMTABLES_H
