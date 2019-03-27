//=- ClangDiagnosticsEmitter.cpp - Generate Clang diagnostics tables -*- C++ -*-
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// These tablegen backends emit Clang diagnostics tables.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Casting.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringToOffsetTable.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
using namespace llvm;

//===----------------------------------------------------------------------===//
// Diagnostic category computation code.
//===----------------------------------------------------------------------===//

namespace {
class DiagGroupParentMap {
  RecordKeeper &Records;
  std::map<const Record*, std::vector<Record*> > Mapping;
public:
  DiagGroupParentMap(RecordKeeper &records) : Records(records) {
    std::vector<Record*> DiagGroups
      = Records.getAllDerivedDefinitions("DiagGroup");
    for (unsigned i = 0, e = DiagGroups.size(); i != e; ++i) {
      std::vector<Record*> SubGroups =
        DiagGroups[i]->getValueAsListOfDefs("SubGroups");
      for (unsigned j = 0, e = SubGroups.size(); j != e; ++j)
        Mapping[SubGroups[j]].push_back(DiagGroups[i]);
    }
  }

  const std::vector<Record*> &getParents(const Record *Group) {
    return Mapping[Group];
  }
};
} // end anonymous namespace.

static std::string
getCategoryFromDiagGroup(const Record *Group,
                         DiagGroupParentMap &DiagGroupParents) {
  // If the DiagGroup has a category, return it.
  std::string CatName = Group->getValueAsString("CategoryName");
  if (!CatName.empty()) return CatName;

  // The diag group may the subgroup of one or more other diagnostic groups,
  // check these for a category as well.
  const std::vector<Record*> &Parents = DiagGroupParents.getParents(Group);
  for (unsigned i = 0, e = Parents.size(); i != e; ++i) {
    CatName = getCategoryFromDiagGroup(Parents[i], DiagGroupParents);
    if (!CatName.empty()) return CatName;
  }
  return "";
}

/// getDiagnosticCategory - Return the category that the specified diagnostic
/// lives in.
static std::string getDiagnosticCategory(const Record *R,
                                         DiagGroupParentMap &DiagGroupParents) {
  // If the diagnostic is in a group, and that group has a category, use it.
  if (DefInit *Group = dyn_cast<DefInit>(R->getValueInit("Group"))) {
    // Check the diagnostic's diag group for a category.
    std::string CatName = getCategoryFromDiagGroup(Group->getDef(),
                                                   DiagGroupParents);
    if (!CatName.empty()) return CatName;
  }

  // If the diagnostic itself has a category, get it.
  return R->getValueAsString("CategoryName");
}

namespace {
  class DiagCategoryIDMap {
    RecordKeeper &Records;
    StringMap<unsigned> CategoryIDs;
    std::vector<std::string> CategoryStrings;
  public:
    DiagCategoryIDMap(RecordKeeper &records) : Records(records) {
      DiagGroupParentMap ParentInfo(Records);

      // The zero'th category is "".
      CategoryStrings.push_back("");
      CategoryIDs[""] = 0;

      std::vector<Record*> Diags =
      Records.getAllDerivedDefinitions("Diagnostic");
      for (unsigned i = 0, e = Diags.size(); i != e; ++i) {
        std::string Category = getDiagnosticCategory(Diags[i], ParentInfo);
        if (Category.empty()) continue;  // Skip diags with no category.

        unsigned &ID = CategoryIDs[Category];
        if (ID != 0) continue;  // Already seen.

        ID = CategoryStrings.size();
        CategoryStrings.push_back(Category);
      }
    }

    unsigned getID(StringRef CategoryString) {
      return CategoryIDs[CategoryString];
    }

    typedef std::vector<std::string>::const_iterator const_iterator;
    const_iterator begin() const { return CategoryStrings.begin(); }
    const_iterator end() const { return CategoryStrings.end(); }
  };

  struct GroupInfo {
    std::vector<const Record*> DiagsInGroup;
    std::vector<std::string> SubGroups;
    unsigned IDNo;

    const Record *ExplicitDef;

    GroupInfo() : ExplicitDef(nullptr) {}
  };
} // end anonymous namespace.

static bool beforeThanCompare(const Record *LHS, const Record *RHS) {
  assert(!LHS->getLoc().empty() && !RHS->getLoc().empty());
  return
    LHS->getLoc().front().getPointer() < RHS->getLoc().front().getPointer();
}

static bool diagGroupBeforeByName(const Record *LHS, const Record *RHS) {
  return LHS->getValueAsString("GroupName") <
         RHS->getValueAsString("GroupName");
}

static bool beforeThanCompareGroups(const GroupInfo *LHS, const GroupInfo *RHS){
  assert(!LHS->DiagsInGroup.empty() && !RHS->DiagsInGroup.empty());
  return beforeThanCompare(LHS->DiagsInGroup.front(),
                           RHS->DiagsInGroup.front());
}

/// Invert the 1-[0/1] mapping of diags to group into a one to many
/// mapping of groups to diags in the group.
static void groupDiagnostics(const std::vector<Record*> &Diags,
                             const std::vector<Record*> &DiagGroups,
                             std::map<std::string, GroupInfo> &DiagsInGroup) {

  for (unsigned i = 0, e = Diags.size(); i != e; ++i) {
    const Record *R = Diags[i];
    DefInit *DI = dyn_cast<DefInit>(R->getValueInit("Group"));
    if (!DI)
      continue;
    assert(R->getValueAsDef("Class")->getName() != "CLASS_NOTE" &&
           "Note can't be in a DiagGroup");
    std::string GroupName = DI->getDef()->getValueAsString("GroupName");
    DiagsInGroup[GroupName].DiagsInGroup.push_back(R);
  }

  typedef SmallPtrSet<GroupInfo *, 16> GroupSetTy;
  GroupSetTy ImplicitGroups;

  // Add all DiagGroup's to the DiagsInGroup list to make sure we pick up empty
  // groups (these are warnings that GCC supports that clang never produces).
  for (unsigned i = 0, e = DiagGroups.size(); i != e; ++i) {
    Record *Group = DiagGroups[i];
    GroupInfo &GI = DiagsInGroup[Group->getValueAsString("GroupName")];
    if (Group->isAnonymous()) {
      if (GI.DiagsInGroup.size() > 1)
        ImplicitGroups.insert(&GI);
    } else {
      if (GI.ExplicitDef)
        assert(GI.ExplicitDef == Group);
      else
        GI.ExplicitDef = Group;
    }

    std::vector<Record*> SubGroups = Group->getValueAsListOfDefs("SubGroups");
    for (unsigned j = 0, e = SubGroups.size(); j != e; ++j)
      GI.SubGroups.push_back(SubGroups[j]->getValueAsString("GroupName"));
  }

  // Assign unique ID numbers to the groups.
  unsigned IDNo = 0;
  for (std::map<std::string, GroupInfo>::iterator
       I = DiagsInGroup.begin(), E = DiagsInGroup.end(); I != E; ++I, ++IDNo)
    I->second.IDNo = IDNo;

  // Sort the implicit groups, so we can warn about them deterministically.
  SmallVector<GroupInfo *, 16> SortedGroups(ImplicitGroups.begin(),
                                            ImplicitGroups.end());
  for (SmallVectorImpl<GroupInfo *>::iterator I = SortedGroups.begin(),
                                              E = SortedGroups.end();
       I != E; ++I) {
    MutableArrayRef<const Record *> GroupDiags = (*I)->DiagsInGroup;
    llvm::sort(GroupDiags, beforeThanCompare);
  }
  llvm::sort(SortedGroups, beforeThanCompareGroups);

  // Warn about the same group being used anonymously in multiple places.
  for (SmallVectorImpl<GroupInfo *>::const_iterator I = SortedGroups.begin(),
                                                    E = SortedGroups.end();
       I != E; ++I) {
    ArrayRef<const Record *> GroupDiags = (*I)->DiagsInGroup;

    if ((*I)->ExplicitDef) {
      std::string Name = (*I)->ExplicitDef->getValueAsString("GroupName");
      for (ArrayRef<const Record *>::const_iterator DI = GroupDiags.begin(),
                                                    DE = GroupDiags.end();
           DI != DE; ++DI) {
        const DefInit *GroupInit = cast<DefInit>((*DI)->getValueInit("Group"));
        const Record *NextDiagGroup = GroupInit->getDef();
        if (NextDiagGroup == (*I)->ExplicitDef)
          continue;

        SrcMgr.PrintMessage((*DI)->getLoc().front(),
                            SourceMgr::DK_Error,
                            Twine("group '") + Name +
                              "' is referred to anonymously");
        SrcMgr.PrintMessage((*I)->ExplicitDef->getLoc().front(),
                            SourceMgr::DK_Note, "group defined here");
      }
    } else {
      // If there's no existing named group, we should just warn once and use
      // notes to list all the other cases.
      ArrayRef<const Record *>::const_iterator DI = GroupDiags.begin(),
                                               DE = GroupDiags.end();
      assert(DI != DE && "We only care about groups with multiple uses!");

      const DefInit *GroupInit = cast<DefInit>((*DI)->getValueInit("Group"));
      const Record *NextDiagGroup = GroupInit->getDef();
      std::string Name = NextDiagGroup->getValueAsString("GroupName");

      SrcMgr.PrintMessage((*DI)->getLoc().front(),
                          SourceMgr::DK_Error,
                          Twine("group '") + Name +
                            "' is referred to anonymously");

      for (++DI; DI != DE; ++DI) {
        SrcMgr.PrintMessage((*DI)->getLoc().front(),
                            SourceMgr::DK_Note, "also referenced here");
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Infer members of -Wpedantic.
//===----------------------------------------------------------------------===//

typedef std::vector<const Record *> RecordVec;
typedef llvm::DenseSet<const Record *> RecordSet;
typedef llvm::PointerUnion<RecordVec*, RecordSet*> VecOrSet;

namespace {
class InferPedantic {
  typedef llvm::DenseMap<const Record*,
                         std::pair<unsigned, Optional<unsigned> > > GMap;

  DiagGroupParentMap &DiagGroupParents;
  const std::vector<Record*> &Diags;
  const std::vector<Record*> DiagGroups;
  std::map<std::string, GroupInfo> &DiagsInGroup;
  llvm::DenseSet<const Record*> DiagsSet;
  GMap GroupCount;
public:
  InferPedantic(DiagGroupParentMap &DiagGroupParents,
                const std::vector<Record*> &Diags,
                const std::vector<Record*> &DiagGroups,
                std::map<std::string, GroupInfo> &DiagsInGroup)
  : DiagGroupParents(DiagGroupParents),
  Diags(Diags),
  DiagGroups(DiagGroups),
  DiagsInGroup(DiagsInGroup) {}

  /// Compute the set of diagnostics and groups that are immediately
  /// in -Wpedantic.
  void compute(VecOrSet DiagsInPedantic,
               VecOrSet GroupsInPedantic);

private:
  /// Determine whether a group is a subgroup of another group.
  bool isSubGroupOfGroup(const Record *Group,
                         llvm::StringRef RootGroupName);

  /// Determine if the diagnostic is an extension.
  bool isExtension(const Record *Diag);

  /// Determine if the diagnostic is off by default.
  bool isOffByDefault(const Record *Diag);

  /// Increment the count for a group, and transitively marked
  /// parent groups when appropriate.
  void markGroup(const Record *Group);

  /// Return true if the diagnostic is in a pedantic group.
  bool groupInPedantic(const Record *Group, bool increment = false);
};
} // end anonymous namespace

bool InferPedantic::isSubGroupOfGroup(const Record *Group,
                                      llvm::StringRef GName) {

  const std::string &GroupName = Group->getValueAsString("GroupName");
  if (GName == GroupName)
    return true;

  const std::vector<Record*> &Parents = DiagGroupParents.getParents(Group);
  for (unsigned i = 0, e = Parents.size(); i != e; ++i)
    if (isSubGroupOfGroup(Parents[i], GName))
      return true;

  return false;
}

/// Determine if the diagnostic is an extension.
bool InferPedantic::isExtension(const Record *Diag) {
  const std::string &ClsName = Diag->getValueAsDef("Class")->getName();
  return ClsName == "CLASS_EXTENSION";
}

bool InferPedantic::isOffByDefault(const Record *Diag) {
  const std::string &DefSeverity =
      Diag->getValueAsDef("DefaultSeverity")->getValueAsString("Name");
  return DefSeverity == "Ignored";
}

bool InferPedantic::groupInPedantic(const Record *Group, bool increment) {
  GMap::mapped_type &V = GroupCount[Group];
  // Lazily compute the threshold value for the group count.
  if (!V.second.hasValue()) {
    const GroupInfo &GI = DiagsInGroup[Group->getValueAsString("GroupName")];
    V.second = GI.SubGroups.size() + GI.DiagsInGroup.size();
  }

  if (increment)
    ++V.first;

  // Consider a group in -Wpendatic IFF if has at least one diagnostic
  // or subgroup AND all of those diagnostics and subgroups are covered
  // by -Wpedantic via our computation.
  return V.first != 0 && V.first == V.second.getValue();
}

void InferPedantic::markGroup(const Record *Group) {
  // If all the diagnostics and subgroups have been marked as being
  // covered by -Wpedantic, increment the count of parent groups.  Once the
  // group's count is equal to the number of subgroups and diagnostics in
  // that group, we can safely add this group to -Wpedantic.
  if (groupInPedantic(Group, /* increment */ true)) {
    const std::vector<Record*> &Parents = DiagGroupParents.getParents(Group);
    for (unsigned i = 0, e = Parents.size(); i != e; ++i)
      markGroup(Parents[i]);
  }
}

void InferPedantic::compute(VecOrSet DiagsInPedantic,
                            VecOrSet GroupsInPedantic) {
  // All extensions that are not on by default are implicitly in the
  // "pedantic" group.  For those that aren't explicitly included in -Wpedantic,
  // mark them for consideration to be included in -Wpedantic directly.
  for (unsigned i = 0, e = Diags.size(); i != e; ++i) {
    Record *R = Diags[i];
    if (isExtension(R) && isOffByDefault(R)) {
      DiagsSet.insert(R);
      if (DefInit *Group = dyn_cast<DefInit>(R->getValueInit("Group"))) {
        const Record *GroupRec = Group->getDef();
        if (!isSubGroupOfGroup(GroupRec, "pedantic")) {
          markGroup(GroupRec);
        }
      }
    }
  }

  // Compute the set of diagnostics that are directly in -Wpedantic.  We
  // march through Diags a second time to ensure the results are emitted
  // in deterministic order.
  for (unsigned i = 0, e = Diags.size(); i != e; ++i) {
    Record *R = Diags[i];
    if (!DiagsSet.count(R))
      continue;
    // Check if the group is implicitly in -Wpedantic.  If so,
    // the diagnostic should not be directly included in the -Wpedantic
    // diagnostic group.
    if (DefInit *Group = dyn_cast<DefInit>(R->getValueInit("Group")))
      if (groupInPedantic(Group->getDef()))
        continue;

    // The diagnostic is not included in a group that is (transitively) in
    // -Wpedantic.  Include it in -Wpedantic directly.
    if (RecordVec *V = DiagsInPedantic.dyn_cast<RecordVec*>())
      V->push_back(R);
    else {
      DiagsInPedantic.get<RecordSet*>()->insert(R);
    }
  }

  if (!GroupsInPedantic)
    return;

  // Compute the set of groups that are directly in -Wpedantic.  We
  // march through the groups to ensure the results are emitted
  /// in a deterministc order.
  for (unsigned i = 0, ei = DiagGroups.size(); i != ei; ++i) {
    Record *Group = DiagGroups[i];
    if (!groupInPedantic(Group))
      continue;

    unsigned ParentsInPedantic = 0;
    const std::vector<Record*> &Parents = DiagGroupParents.getParents(Group);
    for (unsigned j = 0, ej = Parents.size(); j != ej; ++j) {
      if (groupInPedantic(Parents[j]))
        ++ParentsInPedantic;
    }
    // If all the parents are in -Wpedantic, this means that this diagnostic
    // group will be indirectly included by -Wpedantic already.  In that
    // case, do not add it directly to -Wpedantic.  If the group has no
    // parents, obviously it should go into -Wpedantic.
    if (Parents.size() > 0 && ParentsInPedantic == Parents.size())
      continue;

    if (RecordVec *V = GroupsInPedantic.dyn_cast<RecordVec*>())
      V->push_back(Group);
    else {
      GroupsInPedantic.get<RecordSet*>()->insert(Group);
    }
  }
}

namespace {
enum PieceKind {
  MultiPieceClass,
  TextPieceClass,
  PlaceholderPieceClass,
  SelectPieceClass,
  PluralPieceClass,
  DiffPieceClass,
  SubstitutionPieceClass,
};

enum ModifierType {
  MT_Unknown,
  MT_Placeholder,
  MT_Select,
  MT_Sub,
  MT_Plural,
  MT_Diff,
  MT_Ordinal,
  MT_S,
  MT_Q,
  MT_ObjCClass,
  MT_ObjCInstance,
};

static StringRef getModifierName(ModifierType MT) {
  switch (MT) {
  case MT_Select:
    return "select";
  case MT_Sub:
    return "sub";
  case MT_Diff:
    return "diff";
  case MT_Plural:
    return "plural";
  case MT_Ordinal:
    return "ordinal";
  case MT_S:
    return "s";
  case MT_Q:
    return "q";
  case MT_Placeholder:
    return "";
  case MT_ObjCClass:
    return "objcclass";
  case MT_ObjCInstance:
    return "objcinstance";
  case MT_Unknown:
    llvm_unreachable("invalid modifier type");
  }
  // Unhandled case
  llvm_unreachable("invalid modifier type");
}

struct Piece {
  // This type and its derived classes are move-only.
  Piece(PieceKind Kind) : ClassKind(Kind) {}
  Piece(Piece const &O) = delete;
  Piece &operator=(Piece const &) = delete;
  virtual ~Piece() {}

  PieceKind getPieceClass() const { return ClassKind; }
  static bool classof(const Piece *) { return true; }

private:
  PieceKind ClassKind;
};

struct MultiPiece : Piece {
  MultiPiece() : Piece(MultiPieceClass) {}
  MultiPiece(std::vector<Piece *> Pieces)
      : Piece(MultiPieceClass), Pieces(std::move(Pieces)) {}

  std::vector<Piece *> Pieces;

  static bool classof(const Piece *P) {
    return P->getPieceClass() == MultiPieceClass;
  }
};

struct TextPiece : Piece {
  StringRef Role;
  std::string Text;
  TextPiece(StringRef Text, StringRef Role = "")
      : Piece(TextPieceClass), Role(Role), Text(Text.str()) {}

  static bool classof(const Piece *P) {
    return P->getPieceClass() == TextPieceClass;
  }
};

struct PlaceholderPiece : Piece {
  ModifierType Kind;
  int Index;
  PlaceholderPiece(ModifierType Kind, int Index)
      : Piece(PlaceholderPieceClass), Kind(Kind), Index(Index) {}

  static bool classof(const Piece *P) {
    return P->getPieceClass() == PlaceholderPieceClass;
  }
};

struct SelectPiece : Piece {
protected:
  SelectPiece(PieceKind Kind, ModifierType ModKind)
      : Piece(Kind), ModKind(ModKind) {}

public:
  SelectPiece(ModifierType ModKind) : SelectPiece(SelectPieceClass, ModKind) {}

  ModifierType ModKind;
  std::vector<Piece *> Options;
  int Index;

  static bool classof(const Piece *P) {
    return P->getPieceClass() == SelectPieceClass ||
           P->getPieceClass() == PluralPieceClass;
  }
};

struct PluralPiece : SelectPiece {
  PluralPiece() : SelectPiece(PluralPieceClass, MT_Plural) {}

  std::vector<Piece *> OptionPrefixes;
  int Index;

  static bool classof(const Piece *P) {
    return P->getPieceClass() == PluralPieceClass;
  }
};

struct DiffPiece : Piece {
  DiffPiece() : Piece(DiffPieceClass) {}

  Piece *Options[2] = {};
  int Indexes[2] = {};

  static bool classof(const Piece *P) {
    return P->getPieceClass() == DiffPieceClass;
  }
};

struct SubstitutionPiece : Piece {
  SubstitutionPiece() : Piece(SubstitutionPieceClass) {}

  std::string Name;
  std::vector<int> Modifiers;

  static bool classof(const Piece *P) {
    return P->getPieceClass() == SubstitutionPieceClass;
  }
};

/// Diagnostic text, parsed into pieces.


struct DiagnosticTextBuilder {
  DiagnosticTextBuilder(DiagnosticTextBuilder const &) = delete;
  DiagnosticTextBuilder &operator=(DiagnosticTextBuilder const &) = delete;

  DiagnosticTextBuilder(RecordKeeper &Records) {
    // Build up the list of substitution records.
    for (auto *S : Records.getAllDerivedDefinitions("TextSubstitution")) {
      EvaluatingRecordGuard Guard(&EvaluatingRecord, S);
      Substitutions.try_emplace(
          S->getName(), DiagText(*this, S->getValueAsString("Substitution")));
    }

    // Check that no diagnostic definitions have the same name as a
    // substitution.
    for (Record *Diag : Records.getAllDerivedDefinitions("Diagnostic")) {
      StringRef Name = Diag->getName();
      if (Substitutions.count(Name))
        llvm::PrintFatalError(
            Diag->getLoc(),
            "Diagnostic '" + Name +
                "' has same name as TextSubstitution definition");
    }
  }

  std::vector<std::string> buildForDocumentation(StringRef Role,
                                                 const Record *R);
  std::string buildForDefinition(const Record *R);

  Piece *getSubstitution(SubstitutionPiece *S) const {
    auto It = Substitutions.find(S->Name);
    if (It == Substitutions.end())
      PrintFatalError("Failed to find substitution with name: " + S->Name);
    return It->second.Root;
  }

  LLVM_ATTRIBUTE_NORETURN void PrintFatalError(llvm::Twine const &Msg) const {
    assert(EvaluatingRecord && "not evaluating a record?");
    llvm::PrintFatalError(EvaluatingRecord->getLoc(), Msg);
  }

private:
  struct DiagText {
    DiagnosticTextBuilder &Builder;
    std::vector<Piece *> AllocatedPieces;
    Piece *Root = nullptr;

    template <class T, class... Args> T *New(Args &&... args) {
      static_assert(std::is_base_of<Piece, T>::value, "must be piece");
      T *Mem = new T(std::forward<Args>(args)...);
      AllocatedPieces.push_back(Mem);
      return Mem;
    }

    DiagText(DiagnosticTextBuilder &Builder, StringRef Text)
        : Builder(Builder), Root(parseDiagText(Text)) {}

    Piece *parseDiagText(StringRef &Text, bool Nested = false);
    int parseModifier(StringRef &) const;

  public:
    DiagText(DiagText &&O) noexcept
        : Builder(O.Builder), AllocatedPieces(std::move(O.AllocatedPieces)),
          Root(O.Root) {
      O.Root = nullptr;
    }

    ~DiagText() {
      for (Piece *P : AllocatedPieces)
        delete P;
    }
  };

private:
  const Record *EvaluatingRecord = nullptr;
  struct EvaluatingRecordGuard {
    EvaluatingRecordGuard(const Record **Dest, const Record *New)
        : Dest(Dest), Old(*Dest) {
      *Dest = New;
    }
    ~EvaluatingRecordGuard() { *Dest = Old; }
    const Record **Dest;
    const Record *Old;
  };

  StringMap<DiagText> Substitutions;
};

template <class Derived> struct DiagTextVisitor {
  using ModifierMappingsType = Optional<std::vector<int>>;

private:
  Derived &getDerived() { return static_cast<Derived &>(*this); }

public:
  std::vector<int>
  getSubstitutionMappings(SubstitutionPiece *P,
                          const ModifierMappingsType &Mappings) const {
    std::vector<int> NewMappings;
    for (int Idx : P->Modifiers)
      NewMappings.push_back(mapIndex(Idx, Mappings));
    return NewMappings;
  }

  struct SubstitutionContext {
    SubstitutionContext(DiagTextVisitor &Visitor, SubstitutionPiece *P)
        : Visitor(Visitor) {
      Substitution = Visitor.Builder.getSubstitution(P);
      OldMappings = std::move(Visitor.ModifierMappings);
      std::vector<int> NewMappings =
          Visitor.getSubstitutionMappings(P, OldMappings);
      Visitor.ModifierMappings = std::move(NewMappings);
    }

    ~SubstitutionContext() {
      Visitor.ModifierMappings = std::move(OldMappings);
    }

  private:
    DiagTextVisitor &Visitor;
    Optional<std::vector<int>> OldMappings;

  public:
    Piece *Substitution;
  };

public:
  DiagTextVisitor(DiagnosticTextBuilder &Builder) : Builder(Builder) {}

  void Visit(Piece *P) {
    switch (P->getPieceClass()) {
#define CASE(T)                                                                \
  case T##PieceClass:                                                          \
    return getDerived().Visit##T(static_cast<T##Piece *>(P))
      CASE(Multi);
      CASE(Text);
      CASE(Placeholder);
      CASE(Select);
      CASE(Plural);
      CASE(Diff);
      CASE(Substitution);
#undef CASE
    }
  }

  void VisitSubstitution(SubstitutionPiece *P) {
    SubstitutionContext Guard(*this, P);
    Visit(Guard.Substitution);
  }

  int mapIndex(int Idx,
                    ModifierMappingsType const &ModifierMappings) const {
    if (!ModifierMappings)
      return Idx;
    if (ModifierMappings->size() <= static_cast<unsigned>(Idx))
      Builder.PrintFatalError("Modifier value '" + std::to_string(Idx) +
                              "' is not valid for this mapping (has " +
                              std::to_string(ModifierMappings->size()) +
                              " mappings)");
    return (*ModifierMappings)[Idx];
  }

  int mapIndex(int Idx) const {
    return mapIndex(Idx, ModifierMappings);
  }

protected:
  DiagnosticTextBuilder &Builder;
  ModifierMappingsType ModifierMappings;
};

void escapeRST(StringRef Str, std::string &Out) {
  for (auto K : Str) {
    if (StringRef("`*|_[]\\").count(K))
      Out.push_back('\\');
    Out.push_back(K);
  }
}

template <typename It> void padToSameLength(It Begin, It End) {
  size_t Width = 0;
  for (It I = Begin; I != End; ++I)
    Width = std::max(Width, I->size());
  for (It I = Begin; I != End; ++I)
    (*I) += std::string(Width - I->size(), ' ');
}

template <typename It> void makeTableRows(It Begin, It End) {
  if (Begin == End)
    return;
  padToSameLength(Begin, End);
  for (It I = Begin; I != End; ++I)
    *I = "|" + *I + "|";
}

void makeRowSeparator(std::string &Str) {
  for (char &K : Str)
    K = (K == '|' ? '+' : '-');
}

struct DiagTextDocPrinter : DiagTextVisitor<DiagTextDocPrinter> {
  using BaseTy = DiagTextVisitor<DiagTextDocPrinter>;
  DiagTextDocPrinter(DiagnosticTextBuilder &Builder,
                     std::vector<std::string> &RST)
      : BaseTy(Builder), RST(RST) {}

  void gatherNodes(
      Piece *OrigP, const ModifierMappingsType &CurrentMappings,
      std::vector<std::pair<Piece *, ModifierMappingsType>> &Pieces) const {
    if (auto *Sub = dyn_cast<SubstitutionPiece>(OrigP)) {
      ModifierMappingsType NewMappings =
          getSubstitutionMappings(Sub, CurrentMappings);
      return gatherNodes(Builder.getSubstitution(Sub), NewMappings, Pieces);
    }
    if (auto *MD = dyn_cast<MultiPiece>(OrigP)) {
      for (Piece *Node : MD->Pieces)
        gatherNodes(Node, CurrentMappings, Pieces);
      return;
    }
    Pieces.push_back(std::make_pair(OrigP, CurrentMappings));
  }

  void VisitMulti(MultiPiece *P) {
    if (P->Pieces.empty()) {
      RST.push_back("");
      return;
    }

    if (P->Pieces.size() == 1)
      return Visit(P->Pieces[0]);

    // Flatten the list of nodes, replacing any substitution pieces with the
    // recursively flattened substituted node.
    std::vector<std::pair<Piece *, ModifierMappingsType>> Pieces;
    gatherNodes(P, ModifierMappings, Pieces);

    std::string EmptyLinePrefix;
    size_t Start = RST.size();
    bool HasMultipleLines = true;
    for (const std::pair<Piece *, ModifierMappingsType> &NodePair : Pieces) {
      std::vector<std::string> Lines;
      DiagTextDocPrinter Visitor{Builder, Lines};
      Visitor.ModifierMappings = NodePair.second;
      Visitor.Visit(NodePair.first);

      if (Lines.empty())
        continue;

      // We need a vertical separator if either this or the previous piece is a
      // multi-line piece, or this is the last piece.
      const char *Separator = (Lines.size() > 1 || HasMultipleLines) ? "|" : "";
      HasMultipleLines = Lines.size() > 1;

      if (Start + Lines.size() > RST.size())
        RST.resize(Start + Lines.size(), EmptyLinePrefix);

      padToSameLength(Lines.begin(), Lines.end());
      for (size_t I = 0; I != Lines.size(); ++I)
        RST[Start + I] += Separator + Lines[I];
      std::string Empty(Lines[0].size(), ' ');
      for (size_t I = Start + Lines.size(); I != RST.size(); ++I)
        RST[I] += Separator + Empty;
      EmptyLinePrefix += Separator + Empty;
    }
    for (size_t I = Start; I != RST.size(); ++I)
      RST[I] += "|";
    EmptyLinePrefix += "|";

    makeRowSeparator(EmptyLinePrefix);
    RST.insert(RST.begin() + Start, EmptyLinePrefix);
    RST.insert(RST.end(), EmptyLinePrefix);
  }

  void VisitText(TextPiece *P) {
    RST.push_back("");
    auto &S = RST.back();

    StringRef T = P->Text;
    while (!T.empty() && T.front() == ' ') {
      RST.back() += " |nbsp| ";
      T = T.drop_front();
    }

    std::string Suffix;
    while (!T.empty() && T.back() == ' ') {
      Suffix += " |nbsp| ";
      T = T.drop_back();
    }

    if (!T.empty()) {
      S += ':';
      S += P->Role;
      S += ":`";
      escapeRST(T, S);
      S += '`';
    }

    S += Suffix;
  }

  void VisitPlaceholder(PlaceholderPiece *P) {
    RST.push_back(std::string(":placeholder:`") +
                  char('A' + mapIndex(P->Index)) + "`");
  }

  void VisitSelect(SelectPiece *P) {
    std::vector<size_t> SeparatorIndexes;
    SeparatorIndexes.push_back(RST.size());
    RST.emplace_back();
    for (auto *O : P->Options) {
      Visit(O);
      SeparatorIndexes.push_back(RST.size());
      RST.emplace_back();
    }

    makeTableRows(RST.begin() + SeparatorIndexes.front(),
                  RST.begin() + SeparatorIndexes.back() + 1);
    for (size_t I : SeparatorIndexes)
      makeRowSeparator(RST[I]);
  }

  void VisitPlural(PluralPiece *P) { VisitSelect(P); }

  void VisitDiff(DiffPiece *P) { Visit(P->Options[1]); }

  std::vector<std::string> &RST;
};

struct DiagTextPrinter : DiagTextVisitor<DiagTextPrinter> {
public:
  using BaseTy = DiagTextVisitor<DiagTextPrinter>;
  DiagTextPrinter(DiagnosticTextBuilder &Builder, std::string &Result)
      : BaseTy(Builder), Result(Result) {}

  void VisitMulti(MultiPiece *P) {
    for (auto *Child : P->Pieces)
      Visit(Child);
  }
  void VisitText(TextPiece *P) { Result += P->Text; }
  void VisitPlaceholder(PlaceholderPiece *P) {
    Result += "%";
    Result += getModifierName(P->Kind);
    addInt(mapIndex(P->Index));
  }
  void VisitSelect(SelectPiece *P) {
    Result += "%";
    Result += getModifierName(P->ModKind);
    if (P->ModKind == MT_Select) {
      Result += "{";
      for (auto *D : P->Options) {
        Visit(D);
        Result += '|';
      }
      if (!P->Options.empty())
        Result.erase(--Result.end());
      Result += '}';
    }
    addInt(mapIndex(P->Index));
  }

  void VisitPlural(PluralPiece *P) {
    Result += "%plural{";
    assert(P->Options.size() == P->OptionPrefixes.size());
    for (unsigned I = 0, End = P->Options.size(); I < End; ++I) {
      if (P->OptionPrefixes[I])
        Visit(P->OptionPrefixes[I]);
      Visit(P->Options[I]);
      Result += "|";
    }
    if (!P->Options.empty())
      Result.erase(--Result.end());
    Result += '}';
    addInt(mapIndex(P->Index));
  }

  void VisitDiff(DiffPiece *P) {
    Result += "%diff{";
    Visit(P->Options[0]);
    Result += "|";
    Visit(P->Options[1]);
    Result += "}";
    addInt(mapIndex(P->Indexes[0]));
    Result += ",";
    addInt(mapIndex(P->Indexes[1]));
  }

  void addInt(int Val) { Result += std::to_string(Val); }

  std::string &Result;
};

int DiagnosticTextBuilder::DiagText::parseModifier(StringRef &Text) const {
  if (Text.empty() || !isdigit(Text[0]))
    Builder.PrintFatalError("expected modifier in diagnostic");
  int Val = 0;
  do {
    Val *= 10;
    Val += Text[0] - '0';
    Text = Text.drop_front();
  } while (!Text.empty() && isdigit(Text[0]));
  return Val;
}

Piece *DiagnosticTextBuilder::DiagText::parseDiagText(StringRef &Text,
                                                      bool Nested) {
  std::vector<Piece *> Parsed;

  while (!Text.empty()) {
    size_t End = (size_t)-2;
    do
      End = Nested ? Text.find_first_of("%|}", End + 2)
                   : Text.find_first_of('%', End + 2);
    while (End < Text.size() - 1 && Text[End] == '%' &&
           (Text[End + 1] == '%' || Text[End + 1] == '|'));

    if (End) {
      Parsed.push_back(New<TextPiece>(Text.slice(0, End), "diagtext"));
      Text = Text.slice(End, StringRef::npos);
      if (Text.empty())
        break;
    }

    if (Text[0] == '|' || Text[0] == '}')
      break;

    // Drop the '%'.
    Text = Text.drop_front();

    // Extract the (optional) modifier.
    size_t ModLength = Text.find_first_of("0123456789{");
    StringRef Modifier = Text.slice(0, ModLength);
    Text = Text.slice(ModLength, StringRef::npos);
    ModifierType ModType = llvm::StringSwitch<ModifierType>{Modifier}
                               .Case("select", MT_Select)
                               .Case("sub", MT_Sub)
                               .Case("diff", MT_Diff)
                               .Case("plural", MT_Plural)
                               .Case("s", MT_S)
                               .Case("ordinal", MT_Ordinal)
                               .Case("q", MT_Q)
                               .Case("objcclass", MT_ObjCClass)
                               .Case("objcinstance", MT_ObjCInstance)
                               .Case("", MT_Placeholder)
                               .Default(MT_Unknown);

    switch (ModType) {
    case MT_Unknown:
      Builder.PrintFatalError("Unknown modifier type: " + Modifier);
    case MT_Select: {
      SelectPiece *Select = New<SelectPiece>(MT_Select);
      do {
        Text = Text.drop_front(); // '{' or '|'
        Select->Options.push_back(parseDiagText(Text, true));
        assert(!Text.empty() && "malformed %select");
      } while (Text.front() == '|');
      // Drop the trailing '}'.
      Text = Text.drop_front(1);
      Select->Index = parseModifier(Text);
      Parsed.push_back(Select);
      continue;
    }
    case MT_Plural: {
      PluralPiece *Plural = New<PluralPiece>();
      do {
        Text = Text.drop_front(); // '{' or '|'
        size_t End = Text.find_first_of(":");
        if (End == StringRef::npos)
          Builder.PrintFatalError("expected ':' while parsing %plural");
        ++End;
        assert(!Text.empty());
        Plural->OptionPrefixes.push_back(
            New<TextPiece>(Text.slice(0, End), "diagtext"));
        Text = Text.slice(End, StringRef::npos);
        Plural->Options.push_back(parseDiagText(Text, true));
        assert(!Text.empty() && "malformed %select");
      } while (Text.front() == '|');
      // Drop the trailing '}'.
      Text = Text.drop_front(1);
      Plural->Index = parseModifier(Text);
      Parsed.push_back(Plural);
      continue;
    }
    case MT_Sub: {
      SubstitutionPiece *Sub = New<SubstitutionPiece>();
      Text = Text.drop_front(); // '{'
      size_t NameSize = Text.find_first_of('}');
      assert(NameSize != size_t(-1) && "failed to find the end of the name");
      assert(NameSize != 0 && "empty name?");
      Sub->Name = Text.substr(0, NameSize).str();
      Text = Text.drop_front(NameSize);
      Text = Text.drop_front(); // '}'
      if (!Text.empty()) {
        while (true) {
          if (!isdigit(Text[0]))
            break;
          Sub->Modifiers.push_back(parseModifier(Text));
          if (Text.empty() || Text[0] != ',')
            break;
          Text = Text.drop_front(); // ','
          assert(!Text.empty() && isdigit(Text[0]) &&
                 "expected another modifier");
        }
      }
      Parsed.push_back(Sub);
      continue;
    }
    case MT_Diff: {
      DiffPiece *Diff = New<DiffPiece>();
      Text = Text.drop_front(); // '{'
      Diff->Options[0] = parseDiagText(Text, true);
      Text = Text.drop_front(); // '|'
      Diff->Options[1] = parseDiagText(Text, true);

      Text = Text.drop_front(); // '}'
      Diff->Indexes[0] = parseModifier(Text);
      Text = Text.drop_front(); // ','
      Diff->Indexes[1] = parseModifier(Text);
      Parsed.push_back(Diff);
      continue;
    }
    case MT_S: {
      SelectPiece *Select = New<SelectPiece>(ModType);
      Select->Options.push_back(New<TextPiece>(""));
      Select->Options.push_back(New<TextPiece>("s", "diagtext"));
      Select->Index = parseModifier(Text);
      Parsed.push_back(Select);
      continue;
    }
    case MT_Q:
    case MT_Placeholder:
    case MT_ObjCClass:
    case MT_ObjCInstance:
    case MT_Ordinal: {
      Parsed.push_back(New<PlaceholderPiece>(ModType, parseModifier(Text)));
      continue;
    }
    }
  }

  return New<MultiPiece>(Parsed);
}

std::vector<std::string>
DiagnosticTextBuilder::buildForDocumentation(StringRef Severity,
                                             const Record *R) {
  EvaluatingRecordGuard Guard(&EvaluatingRecord, R);
  StringRef Text = R->getValueAsString("Text");

  DiagText D(*this, Text);
  TextPiece *Prefix = D.New<TextPiece>(Severity, Severity);
  Prefix->Text += ": ";
  auto *MP = dyn_cast<MultiPiece>(D.Root);
  if (!MP) {
    MP = D.New<MultiPiece>();
    MP->Pieces.push_back(D.Root);
    D.Root = MP;
  }
  MP->Pieces.insert(MP->Pieces.begin(), Prefix);
  std::vector<std::string> Result;
  DiagTextDocPrinter{*this, Result}.Visit(D.Root);
  return Result;
}

std::string DiagnosticTextBuilder::buildForDefinition(const Record *R) {
  EvaluatingRecordGuard Guard(&EvaluatingRecord, R);
  StringRef Text = R->getValueAsString("Text");
  DiagText D(*this, Text);
  std::string Result;
  DiagTextPrinter{*this, Result}.Visit(D.Root);
  return Result;
}

} // namespace

//===----------------------------------------------------------------------===//
// Warning Tables (.inc file) generation.
//===----------------------------------------------------------------------===//

static bool isError(const Record &Diag) {
  const std::string &ClsName = Diag.getValueAsDef("Class")->getName();
  return ClsName == "CLASS_ERROR";
}

static bool isRemark(const Record &Diag) {
  const std::string &ClsName = Diag.getValueAsDef("Class")->getName();
  return ClsName == "CLASS_REMARK";
}


/// ClangDiagsDefsEmitter - The top-level class emits .def files containing
/// declarations of Clang diagnostics.
namespace clang {
void EmitClangDiagsDefs(RecordKeeper &Records, raw_ostream &OS,
                        const std::string &Component) {
  // Write the #if guard
  if (!Component.empty()) {
    std::string ComponentName = StringRef(Component).upper();
    OS << "#ifdef " << ComponentName << "START\n";
    OS << "__" << ComponentName << "START = DIAG_START_" << ComponentName
       << ",\n";
    OS << "#undef " << ComponentName << "START\n";
    OS << "#endif\n\n";
  }

  DiagnosticTextBuilder DiagTextBuilder(Records);

  std::vector<Record *> Diags = Records.getAllDerivedDefinitions("Diagnostic");

  std::vector<Record*> DiagGroups
    = Records.getAllDerivedDefinitions("DiagGroup");

  std::map<std::string, GroupInfo> DiagsInGroup;
  groupDiagnostics(Diags, DiagGroups, DiagsInGroup);

  DiagCategoryIDMap CategoryIDs(Records);
  DiagGroupParentMap DGParentMap(Records);

  // Compute the set of diagnostics that are in -Wpedantic.
  RecordSet DiagsInPedantic;
  InferPedantic inferPedantic(DGParentMap, Diags, DiagGroups, DiagsInGroup);
  inferPedantic.compute(&DiagsInPedantic, (RecordVec*)nullptr);

  for (unsigned i = 0, e = Diags.size(); i != e; ++i) {
    const Record &R = *Diags[i];

    // Check if this is an error that is accidentally in a warning
    // group.
    if (isError(R)) {
      if (DefInit *Group = dyn_cast<DefInit>(R.getValueInit("Group"))) {
        const Record *GroupRec = Group->getDef();
        const std::string &GroupName = GroupRec->getValueAsString("GroupName");
        PrintFatalError(R.getLoc(), "Error " + R.getName() +
                      " cannot be in a warning group [" + GroupName + "]");
      }
    }

    // Check that all remarks have an associated diagnostic group.
    if (isRemark(R)) {
      if (!isa<DefInit>(R.getValueInit("Group"))) {
        PrintFatalError(R.getLoc(), "Error " + R.getName() +
                                        " not in any diagnostic group");
      }
    }

    // Filter by component.
    if (!Component.empty() && Component != R.getValueAsString("Component"))
      continue;

    OS << "DIAG(" << R.getName() << ", ";
    OS << R.getValueAsDef("Class")->getName();
    OS << ", (unsigned)diag::Severity::"
       << R.getValueAsDef("DefaultSeverity")->getValueAsString("Name");

    // Description string.
    OS << ", \"";
    OS.write_escaped(DiagTextBuilder.buildForDefinition(&R)) << '"';

    // Warning associated with the diagnostic. This is stored as an index into
    // the alphabetically sorted warning table.
    if (DefInit *DI = dyn_cast<DefInit>(R.getValueInit("Group"))) {
      std::map<std::string, GroupInfo>::iterator I =
          DiagsInGroup.find(DI->getDef()->getValueAsString("GroupName"));
      assert(I != DiagsInGroup.end());
      OS << ", " << I->second.IDNo;
    } else if (DiagsInPedantic.count(&R)) {
      std::map<std::string, GroupInfo>::iterator I =
        DiagsInGroup.find("pedantic");
      assert(I != DiagsInGroup.end() && "pedantic group not defined");
      OS << ", " << I->second.IDNo;
    } else {
      OS << ", 0";
    }

    // SFINAE response.
    OS << ", " << R.getValueAsDef("SFINAE")->getName();

    // Default warning has no Werror bit.
    if (R.getValueAsBit("WarningNoWerror"))
      OS << ", true";
    else
      OS << ", false";

    if (R.getValueAsBit("ShowInSystemHeader"))
      OS << ", true";
    else
      OS << ", false";

    // Category number.
    OS << ", " << CategoryIDs.getID(getDiagnosticCategory(&R, DGParentMap));
    OS << ")\n";
  }
}
} // end namespace clang

//===----------------------------------------------------------------------===//
// Warning Group Tables generation
//===----------------------------------------------------------------------===//

static std::string getDiagCategoryEnum(llvm::StringRef name) {
  if (name.empty())
    return "DiagCat_None";
  SmallString<256> enumName = llvm::StringRef("DiagCat_");
  for (llvm::StringRef::iterator I = name.begin(), E = name.end(); I != E; ++I)
    enumName += isalnum(*I) ? *I : '_';
  return enumName.str();
}

/// Emit the array of diagnostic subgroups.
///
/// The array of diagnostic subgroups contains for each group a list of its
/// subgroups. The individual lists are separated by '-1'. Groups with no
/// subgroups are skipped.
///
/// \code
///   static const int16_t DiagSubGroups[] = {
///     /* Empty */ -1,
///     /* DiagSubGroup0 */ 142, -1,
///     /* DiagSubGroup13 */ 265, 322, 399, -1
///   }
/// \endcode
///
static void emitDiagSubGroups(std::map<std::string, GroupInfo> &DiagsInGroup,
                              RecordVec &GroupsInPedantic, raw_ostream &OS) {
  OS << "static const int16_t DiagSubGroups[] = {\n"
     << "  /* Empty */ -1,\n";
  for (auto const &I : DiagsInGroup) {
    const bool IsPedantic = I.first == "pedantic";

    const std::vector<std::string> &SubGroups = I.second.SubGroups;
    if (!SubGroups.empty() || (IsPedantic && !GroupsInPedantic.empty())) {
      OS << "  /* DiagSubGroup" << I.second.IDNo << " */ ";
      for (auto const &SubGroup : SubGroups) {
        std::map<std::string, GroupInfo>::const_iterator RI =
            DiagsInGroup.find(SubGroup);
        assert(RI != DiagsInGroup.end() && "Referenced without existing?");
        OS << RI->second.IDNo << ", ";
      }
      // Emit the groups implicitly in "pedantic".
      if (IsPedantic) {
        for (auto const &Group : GroupsInPedantic) {
          const std::string &GroupName = Group->getValueAsString("GroupName");
          std::map<std::string, GroupInfo>::const_iterator RI =
              DiagsInGroup.find(GroupName);
          assert(RI != DiagsInGroup.end() && "Referenced without existing?");
          OS << RI->second.IDNo << ", ";
        }
      }

      OS << "-1,\n";
    }
  }
  OS << "};\n\n";
}

/// Emit the list of diagnostic arrays.
///
/// This data structure is a large array that contains itself arrays of varying
/// size. Each array represents a list of diagnostics. The different arrays are
/// separated by the value '-1'.
///
/// \code
///   static const int16_t DiagArrays[] = {
///     /* Empty */ -1,
///     /* DiagArray1 */ diag::warn_pragma_message,
///                      -1,
///     /* DiagArray2 */ diag::warn_abs_too_small,
///                      diag::warn_unsigned_abs,
///                      diag::warn_wrong_absolute_value_type,
///                      -1
///   };
/// \endcode
///
static void emitDiagArrays(std::map<std::string, GroupInfo> &DiagsInGroup,
                           RecordVec &DiagsInPedantic, raw_ostream &OS) {
  OS << "static const int16_t DiagArrays[] = {\n"
     << "  /* Empty */ -1,\n";
  for (auto const &I : DiagsInGroup) {
    const bool IsPedantic = I.first == "pedantic";

    const std::vector<const Record *> &V = I.second.DiagsInGroup;
    if (!V.empty() || (IsPedantic && !DiagsInPedantic.empty())) {
      OS << "  /* DiagArray" << I.second.IDNo << " */ ";
      for (auto *Record : V)
        OS << "diag::" << Record->getName() << ", ";
      // Emit the diagnostics implicitly in "pedantic".
      if (IsPedantic) {
        for (auto const &Diag : DiagsInPedantic)
          OS << "diag::" << Diag->getName() << ", ";
      }
      OS << "-1,\n";
    }
  }
  OS << "};\n\n";
}

/// Emit a list of group names.
///
/// This creates a long string which by itself contains a list of pascal style
/// strings, which consist of a length byte directly followed by the string.
///
/// \code
///   static const char DiagGroupNames[] = {
///     \000\020#pragma-messages\t#warnings\020CFString-literal"
///   };
/// \endcode
static void emitDiagGroupNames(StringToOffsetTable &GroupNames,
                               raw_ostream &OS) {
  OS << "static const char DiagGroupNames[] = {\n";
  GroupNames.EmitString(OS);
  OS << "};\n\n";
}

/// Emit diagnostic arrays and related data structures.
///
/// This creates the actual diagnostic array, an array of diagnostic subgroups
/// and an array of subgroup names.
///
/// \code
///  #ifdef GET_DIAG_ARRAYS
///     static const int16_t DiagArrays[];
///     static const int16_t DiagSubGroups[];
///     static const char DiagGroupNames[];
///  #endif
///  \endcode
static void emitAllDiagArrays(std::map<std::string, GroupInfo> &DiagsInGroup,
                              RecordVec &DiagsInPedantic,
                              RecordVec &GroupsInPedantic,
                              StringToOffsetTable &GroupNames,
                              raw_ostream &OS) {
  OS << "\n#ifdef GET_DIAG_ARRAYS\n";
  emitDiagArrays(DiagsInGroup, DiagsInPedantic, OS);
  emitDiagSubGroups(DiagsInGroup, GroupsInPedantic, OS);
  emitDiagGroupNames(GroupNames, OS);
  OS << "#endif // GET_DIAG_ARRAYS\n\n";
}

/// Emit diagnostic table.
///
/// The table is sorted by the name of the diagnostic group. Each element
/// consists of the name of the diagnostic group (given as offset in the
/// group name table), a reference to a list of diagnostics (optional) and a
/// reference to a set of subgroups (optional).
///
/// \code
/// #ifdef GET_DIAG_TABLE
///  {/* abi */              159, /* DiagArray11 */ 19, /* Empty */          0},
///  {/* aggregate-return */ 180, /* Empty */        0, /* Empty */          0},
///  {/* all */              197, /* Empty */        0, /* DiagSubGroup13 */ 3},
///  {/* deprecated */       1981,/* DiagArray1 */ 348, /* DiagSubGroup3 */  9},
/// #endif
/// \endcode
static void emitDiagTable(std::map<std::string, GroupInfo> &DiagsInGroup,
                          RecordVec &DiagsInPedantic,
                          RecordVec &GroupsInPedantic,
                          StringToOffsetTable &GroupNames, raw_ostream &OS) {
  unsigned MaxLen = 0;

  for (auto const &I: DiagsInGroup)
    MaxLen = std::max(MaxLen, (unsigned)I.first.size());

  OS << "\n#ifdef GET_DIAG_TABLE\n";
  unsigned SubGroupIndex = 1, DiagArrayIndex = 1;
  for (auto const &I: DiagsInGroup) {
    // Group option string.
    OS << "  { /* ";
    if (I.first.find_first_not_of("abcdefghijklmnopqrstuvwxyz"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "0123456789!@#$%^*-+=:?") !=
        std::string::npos)
      PrintFatalError("Invalid character in diagnostic group '" + I.first +
                      "'");
    OS << I.first << " */ " << std::string(MaxLen - I.first.size(), ' ');
    // Store a pascal-style length byte at the beginning of the string.
    std::string Name = char(I.first.size()) + I.first;
    OS << GroupNames.GetOrAddStringOffset(Name, false) << ", ";

    // Special handling for 'pedantic'.
    const bool IsPedantic = I.first == "pedantic";

    // Diagnostics in the group.
    const std::vector<const Record *> &V = I.second.DiagsInGroup;
    const bool hasDiags =
        !V.empty() || (IsPedantic && !DiagsInPedantic.empty());
    if (hasDiags) {
      OS << "/* DiagArray" << I.second.IDNo << " */ " << DiagArrayIndex
         << ", ";
      if (IsPedantic)
        DiagArrayIndex += DiagsInPedantic.size();
      DiagArrayIndex += V.size() + 1;
    } else {
      OS << "/* Empty */     0, ";
    }

    // Subgroups.
    const std::vector<std::string> &SubGroups = I.second.SubGroups;
    const bool hasSubGroups =
        !SubGroups.empty() || (IsPedantic && !GroupsInPedantic.empty());
    if (hasSubGroups) {
      OS << "/* DiagSubGroup" << I.second.IDNo << " */ " << SubGroupIndex;
      if (IsPedantic)
        SubGroupIndex += GroupsInPedantic.size();
      SubGroupIndex += SubGroups.size() + 1;
    } else {
      OS << "/* Empty */         0";
    }

    OS << " },\n";
  }
  OS << "#endif // GET_DIAG_TABLE\n\n";
}

/// Emit the table of diagnostic categories.
///
/// The table has the form of macro calls that have two parameters. The
/// category's name as well as an enum that represents the category. The
/// table can be used by defining the macro 'CATEGORY' and including this
/// table right after.
///
/// \code
/// #ifdef GET_CATEGORY_TABLE
///   CATEGORY("Semantic Issue", DiagCat_Semantic_Issue)
///   CATEGORY("Lambda Issue", DiagCat_Lambda_Issue)
/// #endif
/// \endcode
static void emitCategoryTable(RecordKeeper &Records, raw_ostream &OS) {
  DiagCategoryIDMap CategoriesByID(Records);
  OS << "\n#ifdef GET_CATEGORY_TABLE\n";
  for (auto const &C : CategoriesByID)
    OS << "CATEGORY(\"" << C << "\", " << getDiagCategoryEnum(C) << ")\n";
  OS << "#endif // GET_CATEGORY_TABLE\n\n";
}

namespace clang {
void EmitClangDiagGroups(RecordKeeper &Records, raw_ostream &OS) {
  // Compute a mapping from a DiagGroup to all of its parents.
  DiagGroupParentMap DGParentMap(Records);

  std::vector<Record *> Diags = Records.getAllDerivedDefinitions("Diagnostic");

  std::vector<Record *> DiagGroups =
      Records.getAllDerivedDefinitions("DiagGroup");

  std::map<std::string, GroupInfo> DiagsInGroup;
  groupDiagnostics(Diags, DiagGroups, DiagsInGroup);

  // All extensions are implicitly in the "pedantic" group.  Record the
  // implicit set of groups in the "pedantic" group, and use this information
  // later when emitting the group information for Pedantic.
  RecordVec DiagsInPedantic;
  RecordVec GroupsInPedantic;
  InferPedantic inferPedantic(DGParentMap, Diags, DiagGroups, DiagsInGroup);
  inferPedantic.compute(&DiagsInPedantic, &GroupsInPedantic);

  StringToOffsetTable GroupNames;
  for (std::map<std::string, GroupInfo>::const_iterator
           I = DiagsInGroup.begin(),
           E = DiagsInGroup.end();
       I != E; ++I) {
    // Store a pascal-style length byte at the beginning of the string.
    std::string Name = char(I->first.size()) + I->first;
    GroupNames.GetOrAddStringOffset(Name, false);
  }

  emitAllDiagArrays(DiagsInGroup, DiagsInPedantic, GroupsInPedantic, GroupNames,
                    OS);
  emitDiagTable(DiagsInGroup, DiagsInPedantic, GroupsInPedantic, GroupNames,
                OS);
  emitCategoryTable(Records, OS);
}
} // end namespace clang

//===----------------------------------------------------------------------===//
// Diagnostic name index generation
//===----------------------------------------------------------------------===//

namespace {
struct RecordIndexElement
{
  RecordIndexElement() {}
  explicit RecordIndexElement(Record const &R):
    Name(R.getName()) {}

  std::string Name;
};
} // end anonymous namespace.

namespace clang {
void EmitClangDiagsIndexName(RecordKeeper &Records, raw_ostream &OS) {
  const std::vector<Record*> &Diags =
    Records.getAllDerivedDefinitions("Diagnostic");

  std::vector<RecordIndexElement> Index;
  Index.reserve(Diags.size());
  for (unsigned i = 0, e = Diags.size(); i != e; ++i) {
    const Record &R = *(Diags[i]);
    Index.push_back(RecordIndexElement(R));
  }

  llvm::sort(Index,
             [](const RecordIndexElement &Lhs, const RecordIndexElement &Rhs) {
               return Lhs.Name < Rhs.Name;
             });

  for (unsigned i = 0, e = Index.size(); i != e; ++i) {
    const RecordIndexElement &R = Index[i];

    OS << "DIAG_NAME_INDEX(" << R.Name << ")\n";
  }
}

//===----------------------------------------------------------------------===//
// Diagnostic documentation generation
//===----------------------------------------------------------------------===//

namespace docs {
namespace {

bool isRemarkGroup(const Record *DiagGroup,
                   const std::map<std::string, GroupInfo> &DiagsInGroup) {
  bool AnyRemarks = false, AnyNonRemarks = false;

  std::function<void(StringRef)> Visit = [&](StringRef GroupName) {
    auto &GroupInfo = DiagsInGroup.find(GroupName)->second;
    for (const Record *Diag : GroupInfo.DiagsInGroup)
      (isRemark(*Diag) ? AnyRemarks : AnyNonRemarks) = true;
    for (const auto &Name : GroupInfo.SubGroups)
      Visit(Name);
  };
  Visit(DiagGroup->getValueAsString("GroupName"));

  if (AnyRemarks && AnyNonRemarks)
    PrintFatalError(
        DiagGroup->getLoc(),
        "Diagnostic group contains both remark and non-remark diagnostics");
  return AnyRemarks;
}

std::string getDefaultSeverity(const Record *Diag) {
  return Diag->getValueAsDef("DefaultSeverity")->getValueAsString("Name");
}

std::set<std::string>
getDefaultSeverities(const Record *DiagGroup,
                     const std::map<std::string, GroupInfo> &DiagsInGroup) {
  std::set<std::string> States;

  std::function<void(StringRef)> Visit = [&](StringRef GroupName) {
    auto &GroupInfo = DiagsInGroup.find(GroupName)->second;
    for (const Record *Diag : GroupInfo.DiagsInGroup)
      States.insert(getDefaultSeverity(Diag));
    for (const auto &Name : GroupInfo.SubGroups)
      Visit(Name);
  };
  Visit(DiagGroup->getValueAsString("GroupName"));
  return States;
}

void writeHeader(StringRef Str, raw_ostream &OS, char Kind = '-') {
  OS << Str << "\n" << std::string(Str.size(), Kind) << "\n";
}

void writeDiagnosticText(DiagnosticTextBuilder &Builder, const Record *R,
                         StringRef Role, raw_ostream &OS) {
  StringRef Text = R->getValueAsString("Text");
  if (Text == "%0")
    OS << "The text of this diagnostic is not controlled by Clang.\n\n";
  else {
    std::vector<std::string> Out = Builder.buildForDocumentation(Role, R);
    for (auto &Line : Out)
      OS << Line << "\n";
    OS << "\n";
  }
}

}  // namespace
}  // namespace docs

void EmitClangDiagDocs(RecordKeeper &Records, raw_ostream &OS) {
  using namespace docs;

  // Get the documentation introduction paragraph.
  const Record *Documentation = Records.getDef("GlobalDocumentation");
  if (!Documentation) {
    PrintFatalError("The Documentation top-level definition is missing, "
                    "no documentation will be generated.");
    return;
  }

  OS << Documentation->getValueAsString("Intro") << "\n";

  DiagnosticTextBuilder Builder(Records);

  std::vector<Record*> Diags =
      Records.getAllDerivedDefinitions("Diagnostic");

  std::vector<Record*> DiagGroups =
      Records.getAllDerivedDefinitions("DiagGroup");
  llvm::sort(DiagGroups, diagGroupBeforeByName);

  DiagGroupParentMap DGParentMap(Records);

  std::map<std::string, GroupInfo> DiagsInGroup;
  groupDiagnostics(Diags, DiagGroups, DiagsInGroup);

  // Compute the set of diagnostics that are in -Wpedantic.
  {
    RecordSet DiagsInPedanticSet;
    RecordSet GroupsInPedanticSet;
    InferPedantic inferPedantic(DGParentMap, Diags, DiagGroups, DiagsInGroup);
    inferPedantic.compute(&DiagsInPedanticSet, &GroupsInPedanticSet);
    auto &PedDiags = DiagsInGroup["pedantic"];
    // Put the diagnostics into a deterministic order.
    RecordVec DiagsInPedantic(DiagsInPedanticSet.begin(),
                              DiagsInPedanticSet.end());
    RecordVec GroupsInPedantic(GroupsInPedanticSet.begin(),
                               GroupsInPedanticSet.end());
    llvm::sort(DiagsInPedantic, beforeThanCompare);
    llvm::sort(GroupsInPedantic, beforeThanCompare);
    PedDiags.DiagsInGroup.insert(PedDiags.DiagsInGroup.end(),
                                 DiagsInPedantic.begin(),
                                 DiagsInPedantic.end());
    for (auto *Group : GroupsInPedantic)
      PedDiags.SubGroups.push_back(Group->getValueAsString("GroupName"));
  }

  // FIXME: Write diagnostic categories and link to diagnostic groups in each.

  // Write out the diagnostic groups.
  for (const Record *G : DiagGroups) {
    bool IsRemarkGroup = isRemarkGroup(G, DiagsInGroup);
    auto &GroupInfo = DiagsInGroup[G->getValueAsString("GroupName")];
    bool IsSynonym = GroupInfo.DiagsInGroup.empty() &&
                     GroupInfo.SubGroups.size() == 1;

    writeHeader(((IsRemarkGroup ? "-R" : "-W") +
                    G->getValueAsString("GroupName")).str(),
                OS);

    if (!IsSynonym) {
      // FIXME: Ideally, all the diagnostics in a group should have the same
      // default state, but that is not currently the case.
      auto DefaultSeverities = getDefaultSeverities(G, DiagsInGroup);
      if (!DefaultSeverities.empty() && !DefaultSeverities.count("Ignored")) {
        bool AnyNonErrors = DefaultSeverities.count("Warning") ||
                            DefaultSeverities.count("Remark");
        if (!AnyNonErrors)
          OS << "This diagnostic is an error by default, but the flag ``-Wno-"
             << G->getValueAsString("GroupName") << "`` can be used to disable "
             << "the error.\n\n";
        else
          OS << "This diagnostic is enabled by default.\n\n";
      } else if (DefaultSeverities.size() > 1) {
        OS << "Some of the diagnostics controlled by this flag are enabled "
           << "by default.\n\n";
      }
    }

    if (!GroupInfo.SubGroups.empty()) {
      if (IsSynonym)
        OS << "Synonym for ";
      else if (GroupInfo.DiagsInGroup.empty())
        OS << "Controls ";
      else
        OS << "Also controls ";

      bool First = true;
      llvm::sort(GroupInfo.SubGroups);
      for (const auto &Name : GroupInfo.SubGroups) {
        if (!First) OS << ", ";
        OS << "`" << (IsRemarkGroup ? "-R" : "-W") << Name << "`_";
        First = false;
      }
      OS << ".\n\n";
    }

    if (!GroupInfo.DiagsInGroup.empty()) {
      OS << "**Diagnostic text:**\n\n";
      for (const Record *D : GroupInfo.DiagsInGroup) {
        auto Severity = getDefaultSeverity(D);
        Severity[0] = tolower(Severity[0]);
        if (Severity == "ignored")
          Severity = IsRemarkGroup ? "remark" : "warning";

        writeDiagnosticText(Builder, D, Severity, OS);
      }
    }

    auto Doc = G->getValueAsString("Documentation");
    if (!Doc.empty())
      OS << Doc;
    else if (GroupInfo.SubGroups.empty() && GroupInfo.DiagsInGroup.empty())
      OS << "This diagnostic flag exists for GCC compatibility, and has no "
            "effect in Clang.\n";
    OS << "\n";
  }
}

} // end namespace clang
