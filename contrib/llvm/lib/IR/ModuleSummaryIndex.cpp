//===-- ModuleSummaryIndex.cpp - Module Summary Index ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the module index and summary classes for the
// IR library.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "module-summary-index"

STATISTIC(ReadOnlyLiveGVars,
          "Number of live global variables marked read only");

FunctionSummary FunctionSummary::ExternalNode =
    FunctionSummary::makeDummyFunctionSummary({});
bool ValueInfo::isDSOLocal() const {
  // Need to check all summaries are local in case of hash collisions.
  return getSummaryList().size() &&
         llvm::all_of(getSummaryList(),
                      [](const std::unique_ptr<GlobalValueSummary> &Summary) {
                        return Summary->isDSOLocal();
                      });
}

// Gets the number of immutable refs in RefEdgeList
unsigned FunctionSummary::immutableRefCount() const {
  // Here we take advantage of having all readonly references
  // located in the end of the RefEdgeList.
  auto Refs = refs();
  unsigned ImmutableRefCnt = 0;
  for (int I = Refs.size() - 1; I >= 0 && Refs[I].isReadOnly(); --I)
    ImmutableRefCnt++;
  return ImmutableRefCnt;
}

// Collect for the given module the list of function it defines
// (GUID -> Summary).
void ModuleSummaryIndex::collectDefinedFunctionsForModule(
    StringRef ModulePath, GVSummaryMapTy &GVSummaryMap) const {
  for (auto &GlobalList : *this) {
    auto GUID = GlobalList.first;
    for (auto &GlobSummary : GlobalList.second.SummaryList) {
      auto *Summary = dyn_cast_or_null<FunctionSummary>(GlobSummary.get());
      if (!Summary)
        // Ignore global variable, focus on functions
        continue;
      // Ignore summaries from other modules.
      if (Summary->modulePath() != ModulePath)
        continue;
      GVSummaryMap[GUID] = Summary;
    }
  }
}

// Collect for each module the list of function it defines (GUID -> Summary).
void ModuleSummaryIndex::collectDefinedGVSummariesPerModule(
    StringMap<GVSummaryMapTy> &ModuleToDefinedGVSummaries) const {
  for (auto &GlobalList : *this) {
    auto GUID = GlobalList.first;
    for (auto &Summary : GlobalList.second.SummaryList) {
      ModuleToDefinedGVSummaries[Summary->modulePath()][GUID] = Summary.get();
    }
  }
}

GlobalValueSummary *
ModuleSummaryIndex::getGlobalValueSummary(uint64_t ValueGUID,
                                          bool PerModuleIndex) const {
  auto VI = getValueInfo(ValueGUID);
  assert(VI && "GlobalValue not found in index");
  assert((!PerModuleIndex || VI.getSummaryList().size() == 1) &&
         "Expected a single entry per global value in per-module index");
  auto &Summary = VI.getSummaryList()[0];
  return Summary.get();
}

bool ModuleSummaryIndex::isGUIDLive(GlobalValue::GUID GUID) const {
  auto VI = getValueInfo(GUID);
  if (!VI)
    return true;
  const auto &SummaryList = VI.getSummaryList();
  if (SummaryList.empty())
    return true;
  for (auto &I : SummaryList)
    if (isGlobalValueLive(I.get()))
      return true;
  return false;
}

static void propagateConstantsToRefs(GlobalValueSummary *S) {
  // If reference is not readonly then referenced summary is not
  // readonly either. Note that:
  // - All references from GlobalVarSummary are conservatively considered as
  //   not readonly. Tracking them properly requires more complex analysis
  //   then we have now.
  //
  // - AliasSummary objects have no refs at all so this function is a no-op
  //   for them.
  for (auto &VI : S->refs()) {
    if (VI.isReadOnly()) {
      // We only mark refs as readonly when computing function summaries on
      // analysis phase.
      assert(isa<FunctionSummary>(S));
      continue;
    }
    for (auto &Ref : VI.getSummaryList())
      // If references to alias is not readonly then aliasee is not readonly
      if (auto *GVS = dyn_cast<GlobalVarSummary>(Ref->getBaseObject()))
        GVS->setReadOnly(false);
  }
}

// Do the constant propagation in combined index.
// The goal of constant propagation is internalization of readonly
// variables. To determine which variables are readonly and which
// are not we take following steps:
// - During analysis we speculatively assign readonly attribute to
//   all variables which can be internalized. When computing function
//   summary we also assign readonly attribute to a reference if
//   function doesn't modify referenced variable.
//
// - After computing dead symbols in combined index we do the constant
//   propagation. During this step we clear readonly attribute from
//   all variables which:
//   a. are preserved or can't be imported
//   b. referenced by any global variable initializer
//   c. referenced by a function and reference is not readonly
//
// Internalization itself happens in the backend after import is finished
// See internalizeImmutableGVs.
void ModuleSummaryIndex::propagateConstants(
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols) {
  for (auto &P : *this)
    for (auto &S : P.second.SummaryList) {
      if (!isGlobalValueLive(S.get()))
        // We don't examine references from dead objects
        continue;

      // Global variable can't be marked read only if it is not eligible
      // to import since we need to ensure that all external references
      // get a local (imported) copy. It also can't be marked read only
      // if it or any alias (since alias points to the same memory) are
      // preserved or notEligibleToImport, since either of those means
      // there could be writes that are not visible (because preserved
      // means it could have external to DSO writes, and notEligibleToImport
      // means it could have writes via inline assembly leading it to be
      // in the @llvm.*used).
      if (auto *GVS = dyn_cast<GlobalVarSummary>(S->getBaseObject()))
        // Here we intentionally pass S.get() not GVS, because S could be
        // an alias.
        if (!canImportGlobalVar(S.get()) || GUIDPreservedSymbols.count(P.first))
          GVS->setReadOnly(false);
      propagateConstantsToRefs(S.get());
    }
  if (llvm::AreStatisticsEnabled())
    for (auto &P : *this)
      if (P.second.SummaryList.size())
        if (auto *GVS = dyn_cast<GlobalVarSummary>(
                P.second.SummaryList[0]->getBaseObject()))
          if (isGlobalValueLive(GVS) && GVS->isReadOnly())
            ReadOnlyLiveGVars++;
}

// TODO: write a graphviz dumper for SCCs (see ModuleSummaryIndex::exportToDot)
// then delete this function and update its tests
LLVM_DUMP_METHOD
void ModuleSummaryIndex::dumpSCCs(raw_ostream &O) {
  for (scc_iterator<ModuleSummaryIndex *> I =
           scc_begin<ModuleSummaryIndex *>(this);
       !I.isAtEnd(); ++I) {
    O << "SCC (" << utostr(I->size()) << " node" << (I->size() == 1 ? "" : "s")
      << ") {\n";
    for (const ValueInfo V : *I) {
      FunctionSummary *F = nullptr;
      if (V.getSummaryList().size())
        F = cast<FunctionSummary>(V.getSummaryList().front().get());
      O << " " << (F == nullptr ? "External" : "") << " " << utostr(V.getGUID())
        << (I.hasLoop() ? " (has loop)" : "") << "\n";
    }
    O << "}\n";
  }
}

namespace {
struct Attributes {
  void add(const Twine &Name, const Twine &Value,
           const Twine &Comment = Twine());
  void addComment(const Twine &Comment);
  std::string getAsString() const;

  std::vector<std::string> Attrs;
  std::string Comments;
};

struct Edge {
  uint64_t SrcMod;
  int Hotness;
  GlobalValue::GUID Src;
  GlobalValue::GUID Dst;
};
}

void Attributes::add(const Twine &Name, const Twine &Value,
                     const Twine &Comment) {
  std::string A = Name.str();
  A += "=\"";
  A += Value.str();
  A += "\"";
  Attrs.push_back(A);
  addComment(Comment);
}

void Attributes::addComment(const Twine &Comment) {
  if (!Comment.isTriviallyEmpty()) {
    if (Comments.empty())
      Comments = " // ";
    else
      Comments += ", ";
    Comments += Comment.str();
  }
}

std::string Attributes::getAsString() const {
  if (Attrs.empty())
    return "";

  std::string Ret = "[";
  for (auto &A : Attrs)
    Ret += A + ",";
  Ret.pop_back();
  Ret += "];";
  Ret += Comments;
  return Ret;
}

static std::string linkageToString(GlobalValue::LinkageTypes LT) {
  switch (LT) {
  case GlobalValue::ExternalLinkage:
    return "extern";
  case GlobalValue::AvailableExternallyLinkage:
    return "av_ext";
  case GlobalValue::LinkOnceAnyLinkage:
    return "linkonce";
  case GlobalValue::LinkOnceODRLinkage:
    return "linkonce_odr";
  case GlobalValue::WeakAnyLinkage:
    return "weak";
  case GlobalValue::WeakODRLinkage:
    return "weak_odr";
  case GlobalValue::AppendingLinkage:
    return "appending";
  case GlobalValue::InternalLinkage:
    return "internal";
  case GlobalValue::PrivateLinkage:
    return "private";
  case GlobalValue::ExternalWeakLinkage:
    return "extern_weak";
  case GlobalValue::CommonLinkage:
    return "common";
  }

  return "<unknown>";
}

static std::string fflagsToString(FunctionSummary::FFlags F) {
  auto FlagValue = [](unsigned V) { return V ? '1' : '0'; };
  char FlagRep[] = {FlagValue(F.ReadNone),     FlagValue(F.ReadOnly),
                    FlagValue(F.NoRecurse),    FlagValue(F.ReturnDoesNotAlias),
                    FlagValue(F.NoInline), 0};

  return FlagRep;
}

// Get string representation of function instruction count and flags.
static std::string getSummaryAttributes(GlobalValueSummary* GVS) {
  auto *FS = dyn_cast_or_null<FunctionSummary>(GVS);
  if (!FS)
    return "";

  return std::string("inst: ") + std::to_string(FS->instCount()) +
         ", ffl: " + fflagsToString(FS->fflags());
}

static std::string getNodeVisualName(GlobalValue::GUID Id) {
  return std::string("@") + std::to_string(Id);
}

static std::string getNodeVisualName(const ValueInfo &VI) {
  return VI.name().empty() ? getNodeVisualName(VI.getGUID()) : VI.name().str();
}

static std::string getNodeLabel(const ValueInfo &VI, GlobalValueSummary *GVS) {
  if (isa<AliasSummary>(GVS))
    return getNodeVisualName(VI);

  std::string Attrs = getSummaryAttributes(GVS);
  std::string Label =
      getNodeVisualName(VI) + "|" + linkageToString(GVS->linkage());
  if (!Attrs.empty())
    Label += std::string(" (") + Attrs + ")";
  Label += "}";

  return Label;
}

// Write definition of external node, which doesn't have any
// specific module associated with it. Typically this is function
// or variable defined in native object or library.
static void defineExternalNode(raw_ostream &OS, const char *Pfx,
                               const ValueInfo &VI, GlobalValue::GUID Id) {
  auto StrId = std::to_string(Id);
  OS << "  " << StrId << " [label=\"";

  if (VI) {
    OS << getNodeVisualName(VI);
  } else {
    OS << getNodeVisualName(Id);
  }
  OS << "\"]; // defined externally\n";
}

static bool hasReadOnlyFlag(const GlobalValueSummary *S) {
  if (auto *GVS = dyn_cast<GlobalVarSummary>(S))
    return GVS->isReadOnly();
  return false;
}

void ModuleSummaryIndex::exportToDot(raw_ostream &OS) const {
  std::vector<Edge> CrossModuleEdges;
  DenseMap<GlobalValue::GUID, std::vector<uint64_t>> NodeMap;
  StringMap<GVSummaryMapTy> ModuleToDefinedGVS;
  collectDefinedGVSummariesPerModule(ModuleToDefinedGVS);

  // Get node identifier in form MXXX_<GUID>. The MXXX prefix is required,
  // because we may have multiple linkonce functions summaries.
  auto NodeId = [](uint64_t ModId, GlobalValue::GUID Id) {
    return ModId == (uint64_t)-1 ? std::to_string(Id)
                                 : std::string("M") + std::to_string(ModId) +
                                       "_" + std::to_string(Id);
  };

  auto DrawEdge = [&](const char *Pfx, uint64_t SrcMod, GlobalValue::GUID SrcId,
                      uint64_t DstMod, GlobalValue::GUID DstId,
                      int TypeOrHotness) {
    // 0 - alias
    // 1 - reference
    // 2 - constant reference
    // Other value: (hotness - 3).
    TypeOrHotness += 3;
    static const char *EdgeAttrs[] = {
        " [style=dotted]; // alias",
        " [style=dashed]; // ref",
        " [style=dashed,color=forestgreen]; // const-ref",
        " // call (hotness : Unknown)",
        " [color=blue]; // call (hotness : Cold)",
        " // call (hotness : None)",
        " [color=brown]; // call (hotness : Hot)",
        " [style=bold,color=red]; // call (hotness : Critical)"};

    assert(static_cast<size_t>(TypeOrHotness) <
           sizeof(EdgeAttrs) / sizeof(EdgeAttrs[0]));
    OS << Pfx << NodeId(SrcMod, SrcId) << " -> " << NodeId(DstMod, DstId)
       << EdgeAttrs[TypeOrHotness] << "\n";
  };

  OS << "digraph Summary {\n";
  for (auto &ModIt : ModuleToDefinedGVS) {
    auto ModId = getModuleId(ModIt.first());
    OS << "  // Module: " << ModIt.first() << "\n";
    OS << "  subgraph cluster_" << std::to_string(ModId) << " {\n";
    OS << "    style = filled;\n";
    OS << "    color = lightgrey;\n";
    OS << "    label = \"" << sys::path::filename(ModIt.first()) << "\";\n";
    OS << "    node [style=filled,fillcolor=lightblue];\n";

    auto &GVSMap = ModIt.second;
    auto Draw = [&](GlobalValue::GUID IdFrom, GlobalValue::GUID IdTo, int Hotness) {
      if (!GVSMap.count(IdTo)) {
        CrossModuleEdges.push_back({ModId, Hotness, IdFrom, IdTo});
        return;
      }
      DrawEdge("    ", ModId, IdFrom, ModId, IdTo, Hotness);
    };

    for (auto &SummaryIt : GVSMap) {
      NodeMap[SummaryIt.first].push_back(ModId);
      auto Flags = SummaryIt.second->flags();
      Attributes A;
      if (isa<FunctionSummary>(SummaryIt.second)) {
        A.add("shape", "record", "function");
      } else if (isa<AliasSummary>(SummaryIt.second)) {
        A.add("style", "dotted,filled", "alias");
        A.add("shape", "box");
      } else {
        A.add("shape", "Mrecord", "variable");
        if (Flags.Live && hasReadOnlyFlag(SummaryIt.second))
          A.addComment("immutable");
      }

      auto VI = getValueInfo(SummaryIt.first);
      A.add("label", getNodeLabel(VI, SummaryIt.second));
      if (!Flags.Live)
        A.add("fillcolor", "red", "dead");
      else if (Flags.NotEligibleToImport)
        A.add("fillcolor", "yellow", "not eligible to import");

      OS << "    " << NodeId(ModId, SummaryIt.first) << " " << A.getAsString()
         << "\n";
    }
    OS << "    // Edges:\n";

    for (auto &SummaryIt : GVSMap) {
      auto *GVS = SummaryIt.second;
      for (auto &R : GVS->refs())
        Draw(SummaryIt.first, R.getGUID(), R.isReadOnly() ? -1 : -2);

      if (auto *AS = dyn_cast_or_null<AliasSummary>(SummaryIt.second)) {
        GlobalValue::GUID AliaseeId;
        if (AS->hasAliaseeGUID())
          AliaseeId = AS->getAliaseeGUID();
        else {
          auto AliaseeOrigId = AS->getAliasee().getOriginalName();
          AliaseeId = getGUIDFromOriginalID(AliaseeOrigId);
          if (!AliaseeId)
            AliaseeId = AliaseeOrigId;
        }

        Draw(SummaryIt.first, AliaseeId, -3);
        continue;
      }

      if (auto *FS = dyn_cast_or_null<FunctionSummary>(SummaryIt.second))
        for (auto &CGEdge : FS->calls())
          Draw(SummaryIt.first, CGEdge.first.getGUID(),
               static_cast<int>(CGEdge.second.Hotness));
    }
    OS << "  }\n";
  }

  OS << "  // Cross-module edges:\n";
  for (auto &E : CrossModuleEdges) {
    auto &ModList = NodeMap[E.Dst];
    if (ModList.empty()) {
      defineExternalNode(OS, "  ", getValueInfo(E.Dst), E.Dst);
      // Add fake module to the list to draw an edge to an external node
      // in the loop below.
      ModList.push_back(-1);
    }
    for (auto DstMod : ModList)
      // The edge representing call or ref is drawn to every module where target
      // symbol is defined. When target is a linkonce symbol there can be
      // multiple edges representing a single call or ref, both intra-module and
      // cross-module. As we've already drawn all intra-module edges before we
      // skip it here.
      if (DstMod != E.SrcMod)
        DrawEdge("  ", E.SrcMod, E.Src, DstMod, E.Dst, E.Hotness);
  }

  OS << "}";
}
