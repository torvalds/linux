//===- DriverUtils.cpp ----------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains utility functions for the driver. Because there
// are so many small functions, we created this separate file to make
// Driver.cpp less cluttered.
//
//===----------------------------------------------------------------------===//

#include "Driver.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Reproduce.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"

using namespace llvm;
using namespace llvm::sys;
using namespace llvm::opt;

using namespace lld;
using namespace lld::elf;

// Create OptTable

// Create prefix string literals used in Options.td
#define PREFIX(NAME, VALUE) const char *const NAME[] = VALUE;
#include "Options.inc"
#undef PREFIX

// Create table mapping all options defined in Options.td
static const opt::OptTable::Info OptInfo[] = {
#define OPTION(X1, X2, ID, KIND, GROUP, ALIAS, X7, X8, X9, X10, X11, X12)      \
  {X1, X2, X10,         X11,         OPT_##ID, opt::Option::KIND##Class,       \
   X9, X8, OPT_##GROUP, OPT_##ALIAS, X7,       X12},
#include "Options.inc"
#undef OPTION
};

ELFOptTable::ELFOptTable() : OptTable(OptInfo) {}

// Set color diagnostics according to -color-diagnostics={auto,always,never}
// or -no-color-diagnostics flags.
static void handleColorDiagnostics(opt::InputArgList &Args) {
  auto *Arg = Args.getLastArg(OPT_color_diagnostics, OPT_color_diagnostics_eq,
                              OPT_no_color_diagnostics);
  if (!Arg)
    return;
  if (Arg->getOption().getID() == OPT_color_diagnostics) {
    errorHandler().ColorDiagnostics = true;
  } else if (Arg->getOption().getID() == OPT_no_color_diagnostics) {
    errorHandler().ColorDiagnostics = false;
  } else {
    StringRef S = Arg->getValue();
    if (S == "always")
      errorHandler().ColorDiagnostics = true;
    else if (S == "never")
      errorHandler().ColorDiagnostics = false;
    else if (S != "auto")
      error("unknown option: --color-diagnostics=" + S);
  }
}

static cl::TokenizerCallback getQuotingStyle(opt::InputArgList &Args) {
  if (auto *Arg = Args.getLastArg(OPT_rsp_quoting)) {
    StringRef S = Arg->getValue();
    if (S != "windows" && S != "posix")
      error("invalid response file quoting: " + S);
    if (S == "windows")
      return cl::TokenizeWindowsCommandLine;
    return cl::TokenizeGNUCommandLine;
  }
  if (Triple(sys::getProcessTriple()).getOS() == Triple::Win32)
    return cl::TokenizeWindowsCommandLine;
  return cl::TokenizeGNUCommandLine;
}

// Gold LTO plugin takes a `--plugin-opt foo=bar` option as an alias for
// `--plugin-opt=foo=bar`. We want to handle `--plugin-opt=foo=` as an
// option name and `bar` as a value. Unfortunately, OptParser cannot
// handle an option with a space in it.
//
// In this function, we concatenate command line arguments so that
// `--plugin-opt <foo>` is converted to `--plugin-opt=<foo>`. This is a
// bit hacky, but looks like it is still better than handling --plugin-opt
// options by hand.
static void concatLTOPluginOptions(SmallVectorImpl<const char *> &Args) {
  SmallVector<const char *, 256> V;
  for (size_t I = 0, E = Args.size(); I != E; ++I) {
    StringRef S = Args[I];
    if ((S == "-plugin-opt" || S == "--plugin-opt") && I + 1 != E) {
      V.push_back(Saver.save(S + "=" + Args[I + 1]).data());
      ++I;
    } else {
      V.push_back(Args[I]);
    }
  }
  Args = std::move(V);
}

// Parses a given list of options.
opt::InputArgList ELFOptTable::parse(ArrayRef<const char *> Argv) {
  // Make InputArgList from string vectors.
  unsigned MissingIndex;
  unsigned MissingCount;
  SmallVector<const char *, 256> Vec(Argv.data(), Argv.data() + Argv.size());

  // We need to get the quoting style for response files before parsing all
  // options so we parse here before and ignore all the options but
  // --rsp-quoting.
  opt::InputArgList Args = this->ParseArgs(Vec, MissingIndex, MissingCount);

  // Expand response files (arguments in the form of @<filename>)
  // and then parse the argument again.
  cl::ExpandResponseFiles(Saver, getQuotingStyle(Args), Vec);
  concatLTOPluginOptions(Vec);
  Args = this->ParseArgs(Vec, MissingIndex, MissingCount);

  handleColorDiagnostics(Args);
  if (MissingCount)
    error(Twine(Args.getArgString(MissingIndex)) + ": missing argument");

  for (auto *Arg : Args.filtered(OPT_UNKNOWN))
    error("unknown argument: " + Arg->getSpelling());
  return Args;
}

void elf::printHelp() {
  ELFOptTable().PrintHelp(
      outs(), (Config->ProgName + " [options] file...").str().c_str(), "lld",
      false /*ShowHidden*/, true /*ShowAllAliases*/);
  outs() << "\n";

  // Scripts generated by Libtool versions up to at least 2.4.6 (the most
  // recent version as of March 2017) expect /: supported targets:.* elf/
  // in a message for the -help option. If it doesn't match, the scripts
  // assume that the linker doesn't support very basic features such as
  // shared libraries. Therefore, we need to print out at least "elf".
  outs() << Config->ProgName << ": supported targets: elf\n";
}

// Reconstructs command line arguments so that so that you can re-run
// the same command with the same inputs. This is for --reproduce.
std::string elf::createResponseFile(const opt::InputArgList &Args) {
  SmallString<0> Data;
  raw_svector_ostream OS(Data);
  OS << "--chroot .\n";

  // Copy the command line to the output while rewriting paths.
  for (auto *Arg : Args) {
    switch (Arg->getOption().getUnaliasedOption().getID()) {
    case OPT_reproduce:
      break;
    case OPT_INPUT:
      OS << quote(rewritePath(Arg->getValue())) << "\n";
      break;
    case OPT_o:
      // If -o path contains directories, "lld @response.txt" will likely
      // fail because the archive we are creating doesn't contain empty
      // directories for the output path (-o doesn't create directories).
      // Strip directories to prevent the issue.
      OS << "-o " << quote(sys::path::filename(Arg->getValue())) << "\n";
      break;
    case OPT_dynamic_list:
    case OPT_library_path:
    case OPT_rpath:
    case OPT_script:
    case OPT_symbol_ordering_file:
    case OPT_sysroot:
    case OPT_version_script:
      OS << Arg->getSpelling() << " " << quote(rewritePath(Arg->getValue()))
         << "\n";
      break;
    default:
      OS << toString(*Arg) << "\n";
    }
  }
  return Data.str();
}

// Find a file by concatenating given paths. If a resulting path
// starts with "=", the character is replaced with a --sysroot value.
static Optional<std::string> findFile(StringRef Path1, const Twine &Path2) {
  SmallString<128> S;
  if (Path1.startswith("="))
    path::append(S, Config->Sysroot, Path1.substr(1), Path2);
  else
    path::append(S, Path1, Path2);

  if (fs::exists(S))
    return S.str().str();
  return None;
}

Optional<std::string> elf::findFromSearchPaths(StringRef Path) {
  for (StringRef Dir : Config->SearchPaths)
    if (Optional<std::string> S = findFile(Dir, Path))
      return S;
  return None;
}

// This is for -lfoo. We'll look for libfoo.so or libfoo.a from
// search paths.
Optional<std::string> elf::searchLibrary(StringRef Name) {
  if (Name.startswith(":"))
    return findFromSearchPaths(Name.substr(1));

  for (StringRef Dir : Config->SearchPaths) {
    if (!Config->Static)
      if (Optional<std::string> S = findFile(Dir, "lib" + Name + ".so"))
        return S;
    if (Optional<std::string> S = findFile(Dir, "lib" + Name + ".a"))
      return S;
  }
  return None;
}

// If a linker/version script doesn't exist in the current directory, we also
// look for the script in the '-L' search paths. This matches the behaviour of
// '-T', --version-script=, and linker script INPUT() command in ld.bfd.
Optional<std::string> elf::searchScript(StringRef Name) {
  if (fs::exists(Name))
    return Name.str();
  return findFromSearchPaths(Name);
}
