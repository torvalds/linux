//===-- GCMetadata.cpp - Garbage collector metadata -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the GCFunctionInfo class and GCModuleInfo pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/CodeGen/GCStrategy.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Pass.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <string>

using namespace llvm;

namespace {

class Printer : public FunctionPass {
  static char ID;

  raw_ostream &OS;

public:
  explicit Printer(raw_ostream &OS) : FunctionPass(ID), OS(OS) {}

  StringRef getPassName() const override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnFunction(Function &F) override;
  bool doFinalization(Module &M) override;
};

} // end anonymous namespace

INITIALIZE_PASS(GCModuleInfo, "collector-metadata",
                "Create Garbage Collector Module Metadata", false, false)

// -----------------------------------------------------------------------------

GCFunctionInfo::GCFunctionInfo(const Function &F, GCStrategy &S)
    : F(F), S(S), FrameSize(~0LL) {}

GCFunctionInfo::~GCFunctionInfo() = default;

// -----------------------------------------------------------------------------

char GCModuleInfo::ID = 0;

GCModuleInfo::GCModuleInfo() : ImmutablePass(ID) {
  initializeGCModuleInfoPass(*PassRegistry::getPassRegistry());
}

GCFunctionInfo &GCModuleInfo::getFunctionInfo(const Function &F) {
  assert(!F.isDeclaration() && "Can only get GCFunctionInfo for a definition!");
  assert(F.hasGC());

  finfo_map_type::iterator I = FInfoMap.find(&F);
  if (I != FInfoMap.end())
    return *I->second;

  GCStrategy *S = getGCStrategy(F.getGC());
  Functions.push_back(llvm::make_unique<GCFunctionInfo>(F, *S));
  GCFunctionInfo *GFI = Functions.back().get();
  FInfoMap[&F] = GFI;
  return *GFI;
}

void GCModuleInfo::clear() {
  Functions.clear();
  FInfoMap.clear();
  GCStrategyList.clear();
}

// -----------------------------------------------------------------------------

char Printer::ID = 0;

FunctionPass *llvm::createGCInfoPrinter(raw_ostream &OS) {
  return new Printer(OS);
}

StringRef Printer::getPassName() const {
  return "Print Garbage Collector Information";
}

void Printer::getAnalysisUsage(AnalysisUsage &AU) const {
  FunctionPass::getAnalysisUsage(AU);
  AU.setPreservesAll();
  AU.addRequired<GCModuleInfo>();
}

bool Printer::runOnFunction(Function &F) {
  if (F.hasGC())
    return false;

  GCFunctionInfo *FD = &getAnalysis<GCModuleInfo>().getFunctionInfo(F);

  OS << "GC roots for " << FD->getFunction().getName() << ":\n";
  for (GCFunctionInfo::roots_iterator RI = FD->roots_begin(),
                                      RE = FD->roots_end();
       RI != RE; ++RI)
    OS << "\t" << RI->Num << "\t" << RI->StackOffset << "[sp]\n";

  OS << "GC safe points for " << FD->getFunction().getName() << ":\n";
  for (GCFunctionInfo::iterator PI = FD->begin(), PE = FD->end(); PI != PE;
       ++PI) {

    OS << "\t" << PI->Label->getName() << ": " << "post-call"
       << ", live = {";

    for (GCFunctionInfo::live_iterator RI = FD->live_begin(PI),
                                       RE = FD->live_end(PI);
         ;) {
      OS << " " << RI->Num;
      if (++RI == RE)
        break;
      OS << ",";
    }

    OS << " }\n";
  }

  return false;
}

bool Printer::doFinalization(Module &M) {
  GCModuleInfo *GMI = getAnalysisIfAvailable<GCModuleInfo>();
  assert(GMI && "Printer didn't require GCModuleInfo?!");
  GMI->clear();
  return false;
}

GCStrategy *GCModuleInfo::getGCStrategy(const StringRef Name) {
  // TODO: Arguably, just doing a linear search would be faster for small N
  auto NMI = GCStrategyMap.find(Name);
  if (NMI != GCStrategyMap.end())
    return NMI->getValue();

  for (auto& Entry : GCRegistry::entries()) {
    if (Name == Entry.getName()) {
      std::unique_ptr<GCStrategy> S = Entry.instantiate();
      S->Name = Name;
      GCStrategyMap[Name] = S.get();
      GCStrategyList.push_back(std::move(S));
      return GCStrategyList.back().get();
    }
  }

  if (GCRegistry::begin() == GCRegistry::end()) {
    // In normal operation, the registry should not be empty.  There should
    // be the builtin GCs if nothing else.  The most likely scenario here is
    // that we got here without running the initializers used by the Registry
    // itself and it's registration mechanism.
    const std::string error = ("unsupported GC: " + Name).str() +
      " (did you remember to link and initialize the CodeGen library?)";
    report_fatal_error(error);
  } else
    report_fatal_error(std::string("unsupported GC: ") + Name);
}
