//===-- llvm/BinaryFormat/COFF.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains an definitions used in Windows COFF Files.
//
// Structures and enums defined within this file where created using
// information from Microsoft's publicly available PE/COFF format document:
//
// Microsoft Portable Executable and Common Object File Format Specification
// Revision 8.1 - February 15, 2008
//
// As of 5/2/2010, hosted by Microsoft at:
// http://www.microsoft.com/whdc/system/platform/firmware/pecoff.mspx
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_COFF_H
#define LLVM_BINARYFORMAT_COFF_H

#include "llvm/Support/DataTypes.h"
#include <cassert>
#include <cstring>

namespace llvm {
namespace COFF {

// The maximum number of sections that a COFF object can have (inclusive).
const int32_t MaxNumberOfSections16 = 65279;

// The PE signature bytes that follows the DOS stub header.
static const char PEMagic[] = {'P', 'E', '\0', '\0'};

static const char BigObjMagic[] = {
    '\xc7', '\xa1', '\xba', '\xd1', '\xee', '\xba', '\xa9', '\x4b',
    '\xaf', '\x20', '\xfa', '\xf6', '\x6a', '\xa4', '\xdc', '\xb8',
};

static const char ClGlObjMagic[] = {
    '\x38', '\xfe', '\xb3', '\x0c', '\xa5', '\xd9', '\xab', '\x4d',
    '\xac', '\x9b', '\xd6', '\xb6', '\x22', '\x26', '\x53', '\xc2',
};

// The signature bytes that start a .res file.
static const char WinResMagic[] = {
    '\x00', '\x00', '\x00', '\x00', '\x20', '\x00', '\x00', '\x00',
    '\xff', '\xff', '\x00', '\x00', '\xff', '\xff', '\x00', '\x00',
};

// Sizes in bytes of various things in the COFF format.
enum {
  Header16Size = 20,
  Header32Size = 56,
  NameSize = 8,
  Symbol16Size = 18,
  Symbol32Size = 20,
  SectionSize = 40,
  RelocationSize = 10
};

struct header {
  uint16_t Machine;
  int32_t NumberOfSections;
  uint32_t TimeDateStamp;
  uint32_t PointerToSymbolTable;
  uint32_t NumberOfSymbols;
  uint16_t SizeOfOptionalHeader;
  uint16_t Characteristics;
};

struct BigObjHeader {
  enum : uint16_t { MinBigObjectVersion = 2 };

  uint16_t Sig1; ///< Must be IMAGE_FILE_MACHINE_UNKNOWN (0).
  uint16_t Sig2; ///< Must be 0xFFFF.
  uint16_t Version;
  uint16_t Machine;
  uint32_t TimeDateStamp;
  uint8_t UUID[16];
  uint32_t unused1;
  uint32_t unused2;
  uint32_t unused3;
  uint32_t unused4;
  uint32_t NumberOfSections;
  uint32_t PointerToSymbolTable;
  uint32_t NumberOfSymbols;
};

enum MachineTypes : unsigned {
  MT_Invalid = 0xffff,

  IMAGE_FILE_MACHINE_UNKNOWN = 0x0,
  IMAGE_FILE_MACHINE_AM33 = 0x1D3,
  IMAGE_FILE_MACHINE_AMD64 = 0x8664,
  IMAGE_FILE_MACHINE_ARM = 0x1C0,
  IMAGE_FILE_MACHINE_ARMNT = 0x1C4,
  IMAGE_FILE_MACHINE_ARM64 = 0xAA64,
  IMAGE_FILE_MACHINE_EBC = 0xEBC,
  IMAGE_FILE_MACHINE_I386 = 0x14C,
  IMAGE_FILE_MACHINE_IA64 = 0x200,
  IMAGE_FILE_MACHINE_M32R = 0x9041,
  IMAGE_FILE_MACHINE_MIPS16 = 0x266,
  IMAGE_FILE_MACHINE_MIPSFPU = 0x366,
  IMAGE_FILE_MACHINE_MIPSFPU16 = 0x466,
  IMAGE_FILE_MACHINE_POWERPC = 0x1F0,
  IMAGE_FILE_MACHINE_POWERPCFP = 0x1F1,
  IMAGE_FILE_MACHINE_R4000 = 0x166,
  IMAGE_FILE_MACHINE_RISCV32 = 0x5032,
  IMAGE_FILE_MACHINE_RISCV64 = 0x5064,
  IMAGE_FILE_MACHINE_RISCV128 = 0x5128,
  IMAGE_FILE_MACHINE_SH3 = 0x1A2,
  IMAGE_FILE_MACHINE_SH3DSP = 0x1A3,
  IMAGE_FILE_MACHINE_SH4 = 0x1A6,
  IMAGE_FILE_MACHINE_SH5 = 0x1A8,
  IMAGE_FILE_MACHINE_THUMB = 0x1C2,
  IMAGE_FILE_MACHINE_WCEMIPSV2 = 0x169
};

enum Characteristics : unsigned {
  C_Invalid = 0,

  /// The file does not contain base relocations and must be loaded at its
  /// preferred base. If this cannot be done, the loader will error.
  IMAGE_FILE_RELOCS_STRIPPED = 0x0001,
  /// The file is valid and can be run.
  IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002,
  /// COFF line numbers have been stripped. This is deprecated and should be
  /// 0.
  IMAGE_FILE_LINE_NUMS_STRIPPED = 0x0004,
  /// COFF symbol table entries for local symbols have been removed. This is
  /// deprecated and should be 0.
  IMAGE_FILE_LOCAL_SYMS_STRIPPED = 0x0008,
  /// Aggressively trim working set. This is deprecated and must be 0.
  IMAGE_FILE_AGGRESSIVE_WS_TRIM = 0x0010,
  /// Image can handle > 2GiB addresses.
  IMAGE_FILE_LARGE_ADDRESS_AWARE = 0x0020,
  /// Little endian: the LSB precedes the MSB in memory. This is deprecated
  /// and should be 0.
  IMAGE_FILE_BYTES_REVERSED_LO = 0x0080,
  /// Machine is based on a 32bit word architecture.
  IMAGE_FILE_32BIT_MACHINE = 0x0100,
  /// Debugging info has been removed.
  IMAGE_FILE_DEBUG_STRIPPED = 0x0200,
  /// If the image is on removable media, fully load it and copy it to swap.
  IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP = 0x0400,
  /// If the image is on network media, fully load it and copy it to swap.
  IMAGE_FILE_NET_RUN_FROM_SWAP = 0x0800,
  /// The image file is a system file, not a user program.
  IMAGE_FILE_SYSTEM = 0x1000,
  /// The image file is a DLL.
  IMAGE_FILE_DLL = 0x2000,
  /// This file should only be run on a uniprocessor machine.
  IMAGE_FILE_UP_SYSTEM_ONLY = 0x4000,
  /// Big endian: the MSB precedes the LSB in memory. This is deprecated
  /// and should be 0.
  IMAGE_FILE_BYTES_REVERSED_HI = 0x8000
};

enum ResourceTypeID : unsigned {
  RID_Cursor = 1,
  RID_Bitmap = 2,
  RID_Icon = 3,
  RID_Menu = 4,
  RID_Dialog = 5,
  RID_String = 6,
  RID_FontDir = 7,
  RID_Font = 8,
  RID_Accelerator = 9,
  RID_RCData = 10,
  RID_MessageTable = 11,
  RID_Group_Cursor = 12,
  RID_Group_Icon = 14,
  RID_Version = 16,
  RID_DLGInclude = 17,
  RID_PlugPlay = 19,
  RID_VXD = 20,
  RID_AniCursor = 21,
  RID_AniIcon = 22,
  RID_HTML = 23,
  RID_Manifest = 24,
};

struct symbol {
  char Name[NameSize];
  uint32_t Value;
  int32_t SectionNumber;
  uint16_t Type;
  uint8_t StorageClass;
  uint8_t NumberOfAuxSymbols;
};

enum SymbolSectionNumber : int32_t {
  IMAGE_SYM_DEBUG = -2,
  IMAGE_SYM_ABSOLUTE = -1,
  IMAGE_SYM_UNDEFINED = 0
};

/// Storage class tells where and what the symbol represents
enum SymbolStorageClass {
  SSC_Invalid = 0xff,

  IMAGE_SYM_CLASS_END_OF_FUNCTION = -1,  ///< Physical end of function
  IMAGE_SYM_CLASS_NULL = 0,              ///< No symbol
  IMAGE_SYM_CLASS_AUTOMATIC = 1,         ///< Stack variable
  IMAGE_SYM_CLASS_EXTERNAL = 2,          ///< External symbol
  IMAGE_SYM_CLASS_STATIC = 3,            ///< Static
  IMAGE_SYM_CLASS_REGISTER = 4,          ///< Register variable
  IMAGE_SYM_CLASS_EXTERNAL_DEF = 5,      ///< External definition
  IMAGE_SYM_CLASS_LABEL = 6,             ///< Label
  IMAGE_SYM_CLASS_UNDEFINED_LABEL = 7,   ///< Undefined label
  IMAGE_SYM_CLASS_MEMBER_OF_STRUCT = 8,  ///< Member of structure
  IMAGE_SYM_CLASS_ARGUMENT = 9,          ///< Function argument
  IMAGE_SYM_CLASS_STRUCT_TAG = 10,       ///< Structure tag
  IMAGE_SYM_CLASS_MEMBER_OF_UNION = 11,  ///< Member of union
  IMAGE_SYM_CLASS_UNION_TAG = 12,        ///< Union tag
  IMAGE_SYM_CLASS_TYPE_DEFINITION = 13,  ///< Type definition
  IMAGE_SYM_CLASS_UNDEFINED_STATIC = 14, ///< Undefined static
  IMAGE_SYM_CLASS_ENUM_TAG = 15,         ///< Enumeration tag
  IMAGE_SYM_CLASS_MEMBER_OF_ENUM = 16,   ///< Member of enumeration
  IMAGE_SYM_CLASS_REGISTER_PARAM = 17,   ///< Register parameter
  IMAGE_SYM_CLASS_BIT_FIELD = 18,        ///< Bit field
  /// ".bb" or ".eb" - beginning or end of block
  IMAGE_SYM_CLASS_BLOCK = 100,
  /// ".bf" or ".ef" - beginning or end of function
  IMAGE_SYM_CLASS_FUNCTION = 101,
  IMAGE_SYM_CLASS_END_OF_STRUCT = 102, ///< End of structure
  IMAGE_SYM_CLASS_FILE = 103,          ///< File name
  /// Line number, reformatted as symbol
  IMAGE_SYM_CLASS_SECTION = 104,
  IMAGE_SYM_CLASS_WEAK_EXTERNAL = 105, ///< Duplicate tag
  /// External symbol in dmert public lib
  IMAGE_SYM_CLASS_CLR_TOKEN = 107
};

enum SymbolBaseType : unsigned {
  IMAGE_SYM_TYPE_NULL = 0,   ///< No type information or unknown base type.
  IMAGE_SYM_TYPE_VOID = 1,   ///< Used with void pointers and functions.
  IMAGE_SYM_TYPE_CHAR = 2,   ///< A character (signed byte).
  IMAGE_SYM_TYPE_SHORT = 3,  ///< A 2-byte signed integer.
  IMAGE_SYM_TYPE_INT = 4,    ///< A natural integer type on the target.
  IMAGE_SYM_TYPE_LONG = 5,   ///< A 4-byte signed integer.
  IMAGE_SYM_TYPE_FLOAT = 6,  ///< A 4-byte floating-point number.
  IMAGE_SYM_TYPE_DOUBLE = 7, ///< An 8-byte floating-point number.
  IMAGE_SYM_TYPE_STRUCT = 8, ///< A structure.
  IMAGE_SYM_TYPE_UNION = 9,  ///< An union.
  IMAGE_SYM_TYPE_ENUM = 10,  ///< An enumerated type.
  IMAGE_SYM_TYPE_MOE = 11,   ///< A member of enumeration (a specific value).
  IMAGE_SYM_TYPE_BYTE = 12,  ///< A byte; unsigned 1-byte integer.
  IMAGE_SYM_TYPE_WORD = 13,  ///< A word; unsigned 2-byte integer.
  IMAGE_SYM_TYPE_UINT = 14,  ///< An unsigned integer of natural size.
  IMAGE_SYM_TYPE_DWORD = 15  ///< An unsigned 4-byte integer.
};

enum SymbolComplexType : unsigned {
  IMAGE_SYM_DTYPE_NULL = 0,     ///< No complex type; simple scalar variable.
  IMAGE_SYM_DTYPE_POINTER = 1,  ///< A pointer to base type.
  IMAGE_SYM_DTYPE_FUNCTION = 2, ///< A function that returns a base type.
  IMAGE_SYM_DTYPE_ARRAY = 3,    ///< An array of base type.

  /// Type is formed as (base + (derived << SCT_COMPLEX_TYPE_SHIFT))
  SCT_COMPLEX_TYPE_SHIFT = 4
};

enum AuxSymbolType { IMAGE_AUX_SYMBOL_TYPE_TOKEN_DEF = 1 };

struct section {
  char Name[NameSize];
  uint32_t VirtualSize;
  uint32_t VirtualAddress;
  uint32_t SizeOfRawData;
  uint32_t PointerToRawData;
  uint32_t PointerToRelocations;
  uint32_t PointerToLineNumbers;
  uint16_t NumberOfRelocations;
  uint16_t NumberOfLineNumbers;
  uint32_t Characteristics;
};

enum SectionCharacteristics : uint32_t {
  SC_Invalid = 0xffffffff,

  IMAGE_SCN_TYPE_NOLOAD = 0x00000002,
  IMAGE_SCN_TYPE_NO_PAD = 0x00000008,
  IMAGE_SCN_CNT_CODE = 0x00000020,
  IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040,
  IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080,
  IMAGE_SCN_LNK_OTHER = 0x00000100,
  IMAGE_SCN_LNK_INFO = 0x00000200,
  IMAGE_SCN_LNK_REMOVE = 0x00000800,
  IMAGE_SCN_LNK_COMDAT = 0x00001000,
  IMAGE_SCN_GPREL = 0x00008000,
  IMAGE_SCN_MEM_PURGEABLE = 0x00020000,
  IMAGE_SCN_MEM_16BIT = 0x00020000,
  IMAGE_SCN_MEM_LOCKED = 0x00040000,
  IMAGE_SCN_MEM_PRELOAD = 0x00080000,
  IMAGE_SCN_ALIGN_1BYTES = 0x00100000,
  IMAGE_SCN_ALIGN_2BYTES = 0x00200000,
  IMAGE_SCN_ALIGN_4BYTES = 0x00300000,
  IMAGE_SCN_ALIGN_8BYTES = 0x00400000,
  IMAGE_SCN_ALIGN_16BYTES = 0x00500000,
  IMAGE_SCN_ALIGN_32BYTES = 0x00600000,
  IMAGE_SCN_ALIGN_64BYTES = 0x00700000,
  IMAGE_SCN_ALIGN_128BYTES = 0x00800000,
  IMAGE_SCN_ALIGN_256BYTES = 0x00900000,
  IMAGE_SCN_ALIGN_512BYTES = 0x00A00000,
  IMAGE_SCN_ALIGN_1024BYTES = 0x00B00000,
  IMAGE_SCN_ALIGN_2048BYTES = 0x00C00000,
  IMAGE_SCN_ALIGN_4096BYTES = 0x00D00000,
  IMAGE_SCN_ALIGN_8192BYTES = 0x00E00000,
  IMAGE_SCN_LNK_NRELOC_OVFL = 0x01000000,
  IMAGE_SCN_MEM_DISCARDABLE = 0x02000000,
  IMAGE_SCN_MEM_NOT_CACHED = 0x04000000,
  IMAGE_SCN_MEM_NOT_PAGED = 0x08000000,
  IMAGE_SCN_MEM_SHARED = 0x10000000,
  IMAGE_SCN_MEM_EXECUTE = 0x20000000,
  IMAGE_SCN_MEM_READ = 0x40000000,
  IMAGE_SCN_MEM_WRITE = 0x80000000
};

struct relocation {
  uint32_t VirtualAddress;
  uint32_t SymbolTableIndex;
  uint16_t Type;
};

enum RelocationTypeI386 : unsigned {
  IMAGE_REL_I386_ABSOLUTE = 0x0000,
  IMAGE_REL_I386_DIR16 = 0x0001,
  IMAGE_REL_I386_REL16 = 0x0002,
  IMAGE_REL_I386_DIR32 = 0x0006,
  IMAGE_REL_I386_DIR32NB = 0x0007,
  IMAGE_REL_I386_SEG12 = 0x0009,
  IMAGE_REL_I386_SECTION = 0x000A,
  IMAGE_REL_I386_SECREL = 0x000B,
  IMAGE_REL_I386_TOKEN = 0x000C,
  IMAGE_REL_I386_SECREL7 = 0x000D,
  IMAGE_REL_I386_REL32 = 0x0014
};

enum RelocationTypeAMD64 : unsigned {
  IMAGE_REL_AMD64_ABSOLUTE = 0x0000,
  IMAGE_REL_AMD64_ADDR64 = 0x0001,
  IMAGE_REL_AMD64_ADDR32 = 0x0002,
  IMAGE_REL_AMD64_ADDR32NB = 0x0003,
  IMAGE_REL_AMD64_REL32 = 0x0004,
  IMAGE_REL_AMD64_REL32_1 = 0x0005,
  IMAGE_REL_AMD64_REL32_2 = 0x0006,
  IMAGE_REL_AMD64_REL32_3 = 0x0007,
  IMAGE_REL_AMD64_REL32_4 = 0x0008,
  IMAGE_REL_AMD64_REL32_5 = 0x0009,
  IMAGE_REL_AMD64_SECTION = 0x000A,
  IMAGE_REL_AMD64_SECREL = 0x000B,
  IMAGE_REL_AMD64_SECREL7 = 0x000C,
  IMAGE_REL_AMD64_TOKEN = 0x000D,
  IMAGE_REL_AMD64_SREL32 = 0x000E,
  IMAGE_REL_AMD64_PAIR = 0x000F,
  IMAGE_REL_AMD64_SSPAN32 = 0x0010
};

enum RelocationTypesARM : unsigned {
  IMAGE_REL_ARM_ABSOLUTE = 0x0000,
  IMAGE_REL_ARM_ADDR32 = 0x0001,
  IMAGE_REL_ARM_ADDR32NB = 0x0002,
  IMAGE_REL_ARM_BRANCH24 = 0x0003,
  IMAGE_REL_ARM_BRANCH11 = 0x0004,
  IMAGE_REL_ARM_TOKEN = 0x0005,
  IMAGE_REL_ARM_BLX24 = 0x0008,
  IMAGE_REL_ARM_BLX11 = 0x0009,
  IMAGE_REL_ARM_SECTION = 0x000E,
  IMAGE_REL_ARM_SECREL = 0x000F,
  IMAGE_REL_ARM_MOV32A = 0x0010,
  IMAGE_REL_ARM_MOV32T = 0x0011,
  IMAGE_REL_ARM_BRANCH20T = 0x0012,
  IMAGE_REL_ARM_BRANCH24T = 0x0014,
  IMAGE_REL_ARM_BLX23T = 0x0015
};

enum RelocationTypesARM64 : unsigned {
  IMAGE_REL_ARM64_ABSOLUTE = 0x0000,
  IMAGE_REL_ARM64_ADDR32 = 0x0001,
  IMAGE_REL_ARM64_ADDR32NB = 0x0002,
  IMAGE_REL_ARM64_BRANCH26 = 0x0003,
  IMAGE_REL_ARM64_PAGEBASE_REL21 = 0x0004,
  IMAGE_REL_ARM64_REL21 = 0x0005,
  IMAGE_REL_ARM64_PAGEOFFSET_12A = 0x0006,
  IMAGE_REL_ARM64_PAGEOFFSET_12L = 0x0007,
  IMAGE_REL_ARM64_SECREL = 0x0008,
  IMAGE_REL_ARM64_SECREL_LOW12A = 0x0009,
  IMAGE_REL_ARM64_SECREL_HIGH12A = 0x000A,
  IMAGE_REL_ARM64_SECREL_LOW12L = 0x000B,
  IMAGE_REL_ARM64_TOKEN = 0x000C,
  IMAGE_REL_ARM64_SECTION = 0x000D,
  IMAGE_REL_ARM64_ADDR64 = 0x000E,
  IMAGE_REL_ARM64_BRANCH19 = 0x000F,
  IMAGE_REL_ARM64_BRANCH14 = 0x0010,
};

enum COMDATType : unsigned {
  IMAGE_COMDAT_SELECT_NODUPLICATES = 1,
  IMAGE_COMDAT_SELECT_ANY,
  IMAGE_COMDAT_SELECT_SAME_SIZE,
  IMAGE_COMDAT_SELECT_EXACT_MATCH,
  IMAGE_COMDAT_SELECT_ASSOCIATIVE,
  IMAGE_COMDAT_SELECT_LARGEST,
  IMAGE_COMDAT_SELECT_NEWEST
};

// Auxiliary Symbol Formats
struct AuxiliaryFunctionDefinition {
  uint32_t TagIndex;
  uint32_t TotalSize;
  uint32_t PointerToLinenumber;
  uint32_t PointerToNextFunction;
  char unused[2];
};

struct AuxiliarybfAndefSymbol {
  uint8_t unused1[4];
  uint16_t Linenumber;
  uint8_t unused2[6];
  uint32_t PointerToNextFunction;
  uint8_t unused3[2];
};

struct AuxiliaryWeakExternal {
  uint32_t TagIndex;
  uint32_t Characteristics;
  uint8_t unused[10];
};

enum WeakExternalCharacteristics : unsigned {
  IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY = 1,
  IMAGE_WEAK_EXTERN_SEARCH_LIBRARY = 2,
  IMAGE_WEAK_EXTERN_SEARCH_ALIAS = 3
};

struct AuxiliarySectionDefinition {
  uint32_t Length;
  uint16_t NumberOfRelocations;
  uint16_t NumberOfLinenumbers;
  uint32_t CheckSum;
  uint32_t Number;
  uint8_t Selection;
  char unused;
};

struct AuxiliaryCLRToken {
  uint8_t AuxType;
  uint8_t unused1;
  uint32_t SymbolTableIndex;
  char unused2[12];
};

union Auxiliary {
  AuxiliaryFunctionDefinition FunctionDefinition;
  AuxiliarybfAndefSymbol bfAndefSymbol;
  AuxiliaryWeakExternal WeakExternal;
  AuxiliarySectionDefinition SectionDefinition;
};

/// The Import Directory Table.
///
/// There is a single array of these and one entry per imported DLL.
struct ImportDirectoryTableEntry {
  uint32_t ImportLookupTableRVA;
  uint32_t TimeDateStamp;
  uint32_t ForwarderChain;
  uint32_t NameRVA;
  uint32_t ImportAddressTableRVA;
};

/// The PE32 Import Lookup Table.
///
/// There is an array of these for each imported DLL. It represents either
/// the ordinal to import from the target DLL, or a name to lookup and import
/// from the target DLL.
///
/// This also happens to be the same format used by the Import Address Table
/// when it is initially written out to the image.
struct ImportLookupTableEntry32 {
  uint32_t data;

  /// Is this entry specified by ordinal, or name?
  bool isOrdinal() const { return data & 0x80000000; }

  /// Get the ordinal value of this entry. isOrdinal must be true.
  uint16_t getOrdinal() const {
    assert(isOrdinal() && "ILT entry is not an ordinal!");
    return data & 0xFFFF;
  }

  /// Set the ordinal value and set isOrdinal to true.
  void setOrdinal(uint16_t o) {
    data = o;
    data |= 0x80000000;
  }

  /// Get the Hint/Name entry RVA. isOrdinal must be false.
  uint32_t getHintNameRVA() const {
    assert(!isOrdinal() && "ILT entry is not a Hint/Name RVA!");
    return data;
  }

  /// Set the Hint/Name entry RVA and set isOrdinal to false.
  void setHintNameRVA(uint32_t rva) { data = rva; }
};

/// The DOS compatible header at the front of all PEs.
struct DOSHeader {
  uint16_t Magic;
  uint16_t UsedBytesInTheLastPage;
  uint16_t FileSizeInPages;
  uint16_t NumberOfRelocationItems;
  uint16_t HeaderSizeInParagraphs;
  uint16_t MinimumExtraParagraphs;
  uint16_t MaximumExtraParagraphs;
  uint16_t InitialRelativeSS;
  uint16_t InitialSP;
  uint16_t Checksum;
  uint16_t InitialIP;
  uint16_t InitialRelativeCS;
  uint16_t AddressOfRelocationTable;
  uint16_t OverlayNumber;
  uint16_t Reserved[4];
  uint16_t OEMid;
  uint16_t OEMinfo;
  uint16_t Reserved2[10];
  uint32_t AddressOfNewExeHeader;
};

struct PE32Header {
  enum { PE32 = 0x10b, PE32_PLUS = 0x20b };

  uint16_t Magic;
  uint8_t MajorLinkerVersion;
  uint8_t MinorLinkerVersion;
  uint32_t SizeOfCode;
  uint32_t SizeOfInitializedData;
  uint32_t SizeOfUninitializedData;
  uint32_t AddressOfEntryPoint; // RVA
  uint32_t BaseOfCode;          // RVA
  uint32_t BaseOfData;          // RVA
  uint32_t ImageBase;
  uint32_t SectionAlignment;
  uint32_t FileAlignment;
  uint16_t MajorOperatingSystemVersion;
  uint16_t MinorOperatingSystemVersion;
  uint16_t MajorImageVersion;
  uint16_t MinorImageVersion;
  uint16_t MajorSubsystemVersion;
  uint16_t MinorSubsystemVersion;
  uint32_t Win32VersionValue;
  uint32_t SizeOfImage;
  uint32_t SizeOfHeaders;
  uint32_t CheckSum;
  uint16_t Subsystem;
  // FIXME: This should be DllCharacteristics to match the COFF spec.
  uint16_t DLLCharacteristics;
  uint32_t SizeOfStackReserve;
  uint32_t SizeOfStackCommit;
  uint32_t SizeOfHeapReserve;
  uint32_t SizeOfHeapCommit;
  uint32_t LoaderFlags;
  // FIXME: This should be NumberOfRvaAndSizes to match the COFF spec.
  uint32_t NumberOfRvaAndSize;
};

struct DataDirectory {
  uint32_t RelativeVirtualAddress;
  uint32_t Size;
};

enum DataDirectoryIndex : unsigned {
  EXPORT_TABLE = 0,
  IMPORT_TABLE,
  RESOURCE_TABLE,
  EXCEPTION_TABLE,
  CERTIFICATE_TABLE,
  BASE_RELOCATION_TABLE,
  DEBUG_DIRECTORY,
  ARCHITECTURE,
  GLOBAL_PTR,
  TLS_TABLE,
  LOAD_CONFIG_TABLE,
  BOUND_IMPORT,
  IAT,
  DELAY_IMPORT_DESCRIPTOR,
  CLR_RUNTIME_HEADER,

  NUM_DATA_DIRECTORIES
};

enum WindowsSubsystem : unsigned {
  IMAGE_SUBSYSTEM_UNKNOWN = 0, ///< An unknown subsystem.
  IMAGE_SUBSYSTEM_NATIVE = 1,  ///< Device drivers and native Windows processes
  IMAGE_SUBSYSTEM_WINDOWS_GUI = 2,      ///< The Windows GUI subsystem.
  IMAGE_SUBSYSTEM_WINDOWS_CUI = 3,      ///< The Windows character subsystem.
  IMAGE_SUBSYSTEM_OS2_CUI = 5,          ///< The OS/2 character subsytem.
  IMAGE_SUBSYSTEM_POSIX_CUI = 7,        ///< The POSIX character subsystem.
  IMAGE_SUBSYSTEM_NATIVE_WINDOWS = 8,   ///< Native Windows 9x driver.
  IMAGE_SUBSYSTEM_WINDOWS_CE_GUI = 9,   ///< Windows CE.
  IMAGE_SUBSYSTEM_EFI_APPLICATION = 10, ///< An EFI application.
  IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER = 11, ///< An EFI driver with boot
                                                ///  services.
  IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER = 12,      ///< An EFI driver with run-time
                                                ///  services.
  IMAGE_SUBSYSTEM_EFI_ROM = 13,                 ///< An EFI ROM image.
  IMAGE_SUBSYSTEM_XBOX = 14,                    ///< XBOX.
  IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION = 16 ///< A BCD application.
};

enum DLLCharacteristics : unsigned {
  /// ASLR with 64 bit address space.
  IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA = 0x0020,
  /// DLL can be relocated at load time.
  IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE = 0x0040,
  /// Code integrity checks are enforced.
  IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY = 0x0080,
  ///< Image is NX compatible.
  IMAGE_DLL_CHARACTERISTICS_NX_COMPAT = 0x0100,
  /// Isolation aware, but do not isolate the image.
  IMAGE_DLL_CHARACTERISTICS_NO_ISOLATION = 0x0200,
  /// Does not use structured exception handling (SEH). No SEH handler may be
  /// called in this image.
  IMAGE_DLL_CHARACTERISTICS_NO_SEH = 0x0400,
  /// Do not bind the image.
  IMAGE_DLL_CHARACTERISTICS_NO_BIND = 0x0800,
  ///< Image should execute in an AppContainer.
  IMAGE_DLL_CHARACTERISTICS_APPCONTAINER = 0x1000,
  ///< A WDM driver.
  IMAGE_DLL_CHARACTERISTICS_WDM_DRIVER = 0x2000,
  ///< Image supports Control Flow Guard.
  IMAGE_DLL_CHARACTERISTICS_GUARD_CF = 0x4000,
  /// Terminal Server aware.
  IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE = 0x8000
};

enum DebugType : unsigned {
  IMAGE_DEBUG_TYPE_UNKNOWN = 0,
  IMAGE_DEBUG_TYPE_COFF = 1,
  IMAGE_DEBUG_TYPE_CODEVIEW = 2,
  IMAGE_DEBUG_TYPE_FPO = 3,
  IMAGE_DEBUG_TYPE_MISC = 4,
  IMAGE_DEBUG_TYPE_EXCEPTION = 5,
  IMAGE_DEBUG_TYPE_FIXUP = 6,
  IMAGE_DEBUG_TYPE_OMAP_TO_SRC = 7,
  IMAGE_DEBUG_TYPE_OMAP_FROM_SRC = 8,
  IMAGE_DEBUG_TYPE_BORLAND = 9,
  IMAGE_DEBUG_TYPE_RESERVED10 = 10,
  IMAGE_DEBUG_TYPE_CLSID = 11,
  IMAGE_DEBUG_TYPE_VC_FEATURE = 12,
  IMAGE_DEBUG_TYPE_POGO = 13,
  IMAGE_DEBUG_TYPE_ILTCG = 14,
  IMAGE_DEBUG_TYPE_MPX = 15,
  IMAGE_DEBUG_TYPE_REPRO = 16,
};

enum BaseRelocationType : unsigned {
  IMAGE_REL_BASED_ABSOLUTE = 0,
  IMAGE_REL_BASED_HIGH = 1,
  IMAGE_REL_BASED_LOW = 2,
  IMAGE_REL_BASED_HIGHLOW = 3,
  IMAGE_REL_BASED_HIGHADJ = 4,
  IMAGE_REL_BASED_MIPS_JMPADDR = 5,
  IMAGE_REL_BASED_ARM_MOV32A = 5,
  IMAGE_REL_BASED_ARM_MOV32T = 7,
  IMAGE_REL_BASED_MIPS_JMPADDR16 = 9,
  IMAGE_REL_BASED_DIR64 = 10
};

enum ImportType : unsigned {
  IMPORT_CODE = 0,
  IMPORT_DATA = 1,
  IMPORT_CONST = 2
};

enum ImportNameType : unsigned {
  /// Import is by ordinal. This indicates that the value in the Ordinal/Hint
  /// field of the import header is the import's ordinal. If this constant is
  /// not specified, then the Ordinal/Hint field should always be interpreted
  /// as the import's hint.
  IMPORT_ORDINAL = 0,
  /// The import name is identical to the public symbol name
  IMPORT_NAME = 1,
  /// The import name is the public symbol name, but skipping the leading ?,
  /// @, or optionally _.
  IMPORT_NAME_NOPREFIX = 2,
  /// The import name is the public symbol name, but skipping the leading ?,
  /// @, or optionally _, and truncating at the first @.
  IMPORT_NAME_UNDECORATE = 3
};

struct ImportHeader {
  uint16_t Sig1; ///< Must be IMAGE_FILE_MACHINE_UNKNOWN (0).
  uint16_t Sig2; ///< Must be 0xFFFF.
  uint16_t Version;
  uint16_t Machine;
  uint32_t TimeDateStamp;
  uint32_t SizeOfData;
  uint16_t OrdinalHint;
  uint16_t TypeInfo;

  ImportType getType() const { return static_cast<ImportType>(TypeInfo & 0x3); }

  ImportNameType getNameType() const {
    return static_cast<ImportNameType>((TypeInfo & 0x1C) >> 2);
  }
};

enum CodeViewIdentifiers {
  DEBUG_SECTION_MAGIC = 0x4,
  DEBUG_HASHES_SECTION_MAGIC = 0x133C9C5
};

inline bool isReservedSectionNumber(int32_t SectionNumber) {
  return SectionNumber <= 0;
}

} // End namespace COFF.
} // End namespace llvm.

#endif
