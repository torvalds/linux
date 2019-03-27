//===- llvm-pdbutil.h ----------------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_LLVMPDBDUMP_H
#define LLVM_TOOLS_LLVMPDBDUMP_LLVMPDBDUMP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <stdint.h>

namespace llvm {
namespace object {
class COFFObjectFile;
}
namespace pdb {
class PDBSymbolData;
class PDBSymbolFunc;
class PDBFile;
uint32_t getTypeLength(const PDBSymbolData &Symbol);
}
typedef llvm::PointerUnion<object::COFFObjectFile *, pdb::PDBFile *>
    PdbOrCoffObj;
}

namespace opts {

enum class DumpLevel { None, Basic, Verbose };

enum class ModuleSubsection {
  Unknown,
  Lines,
  FileChecksums,
  InlineeLines,
  CrossScopeImports,
  CrossScopeExports,
  StringTable,
  Symbols,
  FrameData,
  CoffSymbolRVAs,
  All
};

namespace pretty {

enum class ClassDefinitionFormat { None, Layout, All };
enum class ClassSortMode {
  None,
  Name,
  Size,
  Padding,
  PaddingPct,
  PaddingImmediate,
  PaddingPctImmediate
};

enum class SymbolSortMode { None, Name, Size };

enum class SymLevel { Functions, Data, Thunks, All };

bool shouldDumpSymLevel(SymLevel Level);
bool compareFunctionSymbols(
    const std::unique_ptr<llvm::pdb::PDBSymbolFunc> &F1,
    const std::unique_ptr<llvm::pdb::PDBSymbolFunc> &F2);
bool compareDataSymbols(const std::unique_ptr<llvm::pdb::PDBSymbolData> &F1,
                        const std::unique_ptr<llvm::pdb::PDBSymbolData> &F2);

extern llvm::cl::list<std::string> WithName;

extern llvm::cl::opt<bool> Compilands;
extern llvm::cl::opt<bool> Symbols;
extern llvm::cl::opt<bool> Globals;
extern llvm::cl::opt<bool> Classes;
extern llvm::cl::opt<bool> Enums;
extern llvm::cl::opt<bool> Funcsigs;
extern llvm::cl::opt<bool> Arrays;
extern llvm::cl::opt<bool> Typedefs;
extern llvm::cl::opt<bool> Pointers;
extern llvm::cl::opt<bool> VTShapes;
extern llvm::cl::opt<bool> All;
extern llvm::cl::opt<bool> ExcludeCompilerGenerated;

extern llvm::cl::opt<bool> NoEnumDefs;
extern llvm::cl::list<std::string> ExcludeTypes;
extern llvm::cl::list<std::string> ExcludeSymbols;
extern llvm::cl::list<std::string> ExcludeCompilands;
extern llvm::cl::list<std::string> IncludeTypes;
extern llvm::cl::list<std::string> IncludeSymbols;
extern llvm::cl::list<std::string> IncludeCompilands;
extern llvm::cl::opt<SymbolSortMode> SymbolOrder;
extern llvm::cl::opt<ClassSortMode> ClassOrder;
extern llvm::cl::opt<uint32_t> SizeThreshold;
extern llvm::cl::opt<uint32_t> PaddingThreshold;
extern llvm::cl::opt<uint32_t> ImmediatePaddingThreshold;
extern llvm::cl::opt<ClassDefinitionFormat> ClassFormat;
extern llvm::cl::opt<uint32_t> ClassRecursionDepth;
}

namespace bytes {
struct NumberRange {
  uint64_t Min;
  llvm::Optional<uint64_t> Max;
};

extern llvm::Optional<NumberRange> DumpBlockRange;
extern llvm::Optional<NumberRange> DumpByteRange;
extern llvm::cl::list<std::string> DumpStreamData;
extern llvm::cl::opt<bool> NameMap;
extern llvm::cl::opt<bool> Fpm;

extern llvm::cl::opt<bool> SectionContributions;
extern llvm::cl::opt<bool> SectionMap;
extern llvm::cl::opt<bool> ModuleInfos;
extern llvm::cl::opt<bool> FileInfo;
extern llvm::cl::opt<bool> TypeServerMap;
extern llvm::cl::opt<bool> ECData;

extern llvm::cl::list<uint32_t> TypeIndex;
extern llvm::cl::list<uint32_t> IdIndex;

extern llvm::cl::opt<uint32_t> ModuleIndex;
extern llvm::cl::opt<bool> ModuleSyms;
extern llvm::cl::opt<bool> ModuleC11;
extern llvm::cl::opt<bool> ModuleC13;
extern llvm::cl::opt<bool> SplitChunks;
} // namespace bytes

namespace dump {

extern llvm::cl::opt<bool> DumpSummary;
extern llvm::cl::opt<bool> DumpFpm;
extern llvm::cl::opt<bool> DumpStreams;
extern llvm::cl::opt<bool> DumpSymbolStats;
extern llvm::cl::opt<bool> DumpUdtStats;
extern llvm::cl::opt<bool> DumpStreamBlocks;

extern llvm::cl::opt<bool> DumpLines;
extern llvm::cl::opt<bool> DumpInlineeLines;
extern llvm::cl::opt<bool> DumpXmi;
extern llvm::cl::opt<bool> DumpXme;
extern llvm::cl::opt<bool> DumpNamedStreams;
extern llvm::cl::opt<bool> DumpStringTable;
extern llvm::cl::opt<bool> DumpStringTableDetails;
extern llvm::cl::opt<bool> DumpTypes;
extern llvm::cl::opt<bool> DumpTypeData;
extern llvm::cl::opt<bool> DumpTypeExtras;
extern llvm::cl::list<uint32_t> DumpTypeIndex;
extern llvm::cl::opt<bool> DumpTypeDependents;
extern llvm::cl::opt<bool> DumpSectionHeaders;

extern llvm::cl::opt<bool> DumpIds;
extern llvm::cl::opt<bool> DumpIdData;
extern llvm::cl::opt<bool> DumpIdExtras;
extern llvm::cl::list<uint32_t> DumpIdIndex;
extern llvm::cl::opt<uint32_t> DumpModi;
extern llvm::cl::opt<bool> JustMyCode;
extern llvm::cl::opt<bool> DontResolveForwardRefs;
extern llvm::cl::opt<bool> DumpSymbols;
extern llvm::cl::opt<bool> DumpSymRecordBytes;
extern llvm::cl::opt<bool> DumpGSIRecords;
extern llvm::cl::opt<bool> DumpGlobals;
extern llvm::cl::list<std::string> DumpGlobalNames;
extern llvm::cl::opt<bool> DumpGlobalExtras;
extern llvm::cl::opt<bool> DumpPublics;
extern llvm::cl::opt<bool> DumpPublicExtras;
extern llvm::cl::opt<bool> DumpSectionContribs;
extern llvm::cl::opt<bool> DumpSectionMap;
extern llvm::cl::opt<bool> DumpModules;
extern llvm::cl::opt<bool> DumpModuleFiles;
extern llvm::cl::opt<bool> DumpFpo;
extern llvm::cl::opt<bool> RawAll;
}

namespace pdb2yaml {
extern llvm::cl::opt<bool> All;
extern llvm::cl::opt<bool> NoFileHeaders;
extern llvm::cl::opt<bool> Minimal;
extern llvm::cl::opt<bool> StreamMetadata;
extern llvm::cl::opt<bool> StreamDirectory;
extern llvm::cl::opt<bool> StringTable;
extern llvm::cl::opt<bool> PdbStream;
extern llvm::cl::opt<bool> DbiStream;
extern llvm::cl::opt<bool> TpiStream;
extern llvm::cl::opt<bool> IpiStream;
extern llvm::cl::opt<bool> PublicsStream;
extern llvm::cl::list<std::string> InputFilename;
extern llvm::cl::opt<bool> DumpModules;
extern llvm::cl::opt<bool> DumpModuleFiles;
extern llvm::cl::list<ModuleSubsection> DumpModuleSubsections;
extern llvm::cl::opt<bool> DumpModuleSyms;
} // namespace pdb2yaml

namespace explain {
enum class InputFileType { PDBFile, PDBStream, DBIStream, Names, ModuleStream };

extern llvm::cl::list<std::string> InputFilename;
extern llvm::cl::list<uint64_t> Offsets;
extern llvm::cl::opt<InputFileType> InputType;
} // namespace explain

namespace exportstream {
extern llvm::cl::opt<std::string> OutputFile;
extern llvm::cl::opt<std::string> Stream;
extern llvm::cl::opt<bool> ForceName;
} // namespace exportstream
}

#endif
