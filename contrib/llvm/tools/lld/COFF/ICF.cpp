//===- ICF.cpp ------------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// ICF is short for Identical Code Folding. That is a size optimization to
// identify and merge two or more read-only sections (typically functions)
// that happened to have the same contents. It usually reduces output size
// by a few percent.
//
// On Windows, ICF is enabled by default.
//
// See ELF/ICF.cpp for the details about the algortihm.
//
//===----------------------------------------------------------------------===//

#include "ICF.h"
#include "Chunks.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Threads.h"
#include "lld/Common/Timer.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/xxhash.h"
#include <algorithm>
#include <atomic>
#include <vector>

using namespace llvm;

namespace lld {
namespace coff {

static Timer ICFTimer("ICF", Timer::root());

class ICF {
public:
  void run(ArrayRef<Chunk *> V);

private:
  void segregate(size_t Begin, size_t End, bool Constant);

  bool assocEquals(const SectionChunk *A, const SectionChunk *B);

  bool equalsConstant(const SectionChunk *A, const SectionChunk *B);
  bool equalsVariable(const SectionChunk *A, const SectionChunk *B);

  uint32_t getHash(SectionChunk *C);
  bool isEligible(SectionChunk *C);

  size_t findBoundary(size_t Begin, size_t End);

  void forEachClassRange(size_t Begin, size_t End,
                         std::function<void(size_t, size_t)> Fn);

  void forEachClass(std::function<void(size_t, size_t)> Fn);

  std::vector<SectionChunk *> Chunks;
  int Cnt = 0;
  std::atomic<bool> Repeat = {false};
};

// Returns true if section S is subject of ICF.
//
// Microsoft's documentation
// (https://msdn.microsoft.com/en-us/library/bxwfs976.aspx; visited April
// 2017) says that /opt:icf folds both functions and read-only data.
// Despite that, the MSVC linker folds only functions. We found
// a few instances of programs that are not safe for data merging.
// Therefore, we merge only functions just like the MSVC tool. However, we also
// merge read-only sections in a couple of cases where the address of the
// section is insignificant to the user program and the behaviour matches that
// of the Visual C++ linker.
bool ICF::isEligible(SectionChunk *C) {
  // Non-comdat chunks, dead chunks, and writable chunks are not elegible.
  bool Writable = C->getOutputCharacteristics() & llvm::COFF::IMAGE_SCN_MEM_WRITE;
  if (!C->isCOMDAT() || !C->Live || Writable)
    return false;

  // Code sections are eligible.
  if (C->getOutputCharacteristics() & llvm::COFF::IMAGE_SCN_MEM_EXECUTE)
    return true;

  // .pdata and .xdata unwind info sections are eligible.
  StringRef OutSecName = C->getSectionName().split('$').first;
  if (OutSecName == ".pdata" || OutSecName == ".xdata")
    return true;

  // So are vtables.
  if (C->Sym && C->Sym->getName().startswith("??_7"))
    return true;

  // Anything else not in an address-significance table is eligible.
  return !C->KeepUnique;
}

// Split an equivalence class into smaller classes.
void ICF::segregate(size_t Begin, size_t End, bool Constant) {
  while (Begin < End) {
    // Divide [Begin, End) into two. Let Mid be the start index of the
    // second group.
    auto Bound = std::stable_partition(
        Chunks.begin() + Begin + 1, Chunks.begin() + End, [&](SectionChunk *S) {
          if (Constant)
            return equalsConstant(Chunks[Begin], S);
          return equalsVariable(Chunks[Begin], S);
        });
    size_t Mid = Bound - Chunks.begin();

    // Split [Begin, End) into [Begin, Mid) and [Mid, End). We use Mid as an
    // equivalence class ID because every group ends with a unique index.
    for (size_t I = Begin; I < Mid; ++I)
      Chunks[I]->Class[(Cnt + 1) % 2] = Mid;

    // If we created a group, we need to iterate the main loop again.
    if (Mid != End)
      Repeat = true;

    Begin = Mid;
  }
}

// Returns true if two sections' associative children are equal.
bool ICF::assocEquals(const SectionChunk *A, const SectionChunk *B) {
  auto ChildClasses = [&](const SectionChunk *SC) {
    std::vector<uint32_t> Classes;
    for (const SectionChunk *C : SC->children())
      if (!C->SectionName.startswith(".debug") &&
          C->SectionName != ".gfids$y" && C->SectionName != ".gljmp$y")
        Classes.push_back(C->Class[Cnt % 2]);
    return Classes;
  };
  return ChildClasses(A) == ChildClasses(B);
}

// Compare "non-moving" part of two sections, namely everything
// except relocation targets.
bool ICF::equalsConstant(const SectionChunk *A, const SectionChunk *B) {
  if (A->Relocs.size() != B->Relocs.size())
    return false;

  // Compare relocations.
  auto Eq = [&](const coff_relocation &R1, const coff_relocation &R2) {
    if (R1.Type != R2.Type ||
        R1.VirtualAddress != R2.VirtualAddress) {
      return false;
    }
    Symbol *B1 = A->File->getSymbol(R1.SymbolTableIndex);
    Symbol *B2 = B->File->getSymbol(R2.SymbolTableIndex);
    if (B1 == B2)
      return true;
    if (auto *D1 = dyn_cast<DefinedRegular>(B1))
      if (auto *D2 = dyn_cast<DefinedRegular>(B2))
        return D1->getValue() == D2->getValue() &&
               D1->getChunk()->Class[Cnt % 2] == D2->getChunk()->Class[Cnt % 2];
    return false;
  };
  if (!std::equal(A->Relocs.begin(), A->Relocs.end(), B->Relocs.begin(), Eq))
    return false;

  // Compare section attributes and contents.
  return A->getOutputCharacteristics() == B->getOutputCharacteristics() &&
         A->SectionName == B->SectionName &&
         A->Header->SizeOfRawData == B->Header->SizeOfRawData &&
         A->Checksum == B->Checksum && A->getContents() == B->getContents() &&
         assocEquals(A, B);
}

// Compare "moving" part of two sections, namely relocation targets.
bool ICF::equalsVariable(const SectionChunk *A, const SectionChunk *B) {
  // Compare relocations.
  auto Eq = [&](const coff_relocation &R1, const coff_relocation &R2) {
    Symbol *B1 = A->File->getSymbol(R1.SymbolTableIndex);
    Symbol *B2 = B->File->getSymbol(R2.SymbolTableIndex);
    if (B1 == B2)
      return true;
    if (auto *D1 = dyn_cast<DefinedRegular>(B1))
      if (auto *D2 = dyn_cast<DefinedRegular>(B2))
        return D1->getChunk()->Class[Cnt % 2] == D2->getChunk()->Class[Cnt % 2];
    return false;
  };
  return std::equal(A->Relocs.begin(), A->Relocs.end(), B->Relocs.begin(),
                    Eq) &&
         assocEquals(A, B);
}

// Find the first Chunk after Begin that has a different class from Begin.
size_t ICF::findBoundary(size_t Begin, size_t End) {
  for (size_t I = Begin + 1; I < End; ++I)
    if (Chunks[Begin]->Class[Cnt % 2] != Chunks[I]->Class[Cnt % 2])
      return I;
  return End;
}

void ICF::forEachClassRange(size_t Begin, size_t End,
                            std::function<void(size_t, size_t)> Fn) {
  while (Begin < End) {
    size_t Mid = findBoundary(Begin, End);
    Fn(Begin, Mid);
    Begin = Mid;
  }
}

// Call Fn on each class group.
void ICF::forEachClass(std::function<void(size_t, size_t)> Fn) {
  // If the number of sections are too small to use threading,
  // call Fn sequentially.
  if (Chunks.size() < 1024) {
    forEachClassRange(0, Chunks.size(), Fn);
    ++Cnt;
    return;
  }

  // Shard into non-overlapping intervals, and call Fn in parallel.
  // The sharding must be completed before any calls to Fn are made
  // so that Fn can modify the Chunks in its shard without causing data
  // races.
  const size_t NumShards = 256;
  size_t Step = Chunks.size() / NumShards;
  size_t Boundaries[NumShards + 1];
  Boundaries[0] = 0;
  Boundaries[NumShards] = Chunks.size();
  parallelForEachN(1, NumShards, [&](size_t I) {
    Boundaries[I] = findBoundary((I - 1) * Step, Chunks.size());
  });
  parallelForEachN(1, NumShards + 1, [&](size_t I) {
    if (Boundaries[I - 1] < Boundaries[I]) {
      forEachClassRange(Boundaries[I - 1], Boundaries[I], Fn);
    }
  });
  ++Cnt;
}

// Merge identical COMDAT sections.
// Two sections are considered the same if their section headers,
// contents and relocations are all the same.
void ICF::run(ArrayRef<Chunk *> Vec) {
  ScopedTimer T(ICFTimer);

  // Collect only mergeable sections and group by hash value.
  uint32_t NextId = 1;
  for (Chunk *C : Vec) {
    if (auto *SC = dyn_cast<SectionChunk>(C)) {
      if (isEligible(SC))
        Chunks.push_back(SC);
      else
        SC->Class[0] = NextId++;
    }
  }

  // Make sure that ICF doesn't merge sections that are being handled by string
  // tail merging.
  for (auto &P : MergeChunk::Instances)
    for (SectionChunk *SC : P.second->Sections)
      SC->Class[0] = NextId++;

  // Initially, we use hash values to partition sections.
  parallelForEach(Chunks, [&](SectionChunk *SC) {
    SC->Class[0] = xxHash64(SC->getContents());
  });

  // Combine the hashes of the sections referenced by each section into its
  // hash.
  for (unsigned Cnt = 0; Cnt != 2; ++Cnt) {
    parallelForEach(Chunks, [&](SectionChunk *SC) {
      uint32_t Hash = SC->Class[Cnt % 2];
      for (Symbol *B : SC->symbols())
        if (auto *Sym = dyn_cast_or_null<DefinedRegular>(B))
          Hash += Sym->getChunk()->Class[Cnt % 2];
      // Set MSB to 1 to avoid collisions with non-hash classs.
      SC->Class[(Cnt + 1) % 2] = Hash | (1U << 31);
    });
  }

  // From now on, sections in Chunks are ordered so that sections in
  // the same group are consecutive in the vector.
  std::stable_sort(Chunks.begin(), Chunks.end(),
                   [](SectionChunk *A, SectionChunk *B) {
                     return A->Class[0] < B->Class[0];
                   });

  // Compare static contents and assign unique IDs for each static content.
  forEachClass([&](size_t Begin, size_t End) { segregate(Begin, End, true); });

  // Split groups by comparing relocations until convergence is obtained.
  do {
    Repeat = false;
    forEachClass(
        [&](size_t Begin, size_t End) { segregate(Begin, End, false); });
  } while (Repeat);

  log("ICF needed " + Twine(Cnt) + " iterations");

  // Merge sections in the same classs.
  forEachClass([&](size_t Begin, size_t End) {
    if (End - Begin == 1)
      return;

    log("Selected " + Chunks[Begin]->getDebugName());
    for (size_t I = Begin + 1; I < End; ++I) {
      log("  Removed " + Chunks[I]->getDebugName());
      Chunks[Begin]->replace(Chunks[I]);
    }
  });
}

// Entry point to ICF.
void doICF(ArrayRef<Chunk *> Chunks) { ICF().run(Chunks); }

} // namespace coff
} // namespace lld
