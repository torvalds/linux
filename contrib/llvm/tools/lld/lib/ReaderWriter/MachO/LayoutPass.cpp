//===-- ReaderWriter/MachO/LayoutPass.cpp - Layout atoms ------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LayoutPass.h"
#include "lld/Core/Instrumentation.h"
#include "lld/Core/PassManager.h"
#include "lld/ReaderWriter/MachOLinkingContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Parallel.h"
#include <algorithm>
#include <set>
#include <utility>

using namespace lld;

#define DEBUG_TYPE "LayoutPass"

namespace lld {
namespace mach_o {

static bool compareAtoms(const LayoutPass::SortKey &,
                         const LayoutPass::SortKey &,
                         LayoutPass::SortOverride customSorter);

#ifndef NDEBUG
// Return "reason (leftval, rightval)"
static std::string formatReason(StringRef reason, int leftVal, int rightVal) {
  return (Twine(reason) + " (" + Twine(leftVal) + ", " + Twine(rightVal) + ")")
      .str();
}

// Less-than relationship of two atoms must be transitive, which is, if a < b
// and b < c, a < c must be true. This function checks the transitivity by
// checking the sort results.
static void checkTransitivity(std::vector<LayoutPass::SortKey> &vec,
                              LayoutPass::SortOverride customSorter) {
  for (auto i = vec.begin(), e = vec.end(); (i + 1) != e; ++i) {
    for (auto j = i + 1; j != e; ++j) {
      assert(compareAtoms(*i, *j, customSorter));
      assert(!compareAtoms(*j, *i, customSorter));
    }
  }
}

// Helper functions to check follow-on graph.
typedef llvm::DenseMap<const DefinedAtom *, const DefinedAtom *> AtomToAtomT;

static std::string atomToDebugString(const Atom *atom) {
  const DefinedAtom *definedAtom = dyn_cast<DefinedAtom>(atom);
  std::string str;
  llvm::raw_string_ostream s(str);
  if (definedAtom->name().empty())
    s << "<anonymous " << definedAtom << ">";
  else
    s << definedAtom->name();
  s << " in ";
  if (definedAtom->customSectionName().empty())
    s << "<anonymous>";
  else
    s << definedAtom->customSectionName();
  s.flush();
  return str;
}

static void showCycleDetectedError(const Registry &registry,
                                   AtomToAtomT &followOnNexts,
                                   const DefinedAtom *atom) {
  const DefinedAtom *start = atom;
  llvm::dbgs() << "There's a cycle in a follow-on chain!\n";
  do {
    llvm::dbgs() << "  " << atomToDebugString(atom) << "\n";
    for (const Reference *ref : *atom) {
      StringRef kindValStr;
      if (!registry.referenceKindToString(ref->kindNamespace(), ref->kindArch(),
                                          ref->kindValue(), kindValStr)) {
        kindValStr = "<unknown>";
      }
      llvm::dbgs() << "    " << kindValStr
                   << ": " << atomToDebugString(ref->target()) << "\n";
    }
    atom = followOnNexts[atom];
  } while (atom != start);
  llvm::report_fatal_error("Cycle detected");
}

/// Exit if there's a cycle in a followon chain reachable from the
/// given root atom. Uses the tortoise and hare algorithm to detect a
/// cycle.
static void checkNoCycleInFollowonChain(const Registry &registry,
                                        AtomToAtomT &followOnNexts,
                                        const DefinedAtom *root) {
  const DefinedAtom *tortoise = root;
  const DefinedAtom *hare = followOnNexts[root];
  while (true) {
    if (!tortoise || !hare)
      return;
    if (tortoise == hare)
      showCycleDetectedError(registry, followOnNexts, tortoise);
    tortoise = followOnNexts[tortoise];
    hare = followOnNexts[followOnNexts[hare]];
  }
}

static void checkReachabilityFromRoot(AtomToAtomT &followOnRoots,
                                      const DefinedAtom *atom) {
  if (!atom) return;
  auto i = followOnRoots.find(atom);
  if (i == followOnRoots.end()) {
    llvm_unreachable(((Twine("Atom <") + atomToDebugString(atom) +
                       "> has no follow-on root!"))
                         .str()
                         .c_str());
  }
  const DefinedAtom *ap = i->second;
  while (true) {
    const DefinedAtom *next = followOnRoots[ap];
    if (!next) {
      llvm_unreachable((Twine("Atom <" + atomToDebugString(atom) +
                              "> is not reachable from its root!"))
                           .str()
                           .c_str());
    }
    if (next == ap)
      return;
    ap = next;
  }
}

static void printDefinedAtoms(const File::AtomRange<DefinedAtom> &atomRange) {
  for (const DefinedAtom *atom : atomRange) {
    llvm::dbgs() << "  file=" << atom->file().path()
                 << ", name=" << atom->name()
                 << ", size=" << atom->size()
                 << ", type=" << atom->contentType()
                 << ", ordinal=" << atom->ordinal()
                 << "\n";
  }
}

/// Verify that the followon chain is sane. Should not be called in
/// release binary.
void LayoutPass::checkFollowonChain(const File::AtomRange<DefinedAtom> &range) {
  ScopedTask task(getDefaultDomain(), "LayoutPass::checkFollowonChain");

  // Verify that there's no cycle in follow-on chain.
  std::set<const DefinedAtom *> roots;
  for (const auto &ai : _followOnRoots)
    roots.insert(ai.second);
  for (const DefinedAtom *root : roots)
    checkNoCycleInFollowonChain(_registry, _followOnNexts, root);

  // Verify that all the atoms in followOnNexts have references to
  // their roots.
  for (const auto &ai : _followOnNexts) {
    checkReachabilityFromRoot(_followOnRoots, ai.first);
    checkReachabilityFromRoot(_followOnRoots, ai.second);
  }
}
#endif // #ifndef NDEBUG

/// The function compares atoms by sorting atoms in the following order
/// a) Sorts atoms by their ordinal overrides (layout-after/ingroup)
/// b) Sorts atoms by their permissions
/// c) Sorts atoms by their content
/// d) Sorts atoms by custom sorter
/// e) Sorts atoms on how they appear using File Ordinality
/// f) Sorts atoms on how they appear within the File
static bool compareAtomsSub(const LayoutPass::SortKey &lc,
                            const LayoutPass::SortKey &rc,
                            LayoutPass::SortOverride customSorter,
                            std::string &reason) {
  const DefinedAtom *left = lc._atom.get();
  const DefinedAtom *right = rc._atom.get();
  if (left == right) {
    reason = "same";
    return false;
  }

  // Find the root of the chain if it is a part of a follow-on chain.
  const DefinedAtom *leftRoot = lc._root;
  const DefinedAtom *rightRoot = rc._root;

  // Sort atoms by their ordinal overrides only if they fall in the same
  // chain.
  if (leftRoot == rightRoot) {
    LLVM_DEBUG(reason = formatReason("override", lc._override, rc._override));
    return lc._override < rc._override;
  }

  // Sort same permissions together.
  DefinedAtom::ContentPermissions leftPerms = leftRoot->permissions();
  DefinedAtom::ContentPermissions rightPerms = rightRoot->permissions();

  if (leftPerms != rightPerms) {
    LLVM_DEBUG(
        reason = formatReason("contentPerms", (int)leftPerms, (int)rightPerms));
    return leftPerms < rightPerms;
  }

  // Sort same content types together.
  DefinedAtom::ContentType leftType = leftRoot->contentType();
  DefinedAtom::ContentType rightType = rightRoot->contentType();

  if (leftType != rightType) {
    LLVM_DEBUG(reason =
                   formatReason("contentType", (int)leftType, (int)rightType));
    return leftType < rightType;
  }

  // Use custom sorter if supplied.
  if (customSorter) {
    bool leftBeforeRight;
    if (customSorter(leftRoot, rightRoot, leftBeforeRight))
      return leftBeforeRight;
  }

  // Sort by .o order.
  const File *leftFile = &leftRoot->file();
  const File *rightFile = &rightRoot->file();

  if (leftFile != rightFile) {
    LLVM_DEBUG(reason = formatReason(".o order", (int)leftFile->ordinal(),
                                     (int)rightFile->ordinal()));
    return leftFile->ordinal() < rightFile->ordinal();
  }

  // Sort by atom order with .o file.
  uint64_t leftOrdinal = leftRoot->ordinal();
  uint64_t rightOrdinal = rightRoot->ordinal();

  if (leftOrdinal != rightOrdinal) {
    LLVM_DEBUG(reason = formatReason("ordinal", (int)leftRoot->ordinal(),
                                     (int)rightRoot->ordinal()));
    return leftOrdinal < rightOrdinal;
  }

  llvm::errs() << "Unordered: <" << left->name() << "> <"
               << right->name() << ">\n";
  llvm_unreachable("Atoms with Same Ordinal!");
}

static bool compareAtoms(const LayoutPass::SortKey &lc,
                         const LayoutPass::SortKey &rc,
                         LayoutPass::SortOverride customSorter) {
  std::string reason;
  bool result = compareAtomsSub(lc, rc, customSorter, reason);
  LLVM_DEBUG({
    StringRef comp = result ? "<" : ">=";
    llvm::dbgs() << "Layout: '" << lc._atom.get()->name()
                 << "' " << comp << " '"
                 << rc._atom.get()->name() << "' (" << reason << ")\n";
  });
  return result;
}

LayoutPass::LayoutPass(const Registry &registry, SortOverride sorter)
    : _registry(registry), _customSorter(std::move(sorter)) {}

// Returns the atom immediately followed by the given atom in the followon
// chain.
const DefinedAtom *LayoutPass::findAtomFollowedBy(
    const DefinedAtom *targetAtom) {
  // Start from the beginning of the chain and follow the chain until
  // we find the targetChain.
  const DefinedAtom *atom = _followOnRoots[targetAtom];
  while (true) {
    const DefinedAtom *prevAtom = atom;
    AtomToAtomT::iterator targetFollowOnAtomsIter = _followOnNexts.find(atom);
    // The target atom must be in the chain of its root.
    assert(targetFollowOnAtomsIter != _followOnNexts.end());
    atom = targetFollowOnAtomsIter->second;
    if (atom == targetAtom)
      return prevAtom;
  }
}

// Check if all the atoms followed by the given target atom are of size zero.
// When this method is called, an atom being added is not of size zero and
// will be added to the head of the followon chain. All the atoms between the
// atom and the targetAtom (specified by layout-after) need to be of size zero
// in this case. Otherwise the desired layout is impossible.
bool LayoutPass::checkAllPrevAtomsZeroSize(const DefinedAtom *targetAtom) {
  const DefinedAtom *atom = _followOnRoots[targetAtom];
  while (true) {
    if (atom == targetAtom)
      return true;
    if (atom->size() != 0)
      // TODO: print warning that an impossible layout is being desired by the
      // user.
      return false;
    AtomToAtomT::iterator targetFollowOnAtomsIter = _followOnNexts.find(atom);
    // The target atom must be in the chain of its root.
    assert(targetFollowOnAtomsIter != _followOnNexts.end());
    atom = targetFollowOnAtomsIter->second;
  }
}

// Set the root of all atoms in targetAtom's chain to the given root.
void LayoutPass::setChainRoot(const DefinedAtom *targetAtom,
                              const DefinedAtom *root) {
  // Walk through the followon chain and override each node's root.
  while (true) {
    _followOnRoots[targetAtom] = root;
    AtomToAtomT::iterator targetFollowOnAtomsIter =
        _followOnNexts.find(targetAtom);
    if (targetFollowOnAtomsIter == _followOnNexts.end())
      return;
    targetAtom = targetFollowOnAtomsIter->second;
  }
}

/// This pass builds the followon tables described by two DenseMaps
/// followOnRoots and followonNexts.
/// The followOnRoots map contains a mapping of a DefinedAtom to its root
/// The followOnNexts map contains a mapping of what DefinedAtom follows the
/// current Atom
/// The algorithm follows a very simple approach
/// a) If the atom is first seen, then make that as the root atom
/// b) The targetAtom which this Atom contains, has the root thats set to the
///    root of the current atom
/// c) If the targetAtom is part of a different tree and the root of the
///    targetAtom is itself, Chain all the atoms that are contained in the tree
///    to the current Tree
/// d) If the targetAtom is part of a different chain and the root of the
///    targetAtom until the targetAtom has all atoms of size 0, then chain the
///    targetAtoms and its tree to the current chain
void LayoutPass::buildFollowOnTable(const File::AtomRange<DefinedAtom> &range) {
  ScopedTask task(getDefaultDomain(), "LayoutPass::buildFollowOnTable");
  // Set the initial size of the followon and the followonNext hash to the
  // number of atoms that we have.
  _followOnRoots.reserve(range.size());
  _followOnNexts.reserve(range.size());
  for (const DefinedAtom *ai : range) {
    for (const Reference *r : *ai) {
      if (r->kindNamespace() != lld::Reference::KindNamespace::all ||
          r->kindValue() != lld::Reference::kindLayoutAfter)
        continue;
      const DefinedAtom *targetAtom = dyn_cast<DefinedAtom>(r->target());
      _followOnNexts[ai] = targetAtom;

      // If we find a followon for the first time, let's make that atom as the
      // root atom.
      if (_followOnRoots.count(ai) == 0)
        _followOnRoots[ai] = ai;

      auto iter = _followOnRoots.find(targetAtom);
      if (iter == _followOnRoots.end()) {
        // If the targetAtom is not a root of any chain, let's make the root of
        // the targetAtom to the root of the current chain.

        // The expression m[i] = m[j] where m is a DenseMap and i != j is not
        // safe. m[j] returns a reference, which would be invalidated when a
        // rehashing occurs. If rehashing occurs to make room for m[i], m[j]
        // becomes invalid, and that invalid reference would be used as the RHS
        // value of the expression.
        // Copy the value to workaround.
        const DefinedAtom *tmp = _followOnRoots[ai];
        _followOnRoots[targetAtom] = tmp;
        continue;
      }
      if (iter->second == targetAtom) {
        // If the targetAtom is the root of a chain, the chain becomes part of
        // the current chain. Rewrite the subchain's root to the current
        // chain's root.
        setChainRoot(targetAtom, _followOnRoots[ai]);
        continue;
      }
      // The targetAtom is already a part of a chain. If the current atom is
      // of size zero, we can insert it in the middle of the chain just
      // before the target atom, while not breaking other atom's followon
      // relationships. If it's not, we can only insert the current atom at
      // the beginning of the chain. All the atoms followed by the target
      // atom must be of size zero in that case to satisfy the followon
      // relationships.
      size_t currentAtomSize = ai->size();
      if (currentAtomSize == 0) {
        const DefinedAtom *targetPrevAtom = findAtomFollowedBy(targetAtom);
        _followOnNexts[targetPrevAtom] = ai;
        const DefinedAtom *tmp = _followOnRoots[targetPrevAtom];
        _followOnRoots[ai] = tmp;
        continue;
      }
      if (!checkAllPrevAtomsZeroSize(targetAtom))
        break;
      _followOnNexts[ai] = _followOnRoots[targetAtom];
      setChainRoot(_followOnRoots[targetAtom], _followOnRoots[ai]);
    }
  }
}

/// Build an ordinal override map by traversing the followon chain, and
/// assigning ordinals to each atom, if the atoms have their ordinals
/// already assigned skip the atom and move to the next. This is the
/// main map thats used to sort the atoms while comparing two atoms together
void
LayoutPass::buildOrdinalOverrideMap(const File::AtomRange<DefinedAtom> &range) {
  ScopedTask task(getDefaultDomain(), "LayoutPass::buildOrdinalOverrideMap");
  uint64_t index = 0;
  for (const DefinedAtom *ai : range) {
    const DefinedAtom *atom = ai;
    if (_ordinalOverrideMap.find(atom) != _ordinalOverrideMap.end())
      continue;
    AtomToAtomT::iterator start = _followOnRoots.find(atom);
    if (start == _followOnRoots.end())
      continue;
    for (const DefinedAtom *nextAtom = start->second; nextAtom;
         nextAtom = _followOnNexts[nextAtom]) {
      AtomToOrdinalT::iterator pos = _ordinalOverrideMap.find(nextAtom);
      if (pos == _ordinalOverrideMap.end())
        _ordinalOverrideMap[nextAtom] = index++;
    }
  }
}

std::vector<LayoutPass::SortKey>
LayoutPass::decorate(File::AtomRange<DefinedAtom> &atomRange) const {
  std::vector<SortKey> ret;
  for (OwningAtomPtr<DefinedAtom> &atom : atomRange.owning_ptrs()) {
    auto ri = _followOnRoots.find(atom.get());
    auto oi = _ordinalOverrideMap.find(atom.get());
    const auto *root = (ri == _followOnRoots.end()) ? atom.get() : ri->second;
    uint64_t override = (oi == _ordinalOverrideMap.end()) ? 0 : oi->second;
    ret.push_back(SortKey(std::move(atom), root, override));
  }
  return ret;
}

void LayoutPass::undecorate(File::AtomRange<DefinedAtom> &atomRange,
                            std::vector<SortKey> &keys) const {
  size_t i = 0;
  for (SortKey &k : keys)
    atomRange[i++] = std::move(k._atom);
}

/// Perform the actual pass
llvm::Error LayoutPass::perform(SimpleFile &mergedFile) {
  LLVM_DEBUG(llvm::dbgs() << "******** Laying out atoms:\n");
  // sort the atoms
  ScopedTask task(getDefaultDomain(), "LayoutPass");
  File::AtomRange<DefinedAtom> atomRange = mergedFile.defined();

  // Build follow on tables
  buildFollowOnTable(atomRange);

  // Check the structure of followon graph if running in debug mode.
  LLVM_DEBUG(checkFollowonChain(atomRange));

  // Build override maps
  buildOrdinalOverrideMap(atomRange);

  LLVM_DEBUG({
    llvm::dbgs() << "unsorted atoms:\n";
    printDefinedAtoms(atomRange);
  });

  std::vector<LayoutPass::SortKey> vec = decorate(atomRange);
  sort(llvm::parallel::par, vec.begin(), vec.end(),
       [&](const LayoutPass::SortKey &l, const LayoutPass::SortKey &r) -> bool {
         return compareAtoms(l, r, _customSorter);
       });
  LLVM_DEBUG(checkTransitivity(vec, _customSorter));
  undecorate(atomRange, vec);

  LLVM_DEBUG({
    llvm::dbgs() << "sorted atoms:\n";
    printDefinedAtoms(atomRange);
  });

  LLVM_DEBUG(llvm::dbgs() << "******** Finished laying out atoms\n");
  return llvm::Error::success();
}

void addLayoutPass(PassManager &pm, const MachOLinkingContext &ctx) {
  pm.add(llvm::make_unique<LayoutPass>(
      ctx.registry(), [&](const DefinedAtom * left, const DefinedAtom * right,
                          bool & leftBeforeRight) ->bool {
    return ctx.customAtomOrderer(left, right, leftBeforeRight);
  }));
}

} // namespace mach_o
} // namespace lld
