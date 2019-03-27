//===-- llvm-dwarfdump.cpp - Debug info dumping utility for llvm ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This program is a utility that works like "dwarfdump".
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Triple.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace object;

/// Parser for options that take an optional offest argument.
/// @{
struct OffsetOption {
  uint64_t Val = 0;
  bool HasValue = false;
  bool IsRequested = false;
};

namespace llvm {
namespace cl {
template <>
class parser<OffsetOption> final : public basic_parser<OffsetOption> {
public:
  parser(Option &O) : basic_parser(O) {}

  /// Return true on error.
  bool parse(Option &O, StringRef ArgName, StringRef Arg, OffsetOption &Val) {
    if (Arg == "") {
      Val.Val = 0;
      Val.HasValue = false;
      Val.IsRequested = true;
      return false;
    }
    if (Arg.getAsInteger(0, Val.Val))
      return O.error("'" + Arg + "' value invalid for integer argument!");
    Val.HasValue = true;
    Val.IsRequested = true;
    return false;
  }

  enum ValueExpected getValueExpectedFlagDefault() const {
    return ValueOptional;
  }

  void printOptionInfo(const Option &O, size_t GlobalWidth) const {
    outs() << "  -" << O.ArgStr;
    Option::printHelpStr(O.HelpStr, GlobalWidth, getOptionWidth(O));
  }

  void printOptionDiff(const Option &O, OffsetOption V, OptVal Default,
                       size_t GlobalWidth) const {
    printOptionName(O, GlobalWidth);
    outs() << "[=offset]";
  }

  // An out-of-line virtual method to provide a 'home' for this class.
  void anchor() override {};
};
} // cl
} // llvm

/// @}
/// Command line options.
/// @{

namespace {
using namespace cl;

OptionCategory DwarfDumpCategory("Specific Options");
static opt<bool> Help("h", desc("Alias for -help"), Hidden,
                      cat(DwarfDumpCategory));
static list<std::string>
    InputFilenames(Positional, desc("<input object files or .dSYM bundles>"),
                   ZeroOrMore, cat(DwarfDumpCategory));

cl::OptionCategory SectionCategory("Section-specific Dump Options",
                                   "These control which sections are dumped. "
                                   "Where applicable these parameters take an "
                                   "optional =<offset> argument to dump only "
                                   "the entry at the specified offset.");

static opt<bool> DumpAll("all", desc("Dump all debug info sections"),
                         cat(SectionCategory));
static alias DumpAllAlias("a", desc("Alias for -all"), aliasopt(DumpAll));

// Options for dumping specific sections.
static unsigned DumpType = DIDT_Null;
static std::array<llvm::Optional<uint64_t>, (unsigned)DIDT_ID_Count>
    DumpOffsets;
#define HANDLE_DWARF_SECTION(ENUM_NAME, ELF_NAME, CMDLINE_NAME)                \
  static opt<OffsetOption> Dump##ENUM_NAME(                                    \
      CMDLINE_NAME, desc("Dump the " ELF_NAME " section"),                     \
      cat(SectionCategory));
#include "llvm/BinaryFormat/Dwarf.def"
#undef HANDLE_DWARF_SECTION

static alias DumpDebugFrameAlias("eh-frame", desc("Alias for -debug-frame"),
                                 NotHidden, cat(SectionCategory),
                                 aliasopt(DumpDebugFrame));
static list<std::string>
    ArchFilters("arch",
                desc("Dump debug information for the specified CPU "
                     "architecture only. Architectures may be specified by "
                     "name or by number. This option can be specified "
                     "multiple times, once for each desired architecture."),
                cat(DwarfDumpCategory));
static opt<bool>
    Diff("diff",
         desc("Emit diff-friendly output by omitting offsets and addresses."),
         cat(DwarfDumpCategory));
static list<std::string>
    Find("find",
         desc("Search for the exact match for <name> in the accelerator tables "
              "and print the matching debug information entries. When no "
              "accelerator tables are available, the slower but more complete "
              "-name option can be used instead."),
         value_desc("name"), cat(DwarfDumpCategory));
static alias FindAlias("f", desc("Alias for -find."), aliasopt(Find));
static opt<bool>
    IgnoreCase("ignore-case",
               desc("Ignore case distinctions in when searching by name."),
               value_desc("i"), cat(DwarfDumpCategory));
static alias IgnoreCaseAlias("i", desc("Alias for -ignore-case."),
                             aliasopt(IgnoreCase));
static list<std::string> Name(
    "name",
    desc("Find and print all debug info entries whose name (DW_AT_name "
         "attribute) matches the exact text in <pattern>.  When used with the "
         "the -regex option <pattern> is interpreted as a regular expression."),
    value_desc("pattern"), cat(DwarfDumpCategory));
static alias NameAlias("n", desc("Alias for -name"), aliasopt(Name));
static opt<unsigned long long> Lookup("lookup",
           desc("Lookup <address> in the debug information and print out any "
                "available file, function, block and line table details."),
           value_desc("address"), cat(DwarfDumpCategory));
static opt<std::string>
    OutputFilename("out-file", cl::init(""),
                   cl::desc("Redirect output to the specified file."),
                   cl::value_desc("filename"));
static alias OutputFilenameAlias("o", desc("Alias for -out-file."),
                                 aliasopt(OutputFilename),
                                 cat(DwarfDumpCategory));
static opt<bool>
    UseRegex("regex",
             desc("Treat any <pattern> strings as regular expressions when "
                  "searching instead of just as an exact string match."),
             cat(DwarfDumpCategory));
static alias RegexAlias("x", desc("Alias for -regex"), aliasopt(UseRegex));
static opt<bool>
    ShowChildren("show-children",
                 desc("Show a debug info entry's children when selectively "
                      "printing with the =<offset> option."),
                 cat(DwarfDumpCategory));
static alias ShowChildrenAlias("c", desc("Alias for -show-children."),
                               aliasopt(ShowChildren));
static opt<bool>
    ShowParents("show-parents",
                desc("Show a debug info entry's parents when selectively "
                     "printing with the =<offset> option."),
                cat(DwarfDumpCategory));
static alias ShowParentsAlias("p", desc("Alias for -show-parents."),
                              aliasopt(ShowParents));
static opt<bool>
    ShowForm("show-form",
             desc("Show DWARF form types after the DWARF attribute types."),
             cat(DwarfDumpCategory));
static alias ShowFormAlias("F", desc("Alias for -show-form."),
                           aliasopt(ShowForm), cat(DwarfDumpCategory));
static opt<unsigned> RecurseDepth(
    "recurse-depth",
    desc("Only recurse to a depth of N when displaying debug info entries."),
    cat(DwarfDumpCategory), init(-1U), value_desc("N"));
static alias RecurseDepthAlias("r", desc("Alias for -recurse-depth."),
                               aliasopt(RecurseDepth));

static opt<bool>
    SummarizeTypes("summarize-types",
                   desc("Abbreviate the description of type unit entries."),
                   cat(DwarfDumpCategory));
static cl::opt<bool>
    Statistics("statistics",
               cl::desc("Emit JSON-formatted debug info quality metrics."),
               cat(DwarfDumpCategory));
static opt<bool> Verify("verify", desc("Verify the DWARF debug info."),
                        cat(DwarfDumpCategory));
static opt<bool> Quiet("quiet", desc("Use with -verify to not emit to STDOUT."),
                       cat(DwarfDumpCategory));
static opt<bool> DumpUUID("uuid", desc("Show the UUID for each architecture."),
                          cat(DwarfDumpCategory));
static alias DumpUUIDAlias("u", desc("Alias for -uuid."), aliasopt(DumpUUID));
static opt<bool> Verbose("verbose",
                         desc("Print more low-level encoding details."),
                         cat(DwarfDumpCategory));
static alias VerboseAlias("v", desc("Alias for -verbose."), aliasopt(Verbose),
                          cat(DwarfDumpCategory));
} // namespace
/// @}
//===----------------------------------------------------------------------===//

static void error(StringRef Prefix, std::error_code EC) {
  if (!EC)
    return;
  WithColor::error() << Prefix << ": " << EC.message() << "\n";
  exit(1);
}

static DIDumpOptions getDumpOpts() {
  DIDumpOptions DumpOpts;
  DumpOpts.DumpType = DumpType;
  DumpOpts.RecurseDepth = RecurseDepth;
  DumpOpts.ShowAddresses = !Diff;
  DumpOpts.ShowChildren = ShowChildren;
  DumpOpts.ShowParents = ShowParents;
  DumpOpts.ShowForm = ShowForm;
  DumpOpts.SummarizeTypes = SummarizeTypes;
  DumpOpts.Verbose = Verbose;
  // In -verify mode, print DIEs without children in error messages.
  if (Verify)
    return DumpOpts.noImplicitRecursion();
  return DumpOpts;
}

static uint32_t getCPUType(MachOObjectFile &MachO) {
  if (MachO.is64Bit())
    return MachO.getHeader64().cputype;
  else
    return MachO.getHeader().cputype;
}

/// Return true if the object file has not been filtered by an --arch option.
static bool filterArch(ObjectFile &Obj) {
  if (ArchFilters.empty())
    return true;

  if (auto *MachO = dyn_cast<MachOObjectFile>(&Obj)) {
    std::string ObjArch =
        Triple::getArchTypeName(MachO->getArchTriple().getArch());

    for (auto Arch : ArchFilters) {
      // Match name.
      if (Arch == ObjArch)
        return true;

      // Match architecture number.
      unsigned Value;
      if (!StringRef(Arch).getAsInteger(0, Value))
        if (Value == getCPUType(*MachO))
          return true;
    }
  }
  return false;
}

using HandlerFn = std::function<bool(ObjectFile &, DWARFContext &DICtx, Twine,
                                     raw_ostream &)>;

/// Print only DIEs that have a certain name.
static bool filterByName(const StringSet<> &Names, DWARFDie Die,
                         StringRef NameRef, raw_ostream &OS) {
  std::string Name =
      (IgnoreCase && !UseRegex) ? NameRef.lower() : NameRef.str();
  if (UseRegex) {
    // Match regular expression.
    for (auto Pattern : Names.keys()) {
      Regex RE(Pattern, IgnoreCase ? Regex::IgnoreCase : Regex::NoFlags);
      std::string Error;
      if (!RE.isValid(Error)) {
        errs() << "error in regular expression: " << Error << "\n";
        exit(1);
      }
      if (RE.match(Name)) {
        Die.dump(OS, 0, getDumpOpts());
        return true;
      }
    }
  } else if (Names.count(Name)) {
    // Match full text.
    Die.dump(OS, 0, getDumpOpts());
    return true;
  }
  return false;
}

/// Print only DIEs that have a certain name.
static void filterByName(const StringSet<> &Names,
                         DWARFContext::unit_iterator_range CUs,
                         raw_ostream &OS) {
  for (const auto &CU : CUs)
    for (const auto &Entry : CU->dies()) {
      DWARFDie Die = {CU.get(), &Entry};
      if (const char *Name = Die.getName(DINameKind::ShortName))
        if (filterByName(Names, Die, Name, OS))
          continue;
      if (const char *Name = Die.getName(DINameKind::LinkageName))
        filterByName(Names, Die, Name, OS);
    }
}

static void getDies(DWARFContext &DICtx, const AppleAcceleratorTable &Accel,
                    StringRef Name, SmallVectorImpl<DWARFDie> &Dies) {
  for (const auto &Entry : Accel.equal_range(Name)) {
    if (llvm::Optional<uint64_t> Off = Entry.getDIESectionOffset()) {
      if (DWARFDie Die = DICtx.getDIEForOffset(*Off))
        Dies.push_back(Die);
    }
  }
}

static DWARFDie toDie(const DWARFDebugNames::Entry &Entry,
                      DWARFContext &DICtx) {
  llvm::Optional<uint64_t> CUOff = Entry.getCUOffset();
  llvm::Optional<uint64_t> Off = Entry.getDIEUnitOffset();
  if (!CUOff || !Off)
    return DWARFDie();

  DWARFCompileUnit *CU = DICtx.getCompileUnitForOffset(*CUOff);
  if (!CU)
    return DWARFDie();

  if (llvm::Optional<uint64_t> DWOId = CU->getDWOId()) {
    // This is a skeleton unit. Look up the DIE in the DWO unit.
    CU = DICtx.getDWOCompileUnitForHash(*DWOId);
    if (!CU)
      return DWARFDie();
  }

  return CU->getDIEForOffset(CU->getOffset() + *Off);
}

static void getDies(DWARFContext &DICtx, const DWARFDebugNames &Accel,
                    StringRef Name, SmallVectorImpl<DWARFDie> &Dies) {
  for (const auto &Entry : Accel.equal_range(Name)) {
    if (DWARFDie Die = toDie(Entry, DICtx))
      Dies.push_back(Die);
  }
}

/// Print only DIEs that have a certain name.
static void filterByAccelName(ArrayRef<std::string> Names, DWARFContext &DICtx,
                              raw_ostream &OS) {
  SmallVector<DWARFDie, 4> Dies;
  for (const auto &Name : Names) {
    getDies(DICtx, DICtx.getAppleNames(), Name, Dies);
    getDies(DICtx, DICtx.getAppleTypes(), Name, Dies);
    getDies(DICtx, DICtx.getAppleNamespaces(), Name, Dies);
    getDies(DICtx, DICtx.getDebugNames(), Name, Dies);
  }
  llvm::sort(Dies);
  Dies.erase(std::unique(Dies.begin(), Dies.end()), Dies.end());

  for (DWARFDie Die : Dies)
    Die.dump(OS, 0, getDumpOpts());
}

/// Handle the --lookup option and dump the DIEs and line info for the given
/// address.
static bool lookup(DWARFContext &DICtx, uint64_t Address, raw_ostream &OS) {
  auto DIEsForAddr = DICtx.getDIEsForAddress(Lookup);

  if (!DIEsForAddr)
    return false;

  DIDumpOptions DumpOpts = getDumpOpts();
  DumpOpts.RecurseDepth = 0;
  DIEsForAddr.CompileUnit->dump(OS, DumpOpts);
  if (DIEsForAddr.FunctionDIE) {
    DIEsForAddr.FunctionDIE.dump(OS, 2, DumpOpts);
    if (DIEsForAddr.BlockDIE)
      DIEsForAddr.BlockDIE.dump(OS, 4, DumpOpts);
  }

  if (DILineInfo LineInfo = DICtx.getLineInfoForAddress(Lookup))
    LineInfo.dump(OS);

  return true;
}

bool collectStatsForObjectFile(ObjectFile &Obj, DWARFContext &DICtx,
                               Twine Filename, raw_ostream &OS);

static bool dumpObjectFile(ObjectFile &Obj, DWARFContext &DICtx, Twine Filename,
                           raw_ostream &OS) {
  logAllUnhandledErrors(DICtx.loadRegisterInfo(Obj), errs(),
                        Filename.str() + ": ");
  // The UUID dump already contains all the same information.
  if (!(DumpType & DIDT_UUID) || DumpType == DIDT_All)
    OS << Filename << ":\tfile format " << Obj.getFileFormatName() << '\n';

  // Handle the --lookup option.
  if (Lookup)
    return lookup(DICtx, Lookup, OS);

  // Handle the --name option.
  if (!Name.empty()) {
    StringSet<> Names;
    for (auto name : Name)
      Names.insert((IgnoreCase && !UseRegex) ? StringRef(name).lower() : name);

    filterByName(Names, DICtx.normal_units(), OS);
    filterByName(Names, DICtx.dwo_units(), OS);
    return true;
  }

  // Handle the --find option and lower it to --debug-info=<offset>.
  if (!Find.empty()) {
    filterByAccelName(Find, DICtx, OS);
    return true;
  }

  // Dump the complete DWARF structure.
  DICtx.dump(OS, getDumpOpts(), DumpOffsets);
  return true;
}

static bool verifyObjectFile(ObjectFile &Obj, DWARFContext &DICtx,
                             Twine Filename, raw_ostream &OS) {
  // Verify the DWARF and exit with non-zero exit status if verification
  // fails.
  raw_ostream &stream = Quiet ? nulls() : OS;
  stream << "Verifying " << Filename.str() << ":\tfile format "
  << Obj.getFileFormatName() << "\n";
  bool Result = DICtx.verify(stream, getDumpOpts());
  if (Result)
    stream << "No errors.\n";
  else
    stream << "Errors detected.\n";
  return Result;
}

static bool handleBuffer(StringRef Filename, MemoryBufferRef Buffer,
                         HandlerFn HandleObj, raw_ostream &OS);

static bool handleArchive(StringRef Filename, Archive &Arch,
                          HandlerFn HandleObj, raw_ostream &OS) {
  bool Result = true;
  Error Err = Error::success();
  for (auto Child : Arch.children(Err)) {
    auto BuffOrErr = Child.getMemoryBufferRef();
    error(Filename, errorToErrorCode(BuffOrErr.takeError()));
    auto NameOrErr = Child.getName();
    error(Filename, errorToErrorCode(NameOrErr.takeError()));
    std::string Name = (Filename + "(" + NameOrErr.get() + ")").str();
    Result &= handleBuffer(Name, BuffOrErr.get(), HandleObj, OS);
  }
  error(Filename, errorToErrorCode(std::move(Err)));

  return Result;
}

static bool handleBuffer(StringRef Filename, MemoryBufferRef Buffer,
                         HandlerFn HandleObj, raw_ostream &OS) {
  Expected<std::unique_ptr<Binary>> BinOrErr = object::createBinary(Buffer);
  error(Filename, errorToErrorCode(BinOrErr.takeError()));

  bool Result = true;
  if (auto *Obj = dyn_cast<ObjectFile>(BinOrErr->get())) {
    if (filterArch(*Obj)) {
      std::unique_ptr<DWARFContext> DICtx = DWARFContext::create(*Obj);
      Result = HandleObj(*Obj, *DICtx, Filename, OS);
    }
  }
  else if (auto *Fat = dyn_cast<MachOUniversalBinary>(BinOrErr->get()))
    for (auto &ObjForArch : Fat->objects()) {
      std::string ObjName =
          (Filename + "(" + ObjForArch.getArchFlagName() + ")").str();
      if (auto MachOOrErr = ObjForArch.getAsObjectFile()) {
        auto &Obj = **MachOOrErr;
        if (filterArch(Obj)) {
          std::unique_ptr<DWARFContext> DICtx = DWARFContext::create(Obj);
          Result &= HandleObj(Obj, *DICtx, ObjName, OS);
        }
        continue;
      } else
        consumeError(MachOOrErr.takeError());
      if (auto ArchiveOrErr = ObjForArch.getAsArchive()) {
        error(ObjName, errorToErrorCode(ArchiveOrErr.takeError()));
        Result &= handleArchive(ObjName, *ArchiveOrErr.get(), HandleObj, OS);
        continue;
      } else
        consumeError(ArchiveOrErr.takeError());
    }
  else if (auto *Arch = dyn_cast<Archive>(BinOrErr->get()))
    Result = handleArchive(Filename, *Arch, HandleObj, OS);
  return Result;
}

static bool handleFile(StringRef Filename, HandlerFn HandleObj,
                       raw_ostream &OS) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BuffOrErr =
  MemoryBuffer::getFileOrSTDIN(Filename);
  error(Filename, BuffOrErr.getError());
  std::unique_ptr<MemoryBuffer> Buffer = std::move(BuffOrErr.get());
  return handleBuffer(Filename, *Buffer, HandleObj, OS);
}

/// If the input path is a .dSYM bundle (as created by the dsymutil tool),
/// replace it with individual entries for each of the object files inside the
/// bundle otherwise return the input path.
static std::vector<std::string> expandBundle(const std::string &InputPath) {
  std::vector<std::string> BundlePaths;
  SmallString<256> BundlePath(InputPath);
  // Normalize input path. This is necessary to accept `bundle.dSYM/`.
  sys::path::remove_dots(BundlePath);
  // Manually open up the bundle to avoid introducing additional dependencies.
  if (sys::fs::is_directory(BundlePath) &&
      sys::path::extension(BundlePath) == ".dSYM") {
    std::error_code EC;
    sys::path::append(BundlePath, "Contents", "Resources", "DWARF");
    for (sys::fs::directory_iterator Dir(BundlePath, EC), DirEnd;
         Dir != DirEnd && !EC; Dir.increment(EC)) {
      const std::string &Path = Dir->path();
      sys::fs::file_status Status;
      EC = sys::fs::status(Path, Status);
      error(Path, EC);
      switch (Status.type()) {
      case sys::fs::file_type::regular_file:
      case sys::fs::file_type::symlink_file:
      case sys::fs::file_type::type_unknown:
        BundlePaths.push_back(Path);
        break;
      default: /*ignore*/;
      }
    }
    error(BundlePath, EC);
  }
  if (!BundlePaths.size())
    BundlePaths.push_back(InputPath);
  return BundlePaths;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();

  HideUnrelatedOptions({&DwarfDumpCategory, &SectionCategory, &ColorCategory});
  cl::ParseCommandLineOptions(
      argc, argv,
      "pretty-print DWARF debug information in object files"
      " and debug info archives.\n");

  if (Help) {
    PrintHelpMessage(/*Hidden =*/false, /*Categorized =*/true);
    return 0;
  }

  // FIXME: Audit interactions between these two options and make them
  //        compatible.
  if (Diff && Verbose) {
    WithColor::error() << "incompatible arguments: specifying both -diff and "
                          "-verbose is currently not supported";
    return 0;
  }

  std::unique_ptr<ToolOutputFile> OutputFile;
  if (!OutputFilename.empty()) {
    std::error_code EC;
    OutputFile = llvm::make_unique<ToolOutputFile>(OutputFilename, EC,
                                                     sys::fs::F_None);
    error("Unable to open output file" + OutputFilename, EC);
    // Don't remove output file if we exit with an error.
    OutputFile->keep();
  }

  raw_ostream &OS = OutputFile ? OutputFile->os() : outs();
  bool OffsetRequested = false;

  // Defaults to dumping all sections, unless brief mode is specified in which
  // case only the .debug_info section in dumped.
#define HANDLE_DWARF_SECTION(ENUM_NAME, ELF_NAME, CMDLINE_NAME)                \
  if (Dump##ENUM_NAME.IsRequested) {                                           \
    DumpType |= DIDT_##ENUM_NAME;                                              \
    if (Dump##ENUM_NAME.HasValue) {                                            \
      DumpOffsets[DIDT_ID_##ENUM_NAME] = Dump##ENUM_NAME.Val;                  \
      OffsetRequested = true;                                                  \
    }                                                                          \
  }
#include "llvm/BinaryFormat/Dwarf.def"
#undef HANDLE_DWARF_SECTION
  if (DumpUUID)
    DumpType |= DIDT_UUID;
  if (DumpAll)
    DumpType = DIDT_All;
  if (DumpType == DIDT_Null) {
    if (Verbose)
      DumpType = DIDT_All;
    else
      DumpType = DIDT_DebugInfo;
  }

  // Unless dumping a specific DIE, default to --show-children.
  if (!ShowChildren && !Verify && !OffsetRequested && Name.empty() && Find.empty())
    ShowChildren = true;

  // Defaults to a.out if no filenames specified.
  if (InputFilenames.empty())
    InputFilenames.push_back("a.out");

  // Expand any .dSYM bundles to the individual object files contained therein.
  std::vector<std::string> Objects;
  for (const auto &F : InputFilenames) {
    auto Objs = expandBundle(F);
    Objects.insert(Objects.end(), Objs.begin(), Objs.end());
  }

  if (Verify) {
    // If we encountered errors during verify, exit with a non-zero exit status.
    if (!all_of(Objects, [&](std::string Object) {
          return handleFile(Object, verifyObjectFile, OS);
        }))
      exit(1);
  } else if (Statistics)
    for (auto Object : Objects)
      handleFile(Object, collectStatsForObjectFile, OS);
  else
    for (auto Object : Objects)
      handleFile(Object, dumpObjectFile, OS);

  return EXIT_SUCCESS;
}
