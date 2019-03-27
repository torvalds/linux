//===- Driver.cpp ---------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The driver drives the entire linking process. It is responsible for
// parsing command line options and doing whatever it is instructed to do.
//
// One notable thing in the LLD's driver when compared to other linkers is
// that the LLD's driver is agnostic on the host operating system.
// Other linkers usually have implicit default values (such as a dynamic
// linker path or library paths) for each host OS.
//
// I don't think implicit default values are useful because they are
// usually explicitly specified by the compiler driver. They can even
// be harmful when you are doing cross-linking. Therefore, in LLD, we
// simply trust the compiler driver to pass all required options and
// don't try to make effort on our side.
//
//===----------------------------------------------------------------------===//

#include "Driver.h"
#include "Config.h"
#include "Filesystem.h"
#include "ICF.h"
#include "InputFiles.h"
#include "InputSection.h"
#include "LinkerScript.h"
#include "MarkLive.h"
#include "OutputSections.h"
#include "ScriptParser.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "Writer.h"
#include "lld/Common/Args.h"
#include "lld/Common/Driver.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Strings.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "lld/Common/Threads.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TarWriter.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>
#include <utility>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::sys;
using namespace llvm::support;

using namespace lld;
using namespace lld::elf;

Configuration *elf::Config;
LinkerDriver *elf::Driver;

static void setConfigs(opt::InputArgList &Args);

bool elf::link(ArrayRef<const char *> Args, bool CanExitEarly,
               raw_ostream &Error) {
  errorHandler().LogName = args::getFilenameWithoutExe(Args[0]);
  errorHandler().ErrorLimitExceededMsg =
      "too many errors emitted, stopping now (use "
      "-error-limit=0 to see all errors)";
  errorHandler().ErrorOS = &Error;
  errorHandler().ExitEarly = CanExitEarly;
  errorHandler().ColorDiagnostics = Error.has_colors();

  InputSections.clear();
  OutputSections.clear();
  BinaryFiles.clear();
  BitcodeFiles.clear();
  ObjectFiles.clear();
  SharedFiles.clear();

  Config = make<Configuration>();
  Driver = make<LinkerDriver>();
  Script = make<LinkerScript>();
  Symtab = make<SymbolTable>();

  Tar = nullptr;
  memset(&In, 0, sizeof(In));

  Config->ProgName = Args[0];

  Driver->main(Args);

  // Exit immediately if we don't need to return to the caller.
  // This saves time because the overhead of calling destructors
  // for all globally-allocated objects is not negligible.
  if (CanExitEarly)
    exitLld(errorCount() ? 1 : 0);

  freeArena();
  return !errorCount();
}

// Parses a linker -m option.
static std::tuple<ELFKind, uint16_t, uint8_t> parseEmulation(StringRef Emul) {
  uint8_t OSABI = 0;
  StringRef S = Emul;
  if (S.endswith("_fbsd")) {
    S = S.drop_back(5);
    OSABI = ELFOSABI_FREEBSD;
  }

  std::pair<ELFKind, uint16_t> Ret =
      StringSwitch<std::pair<ELFKind, uint16_t>>(S)
          .Cases("aarch64elf", "aarch64linux", "aarch64_elf64_le_vec",
                 {ELF64LEKind, EM_AARCH64})
          .Cases("armelf", "armelf_linux_eabi", {ELF32LEKind, EM_ARM})
          .Case("elf32_x86_64", {ELF32LEKind, EM_X86_64})
          .Cases("elf32btsmip", "elf32btsmipn32", {ELF32BEKind, EM_MIPS})
          .Cases("elf32ltsmip", "elf32ltsmipn32", {ELF32LEKind, EM_MIPS})
          .Case("elf32lriscv", {ELF32LEKind, EM_RISCV})
          .Cases("elf32ppc", "elf32ppclinux", {ELF32BEKind, EM_PPC})
          .Case("elf64btsmip", {ELF64BEKind, EM_MIPS})
          .Case("elf64ltsmip", {ELF64LEKind, EM_MIPS})
          .Case("elf64lriscv", {ELF64LEKind, EM_RISCV})
          .Case("elf64ppc", {ELF64BEKind, EM_PPC64})
          .Case("elf64lppc", {ELF64LEKind, EM_PPC64})
          .Cases("elf_amd64", "elf_x86_64", {ELF64LEKind, EM_X86_64})
          .Case("elf_i386", {ELF32LEKind, EM_386})
          .Case("elf_iamcu", {ELF32LEKind, EM_IAMCU})
          .Default({ELFNoneKind, EM_NONE});

  if (Ret.first == ELFNoneKind)
    error("unknown emulation: " + Emul);
  return std::make_tuple(Ret.first, Ret.second, OSABI);
}

// Returns slices of MB by parsing MB as an archive file.
// Each slice consists of a member file in the archive.
std::vector<std::pair<MemoryBufferRef, uint64_t>> static getArchiveMembers(
    MemoryBufferRef MB) {
  std::unique_ptr<Archive> File =
      CHECK(Archive::create(MB),
            MB.getBufferIdentifier() + ": failed to parse archive");

  std::vector<std::pair<MemoryBufferRef, uint64_t>> V;
  Error Err = Error::success();
  bool AddToTar = File->isThin() && Tar;
  for (const ErrorOr<Archive::Child> &COrErr : File->children(Err)) {
    Archive::Child C =
        CHECK(COrErr, MB.getBufferIdentifier() +
                          ": could not get the child of the archive");
    MemoryBufferRef MBRef =
        CHECK(C.getMemoryBufferRef(),
              MB.getBufferIdentifier() +
                  ": could not get the buffer for a child of the archive");
    if (AddToTar)
      Tar->append(relativeToRoot(check(C.getFullName())), MBRef.getBuffer());
    V.push_back(std::make_pair(MBRef, C.getChildOffset()));
  }
  if (Err)
    fatal(MB.getBufferIdentifier() + ": Archive::children failed: " +
          toString(std::move(Err)));

  // Take ownership of memory buffers created for members of thin archives.
  for (std::unique_ptr<MemoryBuffer> &MB : File->takeThinBuffers())
    make<std::unique_ptr<MemoryBuffer>>(std::move(MB));

  return V;
}

// Opens a file and create a file object. Path has to be resolved already.
void LinkerDriver::addFile(StringRef Path, bool WithLOption) {
  using namespace sys::fs;

  Optional<MemoryBufferRef> Buffer = readFile(Path);
  if (!Buffer.hasValue())
    return;
  MemoryBufferRef MBRef = *Buffer;

  if (Config->FormatBinary) {
    Files.push_back(make<BinaryFile>(MBRef));
    return;
  }

  switch (identify_magic(MBRef.getBuffer())) {
  case file_magic::unknown:
    readLinkerScript(MBRef);
    return;
  case file_magic::archive: {
    // Handle -whole-archive.
    if (InWholeArchive) {
      for (const auto &P : getArchiveMembers(MBRef))
        Files.push_back(createObjectFile(P.first, Path, P.second));
      return;
    }

    std::unique_ptr<Archive> File =
        CHECK(Archive::create(MBRef), Path + ": failed to parse archive");

    // If an archive file has no symbol table, it is likely that a user
    // is attempting LTO and using a default ar command that doesn't
    // understand the LLVM bitcode file. It is a pretty common error, so
    // we'll handle it as if it had a symbol table.
    if (!File->isEmpty() && !File->hasSymbolTable()) {
      for (const auto &P : getArchiveMembers(MBRef))
        Files.push_back(make<LazyObjFile>(P.first, Path, P.second));
      return;
    }

    // Handle the regular case.
    Files.push_back(make<ArchiveFile>(std::move(File)));
    return;
  }
  case file_magic::elf_shared_object:
    if (Config->Static || Config->Relocatable) {
      error("attempted static link of dynamic object " + Path);
      return;
    }

    // DSOs usually have DT_SONAME tags in their ELF headers, and the
    // sonames are used to identify DSOs. But if they are missing,
    // they are identified by filenames. We don't know whether the new
    // file has a DT_SONAME or not because we haven't parsed it yet.
    // Here, we set the default soname for the file because we might
    // need it later.
    //
    // If a file was specified by -lfoo, the directory part is not
    // significant, as a user did not specify it. This behavior is
    // compatible with GNU.
    Files.push_back(
        createSharedFile(MBRef, WithLOption ? path::filename(Path) : Path));
    return;
  case file_magic::bitcode:
  case file_magic::elf_relocatable:
    if (InLib)
      Files.push_back(make<LazyObjFile>(MBRef, "", 0));
    else
      Files.push_back(createObjectFile(MBRef));
    break;
  default:
    error(Path + ": unknown file type");
  }
}

// Add a given library by searching it from input search paths.
void LinkerDriver::addLibrary(StringRef Name) {
  if (Optional<std::string> Path = searchLibrary(Name))
    addFile(*Path, /*WithLOption=*/true);
  else
    error("unable to find library -l" + Name);
}

// This function is called on startup. We need this for LTO since
// LTO calls LLVM functions to compile bitcode files to native code.
// Technically this can be delayed until we read bitcode files, but
// we don't bother to do lazily because the initialization is fast.
static void initLLVM() {
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();
}

// Some command line options or some combinations of them are not allowed.
// This function checks for such errors.
static void checkOptions() {
  // The MIPS ABI as of 2016 does not support the GNU-style symbol lookup
  // table which is a relatively new feature.
  if (Config->EMachine == EM_MIPS && Config->GnuHash)
    error("the .gnu.hash section is not compatible with the MIPS target");

  if (Config->FixCortexA53Errata843419 && Config->EMachine != EM_AARCH64)
    error("--fix-cortex-a53-843419 is only supported on AArch64 targets");

  if (Config->TocOptimize && Config->EMachine != EM_PPC64)
    error("--toc-optimize is only supported on the PowerPC64 target");

  if (Config->Pie && Config->Shared)
    error("-shared and -pie may not be used together");

  if (!Config->Shared && !Config->FilterList.empty())
    error("-F may not be used without -shared");

  if (!Config->Shared && !Config->AuxiliaryList.empty())
    error("-f may not be used without -shared");

  if (!Config->Relocatable && !Config->DefineCommon)
    error("-no-define-common not supported in non relocatable output");

  if (Config->Relocatable) {
    if (Config->Shared)
      error("-r and -shared may not be used together");
    if (Config->GcSections)
      error("-r and --gc-sections may not be used together");
    if (Config->GdbIndex)
      error("-r and --gdb-index may not be used together");
    if (Config->ICF != ICFLevel::None)
      error("-r and --icf may not be used together");
    if (Config->Pie)
      error("-r and -pie may not be used together");
  }

  if (Config->ExecuteOnly) {
    if (Config->EMachine != EM_AARCH64)
      error("-execute-only is only supported on AArch64 targets");

    if (Config->SingleRoRx && !Script->HasSectionsCommand)
      error("-execute-only and -no-rosegment cannot be used together");
  }
}

static const char *getReproduceOption(opt::InputArgList &Args) {
  if (auto *Arg = Args.getLastArg(OPT_reproduce))
    return Arg->getValue();
  return getenv("LLD_REPRODUCE");
}

static bool hasZOption(opt::InputArgList &Args, StringRef Key) {
  for (auto *Arg : Args.filtered(OPT_z))
    if (Key == Arg->getValue())
      return true;
  return false;
}

static bool getZFlag(opt::InputArgList &Args, StringRef K1, StringRef K2,
                     bool Default) {
  for (auto *Arg : Args.filtered_reverse(OPT_z)) {
    if (K1 == Arg->getValue())
      return true;
    if (K2 == Arg->getValue())
      return false;
  }
  return Default;
}

static bool isKnownZFlag(StringRef S) {
  return S == "combreloc" || S == "copyreloc" || S == "defs" ||
         S == "execstack" || S == "global" || S == "hazardplt" ||
         S == "ifunc-noplt" ||
         S == "initfirst" || S == "interpose" ||
         S == "keep-text-section-prefix" || S == "lazy" || S == "muldefs" ||
         S == "nocombreloc" || S == "nocopyreloc" || S == "nodefaultlib" ||
         S == "nodelete" || S == "nodlopen" || S == "noexecstack" ||
         S == "nokeep-text-section-prefix" || S == "norelro" || S == "notext" ||
         S == "now" || S == "origin" || S == "relro" || S == "retpolineplt" ||
         S == "rodynamic" || S == "text" || S == "wxneeded" ||
         S.startswith("max-page-size=") || S.startswith("stack-size=");
}

// Report an error for an unknown -z option.
static void checkZOptions(opt::InputArgList &Args) {
  for (auto *Arg : Args.filtered(OPT_z))
    if (!isKnownZFlag(Arg->getValue()))
      error("unknown -z value: " + StringRef(Arg->getValue()));
}

void LinkerDriver::main(ArrayRef<const char *> ArgsArr) {
  ELFOptTable Parser;
  opt::InputArgList Args = Parser.parse(ArgsArr.slice(1));

  // Interpret this flag early because error() depends on them.
  errorHandler().ErrorLimit = args::getInteger(Args, OPT_error_limit, 20);

  // Handle -help
  if (Args.hasArg(OPT_help)) {
    printHelp();
    return;
  }

  // Handle -v or -version.
  //
  // A note about "compatible with GNU linkers" message: this is a hack for
  // scripts generated by GNU Libtool 2.4.6 (released in February 2014 and
  // still the newest version in March 2017) or earlier to recognize LLD as
  // a GNU compatible linker. As long as an output for the -v option
  // contains "GNU" or "with BFD", they recognize us as GNU-compatible.
  //
  // This is somewhat ugly hack, but in reality, we had no choice other
  // than doing this. Considering the very long release cycle of Libtool,
  // it is not easy to improve it to recognize LLD as a GNU compatible
  // linker in a timely manner. Even if we can make it, there are still a
  // lot of "configure" scripts out there that are generated by old version
  // of Libtool. We cannot convince every software developer to migrate to
  // the latest version and re-generate scripts. So we have this hack.
  if (Args.hasArg(OPT_v) || Args.hasArg(OPT_version))
    message(getLLDVersion() + " (compatible with GNU linkers)");

  if (const char *Path = getReproduceOption(Args)) {
    // Note that --reproduce is a debug option so you can ignore it
    // if you are trying to understand the whole picture of the code.
    Expected<std::unique_ptr<TarWriter>> ErrOrWriter =
        TarWriter::create(Path, path::stem(Path));
    if (ErrOrWriter) {
      Tar = std::move(*ErrOrWriter);
      Tar->append("response.txt", createResponseFile(Args));
      Tar->append("version.txt", getLLDVersion() + "\n");
    } else {
      error("--reproduce: " + toString(ErrOrWriter.takeError()));
    }
  }

  readConfigs(Args);
  checkZOptions(Args);

  // The behavior of -v or --version is a bit strange, but this is
  // needed for compatibility with GNU linkers.
  if (Args.hasArg(OPT_v) && !Args.hasArg(OPT_INPUT))
    return;
  if (Args.hasArg(OPT_version))
    return;

  initLLVM();
  createFiles(Args);
  if (errorCount())
    return;

  inferMachineType();
  setConfigs(Args);
  checkOptions();
  if (errorCount())
    return;

  switch (Config->EKind) {
  case ELF32LEKind:
    link<ELF32LE>(Args);
    return;
  case ELF32BEKind:
    link<ELF32BE>(Args);
    return;
  case ELF64LEKind:
    link<ELF64LE>(Args);
    return;
  case ELF64BEKind:
    link<ELF64BE>(Args);
    return;
  default:
    llvm_unreachable("unknown Config->EKind");
  }
}

static std::string getRpath(opt::InputArgList &Args) {
  std::vector<StringRef> V = args::getStrings(Args, OPT_rpath);
  return llvm::join(V.begin(), V.end(), ":");
}

// Determines what we should do if there are remaining unresolved
// symbols after the name resolution.
static UnresolvedPolicy getUnresolvedSymbolPolicy(opt::InputArgList &Args) {
  UnresolvedPolicy ErrorOrWarn = Args.hasFlag(OPT_error_unresolved_symbols,
                                              OPT_warn_unresolved_symbols, true)
                                     ? UnresolvedPolicy::ReportError
                                     : UnresolvedPolicy::Warn;

  // Process the last of -unresolved-symbols, -no-undefined or -z defs.
  for (auto *Arg : llvm::reverse(Args)) {
    switch (Arg->getOption().getID()) {
    case OPT_unresolved_symbols: {
      StringRef S = Arg->getValue();
      if (S == "ignore-all" || S == "ignore-in-object-files")
        return UnresolvedPolicy::Ignore;
      if (S == "ignore-in-shared-libs" || S == "report-all")
        return ErrorOrWarn;
      error("unknown --unresolved-symbols value: " + S);
      continue;
    }
    case OPT_no_undefined:
      return ErrorOrWarn;
    case OPT_z:
      if (StringRef(Arg->getValue()) == "defs")
        return ErrorOrWarn;
      continue;
    }
  }

  // -shared implies -unresolved-symbols=ignore-all because missing
  // symbols are likely to be resolved at runtime using other DSOs.
  if (Config->Shared)
    return UnresolvedPolicy::Ignore;
  return ErrorOrWarn;
}

static Target2Policy getTarget2(opt::InputArgList &Args) {
  StringRef S = Args.getLastArgValue(OPT_target2, "got-rel");
  if (S == "rel")
    return Target2Policy::Rel;
  if (S == "abs")
    return Target2Policy::Abs;
  if (S == "got-rel")
    return Target2Policy::GotRel;
  error("unknown --target2 option: " + S);
  return Target2Policy::GotRel;
}

static bool isOutputFormatBinary(opt::InputArgList &Args) {
  StringRef S = Args.getLastArgValue(OPT_oformat, "elf");
  if (S == "binary")
    return true;
  if (!S.startswith("elf"))
    error("unknown --oformat value: " + S);
  return false;
}

static DiscardPolicy getDiscard(opt::InputArgList &Args) {
  if (Args.hasArg(OPT_relocatable))
    return DiscardPolicy::None;

  auto *Arg =
      Args.getLastArg(OPT_discard_all, OPT_discard_locals, OPT_discard_none);
  if (!Arg)
    return DiscardPolicy::Default;
  if (Arg->getOption().getID() == OPT_discard_all)
    return DiscardPolicy::All;
  if (Arg->getOption().getID() == OPT_discard_locals)
    return DiscardPolicy::Locals;
  return DiscardPolicy::None;
}

static StringRef getDynamicLinker(opt::InputArgList &Args) {
  auto *Arg = Args.getLastArg(OPT_dynamic_linker, OPT_no_dynamic_linker);
  if (!Arg || Arg->getOption().getID() == OPT_no_dynamic_linker)
    return "";
  return Arg->getValue();
}

static ICFLevel getICF(opt::InputArgList &Args) {
  auto *Arg = Args.getLastArg(OPT_icf_none, OPT_icf_safe, OPT_icf_all);
  if (!Arg || Arg->getOption().getID() == OPT_icf_none)
    return ICFLevel::None;
  if (Arg->getOption().getID() == OPT_icf_safe)
    return ICFLevel::Safe;
  return ICFLevel::All;
}

static StripPolicy getStrip(opt::InputArgList &Args) {
  if (Args.hasArg(OPT_relocatable))
    return StripPolicy::None;

  auto *Arg = Args.getLastArg(OPT_strip_all, OPT_strip_debug);
  if (!Arg)
    return StripPolicy::None;
  if (Arg->getOption().getID() == OPT_strip_all)
    return StripPolicy::All;
  return StripPolicy::Debug;
}

static uint64_t parseSectionAddress(StringRef S, const opt::Arg &Arg) {
  uint64_t VA = 0;
  if (S.startswith("0x"))
    S = S.drop_front(2);
  if (!to_integer(S, VA, 16))
    error("invalid argument: " + toString(Arg));
  return VA;
}

static StringMap<uint64_t> getSectionStartMap(opt::InputArgList &Args) {
  StringMap<uint64_t> Ret;
  for (auto *Arg : Args.filtered(OPT_section_start)) {
    StringRef Name;
    StringRef Addr;
    std::tie(Name, Addr) = StringRef(Arg->getValue()).split('=');
    Ret[Name] = parseSectionAddress(Addr, *Arg);
  }

  if (auto *Arg = Args.getLastArg(OPT_Ttext))
    Ret[".text"] = parseSectionAddress(Arg->getValue(), *Arg);
  if (auto *Arg = Args.getLastArg(OPT_Tdata))
    Ret[".data"] = parseSectionAddress(Arg->getValue(), *Arg);
  if (auto *Arg = Args.getLastArg(OPT_Tbss))
    Ret[".bss"] = parseSectionAddress(Arg->getValue(), *Arg);
  return Ret;
}

static SortSectionPolicy getSortSection(opt::InputArgList &Args) {
  StringRef S = Args.getLastArgValue(OPT_sort_section);
  if (S == "alignment")
    return SortSectionPolicy::Alignment;
  if (S == "name")
    return SortSectionPolicy::Name;
  if (!S.empty())
    error("unknown --sort-section rule: " + S);
  return SortSectionPolicy::Default;
}

static OrphanHandlingPolicy getOrphanHandling(opt::InputArgList &Args) {
  StringRef S = Args.getLastArgValue(OPT_orphan_handling, "place");
  if (S == "warn")
    return OrphanHandlingPolicy::Warn;
  if (S == "error")
    return OrphanHandlingPolicy::Error;
  if (S != "place")
    error("unknown --orphan-handling mode: " + S);
  return OrphanHandlingPolicy::Place;
}

// Parse --build-id or --build-id=<style>. We handle "tree" as a
// synonym for "sha1" because all our hash functions including
// -build-id=sha1 are actually tree hashes for performance reasons.
static std::pair<BuildIdKind, std::vector<uint8_t>>
getBuildId(opt::InputArgList &Args) {
  auto *Arg = Args.getLastArg(OPT_build_id, OPT_build_id_eq);
  if (!Arg)
    return {BuildIdKind::None, {}};

  if (Arg->getOption().getID() == OPT_build_id)
    return {BuildIdKind::Fast, {}};

  StringRef S = Arg->getValue();
  if (S == "fast")
    return {BuildIdKind::Fast, {}};
  if (S == "md5")
    return {BuildIdKind::Md5, {}};
  if (S == "sha1" || S == "tree")
    return {BuildIdKind::Sha1, {}};
  if (S == "uuid")
    return {BuildIdKind::Uuid, {}};
  if (S.startswith("0x"))
    return {BuildIdKind::Hexstring, parseHex(S.substr(2))};

  if (S != "none")
    error("unknown --build-id style: " + S);
  return {BuildIdKind::None, {}};
}

static std::pair<bool, bool> getPackDynRelocs(opt::InputArgList &Args) {
  StringRef S = Args.getLastArgValue(OPT_pack_dyn_relocs, "none");
  if (S == "android")
    return {true, false};
  if (S == "relr")
    return {false, true};
  if (S == "android+relr")
    return {true, true};

  if (S != "none")
    error("unknown -pack-dyn-relocs format: " + S);
  return {false, false};
}

static void readCallGraph(MemoryBufferRef MB) {
  // Build a map from symbol name to section
  DenseMap<StringRef, Symbol *> Map;
  for (InputFile *File : ObjectFiles)
    for (Symbol *Sym : File->getSymbols())
      Map[Sym->getName()] = Sym;

  auto FindSection = [&](StringRef Name) -> InputSectionBase * {
    Symbol *Sym = Map.lookup(Name);
    if (!Sym) {
      if (Config->WarnSymbolOrdering)
        warn(MB.getBufferIdentifier() + ": no such symbol: " + Name);
      return nullptr;
    }
    maybeWarnUnorderableSymbol(Sym);

    if (Defined *DR = dyn_cast_or_null<Defined>(Sym))
      return dyn_cast_or_null<InputSectionBase>(DR->Section);
    return nullptr;
  };

  for (StringRef Line : args::getLines(MB)) {
    SmallVector<StringRef, 3> Fields;
    Line.split(Fields, ' ');
    uint64_t Count;

    if (Fields.size() != 3 || !to_integer(Fields[2], Count)) {
      error(MB.getBufferIdentifier() + ": parse error");
      return;
    }

    if (InputSectionBase *From = FindSection(Fields[0]))
      if (InputSectionBase *To = FindSection(Fields[1]))
        Config->CallGraphProfile[std::make_pair(From, To)] += Count;
  }
}

template <class ELFT> static void readCallGraphsFromObjectFiles() {
  for (auto File : ObjectFiles) {
    auto *Obj = cast<ObjFile<ELFT>>(File);

    for (const Elf_CGProfile_Impl<ELFT> &CGPE : Obj->CGProfile) {
      auto *FromSym = dyn_cast<Defined>(&Obj->getSymbol(CGPE.cgp_from));
      auto *ToSym = dyn_cast<Defined>(&Obj->getSymbol(CGPE.cgp_to));
      if (!FromSym || !ToSym)
        continue;

      auto *From = dyn_cast_or_null<InputSectionBase>(FromSym->Section);
      auto *To = dyn_cast_or_null<InputSectionBase>(ToSym->Section);
      if (From && To)
        Config->CallGraphProfile[{From, To}] += CGPE.cgp_weight;
    }
  }
}

static bool getCompressDebugSections(opt::InputArgList &Args) {
  StringRef S = Args.getLastArgValue(OPT_compress_debug_sections, "none");
  if (S == "none")
    return false;
  if (S != "zlib")
    error("unknown --compress-debug-sections value: " + S);
  if (!zlib::isAvailable())
    error("--compress-debug-sections: zlib is not available");
  return true;
}

static std::pair<StringRef, StringRef> getOldNewOptions(opt::InputArgList &Args,
                                                        unsigned Id) {
  auto *Arg = Args.getLastArg(Id);
  if (!Arg)
    return {"", ""};

  StringRef S = Arg->getValue();
  std::pair<StringRef, StringRef> Ret = S.split(';');
  if (Ret.second.empty())
    error(Arg->getSpelling() + " expects 'old;new' format, but got " + S);
  return Ret;
}

// Parse the symbol ordering file and warn for any duplicate entries.
static std::vector<StringRef> getSymbolOrderingFile(MemoryBufferRef MB) {
  SetVector<StringRef> Names;
  for (StringRef S : args::getLines(MB))
    if (!Names.insert(S) && Config->WarnSymbolOrdering)
      warn(MB.getBufferIdentifier() + ": duplicate ordered symbol: " + S);

  return Names.takeVector();
}

static void parseClangOption(StringRef Opt, const Twine &Msg) {
  std::string Err;
  raw_string_ostream OS(Err);

  const char *Argv[] = {Config->ProgName.data(), Opt.data()};
  if (cl::ParseCommandLineOptions(2, Argv, "", &OS))
    return;
  OS.flush();
  error(Msg + ": " + StringRef(Err).trim());
}

// Initializes Config members by the command line options.
void LinkerDriver::readConfigs(opt::InputArgList &Args) {
  errorHandler().Verbose = Args.hasArg(OPT_verbose);
  errorHandler().FatalWarnings =
      Args.hasFlag(OPT_fatal_warnings, OPT_no_fatal_warnings, false);
  ThreadsEnabled = Args.hasFlag(OPT_threads, OPT_no_threads, true);

  Config->AllowMultipleDefinition =
      Args.hasFlag(OPT_allow_multiple_definition,
                   OPT_no_allow_multiple_definition, false) ||
      hasZOption(Args, "muldefs");
  Config->AllowShlibUndefined =
      Args.hasFlag(OPT_allow_shlib_undefined, OPT_no_allow_shlib_undefined,
                   Args.hasArg(OPT_shared));
  Config->AuxiliaryList = args::getStrings(Args, OPT_auxiliary);
  Config->Bsymbolic = Args.hasArg(OPT_Bsymbolic);
  Config->BsymbolicFunctions = Args.hasArg(OPT_Bsymbolic_functions);
  Config->CheckSections =
      Args.hasFlag(OPT_check_sections, OPT_no_check_sections, true);
  Config->Chroot = Args.getLastArgValue(OPT_chroot);
  Config->CompressDebugSections = getCompressDebugSections(Args);
  Config->Cref = Args.hasFlag(OPT_cref, OPT_no_cref, false);
  Config->DefineCommon = Args.hasFlag(OPT_define_common, OPT_no_define_common,
                                      !Args.hasArg(OPT_relocatable));
  Config->Demangle = Args.hasFlag(OPT_demangle, OPT_no_demangle, true);
  Config->DisableVerify = Args.hasArg(OPT_disable_verify);
  Config->Discard = getDiscard(Args);
  Config->DwoDir = Args.getLastArgValue(OPT_plugin_opt_dwo_dir_eq);
  Config->DynamicLinker = getDynamicLinker(Args);
  Config->EhFrameHdr =
      Args.hasFlag(OPT_eh_frame_hdr, OPT_no_eh_frame_hdr, false);
  Config->EmitLLVM = Args.hasArg(OPT_plugin_opt_emit_llvm, false);
  Config->EmitRelocs = Args.hasArg(OPT_emit_relocs);
  Config->CallGraphProfileSort = Args.hasFlag(
      OPT_call_graph_profile_sort, OPT_no_call_graph_profile_sort, true);
  Config->EnableNewDtags =
      Args.hasFlag(OPT_enable_new_dtags, OPT_disable_new_dtags, true);
  Config->Entry = Args.getLastArgValue(OPT_entry);
  Config->ExecuteOnly =
      Args.hasFlag(OPT_execute_only, OPT_no_execute_only, false);
  Config->ExportDynamic =
      Args.hasFlag(OPT_export_dynamic, OPT_no_export_dynamic, false);
  Config->FilterList = args::getStrings(Args, OPT_filter);
  Config->Fini = Args.getLastArgValue(OPT_fini, "_fini");
  Config->FixCortexA53Errata843419 = Args.hasArg(OPT_fix_cortex_a53_843419);
  Config->GcSections = Args.hasFlag(OPT_gc_sections, OPT_no_gc_sections, false);
  Config->GnuUnique = Args.hasFlag(OPT_gnu_unique, OPT_no_gnu_unique, true);
  Config->GdbIndex = Args.hasFlag(OPT_gdb_index, OPT_no_gdb_index, false);
  Config->ICF = getICF(Args);
  Config->IgnoreDataAddressEquality =
      Args.hasArg(OPT_ignore_data_address_equality);
  Config->IgnoreFunctionAddressEquality =
      Args.hasArg(OPT_ignore_function_address_equality);
  Config->Init = Args.getLastArgValue(OPT_init, "_init");
  Config->LTOAAPipeline = Args.getLastArgValue(OPT_lto_aa_pipeline);
  Config->LTODebugPassManager = Args.hasArg(OPT_lto_debug_pass_manager);
  Config->LTONewPassManager = Args.hasArg(OPT_lto_new_pass_manager);
  Config->LTONewPmPasses = Args.getLastArgValue(OPT_lto_newpm_passes);
  Config->LTOO = args::getInteger(Args, OPT_lto_O, 2);
  Config->LTOObjPath = Args.getLastArgValue(OPT_plugin_opt_obj_path_eq);
  Config->LTOPartitions = args::getInteger(Args, OPT_lto_partitions, 1);
  Config->LTOSampleProfile = Args.getLastArgValue(OPT_lto_sample_profile);
  Config->MapFile = Args.getLastArgValue(OPT_Map);
  Config->MipsGotSize = args::getInteger(Args, OPT_mips_got_size, 0xfff0);
  Config->MergeArmExidx =
      Args.hasFlag(OPT_merge_exidx_entries, OPT_no_merge_exidx_entries, true);
  Config->NoinhibitExec = Args.hasArg(OPT_noinhibit_exec);
  Config->Nostdlib = Args.hasArg(OPT_nostdlib);
  Config->OFormatBinary = isOutputFormatBinary(Args);
  Config->Omagic = Args.hasFlag(OPT_omagic, OPT_no_omagic, false);
  Config->OptRemarksFilename = Args.getLastArgValue(OPT_opt_remarks_filename);
  Config->OptRemarksWithHotness = Args.hasArg(OPT_opt_remarks_with_hotness);
  Config->Optimize = args::getInteger(Args, OPT_O, 1);
  Config->OrphanHandling = getOrphanHandling(Args);
  Config->OutputFile = Args.getLastArgValue(OPT_o);
  Config->Pie = Args.hasFlag(OPT_pie, OPT_no_pie, false);
  Config->PrintIcfSections =
      Args.hasFlag(OPT_print_icf_sections, OPT_no_print_icf_sections, false);
  Config->PrintGcSections =
      Args.hasFlag(OPT_print_gc_sections, OPT_no_print_gc_sections, false);
  Config->Rpath = getRpath(Args);
  Config->Relocatable = Args.hasArg(OPT_relocatable);
  Config->SaveTemps = Args.hasArg(OPT_save_temps);
  Config->SearchPaths = args::getStrings(Args, OPT_library_path);
  Config->SectionStartMap = getSectionStartMap(Args);
  Config->Shared = Args.hasArg(OPT_shared);
  Config->SingleRoRx = Args.hasArg(OPT_no_rosegment);
  Config->SoName = Args.getLastArgValue(OPT_soname);
  Config->SortSection = getSortSection(Args);
  Config->SplitStackAdjustSize = args::getInteger(Args, OPT_split_stack_adjust_size, 16384);
  Config->Strip = getStrip(Args);
  Config->Sysroot = Args.getLastArgValue(OPT_sysroot);
  Config->Target1Rel = Args.hasFlag(OPT_target1_rel, OPT_target1_abs, false);
  Config->Target2 = getTarget2(Args);
  Config->ThinLTOCacheDir = Args.getLastArgValue(OPT_thinlto_cache_dir);
  Config->ThinLTOCachePolicy = CHECK(
      parseCachePruningPolicy(Args.getLastArgValue(OPT_thinlto_cache_policy)),
      "--thinlto-cache-policy: invalid cache policy");
  Config->ThinLTOEmitImportsFiles =
      Args.hasArg(OPT_plugin_opt_thinlto_emit_imports_files);
  Config->ThinLTOIndexOnly = Args.hasArg(OPT_plugin_opt_thinlto_index_only) ||
                             Args.hasArg(OPT_plugin_opt_thinlto_index_only_eq);
  Config->ThinLTOIndexOnlyArg =
      Args.getLastArgValue(OPT_plugin_opt_thinlto_index_only_eq);
  Config->ThinLTOJobs = args::getInteger(Args, OPT_thinlto_jobs, -1u);
  Config->ThinLTOObjectSuffixReplace =
      getOldNewOptions(Args, OPT_plugin_opt_thinlto_object_suffix_replace_eq);
  Config->ThinLTOPrefixReplace =
      getOldNewOptions(Args, OPT_plugin_opt_thinlto_prefix_replace_eq);
  Config->Trace = Args.hasArg(OPT_trace);
  Config->Undefined = args::getStrings(Args, OPT_undefined);
  Config->UndefinedVersion =
      Args.hasFlag(OPT_undefined_version, OPT_no_undefined_version, true);
  Config->UseAndroidRelrTags = Args.hasFlag(
      OPT_use_android_relr_tags, OPT_no_use_android_relr_tags, false);
  Config->UnresolvedSymbols = getUnresolvedSymbolPolicy(Args);
  Config->WarnBackrefs =
      Args.hasFlag(OPT_warn_backrefs, OPT_no_warn_backrefs, false);
  Config->WarnCommon = Args.hasFlag(OPT_warn_common, OPT_no_warn_common, false);
  Config->WarnIfuncTextrel =
      Args.hasFlag(OPT_warn_ifunc_textrel, OPT_no_warn_ifunc_textrel, false);
  Config->WarnSymbolOrdering =
      Args.hasFlag(OPT_warn_symbol_ordering, OPT_no_warn_symbol_ordering, true);
  Config->ZCombreloc = getZFlag(Args, "combreloc", "nocombreloc", true);
  Config->ZCopyreloc = getZFlag(Args, "copyreloc", "nocopyreloc", true);
  Config->ZExecstack = getZFlag(Args, "execstack", "noexecstack", false);
  Config->ZGlobal = hasZOption(Args, "global");
  Config->ZHazardplt = hasZOption(Args, "hazardplt");
  Config->ZIfuncnoplt = hasZOption(Args, "ifunc-noplt");
  Config->ZInitfirst = hasZOption(Args, "initfirst");
  Config->ZInterpose = hasZOption(Args, "interpose");
  Config->ZKeepTextSectionPrefix = getZFlag(
      Args, "keep-text-section-prefix", "nokeep-text-section-prefix", false);
  Config->ZNodefaultlib = hasZOption(Args, "nodefaultlib");
  Config->ZNodelete = hasZOption(Args, "nodelete");
  Config->ZNodlopen = hasZOption(Args, "nodlopen");
  Config->ZNow = getZFlag(Args, "now", "lazy", false);
  Config->ZOrigin = hasZOption(Args, "origin");
  Config->ZRelro = getZFlag(Args, "relro", "norelro", true);
  Config->ZRetpolineplt = hasZOption(Args, "retpolineplt");
  Config->ZRodynamic = hasZOption(Args, "rodynamic");
  Config->ZStackSize = args::getZOptionValue(Args, OPT_z, "stack-size", 0);
  Config->ZText = getZFlag(Args, "text", "notext", true);
  Config->ZWxneeded = hasZOption(Args, "wxneeded");

  // Parse LTO options.
  if (auto *Arg = Args.getLastArg(OPT_plugin_opt_mcpu_eq))
    parseClangOption(Saver.save("-mcpu=" + StringRef(Arg->getValue())),
                     Arg->getSpelling());

  for (auto *Arg : Args.filtered(OPT_plugin_opt))
    parseClangOption(Arg->getValue(), Arg->getSpelling());

  // Parse -mllvm options.
  for (auto *Arg : Args.filtered(OPT_mllvm))
    parseClangOption(Arg->getValue(), Arg->getSpelling());

  if (Config->LTOO > 3)
    error("invalid optimization level for LTO: " + Twine(Config->LTOO));
  if (Config->LTOPartitions == 0)
    error("--lto-partitions: number of threads must be > 0");
  if (Config->ThinLTOJobs == 0)
    error("--thinlto-jobs: number of threads must be > 0");

  if (Config->SplitStackAdjustSize < 0)
    error("--split-stack-adjust-size: size must be >= 0");

  // Parse ELF{32,64}{LE,BE} and CPU type.
  if (auto *Arg = Args.getLastArg(OPT_m)) {
    StringRef S = Arg->getValue();
    std::tie(Config->EKind, Config->EMachine, Config->OSABI) =
        parseEmulation(S);
    Config->MipsN32Abi = (S == "elf32btsmipn32" || S == "elf32ltsmipn32");
    Config->Emulation = S;
  }

  // Parse -hash-style={sysv,gnu,both}.
  if (auto *Arg = Args.getLastArg(OPT_hash_style)) {
    StringRef S = Arg->getValue();
    if (S == "sysv")
      Config->SysvHash = true;
    else if (S == "gnu")
      Config->GnuHash = true;
    else if (S == "both")
      Config->SysvHash = Config->GnuHash = true;
    else
      error("unknown -hash-style: " + S);
  }

  if (Args.hasArg(OPT_print_map))
    Config->MapFile = "-";

  // --omagic is an option to create old-fashioned executables in which
  // .text segments are writable. Today, the option is still in use to
  // create special-purpose programs such as boot loaders. It doesn't
  // make sense to create PT_GNU_RELRO for such executables.
  if (Config->Omagic)
    Config->ZRelro = false;

  std::tie(Config->BuildId, Config->BuildIdVector) = getBuildId(Args);

  std::tie(Config->AndroidPackDynRelocs, Config->RelrPackDynRelocs) =
      getPackDynRelocs(Args);

  if (auto *Arg = Args.getLastArg(OPT_symbol_ordering_file))
    if (Optional<MemoryBufferRef> Buffer = readFile(Arg->getValue()))
      Config->SymbolOrderingFile = getSymbolOrderingFile(*Buffer);

  // If --retain-symbol-file is used, we'll keep only the symbols listed in
  // the file and discard all others.
  if (auto *Arg = Args.getLastArg(OPT_retain_symbols_file)) {
    Config->DefaultSymbolVersion = VER_NDX_LOCAL;
    if (Optional<MemoryBufferRef> Buffer = readFile(Arg->getValue()))
      for (StringRef S : args::getLines(*Buffer))
        Config->VersionScriptGlobals.push_back(
            {S, /*IsExternCpp*/ false, /*HasWildcard*/ false});
  }

  bool HasExportDynamic =
      Args.hasFlag(OPT_export_dynamic, OPT_no_export_dynamic, false);

  // Parses -dynamic-list and -export-dynamic-symbol. They make some
  // symbols private. Note that -export-dynamic takes precedence over them
  // as it says all symbols should be exported.
  if (!HasExportDynamic) {
    for (auto *Arg : Args.filtered(OPT_dynamic_list))
      if (Optional<MemoryBufferRef> Buffer = readFile(Arg->getValue()))
        readDynamicList(*Buffer);

    for (auto *Arg : Args.filtered(OPT_export_dynamic_symbol))
      Config->DynamicList.push_back(
          {Arg->getValue(), /*IsExternCpp*/ false, /*HasWildcard*/ false});
  }

  // If --export-dynamic-symbol=foo is given and symbol foo is defined in
  // an object file in an archive file, that object file should be pulled
  // out and linked. (It doesn't have to behave like that from technical
  // point of view, but this is needed for compatibility with GNU.)
  for (auto *Arg : Args.filtered(OPT_export_dynamic_symbol))
    Config->Undefined.push_back(Arg->getValue());

  for (auto *Arg : Args.filtered(OPT_version_script))
    if (Optional<std::string> Path = searchScript(Arg->getValue())) {
      if (Optional<MemoryBufferRef> Buffer = readFile(*Path))
        readVersionScript(*Buffer);
    } else {
      error(Twine("cannot find version script ") + Arg->getValue());
    }
}

// Some Config members do not directly correspond to any particular
// command line options, but computed based on other Config values.
// This function initialize such members. See Config.h for the details
// of these values.
static void setConfigs(opt::InputArgList &Args) {
  ELFKind K = Config->EKind;
  uint16_t M = Config->EMachine;

  Config->CopyRelocs = (Config->Relocatable || Config->EmitRelocs);
  Config->Is64 = (K == ELF64LEKind || K == ELF64BEKind);
  Config->IsLE = (K == ELF32LEKind || K == ELF64LEKind);
  Config->Endianness = Config->IsLE ? endianness::little : endianness::big;
  Config->IsMips64EL = (K == ELF64LEKind && M == EM_MIPS);
  Config->Pic = Config->Pie || Config->Shared;
  Config->PicThunk = Args.hasArg(OPT_pic_veneer, Config->Pic);
  Config->Wordsize = Config->Is64 ? 8 : 4;

  // ELF defines two different ways to store relocation addends as shown below:
  //
  //  Rel:  Addends are stored to the location where relocations are applied.
  //  Rela: Addends are stored as part of relocation entry.
  //
  // In other words, Rela makes it easy to read addends at the price of extra
  // 4 or 8 byte for each relocation entry. We don't know why ELF defined two
  // different mechanisms in the first place, but this is how the spec is
  // defined.
  //
  // You cannot choose which one, Rel or Rela, you want to use. Instead each
  // ABI defines which one you need to use. The following expression expresses
  // that.
  Config->IsRela = M == EM_AARCH64 || M == EM_AMDGPU || M == EM_HEXAGON ||
                   M == EM_PPC || M == EM_PPC64 || M == EM_RISCV ||
                   M == EM_X86_64;

  // If the output uses REL relocations we must store the dynamic relocation
  // addends to the output sections. We also store addends for RELA relocations
  // if --apply-dynamic-relocs is used.
  // We default to not writing the addends when using RELA relocations since
  // any standard conforming tool can find it in r_addend.
  Config->WriteAddends = Args.hasFlag(OPT_apply_dynamic_relocs,
                                      OPT_no_apply_dynamic_relocs, false) ||
                         !Config->IsRela;

  Config->TocOptimize =
      Args.hasFlag(OPT_toc_optimize, OPT_no_toc_optimize, M == EM_PPC64);
}

// Returns a value of "-format" option.
static bool isFormatBinary(StringRef S) {
  if (S == "binary")
    return true;
  if (S == "elf" || S == "default")
    return false;
  error("unknown -format value: " + S +
        " (supported formats: elf, default, binary)");
  return false;
}

void LinkerDriver::createFiles(opt::InputArgList &Args) {
  // For --{push,pop}-state.
  std::vector<std::tuple<bool, bool, bool>> Stack;

  // Iterate over argv to process input files and positional arguments.
  for (auto *Arg : Args) {
    switch (Arg->getOption().getUnaliasedOption().getID()) {
    case OPT_library:
      addLibrary(Arg->getValue());
      break;
    case OPT_INPUT:
      addFile(Arg->getValue(), /*WithLOption=*/false);
      break;
    case OPT_defsym: {
      StringRef From;
      StringRef To;
      std::tie(From, To) = StringRef(Arg->getValue()).split('=');
      if (From.empty() || To.empty())
        error("-defsym: syntax error: " + StringRef(Arg->getValue()));
      else
        readDefsym(From, MemoryBufferRef(To, "-defsym"));
      break;
    }
    case OPT_script:
      if (Optional<std::string> Path = searchScript(Arg->getValue())) {
        if (Optional<MemoryBufferRef> MB = readFile(*Path))
          readLinkerScript(*MB);
        break;
      }
      error(Twine("cannot find linker script ") + Arg->getValue());
      break;
    case OPT_as_needed:
      Config->AsNeeded = true;
      break;
    case OPT_format:
      Config->FormatBinary = isFormatBinary(Arg->getValue());
      break;
    case OPT_no_as_needed:
      Config->AsNeeded = false;
      break;
    case OPT_Bstatic:
      Config->Static = true;
      break;
    case OPT_Bdynamic:
      Config->Static = false;
      break;
    case OPT_whole_archive:
      InWholeArchive = true;
      break;
    case OPT_no_whole_archive:
      InWholeArchive = false;
      break;
    case OPT_just_symbols:
      if (Optional<MemoryBufferRef> MB = readFile(Arg->getValue())) {
        Files.push_back(createObjectFile(*MB));
        Files.back()->JustSymbols = true;
      }
      break;
    case OPT_start_group:
      if (InputFile::IsInGroup)
        error("nested --start-group");
      InputFile::IsInGroup = true;
      break;
    case OPT_end_group:
      if (!InputFile::IsInGroup)
        error("stray --end-group");
      InputFile::IsInGroup = false;
      ++InputFile::NextGroupId;
      break;
    case OPT_start_lib:
      if (InLib)
        error("nested --start-lib");
      if (InputFile::IsInGroup)
        error("may not nest --start-lib in --start-group");
      InLib = true;
      InputFile::IsInGroup = true;
      break;
    case OPT_end_lib:
      if (!InLib)
        error("stray --end-lib");
      InLib = false;
      InputFile::IsInGroup = false;
      ++InputFile::NextGroupId;
      break;
    case OPT_push_state:
      Stack.emplace_back(Config->AsNeeded, Config->Static, InWholeArchive);
      break;
    case OPT_pop_state:
      if (Stack.empty()) {
        error("unbalanced --push-state/--pop-state");
        break;
      }
      std::tie(Config->AsNeeded, Config->Static, InWholeArchive) = Stack.back();
      Stack.pop_back();
      break;
    }
  }

  if (Files.empty() && errorCount() == 0)
    error("no input files");
}

// If -m <machine_type> was not given, infer it from object files.
void LinkerDriver::inferMachineType() {
  if (Config->EKind != ELFNoneKind)
    return;

  for (InputFile *F : Files) {
    if (F->EKind == ELFNoneKind)
      continue;
    Config->EKind = F->EKind;
    Config->EMachine = F->EMachine;
    Config->OSABI = F->OSABI;
    Config->MipsN32Abi = Config->EMachine == EM_MIPS && isMipsN32Abi(F);
    return;
  }
  error("target emulation unknown: -m or at least one .o file required");
}

// Parse -z max-page-size=<value>. The default value is defined by
// each target.
static uint64_t getMaxPageSize(opt::InputArgList &Args) {
  uint64_t Val = args::getZOptionValue(Args, OPT_z, "max-page-size",
                                       Target->DefaultMaxPageSize);
  if (!isPowerOf2_64(Val))
    error("max-page-size: value isn't a power of 2");
  return Val;
}

// Parses -image-base option.
static Optional<uint64_t> getImageBase(opt::InputArgList &Args) {
  // Because we are using "Config->MaxPageSize" here, this function has to be
  // called after the variable is initialized.
  auto *Arg = Args.getLastArg(OPT_image_base);
  if (!Arg)
    return None;

  StringRef S = Arg->getValue();
  uint64_t V;
  if (!to_integer(S, V)) {
    error("-image-base: number expected, but got " + S);
    return 0;
  }
  if ((V % Config->MaxPageSize) != 0)
    warn("-image-base: address isn't multiple of page size: " + S);
  return V;
}

// Parses `--exclude-libs=lib,lib,...`.
// The library names may be delimited by commas or colons.
static DenseSet<StringRef> getExcludeLibs(opt::InputArgList &Args) {
  DenseSet<StringRef> Ret;
  for (auto *Arg : Args.filtered(OPT_exclude_libs)) {
    StringRef S = Arg->getValue();
    for (;;) {
      size_t Pos = S.find_first_of(",:");
      if (Pos == StringRef::npos)
        break;
      Ret.insert(S.substr(0, Pos));
      S = S.substr(Pos + 1);
    }
    Ret.insert(S);
  }
  return Ret;
}

// Handles the -exclude-libs option. If a static library file is specified
// by the -exclude-libs option, all public symbols from the archive become
// private unless otherwise specified by version scripts or something.
// A special library name "ALL" means all archive files.
//
// This is not a popular option, but some programs such as bionic libc use it.
template <class ELFT>
static void excludeLibs(opt::InputArgList &Args) {
  DenseSet<StringRef> Libs = getExcludeLibs(Args);
  bool All = Libs.count("ALL");

  auto Visit = [&](InputFile *File) {
    if (!File->ArchiveName.empty())
      if (All || Libs.count(path::filename(File->ArchiveName)))
        for (Symbol *Sym : File->getSymbols())
          if (!Sym->isLocal() && Sym->File == File)
            Sym->VersionId = VER_NDX_LOCAL;
  };

  for (InputFile *File : ObjectFiles)
    Visit(File);

  for (BitcodeFile *File : BitcodeFiles)
    Visit(File);
}

// Force Sym to be entered in the output. Used for -u or equivalent.
template <class ELFT> static void handleUndefined(StringRef Name) {
  Symbol *Sym = Symtab->find(Name);
  if (!Sym)
    return;

  // Since symbol S may not be used inside the program, LTO may
  // eliminate it. Mark the symbol as "used" to prevent it.
  Sym->IsUsedInRegularObj = true;

  if (Sym->isLazy())
    Symtab->fetchLazy<ELFT>(Sym);
}

template <class ELFT> static void handleLibcall(StringRef Name) {
  Symbol *Sym = Symtab->find(Name);
  if (!Sym || !Sym->isLazy())
    return;

  MemoryBufferRef MB;
  if (auto *LO = dyn_cast<LazyObject>(Sym))
    MB = LO->File->MB;
  else
    MB = cast<LazyArchive>(Sym)->getMemberBuffer();

  if (isBitcode(MB))
    Symtab->fetchLazy<ELFT>(Sym);
}

// If all references to a DSO happen to be weak, the DSO is not added
// to DT_NEEDED. If that happens, we need to eliminate shared symbols
// created from the DSO. Otherwise, they become dangling references
// that point to a non-existent DSO.
template <class ELFT> static void demoteSharedSymbols() {
  for (Symbol *Sym : Symtab->getSymbols()) {
    if (auto *S = dyn_cast<SharedSymbol>(Sym)) {
      if (!S->getFile<ELFT>().IsNeeded) {
        bool Used = S->Used;
        replaceSymbol<Undefined>(S, nullptr, S->getName(), STB_WEAK, S->StOther,
                                 S->Type);
        S->Used = Used;
      }
    }
  }
}

// The section referred to by S is considered address-significant. Set the
// KeepUnique flag on the section if appropriate.
static void markAddrsig(Symbol *S) {
  if (auto *D = dyn_cast_or_null<Defined>(S))
    if (D->Section)
      // We don't need to keep text sections unique under --icf=all even if they
      // are address-significant.
      if (Config->ICF == ICFLevel::Safe || !(D->Section->Flags & SHF_EXECINSTR))
        D->Section->KeepUnique = true;
}

// Record sections that define symbols mentioned in --keep-unique <symbol>
// and symbols referred to by address-significance tables. These sections are
// ineligible for ICF.
template <class ELFT>
static void findKeepUniqueSections(opt::InputArgList &Args) {
  for (auto *Arg : Args.filtered(OPT_keep_unique)) {
    StringRef Name = Arg->getValue();
    auto *D = dyn_cast_or_null<Defined>(Symtab->find(Name));
    if (!D || !D->Section) {
      warn("could not find symbol " + Name + " to keep unique");
      continue;
    }
    D->Section->KeepUnique = true;
  }

  // --icf=all --ignore-data-address-equality means that we can ignore
  // the dynsym and address-significance tables entirely.
  if (Config->ICF == ICFLevel::All && Config->IgnoreDataAddressEquality)
    return;

  // Symbols in the dynsym could be address-significant in other executables
  // or DSOs, so we conservatively mark them as address-significant.
  for (Symbol *S : Symtab->getSymbols())
    if (S->includeInDynsym())
      markAddrsig(S);

  // Visit the address-significance table in each object file and mark each
  // referenced symbol as address-significant.
  for (InputFile *F : ObjectFiles) {
    auto *Obj = cast<ObjFile<ELFT>>(F);
    ArrayRef<Symbol *> Syms = Obj->getSymbols();
    if (Obj->AddrsigSec) {
      ArrayRef<uint8_t> Contents =
          check(Obj->getObj().getSectionContents(Obj->AddrsigSec));
      const uint8_t *Cur = Contents.begin();
      while (Cur != Contents.end()) {
        unsigned Size;
        const char *Err;
        uint64_t SymIndex = decodeULEB128(Cur, &Size, Contents.end(), &Err);
        if (Err)
          fatal(toString(F) + ": could not decode addrsig section: " + Err);
        markAddrsig(Syms[SymIndex]);
        Cur += Size;
      }
    } else {
      // If an object file does not have an address-significance table,
      // conservatively mark all of its symbols as address-significant.
      for (Symbol *S : Syms)
        markAddrsig(S);
    }
  }
}

template <class ELFT> static Symbol *addUndefined(StringRef Name) {
  return Symtab->addUndefined<ELFT>(Name, STB_GLOBAL, STV_DEFAULT, 0, false,
                                    nullptr);
}

// The --wrap option is a feature to rename symbols so that you can write
// wrappers for existing functions. If you pass `-wrap=foo`, all
// occurrences of symbol `foo` are resolved to `wrap_foo` (so, you are
// expected to write `wrap_foo` function as a wrapper). The original
// symbol becomes accessible as `real_foo`, so you can call that from your
// wrapper.
//
// This data structure is instantiated for each -wrap option.
struct WrappedSymbol {
  Symbol *Sym;
  Symbol *Real;
  Symbol *Wrap;
};

// Handles -wrap option.
//
// This function instantiates wrapper symbols. At this point, they seem
// like they are not being used at all, so we explicitly set some flags so
// that LTO won't eliminate them.
template <class ELFT>
static std::vector<WrappedSymbol> addWrappedSymbols(opt::InputArgList &Args) {
  std::vector<WrappedSymbol> V;
  DenseSet<StringRef> Seen;

  for (auto *Arg : Args.filtered(OPT_wrap)) {
    StringRef Name = Arg->getValue();
    if (!Seen.insert(Name).second)
      continue;

    Symbol *Sym = Symtab->find(Name);
    if (!Sym)
      continue;

    Symbol *Real = addUndefined<ELFT>(Saver.save("__real_" + Name));
    Symbol *Wrap = addUndefined<ELFT>(Saver.save("__wrap_" + Name));
    V.push_back({Sym, Real, Wrap});

    // We want to tell LTO not to inline symbols to be overwritten
    // because LTO doesn't know the final symbol contents after renaming.
    Real->CanInline = false;
    Sym->CanInline = false;

    // Tell LTO not to eliminate these symbols.
    Sym->IsUsedInRegularObj = true;
    Wrap->IsUsedInRegularObj = true;
  }
  return V;
}

// Do renaming for -wrap by updating pointers to symbols.
//
// When this function is executed, only InputFiles and symbol table
// contain pointers to symbol objects. We visit them to replace pointers,
// so that wrapped symbols are swapped as instructed by the command line.
template <class ELFT> static void wrapSymbols(ArrayRef<WrappedSymbol> Wrapped) {
  DenseMap<Symbol *, Symbol *> Map;
  for (const WrappedSymbol &W : Wrapped) {
    Map[W.Sym] = W.Wrap;
    Map[W.Real] = W.Sym;
  }

  // Update pointers in input files.
  parallelForEach(ObjectFiles, [&](InputFile *File) {
    std::vector<Symbol *> &Syms = File->getMutableSymbols();
    for (size_t I = 0, E = Syms.size(); I != E; ++I)
      if (Symbol *S = Map.lookup(Syms[I]))
        Syms[I] = S;
  });

  // Update pointers in the symbol table.
  for (const WrappedSymbol &W : Wrapped)
    Symtab->wrap(W.Sym, W.Real, W.Wrap);
}

static const char *LibcallRoutineNames[] = {
#define HANDLE_LIBCALL(code, name) name,
#include "llvm/IR/RuntimeLibcalls.def"
#undef HANDLE_LIBCALL
};

// Do actual linking. Note that when this function is called,
// all linker scripts have already been parsed.
template <class ELFT> void LinkerDriver::link(opt::InputArgList &Args) {
  Target = getTarget();
  InX<ELFT>::VerSym = nullptr;
  InX<ELFT>::VerNeed = nullptr;

  Config->MaxPageSize = getMaxPageSize(Args);
  Config->ImageBase = getImageBase(Args);

  // If a -hash-style option was not given, set to a default value,
  // which varies depending on the target.
  if (!Args.hasArg(OPT_hash_style)) {
    if (Config->EMachine == EM_MIPS)
      Config->SysvHash = true;
    else
      Config->SysvHash = Config->GnuHash = true;
  }

  // Default output filename is "a.out" by the Unix tradition.
  if (Config->OutputFile.empty())
    Config->OutputFile = "a.out";

  // Fail early if the output file or map file is not writable. If a user has a
  // long link, e.g. due to a large LTO link, they do not wish to run it and
  // find that it failed because there was a mistake in their command-line.
  if (auto E = tryCreateFile(Config->OutputFile))
    error("cannot open output file " + Config->OutputFile + ": " + E.message());
  if (auto E = tryCreateFile(Config->MapFile))
    error("cannot open map file " + Config->MapFile + ": " + E.message());
  if (errorCount())
    return;

  // Use default entry point name if no name was given via the command
  // line nor linker scripts. For some reason, MIPS entry point name is
  // different from others.
  Config->WarnMissingEntry =
      (!Config->Entry.empty() || (!Config->Shared && !Config->Relocatable));
  if (Config->Entry.empty() && !Config->Relocatable)
    Config->Entry = (Config->EMachine == EM_MIPS) ? "__start" : "_start";

  // Handle --trace-symbol.
  for (auto *Arg : Args.filtered(OPT_trace_symbol))
    Symtab->trace(Arg->getValue());

  // Add all files to the symbol table. This will add almost all
  // symbols that we need to the symbol table.
  for (InputFile *F : Files)
    Symtab->addFile<ELFT>(F);

  // Now that we have every file, we can decide if we will need a
  // dynamic symbol table.
  // We need one if we were asked to export dynamic symbols or if we are
  // producing a shared library.
  // We also need one if any shared libraries are used and for pie executables
  // (probably because the dynamic linker needs it).
  Config->HasDynSymTab =
      !SharedFiles.empty() || Config->Pic || Config->ExportDynamic;

  // Some symbols (such as __ehdr_start) are defined lazily only when there
  // are undefined symbols for them, so we add these to trigger that logic.
  for (StringRef Name : Script->ReferencedSymbols)
    addUndefined<ELFT>(Name);

  // Handle the `--undefined <sym>` options.
  for (StringRef S : Config->Undefined)
    handleUndefined<ELFT>(S);

  // If an entry symbol is in a static archive, pull out that file now.
  handleUndefined<ELFT>(Config->Entry);

  // If any of our inputs are bitcode files, the LTO code generator may create
  // references to certain library functions that might not be explicit in the
  // bitcode file's symbol table. If any of those library functions are defined
  // in a bitcode file in an archive member, we need to arrange to use LTO to
  // compile those archive members by adding them to the link beforehand.
  //
  // However, adding all libcall symbols to the link can have undesired
  // consequences. For example, the libgcc implementation of
  // __sync_val_compare_and_swap_8 on 32-bit ARM pulls in an .init_array entry
  // that aborts the program if the Linux kernel does not support 64-bit
  // atomics, which would prevent the program from running even if it does not
  // use 64-bit atomics.
  //
  // Therefore, we only add libcall symbols to the link before LTO if we have
  // to, i.e. if the symbol's definition is in bitcode. Any other required
  // libcall symbols will be added to the link after LTO when we add the LTO
  // object file to the link.
  if (!BitcodeFiles.empty())
    for (const char *S : LibcallRoutineNames)
      handleLibcall<ELFT>(S);

  // Return if there were name resolution errors.
  if (errorCount())
    return;

  // Now when we read all script files, we want to finalize order of linker
  // script commands, which can be not yet final because of INSERT commands.
  Script->processInsertCommands();

  // We want to declare linker script's symbols early,
  // so that we can version them.
  // They also might be exported if referenced by DSOs.
  Script->declareSymbols();

  // Handle the -exclude-libs option.
  if (Args.hasArg(OPT_exclude_libs))
    excludeLibs<ELFT>(Args);

  // Create ElfHeader early. We need a dummy section in
  // addReservedSymbols to mark the created symbols as not absolute.
  Out::ElfHeader = make<OutputSection>("", 0, SHF_ALLOC);
  Out::ElfHeader->Size = sizeof(typename ELFT::Ehdr);

  // Create wrapped symbols for -wrap option.
  std::vector<WrappedSymbol> Wrapped = addWrappedSymbols<ELFT>(Args);

  // We need to create some reserved symbols such as _end. Create them.
  if (!Config->Relocatable)
    addReservedSymbols();

  // Apply version scripts.
  //
  // For a relocatable output, version scripts don't make sense, and
  // parsing a symbol version string (e.g. dropping "@ver1" from a symbol
  // name "foo@ver1") rather do harm, so we don't call this if -r is given.
  if (!Config->Relocatable)
    Symtab->scanVersionScript();

  // Do link-time optimization if given files are LLVM bitcode files.
  // This compiles bitcode files into real object files.
  //
  // With this the symbol table should be complete. After this, no new names
  // except a few linker-synthesized ones will be added to the symbol table.
  Symtab->addCombinedLTOObject<ELFT>();
  if (errorCount())
    return;

  // If -thinlto-index-only is given, we should create only "index
  // files" and not object files. Index file creation is already done
  // in addCombinedLTOObject, so we are done if that's the case.
  if (Config->ThinLTOIndexOnly)
    return;

  // Likewise, --plugin-opt=emit-llvm is an option to make LTO create
  // an output file in bitcode and exit, so that you can just get a
  // combined bitcode file.
  if (Config->EmitLLVM)
    return;

  // Apply symbol renames for -wrap.
  if (!Wrapped.empty())
    wrapSymbols<ELFT>(Wrapped);

  // Now that we have a complete list of input files.
  // Beyond this point, no new files are added.
  // Aggregate all input sections into one place.
  for (InputFile *F : ObjectFiles)
    for (InputSectionBase *S : F->getSections())
      if (S && S != &InputSection::Discarded)
        InputSections.push_back(S);
  for (BinaryFile *F : BinaryFiles)
    for (InputSectionBase *S : F->getSections())
      InputSections.push_back(cast<InputSection>(S));

  // We do not want to emit debug sections if --strip-all
  // or -strip-debug are given.
  if (Config->Strip != StripPolicy::None)
    llvm::erase_if(InputSections, [](InputSectionBase *S) {
      return S->Name.startswith(".debug") || S->Name.startswith(".zdebug");
    });

  Config->EFlags = Target->calcEFlags();

  if (Config->EMachine == EM_ARM) {
    // FIXME: These warnings can be removed when lld only uses these features
    // when the input objects have been compiled with an architecture that
    // supports them.
    if (Config->ARMHasBlx == false)
      warn("lld uses blx instruction, no object with architecture supporting "
           "feature detected");
  }

  // This adds a .comment section containing a version string. We have to add it
  // before mergeSections because the .comment section is a mergeable section.
  if (!Config->Relocatable)
    InputSections.push_back(createCommentSection());

  // Do size optimizations: garbage collection, merging of SHF_MERGE sections
  // and identical code folding.
  splitSections<ELFT>();
  markLive<ELFT>();
  demoteSharedSymbols<ELFT>();
  mergeSections();
  if (Config->ICF != ICFLevel::None) {
    findKeepUniqueSections<ELFT>(Args);
    doIcf<ELFT>();
  }

  // Read the callgraph now that we know what was gced or icfed
  if (Config->CallGraphProfileSort) {
    if (auto *Arg = Args.getLastArg(OPT_call_graph_ordering_file))
      if (Optional<MemoryBufferRef> Buffer = readFile(Arg->getValue()))
        readCallGraph(*Buffer);
    readCallGraphsFromObjectFiles<ELFT>();
  }

  // Write the result to the file.
  writeResult<ELFT>();
}
