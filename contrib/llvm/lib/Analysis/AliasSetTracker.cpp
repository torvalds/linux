//===- AliasSetTracker.cpp - Alias Sets Tracker implementation-------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the AliasSetTracker and AliasSet classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/GuardUtils.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <vector>

using namespace llvm;

static cl::opt<unsigned>
    SaturationThreshold("alias-set-saturation-threshold", cl::Hidden,
                        cl::init(250),
                        cl::desc("The maximum number of pointers may-alias "
                                 "sets may contain before degradation"));

/// mergeSetIn - Merge the specified alias set into this alias set.
///
void AliasSet::mergeSetIn(AliasSet &AS, AliasSetTracker &AST) {
  assert(!AS.Forward && "Alias set is already forwarding!");
  assert(!Forward && "This set is a forwarding set!!");

  bool WasMustAlias = (Alias == SetMustAlias);
  // Update the alias and access types of this set...
  Access |= AS.Access;
  Alias  |= AS.Alias;

  if (Alias == SetMustAlias) {
    // Check that these two merged sets really are must aliases.  Since both
    // used to be must-alias sets, we can just check any pointer from each set
    // for aliasing.
    AliasAnalysis &AA = AST.getAliasAnalysis();
    PointerRec *L = getSomePointer();
    PointerRec *R = AS.getSomePointer();

    // If the pointers are not a must-alias pair, this set becomes a may alias.
    if (AA.alias(MemoryLocation(L->getValue(), L->getSize(), L->getAAInfo()),
                 MemoryLocation(R->getValue(), R->getSize(), R->getAAInfo())) !=
        MustAlias)
      Alias = SetMayAlias;
  }

  if (Alias == SetMayAlias) {
    if (WasMustAlias)
      AST.TotalMayAliasSetSize += size();
    if (AS.Alias == SetMustAlias)
      AST.TotalMayAliasSetSize += AS.size();
  }

  bool ASHadUnknownInsts = !AS.UnknownInsts.empty();
  if (UnknownInsts.empty()) {            // Merge call sites...
    if (ASHadUnknownInsts) {
      std::swap(UnknownInsts, AS.UnknownInsts);
      addRef();
    }
  } else if (ASHadUnknownInsts) {
    UnknownInsts.insert(UnknownInsts.end(), AS.UnknownInsts.begin(), AS.UnknownInsts.end());
    AS.UnknownInsts.clear();
  }

  AS.Forward = this; // Forward across AS now...
  addRef();          // AS is now pointing to us...

  // Merge the list of constituent pointers...
  if (AS.PtrList) {
    SetSize += AS.size();
    AS.SetSize = 0;
    *PtrListEnd = AS.PtrList;
    AS.PtrList->setPrevInList(PtrListEnd);
    PtrListEnd = AS.PtrListEnd;

    AS.PtrList = nullptr;
    AS.PtrListEnd = &AS.PtrList;
    assert(*AS.PtrListEnd == nullptr && "End of list is not null?");
  }
  if (ASHadUnknownInsts)
    AS.dropRef(AST);
}

void AliasSetTracker::removeAliasSet(AliasSet *AS) {
  if (AliasSet *Fwd = AS->Forward) {
    Fwd->dropRef(*this);
    AS->Forward = nullptr;
  } else // Update TotalMayAliasSetSize only if not forwarding.
      if (AS->Alias == AliasSet::SetMayAlias)
        TotalMayAliasSetSize -= AS->size();

  AliasSets.erase(AS);
}

void AliasSet::removeFromTracker(AliasSetTracker &AST) {
  assert(RefCount == 0 && "Cannot remove non-dead alias set from tracker!");
  AST.removeAliasSet(this);
}

void AliasSet::addPointer(AliasSetTracker &AST, PointerRec &Entry,
                          LocationSize Size, const AAMDNodes &AAInfo,
                          bool KnownMustAlias) {
  assert(!Entry.hasAliasSet() && "Entry already in set!");

  // Check to see if we have to downgrade to _may_ alias.
  if (isMustAlias() && !KnownMustAlias)
    if (PointerRec *P = getSomePointer()) {
      AliasAnalysis &AA = AST.getAliasAnalysis();
      AliasResult Result =
          AA.alias(MemoryLocation(P->getValue(), P->getSize(), P->getAAInfo()),
                   MemoryLocation(Entry.getValue(), Size, AAInfo));
      if (Result != MustAlias) {
        Alias = SetMayAlias;
        AST.TotalMayAliasSetSize += size();
      } else {
        // First entry of must alias must have maximum size!
        P->updateSizeAndAAInfo(Size, AAInfo);
      }
      assert(Result != NoAlias && "Cannot be part of must set!");
    }

  Entry.setAliasSet(this);
  Entry.updateSizeAndAAInfo(Size, AAInfo);

  // Add it to the end of the list...
  ++SetSize;
  assert(*PtrListEnd == nullptr && "End of list is not null?");
  *PtrListEnd = &Entry;
  PtrListEnd = Entry.setPrevInList(PtrListEnd);
  assert(*PtrListEnd == nullptr && "End of list is not null?");
  // Entry points to alias set.
  addRef();

  if (Alias == SetMayAlias)
    AST.TotalMayAliasSetSize++;
}

void AliasSet::addUnknownInst(Instruction *I, AliasAnalysis &AA) {
  if (UnknownInsts.empty())
    addRef();
  UnknownInsts.emplace_back(I);

  // Guards are marked as modifying memory for control flow modelling purposes,
  // but don't actually modify any specific memory location.
  using namespace PatternMatch;
  bool MayWriteMemory = I->mayWriteToMemory() && !isGuard(I) &&
    !(I->use_empty() && match(I, m_Intrinsic<Intrinsic::invariant_start>()));
  if (!MayWriteMemory) {
    Alias = SetMayAlias;
    Access |= RefAccess;
    return;
  }

  // FIXME: This should use mod/ref information to make this not suck so bad
  Alias = SetMayAlias;
  Access = ModRefAccess;
}

/// aliasesPointer - Return true if the specified pointer "may" (or must)
/// alias one of the members in the set.
///
bool AliasSet::aliasesPointer(const Value *Ptr, LocationSize Size,
                              const AAMDNodes &AAInfo,
                              AliasAnalysis &AA) const {
  if (AliasAny)
    return true;

  if (Alias == SetMustAlias) {
    assert(UnknownInsts.empty() && "Illegal must alias set!");

    // If this is a set of MustAliases, only check to see if the pointer aliases
    // SOME value in the set.
    PointerRec *SomePtr = getSomePointer();
    assert(SomePtr && "Empty must-alias set??");
    return AA.alias(MemoryLocation(SomePtr->getValue(), SomePtr->getSize(),
                                   SomePtr->getAAInfo()),
                    MemoryLocation(Ptr, Size, AAInfo));
  }

  // If this is a may-alias set, we have to check all of the pointers in the set
  // to be sure it doesn't alias the set...
  for (iterator I = begin(), E = end(); I != E; ++I)
    if (AA.alias(MemoryLocation(Ptr, Size, AAInfo),
                 MemoryLocation(I.getPointer(), I.getSize(), I.getAAInfo())))
      return true;

  // Check the unknown instructions...
  if (!UnknownInsts.empty()) {
    for (unsigned i = 0, e = UnknownInsts.size(); i != e; ++i)
      if (auto *Inst = getUnknownInst(i))
        if (isModOrRefSet(
                AA.getModRefInfo(Inst, MemoryLocation(Ptr, Size, AAInfo))))
          return true;
  }

  return false;
}

bool AliasSet::aliasesUnknownInst(const Instruction *Inst,
                                  AliasAnalysis &AA) const {

  if (AliasAny)
    return true;

  assert(Inst->mayReadOrWriteMemory() &&
         "Instruction must either read or write memory.");

  for (unsigned i = 0, e = UnknownInsts.size(); i != e; ++i) {
    if (auto *UnknownInst = getUnknownInst(i)) {
      const auto *C1 = dyn_cast<CallBase>(UnknownInst);
      const auto *C2 = dyn_cast<CallBase>(Inst);
      if (!C1 || !C2 || isModOrRefSet(AA.getModRefInfo(C1, C2)) ||
          isModOrRefSet(AA.getModRefInfo(C2, C1)))
        return true;
    }
  }

  for (iterator I = begin(), E = end(); I != E; ++I)
    if (isModOrRefSet(AA.getModRefInfo(
            Inst, MemoryLocation(I.getPointer(), I.getSize(), I.getAAInfo()))))
      return true;

  return false;
}

Instruction* AliasSet::getUniqueInstruction() {
  if (AliasAny)
    // May have collapses alias set
    return nullptr;
  if (begin() != end()) {
    if (!UnknownInsts.empty())
      // Another instruction found
      return nullptr;
    if (std::next(begin()) != end())
      // Another instruction found
      return nullptr;
    Value *Addr = begin()->getValue();
    assert(!Addr->user_empty() &&
           "where's the instruction which added this pointer?");
    if (std::next(Addr->user_begin()) != Addr->user_end())
      // Another instruction found -- this is really restrictive
      // TODO: generalize!
      return nullptr;
    return cast<Instruction>(*(Addr->user_begin()));
  }
  if (1 != UnknownInsts.size())
    return nullptr;
  return cast<Instruction>(UnknownInsts[0]);
}

void AliasSetTracker::clear() {
  // Delete all the PointerRec entries.
  for (PointerMapType::iterator I = PointerMap.begin(), E = PointerMap.end();
       I != E; ++I)
    I->second->eraseFromList();

  PointerMap.clear();

  // The alias sets should all be clear now.
  AliasSets.clear();
}


/// mergeAliasSetsForPointer - Given a pointer, merge all alias sets that may
/// alias the pointer. Return the unified set, or nullptr if no set that aliases
/// the pointer was found.
AliasSet *AliasSetTracker::mergeAliasSetsForPointer(const Value *Ptr,
                                                    LocationSize Size,
                                                    const AAMDNodes &AAInfo) {
  AliasSet *FoundSet = nullptr;
  for (iterator I = begin(), E = end(); I != E;) {
    iterator Cur = I++;
    if (Cur->Forward || !Cur->aliasesPointer(Ptr, Size, AAInfo, AA)) continue;

    if (!FoundSet) {      // If this is the first alias set ptr can go into.
      FoundSet = &*Cur;   // Remember it.
    } else {              // Otherwise, we must merge the sets.
      FoundSet->mergeSetIn(*Cur, *this);     // Merge in contents.
    }
  }

  return FoundSet;
}

AliasSet *AliasSetTracker::findAliasSetForUnknownInst(Instruction *Inst) {
  AliasSet *FoundSet = nullptr;
  for (iterator I = begin(), E = end(); I != E;) {
    iterator Cur = I++;
    if (Cur->Forward || !Cur->aliasesUnknownInst(Inst, AA))
      continue;
    if (!FoundSet)            // If this is the first alias set ptr can go into.
      FoundSet = &*Cur;       // Remember it.
    else   // Otherwise, we must merge the sets.
      FoundSet->mergeSetIn(*Cur, *this);     // Merge in contents.
  }
  return FoundSet;
}

AliasSet &AliasSetTracker::getAliasSetFor(const MemoryLocation &MemLoc) {

  Value * const Pointer = const_cast<Value*>(MemLoc.Ptr);
  const LocationSize Size = MemLoc.Size;
  const AAMDNodes &AAInfo = MemLoc.AATags;
  
  AliasSet::PointerRec &Entry = getEntryFor(Pointer);

  if (AliasAnyAS) {
    // At this point, the AST is saturated, so we only have one active alias
    // set. That means we already know which alias set we want to return, and
    // just need to add the pointer to that set to keep the data structure
    // consistent.
    // This, of course, means that we will never need a merge here.
    if (Entry.hasAliasSet()) {
      Entry.updateSizeAndAAInfo(Size, AAInfo);
      assert(Entry.getAliasSet(*this) == AliasAnyAS &&
             "Entry in saturated AST must belong to only alias set");
    } else {
      AliasAnyAS->addPointer(*this, Entry, Size, AAInfo);
    }
    return *AliasAnyAS;
  }

  // Check to see if the pointer is already known.
  if (Entry.hasAliasSet()) {
    // If the size changed, we may need to merge several alias sets.
    // Note that we can *not* return the result of mergeAliasSetsForPointer
    // due to a quirk of alias analysis behavior. Since alias(undef, undef)
    // is NoAlias, mergeAliasSetsForPointer(undef, ...) will not find the
    // the right set for undef, even if it exists.
    if (Entry.updateSizeAndAAInfo(Size, AAInfo))
      mergeAliasSetsForPointer(Pointer, Size, AAInfo);
    // Return the set!
    return *Entry.getAliasSet(*this)->getForwardedTarget(*this);
  }

  if (AliasSet *AS = mergeAliasSetsForPointer(Pointer, Size, AAInfo)) {
    // Add it to the alias set it aliases.
    AS->addPointer(*this, Entry, Size, AAInfo);
    return *AS;
  }

  // Otherwise create a new alias set to hold the loaded pointer.
  AliasSets.push_back(new AliasSet());
  AliasSets.back().addPointer(*this, Entry, Size, AAInfo);
  return AliasSets.back();
}

void AliasSetTracker::add(Value *Ptr, LocationSize Size,
                          const AAMDNodes &AAInfo) {
  addPointer(MemoryLocation(Ptr, Size, AAInfo), AliasSet::NoAccess);
}

void AliasSetTracker::add(LoadInst *LI) {
  if (isStrongerThanMonotonic(LI->getOrdering()))
    return addUnknown(LI);
  addPointer(MemoryLocation::get(LI), AliasSet::RefAccess);
}

void AliasSetTracker::add(StoreInst *SI) {
  if (isStrongerThanMonotonic(SI->getOrdering()))
    return addUnknown(SI);
  addPointer(MemoryLocation::get(SI), AliasSet::ModAccess);
}

void AliasSetTracker::add(VAArgInst *VAAI) {
  addPointer(MemoryLocation::get(VAAI), AliasSet::ModRefAccess);
}

void AliasSetTracker::add(AnyMemSetInst *MSI) {
  addPointer(MemoryLocation::getForDest(MSI), AliasSet::ModAccess);
}

void AliasSetTracker::add(AnyMemTransferInst *MTI) {
  addPointer(MemoryLocation::getForDest(MTI), AliasSet::ModAccess);
  addPointer(MemoryLocation::getForSource(MTI), AliasSet::RefAccess);
}

void AliasSetTracker::addUnknown(Instruction *Inst) {
  if (isa<DbgInfoIntrinsic>(Inst))
    return; // Ignore DbgInfo Intrinsics.

  if (auto *II = dyn_cast<IntrinsicInst>(Inst)) {
    // These intrinsics will show up as affecting memory, but they are just
    // markers.
    switch (II->getIntrinsicID()) {
    default:
      break;
      // FIXME: Add lifetime/invariant intrinsics (See: PR30807).
    case Intrinsic::assume:
    case Intrinsic::sideeffect:
      return;
    }
  }
  if (!Inst->mayReadOrWriteMemory())
    return; // doesn't alias anything

  AliasSet *AS = findAliasSetForUnknownInst(Inst);
  if (AS) {
    AS->addUnknownInst(Inst, AA);
    return;
  }
  AliasSets.push_back(new AliasSet());
  AS = &AliasSets.back();
  AS->addUnknownInst(Inst, AA);
}

void AliasSetTracker::add(Instruction *I) {
  // Dispatch to one of the other add methods.
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return add(LI);
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return add(SI);
  if (VAArgInst *VAAI = dyn_cast<VAArgInst>(I))
    return add(VAAI);
  if (AnyMemSetInst *MSI = dyn_cast<AnyMemSetInst>(I))
    return add(MSI);
  if (AnyMemTransferInst *MTI = dyn_cast<AnyMemTransferInst>(I))
    return add(MTI);

  // Handle all calls with known mod/ref sets genericall
  if (auto *Call = dyn_cast<CallBase>(I))
    if (Call->onlyAccessesArgMemory()) {
      auto getAccessFromModRef = [](ModRefInfo MRI) {
        if (isRefSet(MRI) && isModSet(MRI))
          return AliasSet::ModRefAccess;
        else if (isModSet(MRI))
          return AliasSet::ModAccess;
        else if (isRefSet(MRI))
          return AliasSet::RefAccess;
        else
          return AliasSet::NoAccess;
      };

      ModRefInfo CallMask = createModRefInfo(AA.getModRefBehavior(Call));

      // Some intrinsics are marked as modifying memory for control flow
      // modelling purposes, but don't actually modify any specific memory
      // location.
      using namespace PatternMatch;
      if (Call->use_empty() &&
          match(Call, m_Intrinsic<Intrinsic::invariant_start>()))
        CallMask = clearMod(CallMask);

      for (auto IdxArgPair : enumerate(Call->args())) {
        int ArgIdx = IdxArgPair.index();
        const Value *Arg = IdxArgPair.value();
        if (!Arg->getType()->isPointerTy())
          continue;
        MemoryLocation ArgLoc =
            MemoryLocation::getForArgument(Call, ArgIdx, nullptr);
        ModRefInfo ArgMask = AA.getArgModRefInfo(Call, ArgIdx);
        ArgMask = intersectModRef(CallMask, ArgMask);
        if (!isNoModRef(ArgMask))
          addPointer(ArgLoc, getAccessFromModRef(ArgMask));
      }
      return;
    }

  return addUnknown(I);
}

void AliasSetTracker::add(BasicBlock &BB) {
  for (auto &I : BB)
    add(&I);
}

void AliasSetTracker::add(const AliasSetTracker &AST) {
  assert(&AA == &AST.AA &&
         "Merging AliasSetTracker objects with different Alias Analyses!");

  // Loop over all of the alias sets in AST, adding the pointers contained
  // therein into the current alias sets.  This can cause alias sets to be
  // merged together in the current AST.
  for (const AliasSet &AS : AST) {
    if (AS.Forward)
      continue; // Ignore forwarding alias sets

    // If there are any call sites in the alias set, add them to this AST.
    for (unsigned i = 0, e = AS.UnknownInsts.size(); i != e; ++i)
      if (auto *Inst = AS.getUnknownInst(i))
        add(Inst);

    // Loop over all of the pointers in this alias set.
    for (AliasSet::iterator ASI = AS.begin(), E = AS.end(); ASI != E; ++ASI)
      addPointer(
          MemoryLocation(ASI.getPointer(), ASI.getSize(), ASI.getAAInfo()),
          (AliasSet::AccessLattice)AS.Access);
  }
}

// deleteValue method - This method is used to remove a pointer value from the
// AliasSetTracker entirely.  It should be used when an instruction is deleted
// from the program to update the AST.  If you don't use this, you would have
// dangling pointers to deleted instructions.
//
void AliasSetTracker::deleteValue(Value *PtrVal) {
  // First, look up the PointerRec for this pointer.
  PointerMapType::iterator I = PointerMap.find_as(PtrVal);
  if (I == PointerMap.end()) return;  // Noop

  // If we found one, remove the pointer from the alias set it is in.
  AliasSet::PointerRec *PtrValEnt = I->second;
  AliasSet *AS = PtrValEnt->getAliasSet(*this);

  // Unlink and delete from the list of values.
  PtrValEnt->eraseFromList();

  if (AS->Alias == AliasSet::SetMayAlias) {
    AS->SetSize--;
    TotalMayAliasSetSize--;
  }

  // Stop using the alias set.
  AS->dropRef(*this);

  PointerMap.erase(I);
}

// copyValue - This method should be used whenever a preexisting value in the
// program is copied or cloned, introducing a new value.  Note that it is ok for
// clients that use this method to introduce the same value multiple times: if
// the tracker already knows about a value, it will ignore the request.
//
void AliasSetTracker::copyValue(Value *From, Value *To) {
  // First, look up the PointerRec for this pointer.
  PointerMapType::iterator I = PointerMap.find_as(From);
  if (I == PointerMap.end())
    return;  // Noop
  assert(I->second->hasAliasSet() && "Dead entry?");

  AliasSet::PointerRec &Entry = getEntryFor(To);
  if (Entry.hasAliasSet()) return;    // Already in the tracker!

  // getEntryFor above may invalidate iterator \c I, so reinitialize it.
  I = PointerMap.find_as(From);
  // Add it to the alias set it aliases...
  AliasSet *AS = I->second->getAliasSet(*this);
  AS->addPointer(*this, Entry, I->second->getSize(),
                 I->second->getAAInfo(),
                 true);
}

AliasSet &AliasSetTracker::mergeAllAliasSets() {
  assert(!AliasAnyAS && (TotalMayAliasSetSize > SaturationThreshold) &&
         "Full merge should happen once, when the saturation threshold is "
         "reached");

  // Collect all alias sets, so that we can drop references with impunity
  // without worrying about iterator invalidation.
  std::vector<AliasSet *> ASVector;
  ASVector.reserve(SaturationThreshold);
  for (iterator I = begin(), E = end(); I != E; I++)
    ASVector.push_back(&*I);

  // Copy all instructions and pointers into a new set, and forward all other
  // sets to it.
  AliasSets.push_back(new AliasSet());
  AliasAnyAS = &AliasSets.back();
  AliasAnyAS->Alias = AliasSet::SetMayAlias;
  AliasAnyAS->Access = AliasSet::ModRefAccess;
  AliasAnyAS->AliasAny = true;

  for (auto Cur : ASVector) {
    // If Cur was already forwarding, just forward to the new AS instead.
    AliasSet *FwdTo = Cur->Forward;
    if (FwdTo) {
      Cur->Forward = AliasAnyAS;
      AliasAnyAS->addRef();
      FwdTo->dropRef(*this);
      continue;
    }

    // Otherwise, perform the actual merge.
    AliasAnyAS->mergeSetIn(*Cur, *this);
  }

  return *AliasAnyAS;
}

AliasSet &AliasSetTracker::addPointer(MemoryLocation Loc,
                                      AliasSet::AccessLattice E) {
  AliasSet &AS = getAliasSetFor(Loc);
  AS.Access |= E;

  if (!AliasAnyAS && (TotalMayAliasSetSize > SaturationThreshold)) {
    // The AST is now saturated. From here on, we conservatively consider all
    // pointers to alias each-other.
    return mergeAllAliasSets();
  }

  return AS;
}

//===----------------------------------------------------------------------===//
//               AliasSet/AliasSetTracker Printing Support
//===----------------------------------------------------------------------===//

void AliasSet::print(raw_ostream &OS) const {
  OS << "  AliasSet[" << (const void*)this << ", " << RefCount << "] ";
  OS << (Alias == SetMustAlias ? "must" : "may") << " alias, ";
  switch (Access) {
  case NoAccess:     OS << "No access "; break;
  case RefAccess:    OS << "Ref       "; break;
  case ModAccess:    OS << "Mod       "; break;
  case ModRefAccess: OS << "Mod/Ref   "; break;
  default: llvm_unreachable("Bad value for Access!");
  }
  if (Forward)
    OS << " forwarding to " << (void*)Forward;

  if (!empty()) {
    OS << "Pointers: ";
    for (iterator I = begin(), E = end(); I != E; ++I) {
      if (I != begin()) OS << ", ";
      I.getPointer()->printAsOperand(OS << "(");
      if (I.getSize() == LocationSize::unknown())
        OS << ", unknown)";
      else 
        OS << ", " << I.getSize() << ")";
    }
  }
  if (!UnknownInsts.empty()) {
    OS << "\n    " << UnknownInsts.size() << " Unknown instructions: ";
    for (unsigned i = 0, e = UnknownInsts.size(); i != e; ++i) {
      if (i) OS << ", ";
      if (auto *I = getUnknownInst(i)) {
        if (I->hasName())
          I->printAsOperand(OS);
        else
          I->print(OS);
      }
    }
  }
  OS << "\n";
}

void AliasSetTracker::print(raw_ostream &OS) const {
  OS << "Alias Set Tracker: " << AliasSets.size() << " alias sets for "
     << PointerMap.size() << " pointer values.\n";
  for (const AliasSet &AS : *this)
    AS.print(OS);
  OS << "\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void AliasSet::dump() const { print(dbgs()); }
LLVM_DUMP_METHOD void AliasSetTracker::dump() const { print(dbgs()); }
#endif

//===----------------------------------------------------------------------===//
//                     ASTCallbackVH Class Implementation
//===----------------------------------------------------------------------===//

void AliasSetTracker::ASTCallbackVH::deleted() {
  assert(AST && "ASTCallbackVH called with a null AliasSetTracker!");
  AST->deleteValue(getValPtr());
  // this now dangles!
}

void AliasSetTracker::ASTCallbackVH::allUsesReplacedWith(Value *V) {
  AST->copyValue(getValPtr(), V);
}

AliasSetTracker::ASTCallbackVH::ASTCallbackVH(Value *V, AliasSetTracker *ast)
  : CallbackVH(V), AST(ast) {}

AliasSetTracker::ASTCallbackVH &
AliasSetTracker::ASTCallbackVH::operator=(Value *V) {
  return *this = ASTCallbackVH(V, AST);
}

//===----------------------------------------------------------------------===//
//                            AliasSetPrinter Pass
//===----------------------------------------------------------------------===//

namespace {

  class AliasSetPrinter : public FunctionPass {
    AliasSetTracker *Tracker;

  public:
    static char ID; // Pass identification, replacement for typeid

    AliasSetPrinter() : FunctionPass(ID) {
      initializeAliasSetPrinterPass(*PassRegistry::getPassRegistry());
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
      AU.addRequired<AAResultsWrapperPass>();
    }

    bool runOnFunction(Function &F) override {
      auto &AAWP = getAnalysis<AAResultsWrapperPass>();
      Tracker = new AliasSetTracker(AAWP.getAAResults());
      errs() << "Alias sets for function '" << F.getName() << "':\n";
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I)
        Tracker->add(&*I);
      Tracker->print(errs());
      delete Tracker;
      return false;
    }
  };

} // end anonymous namespace

char AliasSetPrinter::ID = 0;

INITIALIZE_PASS_BEGIN(AliasSetPrinter, "print-alias-sets",
                "Alias Set Printer", false, true)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(AliasSetPrinter, "print-alias-sets",
                "Alias Set Printer", false, true)
