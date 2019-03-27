//===- ASTReaderStmt.cpp - Stmt/Expr Deserialization ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Statement/expression deserialization.  This implements the
// ASTReader::ReadStmt method.
//
//===----------------------------------------------------------------------===//

#include "clang/Serialization/ASTReader.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/AttrIterator.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclAccessPair.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/CapturedStmt.h"
#include "clang/Basic/ExpressionTraits.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Lambda.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TypeTraits.h"
#include "clang/Lex/Token.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitstreamReader.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>

using namespace clang;
using namespace serialization;

namespace clang {

  class ASTStmtReader : public StmtVisitor<ASTStmtReader> {
    friend class OMPClauseReader;

    ASTRecordReader &Record;
    llvm::BitstreamCursor &DeclsCursor;

    SourceLocation ReadSourceLocation() {
      return Record.readSourceLocation();
    }

    SourceRange ReadSourceRange() {
      return Record.readSourceRange();
    }

    std::string ReadString() {
      return Record.readString();
    }

    TypeSourceInfo *GetTypeSourceInfo() {
      return Record.getTypeSourceInfo();
    }

    Decl *ReadDecl() {
      return Record.readDecl();
    }

    template<typename T>
    T *ReadDeclAs() {
      return Record.readDeclAs<T>();
    }

    void ReadDeclarationNameLoc(DeclarationNameLoc &DNLoc,
                                DeclarationName Name) {
      Record.readDeclarationNameLoc(DNLoc, Name);
    }

    void ReadDeclarationNameInfo(DeclarationNameInfo &NameInfo) {
      Record.readDeclarationNameInfo(NameInfo);
    }

  public:
    ASTStmtReader(ASTRecordReader &Record, llvm::BitstreamCursor &Cursor)
        : Record(Record), DeclsCursor(Cursor) {}

    /// The number of record fields required for the Stmt class
    /// itself.
    static const unsigned NumStmtFields = 0;

    /// The number of record fields required for the Expr class
    /// itself.
    static const unsigned NumExprFields = NumStmtFields + 7;

    /// Read and initialize a ExplicitTemplateArgumentList structure.
    void ReadTemplateKWAndArgsInfo(ASTTemplateKWAndArgsInfo &Args,
                                   TemplateArgumentLoc *ArgsLocArray,
                                   unsigned NumTemplateArgs);

    /// Read and initialize a ExplicitTemplateArgumentList structure.
    void ReadExplicitTemplateArgumentList(ASTTemplateArgumentListInfo &ArgList,
                                          unsigned NumTemplateArgs);

    void VisitStmt(Stmt *S);
#define STMT(Type, Base) \
    void Visit##Type(Type *);
#include "clang/AST/StmtNodes.inc"
  };

} // namespace clang

void ASTStmtReader::ReadTemplateKWAndArgsInfo(ASTTemplateKWAndArgsInfo &Args,
                                              TemplateArgumentLoc *ArgsLocArray,
                                              unsigned NumTemplateArgs) {
  SourceLocation TemplateKWLoc = ReadSourceLocation();
  TemplateArgumentListInfo ArgInfo;
  ArgInfo.setLAngleLoc(ReadSourceLocation());
  ArgInfo.setRAngleLoc(ReadSourceLocation());
  for (unsigned i = 0; i != NumTemplateArgs; ++i)
    ArgInfo.addArgument(Record.readTemplateArgumentLoc());
  Args.initializeFrom(TemplateKWLoc, ArgInfo, ArgsLocArray);
}

void ASTStmtReader::VisitStmt(Stmt *S) {
  assert(Record.getIdx() == NumStmtFields && "Incorrect statement field count");
}

void ASTStmtReader::VisitNullStmt(NullStmt *S) {
  VisitStmt(S);
  S->setSemiLoc(ReadSourceLocation());
  S->NullStmtBits.HasLeadingEmptyMacro = Record.readInt();
}

void ASTStmtReader::VisitCompoundStmt(CompoundStmt *S) {
  VisitStmt(S);
  SmallVector<Stmt *, 16> Stmts;
  unsigned NumStmts = Record.readInt();
  while (NumStmts--)
    Stmts.push_back(Record.readSubStmt());
  S->setStmts(Stmts);
  S->CompoundStmtBits.LBraceLoc = ReadSourceLocation();
  S->RBraceLoc = ReadSourceLocation();
}

void ASTStmtReader::VisitSwitchCase(SwitchCase *S) {
  VisitStmt(S);
  Record.recordSwitchCaseID(S, Record.readInt());
  S->setKeywordLoc(ReadSourceLocation());
  S->setColonLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitCaseStmt(CaseStmt *S) {
  VisitSwitchCase(S);
  bool CaseStmtIsGNURange = Record.readInt();
  S->setLHS(Record.readSubExpr());
  S->setSubStmt(Record.readSubStmt());
  if (CaseStmtIsGNURange) {
    S->setRHS(Record.readSubExpr());
    S->setEllipsisLoc(ReadSourceLocation());
  }
}

void ASTStmtReader::VisitDefaultStmt(DefaultStmt *S) {
  VisitSwitchCase(S);
  S->setSubStmt(Record.readSubStmt());
}

void ASTStmtReader::VisitLabelStmt(LabelStmt *S) {
  VisitStmt(S);
  auto *LD = ReadDeclAs<LabelDecl>();
  LD->setStmt(S);
  S->setDecl(LD);
  S->setSubStmt(Record.readSubStmt());
  S->setIdentLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitAttributedStmt(AttributedStmt *S) {
  VisitStmt(S);
  // NumAttrs in AttributedStmt is set when creating an empty
  // AttributedStmt in AttributedStmt::CreateEmpty, since it is needed
  // to allocate the right amount of space for the trailing Attr *.
  uint64_t NumAttrs = Record.readInt();
  AttrVec Attrs;
  Record.readAttributes(Attrs);
  (void)NumAttrs;
  assert(NumAttrs == S->AttributedStmtBits.NumAttrs);
  assert(NumAttrs == Attrs.size());
  std::copy(Attrs.begin(), Attrs.end(), S->getAttrArrayPtr());
  S->SubStmt = Record.readSubStmt();
  S->AttributedStmtBits.AttrLoc = ReadSourceLocation();
}

void ASTStmtReader::VisitIfStmt(IfStmt *S) {
  VisitStmt(S);

  S->setConstexpr(Record.readInt());
  bool HasElse = Record.readInt();
  bool HasVar = Record.readInt();
  bool HasInit = Record.readInt();

  S->setCond(Record.readSubExpr());
  S->setThen(Record.readSubStmt());
  if (HasElse)
    S->setElse(Record.readSubStmt());
  if (HasVar)
    S->setConditionVariable(Record.getContext(), ReadDeclAs<VarDecl>());
  if (HasInit)
    S->setInit(Record.readSubStmt());

  S->setIfLoc(ReadSourceLocation());
  if (HasElse)
    S->setElseLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitSwitchStmt(SwitchStmt *S) {
  VisitStmt(S);

  bool HasInit = Record.readInt();
  bool HasVar = Record.readInt();
  bool AllEnumCasesCovered = Record.readInt();
  if (AllEnumCasesCovered)
    S->setAllEnumCasesCovered();

  S->setCond(Record.readSubExpr());
  S->setBody(Record.readSubStmt());
  if (HasInit)
    S->setInit(Record.readSubStmt());
  if (HasVar)
    S->setConditionVariable(Record.getContext(), ReadDeclAs<VarDecl>());

  S->setSwitchLoc(ReadSourceLocation());

  SwitchCase *PrevSC = nullptr;
  for (auto E = Record.size(); Record.getIdx() != E; ) {
    SwitchCase *SC = Record.getSwitchCaseWithID(Record.readInt());
    if (PrevSC)
      PrevSC->setNextSwitchCase(SC);
    else
      S->setSwitchCaseList(SC);

    PrevSC = SC;
  }
}

void ASTStmtReader::VisitWhileStmt(WhileStmt *S) {
  VisitStmt(S);

  bool HasVar = Record.readInt();

  S->setCond(Record.readSubExpr());
  S->setBody(Record.readSubStmt());
  if (HasVar)
    S->setConditionVariable(Record.getContext(), ReadDeclAs<VarDecl>());

  S->setWhileLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitDoStmt(DoStmt *S) {
  VisitStmt(S);
  S->setCond(Record.readSubExpr());
  S->setBody(Record.readSubStmt());
  S->setDoLoc(ReadSourceLocation());
  S->setWhileLoc(ReadSourceLocation());
  S->setRParenLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitForStmt(ForStmt *S) {
  VisitStmt(S);
  S->setInit(Record.readSubStmt());
  S->setCond(Record.readSubExpr());
  S->setConditionVariable(Record.getContext(), ReadDeclAs<VarDecl>());
  S->setInc(Record.readSubExpr());
  S->setBody(Record.readSubStmt());
  S->setForLoc(ReadSourceLocation());
  S->setLParenLoc(ReadSourceLocation());
  S->setRParenLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitGotoStmt(GotoStmt *S) {
  VisitStmt(S);
  S->setLabel(ReadDeclAs<LabelDecl>());
  S->setGotoLoc(ReadSourceLocation());
  S->setLabelLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitIndirectGotoStmt(IndirectGotoStmt *S) {
  VisitStmt(S);
  S->setGotoLoc(ReadSourceLocation());
  S->setStarLoc(ReadSourceLocation());
  S->setTarget(Record.readSubExpr());
}

void ASTStmtReader::VisitContinueStmt(ContinueStmt *S) {
  VisitStmt(S);
  S->setContinueLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitBreakStmt(BreakStmt *S) {
  VisitStmt(S);
  S->setBreakLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitReturnStmt(ReturnStmt *S) {
  VisitStmt(S);

  bool HasNRVOCandidate = Record.readInt();

  S->setRetValue(Record.readSubExpr());
  if (HasNRVOCandidate)
    S->setNRVOCandidate(ReadDeclAs<VarDecl>());

  S->setReturnLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitDeclStmt(DeclStmt *S) {
  VisitStmt(S);
  S->setStartLoc(ReadSourceLocation());
  S->setEndLoc(ReadSourceLocation());

  if (Record.size() - Record.getIdx() == 1) {
    // Single declaration
    S->setDeclGroup(DeclGroupRef(ReadDecl()));
  } else {
    SmallVector<Decl *, 16> Decls;
    int N = Record.size() - Record.getIdx();
    Decls.reserve(N);
    for (int I = 0; I < N; ++I)
      Decls.push_back(ReadDecl());
    S->setDeclGroup(DeclGroupRef(DeclGroup::Create(Record.getContext(),
                                                   Decls.data(),
                                                   Decls.size())));
  }
}

void ASTStmtReader::VisitAsmStmt(AsmStmt *S) {
  VisitStmt(S);
  S->NumOutputs = Record.readInt();
  S->NumInputs = Record.readInt();
  S->NumClobbers = Record.readInt();
  S->setAsmLoc(ReadSourceLocation());
  S->setVolatile(Record.readInt());
  S->setSimple(Record.readInt());
}

void ASTStmtReader::VisitGCCAsmStmt(GCCAsmStmt *S) {
  VisitAsmStmt(S);
  S->setRParenLoc(ReadSourceLocation());
  S->setAsmString(cast_or_null<StringLiteral>(Record.readSubStmt()));

  unsigned NumOutputs = S->getNumOutputs();
  unsigned NumInputs = S->getNumInputs();
  unsigned NumClobbers = S->getNumClobbers();

  // Outputs and inputs
  SmallVector<IdentifierInfo *, 16> Names;
  SmallVector<StringLiteral*, 16> Constraints;
  SmallVector<Stmt*, 16> Exprs;
  for (unsigned I = 0, N = NumOutputs + NumInputs; I != N; ++I) {
    Names.push_back(Record.getIdentifierInfo());
    Constraints.push_back(cast_or_null<StringLiteral>(Record.readSubStmt()));
    Exprs.push_back(Record.readSubStmt());
  }

  // Constraints
  SmallVector<StringLiteral*, 16> Clobbers;
  for (unsigned I = 0; I != NumClobbers; ++I)
    Clobbers.push_back(cast_or_null<StringLiteral>(Record.readSubStmt()));

  S->setOutputsAndInputsAndClobbers(Record.getContext(),
                                    Names.data(), Constraints.data(),
                                    Exprs.data(), NumOutputs, NumInputs,
                                    Clobbers.data(), NumClobbers);
}

void ASTStmtReader::VisitMSAsmStmt(MSAsmStmt *S) {
  VisitAsmStmt(S);
  S->LBraceLoc = ReadSourceLocation();
  S->EndLoc = ReadSourceLocation();
  S->NumAsmToks = Record.readInt();
  std::string AsmStr = ReadString();

  // Read the tokens.
  SmallVector<Token, 16> AsmToks;
  AsmToks.reserve(S->NumAsmToks);
  for (unsigned i = 0, e = S->NumAsmToks; i != e; ++i) {
    AsmToks.push_back(Record.readToken());
  }

  // The calls to reserve() for the FooData vectors are mandatory to
  // prevent dead StringRefs in the Foo vectors.

  // Read the clobbers.
  SmallVector<std::string, 16> ClobbersData;
  SmallVector<StringRef, 16> Clobbers;
  ClobbersData.reserve(S->NumClobbers);
  Clobbers.reserve(S->NumClobbers);
  for (unsigned i = 0, e = S->NumClobbers; i != e; ++i) {
    ClobbersData.push_back(ReadString());
    Clobbers.push_back(ClobbersData.back());
  }

  // Read the operands.
  unsigned NumOperands = S->NumOutputs + S->NumInputs;
  SmallVector<Expr*, 16> Exprs;
  SmallVector<std::string, 16> ConstraintsData;
  SmallVector<StringRef, 16> Constraints;
  Exprs.reserve(NumOperands);
  ConstraintsData.reserve(NumOperands);
  Constraints.reserve(NumOperands);
  for (unsigned i = 0; i != NumOperands; ++i) {
    Exprs.push_back(cast<Expr>(Record.readSubStmt()));
    ConstraintsData.push_back(ReadString());
    Constraints.push_back(ConstraintsData.back());
  }

  S->initialize(Record.getContext(), AsmStr, AsmToks,
                Constraints, Exprs, Clobbers);
}

void ASTStmtReader::VisitCoroutineBodyStmt(CoroutineBodyStmt *S) {
  VisitStmt(S);
  assert(Record.peekInt() == S->NumParams);
  Record.skipInts(1);
  auto *StoredStmts = S->getStoredStmts();
  for (unsigned i = 0;
       i < CoroutineBodyStmt::SubStmt::FirstParamMove + S->NumParams; ++i)
    StoredStmts[i] = Record.readSubStmt();
}

void ASTStmtReader::VisitCoreturnStmt(CoreturnStmt *S) {
  VisitStmt(S);
  S->CoreturnLoc = Record.readSourceLocation();
  for (auto &SubStmt: S->SubStmts)
    SubStmt = Record.readSubStmt();
  S->IsImplicit = Record.readInt() != 0;
}

void ASTStmtReader::VisitCoawaitExpr(CoawaitExpr *E) {
  VisitExpr(E);
  E->KeywordLoc = ReadSourceLocation();
  for (auto &SubExpr: E->SubExprs)
    SubExpr = Record.readSubStmt();
  E->OpaqueValue = cast_or_null<OpaqueValueExpr>(Record.readSubStmt());
  E->setIsImplicit(Record.readInt() != 0);
}

void ASTStmtReader::VisitCoyieldExpr(CoyieldExpr *E) {
  VisitExpr(E);
  E->KeywordLoc = ReadSourceLocation();
  for (auto &SubExpr: E->SubExprs)
    SubExpr = Record.readSubStmt();
  E->OpaqueValue = cast_or_null<OpaqueValueExpr>(Record.readSubStmt());
}

void ASTStmtReader::VisitDependentCoawaitExpr(DependentCoawaitExpr *E) {
  VisitExpr(E);
  E->KeywordLoc = ReadSourceLocation();
  for (auto &SubExpr: E->SubExprs)
    SubExpr = Record.readSubStmt();
}

void ASTStmtReader::VisitCapturedStmt(CapturedStmt *S) {
  VisitStmt(S);
  Record.skipInts(1);
  S->setCapturedDecl(ReadDeclAs<CapturedDecl>());
  S->setCapturedRegionKind(static_cast<CapturedRegionKind>(Record.readInt()));
  S->setCapturedRecordDecl(ReadDeclAs<RecordDecl>());

  // Capture inits
  for (CapturedStmt::capture_init_iterator I = S->capture_init_begin(),
                                           E = S->capture_init_end();
       I != E; ++I)
    *I = Record.readSubExpr();

  // Body
  S->setCapturedStmt(Record.readSubStmt());
  S->getCapturedDecl()->setBody(S->getCapturedStmt());

  // Captures
  for (auto &I : S->captures()) {
    I.VarAndKind.setPointer(ReadDeclAs<VarDecl>());
    I.VarAndKind.setInt(
        static_cast<CapturedStmt::VariableCaptureKind>(Record.readInt()));
    I.Loc = ReadSourceLocation();
  }
}

void ASTStmtReader::VisitExpr(Expr *E) {
  VisitStmt(E);
  E->setType(Record.readType());
  E->setTypeDependent(Record.readInt());
  E->setValueDependent(Record.readInt());
  E->setInstantiationDependent(Record.readInt());
  E->ExprBits.ContainsUnexpandedParameterPack = Record.readInt();
  E->setValueKind(static_cast<ExprValueKind>(Record.readInt()));
  E->setObjectKind(static_cast<ExprObjectKind>(Record.readInt()));
  assert(Record.getIdx() == NumExprFields &&
         "Incorrect expression field count");
}

void ASTStmtReader::VisitConstantExpr(ConstantExpr *E) {
  VisitExpr(E);
  E->setSubExpr(Record.readSubExpr());
}

void ASTStmtReader::VisitPredefinedExpr(PredefinedExpr *E) {
  VisitExpr(E);
  bool HasFunctionName = Record.readInt();
  E->PredefinedExprBits.HasFunctionName = HasFunctionName;
  E->PredefinedExprBits.Kind = Record.readInt();
  E->setLocation(ReadSourceLocation());
  if (HasFunctionName)
    E->setFunctionName(cast<StringLiteral>(Record.readSubExpr()));
}

void ASTStmtReader::VisitDeclRefExpr(DeclRefExpr *E) {
  VisitExpr(E);

  E->DeclRefExprBits.HasQualifier = Record.readInt();
  E->DeclRefExprBits.HasFoundDecl = Record.readInt();
  E->DeclRefExprBits.HasTemplateKWAndArgsInfo = Record.readInt();
  E->DeclRefExprBits.HadMultipleCandidates = Record.readInt();
  E->DeclRefExprBits.RefersToEnclosingVariableOrCapture = Record.readInt();
  unsigned NumTemplateArgs = 0;
  if (E->hasTemplateKWAndArgsInfo())
    NumTemplateArgs = Record.readInt();

  if (E->hasQualifier())
    new (E->getTrailingObjects<NestedNameSpecifierLoc>())
        NestedNameSpecifierLoc(Record.readNestedNameSpecifierLoc());

  if (E->hasFoundDecl())
    *E->getTrailingObjects<NamedDecl *>() = ReadDeclAs<NamedDecl>();

  if (E->hasTemplateKWAndArgsInfo())
    ReadTemplateKWAndArgsInfo(
        *E->getTrailingObjects<ASTTemplateKWAndArgsInfo>(),
        E->getTrailingObjects<TemplateArgumentLoc>(), NumTemplateArgs);

  E->setDecl(ReadDeclAs<ValueDecl>());
  E->setLocation(ReadSourceLocation());
  ReadDeclarationNameLoc(E->DNLoc, E->getDecl()->getDeclName());
}

void ASTStmtReader::VisitIntegerLiteral(IntegerLiteral *E) {
  VisitExpr(E);
  E->setLocation(ReadSourceLocation());
  E->setValue(Record.getContext(), Record.readAPInt());
}

void ASTStmtReader::VisitFixedPointLiteral(FixedPointLiteral *E) {
  VisitExpr(E);
  E->setLocation(ReadSourceLocation());
  E->setValue(Record.getContext(), Record.readAPInt());
}

void ASTStmtReader::VisitFloatingLiteral(FloatingLiteral *E) {
  VisitExpr(E);
  E->setRawSemantics(static_cast<Stmt::APFloatSemantics>(Record.readInt()));
  E->setExact(Record.readInt());
  E->setValue(Record.getContext(), Record.readAPFloat(E->getSemantics()));
  E->setLocation(ReadSourceLocation());
}

void ASTStmtReader::VisitImaginaryLiteral(ImaginaryLiteral *E) {
  VisitExpr(E);
  E->setSubExpr(Record.readSubExpr());
}

void ASTStmtReader::VisitStringLiteral(StringLiteral *E) {
  VisitExpr(E);

  // NumConcatenated, Length and CharByteWidth are set by the empty
  // ctor since they are needed to allocate storage for the trailing objects.
  unsigned NumConcatenated = Record.readInt();
  unsigned Length = Record.readInt();
  unsigned CharByteWidth = Record.readInt();
  assert((NumConcatenated == E->getNumConcatenated()) &&
         "Wrong number of concatenated tokens!");
  assert((Length == E->getLength()) && "Wrong Length!");
  assert((CharByteWidth == E->getCharByteWidth()) && "Wrong character width!");
  E->StringLiteralBits.Kind = Record.readInt();
  E->StringLiteralBits.IsPascal = Record.readInt();

  // The character width is originally computed via mapCharByteWidth.
  // Check that the deserialized character width is consistant with the result
  // of calling mapCharByteWidth.
  assert((CharByteWidth ==
          StringLiteral::mapCharByteWidth(Record.getContext().getTargetInfo(),
                                          E->getKind())) &&
         "Wrong character width!");

  // Deserialize the trailing array of SourceLocation.
  for (unsigned I = 0; I < NumConcatenated; ++I)
    E->setStrTokenLoc(I, ReadSourceLocation());

  // Deserialize the trailing array of char holding the string data.
  char *StrData = E->getStrDataAsChar();
  for (unsigned I = 0; I < Length * CharByteWidth; ++I)
    StrData[I] = Record.readInt();
}

void ASTStmtReader::VisitCharacterLiteral(CharacterLiteral *E) {
  VisitExpr(E);
  E->setValue(Record.readInt());
  E->setLocation(ReadSourceLocation());
  E->setKind(static_cast<CharacterLiteral::CharacterKind>(Record.readInt()));
}

void ASTStmtReader::VisitParenExpr(ParenExpr *E) {
  VisitExpr(E);
  E->setLParen(ReadSourceLocation());
  E->setRParen(ReadSourceLocation());
  E->setSubExpr(Record.readSubExpr());
}

void ASTStmtReader::VisitParenListExpr(ParenListExpr *E) {
  VisitExpr(E);
  unsigned NumExprs = Record.readInt();
  assert((NumExprs == E->getNumExprs()) && "Wrong NumExprs!");
  for (unsigned I = 0; I != NumExprs; ++I)
    E->getTrailingObjects<Stmt *>()[I] = Record.readSubStmt();
  E->LParenLoc = ReadSourceLocation();
  E->RParenLoc = ReadSourceLocation();
}

void ASTStmtReader::VisitUnaryOperator(UnaryOperator *E) {
  VisitExpr(E);
  E->setSubExpr(Record.readSubExpr());
  E->setOpcode((UnaryOperator::Opcode)Record.readInt());
  E->setOperatorLoc(ReadSourceLocation());
  E->setCanOverflow(Record.readInt());
}

void ASTStmtReader::VisitOffsetOfExpr(OffsetOfExpr *E) {
  VisitExpr(E);
  assert(E->getNumComponents() == Record.peekInt());
  Record.skipInts(1);
  assert(E->getNumExpressions() == Record.peekInt());
  Record.skipInts(1);
  E->setOperatorLoc(ReadSourceLocation());
  E->setRParenLoc(ReadSourceLocation());
  E->setTypeSourceInfo(GetTypeSourceInfo());
  for (unsigned I = 0, N = E->getNumComponents(); I != N; ++I) {
    auto Kind = static_cast<OffsetOfNode::Kind>(Record.readInt());
    SourceLocation Start = ReadSourceLocation();
    SourceLocation End = ReadSourceLocation();
    switch (Kind) {
    case OffsetOfNode::Array:
      E->setComponent(I, OffsetOfNode(Start, Record.readInt(), End));
      break;

    case OffsetOfNode::Field:
      E->setComponent(
          I, OffsetOfNode(Start, ReadDeclAs<FieldDecl>(), End));
      break;

    case OffsetOfNode::Identifier:
      E->setComponent(
          I,
          OffsetOfNode(Start, Record.getIdentifierInfo(), End));
      break;

    case OffsetOfNode::Base: {
      auto *Base = new (Record.getContext()) CXXBaseSpecifier();
      *Base = Record.readCXXBaseSpecifier();
      E->setComponent(I, OffsetOfNode(Base));
      break;
    }
    }
  }

  for (unsigned I = 0, N = E->getNumExpressions(); I != N; ++I)
    E->setIndexExpr(I, Record.readSubExpr());
}

void ASTStmtReader::VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *E) {
  VisitExpr(E);
  E->setKind(static_cast<UnaryExprOrTypeTrait>(Record.readInt()));
  if (Record.peekInt() == 0) {
    E->setArgument(Record.readSubExpr());
    Record.skipInts(1);
  } else {
    E->setArgument(GetTypeSourceInfo());
  }
  E->setOperatorLoc(ReadSourceLocation());
  E->setRParenLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
  VisitExpr(E);
  E->setLHS(Record.readSubExpr());
  E->setRHS(Record.readSubExpr());
  E->setRBracketLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitOMPArraySectionExpr(OMPArraySectionExpr *E) {
  VisitExpr(E);
  E->setBase(Record.readSubExpr());
  E->setLowerBound(Record.readSubExpr());
  E->setLength(Record.readSubExpr());
  E->setColonLoc(ReadSourceLocation());
  E->setRBracketLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitCallExpr(CallExpr *E) {
  VisitExpr(E);
  unsigned NumArgs = Record.readInt();
  assert((NumArgs == E->getNumArgs()) && "Wrong NumArgs!");
  E->setRParenLoc(ReadSourceLocation());
  E->setCallee(Record.readSubExpr());
  for (unsigned I = 0; I != NumArgs; ++I)
    E->setArg(I, Record.readSubExpr());
  E->setADLCallKind(static_cast<CallExpr::ADLCallKind>(Record.readInt()));
}

void ASTStmtReader::VisitCXXMemberCallExpr(CXXMemberCallExpr *E) {
  VisitCallExpr(E);
}

void ASTStmtReader::VisitMemberExpr(MemberExpr *E) {
  // Don't call VisitExpr, this is fully initialized at creation.
  assert(E->getStmtClass() == Stmt::MemberExprClass &&
         "It's a subclass, we must advance Idx!");
}

void ASTStmtReader::VisitObjCIsaExpr(ObjCIsaExpr *E) {
  VisitExpr(E);
  E->setBase(Record.readSubExpr());
  E->setIsaMemberLoc(ReadSourceLocation());
  E->setOpLoc(ReadSourceLocation());
  E->setArrow(Record.readInt());
}

void ASTStmtReader::
VisitObjCIndirectCopyRestoreExpr(ObjCIndirectCopyRestoreExpr *E) {
  VisitExpr(E);
  E->Operand = Record.readSubExpr();
  E->setShouldCopy(Record.readInt());
}

void ASTStmtReader::VisitObjCBridgedCastExpr(ObjCBridgedCastExpr *E) {
  VisitExplicitCastExpr(E);
  E->LParenLoc = ReadSourceLocation();
  E->BridgeKeywordLoc = ReadSourceLocation();
  E->Kind = Record.readInt();
}

void ASTStmtReader::VisitCastExpr(CastExpr *E) {
  VisitExpr(E);
  unsigned NumBaseSpecs = Record.readInt();
  assert(NumBaseSpecs == E->path_size());
  E->setSubExpr(Record.readSubExpr());
  E->setCastKind((CastKind)Record.readInt());
  CastExpr::path_iterator BaseI = E->path_begin();
  while (NumBaseSpecs--) {
    auto *BaseSpec = new (Record.getContext()) CXXBaseSpecifier;
    *BaseSpec = Record.readCXXBaseSpecifier();
    *BaseI++ = BaseSpec;
  }
}

void ASTStmtReader::VisitBinaryOperator(BinaryOperator *E) {
  VisitExpr(E);
  E->setLHS(Record.readSubExpr());
  E->setRHS(Record.readSubExpr());
  E->setOpcode((BinaryOperator::Opcode)Record.readInt());
  E->setOperatorLoc(ReadSourceLocation());
  E->setFPFeatures(FPOptions(Record.readInt()));
}

void ASTStmtReader::VisitCompoundAssignOperator(CompoundAssignOperator *E) {
  VisitBinaryOperator(E);
  E->setComputationLHSType(Record.readType());
  E->setComputationResultType(Record.readType());
}

void ASTStmtReader::VisitConditionalOperator(ConditionalOperator *E) {
  VisitExpr(E);
  E->SubExprs[ConditionalOperator::COND] = Record.readSubExpr();
  E->SubExprs[ConditionalOperator::LHS] = Record.readSubExpr();
  E->SubExprs[ConditionalOperator::RHS] = Record.readSubExpr();
  E->QuestionLoc = ReadSourceLocation();
  E->ColonLoc = ReadSourceLocation();
}

void
ASTStmtReader::VisitBinaryConditionalOperator(BinaryConditionalOperator *E) {
  VisitExpr(E);
  E->OpaqueValue = cast<OpaqueValueExpr>(Record.readSubExpr());
  E->SubExprs[BinaryConditionalOperator::COMMON] = Record.readSubExpr();
  E->SubExprs[BinaryConditionalOperator::COND] = Record.readSubExpr();
  E->SubExprs[BinaryConditionalOperator::LHS] = Record.readSubExpr();
  E->SubExprs[BinaryConditionalOperator::RHS] = Record.readSubExpr();
  E->QuestionLoc = ReadSourceLocation();
  E->ColonLoc = ReadSourceLocation();
}

void ASTStmtReader::VisitImplicitCastExpr(ImplicitCastExpr *E) {
  VisitCastExpr(E);
  E->setIsPartOfExplicitCast(Record.readInt());
}

void ASTStmtReader::VisitExplicitCastExpr(ExplicitCastExpr *E) {
  VisitCastExpr(E);
  E->setTypeInfoAsWritten(GetTypeSourceInfo());
}

void ASTStmtReader::VisitCStyleCastExpr(CStyleCastExpr *E) {
  VisitExplicitCastExpr(E);
  E->setLParenLoc(ReadSourceLocation());
  E->setRParenLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {
  VisitExpr(E);
  E->setLParenLoc(ReadSourceLocation());
  E->setTypeSourceInfo(GetTypeSourceInfo());
  E->setInitializer(Record.readSubExpr());
  E->setFileScope(Record.readInt());
}

void ASTStmtReader::VisitExtVectorElementExpr(ExtVectorElementExpr *E) {
  VisitExpr(E);
  E->setBase(Record.readSubExpr());
  E->setAccessor(Record.getIdentifierInfo());
  E->setAccessorLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitInitListExpr(InitListExpr *E) {
  VisitExpr(E);
  if (auto *SyntForm = cast_or_null<InitListExpr>(Record.readSubStmt()))
    E->setSyntacticForm(SyntForm);
  E->setLBraceLoc(ReadSourceLocation());
  E->setRBraceLoc(ReadSourceLocation());
  bool isArrayFiller = Record.readInt();
  Expr *filler = nullptr;
  if (isArrayFiller) {
    filler = Record.readSubExpr();
    E->ArrayFillerOrUnionFieldInit = filler;
  } else
    E->ArrayFillerOrUnionFieldInit = ReadDeclAs<FieldDecl>();
  E->sawArrayRangeDesignator(Record.readInt());
  unsigned NumInits = Record.readInt();
  E->reserveInits(Record.getContext(), NumInits);
  if (isArrayFiller) {
    for (unsigned I = 0; I != NumInits; ++I) {
      Expr *init = Record.readSubExpr();
      E->updateInit(Record.getContext(), I, init ? init : filler);
    }
  } else {
    for (unsigned I = 0; I != NumInits; ++I)
      E->updateInit(Record.getContext(), I, Record.readSubExpr());
  }
}

void ASTStmtReader::VisitDesignatedInitExpr(DesignatedInitExpr *E) {
  using Designator = DesignatedInitExpr::Designator;

  VisitExpr(E);
  unsigned NumSubExprs = Record.readInt();
  assert(NumSubExprs == E->getNumSubExprs() && "Wrong number of subexprs");
  for (unsigned I = 0; I != NumSubExprs; ++I)
    E->setSubExpr(I, Record.readSubExpr());
  E->setEqualOrColonLoc(ReadSourceLocation());
  E->setGNUSyntax(Record.readInt());

  SmallVector<Designator, 4> Designators;
  while (Record.getIdx() < Record.size()) {
    switch ((DesignatorTypes)Record.readInt()) {
    case DESIG_FIELD_DECL: {
      auto *Field = ReadDeclAs<FieldDecl>();
      SourceLocation DotLoc = ReadSourceLocation();
      SourceLocation FieldLoc = ReadSourceLocation();
      Designators.push_back(Designator(Field->getIdentifier(), DotLoc,
                                       FieldLoc));
      Designators.back().setField(Field);
      break;
    }

    case DESIG_FIELD_NAME: {
      const IdentifierInfo *Name = Record.getIdentifierInfo();
      SourceLocation DotLoc = ReadSourceLocation();
      SourceLocation FieldLoc = ReadSourceLocation();
      Designators.push_back(Designator(Name, DotLoc, FieldLoc));
      break;
    }

    case DESIG_ARRAY: {
      unsigned Index = Record.readInt();
      SourceLocation LBracketLoc = ReadSourceLocation();
      SourceLocation RBracketLoc = ReadSourceLocation();
      Designators.push_back(Designator(Index, LBracketLoc, RBracketLoc));
      break;
    }

    case DESIG_ARRAY_RANGE: {
      unsigned Index = Record.readInt();
      SourceLocation LBracketLoc = ReadSourceLocation();
      SourceLocation EllipsisLoc = ReadSourceLocation();
      SourceLocation RBracketLoc = ReadSourceLocation();
      Designators.push_back(Designator(Index, LBracketLoc, EllipsisLoc,
                                       RBracketLoc));
      break;
    }
    }
  }
  E->setDesignators(Record.getContext(),
                    Designators.data(), Designators.size());
}

void ASTStmtReader::VisitDesignatedInitUpdateExpr(DesignatedInitUpdateExpr *E) {
  VisitExpr(E);
  E->setBase(Record.readSubExpr());
  E->setUpdater(Record.readSubExpr());
}

void ASTStmtReader::VisitNoInitExpr(NoInitExpr *E) {
  VisitExpr(E);
}

void ASTStmtReader::VisitArrayInitLoopExpr(ArrayInitLoopExpr *E) {
  VisitExpr(E);
  E->SubExprs[0] = Record.readSubExpr();
  E->SubExprs[1] = Record.readSubExpr();
}

void ASTStmtReader::VisitArrayInitIndexExpr(ArrayInitIndexExpr *E) {
  VisitExpr(E);
}

void ASTStmtReader::VisitImplicitValueInitExpr(ImplicitValueInitExpr *E) {
  VisitExpr(E);
}

void ASTStmtReader::VisitVAArgExpr(VAArgExpr *E) {
  VisitExpr(E);
  E->setSubExpr(Record.readSubExpr());
  E->setWrittenTypeInfo(GetTypeSourceInfo());
  E->setBuiltinLoc(ReadSourceLocation());
  E->setRParenLoc(ReadSourceLocation());
  E->setIsMicrosoftABI(Record.readInt());
}

void ASTStmtReader::VisitAddrLabelExpr(AddrLabelExpr *E) {
  VisitExpr(E);
  E->setAmpAmpLoc(ReadSourceLocation());
  E->setLabelLoc(ReadSourceLocation());
  E->setLabel(ReadDeclAs<LabelDecl>());
}

void ASTStmtReader::VisitStmtExpr(StmtExpr *E) {
  VisitExpr(E);
  E->setLParenLoc(ReadSourceLocation());
  E->setRParenLoc(ReadSourceLocation());
  E->setSubStmt(cast_or_null<CompoundStmt>(Record.readSubStmt()));
}

void ASTStmtReader::VisitChooseExpr(ChooseExpr *E) {
  VisitExpr(E);
  E->setCond(Record.readSubExpr());
  E->setLHS(Record.readSubExpr());
  E->setRHS(Record.readSubExpr());
  E->setBuiltinLoc(ReadSourceLocation());
  E->setRParenLoc(ReadSourceLocation());
  E->setIsConditionTrue(Record.readInt());
}

void ASTStmtReader::VisitGNUNullExpr(GNUNullExpr *E) {
  VisitExpr(E);
  E->setTokenLocation(ReadSourceLocation());
}

void ASTStmtReader::VisitShuffleVectorExpr(ShuffleVectorExpr *E) {
  VisitExpr(E);
  SmallVector<Expr *, 16> Exprs;
  unsigned NumExprs = Record.readInt();
  while (NumExprs--)
    Exprs.push_back(Record.readSubExpr());
  E->setExprs(Record.getContext(), Exprs);
  E->setBuiltinLoc(ReadSourceLocation());
  E->setRParenLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitConvertVectorExpr(ConvertVectorExpr *E) {
  VisitExpr(E);
  E->BuiltinLoc = ReadSourceLocation();
  E->RParenLoc = ReadSourceLocation();
  E->TInfo = GetTypeSourceInfo();
  E->SrcExpr = Record.readSubExpr();
}

void ASTStmtReader::VisitBlockExpr(BlockExpr *E) {
  VisitExpr(E);
  E->setBlockDecl(ReadDeclAs<BlockDecl>());
}

void ASTStmtReader::VisitGenericSelectionExpr(GenericSelectionExpr *E) {
  VisitExpr(E);
  E->NumAssocs = Record.readInt();
  E->AssocTypes = new (Record.getContext()) TypeSourceInfo*[E->NumAssocs];
  E->SubExprs =
   new(Record.getContext()) Stmt*[GenericSelectionExpr::END_EXPR+E->NumAssocs];

  E->SubExprs[GenericSelectionExpr::CONTROLLING] = Record.readSubExpr();
  for (unsigned I = 0, N = E->getNumAssocs(); I != N; ++I) {
    E->AssocTypes[I] = GetTypeSourceInfo();
    E->SubExprs[GenericSelectionExpr::END_EXPR+I] = Record.readSubExpr();
  }
  E->ResultIndex = Record.readInt();

  E->GenericLoc = ReadSourceLocation();
  E->DefaultLoc = ReadSourceLocation();
  E->RParenLoc = ReadSourceLocation();
}

void ASTStmtReader::VisitPseudoObjectExpr(PseudoObjectExpr *E) {
  VisitExpr(E);
  unsigned numSemanticExprs = Record.readInt();
  assert(numSemanticExprs + 1 == E->PseudoObjectExprBits.NumSubExprs);
  E->PseudoObjectExprBits.ResultIndex = Record.readInt();

  // Read the syntactic expression.
  E->getSubExprsBuffer()[0] = Record.readSubExpr();

  // Read all the semantic expressions.
  for (unsigned i = 0; i != numSemanticExprs; ++i) {
    Expr *subExpr = Record.readSubExpr();
    E->getSubExprsBuffer()[i+1] = subExpr;
  }
}

void ASTStmtReader::VisitAtomicExpr(AtomicExpr *E) {
  VisitExpr(E);
  E->Op = AtomicExpr::AtomicOp(Record.readInt());
  E->NumSubExprs = AtomicExpr::getNumSubExprs(E->Op);
  for (unsigned I = 0; I != E->NumSubExprs; ++I)
    E->SubExprs[I] = Record.readSubExpr();
  E->BuiltinLoc = ReadSourceLocation();
  E->RParenLoc = ReadSourceLocation();
}

//===----------------------------------------------------------------------===//
// Objective-C Expressions and Statements

void ASTStmtReader::VisitObjCStringLiteral(ObjCStringLiteral *E) {
  VisitExpr(E);
  E->setString(cast<StringLiteral>(Record.readSubStmt()));
  E->setAtLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitObjCBoxedExpr(ObjCBoxedExpr *E) {
  VisitExpr(E);
  // could be one of several IntegerLiteral, FloatLiteral, etc.
  E->SubExpr = Record.readSubStmt();
  E->BoxingMethod = ReadDeclAs<ObjCMethodDecl>();
  E->Range = ReadSourceRange();
}

void ASTStmtReader::VisitObjCArrayLiteral(ObjCArrayLiteral *E) {
  VisitExpr(E);
  unsigned NumElements = Record.readInt();
  assert(NumElements == E->getNumElements() && "Wrong number of elements");
  Expr **Elements = E->getElements();
  for (unsigned I = 0, N = NumElements; I != N; ++I)
    Elements[I] = Record.readSubExpr();
  E->ArrayWithObjectsMethod = ReadDeclAs<ObjCMethodDecl>();
  E->Range = ReadSourceRange();
}

void ASTStmtReader::VisitObjCDictionaryLiteral(ObjCDictionaryLiteral *E) {
  VisitExpr(E);
  unsigned NumElements = Record.readInt();
  assert(NumElements == E->getNumElements() && "Wrong number of elements");
  bool HasPackExpansions = Record.readInt();
  assert(HasPackExpansions == E->HasPackExpansions &&"Pack expansion mismatch");
  auto *KeyValues =
      E->getTrailingObjects<ObjCDictionaryLiteral::KeyValuePair>();
  auto *Expansions =
      E->getTrailingObjects<ObjCDictionaryLiteral::ExpansionData>();
  for (unsigned I = 0; I != NumElements; ++I) {
    KeyValues[I].Key = Record.readSubExpr();
    KeyValues[I].Value = Record.readSubExpr();
    if (HasPackExpansions) {
      Expansions[I].EllipsisLoc = ReadSourceLocation();
      Expansions[I].NumExpansionsPlusOne = Record.readInt();
    }
  }
  E->DictWithObjectsMethod = ReadDeclAs<ObjCMethodDecl>();
  E->Range = ReadSourceRange();
}

void ASTStmtReader::VisitObjCEncodeExpr(ObjCEncodeExpr *E) {
  VisitExpr(E);
  E->setEncodedTypeSourceInfo(GetTypeSourceInfo());
  E->setAtLoc(ReadSourceLocation());
  E->setRParenLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitObjCSelectorExpr(ObjCSelectorExpr *E) {
  VisitExpr(E);
  E->setSelector(Record.readSelector());
  E->setAtLoc(ReadSourceLocation());
  E->setRParenLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitObjCProtocolExpr(ObjCProtocolExpr *E) {
  VisitExpr(E);
  E->setProtocol(ReadDeclAs<ObjCProtocolDecl>());
  E->setAtLoc(ReadSourceLocation());
  E->ProtoLoc = ReadSourceLocation();
  E->setRParenLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitObjCIvarRefExpr(ObjCIvarRefExpr *E) {
  VisitExpr(E);
  E->setDecl(ReadDeclAs<ObjCIvarDecl>());
  E->setLocation(ReadSourceLocation());
  E->setOpLoc(ReadSourceLocation());
  E->setBase(Record.readSubExpr());
  E->setIsArrow(Record.readInt());
  E->setIsFreeIvar(Record.readInt());
}

void ASTStmtReader::VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *E) {
  VisitExpr(E);
  unsigned MethodRefFlags = Record.readInt();
  bool Implicit = Record.readInt() != 0;
  if (Implicit) {
    auto *Getter = ReadDeclAs<ObjCMethodDecl>();
    auto *Setter = ReadDeclAs<ObjCMethodDecl>();
    E->setImplicitProperty(Getter, Setter, MethodRefFlags);
  } else {
    E->setExplicitProperty(ReadDeclAs<ObjCPropertyDecl>(), MethodRefFlags);
  }
  E->setLocation(ReadSourceLocation());
  E->setReceiverLocation(ReadSourceLocation());
  switch (Record.readInt()) {
  case 0:
    E->setBase(Record.readSubExpr());
    break;
  case 1:
    E->setSuperReceiver(Record.readType());
    break;
  case 2:
    E->setClassReceiver(ReadDeclAs<ObjCInterfaceDecl>());
    break;
  }
}

void ASTStmtReader::VisitObjCSubscriptRefExpr(ObjCSubscriptRefExpr *E) {
  VisitExpr(E);
  E->setRBracket(ReadSourceLocation());
  E->setBaseExpr(Record.readSubExpr());
  E->setKeyExpr(Record.readSubExpr());
  E->GetAtIndexMethodDecl = ReadDeclAs<ObjCMethodDecl>();
  E->SetAtIndexMethodDecl = ReadDeclAs<ObjCMethodDecl>();
}

void ASTStmtReader::VisitObjCMessageExpr(ObjCMessageExpr *E) {
  VisitExpr(E);
  assert(Record.peekInt() == E->getNumArgs());
  Record.skipInts(1);
  unsigned NumStoredSelLocs = Record.readInt();
  E->SelLocsKind = Record.readInt();
  E->setDelegateInitCall(Record.readInt());
  E->IsImplicit = Record.readInt();
  auto Kind = static_cast<ObjCMessageExpr::ReceiverKind>(Record.readInt());
  switch (Kind) {
  case ObjCMessageExpr::Instance:
    E->setInstanceReceiver(Record.readSubExpr());
    break;

  case ObjCMessageExpr::Class:
    E->setClassReceiver(GetTypeSourceInfo());
    break;

  case ObjCMessageExpr::SuperClass:
  case ObjCMessageExpr::SuperInstance: {
    QualType T = Record.readType();
    SourceLocation SuperLoc = ReadSourceLocation();
    E->setSuper(SuperLoc, T, Kind == ObjCMessageExpr::SuperInstance);
    break;
  }
  }

  assert(Kind == E->getReceiverKind());

  if (Record.readInt())
    E->setMethodDecl(ReadDeclAs<ObjCMethodDecl>());
  else
    E->setSelector(Record.readSelector());

  E->LBracLoc = ReadSourceLocation();
  E->RBracLoc = ReadSourceLocation();

  for (unsigned I = 0, N = E->getNumArgs(); I != N; ++I)
    E->setArg(I, Record.readSubExpr());

  SourceLocation *Locs = E->getStoredSelLocs();
  for (unsigned I = 0; I != NumStoredSelLocs; ++I)
    Locs[I] = ReadSourceLocation();
}

void ASTStmtReader::VisitObjCForCollectionStmt(ObjCForCollectionStmt *S) {
  VisitStmt(S);
  S->setElement(Record.readSubStmt());
  S->setCollection(Record.readSubExpr());
  S->setBody(Record.readSubStmt());
  S->setForLoc(ReadSourceLocation());
  S->setRParenLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitObjCAtCatchStmt(ObjCAtCatchStmt *S) {
  VisitStmt(S);
  S->setCatchBody(Record.readSubStmt());
  S->setCatchParamDecl(ReadDeclAs<VarDecl>());
  S->setAtCatchLoc(ReadSourceLocation());
  S->setRParenLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitObjCAtFinallyStmt(ObjCAtFinallyStmt *S) {
  VisitStmt(S);
  S->setFinallyBody(Record.readSubStmt());
  S->setAtFinallyLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitObjCAutoreleasePoolStmt(ObjCAutoreleasePoolStmt *S) {
  VisitStmt(S);
  S->setSubStmt(Record.readSubStmt());
  S->setAtLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitObjCAtTryStmt(ObjCAtTryStmt *S) {
  VisitStmt(S);
  assert(Record.peekInt() == S->getNumCatchStmts());
  Record.skipInts(1);
  bool HasFinally = Record.readInt();
  S->setTryBody(Record.readSubStmt());
  for (unsigned I = 0, N = S->getNumCatchStmts(); I != N; ++I)
    S->setCatchStmt(I, cast_or_null<ObjCAtCatchStmt>(Record.readSubStmt()));

  if (HasFinally)
    S->setFinallyStmt(Record.readSubStmt());
  S->setAtTryLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitObjCAtSynchronizedStmt(ObjCAtSynchronizedStmt *S) {
  VisitStmt(S);
  S->setSynchExpr(Record.readSubStmt());
  S->setSynchBody(Record.readSubStmt());
  S->setAtSynchronizedLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitObjCAtThrowStmt(ObjCAtThrowStmt *S) {
  VisitStmt(S);
  S->setThrowExpr(Record.readSubStmt());
  S->setThrowLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitObjCBoolLiteralExpr(ObjCBoolLiteralExpr *E) {
  VisitExpr(E);
  E->setValue(Record.readInt());
  E->setLocation(ReadSourceLocation());
}

void ASTStmtReader::VisitObjCAvailabilityCheckExpr(ObjCAvailabilityCheckExpr *E) {
  VisitExpr(E);
  SourceRange R = Record.readSourceRange();
  E->AtLoc = R.getBegin();
  E->RParen = R.getEnd();
  E->VersionToCheck = Record.readVersionTuple();
}

//===----------------------------------------------------------------------===//
// C++ Expressions and Statements
//===----------------------------------------------------------------------===//

void ASTStmtReader::VisitCXXCatchStmt(CXXCatchStmt *S) {
  VisitStmt(S);
  S->CatchLoc = ReadSourceLocation();
  S->ExceptionDecl = ReadDeclAs<VarDecl>();
  S->HandlerBlock = Record.readSubStmt();
}

void ASTStmtReader::VisitCXXTryStmt(CXXTryStmt *S) {
  VisitStmt(S);
  assert(Record.peekInt() == S->getNumHandlers() && "NumStmtFields is wrong ?");
  Record.skipInts(1);
  S->TryLoc = ReadSourceLocation();
  S->getStmts()[0] = Record.readSubStmt();
  for (unsigned i = 0, e = S->getNumHandlers(); i != e; ++i)
    S->getStmts()[i + 1] = Record.readSubStmt();
}

void ASTStmtReader::VisitCXXForRangeStmt(CXXForRangeStmt *S) {
  VisitStmt(S);
  S->ForLoc = ReadSourceLocation();
  S->CoawaitLoc = ReadSourceLocation();
  S->ColonLoc = ReadSourceLocation();
  S->RParenLoc = ReadSourceLocation();
  S->setInit(Record.readSubStmt());
  S->setRangeStmt(Record.readSubStmt());
  S->setBeginStmt(Record.readSubStmt());
  S->setEndStmt(Record.readSubStmt());
  S->setCond(Record.readSubExpr());
  S->setInc(Record.readSubExpr());
  S->setLoopVarStmt(Record.readSubStmt());
  S->setBody(Record.readSubStmt());
}

void ASTStmtReader::VisitMSDependentExistsStmt(MSDependentExistsStmt *S) {
  VisitStmt(S);
  S->KeywordLoc = ReadSourceLocation();
  S->IsIfExists = Record.readInt();
  S->QualifierLoc = Record.readNestedNameSpecifierLoc();
  ReadDeclarationNameInfo(S->NameInfo);
  S->SubStmt = Record.readSubStmt();
}

void ASTStmtReader::VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
  VisitCallExpr(E);
  E->CXXOperatorCallExprBits.OperatorKind = Record.readInt();
  E->CXXOperatorCallExprBits.FPFeatures = Record.readInt();
  E->Range = Record.readSourceRange();
}

void ASTStmtReader::VisitCXXConstructExpr(CXXConstructExpr *E) {
  VisitExpr(E);

  unsigned NumArgs = Record.readInt();
  assert((NumArgs == E->getNumArgs()) && "Wrong NumArgs!");

  E->CXXConstructExprBits.Elidable = Record.readInt();
  E->CXXConstructExprBits.HadMultipleCandidates = Record.readInt();
  E->CXXConstructExprBits.ListInitialization = Record.readInt();
  E->CXXConstructExprBits.StdInitListInitialization = Record.readInt();
  E->CXXConstructExprBits.ZeroInitialization = Record.readInt();
  E->CXXConstructExprBits.ConstructionKind = Record.readInt();
  E->CXXConstructExprBits.Loc = ReadSourceLocation();
  E->Constructor = ReadDeclAs<CXXConstructorDecl>();
  E->ParenOrBraceRange = ReadSourceRange();

  for (unsigned I = 0; I != NumArgs; ++I)
    E->setArg(I, Record.readSubExpr());
}

void ASTStmtReader::VisitCXXInheritedCtorInitExpr(CXXInheritedCtorInitExpr *E) {
  VisitExpr(E);
  E->Constructor = ReadDeclAs<CXXConstructorDecl>();
  E->Loc = ReadSourceLocation();
  E->ConstructsVirtualBase = Record.readInt();
  E->InheritedFromVirtualBase = Record.readInt();
}

void ASTStmtReader::VisitCXXTemporaryObjectExpr(CXXTemporaryObjectExpr *E) {
  VisitCXXConstructExpr(E);
  E->TSI = GetTypeSourceInfo();
}

void ASTStmtReader::VisitLambdaExpr(LambdaExpr *E) {
  VisitExpr(E);
  unsigned NumCaptures = Record.readInt();
  assert(NumCaptures == E->NumCaptures);(void)NumCaptures;
  E->IntroducerRange = ReadSourceRange();
  E->CaptureDefault = static_cast<LambdaCaptureDefault>(Record.readInt());
  E->CaptureDefaultLoc = ReadSourceLocation();
  E->ExplicitParams = Record.readInt();
  E->ExplicitResultType = Record.readInt();
  E->ClosingBrace = ReadSourceLocation();

  // Read capture initializers.
  for (LambdaExpr::capture_init_iterator C = E->capture_init_begin(),
                                      CEnd = E->capture_init_end();
       C != CEnd; ++C)
    *C = Record.readSubExpr();
}

void
ASTStmtReader::VisitCXXStdInitializerListExpr(CXXStdInitializerListExpr *E) {
  VisitExpr(E);
  E->SubExpr = Record.readSubExpr();
}

void ASTStmtReader::VisitCXXNamedCastExpr(CXXNamedCastExpr *E) {
  VisitExplicitCastExpr(E);
  SourceRange R = ReadSourceRange();
  E->Loc = R.getBegin();
  E->RParenLoc = R.getEnd();
  R = ReadSourceRange();
  E->AngleBrackets = R;
}

void ASTStmtReader::VisitCXXStaticCastExpr(CXXStaticCastExpr *E) {
  return VisitCXXNamedCastExpr(E);
}

void ASTStmtReader::VisitCXXDynamicCastExpr(CXXDynamicCastExpr *E) {
  return VisitCXXNamedCastExpr(E);
}

void ASTStmtReader::VisitCXXReinterpretCastExpr(CXXReinterpretCastExpr *E) {
  return VisitCXXNamedCastExpr(E);
}

void ASTStmtReader::VisitCXXConstCastExpr(CXXConstCastExpr *E) {
  return VisitCXXNamedCastExpr(E);
}

void ASTStmtReader::VisitCXXFunctionalCastExpr(CXXFunctionalCastExpr *E) {
  VisitExplicitCastExpr(E);
  E->setLParenLoc(ReadSourceLocation());
  E->setRParenLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitUserDefinedLiteral(UserDefinedLiteral *E) {
  VisitCallExpr(E);
  E->UDSuffixLoc = ReadSourceLocation();
}

void ASTStmtReader::VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *E) {
  VisitExpr(E);
  E->setValue(Record.readInt());
  E->setLocation(ReadSourceLocation());
}

void ASTStmtReader::VisitCXXNullPtrLiteralExpr(CXXNullPtrLiteralExpr *E) {
  VisitExpr(E);
  E->setLocation(ReadSourceLocation());
}

void ASTStmtReader::VisitCXXTypeidExpr(CXXTypeidExpr *E) {
  VisitExpr(E);
  E->setSourceRange(ReadSourceRange());
  if (E->isTypeOperand()) { // typeid(int)
    E->setTypeOperandSourceInfo(
        GetTypeSourceInfo());
    return;
  }

  // typeid(42+2)
  E->setExprOperand(Record.readSubExpr());
}

void ASTStmtReader::VisitCXXThisExpr(CXXThisExpr *E) {
  VisitExpr(E);
  E->setLocation(ReadSourceLocation());
  E->setImplicit(Record.readInt());
}

void ASTStmtReader::VisitCXXThrowExpr(CXXThrowExpr *E) {
  VisitExpr(E);
  E->CXXThrowExprBits.ThrowLoc = ReadSourceLocation();
  E->Operand = Record.readSubExpr();
  E->CXXThrowExprBits.IsThrownVariableInScope = Record.readInt();
}

void ASTStmtReader::VisitCXXDefaultArgExpr(CXXDefaultArgExpr *E) {
  VisitExpr(E);
  E->Param = ReadDeclAs<ParmVarDecl>();
  E->CXXDefaultArgExprBits.Loc = ReadSourceLocation();
}

void ASTStmtReader::VisitCXXDefaultInitExpr(CXXDefaultInitExpr *E) {
  VisitExpr(E);
  E->Field = ReadDeclAs<FieldDecl>();
  E->CXXDefaultInitExprBits.Loc = ReadSourceLocation();
}

void ASTStmtReader::VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E) {
  VisitExpr(E);
  E->setTemporary(Record.readCXXTemporary());
  E->setSubExpr(Record.readSubExpr());
}

void ASTStmtReader::VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr *E) {
  VisitExpr(E);
  E->TypeInfo = GetTypeSourceInfo();
  E->CXXScalarValueInitExprBits.RParenLoc = ReadSourceLocation();
}

void ASTStmtReader::VisitCXXNewExpr(CXXNewExpr *E) {
  VisitExpr(E);

  bool IsArray = Record.readInt();
  bool HasInit = Record.readInt();
  unsigned NumPlacementArgs = Record.readInt();
  bool IsParenTypeId = Record.readInt();

  E->CXXNewExprBits.IsGlobalNew = Record.readInt();
  E->CXXNewExprBits.ShouldPassAlignment = Record.readInt();
  E->CXXNewExprBits.UsualArrayDeleteWantsSize = Record.readInt();
  E->CXXNewExprBits.StoredInitializationStyle = Record.readInt();

  assert((IsArray == E->isArray()) && "Wrong IsArray!");
  assert((HasInit == E->hasInitializer()) && "Wrong HasInit!");
  assert((NumPlacementArgs == E->getNumPlacementArgs()) &&
         "Wrong NumPlacementArgs!");
  assert((IsParenTypeId == E->isParenTypeId()) && "Wrong IsParenTypeId!");
  (void)IsArray;
  (void)HasInit;
  (void)NumPlacementArgs;

  E->setOperatorNew(ReadDeclAs<FunctionDecl>());
  E->setOperatorDelete(ReadDeclAs<FunctionDecl>());
  E->AllocatedTypeInfo = GetTypeSourceInfo();
  if (IsParenTypeId)
    E->getTrailingObjects<SourceRange>()[0] = ReadSourceRange();
  E->Range = ReadSourceRange();
  E->DirectInitRange = ReadSourceRange();

  // Install all the subexpressions.
  for (CXXNewExpr::raw_arg_iterator I = E->raw_arg_begin(),
                                    N = E->raw_arg_end();
       I != N; ++I)
    *I = Record.readSubStmt();
}

void ASTStmtReader::VisitCXXDeleteExpr(CXXDeleteExpr *E) {
  VisitExpr(E);
  E->CXXDeleteExprBits.GlobalDelete = Record.readInt();
  E->CXXDeleteExprBits.ArrayForm = Record.readInt();
  E->CXXDeleteExprBits.ArrayFormAsWritten = Record.readInt();
  E->CXXDeleteExprBits.UsualArrayDeleteWantsSize = Record.readInt();
  E->OperatorDelete = ReadDeclAs<FunctionDecl>();
  E->Argument = Record.readSubExpr();
  E->CXXDeleteExprBits.Loc = ReadSourceLocation();
}

void ASTStmtReader::VisitCXXPseudoDestructorExpr(CXXPseudoDestructorExpr *E) {
  VisitExpr(E);

  E->Base = Record.readSubExpr();
  E->IsArrow = Record.readInt();
  E->OperatorLoc = ReadSourceLocation();
  E->QualifierLoc = Record.readNestedNameSpecifierLoc();
  E->ScopeType = GetTypeSourceInfo();
  E->ColonColonLoc = ReadSourceLocation();
  E->TildeLoc = ReadSourceLocation();

  IdentifierInfo *II = Record.getIdentifierInfo();
  if (II)
    E->setDestroyedType(II, ReadSourceLocation());
  else
    E->setDestroyedType(GetTypeSourceInfo());
}

void ASTStmtReader::VisitExprWithCleanups(ExprWithCleanups *E) {
  VisitExpr(E);

  unsigned NumObjects = Record.readInt();
  assert(NumObjects == E->getNumObjects());
  for (unsigned i = 0; i != NumObjects; ++i)
    E->getTrailingObjects<BlockDecl *>()[i] =
        ReadDeclAs<BlockDecl>();

  E->ExprWithCleanupsBits.CleanupsHaveSideEffects = Record.readInt();
  E->SubExpr = Record.readSubExpr();
}

void ASTStmtReader::VisitCXXDependentScopeMemberExpr(
    CXXDependentScopeMemberExpr *E) {
  VisitExpr(E);

  bool HasTemplateKWAndArgsInfo = Record.readInt();
  unsigned NumTemplateArgs = Record.readInt();
  bool HasFirstQualifierFoundInScope = Record.readInt();

  assert((HasTemplateKWAndArgsInfo == E->hasTemplateKWAndArgsInfo()) &&
         "Wrong HasTemplateKWAndArgsInfo!");
  assert(
      (HasFirstQualifierFoundInScope == E->hasFirstQualifierFoundInScope()) &&
      "Wrong HasFirstQualifierFoundInScope!");

  if (HasTemplateKWAndArgsInfo)
    ReadTemplateKWAndArgsInfo(
        *E->getTrailingObjects<ASTTemplateKWAndArgsInfo>(),
        E->getTrailingObjects<TemplateArgumentLoc>(), NumTemplateArgs);

  assert((NumTemplateArgs == E->getNumTemplateArgs()) &&
         "Wrong NumTemplateArgs!");

  E->CXXDependentScopeMemberExprBits.IsArrow = Record.readInt();
  E->CXXDependentScopeMemberExprBits.OperatorLoc = ReadSourceLocation();
  E->BaseType = Record.readType();
  E->QualifierLoc = Record.readNestedNameSpecifierLoc();
  E->Base = Record.readSubExpr();

  if (HasFirstQualifierFoundInScope)
    *E->getTrailingObjects<NamedDecl *>() = ReadDeclAs<NamedDecl>();

  ReadDeclarationNameInfo(E->MemberNameInfo);
}

void
ASTStmtReader::VisitDependentScopeDeclRefExpr(DependentScopeDeclRefExpr *E) {
  VisitExpr(E);

  if (Record.readInt()) // HasTemplateKWAndArgsInfo
    ReadTemplateKWAndArgsInfo(
        *E->getTrailingObjects<ASTTemplateKWAndArgsInfo>(),
        E->getTrailingObjects<TemplateArgumentLoc>(),
        /*NumTemplateArgs=*/Record.readInt());

  E->QualifierLoc = Record.readNestedNameSpecifierLoc();
  ReadDeclarationNameInfo(E->NameInfo);
}

void
ASTStmtReader::VisitCXXUnresolvedConstructExpr(CXXUnresolvedConstructExpr *E) {
  VisitExpr(E);
  assert(Record.peekInt() == E->arg_size() &&
         "Read wrong record during creation ?");
  Record.skipInts(1);
  for (unsigned I = 0, N = E->arg_size(); I != N; ++I)
    E->setArg(I, Record.readSubExpr());
  E->TSI = GetTypeSourceInfo();
  E->setLParenLoc(ReadSourceLocation());
  E->setRParenLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitOverloadExpr(OverloadExpr *E) {
  VisitExpr(E);

  unsigned NumResults = Record.readInt();
  bool HasTemplateKWAndArgsInfo = Record.readInt();
  assert((E->getNumDecls() == NumResults) && "Wrong NumResults!");
  assert((E->hasTemplateKWAndArgsInfo() == HasTemplateKWAndArgsInfo) &&
         "Wrong HasTemplateKWAndArgsInfo!");

  if (HasTemplateKWAndArgsInfo) {
    unsigned NumTemplateArgs = Record.readInt();
    ReadTemplateKWAndArgsInfo(*E->getTrailingASTTemplateKWAndArgsInfo(),
                              E->getTrailingTemplateArgumentLoc(),
                              NumTemplateArgs);
    assert((E->getNumTemplateArgs() == NumTemplateArgs) &&
           "Wrong NumTemplateArgs!");
  }

  UnresolvedSet<8> Decls;
  for (unsigned I = 0; I != NumResults; ++I) {
    auto *D = ReadDeclAs<NamedDecl>();
    auto AS = (AccessSpecifier)Record.readInt();
    Decls.addDecl(D, AS);
  }

  DeclAccessPair *Results = E->getTrailingResults();
  UnresolvedSetIterator Iter = Decls.begin();
  for (unsigned I = 0; I != NumResults; ++I) {
    Results[I] = (Iter + I).getPair();
  }

  ReadDeclarationNameInfo(E->NameInfo);
  E->QualifierLoc = Record.readNestedNameSpecifierLoc();
}

void ASTStmtReader::VisitUnresolvedMemberExpr(UnresolvedMemberExpr *E) {
  VisitOverloadExpr(E);
  E->UnresolvedMemberExprBits.IsArrow = Record.readInt();
  E->UnresolvedMemberExprBits.HasUnresolvedUsing = Record.readInt();
  E->Base = Record.readSubExpr();
  E->BaseType = Record.readType();
  E->OperatorLoc = ReadSourceLocation();
}

void ASTStmtReader::VisitUnresolvedLookupExpr(UnresolvedLookupExpr *E) {
  VisitOverloadExpr(E);
  E->UnresolvedLookupExprBits.RequiresADL = Record.readInt();
  E->UnresolvedLookupExprBits.Overloaded = Record.readInt();
  E->NamingClass = ReadDeclAs<CXXRecordDecl>();
}

void ASTStmtReader::VisitTypeTraitExpr(TypeTraitExpr *E) {
  VisitExpr(E);
  E->TypeTraitExprBits.NumArgs = Record.readInt();
  E->TypeTraitExprBits.Kind = Record.readInt();
  E->TypeTraitExprBits.Value = Record.readInt();
  SourceRange Range = ReadSourceRange();
  E->Loc = Range.getBegin();
  E->RParenLoc = Range.getEnd();

  auto **Args = E->getTrailingObjects<TypeSourceInfo *>();
  for (unsigned I = 0, N = E->getNumArgs(); I != N; ++I)
    Args[I] = GetTypeSourceInfo();
}

void ASTStmtReader::VisitArrayTypeTraitExpr(ArrayTypeTraitExpr *E) {
  VisitExpr(E);
  E->ATT = (ArrayTypeTrait)Record.readInt();
  E->Value = (unsigned int)Record.readInt();
  SourceRange Range = ReadSourceRange();
  E->Loc = Range.getBegin();
  E->RParen = Range.getEnd();
  E->QueriedType = GetTypeSourceInfo();
  E->Dimension = Record.readSubExpr();
}

void ASTStmtReader::VisitExpressionTraitExpr(ExpressionTraitExpr *E) {
  VisitExpr(E);
  E->ET = (ExpressionTrait)Record.readInt();
  E->Value = (bool)Record.readInt();
  SourceRange Range = ReadSourceRange();
  E->QueriedExpression = Record.readSubExpr();
  E->Loc = Range.getBegin();
  E->RParen = Range.getEnd();
}

void ASTStmtReader::VisitCXXNoexceptExpr(CXXNoexceptExpr *E) {
  VisitExpr(E);
  E->CXXNoexceptExprBits.Value = Record.readInt();
  E->Range = ReadSourceRange();
  E->Operand = Record.readSubExpr();
}

void ASTStmtReader::VisitPackExpansionExpr(PackExpansionExpr *E) {
  VisitExpr(E);
  E->EllipsisLoc = ReadSourceLocation();
  E->NumExpansions = Record.readInt();
  E->Pattern = Record.readSubExpr();
}

void ASTStmtReader::VisitSizeOfPackExpr(SizeOfPackExpr *E) {
  VisitExpr(E);
  unsigned NumPartialArgs = Record.readInt();
  E->OperatorLoc = ReadSourceLocation();
  E->PackLoc = ReadSourceLocation();
  E->RParenLoc = ReadSourceLocation();
  E->Pack = Record.readDeclAs<NamedDecl>();
  if (E->isPartiallySubstituted()) {
    assert(E->Length == NumPartialArgs);
    for (auto *I = E->getTrailingObjects<TemplateArgument>(),
              *E = I + NumPartialArgs;
         I != E; ++I)
      new (I) TemplateArgument(Record.readTemplateArgument());
  } else if (!E->isValueDependent()) {
    E->Length = Record.readInt();
  }
}

void ASTStmtReader::VisitSubstNonTypeTemplateParmExpr(
                                              SubstNonTypeTemplateParmExpr *E) {
  VisitExpr(E);
  E->Param = ReadDeclAs<NonTypeTemplateParmDecl>();
  E->SubstNonTypeTemplateParmExprBits.NameLoc = ReadSourceLocation();
  E->Replacement = Record.readSubExpr();
}

void ASTStmtReader::VisitSubstNonTypeTemplateParmPackExpr(
                                          SubstNonTypeTemplateParmPackExpr *E) {
  VisitExpr(E);
  E->Param = ReadDeclAs<NonTypeTemplateParmDecl>();
  TemplateArgument ArgPack = Record.readTemplateArgument();
  if (ArgPack.getKind() != TemplateArgument::Pack)
    return;

  E->Arguments = ArgPack.pack_begin();
  E->NumArguments = ArgPack.pack_size();
  E->NameLoc = ReadSourceLocation();
}

void ASTStmtReader::VisitFunctionParmPackExpr(FunctionParmPackExpr *E) {
  VisitExpr(E);
  E->NumParameters = Record.readInt();
  E->ParamPack = ReadDeclAs<ParmVarDecl>();
  E->NameLoc = ReadSourceLocation();
  auto **Parms = E->getTrailingObjects<ParmVarDecl *>();
  for (unsigned i = 0, n = E->NumParameters; i != n; ++i)
    Parms[i] = ReadDeclAs<ParmVarDecl>();
}

void ASTStmtReader::VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *E) {
  VisitExpr(E);
  E->State = Record.readSubExpr();
  auto *VD = ReadDeclAs<ValueDecl>();
  unsigned ManglingNumber = Record.readInt();
  E->setExtendingDecl(VD, ManglingNumber);
}

void ASTStmtReader::VisitCXXFoldExpr(CXXFoldExpr *E) {
  VisitExpr(E);
  E->LParenLoc = ReadSourceLocation();
  E->EllipsisLoc = ReadSourceLocation();
  E->RParenLoc = ReadSourceLocation();
  E->SubExprs[0] = Record.readSubExpr();
  E->SubExprs[1] = Record.readSubExpr();
  E->Opcode = (BinaryOperatorKind)Record.readInt();
}

void ASTStmtReader::VisitOpaqueValueExpr(OpaqueValueExpr *E) {
  VisitExpr(E);
  E->SourceExpr = Record.readSubExpr();
  E->OpaqueValueExprBits.Loc = ReadSourceLocation();
  E->setIsUnique(Record.readInt());
}

void ASTStmtReader::VisitTypoExpr(TypoExpr *E) {
  llvm_unreachable("Cannot read TypoExpr nodes");
}

//===----------------------------------------------------------------------===//
// Microsoft Expressions and Statements
//===----------------------------------------------------------------------===//
void ASTStmtReader::VisitMSPropertyRefExpr(MSPropertyRefExpr *E) {
  VisitExpr(E);
  E->IsArrow = (Record.readInt() != 0);
  E->BaseExpr = Record.readSubExpr();
  E->QualifierLoc = Record.readNestedNameSpecifierLoc();
  E->MemberLoc = ReadSourceLocation();
  E->TheDecl = ReadDeclAs<MSPropertyDecl>();
}

void ASTStmtReader::VisitMSPropertySubscriptExpr(MSPropertySubscriptExpr *E) {
  VisitExpr(E);
  E->setBase(Record.readSubExpr());
  E->setIdx(Record.readSubExpr());
  E->setRBracketLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitCXXUuidofExpr(CXXUuidofExpr *E) {
  VisitExpr(E);
  E->setSourceRange(ReadSourceRange());
  std::string UuidStr = ReadString();
  E->setUuidStr(StringRef(UuidStr).copy(Record.getContext()));
  if (E->isTypeOperand()) { // __uuidof(ComType)
    E->setTypeOperandSourceInfo(
        GetTypeSourceInfo());
    return;
  }

  // __uuidof(expr)
  E->setExprOperand(Record.readSubExpr());
}

void ASTStmtReader::VisitSEHLeaveStmt(SEHLeaveStmt *S) {
  VisitStmt(S);
  S->setLeaveLoc(ReadSourceLocation());
}

void ASTStmtReader::VisitSEHExceptStmt(SEHExceptStmt *S) {
  VisitStmt(S);
  S->Loc = ReadSourceLocation();
  S->Children[SEHExceptStmt::FILTER_EXPR] = Record.readSubStmt();
  S->Children[SEHExceptStmt::BLOCK] = Record.readSubStmt();
}

void ASTStmtReader::VisitSEHFinallyStmt(SEHFinallyStmt *S) {
  VisitStmt(S);
  S->Loc = ReadSourceLocation();
  S->Block = Record.readSubStmt();
}

void ASTStmtReader::VisitSEHTryStmt(SEHTryStmt *S) {
  VisitStmt(S);
  S->IsCXXTry = Record.readInt();
  S->TryLoc = ReadSourceLocation();
  S->Children[SEHTryStmt::TRY] = Record.readSubStmt();
  S->Children[SEHTryStmt::HANDLER] = Record.readSubStmt();
}

//===----------------------------------------------------------------------===//
// CUDA Expressions and Statements
//===----------------------------------------------------------------------===//

void ASTStmtReader::VisitCUDAKernelCallExpr(CUDAKernelCallExpr *E) {
  VisitCallExpr(E);
  E->setConfig(cast<CallExpr>(Record.readSubExpr()));
}

//===----------------------------------------------------------------------===//
// OpenCL Expressions and Statements.
//===----------------------------------------------------------------------===//
void ASTStmtReader::VisitAsTypeExpr(AsTypeExpr *E) {
  VisitExpr(E);
  E->BuiltinLoc = ReadSourceLocation();
  E->RParenLoc = ReadSourceLocation();
  E->SrcExpr = Record.readSubExpr();
}

//===----------------------------------------------------------------------===//
// OpenMP Directives.
//===----------------------------------------------------------------------===//

void ASTStmtReader::VisitOMPExecutableDirective(OMPExecutableDirective *E) {
  E->setLocStart(ReadSourceLocation());
  E->setLocEnd(ReadSourceLocation());
  OMPClauseReader ClauseReader(Record);
  SmallVector<OMPClause *, 5> Clauses;
  for (unsigned i = 0; i < E->getNumClauses(); ++i)
    Clauses.push_back(ClauseReader.readClause());
  E->setClauses(Clauses);
  if (E->hasAssociatedStmt())
    E->setAssociatedStmt(Record.readSubStmt());
}

void ASTStmtReader::VisitOMPLoopDirective(OMPLoopDirective *D) {
  VisitStmt(D);
  // Two fields (NumClauses and CollapsedNum) were read in ReadStmtFromStream.
  Record.skipInts(2);
  VisitOMPExecutableDirective(D);
  D->setIterationVariable(Record.readSubExpr());
  D->setLastIteration(Record.readSubExpr());
  D->setCalcLastIteration(Record.readSubExpr());
  D->setPreCond(Record.readSubExpr());
  D->setCond(Record.readSubExpr());
  D->setInit(Record.readSubExpr());
  D->setInc(Record.readSubExpr());
  D->setPreInits(Record.readSubStmt());
  if (isOpenMPWorksharingDirective(D->getDirectiveKind()) ||
      isOpenMPTaskLoopDirective(D->getDirectiveKind()) ||
      isOpenMPDistributeDirective(D->getDirectiveKind())) {
    D->setIsLastIterVariable(Record.readSubExpr());
    D->setLowerBoundVariable(Record.readSubExpr());
    D->setUpperBoundVariable(Record.readSubExpr());
    D->setStrideVariable(Record.readSubExpr());
    D->setEnsureUpperBound(Record.readSubExpr());
    D->setNextLowerBound(Record.readSubExpr());
    D->setNextUpperBound(Record.readSubExpr());
    D->setNumIterations(Record.readSubExpr());
  }
  if (isOpenMPLoopBoundSharingDirective(D->getDirectiveKind())) {
    D->setPrevLowerBoundVariable(Record.readSubExpr());
    D->setPrevUpperBoundVariable(Record.readSubExpr());
    D->setDistInc(Record.readSubExpr());
    D->setPrevEnsureUpperBound(Record.readSubExpr());
    D->setCombinedLowerBoundVariable(Record.readSubExpr());
    D->setCombinedUpperBoundVariable(Record.readSubExpr());
    D->setCombinedEnsureUpperBound(Record.readSubExpr());
    D->setCombinedInit(Record.readSubExpr());
    D->setCombinedCond(Record.readSubExpr());
    D->setCombinedNextLowerBound(Record.readSubExpr());
    D->setCombinedNextUpperBound(Record.readSubExpr());
    D->setCombinedDistCond(Record.readSubExpr());
    D->setCombinedParForInDistCond(Record.readSubExpr());
  }
  SmallVector<Expr *, 4> Sub;
  unsigned CollapsedNum = D->getCollapsedNumber();
  Sub.reserve(CollapsedNum);
  for (unsigned i = 0; i < CollapsedNum; ++i)
    Sub.push_back(Record.readSubExpr());
  D->setCounters(Sub);
  Sub.clear();
  for (unsigned i = 0; i < CollapsedNum; ++i)
    Sub.push_back(Record.readSubExpr());
  D->setPrivateCounters(Sub);
  Sub.clear();
  for (unsigned i = 0; i < CollapsedNum; ++i)
    Sub.push_back(Record.readSubExpr());
  D->setInits(Sub);
  Sub.clear();
  for (unsigned i = 0; i < CollapsedNum; ++i)
    Sub.push_back(Record.readSubExpr());
  D->setUpdates(Sub);
  Sub.clear();
  for (unsigned i = 0; i < CollapsedNum; ++i)
    Sub.push_back(Record.readSubExpr());
  D->setFinals(Sub);
}

void ASTStmtReader::VisitOMPParallelDirective(OMPParallelDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
  D->setHasCancel(Record.readInt());
}

void ASTStmtReader::VisitOMPSimdDirective(OMPSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPForDirective(OMPForDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readInt());
}

void ASTStmtReader::VisitOMPForSimdDirective(OMPForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPSectionsDirective(OMPSectionsDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
  D->setHasCancel(Record.readInt());
}

void ASTStmtReader::VisitOMPSectionDirective(OMPSectionDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->setHasCancel(Record.readInt());
}

void ASTStmtReader::VisitOMPSingleDirective(OMPSingleDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPMasterDirective(OMPMasterDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPCriticalDirective(OMPCriticalDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
  ReadDeclarationNameInfo(D->DirName);
}

void ASTStmtReader::VisitOMPParallelForDirective(OMPParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readInt());
}

void ASTStmtReader::VisitOMPParallelForSimdDirective(
    OMPParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPParallelSectionsDirective(
    OMPParallelSectionsDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
  D->setHasCancel(Record.readInt());
}

void ASTStmtReader::VisitOMPTaskDirective(OMPTaskDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
  D->setHasCancel(Record.readInt());
}

void ASTStmtReader::VisitOMPTaskyieldDirective(OMPTaskyieldDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPBarrierDirective(OMPBarrierDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTaskwaitDirective(OMPTaskwaitDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTaskgroupDirective(OMPTaskgroupDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
  D->setReductionRef(Record.readSubExpr());
}

void ASTStmtReader::VisitOMPFlushDirective(OMPFlushDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPOrderedDirective(OMPOrderedDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPAtomicDirective(OMPAtomicDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
  D->setX(Record.readSubExpr());
  D->setV(Record.readSubExpr());
  D->setExpr(Record.readSubExpr());
  D->setUpdateExpr(Record.readSubExpr());
  D->IsXLHSInRHSPart = Record.readInt() != 0;
  D->IsPostfixUpdate = Record.readInt() != 0;
}

void ASTStmtReader::VisitOMPTargetDirective(OMPTargetDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTargetDataDirective(OMPTargetDataDirective *D) {
  VisitStmt(D);
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTargetEnterDataDirective(
    OMPTargetEnterDataDirective *D) {
  VisitStmt(D);
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTargetExitDataDirective(
    OMPTargetExitDataDirective *D) {
  VisitStmt(D);
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTargetParallelDirective(
    OMPTargetParallelDirective *D) {
  VisitStmt(D);
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTargetParallelForDirective(
    OMPTargetParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readInt());
}

void ASTStmtReader::VisitOMPTeamsDirective(OMPTeamsDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPCancellationPointDirective(
    OMPCancellationPointDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->setCancelRegion(static_cast<OpenMPDirectiveKind>(Record.readInt()));
}

void ASTStmtReader::VisitOMPCancelDirective(OMPCancelDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
  D->setCancelRegion(static_cast<OpenMPDirectiveKind>(Record.readInt()));
}

void ASTStmtReader::VisitOMPTaskLoopDirective(OMPTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTaskLoopSimdDirective(OMPTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPDistributeDirective(OMPDistributeDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTargetUpdateDirective(OMPTargetUpdateDirective *D) {
  VisitStmt(D);
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPDistributeParallelForDirective(
    OMPDistributeParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readInt());
}

void ASTStmtReader::VisitOMPDistributeParallelForSimdDirective(
    OMPDistributeParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPDistributeSimdDirective(
    OMPDistributeSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTargetParallelForSimdDirective(
    OMPTargetParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTargetSimdDirective(OMPTargetSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTeamsDistributeDirective(
    OMPTeamsDistributeDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTeamsDistributeSimdDirective(
    OMPTeamsDistributeSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTeamsDistributeParallelForSimdDirective(
    OMPTeamsDistributeParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTeamsDistributeParallelForDirective(
    OMPTeamsDistributeParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readInt());
}

void ASTStmtReader::VisitOMPTargetTeamsDirective(OMPTargetTeamsDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTargetTeamsDistributeDirective(
    OMPTargetTeamsDistributeDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTargetTeamsDistributeParallelForDirective(
    OMPTargetTeamsDistributeParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readInt());
}

void ASTStmtReader::VisitOMPTargetTeamsDistributeParallelForSimdDirective(
    OMPTargetTeamsDistributeParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTargetTeamsDistributeSimdDirective(
    OMPTargetTeamsDistributeSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

//===----------------------------------------------------------------------===//
// ASTReader Implementation
//===----------------------------------------------------------------------===//

Stmt *ASTReader::ReadStmt(ModuleFile &F) {
  switch (ReadingKind) {
  case Read_None:
    llvm_unreachable("should not call this when not reading anything");
  case Read_Decl:
  case Read_Type:
    return ReadStmtFromStream(F);
  case Read_Stmt:
    return ReadSubStmt();
  }

  llvm_unreachable("ReadingKind not set ?");
}

Expr *ASTReader::ReadExpr(ModuleFile &F) {
  return cast_or_null<Expr>(ReadStmt(F));
}

Expr *ASTReader::ReadSubExpr() {
  return cast_or_null<Expr>(ReadSubStmt());
}

// Within the bitstream, expressions are stored in Reverse Polish
// Notation, with each of the subexpressions preceding the
// expression they are stored in. Subexpressions are stored from last to first.
// To evaluate expressions, we continue reading expressions and placing them on
// the stack, with expressions having operands removing those operands from the
// stack. Evaluation terminates when we see a STMT_STOP record, and
// the single remaining expression on the stack is our result.
Stmt *ASTReader::ReadStmtFromStream(ModuleFile &F) {
  ReadingKindTracker ReadingKind(Read_Stmt, *this);
  llvm::BitstreamCursor &Cursor = F.DeclsCursor;

  // Map of offset to previously deserialized stmt. The offset points
  // just after the stmt record.
  llvm::DenseMap<uint64_t, Stmt *> StmtEntries;

#ifndef NDEBUG
  unsigned PrevNumStmts = StmtStack.size();
#endif

  ASTRecordReader Record(*this, F);
  ASTStmtReader Reader(Record, Cursor);
  Stmt::EmptyShell Empty;

  while (true) {
    llvm::BitstreamEntry Entry = Cursor.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock: // Handled for us already.
    case llvm::BitstreamEntry::Error:
      Error("malformed block record in AST file");
      return nullptr;
    case llvm::BitstreamEntry::EndBlock:
      goto Done;
    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    ASTContext &Context = getContext();
    Stmt *S = nullptr;
    bool Finished = false;
    bool IsStmtReference = false;
    switch ((StmtCode)Record.readRecord(Cursor, Entry.ID)) {
    case STMT_STOP:
      Finished = true;
      break;

    case STMT_REF_PTR:
      IsStmtReference = true;
      assert(StmtEntries.find(Record[0]) != StmtEntries.end() &&
             "No stmt was recorded for this offset reference!");
      S = StmtEntries[Record.readInt()];
      break;

    case STMT_NULL_PTR:
      S = nullptr;
      break;

    case STMT_NULL:
      S = new (Context) NullStmt(Empty);
      break;

    case STMT_COMPOUND:
      S = CompoundStmt::CreateEmpty(
          Context, /*NumStmts=*/Record[ASTStmtReader::NumStmtFields]);
      break;

    case STMT_CASE:
      S = CaseStmt::CreateEmpty(
          Context,
          /*CaseStmtIsGNURange*/ Record[ASTStmtReader::NumStmtFields + 3]);
      break;

    case STMT_DEFAULT:
      S = new (Context) DefaultStmt(Empty);
      break;

    case STMT_LABEL:
      S = new (Context) LabelStmt(Empty);
      break;

    case STMT_ATTRIBUTED:
      S = AttributedStmt::CreateEmpty(
        Context,
        /*NumAttrs*/Record[ASTStmtReader::NumStmtFields]);
      break;

    case STMT_IF:
      S = IfStmt::CreateEmpty(
          Context,
          /* HasElse=*/Record[ASTStmtReader::NumStmtFields + 1],
          /* HasVar=*/Record[ASTStmtReader::NumStmtFields + 2],
          /* HasInit=*/Record[ASTStmtReader::NumStmtFields + 3]);
      break;

    case STMT_SWITCH:
      S = SwitchStmt::CreateEmpty(
          Context,
          /* HasInit=*/Record[ASTStmtReader::NumStmtFields],
          /* HasVar=*/Record[ASTStmtReader::NumStmtFields + 1]);
      break;

    case STMT_WHILE:
      S = WhileStmt::CreateEmpty(
          Context,
          /* HasVar=*/Record[ASTStmtReader::NumStmtFields]);
      break;

    case STMT_DO:
      S = new (Context) DoStmt(Empty);
      break;

    case STMT_FOR:
      S = new (Context) ForStmt(Empty);
      break;

    case STMT_GOTO:
      S = new (Context) GotoStmt(Empty);
      break;

    case STMT_INDIRECT_GOTO:
      S = new (Context) IndirectGotoStmt(Empty);
      break;

    case STMT_CONTINUE:
      S = new (Context) ContinueStmt(Empty);
      break;

    case STMT_BREAK:
      S = new (Context) BreakStmt(Empty);
      break;

    case STMT_RETURN:
      S = ReturnStmt::CreateEmpty(
          Context, /* HasNRVOCandidate=*/Record[ASTStmtReader::NumStmtFields]);
      break;

    case STMT_DECL:
      S = new (Context) DeclStmt(Empty);
      break;

    case STMT_GCCASM:
      S = new (Context) GCCAsmStmt(Empty);
      break;

    case STMT_MSASM:
      S = new (Context) MSAsmStmt(Empty);
      break;

    case STMT_CAPTURED:
      S = CapturedStmt::CreateDeserialized(
          Context, Record[ASTStmtReader::NumStmtFields]);
      break;

    case EXPR_CONSTANT:
      S = new (Context) ConstantExpr(Empty);
      break;

    case EXPR_PREDEFINED:
      S = PredefinedExpr::CreateEmpty(
          Context,
          /*HasFunctionName*/ Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_DECL_REF:
      S = DeclRefExpr::CreateEmpty(
        Context,
        /*HasQualifier=*/Record[ASTStmtReader::NumExprFields],
        /*HasFoundDecl=*/Record[ASTStmtReader::NumExprFields + 1],
        /*HasTemplateKWAndArgsInfo=*/Record[ASTStmtReader::NumExprFields + 2],
        /*NumTemplateArgs=*/Record[ASTStmtReader::NumExprFields + 2] ?
          Record[ASTStmtReader::NumExprFields + 5] : 0);
      break;

    case EXPR_INTEGER_LITERAL:
      S = IntegerLiteral::Create(Context, Empty);
      break;

    case EXPR_FLOATING_LITERAL:
      S = FloatingLiteral::Create(Context, Empty);
      break;

    case EXPR_IMAGINARY_LITERAL:
      S = new (Context) ImaginaryLiteral(Empty);
      break;

    case EXPR_STRING_LITERAL:
      S = StringLiteral::CreateEmpty(
          Context,
          /* NumConcatenated=*/Record[ASTStmtReader::NumExprFields],
          /* Length=*/Record[ASTStmtReader::NumExprFields + 1],
          /* CharByteWidth=*/Record[ASTStmtReader::NumExprFields + 2]);
      break;

    case EXPR_CHARACTER_LITERAL:
      S = new (Context) CharacterLiteral(Empty);
      break;

    case EXPR_PAREN:
      S = new (Context) ParenExpr(Empty);
      break;

    case EXPR_PAREN_LIST:
      S = ParenListExpr::CreateEmpty(
          Context,
          /* NumExprs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_UNARY_OPERATOR:
      S = new (Context) UnaryOperator(Empty);
      break;

    case EXPR_OFFSETOF:
      S = OffsetOfExpr::CreateEmpty(Context,
                                    Record[ASTStmtReader::NumExprFields],
                                    Record[ASTStmtReader::NumExprFields + 1]);
      break;

    case EXPR_SIZEOF_ALIGN_OF:
      S = new (Context) UnaryExprOrTypeTraitExpr(Empty);
      break;

    case EXPR_ARRAY_SUBSCRIPT:
      S = new (Context) ArraySubscriptExpr(Empty);
      break;

    case EXPR_OMP_ARRAY_SECTION:
      S = new (Context) OMPArraySectionExpr(Empty);
      break;

    case EXPR_CALL:
      S = CallExpr::CreateEmpty(
          Context, /*NumArgs=*/Record[ASTStmtReader::NumExprFields], Empty);
      break;

    case EXPR_MEMBER: {
      // We load everything here and fully initialize it at creation.
      // That way we can use MemberExpr::Create and don't have to duplicate its
      // logic with a MemberExpr::CreateEmpty.

      assert(Record.getIdx() == 0);
      NestedNameSpecifierLoc QualifierLoc;
      if (Record.readInt()) { // HasQualifier.
        QualifierLoc = Record.readNestedNameSpecifierLoc();
      }

      SourceLocation TemplateKWLoc;
      TemplateArgumentListInfo ArgInfo;
      bool HasTemplateKWAndArgsInfo = Record.readInt();
      if (HasTemplateKWAndArgsInfo) {
        TemplateKWLoc = Record.readSourceLocation();
        unsigned NumTemplateArgs = Record.readInt();
        ArgInfo.setLAngleLoc(Record.readSourceLocation());
        ArgInfo.setRAngleLoc(Record.readSourceLocation());
        for (unsigned i = 0; i != NumTemplateArgs; ++i)
          ArgInfo.addArgument(Record.readTemplateArgumentLoc());
      }

      bool HadMultipleCandidates = Record.readInt();

      auto *FoundD = Record.readDeclAs<NamedDecl>();
      auto AS = (AccessSpecifier)Record.readInt();
      DeclAccessPair FoundDecl = DeclAccessPair::make(FoundD, AS);

      QualType T = Record.readType();
      auto VK = static_cast<ExprValueKind>(Record.readInt());
      auto OK = static_cast<ExprObjectKind>(Record.readInt());
      Expr *Base = ReadSubExpr();
      auto *MemberD = Record.readDeclAs<ValueDecl>();
      SourceLocation MemberLoc = Record.readSourceLocation();
      DeclarationNameInfo MemberNameInfo(MemberD->getDeclName(), MemberLoc);
      bool IsArrow = Record.readInt();
      SourceLocation OperatorLoc = Record.readSourceLocation();

      S = MemberExpr::Create(Context, Base, IsArrow, OperatorLoc, QualifierLoc,
                             TemplateKWLoc, MemberD, FoundDecl, MemberNameInfo,
                             HasTemplateKWAndArgsInfo ? &ArgInfo : nullptr, T,
                             VK, OK);
      Record.readDeclarationNameLoc(cast<MemberExpr>(S)->MemberDNLoc,
                                    MemberD->getDeclName());
      if (HadMultipleCandidates)
        cast<MemberExpr>(S)->setHadMultipleCandidates(true);
      break;
    }

    case EXPR_BINARY_OPERATOR:
      S = new (Context) BinaryOperator(Empty);
      break;

    case EXPR_COMPOUND_ASSIGN_OPERATOR:
      S = new (Context) CompoundAssignOperator(Empty);
      break;

    case EXPR_CONDITIONAL_OPERATOR:
      S = new (Context) ConditionalOperator(Empty);
      break;

    case EXPR_BINARY_CONDITIONAL_OPERATOR:
      S = new (Context) BinaryConditionalOperator(Empty);
      break;

    case EXPR_IMPLICIT_CAST:
      S = ImplicitCastExpr::CreateEmpty(Context,
                       /*PathSize*/ Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CSTYLE_CAST:
      S = CStyleCastExpr::CreateEmpty(Context,
                       /*PathSize*/ Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_COMPOUND_LITERAL:
      S = new (Context) CompoundLiteralExpr(Empty);
      break;

    case EXPR_EXT_VECTOR_ELEMENT:
      S = new (Context) ExtVectorElementExpr(Empty);
      break;

    case EXPR_INIT_LIST:
      S = new (Context) InitListExpr(Empty);
      break;

    case EXPR_DESIGNATED_INIT:
      S = DesignatedInitExpr::CreateEmpty(Context,
                                     Record[ASTStmtReader::NumExprFields] - 1);

      break;

    case EXPR_DESIGNATED_INIT_UPDATE:
      S = new (Context) DesignatedInitUpdateExpr(Empty);
      break;

    case EXPR_IMPLICIT_VALUE_INIT:
      S = new (Context) ImplicitValueInitExpr(Empty);
      break;

    case EXPR_NO_INIT:
      S = new (Context) NoInitExpr(Empty);
      break;

    case EXPR_ARRAY_INIT_LOOP:
      S = new (Context) ArrayInitLoopExpr(Empty);
      break;

    case EXPR_ARRAY_INIT_INDEX:
      S = new (Context) ArrayInitIndexExpr(Empty);
      break;

    case EXPR_VA_ARG:
      S = new (Context) VAArgExpr(Empty);
      break;

    case EXPR_ADDR_LABEL:
      S = new (Context) AddrLabelExpr(Empty);
      break;

    case EXPR_STMT:
      S = new (Context) StmtExpr(Empty);
      break;

    case EXPR_CHOOSE:
      S = new (Context) ChooseExpr(Empty);
      break;

    case EXPR_GNU_NULL:
      S = new (Context) GNUNullExpr(Empty);
      break;

    case EXPR_SHUFFLE_VECTOR:
      S = new (Context) ShuffleVectorExpr(Empty);
      break;

    case EXPR_CONVERT_VECTOR:
      S = new (Context) ConvertVectorExpr(Empty);
      break;

    case EXPR_BLOCK:
      S = new (Context) BlockExpr(Empty);
      break;

    case EXPR_GENERIC_SELECTION:
      S = new (Context) GenericSelectionExpr(Empty);
      break;

    case EXPR_OBJC_STRING_LITERAL:
      S = new (Context) ObjCStringLiteral(Empty);
      break;

    case EXPR_OBJC_BOXED_EXPRESSION:
      S = new (Context) ObjCBoxedExpr(Empty);
      break;

    case EXPR_OBJC_ARRAY_LITERAL:
      S = ObjCArrayLiteral::CreateEmpty(Context,
                                        Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_OBJC_DICTIONARY_LITERAL:
      S = ObjCDictionaryLiteral::CreateEmpty(Context,
            Record[ASTStmtReader::NumExprFields],
            Record[ASTStmtReader::NumExprFields + 1]);
      break;

    case EXPR_OBJC_ENCODE:
      S = new (Context) ObjCEncodeExpr(Empty);
      break;

    case EXPR_OBJC_SELECTOR_EXPR:
      S = new (Context) ObjCSelectorExpr(Empty);
      break;

    case EXPR_OBJC_PROTOCOL_EXPR:
      S = new (Context) ObjCProtocolExpr(Empty);
      break;

    case EXPR_OBJC_IVAR_REF_EXPR:
      S = new (Context) ObjCIvarRefExpr(Empty);
      break;

    case EXPR_OBJC_PROPERTY_REF_EXPR:
      S = new (Context) ObjCPropertyRefExpr(Empty);
      break;

    case EXPR_OBJC_SUBSCRIPT_REF_EXPR:
      S = new (Context) ObjCSubscriptRefExpr(Empty);
      break;

    case EXPR_OBJC_KVC_REF_EXPR:
      llvm_unreachable("mismatching AST file");

    case EXPR_OBJC_MESSAGE_EXPR:
      S = ObjCMessageExpr::CreateEmpty(Context,
                                     Record[ASTStmtReader::NumExprFields],
                                     Record[ASTStmtReader::NumExprFields + 1]);
      break;

    case EXPR_OBJC_ISA:
      S = new (Context) ObjCIsaExpr(Empty);
      break;

    case EXPR_OBJC_INDIRECT_COPY_RESTORE:
      S = new (Context) ObjCIndirectCopyRestoreExpr(Empty);
      break;

    case EXPR_OBJC_BRIDGED_CAST:
      S = new (Context) ObjCBridgedCastExpr(Empty);
      break;

    case STMT_OBJC_FOR_COLLECTION:
      S = new (Context) ObjCForCollectionStmt(Empty);
      break;

    case STMT_OBJC_CATCH:
      S = new (Context) ObjCAtCatchStmt(Empty);
      break;

    case STMT_OBJC_FINALLY:
      S = new (Context) ObjCAtFinallyStmt(Empty);
      break;

    case STMT_OBJC_AT_TRY:
      S = ObjCAtTryStmt::CreateEmpty(Context,
                                     Record[ASTStmtReader::NumStmtFields],
                                     Record[ASTStmtReader::NumStmtFields + 1]);
      break;

    case STMT_OBJC_AT_SYNCHRONIZED:
      S = new (Context) ObjCAtSynchronizedStmt(Empty);
      break;

    case STMT_OBJC_AT_THROW:
      S = new (Context) ObjCAtThrowStmt(Empty);
      break;

    case STMT_OBJC_AUTORELEASE_POOL:
      S = new (Context) ObjCAutoreleasePoolStmt(Empty);
      break;

    case EXPR_OBJC_BOOL_LITERAL:
      S = new (Context) ObjCBoolLiteralExpr(Empty);
      break;

    case EXPR_OBJC_AVAILABILITY_CHECK:
      S = new (Context) ObjCAvailabilityCheckExpr(Empty);
      break;

    case STMT_SEH_LEAVE:
      S = new (Context) SEHLeaveStmt(Empty);
      break;

    case STMT_SEH_EXCEPT:
      S = new (Context) SEHExceptStmt(Empty);
      break;

    case STMT_SEH_FINALLY:
      S = new (Context) SEHFinallyStmt(Empty);
      break;

    case STMT_SEH_TRY:
      S = new (Context) SEHTryStmt(Empty);
      break;

    case STMT_CXX_CATCH:
      S = new (Context) CXXCatchStmt(Empty);
      break;

    case STMT_CXX_TRY:
      S = CXXTryStmt::Create(Context, Empty,
             /*NumHandlers=*/Record[ASTStmtReader::NumStmtFields]);
      break;

    case STMT_CXX_FOR_RANGE:
      S = new (Context) CXXForRangeStmt(Empty);
      break;

    case STMT_MS_DEPENDENT_EXISTS:
      S = new (Context) MSDependentExistsStmt(SourceLocation(), true,
                                              NestedNameSpecifierLoc(),
                                              DeclarationNameInfo(),
                                              nullptr);
      break;

    case STMT_OMP_PARALLEL_DIRECTIVE:
      S =
        OMPParallelDirective::CreateEmpty(Context,
                                          Record[ASTStmtReader::NumStmtFields],
                                          Empty);
      break;

    case STMT_OMP_SIMD_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPSimdDirective::CreateEmpty(Context, NumClauses,
                                        CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_FOR_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPForDirective::CreateEmpty(Context, NumClauses, CollapsedNum,
                                       Empty);
      break;
    }

    case STMT_OMP_FOR_SIMD_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPForSimdDirective::CreateEmpty(Context, NumClauses, CollapsedNum,
                                           Empty);
      break;
    }

    case STMT_OMP_SECTIONS_DIRECTIVE:
      S = OMPSectionsDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_SECTION_DIRECTIVE:
      S = OMPSectionDirective::CreateEmpty(Context, Empty);
      break;

    case STMT_OMP_SINGLE_DIRECTIVE:
      S = OMPSingleDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_MASTER_DIRECTIVE:
      S = OMPMasterDirective::CreateEmpty(Context, Empty);
      break;

    case STMT_OMP_CRITICAL_DIRECTIVE:
      S = OMPCriticalDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_PARALLEL_FOR_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPParallelForDirective::CreateEmpty(Context, NumClauses,
                                               CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_PARALLEL_FOR_SIMD_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPParallelForSimdDirective::CreateEmpty(Context, NumClauses,
                                                   CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_PARALLEL_SECTIONS_DIRECTIVE:
      S = OMPParallelSectionsDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TASK_DIRECTIVE:
      S = OMPTaskDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TASKYIELD_DIRECTIVE:
      S = OMPTaskyieldDirective::CreateEmpty(Context, Empty);
      break;

    case STMT_OMP_BARRIER_DIRECTIVE:
      S = OMPBarrierDirective::CreateEmpty(Context, Empty);
      break;

    case STMT_OMP_TASKWAIT_DIRECTIVE:
      S = OMPTaskwaitDirective::CreateEmpty(Context, Empty);
      break;

    case STMT_OMP_TASKGROUP_DIRECTIVE:
      S = OMPTaskgroupDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_FLUSH_DIRECTIVE:
      S = OMPFlushDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_ORDERED_DIRECTIVE:
      S = OMPOrderedDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_ATOMIC_DIRECTIVE:
      S = OMPAtomicDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_DIRECTIVE:
      S = OMPTargetDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_DATA_DIRECTIVE:
      S = OMPTargetDataDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_ENTER_DATA_DIRECTIVE:
      S = OMPTargetEnterDataDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_EXIT_DATA_DIRECTIVE:
      S = OMPTargetExitDataDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_PARALLEL_DIRECTIVE:
      S = OMPTargetParallelDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_PARALLEL_FOR_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetParallelForDirective::CreateEmpty(Context, NumClauses,
                                                     CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_UPDATE_DIRECTIVE:
      S = OMPTargetUpdateDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TEAMS_DIRECTIVE:
      S = OMPTeamsDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_CANCELLATION_POINT_DIRECTIVE:
      S = OMPCancellationPointDirective::CreateEmpty(Context, Empty);
      break;

    case STMT_OMP_CANCEL_DIRECTIVE:
      S = OMPCancelDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TASKLOOP_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTaskLoopDirective::CreateEmpty(Context, NumClauses, CollapsedNum,
                                            Empty);
      break;
    }

    case STMT_OMP_TASKLOOP_SIMD_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTaskLoopSimdDirective::CreateEmpty(Context, NumClauses,
                                                CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_DISTRIBUTE_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPDistributeDirective::CreateEmpty(Context, NumClauses, CollapsedNum,
                                              Empty);
      break;
    }

    case STMT_OMP_DISTRIBUTE_PARALLEL_FOR_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPDistributeParallelForDirective::CreateEmpty(Context, NumClauses,
                                                         CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_DISTRIBUTE_PARALLEL_FOR_SIMD_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPDistributeParallelForSimdDirective::CreateEmpty(Context, NumClauses,
                                                             CollapsedNum,
                                                             Empty);
      break;
    }

    case STMT_OMP_DISTRIBUTE_SIMD_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPDistributeSimdDirective::CreateEmpty(Context, NumClauses,
                                                  CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_PARALLEL_FOR_SIMD_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetParallelForSimdDirective::CreateEmpty(Context, NumClauses,
                                                         CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_SIMD_DIRECTIVE: {
      auto NumClauses = Record[ASTStmtReader::NumStmtFields];
      auto CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetSimdDirective::CreateEmpty(Context, NumClauses, CollapsedNum,
                                              Empty);
      break;
    }

     case STMT_OMP_TEAMS_DISTRIBUTE_DIRECTIVE: {
      auto NumClauses = Record[ASTStmtReader::NumStmtFields];
      auto CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTeamsDistributeDirective::CreateEmpty(Context, NumClauses,
                                                   CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TEAMS_DISTRIBUTE_SIMD_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTeamsDistributeSimdDirective::CreateEmpty(Context, NumClauses,
                                                       CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TEAMS_DISTRIBUTE_PARALLEL_FOR_SIMD_DIRECTIVE: {
      auto NumClauses = Record[ASTStmtReader::NumStmtFields];
      auto CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTeamsDistributeParallelForSimdDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TEAMS_DISTRIBUTE_PARALLEL_FOR_DIRECTIVE: {
      auto NumClauses = Record[ASTStmtReader::NumStmtFields];
      auto CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTeamsDistributeParallelForDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_TEAMS_DIRECTIVE:
      S = OMPTargetTeamsDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_TEAMS_DISTRIBUTE_DIRECTIVE: {
      auto NumClauses = Record[ASTStmtReader::NumStmtFields];
      auto CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetTeamsDistributeDirective::CreateEmpty(Context, NumClauses,
                                                         CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_TEAMS_DISTRIBUTE_PARALLEL_FOR_DIRECTIVE: {
      auto NumClauses = Record[ASTStmtReader::NumStmtFields];
      auto CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetTeamsDistributeParallelForDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_TEAMS_DISTRIBUTE_PARALLEL_FOR_SIMD_DIRECTIVE: {
      auto NumClauses = Record[ASTStmtReader::NumStmtFields];
      auto CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetTeamsDistributeParallelForSimdDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_TEAMS_DISTRIBUTE_SIMD_DIRECTIVE: {
      auto NumClauses = Record[ASTStmtReader::NumStmtFields];
      auto CollapsedNum = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetTeamsDistributeSimdDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case EXPR_CXX_OPERATOR_CALL:
      S = CXXOperatorCallExpr::CreateEmpty(
          Context, /*NumArgs=*/Record[ASTStmtReader::NumExprFields], Empty);
      break;

    case EXPR_CXX_MEMBER_CALL:
      S = CXXMemberCallExpr::CreateEmpty(
          Context, /*NumArgs=*/Record[ASTStmtReader::NumExprFields], Empty);
      break;

    case EXPR_CXX_CONSTRUCT:
      S = CXXConstructExpr::CreateEmpty(
          Context,
          /* NumArgs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_INHERITED_CTOR_INIT:
      S = new (Context) CXXInheritedCtorInitExpr(Empty);
      break;

    case EXPR_CXX_TEMPORARY_OBJECT:
      S = CXXTemporaryObjectExpr::CreateEmpty(
          Context,
          /* NumArgs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_STATIC_CAST:
      S = CXXStaticCastExpr::CreateEmpty(Context,
                       /*PathSize*/ Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_DYNAMIC_CAST:
      S = CXXDynamicCastExpr::CreateEmpty(Context,
                       /*PathSize*/ Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_REINTERPRET_CAST:
      S = CXXReinterpretCastExpr::CreateEmpty(Context,
                       /*PathSize*/ Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_CONST_CAST:
      S = CXXConstCastExpr::CreateEmpty(Context);
      break;

    case EXPR_CXX_FUNCTIONAL_CAST:
      S = CXXFunctionalCastExpr::CreateEmpty(Context,
                       /*PathSize*/ Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_USER_DEFINED_LITERAL:
      S = UserDefinedLiteral::CreateEmpty(
          Context, /*NumArgs=*/Record[ASTStmtReader::NumExprFields], Empty);
      break;

    case EXPR_CXX_STD_INITIALIZER_LIST:
      S = new (Context) CXXStdInitializerListExpr(Empty);
      break;

    case EXPR_CXX_BOOL_LITERAL:
      S = new (Context) CXXBoolLiteralExpr(Empty);
      break;

    case EXPR_CXX_NULL_PTR_LITERAL:
      S = new (Context) CXXNullPtrLiteralExpr(Empty);
      break;

    case EXPR_CXX_TYPEID_EXPR:
      S = new (Context) CXXTypeidExpr(Empty, true);
      break;

    case EXPR_CXX_TYPEID_TYPE:
      S = new (Context) CXXTypeidExpr(Empty, false);
      break;

    case EXPR_CXX_UUIDOF_EXPR:
      S = new (Context) CXXUuidofExpr(Empty, true);
      break;

    case EXPR_CXX_PROPERTY_REF_EXPR:
      S = new (Context) MSPropertyRefExpr(Empty);
      break;

    case EXPR_CXX_PROPERTY_SUBSCRIPT_EXPR:
      S = new (Context) MSPropertySubscriptExpr(Empty);
      break;

    case EXPR_CXX_UUIDOF_TYPE:
      S = new (Context) CXXUuidofExpr(Empty, false);
      break;

    case EXPR_CXX_THIS:
      S = new (Context) CXXThisExpr(Empty);
      break;

    case EXPR_CXX_THROW:
      S = new (Context) CXXThrowExpr(Empty);
      break;

    case EXPR_CXX_DEFAULT_ARG:
      S = new (Context) CXXDefaultArgExpr(Empty);
      break;

    case EXPR_CXX_DEFAULT_INIT:
      S = new (Context) CXXDefaultInitExpr(Empty);
      break;

    case EXPR_CXX_BIND_TEMPORARY:
      S = new (Context) CXXBindTemporaryExpr(Empty);
      break;

    case EXPR_CXX_SCALAR_VALUE_INIT:
      S = new (Context) CXXScalarValueInitExpr(Empty);
      break;

    case EXPR_CXX_NEW:
      S = CXXNewExpr::CreateEmpty(
          Context,
          /*IsArray=*/Record[ASTStmtReader::NumExprFields],
          /*HasInit=*/Record[ASTStmtReader::NumExprFields + 1],
          /*NumPlacementArgs=*/Record[ASTStmtReader::NumExprFields + 2],
          /*IsParenTypeId=*/Record[ASTStmtReader::NumExprFields + 3]);
      break;

    case EXPR_CXX_DELETE:
      S = new (Context) CXXDeleteExpr(Empty);
      break;

    case EXPR_CXX_PSEUDO_DESTRUCTOR:
      S = new (Context) CXXPseudoDestructorExpr(Empty);
      break;

    case EXPR_EXPR_WITH_CLEANUPS:
      S = ExprWithCleanups::Create(Context, Empty,
                                   Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_DEPENDENT_SCOPE_MEMBER:
      S = CXXDependentScopeMemberExpr::CreateEmpty(
          Context,
          /*HasTemplateKWAndArgsInfo=*/Record[ASTStmtReader::NumExprFields],
          /*NumTemplateArgs=*/Record[ASTStmtReader::NumExprFields + 1],
          /*HasFirstQualifierFoundInScope=*/
          Record[ASTStmtReader::NumExprFields + 2]);
      break;

    case EXPR_CXX_DEPENDENT_SCOPE_DECL_REF:
      S = DependentScopeDeclRefExpr::CreateEmpty(Context,
         /*HasTemplateKWAndArgsInfo=*/Record[ASTStmtReader::NumExprFields],
                  /*NumTemplateArgs=*/Record[ASTStmtReader::NumExprFields]
                                   ? Record[ASTStmtReader::NumExprFields + 1]
                                   : 0);
      break;

    case EXPR_CXX_UNRESOLVED_CONSTRUCT:
      S = CXXUnresolvedConstructExpr::CreateEmpty(Context,
                              /*NumArgs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_UNRESOLVED_MEMBER:
      S = UnresolvedMemberExpr::CreateEmpty(
          Context,
          /*NumResults=*/Record[ASTStmtReader::NumExprFields],
          /*HasTemplateKWAndArgsInfo=*/Record[ASTStmtReader::NumExprFields + 1],
          /*NumTemplateArgs=*/
          Record[ASTStmtReader::NumExprFields + 1]
              ? Record[ASTStmtReader::NumExprFields + 2]
              : 0);
      break;

    case EXPR_CXX_UNRESOLVED_LOOKUP:
      S = UnresolvedLookupExpr::CreateEmpty(
          Context,
          /*NumResults=*/Record[ASTStmtReader::NumExprFields],
          /*HasTemplateKWAndArgsInfo=*/Record[ASTStmtReader::NumExprFields + 1],
          /*NumTemplateArgs=*/
          Record[ASTStmtReader::NumExprFields + 1]
              ? Record[ASTStmtReader::NumExprFields + 2]
              : 0);
      break;

    case EXPR_TYPE_TRAIT:
      S = TypeTraitExpr::CreateDeserialized(Context,
            Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_ARRAY_TYPE_TRAIT:
      S = new (Context) ArrayTypeTraitExpr(Empty);
      break;

    case EXPR_CXX_EXPRESSION_TRAIT:
      S = new (Context) ExpressionTraitExpr(Empty);
      break;

    case EXPR_CXX_NOEXCEPT:
      S = new (Context) CXXNoexceptExpr(Empty);
      break;

    case EXPR_PACK_EXPANSION:
      S = new (Context) PackExpansionExpr(Empty);
      break;

    case EXPR_SIZEOF_PACK:
      S = SizeOfPackExpr::CreateDeserialized(
              Context,
              /*NumPartialArgs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_SUBST_NON_TYPE_TEMPLATE_PARM:
      S = new (Context) SubstNonTypeTemplateParmExpr(Empty);
      break;

    case EXPR_SUBST_NON_TYPE_TEMPLATE_PARM_PACK:
      S = new (Context) SubstNonTypeTemplateParmPackExpr(Empty);
      break;

    case EXPR_FUNCTION_PARM_PACK:
      S = FunctionParmPackExpr::CreateEmpty(Context,
                                          Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_MATERIALIZE_TEMPORARY:
      S = new (Context) MaterializeTemporaryExpr(Empty);
      break;

    case EXPR_CXX_FOLD:
      S = new (Context) CXXFoldExpr(Empty);
      break;

    case EXPR_OPAQUE_VALUE:
      S = new (Context) OpaqueValueExpr(Empty);
      break;

    case EXPR_CUDA_KERNEL_CALL:
      S = CUDAKernelCallExpr::CreateEmpty(
          Context, /*NumArgs=*/Record[ASTStmtReader::NumExprFields], Empty);
      break;

    case EXPR_ASTYPE:
      S = new (Context) AsTypeExpr(Empty);
      break;

    case EXPR_PSEUDO_OBJECT: {
      unsigned numSemanticExprs = Record[ASTStmtReader::NumExprFields];
      S = PseudoObjectExpr::Create(Context, Empty, numSemanticExprs);
      break;
    }

    case EXPR_ATOMIC:
      S = new (Context) AtomicExpr(Empty);
      break;

    case EXPR_LAMBDA: {
      unsigned NumCaptures = Record[ASTStmtReader::NumExprFields];
      S = LambdaExpr::CreateDeserialized(Context, NumCaptures);
      break;
    }

    case STMT_COROUTINE_BODY: {
      unsigned NumParams = Record[ASTStmtReader::NumStmtFields];
      S = CoroutineBodyStmt::Create(Context, Empty, NumParams);
      break;
    }

    case STMT_CORETURN:
      S = new (Context) CoreturnStmt(Empty);
      break;

    case EXPR_COAWAIT:
      S = new (Context) CoawaitExpr(Empty);
      break;

    case EXPR_COYIELD:
      S = new (Context) CoyieldExpr(Empty);
      break;

    case EXPR_DEPENDENT_COAWAIT:
      S = new (Context) DependentCoawaitExpr(Empty);
      break;
    }

    // We hit a STMT_STOP, so we're done with this expression.
    if (Finished)
      break;

    ++NumStatementsRead;

    if (S && !IsStmtReference) {
      Reader.Visit(S);
      StmtEntries[Cursor.GetCurrentBitNo()] = S;
    }

    assert(Record.getIdx() == Record.size() &&
           "Invalid deserialization of statement");
    StmtStack.push_back(S);
  }
Done:
  assert(StmtStack.size() > PrevNumStmts && "Read too many sub-stmts!");
  assert(StmtStack.size() == PrevNumStmts + 1 && "Extra expressions on stack!");
  return StmtStack.pop_back_val();
}
