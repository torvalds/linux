//===- ICF.cpp ------------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// ICF is short for Identical Code Folding. This is a size optimization to
// identify and merge two or more read-only sections (typically functions)
// that happened to have the same contents. It usually reduces output size
// by a few percent.
//
// In ICF, two sections are considered identical if they have the same
// section flags, section data, and relocations. Relocations are tricky,
// because two relocations are considered the same if they have the same
// relocation types, values, and if they point to the same sections *in
// terms of ICF*.
//
// Here is an example. If foo and bar defined below are compiled to the
// same machine instructions, ICF can and should merge the two, although
// their relocations point to each other.
//
//   void foo() { bar(); }
//   void bar() { foo(); }
//
// If you merge the two, their relocations point to the same section and
// thus you know they are mergeable, but how do you know they are
// mergeable in the first place? This is not an easy problem to solve.
//
// What we are doing in LLD is to partition sections into equivalence
// classes. Sections in the same equivalence class when the algorithm
// terminates are considered identical. Here are details:
//
// 1. First, we partition sections using their hash values as keys. Hash
//    values contain section types, section contents and numbers of
//    relocations. During this step, relocation targets are not taken into
//    account. We just put sections that apparently differ into different
//    equivalence classes.
//
// 2. Next, for each equivalence class, we visit sections to compare
//    relocation targets. Relocation targets are considered equivalent if
//    their targets are in the same equivalence class. Sections with
//    different relocation targets are put into different equivalence
//    clases.
//
// 3. If we split an equivalence class in step 2, two relocations
//    previously target the same equivalence class may now target
//    different equivalence classes. Therefore, we repeat step 2 until a
//    convergence is obtained.
//
// 4. For each equivalence class C, pick an arbitrary section in C, and
//    merge all the other sections in C with it.
//
// For small programs, this algorithm needs 3-5 iterations. For large
// programs such as Chromium, it takes more than 20 iterations.
//
// This algorithm was mentioned as an "optimistic algorithm" in [1],
// though gold implements a different algorithm than this.
//
// We parallelize each step so that multiple threads can work on different
// equivalence classes concurrently. That gave us a large performance
// boost when applying ICF on large programs. For example, MSVC link.exe
// or GNU gold takes 10-20 seconds to apply ICF on Chromium, whose output
// size is about 1.5 GB, but LLD can finish it in less than 2 seconds on a
// 2.8 GHz 40 core machine. Even without threading, LLD's ICF is still
// faster than MSVC or gold though.
//
// [1] Safe ICF: Pointer Safe and Unwinding aware Identical Code Folding
// in the Gold Linker
// http://static.googleusercontent.com/media/research.google.com/en//pubs/archive/36912.pdf
//
//===----------------------------------------------------------------------===//

#include "ICF.h"
#include "Config.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Writer.h"
#include "lld/Common/Threads.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/xxhash.h"
#include <algorithm>
#include <atomic>

using namespace lld;
using namespace lld::elf;
using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;

namespace {
template <class ELFT> class ICF {
public:
  void run();

private:
  void segregate(size_t Begin, size_t End, bool Constant);

  template <class RelTy>
  bool constantEq(const InputSection *A, ArrayRef<RelTy> RelsA,
                  const InputSection *B, ArrayRef<RelTy> RelsB);

  template <class RelTy>
  bool variableEq(const InputSection *A, ArrayRef<RelTy> RelsA,
                  const InputSection *B, ArrayRef<RelTy> RelsB);

  bool equalsConstant(const InputSection *A, const InputSection *B);
  bool equalsVariable(const InputSection *A, const InputSection *B);

  size_t findBoundary(size_t Begin, size_t End);

  void forEachClassRange(size_t Begin, size_t End,
                         llvm::function_ref<void(size_t, size_t)> Fn);

  void forEachClass(llvm::function_ref<void(size_t, size_t)> Fn);

  std::vector<InputSection *> Sections;

  // We repeat the main loop while `Repeat` is true.
  std::atomic<bool> Repeat;

  // The main loop counter.
  int Cnt = 0;

  // We have two locations for equivalence classes. On the first iteration
  // of the main loop, Class[0] has a valid value, and Class[1] contains
  // garbage. We read equivalence classes from slot 0 and write to slot 1.
  // So, Class[0] represents the current class, and Class[1] represents
  // the next class. On each iteration, we switch their roles and use them
  // alternately.
  //
  // Why are we doing this? Recall that other threads may be working on
  // other equivalence classes in parallel. They may read sections that we
  // are updating. We cannot update equivalence classes in place because
  // it breaks the invariance that all possibly-identical sections must be
  // in the same equivalence class at any moment. In other words, the for
  // loop to update equivalence classes is not atomic, and that is
  // observable from other threads. By writing new classes to other
  // places, we can keep the invariance.
  //
  // Below, `Current` has the index of the current class, and `Next` has
  // the index of the next class. If threading is enabled, they are either
  // (0, 1) or (1, 0).
  //
  // Note on single-thread: if that's the case, they are always (0, 0)
  // because we can safely read the next class without worrying about race
  // conditions. Using the same location makes this algorithm converge
  // faster because it uses results of the same iteration earlier.
  int Current = 0;
  int Next = 0;
};
}

// Returns true if section S is subject of ICF.
static bool isEligible(InputSection *S) {
  if (!S->Live || S->KeepUnique || !(S->Flags & SHF_ALLOC))
    return false;

  // Don't merge writable sections. .data.rel.ro sections are marked as writable
  // but are semantically read-only.
  if ((S->Flags & SHF_WRITE) && S->Name != ".data.rel.ro" &&
      !S->Name.startswith(".data.rel.ro."))
    return false;

  // SHF_LINK_ORDER sections are ICF'd as a unit with their dependent sections,
  // so we don't consider them for ICF individually.
  if (S->Flags & SHF_LINK_ORDER)
    return false;

  // Don't merge synthetic sections as their Data member is not valid and empty.
  // The Data member needs to be valid for ICF as it is used by ICF to determine
  // the equality of section contents.
  if (isa<SyntheticSection>(S))
    return false;

  // .init and .fini contains instructions that must be executed to initialize
  // and finalize the process. They cannot and should not be merged.
  if (S->Name == ".init" || S->Name == ".fini")
    return false;

  // A user program may enumerate sections named with a C identifier using
  // __start_* and __stop_* symbols. We cannot ICF any such sections because
  // that could change program semantics.
  if (isValidCIdentifier(S->Name))
    return false;

  return true;
}

// Split an equivalence class into smaller classes.
template <class ELFT>
void ICF<ELFT>::segregate(size_t Begin, size_t End, bool Constant) {
  // This loop rearranges sections in [Begin, End) so that all sections
  // that are equal in terms of equals{Constant,Variable} are contiguous
  // in [Begin, End).
  //
  // The algorithm is quadratic in the worst case, but that is not an
  // issue in practice because the number of the distinct sections in
  // each range is usually very small.

  while (Begin < End) {
    // Divide [Begin, End) into two. Let Mid be the start index of the
    // second group.
    auto Bound =
        std::stable_partition(Sections.begin() + Begin + 1,
                              Sections.begin() + End, [&](InputSection *S) {
                                if (Constant)
                                  return equalsConstant(Sections[Begin], S);
                                return equalsVariable(Sections[Begin], S);
                              });
    size_t Mid = Bound - Sections.begin();

    // Now we split [Begin, End) into [Begin, Mid) and [Mid, End) by
    // updating the sections in [Begin, Mid). We use Mid as an equivalence
    // class ID because every group ends with a unique index.
    for (size_t I = Begin; I < Mid; ++I)
      Sections[I]->Class[Next] = Mid;

    // If we created a group, we need to iterate the main loop again.
    if (Mid != End)
      Repeat = true;

    Begin = Mid;
  }
}

// Compare two lists of relocations.
template <class ELFT>
template <class RelTy>
bool ICF<ELFT>::constantEq(const InputSection *SecA, ArrayRef<RelTy> RA,
                           const InputSection *SecB, ArrayRef<RelTy> RB) {
  for (size_t I = 0; I < RA.size(); ++I) {
    if (RA[I].r_offset != RB[I].r_offset ||
        RA[I].getType(Config->IsMips64EL) != RB[I].getType(Config->IsMips64EL))
      return false;

    uint64_t AddA = getAddend<ELFT>(RA[I]);
    uint64_t AddB = getAddend<ELFT>(RB[I]);

    Symbol &SA = SecA->template getFile<ELFT>()->getRelocTargetSym(RA[I]);
    Symbol &SB = SecB->template getFile<ELFT>()->getRelocTargetSym(RB[I]);
    if (&SA == &SB) {
      if (AddA == AddB)
        continue;
      return false;
    }

    auto *DA = dyn_cast<Defined>(&SA);
    auto *DB = dyn_cast<Defined>(&SB);

    // Placeholder symbols generated by linker scripts look the same now but
    // may have different values later.
    if (!DA || !DB || DA->ScriptDefined || DB->ScriptDefined)
      return false;

    // Relocations referring to absolute symbols are constant-equal if their
    // values are equal.
    if (!DA->Section && !DB->Section && DA->Value + AddA == DB->Value + AddB)
      continue;
    if (!DA->Section || !DB->Section)
      return false;

    if (DA->Section->kind() != DB->Section->kind())
      return false;

    // Relocations referring to InputSections are constant-equal if their
    // section offsets are equal.
    if (isa<InputSection>(DA->Section)) {
      if (DA->Value + AddA == DB->Value + AddB)
        continue;
      return false;
    }

    // Relocations referring to MergeInputSections are constant-equal if their
    // offsets in the output section are equal.
    auto *X = dyn_cast<MergeInputSection>(DA->Section);
    if (!X)
      return false;
    auto *Y = cast<MergeInputSection>(DB->Section);
    if (X->getParent() != Y->getParent())
      return false;

    uint64_t OffsetA =
        SA.isSection() ? X->getOffset(AddA) : X->getOffset(DA->Value) + AddA;
    uint64_t OffsetB =
        SB.isSection() ? Y->getOffset(AddB) : Y->getOffset(DB->Value) + AddB;
    if (OffsetA != OffsetB)
      return false;
  }

  return true;
}

// Compare "non-moving" part of two InputSections, namely everything
// except relocation targets.
template <class ELFT>
bool ICF<ELFT>::equalsConstant(const InputSection *A, const InputSection *B) {
  if (A->NumRelocations != B->NumRelocations || A->Flags != B->Flags ||
      A->getSize() != B->getSize() || A->data() != B->data())
    return false;

  // If two sections have different output sections, we cannot merge them.
  // FIXME: This doesn't do the right thing in the case where there is a linker
  // script. We probably need to move output section assignment before ICF to
  // get the correct behaviour here.
  if (getOutputSectionName(A) != getOutputSectionName(B))
    return false;

  if (A->AreRelocsRela)
    return constantEq(A, A->template relas<ELFT>(), B,
                      B->template relas<ELFT>());
  return constantEq(A, A->template rels<ELFT>(), B, B->template rels<ELFT>());
}

// Compare two lists of relocations. Returns true if all pairs of
// relocations point to the same section in terms of ICF.
template <class ELFT>
template <class RelTy>
bool ICF<ELFT>::variableEq(const InputSection *SecA, ArrayRef<RelTy> RA,
                           const InputSection *SecB, ArrayRef<RelTy> RB) {
  assert(RA.size() == RB.size());

  for (size_t I = 0; I < RA.size(); ++I) {
    // The two sections must be identical.
    Symbol &SA = SecA->template getFile<ELFT>()->getRelocTargetSym(RA[I]);
    Symbol &SB = SecB->template getFile<ELFT>()->getRelocTargetSym(RB[I]);
    if (&SA == &SB)
      continue;

    auto *DA = cast<Defined>(&SA);
    auto *DB = cast<Defined>(&SB);

    // We already dealt with absolute and non-InputSection symbols in
    // constantEq, and for InputSections we have already checked everything
    // except the equivalence class.
    if (!DA->Section)
      continue;
    auto *X = dyn_cast<InputSection>(DA->Section);
    if (!X)
      continue;
    auto *Y = cast<InputSection>(DB->Section);

    // Ineligible sections are in the special equivalence class 0.
    // They can never be the same in terms of the equivalence class.
    if (X->Class[Current] == 0)
      return false;
    if (X->Class[Current] != Y->Class[Current])
      return false;
  };

  return true;
}

// Compare "moving" part of two InputSections, namely relocation targets.
template <class ELFT>
bool ICF<ELFT>::equalsVariable(const InputSection *A, const InputSection *B) {
  if (A->AreRelocsRela)
    return variableEq(A, A->template relas<ELFT>(), B,
                      B->template relas<ELFT>());
  return variableEq(A, A->template rels<ELFT>(), B, B->template rels<ELFT>());
}

template <class ELFT> size_t ICF<ELFT>::findBoundary(size_t Begin, size_t End) {
  uint32_t Class = Sections[Begin]->Class[Current];
  for (size_t I = Begin + 1; I < End; ++I)
    if (Class != Sections[I]->Class[Current])
      return I;
  return End;
}

// Sections in the same equivalence class are contiguous in Sections
// vector. Therefore, Sections vector can be considered as contiguous
// groups of sections, grouped by the class.
//
// This function calls Fn on every group within [Begin, End).
template <class ELFT>
void ICF<ELFT>::forEachClassRange(size_t Begin, size_t End,
                                  llvm::function_ref<void(size_t, size_t)> Fn) {
  while (Begin < End) {
    size_t Mid = findBoundary(Begin, End);
    Fn(Begin, Mid);
    Begin = Mid;
  }
}

// Call Fn on each equivalence class.
template <class ELFT>
void ICF<ELFT>::forEachClass(llvm::function_ref<void(size_t, size_t)> Fn) {
  // If threading is disabled or the number of sections are
  // too small to use threading, call Fn sequentially.
  if (!ThreadsEnabled || Sections.size() < 1024) {
    forEachClassRange(0, Sections.size(), Fn);
    ++Cnt;
    return;
  }

  Current = Cnt % 2;
  Next = (Cnt + 1) % 2;

  // Shard into non-overlapping intervals, and call Fn in parallel.
  // The sharding must be completed before any calls to Fn are made
  // so that Fn can modify the Chunks in its shard without causing data
  // races.
  const size_t NumShards = 256;
  size_t Step = Sections.size() / NumShards;
  size_t Boundaries[NumShards + 1];
  Boundaries[0] = 0;
  Boundaries[NumShards] = Sections.size();

  parallelForEachN(1, NumShards, [&](size_t I) {
    Boundaries[I] = findBoundary((I - 1) * Step, Sections.size());
  });

  parallelForEachN(1, NumShards + 1, [&](size_t I) {
    if (Boundaries[I - 1] < Boundaries[I])
      forEachClassRange(Boundaries[I - 1], Boundaries[I], Fn);
  });
  ++Cnt;
}

// Combine the hashes of the sections referenced by the given section into its
// hash.
template <class ELFT, class RelTy>
static void combineRelocHashes(unsigned Cnt, InputSection *IS,
                               ArrayRef<RelTy> Rels) {
  uint32_t Hash = IS->Class[Cnt % 2];
  for (RelTy Rel : Rels) {
    Symbol &S = IS->template getFile<ELFT>()->getRelocTargetSym(Rel);
    if (auto *D = dyn_cast<Defined>(&S))
      if (auto *RelSec = dyn_cast_or_null<InputSection>(D->Section))
        Hash += RelSec->Class[Cnt % 2];
  }
  // Set MSB to 1 to avoid collisions with non-hash IDs.
  IS->Class[(Cnt + 1) % 2] = Hash | (1U << 31);
}

static void print(const Twine &S) {
  if (Config->PrintIcfSections)
    message(S);
}

// The main function of ICF.
template <class ELFT> void ICF<ELFT>::run() {
  // Collect sections to merge.
  for (InputSectionBase *Sec : InputSections)
    if (auto *S = dyn_cast<InputSection>(Sec))
      if (isEligible(S))
        Sections.push_back(S);

  // Initially, we use hash values to partition sections.
  parallelForEach(Sections, [&](InputSection *S) {
    S->Class[0] = xxHash64(S->data());
  });

  for (unsigned Cnt = 0; Cnt != 2; ++Cnt) {
    parallelForEach(Sections, [&](InputSection *S) {
      if (S->AreRelocsRela)
        combineRelocHashes<ELFT>(Cnt, S, S->template relas<ELFT>());
      else
        combineRelocHashes<ELFT>(Cnt, S, S->template rels<ELFT>());
    });
  }

  // From now on, sections in Sections vector are ordered so that sections
  // in the same equivalence class are consecutive in the vector.
  std::stable_sort(Sections.begin(), Sections.end(),
                   [](InputSection *A, InputSection *B) {
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

  // Merge sections by the equivalence class.
  forEachClassRange(0, Sections.size(), [&](size_t Begin, size_t End) {
    if (End - Begin == 1)
      return;
    print("selected section " + toString(Sections[Begin]));
    for (size_t I = Begin + 1; I < End; ++I) {
      print("  removing identical section " + toString(Sections[I]));
      Sections[Begin]->replace(Sections[I]);

      // At this point we know sections merged are fully identical and hence
      // we want to remove duplicate implicit dependencies such as link order
      // and relocation sections.
      for (InputSection *IS : Sections[I]->DependentSections)
        IS->Live = false;
    }
  });
}

// ICF entry point function.
template <class ELFT> void elf::doIcf() { ICF<ELFT>().run(); }

template void elf::doIcf<ELF32LE>();
template void elf::doIcf<ELF32BE>();
template void elf::doIcf<ELF64LE>();
template void elf::doIcf<ELF64BE>();
