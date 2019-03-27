//==- ProgramPoint.cpp - Program Points for Path-Sensitive Analysis -*- C++ -*-/
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the interface ProgramPoint, which identifies a
//  distinct location in a function.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/ProgramPoint.h"

using namespace clang;

ProgramPointTag::~ProgramPointTag() {}

ProgramPoint ProgramPoint::getProgramPoint(const Stmt *S, ProgramPoint::Kind K,
                                           const LocationContext *LC,
                                           const ProgramPointTag *tag){
  switch (K) {
    default:
      llvm_unreachable("Unhandled ProgramPoint kind");
    case ProgramPoint::PreStmtKind:
      return PreStmt(S, LC, tag);
    case ProgramPoint::PostStmtKind:
      return PostStmt(S, LC, tag);
    case ProgramPoint::PreLoadKind:
      return PreLoad(S, LC, tag);
    case ProgramPoint::PostLoadKind:
      return PostLoad(S, LC, tag);
    case ProgramPoint::PreStoreKind:
      return PreStore(S, LC, tag);
    case ProgramPoint::PostLValueKind:
      return PostLValue(S, LC, tag);
    case ProgramPoint::PostStmtPurgeDeadSymbolsKind:
      return PostStmtPurgeDeadSymbols(S, LC, tag);
    case ProgramPoint::PreStmtPurgeDeadSymbolsKind:
      return PreStmtPurgeDeadSymbols(S, LC, tag);
  }
}

LLVM_DUMP_METHOD void ProgramPoint::dump() const {
  return print(/*CR=*/"\n", llvm::errs());
}

static void printLocation(raw_ostream &Out, SourceLocation SLoc,
                          const SourceManager &SM,
                          StringRef CR,
                          StringRef Postfix) {
  if (SLoc.isFileID()) {
    Out << CR << "line=" << SM.getExpansionLineNumber(SLoc)
        << " col=" << SM.getExpansionColumnNumber(SLoc) << Postfix;
  }
}

void ProgramPoint::print(StringRef CR, llvm::raw_ostream &Out) const {
  const ASTContext &Context =
      getLocationContext()->getAnalysisDeclContext()->getASTContext();
  const SourceManager &SM = Context.getSourceManager();
  switch (getKind()) {
  case ProgramPoint::BlockEntranceKind:
    Out << "Block Entrance: B"
        << castAs<BlockEntrance>().getBlock()->getBlockID();
    break;

  case ProgramPoint::FunctionExitKind: {
    auto FEP = getAs<FunctionExitPoint>();
    Out << "Function Exit: B" << FEP->getBlock()->getBlockID();
    if (const ReturnStmt *RS = FEP->getStmt()) {
      Out << CR << " Return: S" << RS->getID(Context) << CR;
      RS->printPretty(Out, /*helper=*/nullptr, Context.getPrintingPolicy(),
                      /*Indentation=*/2, /*NewlineSymbol=*/CR);
    }
    break;
  }
  case ProgramPoint::BlockExitKind:
    assert(false);
    break;

  case ProgramPoint::CallEnterKind:
    Out << "CallEnter";
    break;

  case ProgramPoint::CallExitBeginKind:
    Out << "CallExitBegin";
    break;

  case ProgramPoint::CallExitEndKind:
    Out << "CallExitEnd";
    break;

  case ProgramPoint::PostStmtPurgeDeadSymbolsKind:
    Out << "PostStmtPurgeDeadSymbols";
    break;

  case ProgramPoint::PreStmtPurgeDeadSymbolsKind:
    Out << "PreStmtPurgeDeadSymbols";
    break;

  case ProgramPoint::EpsilonKind:
    Out << "Epsilon Point";
    break;

  case ProgramPoint::LoopExitKind: {
    LoopExit LE = castAs<LoopExit>();
    Out << "LoopExit: " << LE.getLoopStmt()->getStmtClassName();
    break;
  }

  case ProgramPoint::PreImplicitCallKind: {
    ImplicitCallPoint PC = castAs<ImplicitCallPoint>();
    Out << "PreCall: ";
    PC.getDecl()->print(Out, Context.getLangOpts());
    printLocation(Out, PC.getLocation(), SM, CR, /*Postfix=*/CR);
    break;
  }

  case ProgramPoint::PostImplicitCallKind: {
    ImplicitCallPoint PC = castAs<ImplicitCallPoint>();
    Out << "PostCall: ";
    PC.getDecl()->print(Out, Context.getLangOpts());
    printLocation(Out, PC.getLocation(), SM, CR, /*Postfix=*/CR);
    break;
  }

  case ProgramPoint::PostInitializerKind: {
    Out << "PostInitializer: ";
    const CXXCtorInitializer *Init = castAs<PostInitializer>().getInitializer();
    if (const FieldDecl *FD = Init->getAnyMember())
      Out << *FD;
    else {
      QualType Ty = Init->getTypeSourceInfo()->getType();
      Ty = Ty.getLocalUnqualifiedType();
      Ty.print(Out, Context.getLangOpts());
    }
    break;
  }

  case ProgramPoint::BlockEdgeKind: {
    const BlockEdge &E = castAs<BlockEdge>();
    Out << "Edge: (B" << E.getSrc()->getBlockID() << ", B"
        << E.getDst()->getBlockID() << ')';

    if (const Stmt *T = E.getSrc()->getTerminator()) {
      SourceLocation SLoc = T->getBeginLoc();

      Out << "\\|Terminator: ";
      E.getSrc()->printTerminator(Out, Context.getLangOpts());
      printLocation(Out, SLoc, SM, CR, /*Postfix=*/"");

      if (isa<SwitchStmt>(T)) {
        const Stmt *Label = E.getDst()->getLabel();

        if (Label) {
          if (const auto *C = dyn_cast<CaseStmt>(Label)) {
            Out << CR << "case ";
            if (C->getLHS())
              C->getLHS()->printPretty(
                  Out, nullptr, Context.getPrintingPolicy(),
                  /*Indentation=*/0, /*NewlineSymbol=*/CR);

            if (const Stmt *RHS = C->getRHS()) {
              Out << " .. ";
              RHS->printPretty(Out, nullptr, Context.getPrintingPolicy(),
                               /*Indetation=*/0, /*NewlineSymbol=*/CR);
            }

            Out << ":";
          } else {
            assert(isa<DefaultStmt>(Label));
            Out << CR << "default:";
          }
        } else
          Out << CR << "(implicit) default:";
      } else if (isa<IndirectGotoStmt>(T)) {
        // FIXME
      } else {
        Out << CR << "Condition: ";
        if (*E.getSrc()->succ_begin() == E.getDst())
          Out << "true";
        else
          Out << "false";
      }

      Out << CR;
    }

    break;
  }

  default: {
    const Stmt *S = castAs<StmtPoint>().getStmt();
    assert(S != nullptr && "Expecting non-null Stmt");

    Out << S->getStmtClassName() << " S" << S->getID(Context) << " <"
        << (const void *)S << "> ";
    S->printPretty(Out, /*helper=*/nullptr, Context.getPrintingPolicy(),
                   /*Indentation=*/2, /*NewlineSymbol=*/CR);
    printLocation(Out, S->getBeginLoc(), SM, CR, /*Postfix=*/"");

    if (getAs<PreStmt>())
      Out << CR << "PreStmt" << CR;
    else if (getAs<PostLoad>())
      Out << CR << "PostLoad" << CR;
    else if (getAs<PostStore>())
      Out << CR << "PostStore" << CR;
    else if (getAs<PostLValue>())
      Out << CR << "PostLValue" << CR;
    else if (getAs<PostAllocatorCall>())
      Out << CR << "PostAllocatorCall" << CR;

    break;
  }
  }
}

SimpleProgramPointTag::SimpleProgramPointTag(StringRef MsgProvider,
                                             StringRef Msg)
  : Desc((MsgProvider + " : " + Msg).str()) {}

StringRef SimpleProgramPointTag::getTagDescription() const {
  return Desc;
}
