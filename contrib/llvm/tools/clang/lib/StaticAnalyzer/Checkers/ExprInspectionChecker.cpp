//==- ExprInspectionChecker.cpp - Used for regression tests ------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Checkers/SValExplainer.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/IssueHash.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ScopedPrinter.h"

using namespace clang;
using namespace ento;

namespace {
class ExprInspectionChecker : public Checker<eval::Call, check::DeadSymbols,
                                             check::EndAnalysis> {
  mutable std::unique_ptr<BugType> BT;

  // These stats are per-analysis, not per-branch, hence they shouldn't
  // stay inside the program state.
  struct ReachedStat {
    ExplodedNode *ExampleNode;
    unsigned NumTimesReached;
  };
  mutable llvm::DenseMap<const CallExpr *, ReachedStat> ReachedStats;

  void analyzerEval(const CallExpr *CE, CheckerContext &C) const;
  void analyzerCheckInlined(const CallExpr *CE, CheckerContext &C) const;
  void analyzerWarnIfReached(const CallExpr *CE, CheckerContext &C) const;
  void analyzerNumTimesReached(const CallExpr *CE, CheckerContext &C) const;
  void analyzerCrash(const CallExpr *CE, CheckerContext &C) const;
  void analyzerWarnOnDeadSymbol(const CallExpr *CE, CheckerContext &C) const;
  void analyzerDump(const CallExpr *CE, CheckerContext &C) const;
  void analyzerExplain(const CallExpr *CE, CheckerContext &C) const;
  void analyzerPrintState(const CallExpr *CE, CheckerContext &C) const;
  void analyzerGetExtent(const CallExpr *CE, CheckerContext &C) const;
  void analyzerHashDump(const CallExpr *CE, CheckerContext &C) const;
  void analyzerDenote(const CallExpr *CE, CheckerContext &C) const;
  void analyzerExpress(const CallExpr *CE, CheckerContext &C) const;

  typedef void (ExprInspectionChecker::*FnCheck)(const CallExpr *,
                                                 CheckerContext &C) const;

  ExplodedNode *reportBug(llvm::StringRef Msg, CheckerContext &C) const;
  ExplodedNode *reportBug(llvm::StringRef Msg, BugReporter &BR,
                          ExplodedNode *N) const;

public:
  bool evalCall(const CallExpr *CE, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const;
  void checkEndAnalysis(ExplodedGraph &G, BugReporter &BR,
                        ExprEngine &Eng) const;
};
}

REGISTER_SET_WITH_PROGRAMSTATE(MarkedSymbols, SymbolRef)
REGISTER_MAP_WITH_PROGRAMSTATE(DenotedSymbols, SymbolRef, const StringLiteral *)

bool ExprInspectionChecker::evalCall(const CallExpr *CE,
                                     CheckerContext &C) const {
  // These checks should have no effect on the surrounding environment
  // (globals should not be invalidated, etc), hence the use of evalCall.
  FnCheck Handler = llvm::StringSwitch<FnCheck>(C.getCalleeName(CE))
    .Case("clang_analyzer_eval", &ExprInspectionChecker::analyzerEval)
    .Case("clang_analyzer_checkInlined",
          &ExprInspectionChecker::analyzerCheckInlined)
    .Case("clang_analyzer_crash", &ExprInspectionChecker::analyzerCrash)
    .Case("clang_analyzer_warnIfReached",
          &ExprInspectionChecker::analyzerWarnIfReached)
    .Case("clang_analyzer_warnOnDeadSymbol",
          &ExprInspectionChecker::analyzerWarnOnDeadSymbol)
    .StartsWith("clang_analyzer_explain", &ExprInspectionChecker::analyzerExplain)
    .StartsWith("clang_analyzer_dump", &ExprInspectionChecker::analyzerDump)
    .Case("clang_analyzer_getExtent", &ExprInspectionChecker::analyzerGetExtent)
    .Case("clang_analyzer_printState",
          &ExprInspectionChecker::analyzerPrintState)
    .Case("clang_analyzer_numTimesReached",
          &ExprInspectionChecker::analyzerNumTimesReached)
    .Case("clang_analyzer_hashDump", &ExprInspectionChecker::analyzerHashDump)
    .Case("clang_analyzer_denote", &ExprInspectionChecker::analyzerDenote)
    .Case("clang_analyzer_express", &ExprInspectionChecker::analyzerExpress)
    .Default(nullptr);

  if (!Handler)
    return false;

  (this->*Handler)(CE, C);
  return true;
}

static const char *getArgumentValueString(const CallExpr *CE,
                                          CheckerContext &C) {
  if (CE->getNumArgs() == 0)
    return "Missing assertion argument";

  ExplodedNode *N = C.getPredecessor();
  const LocationContext *LC = N->getLocationContext();
  ProgramStateRef State = N->getState();

  const Expr *Assertion = CE->getArg(0);
  SVal AssertionVal = State->getSVal(Assertion, LC);

  if (AssertionVal.isUndef())
    return "UNDEFINED";

  ProgramStateRef StTrue, StFalse;
  std::tie(StTrue, StFalse) =
    State->assume(AssertionVal.castAs<DefinedOrUnknownSVal>());

  if (StTrue) {
    if (StFalse)
      return "UNKNOWN";
    else
      return "TRUE";
  } else {
    if (StFalse)
      return "FALSE";
    else
      llvm_unreachable("Invalid constraint; neither true or false.");
  }
}

ExplodedNode *ExprInspectionChecker::reportBug(llvm::StringRef Msg,
                                               CheckerContext &C) const {
  ExplodedNode *N = C.generateNonFatalErrorNode();
  reportBug(Msg, C.getBugReporter(), N);
  return N;
}

ExplodedNode *ExprInspectionChecker::reportBug(llvm::StringRef Msg,
                                               BugReporter &BR,
                                               ExplodedNode *N) const {
  if (!N)
    return nullptr;

  if (!BT)
    BT.reset(new BugType(this, "Checking analyzer assumptions", "debug"));

  BR.emitReport(llvm::make_unique<BugReport>(*BT, Msg, N));
  return N;
}

void ExprInspectionChecker::analyzerEval(const CallExpr *CE,
                                         CheckerContext &C) const {
  const LocationContext *LC = C.getPredecessor()->getLocationContext();

  // A specific instantiation of an inlined function may have more constrained
  // values than can generally be assumed. Skip the check.
  if (LC->getStackFrame()->getParent() != nullptr)
    return;

  reportBug(getArgumentValueString(CE, C), C);
}

void ExprInspectionChecker::analyzerWarnIfReached(const CallExpr *CE,
                                                  CheckerContext &C) const {
  reportBug("REACHABLE", C);
}

void ExprInspectionChecker::analyzerNumTimesReached(const CallExpr *CE,
                                                    CheckerContext &C) const {
  ++ReachedStats[CE].NumTimesReached;
  if (!ReachedStats[CE].ExampleNode) {
    // Later, in checkEndAnalysis, we'd throw a report against it.
    ReachedStats[CE].ExampleNode = C.generateNonFatalErrorNode();
  }
}

void ExprInspectionChecker::analyzerCheckInlined(const CallExpr *CE,
                                                 CheckerContext &C) const {
  const LocationContext *LC = C.getPredecessor()->getLocationContext();

  // An inlined function could conceivably also be analyzed as a top-level
  // function. We ignore this case and only emit a message (TRUE or FALSE)
  // when we are analyzing it as an inlined function. This means that
  // clang_analyzer_checkInlined(true) should always print TRUE, but
  // clang_analyzer_checkInlined(false) should never actually print anything.
  if (LC->getStackFrame()->getParent() == nullptr)
    return;

  reportBug(getArgumentValueString(CE, C), C);
}

void ExprInspectionChecker::analyzerExplain(const CallExpr *CE,
                                            CheckerContext &C) const {
  if (CE->getNumArgs() == 0) {
    reportBug("Missing argument for explaining", C);
    return;
  }

  SVal V = C.getSVal(CE->getArg(0));
  SValExplainer Ex(C.getASTContext());
  reportBug(Ex.Visit(V), C);
}

void ExprInspectionChecker::analyzerDump(const CallExpr *CE,
                                         CheckerContext &C) const {
  if (CE->getNumArgs() == 0) {
    reportBug("Missing argument for dumping", C);
    return;
  }

  SVal V = C.getSVal(CE->getArg(0));

  llvm::SmallString<32> Str;
  llvm::raw_svector_ostream OS(Str);
  V.dumpToStream(OS);
  reportBug(OS.str(), C);
}

void ExprInspectionChecker::analyzerGetExtent(const CallExpr *CE,
                                              CheckerContext &C) const {
  if (CE->getNumArgs() == 0) {
    reportBug("Missing region for obtaining extent", C);
    return;
  }

  auto MR = dyn_cast_or_null<SubRegion>(C.getSVal(CE->getArg(0)).getAsRegion());
  if (!MR) {
    reportBug("Obtaining extent of a non-region", C);
    return;
  }

  ProgramStateRef State = C.getState();
  State = State->BindExpr(CE, C.getLocationContext(),
                          MR->getExtent(C.getSValBuilder()));
  C.addTransition(State);
}

void ExprInspectionChecker::analyzerPrintState(const CallExpr *CE,
                                               CheckerContext &C) const {
  C.getState()->dump();
}

void ExprInspectionChecker::analyzerWarnOnDeadSymbol(const CallExpr *CE,
                                                     CheckerContext &C) const {
  if (CE->getNumArgs() == 0)
    return;
  SVal Val = C.getSVal(CE->getArg(0));
  SymbolRef Sym = Val.getAsSymbol();
  if (!Sym)
    return;

  ProgramStateRef State = C.getState();
  State = State->add<MarkedSymbols>(Sym);
  C.addTransition(State);
}

void ExprInspectionChecker::checkDeadSymbols(SymbolReaper &SymReaper,
                                             CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  const MarkedSymbolsTy &Syms = State->get<MarkedSymbols>();
  ExplodedNode *N = C.getPredecessor();
  for (auto I = Syms.begin(), E = Syms.end(); I != E; ++I) {
    SymbolRef Sym = *I;
    if (!SymReaper.isDead(Sym))
      continue;

    // The non-fatal error node should be the same for all reports.
    if (ExplodedNode *BugNode = reportBug("SYMBOL DEAD", C))
      N = BugNode;
    State = State->remove<MarkedSymbols>(Sym);
  }

  for (auto I : State->get<DenotedSymbols>()) {
    SymbolRef Sym = I.first;
    if (!SymReaper.isLive(Sym))
      State = State->remove<DenotedSymbols>(Sym);
  }

  C.addTransition(State, N);
}

void ExprInspectionChecker::checkEndAnalysis(ExplodedGraph &G, BugReporter &BR,
                                             ExprEngine &Eng) const {
  for (auto Item: ReachedStats) {
    unsigned NumTimesReached = Item.second.NumTimesReached;
    ExplodedNode *N = Item.second.ExampleNode;

    reportBug(llvm::to_string(NumTimesReached), BR, N);
  }
  ReachedStats.clear();
}

void ExprInspectionChecker::analyzerCrash(const CallExpr *CE,
                                          CheckerContext &C) const {
  LLVM_BUILTIN_TRAP;
}

void ExprInspectionChecker::analyzerHashDump(const CallExpr *CE,
                                             CheckerContext &C) const {
  const LangOptions &Opts = C.getLangOpts();
  const SourceManager &SM = C.getSourceManager();
  FullSourceLoc FL(CE->getArg(0)->getBeginLoc(), SM);
  std::string HashContent =
      GetIssueString(SM, FL, getCheckName().getName(), "Category",
                     C.getLocationContext()->getDecl(), Opts);

  reportBug(HashContent, C);
}

void ExprInspectionChecker::analyzerDenote(const CallExpr *CE,
                                           CheckerContext &C) const {
  if (CE->getNumArgs() < 2) {
    reportBug("clang_analyzer_denote() requires a symbol and a string literal",
              C);
    return;
  }

  SymbolRef Sym = C.getSVal(CE->getArg(0)).getAsSymbol();
  if (!Sym) {
    reportBug("Not a symbol", C);
    return;
  }

  const auto *E = dyn_cast<StringLiteral>(CE->getArg(1)->IgnoreParenCasts());
  if (!E) {
    reportBug("Not a string literal", C);
    return;
  }

  ProgramStateRef State = C.getState();

  C.addTransition(C.getState()->set<DenotedSymbols>(Sym, E));
}

namespace {
class SymbolExpressor
    : public SymExprVisitor<SymbolExpressor, Optional<std::string>> {
  ProgramStateRef State;

public:
  SymbolExpressor(ProgramStateRef State) : State(State) {}

  Optional<std::string> lookup(const SymExpr *S) {
    if (const StringLiteral *const *SLPtr = State->get<DenotedSymbols>(S)) {
      const StringLiteral *SL = *SLPtr;
      return std::string(SL->getBytes());
    }
    return None;
  }

  Optional<std::string> VisitSymExpr(const SymExpr *S) {
    return lookup(S);
  }

  Optional<std::string> VisitSymIntExpr(const SymIntExpr *S) {
    if (Optional<std::string> Str = lookup(S))
      return Str;
    if (Optional<std::string> Str = Visit(S->getLHS()))
      return (*Str + " " + BinaryOperator::getOpcodeStr(S->getOpcode()) + " " +
              std::to_string(S->getRHS().getLimitedValue()) +
              (S->getRHS().isUnsigned() ? "U" : ""))
          .str();
    return None;
  }

  Optional<std::string> VisitSymSymExpr(const SymSymExpr *S) {
    if (Optional<std::string> Str = lookup(S))
      return Str;
    if (Optional<std::string> Str1 = Visit(S->getLHS()))
      if (Optional<std::string> Str2 = Visit(S->getRHS()))
        return (*Str1 + " " + BinaryOperator::getOpcodeStr(S->getOpcode()) +
                " " + *Str2).str();
    return None;
  }

  Optional<std::string> VisitSymbolCast(const SymbolCast *S) {
    if (Optional<std::string> Str = lookup(S))
      return Str;
    if (Optional<std::string> Str = Visit(S->getOperand()))
      return (Twine("(") + S->getType().getAsString() + ")" + *Str).str();
    return None;
  }
};
} // namespace

void ExprInspectionChecker::analyzerExpress(const CallExpr *CE,
                                            CheckerContext &C) const {
  if (CE->getNumArgs() == 0) {
    reportBug("clang_analyzer_express() requires a symbol", C);
    return;
  }

  SymbolRef Sym = C.getSVal(CE->getArg(0)).getAsSymbol();
  if (!Sym) {
    reportBug("Not a symbol", C);
    return;
  }

  SymbolExpressor V(C.getState());
  auto Str = V.Visit(Sym);
  if (!Str) {
    reportBug("Unable to express", C);
    return;
  }

  reportBug(*Str, C);
}

void ento::registerExprInspectionChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<ExprInspectionChecker>();
}
