//===-- StreamChecker.cpp -----------------------------------------*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines checkers that model and check stream handling functions.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"

using namespace clang;
using namespace ento;

namespace {

struct StreamState {
  enum Kind { Opened, Closed, OpenFailed, Escaped } K;
  const Stmt *S;

  StreamState(Kind k, const Stmt *s) : K(k), S(s) {}

  bool isOpened() const { return K == Opened; }
  bool isClosed() const { return K == Closed; }
  //bool isOpenFailed() const { return K == OpenFailed; }
  //bool isEscaped() const { return K == Escaped; }

  bool operator==(const StreamState &X) const {
    return K == X.K && S == X.S;
  }

  static StreamState getOpened(const Stmt *s) { return StreamState(Opened, s); }
  static StreamState getClosed(const Stmt *s) { return StreamState(Closed, s); }
  static StreamState getOpenFailed(const Stmt *s) {
    return StreamState(OpenFailed, s);
  }
  static StreamState getEscaped(const Stmt *s) {
    return StreamState(Escaped, s);
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(K);
    ID.AddPointer(S);
  }
};

class StreamChecker : public Checker<eval::Call,
                                     check::DeadSymbols > {
  mutable IdentifierInfo *II_fopen, *II_tmpfile, *II_fclose, *II_fread,
                 *II_fwrite,
                 *II_fseek, *II_ftell, *II_rewind, *II_fgetpos, *II_fsetpos,
                 *II_clearerr, *II_feof, *II_ferror, *II_fileno;
  mutable std::unique_ptr<BuiltinBug> BT_nullfp, BT_illegalwhence,
      BT_doubleclose, BT_ResourceLeak;

public:
  StreamChecker()
    : II_fopen(nullptr), II_tmpfile(nullptr), II_fclose(nullptr),
      II_fread(nullptr), II_fwrite(nullptr), II_fseek(nullptr),
      II_ftell(nullptr), II_rewind(nullptr), II_fgetpos(nullptr),
      II_fsetpos(nullptr), II_clearerr(nullptr), II_feof(nullptr),
      II_ferror(nullptr), II_fileno(nullptr) {}

  bool evalCall(const CallExpr *CE, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const;

private:
  void Fopen(CheckerContext &C, const CallExpr *CE) const;
  void Tmpfile(CheckerContext &C, const CallExpr *CE) const;
  void Fclose(CheckerContext &C, const CallExpr *CE) const;
  void Fread(CheckerContext &C, const CallExpr *CE) const;
  void Fwrite(CheckerContext &C, const CallExpr *CE) const;
  void Fseek(CheckerContext &C, const CallExpr *CE) const;
  void Ftell(CheckerContext &C, const CallExpr *CE) const;
  void Rewind(CheckerContext &C, const CallExpr *CE) const;
  void Fgetpos(CheckerContext &C, const CallExpr *CE) const;
  void Fsetpos(CheckerContext &C, const CallExpr *CE) const;
  void Clearerr(CheckerContext &C, const CallExpr *CE) const;
  void Feof(CheckerContext &C, const CallExpr *CE) const;
  void Ferror(CheckerContext &C, const CallExpr *CE) const;
  void Fileno(CheckerContext &C, const CallExpr *CE) const;

  void OpenFileAux(CheckerContext &C, const CallExpr *CE) const;

  ProgramStateRef CheckNullStream(SVal SV, ProgramStateRef state,
                                 CheckerContext &C) const;
  ProgramStateRef CheckDoubleClose(const CallExpr *CE, ProgramStateRef state,
                                 CheckerContext &C) const;
};

} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(StreamMap, SymbolRef, StreamState)


bool StreamChecker::evalCall(const CallExpr *CE, CheckerContext &C) const {
  const FunctionDecl *FD = C.getCalleeDecl(CE);
  if (!FD || FD->getKind() != Decl::Function)
    return false;

  ASTContext &Ctx = C.getASTContext();
  if (!II_fopen)
    II_fopen = &Ctx.Idents.get("fopen");
  if (!II_tmpfile)
    II_tmpfile = &Ctx.Idents.get("tmpfile");
  if (!II_fclose)
    II_fclose = &Ctx.Idents.get("fclose");
  if (!II_fread)
    II_fread = &Ctx.Idents.get("fread");
  if (!II_fwrite)
    II_fwrite = &Ctx.Idents.get("fwrite");
  if (!II_fseek)
    II_fseek = &Ctx.Idents.get("fseek");
  if (!II_ftell)
    II_ftell = &Ctx.Idents.get("ftell");
  if (!II_rewind)
    II_rewind = &Ctx.Idents.get("rewind");
  if (!II_fgetpos)
    II_fgetpos = &Ctx.Idents.get("fgetpos");
  if (!II_fsetpos)
    II_fsetpos = &Ctx.Idents.get("fsetpos");
  if (!II_clearerr)
    II_clearerr = &Ctx.Idents.get("clearerr");
  if (!II_feof)
    II_feof = &Ctx.Idents.get("feof");
  if (!II_ferror)
    II_ferror = &Ctx.Idents.get("ferror");
  if (!II_fileno)
    II_fileno = &Ctx.Idents.get("fileno");

  if (FD->getIdentifier() == II_fopen) {
    Fopen(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_tmpfile) {
    Tmpfile(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fclose) {
    Fclose(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fread) {
    Fread(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fwrite) {
    Fwrite(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fseek) {
    Fseek(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_ftell) {
    Ftell(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_rewind) {
    Rewind(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fgetpos) {
    Fgetpos(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fsetpos) {
    Fsetpos(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_clearerr) {
    Clearerr(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_feof) {
    Feof(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_ferror) {
    Ferror(C, CE);
    return true;
  }
  if (FD->getIdentifier() == II_fileno) {
    Fileno(C, CE);
    return true;
  }

  return false;
}

void StreamChecker::Fopen(CheckerContext &C, const CallExpr *CE) const {
  OpenFileAux(C, CE);
}

void StreamChecker::Tmpfile(CheckerContext &C, const CallExpr *CE) const {
  OpenFileAux(C, CE);
}

void StreamChecker::OpenFileAux(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  SValBuilder &svalBuilder = C.getSValBuilder();
  const LocationContext *LCtx = C.getPredecessor()->getLocationContext();
  DefinedSVal RetVal = svalBuilder.conjureSymbolVal(nullptr, CE, LCtx,
                                                    C.blockCount())
      .castAs<DefinedSVal>();
  state = state->BindExpr(CE, C.getLocationContext(), RetVal);

  ConstraintManager &CM = C.getConstraintManager();
  // Bifurcate the state into two: one with a valid FILE* pointer, the other
  // with a NULL.
  ProgramStateRef stateNotNull, stateNull;
  std::tie(stateNotNull, stateNull) = CM.assumeDual(state, RetVal);

  if (SymbolRef Sym = RetVal.getAsSymbol()) {
    // if RetVal is not NULL, set the symbol's state to Opened.
    stateNotNull =
      stateNotNull->set<StreamMap>(Sym,StreamState::getOpened(CE));
    stateNull =
      stateNull->set<StreamMap>(Sym, StreamState::getOpenFailed(CE));

    C.addTransition(stateNotNull);
    C.addTransition(stateNull);
  }
}

void StreamChecker::Fclose(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = CheckDoubleClose(CE, C.getState(), C);
  if (state)
    C.addTransition(state);
}

void StreamChecker::Fread(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  if (!CheckNullStream(C.getSVal(CE->getArg(3)), state, C))
    return;
}

void StreamChecker::Fwrite(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  if (!CheckNullStream(C.getSVal(CE->getArg(3)), state, C))
    return;
}

void StreamChecker::Fseek(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  if (!(state = CheckNullStream(C.getSVal(CE->getArg(0)), state, C)))
    return;
  // Check the legality of the 'whence' argument of 'fseek'.
  SVal Whence = state->getSVal(CE->getArg(2), C.getLocationContext());
  Optional<nonloc::ConcreteInt> CI = Whence.getAs<nonloc::ConcreteInt>();

  if (!CI)
    return;

  int64_t x = CI->getValue().getSExtValue();
  if (x >= 0 && x <= 2)
    return;

  if (ExplodedNode *N = C.generateNonFatalErrorNode(state)) {
    if (!BT_illegalwhence)
      BT_illegalwhence.reset(
          new BuiltinBug(this, "Illegal whence argument",
                         "The whence argument to fseek() should be "
                         "SEEK_SET, SEEK_END, or SEEK_CUR."));
    C.emitReport(llvm::make_unique<BugReport>(
        *BT_illegalwhence, BT_illegalwhence->getDescription(), N));
  }
}

void StreamChecker::Ftell(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  if (!CheckNullStream(C.getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Rewind(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  if (!CheckNullStream(C.getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Fgetpos(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  if (!CheckNullStream(C.getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Fsetpos(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  if (!CheckNullStream(C.getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Clearerr(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  if (!CheckNullStream(C.getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Feof(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  if (!CheckNullStream(C.getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Ferror(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  if (!CheckNullStream(C.getSVal(CE->getArg(0)), state, C))
    return;
}

void StreamChecker::Fileno(CheckerContext &C, const CallExpr *CE) const {
  ProgramStateRef state = C.getState();
  if (!CheckNullStream(C.getSVal(CE->getArg(0)), state, C))
    return;
}

ProgramStateRef StreamChecker::CheckNullStream(SVal SV, ProgramStateRef state,
                                    CheckerContext &C) const {
  Optional<DefinedSVal> DV = SV.getAs<DefinedSVal>();
  if (!DV)
    return nullptr;

  ConstraintManager &CM = C.getConstraintManager();
  ProgramStateRef stateNotNull, stateNull;
  std::tie(stateNotNull, stateNull) = CM.assumeDual(state, *DV);

  if (!stateNotNull && stateNull) {
    if (ExplodedNode *N = C.generateErrorNode(stateNull)) {
      if (!BT_nullfp)
        BT_nullfp.reset(new BuiltinBug(this, "NULL stream pointer",
                                       "Stream pointer might be NULL."));
      C.emitReport(llvm::make_unique<BugReport>(
          *BT_nullfp, BT_nullfp->getDescription(), N));
    }
    return nullptr;
  }
  return stateNotNull;
}

ProgramStateRef StreamChecker::CheckDoubleClose(const CallExpr *CE,
                                               ProgramStateRef state,
                                               CheckerContext &C) const {
  SymbolRef Sym = C.getSVal(CE->getArg(0)).getAsSymbol();
  if (!Sym)
    return state;

  const StreamState *SS = state->get<StreamMap>(Sym);

  // If the file stream is not tracked, return.
  if (!SS)
    return state;

  // Check: Double close a File Descriptor could cause undefined behaviour.
  // Conforming to man-pages
  if (SS->isClosed()) {
    ExplodedNode *N = C.generateErrorNode();
    if (N) {
      if (!BT_doubleclose)
        BT_doubleclose.reset(new BuiltinBug(
            this, "Double fclose", "Try to close a file Descriptor already"
                                   " closed. Cause undefined behaviour."));
      C.emitReport(llvm::make_unique<BugReport>(
          *BT_doubleclose, BT_doubleclose->getDescription(), N));
    }
    return nullptr;
  }

  // Close the File Descriptor.
  return state->set<StreamMap>(Sym, StreamState::getClosed(CE));
}

void StreamChecker::checkDeadSymbols(SymbolReaper &SymReaper,
                                     CheckerContext &C) const {
  ProgramStateRef state = C.getState();

  // TODO: Clean up the state.
  const StreamMapTy &Map = state->get<StreamMap>();
  for (const auto &I: Map) {
    SymbolRef Sym = I.first;
    const StreamState &SS = I.second;
    if (!SymReaper.isDead(Sym) || !SS.isOpened())
      continue;

    ExplodedNode *N = C.generateErrorNode();
    if (!N)
      return;

    if (!BT_ResourceLeak)
      BT_ResourceLeak.reset(
          new BuiltinBug(this, "Resource Leak",
                         "Opened File never closed. Potential Resource leak."));
    C.emitReport(llvm::make_unique<BugReport>(
        *BT_ResourceLeak, BT_ResourceLeak->getDescription(), N));
  }
}

void ento::registerStreamChecker(CheckerManager &mgr) {
  mgr.registerChecker<StreamChecker>();
}
