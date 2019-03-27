//===- Pass.cpp - LLVM Pass Infrastructure Implementation -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the LLVM Pass infrastructure.  It is primarily
// responsible with ensuring that passes are executed and batched together
// optimally.
//
//===----------------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassNameParser.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/PassInfo.h"
#include "llvm/PassRegistry.h"
#include "llvm/PassSupport.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "ir"

//===----------------------------------------------------------------------===//
// Pass Implementation
//

// Force out-of-line virtual method.
Pass::~Pass() {
  delete Resolver;
}

// Force out-of-line virtual method.
ModulePass::~ModulePass() = default;

Pass *ModulePass::createPrinterPass(raw_ostream &OS,
                                    const std::string &Banner) const {
  return createPrintModulePass(OS, Banner);
}

PassManagerType ModulePass::getPotentialPassManagerType() const {
  return PMT_ModulePassManager;
}

bool ModulePass::skipModule(Module &M) const {
  return !M.getContext().getOptPassGate().shouldRunPass(this, M);
}

bool Pass::mustPreserveAnalysisID(char &AID) const {
  return Resolver->getAnalysisIfAvailable(&AID, true) != nullptr;
}

// dumpPassStructure - Implement the -debug-pass=Structure option
void Pass::dumpPassStructure(unsigned Offset) {
  dbgs().indent(Offset*2) << getPassName() << "\n";
}

/// getPassName - Return a nice clean name for a pass.  This usually
/// implemented in terms of the name that is registered by one of the
/// Registration templates, but can be overloaded directly.
StringRef Pass::getPassName() const {
  AnalysisID AID =  getPassID();
  const PassInfo *PI = PassRegistry::getPassRegistry()->getPassInfo(AID);
  if (PI)
    return PI->getPassName();
  return "Unnamed pass: implement Pass::getPassName()";
}

void Pass::preparePassManager(PMStack &) {
  // By default, don't do anything.
}

PassManagerType Pass::getPotentialPassManagerType() const {
  // Default implementation.
  return PMT_Unknown;
}

void Pass::getAnalysisUsage(AnalysisUsage &) const {
  // By default, no analysis results are used, all are invalidated.
}

void Pass::releaseMemory() {
  // By default, don't do anything.
}

void Pass::verifyAnalysis() const {
  // By default, don't do anything.
}

void *Pass::getAdjustedAnalysisPointer(AnalysisID AID) {
  return this;
}

ImmutablePass *Pass::getAsImmutablePass() {
  return nullptr;
}

PMDataManager *Pass::getAsPMDataManager() {
  return nullptr;
}

void Pass::setResolver(AnalysisResolver *AR) {
  assert(!Resolver && "Resolver is already set");
  Resolver = AR;
}

// print - Print out the internal state of the pass.  This is called by Analyze
// to print out the contents of an analysis.  Otherwise it is not necessary to
// implement this method.
void Pass::print(raw_ostream &OS, const Module *) const {
  OS << "Pass::print not implemented for pass: '" << getPassName() << "'!\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
// dump - call print(cerr);
LLVM_DUMP_METHOD void Pass::dump() const {
  print(dbgs(), nullptr);
}
#endif

//===----------------------------------------------------------------------===//
// ImmutablePass Implementation
//
// Force out-of-line virtual method.
ImmutablePass::~ImmutablePass() = default;

void ImmutablePass::initializePass() {
  // By default, don't do anything.
}

//===----------------------------------------------------------------------===//
// FunctionPass Implementation
//

Pass *FunctionPass::createPrinterPass(raw_ostream &OS,
                                      const std::string &Banner) const {
  return createPrintFunctionPass(OS, Banner);
}

PassManagerType FunctionPass::getPotentialPassManagerType() const {
  return PMT_FunctionPassManager;
}

bool FunctionPass::skipFunction(const Function &F) const {
  if (!F.getContext().getOptPassGate().shouldRunPass(this, F))
    return true;

  if (F.hasFnAttribute(Attribute::OptimizeNone)) {
    LLVM_DEBUG(dbgs() << "Skipping pass '" << getPassName() << "' on function "
                      << F.getName() << "\n");
    return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// BasicBlockPass Implementation
//

Pass *BasicBlockPass::createPrinterPass(raw_ostream &OS,
                                        const std::string &Banner) const {
  return createPrintBasicBlockPass(OS, Banner);
}

bool BasicBlockPass::doInitialization(Function &) {
  // By default, don't do anything.
  return false;
}

bool BasicBlockPass::doFinalization(Function &) {
  // By default, don't do anything.
  return false;
}

bool BasicBlockPass::skipBasicBlock(const BasicBlock &BB) const {
  const Function *F = BB.getParent();
  if (!F)
    return false;
  if (!F->getContext().getOptPassGate().shouldRunPass(this, BB))
    return true;
  if (F->hasFnAttribute(Attribute::OptimizeNone)) {
    // Report this only once per function.
    if (&BB == &F->getEntryBlock())
      LLVM_DEBUG(dbgs() << "Skipping pass '" << getPassName()
                        << "' on function " << F->getName() << "\n");
    return true;
  }
  return false;
}

PassManagerType BasicBlockPass::getPotentialPassManagerType() const {
  return PMT_BasicBlockPassManager;
}

const PassInfo *Pass::lookupPassInfo(const void *TI) {
  return PassRegistry::getPassRegistry()->getPassInfo(TI);
}

const PassInfo *Pass::lookupPassInfo(StringRef Arg) {
  return PassRegistry::getPassRegistry()->getPassInfo(Arg);
}

Pass *Pass::createPass(AnalysisID ID) {
  const PassInfo *PI = PassRegistry::getPassRegistry()->getPassInfo(ID);
  if (!PI)
    return nullptr;
  return PI->createPass();
}

//===----------------------------------------------------------------------===//
//                  Analysis Group Implementation Code
//===----------------------------------------------------------------------===//

// RegisterAGBase implementation

RegisterAGBase::RegisterAGBase(StringRef Name, const void *InterfaceID,
                               const void *PassID, bool isDefault)
    : PassInfo(Name, InterfaceID) {
  PassRegistry::getPassRegistry()->registerAnalysisGroup(InterfaceID, PassID,
                                                         *this, isDefault);
}

//===----------------------------------------------------------------------===//
// PassRegistrationListener implementation
//

// enumeratePasses - Iterate over the registered passes, calling the
// passEnumerate callback on each PassInfo object.
void PassRegistrationListener::enumeratePasses() {
  PassRegistry::getPassRegistry()->enumerateWith(this);
}

PassNameParser::PassNameParser(cl::Option &O)
    : cl::parser<const PassInfo *>(O) {
  PassRegistry::getPassRegistry()->addRegistrationListener(this);
}

// This only gets called during static destruction, in which case the
// PassRegistry will have already been destroyed by llvm_shutdown().  So
// attempting to remove the registration listener is an error.
PassNameParser::~PassNameParser() = default;

//===----------------------------------------------------------------------===//
//   AnalysisUsage Class Implementation
//

namespace {

struct GetCFGOnlyPasses : public PassRegistrationListener {
  using VectorType = AnalysisUsage::VectorType;

  VectorType &CFGOnlyList;

  GetCFGOnlyPasses(VectorType &L) : CFGOnlyList(L) {}

  void passEnumerate(const PassInfo *P) override {
    if (P->isCFGOnlyPass())
      CFGOnlyList.push_back(P->getTypeInfo());
  }
};

} // end anonymous namespace

// setPreservesCFG - This function should be called to by the pass, iff they do
// not:
//
//  1. Add or remove basic blocks from the function
//  2. Modify terminator instructions in any way.
//
// This function annotates the AnalysisUsage info object to say that analyses
// that only depend on the CFG are preserved by this pass.
void AnalysisUsage::setPreservesCFG() {
  // Since this transformation doesn't modify the CFG, it preserves all analyses
  // that only depend on the CFG (like dominators, loop info, etc...)
  GetCFGOnlyPasses(Preserved).enumeratePasses();
}

AnalysisUsage &AnalysisUsage::addPreserved(StringRef Arg) {
  const PassInfo *PI = Pass::lookupPassInfo(Arg);
  // If the pass exists, preserve it. Otherwise silently do nothing.
  if (PI) Preserved.push_back(PI->getTypeInfo());
  return *this;
}

AnalysisUsage &AnalysisUsage::addRequiredID(const void *ID) {
  Required.push_back(ID);
  return *this;
}

AnalysisUsage &AnalysisUsage::addRequiredID(char &ID) {
  Required.push_back(&ID);
  return *this;
}

AnalysisUsage &AnalysisUsage::addRequiredTransitiveID(char &ID) {
  Required.push_back(&ID);
  RequiredTransitive.push_back(&ID);
  return *this;
}
