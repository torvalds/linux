//===- lib/Driver/DarwinLdDriver.cpp --------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// Concrete instance of the Driver for darwin's ld.
///
//===----------------------------------------------------------------------===//

#include "lld/Common/Args.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/LLVM.h"
#include "lld/Core/ArchiveLibraryFile.h"
#include "lld/Core/Error.h"
#include "lld/Core/File.h"
#include "lld/Core/Instrumentation.h"
#include "lld/Core/LinkingContext.h"
#include "lld/Core/Node.h"
#include "lld/Core/PassManager.h"
#include "lld/Core/Resolver.h"
#include "lld/Core/SharedLibraryFile.h"
#include "lld/Core/Simple.h"
#include "lld/ReaderWriter/MachOLinkingContext.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace lld;

namespace {

// Create enum with OPT_xxx values for each option in DarwinLdOptions.td
enum {
  OPT_INVALID = 0,
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELP, META, VALUES)                                             \
  OPT_##ID,
#include "DarwinLdOptions.inc"
#undef OPTION
};

// Create prefix string literals used in DarwinLdOptions.td
#define PREFIX(NAME, VALUE) const char *const NAME[] = VALUE;
#include "DarwinLdOptions.inc"
#undef PREFIX

// Create table mapping all options defined in DarwinLdOptions.td
static const llvm::opt::OptTable::Info InfoTable[] = {
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELPTEXT, METAVAR, VALUES)                                      \
  {PREFIX,      NAME,      HELPTEXT,                                           \
   METAVAR,     OPT_##ID,  llvm::opt::Option::KIND##Class,                     \
   PARAM,       FLAGS,     OPT_##GROUP,                                        \
   OPT_##ALIAS, ALIASARGS, VALUES},
#include "DarwinLdOptions.inc"
#undef OPTION
};

// Create OptTable class for parsing actual command line arguments
class DarwinLdOptTable : public llvm::opt::OptTable {
public:
  DarwinLdOptTable() : OptTable(InfoTable) {}
};

static std::vector<std::unique_ptr<File>>
makeErrorFile(StringRef path, std::error_code ec) {
  std::vector<std::unique_ptr<File>> result;
  result.push_back(llvm::make_unique<ErrorFile>(path, ec));
  return result;
}

static std::vector<std::unique_ptr<File>>
parseMemberFiles(std::unique_ptr<File> file) {
  std::vector<std::unique_ptr<File>> members;
  if (auto *archive = dyn_cast<ArchiveLibraryFile>(file.get())) {
    if (std::error_code ec = archive->parseAllMembers(members))
      return makeErrorFile(file->path(), ec);
  } else {
    members.push_back(std::move(file));
  }
  return members;
}

std::vector<std::unique_ptr<File>> loadFile(MachOLinkingContext &ctx,
                                            StringRef path, bool wholeArchive,
                                            bool upwardDylib) {
  if (ctx.logInputFiles())
    message(path);

  ErrorOr<std::unique_ptr<MemoryBuffer>> mbOrErr = ctx.getMemoryBuffer(path);
  if (std::error_code ec = mbOrErr.getError())
    return makeErrorFile(path, ec);
  ErrorOr<std::unique_ptr<File>> fileOrErr =
      ctx.registry().loadFile(std::move(mbOrErr.get()));
  if (std::error_code ec = fileOrErr.getError())
    return makeErrorFile(path, ec);
  std::unique_ptr<File> &file = fileOrErr.get();

  // If file is a dylib, inform LinkingContext about it.
  if (SharedLibraryFile *shl = dyn_cast<SharedLibraryFile>(file.get())) {
    if (std::error_code ec = shl->parse())
      return makeErrorFile(path, ec);
    ctx.registerDylib(reinterpret_cast<mach_o::MachODylibFile *>(shl),
                      upwardDylib);
  }
  if (wholeArchive)
    return parseMemberFiles(std::move(file));
  std::vector<std::unique_ptr<File>> files;
  files.push_back(std::move(file));
  return files;
}

} // end anonymous namespace

// Test may be running on Windows. Canonicalize the path
// separator to '/' to get consistent outputs for tests.
static std::string canonicalizePath(StringRef path) {
  char sep = llvm::sys::path::get_separator().front();
  if (sep != '/') {
    std::string fixedPath = path;
    std::replace(fixedPath.begin(), fixedPath.end(), sep, '/');
    return fixedPath;
  } else {
    return path;
  }
}

static void addFile(StringRef path, MachOLinkingContext &ctx,
                    bool loadWholeArchive, bool upwardDylib) {
  std::vector<std::unique_ptr<File>> files =
      loadFile(ctx, path, loadWholeArchive, upwardDylib);
  for (std::unique_ptr<File> &file : files)
    ctx.getNodes().push_back(llvm::make_unique<FileNode>(std::move(file)));
}

// Export lists are one symbol per line.  Blank lines are ignored.
// Trailing comments start with #.
static std::error_code parseExportsList(StringRef exportFilePath,
                                        MachOLinkingContext &ctx) {
  // Map in export list file.
  ErrorOr<std::unique_ptr<MemoryBuffer>> mb =
                                   MemoryBuffer::getFileOrSTDIN(exportFilePath);
  if (std::error_code ec = mb.getError())
    return ec;
  ctx.addInputFileDependency(exportFilePath);
  StringRef buffer = mb->get()->getBuffer();
  while (!buffer.empty()) {
    // Split off each line in the file.
    std::pair<StringRef, StringRef> lineAndRest = buffer.split('\n');
    StringRef line = lineAndRest.first;
    // Ignore trailing # comments.
    std::pair<StringRef, StringRef> symAndComment = line.split('#');
    StringRef sym = symAndComment.first.trim();
    if (!sym.empty())
      ctx.addExportSymbol(sym);
    buffer = lineAndRest.second;
  }
  return std::error_code();
}

/// Order files are one symbol per line. Blank lines are ignored.
/// Trailing comments start with #. Symbol names can be prefixed with an
/// architecture name and/or .o leaf name.  Examples:
///     _foo
///     bar.o:_bar
///     libfrob.a(bar.o):_bar
///     x86_64:_foo64
static std::error_code parseOrderFile(StringRef orderFilePath,
                                      MachOLinkingContext &ctx) {
  // Map in order file.
  ErrorOr<std::unique_ptr<MemoryBuffer>> mb =
                                   MemoryBuffer::getFileOrSTDIN(orderFilePath);
  if (std::error_code ec = mb.getError())
    return ec;
  ctx.addInputFileDependency(orderFilePath);
  StringRef buffer = mb->get()->getBuffer();
  while (!buffer.empty()) {
    // Split off each line in the file.
    std::pair<StringRef, StringRef> lineAndRest = buffer.split('\n');
    StringRef line = lineAndRest.first;
    buffer = lineAndRest.second;
    // Ignore trailing # comments.
    std::pair<StringRef, StringRef> symAndComment = line.split('#');
    if (symAndComment.first.empty())
      continue;
    StringRef sym = symAndComment.first.trim();
    if (sym.empty())
      continue;
    // Check for prefix.
    StringRef prefix;
    std::pair<StringRef, StringRef> prefixAndSym = sym.split(':');
    if (!prefixAndSym.second.empty()) {
      sym = prefixAndSym.second;
      prefix = prefixAndSym.first;
      if (!prefix.endswith(".o") && !prefix.endswith(".o)")) {
        // If arch name prefix does not match arch being linked, ignore symbol.
        if (!ctx.archName().equals(prefix))
          continue;
        prefix = "";
      }
    } else
     sym = prefixAndSym.first;
    if (!sym.empty()) {
      ctx.appendOrderedSymbol(sym, prefix);
      //llvm::errs() << sym << ", prefix=" << prefix << "\n";
    }
  }
  return std::error_code();
}

//
// There are two variants of the  -filelist option:
//
//   -filelist <path>
// In this variant, the path is to a text file which contains one file path
// per line.  There are no comments or trimming of whitespace.
//
//   -fileList <path>,<dir>
// In this variant, the path is to a text file which contains a partial path
// per line. The <dir> prefix is prepended to each partial path.
//
static llvm::Error loadFileList(StringRef fileListPath,
                                MachOLinkingContext &ctx, bool forceLoad) {
  // If there is a comma, split off <dir>.
  std::pair<StringRef, StringRef> opt = fileListPath.split(',');
  StringRef filePath = opt.first;
  StringRef dirName = opt.second;
  ctx.addInputFileDependency(filePath);
  // Map in file list file.
  ErrorOr<std::unique_ptr<MemoryBuffer>> mb =
                                        MemoryBuffer::getFileOrSTDIN(filePath);
  if (std::error_code ec = mb.getError())
    return llvm::errorCodeToError(ec);
  StringRef buffer = mb->get()->getBuffer();
  while (!buffer.empty()) {
    // Split off each line in the file.
    std::pair<StringRef, StringRef> lineAndRest = buffer.split('\n');
    StringRef line = lineAndRest.first;
    StringRef path;
    if (!dirName.empty()) {
      // If there is a <dir> then prepend dir to each line.
      SmallString<256> fullPath;
      fullPath.assign(dirName);
      llvm::sys::path::append(fullPath, Twine(line));
      path = ctx.copy(fullPath.str());
    } else {
      // No <dir> use whole line as input file path.
      path = ctx.copy(line);
    }
    if (!ctx.pathExists(path)) {
      return llvm::make_error<GenericError>(Twine("File not found '")
                                            + path
                                            + "'");
    }
    if (ctx.testingFileUsage()) {
      message("Found filelist entry " + canonicalizePath(path));
    }
    addFile(path, ctx, forceLoad, false);
    buffer = lineAndRest.second;
  }
  return llvm::Error::success();
}

/// Parse number assuming it is base 16, but allow 0x prefix.
static bool parseNumberBase16(StringRef numStr, uint64_t &baseAddress) {
  if (numStr.startswith_lower("0x"))
    numStr = numStr.drop_front(2);
  return numStr.getAsInteger(16, baseAddress);
}

static void parseLLVMOptions(const LinkingContext &ctx) {
  // Honor -mllvm
  if (!ctx.llvmOptions().empty()) {
    unsigned numArgs = ctx.llvmOptions().size();
    auto **args = new const char *[numArgs + 2];
    args[0] = "lld (LLVM option parsing)";
    for (unsigned i = 0; i != numArgs; ++i)
      args[i + 1] = ctx.llvmOptions()[i];
    args[numArgs + 1] = nullptr;
    llvm::cl::ParseCommandLineOptions(numArgs + 1, args);
  }
}

namespace lld {
namespace mach_o {

bool parse(llvm::ArrayRef<const char *> args, MachOLinkingContext &ctx) {
  // Parse command line options using DarwinLdOptions.td
  DarwinLdOptTable table;
  unsigned missingIndex;
  unsigned missingCount;
  llvm::opt::InputArgList parsedArgs =
      table.ParseArgs(args.slice(1), missingIndex, missingCount);
  if (missingCount) {
    error("missing arg value for '" +
          Twine(parsedArgs.getArgString(missingIndex)) + "' expected " +
          Twine(missingCount) + " argument(s).");
    return false;
  }

  for (auto unknownArg : parsedArgs.filtered(OPT_UNKNOWN)) {
    warn("ignoring unknown argument: " +
         Twine(unknownArg->getAsString(parsedArgs)));
  }

  errorHandler().Verbose = parsedArgs.hasArg(OPT_v);
  errorHandler().ErrorLimit = args::getInteger(parsedArgs, OPT_error_limit, 20);

  // Figure out output kind ( -dylib, -r, -bundle, -preload, or -static )
  llvm::MachO::HeaderFileType fileType = llvm::MachO::MH_EXECUTE;
  bool isStaticExecutable = false;
  if (llvm::opt::Arg *kind = parsedArgs.getLastArg(
          OPT_dylib, OPT_relocatable, OPT_bundle, OPT_static, OPT_preload)) {
    switch (kind->getOption().getID()) {
    case OPT_dylib:
      fileType = llvm::MachO::MH_DYLIB;
      break;
    case OPT_relocatable:
      fileType = llvm::MachO::MH_OBJECT;
      break;
    case OPT_bundle:
      fileType = llvm::MachO::MH_BUNDLE;
      break;
    case OPT_static:
      fileType = llvm::MachO::MH_EXECUTE;
      isStaticExecutable = true;
      break;
    case OPT_preload:
      fileType = llvm::MachO::MH_PRELOAD;
      break;
    }
  }

  // Handle -arch xxx
  MachOLinkingContext::Arch arch = MachOLinkingContext::arch_unknown;
  if (llvm::opt::Arg *archStr = parsedArgs.getLastArg(OPT_arch)) {
    arch = MachOLinkingContext::archFromName(archStr->getValue());
    if (arch == MachOLinkingContext::arch_unknown) {
      error("unknown arch named '" + Twine(archStr->getValue()) + "'");
      return false;
    }
  }
  // If no -arch specified, scan input files to find first non-fat .o file.
  if (arch == MachOLinkingContext::arch_unknown) {
    for (auto &inFile : parsedArgs.filtered(OPT_INPUT)) {
      // This is expensive because it opens and maps the file.  But that is
      // ok because no -arch is rare.
      if (MachOLinkingContext::isThinObjectFile(inFile->getValue(), arch))
        break;
    }
    if (arch == MachOLinkingContext::arch_unknown &&
        !parsedArgs.getLastArg(OPT_test_file_usage)) {
      // If no -arch and no options at all, print usage message.
      if (parsedArgs.size() == 0) {
        table.PrintHelp(llvm::outs(),
                        (std::string(args[0]) + " [options] file...").c_str(),
                        "LLVM Linker", false);
      } else {
        error("-arch not specified and could not be inferred");
      }
      return false;
    }
  }

  // Handle -macosx_version_min or -ios_version_min
  MachOLinkingContext::OS os = MachOLinkingContext::OS::unknown;
  uint32_t minOSVersion = 0;
  if (llvm::opt::Arg *minOS =
          parsedArgs.getLastArg(OPT_macosx_version_min, OPT_ios_version_min,
                                OPT_ios_simulator_version_min)) {
    switch (minOS->getOption().getID()) {
    case OPT_macosx_version_min:
      os = MachOLinkingContext::OS::macOSX;
      if (MachOLinkingContext::parsePackedVersion(minOS->getValue(),
                                                  minOSVersion)) {
        error("malformed macosx_version_min value");
        return false;
      }
      break;
    case OPT_ios_version_min:
      os = MachOLinkingContext::OS::iOS;
      if (MachOLinkingContext::parsePackedVersion(minOS->getValue(),
                                                  minOSVersion)) {
        error("malformed ios_version_min value");
        return false;
      }
      break;
    case OPT_ios_simulator_version_min:
      os = MachOLinkingContext::OS::iOS_simulator;
      if (MachOLinkingContext::parsePackedVersion(minOS->getValue(),
                                                  minOSVersion)) {
        error("malformed ios_simulator_version_min value");
        return false;
      }
      break;
    }
  } else {
    // No min-os version on command line, check environment variables
  }

  // Handle export_dynamic
  // FIXME: Should we warn when this applies to something other than a static
  // executable or dylib?  Those are the only cases where this has an effect.
  // Note, this has to come before ctx.configure() so that we get the correct
  // value for _globalsAreDeadStripRoots.
  bool exportDynamicSymbols = parsedArgs.hasArg(OPT_export_dynamic);

  // Now that there's enough information parsed in, let the linking context
  // set up default values.
  ctx.configure(fileType, arch, os, minOSVersion, exportDynamicSymbols);

  // Handle -e xxx
  if (llvm::opt::Arg *entry = parsedArgs.getLastArg(OPT_entry))
    ctx.setEntrySymbolName(entry->getValue());

  // Handle -o xxx
  if (llvm::opt::Arg *outpath = parsedArgs.getLastArg(OPT_output))
    ctx.setOutputPath(outpath->getValue());
  else
    ctx.setOutputPath("a.out");

  // Handle -image_base XXX and -seg1addr XXXX
  if (llvm::opt::Arg *imageBase = parsedArgs.getLastArg(OPT_image_base)) {
    uint64_t baseAddress;
    if (parseNumberBase16(imageBase->getValue(), baseAddress)) {
      error("image_base expects a hex number");
      return false;
    } else if (baseAddress < ctx.pageZeroSize()) {
      error("image_base overlaps with __PAGEZERO");
      return false;
    } else if (baseAddress % ctx.pageSize()) {
      error("image_base must be a multiple of page size (0x" +
            llvm::utohexstr(ctx.pageSize()) + ")");
      return false;
    }

    ctx.setBaseAddress(baseAddress);
  }

  // Handle -dead_strip
  if (parsedArgs.getLastArg(OPT_dead_strip))
    ctx.setDeadStripping(true);

  bool globalWholeArchive = false;
  // Handle -all_load
  if (parsedArgs.getLastArg(OPT_all_load))
    globalWholeArchive = true;

  // Handle -install_name
  if (llvm::opt::Arg *installName = parsedArgs.getLastArg(OPT_install_name))
    ctx.setInstallName(installName->getValue());
  else
    ctx.setInstallName(ctx.outputPath());

  // Handle -mark_dead_strippable_dylib
  if (parsedArgs.getLastArg(OPT_mark_dead_strippable_dylib))
    ctx.setDeadStrippableDylib(true);

  // Handle -compatibility_version and -current_version
  if (llvm::opt::Arg *vers = parsedArgs.getLastArg(OPT_compatibility_version)) {
    if (ctx.outputMachOType() != llvm::MachO::MH_DYLIB) {
      error("-compatibility_version can only be used with -dylib");
      return false;
    }
    uint32_t parsedVers;
    if (MachOLinkingContext::parsePackedVersion(vers->getValue(), parsedVers)) {
      error("-compatibility_version value is malformed");
      return false;
    }
    ctx.setCompatibilityVersion(parsedVers);
  }

  if (llvm::opt::Arg *vers = parsedArgs.getLastArg(OPT_current_version)) {
    if (ctx.outputMachOType() != llvm::MachO::MH_DYLIB) {
      error("-current_version can only be used with -dylib");
      return false;
    }
    uint32_t parsedVers;
    if (MachOLinkingContext::parsePackedVersion(vers->getValue(), parsedVers)) {
      error("-current_version value is malformed");
      return false;
    }
    ctx.setCurrentVersion(parsedVers);
  }

  // Handle -bundle_loader
  if (llvm::opt::Arg *loader = parsedArgs.getLastArg(OPT_bundle_loader))
    ctx.setBundleLoader(loader->getValue());

  // Handle -sectalign segname sectname align
  for (auto &alignArg : parsedArgs.filtered(OPT_sectalign)) {
    const char* segName   = alignArg->getValue(0);
    const char* sectName  = alignArg->getValue(1);
    const char* alignStr  = alignArg->getValue(2);
    if ((alignStr[0] == '0') && (alignStr[1] == 'x'))
      alignStr += 2;
    unsigned long long alignValue;
    if (llvm::getAsUnsignedInteger(alignStr, 16, alignValue)) {
      error("-sectalign alignment value '" + Twine(alignStr) +
            "' not a valid number");
      return false;
    }
    uint16_t align = 1 << llvm::countTrailingZeros(alignValue);
    if (!llvm::isPowerOf2_64(alignValue)) {
      std::string Msg;
      llvm::raw_string_ostream OS(Msg);
      OS << "alignment for '-sectalign " << segName << " " << sectName
         << llvm::format(" 0x%llX", alignValue)
         << "' is not a power of two, using " << llvm::format("0x%08X", align);
      OS.flush();
      warn(Msg);
    }
    ctx.addSectionAlignment(segName, sectName, align);
  }

  // Handle -mllvm
  for (auto &llvmArg : parsedArgs.filtered(OPT_mllvm)) {
    ctx.appendLLVMOption(llvmArg->getValue());
  }

  // Handle -print_atoms
  if (parsedArgs.getLastArg(OPT_print_atoms))
    ctx.setPrintAtoms();

  // Handle -t (trace) option.
  if (parsedArgs.getLastArg(OPT_t))
    ctx.setLogInputFiles(true);

  // Handle -demangle option.
  if (parsedArgs.getLastArg(OPT_demangle))
    ctx.setDemangleSymbols(true);

  // Handle -keep_private_externs
  if (parsedArgs.getLastArg(OPT_keep_private_externs)) {
    ctx.setKeepPrivateExterns(true);
    if (ctx.outputMachOType() != llvm::MachO::MH_OBJECT)
      warn("-keep_private_externs only used in -r mode");
  }

  // Handle -dependency_info <path> used by Xcode.
  if (llvm::opt::Arg *depInfo = parsedArgs.getLastArg(OPT_dependency_info))
    if (std::error_code ec = ctx.createDependencyFile(depInfo->getValue()))
      warn(ec.message() + ", processing '-dependency_info " +
           depInfo->getValue());

  // In -test_file_usage mode, we'll be given an explicit list of paths that
  // exist. We'll also be expected to print out information about how we located
  // libraries and so on that the user specified, but not to actually do any
  // linking.
  if (parsedArgs.getLastArg(OPT_test_file_usage)) {
    ctx.setTestingFileUsage();

    // With paths existing by fiat, linking is not going to end well.
    ctx.setDoNothing(true);

    // Only bother looking for an existence override if we're going to use it.
    for (auto existingPath : parsedArgs.filtered(OPT_path_exists)) {
      ctx.addExistingPathForDebug(existingPath->getValue());
    }
  }

  // Register possible input file parsers.
  if (!ctx.doNothing()) {
    ctx.registry().addSupportMachOObjects(ctx);
    ctx.registry().addSupportArchives(ctx.logInputFiles());
    ctx.registry().addSupportYamlFiles();
  }

  // Now construct the set of library search directories, following ld64's
  // baroque set of accumulated hacks. Mostly, the algorithm constructs
  //     { syslibroots } x { libpaths }
  //
  // Unfortunately, there are numerous exceptions:
  //   1. Only absolute paths get modified by syslibroot options.
  //   2. If there is just 1 -syslibroot, system paths not found in it are
  //      skipped.
  //   3. If the last -syslibroot is "/", all of them are ignored entirely.
  //   4. If { syslibroots } x path ==  {}, the original path is kept.
  std::vector<StringRef> sysLibRoots;
  for (auto syslibRoot : parsedArgs.filtered(OPT_syslibroot)) {
    sysLibRoots.push_back(syslibRoot->getValue());
  }
  if (!sysLibRoots.empty()) {
    // Ignore all if last -syslibroot is "/".
    if (sysLibRoots.back() != "/")
      ctx.setSysLibRoots(sysLibRoots);
  }

  // Paths specified with -L come first, and are not considered system paths for
  // the case where there is precisely 1 -syslibroot.
  for (auto libPath : parsedArgs.filtered(OPT_L)) {
    ctx.addModifiedSearchDir(libPath->getValue());
  }

  // Process -F directories (where to look for frameworks).
  for (auto fwPath : parsedArgs.filtered(OPT_F)) {
    ctx.addFrameworkSearchDir(fwPath->getValue());
  }

  // -Z suppresses the standard search paths.
  if (!parsedArgs.hasArg(OPT_Z)) {
    ctx.addModifiedSearchDir("/usr/lib", true);
    ctx.addModifiedSearchDir("/usr/local/lib", true);
    ctx.addFrameworkSearchDir("/Library/Frameworks", true);
    ctx.addFrameworkSearchDir("/System/Library/Frameworks", true);
  }

  // Now that we've constructed the final set of search paths, print out those
  // search paths in verbose mode.
  if (errorHandler().Verbose) {
    message("Library search paths:");
    for (auto path : ctx.searchDirs()) {
      message("    " + path);
    }
    message("Framework search paths:");
    for (auto path : ctx.frameworkDirs()) {
      message("    " + path);
    }
  }

  // Handle -exported_symbols_list <file>
  for (auto expFile : parsedArgs.filtered(OPT_exported_symbols_list)) {
    if (ctx.exportMode() == MachOLinkingContext::ExportMode::blackList) {
      error("-exported_symbols_list cannot be combined with "
            "-unexported_symbol[s_list]");
      return false;
    }
    ctx.setExportMode(MachOLinkingContext::ExportMode::whiteList);
    if (std::error_code ec = parseExportsList(expFile->getValue(), ctx)) {
      error(ec.message() + ", processing '-exported_symbols_list " +
            expFile->getValue());
      return false;
    }
  }

  // Handle -exported_symbol <symbol>
  for (auto symbol : parsedArgs.filtered(OPT_exported_symbol)) {
    if (ctx.exportMode() == MachOLinkingContext::ExportMode::blackList) {
      error("-exported_symbol cannot be combined with "
            "-unexported_symbol[s_list]");
      return false;
    }
    ctx.setExportMode(MachOLinkingContext::ExportMode::whiteList);
    ctx.addExportSymbol(symbol->getValue());
  }

  // Handle -unexported_symbols_list <file>
  for (auto expFile : parsedArgs.filtered(OPT_unexported_symbols_list)) {
    if (ctx.exportMode() == MachOLinkingContext::ExportMode::whiteList) {
      error("-unexported_symbols_list cannot be combined with "
            "-exported_symbol[s_list]");
      return false;
    }
    ctx.setExportMode(MachOLinkingContext::ExportMode::blackList);
    if (std::error_code ec = parseExportsList(expFile->getValue(), ctx)) {
      error(ec.message() + ", processing '-unexported_symbols_list " +
            expFile->getValue());
      return false;
    }
  }

  // Handle -unexported_symbol <symbol>
  for (auto symbol : parsedArgs.filtered(OPT_unexported_symbol)) {
    if (ctx.exportMode() == MachOLinkingContext::ExportMode::whiteList) {
      error("-unexported_symbol cannot be combined with "
            "-exported_symbol[s_list]");
      return false;
    }
    ctx.setExportMode(MachOLinkingContext::ExportMode::blackList);
    ctx.addExportSymbol(symbol->getValue());
  }

  // Handle obosolete -multi_module and -single_module
  if (llvm::opt::Arg *mod =
          parsedArgs.getLastArg(OPT_multi_module, OPT_single_module)) {
    if (mod->getOption().getID() == OPT_multi_module)
      warn("-multi_module is obsolete and being ignored");
    else if (ctx.outputMachOType() != llvm::MachO::MH_DYLIB)
      warn("-single_module being ignored. It is only for use when producing a "
           "dylib");
  }

  // Handle obsolete ObjC options: -objc_gc_compaction, -objc_gc, -objc_gc_only
  if (parsedArgs.getLastArg(OPT_objc_gc_compaction)) {
    error("-objc_gc_compaction is not supported");
    return false;
  }

  if (parsedArgs.getLastArg(OPT_objc_gc)) {
    error("-objc_gc is not supported");
    return false;
  }

  if (parsedArgs.getLastArg(OPT_objc_gc_only)) {
    error("-objc_gc_only is not supported");
    return false;
  }

  // Handle -pie or -no_pie
  if (llvm::opt::Arg *pie = parsedArgs.getLastArg(OPT_pie, OPT_no_pie)) {
    switch (ctx.outputMachOType()) {
    case llvm::MachO::MH_EXECUTE:
      switch (ctx.os()) {
      case MachOLinkingContext::OS::macOSX:
        if ((minOSVersion < 0x000A0500) &&
            (pie->getOption().getID() == OPT_pie)) {
          error("-pie can only be used when targeting Mac OS X 10.5 or later");
          return false;
        }
        break;
      case MachOLinkingContext::OS::iOS:
        if ((minOSVersion < 0x00040200) &&
            (pie->getOption().getID() == OPT_pie)) {
          error("-pie can only be used when targeting iOS 4.2 or later");
          return false;
        }
        break;
      case MachOLinkingContext::OS::iOS_simulator:
        if (pie->getOption().getID() == OPT_no_pie) {
          error("iOS simulator programs must be built PIE");
          return false;
        }
        break;
      case MachOLinkingContext::OS::unknown:
        break;
      }
      ctx.setPIE(pie->getOption().getID() == OPT_pie);
      break;
    case llvm::MachO::MH_PRELOAD:
      break;
    case llvm::MachO::MH_DYLIB:
    case llvm::MachO::MH_BUNDLE:
      warn(pie->getSpelling() +
           " being ignored. It is only used when linking main executables");
      break;
    default:
      error(pie->getSpelling() +
            " can only used when linking main executables");
      return false;
    }
  }

  // Handle -version_load_command or -no_version_load_command
  {
    bool flagOn = false;
    bool flagOff = false;
    if (auto *arg = parsedArgs.getLastArg(OPT_version_load_command,
                                          OPT_no_version_load_command)) {
      flagOn = arg->getOption().getID() == OPT_version_load_command;
      flagOff = arg->getOption().getID() == OPT_no_version_load_command;
    }

    // default to adding version load command for dynamic code,
    // static code must opt-in
    switch (ctx.outputMachOType()) {
      case llvm::MachO::MH_OBJECT:
        ctx.setGenerateVersionLoadCommand(false);
        break;
      case llvm::MachO::MH_EXECUTE:
        // dynamic executables default to generating a version load command,
        // while static exectuables only generate it if required.
        if (isStaticExecutable) {
          if (flagOn)
            ctx.setGenerateVersionLoadCommand(true);
        } else {
          if (!flagOff)
            ctx.setGenerateVersionLoadCommand(true);
        }
        break;
      case llvm::MachO::MH_PRELOAD:
      case llvm::MachO::MH_KEXT_BUNDLE:
        if (flagOn)
          ctx.setGenerateVersionLoadCommand(true);
        break;
      case llvm::MachO::MH_DYLINKER:
      case llvm::MachO::MH_DYLIB:
      case llvm::MachO::MH_BUNDLE:
        if (!flagOff)
          ctx.setGenerateVersionLoadCommand(true);
        break;
      case llvm::MachO::MH_FVMLIB:
      case llvm::MachO::MH_DYLDLINK:
      case llvm::MachO::MH_DYLIB_STUB:
      case llvm::MachO::MH_DSYM:
        // We don't generate load commands for these file types, even if
        // forced on.
        break;
    }
  }

  // Handle -function_starts or -no_function_starts
  {
    bool flagOn = false;
    bool flagOff = false;
    if (auto *arg = parsedArgs.getLastArg(OPT_function_starts,
                                          OPT_no_function_starts)) {
      flagOn = arg->getOption().getID() == OPT_function_starts;
      flagOff = arg->getOption().getID() == OPT_no_function_starts;
    }

    // default to adding functions start for dynamic code, static code must
    // opt-in
    switch (ctx.outputMachOType()) {
      case llvm::MachO::MH_OBJECT:
        ctx.setGenerateFunctionStartsLoadCommand(false);
        break;
      case llvm::MachO::MH_EXECUTE:
        // dynamic executables default to generating a version load command,
        // while static exectuables only generate it if required.
        if (isStaticExecutable) {
          if (flagOn)
            ctx.setGenerateFunctionStartsLoadCommand(true);
        } else {
          if (!flagOff)
            ctx.setGenerateFunctionStartsLoadCommand(true);
        }
        break;
      case llvm::MachO::MH_PRELOAD:
      case llvm::MachO::MH_KEXT_BUNDLE:
        if (flagOn)
          ctx.setGenerateFunctionStartsLoadCommand(true);
        break;
      case llvm::MachO::MH_DYLINKER:
      case llvm::MachO::MH_DYLIB:
      case llvm::MachO::MH_BUNDLE:
        if (!flagOff)
          ctx.setGenerateFunctionStartsLoadCommand(true);
        break;
      case llvm::MachO::MH_FVMLIB:
      case llvm::MachO::MH_DYLDLINK:
      case llvm::MachO::MH_DYLIB_STUB:
      case llvm::MachO::MH_DSYM:
        // We don't generate load commands for these file types, even if
        // forced on.
        break;
    }
  }

  // Handle -data_in_code_info or -no_data_in_code_info
  {
    bool flagOn = false;
    bool flagOff = false;
    if (auto *arg = parsedArgs.getLastArg(OPT_data_in_code_info,
                                          OPT_no_data_in_code_info)) {
      flagOn = arg->getOption().getID() == OPT_data_in_code_info;
      flagOff = arg->getOption().getID() == OPT_no_data_in_code_info;
    }

    // default to adding data in code for dynamic code, static code must
    // opt-in
    switch (ctx.outputMachOType()) {
      case llvm::MachO::MH_OBJECT:
        if (!flagOff)
          ctx.setGenerateDataInCodeLoadCommand(true);
        break;
      case llvm::MachO::MH_EXECUTE:
        // dynamic executables default to generating a version load command,
        // while static exectuables only generate it if required.
        if (isStaticExecutable) {
          if (flagOn)
            ctx.setGenerateDataInCodeLoadCommand(true);
        } else {
          if (!flagOff)
            ctx.setGenerateDataInCodeLoadCommand(true);
        }
        break;
      case llvm::MachO::MH_PRELOAD:
      case llvm::MachO::MH_KEXT_BUNDLE:
        if (flagOn)
          ctx.setGenerateDataInCodeLoadCommand(true);
        break;
      case llvm::MachO::MH_DYLINKER:
      case llvm::MachO::MH_DYLIB:
      case llvm::MachO::MH_BUNDLE:
        if (!flagOff)
          ctx.setGenerateDataInCodeLoadCommand(true);
        break;
      case llvm::MachO::MH_FVMLIB:
      case llvm::MachO::MH_DYLDLINK:
      case llvm::MachO::MH_DYLIB_STUB:
      case llvm::MachO::MH_DSYM:
        // We don't generate load commands for these file types, even if
        // forced on.
        break;
    }
  }

  // Handle sdk_version
  if (llvm::opt::Arg *arg = parsedArgs.getLastArg(OPT_sdk_version)) {
    uint32_t sdkVersion = 0;
    if (MachOLinkingContext::parsePackedVersion(arg->getValue(),
                                                sdkVersion)) {
      error("malformed sdkVersion value");
      return false;
    }
    ctx.setSdkVersion(sdkVersion);
  } else if (ctx.generateVersionLoadCommand()) {
    // If we don't have an sdk version, but were going to emit a load command
    // with min_version, then we need to give an warning as we have no sdk
    // version to put in that command.
    // FIXME: We need to decide whether to make this an error.
    warn("-sdk_version is required when emitting min version load command.  "
         "Setting sdk version to match provided min version");
    ctx.setSdkVersion(ctx.osMinVersion());
  }

  // Handle source_version
  if (llvm::opt::Arg *arg = parsedArgs.getLastArg(OPT_source_version)) {
    uint64_t version = 0;
    if (MachOLinkingContext::parsePackedVersion(arg->getValue(),
                                                version)) {
      error("malformed source_version value");
      return false;
    }
    ctx.setSourceVersion(version);
  }

  // Handle stack_size
  if (llvm::opt::Arg *stackSize = parsedArgs.getLastArg(OPT_stack_size)) {
    uint64_t stackSizeVal;
    if (parseNumberBase16(stackSize->getValue(), stackSizeVal)) {
      error("stack_size expects a hex number");
      return false;
    }
    if ((stackSizeVal % ctx.pageSize()) != 0) {
      error("stack_size must be a multiple of page size (0x" +
            llvm::utohexstr(ctx.pageSize()) + ")");
      return false;
    }

    ctx.setStackSize(stackSizeVal);
  }

  // Handle debug info handling options: -S
  if (parsedArgs.hasArg(OPT_S))
    ctx.setDebugInfoMode(MachOLinkingContext::DebugInfoMode::noDebugMap);

  // Handle -order_file <file>
  for (auto orderFile : parsedArgs.filtered(OPT_order_file)) {
    if (std::error_code ec = parseOrderFile(orderFile->getValue(), ctx)) {
      error(ec.message() + ", processing '-order_file " + orderFile->getValue()
            + "'");
      return false;
    }
  }

  // Handle -flat_namespace.
  if (llvm::opt::Arg *ns =
          parsedArgs.getLastArg(OPT_flat_namespace, OPT_twolevel_namespace)) {
    if (ns->getOption().getID() == OPT_flat_namespace)
      ctx.setUseFlatNamespace(true);
  }

  // Handle -undefined
  if (llvm::opt::Arg *undef = parsedArgs.getLastArg(OPT_undefined)) {
    MachOLinkingContext::UndefinedMode UndefMode;
    if (StringRef(undef->getValue()).equals("error"))
      UndefMode = MachOLinkingContext::UndefinedMode::error;
    else if (StringRef(undef->getValue()).equals("warning"))
      UndefMode = MachOLinkingContext::UndefinedMode::warning;
    else if (StringRef(undef->getValue()).equals("suppress"))
      UndefMode = MachOLinkingContext::UndefinedMode::suppress;
    else if (StringRef(undef->getValue()).equals("dynamic_lookup"))
      UndefMode = MachOLinkingContext::UndefinedMode::dynamicLookup;
    else {
      error("invalid option to -undefined [ warning | error | suppress | "
            "dynamic_lookup ]");
      return false;
    }

    if (ctx.useFlatNamespace()) {
      // If we're using -flat_namespace then 'warning', 'suppress' and
      // 'dynamic_lookup' are all equivalent, so map them to 'suppress'.
      if (UndefMode != MachOLinkingContext::UndefinedMode::error)
        UndefMode = MachOLinkingContext::UndefinedMode::suppress;
    } else {
      // If we're using -twolevel_namespace then 'warning' and 'suppress' are
      // illegal. Emit a diagnostic if they've been (mis)used.
      if (UndefMode == MachOLinkingContext::UndefinedMode::warning ||
          UndefMode == MachOLinkingContext::UndefinedMode::suppress) {
        error("can't use -undefined warning or suppress with "
              "-twolevel_namespace");
        return false;
      }
    }

    ctx.setUndefinedMode(UndefMode);
  }

  // Handle -no_objc_category_merging.
  if (parsedArgs.getLastArg(OPT_no_objc_category_merging))
    ctx.setMergeObjCCategories(false);

  // Handle -rpath <path>
  if (parsedArgs.hasArg(OPT_rpath)) {
    switch (ctx.outputMachOType()) {
      case llvm::MachO::MH_EXECUTE:
      case llvm::MachO::MH_DYLIB:
      case llvm::MachO::MH_BUNDLE:
        if (!ctx.minOS("10.5", "2.0")) {
          if (ctx.os() == MachOLinkingContext::OS::macOSX)
            error("-rpath can only be used when targeting OS X 10.5 or later");
          else
            error("-rpath can only be used when targeting iOS 2.0 or later");
          return false;
        }
        break;
      default:
        error("-rpath can only be used when creating a dynamic final linked "
              "image");
        return false;
    }

    for (auto rPath : parsedArgs.filtered(OPT_rpath)) {
      ctx.addRpath(rPath->getValue());
    }
  }

  // Parse the LLVM options before we process files in case the file handling
  // makes use of things like LLVM_DEBUG().
  parseLLVMOptions(ctx);

  // Handle input files and sectcreate.
  for (auto &arg : parsedArgs) {
    bool upward;
    llvm::Optional<StringRef> resolvedPath;
    switch (arg->getOption().getID()) {
    default:
      continue;
    case OPT_INPUT:
      addFile(arg->getValue(), ctx, globalWholeArchive, false);
      break;
    case OPT_upward_library:
      addFile(arg->getValue(), ctx, false, true);
      break;
    case OPT_force_load:
      addFile(arg->getValue(), ctx, true, false);
      break;
    case OPT_l:
    case OPT_upward_l:
      upward = (arg->getOption().getID() == OPT_upward_l);
      resolvedPath = ctx.searchLibrary(arg->getValue());
      if (!resolvedPath) {
        error("Unable to find library for " + arg->getSpelling() +
              arg->getValue());
        return false;
      } else if (ctx.testingFileUsage()) {
        message(Twine("Found ") + (upward ? "upward " : " ") + "library " +
                canonicalizePath(resolvedPath.getValue()));
      }
      addFile(resolvedPath.getValue(), ctx, globalWholeArchive, upward);
      break;
    case OPT_framework:
    case OPT_upward_framework:
      upward = (arg->getOption().getID() == OPT_upward_framework);
      resolvedPath = ctx.findPathForFramework(arg->getValue());
      if (!resolvedPath) {
        error("Unable to find framework for " + arg->getSpelling() + " " +
              arg->getValue());
        return false;
      } else if (ctx.testingFileUsage()) {
        message(Twine("Found ") + (upward ? "upward " : " ") + "framework " +
                canonicalizePath(resolvedPath.getValue()));
      }
      addFile(resolvedPath.getValue(), ctx, globalWholeArchive, upward);
      break;
    case OPT_filelist:
      if (auto ec = loadFileList(arg->getValue(), ctx, globalWholeArchive)) {
        handleAllErrors(std::move(ec), [&](const llvm::ErrorInfoBase &EI) {
          error(EI.message() + ", processing '-filelist " + arg->getValue());
        });
        return false;
      }
      break;
    case OPT_sectcreate: {
        const char* seg  = arg->getValue(0);
        const char* sect = arg->getValue(1);
        const char* fileName = arg->getValue(2);

        ErrorOr<std::unique_ptr<MemoryBuffer>> contentOrErr =
          MemoryBuffer::getFile(fileName);

        if (!contentOrErr) {
          error("can't open -sectcreate file " + Twine(fileName));
          return false;
        }

        ctx.addSectCreateSection(seg, sect, std::move(*contentOrErr));
      }
      break;
    }
  }

  if (ctx.getNodes().empty()) {
    error("No input files");
    return false;
  }

  // Validate the combination of options used.
  return ctx.validate();
}

static void createFiles(MachOLinkingContext &ctx, bool Implicit) {
  std::vector<std::unique_ptr<File>> Files;
  if (Implicit)
    ctx.createImplicitFiles(Files);
  else
    ctx.createInternalFiles(Files);
  for (auto i = Files.rbegin(), e = Files.rend(); i != e; ++i) {
    auto &members = ctx.getNodes();
    members.insert(members.begin(), llvm::make_unique<FileNode>(std::move(*i)));
  }
}

/// This is where the link is actually performed.
bool link(llvm::ArrayRef<const char *> args, bool CanExitEarly,
          raw_ostream &Error) {
  errorHandler().LogName = args::getFilenameWithoutExe(args[0]);
  errorHandler().ErrorLimitExceededMsg =
      "too many errors emitted, stopping now (use "
      "'-error-limit 0' to see all errors)";
  errorHandler().ErrorOS = &Error;
  errorHandler().ExitEarly = CanExitEarly;
  errorHandler().ColorDiagnostics = Error.has_colors();

  MachOLinkingContext ctx;
  if (!parse(args, ctx))
    return false;
  if (ctx.doNothing())
    return true;
  if (ctx.getNodes().empty())
    return false;

  for (std::unique_ptr<Node> &ie : ctx.getNodes())
    if (FileNode *node = dyn_cast<FileNode>(ie.get()))
      node->getFile()->parse();

  createFiles(ctx, false /* Implicit */);

  // Give target a chance to add files
  createFiles(ctx, true /* Implicit */);

  // Give target a chance to postprocess input files.
  // Mach-O uses this chance to move all object files before library files.
  ctx.finalizeInputFiles();

  // Do core linking.
  ScopedTask resolveTask(getDefaultDomain(), "Resolve");
  Resolver resolver(ctx);
  if (!resolver.resolve())
    return false;
  SimpleFile *merged = nullptr;
  {
    std::unique_ptr<SimpleFile> mergedFile = resolver.resultFile();
    merged = mergedFile.get();
    auto &members = ctx.getNodes();
    members.insert(members.begin(),
                   llvm::make_unique<FileNode>(std::move(mergedFile)));
  }
  resolveTask.end();

  // Run passes on linked atoms.
  ScopedTask passTask(getDefaultDomain(), "Passes");
  PassManager pm;
  ctx.addPasses(pm);
  if (auto ec = pm.runOnFile(*merged)) {
    // FIXME: This should be passed to logAllUnhandledErrors but it needs
    // to be passed a Twine instead of a string.
    *errorHandler().ErrorOS << "Failed to run passes on file '"
                            << ctx.outputPath() << "': ";
    logAllUnhandledErrors(std::move(ec), *errorHandler().ErrorOS,
                          std::string());
    return false;
  }

  passTask.end();

  // Give linked atoms to Writer to generate output file.
  ScopedTask writeTask(getDefaultDomain(), "Write");
  if (auto ec = ctx.writeFile(*merged)) {
    // FIXME: This should be passed to logAllUnhandledErrors but it needs
    // to be passed a Twine instead of a string.
    *errorHandler().ErrorOS << "Failed to write file '" << ctx.outputPath()
                            << "': ";
    logAllUnhandledErrors(std::move(ec), *errorHandler().ErrorOS,
                          std::string());
    return false;
  }

  // Call exit() if we can to avoid calling destructors.
  if (CanExitEarly)
    exitLld(errorCount() ? 1 : 0);


  return true;
}

} // end namespace mach_o
} // end namespace lld
