//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_OBJDUMP_LLVM_OBJDUMP_H
#define LLVM_TOOLS_LLVM_OBJDUMP_LLVM_OBJDUMP_H

#include "llvm/DebugInfo/DIContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Object/Archive.h"

namespace llvm {
class StringRef;

namespace object {
  class COFFObjectFile;
  class COFFImportFile;
  class MachOObjectFile;
  class MachOUniversalBinary;
  class ObjectFile;
  class Archive;
  class RelocationRef;
}

extern cl::opt<std::string> TripleName;
extern cl::opt<std::string> ArchName;
extern cl::opt<std::string> MCPU;
extern cl::list<std::string> MAttrs;
extern cl::list<std::string> FilterSections;
extern cl::opt<bool> AllHeaders;
extern cl::opt<bool> Demangle;
extern cl::opt<bool> Disassemble;
extern cl::opt<bool> DisassembleAll;
extern cl::opt<bool> NoShowRawInsn;
extern cl::opt<bool> NoLeadingAddr;
extern cl::opt<bool> PrivateHeaders;
extern cl::opt<bool> FileHeaders;
extern cl::opt<bool> FirstPrivateHeader;
extern cl::opt<bool> ExportsTrie;
extern cl::opt<bool> Rebase;
extern cl::opt<bool> Bind;
extern cl::opt<bool> LazyBind;
extern cl::opt<bool> WeakBind;
extern cl::opt<bool> RawClangAST;
extern cl::opt<bool> UniversalHeaders;
extern cl::opt<bool> ArchiveHeaders;
extern cl::opt<bool> IndirectSymbols;
extern cl::opt<bool> DataInCode;
extern cl::opt<bool> LinkOptHints;
extern cl::opt<bool> InfoPlist;
extern cl::opt<bool> DylibsUsed;
extern cl::opt<bool> DylibId;
extern cl::opt<bool> ObjcMetaData;
extern cl::opt<std::string> DisSymName;
extern cl::opt<bool> NonVerbose;
extern cl::opt<bool> Relocations;
extern cl::opt<bool> DynamicRelocations;
extern cl::opt<bool> SectionHeaders;
extern cl::opt<bool> SectionContents;
extern cl::opt<bool> SymbolTable;
extern cl::opt<bool> UnwindInfo;
extern cl::opt<bool> PrintImmHex;
extern cl::opt<DIDumpType> DwarfDumpType;

// Various helper functions.
void error(std::error_code ec);
bool isRelocAddressLess(object::RelocationRef A, object::RelocationRef B);
void parseInputMachO(StringRef Filename);
void parseInputMachO(object::MachOUniversalBinary *UB);
void printCOFFUnwindInfo(const object::COFFObjectFile *O);
void printMachOUnwindInfo(const object::MachOObjectFile *O);
void printMachOExportsTrie(const object::MachOObjectFile *O);
void printMachORebaseTable(object::MachOObjectFile *O);
void printMachOBindTable(object::MachOObjectFile *O);
void printMachOLazyBindTable(object::MachOObjectFile *O);
void printMachOWeakBindTable(object::MachOObjectFile *O);
void printELFFileHeader(const object::ObjectFile *O);
void printELFDynamicSection(const object::ObjectFile *Obj);
void printCOFFFileHeader(const object::ObjectFile *O);
void printCOFFSymbolTable(const object::COFFImportFile *I);
void printCOFFSymbolTable(const object::COFFObjectFile *O);
void printMachOFileHeader(const object::ObjectFile *O);
void printMachOLoadCommands(const object::ObjectFile *O);
void printWasmFileHeader(const object::ObjectFile *O);
void printExportsTrie(const object::ObjectFile *O);
void printRebaseTable(object::ObjectFile *O);
void printBindTable(object::ObjectFile *O);
void printLazyBindTable(object::ObjectFile *O);
void printWeakBindTable(object::ObjectFile *O);
void printRawClangAST(const object::ObjectFile *O);
void printRelocations(const object::ObjectFile *O);
void printDynamicRelocations(const object::ObjectFile *O);
void printSectionHeaders(const object::ObjectFile *O);
void printSectionContents(const object::ObjectFile *O);
void printSymbolTable(const object::ObjectFile *O, StringRef ArchiveName,
                      StringRef ArchitectureName = StringRef());
void warn(StringRef Message);
LLVM_ATTRIBUTE_NORETURN void error(Twine Message);
LLVM_ATTRIBUTE_NORETURN void report_error(StringRef File, Twine Message);
LLVM_ATTRIBUTE_NORETURN void report_error(StringRef File, std::error_code EC);
LLVM_ATTRIBUTE_NORETURN void report_error(StringRef File, llvm::Error E);
LLVM_ATTRIBUTE_NORETURN void report_error(StringRef FileName,
                                          StringRef ArchiveName,
                                          llvm::Error E,
                                          StringRef ArchitectureName
                                                    = StringRef());
LLVM_ATTRIBUTE_NORETURN void report_error(StringRef ArchiveName,
                                          const object::Archive::Child &C,
                                          llvm::Error E,
                                          StringRef ArchitectureName
                                                    = StringRef());

} // end namespace llvm

#endif
