//===- llvm/Analysis/AliasSetTracker.h - Build Alias Sets -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines two classes: AliasSetTracker and AliasSet. These interfaces
// are used to classify a collection of pointer references into a maximal number
// of disjoint sets. Each AliasSet object constructed by the AliasSetTracker
// object refers to memory disjoint from the other sets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ALIASSETTRACKER_H
#define LLVM_ANALYSIS_ALIASSETTRACKER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace llvm {

class AliasSetTracker;
class BasicBlock;
class LoadInst;
class AnyMemSetInst;
class AnyMemTransferInst;
class raw_ostream;
class StoreInst;
class VAArgInst;
class Value;

class AliasSet : public ilist_node<AliasSet> {
  friend class AliasSetTracker;

  class PointerRec {
    Value *Val;  // The pointer this record corresponds to.
    PointerRec **PrevInList = nullptr;
    PointerRec *NextInList = nullptr;
    AliasSet *AS = nullptr;
    LocationSize Size = LocationSize::mapEmpty();
    AAMDNodes AAInfo;

    // Whether the size for this record has been set at all. This makes no
    // guarantees about the size being known.
    bool isSizeSet() const { return Size != LocationSize::mapEmpty(); }

  public:
    PointerRec(Value *V)
      : Val(V), AAInfo(DenseMapInfo<AAMDNodes>::getEmptyKey()) {}

    Value *getValue() const { return Val; }

    PointerRec *getNext() const { return NextInList; }
    bool hasAliasSet() const { return AS != nullptr; }

    PointerRec** setPrevInList(PointerRec **PIL) {
      PrevInList = PIL;
      return &NextInList;
    }

    bool updateSizeAndAAInfo(LocationSize NewSize, const AAMDNodes &NewAAInfo) {
      bool SizeChanged = false;
      if (NewSize != Size) {
        LocationSize OldSize = Size;
        Size = isSizeSet() ? Size.unionWith(NewSize) : NewSize;
        SizeChanged = OldSize != Size;
      }

      if (AAInfo == DenseMapInfo<AAMDNodes>::getEmptyKey())
        // We don't have a AAInfo yet. Set it to NewAAInfo.
        AAInfo = NewAAInfo;
      else {
        AAMDNodes Intersection(AAInfo.intersect(NewAAInfo));
        if (!Intersection) {
          // NewAAInfo conflicts with AAInfo.
          AAInfo = DenseMapInfo<AAMDNodes>::getTombstoneKey();
          return SizeChanged;
        }
        AAInfo = Intersection;
      }
      return SizeChanged;
    }

    LocationSize getSize() const {
      assert(isSizeSet() && "Getting an unset size!");
      return Size;
    }

    /// Return the AAInfo, or null if there is no information or conflicting
    /// information.
    AAMDNodes getAAInfo() const {
      // If we have missing or conflicting AAInfo, return null.
      if (AAInfo == DenseMapInfo<AAMDNodes>::getEmptyKey() ||
          AAInfo == DenseMapInfo<AAMDNodes>::getTombstoneKey())
        return AAMDNodes();
      return AAInfo;
    }

    AliasSet *getAliasSet(AliasSetTracker &AST) {
      assert(AS && "No AliasSet yet!");
      if (AS->Forward) {
        AliasSet *OldAS = AS;
        AS = OldAS->getForwardedTarget(AST);
        AS->addRef();
        OldAS->dropRef(AST);
      }
      return AS;
    }

    void setAliasSet(AliasSet *as) {
      assert(!AS && "Already have an alias set!");
      AS = as;
    }

    void eraseFromList() {
      if (NextInList) NextInList->PrevInList = PrevInList;
      *PrevInList = NextInList;
      if (AS->PtrListEnd == &NextInList) {
        AS->PtrListEnd = PrevInList;
        assert(*AS->PtrListEnd == nullptr && "List not terminated right!");
      }
      delete this;
    }
  };

  // Doubly linked list of nodes.
  PointerRec *PtrList = nullptr;
  PointerRec **PtrListEnd;
  // Forwarding pointer.
  AliasSet *Forward = nullptr;

  /// All instructions without a specific address in this alias set.
  /// In rare cases this vector can have a null'ed out WeakVH
  /// instances (can happen if some other loop pass deletes an
  /// instruction in this list).
  std::vector<WeakVH> UnknownInsts;

  /// Number of nodes pointing to this AliasSet plus the number of AliasSets
  /// forwarding to it.
  unsigned RefCount : 27;

  // Signifies that this set should be considered to alias any pointer.
  // Use when the tracker holding this set is saturated.
  unsigned AliasAny : 1;

  /// The kinds of access this alias set models.
  ///
  /// We keep track of whether this alias set merely refers to the locations of
  /// memory (and not any particular access), whether it modifies or references
  /// the memory, or whether it does both. The lattice goes from "NoAccess" to
  /// either RefAccess or ModAccess, then to ModRefAccess as necessary.
  enum AccessLattice {
    NoAccess = 0,
    RefAccess = 1,
    ModAccess = 2,
    ModRefAccess = RefAccess | ModAccess
  };
  unsigned Access : 2;

  /// The kind of alias relationship between pointers of the set.
  ///
  /// These represent conservatively correct alias results between any members
  /// of the set. We represent these independently of the values of alias
  /// results in order to pack it into a single bit. Lattice goes from
  /// MustAlias to MayAlias.
  enum AliasLattice {
    SetMustAlias = 0, SetMayAlias = 1
  };
  unsigned Alias : 1;

  unsigned SetSize = 0;

  void addRef() { ++RefCount; }

  void dropRef(AliasSetTracker &AST) {
    assert(RefCount >= 1 && "Invalid reference count detected!");
    if (--RefCount == 0)
      removeFromTracker(AST);
  }

  Instruction *getUnknownInst(unsigned i) const {
    assert(i < UnknownInsts.size());
    return cast_or_null<Instruction>(UnknownInsts[i]);
  }

public:
  AliasSet(const AliasSet &) = delete;
  AliasSet &operator=(const AliasSet &) = delete;

  /// Accessors...
  bool isRef() const { return Access & RefAccess; }
  bool isMod() const { return Access & ModAccess; }
  bool isMustAlias() const { return Alias == SetMustAlias; }
  bool isMayAlias()  const { return Alias == SetMayAlias; }

  /// Return true if this alias set should be ignored as part of the
  /// AliasSetTracker object.
  bool isForwardingAliasSet() const { return Forward; }

  /// Merge the specified alias set into this alias set.
  void mergeSetIn(AliasSet &AS, AliasSetTracker &AST);

  // Alias Set iteration - Allow access to all of the pointers which are part of
  // this alias set.
  class iterator;
  iterator begin() const { return iterator(PtrList); }
  iterator end()   const { return iterator(); }
  bool empty() const { return PtrList == nullptr; }

  // Unfortunately, ilist::size() is linear, so we have to add code to keep
  // track of the list's exact size.
  unsigned size() { return SetSize; }

  /// If this alias set is known to contain a single instruction and *only* a
  /// single unique instruction, return it.  Otherwise, return nullptr.
  Instruction* getUniqueInstruction();

  void print(raw_ostream &OS) const;
  void dump() const;

  /// Define an iterator for alias sets... this is just a forward iterator.
  class iterator : public std::iterator<std::forward_iterator_tag,
                                        PointerRec, ptrdiff_t> {
    PointerRec *CurNode;

  public:
    explicit iterator(PointerRec *CN = nullptr) : CurNode(CN) {}

    bool operator==(const iterator& x) const {
      return CurNode == x.CurNode;
    }
    bool operator!=(const iterator& x) const { return !operator==(x); }

    value_type &operator*() const {
      assert(CurNode && "Dereferencing AliasSet.end()!");
      return *CurNode;
    }
    value_type *operator->() const { return &operator*(); }

    Value *getPointer() const { return CurNode->getValue(); }
    LocationSize getSize() const { return CurNode->getSize(); }
    AAMDNodes getAAInfo() const { return CurNode->getAAInfo(); }

    iterator& operator++() {                // Preincrement
      assert(CurNode && "Advancing past AliasSet.end()!");
      CurNode = CurNode->getNext();
      return *this;
    }
    iterator operator++(int) { // Postincrement
      iterator tmp = *this; ++*this; return tmp;
    }
  };

private:
  // Can only be created by AliasSetTracker.
  AliasSet()
      : PtrListEnd(&PtrList), RefCount(0),  AliasAny(false), Access(NoAccess),
        Alias(SetMustAlias) {}

  PointerRec *getSomePointer() const {
    return PtrList;
  }

  /// Return the real alias set this represents. If this has been merged with
  /// another set and is forwarding, return the ultimate destination set. This
  /// also implements the union-find collapsing as well.
  AliasSet *getForwardedTarget(AliasSetTracker &AST) {
    if (!Forward) return this;

    AliasSet *Dest = Forward->getForwardedTarget(AST);
    if (Dest != Forward) {
      Dest->addRef();
      Forward->dropRef(AST);
      Forward = Dest;
    }
    return Dest;
  }

  void removeFromTracker(AliasSetTracker &AST);

  void addPointer(AliasSetTracker &AST, PointerRec &Entry, LocationSize Size,
                  const AAMDNodes &AAInfo, bool KnownMustAlias = false);
  void addUnknownInst(Instruction *I, AliasAnalysis &AA);

  void removeUnknownInst(AliasSetTracker &AST, Instruction *I) {
    bool WasEmpty = UnknownInsts.empty();
    for (size_t i = 0, e = UnknownInsts.size(); i != e; ++i)
      if (UnknownInsts[i] == I) {
        UnknownInsts[i] = UnknownInsts.back();
        UnknownInsts.pop_back();
        --i; --e;  // Revisit the moved entry.
      }
    if (!WasEmpty && UnknownInsts.empty())
      dropRef(AST);
  }

public:
  /// Return true if the specified pointer "may" (or must) alias one of the
  /// members in the set.
  bool aliasesPointer(const Value *Ptr, LocationSize Size,
                      const AAMDNodes &AAInfo, AliasAnalysis &AA) const;
  bool aliasesUnknownInst(const Instruction *Inst, AliasAnalysis &AA) const;
};

inline raw_ostream& operator<<(raw_ostream &OS, const AliasSet &AS) {
  AS.print(OS);
  return OS;
}

class AliasSetTracker {
  /// A CallbackVH to arrange for AliasSetTracker to be notified whenever a
  /// Value is deleted.
  class ASTCallbackVH final : public CallbackVH {
    AliasSetTracker *AST;

    void deleted() override;
    void allUsesReplacedWith(Value *) override;

  public:
    ASTCallbackVH(Value *V, AliasSetTracker *AST = nullptr);

    ASTCallbackVH &operator=(Value *V);
  };
  /// Traits to tell DenseMap that tell us how to compare and hash the value
  /// handle.
  struct ASTCallbackVHDenseMapInfo : public DenseMapInfo<Value *> {};

  AliasAnalysis &AA;
  ilist<AliasSet> AliasSets;

  using PointerMapType = DenseMap<ASTCallbackVH, AliasSet::PointerRec *,
                                  ASTCallbackVHDenseMapInfo>;

  // Map from pointers to their node
  PointerMapType PointerMap;

public:
  /// Create an empty collection of AliasSets, and use the specified alias
  /// analysis object to disambiguate load and store addresses.
  explicit AliasSetTracker(AliasAnalysis &aa) : AA(aa) {}
  ~AliasSetTracker() { clear(); }

  /// These methods are used to add different types of instructions to the alias
  /// sets. Adding a new instruction can result in one of three actions
  /// happening:
  ///
  ///   1. If the instruction doesn't alias any other sets, create a new set.
  ///   2. If the instruction aliases exactly one set, add it to the set
  ///   3. If the instruction aliases multiple sets, merge the sets, and add
  ///      the instruction to the result.
  ///
  /// These methods return true if inserting the instruction resulted in the
  /// addition of a new alias set (i.e., the pointer did not alias anything).
  ///
  void add(Value *Ptr, LocationSize Size, const AAMDNodes &AAInfo); // Add a loc
  void add(LoadInst *LI);
  void add(StoreInst *SI);
  void add(VAArgInst *VAAI);
  void add(AnyMemSetInst *MSI);
  void add(AnyMemTransferInst *MTI);
  void add(Instruction *I);       // Dispatch to one of the other add methods...
  void add(BasicBlock &BB);       // Add all instructions in basic block
  void add(const AliasSetTracker &AST); // Add alias relations from another AST
  void addUnknown(Instruction *I);

  void clear();

  /// Return the alias sets that are active.
  const ilist<AliasSet> &getAliasSets() const { return AliasSets; }

  /// Return the alias set which contains the specified memory location.  If
  /// the memory location aliases two or more existing alias sets, will have
  /// the effect of merging those alias sets before the single resulting alias
  /// set is returned.
  AliasSet &getAliasSetFor(const MemoryLocation &MemLoc);

  /// Return the underlying alias analysis object used by this tracker.
  AliasAnalysis &getAliasAnalysis() const { return AA; }

  /// This method is used to remove a pointer value from the AliasSetTracker
  /// entirely. It should be used when an instruction is deleted from the
  /// program to update the AST. If you don't use this, you would have dangling
  /// pointers to deleted instructions.
  void deleteValue(Value *PtrVal);

  /// This method should be used whenever a preexisting value in the program is
  /// copied or cloned, introducing a new value.  Note that it is ok for clients
  /// that use this method to introduce the same value multiple times: if the
  /// tracker already knows about a value, it will ignore the request.
  void copyValue(Value *From, Value *To);

  using iterator = ilist<AliasSet>::iterator;
  using const_iterator = ilist<AliasSet>::const_iterator;

  const_iterator begin() const { return AliasSets.begin(); }
  const_iterator end()   const { return AliasSets.end(); }

  iterator begin() { return AliasSets.begin(); }
  iterator end()   { return AliasSets.end(); }

  void print(raw_ostream &OS) const;
  void dump() const;

private:
  friend class AliasSet;

  // The total number of pointers contained in all "may" alias sets.
  unsigned TotalMayAliasSetSize = 0;

  // A non-null value signifies this AST is saturated. A saturated AST lumps
  // all pointers into a single "May" set.
  AliasSet *AliasAnyAS = nullptr;

  void removeAliasSet(AliasSet *AS);

  /// Just like operator[] on the map, except that it creates an entry for the
  /// pointer if it doesn't already exist.
  AliasSet::PointerRec &getEntryFor(Value *V) {
    AliasSet::PointerRec *&Entry = PointerMap[ASTCallbackVH(V, this)];
    if (!Entry)
      Entry = new AliasSet::PointerRec(V);
    return *Entry;
  }

  AliasSet &addPointer(MemoryLocation Loc, AliasSet::AccessLattice E);
  AliasSet *mergeAliasSetsForPointer(const Value *Ptr, LocationSize Size,
                                     const AAMDNodes &AAInfo);

  /// Merge all alias sets into a single set that is considered to alias any
  /// pointer.
  AliasSet &mergeAllAliasSets();

  AliasSet *findAliasSetForUnknownInst(Instruction *Inst);
};

inline raw_ostream& operator<<(raw_ostream &OS, const AliasSetTracker &AST) {
  AST.print(OS);
  return OS;
}

} // end namespace llvm

#endif // LLVM_ANALYSIS_ALIASSETTRACKER_H
