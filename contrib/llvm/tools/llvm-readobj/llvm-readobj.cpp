//===- llvm-readobj.cpp - Dump contents of an Object File -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a tool similar to readelf, except it works on multiple object file
// formats. The main purpose of this tool is to provide detailed output suitable
// for FileCheck.
//
// Flags should be similar to readelf where supported, but the output format
// does not need to be identical. The point is to not make users learn yet
// another set of flags.
//
// Output should be specialized for each format where appropriate.
//
//===----------------------------------------------------------------------===//

#include "llvm-readobj.h"
#include "Error.h"
#include "ObjDumper.h"
#include "WindowsResourceDumper.h"
#include "llvm/DebugInfo/CodeView/MergingTypeTableBuilder.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFFImportFile.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/WindowsResource.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;
using namespace llvm::object;

namespace opts {
  cl::list<std::string> InputFilenames(cl::Positional,
    cl::desc("<input object files>"),
    cl::ZeroOrMore);

  // -all, -a
  cl::opt<bool>
      All("all",
          cl::desc("Equivalent to setting: --file-headers, --program-headers, "
                   "--section-headers, --symbols, --relocations, "
                   "--dynamic-table, --notes, --version-info, --unwind, "
                   "--section-groups and --elf-hash-histogram."));
  cl::alias AllShort("a", cl::desc("Alias for --all"), cl::aliasopt(All));

  // --headers -e
  cl::opt<bool>
      Headers("headers",
          cl::desc("Equivalent to setting: --file-headers, --program-headers, "
                   "--section-headers"));
  cl::alias HeadersShort("e", cl::desc("Alias for --headers"),
     cl::aliasopt(Headers));

  // -wide, -W
  cl::opt<bool>
      WideOutput("wide", cl::desc("Ignored for compatibility with GNU readelf"),
                 cl::Hidden);
  cl::alias WideOutputShort("W",
    cl::desc("Alias for --wide"),
    cl::aliasopt(WideOutput));

  // -file-headers, -file-header, -h
  cl::opt<bool> FileHeaders("file-headers",
    cl::desc("Display file headers "));
  cl::alias FileHeadersShort("h", cl::desc("Alias for --file-headers"),
                             cl::aliasopt(FileHeaders), cl::NotHidden);
  cl::alias FileHeadersSingular("file-header",
                                cl::desc("Alias for --file-headers"),
                                cl::aliasopt(FileHeaders));

  // -section-headers, -sections, -S
  // Also -s in llvm-readobj mode.
  cl::opt<bool> SectionHeaders("section-headers",
                               cl::desc("Display all section headers."));
  cl::alias SectionsShortUpper("S", cl::desc("Alias for --section-headers"),
                               cl::aliasopt(SectionHeaders), cl::NotHidden);
  cl::alias SectionHeadersAlias("sections",
                                cl::desc("Alias for --section-headers"),
                                cl::aliasopt(SectionHeaders), cl::NotHidden);

  // -section-relocations
  // Also -sr in llvm-readobj mode.
  cl::opt<bool> SectionRelocations("section-relocations",
    cl::desc("Display relocations for each section shown."));

  // -section-symbols
  // Also -st in llvm-readobj mode.
  cl::opt<bool> SectionSymbols("section-symbols",
    cl::desc("Display symbols for each section shown."));

  // -section-data
  // Also -sd in llvm-readobj mode.
  cl::opt<bool> SectionData("section-data",
    cl::desc("Display section data for each section shown."));

  // -relocations, -relocs, -r
  cl::opt<bool> Relocations("relocations",
    cl::desc("Display the relocation entries in the file"));
  cl::alias RelocationsShort("r", cl::desc("Alias for --relocations"),
                             cl::aliasopt(Relocations), cl::NotHidden);
  cl::alias RelocationsGNU("relocs", cl::desc("Alias for --relocations"),
                           cl::aliasopt(Relocations));

  // -notes, -n
  cl::opt<bool> Notes("notes", cl::desc("Display the ELF notes in the file"));
  cl::alias NotesShort("n", cl::desc("Alias for --notes"), cl::aliasopt(Notes));

  // -dyn-relocations
  cl::opt<bool> DynRelocs("dyn-relocations",
    cl::desc("Display the dynamic relocation entries in the file"));

  // -symbols
  // Also -s in llvm-readelf mode, or -t in llvm-readobj mode.
  cl::opt<bool> Symbols("symbols",
    cl::desc("Display the symbol table"));
  cl::alias SymbolsGNU("syms", cl::desc("Alias for --symbols"),
                       cl::aliasopt(Symbols));

  // -dyn-symbols, -dyn-syms
  // Also -dt in llvm-readobj mode.
  cl::opt<bool> DynamicSymbols("dyn-symbols",
    cl::desc("Display the dynamic symbol table"));
  cl::alias DynSymsGNU("dyn-syms", cl::desc("Alias for --dyn-symbols"),
                       cl::aliasopt(DynamicSymbols));

  // -unwind, -u
  cl::opt<bool> UnwindInfo("unwind",
    cl::desc("Display unwind information"));
  cl::alias UnwindInfoShort("u",
    cl::desc("Alias for --unwind"),
    cl::aliasopt(UnwindInfo));

  // -dynamic-table, -dynamic, -d
  cl::opt<bool> DynamicTable("dynamic-table",
    cl::desc("Display the ELF .dynamic section table"));
  cl::alias DynamicTableShort("d", cl::desc("Alias for --dynamic-table"),
                              cl::aliasopt(DynamicTable), cl::NotHidden);
  cl::alias DynamicTableAlias("dynamic", cl::desc("Alias for --dynamic-table"),
                              cl::aliasopt(DynamicTable));

  // -needed-libs
  cl::opt<bool> NeededLibraries("needed-libs",
    cl::desc("Display the needed libraries"));

  // -program-headers, -segments, -l
  cl::opt<bool> ProgramHeaders("program-headers",
    cl::desc("Display ELF program headers"));
  cl::alias ProgramHeadersShort("l", cl::desc("Alias for --program-headers"),
                                cl::aliasopt(ProgramHeaders), cl::NotHidden);
  cl::alias SegmentsAlias("segments", cl::desc("Alias for --program-headers"),
                          cl::aliasopt(ProgramHeaders));

  // -string-dump, -p
  cl::list<std::string> StringDump("string-dump", cl::desc("<number|name>"),
                                   cl::ZeroOrMore);
  cl::alias StringDumpShort("p", cl::desc("Alias for --string-dump"),
                            cl::aliasopt(StringDump));

  // -hex-dump, -x
  cl::list<std::string> HexDump("hex-dump", cl::desc("<number|name>"),
                                cl::ZeroOrMore);
  cl::alias HexDumpShort("x", cl::desc("Alias for --hex-dump"),
                         cl::aliasopt(HexDump));

  // -hash-table
  cl::opt<bool> HashTable("hash-table",
    cl::desc("Display ELF hash table"));

  // -gnu-hash-table
  cl::opt<bool> GnuHashTable("gnu-hash-table",
    cl::desc("Display ELF .gnu.hash section"));

  // -expand-relocs
  cl::opt<bool> ExpandRelocs("expand-relocs",
    cl::desc("Expand each shown relocation to multiple lines"));

  // -raw-relr
  cl::opt<bool> RawRelr("raw-relr",
    cl::desc("Do not decode relocations in SHT_RELR section, display raw contents"));

  // -codeview
  cl::opt<bool> CodeView("codeview",
                         cl::desc("Display CodeView debug information"));

  // -codeview-merged-types
  cl::opt<bool>
      CodeViewMergedTypes("codeview-merged-types",
                          cl::desc("Display the merged CodeView type stream"));

  // -codeview-subsection-bytes
  cl::opt<bool> CodeViewSubsectionBytes(
      "codeview-subsection-bytes",
      cl::desc("Dump raw contents of codeview debug sections and records"));

  // -arm-attributes
  cl::opt<bool> ARMAttributes("arm-attributes",
                              cl::desc("Display the ARM attributes section"));

  // -mips-plt-got
  cl::opt<bool>
  MipsPLTGOT("mips-plt-got",
             cl::desc("Display the MIPS GOT and PLT GOT sections"));

  // -mips-abi-flags
  cl::opt<bool> MipsABIFlags("mips-abi-flags",
                             cl::desc("Display the MIPS.abiflags section"));

  // -mips-reginfo
  cl::opt<bool> MipsReginfo("mips-reginfo",
                            cl::desc("Display the MIPS .reginfo section"));

  // -mips-options
  cl::opt<bool> MipsOptions("mips-options",
                            cl::desc("Display the MIPS .MIPS.options section"));

  // -coff-imports
  cl::opt<bool>
  COFFImports("coff-imports", cl::desc("Display the PE/COFF import table"));

  // -coff-exports
  cl::opt<bool>
  COFFExports("coff-exports", cl::desc("Display the PE/COFF export table"));

  // -coff-directives
  cl::opt<bool>
  COFFDirectives("coff-directives",
                 cl::desc("Display the PE/COFF .drectve section"));

  // -coff-basereloc
  cl::opt<bool>
  COFFBaseRelocs("coff-basereloc",
                 cl::desc("Display the PE/COFF .reloc section"));

  // -coff-debug-directory
  cl::opt<bool>
  COFFDebugDirectory("coff-debug-directory",
                     cl::desc("Display the PE/COFF debug directory"));

  // -coff-resources
  cl::opt<bool> COFFResources("coff-resources",
                              cl::desc("Display the PE/COFF .rsrc section"));

  // -coff-load-config
  cl::opt<bool>
  COFFLoadConfig("coff-load-config",
                 cl::desc("Display the PE/COFF load config"));

  // -elf-linker-options
  cl::opt<bool>
  ELFLinkerOptions("elf-linker-options",
                   cl::desc("Display the ELF .linker-options section"));

  // -macho-data-in-code
  cl::opt<bool>
  MachODataInCode("macho-data-in-code",
                  cl::desc("Display MachO Data in Code command"));

  // -macho-indirect-symbols
  cl::opt<bool>
  MachOIndirectSymbols("macho-indirect-symbols",
                  cl::desc("Display MachO indirect symbols"));

  // -macho-linker-options
  cl::opt<bool>
  MachOLinkerOptions("macho-linker-options",
                  cl::desc("Display MachO linker options"));

  // -macho-segment
  cl::opt<bool>
  MachOSegment("macho-segment",
                  cl::desc("Display MachO Segment command"));

  // -macho-version-min
  cl::opt<bool>
  MachOVersionMin("macho-version-min",
                  cl::desc("Display MachO version min command"));

  // -macho-dysymtab
  cl::opt<bool>
  MachODysymtab("macho-dysymtab",
                  cl::desc("Display MachO Dysymtab command"));

  // -stackmap
  cl::opt<bool>
  PrintStackMap("stackmap",
                cl::desc("Display contents of stackmap section"));

  // -version-info, -V
  cl::opt<bool>
      VersionInfo("version-info",
                  cl::desc("Display ELF version sections (if present)"));
  cl::alias VersionInfoShort("V", cl::desc("Alias for -version-info"),
                             cl::aliasopt(VersionInfo));

  // -elf-section-groups, -section-groups, -g
  cl::opt<bool> SectionGroups("elf-section-groups",
                              cl::desc("Display ELF section group contents"));
  cl::alias SectionGroupsAlias("section-groups",
                               cl::desc("Alias for -elf-sections-groups"),
                               cl::aliasopt(SectionGroups));
  cl::alias SectionGroupsShort("g", cl::desc("Alias for -elf-sections-groups"),
                               cl::aliasopt(SectionGroups));

  // -elf-hash-histogram, -histogram, -I
  cl::opt<bool> HashHistogram(
      "elf-hash-histogram",
      cl::desc("Display bucket list histogram for hash sections"));
  cl::alias HashHistogramShort("I", cl::desc("Alias for -elf-hash-histogram"),
                               cl::aliasopt(HashHistogram));
  cl::alias HistogramAlias("histogram",
                           cl::desc("Alias for --elf-hash-histogram"),
                           cl::aliasopt(HashHistogram));

  // -elf-cg-profile
  cl::opt<bool> CGProfile("elf-cg-profile", cl::desc("Display callgraph profile section"));

  // -addrsig
  cl::opt<bool> Addrsig("addrsig",
                        cl::desc("Display address-significance table"));

  // -elf-output-style
  cl::opt<OutputStyleTy>
      Output("elf-output-style", cl::desc("Specify ELF dump style"),
             cl::values(clEnumVal(LLVM, "LLVM default style"),
                        clEnumVal(GNU, "GNU readelf style")),
             cl::init(LLVM));
} // namespace opts

namespace llvm {

LLVM_ATTRIBUTE_NORETURN void reportError(Twine Msg) {
  errs() << "\nError reading file: " << Msg << ".\n";
  errs().flush();
  exit(1);
}

void error(Error EC) {
  if (!EC)
    return;
  handleAllErrors(std::move(EC),
                  [&](const ErrorInfoBase &EI) { reportError(EI.message()); });
}

void error(std::error_code EC) {
  if (!EC)
    return;
  reportError(EC.message());
}

bool relocAddressLess(RelocationRef a, RelocationRef b) {
  return a.getOffset() < b.getOffset();
}

} // namespace llvm

static void reportError(StringRef Input, std::error_code EC) {
  if (Input == "-")
    Input = "<stdin>";

  reportError(Twine(Input) + ": " + EC.message());
}

static void reportError(StringRef Input, Error Err) {
  if (Input == "-")
    Input = "<stdin>";
  std::string ErrMsg;
  {
    raw_string_ostream ErrStream(ErrMsg);
    logAllUnhandledErrors(std::move(Err), ErrStream, Input + ": ");
  }
  reportError(ErrMsg);
}

static bool isMipsArch(unsigned Arch) {
  switch (Arch) {
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    return true;
  default:
    return false;
  }
}
namespace {
struct ReadObjTypeTableBuilder {
  ReadObjTypeTableBuilder()
      : Allocator(), IDTable(Allocator), TypeTable(Allocator) {}

  llvm::BumpPtrAllocator Allocator;
  llvm::codeview::MergingTypeTableBuilder IDTable;
  llvm::codeview::MergingTypeTableBuilder TypeTable;
};
}
static ReadObjTypeTableBuilder CVTypes;

/// Creates an format-specific object file dumper.
static std::error_code createDumper(const ObjectFile *Obj,
                                    ScopedPrinter &Writer,
                                    std::unique_ptr<ObjDumper> &Result) {
  if (!Obj)
    return readobj_error::unsupported_file_format;

  if (Obj->isCOFF())
    return createCOFFDumper(Obj, Writer, Result);
  if (Obj->isELF())
    return createELFDumper(Obj, Writer, Result);
  if (Obj->isMachO())
    return createMachODumper(Obj, Writer, Result);
  if (Obj->isWasm())
    return createWasmDumper(Obj, Writer, Result);

  return readobj_error::unsupported_obj_file_format;
}

/// Dumps the specified object file.
static void dumpObject(const ObjectFile *Obj, ScopedPrinter &Writer) {
  std::unique_ptr<ObjDumper> Dumper;
  if (std::error_code EC = createDumper(Obj, Writer, Dumper))
    reportError(Obj->getFileName(), EC);

  if (opts::Output == opts::LLVM) {
    Writer.startLine() << "\n";
    Writer.printString("File", Obj->getFileName());
    Writer.printString("Format", Obj->getFileFormatName());
    Writer.printString("Arch", Triple::getArchTypeName(
                                   (llvm::Triple::ArchType)Obj->getArch()));
    Writer.printString("AddressSize",
                       formatv("{0}bit", 8 * Obj->getBytesInAddress()));
    Dumper->printLoadName();
  }

  if (opts::FileHeaders)
    Dumper->printFileHeaders();
  if (opts::SectionHeaders)
    Dumper->printSectionHeaders();
  if (opts::Relocations)
    Dumper->printRelocations();
  if (opts::DynRelocs)
    Dumper->printDynamicRelocations();
  if (opts::Symbols)
    Dumper->printSymbols();
  if (opts::DynamicSymbols)
    Dumper->printDynamicSymbols();
  if (opts::UnwindInfo)
    Dumper->printUnwindInfo();
  if (opts::DynamicTable)
    Dumper->printDynamicTable();
  if (opts::NeededLibraries)
    Dumper->printNeededLibraries();
  if (opts::ProgramHeaders)
    Dumper->printProgramHeaders();
  if (!opts::StringDump.empty())
    llvm::for_each(opts::StringDump, [&Dumper, Obj](StringRef SectionName) {
      Dumper->printSectionAsString(Obj, SectionName);
    });
  if (!opts::HexDump.empty())
    llvm::for_each(opts::HexDump, [&Dumper, Obj](StringRef SectionName) {
      Dumper->printSectionAsHex(Obj, SectionName);
    });
  if (opts::HashTable)
    Dumper->printHashTable();
  if (opts::GnuHashTable)
    Dumper->printGnuHashTable();
  if (opts::VersionInfo)
    Dumper->printVersionInfo();
  if (Obj->isELF()) {
    if (opts::ELFLinkerOptions)
      Dumper->printELFLinkerOptions();
    if (Obj->getArch() == llvm::Triple::arm)
      if (opts::ARMAttributes)
        Dumper->printAttributes();
    if (isMipsArch(Obj->getArch())) {
      if (opts::MipsPLTGOT)
        Dumper->printMipsPLTGOT();
      if (opts::MipsABIFlags)
        Dumper->printMipsABIFlags();
      if (opts::MipsReginfo)
        Dumper->printMipsReginfo();
      if (opts::MipsOptions)
        Dumper->printMipsOptions();
    }
    if (opts::SectionGroups)
      Dumper->printGroupSections();
    if (opts::HashHistogram)
      Dumper->printHashHistogram();
    if (opts::CGProfile)
      Dumper->printCGProfile();
    if (opts::Addrsig)
      Dumper->printAddrsig();
    if (opts::Notes)
      Dumper->printNotes();
  }
  if (Obj->isCOFF()) {
    if (opts::COFFImports)
      Dumper->printCOFFImports();
    if (opts::COFFExports)
      Dumper->printCOFFExports();
    if (opts::COFFDirectives)
      Dumper->printCOFFDirectives();
    if (opts::COFFBaseRelocs)
      Dumper->printCOFFBaseReloc();
    if (opts::COFFDebugDirectory)
      Dumper->printCOFFDebugDirectory();
    if (opts::COFFResources)
      Dumper->printCOFFResources();
    if (opts::COFFLoadConfig)
      Dumper->printCOFFLoadConfig();
    if (opts::Addrsig)
      Dumper->printAddrsig();
    if (opts::CodeView)
      Dumper->printCodeViewDebugInfo();
    if (opts::CodeViewMergedTypes)
      Dumper->mergeCodeViewTypes(CVTypes.IDTable, CVTypes.TypeTable);
  }
  if (Obj->isMachO()) {
    if (opts::MachODataInCode)
      Dumper->printMachODataInCode();
    if (opts::MachOIndirectSymbols)
      Dumper->printMachOIndirectSymbols();
    if (opts::MachOLinkerOptions)
      Dumper->printMachOLinkerOptions();
    if (opts::MachOSegment)
      Dumper->printMachOSegment();
    if (opts::MachOVersionMin)
      Dumper->printMachOVersionMin();
    if (opts::MachODysymtab)
      Dumper->printMachODysymtab();
  }
  if (opts::PrintStackMap)
    Dumper->printStackMap();
}

/// Dumps each object file in \a Arc;
static void dumpArchive(const Archive *Arc, ScopedPrinter &Writer) {
  Error Err = Error::success();
  for (auto &Child : Arc->children(Err)) {
    Expected<std::unique_ptr<Binary>> ChildOrErr = Child.getAsBinary();
    if (!ChildOrErr) {
      if (auto E = isNotObjectErrorInvalidFileType(ChildOrErr.takeError())) {
        reportError(Arc->getFileName(), ChildOrErr.takeError());
      }
      continue;
    }
    if (ObjectFile *Obj = dyn_cast<ObjectFile>(&*ChildOrErr.get()))
      dumpObject(Obj, Writer);
    else if (COFFImportFile *Imp = dyn_cast<COFFImportFile>(&*ChildOrErr.get()))
      dumpCOFFImportFile(Imp, Writer);
    else
      reportError(Arc->getFileName(), readobj_error::unrecognized_file_format);
  }
  if (Err)
    reportError(Arc->getFileName(), std::move(Err));
}

/// Dumps each object file in \a MachO Universal Binary;
static void dumpMachOUniversalBinary(const MachOUniversalBinary *UBinary,
                                     ScopedPrinter &Writer) {
  for (const MachOUniversalBinary::ObjectForArch &Obj : UBinary->objects()) {
    Expected<std::unique_ptr<MachOObjectFile>> ObjOrErr = Obj.getAsObjectFile();
    if (ObjOrErr)
      dumpObject(&*ObjOrErr.get(), Writer);
    else if (auto E = isNotObjectErrorInvalidFileType(ObjOrErr.takeError())) {
      reportError(UBinary->getFileName(), ObjOrErr.takeError());
    }
    else if (Expected<std::unique_ptr<Archive>> AOrErr = Obj.getAsArchive())
      dumpArchive(&*AOrErr.get(), Writer);
  }
}

/// Dumps \a WinRes, Windows Resource (.res) file;
static void dumpWindowsResourceFile(WindowsResource *WinRes) {
  ScopedPrinter Printer{outs()};
  WindowsRes::Dumper Dumper(WinRes, Printer);
  if (auto Err = Dumper.printData())
    reportError(WinRes->getFileName(), std::move(Err));
}


/// Opens \a File and dumps it.
static void dumpInput(StringRef File) {
  ScopedPrinter Writer(outs());

  // Attempt to open the binary.
  Expected<OwningBinary<Binary>> BinaryOrErr = createBinary(File);
  if (!BinaryOrErr)
    reportError(File, BinaryOrErr.takeError());
  Binary &Binary = *BinaryOrErr.get().getBinary();

  if (Archive *Arc = dyn_cast<Archive>(&Binary))
    dumpArchive(Arc, Writer);
  else if (MachOUniversalBinary *UBinary =
               dyn_cast<MachOUniversalBinary>(&Binary))
    dumpMachOUniversalBinary(UBinary, Writer);
  else if (ObjectFile *Obj = dyn_cast<ObjectFile>(&Binary))
    dumpObject(Obj, Writer);
  else if (COFFImportFile *Import = dyn_cast<COFFImportFile>(&Binary))
    dumpCOFFImportFile(Import, Writer);
  else if (WindowsResource *WinRes = dyn_cast<WindowsResource>(&Binary))
    dumpWindowsResourceFile(WinRes);
  else
    reportError(File, readobj_error::unrecognized_file_format);
}

/// Registers aliases that should only be allowed by readobj.
static void registerReadobjAliases() {
  // -s has meant --sections for a very long time in llvm-readobj despite
  // meaning --symbols in readelf.
  static cl::alias SectionsShort("s", cl::desc("Alias for --section-headers"),
                                 cl::aliasopt(opts::SectionHeaders),
                                 cl::NotHidden);

  // Only register -t in llvm-readobj, as readelf reserves it for
  // --section-details (not implemented yet).
  static cl::alias SymbolsShort("t", cl::desc("Alias for --symbols"),
                                cl::aliasopt(opts::Symbols), cl::NotHidden);

  // The following two-letter aliases are only provided for readobj, as readelf
  // allows single-letter args to be grouped together.
  static cl::alias SectionRelocationsShort(
      "sr", cl::desc("Alias for --section-relocations"),
      cl::aliasopt(opts::SectionRelocations));
  static cl::alias SectionDataShort("sd", cl::desc("Alias for --section-data"),
                                    cl::aliasopt(opts::SectionData));
  static cl::alias SectionSymbolsShort("st",
                                       cl::desc("Alias for --section-symbols"),
                                       cl::aliasopt(opts::SectionSymbols));
  static cl::alias DynamicSymbolsShort("dt",
                                       cl::desc("Alias for --dyn-symbols"),
                                       cl::aliasopt(opts::DynamicSymbols));
}

/// Registers aliases that should only be allowed by readelf.
static void registerReadelfAliases() {
  // -s is here because for readobj it means --sections.
  static cl::alias SymbolsShort("s", cl::desc("Alias for --symbols"),
                                cl::aliasopt(opts::Symbols), cl::NotHidden,
                                cl::Grouping);

  // Allow all single letter flags to be grouped together.
  for (auto &OptEntry : cl::getRegisteredOptions()) {
    StringRef ArgName = OptEntry.getKey();
    cl::Option *Option = OptEntry.getValue();
    if (ArgName.size() == 1)
      Option->setFormattingFlag(cl::Grouping);
  }
}

int main(int argc, const char *argv[]) {
  InitLLVM X(argc, argv);

  // Register the target printer for --version.
  cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);

  if (sys::path::stem(argv[0]).contains("readelf")) {
    opts::Output = opts::GNU;
    registerReadelfAliases();
  } else {
    registerReadobjAliases();
  }

  cl::ParseCommandLineOptions(argc, argv, "LLVM Object Reader\n");

  if (opts::All) {
    opts::FileHeaders = true;
    opts::ProgramHeaders = true;
    opts::SectionHeaders = true;
    opts::Symbols = true;
    opts::Relocations = true;
    opts::DynamicTable = true;
    opts::Notes = true;
    opts::VersionInfo = true;
    opts::UnwindInfo = true;
    opts::SectionGroups = true;
    opts::HashHistogram = true;
  }

  if (opts::Headers) {
    opts::FileHeaders = true;
    opts::ProgramHeaders = true;
    opts::SectionHeaders = true;
  }

  // Default to stdin if no filename is specified.
  if (opts::InputFilenames.empty())
    opts::InputFilenames.push_back("-");

  llvm::for_each(opts::InputFilenames, dumpInput);

  if (opts::CodeViewMergedTypes) {
    ScopedPrinter W(outs());
    dumpCodeViewMergedTypes(W, CVTypes.IDTable, CVTypes.TypeTable);
  }

  return 0;
}
