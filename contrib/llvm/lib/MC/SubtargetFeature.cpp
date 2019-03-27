//===- SubtargetFeature.cpp - CPU characteristics Implementation ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file Implements the SubtargetFeature interface.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/SubtargetFeature.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

using namespace llvm;

/// Determine if a feature has a flag; '+' or '-'
static inline bool hasFlag(StringRef Feature) {
  assert(!Feature.empty() && "Empty string");
  // Get first character
  char Ch = Feature[0];
  // Check if first character is '+' or '-' flag
  return Ch == '+' || Ch =='-';
}

/// Return string stripped of flag.
static inline std::string StripFlag(StringRef Feature) {
  return hasFlag(Feature) ? Feature.substr(1) : Feature;
}

/// Return true if enable flag; '+'.
static inline bool isEnabled(StringRef Feature) {
  assert(!Feature.empty() && "Empty string");
  // Get first character
  char Ch = Feature[0];
  // Check if first character is '+' for enabled
  return Ch == '+';
}

/// Splits a string of comma separated items in to a vector of strings.
static void Split(std::vector<std::string> &V, StringRef S) {
  SmallVector<StringRef, 3> Tmp;
  S.split(Tmp, ',', -1, false /* KeepEmpty */);
  V.assign(Tmp.begin(), Tmp.end());
}

void SubtargetFeatures::AddFeature(StringRef String, bool Enable) {
  // Don't add empty features.
  if (!String.empty())
    // Convert to lowercase, prepend flag if we don't already have a flag.
    Features.push_back(hasFlag(String) ? String.lower()
                                       : (Enable ? "+" : "-") + String.lower());
}

/// Find KV in array using binary search.
static const SubtargetFeatureKV *Find(StringRef S,
                                      ArrayRef<SubtargetFeatureKV> A) {
  // Binary search the array
  auto F = std::lower_bound(A.begin(), A.end(), S);
  // If not found then return NULL
  if (F == A.end() || StringRef(F->Key) != S) return nullptr;
  // Return the found array item
  return F;
}

/// Return the length of the longest entry in the table.
static size_t getLongestEntryLength(ArrayRef<SubtargetFeatureKV> Table) {
  size_t MaxLen = 0;
  for (auto &I : Table)
    MaxLen = std::max(MaxLen, std::strlen(I.Key));
  return MaxLen;
}

/// Display help for feature choices.
static void Help(ArrayRef<SubtargetFeatureKV> CPUTable,
                 ArrayRef<SubtargetFeatureKV> FeatTable) {
  // Determine the length of the longest CPU and Feature entries.
  unsigned MaxCPULen  = getLongestEntryLength(CPUTable);
  unsigned MaxFeatLen = getLongestEntryLength(FeatTable);

  // Print the CPU table.
  errs() << "Available CPUs for this target:\n\n";
  for (auto &CPU : CPUTable)
    errs() << format("  %-*s - %s.\n", MaxCPULen, CPU.Key, CPU.Desc);
  errs() << '\n';

  // Print the Feature table.
  errs() << "Available features for this target:\n\n";
  for (auto &Feature : FeatTable)
    errs() << format("  %-*s - %s.\n", MaxFeatLen, Feature.Key, Feature.Desc);
  errs() << '\n';

  errs() << "Use +feature to enable a feature, or -feature to disable it.\n"
            "For example, llc -mcpu=mycpu -mattr=+feature1,-feature2\n";
}

SubtargetFeatures::SubtargetFeatures(StringRef Initial) {
  // Break up string into separate features
  Split(Features, Initial);
}

std::string SubtargetFeatures::getString() const {
  return join(Features.begin(), Features.end(), ",");
}

/// For each feature that is (transitively) implied by this feature, set it.
static
void SetImpliedBits(FeatureBitset &Bits, const SubtargetFeatureKV &FeatureEntry,
                    ArrayRef<SubtargetFeatureKV> FeatureTable) {
  for (const SubtargetFeatureKV &FE : FeatureTable) {
    if (FeatureEntry.Value == FE.Value) continue;

    if ((FeatureEntry.Implies & FE.Value).any()) {
      Bits |= FE.Value;
      SetImpliedBits(Bits, FE, FeatureTable);
    }
  }
}

/// For each feature that (transitively) implies this feature, clear it.
static
void ClearImpliedBits(FeatureBitset &Bits,
                      const SubtargetFeatureKV &FeatureEntry,
                      ArrayRef<SubtargetFeatureKV> FeatureTable) {
  for (const SubtargetFeatureKV &FE : FeatureTable) {
    if (FeatureEntry.Value == FE.Value) continue;

    if ((FE.Implies & FeatureEntry.Value).any()) {
      Bits &= ~FE.Value;
      ClearImpliedBits(Bits, FE, FeatureTable);
    }
  }
}

void
SubtargetFeatures::ToggleFeature(FeatureBitset &Bits, StringRef Feature,
                                 ArrayRef<SubtargetFeatureKV> FeatureTable) {
  // Find feature in table.
  const SubtargetFeatureKV *FeatureEntry =
      Find(StripFlag(Feature), FeatureTable);
  // If there is a match
  if (FeatureEntry) {
    if ((Bits & FeatureEntry->Value) == FeatureEntry->Value) {
      Bits &= ~FeatureEntry->Value;
      // For each feature that implies this, clear it.
      ClearImpliedBits(Bits, *FeatureEntry, FeatureTable);
    } else {
      Bits |=  FeatureEntry->Value;

      // For each feature that this implies, set it.
      SetImpliedBits(Bits, *FeatureEntry, FeatureTable);
    }
  } else {
    errs() << "'" << Feature << "' is not a recognized feature for this target"
           << " (ignoring feature)\n";
  }
}

void SubtargetFeatures::ApplyFeatureFlag(FeatureBitset &Bits, StringRef Feature,
                                    ArrayRef<SubtargetFeatureKV> FeatureTable) {
  assert(hasFlag(Feature));

  // Find feature in table.
  const SubtargetFeatureKV *FeatureEntry =
      Find(StripFlag(Feature), FeatureTable);
  // If there is a match
  if (FeatureEntry) {
    // Enable/disable feature in bits
    if (isEnabled(Feature)) {
      Bits |= FeatureEntry->Value;

      // For each feature that this implies, set it.
      SetImpliedBits(Bits, *FeatureEntry, FeatureTable);
    } else {
      Bits &= ~FeatureEntry->Value;

      // For each feature that implies this, clear it.
      ClearImpliedBits(Bits, *FeatureEntry, FeatureTable);
    }
  } else {
    errs() << "'" << Feature << "' is not a recognized feature for this target"
           << " (ignoring feature)\n";
  }
}

FeatureBitset
SubtargetFeatures::getFeatureBits(StringRef CPU,
                                  ArrayRef<SubtargetFeatureKV> CPUTable,
                                  ArrayRef<SubtargetFeatureKV> FeatureTable) {
  if (CPUTable.empty() || FeatureTable.empty())
    return FeatureBitset();

  assert(std::is_sorted(std::begin(CPUTable), std::end(CPUTable)) &&
         "CPU table is not sorted");
  assert(std::is_sorted(std::begin(FeatureTable), std::end(FeatureTable)) &&
         "CPU features table is not sorted");
  // Resulting bits
  FeatureBitset Bits;

  // Check if help is needed
  if (CPU == "help")
    Help(CPUTable, FeatureTable);

  // Find CPU entry if CPU name is specified.
  else if (!CPU.empty()) {
    const SubtargetFeatureKV *CPUEntry = Find(CPU, CPUTable);

    // If there is a match
    if (CPUEntry) {
      // Set base feature bits
      Bits = CPUEntry->Value;

      // Set the feature implied by this CPU feature, if any.
      for (auto &FE : FeatureTable) {
        if ((CPUEntry->Value & FE.Value).any())
          SetImpliedBits(Bits, FE, FeatureTable);
      }
    } else {
      errs() << "'" << CPU << "' is not a recognized processor for this target"
             << " (ignoring processor)\n";
    }
  }

  // Iterate through each feature
  for (const std::string &Feature : Features) {
    // Check for help
    if (Feature == "+help")
      Help(CPUTable, FeatureTable);

    ApplyFeatureFlag(Bits, Feature, FeatureTable);
  }

  return Bits;
}

void SubtargetFeatures::print(raw_ostream &OS) const {
  for (auto &F : Features)
    OS << F << " ";
  OS << "\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SubtargetFeatures::dump() const {
  print(dbgs());
}
#endif

void SubtargetFeatures::getDefaultSubtargetFeatures(const Triple& Triple) {
  // FIXME: This is an inelegant way of specifying the features of a
  // subtarget. It would be better if we could encode this information
  // into the IR. See <rdar://5972456>.
  if (Triple.getVendor() == Triple::Apple) {
    if (Triple.getArch() == Triple::ppc) {
      // powerpc-apple-*
      AddFeature("altivec");
    } else if (Triple.getArch() == Triple::ppc64) {
      // powerpc64-apple-*
      AddFeature("64bit");
      AddFeature("altivec");
    }
  }
}
