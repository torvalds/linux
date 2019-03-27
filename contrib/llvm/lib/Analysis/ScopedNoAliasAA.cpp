//===- ScopedNoAliasAA.cpp - Scoped No-Alias Alias Analysis ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the ScopedNoAlias alias-analysis pass, which implements
// metadata-based scoped no-alias support.
//
// Alias-analysis scopes are defined by an id (which can be a string or some
// other metadata node), a domain node, and an optional descriptive string.
// A domain is defined by an id (which can be a string or some other metadata
// node), and an optional descriptive string.
//
// !dom0 =   metadata !{ metadata !"domain of foo()" }
// !scope1 = metadata !{ metadata !scope1, metadata !dom0, metadata !"scope 1" }
// !scope2 = metadata !{ metadata !scope2, metadata !dom0, metadata !"scope 2" }
//
// Loads and stores can be tagged with an alias-analysis scope, and also, with
// a noalias tag for a specific scope:
//
// ... = load %ptr1, !alias.scope !{ !scope1 }
// ... = load %ptr2, !alias.scope !{ !scope1, !scope2 }, !noalias !{ !scope1 }
//
// When evaluating an aliasing query, if one of the instructions is associated
// has a set of noalias scopes in some domain that is a superset of the alias
// scopes in that domain of some other instruction, then the two memory
// accesses are assumed not to alias.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

// A handy option for disabling scoped no-alias functionality. The same effect
// can also be achieved by stripping the associated metadata tags from IR, but
// this option is sometimes more convenient.
static cl::opt<bool> EnableScopedNoAlias("enable-scoped-noalias",
                                         cl::init(true), cl::Hidden);

namespace {

/// This is a simple wrapper around an MDNode which provides a higher-level
/// interface by hiding the details of how alias analysis information is encoded
/// in its operands.
class AliasScopeNode {
  const MDNode *Node = nullptr;

public:
  AliasScopeNode() = default;
  explicit AliasScopeNode(const MDNode *N) : Node(N) {}

  /// Get the MDNode for this AliasScopeNode.
  const MDNode *getNode() const { return Node; }

  /// Get the MDNode for this AliasScopeNode's domain.
  const MDNode *getDomain() const {
    if (Node->getNumOperands() < 2)
      return nullptr;
    return dyn_cast_or_null<MDNode>(Node->getOperand(1));
  }
};

} // end anonymous namespace

AliasResult ScopedNoAliasAAResult::alias(const MemoryLocation &LocA,
                                         const MemoryLocation &LocB) {
  if (!EnableScopedNoAlias)
    return AAResultBase::alias(LocA, LocB);

  // Get the attached MDNodes.
  const MDNode *AScopes = LocA.AATags.Scope, *BScopes = LocB.AATags.Scope;

  const MDNode *ANoAlias = LocA.AATags.NoAlias, *BNoAlias = LocB.AATags.NoAlias;

  if (!mayAliasInScopes(AScopes, BNoAlias))
    return NoAlias;

  if (!mayAliasInScopes(BScopes, ANoAlias))
    return NoAlias;

  // If they may alias, chain to the next AliasAnalysis.
  return AAResultBase::alias(LocA, LocB);
}

ModRefInfo ScopedNoAliasAAResult::getModRefInfo(const CallBase *Call,
                                                const MemoryLocation &Loc) {
  if (!EnableScopedNoAlias)
    return AAResultBase::getModRefInfo(Call, Loc);

  if (!mayAliasInScopes(Loc.AATags.Scope,
                        Call->getMetadata(LLVMContext::MD_noalias)))
    return ModRefInfo::NoModRef;

  if (!mayAliasInScopes(Call->getMetadata(LLVMContext::MD_alias_scope),
                        Loc.AATags.NoAlias))
    return ModRefInfo::NoModRef;

  return AAResultBase::getModRefInfo(Call, Loc);
}

ModRefInfo ScopedNoAliasAAResult::getModRefInfo(const CallBase *Call1,
                                                const CallBase *Call2) {
  if (!EnableScopedNoAlias)
    return AAResultBase::getModRefInfo(Call1, Call2);

  if (!mayAliasInScopes(Call1->getMetadata(LLVMContext::MD_alias_scope),
                        Call2->getMetadata(LLVMContext::MD_noalias)))
    return ModRefInfo::NoModRef;

  if (!mayAliasInScopes(Call2->getMetadata(LLVMContext::MD_alias_scope),
                        Call1->getMetadata(LLVMContext::MD_noalias)))
    return ModRefInfo::NoModRef;

  return AAResultBase::getModRefInfo(Call1, Call2);
}

static void collectMDInDomain(const MDNode *List, const MDNode *Domain,
                              SmallPtrSetImpl<const MDNode *> &Nodes) {
  for (const MDOperand &MDOp : List->operands())
    if (const MDNode *MD = dyn_cast<MDNode>(MDOp))
      if (AliasScopeNode(MD).getDomain() == Domain)
        Nodes.insert(MD);
}

bool ScopedNoAliasAAResult::mayAliasInScopes(const MDNode *Scopes,
                                             const MDNode *NoAlias) const {
  if (!Scopes || !NoAlias)
    return true;

  // Collect the set of scope domains relevant to the noalias scopes.
  SmallPtrSet<const MDNode *, 16> Domains;
  for (const MDOperand &MDOp : NoAlias->operands())
    if (const MDNode *NAMD = dyn_cast<MDNode>(MDOp))
      if (const MDNode *Domain = AliasScopeNode(NAMD).getDomain())
        Domains.insert(Domain);

  // We alias unless, for some domain, the set of noalias scopes in that domain
  // is a superset of the set of alias scopes in that domain.
  for (const MDNode *Domain : Domains) {
    SmallPtrSet<const MDNode *, 16> ScopeNodes;
    collectMDInDomain(Scopes, Domain, ScopeNodes);
    if (ScopeNodes.empty())
      continue;

    SmallPtrSet<const MDNode *, 16> NANodes;
    collectMDInDomain(NoAlias, Domain, NANodes);

    // To not alias, all of the nodes in ScopeNodes must be in NANodes.
    bool FoundAll = true;
    for (const MDNode *SMD : ScopeNodes)
      if (!NANodes.count(SMD)) {
        FoundAll = false;
        break;
      }

    if (FoundAll)
      return false;
  }

  return true;
}

AnalysisKey ScopedNoAliasAA::Key;

ScopedNoAliasAAResult ScopedNoAliasAA::run(Function &F,
                                           FunctionAnalysisManager &AM) {
  return ScopedNoAliasAAResult();
}

char ScopedNoAliasAAWrapperPass::ID = 0;

INITIALIZE_PASS(ScopedNoAliasAAWrapperPass, "scoped-noalias",
                "Scoped NoAlias Alias Analysis", false, true)

ImmutablePass *llvm::createScopedNoAliasAAWrapperPass() {
  return new ScopedNoAliasAAWrapperPass();
}

ScopedNoAliasAAWrapperPass::ScopedNoAliasAAWrapperPass() : ImmutablePass(ID) {
  initializeScopedNoAliasAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

bool ScopedNoAliasAAWrapperPass::doInitialization(Module &M) {
  Result.reset(new ScopedNoAliasAAResult());
  return false;
}

bool ScopedNoAliasAAWrapperPass::doFinalization(Module &M) {
  Result.reset();
  return false;
}

void ScopedNoAliasAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}
