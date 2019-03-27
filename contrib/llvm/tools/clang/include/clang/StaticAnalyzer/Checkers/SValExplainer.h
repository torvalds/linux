//== SValExplainer.h - Symbolic value explainer -----------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines SValExplainer, a class for pretty-printing a
//  human-readable description of a symbolic value. For example,
//  "reg_$0<x>" is turned into "initial value of variable 'x'".
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CHECKERS_SVALEXPLAINER_H
#define LLVM_CLANG_STATICANALYZER_CHECKERS_SVALEXPLAINER_H

#include "clang/AST/DeclCXX.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValVisitor.h"

namespace clang {

namespace ento {

class SValExplainer : public FullSValVisitor<SValExplainer, std::string> {
private:
  ASTContext &ACtx;

  std::string printStmt(const Stmt *S) {
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    S->printPretty(OS, nullptr, PrintingPolicy(ACtx.getLangOpts()));
    return OS.str();
  }

  bool isThisObject(const SymbolicRegion *R) {
    if (auto S = dyn_cast<SymbolRegionValue>(R->getSymbol()))
      if (isa<CXXThisRegion>(S->getRegion()))
        return true;
    return false;
  }

public:
  SValExplainer(ASTContext &Ctx) : ACtx(Ctx) {}

  std::string VisitUnknownVal(UnknownVal V) {
    return "unknown value";
  }

  std::string VisitUndefinedVal(UndefinedVal V) {
    return "undefined value";
  }

  std::string VisitLocMemRegionVal(loc::MemRegionVal V) {
    const MemRegion *R = V.getRegion();
    // Avoid the weird "pointer to pointee of ...".
    if (auto SR = dyn_cast<SymbolicRegion>(R)) {
      // However, "pointer to 'this' object" is fine.
      if (!isThisObject(SR))
        return Visit(SR->getSymbol());
    }
    return "pointer to " + Visit(R);
  }

  std::string VisitLocConcreteInt(loc::ConcreteInt V) {
    llvm::APSInt I = V.getValue();
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    OS << "concrete memory address '" << I << "'";
    return OS.str();
  }

  std::string VisitNonLocSymbolVal(nonloc::SymbolVal V) {
    return Visit(V.getSymbol());
  }

  std::string VisitNonLocConcreteInt(nonloc::ConcreteInt V) {
    llvm::APSInt I = V.getValue();
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    OS << (I.isSigned() ? "signed " : "unsigned ") << I.getBitWidth()
       << "-bit integer '" << I << "'";
    return OS.str();
  }

  std::string VisitNonLocLazyCompoundVal(nonloc::LazyCompoundVal V) {
    return "lazily frozen compound value of " + Visit(V.getRegion());
  }

  std::string VisitSymbolRegionValue(const SymbolRegionValue *S) {
    const MemRegion *R = S->getRegion();
    // Special handling for argument values.
    if (auto V = dyn_cast<VarRegion>(R))
      if (auto D = dyn_cast<ParmVarDecl>(V->getDecl()))
        return "argument '" + D->getQualifiedNameAsString() + "'";
    return "initial value of " + Visit(R);
  }

  std::string VisitSymbolConjured(const SymbolConjured *S) {
    return "symbol of type '" + S->getType().getAsString() +
           "' conjured at statement '" + printStmt(S->getStmt()) + "'";
  }

  std::string VisitSymbolDerived(const SymbolDerived *S) {
    return "value derived from (" + Visit(S->getParentSymbol()) +
           ") for " + Visit(S->getRegion());
  }

  std::string VisitSymbolExtent(const SymbolExtent *S) {
    return "extent of " + Visit(S->getRegion());
  }

  std::string VisitSymbolMetadata(const SymbolMetadata *S) {
    return "metadata of type '" + S->getType().getAsString() + "' tied to " +
           Visit(S->getRegion());
  }

  std::string VisitSymIntExpr(const SymIntExpr *S) {
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    OS << "(" << Visit(S->getLHS()) << ") "
       << std::string(BinaryOperator::getOpcodeStr(S->getOpcode())) << " "
       << S->getRHS();
    return OS.str();
  }

  // TODO: IntSymExpr doesn't appear in practice.
  // Add the relevant code once it does.

  std::string VisitSymSymExpr(const SymSymExpr *S) {
    return "(" + Visit(S->getLHS()) + ") " +
           std::string(BinaryOperator::getOpcodeStr(S->getOpcode())) +
           " (" + Visit(S->getRHS()) + ")";
  }

  // TODO: SymbolCast doesn't appear in practice.
  // Add the relevant code once it does.

  std::string VisitSymbolicRegion(const SymbolicRegion *R) {
    // Explain 'this' object here.
    // TODO: Explain CXXThisRegion itself, find a way to test it.
    if (isThisObject(R))
      return "'this' object";
    // Objective-C objects are not normal symbolic regions. At least,
    // they're always on the heap.
    if (R->getSymbol()->getType()
            .getCanonicalType()->getAs<ObjCObjectPointerType>())
      return "object at " + Visit(R->getSymbol());
    // Other heap-based symbolic regions are also special.
    if (isa<HeapSpaceRegion>(R->getMemorySpace()))
      return "heap segment that starts at " + Visit(R->getSymbol());
    return "pointee of " + Visit(R->getSymbol());
  }

  std::string VisitAllocaRegion(const AllocaRegion *R) {
    return "region allocated by '" + printStmt(R->getExpr()) + "'";
  }

  std::string VisitCompoundLiteralRegion(const CompoundLiteralRegion *R) {
    return "compound literal " + printStmt(R->getLiteralExpr());
  }

  std::string VisitStringRegion(const StringRegion *R) {
    return "string literal " + R->getString();
  }

  std::string VisitElementRegion(const ElementRegion *R) {
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    OS << "element of type '" << R->getElementType().getAsString()
       << "' with index ";
    // For concrete index: omit type of the index integer.
    if (auto I = R->getIndex().getAs<nonloc::ConcreteInt>())
      OS << I->getValue();
    else
      OS << "'" << Visit(R->getIndex()) << "'";
    OS << " of " + Visit(R->getSuperRegion());
    return OS.str();
  }

  std::string VisitVarRegion(const VarRegion *R) {
    const VarDecl *VD = R->getDecl();
    std::string Name = VD->getQualifiedNameAsString();
    if (isa<ParmVarDecl>(VD))
      return "parameter '" + Name + "'";
    else if (VD->hasAttr<BlocksAttr>())
      return "block variable '" + Name + "'";
    else if (VD->hasLocalStorage())
      return "local variable '" + Name + "'";
    else if (VD->isStaticLocal())
      return "static local variable '" + Name + "'";
    else if (VD->hasGlobalStorage())
      return "global variable '" + Name + "'";
    else
      llvm_unreachable("A variable is either local or global");
  }

  std::string VisitObjCIvarRegion(const ObjCIvarRegion *R) {
    return "instance variable '" + R->getDecl()->getNameAsString() + "' of " +
           Visit(R->getSuperRegion());
  }

  std::string VisitFieldRegion(const FieldRegion *R) {
    return "field '" + R->getDecl()->getNameAsString() + "' of " +
           Visit(R->getSuperRegion());
  }

  std::string VisitCXXTempObjectRegion(const CXXTempObjectRegion *R) {
    return "temporary object constructed at statement '" +
           printStmt(R->getExpr()) + "'";
  }

  std::string VisitCXXBaseObjectRegion(const CXXBaseObjectRegion *R) {
    return "base object '" + R->getDecl()->getQualifiedNameAsString() +
           "' inside " + Visit(R->getSuperRegion());
  }

  std::string VisitSVal(SVal V) {
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    OS << V;
    return "a value unsupported by the explainer: (" +
           std::string(OS.str()) + ")";
  }

  std::string VisitSymExpr(SymbolRef S) {
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    S->dumpToStream(OS);
    return "a symbolic expression unsupported by the explainer: (" +
           std::string(OS.str()) + ")";
  }

  std::string VisitMemRegion(const MemRegion *R) {
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    OS << R;
    return "a memory region unsupported by the explainer (" +
           std::string(OS.str()) + ")";
  }
};

} // end namespace ento

} // end namespace clang

#endif
