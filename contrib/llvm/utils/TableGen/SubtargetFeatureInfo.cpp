//===- SubtargetFeatureInfo.cpp - Helpers for subtarget features ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SubtargetFeatureInfo.h"

#include "Types.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/TableGen/Record.h"

#include <map>

using namespace llvm;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SubtargetFeatureInfo::dump() const {
  errs() << getEnumName() << " " << Index << "\n" << *TheDef;
}
#endif

std::vector<std::pair<Record *, SubtargetFeatureInfo>>
SubtargetFeatureInfo::getAll(const RecordKeeper &Records) {
  std::vector<std::pair<Record *, SubtargetFeatureInfo>> SubtargetFeatures;
  std::vector<Record *> AllPredicates =
      Records.getAllDerivedDefinitions("Predicate");
  for (Record *Pred : AllPredicates) {
    // Ignore predicates that are not intended for the assembler.
    //
    // The "AssemblerMatcherPredicate" string should be promoted to an argument
    // if we re-use the machinery for non-assembler purposes in future.
    if (!Pred->getValueAsBit("AssemblerMatcherPredicate"))
      continue;

    if (Pred->getName().empty())
      PrintFatalError(Pred->getLoc(), "Predicate has no name!");

    SubtargetFeatures.emplace_back(
        Pred, SubtargetFeatureInfo(Pred, SubtargetFeatures.size()));
  }
  return SubtargetFeatures;
}

void SubtargetFeatureInfo::emitSubtargetFeatureFlagEnumeration(
    SubtargetFeatureInfoMap &SubtargetFeatures, raw_ostream &OS) {
  OS << "// Flags for subtarget features that participate in "
     << "instruction matching.\n";
  OS << "enum SubtargetFeatureFlag : "
     << getMinimalTypeForEnumBitfield(SubtargetFeatures.size()) << " {\n";
  for (const auto &SF : SubtargetFeatures) {
    const SubtargetFeatureInfo &SFI = SF.second;
    OS << "  " << SFI.getEnumName() << " = (1ULL << " << SFI.Index << "),\n";
  }
  OS << "  Feature_None = 0\n";
  OS << "};\n\n";
}

void SubtargetFeatureInfo::emitSubtargetFeatureBitEnumeration(
    SubtargetFeatureInfoMap &SubtargetFeatures, raw_ostream &OS) {
  OS << "// Bits for subtarget features that participate in "
     << "instruction matching.\n";
  OS << "enum SubtargetFeatureBits : "
     << getMinimalTypeForRange(SubtargetFeatures.size()) << " {\n";
  for (const auto &SF : SubtargetFeatures) {
    const SubtargetFeatureInfo &SFI = SF.second;
    OS << "  " << SFI.getEnumBitName() << " = " << SFI.Index << ",\n";
  }
  OS << "};\n\n";
}

void SubtargetFeatureInfo::emitNameTable(
    SubtargetFeatureInfoMap &SubtargetFeatures, raw_ostream &OS) {
  // Need to sort the name table so that lookup by the log of the enum value
  // gives the proper name. More specifically, for a feature of value 1<<n,
  // SubtargetFeatureNames[n] should be the name of the feature.
  uint64_t IndexUB = 0;
  for (const auto &SF : SubtargetFeatures)
    if (IndexUB <= SF.second.Index)
      IndexUB = SF.second.Index+1;

  std::vector<std::string> Names;
  if (IndexUB > 0)
    Names.resize(IndexUB);
  for (const auto &SF : SubtargetFeatures)
    Names[SF.second.Index] = SF.second.getEnumName();

  OS << "static const char *SubtargetFeatureNames[] = {\n";
  for (uint64_t I = 0; I < IndexUB; ++I)
    OS << "  \"" << Names[I] << "\",\n";

  // A small number of targets have no predicates. Null terminate the array to
  // avoid a zero-length array.
  OS << "  nullptr\n"
     << "};\n\n";
}

void SubtargetFeatureInfo::emitComputeAvailableFeatures(
    StringRef TargetName, StringRef ClassName, StringRef FuncName,
    SubtargetFeatureInfoMap &SubtargetFeatures, raw_ostream &OS,
    StringRef ExtraParams) {
  OS << "PredicateBitset " << TargetName << ClassName << "::\n"
     << FuncName << "(const " << TargetName << "Subtarget *Subtarget";
  if (!ExtraParams.empty())
    OS << ", " << ExtraParams;
  OS << ") const {\n";
  OS << "  PredicateBitset Features;\n";
  for (const auto &SF : SubtargetFeatures) {
    const SubtargetFeatureInfo &SFI = SF.second;

    OS << "  if (" << SFI.TheDef->getValueAsString("CondString") << ")\n";
    OS << "    Features[" << SFI.getEnumBitName() << "] = 1;\n";
  }
  OS << "  return Features;\n";
  OS << "}\n\n";
}

void SubtargetFeatureInfo::emitComputeAssemblerAvailableFeatures(
    StringRef TargetName, StringRef ClassName, StringRef FuncName,
    SubtargetFeatureInfoMap &SubtargetFeatures, raw_ostream &OS) {
  OS << "uint64_t " << TargetName << ClassName << "::\n"
     << FuncName << "(const FeatureBitset& FB) const {\n";
  OS << "  uint64_t Features = 0;\n";
  for (const auto &SF : SubtargetFeatures) {
    const SubtargetFeatureInfo &SFI = SF.second;

    OS << "  if (";
    std::string CondStorage =
        SFI.TheDef->getValueAsString("AssemblerCondString");
    StringRef Conds = CondStorage;
    std::pair<StringRef, StringRef> Comma = Conds.split(',');
    bool First = true;
    do {
      if (!First)
        OS << " && ";

      bool Neg = false;
      StringRef Cond = Comma.first;
      if (Cond[0] == '!') {
        Neg = true;
        Cond = Cond.substr(1);
      }

      OS << "(";
      if (Neg)
        OS << "!";
      OS << "FB[" << TargetName << "::" << Cond << "])";

      if (Comma.second.empty())
        break;

      First = false;
      Comma = Comma.second.split(',');
    } while (true);

    OS << ")\n";
    OS << "    Features |= " << SFI.getEnumName() << ";\n";
  }
  OS << "  return Features;\n";
  OS << "}\n\n";
}
