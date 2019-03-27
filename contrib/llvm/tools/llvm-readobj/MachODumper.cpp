//===-- MachODump.cpp - Object file dumping utility for llvm --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the MachO-specific dumper for llvm-readobj.
//
//===----------------------------------------------------------------------===//

#include "Error.h"
#include "ObjDumper.h"
#include "StackMapPrinter.h"
#include "llvm-readobj.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ScopedPrinter.h"

using namespace llvm;
using namespace object;

namespace {

class MachODumper : public ObjDumper {
public:
  MachODumper(const MachOObjectFile *Obj, ScopedPrinter &Writer)
      : ObjDumper(Writer), Obj(Obj) {}

  void printFileHeaders() override;
  void printSectionHeaders() override;
  void printRelocations() override;
  void printSymbols() override;
  void printDynamicSymbols() override;
  void printUnwindInfo() override;
  void printStackMap() const override;

  void printNeededLibraries() override;

  // MachO-specific.
  void printMachODataInCode() override;
  void printMachOVersionMin() override;
  void printMachODysymtab() override;
  void printMachOSegment() override;
  void printMachOIndirectSymbols() override;
  void printMachOLinkerOptions () override;

private:
  template<class MachHeader>
  void printFileHeaders(const MachHeader &Header);

  void printSymbol(const SymbolRef &Symbol);

  void printRelocation(const RelocationRef &Reloc);

  void printRelocation(const MachOObjectFile *Obj, const RelocationRef &Reloc);

  void printSectionHeaders(const MachOObjectFile *Obj);

  const MachOObjectFile *Obj;
};

} // namespace


namespace llvm {

std::error_code createMachODumper(const object::ObjectFile *Obj,
                                  ScopedPrinter &Writer,
                                  std::unique_ptr<ObjDumper> &Result) {
  const MachOObjectFile *MachOObj = dyn_cast<MachOObjectFile>(Obj);
  if (!MachOObj)
    return readobj_error::unsupported_obj_file_format;

  Result.reset(new MachODumper(MachOObj, Writer));
  return readobj_error::success;
}

} // namespace llvm

static const EnumEntry<uint32_t> MachOMagics[] = {
  { "Magic",      MachO::MH_MAGIC    },
  { "Cigam",      MachO::MH_CIGAM    },
  { "Magic64",    MachO::MH_MAGIC_64 },
  { "Cigam64",    MachO::MH_CIGAM_64 },
  { "FatMagic",   MachO::FAT_MAGIC   },
  { "FatCigam",   MachO::FAT_CIGAM   },
};

static const EnumEntry<uint32_t> MachOHeaderFileTypes[] = {
  { "Relocatable",          MachO::MH_OBJECT      },
  { "Executable",           MachO::MH_EXECUTE     },
  { "FixedVMLibrary",       MachO::MH_FVMLIB      },
  { "Core",                 MachO::MH_CORE        },
  { "PreloadedExecutable",  MachO::MH_PRELOAD     },
  { "DynamicLibrary",       MachO::MH_DYLIB       },
  { "DynamicLinker",        MachO::MH_DYLINKER    },
  { "Bundle",               MachO::MH_BUNDLE      },
  { "DynamicLibraryStub",   MachO::MH_DYLIB_STUB  },
  { "DWARFSymbol",          MachO::MH_DSYM        },
  { "KextBundle",           MachO::MH_KEXT_BUNDLE },
};

static const EnumEntry<uint32_t> MachOHeaderCpuTypes[] = {
  { "Any"       , static_cast<uint32_t>(MachO::CPU_TYPE_ANY) },
  { "X86"       , MachO::CPU_TYPE_X86       },
  { "X86-64"    , MachO::CPU_TYPE_X86_64    },
  { "Mc98000"   , MachO::CPU_TYPE_MC98000   },
  { "Arm"       , MachO::CPU_TYPE_ARM       },
  { "Arm64"     , MachO::CPU_TYPE_ARM64     },
  { "Sparc"     , MachO::CPU_TYPE_SPARC     },
  { "PowerPC"   , MachO::CPU_TYPE_POWERPC   },
  { "PowerPC64" , MachO::CPU_TYPE_POWERPC64 },
};

static const EnumEntry<uint32_t> MachOHeaderCpuSubtypesX86[] = {
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_I386_ALL),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_386),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_486),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_486SX),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_586),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_PENTPRO),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_PENTII_M3),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_PENTII_M5),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_CELERON),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_CELERON_MOBILE),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_PENTIUM_3),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_PENTIUM_3_M),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_PENTIUM_3_XEON),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_PENTIUM_M),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_PENTIUM_4),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_PENTIUM_4_M),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ITANIUM),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ITANIUM_2),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_XEON),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_XEON_MP),
};

static const EnumEntry<uint32_t> MachOHeaderCpuSubtypesX64[] = {
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_X86_64_ALL),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_X86_ARCH1),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_X86_64_H),
};

static const EnumEntry<uint32_t> MachOHeaderCpuSubtypesARM[] = {
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM_ALL),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM_V4T),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM_V6),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM_V5),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM_V5TEJ),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM_XSCALE),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM_V7),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM_V7S),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM_V7K),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM_V6M),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM_V7M),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM_V7EM),
};

static const EnumEntry<uint32_t> MachOHeaderCpuSubtypesARM64[] = {
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_ARM64_ALL),
};

static const EnumEntry<uint32_t> MachOHeaderCpuSubtypesSPARC[] = {
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_SPARC_ALL),
};

static const EnumEntry<uint32_t> MachOHeaderCpuSubtypesPPC[] = {
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_ALL),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_601),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_602),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_603),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_603e),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_603ev),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_604),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_604e),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_620),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_750),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_7400),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_7450),
  LLVM_READOBJ_ENUM_ENT(MachO, CPU_SUBTYPE_POWERPC_970),
};

static const EnumEntry<uint32_t> MachOHeaderFlags[] = {
  LLVM_READOBJ_ENUM_ENT(MachO, MH_NOUNDEFS),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_INCRLINK),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_DYLDLINK),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_BINDATLOAD),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_PREBOUND),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_SPLIT_SEGS),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_LAZY_INIT),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_TWOLEVEL),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_FORCE_FLAT),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_NOMULTIDEFS),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_NOFIXPREBINDING),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_PREBINDABLE),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_ALLMODSBOUND),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_SUBSECTIONS_VIA_SYMBOLS),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_CANONICAL),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_WEAK_DEFINES),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_BINDS_TO_WEAK),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_ALLOW_STACK_EXECUTION),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_ROOT_SAFE),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_SETUID_SAFE),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_NO_REEXPORTED_DYLIBS),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_PIE),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_DEAD_STRIPPABLE_DYLIB),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_HAS_TLV_DESCRIPTORS),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_NO_HEAP_EXECUTION),
  LLVM_READOBJ_ENUM_ENT(MachO, MH_APP_EXTENSION_SAFE),
};

static const EnumEntry<unsigned> MachOSectionAttributes[] = {
  { "LocReloc"         , 1 <<  0 /*S_ATTR_LOC_RELOC          */ },
  { "ExtReloc"         , 1 <<  1 /*S_ATTR_EXT_RELOC          */ },
  { "SomeInstructions" , 1 <<  2 /*S_ATTR_SOME_INSTRUCTIONS  */ },
  { "Debug"            , 1 << 17 /*S_ATTR_DEBUG              */ },
  { "SelfModifyingCode", 1 << 18 /*S_ATTR_SELF_MODIFYING_CODE*/ },
  { "LiveSupport"      , 1 << 19 /*S_ATTR_LIVE_SUPPORT       */ },
  { "NoDeadStrip"      , 1 << 20 /*S_ATTR_NO_DEAD_STRIP      */ },
  { "StripStaticSyms"  , 1 << 21 /*S_ATTR_STRIP_STATIC_SYMS  */ },
  { "NoTOC"            , 1 << 22 /*S_ATTR_NO_TOC             */ },
  { "PureInstructions" , 1 << 23 /*S_ATTR_PURE_INSTRUCTIONS  */ },
};

static const EnumEntry<unsigned> MachOSymbolRefTypes[] = {
  { "UndefinedNonLazy",                     0 },
  { "ReferenceFlagUndefinedLazy",           1 },
  { "ReferenceFlagDefined",                 2 },
  { "ReferenceFlagPrivateDefined",          3 },
  { "ReferenceFlagPrivateUndefinedNonLazy", 4 },
  { "ReferenceFlagPrivateUndefinedLazy",    5 }
};

static const EnumEntry<unsigned> MachOSymbolFlags[] = {
  { "ReferencedDynamically", 0x10 },
  { "NoDeadStrip",           0x20 },
  { "WeakRef",               0x40 },
  { "WeakDef",               0x80 },
  { "AltEntry",             0x200 },
};

static const EnumEntry<unsigned> MachOSymbolTypes[] = {
  { "Undef",           0x0 },
  { "Abs",             0x2 },
  { "Indirect",        0xA },
  { "PreboundUndef",   0xC },
  { "Section",         0xE }
};

namespace {
  struct MachOSection {
    ArrayRef<char> Name;
    ArrayRef<char> SegmentName;
    uint64_t Address;
    uint64_t Size;
    uint32_t Offset;
    uint32_t Alignment;
    uint32_t RelocationTableOffset;
    uint32_t NumRelocationTableEntries;
    uint32_t Flags;
    uint32_t Reserved1;
    uint32_t Reserved2;
    uint32_t Reserved3;
  };

  struct MachOSegment {
    std::string CmdName;
    std::string SegName;
    uint64_t cmdsize;
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
  };

  struct MachOSymbol {
    uint32_t StringIndex;
    uint8_t Type;
    uint8_t SectionIndex;
    uint16_t Flags;
    uint64_t Value;
  };
}

static std::string getMask(uint32_t prot)
{
  // TODO (davide): This always assumes prot is valid.
  // Catch mistakes and report if needed.
  std::string Prot;
  Prot = "";
  Prot += (prot & MachO::VM_PROT_READ) ? "r" : "-";
  Prot += (prot & MachO::VM_PROT_WRITE) ? "w" : "-";
  Prot += (prot & MachO::VM_PROT_EXECUTE) ? "x" : "-";
  return Prot;
}

static void getSection(const MachOObjectFile *Obj,
                       DataRefImpl Sec,
                       MachOSection &Section) {
  if (!Obj->is64Bit()) {
    MachO::section Sect = Obj->getSection(Sec);
    Section.Address     = Sect.addr;
    Section.Size        = Sect.size;
    Section.Offset      = Sect.offset;
    Section.Alignment   = Sect.align;
    Section.RelocationTableOffset = Sect.reloff;
    Section.NumRelocationTableEntries = Sect.nreloc;
    Section.Flags       = Sect.flags;
    Section.Reserved1   = Sect.reserved1;
    Section.Reserved2   = Sect.reserved2;
    return;
  }
  MachO::section_64 Sect = Obj->getSection64(Sec);
  Section.Address     = Sect.addr;
  Section.Size        = Sect.size;
  Section.Offset      = Sect.offset;
  Section.Alignment   = Sect.align;
  Section.RelocationTableOffset = Sect.reloff;
  Section.NumRelocationTableEntries = Sect.nreloc;
  Section.Flags       = Sect.flags;
  Section.Reserved1   = Sect.reserved1;
  Section.Reserved2   = Sect.reserved2;
  Section.Reserved3   = Sect.reserved3;
}

static void getSegment(const MachOObjectFile *Obj,
                       const MachOObjectFile::LoadCommandInfo &L,
                       MachOSegment &Segment) {
  if (!Obj->is64Bit()) {
    MachO::segment_command SC = Obj->getSegmentLoadCommand(L);
    Segment.CmdName = "LC_SEGMENT";
    Segment.SegName = SC.segname;
    Segment.cmdsize = SC.cmdsize;
    Segment.vmaddr = SC.vmaddr;
    Segment.vmsize = SC.vmsize;
    Segment.fileoff = SC.fileoff;
    Segment.filesize = SC.filesize;
    Segment.maxprot = SC.maxprot;
    Segment.initprot = SC.initprot;
    Segment.nsects = SC.nsects;
    Segment.flags = SC.flags;
    return;
  }
  MachO::segment_command_64 SC = Obj->getSegment64LoadCommand(L);
  Segment.CmdName = "LC_SEGMENT_64";
  Segment.SegName = SC.segname;
  Segment.cmdsize = SC.cmdsize;
  Segment.vmaddr = SC.vmaddr;
  Segment.vmsize = SC.vmsize;
  Segment.fileoff = SC.fileoff;
  Segment.filesize = SC.filesize;
  Segment.maxprot = SC.maxprot;
  Segment.initprot = SC.initprot;
  Segment.nsects = SC.nsects;
  Segment.flags = SC.flags;
}

static void getSymbol(const MachOObjectFile *Obj,
                      DataRefImpl DRI,
                      MachOSymbol &Symbol) {
  if (!Obj->is64Bit()) {
    MachO::nlist Entry = Obj->getSymbolTableEntry(DRI);
    Symbol.StringIndex  = Entry.n_strx;
    Symbol.Type         = Entry.n_type;
    Symbol.SectionIndex = Entry.n_sect;
    Symbol.Flags        = Entry.n_desc;
    Symbol.Value        = Entry.n_value;
    return;
  }
  MachO::nlist_64 Entry = Obj->getSymbol64TableEntry(DRI);
  Symbol.StringIndex  = Entry.n_strx;
  Symbol.Type         = Entry.n_type;
  Symbol.SectionIndex = Entry.n_sect;
  Symbol.Flags        = Entry.n_desc;
  Symbol.Value        = Entry.n_value;
}

void MachODumper::printFileHeaders() {
  DictScope H(W, "MachHeader");
  if (!Obj->is64Bit()) {
    printFileHeaders(Obj->getHeader());
  } else {
    printFileHeaders(Obj->getHeader64());
    W.printHex("Reserved", Obj->getHeader64().reserved);
  }
}

template<class MachHeader>
void MachODumper::printFileHeaders(const MachHeader &Header) {
  W.printEnum("Magic", Header.magic, makeArrayRef(MachOMagics));
  W.printEnum("CpuType", Header.cputype, makeArrayRef(MachOHeaderCpuTypes));
  uint32_t subtype = Header.cpusubtype & ~MachO::CPU_SUBTYPE_MASK;
  switch (Header.cputype) {
  case MachO::CPU_TYPE_X86:
    W.printEnum("CpuSubType", subtype, makeArrayRef(MachOHeaderCpuSubtypesX86));
    break;
  case MachO::CPU_TYPE_X86_64:
    W.printEnum("CpuSubType", subtype, makeArrayRef(MachOHeaderCpuSubtypesX64));
    break;
  case MachO::CPU_TYPE_ARM:
    W.printEnum("CpuSubType", subtype, makeArrayRef(MachOHeaderCpuSubtypesARM));
    break;
  case MachO::CPU_TYPE_POWERPC:
    W.printEnum("CpuSubType", subtype, makeArrayRef(MachOHeaderCpuSubtypesPPC));
    break;
  case MachO::CPU_TYPE_SPARC:
    W.printEnum("CpuSubType", subtype, makeArrayRef(MachOHeaderCpuSubtypesSPARC));
    break;
  case MachO::CPU_TYPE_ARM64:
    W.printEnum("CpuSubType", subtype, makeArrayRef(MachOHeaderCpuSubtypesARM64));
    break;
  case MachO::CPU_TYPE_POWERPC64:
  default:
    W.printHex("CpuSubtype", subtype);
  }
  W.printEnum("FileType", Header.filetype, makeArrayRef(MachOHeaderFileTypes));
  W.printNumber("NumOfLoadCommands", Header.ncmds);
  W.printNumber("SizeOfLoadCommands", Header.sizeofcmds);
  W.printFlags("Flags", Header.flags, makeArrayRef(MachOHeaderFlags));
}

void MachODumper::printSectionHeaders() { return printSectionHeaders(Obj); }

void MachODumper::printSectionHeaders(const MachOObjectFile *Obj) {
  ListScope Group(W, "Sections");

  int SectionIndex = -1;
  for (const SectionRef &Section : Obj->sections()) {
    ++SectionIndex;

    MachOSection MOSection;
    getSection(Obj, Section.getRawDataRefImpl(), MOSection);
    DataRefImpl DR = Section.getRawDataRefImpl();

    StringRef Name;
    error(Section.getName(Name));

    ArrayRef<char> RawName = Obj->getSectionRawName(DR);
    StringRef SegmentName = Obj->getSectionFinalSegmentName(DR);
    ArrayRef<char> RawSegmentName = Obj->getSectionRawFinalSegmentName(DR);

    DictScope SectionD(W, "Section");
    W.printNumber("Index", SectionIndex);
    W.printBinary("Name", Name, RawName);
    W.printBinary("Segment", SegmentName, RawSegmentName);
    W.printHex("Address", MOSection.Address);
    W.printHex("Size", MOSection.Size);
    W.printNumber("Offset", MOSection.Offset);
    W.printNumber("Alignment", MOSection.Alignment);
    W.printHex("RelocationOffset", MOSection.RelocationTableOffset);
    W.printNumber("RelocationCount", MOSection.NumRelocationTableEntries);
    W.printEnum("Type", MOSection.Flags & 0xFF,
                makeArrayRef(MachOSectionAttributes));
    W.printFlags("Attributes", MOSection.Flags >> 8,
                 makeArrayRef(MachOSectionAttributes));
    W.printHex("Reserved1", MOSection.Reserved1);
    W.printHex("Reserved2", MOSection.Reserved2);
    if (Obj->is64Bit())
      W.printHex("Reserved3", MOSection.Reserved3);

    if (opts::SectionRelocations) {
      ListScope D(W, "Relocations");
      for (const RelocationRef &Reloc : Section.relocations())
        printRelocation(Reloc);
    }

    if (opts::SectionSymbols) {
      ListScope D(W, "Symbols");
      for (const SymbolRef &Symbol : Obj->symbols()) {
        if (!Section.containsSymbol(Symbol))
          continue;

        printSymbol(Symbol);
      }
    }

    if (opts::SectionData) {
      bool IsBSS = Section.isBSS();
      if (!IsBSS) {
        StringRef Data;
        error(Section.getContents(Data));

        W.printBinaryBlock("SectionData", Data);
      }
    }
  }
}

void MachODumper::printRelocations() {
  ListScope D(W, "Relocations");

  std::error_code EC;
  for (const SectionRef &Section : Obj->sections()) {
    StringRef Name;
    error(Section.getName(Name));

    bool PrintedGroup = false;
    for (const RelocationRef &Reloc : Section.relocations()) {
      if (!PrintedGroup) {
        W.startLine() << "Section " << Name << " {\n";
        W.indent();
        PrintedGroup = true;
      }

      printRelocation(Reloc);
    }

    if (PrintedGroup) {
      W.unindent();
      W.startLine() << "}\n";
    }
  }
}

void MachODumper::printRelocation(const RelocationRef &Reloc) {
  return printRelocation(Obj, Reloc);
}

void MachODumper::printRelocation(const MachOObjectFile *Obj,
                                  const RelocationRef &Reloc) {
  uint64_t Offset = Reloc.getOffset();
  SmallString<32> RelocName;
  Reloc.getTypeName(RelocName);

  DataRefImpl DR = Reloc.getRawDataRefImpl();
  MachO::any_relocation_info RE = Obj->getRelocation(DR);
  bool IsScattered = Obj->isRelocationScattered(RE);
  bool IsExtern = !IsScattered && Obj->getPlainRelocationExternal(RE);

  StringRef TargetName;
  if (IsExtern) {
    symbol_iterator Symbol = Reloc.getSymbol();
    if (Symbol != Obj->symbol_end()) {
      Expected<StringRef> TargetNameOrErr = Symbol->getName();
      if (!TargetNameOrErr)
        error(errorToErrorCode(TargetNameOrErr.takeError()));
      TargetName = *TargetNameOrErr;
    }
  } else if (!IsScattered) {
    section_iterator SecI = Obj->getRelocationSection(DR);
    if (SecI != Obj->section_end()) {
      error(SecI->getName(TargetName));
    }
  }
  if (TargetName.empty())
    TargetName = "-";

  if (opts::ExpandRelocs) {
    DictScope Group(W, "Relocation");
    W.printHex("Offset", Offset);
    W.printNumber("PCRel", Obj->getAnyRelocationPCRel(RE));
    W.printNumber("Length", Obj->getAnyRelocationLength(RE));
    W.printNumber("Type", RelocName, Obj->getAnyRelocationType(RE));
    if (IsScattered) {
      W.printHex("Value", Obj->getScatteredRelocationValue(RE));
    } else {
      const char *Kind = IsExtern ? "Symbol" : "Section";
      W.printNumber(Kind, TargetName, Obj->getPlainRelocationSymbolNum(RE));
    }
  } else {
    SmallString<32> SymbolNameOrOffset("0x");
    if (IsScattered) {
      // Scattered relocations don't really have an associated symbol for some
      // reason, even if one exists in the symtab at the correct address.
      SymbolNameOrOffset += utohexstr(Obj->getScatteredRelocationValue(RE));
    } else {
      SymbolNameOrOffset = TargetName;
    }

    raw_ostream& OS = W.startLine();
    OS << W.hex(Offset)
       << " " << Obj->getAnyRelocationPCRel(RE)
       << " " << Obj->getAnyRelocationLength(RE);
    if (IsScattered)
      OS << " n/a";
    else
      OS << " " << Obj->getPlainRelocationExternal(RE);
    OS << " " << RelocName
       << " " << IsScattered
       << " " << SymbolNameOrOffset
       << "\n";
  }
}

void MachODumper::printSymbols() {
  ListScope Group(W, "Symbols");

  for (const SymbolRef &Symbol : Obj->symbols()) {
    printSymbol(Symbol);
  }
}

void MachODumper::printDynamicSymbols() {
  ListScope Group(W, "DynamicSymbols");
}

void MachODumper::printSymbol(const SymbolRef &Symbol) {
  StringRef SymbolName;
  Expected<StringRef> SymbolNameOrErr = Symbol.getName();
  if (!SymbolNameOrErr) {
    // TODO: Actually report errors helpfully.
    consumeError(SymbolNameOrErr.takeError());
  } else
    SymbolName = *SymbolNameOrErr;

  MachOSymbol MOSymbol;
  getSymbol(Obj, Symbol.getRawDataRefImpl(), MOSymbol);

  StringRef SectionName = "";
  Expected<section_iterator> SecIOrErr = Symbol.getSection();
  error(errorToErrorCode(SecIOrErr.takeError()));
  section_iterator SecI = *SecIOrErr;
  if (SecI != Obj->section_end())
    error(SecI->getName(SectionName));

  DictScope D(W, "Symbol");
  W.printNumber("Name", SymbolName, MOSymbol.StringIndex);
  if (MOSymbol.Type & MachO::N_STAB) {
    W.printHex("Type", "SymDebugTable", MOSymbol.Type);
  } else {
    if (MOSymbol.Type & MachO::N_PEXT)
      W.startLine() << "PrivateExtern\n";
    if (MOSymbol.Type & MachO::N_EXT)
      W.startLine() << "Extern\n";
    W.printEnum("Type", uint8_t(MOSymbol.Type & MachO::N_TYPE),
                makeArrayRef(MachOSymbolTypes));
  }
  W.printHex("Section", SectionName, MOSymbol.SectionIndex);
  W.printEnum("RefType", static_cast<uint16_t>(MOSymbol.Flags & 0xF),
              makeArrayRef(MachOSymbolRefTypes));
  W.printFlags("Flags", static_cast<uint16_t>(MOSymbol.Flags & ~0xF),
               makeArrayRef(MachOSymbolFlags));
  W.printHex("Value", MOSymbol.Value);
}

void MachODumper::printUnwindInfo() {
  W.startLine() << "UnwindInfo not implemented.\n";
}

void MachODumper::printStackMap() const {
  object::SectionRef StackMapSection;
  for (auto Sec : Obj->sections()) {
    StringRef Name;
    Sec.getName(Name);
    if (Name == "__llvm_stackmaps") {
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

void MachODumper::printNeededLibraries() {
  ListScope D(W, "NeededLibraries");

  using LibsTy = std::vector<StringRef>;
  LibsTy Libs;

  for (const auto &Command : Obj->load_commands()) {
    if (Command.C.cmd == MachO::LC_LOAD_DYLIB ||
        Command.C.cmd == MachO::LC_ID_DYLIB ||
        Command.C.cmd == MachO::LC_LOAD_WEAK_DYLIB ||
        Command.C.cmd == MachO::LC_REEXPORT_DYLIB ||
        Command.C.cmd == MachO::LC_LAZY_LOAD_DYLIB ||
        Command.C.cmd == MachO::LC_LOAD_UPWARD_DYLIB) {
      MachO::dylib_command Dl = Obj->getDylibIDLoadCommand(Command);
      if (Dl.dylib.name < Dl.cmdsize) {
        auto *P = static_cast<const char*>(Command.Ptr) + Dl.dylib.name;
        Libs.push_back(P);
      }
    }
  }

  std::stable_sort(Libs.begin(), Libs.end());

  for (const auto &L : Libs) {
    outs() << "  " << L << "\n";
  }
}

void MachODumper::printMachODataInCode() {
  for (const auto &Load : Obj->load_commands()) {
    if (Load.C.cmd  == MachO::LC_DATA_IN_CODE) {
      MachO::linkedit_data_command LLC = Obj->getLinkeditDataLoadCommand(Load);
      DictScope Group(W, "DataInCode");
      W.printNumber("Data offset", LLC.dataoff);
      W.printNumber("Data size", LLC.datasize);
      ListScope D(W, "Data entries");
      unsigned NumRegions = LLC.datasize / sizeof(MachO::data_in_code_entry);
      for (unsigned i = 0; i < NumRegions; ++i) {
        MachO::data_in_code_entry DICE = Obj->getDataInCodeTableEntry(
                                                              LLC.dataoff, i);
        DictScope Group(W, "Entry");
        W.printNumber("Index", i);
        W.printNumber("Offset", DICE.offset);
        W.printNumber("Length", DICE.length);
        W.printNumber("Kind", DICE.kind);
      }
    }
  }
}

void MachODumper::printMachOVersionMin() {
  for (const auto &Load : Obj->load_commands()) {
    StringRef Cmd;
    switch (Load.C.cmd) {
    case MachO::LC_VERSION_MIN_MACOSX:
      Cmd = "LC_VERSION_MIN_MACOSX";
      break;
    case MachO::LC_VERSION_MIN_IPHONEOS:
      Cmd = "LC_VERSION_MIN_IPHONEOS";
      break;
    case MachO::LC_VERSION_MIN_TVOS:
      Cmd = "LC_VERSION_MIN_TVOS";
      break;
    case MachO::LC_VERSION_MIN_WATCHOS:
      Cmd = "LC_VERSION_MIN_WATCHOS";
      break;
    case MachO::LC_BUILD_VERSION:
      Cmd = "LC_BUILD_VERSION";
      break;
    default:
      continue;
    }

    DictScope Group(W, "MinVersion");
    // Handle LC_BUILD_VERSION.
    if (Load.C.cmd == MachO::LC_BUILD_VERSION) {
      MachO::build_version_command BVC = Obj->getBuildVersionLoadCommand(Load);
      W.printString("Cmd", Cmd);
      W.printNumber("Size", BVC.cmdsize);
      W.printString("Platform",
                    MachOObjectFile::getBuildPlatform(BVC.platform));
      W.printString("Version", MachOObjectFile::getVersionString(BVC.minos));
      if (BVC.sdk)
        W.printString("SDK", MachOObjectFile::getVersionString(BVC.sdk));
      else
        W.printString("SDK", StringRef("n/a"));
      continue;
    }

    MachO::version_min_command VMC = Obj->getVersionMinLoadCommand(Load);
    W.printString("Cmd", Cmd);
    W.printNumber("Size", VMC.cmdsize);
    SmallString<32> Version;
    Version = utostr(MachOObjectFile::getVersionMinMajor(VMC, false)) + "." +
              utostr(MachOObjectFile::getVersionMinMinor(VMC, false));
    uint32_t Update = MachOObjectFile::getVersionMinUpdate(VMC, false);
    if (Update != 0)
      Version += "." + utostr(MachOObjectFile::getVersionMinUpdate(VMC, false));
    W.printString("Version", Version);
    SmallString<32> SDK;
    if (VMC.sdk == 0)
      SDK = "n/a";
    else {
      SDK = utostr(MachOObjectFile::getVersionMinMajor(VMC, true)) + "." +
            utostr(MachOObjectFile::getVersionMinMinor(VMC, true));
      uint32_t Update = MachOObjectFile::getVersionMinUpdate(VMC, true);
      if (Update != 0)
        SDK += "." + utostr(MachOObjectFile::getVersionMinUpdate(VMC, true));
    }
    W.printString("SDK", SDK);
  }
}

void MachODumper::printMachODysymtab() {
  for (const auto &Load : Obj->load_commands()) {
    if (Load.C.cmd == MachO::LC_DYSYMTAB) {
      MachO::dysymtab_command DLC = Obj->getDysymtabLoadCommand();
      DictScope Group(W, "Dysymtab");
      W.printNumber("ilocalsym", DLC.ilocalsym);
      W.printNumber("nlocalsym", DLC.nlocalsym);
      W.printNumber("iextdefsym", DLC.iextdefsym);
      W.printNumber("nextdefsym", DLC.nextdefsym);
      W.printNumber("iundefsym", DLC.iundefsym);
      W.printNumber("nundefsym", DLC.nundefsym);
      W.printNumber("tocoff", DLC.tocoff);
      W.printNumber("ntoc", DLC.ntoc);
      W.printNumber("modtaboff", DLC.modtaboff);
      W.printNumber("nmodtab", DLC.nmodtab);
      W.printNumber("extrefsymoff", DLC.extrefsymoff);
      W.printNumber("nextrefsyms", DLC.nextrefsyms);
      W.printNumber("indirectsymoff", DLC.indirectsymoff);
      W.printNumber("nindirectsyms", DLC.nindirectsyms);
      W.printNumber("extreloff", DLC.extreloff);
      W.printNumber("nextrel", DLC.nextrel);
      W.printNumber("locreloff", DLC.locreloff);
      W.printNumber("nlocrel", DLC.nlocrel);
    }
  }
}

void MachODumper::printMachOSegment() {
  for (const auto &Load : Obj->load_commands()) {
    if (Load.C.cmd == MachO::LC_SEGMENT || Load.C.cmd == MachO::LC_SEGMENT_64) {
      MachOSegment MOSegment;
      getSegment(Obj, Load, MOSegment);
      DictScope Group(W, "Segment");
      W.printString("Cmd", MOSegment.CmdName);
      W.printString("Name", MOSegment.SegName);
      W.printNumber("Size", MOSegment.cmdsize);
      W.printHex("vmaddr", MOSegment.vmaddr);
      W.printHex("vmsize", MOSegment.vmsize);
      W.printNumber("fileoff", MOSegment.fileoff);
      W.printNumber("filesize", MOSegment.filesize);
      W.printString("maxprot", getMask(MOSegment.maxprot));
      W.printString("initprot", getMask(MOSegment.initprot));
      W.printNumber("nsects", MOSegment.nsects);
      W.printHex("flags", MOSegment.flags);
    }
  }
}

void MachODumper::printMachOIndirectSymbols() {
  for (const auto &Load : Obj->load_commands()) {
    if (Load.C.cmd == MachO::LC_DYSYMTAB) {
      MachO::dysymtab_command DLC = Obj->getDysymtabLoadCommand();
      DictScope Group(W, "Indirect Symbols");
      W.printNumber("Number", DLC.nindirectsyms);
      ListScope D(W, "Symbols");
      for (unsigned i = 0; i < DLC.nindirectsyms; ++i) {
        DictScope Group(W, "Entry");
        W.printNumber("Entry Index", i);
        W.printHex("Symbol Index", Obj->getIndirectSymbolTableEntry(DLC, i));
      }
    }
  }
}

void MachODumper::printMachOLinkerOptions() {
  for (const auto &Load : Obj->load_commands()) {
    if (Load.C.cmd == MachO::LC_LINKER_OPTION) {
      MachO::linker_option_command LOLC = Obj->getLinkerOptionLoadCommand(Load);
      DictScope Group(W, "Linker Options");
      W.printNumber("Size", LOLC.cmdsize);
      ListScope D(W, "Strings");
      uint64_t DataSize = LOLC.cmdsize - sizeof(MachO::linker_option_command);
      const char *P = Load.Ptr + sizeof(MachO::linker_option_command);
      StringRef Data(P, DataSize);
      for (unsigned i = 0; i < LOLC.count; ++i) {
        std::pair<StringRef,StringRef> Split = Data.split('\0');
        W.printString("Value", Split.first);
        Data = Split.second;
      }
    }
  }
}
