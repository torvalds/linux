//===----- UninitializedObjectChecker.cpp ------------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a checker that reports uninitialized fields in objects
// created after a constructor call.
//
// To read about command line options and how the checker works, refer to the
// top of the file and inline comments in UninitializedObject.h.
//
// Some of the logic is implemented in UninitializedPointee.cpp, to reduce the
// complexity of this file.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "UninitializedObject.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicTypeMap.h"

using namespace clang;
using namespace clang::ento;

/// We'll mark fields (and pointee of fields) that are confirmed to be
/// uninitialized as already analyzed.
REGISTER_SET_WITH_PROGRAMSTATE(AnalyzedRegions, const MemRegion *)

namespace {

class UninitializedObjectChecker
    : public Checker<check::EndFunction, check::DeadSymbols> {
  std::unique_ptr<BuiltinBug> BT_uninitField;

public:
  // The fields of this struct will be initialized when registering the checker.
  UninitObjCheckerOptions Opts;

  UninitializedObjectChecker()
      : BT_uninitField(new BuiltinBug(this, "Uninitialized fields")) {}

  void checkEndFunction(const ReturnStmt *RS, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const;
};

/// A basic field type, that is not a pointer or a reference, it's dynamic and
/// static type is the same.
class RegularField final : public FieldNode {
public:
  RegularField(const FieldRegion *FR) : FieldNode(FR) {}

  virtual void printNoteMsg(llvm::raw_ostream &Out) const override {
    Out << "uninitialized field ";
  }

  virtual void printPrefix(llvm::raw_ostream &Out) const override {}

  virtual void printNode(llvm::raw_ostream &Out) const override {
    Out << getVariableName(getDecl());
  }

  virtual void printSeparator(llvm::raw_ostream &Out) const override {
    Out << '.';
  }
};

/// Represents that the FieldNode that comes after this is declared in a base
/// of the previous FieldNode. As such, this descendant doesn't wrap a
/// FieldRegion, and is purely a tool to describe a relation between two other
/// FieldRegion wrapping descendants.
class BaseClass final : public FieldNode {
  const QualType BaseClassT;

public:
  BaseClass(const QualType &T) : FieldNode(nullptr), BaseClassT(T) {
    assert(!T.isNull());
    assert(T->getAsCXXRecordDecl());
  }

  virtual void printNoteMsg(llvm::raw_ostream &Out) const override {
    llvm_unreachable("This node can never be the final node in the "
                     "fieldchain!");
  }

  virtual void printPrefix(llvm::raw_ostream &Out) const override {}

  virtual void printNode(llvm::raw_ostream &Out) const override {
    Out << BaseClassT->getAsCXXRecordDecl()->getName() << "::";
  }

  virtual void printSeparator(llvm::raw_ostream &Out) const override {}

  virtual bool isBase() const override { return true; }
};

} // end of anonymous namespace

// Utility function declarations.

/// Returns the region that was constructed by CtorDecl, or nullptr if that
/// isn't possible.
static const TypedValueRegion *
getConstructedRegion(const CXXConstructorDecl *CtorDecl,
                     CheckerContext &Context);

/// Checks whether the object constructed by \p Ctor will be analyzed later
/// (e.g. if the object is a field of another object, in which case we'd check
/// it multiple times).
static bool willObjectBeAnalyzedLater(const CXXConstructorDecl *Ctor,
                                      CheckerContext &Context);

/// Checks whether RD contains a field with a name or type name that matches
/// \p Pattern.
static bool shouldIgnoreRecord(const RecordDecl *RD, StringRef Pattern);

//===----------------------------------------------------------------------===//
//                  Methods for UninitializedObjectChecker.
//===----------------------------------------------------------------------===//

void UninitializedObjectChecker::checkEndFunction(
    const ReturnStmt *RS, CheckerContext &Context) const {

  const auto *CtorDecl = dyn_cast_or_null<CXXConstructorDecl>(
      Context.getLocationContext()->getDecl());
  if (!CtorDecl)
    return;

  if (!CtorDecl->isUserProvided())
    return;

  if (CtorDecl->getParent()->isUnion())
    return;

  // This avoids essentially the same error being reported multiple times.
  if (willObjectBeAnalyzedLater(CtorDecl, Context))
    return;

  const TypedValueRegion *R = getConstructedRegion(CtorDecl, Context);
  if (!R)
    return;

  FindUninitializedFields F(Context.getState(), R, Opts);

  std::pair<ProgramStateRef, const UninitFieldMap &> UninitInfo =
      F.getResults();

  ProgramStateRef UpdatedState = UninitInfo.first;
  const UninitFieldMap &UninitFields = UninitInfo.second;

  if (UninitFields.empty()) {
    Context.addTransition(UpdatedState);
    return;
  }

  // There are uninitialized fields in the record.

  ExplodedNode *Node = Context.generateNonFatalErrorNode(UpdatedState);
  if (!Node)
    return;

  PathDiagnosticLocation LocUsedForUniqueing;
  const Stmt *CallSite = Context.getStackFrame()->getCallSite();
  if (CallSite)
    LocUsedForUniqueing = PathDiagnosticLocation::createBegin(
        CallSite, Context.getSourceManager(), Node->getLocationContext());

  // For Plist consumers that don't support notes just yet, we'll convert notes
  // to warnings.
  if (Opts.ShouldConvertNotesToWarnings) {
    for (const auto &Pair : UninitFields) {

      auto Report = llvm::make_unique<BugReport>(
          *BT_uninitField, Pair.second, Node, LocUsedForUniqueing,
          Node->getLocationContext()->getDecl());
      Context.emitReport(std::move(Report));
    }
    return;
  }

  SmallString<100> WarningBuf;
  llvm::raw_svector_ostream WarningOS(WarningBuf);
  WarningOS << UninitFields.size() << " uninitialized field"
            << (UninitFields.size() == 1 ? "" : "s")
            << " at the end of the constructor call";

  auto Report = llvm::make_unique<BugReport>(
      *BT_uninitField, WarningOS.str(), Node, LocUsedForUniqueing,
      Node->getLocationContext()->getDecl());

  for (const auto &Pair : UninitFields) {
    Report->addNote(Pair.second,
                    PathDiagnosticLocation::create(Pair.first->getDecl(),
                                                   Context.getSourceManager()));
  }
  Context.emitReport(std::move(Report));
}

void UninitializedObjectChecker::checkDeadSymbols(SymbolReaper &SR,
                                                  CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  for (const MemRegion *R : State->get<AnalyzedRegions>()) {
    if (!SR.isLiveRegion(R))
      State = State->remove<AnalyzedRegions>(R);
  }
}

//===----------------------------------------------------------------------===//
//                   Methods for FindUninitializedFields.
//===----------------------------------------------------------------------===//

FindUninitializedFields::FindUninitializedFields(
    ProgramStateRef State, const TypedValueRegion *const R,
    const UninitObjCheckerOptions &Opts)
    : State(State), ObjectR(R), Opts(Opts) {

  isNonUnionUninit(ObjectR, FieldChainInfo(ChainFactory));

  // In non-pedantic mode, if ObjectR doesn't contain a single initialized
  // field, we'll assume that Object was intentionally left uninitialized.
  if (!Opts.IsPedantic && !isAnyFieldInitialized())
    UninitFields.clear();
}

bool FindUninitializedFields::addFieldToUninits(FieldChainInfo Chain,
                                                const MemRegion *PointeeR) {
  const FieldRegion *FR = Chain.getUninitRegion();

  assert((PointeeR || !isDereferencableType(FR->getDecl()->getType())) &&
         "One must also pass the pointee region as a parameter for "
         "dereferenceable fields!");

  if (State->contains<AnalyzedRegions>(FR))
    return false;

  if (PointeeR) {
    if (State->contains<AnalyzedRegions>(PointeeR)) {
      return false;
    }
    State = State->add<AnalyzedRegions>(PointeeR);
  }

  State = State->add<AnalyzedRegions>(FR);

  if (State->getStateManager().getContext().getSourceManager().isInSystemHeader(
          FR->getDecl()->getLocation()))
    return false;

  UninitFieldMap::mapped_type NoteMsgBuf;
  llvm::raw_svector_ostream OS(NoteMsgBuf);
  Chain.printNoteMsg(OS);
  return UninitFields.insert({FR, std::move(NoteMsgBuf)}).second;
}

bool FindUninitializedFields::isNonUnionUninit(const TypedValueRegion *R,
                                               FieldChainInfo LocalChain) {
  assert(R->getValueType()->isRecordType() &&
         !R->getValueType()->isUnionType() &&
         "This method only checks non-union record objects!");

  const RecordDecl *RD = R->getValueType()->getAsRecordDecl()->getDefinition();

  if (!RD) {
    IsAnyFieldInitialized = true;
    return true;
  }

  if (!Opts.IgnoredRecordsWithFieldPattern.empty() &&
      shouldIgnoreRecord(RD, Opts.IgnoredRecordsWithFieldPattern)) {
    IsAnyFieldInitialized = true;
    return false;
  }

  bool ContainsUninitField = false;

  // Are all of this non-union's fields initialized?
  for (const FieldDecl *I : RD->fields()) {

    const auto FieldVal =
        State->getLValue(I, loc::MemRegionVal(R)).castAs<loc::MemRegionVal>();
    const auto *FR = FieldVal.getRegionAs<FieldRegion>();
    QualType T = I->getType();

    // If LocalChain already contains FR, then we encountered a cyclic
    // reference. In this case, region FR is already under checking at an
    // earlier node in the directed tree.
    if (LocalChain.contains(FR))
      return false;

    if (T->isStructureOrClassType()) {
      if (isNonUnionUninit(FR, LocalChain.add(RegularField(FR))))
        ContainsUninitField = true;
      continue;
    }

    if (T->isUnionType()) {
      if (isUnionUninit(FR)) {
        if (addFieldToUninits(LocalChain.add(RegularField(FR))))
          ContainsUninitField = true;
      } else
        IsAnyFieldInitialized = true;
      continue;
    }

    if (T->isArrayType()) {
      IsAnyFieldInitialized = true;
      continue;
    }

    SVal V = State->getSVal(FieldVal);

    if (isDereferencableType(T) || V.getAs<nonloc::LocAsInteger>()) {
      if (isDereferencableUninit(FR, LocalChain))
        ContainsUninitField = true;
      continue;
    }

    if (isPrimitiveType(T)) {
      if (isPrimitiveUninit(V)) {
        if (addFieldToUninits(LocalChain.add(RegularField(FR))))
          ContainsUninitField = true;
      }
      continue;
    }

    llvm_unreachable("All cases are handled!");
  }

  // Checking bases. The checker will regard inherited data members as direct
  // fields.
  const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD);
  if (!CXXRD)
    return ContainsUninitField;

  for (const CXXBaseSpecifier &BaseSpec : CXXRD->bases()) {
    const auto *BaseRegion = State->getLValue(BaseSpec, R)
                                 .castAs<loc::MemRegionVal>()
                                 .getRegionAs<TypedValueRegion>();

    // If the head of the list is also a BaseClass, we'll overwrite it to avoid
    // note messages like 'this->A::B::x'.
    if (!LocalChain.isEmpty() && LocalChain.getHead().isBase()) {
      if (isNonUnionUninit(BaseRegion, LocalChain.replaceHead(
                                           BaseClass(BaseSpec.getType()))))
        ContainsUninitField = true;
    } else {
      if (isNonUnionUninit(BaseRegion,
                           LocalChain.add(BaseClass(BaseSpec.getType()))))
        ContainsUninitField = true;
    }
  }

  return ContainsUninitField;
}

bool FindUninitializedFields::isUnionUninit(const TypedValueRegion *R) {
  assert(R->getValueType()->isUnionType() &&
         "This method only checks union objects!");
  // TODO: Implement support for union fields.
  return false;
}

bool FindUninitializedFields::isPrimitiveUninit(const SVal &V) {
  if (V.isUndef())
    return true;

  IsAnyFieldInitialized = true;
  return false;
}

//===----------------------------------------------------------------------===//
//                       Methods for FieldChainInfo.
//===----------------------------------------------------------------------===//

bool FieldChainInfo::contains(const FieldRegion *FR) const {
  for (const FieldNode &Node : Chain) {
    if (Node.isSameRegion(FR))
      return true;
  }
  return false;
}

/// Prints every element except the last to `Out`. Since ImmutableLists store
/// elements in reverse order, and have no reverse iterators, we use a
/// recursive function to print the fieldchain correctly. The last element in
/// the chain is to be printed by `FieldChainInfo::print`.
static void printTail(llvm::raw_ostream &Out,
                      const FieldChainInfo::FieldChain L);

// FIXME: This function constructs an incorrect string in the following case:
//
//   struct Base { int x; };
//   struct D1 : Base {}; struct D2 : Base {};
//
//   struct MostDerived : D1, D2 {
//     MostDerived() {}
//   }
//
// A call to MostDerived::MostDerived() will cause two notes that say
// "uninitialized field 'this->x'", but we can't refer to 'x' directly,
// we need an explicit namespace resolution whether the uninit field was
// 'D1::x' or 'D2::x'.
void FieldChainInfo::printNoteMsg(llvm::raw_ostream &Out) const {
  if (Chain.isEmpty())
    return;

  const FieldNode &LastField = getHead();

  LastField.printNoteMsg(Out);
  Out << '\'';

  for (const FieldNode &Node : Chain)
    Node.printPrefix(Out);

  Out << "this->";
  printTail(Out, Chain.getTail());
  LastField.printNode(Out);
  Out << '\'';
}

static void printTail(llvm::raw_ostream &Out,
                      const FieldChainInfo::FieldChain L) {
  if (L.isEmpty())
    return;

  printTail(Out, L.getTail());

  L.getHead().printNode(Out);
  L.getHead().printSeparator(Out);
}

//===----------------------------------------------------------------------===//
//                           Utility functions.
//===----------------------------------------------------------------------===//

static const TypedValueRegion *
getConstructedRegion(const CXXConstructorDecl *CtorDecl,
                     CheckerContext &Context) {

  Loc ThisLoc = Context.getSValBuilder().getCXXThis(CtorDecl,
                                                    Context.getStackFrame());

  SVal ObjectV = Context.getState()->getSVal(ThisLoc);

  auto *R = ObjectV.getAsRegion()->getAs<TypedValueRegion>();
  if (R && !R->getValueType()->getAsCXXRecordDecl())
    return nullptr;

  return R;
}

static bool willObjectBeAnalyzedLater(const CXXConstructorDecl *Ctor,
                                      CheckerContext &Context) {

  const TypedValueRegion *CurrRegion = getConstructedRegion(Ctor, Context);
  if (!CurrRegion)
    return false;

  const LocationContext *LC = Context.getLocationContext();
  while ((LC = LC->getParent())) {

    // If \p Ctor was called by another constructor.
    const auto *OtherCtor = dyn_cast<CXXConstructorDecl>(LC->getDecl());
    if (!OtherCtor)
      continue;

    const TypedValueRegion *OtherRegion =
        getConstructedRegion(OtherCtor, Context);
    if (!OtherRegion)
      continue;

    // If the CurrRegion is a subregion of OtherRegion, it will be analyzed
    // during the analysis of OtherRegion.
    if (CurrRegion->isSubRegionOf(OtherRegion))
      return true;
  }

  return false;
}

static bool shouldIgnoreRecord(const RecordDecl *RD, StringRef Pattern) {
  llvm::Regex R(Pattern);

  for (const FieldDecl *FD : RD->fields()) {
    if (R.match(FD->getType().getAsString()))
      return true;
    if (R.match(FD->getName()))
      return true;
  }

  return false;
}

std::string clang::ento::getVariableName(const FieldDecl *Field) {
  // If Field is a captured lambda variable, Field->getName() will return with
  // an empty string. We can however acquire it's name from the lambda's
  // captures.
  const auto *CXXParent = dyn_cast<CXXRecordDecl>(Field->getParent());

  if (CXXParent && CXXParent->isLambda()) {
    assert(CXXParent->captures_begin());
    auto It = CXXParent->captures_begin() + Field->getFieldIndex();

    if (It->capturesVariable())
      return llvm::Twine("/*captured variable*/" +
                         It->getCapturedVar()->getName())
          .str();

    if (It->capturesThis())
      return "/*'this' capture*/";

    llvm_unreachable("No other capture type is expected!");
  }

  return Field->getName();
}

void ento::registerUninitializedObjectChecker(CheckerManager &Mgr) {
  auto Chk = Mgr.registerChecker<UninitializedObjectChecker>();

  AnalyzerOptions &AnOpts = Mgr.getAnalyzerOptions();
  UninitObjCheckerOptions &ChOpts = Chk->Opts;

  ChOpts.IsPedantic =
      AnOpts.getCheckerBooleanOption("Pedantic", /*DefaultVal*/ false, Chk);
  ChOpts.ShouldConvertNotesToWarnings =
      AnOpts.getCheckerBooleanOption("NotesAsWarnings", /*DefaultVal*/ false, Chk);
  ChOpts.CheckPointeeInitialization = AnOpts.getCheckerBooleanOption(
      "CheckPointeeInitialization", /*DefaultVal*/ false, Chk);
  ChOpts.IgnoredRecordsWithFieldPattern =
      AnOpts.getCheckerStringOption("IgnoreRecordsWithField",
                               /*DefaultVal*/ "", Chk);
}
