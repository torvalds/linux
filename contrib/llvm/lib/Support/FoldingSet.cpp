//===-- Support/FoldingSet.cpp - Uniquing Hash Set --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a hash set that can be used to remove duplication of
// nodes in a graph.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstring>
using namespace llvm;

//===----------------------------------------------------------------------===//
// FoldingSetNodeIDRef Implementation

/// ComputeHash - Compute a strong hash value for this FoldingSetNodeIDRef,
/// used to lookup the node in the FoldingSetBase.
unsigned FoldingSetNodeIDRef::ComputeHash() const {
  return static_cast<unsigned>(hash_combine_range(Data, Data+Size));
}

bool FoldingSetNodeIDRef::operator==(FoldingSetNodeIDRef RHS) const {
  if (Size != RHS.Size) return false;
  return memcmp(Data, RHS.Data, Size*sizeof(*Data)) == 0;
}

/// Used to compare the "ordering" of two nodes as defined by the
/// profiled bits and their ordering defined by memcmp().
bool FoldingSetNodeIDRef::operator<(FoldingSetNodeIDRef RHS) const {
  if (Size != RHS.Size)
    return Size < RHS.Size;
  return memcmp(Data, RHS.Data, Size*sizeof(*Data)) < 0;
}

//===----------------------------------------------------------------------===//
// FoldingSetNodeID Implementation

/// Add* - Add various data types to Bit data.
///
void FoldingSetNodeID::AddPointer(const void *Ptr) {
  // Note: this adds pointers to the hash using sizes and endianness that
  // depend on the host. It doesn't matter, however, because hashing on
  // pointer values is inherently unstable. Nothing should depend on the
  // ordering of nodes in the folding set.
  static_assert(sizeof(uintptr_t) <= sizeof(unsigned long long),
                "unexpected pointer size");
  AddInteger(reinterpret_cast<uintptr_t>(Ptr));
}
void FoldingSetNodeID::AddInteger(signed I) {
  Bits.push_back(I);
}
void FoldingSetNodeID::AddInteger(unsigned I) {
  Bits.push_back(I);
}
void FoldingSetNodeID::AddInteger(long I) {
  AddInteger((unsigned long)I);
}
void FoldingSetNodeID::AddInteger(unsigned long I) {
  if (sizeof(long) == sizeof(int))
    AddInteger(unsigned(I));
  else if (sizeof(long) == sizeof(long long)) {
    AddInteger((unsigned long long)I);
  } else {
    llvm_unreachable("unexpected sizeof(long)");
  }
}
void FoldingSetNodeID::AddInteger(long long I) {
  AddInteger((unsigned long long)I);
}
void FoldingSetNodeID::AddInteger(unsigned long long I) {
  AddInteger(unsigned(I));
  AddInteger(unsigned(I >> 32));
}

void FoldingSetNodeID::AddString(StringRef String) {
  unsigned Size =  String.size();
  Bits.push_back(Size);
  if (!Size) return;

  unsigned Units = Size / 4;
  unsigned Pos = 0;
  const unsigned *Base = (const unsigned*) String.data();

  // If the string is aligned do a bulk transfer.
  if (!((intptr_t)Base & 3)) {
    Bits.append(Base, Base + Units);
    Pos = (Units + 1) * 4;
  } else {
    // Otherwise do it the hard way.
    // To be compatible with above bulk transfer, we need to take endianness
    // into account.
    static_assert(sys::IsBigEndianHost || sys::IsLittleEndianHost,
                  "Unexpected host endianness");
    if (sys::IsBigEndianHost) {
      for (Pos += 4; Pos <= Size; Pos += 4) {
        unsigned V = ((unsigned char)String[Pos - 4] << 24) |
                     ((unsigned char)String[Pos - 3] << 16) |
                     ((unsigned char)String[Pos - 2] << 8) |
                      (unsigned char)String[Pos - 1];
        Bits.push_back(V);
      }
    } else {  // Little-endian host
      for (Pos += 4; Pos <= Size; Pos += 4) {
        unsigned V = ((unsigned char)String[Pos - 1] << 24) |
                     ((unsigned char)String[Pos - 2] << 16) |
                     ((unsigned char)String[Pos - 3] << 8) |
                      (unsigned char)String[Pos - 4];
        Bits.push_back(V);
      }
    }
  }

  // With the leftover bits.
  unsigned V = 0;
  // Pos will have overshot size by 4 - #bytes left over.
  // No need to take endianness into account here - this is always executed.
  switch (Pos - Size) {
  case 1: V = (V << 8) | (unsigned char)String[Size - 3]; LLVM_FALLTHROUGH;
  case 2: V = (V << 8) | (unsigned char)String[Size - 2]; LLVM_FALLTHROUGH;
  case 3: V = (V << 8) | (unsigned char)String[Size - 1]; break;
  default: return; // Nothing left.
  }

  Bits.push_back(V);
}

// AddNodeID - Adds the Bit data of another ID to *this.
void FoldingSetNodeID::AddNodeID(const FoldingSetNodeID &ID) {
  Bits.append(ID.Bits.begin(), ID.Bits.end());
}

/// ComputeHash - Compute a strong hash value for this FoldingSetNodeID, used to
/// lookup the node in the FoldingSetBase.
unsigned FoldingSetNodeID::ComputeHash() const {
  return FoldingSetNodeIDRef(Bits.data(), Bits.size()).ComputeHash();
}

/// operator== - Used to compare two nodes to each other.
///
bool FoldingSetNodeID::operator==(const FoldingSetNodeID &RHS) const {
  return *this == FoldingSetNodeIDRef(RHS.Bits.data(), RHS.Bits.size());
}

/// operator== - Used to compare two nodes to each other.
///
bool FoldingSetNodeID::operator==(FoldingSetNodeIDRef RHS) const {
  return FoldingSetNodeIDRef(Bits.data(), Bits.size()) == RHS;
}

/// Used to compare the "ordering" of two nodes as defined by the
/// profiled bits and their ordering defined by memcmp().
bool FoldingSetNodeID::operator<(const FoldingSetNodeID &RHS) const {
  return *this < FoldingSetNodeIDRef(RHS.Bits.data(), RHS.Bits.size());
}

bool FoldingSetNodeID::operator<(FoldingSetNodeIDRef RHS) const {
  return FoldingSetNodeIDRef(Bits.data(), Bits.size()) < RHS;
}

/// Intern - Copy this node's data to a memory region allocated from the
/// given allocator and return a FoldingSetNodeIDRef describing the
/// interned data.
FoldingSetNodeIDRef
FoldingSetNodeID::Intern(BumpPtrAllocator &Allocator) const {
  unsigned *New = Allocator.Allocate<unsigned>(Bits.size());
  std::uninitialized_copy(Bits.begin(), Bits.end(), New);
  return FoldingSetNodeIDRef(New, Bits.size());
}

//===----------------------------------------------------------------------===//
/// Helper functions for FoldingSetBase.

/// GetNextPtr - In order to save space, each bucket is a
/// singly-linked-list. In order to make deletion more efficient, we make
/// the list circular, so we can delete a node without computing its hash.
/// The problem with this is that the start of the hash buckets are not
/// Nodes.  If NextInBucketPtr is a bucket pointer, this method returns null:
/// use GetBucketPtr when this happens.
static FoldingSetBase::Node *GetNextPtr(void *NextInBucketPtr) {
  // The low bit is set if this is the pointer back to the bucket.
  if (reinterpret_cast<intptr_t>(NextInBucketPtr) & 1)
    return nullptr;

  return static_cast<FoldingSetBase::Node*>(NextInBucketPtr);
}


/// testing.
static void **GetBucketPtr(void *NextInBucketPtr) {
  intptr_t Ptr = reinterpret_cast<intptr_t>(NextInBucketPtr);
  assert((Ptr & 1) && "Not a bucket pointer");
  return reinterpret_cast<void**>(Ptr & ~intptr_t(1));
}

/// GetBucketFor - Hash the specified node ID and return the hash bucket for
/// the specified ID.
static void **GetBucketFor(unsigned Hash, void **Buckets, unsigned NumBuckets) {
  // NumBuckets is always a power of 2.
  unsigned BucketNum = Hash & (NumBuckets-1);
  return Buckets + BucketNum;
}

/// AllocateBuckets - Allocated initialized bucket memory.
static void **AllocateBuckets(unsigned NumBuckets) {
  void **Buckets = static_cast<void**>(safe_calloc(NumBuckets + 1,
                                                   sizeof(void*)));
  // Set the very last bucket to be a non-null "pointer".
  Buckets[NumBuckets] = reinterpret_cast<void*>(-1);
  return Buckets;
}

//===----------------------------------------------------------------------===//
// FoldingSetBase Implementation

void FoldingSetBase::anchor() {}

FoldingSetBase::FoldingSetBase(unsigned Log2InitSize) {
  assert(5 < Log2InitSize && Log2InitSize < 32 &&
         "Initial hash table size out of range");
  NumBuckets = 1 << Log2InitSize;
  Buckets = AllocateBuckets(NumBuckets);
  NumNodes = 0;
}

FoldingSetBase::FoldingSetBase(FoldingSetBase &&Arg)
    : Buckets(Arg.Buckets), NumBuckets(Arg.NumBuckets), NumNodes(Arg.NumNodes) {
  Arg.Buckets = nullptr;
  Arg.NumBuckets = 0;
  Arg.NumNodes = 0;
}

FoldingSetBase &FoldingSetBase::operator=(FoldingSetBase &&RHS) {
  free(Buckets); // This may be null if the set is in a moved-from state.
  Buckets = RHS.Buckets;
  NumBuckets = RHS.NumBuckets;
  NumNodes = RHS.NumNodes;
  RHS.Buckets = nullptr;
  RHS.NumBuckets = 0;
  RHS.NumNodes = 0;
  return *this;
}

FoldingSetBase::~FoldingSetBase() {
  free(Buckets);
}

void FoldingSetBase::clear() {
  // Set all but the last bucket to null pointers.
  memset(Buckets, 0, NumBuckets*sizeof(void*));

  // Set the very last bucket to be a non-null "pointer".
  Buckets[NumBuckets] = reinterpret_cast<void*>(-1);

  // Reset the node count to zero.
  NumNodes = 0;
}

void FoldingSetBase::GrowBucketCount(unsigned NewBucketCount) {
  assert((NewBucketCount > NumBuckets) && "Can't shrink a folding set with GrowBucketCount");
  assert(isPowerOf2_32(NewBucketCount) && "Bad bucket count!");
  void **OldBuckets = Buckets;
  unsigned OldNumBuckets = NumBuckets;

  // Clear out new buckets.
  Buckets = AllocateBuckets(NewBucketCount);
  // Set NumBuckets only if allocation of new buckets was successful.
  NumBuckets = NewBucketCount;
  NumNodes = 0;

  // Walk the old buckets, rehashing nodes into their new place.
  FoldingSetNodeID TempID;
  for (unsigned i = 0; i != OldNumBuckets; ++i) {
    void *Probe = OldBuckets[i];
    if (!Probe) continue;
    while (Node *NodeInBucket = GetNextPtr(Probe)) {
      // Figure out the next link, remove NodeInBucket from the old link.
      Probe = NodeInBucket->getNextInBucket();
      NodeInBucket->SetNextInBucket(nullptr);

      // Insert the node into the new bucket, after recomputing the hash.
      InsertNode(NodeInBucket,
                 GetBucketFor(ComputeNodeHash(NodeInBucket, TempID),
                              Buckets, NumBuckets));
      TempID.clear();
    }
  }

  free(OldBuckets);
}

/// GrowHashTable - Double the size of the hash table and rehash everything.
///
void FoldingSetBase::GrowHashTable() {
  GrowBucketCount(NumBuckets * 2);
}

void FoldingSetBase::reserve(unsigned EltCount) {
  // This will give us somewhere between EltCount / 2 and
  // EltCount buckets.  This puts us in the load factor
  // range of 1.0 - 2.0.
  if(EltCount < capacity())
    return;
  GrowBucketCount(PowerOf2Floor(EltCount));
}

/// FindNodeOrInsertPos - Look up the node specified by ID.  If it exists,
/// return it.  If not, return the insertion token that will make insertion
/// faster.
FoldingSetBase::Node *
FoldingSetBase::FindNodeOrInsertPos(const FoldingSetNodeID &ID,
                                    void *&InsertPos) {
  unsigned IDHash = ID.ComputeHash();
  void **Bucket = GetBucketFor(IDHash, Buckets, NumBuckets);
  void *Probe = *Bucket;

  InsertPos = nullptr;

  FoldingSetNodeID TempID;
  while (Node *NodeInBucket = GetNextPtr(Probe)) {
    if (NodeEquals(NodeInBucket, ID, IDHash, TempID))
      return NodeInBucket;
    TempID.clear();

    Probe = NodeInBucket->getNextInBucket();
  }

  // Didn't find the node, return null with the bucket as the InsertPos.
  InsertPos = Bucket;
  return nullptr;
}

/// InsertNode - Insert the specified node into the folding set, knowing that it
/// is not already in the map.  InsertPos must be obtained from
/// FindNodeOrInsertPos.
void FoldingSetBase::InsertNode(Node *N, void *InsertPos) {
  assert(!N->getNextInBucket());
  // Do we need to grow the hashtable?
  if (NumNodes+1 > capacity()) {
    GrowHashTable();
    FoldingSetNodeID TempID;
    InsertPos = GetBucketFor(ComputeNodeHash(N, TempID), Buckets, NumBuckets);
  }

  ++NumNodes;

  /// The insert position is actually a bucket pointer.
  void **Bucket = static_cast<void**>(InsertPos);

  void *Next = *Bucket;

  // If this is the first insertion into this bucket, its next pointer will be
  // null.  Pretend as if it pointed to itself, setting the low bit to indicate
  // that it is a pointer to the bucket.
  if (!Next)
    Next = reinterpret_cast<void*>(reinterpret_cast<intptr_t>(Bucket)|1);

  // Set the node's next pointer, and make the bucket point to the node.
  N->SetNextInBucket(Next);
  *Bucket = N;
}

/// RemoveNode - Remove a node from the folding set, returning true if one was
/// removed or false if the node was not in the folding set.
bool FoldingSetBase::RemoveNode(Node *N) {
  // Because each bucket is a circular list, we don't need to compute N's hash
  // to remove it.
  void *Ptr = N->getNextInBucket();
  if (!Ptr) return false;  // Not in folding set.

  --NumNodes;
  N->SetNextInBucket(nullptr);

  // Remember what N originally pointed to, either a bucket or another node.
  void *NodeNextPtr = Ptr;

  // Chase around the list until we find the node (or bucket) which points to N.
  while (true) {
    if (Node *NodeInBucket = GetNextPtr(Ptr)) {
      // Advance pointer.
      Ptr = NodeInBucket->getNextInBucket();

      // We found a node that points to N, change it to point to N's next node,
      // removing N from the list.
      if (Ptr == N) {
        NodeInBucket->SetNextInBucket(NodeNextPtr);
        return true;
      }
    } else {
      void **Bucket = GetBucketPtr(Ptr);
      Ptr = *Bucket;

      // If we found that the bucket points to N, update the bucket to point to
      // whatever is next.
      if (Ptr == N) {
        *Bucket = NodeNextPtr;
        return true;
      }
    }
  }
}

/// GetOrInsertNode - If there is an existing simple Node exactly
/// equal to the specified node, return it.  Otherwise, insert 'N' and it
/// instead.
FoldingSetBase::Node *FoldingSetBase::GetOrInsertNode(FoldingSetBase::Node *N) {
  FoldingSetNodeID ID;
  GetNodeProfile(N, ID);
  void *IP;
  if (Node *E = FindNodeOrInsertPos(ID, IP))
    return E;
  InsertNode(N, IP);
  return N;
}

//===----------------------------------------------------------------------===//
// FoldingSetIteratorImpl Implementation

FoldingSetIteratorImpl::FoldingSetIteratorImpl(void **Bucket) {
  // Skip to the first non-null non-self-cycle bucket.
  while (*Bucket != reinterpret_cast<void*>(-1) &&
         (!*Bucket || !GetNextPtr(*Bucket)))
    ++Bucket;

  NodePtr = static_cast<FoldingSetNode*>(*Bucket);
}

void FoldingSetIteratorImpl::advance() {
  // If there is another link within this bucket, go to it.
  void *Probe = NodePtr->getNextInBucket();

  if (FoldingSetNode *NextNodeInBucket = GetNextPtr(Probe))
    NodePtr = NextNodeInBucket;
  else {
    // Otherwise, this is the last link in this bucket.
    void **Bucket = GetBucketPtr(Probe);

    // Skip to the next non-null non-self-cycle bucket.
    do {
      ++Bucket;
    } while (*Bucket != reinterpret_cast<void*>(-1) &&
             (!*Bucket || !GetNextPtr(*Bucket)));

    NodePtr = static_cast<FoldingSetNode*>(*Bucket);
  }
}

//===----------------------------------------------------------------------===//
// FoldingSetBucketIteratorImpl Implementation

FoldingSetBucketIteratorImpl::FoldingSetBucketIteratorImpl(void **Bucket) {
  Ptr = (!*Bucket || !GetNextPtr(*Bucket)) ? (void*) Bucket : *Bucket;
}
