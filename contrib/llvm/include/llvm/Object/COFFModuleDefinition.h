//===--- COFFModuleDefinition.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Windows-specific.
// A parser for the module-definition file (.def file).
// Parsed results are directly written to Config global variable.
//
// The format of module-definition files are described in this document:
// https://msdn.microsoft.com/en-us/library/28d6s79h.aspx
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_COFF_MODULE_DEFINITION_H
#define LLVM_OBJECT_COFF_MODULE_DEFINITION_H

#include "llvm/Object/COFF.h"
#include "llvm/Object/COFFImportFile.h"

namespace llvm {
namespace object {

struct COFFModuleDefinition {
  std::vector<COFFShortExport> Exports;
  std::string OutputFile;
  std::string ImportName;
  uint64_t ImageBase = 0;
  uint64_t StackReserve = 0;
  uint64_t StackCommit = 0;
  uint64_t HeapReserve = 0;
  uint64_t HeapCommit = 0;
  uint32_t MajorImageVersion = 0;
  uint32_t MinorImageVersion = 0;
  uint32_t MajorOSVersion = 0;
  uint32_t MinorOSVersion = 0;
};

// mingw and wine def files do not mangle _ for x86 which
// is a consequence of legacy binutils' dlltool functionality.
// This MingwDef flag should be removed once mingw stops this pratice.
Expected<COFFModuleDefinition>
parseCOFFModuleDefinition(MemoryBufferRef MB, COFF::MachineTypes Machine,
                          bool MingwDef = false);

} // End namespace object.
} // End namespace llvm.

#endif
