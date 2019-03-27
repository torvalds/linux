//===- CanonicalizeAliases.cpp - ThinLTO Support: Canonicalize Aliases ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Currently this file implements partial alias canonicalization, to
// flatten chains of aliases (also done by GlobalOpt, but not on for
// O0 compiles). E.g.
//  @a = alias i8, i8 *@b
//  @b = alias i8, i8 *@g
//
// will be converted to:
//  @a = alias i8, i8 *@g  <-- @a is now an alias to base object @g
//  @b = alias i8, i8 *@g
//
// Eventually this file will implement full alias canonicalation, so that
// all aliasees are private anonymous values. E.g.
//  @a = alias i8, i8 *@g
//  @g = global i8 0
//
// will be converted to:
//  @0 = private global
//  @a = alias i8, i8* @0
//  @g = alias i8, i8* @0
//
// This simplifies optimization and ThinLTO linking of the original symbols.
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/CanonicalizeAliases.h"

#include "llvm/IR/Operator.h"
#include "llvm/IR/ValueHandle.h"

using namespace llvm;

namespace {

static Constant *canonicalizeAlias(Constant *C, bool &Changed) {
  if (auto *GA = dyn_cast<GlobalAlias>(C)) {
    auto *NewAliasee = canonicalizeAlias(GA->getAliasee(), Changed);
    if (NewAliasee != GA->getAliasee()) {
      GA->setAliasee(NewAliasee);
      Changed = true;
    }
    return NewAliasee;
  }

  auto *CE = dyn_cast<ConstantExpr>(C);
  if (!CE)
    return C;

  std::vector<Constant *> Ops;
  for (Use &U : CE->operands())
    Ops.push_back(canonicalizeAlias(cast<Constant>(U), Changed));
  return CE->getWithOperands(Ops);
}

/// Convert aliases to canonical form.
static bool canonicalizeAliases(Module &M) {
  bool Changed = false;
  for (auto &GA : M.aliases())
    canonicalizeAlias(&GA, Changed);
  return Changed;
}

// Legacy pass that canonicalizes aliases.
class CanonicalizeAliasesLegacyPass : public ModulePass {

public:
  /// Pass identification, replacement for typeid
  static char ID;

  /// Specify pass name for debug output
  StringRef getPassName() const override { return "Canonicalize Aliases"; }

  explicit CanonicalizeAliasesLegacyPass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override { return canonicalizeAliases(M); }
};
char CanonicalizeAliasesLegacyPass::ID = 0;

} // anonymous namespace

PreservedAnalyses CanonicalizeAliasesPass::run(Module &M,
                                               ModuleAnalysisManager &AM) {
  if (!canonicalizeAliases(M))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}

INITIALIZE_PASS_BEGIN(CanonicalizeAliasesLegacyPass, "canonicalize-aliases",
                      "Canonicalize aliases", false, false)
INITIALIZE_PASS_END(CanonicalizeAliasesLegacyPass, "canonicalize-aliases",
                    "Canonicalize aliases", false, false)

namespace llvm {
ModulePass *createCanonicalizeAliasesPass() {
  return new CanonicalizeAliasesLegacyPass();
}
} // namespace llvm
