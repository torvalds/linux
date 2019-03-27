//===- CheckerRegistry.cpp - Maintains all available checkers -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace clang;
using namespace ento;
using llvm::sys::DynamicLibrary;

using RegisterCheckersFn = void (*)(CheckerRegistry &);

static bool isCompatibleAPIVersion(const char *versionString) {
  // If the version string is null, it's not an analyzer plugin.
  if (!versionString)
    return false;

  // For now, none of the static analyzer API is considered stable.
  // Versions must match exactly.
  return strcmp(versionString, CLANG_ANALYZER_API_VERSION_STRING) == 0;
}

CheckerRegistry::CheckerRegistry(ArrayRef<std::string> plugins,
                                 DiagnosticsEngine &diags) : Diags(diags) {
#define GET_CHECKERS
#define CHECKER(FULLNAME, CLASS, HELPTEXT, DOC_URI)                            \
  addChecker(register##CLASS, FULLNAME, HELPTEXT, DOC_URI);
#include "clang/StaticAnalyzer/Checkers/Checkers.inc"
#undef CHECKER
#undef GET_CHECKERS

  for (ArrayRef<std::string>::iterator i = plugins.begin(), e = plugins.end();
       i != e; ++i) {
    // Get access to the plugin.
    std::string err;
    DynamicLibrary lib = DynamicLibrary::getPermanentLibrary(i->c_str(), &err);
    if (!lib.isValid()) {
      diags.Report(diag::err_fe_unable_to_load_plugin) << *i << err;
      continue;
    }

    // See if it's compatible with this build of clang.
    const char *pluginAPIVersion =
      (const char *) lib.getAddressOfSymbol("clang_analyzerAPIVersionString");
    if (!isCompatibleAPIVersion(pluginAPIVersion)) {
      Diags.Report(diag::warn_incompatible_analyzer_plugin_api)
          << llvm::sys::path::filename(*i);
      Diags.Report(diag::note_incompatible_analyzer_plugin_api)
          << CLANG_ANALYZER_API_VERSION_STRING
          << pluginAPIVersion;
      continue;
    }

    // Register its checkers.
    RegisterCheckersFn registerPluginCheckers =
      (RegisterCheckersFn) (intptr_t) lib.getAddressOfSymbol(
                                                      "clang_registerCheckers");
    if (registerPluginCheckers)
      registerPluginCheckers(*this);
  }
}

static constexpr char PackageSeparator = '.';

static bool checkerNameLT(const CheckerRegistry::CheckerInfo &a,
                          const CheckerRegistry::CheckerInfo &b) {
  return a.FullName < b.FullName;
}

static bool isInPackage(const CheckerRegistry::CheckerInfo &checker,
                        StringRef packageName) {
  // Does the checker's full name have the package as a prefix?
  if (!checker.FullName.startswith(packageName))
    return false;

  // Is the package actually just the name of a specific checker?
  if (checker.FullName.size() == packageName.size())
    return true;

  // Is the checker in the package (or a subpackage)?
  if (checker.FullName[packageName.size()] == PackageSeparator)
    return true;

  return false;
}

CheckerRegistry::CheckerInfoSet CheckerRegistry::getEnabledCheckers(
                                            const AnalyzerOptions &Opts) const {

  assert(std::is_sorted(Checkers.begin(), Checkers.end(), checkerNameLT) &&
         "In order to efficiently gather checkers, this function expects them "
         "to be already sorted!");

  CheckerInfoSet enabledCheckers;
  const auto end = Checkers.cend();

  for (const std::pair<std::string, bool> &opt : Opts.CheckersControlList) {
    // Use a binary search to find the possible start of the package.
    CheckerRegistry::CheckerInfo packageInfo(nullptr, opt.first, "", "");
    auto firstRelatedChecker =
      std::lower_bound(Checkers.cbegin(), end, packageInfo, checkerNameLT);

    if (firstRelatedChecker == end ||
        !isInPackage(*firstRelatedChecker, opt.first)) {
      Diags.Report(diag::err_unknown_analyzer_checker) << opt.first;
      Diags.Report(diag::note_suggest_disabling_all_checkers);
      return {};
    }

    // See how large the package is.
    // If the package doesn't exist, assume the option refers to a single
    // checker.
    size_t size = 1;
    llvm::StringMap<size_t>::const_iterator packageSize =
      Packages.find(opt.first);
    if (packageSize != Packages.end())
      size = packageSize->getValue();

    // Step through all the checkers in the package.
    for (auto lastRelatedChecker = firstRelatedChecker+size;
         firstRelatedChecker != lastRelatedChecker; ++firstRelatedChecker)
      if (opt.second)
        enabledCheckers.insert(&*firstRelatedChecker);
      else
        enabledCheckers.remove(&*firstRelatedChecker);
  }

  return enabledCheckers;
}

void CheckerRegistry::addChecker(InitializationFunction Fn, StringRef Name,
                                 StringRef Desc, StringRef DocsUri) {
  Checkers.emplace_back(Fn, Name, Desc, DocsUri);

  // Record the presence of the checker in its packages.
  StringRef packageName, leafName;
  std::tie(packageName, leafName) = Name.rsplit(PackageSeparator);
  while (!leafName.empty()) {
    Packages[packageName] += 1;
    std::tie(packageName, leafName) = packageName.rsplit(PackageSeparator);
  }
}

void CheckerRegistry::initializeManager(CheckerManager &checkerMgr,
                                        const AnalyzerOptions &Opts) const {
  // Sort checkers for efficient collection.
  llvm::sort(Checkers, checkerNameLT);

  // Collect checkers enabled by the options.
  CheckerInfoSet enabledCheckers = getEnabledCheckers(Opts);

  // Initialize the CheckerManager with all enabled checkers.
  for (const auto *i : enabledCheckers) {
    checkerMgr.setCurrentCheckName(CheckName(i->FullName));
    i->Initialize(checkerMgr);
  }
}

void CheckerRegistry::validateCheckerOptions(
                                            const AnalyzerOptions &opts) const {
  for (const auto &config : opts.Config) {
    size_t pos = config.getKey().find(':');
    if (pos == StringRef::npos)
      continue;

    bool hasChecker = false;
    StringRef checkerName = config.getKey().substr(0, pos);
    for (const auto &checker : Checkers) {
      if (checker.FullName.startswith(checkerName) &&
          (checker.FullName.size() == pos || checker.FullName[pos] == '.')) {
        hasChecker = true;
        break;
      }
    }
    if (!hasChecker)
      Diags.Report(diag::err_unknown_analyzer_checker) << checkerName;
  }
}

void CheckerRegistry::printHelp(raw_ostream &out,
                                size_t maxNameChars) const {
  // FIXME: Alphabetical sort puts 'experimental' in the middle.
  // Would it be better to name it '~experimental' or something else
  // that's ASCIIbetically last?
  llvm::sort(Checkers, checkerNameLT);

  // FIXME: Print available packages.

  out << "CHECKERS:\n";

  // Find the maximum option length.
  size_t optionFieldWidth = 0;
  for (const auto &i : Checkers) {
    // Limit the amount of padding we are willing to give up for alignment.
    //   Package.Name     Description  [Hidden]
    size_t nameLength = i.FullName.size();
    if (nameLength <= maxNameChars)
      optionFieldWidth = std::max(optionFieldWidth, nameLength);
  }

  const size_t initialPad = 2;
  for (const auto &i : Checkers) {
    out.indent(initialPad) << i.FullName;

    int pad = optionFieldWidth - i.FullName.size();

    // Break on long option names.
    if (pad < 0) {
      out << '\n';
      pad = optionFieldWidth + initialPad;
    }
    out.indent(pad + 2) << i.Desc;

    out << '\n';
  }
}

void CheckerRegistry::printList(raw_ostream &out,
                                const AnalyzerOptions &opts) const {
  // Sort checkers for efficient collection.
  llvm::sort(Checkers, checkerNameLT);

  // Collect checkers enabled by the options.
  CheckerInfoSet enabledCheckers = getEnabledCheckers(opts);

  for (const auto *i : enabledCheckers)
    out << i->FullName << '\n';
}
