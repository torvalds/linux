//===-- COFFDumper.cpp - COFF-specific dumper -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the COFF-specific dumper for llvm-readobj.
///
//===----------------------------------------------------------------------===//

#include "ARMWinEHPrinter.h"
#include "Error.h"
#include "ObjDumper.h"
#include "StackMapPrinter.h"
#include "Win64EHDumper.h"
#include "llvm-readobj.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugInlineeLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/Line.h"
#include "llvm/DebugInfo/CodeView/MergingTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/SymbolDumpDelegate.h"
#include "llvm/DebugInfo/CodeView/SymbolDumper.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeDumpVisitor.h"
#include "llvm/DebugInfo/CodeView/TypeHashing.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeStreamMerger.h"
#include "llvm/DebugInfo/CodeView/TypeTableCollection.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/Win64EH.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::codeview;
using namespace llvm::support;
using namespace llvm::Win64EH;

namespace {

struct LoadConfigTables {
  uint64_t SEHTableVA = 0;
  uint64_t SEHTableCount = 0;
  uint32_t GuardFlags = 0;
  uint64_t GuardFidTableVA = 0;
  uint64_t GuardFidTableCount = 0;
  uint64_t GuardLJmpTableVA = 0;
  uint64_t GuardLJmpTableCount = 0;
};

class COFFDumper : public ObjDumper {
public:
  friend class COFFObjectDumpDelegate;
  COFFDumper(const llvm::object::COFFObjectFile *Obj, ScopedPrinter &Writer)
      : ObjDumper(Writer), Obj(Obj), Writer(Writer), Types(100) {}

  void printFileHeaders() override;
  void printSectionHeaders() override;
  void printRelocations() override;
  void printSymbols() override;
  void printDynamicSymbols() override;
  void printUnwindInfo() override;

  void printNeededLibraries() override;

  void printCOFFImports() override;
  void printCOFFExports() override;
  void printCOFFDirectives() override;
  void printCOFFBaseReloc() override;
  void printCOFFDebugDirectory() override;
  void printCOFFResources() override;
  void printCOFFLoadConfig() override;
  void printCodeViewDebugInfo() override;
  void
  mergeCodeViewTypes(llvm::codeview::MergingTypeTableBuilder &CVIDs,
                     llvm::codeview::MergingTypeTableBuilder &CVTypes) override;
  void printStackMap() const override;
  void printAddrsig() override;
private:
  void printSymbol(const SymbolRef &Sym);
  void printRelocation(const SectionRef &Section, const RelocationRef &Reloc,
                       uint64_t Bias = 0);
  void printDataDirectory(uint32_t Index, const std::string &FieldName);

  void printDOSHeader(const dos_header *DH);
  template <class PEHeader> void printPEHeader(const PEHeader *Hdr);
  void printBaseOfDataField(const pe32_header *Hdr);
  void printBaseOfDataField(const pe32plus_header *Hdr);
  template <typename T>
  void printCOFFLoadConfig(const T *Conf, LoadConfigTables &Tables);
  typedef void (*PrintExtraCB)(raw_ostream &, const uint8_t *);
  void printRVATable(uint64_t TableVA, uint64_t Count, uint64_t EntrySize,
                     PrintExtraCB PrintExtra = 0);

  void printCodeViewSymbolSection(StringRef SectionName, const SectionRef &Section);
  void printCodeViewTypeSection(StringRef SectionName, const SectionRef &Section);
  StringRef getTypeName(TypeIndex Ty);
  StringRef getFileNameForFileOffset(uint32_t FileOffset);
  void printFileNameForOffset(StringRef Label, uint32_t FileOffset);
  void printTypeIndex(StringRef FieldName, TypeIndex TI) {
    // Forward to CVTypeDumper for simplicity.
    codeview::printTypeIndex(Writer, FieldName, TI, Types);
  }

  void printCodeViewSymbolsSubsection(StringRef Subsection,
                                      const SectionRef &Section,
                                      StringRef SectionContents);

  void printCodeViewFileChecksums(StringRef Subsection);

  void printCodeViewInlineeLines(StringRef Subsection);

  void printRelocatedField(StringRef Label, const coff_section *Sec,
                           uint32_t RelocOffset, uint32_t Offset,
                           StringRef *RelocSym = nullptr);

  uint32_t countTotalTableEntries(ResourceSectionRef RSF,
                                  const coff_resource_dir_table &Table,
                                  StringRef Level);

  void printResourceDirectoryTable(ResourceSectionRef RSF,
                                   const coff_resource_dir_table &Table,
                                   StringRef Level);

  void printBinaryBlockWithRelocs(StringRef Label, const SectionRef &Sec,
                                  StringRef SectionContents, StringRef Block);

  /// Given a .debug$S section, find the string table and file checksum table.
  void initializeFileAndStringTables(BinaryStreamReader &Reader);

  void cacheRelocations();

  std::error_code resolveSymbol(const coff_section *Section, uint64_t Offset,
                                SymbolRef &Sym);
  std::error_code resolveSymbolName(const coff_section *Section,
                                    uint64_t Offset, StringRef &Name);
  std::error_code resolveSymbolName(const coff_section *Section,
                                    StringRef SectionContents,
                                    const void *RelocPtr, StringRef &Name);
  void printImportedSymbols(iterator_range<imported_symbol_iterator> Range);
  void printDelayImportedSymbols(
      const DelayImportDirectoryEntryRef &I,
      iterator_range<imported_symbol_iterator> Range);
  ErrorOr<const coff_resource_dir_entry &>
  getResourceDirectoryTableEntry(const coff_resource_dir_table &Table,
                                 uint32_t Index);

  typedef DenseMap<const coff_section*, std::vector<RelocationRef> > RelocMapTy;

  const llvm::object::COFFObjectFile *Obj;
  bool RelocCached = false;
  RelocMapTy RelocMap;

  DebugChecksumsSubsectionRef CVFileChecksumTable;

  DebugStringTableSubsectionRef CVStringTable;

  /// Track the compilation CPU type. S_COMPILE3 symbol records typically come
  /// first, but if we don't see one, just assume an X64 CPU type. It is common.
  CPUType CompilationCPUType = CPUType::X64;

  ScopedPrinter &Writer;
  BinaryByteStream TypeContents;
  LazyRandomTypeCollection Types;
};

class COFFObjectDumpDelegate : public SymbolDumpDelegate {
public:
  COFFObjectDumpDelegate(COFFDumper &CD, const SectionRef &SR,
                         const COFFObjectFile *Obj, StringRef SectionContents)
      : CD(CD), SR(SR), SectionContents(SectionContents) {
    Sec = Obj->getCOFFSection(SR);
  }

  uint32_t getRecordOffset(BinaryStreamReader Reader) override {
    ArrayRef<uint8_t> Data;
    if (auto EC = Reader.readLongestContiguousChunk(Data)) {
      llvm::consumeError(std::move(EC));
      return 0;
    }
    return Data.data() - SectionContents.bytes_begin();
  }

  void printRelocatedField(StringRef Label, uint32_t RelocOffset,
                           uint32_t Offset, StringRef *RelocSym) override {
    CD.printRelocatedField(Label, Sec, RelocOffset, Offset, RelocSym);
  }

  void printBinaryBlockWithRelocs(StringRef Label,
                                  ArrayRef<uint8_t> Block) override {
    StringRef SBlock(reinterpret_cast<const char *>(Block.data()),
                     Block.size());
    if (opts::CodeViewSubsectionBytes)
      CD.printBinaryBlockWithRelocs(Label, SR, SectionContents, SBlock);
  }

  StringRef getFileNameForFileOffset(uint32_t FileOffset) override {
    return CD.getFileNameForFileOffset(FileOffset);
  }

  DebugStringTableSubsectionRef getStringTable() override {
    return CD.CVStringTable;
  }

private:
  COFFDumper &CD;
  const SectionRef &SR;
  const coff_section *Sec;
  StringRef SectionContents;
};

} // end namespace

namespace llvm {

std::error_code createCOFFDumper(const object::ObjectFile *Obj,
                                 ScopedPrinter &Writer,
                                 std::unique_ptr<ObjDumper> &Result) {
  const COFFObjectFile *COFFObj = dyn_cast<COFFObjectFile>(Obj);
  if (!COFFObj)
    return readobj_error::unsupported_obj_file_format;

  Result.reset(new COFFDumper(COFFObj, Writer));
  return readobj_error::success;
}

} // namespace llvm

// Given a section and an offset into this section the function returns the
// symbol used for the relocation at the offset.
std::error_code COFFDumper::resolveSymbol(const coff_section *Section,
                                          uint64_t Offset, SymbolRef &Sym) {
  cacheRelocations();
  const auto &Relocations = RelocMap[Section];
  auto SymI = Obj->symbol_end();
  for (const auto &Relocation : Relocations) {
    uint64_t RelocationOffset = Relocation.getOffset();

    if (RelocationOffset == Offset) {
      SymI = Relocation.getSymbol();
      break;
    }
  }
  if (SymI == Obj->symbol_end())
    return readobj_error::unknown_symbol;
  Sym = *SymI;
  return readobj_error::success;
}

// Given a section and an offset into this section the function returns the name
// of the symbol used for the relocation at the offset.
std::error_code COFFDumper::resolveSymbolName(const coff_section *Section,
                                              uint64_t Offset,
                                              StringRef &Name) {
  SymbolRef Symbol;
  if (std::error_code EC = resolveSymbol(Section, Offset, Symbol))
    return EC;
  Expected<StringRef> NameOrErr = Symbol.getName();
  if (!NameOrErr)
    return errorToErrorCode(NameOrErr.takeError());
  Name = *NameOrErr;
  return std::error_code();
}

// Helper for when you have a pointer to real data and you want to know about
// relocations against it.
std::error_code COFFDumper::resolveSymbolName(const coff_section *Section,
                                              StringRef SectionContents,
                                              const void *RelocPtr,
                                              StringRef &Name) {
  assert(SectionContents.data() < RelocPtr &&
         RelocPtr < SectionContents.data() + SectionContents.size() &&
         "pointer to relocated object is not in section");
  uint64_t Offset = ptrdiff_t(reinterpret_cast<const char *>(RelocPtr) -
                              SectionContents.data());
  return resolveSymbolName(Section, Offset, Name);
}

void COFFDumper::printRelocatedField(StringRef Label, const coff_section *Sec,
                                     uint32_t RelocOffset, uint32_t Offset,
                                     StringRef *RelocSym) {
  StringRef SymStorage;
  StringRef &Symbol = RelocSym ? *RelocSym : SymStorage;
  if (!resolveSymbolName(Sec, RelocOffset, Symbol))
    W.printSymbolOffset(Label, Symbol, Offset);
  else
    W.printHex(Label, RelocOffset);
}

void COFFDumper::printBinaryBlockWithRelocs(StringRef Label,
                                            const SectionRef &Sec,
                                            StringRef SectionContents,
                                            StringRef Block) {
  W.printBinaryBlock(Label, Block);

  assert(SectionContents.begin() < Block.begin() &&
         SectionContents.end() >= Block.end() &&
         "Block is not contained in SectionContents");
  uint64_t OffsetStart = Block.data() - SectionContents.data();
  uint64_t OffsetEnd = OffsetStart + Block.size();

  W.flush();
  cacheRelocations();
  ListScope D(W, "BlockRelocations");
  const coff_section *Section = Obj->getCOFFSection(Sec);
  const auto &Relocations = RelocMap[Section];
  for (const auto &Relocation : Relocations) {
    uint64_t RelocationOffset = Relocation.getOffset();
    if (OffsetStart <= RelocationOffset && RelocationOffset < OffsetEnd)
      printRelocation(Sec, Relocation, OffsetStart);
  }
}

static const EnumEntry<COFF::MachineTypes> ImageFileMachineType[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_UNKNOWN  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_AM33     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_AMD64    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_ARM      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_ARM64    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_ARMNT    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_EBC      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_I386     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_IA64     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_M32R     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_MIPS16   ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_MIPSFPU  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_MIPSFPU16),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_POWERPC  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_POWERPCFP),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_R4000    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_SH3      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_SH3DSP   ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_SH4      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_SH5      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_THUMB    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_MACHINE_WCEMIPSV2)
};

static const EnumEntry<COFF::Characteristics> ImageFileCharacteristics[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_RELOCS_STRIPPED        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_EXECUTABLE_IMAGE       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_LINE_NUMS_STRIPPED     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_LOCAL_SYMS_STRIPPED    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_AGGRESSIVE_WS_TRIM     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_LARGE_ADDRESS_AWARE    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_BYTES_REVERSED_LO      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_32BIT_MACHINE          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_DEBUG_STRIPPED         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_NET_RUN_FROM_SWAP      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_SYSTEM                 ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_DLL                    ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_UP_SYSTEM_ONLY         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_FILE_BYTES_REVERSED_HI      )
};

static const EnumEntry<COFF::WindowsSubsystem> PEWindowsSubsystem[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_UNKNOWN                ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_NATIVE                 ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_WINDOWS_GUI            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_WINDOWS_CUI            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_POSIX_CUI              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_WINDOWS_CE_GUI         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_EFI_APPLICATION        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER     ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_EFI_ROM                ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SUBSYSTEM_XBOX                   ),
};

static const EnumEntry<COFF::DLLCharacteristics> PEDLLCharacteristics[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY      ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_NX_COMPAT            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_NO_ISOLATION         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_NO_SEH               ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_NO_BIND              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_APPCONTAINER         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_GUARD_CF             ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE),
};

static const EnumEntry<COFF::SectionCharacteristics>
ImageSectionCharacteristics[] = {
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_TYPE_NOLOAD           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_TYPE_NO_PAD           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_CNT_CODE              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_CNT_INITIALIZED_DATA  ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_CNT_UNINITIALIZED_DATA),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_OTHER             ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_INFO              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_REMOVE            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_COMDAT            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_GPREL                 ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_PURGEABLE         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_16BIT             ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_LOCKED            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_PRELOAD           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_1BYTES          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_2BYTES          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_4BYTES          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_8BYTES          ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_16BYTES         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_32BYTES         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_64BYTES         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_128BYTES        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_256BYTES        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_512BYTES        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_1024BYTES       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_2048BYTES       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_4096BYTES       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_ALIGN_8192BYTES       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_LNK_NRELOC_OVFL       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_DISCARDABLE       ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_NOT_CACHED        ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_NOT_PAGED         ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_SHARED            ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_EXECUTE           ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_READ              ),
  LLVM_READOBJ_ENUM_ENT(COFF, IMAGE_SCN_MEM_WRITE             )
};

static const EnumEntry<COFF::SymbolBaseType> ImageSymType[] = {
  { "Null"  , COFF::IMAGE_SYM_TYPE_NULL   },
  { "Void"  , COFF::IMAGE_SYM_TYPE_VOID   },
  { "Char"  , COFF::IMAGE_SYM_TYPE_CHAR   },
  { "Short" , COFF::IMAGE_SYM_TYPE_SHORT  },
  { "Int"   , COFF::IMAGE_SYM_TYPE_INT    },
  { "Long"  , COFF::IMAGE_SYM_TYPE_LONG   },
  { "Float" , COFF::IMAGE_SYM_TYPE_FLOAT  },
  { "Double", COFF::IMAGE_SYM_TYPE_DOUBLE },
  { "Struct", COFF::IMAGE_SYM_TYPE_STRUCT },
  { "Union" , COFF::IMAGE_SYM_TYPE_UNION  },
  { "Enum"  , COFF::IMAGE_SYM_TYPE_ENUM   },
  { "MOE"   , COFF::IMAGE_SYM_TYPE_MOE    },
  { "Byte"  , COFF::IMAGE_SYM_TYPE_BYTE   },
  { "Word"  , COFF::IMAGE_SYM_TYPE_WORD   },
  { "UInt"  , COFF::IMAGE_SYM_TYPE_UINT   },
  { "DWord" , COFF::IMAGE_SYM_TYPE_DWORD  }
};

static const EnumEntry<COFF::SymbolComplexType> ImageSymDType[] = {
  { "Null"    , COFF::IMAGE_SYM_DTYPE_NULL     },
  { "Pointer" , COFF::IMAGE_SYM_DTYPE_POINTER  },
  { "Function", COFF::IMAGE_SYM_DTYPE_FUNCTION },
  { "Array"   , COFF::IMAGE_SYM_DTYPE_ARRAY    }
};

static const EnumEntry<COFF::SymbolStorageClass> ImageSymClass[] = {
  { "EndOfFunction"  , COFF::IMAGE_SYM_CLASS_END_OF_FUNCTION  },
  { "Null"           , COFF::IMAGE_SYM_CLASS_NULL             },
  { "Automatic"      , COFF::IMAGE_SYM_CLASS_AUTOMATIC        },
  { "External"       , COFF::IMAGE_SYM_CLASS_EXTERNAL         },
  { "Static"         , COFF::IMAGE_SYM_CLASS_STATIC           },
  { "Register"       , COFF::IMAGE_SYM_CLASS_REGISTER         },
  { "ExternalDef"    , COFF::IMAGE_SYM_CLASS_EXTERNAL_DEF     },
  { "Label"          , COFF::IMAGE_SYM_CLASS_LABEL            },
  { "UndefinedLabel" , COFF::IMAGE_SYM_CLASS_UNDEFINED_LABEL  },
  { "MemberOfStruct" , COFF::IMAGE_SYM_CLASS_MEMBER_OF_STRUCT },
  { "Argument"       , COFF::IMAGE_SYM_CLASS_ARGUMENT         },
  { "StructTag"      , COFF::IMAGE_SYM_CLASS_STRUCT_TAG       },
  { "MemberOfUnion"  , COFF::IMAGE_SYM_CLASS_MEMBER_OF_UNION  },
  { "UnionTag"       , COFF::IMAGE_SYM_CLASS_UNION_TAG        },
  { "TypeDefinition" , COFF::IMAGE_SYM_CLASS_TYPE_DEFINITION  },
  { "UndefinedStatic", COFF::IMAGE_SYM_CLASS_UNDEFINED_STATIC },
  { "EnumTag"        , COFF::IMAGE_SYM_CLASS_ENUM_TAG         },
  { "MemberOfEnum"   , COFF::IMAGE_SYM_CLASS_MEMBER_OF_ENUM   },
  { "RegisterParam"  , COFF::IMAGE_SYM_CLASS_REGISTER_PARAM   },
  { "BitField"       , COFF::IMAGE_SYM_CLASS_BIT_FIELD        },
  { "Block"          , COFF::IMAGE_SYM_CLASS_BLOCK            },
  { "Function"       , COFF::IMAGE_SYM_CLASS_FUNCTION         },
  { "EndOfStruct"    , COFF::IMAGE_SYM_CLASS_END_OF_STRUCT    },
  { "File"           , COFF::IMAGE_SYM_CLASS_FILE             },
  { "Section"        , COFF::IMAGE_SYM_CLASS_SECTION          },
  { "WeakExternal"   , COFF::IMAGE_SYM_CLASS_WEAK_EXTERNAL    },
  { "CLRToken"       , COFF::IMAGE_SYM_CLASS_CLR_TOKEN        }
};

static const EnumEntry<COFF::COMDATType> ImageCOMDATSelect[] = {
  { "NoDuplicates", COFF::IMAGE_COMDAT_SELECT_NODUPLICATES },
  { "Any"         , COFF::IMAGE_COMDAT_SELECT_ANY          },
  { "SameSize"    , COFF::IMAGE_COMDAT_SELECT_SAME_SIZE    },
  { "ExactMatch"  , COFF::IMAGE_COMDAT_SELECT_EXACT_MATCH  },
  { "Associative" , COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE  },
  { "Largest"     , COFF::IMAGE_COMDAT_SELECT_LARGEST      },
  { "Newest"      , COFF::IMAGE_COMDAT_SELECT_NEWEST       }
};

static const EnumEntry<COFF::DebugType> ImageDebugType[] = {
  { "Unknown"    , COFF::IMAGE_DEBUG_TYPE_UNKNOWN       },
  { "COFF"       , COFF::IMAGE_DEBUG_TYPE_COFF          },
  { "CodeView"   , COFF::IMAGE_DEBUG_TYPE_CODEVIEW      },
  { "FPO"        , COFF::IMAGE_DEBUG_TYPE_FPO           },
  { "Misc"       , COFF::IMAGE_DEBUG_TYPE_MISC          },
  { "Exception"  , COFF::IMAGE_DEBUG_TYPE_EXCEPTION     },
  { "Fixup"      , COFF::IMAGE_DEBUG_TYPE_FIXUP         },
  { "OmapToSrc"  , COFF::IMAGE_DEBUG_TYPE_OMAP_TO_SRC   },
  { "OmapFromSrc", COFF::IMAGE_DEBUG_TYPE_OMAP_FROM_SRC },
  { "Borland"    , COFF::IMAGE_DEBUG_TYPE_BORLAND       },
  { "Reserved10" , COFF::IMAGE_DEBUG_TYPE_RESERVED10    },
  { "CLSID"      , COFF::IMAGE_DEBUG_TYPE_CLSID         },
  { "VCFeature"  , COFF::IMAGE_DEBUG_TYPE_VC_FEATURE    },
  { "POGO"       , COFF::IMAGE_DEBUG_TYPE_POGO          },
  { "ILTCG"      , COFF::IMAGE_DEBUG_TYPE_ILTCG         },
  { "MPX"        , COFF::IMAGE_DEBUG_TYPE_MPX           },
  { "Repro"      , COFF::IMAGE_DEBUG_TYPE_REPRO         },
};

static const EnumEntry<COFF::WeakExternalCharacteristics>
WeakExternalCharacteristics[] = {
  { "NoLibrary", COFF::IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY },
  { "Library"  , COFF::IMAGE_WEAK_EXTERN_SEARCH_LIBRARY   },
  { "Alias"    , COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS     }
};

static const EnumEntry<uint32_t> SubSectionTypes[] = {
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, Symbols),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, Lines),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, StringTable),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, FileChecksums),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, FrameData),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, InlineeLines),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, CrossScopeImports),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, CrossScopeExports),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, ILLines),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, FuncMDTokenMap),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, TypeMDTokenMap),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, MergedAssemblyInput),
    LLVM_READOBJ_ENUM_CLASS_ENT(DebugSubsectionKind, CoffSymbolRVA),
};

static const EnumEntry<uint32_t> FrameDataFlags[] = {
    LLVM_READOBJ_ENUM_ENT(FrameData, HasSEH),
    LLVM_READOBJ_ENUM_ENT(FrameData, HasEH),
    LLVM_READOBJ_ENUM_ENT(FrameData, IsFunctionStart),
};

static const EnumEntry<uint8_t> FileChecksumKindNames[] = {
  LLVM_READOBJ_ENUM_CLASS_ENT(FileChecksumKind, None),
  LLVM_READOBJ_ENUM_CLASS_ENT(FileChecksumKind, MD5),
  LLVM_READOBJ_ENUM_CLASS_ENT(FileChecksumKind, SHA1),
  LLVM_READOBJ_ENUM_CLASS_ENT(FileChecksumKind, SHA256),
};

static const EnumEntry<COFF::ResourceTypeID> ResourceTypeNames[]{
    {"kRT_CURSOR (ID 1)", COFF::RID_Cursor},
    {"kRT_BITMAP (ID 2)", COFF::RID_Bitmap},
    {"kRT_ICON (ID 3)", COFF::RID_Icon},
    {"kRT_MENU (ID 4)", COFF::RID_Menu},
    {"kRT_DIALOG (ID 5)", COFF::RID_Dialog},
    {"kRT_STRING (ID 6)", COFF::RID_String},
    {"kRT_FONTDIR (ID 7)", COFF::RID_FontDir},
    {"kRT_FONT (ID 8)", COFF::RID_Font},
    {"kRT_ACCELERATOR (ID 9)", COFF::RID_Accelerator},
    {"kRT_RCDATA (ID 10)", COFF::RID_RCData},
    {"kRT_MESSAGETABLE (ID 11)", COFF::RID_MessageTable},
    {"kRT_GROUP_CURSOR (ID 12)", COFF::RID_Group_Cursor},
    {"kRT_GROUP_ICON (ID 14)", COFF::RID_Group_Icon},
    {"kRT_VERSION (ID 16)", COFF::RID_Version},
    {"kRT_DLGINCLUDE (ID 17)", COFF::RID_DLGInclude},
    {"kRT_PLUGPLAY (ID 19)", COFF::RID_PlugPlay},
    {"kRT_VXD (ID 20)", COFF::RID_VXD},
    {"kRT_ANICURSOR (ID 21)", COFF::RID_AniCursor},
    {"kRT_ANIICON (ID 22)", COFF::RID_AniIcon},
    {"kRT_HTML (ID 23)", COFF::RID_HTML},
    {"kRT_MANIFEST (ID 24)", COFF::RID_Manifest}};

template <typename T>
static std::error_code getSymbolAuxData(const COFFObjectFile *Obj,
                                        COFFSymbolRef Symbol,
                                        uint8_t AuxSymbolIdx, const T *&Aux) {
  ArrayRef<uint8_t> AuxData = Obj->getSymbolAuxData(Symbol);
  AuxData = AuxData.slice(AuxSymbolIdx * Obj->getSymbolTableEntrySize());
  Aux = reinterpret_cast<const T*>(AuxData.data());
  return readobj_error::success;
}

void COFFDumper::cacheRelocations() {
  if (RelocCached)
    return;
  RelocCached = true;

  for (const SectionRef &S : Obj->sections()) {
    const coff_section *Section = Obj->getCOFFSection(S);

    for (const RelocationRef &Reloc : S.relocations())
      RelocMap[Section].push_back(Reloc);

    // Sort relocations by address.
    llvm::sort(RelocMap[Section], relocAddressLess);
  }
}

void COFFDumper::printDataDirectory(uint32_t Index, const std::string &FieldName) {
  const data_directory *Data;
  if (Obj->getDataDirectory(Index, Data))
    return;
  W.printHex(FieldName + "RVA", Data->RelativeVirtualAddress);
  W.printHex(FieldName + "Size", Data->Size);
}

void COFFDumper::printFileHeaders() {
  time_t TDS = Obj->getTimeDateStamp();
  char FormattedTime[20] = { };
  strftime(FormattedTime, 20, "%Y-%m-%d %H:%M:%S", gmtime(&TDS));

  {
    DictScope D(W, "ImageFileHeader");
    W.printEnum  ("Machine", Obj->getMachine(),
                    makeArrayRef(ImageFileMachineType));
    W.printNumber("SectionCount", Obj->getNumberOfSections());
    W.printHex   ("TimeDateStamp", FormattedTime, Obj->getTimeDateStamp());
    W.printHex   ("PointerToSymbolTable", Obj->getPointerToSymbolTable());
    W.printNumber("SymbolCount", Obj->getNumberOfSymbols());
    W.printNumber("OptionalHeaderSize", Obj->getSizeOfOptionalHeader());
    W.printFlags ("Characteristics", Obj->getCharacteristics(),
                    makeArrayRef(ImageFileCharacteristics));
  }

  // Print PE header. This header does not exist if this is an object file and
  // not an executable.
  const pe32_header *PEHeader = nullptr;
  error(Obj->getPE32Header(PEHeader));
  if (PEHeader)
    printPEHeader<pe32_header>(PEHeader);

  const pe32plus_header *PEPlusHeader = nullptr;
  error(Obj->getPE32PlusHeader(PEPlusHeader));
  if (PEPlusHeader)
    printPEHeader<pe32plus_header>(PEPlusHeader);

  if (const dos_header *DH = Obj->getDOSHeader())
    printDOSHeader(DH);
}

void COFFDumper::printDOSHeader(const dos_header *DH) {
  DictScope D(W, "DOSHeader");
  W.printString("Magic", StringRef(DH->Magic, sizeof(DH->Magic)));
  W.printNumber("UsedBytesInTheLastPage", DH->UsedBytesInTheLastPage);
  W.printNumber("FileSizeInPages", DH->FileSizeInPages);
  W.printNumber("NumberOfRelocationItems", DH->NumberOfRelocationItems);
  W.printNumber("HeaderSizeInParagraphs", DH->HeaderSizeInParagraphs);
  W.printNumber("MinimumExtraParagraphs", DH->MinimumExtraParagraphs);
  W.printNumber("MaximumExtraParagraphs", DH->MaximumExtraParagraphs);
  W.printNumber("InitialRelativeSS", DH->InitialRelativeSS);
  W.printNumber("InitialSP", DH->InitialSP);
  W.printNumber("Checksum", DH->Checksum);
  W.printNumber("InitialIP", DH->InitialIP);
  W.printNumber("InitialRelativeCS", DH->InitialRelativeCS);
  W.printNumber("AddressOfRelocationTable", DH->AddressOfRelocationTable);
  W.printNumber("OverlayNumber", DH->OverlayNumber);
  W.printNumber("OEMid", DH->OEMid);
  W.printNumber("OEMinfo", DH->OEMinfo);
  W.printNumber("AddressOfNewExeHeader", DH->AddressOfNewExeHeader);
}

template <class PEHeader>
void COFFDumper::printPEHeader(const PEHeader *Hdr) {
  DictScope D(W, "ImageOptionalHeader");
  W.printHex   ("Magic", Hdr->Magic);
  W.printNumber("MajorLinkerVersion", Hdr->MajorLinkerVersion);
  W.printNumber("MinorLinkerVersion", Hdr->MinorLinkerVersion);
  W.printNumber("SizeOfCode", Hdr->SizeOfCode);
  W.printNumber("SizeOfInitializedData", Hdr->SizeOfInitializedData);
  W.printNumber("SizeOfUninitializedData", Hdr->SizeOfUninitializedData);
  W.printHex   ("AddressOfEntryPoint", Hdr->AddressOfEntryPoint);
  W.printHex   ("BaseOfCode", Hdr->BaseOfCode);
  printBaseOfDataField(Hdr);
  W.printHex   ("ImageBase", Hdr->ImageBase);
  W.printNumber("SectionAlignment", Hdr->SectionAlignment);
  W.printNumber("FileAlignment", Hdr->FileAlignment);
  W.printNumber("MajorOperatingSystemVersion",
                Hdr->MajorOperatingSystemVersion);
  W.printNumber("MinorOperatingSystemVersion",
                Hdr->MinorOperatingSystemVersion);
  W.printNumber("MajorImageVersion", Hdr->MajorImageVersion);
  W.printNumber("MinorImageVersion", Hdr->MinorImageVersion);
  W.printNumber("MajorSubsystemVersion", Hdr->MajorSubsystemVersion);
  W.printNumber("MinorSubsystemVersion", Hdr->MinorSubsystemVersion);
  W.printNumber("SizeOfImage", Hdr->SizeOfImage);
  W.printNumber("SizeOfHeaders", Hdr->SizeOfHeaders);
  W.printEnum  ("Subsystem", Hdr->Subsystem, makeArrayRef(PEWindowsSubsystem));
  W.printFlags ("Characteristics", Hdr->DLLCharacteristics,
                makeArrayRef(PEDLLCharacteristics));
  W.printNumber("SizeOfStackReserve", Hdr->SizeOfStackReserve);
  W.printNumber("SizeOfStackCommit", Hdr->SizeOfStackCommit);
  W.printNumber("SizeOfHeapReserve", Hdr->SizeOfHeapReserve);
  W.printNumber("SizeOfHeapCommit", Hdr->SizeOfHeapCommit);
  W.printNumber("NumberOfRvaAndSize", Hdr->NumberOfRvaAndSize);

  if (Hdr->NumberOfRvaAndSize > 0) {
    DictScope D(W, "DataDirectory");
    static const char * const directory[] = {
      "ExportTable", "ImportTable", "ResourceTable", "ExceptionTable",
      "CertificateTable", "BaseRelocationTable", "Debug", "Architecture",
      "GlobalPtr", "TLSTable", "LoadConfigTable", "BoundImport", "IAT",
      "DelayImportDescriptor", "CLRRuntimeHeader", "Reserved"
    };

    for (uint32_t i = 0; i < Hdr->NumberOfRvaAndSize; ++i)
      printDataDirectory(i, directory[i]);
  }
}

void COFFDumper::printCOFFDebugDirectory() {
  ListScope LS(W, "DebugDirectory");
  for (const debug_directory &D : Obj->debug_directories()) {
    char FormattedTime[20] = {};
    time_t TDS = D.TimeDateStamp;
    strftime(FormattedTime, 20, "%Y-%m-%d %H:%M:%S", gmtime(&TDS));
    DictScope S(W, "DebugEntry");
    W.printHex("Characteristics", D.Characteristics);
    W.printHex("TimeDateStamp", FormattedTime, D.TimeDateStamp);
    W.printHex("MajorVersion", D.MajorVersion);
    W.printHex("MinorVersion", D.MinorVersion);
    W.printEnum("Type", D.Type, makeArrayRef(ImageDebugType));
    W.printHex("SizeOfData", D.SizeOfData);
    W.printHex("AddressOfRawData", D.AddressOfRawData);
    W.printHex("PointerToRawData", D.PointerToRawData);
    if (D.Type == COFF::IMAGE_DEBUG_TYPE_CODEVIEW) {
      const codeview::DebugInfo *DebugInfo;
      StringRef PDBFileName;
      error(Obj->getDebugPDBInfo(&D, DebugInfo, PDBFileName));
      DictScope PDBScope(W, "PDBInfo");
      W.printHex("PDBSignature", DebugInfo->Signature.CVSignature);
      if (DebugInfo->Signature.CVSignature == OMF::Signature::PDB70) {
        W.printBinary("PDBGUID", makeArrayRef(DebugInfo->PDB70.Signature));
        W.printNumber("PDBAge", DebugInfo->PDB70.Age);
        W.printString("PDBFileName", PDBFileName);
      }
    } else if (D.SizeOfData != 0) {
      // FIXME: Type values of 12 and 13 are commonly observed but are not in
      // the documented type enum.  Figure out what they mean.
      ArrayRef<uint8_t> RawData;
      error(
          Obj->getRvaAndSizeAsBytes(D.AddressOfRawData, D.SizeOfData, RawData));
      W.printBinaryBlock("RawData", RawData);
    }
  }
}

void COFFDumper::printRVATable(uint64_t TableVA, uint64_t Count,
                               uint64_t EntrySize, PrintExtraCB PrintExtra) {
  uintptr_t TableStart, TableEnd;
  error(Obj->getVaPtr(TableVA, TableStart));
  error(Obj->getVaPtr(TableVA + Count * EntrySize - 1, TableEnd));
  TableEnd++;
  for (uintptr_t I = TableStart; I < TableEnd; I += EntrySize) {
    uint32_t RVA = *reinterpret_cast<const ulittle32_t *>(I);
    raw_ostream &OS = W.startLine();
    OS << W.hex(Obj->getImageBase() + RVA);
    if (PrintExtra)
      PrintExtra(OS, reinterpret_cast<const uint8_t *>(I));
    OS << '\n';
  }
}

void COFFDumper::printCOFFLoadConfig() {
  LoadConfigTables Tables;
  if (Obj->is64())
    printCOFFLoadConfig(Obj->getLoadConfig64(), Tables);
  else
    printCOFFLoadConfig(Obj->getLoadConfig32(), Tables);

  if (Tables.SEHTableVA) {
    ListScope LS(W, "SEHTable");
    printRVATable(Tables.SEHTableVA, Tables.SEHTableCount, 4);
  }

  if (Tables.GuardFidTableVA) {
    ListScope LS(W, "GuardFidTable");
    if (Tables.GuardFlags & uint32_t(coff_guard_flags::FidTableHasFlags)) {
      auto PrintGuardFlags = [](raw_ostream &OS, const uint8_t *Entry) {
        uint8_t Flags = *reinterpret_cast<const uint8_t *>(Entry + 4);
        if (Flags)
          OS << " flags " << utohexstr(Flags);
      };
      printRVATable(Tables.GuardFidTableVA, Tables.GuardFidTableCount, 5,
                    PrintGuardFlags);
    } else {
      printRVATable(Tables.GuardFidTableVA, Tables.GuardFidTableCount, 4);
    }
  }

  if (Tables.GuardLJmpTableVA) {
    ListScope LS(W, "GuardLJmpTable");
    printRVATable(Tables.GuardLJmpTableVA, Tables.GuardLJmpTableCount, 4);
  }
}

template <typename T>
void COFFDumper::printCOFFLoadConfig(const T *Conf, LoadConfigTables &Tables) {
  if (!Conf)
    return;

  ListScope LS(W, "LoadConfig");
  char FormattedTime[20] = {};
  time_t TDS = Conf->TimeDateStamp;
  strftime(FormattedTime, 20, "%Y-%m-%d %H:%M:%S", gmtime(&TDS));
  W.printHex("Size", Conf->Size);

  // Print everything before SecurityCookie. The vast majority of images today
  // have all these fields.
  if (Conf->Size < offsetof(T, SEHandlerTable))
    return;
  W.printHex("TimeDateStamp", FormattedTime, TDS);
  W.printHex("MajorVersion", Conf->MajorVersion);
  W.printHex("MinorVersion", Conf->MinorVersion);
  W.printHex("GlobalFlagsClear", Conf->GlobalFlagsClear);
  W.printHex("GlobalFlagsSet", Conf->GlobalFlagsSet);
  W.printHex("CriticalSectionDefaultTimeout",
             Conf->CriticalSectionDefaultTimeout);
  W.printHex("DeCommitFreeBlockThreshold", Conf->DeCommitFreeBlockThreshold);
  W.printHex("DeCommitTotalFreeThreshold", Conf->DeCommitTotalFreeThreshold);
  W.printHex("LockPrefixTable", Conf->LockPrefixTable);
  W.printHex("MaximumAllocationSize", Conf->MaximumAllocationSize);
  W.printHex("VirtualMemoryThreshold", Conf->VirtualMemoryThreshold);
  W.printHex("ProcessHeapFlags", Conf->ProcessHeapFlags);
  W.printHex("ProcessAffinityMask", Conf->ProcessAffinityMask);
  W.printHex("CSDVersion", Conf->CSDVersion);
  W.printHex("DependentLoadFlags", Conf->DependentLoadFlags);
  W.printHex("EditList", Conf->EditList);
  W.printHex("SecurityCookie", Conf->SecurityCookie);

  // Print the safe SEH table if present.
  if (Conf->Size < offsetof(coff_load_configuration32, GuardCFCheckFunction))
    return;
  W.printHex("SEHandlerTable", Conf->SEHandlerTable);
  W.printNumber("SEHandlerCount", Conf->SEHandlerCount);

  Tables.SEHTableVA = Conf->SEHandlerTable;
  Tables.SEHTableCount = Conf->SEHandlerCount;

  // Print everything before CodeIntegrity. (2015)
  if (Conf->Size < offsetof(T, CodeIntegrity))
    return;
  W.printHex("GuardCFCheckFunction", Conf->GuardCFCheckFunction);
  W.printHex("GuardCFCheckDispatch", Conf->GuardCFCheckDispatch);
  W.printHex("GuardCFFunctionTable", Conf->GuardCFFunctionTable);
  W.printNumber("GuardCFFunctionCount", Conf->GuardCFFunctionCount);
  W.printHex("GuardFlags", Conf->GuardFlags);

  Tables.GuardFidTableVA = Conf->GuardCFFunctionTable;
  Tables.GuardFidTableCount = Conf->GuardCFFunctionCount;
  Tables.GuardFlags = Conf->GuardFlags;

  // Print the rest. (2017)
  if (Conf->Size < sizeof(T))
    return;
  W.printHex("GuardAddressTakenIatEntryTable",
             Conf->GuardAddressTakenIatEntryTable);
  W.printNumber("GuardAddressTakenIatEntryCount",
                Conf->GuardAddressTakenIatEntryCount);
  W.printHex("GuardLongJumpTargetTable", Conf->GuardLongJumpTargetTable);
  W.printNumber("GuardLongJumpTargetCount", Conf->GuardLongJumpTargetCount);
  W.printHex("DynamicValueRelocTable", Conf->DynamicValueRelocTable);
  W.printHex("CHPEMetadataPointer", Conf->CHPEMetadataPointer);
  W.printHex("GuardRFFailureRoutine", Conf->GuardRFFailureRoutine);
  W.printHex("GuardRFFailureRoutineFunctionPointer",
             Conf->GuardRFFailureRoutineFunctionPointer);
  W.printHex("DynamicValueRelocTableOffset",
             Conf->DynamicValueRelocTableOffset);
  W.printNumber("DynamicValueRelocTableSection",
                Conf->DynamicValueRelocTableSection);
  W.printHex("GuardRFVerifyStackPointerFunctionPointer",
             Conf->GuardRFVerifyStackPointerFunctionPointer);
  W.printHex("HotPatchTableOffset", Conf->HotPatchTableOffset);

  Tables.GuardLJmpTableVA = Conf->GuardLongJumpTargetTable;
  Tables.GuardLJmpTableCount = Conf->GuardLongJumpTargetCount;
}

void COFFDumper::printBaseOfDataField(const pe32_header *Hdr) {
  W.printHex("BaseOfData", Hdr->BaseOfData);
}

void COFFDumper::printBaseOfDataField(const pe32plus_header *) {}

void COFFDumper::printCodeViewDebugInfo() {
  // Print types first to build CVUDTNames, then print symbols.
  for (const SectionRef &S : Obj->sections()) {
    StringRef SectionName;
    error(S.getName(SectionName));
    // .debug$T is a standard CodeView type section, while .debug$P is the same
    // format but used for MSVC precompiled header object files.
    if (SectionName == ".debug$T" || SectionName == ".debug$P")
      printCodeViewTypeSection(SectionName, S);
  }
  for (const SectionRef &S : Obj->sections()) {
    StringRef SectionName;
    error(S.getName(SectionName));
    if (SectionName == ".debug$S")
      printCodeViewSymbolSection(SectionName, S);
  }
}

void COFFDumper::initializeFileAndStringTables(BinaryStreamReader &Reader) {
  while (Reader.bytesRemaining() > 0 &&
         (!CVFileChecksumTable.valid() || !CVStringTable.valid())) {
    // The section consists of a number of subsection in the following format:
    // |SubSectionType|SubSectionSize|Contents...|
    uint32_t SubType, SubSectionSize;
    error(Reader.readInteger(SubType));
    error(Reader.readInteger(SubSectionSize));

    StringRef Contents;
    error(Reader.readFixedString(Contents, SubSectionSize));

    BinaryStreamRef ST(Contents, support::little);
    switch (DebugSubsectionKind(SubType)) {
    case DebugSubsectionKind::FileChecksums:
      error(CVFileChecksumTable.initialize(ST));
      break;
    case DebugSubsectionKind::StringTable:
      error(CVStringTable.initialize(ST));
      break;
    default:
      break;
    }

    uint32_t PaddedSize = alignTo(SubSectionSize, 4);
    error(Reader.skip(PaddedSize - SubSectionSize));
  }
}

void COFFDumper::printCodeViewSymbolSection(StringRef SectionName,
                                            const SectionRef &Section) {
  StringRef SectionContents;
  error(Section.getContents(SectionContents));
  StringRef Data = SectionContents;

  SmallVector<StringRef, 10> FunctionNames;
  StringMap<StringRef> FunctionLineTables;

  ListScope D(W, "CodeViewDebugInfo");
  // Print the section to allow correlation with printSectionHeaders.
  W.printNumber("Section", SectionName, Obj->getSectionID(Section));

  uint32_t Magic;
  error(consume(Data, Magic));
  W.printHex("Magic", Magic);
  if (Magic != COFF::DEBUG_SECTION_MAGIC)
    return error(object_error::parse_failed);

  BinaryStreamReader FSReader(Data, support::little);
  initializeFileAndStringTables(FSReader);

  // TODO: Convert this over to using ModuleSubstreamVisitor.
  while (!Data.empty()) {
    // The section consists of a number of subsection in the following format:
    // |SubSectionType|SubSectionSize|Contents...|
    uint32_t SubType, SubSectionSize;
    error(consume(Data, SubType));
    error(consume(Data, SubSectionSize));

    ListScope S(W, "Subsection");
    W.printEnum("SubSectionType", SubType, makeArrayRef(SubSectionTypes));
    W.printHex("SubSectionSize", SubSectionSize);

    // Get the contents of the subsection.
    if (SubSectionSize > Data.size())
      return error(object_error::parse_failed);
    StringRef Contents = Data.substr(0, SubSectionSize);

    // Add SubSectionSize to the current offset and align that offset to find
    // the next subsection.
    size_t SectionOffset = Data.data() - SectionContents.data();
    size_t NextOffset = SectionOffset + SubSectionSize;
    NextOffset = alignTo(NextOffset, 4);
    if (NextOffset > SectionContents.size())
      return error(object_error::parse_failed);
    Data = SectionContents.drop_front(NextOffset);

    // Optionally print the subsection bytes in case our parsing gets confused
    // later.
    if (opts::CodeViewSubsectionBytes)
      printBinaryBlockWithRelocs("SubSectionContents", Section, SectionContents,
                                 Contents);

    switch (DebugSubsectionKind(SubType)) {
    case DebugSubsectionKind::Symbols:
      printCodeViewSymbolsSubsection(Contents, Section, SectionContents);
      break;

    case DebugSubsectionKind::InlineeLines:
      printCodeViewInlineeLines(Contents);
      break;

    case DebugSubsectionKind::FileChecksums:
      printCodeViewFileChecksums(Contents);
      break;

    case DebugSubsectionKind::Lines: {
      // Holds a PC to file:line table.  Some data to parse this subsection is
      // stored in the other subsections, so just check sanity and store the
      // pointers for deferred processing.

      if (SubSectionSize < 12) {
        // There should be at least three words to store two function
        // relocations and size of the code.
        error(object_error::parse_failed);
        return;
      }

      StringRef LinkageName;
      error(resolveSymbolName(Obj->getCOFFSection(Section), SectionOffset,
                              LinkageName));
      W.printString("LinkageName", LinkageName);
      if (FunctionLineTables.count(LinkageName) != 0) {
        // Saw debug info for this function already?
        error(object_error::parse_failed);
        return;
      }

      FunctionLineTables[LinkageName] = Contents;
      FunctionNames.push_back(LinkageName);
      break;
    }
    case DebugSubsectionKind::FrameData: {
      // First four bytes is a relocation against the function.
      BinaryStreamReader SR(Contents, llvm::support::little);

      DebugFrameDataSubsectionRef FrameData;
      error(FrameData.initialize(SR));

      StringRef LinkageName;
      error(resolveSymbolName(Obj->getCOFFSection(Section), SectionContents,
                              FrameData.getRelocPtr(), LinkageName));
      W.printString("LinkageName", LinkageName);

      // To find the active frame description, search this array for the
      // smallest PC range that includes the current PC.
      for (const auto &FD : FrameData) {
        StringRef FrameFunc = error(CVStringTable.getString(FD.FrameFunc));

        DictScope S(W, "FrameData");
        W.printHex("RvaStart", FD.RvaStart);
        W.printHex("CodeSize", FD.CodeSize);
        W.printHex("LocalSize", FD.LocalSize);
        W.printHex("ParamsSize", FD.ParamsSize);
        W.printHex("MaxStackSize", FD.MaxStackSize);
        W.printHex("PrologSize", FD.PrologSize);
        W.printHex("SavedRegsSize", FD.SavedRegsSize);
        W.printFlags("Flags", FD.Flags, makeArrayRef(FrameDataFlags));

        // The FrameFunc string is a small RPN program. It can be broken up into
        // statements that end in the '=' operator, which assigns the value on
        // the top of the stack to the previously pushed variable. Variables can
        // be temporary values ($T0) or physical registers ($esp). Print each
        // assignment on its own line to make these programs easier to read.
        {
          ListScope FFS(W, "FrameFunc");
          while (!FrameFunc.empty()) {
            size_t EqOrEnd = FrameFunc.find('=');
            if (EqOrEnd == StringRef::npos)
              EqOrEnd = FrameFunc.size();
            else
              ++EqOrEnd;
            StringRef Stmt = FrameFunc.substr(0, EqOrEnd);
            W.printString(Stmt);
            FrameFunc = FrameFunc.drop_front(EqOrEnd).trim();
          }
        }
      }
      break;
    }

    // Do nothing for unrecognized subsections.
    default:
      break;
    }
    W.flush();
  }

  // Dump the line tables now that we've read all the subsections and know all
  // the required information.
  for (unsigned I = 0, E = FunctionNames.size(); I != E; ++I) {
    StringRef Name = FunctionNames[I];
    ListScope S(W, "FunctionLineTable");
    W.printString("LinkageName", Name);

    BinaryStreamReader Reader(FunctionLineTables[Name], support::little);

    DebugLinesSubsectionRef LineInfo;
    error(LineInfo.initialize(Reader));

    W.printHex("Flags", LineInfo.header()->Flags);
    W.printHex("CodeSize", LineInfo.header()->CodeSize);
    for (const auto &Entry : LineInfo) {

      ListScope S(W, "FilenameSegment");
      printFileNameForOffset("Filename", Entry.NameIndex);
      uint32_t ColumnIndex = 0;
      for (const auto &Line : Entry.LineNumbers) {
        if (Line.Offset >= LineInfo.header()->CodeSize) {
          error(object_error::parse_failed);
          return;
        }

        std::string PC = formatv("+{0:X}", uint32_t(Line.Offset));
        ListScope PCScope(W, PC);
        codeview::LineInfo LI(Line.Flags);

        if (LI.isAlwaysStepInto())
          W.printString("StepInto", StringRef("Always"));
        else if (LI.isNeverStepInto())
          W.printString("StepInto", StringRef("Never"));
        else
          W.printNumber("LineNumberStart", LI.getStartLine());
        W.printNumber("LineNumberEndDelta", LI.getLineDelta());
        W.printBoolean("IsStatement", LI.isStatement());
        if (LineInfo.hasColumnInfo()) {
          W.printNumber("ColStart", Entry.Columns[ColumnIndex].StartColumn);
          W.printNumber("ColEnd", Entry.Columns[ColumnIndex].EndColumn);
          ++ColumnIndex;
        }
      }
    }
  }
}

void COFFDumper::printCodeViewSymbolsSubsection(StringRef Subsection,
                                                const SectionRef &Section,
                                                StringRef SectionContents) {
  ArrayRef<uint8_t> BinaryData(Subsection.bytes_begin(),
                               Subsection.bytes_end());
  auto CODD = llvm::make_unique<COFFObjectDumpDelegate>(*this, Section, Obj,
                                                        SectionContents);
  CVSymbolDumper CVSD(W, Types, CodeViewContainer::ObjectFile, std::move(CODD),
                      CompilationCPUType, opts::CodeViewSubsectionBytes);
  CVSymbolArray Symbols;
  BinaryStreamReader Reader(BinaryData, llvm::support::little);
  if (auto EC = Reader.readArray(Symbols, Reader.getLength())) {
    consumeError(std::move(EC));
    W.flush();
    error(object_error::parse_failed);
  }

  if (auto EC = CVSD.dump(Symbols)) {
    W.flush();
    error(std::move(EC));
  }
  CompilationCPUType = CVSD.getCompilationCPUType();
  W.flush();
}

void COFFDumper::printCodeViewFileChecksums(StringRef Subsection) {
  BinaryStreamRef Stream(Subsection, llvm::support::little);
  DebugChecksumsSubsectionRef Checksums;
  error(Checksums.initialize(Stream));

  for (auto &FC : Checksums) {
    DictScope S(W, "FileChecksum");

    StringRef Filename = error(CVStringTable.getString(FC.FileNameOffset));
    W.printHex("Filename", Filename, FC.FileNameOffset);
    W.printHex("ChecksumSize", FC.Checksum.size());
    W.printEnum("ChecksumKind", uint8_t(FC.Kind),
                makeArrayRef(FileChecksumKindNames));

    W.printBinary("ChecksumBytes", FC.Checksum);
  }
}

void COFFDumper::printCodeViewInlineeLines(StringRef Subsection) {
  BinaryStreamReader SR(Subsection, llvm::support::little);
  DebugInlineeLinesSubsectionRef Lines;
  error(Lines.initialize(SR));

  for (auto &Line : Lines) {
    DictScope S(W, "InlineeSourceLine");
    printTypeIndex("Inlinee", Line.Header->Inlinee);
    printFileNameForOffset("FileID", Line.Header->FileID);
    W.printNumber("SourceLineNum", Line.Header->SourceLineNum);

    if (Lines.hasExtraFiles()) {
      W.printNumber("ExtraFileCount", Line.ExtraFiles.size());
      ListScope ExtraFiles(W, "ExtraFiles");
      for (const auto &FID : Line.ExtraFiles) {
        printFileNameForOffset("FileID", FID);
      }
    }
  }
}

StringRef COFFDumper::getFileNameForFileOffset(uint32_t FileOffset) {
  // The file checksum subsection should precede all references to it.
  if (!CVFileChecksumTable.valid() || !CVStringTable.valid())
    error(object_error::parse_failed);

  auto Iter = CVFileChecksumTable.getArray().at(FileOffset);

  // Check if the file checksum table offset is valid.
  if (Iter == CVFileChecksumTable.end())
    error(object_error::parse_failed);

  return error(CVStringTable.getString(Iter->FileNameOffset));
}

void COFFDumper::printFileNameForOffset(StringRef Label, uint32_t FileOffset) {
  W.printHex(Label, getFileNameForFileOffset(FileOffset), FileOffset);
}

void COFFDumper::mergeCodeViewTypes(MergingTypeTableBuilder &CVIDs,
                                    MergingTypeTableBuilder &CVTypes) {
  for (const SectionRef &S : Obj->sections()) {
    StringRef SectionName;
    error(S.getName(SectionName));
    if (SectionName == ".debug$T") {
      StringRef Data;
      error(S.getContents(Data));
      uint32_t Magic;
      error(consume(Data, Magic));
      if (Magic != 4)
        error(object_error::parse_failed);

      CVTypeArray Types;
      BinaryStreamReader Reader(Data, llvm::support::little);
      if (auto EC = Reader.readArray(Types, Reader.getLength())) {
        consumeError(std::move(EC));
        W.flush();
        error(object_error::parse_failed);
      }
      SmallVector<TypeIndex, 128> SourceToDest;
      Optional<uint32_t> PCHSignature;
      if (auto EC = mergeTypeAndIdRecords(CVIDs, CVTypes, SourceToDest, Types,
                                          PCHSignature))
        return error(std::move(EC));
    }
  }
}

void COFFDumper::printCodeViewTypeSection(StringRef SectionName,
                                          const SectionRef &Section) {
  ListScope D(W, "CodeViewTypes");
  W.printNumber("Section", SectionName, Obj->getSectionID(Section));

  StringRef Data;
  error(Section.getContents(Data));
  if (opts::CodeViewSubsectionBytes)
    W.printBinaryBlock("Data", Data);

  uint32_t Magic;
  error(consume(Data, Magic));
  W.printHex("Magic", Magic);
  if (Magic != COFF::DEBUG_SECTION_MAGIC)
    return error(object_error::parse_failed);

  Types.reset(Data, 100);

  TypeDumpVisitor TDV(Types, &W, opts::CodeViewSubsectionBytes);
  error(codeview::visitTypeStream(Types, TDV));
  W.flush();
}

void COFFDumper::printSectionHeaders() {
  ListScope SectionsD(W, "Sections");
  int SectionNumber = 0;
  for (const SectionRef &Sec : Obj->sections()) {
    ++SectionNumber;
    const coff_section *Section = Obj->getCOFFSection(Sec);

    StringRef Name;
    error(Sec.getName(Name));

    DictScope D(W, "Section");
    W.printNumber("Number", SectionNumber);
    W.printBinary("Name", Name, Section->Name);
    W.printHex   ("VirtualSize", Section->VirtualSize);
    W.printHex   ("VirtualAddress", Section->VirtualAddress);
    W.printNumber("RawDataSize", Section->SizeOfRawData);
    W.printHex   ("PointerToRawData", Section->PointerToRawData);
    W.printHex   ("PointerToRelocations", Section->PointerToRelocations);
    W.printHex   ("PointerToLineNumbers", Section->PointerToLinenumbers);
    W.printNumber("RelocationCount", Section->NumberOfRelocations);
    W.printNumber("LineNumberCount", Section->NumberOfLinenumbers);
    W.printFlags ("Characteristics", Section->Characteristics,
                    makeArrayRef(ImageSectionCharacteristics),
                    COFF::SectionCharacteristics(0x00F00000));

    if (opts::SectionRelocations) {
      ListScope D(W, "Relocations");
      for (const RelocationRef &Reloc : Sec.relocations())
        printRelocation(Sec, Reloc);
    }

    if (opts::SectionSymbols) {
      ListScope D(W, "Symbols");
      for (const SymbolRef &Symbol : Obj->symbols()) {
        if (!Sec.containsSymbol(Symbol))
          continue;

        printSymbol(Symbol);
      }
    }

    if (opts::SectionData &&
        !(Section->Characteristics & COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA)) {
      StringRef Data;
      error(Sec.getContents(Data));

      W.printBinaryBlock("SectionData", Data);
    }
  }
}

void COFFDumper::printRelocations() {
  ListScope D(W, "Relocations");

  int SectionNumber = 0;
  for (const SectionRef &Section : Obj->sections()) {
    ++SectionNumber;
    StringRef Name;
    error(Section.getName(Name));

    bool PrintedGroup = false;
    for (const RelocationRef &Reloc : Section.relocations()) {
      if (!PrintedGroup) {
        W.startLine() << "Section (" << SectionNumber << ") " << Name << " {\n";
        W.indent();
        PrintedGroup = true;
      }

      printRelocation(Section, Reloc);
    }

    if (PrintedGroup) {
      W.unindent();
      W.startLine() << "}\n";
    }
  }
}

void COFFDumper::printRelocation(const SectionRef &Section,
                                 const RelocationRef &Reloc, uint64_t Bias) {
  uint64_t Offset = Reloc.getOffset() - Bias;
  uint64_t RelocType = Reloc.getType();
  SmallString<32> RelocName;
  StringRef SymbolName;
  Reloc.getTypeName(RelocName);
  symbol_iterator Symbol = Reloc.getSymbol();
  int64_t SymbolIndex = -1;
  if (Symbol != Obj->symbol_end()) {
    Expected<StringRef> SymbolNameOrErr = Symbol->getName();
    error(errorToErrorCode(SymbolNameOrErr.takeError()));
    SymbolName = *SymbolNameOrErr;
    SymbolIndex = Obj->getSymbolIndex(Obj->getCOFFSymbol(*Symbol));
  }

  if (opts::ExpandRelocs) {
    DictScope Group(W, "Relocation");
    W.printHex("Offset", Offset);
    W.printNumber("Type", RelocName, RelocType);
    W.printString("Symbol", SymbolName.empty() ? "-" : SymbolName);
    W.printNumber("SymbolIndex", SymbolIndex);
  } else {
    raw_ostream& OS = W.startLine();
    OS << W.hex(Offset)
       << " " << RelocName
       << " " << (SymbolName.empty() ? "-" : SymbolName)
       << " (" << SymbolIndex << ")"
       << "\n";
  }
}

void COFFDumper::printSymbols() {
  ListScope Group(W, "Symbols");

  for (const SymbolRef &Symbol : Obj->symbols())
    printSymbol(Symbol);
}

void COFFDumper::printDynamicSymbols() { ListScope Group(W, "DynamicSymbols"); }

static ErrorOr<StringRef>
getSectionName(const llvm::object::COFFObjectFile *Obj, int32_t SectionNumber,
               const coff_section *Section) {
  if (Section) {
    StringRef SectionName;
    if (std::error_code EC = Obj->getSectionName(Section, SectionName))
      return EC;
    return SectionName;
  }
  if (SectionNumber == llvm::COFF::IMAGE_SYM_DEBUG)
    return StringRef("IMAGE_SYM_DEBUG");
  if (SectionNumber == llvm::COFF::IMAGE_SYM_ABSOLUTE)
    return StringRef("IMAGE_SYM_ABSOLUTE");
  if (SectionNumber == llvm::COFF::IMAGE_SYM_UNDEFINED)
    return StringRef("IMAGE_SYM_UNDEFINED");
  return StringRef("");
}

void COFFDumper::printSymbol(const SymbolRef &Sym) {
  DictScope D(W, "Symbol");

  COFFSymbolRef Symbol = Obj->getCOFFSymbol(Sym);
  const coff_section *Section;
  if (std::error_code EC = Obj->getSection(Symbol.getSectionNumber(), Section)) {
    W.startLine() << "Invalid section number: " << EC.message() << "\n";
    W.flush();
    return;
  }

  StringRef SymbolName;
  if (Obj->getSymbolName(Symbol, SymbolName))
    SymbolName = "";

  StringRef SectionName = "";
  ErrorOr<StringRef> Res =
      getSectionName(Obj, Symbol.getSectionNumber(), Section);
  if (Res)
    SectionName = *Res;

  W.printString("Name", SymbolName);
  W.printNumber("Value", Symbol.getValue());
  W.printNumber("Section", SectionName, Symbol.getSectionNumber());
  W.printEnum  ("BaseType", Symbol.getBaseType(), makeArrayRef(ImageSymType));
  W.printEnum  ("ComplexType", Symbol.getComplexType(),
                                                   makeArrayRef(ImageSymDType));
  W.printEnum  ("StorageClass", Symbol.getStorageClass(),
                                                   makeArrayRef(ImageSymClass));
  W.printNumber("AuxSymbolCount", Symbol.getNumberOfAuxSymbols());

  for (uint8_t I = 0; I < Symbol.getNumberOfAuxSymbols(); ++I) {
    if (Symbol.isFunctionDefinition()) {
      const coff_aux_function_definition *Aux;
      error(getSymbolAuxData(Obj, Symbol, I, Aux));

      DictScope AS(W, "AuxFunctionDef");
      W.printNumber("TagIndex", Aux->TagIndex);
      W.printNumber("TotalSize", Aux->TotalSize);
      W.printHex("PointerToLineNumber", Aux->PointerToLinenumber);
      W.printHex("PointerToNextFunction", Aux->PointerToNextFunction);

    } else if (Symbol.isAnyUndefined()) {
      const coff_aux_weak_external *Aux;
      error(getSymbolAuxData(Obj, Symbol, I, Aux));

      Expected<COFFSymbolRef> Linked = Obj->getSymbol(Aux->TagIndex);
      StringRef LinkedName;
      std::error_code EC = errorToErrorCode(Linked.takeError());
      if (EC || (EC = Obj->getSymbolName(*Linked, LinkedName))) {
        LinkedName = "";
        error(EC);
      }

      DictScope AS(W, "AuxWeakExternal");
      W.printNumber("Linked", LinkedName, Aux->TagIndex);
      W.printEnum  ("Search", Aux->Characteristics,
                    makeArrayRef(WeakExternalCharacteristics));

    } else if (Symbol.isFileRecord()) {
      const char *FileName;
      error(getSymbolAuxData(Obj, Symbol, I, FileName));

      DictScope AS(W, "AuxFileRecord");

      StringRef Name(FileName, Symbol.getNumberOfAuxSymbols() *
                                   Obj->getSymbolTableEntrySize());
      W.printString("FileName", Name.rtrim(StringRef("\0", 1)));
      break;
    } else if (Symbol.isSectionDefinition()) {
      const coff_aux_section_definition *Aux;
      error(getSymbolAuxData(Obj, Symbol, I, Aux));

      int32_t AuxNumber = Aux->getNumber(Symbol.isBigObj());

      DictScope AS(W, "AuxSectionDef");
      W.printNumber("Length", Aux->Length);
      W.printNumber("RelocationCount", Aux->NumberOfRelocations);
      W.printNumber("LineNumberCount", Aux->NumberOfLinenumbers);
      W.printHex("Checksum", Aux->CheckSum);
      W.printNumber("Number", AuxNumber);
      W.printEnum("Selection", Aux->Selection, makeArrayRef(ImageCOMDATSelect));

      if (Section && Section->Characteristics & COFF::IMAGE_SCN_LNK_COMDAT
          && Aux->Selection == COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE) {
        const coff_section *Assoc;
        StringRef AssocName = "";
        std::error_code EC = Obj->getSection(AuxNumber, Assoc);
        ErrorOr<StringRef> Res = getSectionName(Obj, AuxNumber, Assoc);
        if (Res)
          AssocName = *Res;
        if (!EC)
          EC = Res.getError();
        if (EC) {
          AssocName = "";
          error(EC);
        }

        W.printNumber("AssocSection", AssocName, AuxNumber);
      }
    } else if (Symbol.isCLRToken()) {
      const coff_aux_clr_token *Aux;
      error(getSymbolAuxData(Obj, Symbol, I, Aux));

      Expected<COFFSymbolRef> ReferredSym =
          Obj->getSymbol(Aux->SymbolTableIndex);
      StringRef ReferredName;
      std::error_code EC = errorToErrorCode(ReferredSym.takeError());
      if (EC || (EC = Obj->getSymbolName(*ReferredSym, ReferredName))) {
        ReferredName = "";
        error(EC);
      }

      DictScope AS(W, "AuxCLRToken");
      W.printNumber("AuxType", Aux->AuxType);
      W.printNumber("Reserved", Aux->Reserved);
      W.printNumber("SymbolTableIndex", ReferredName, Aux->SymbolTableIndex);

    } else {
      W.startLine() << "<unhandled auxiliary record>\n";
    }
  }
}

void COFFDumper::printUnwindInfo() {
  ListScope D(W, "UnwindInformation");
  switch (Obj->getMachine()) {
  case COFF::IMAGE_FILE_MACHINE_AMD64: {
    Win64EH::Dumper Dumper(W);
    Win64EH::Dumper::SymbolResolver
    Resolver = [](const object::coff_section *Section, uint64_t Offset,
                  SymbolRef &Symbol, void *user_data) -> std::error_code {
      COFFDumper *Dumper = reinterpret_cast<COFFDumper *>(user_data);
      return Dumper->resolveSymbol(Section, Offset, Symbol);
    };
    Win64EH::Dumper::Context Ctx(*Obj, Resolver, this);
    Dumper.printData(Ctx);
    break;
  }
  case COFF::IMAGE_FILE_MACHINE_ARM64:
  case COFF::IMAGE_FILE_MACHINE_ARMNT: {
    ARM::WinEH::Decoder Decoder(W, Obj->getMachine() ==
                                       COFF::IMAGE_FILE_MACHINE_ARM64);
    Decoder.dumpProcedureData(*Obj);
    break;
  }
  default:
    W.printEnum("unsupported Image Machine", Obj->getMachine(),
                makeArrayRef(ImageFileMachineType));
    break;
  }
}

void COFFDumper::printNeededLibraries() {
  ListScope D(W, "NeededLibraries");

  using LibsTy = std::vector<StringRef>;
  LibsTy Libs;

  for (const ImportDirectoryEntryRef &DirRef : Obj->import_directories()) {
    StringRef Name;
    if (!DirRef.getName(Name))
      Libs.push_back(Name);
  }

  std::stable_sort(Libs.begin(), Libs.end());

  for (const auto &L : Libs) {
    outs() << "  " << L << "\n";
  }
}

void COFFDumper::printImportedSymbols(
    iterator_range<imported_symbol_iterator> Range) {
  for (const ImportedSymbolRef &I : Range) {
    StringRef Sym;
    error(I.getSymbolName(Sym));
    uint16_t Ordinal;
    error(I.getOrdinal(Ordinal));
    W.printNumber("Symbol", Sym, Ordinal);
  }
}

void COFFDumper::printDelayImportedSymbols(
    const DelayImportDirectoryEntryRef &I,
    iterator_range<imported_symbol_iterator> Range) {
  int Index = 0;
  for (const ImportedSymbolRef &S : Range) {
    DictScope Import(W, "Import");
    StringRef Sym;
    error(S.getSymbolName(Sym));
    uint16_t Ordinal;
    error(S.getOrdinal(Ordinal));
    W.printNumber("Symbol", Sym, Ordinal);
    uint64_t Addr;
    error(I.getImportAddress(Index++, Addr));
    W.printHex("Address", Addr);
  }
}

void COFFDumper::printCOFFImports() {
  // Regular imports
  for (const ImportDirectoryEntryRef &I : Obj->import_directories()) {
    DictScope Import(W, "Import");
    StringRef Name;
    error(I.getName(Name));
    W.printString("Name", Name);
    uint32_t ILTAddr;
    error(I.getImportLookupTableRVA(ILTAddr));
    W.printHex("ImportLookupTableRVA", ILTAddr);
    uint32_t IATAddr;
    error(I.getImportAddressTableRVA(IATAddr));
    W.printHex("ImportAddressTableRVA", IATAddr);
    // The import lookup table can be missing with certain older linkers, so
    // fall back to the import address table in that case.
    if (ILTAddr)
      printImportedSymbols(I.lookup_table_symbols());
    else
      printImportedSymbols(I.imported_symbols());
  }

  // Delay imports
  for (const DelayImportDirectoryEntryRef &I : Obj->delay_import_directories()) {
    DictScope Import(W, "DelayImport");
    StringRef Name;
    error(I.getName(Name));
    W.printString("Name", Name);
    const delay_import_directory_table_entry *Table;
    error(I.getDelayImportTable(Table));
    W.printHex("Attributes", Table->Attributes);
    W.printHex("ModuleHandle", Table->ModuleHandle);
    W.printHex("ImportAddressTable", Table->DelayImportAddressTable);
    W.printHex("ImportNameTable", Table->DelayImportNameTable);
    W.printHex("BoundDelayImportTable", Table->BoundDelayImportTable);
    W.printHex("UnloadDelayImportTable", Table->UnloadDelayImportTable);
    printDelayImportedSymbols(I, I.imported_symbols());
  }
}

void COFFDumper::printCOFFExports() {
  for (const ExportDirectoryEntryRef &E : Obj->export_directories()) {
    DictScope Export(W, "Export");

    StringRef Name;
    uint32_t Ordinal, RVA;

    error(E.getSymbolName(Name));
    error(E.getOrdinal(Ordinal));
    error(E.getExportRVA(RVA));

    W.printNumber("Ordinal", Ordinal);
    W.printString("Name", Name);
    W.printHex("RVA", RVA);
  }
}

void COFFDumper::printCOFFDirectives() {
  for (const SectionRef &Section : Obj->sections()) {
    StringRef Contents;
    StringRef Name;

    error(Section.getName(Name));
    if (Name != ".drectve")
      continue;

    error(Section.getContents(Contents));

    W.printString("Directive(s)", Contents);
  }
}

static std::string getBaseRelocTypeName(uint8_t Type) {
  switch (Type) {
  case COFF::IMAGE_REL_BASED_ABSOLUTE: return "ABSOLUTE";
  case COFF::IMAGE_REL_BASED_HIGH: return "HIGH";
  case COFF::IMAGE_REL_BASED_LOW: return "LOW";
  case COFF::IMAGE_REL_BASED_HIGHLOW: return "HIGHLOW";
  case COFF::IMAGE_REL_BASED_HIGHADJ: return "HIGHADJ";
  case COFF::IMAGE_REL_BASED_ARM_MOV32T: return "ARM_MOV32(T)";
  case COFF::IMAGE_REL_BASED_DIR64: return "DIR64";
  default: return "unknown (" + llvm::utostr(Type) + ")";
  }
}

void COFFDumper::printCOFFBaseReloc() {
  ListScope D(W, "BaseReloc");
  for (const BaseRelocRef &I : Obj->base_relocs()) {
    uint8_t Type;
    uint32_t RVA;
    error(I.getRVA(RVA));
    error(I.getType(Type));
    DictScope Import(W, "Entry");
    W.printString("Type", getBaseRelocTypeName(Type));
    W.printHex("Address", RVA);
  }
}

void COFFDumper::printCOFFResources() {
  ListScope ResourcesD(W, "Resources");
  for (const SectionRef &S : Obj->sections()) {
    StringRef Name;
    error(S.getName(Name));
    if (!Name.startswith(".rsrc"))
      continue;

    StringRef Ref;
    error(S.getContents(Ref));

    if ((Name == ".rsrc") || (Name == ".rsrc$01")) {
      ResourceSectionRef RSF(Ref);
      auto &BaseTable = unwrapOrError(RSF.getBaseTable());
      W.printNumber("Total Number of Resources",
                    countTotalTableEntries(RSF, BaseTable, "Type"));
      W.printHex("Base Table Address",
                 Obj->getCOFFSection(S)->PointerToRawData);
      W.startLine() << "\n";
      printResourceDirectoryTable(RSF, BaseTable, "Type");
    }
    if (opts::SectionData)
      W.printBinaryBlock(Name.str() + " Data", Ref);
  }
}

uint32_t
COFFDumper::countTotalTableEntries(ResourceSectionRef RSF,
                                   const coff_resource_dir_table &Table,
                                   StringRef Level) {
  uint32_t TotalEntries = 0;
  for (int i = 0; i < Table.NumberOfNameEntries + Table.NumberOfIDEntries;
       i++) {
    auto Entry = unwrapOrError(getResourceDirectoryTableEntry(Table, i));
    if (Entry.Offset.isSubDir()) {
      StringRef NextLevel;
      if (Level == "Name")
        NextLevel = "Language";
      else
        NextLevel = "Name";
      auto &NextTable = unwrapOrError(RSF.getEntrySubDir(Entry));
      TotalEntries += countTotalTableEntries(RSF, NextTable, NextLevel);
    } else {
      TotalEntries += 1;
    }
  }
  return TotalEntries;
}

void COFFDumper::printResourceDirectoryTable(
    ResourceSectionRef RSF, const coff_resource_dir_table &Table,
    StringRef Level) {

  W.printNumber("Number of String Entries", Table.NumberOfNameEntries);
  W.printNumber("Number of ID Entries", Table.NumberOfIDEntries);

  // Iterate through level in resource directory tree.
  for (int i = 0; i < Table.NumberOfNameEntries + Table.NumberOfIDEntries;
       i++) {
    auto Entry = unwrapOrError(getResourceDirectoryTableEntry(Table, i));
    StringRef Name;
    SmallString<20> IDStr;
    raw_svector_ostream OS(IDStr);
    if (i < Table.NumberOfNameEntries) {
      ArrayRef<UTF16> RawEntryNameString = unwrapOrError(RSF.getEntryNameString(Entry));
      std::vector<UTF16> EndianCorrectedNameString;
      if (llvm::sys::IsBigEndianHost) {
        EndianCorrectedNameString.resize(RawEntryNameString.size() + 1);
        std::copy(RawEntryNameString.begin(), RawEntryNameString.end(),
                  EndianCorrectedNameString.begin() + 1);
        EndianCorrectedNameString[0] = UNI_UTF16_BYTE_ORDER_MARK_SWAPPED;
        RawEntryNameString = makeArrayRef(EndianCorrectedNameString);
      }
      std::string EntryNameString;
      if (!llvm::convertUTF16ToUTF8String(RawEntryNameString, EntryNameString))
        error(object_error::parse_failed);
      OS << ": ";
      OS << EntryNameString;
    } else {
      if (Level == "Type") {
        ScopedPrinter Printer(OS);
        Printer.printEnum("", Entry.Identifier.ID,
                          makeArrayRef(ResourceTypeNames));
        IDStr = IDStr.slice(0, IDStr.find_first_of(")", 0) + 1);
      } else {
        OS << ": (ID " << Entry.Identifier.ID << ")";
      }
    }
    Name = StringRef(IDStr);
    ListScope ResourceType(W, Level.str() + Name.str());
    if (Entry.Offset.isSubDir()) {
      W.printHex("Table Offset", Entry.Offset.value());
      StringRef NextLevel;
      if (Level == "Name")
        NextLevel = "Language";
      else
        NextLevel = "Name";
      auto &NextTable = unwrapOrError(RSF.getEntrySubDir(Entry));
      printResourceDirectoryTable(RSF, NextTable, NextLevel);
    } else {
      W.printHex("Entry Offset", Entry.Offset.value());
      char FormattedTime[20] = {};
      time_t TDS = time_t(Table.TimeDateStamp);
      strftime(FormattedTime, 20, "%Y-%m-%d %H:%M:%S", gmtime(&TDS));
      W.printHex("Time/Date Stamp", FormattedTime, Table.TimeDateStamp);
      W.printNumber("Major Version", Table.MajorVersion);
      W.printNumber("Minor Version", Table.MinorVersion);
      W.printNumber("Characteristics", Table.Characteristics);
    }
  }
}

ErrorOr<const coff_resource_dir_entry &>
COFFDumper::getResourceDirectoryTableEntry(const coff_resource_dir_table &Table,
                                           uint32_t Index) {
  if (Index >= (uint32_t)(Table.NumberOfNameEntries + Table.NumberOfIDEntries))
    return object_error::parse_failed;
  auto TablePtr = reinterpret_cast<const coff_resource_dir_entry *>(&Table + 1);
  return TablePtr[Index];
}

void COFFDumper::printStackMap() const {
  object::SectionRef StackMapSection;
  for (auto Sec : Obj->sections()) {
    StringRef Name;
    Sec.getName(Name);
    if (Name == ".llvm_stackmaps") {
      StackMapSection = Sec;
      break;
    }
  }

  if (StackMapSection == object::SectionRef())
    return;

  StringRef StackMapContents;
  StackMapSection.getContents(StackMapContents);
  ArrayRef<uint8_t> StackMapContentsArray(
      reinterpret_cast<const uint8_t*>(StackMapContents.data()),
      StackMapContents.size());

  if (Obj->isLittleEndian())
    prettyPrintStackMap(
        W, StackMapV2Parser<support::little>(StackMapContentsArray));
  else
    prettyPrintStackMap(W,
                        StackMapV2Parser<support::big>(StackMapContentsArray));
}

void COFFDumper::printAddrsig() {
  object::SectionRef AddrsigSection;
  for (auto Sec : Obj->sections()) {
    StringRef Name;
    Sec.getName(Name);
    if (Name == ".llvm_addrsig") {
      AddrsigSection = Sec;
      break;
    }
  }

  if (AddrsigSection == object::SectionRef())
    return;

  StringRef AddrsigContents;
  AddrsigSection.getContents(AddrsigContents);
  ArrayRef<uint8_t> AddrsigContentsArray(
      reinterpret_cast<const uint8_t*>(AddrsigContents.data()),
      AddrsigContents.size());

  ListScope L(W, "Addrsig");
  auto *Cur = reinterpret_cast<const uint8_t *>(AddrsigContents.begin());
  auto *End = reinterpret_cast<const uint8_t *>(AddrsigContents.end());
  while (Cur != End) {
    unsigned Size;
    const char *Err;
    uint64_t SymIndex = decodeULEB128(Cur, &Size, End, &Err);
    if (Err)
      reportError(Err);

    Expected<COFFSymbolRef> Sym = Obj->getSymbol(SymIndex);
    StringRef SymName;
    std::error_code EC = errorToErrorCode(Sym.takeError());
    if (EC || (EC = Obj->getSymbolName(*Sym, SymName))) {
      SymName = "";
      error(EC);
    }

    W.printNumber("Sym", SymName, SymIndex);
    Cur += Size;
  }
}

void llvm::dumpCodeViewMergedTypes(
    ScopedPrinter &Writer, llvm::codeview::MergingTypeTableBuilder &IDTable,
    llvm::codeview::MergingTypeTableBuilder &CVTypes) {
  // Flatten it first, then run our dumper on it.
  SmallString<0> TypeBuf;
  CVTypes.ForEachRecord([&](TypeIndex TI, const CVType &Record) {
    TypeBuf.append(Record.RecordData.begin(), Record.RecordData.end());
  });

  TypeTableCollection TpiTypes(CVTypes.records());
  {
    ListScope S(Writer, "MergedTypeStream");
    TypeDumpVisitor TDV(TpiTypes, &Writer, opts::CodeViewSubsectionBytes);
    error(codeview::visitTypeStream(TpiTypes, TDV));
    Writer.flush();
  }

  // Flatten the id stream and print it next. The ID stream refers to names from
  // the type stream.
  TypeTableCollection IpiTypes(IDTable.records());
  {
    ListScope S(Writer, "MergedIDStream");
    TypeDumpVisitor TDV(TpiTypes, &Writer, opts::CodeViewSubsectionBytes);
    TDV.setIpiTypes(IpiTypes);
    error(codeview::visitTypeStream(IpiTypes, TDV));
    Writer.flush();
  }
}
