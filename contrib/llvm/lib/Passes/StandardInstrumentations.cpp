//===- Standard pass instrumentations handling ----------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines IR-printing pass instrumentation callbacks as well as
/// StandardInstrumentations class that manages standard pass instrumentations.
///
//===----------------------------------------------------------------------===//

#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

/// Extracting Module out of \p IR unit. Also fills a textual description
/// of \p IR for use in header when printing.
Optional<std::pair<const Module *, std::string>> unwrapModule(Any IR) {
  if (any_isa<const Module *>(IR))
    return std::make_pair(any_cast<const Module *>(IR), std::string());

  if (any_isa<const Function *>(IR)) {
    const Function *F = any_cast<const Function *>(IR);
    if (!llvm::isFunctionInPrintList(F->getName()))
      return None;
    const Module *M = F->getParent();
    return std::make_pair(M, formatv(" (function: {0})", F->getName()).str());
  }

  if (any_isa<const LazyCallGraph::SCC *>(IR)) {
    const LazyCallGraph::SCC *C = any_cast<const LazyCallGraph::SCC *>(IR);
    for (const LazyCallGraph::Node &N : *C) {
      const Function &F = N.getFunction();
      if (!F.isDeclaration() && isFunctionInPrintList(F.getName())) {
        const Module *M = F.getParent();
        return std::make_pair(M, formatv(" (scc: {0})", C->getName()).str());
      }
    }
    return None;
  }

  if (any_isa<const Loop *>(IR)) {
    const Loop *L = any_cast<const Loop *>(IR);
    const Function *F = L->getHeader()->getParent();
    if (!isFunctionInPrintList(F->getName()))
      return None;
    const Module *M = F->getParent();
    std::string LoopName;
    raw_string_ostream ss(LoopName);
    L->getHeader()->printAsOperand(ss, false);
    return std::make_pair(M, formatv(" (loop: {0})", ss.str()).str());
  }

  llvm_unreachable("Unknown IR unit");
}

void printIR(const Module *M, StringRef Banner, StringRef Extra = StringRef()) {
  dbgs() << Banner << Extra << "\n";
  M->print(dbgs(), nullptr, false);
}
void printIR(const Function *F, StringRef Banner,
             StringRef Extra = StringRef()) {
  if (!llvm::isFunctionInPrintList(F->getName()))
    return;
  dbgs() << Banner << Extra << "\n" << static_cast<const Value &>(*F);
}
void printIR(const LazyCallGraph::SCC *C, StringRef Banner,
             StringRef Extra = StringRef()) {
  bool BannerPrinted = false;
  for (const LazyCallGraph::Node &N : *C) {
    const Function &F = N.getFunction();
    if (!F.isDeclaration() && llvm::isFunctionInPrintList(F.getName())) {
      if (!BannerPrinted) {
        dbgs() << Banner << Extra << "\n";
        BannerPrinted = true;
      }
      F.print(dbgs());
    }
  }
}
void printIR(const Loop *L, StringRef Banner) {
  const Function *F = L->getHeader()->getParent();
  if (!llvm::isFunctionInPrintList(F->getName()))
    return;
  llvm::printLoop(const_cast<Loop &>(*L), dbgs(), Banner);
}

/// Generic IR-printing helper that unpacks a pointer to IRUnit wrapped into
/// llvm::Any and does actual print job.
void unwrapAndPrint(Any IR, StringRef Banner, bool ForceModule = false) {
  if (ForceModule) {
    if (auto UnwrappedModule = unwrapModule(IR))
      printIR(UnwrappedModule->first, Banner, UnwrappedModule->second);
    return;
  }

  if (any_isa<const Module *>(IR)) {
    const Module *M = any_cast<const Module *>(IR);
    assert(M && "module should be valid for printing");
    printIR(M, Banner);
    return;
  }

  if (any_isa<const Function *>(IR)) {
    const Function *F = any_cast<const Function *>(IR);
    assert(F && "function should be valid for printing");
    printIR(F, Banner);
    return;
  }

  if (any_isa<const LazyCallGraph::SCC *>(IR)) {
    const LazyCallGraph::SCC *C = any_cast<const LazyCallGraph::SCC *>(IR);
    assert(C && "scc should be valid for printing");
    std::string Extra = formatv(" (scc: {0})", C->getName());
    printIR(C, Banner, Extra);
    return;
  }

  if (any_isa<const Loop *>(IR)) {
    const Loop *L = any_cast<const Loop *>(IR);
    assert(L && "Loop should be valid for printing");
    printIR(L, Banner);
    return;
  }
  llvm_unreachable("Unknown wrapped IR type");
}

} // namespace

PrintIRInstrumentation::~PrintIRInstrumentation() {
  assert(ModuleDescStack.empty() && "ModuleDescStack is not empty at exit");
}

void PrintIRInstrumentation::pushModuleDesc(StringRef PassID, Any IR) {
  assert(StoreModuleDesc);
  const Module *M = nullptr;
  std::string Extra;
  if (auto UnwrappedModule = unwrapModule(IR))
    std::tie(M, Extra) = UnwrappedModule.getValue();
  ModuleDescStack.emplace_back(M, Extra, PassID);
}

PrintIRInstrumentation::PrintModuleDesc
PrintIRInstrumentation::popModuleDesc(StringRef PassID) {
  assert(!ModuleDescStack.empty() && "empty ModuleDescStack");
  PrintModuleDesc ModuleDesc = ModuleDescStack.pop_back_val();
  assert(std::get<2>(ModuleDesc).equals(PassID) && "malformed ModuleDescStack");
  return ModuleDesc;
}

bool PrintIRInstrumentation::printBeforePass(StringRef PassID, Any IR) {
  if (PassID.startswith("PassManager<") || PassID.contains("PassAdaptor<"))
    return true;

  // Saving Module for AfterPassInvalidated operations.
  // Note: here we rely on a fact that we do not change modules while
  // traversing the pipeline, so the latest captured module is good
  // for all print operations that has not happen yet.
  if (StoreModuleDesc && llvm::shouldPrintAfterPass(PassID))
    pushModuleDesc(PassID, IR);

  if (!llvm::shouldPrintBeforePass(PassID))
    return true;

  SmallString<20> Banner = formatv("*** IR Dump Before {0} ***", PassID);
  unwrapAndPrint(IR, Banner, llvm::forcePrintModuleIR());
  return true;
}

void PrintIRInstrumentation::printAfterPass(StringRef PassID, Any IR) {
  if (PassID.startswith("PassManager<") || PassID.contains("PassAdaptor<"))
    return;

  if (!llvm::shouldPrintAfterPass(PassID))
    return;

  if (StoreModuleDesc)
    popModuleDesc(PassID);

  SmallString<20> Banner = formatv("*** IR Dump After {0} ***", PassID);
  unwrapAndPrint(IR, Banner, llvm::forcePrintModuleIR());
}

void PrintIRInstrumentation::printAfterPassInvalidated(StringRef PassID) {
  if (!StoreModuleDesc || !llvm::shouldPrintAfterPass(PassID))
    return;

  if (PassID.startswith("PassManager<") || PassID.contains("PassAdaptor<"))
    return;

  const Module *M;
  std::string Extra;
  StringRef StoredPassID;
  std::tie(M, Extra, StoredPassID) = popModuleDesc(PassID);
  // Additional filtering (e.g. -filter-print-func) can lead to module
  // printing being skipped.
  if (!M)
    return;

  SmallString<20> Banner =
      formatv("*** IR Dump After {0} *** invalidated: ", PassID);
  printIR(M, Banner, Extra);
}

void PrintIRInstrumentation::registerCallbacks(
    PassInstrumentationCallbacks &PIC) {
  // BeforePass callback is not just for printing, it also saves a Module
  // for later use in AfterPassInvalidated.
  StoreModuleDesc = llvm::forcePrintModuleIR() && llvm::shouldPrintAfterPass();
  if (llvm::shouldPrintBeforePass() || StoreModuleDesc)
    PIC.registerBeforePassCallback(
        [this](StringRef P, Any IR) { return this->printBeforePass(P, IR); });

  if (llvm::shouldPrintAfterPass()) {
    PIC.registerAfterPassCallback(
        [this](StringRef P, Any IR) { this->printAfterPass(P, IR); });
    PIC.registerAfterPassInvalidatedCallback(
        [this](StringRef P) { this->printAfterPassInvalidated(P); });
  }
}

void StandardInstrumentations::registerCallbacks(
    PassInstrumentationCallbacks &PIC) {
  PrintIR.registerCallbacks(PIC);
  TimePasses.registerCallbacks(PIC);
}
