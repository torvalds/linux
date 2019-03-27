//===- LLVMContextImpl.cpp - Implement LLVMContextImpl --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the opaque LLVMContextImpl.
//
//===----------------------------------------------------------------------===//

#include "LLVMContextImpl.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/ManagedStatic.h"
#include <cassert>
#include <utility>

using namespace llvm;

LLVMContextImpl::LLVMContextImpl(LLVMContext &C)
  : DiagHandler(llvm::make_unique<DiagnosticHandler>()),
    VoidTy(C, Type::VoidTyID),
    LabelTy(C, Type::LabelTyID),
    HalfTy(C, Type::HalfTyID),
    FloatTy(C, Type::FloatTyID),
    DoubleTy(C, Type::DoubleTyID),
    MetadataTy(C, Type::MetadataTyID),
    TokenTy(C, Type::TokenTyID),
    X86_FP80Ty(C, Type::X86_FP80TyID),
    FP128Ty(C, Type::FP128TyID),
    PPC_FP128Ty(C, Type::PPC_FP128TyID),
    X86_MMXTy(C, Type::X86_MMXTyID),
    Int1Ty(C, 1),
    Int8Ty(C, 8),
    Int16Ty(C, 16),
    Int32Ty(C, 32),
    Int64Ty(C, 64),
    Int128Ty(C, 128) {}

LLVMContextImpl::~LLVMContextImpl() {
  // NOTE: We need to delete the contents of OwnedModules, but Module's dtor
  // will call LLVMContextImpl::removeModule, thus invalidating iterators into
  // the container. Avoid iterators during this operation:
  while (!OwnedModules.empty())
    delete *OwnedModules.begin();

#ifndef NDEBUG
  // Check for metadata references from leaked Instructions.
  for (auto &Pair : InstructionMetadata)
    Pair.first->dump();
  assert(InstructionMetadata.empty() &&
         "Instructions with metadata have been leaked");
#endif

  // Drop references for MDNodes.  Do this before Values get deleted to avoid
  // unnecessary RAUW when nodes are still unresolved.
  for (auto *I : DistinctMDNodes)
    I->dropAllReferences();
#define HANDLE_MDNODE_LEAF_UNIQUABLE(CLASS)                                    \
  for (auto *I : CLASS##s)                                                     \
    I->dropAllReferences();
#include "llvm/IR/Metadata.def"

  // Also drop references that come from the Value bridges.
  for (auto &Pair : ValuesAsMetadata)
    Pair.second->dropUsers();
  for (auto &Pair : MetadataAsValues)
    Pair.second->dropUse();

  // Destroy MDNodes.
  for (MDNode *I : DistinctMDNodes)
    I->deleteAsSubclass();
#define HANDLE_MDNODE_LEAF_UNIQUABLE(CLASS)                                    \
  for (CLASS * I : CLASS##s)                                                   \
    delete I;
#include "llvm/IR/Metadata.def"

  // Free the constants.
  for (auto *I : ExprConstants)
    I->dropAllReferences();
  for (auto *I : ArrayConstants)
    I->dropAllReferences();
  for (auto *I : StructConstants)
    I->dropAllReferences();
  for (auto *I : VectorConstants)
    I->dropAllReferences();
  ExprConstants.freeConstants();
  ArrayConstants.freeConstants();
  StructConstants.freeConstants();
  VectorConstants.freeConstants();
  InlineAsms.freeConstants();

  CAZConstants.clear();
  CPNConstants.clear();
  UVConstants.clear();
  IntConstants.clear();
  FPConstants.clear();

  for (auto &CDSConstant : CDSConstants)
    delete CDSConstant.second;
  CDSConstants.clear();

  // Destroy attributes.
  for (FoldingSetIterator<AttributeImpl> I = AttrsSet.begin(),
         E = AttrsSet.end(); I != E; ) {
    FoldingSetIterator<AttributeImpl> Elem = I++;
    delete &*Elem;
  }

  // Destroy attribute lists.
  for (FoldingSetIterator<AttributeListImpl> I = AttrsLists.begin(),
                                             E = AttrsLists.end();
       I != E;) {
    FoldingSetIterator<AttributeListImpl> Elem = I++;
    delete &*Elem;
  }

  // Destroy attribute node lists.
  for (FoldingSetIterator<AttributeSetNode> I = AttrsSetNodes.begin(),
         E = AttrsSetNodes.end(); I != E; ) {
    FoldingSetIterator<AttributeSetNode> Elem = I++;
    delete &*Elem;
  }

  // Destroy MetadataAsValues.
  {
    SmallVector<MetadataAsValue *, 8> MDVs;
    MDVs.reserve(MetadataAsValues.size());
    for (auto &Pair : MetadataAsValues)
      MDVs.push_back(Pair.second);
    MetadataAsValues.clear();
    for (auto *V : MDVs)
      delete V;
  }

  // Destroy ValuesAsMetadata.
  for (auto &Pair : ValuesAsMetadata)
    delete Pair.second;
}

void LLVMContextImpl::dropTriviallyDeadConstantArrays() {
  bool Changed;
  do {
    Changed = false;

    for (auto I = ArrayConstants.begin(), E = ArrayConstants.end(); I != E;) {
      auto *C = *I++;
      if (C->use_empty()) {
        Changed = true;
        C->destroyConstant();
      }
    }
  } while (Changed);
}

void Module::dropTriviallyDeadConstantArrays() {
  Context.pImpl->dropTriviallyDeadConstantArrays();
}

namespace llvm {

/// Make MDOperand transparent for hashing.
///
/// This overload of an implementation detail of the hashing library makes
/// MDOperand hash to the same value as a \a Metadata pointer.
///
/// Note that overloading \a hash_value() as follows:
///
/// \code
///     size_t hash_value(const MDOperand &X) { return hash_value(X.get()); }
/// \endcode
///
/// does not cause MDOperand to be transparent.  In particular, a bare pointer
/// doesn't get hashed before it's combined, whereas \a MDOperand would.
static const Metadata *get_hashable_data(const MDOperand &X) { return X.get(); }

} // end namespace llvm

unsigned MDNodeOpsKey::calculateHash(MDNode *N, unsigned Offset) {
  unsigned Hash = hash_combine_range(N->op_begin() + Offset, N->op_end());
#ifndef NDEBUG
  {
    SmallVector<Metadata *, 8> MDs(N->op_begin() + Offset, N->op_end());
    unsigned RawHash = calculateHash(MDs);
    assert(Hash == RawHash &&
           "Expected hash of MDOperand to equal hash of Metadata*");
  }
#endif
  return Hash;
}

unsigned MDNodeOpsKey::calculateHash(ArrayRef<Metadata *> Ops) {
  return hash_combine_range(Ops.begin(), Ops.end());
}

StringMapEntry<uint32_t> *LLVMContextImpl::getOrInsertBundleTag(StringRef Tag) {
  uint32_t NewIdx = BundleTagCache.size();
  return &*(BundleTagCache.insert(std::make_pair(Tag, NewIdx)).first);
}

void LLVMContextImpl::getOperandBundleTags(SmallVectorImpl<StringRef> &Tags) const {
  Tags.resize(BundleTagCache.size());
  for (const auto &T : BundleTagCache)
    Tags[T.second] = T.first();
}

uint32_t LLVMContextImpl::getOperandBundleTagID(StringRef Tag) const {
  auto I = BundleTagCache.find(Tag);
  assert(I != BundleTagCache.end() && "Unknown tag!");
  return I->second;
}

SyncScope::ID LLVMContextImpl::getOrInsertSyncScopeID(StringRef SSN) {
  auto NewSSID = SSC.size();
  assert(NewSSID < std::numeric_limits<SyncScope::ID>::max() &&
         "Hit the maximum number of synchronization scopes allowed!");
  return SSC.insert(std::make_pair(SSN, SyncScope::ID(NewSSID))).first->second;
}

void LLVMContextImpl::getSyncScopeNames(
    SmallVectorImpl<StringRef> &SSNs) const {
  SSNs.resize(SSC.size());
  for (const auto &SSE : SSC)
    SSNs[SSE.second] = SSE.first();
}

/// Singleton instance of the OptBisect class.
///
/// This singleton is accessed via the LLVMContext::getOptPassGate() function.
/// It provides a mechanism to disable passes and individual optimizations at
/// compile time based on a command line option (-opt-bisect-limit) in order to
/// perform a bisecting search for optimization-related problems.
///
/// Even if multiple LLVMContext objects are created, they will all return the
/// same instance of OptBisect in order to provide a single bisect count.  Any
/// code that uses the OptBisect object should be serialized when bisection is
/// enabled in order to enable a consistent bisect count.
static ManagedStatic<OptBisect> OptBisector;

OptPassGate &LLVMContextImpl::getOptPassGate() const {
  if (!OPG)
    OPG = &(*OptBisector);
  return *OPG;
}

void LLVMContextImpl::setOptPassGate(OptPassGate& OPG) {
  this->OPG = &OPG;
}
