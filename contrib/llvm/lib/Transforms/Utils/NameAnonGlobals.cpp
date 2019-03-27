//===- NameAnonGlobals.cpp - ThinLTO Support: Name Unnamed Globals --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements naming anonymous globals to make sure they can be
// referred to by ThinLTO.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/NameAnonGlobals.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MD5.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

namespace {
// Compute a "unique" hash for the module based on the name of the public
// globals.
class ModuleHasher {
  Module &TheModule;
  std::string TheHash;

public:
  ModuleHasher(Module &M) : TheModule(M) {}

  /// Return the lazily computed hash.
  std::string &get() {
    if (!TheHash.empty())
      // Cache hit :)
      return TheHash;

    MD5 Hasher;
    for (auto &F : TheModule) {
      if (F.isDeclaration() || F.hasLocalLinkage() || !F.hasName())
        continue;
      auto Name = F.getName();
      Hasher.update(Name);
    }
    for (auto &GV : TheModule.globals()) {
      if (GV.isDeclaration() || GV.hasLocalLinkage() || !GV.hasName())
        continue;
      auto Name = GV.getName();
      Hasher.update(Name);
    }

    // Now return the result.
    MD5::MD5Result Hash;
    Hasher.final(Hash);
    SmallString<32> Result;
    MD5::stringifyResult(Hash, Result);
    TheHash = Result.str();
    return TheHash;
  }
};
} // end anonymous namespace

// Rename all the anon globals in the module
bool llvm::nameUnamedGlobals(Module &M) {
  bool Changed = false;
  ModuleHasher ModuleHash(M);
  int count = 0;
  auto RenameIfNeed = [&](GlobalValue &GV) {
    if (GV.hasName())
      return;
    GV.setName(Twine("anon.") + ModuleHash.get() + "." + Twine(count++));
    Changed = true;
  };
  for (auto &GO : M.global_objects())
    RenameIfNeed(GO);
  for (auto &GA : M.aliases())
    RenameIfNeed(GA);

  return Changed;
}

namespace {

// Legacy pass that provides a name to every anon globals.
class NameAnonGlobalLegacyPass : public ModulePass {

public:
  /// Pass identification, replacement for typeid
  static char ID;

  /// Specify pass name for debug output
  StringRef getPassName() const override { return "Name Anon Globals"; }

  explicit NameAnonGlobalLegacyPass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override { return nameUnamedGlobals(M); }
};
char NameAnonGlobalLegacyPass::ID = 0;

} // anonymous namespace

PreservedAnalyses NameAnonGlobalPass::run(Module &M,
                                          ModuleAnalysisManager &AM) {
  if (!nameUnamedGlobals(M))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}

INITIALIZE_PASS_BEGIN(NameAnonGlobalLegacyPass, "name-anon-globals",
                      "Provide a name to nameless globals", false, false)
INITIALIZE_PASS_END(NameAnonGlobalLegacyPass, "name-anon-globals",
                    "Provide a name to nameless globals", false, false)

namespace llvm {
ModulePass *createNameAnonGlobalPass() {
  return new NameAnonGlobalLegacyPass();
}
}
