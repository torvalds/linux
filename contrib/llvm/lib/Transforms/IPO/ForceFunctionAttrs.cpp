//===- ForceFunctionAttrs.cpp - Force function attrs for debugging --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/ForceFunctionAttrs.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "forceattrs"

static cl::list<std::string>
    ForceAttributes("force-attribute", cl::Hidden,
                    cl::desc("Add an attribute to a function. This should be a "
                             "pair of 'function-name:attribute-name', for "
                             "example -force-attribute=foo:noinline. This "
                             "option can be specified multiple times."));

static Attribute::AttrKind parseAttrKind(StringRef Kind) {
  return StringSwitch<Attribute::AttrKind>(Kind)
      .Case("alwaysinline", Attribute::AlwaysInline)
      .Case("builtin", Attribute::Builtin)
      .Case("cold", Attribute::Cold)
      .Case("convergent", Attribute::Convergent)
      .Case("inlinehint", Attribute::InlineHint)
      .Case("jumptable", Attribute::JumpTable)
      .Case("minsize", Attribute::MinSize)
      .Case("naked", Attribute::Naked)
      .Case("nobuiltin", Attribute::NoBuiltin)
      .Case("noduplicate", Attribute::NoDuplicate)
      .Case("noimplicitfloat", Attribute::NoImplicitFloat)
      .Case("noinline", Attribute::NoInline)
      .Case("nonlazybind", Attribute::NonLazyBind)
      .Case("noredzone", Attribute::NoRedZone)
      .Case("noreturn", Attribute::NoReturn)
      .Case("nocf_check", Attribute::NoCfCheck)
      .Case("norecurse", Attribute::NoRecurse)
      .Case("nounwind", Attribute::NoUnwind)
      .Case("optforfuzzing", Attribute::OptForFuzzing)
      .Case("optnone", Attribute::OptimizeNone)
      .Case("optsize", Attribute::OptimizeForSize)
      .Case("readnone", Attribute::ReadNone)
      .Case("readonly", Attribute::ReadOnly)
      .Case("argmemonly", Attribute::ArgMemOnly)
      .Case("returns_twice", Attribute::ReturnsTwice)
      .Case("safestack", Attribute::SafeStack)
      .Case("shadowcallstack", Attribute::ShadowCallStack)
      .Case("sanitize_address", Attribute::SanitizeAddress)
      .Case("sanitize_hwaddress", Attribute::SanitizeHWAddress)
      .Case("sanitize_memory", Attribute::SanitizeMemory)
      .Case("sanitize_thread", Attribute::SanitizeThread)
      .Case("speculative_load_hardening", Attribute::SpeculativeLoadHardening)
      .Case("ssp", Attribute::StackProtect)
      .Case("sspreq", Attribute::StackProtectReq)
      .Case("sspstrong", Attribute::StackProtectStrong)
      .Case("strictfp", Attribute::StrictFP)
      .Case("uwtable", Attribute::UWTable)
      .Default(Attribute::None);
}

/// If F has any forced attributes given on the command line, add them.
static void addForcedAttributes(Function &F) {
  for (auto &S : ForceAttributes) {
    auto KV = StringRef(S).split(':');
    if (KV.first != F.getName())
      continue;

    auto Kind = parseAttrKind(KV.second);
    if (Kind == Attribute::None) {
      LLVM_DEBUG(dbgs() << "ForcedAttribute: " << KV.second
                        << " unknown or not handled!\n");
      continue;
    }
    if (F.hasFnAttribute(Kind))
      continue;
    F.addFnAttr(Kind);
  }
}

PreservedAnalyses ForceFunctionAttrsPass::run(Module &M,
                                              ModuleAnalysisManager &) {
  if (ForceAttributes.empty())
    return PreservedAnalyses::all();

  for (Function &F : M.functions())
    addForcedAttributes(F);

  // Just conservatively invalidate analyses, this isn't likely to be important.
  return PreservedAnalyses::none();
}

namespace {
struct ForceFunctionAttrsLegacyPass : public ModulePass {
  static char ID; // Pass identification, replacement for typeid
  ForceFunctionAttrsLegacyPass() : ModulePass(ID) {
    initializeForceFunctionAttrsLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override {
    if (ForceAttributes.empty())
      return false;

    for (Function &F : M.functions())
      addForcedAttributes(F);

    // Conservatively assume we changed something.
    return true;
  }
};
}

char ForceFunctionAttrsLegacyPass::ID = 0;
INITIALIZE_PASS(ForceFunctionAttrsLegacyPass, "forceattrs",
                "Force set function attributes", false, false)

Pass *llvm::createForceFunctionAttrsLegacyPass() {
  return new ForceFunctionAttrsLegacyPass();
}
