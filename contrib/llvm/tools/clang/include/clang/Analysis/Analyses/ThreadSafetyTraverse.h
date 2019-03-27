//===- ThreadSafetyTraverse.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a framework for doing generic traversals and rewriting
// operations over the Thread Safety TIL.
//
// UNDER CONSTRUCTION.  USE AT YOUR OWN RISK.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYTRAVERSE_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYTRAVERSE_H

#include "clang/AST/Decl.h"
#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyUtil.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include <cstdint>
#include <ostream>

namespace clang {
namespace threadSafety {
namespace til {

// Defines an interface used to traverse SExprs.  Traversals have been made as
// generic as possible, and are intended to handle any kind of pass over the
// AST, e.g. visitors, copying, non-destructive rewriting, destructive
// (in-place) rewriting, hashing, typing, etc.
//
// Traversals implement the functional notion of a "fold" operation on SExprs.
// Each SExpr class provides a traverse method, which does the following:
//   * e->traverse(v):
//       // compute a result r_i for each subexpression e_i
//       for (i = 1..n)  r_i = v.traverse(e_i);
//       // combine results into a result for e,  where X is the class of e
//       return v.reduceX(*e, r_1, .. r_n).
//
// A visitor can control the traversal by overriding the following methods:
//   * v.traverse(e):
//       return v.traverseByCase(e), which returns v.traverseX(e)
//   * v.traverseX(e):   (X is the class of e)
//       return e->traverse(v).
//   * v.reduceX(*e, r_1, .. r_n):
//       compute a result for a node of type X
//
// The reduceX methods control the kind of traversal (visitor, copy, etc.).
// They are defined in derived classes.
//
// Class R defines the basic interface types (R_SExpr).
template <class Self, class R>
class Traversal {
public:
  Self *self() { return static_cast<Self *>(this); }

  // Traverse an expression -- returning a result of type R_SExpr.
  // Override this method to do something for every expression, regardless
  // of which kind it is.
  // E is a reference, so this can be use for in-place updates.
  // The type T must be a subclass of SExpr.
  template <class T>
  typename R::R_SExpr traverse(T* &E, typename R::R_Ctx Ctx) {
    return traverseSExpr(E, Ctx);
  }

  // Override this method to do something for every expression.
  // Does not allow in-place updates.
  typename R::R_SExpr traverseSExpr(SExpr *E, typename R::R_Ctx Ctx) {
    return traverseByCase(E, Ctx);
  }

  // Helper method to call traverseX(e) on the appropriate type.
  typename R::R_SExpr traverseByCase(SExpr *E, typename R::R_Ctx Ctx) {
    switch (E->opcode()) {
#define TIL_OPCODE_DEF(X)                                                   \
    case COP_##X:                                                           \
      return self()->traverse##X(cast<X>(E), Ctx);
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
    }
    return self()->reduceNull();
  }

// Traverse e, by static dispatch on the type "X" of e.
// Override these methods to do something for a particular kind of term.
#define TIL_OPCODE_DEF(X)                                                   \
  typename R::R_SExpr traverse##X(X *e, typename R::R_Ctx Ctx) {            \
    return e->traverse(*self(), Ctx);                                       \
  }
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
};

// Base class for simple reducers that don't much care about the context.
class SimpleReducerBase {
public:
  enum TraversalKind {
    // Ordinary subexpressions.
    TRV_Normal,

    // Declarations (e.g. function bodies).
    TRV_Decl,

    // Expressions that require lazy evaluation.
    TRV_Lazy,

    // Type expressions.
    TRV_Type
  };

  // R_Ctx defines a "context" for the traversal, which encodes information
  // about where a term appears.  This can be used to encoding the
  // "current continuation" for CPS transforms, or other information.
  using R_Ctx = TraversalKind;

  // Create context for an ordinary subexpression.
  R_Ctx subExprCtx(R_Ctx Ctx) { return TRV_Normal; }

  // Create context for a subexpression that occurs in a declaration position
  // (e.g. function body).
  R_Ctx declCtx(R_Ctx Ctx) { return TRV_Decl; }

  // Create context for a subexpression that occurs in a position that
  // should be reduced lazily.  (e.g. code body).
  R_Ctx lazyCtx(R_Ctx Ctx) { return TRV_Lazy; }

  // Create context for a subexpression that occurs in a type position.
  R_Ctx typeCtx(R_Ctx Ctx) { return TRV_Type; }
};

// Base class for traversals that rewrite an SExpr to another SExpr.
class CopyReducerBase : public SimpleReducerBase {
public:
  // R_SExpr is the result type for a traversal.
  // A copy or non-destructive rewrite returns a newly allocated term.
  using R_SExpr = SExpr *;
  using R_BasicBlock = BasicBlock *;

  // Container is a minimal interface used to store results when traversing
  // SExprs of variable arity, such as Phi, Goto, and SCFG.
  template <class T> class Container {
  public:
    // Allocate a new container with a capacity for n elements.
    Container(CopyReducerBase &S, unsigned N) : Elems(S.Arena, N) {}

    // Push a new element onto the container.
    void push_back(T E) { Elems.push_back(E); }

    SimpleArray<T> Elems;
  };

  CopyReducerBase(MemRegionRef A) : Arena(A) {}

protected:
  MemRegionRef Arena;
};

// Base class for visit traversals.
class VisitReducerBase : public SimpleReducerBase {
public:
  // A visitor returns a bool, representing success or failure.
  using R_SExpr = bool;
  using R_BasicBlock = bool;

  // A visitor "container" is a single bool, which accumulates success.
  template <class T> class Container {
  public:
    bool Success = true;

    Container(VisitReducerBase &S, unsigned N) {}

    void push_back(bool E) { Success = Success && E; }
  };
};

// Implements a traversal that visits each subexpression, and returns either
// true or false.
template <class Self>
class VisitReducer : public Traversal<Self, VisitReducerBase>,
                     public VisitReducerBase {
public:
  VisitReducer() = default;

public:
  R_SExpr reduceNull() { return true; }
  R_SExpr reduceUndefined(Undefined &Orig) { return true; }
  R_SExpr reduceWildcard(Wildcard &Orig) { return true; }

  R_SExpr reduceLiteral(Literal &Orig) { return true; }
  template<class T>
  R_SExpr reduceLiteralT(LiteralT<T> &Orig) { return true; }
  R_SExpr reduceLiteralPtr(Literal &Orig) { return true; }

  R_SExpr reduceFunction(Function &Orig, Variable *Nvd, R_SExpr E0) {
    return Nvd && E0;
  }

  R_SExpr reduceSFunction(SFunction &Orig, Variable *Nvd, R_SExpr E0) {
    return Nvd && E0;
  }

  R_SExpr reduceCode(Code &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }

  R_SExpr reduceField(Field &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }

  R_SExpr reduceApply(Apply &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }

  R_SExpr reduceSApply(SApply &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }

  R_SExpr reduceProject(Project &Orig, R_SExpr E0) { return E0; }
  R_SExpr reduceCall(Call &Orig, R_SExpr E0) { return E0; }
  R_SExpr reduceAlloc(Alloc &Orig, R_SExpr E0) { return E0; }
  R_SExpr reduceLoad(Load &Orig, R_SExpr E0) { return E0; }
  R_SExpr reduceStore(Store &Orig, R_SExpr E0, R_SExpr E1) { return E0 && E1; }

  R_SExpr reduceArrayIndex(Store &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }

  R_SExpr reduceArrayAdd(Store &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }

  R_SExpr reduceUnaryOp(UnaryOp &Orig, R_SExpr E0) { return E0; }

  R_SExpr reduceBinaryOp(BinaryOp &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }

  R_SExpr reduceCast(Cast &Orig, R_SExpr E0) { return E0; }

  R_SExpr reduceSCFG(SCFG &Orig, Container<BasicBlock *> Bbs) {
    return Bbs.Success;
  }

  R_BasicBlock reduceBasicBlock(BasicBlock &Orig, Container<R_SExpr> &As,
                                Container<R_SExpr> &Is, R_SExpr T) {
    return (As.Success && Is.Success && T);
  }

  R_SExpr reducePhi(Phi &Orig, Container<R_SExpr> &As) {
    return As.Success;
  }

  R_SExpr reduceGoto(Goto &Orig, BasicBlock *B) {
    return true;
  }

  R_SExpr reduceBranch(Branch &O, R_SExpr C, BasicBlock *B0, BasicBlock *B1) {
    return C;
  }

  R_SExpr reduceReturn(Return &O, R_SExpr E) {
    return E;
  }

  R_SExpr reduceIdentifier(Identifier &Orig) {
    return true;
  }

  R_SExpr reduceIfThenElse(IfThenElse &Orig, R_SExpr C, R_SExpr T, R_SExpr E) {
    return C && T && E;
  }

  R_SExpr reduceLet(Let &Orig, Variable *Nvd, R_SExpr B) {
    return Nvd && B;
  }

  Variable *enterScope(Variable &Orig, R_SExpr E0) { return &Orig; }
  void exitScope(const Variable &Orig) {}
  void enterCFG(SCFG &Cfg) {}
  void exitCFG(SCFG &Cfg) {}
  void enterBasicBlock(BasicBlock &BB) {}
  void exitBasicBlock(BasicBlock &BB) {}

  Variable *reduceVariableRef(Variable *Ovd) { return Ovd; }
  BasicBlock *reduceBasicBlockRef(BasicBlock *Obb) { return Obb; }

public:
  bool traverse(SExpr *E, TraversalKind K = TRV_Normal) {
    Success = Success && this->traverseByCase(E);
    return Success;
  }

  static bool visit(SExpr *E) {
    Self Visitor;
    return Visitor.traverse(E, TRV_Normal);
  }

private:
  bool Success;
};

// Basic class for comparison operations over expressions.
template <typename Self>
class Comparator {
protected:
  Self *self() { return reinterpret_cast<Self *>(this); }

public:
  bool compareByCase(const SExpr *E1, const SExpr* E2) {
    switch (E1->opcode()) {
#define TIL_OPCODE_DEF(X)                                                     \
    case COP_##X:                                                             \
      return cast<X>(E1)->compare(cast<X>(E2), *self());
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
    }
    return false;
  }
};

class EqualsComparator : public Comparator<EqualsComparator> {
public:
  // Result type for the comparison, e.g. bool for simple equality,
  // or int for lexigraphic comparison (-1, 0, 1).  Must have one value which
  // denotes "true".
  using CType = bool;

  CType trueResult() { return true; }
  bool notTrue(CType ct) { return !ct; }

  bool compareIntegers(unsigned i, unsigned j) { return i == j; }
  bool compareStrings (StringRef s, StringRef r) { return s == r; }
  bool comparePointers(const void* P, const void* Q) { return P == Q; }

  bool compare(const SExpr *E1, const SExpr* E2) {
    if (E1->opcode() != E2->opcode())
      return false;
    return compareByCase(E1, E2);
  }

  // TODO -- handle alpha-renaming of variables
  void enterScope(const Variable *V1, const Variable *V2) {}
  void leaveScope() {}

  bool compareVariableRefs(const Variable *V1, const Variable *V2) {
    return V1 == V2;
  }

  static bool compareExprs(const SExpr *E1, const SExpr* E2) {
    EqualsComparator Eq;
    return Eq.compare(E1, E2);
  }
};

class MatchComparator : public Comparator<MatchComparator> {
public:
  // Result type for the comparison, e.g. bool for simple equality,
  // or int for lexigraphic comparison (-1, 0, 1).  Must have one value which
  // denotes "true".
  using CType = bool;

  CType trueResult() { return true; }
  bool notTrue(CType ct) { return !ct; }

  bool compareIntegers(unsigned i, unsigned j) { return i == j; }
  bool compareStrings (StringRef s, StringRef r) { return s == r; }
  bool comparePointers(const void *P, const void *Q) { return P == Q; }

  bool compare(const SExpr *E1, const SExpr *E2) {
    // Wildcards match anything.
    if (E1->opcode() == COP_Wildcard || E2->opcode() == COP_Wildcard)
      return true;
    // otherwise normal equality.
    if (E1->opcode() != E2->opcode())
      return false;
    return compareByCase(E1, E2);
  }

  // TODO -- handle alpha-renaming of variables
  void enterScope(const Variable* V1, const Variable* V2) {}
  void leaveScope() {}

  bool compareVariableRefs(const Variable* V1, const Variable* V2) {
    return V1 == V2;
  }

  static bool compareExprs(const SExpr *E1, const SExpr* E2) {
    MatchComparator Matcher;
    return Matcher.compare(E1, E2);
  }
};

// inline std::ostream& operator<<(std::ostream& SS, StringRef R) {
//   return SS.write(R.data(), R.size());
// }

// Pretty printer for TIL expressions
template <typename Self, typename StreamType>
class PrettyPrinter {
private:
  // Print out additional information.
  bool Verbose;

  // Omit redundant decls.
  bool Cleanup;

  // Print exprs in C-like syntax.
  bool CStyle;

public:
  PrettyPrinter(bool V = false, bool C = true, bool CS = true)
      : Verbose(V), Cleanup(C), CStyle(CS) {}

  static void print(const SExpr *E, StreamType &SS) {
    Self printer;
    printer.printSExpr(E, SS, Prec_MAX);
  }

protected:
  Self *self() { return reinterpret_cast<Self *>(this); }

  void newline(StreamType &SS) {
    SS << "\n";
  }

  // TODO: further distinguish between binary operations.
  static const unsigned Prec_Atom = 0;
  static const unsigned Prec_Postfix = 1;
  static const unsigned Prec_Unary = 2;
  static const unsigned Prec_Binary = 3;
  static const unsigned Prec_Other = 4;
  static const unsigned Prec_Decl = 5;
  static const unsigned Prec_MAX = 6;

  // Return the precedence of a given node, for use in pretty printing.
  unsigned precedence(const SExpr *E) {
    switch (E->opcode()) {
      case COP_Future:     return Prec_Atom;
      case COP_Undefined:  return Prec_Atom;
      case COP_Wildcard:   return Prec_Atom;

      case COP_Literal:    return Prec_Atom;
      case COP_LiteralPtr: return Prec_Atom;
      case COP_Variable:   return Prec_Atom;
      case COP_Function:   return Prec_Decl;
      case COP_SFunction:  return Prec_Decl;
      case COP_Code:       return Prec_Decl;
      case COP_Field:      return Prec_Decl;

      case COP_Apply:      return Prec_Postfix;
      case COP_SApply:     return Prec_Postfix;
      case COP_Project:    return Prec_Postfix;

      case COP_Call:       return Prec_Postfix;
      case COP_Alloc:      return Prec_Other;
      case COP_Load:       return Prec_Postfix;
      case COP_Store:      return Prec_Other;
      case COP_ArrayIndex: return Prec_Postfix;
      case COP_ArrayAdd:   return Prec_Postfix;

      case COP_UnaryOp:    return Prec_Unary;
      case COP_BinaryOp:   return Prec_Binary;
      case COP_Cast:       return Prec_Atom;

      case COP_SCFG:       return Prec_Decl;
      case COP_BasicBlock: return Prec_MAX;
      case COP_Phi:        return Prec_Atom;
      case COP_Goto:       return Prec_Atom;
      case COP_Branch:     return Prec_Atom;
      case COP_Return:     return Prec_Other;

      case COP_Identifier: return Prec_Atom;
      case COP_IfThenElse: return Prec_Other;
      case COP_Let:        return Prec_Decl;
    }
    return Prec_MAX;
  }

  void printBlockLabel(StreamType & SS, const BasicBlock *BB, int index) {
    if (!BB) {
      SS << "BB_null";
      return;
    }
    SS << "BB_";
    SS << BB->blockID();
    if (index >= 0) {
      SS << ":";
      SS << index;
    }
  }

  void printSExpr(const SExpr *E, StreamType &SS, unsigned P, bool Sub=true) {
    if (!E) {
      self()->printNull(SS);
      return;
    }
    if (Sub && E->block() && E->opcode() != COP_Variable) {
      SS << "_x" << E->id();
      return;
    }
    if (self()->precedence(E) > P) {
      // Wrap expr in () if necessary.
      SS << "(";
      self()->printSExpr(E, SS, Prec_MAX);
      SS << ")";
      return;
    }

    switch (E->opcode()) {
#define TIL_OPCODE_DEF(X)                                                  \
    case COP_##X:                                                          \
      self()->print##X(cast<X>(E), SS);                                    \
      return;
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
    }
  }

  void printNull(StreamType &SS) {
    SS << "#null";
  }

  void printFuture(const Future *E, StreamType &SS) {
    self()->printSExpr(E->maybeGetResult(), SS, Prec_Atom);
  }

  void printUndefined(const Undefined *E, StreamType &SS) {
    SS << "#undefined";
  }

  void printWildcard(const Wildcard *E, StreamType &SS) {
    SS << "*";
  }

  template<class T>
  void printLiteralT(const LiteralT<T> *E, StreamType &SS) {
    SS << E->value();
  }

  void printLiteralT(const LiteralT<uint8_t> *E, StreamType &SS) {
    SS << "'" << E->value() << "'";
  }

  void printLiteral(const Literal *E, StreamType &SS) {
    if (E->clangExpr()) {
      SS << getSourceLiteralString(E->clangExpr());
      return;
    }
    else {
      ValueType VT = E->valueType();
      switch (VT.Base) {
      case ValueType::BT_Void:
        SS << "void";
        return;
      case ValueType::BT_Bool:
        if (E->as<bool>().value())
          SS << "true";
        else
          SS << "false";
        return;
      case ValueType::BT_Int:
        switch (VT.Size) {
        case ValueType::ST_8:
          if (VT.Signed)
            printLiteralT(&E->as<int8_t>(), SS);
          else
            printLiteralT(&E->as<uint8_t>(), SS);
          return;
        case ValueType::ST_16:
          if (VT.Signed)
            printLiteralT(&E->as<int16_t>(), SS);
          else
            printLiteralT(&E->as<uint16_t>(), SS);
          return;
        case ValueType::ST_32:
          if (VT.Signed)
            printLiteralT(&E->as<int32_t>(), SS);
          else
            printLiteralT(&E->as<uint32_t>(), SS);
          return;
        case ValueType::ST_64:
          if (VT.Signed)
            printLiteralT(&E->as<int64_t>(), SS);
          else
            printLiteralT(&E->as<uint64_t>(), SS);
          return;
        default:
          break;
        }
        break;
      case ValueType::BT_Float:
        switch (VT.Size) {
        case ValueType::ST_32:
          printLiteralT(&E->as<float>(), SS);
          return;
        case ValueType::ST_64:
          printLiteralT(&E->as<double>(), SS);
          return;
        default:
          break;
        }
        break;
      case ValueType::BT_String:
        SS << "\"";
        printLiteralT(&E->as<StringRef>(), SS);
        SS << "\"";
        return;
      case ValueType::BT_Pointer:
        SS << "#ptr";
        return;
      case ValueType::BT_ValueRef:
        SS << "#vref";
        return;
      }
    }
    SS << "#lit";
  }

  void printLiteralPtr(const LiteralPtr *E, StreamType &SS) {
    SS << E->clangDecl()->getNameAsString();
  }

  void printVariable(const Variable *V, StreamType &SS, bool IsVarDecl=false) {
    if (CStyle && V->kind() == Variable::VK_SFun)
      SS << "this";
    else
      SS << V->name() << V->id();
  }

  void printFunction(const Function *E, StreamType &SS, unsigned sugared = 0) {
    switch (sugared) {
      default:
        SS << "\\(";   // Lambda
        break;
      case 1:
        SS << "(";     // Slot declarations
        break;
      case 2:
        SS << ", ";    // Curried functions
        break;
    }
    self()->printVariable(E->variableDecl(), SS, true);
    SS << ": ";
    self()->printSExpr(E->variableDecl()->definition(), SS, Prec_MAX);

    const SExpr *B = E->body();
    if (B && B->opcode() == COP_Function)
      self()->printFunction(cast<Function>(B), SS, 2);
    else {
      SS << ")";
      self()->printSExpr(B, SS, Prec_Decl);
    }
  }

  void printSFunction(const SFunction *E, StreamType &SS) {
    SS << "@";
    self()->printVariable(E->variableDecl(), SS, true);
    SS << " ";
    self()->printSExpr(E->body(), SS, Prec_Decl);
  }

  void printCode(const Code *E, StreamType &SS) {
    SS << ": ";
    self()->printSExpr(E->returnType(), SS, Prec_Decl-1);
    SS << " -> ";
    self()->printSExpr(E->body(), SS, Prec_Decl);
  }

  void printField(const Field *E, StreamType &SS) {
    SS << ": ";
    self()->printSExpr(E->range(), SS, Prec_Decl-1);
    SS << " = ";
    self()->printSExpr(E->body(), SS, Prec_Decl);
  }

  void printApply(const Apply *E, StreamType &SS, bool sugared = false) {
    const SExpr *F = E->fun();
    if (F->opcode() == COP_Apply) {
      printApply(cast<Apply>(F), SS, true);
      SS << ", ";
    } else {
      self()->printSExpr(F, SS, Prec_Postfix);
      SS << "(";
    }
    self()->printSExpr(E->arg(), SS, Prec_MAX);
    if (!sugared)
      SS << ")$";
  }

  void printSApply(const SApply *E, StreamType &SS) {
    self()->printSExpr(E->sfun(), SS, Prec_Postfix);
    if (E->isDelegation()) {
      SS << "@(";
      self()->printSExpr(E->arg(), SS, Prec_MAX);
      SS << ")";
    }
  }

  void printProject(const Project *E, StreamType &SS) {
    if (CStyle) {
      // Omit the  this->
      if (const auto *SAP = dyn_cast<SApply>(E->record())) {
        if (const auto *V = dyn_cast<Variable>(SAP->sfun())) {
          if (!SAP->isDelegation() && V->kind() == Variable::VK_SFun) {
            SS << E->slotName();
            return;
          }
        }
      }
      if (isa<Wildcard>(E->record())) {
        // handle existentials
        SS << "&";
        SS << E->clangDecl()->getQualifiedNameAsString();
        return;
      }
    }
    self()->printSExpr(E->record(), SS, Prec_Postfix);
    if (CStyle && E->isArrow())
      SS << "->";
    else
      SS << ".";
    SS << E->slotName();
  }

  void printCall(const Call *E, StreamType &SS) {
    const SExpr *T = E->target();
    if (T->opcode() == COP_Apply) {
      self()->printApply(cast<Apply>(T), SS, true);
      SS << ")";
    }
    else {
      self()->printSExpr(T, SS, Prec_Postfix);
      SS << "()";
    }
  }

  void printAlloc(const Alloc *E, StreamType &SS) {
    SS << "new ";
    self()->printSExpr(E->dataType(), SS, Prec_Other-1);
  }

  void printLoad(const Load *E, StreamType &SS) {
    self()->printSExpr(E->pointer(), SS, Prec_Postfix);
    if (!CStyle)
      SS << "^";
  }

  void printStore(const Store *E, StreamType &SS) {
    self()->printSExpr(E->destination(), SS, Prec_Other-1);
    SS << " := ";
    self()->printSExpr(E->source(), SS, Prec_Other-1);
  }

  void printArrayIndex(const ArrayIndex *E, StreamType &SS) {
    self()->printSExpr(E->array(), SS, Prec_Postfix);
    SS << "[";
    self()->printSExpr(E->index(), SS, Prec_MAX);
    SS << "]";
  }

  void printArrayAdd(const ArrayAdd *E, StreamType &SS) {
    self()->printSExpr(E->array(), SS, Prec_Postfix);
    SS << " + ";
    self()->printSExpr(E->index(), SS, Prec_Atom);
  }

  void printUnaryOp(const UnaryOp *E, StreamType &SS) {
    SS << getUnaryOpcodeString(E->unaryOpcode());
    self()->printSExpr(E->expr(), SS, Prec_Unary);
  }

  void printBinaryOp(const BinaryOp *E, StreamType &SS) {
    self()->printSExpr(E->expr0(), SS, Prec_Binary-1);
    SS << " " << getBinaryOpcodeString(E->binaryOpcode()) << " ";
    self()->printSExpr(E->expr1(), SS, Prec_Binary-1);
  }

  void printCast(const Cast *E, StreamType &SS) {
    if (!CStyle) {
      SS << "cast[";
      switch (E->castOpcode()) {
      case CAST_none:
        SS << "none";
        break;
      case CAST_extendNum:
        SS << "extendNum";
        break;
      case CAST_truncNum:
        SS << "truncNum";
        break;
      case CAST_toFloat:
        SS << "toFloat";
        break;
      case CAST_toInt:
        SS << "toInt";
        break;
      case CAST_objToPtr:
        SS << "objToPtr";
        break;
      }
      SS << "](";
      self()->printSExpr(E->expr(), SS, Prec_Unary);
      SS << ")";
      return;
    }
    self()->printSExpr(E->expr(), SS, Prec_Unary);
  }

  void printSCFG(const SCFG *E, StreamType &SS) {
    SS << "CFG {\n";
    for (const auto *BBI : *E)
      printBasicBlock(BBI, SS);
    SS << "}";
    newline(SS);
  }

  void printBBInstr(const SExpr *E, StreamType &SS) {
    bool Sub = false;
    if (E->opcode() == COP_Variable) {
      const auto *V = cast<Variable>(E);
      SS << "let " << V->name() << V->id() << " = ";
      E = V->definition();
      Sub = true;
    }
    else if (E->opcode() != COP_Store) {
      SS << "let _x" << E->id() << " = ";
    }
    self()->printSExpr(E, SS, Prec_MAX, Sub);
    SS << ";";
    newline(SS);
  }

  void printBasicBlock(const BasicBlock *E, StreamType &SS) {
    SS << "BB_" << E->blockID() << ":";
    if (E->parent())
      SS << " BB_" << E->parent()->blockID();
    newline(SS);

    for (const auto *A : E->arguments())
      printBBInstr(A, SS);

    for (const auto *I : E->instructions())
      printBBInstr(I, SS);

    const SExpr *T = E->terminator();
    if (T) {
      self()->printSExpr(T, SS, Prec_MAX, false);
      SS << ";";
      newline(SS);
    }
    newline(SS);
  }

  void printPhi(const Phi *E, StreamType &SS) {
    SS << "phi(";
    if (E->status() == Phi::PH_SingleVal)
      self()->printSExpr(E->values()[0], SS, Prec_MAX);
    else {
      unsigned i = 0;
      for (const auto *V : E->values()) {
        if (i++ > 0)
          SS << ", ";
        self()->printSExpr(V, SS, Prec_MAX);
      }
    }
    SS << ")";
  }

  void printGoto(const Goto *E, StreamType &SS) {
    SS << "goto ";
    printBlockLabel(SS, E->targetBlock(), E->index());
  }

  void printBranch(const Branch *E, StreamType &SS) {
    SS << "branch (";
    self()->printSExpr(E->condition(), SS, Prec_MAX);
    SS << ") ";
    printBlockLabel(SS, E->thenBlock(), -1);
    SS << " ";
    printBlockLabel(SS, E->elseBlock(), -1);
  }

  void printReturn(const Return *E, StreamType &SS) {
    SS << "return ";
    self()->printSExpr(E->returnValue(), SS, Prec_Other);
  }

  void printIdentifier(const Identifier *E, StreamType &SS) {
    SS << E->name();
  }

  void printIfThenElse(const IfThenElse *E, StreamType &SS) {
    if (CStyle) {
      printSExpr(E->condition(), SS, Prec_Unary);
      SS << " ? ";
      printSExpr(E->thenExpr(), SS, Prec_Unary);
      SS << " : ";
      printSExpr(E->elseExpr(), SS, Prec_Unary);
      return;
    }
    SS << "if (";
    printSExpr(E->condition(), SS, Prec_MAX);
    SS << ") then ";
    printSExpr(E->thenExpr(), SS, Prec_Other);
    SS << " else ";
    printSExpr(E->elseExpr(), SS, Prec_Other);
  }

  void printLet(const Let *E, StreamType &SS) {
    SS << "let ";
    printVariable(E->variableDecl(), SS, true);
    SS << " = ";
    printSExpr(E->variableDecl()->definition(), SS, Prec_Decl-1);
    SS << "; ";
    printSExpr(E->body(), SS, Prec_Decl-1);
  }
};

class StdPrinter : public PrettyPrinter<StdPrinter, std::ostream> {};

} // namespace til
} // namespace threadSafety
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYTRAVERSE_H
