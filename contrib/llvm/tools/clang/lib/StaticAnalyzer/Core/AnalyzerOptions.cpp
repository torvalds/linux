//===- AnalyzerOptions.cpp - Analysis Engine Options ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains special accessors for analyzer configuration options
// with string representations.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

using namespace clang;
using namespace ento;
using namespace llvm;

std::vector<StringRef>
AnalyzerOptions::getRegisteredCheckers(bool IncludeExperimental /* = false */) {
  static const StringRef StaticAnalyzerChecks[] = {
#define GET_CHECKERS
#define CHECKER(FULLNAME, CLASS, HELPTEXT, DOC_URI)                            \
  FULLNAME,
#include "clang/StaticAnalyzer/Checkers/Checkers.inc"
#undef CHECKER
#undef GET_CHECKERS
  };
  std::vector<StringRef> Result;
  for (StringRef CheckName : StaticAnalyzerChecks) {
    if (!CheckName.startswith("debug.") &&
        (IncludeExperimental || !CheckName.startswith("alpha.")))
      Result.push_back(CheckName);
  }
  return Result;
}

ExplorationStrategyKind
AnalyzerOptions::getExplorationStrategy() const {
  auto K =
    llvm::StringSwitch<llvm::Optional<ExplorationStrategyKind>>(
                                                            ExplorationStrategy)
          .Case("dfs", ExplorationStrategyKind::DFS)
          .Case("bfs", ExplorationStrategyKind::BFS)
          .Case("unexplored_first",
                ExplorationStrategyKind::UnexploredFirst)
          .Case("unexplored_first_queue",
                ExplorationStrategyKind::UnexploredFirstQueue)
          .Case("unexplored_first_location_queue",
                ExplorationStrategyKind::UnexploredFirstLocationQueue)
          .Case("bfs_block_dfs_contents",
                ExplorationStrategyKind::BFSBlockDFSContents)
          .Default(None);
  assert(K.hasValue() && "User mode is invalid.");
  return K.getValue();
}

IPAKind AnalyzerOptions::getIPAMode() const {
  auto K = llvm::StringSwitch<llvm::Optional<IPAKind>>(IPAMode)
          .Case("none", IPAK_None)
          .Case("basic-inlining", IPAK_BasicInlining)
          .Case("inlining", IPAK_Inlining)
          .Case("dynamic", IPAK_DynamicDispatch)
          .Case("dynamic-bifurcate", IPAK_DynamicDispatchBifurcate)
          .Default(None);
  assert(K.hasValue() && "IPA Mode is invalid.");

  return K.getValue();
}

bool
AnalyzerOptions::mayInlineCXXMemberFunction(
                                          CXXInlineableMemberKind Param) const {
  if (getIPAMode() < IPAK_Inlining)
    return false;

  auto K =
    llvm::StringSwitch<llvm::Optional<CXXInlineableMemberKind>>(
                                                          CXXMemberInliningMode)
    .Case("constructors", CIMK_Constructors)
    .Case("destructors", CIMK_Destructors)
    .Case("methods", CIMK_MemberFunctions)
    .Case("none", CIMK_None)
    .Default(None);

  assert(K.hasValue() && "Invalid c++ member function inlining mode.");

  return *K >= Param;
}

StringRef AnalyzerOptions::getCheckerStringOption(StringRef OptionName,
                                                  StringRef DefaultVal,
                                                  const CheckerBase *C,
                                                  bool SearchInParents) const {
  assert(C);
  // Search for a package option if the option for the checker is not specified
  // and search in parents is enabled.
  StringRef CheckerName = C->getTagDescription();

  assert(!CheckerName.empty() &&
         "Empty checker name! Make sure the checker object (including it's "
         "bases!) if fully initialized before calling this function!");
  ConfigTable::const_iterator E = Config.end();
  do {
    ConfigTable::const_iterator I =
        Config.find((Twine(CheckerName) + ":" + OptionName).str());
    if (I != E)
      return StringRef(I->getValue());
    size_t Pos = CheckerName.rfind('.');
    if (Pos == StringRef::npos)
      return DefaultVal;
    CheckerName = CheckerName.substr(0, Pos);
  } while (!CheckerName.empty() && SearchInParents);
  return DefaultVal;
}

bool AnalyzerOptions::getCheckerBooleanOption(StringRef Name, bool DefaultVal,
                                              const CheckerBase *C,
                                              bool SearchInParents) const {
  // FIXME: We should emit a warning here if the value is something other than
  // "true", "false", or the empty string (meaning the default value),
  // but the AnalyzerOptions doesn't have access to a diagnostic engine.
  assert(C);
  return llvm::StringSwitch<bool>(
      getCheckerStringOption(Name, DefaultVal ? "true" : "false", C,
                             SearchInParents))
      .Case("true", true)
      .Case("false", false)
      .Default(DefaultVal);
}

int AnalyzerOptions::getCheckerIntegerOption(StringRef Name, int DefaultVal,
                                        const CheckerBase *C,
                                        bool SearchInParents) const {
  int Ret = DefaultVal;
  bool HasFailed = getCheckerStringOption(Name, std::to_string(DefaultVal), C,
                                          SearchInParents)
                     .getAsInteger(10, Ret);
  assert(!HasFailed && "analyzer-config option should be numeric");
  (void)HasFailed;
  return Ret;
}
