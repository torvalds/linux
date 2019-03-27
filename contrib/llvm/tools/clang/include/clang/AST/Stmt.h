//===- Stmt.h - Classes for representing statements -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Stmt interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_STMT_H
#define LLVM_CLANG_AST_STMT_H

#include "clang/AST/DeclGroup.h"
#include "clang/AST/StmtIterator.h"
#include "clang/Basic/CapturedStmt.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <string>

namespace llvm {

class FoldingSetNodeID;

} // namespace llvm

namespace clang {

class ASTContext;
class Attr;
class CapturedDecl;
class Decl;
class Expr;
class LabelDecl;
class ODRHash;
class PrinterHelper;
struct PrintingPolicy;
class RecordDecl;
class SourceManager;
class StringLiteral;
class Token;
class VarDecl;

//===----------------------------------------------------------------------===//
// AST classes for statements.
//===----------------------------------------------------------------------===//

/// Stmt - This represents one statement.
///
class alignas(void *) Stmt {
public:
  enum StmtClass {
    NoStmtClass = 0,
#define STMT(CLASS, PARENT) CLASS##Class,
#define STMT_RANGE(BASE, FIRST, LAST) \
        first##BASE##Constant=FIRST##Class, last##BASE##Constant=LAST##Class,
#define LAST_STMT_RANGE(BASE, FIRST, LAST) \
        first##BASE##Constant=FIRST##Class, last##BASE##Constant=LAST##Class
#define ABSTRACT_STMT(STMT)
#include "clang/AST/StmtNodes.inc"
  };

  // Make vanilla 'new' and 'delete' illegal for Stmts.
protected:
  friend class ASTStmtReader;
  friend class ASTStmtWriter;

  void *operator new(size_t bytes) noexcept {
    llvm_unreachable("Stmts cannot be allocated with regular 'new'.");
  }

  void operator delete(void *data) noexcept {
    llvm_unreachable("Stmts cannot be released with regular 'delete'.");
  }

  //===--- Statement bitfields classes ---===//

  class StmtBitfields {
    friend class Stmt;

    /// The statement class.
    unsigned sClass : 8;
  };
  enum { NumStmtBits = 8 };

  class NullStmtBitfields {
    friend class ASTStmtReader;
    friend class ASTStmtWriter;
    friend class NullStmt;

    unsigned : NumStmtBits;

    /// True if the null statement was preceded by an empty macro, e.g:
    /// @code
    ///   #define CALL(x)
    ///   CALL(0);
    /// @endcode
    unsigned HasLeadingEmptyMacro : 1;

    /// The location of the semi-colon.
    SourceLocation SemiLoc;
  };

  class CompoundStmtBitfields {
    friend class ASTStmtReader;
    friend class CompoundStmt;

    unsigned : NumStmtBits;

    unsigned NumStmts : 32 - NumStmtBits;

    /// The location of the opening "{".
    SourceLocation LBraceLoc;
  };

  class LabelStmtBitfields {
    friend class LabelStmt;

    unsigned : NumStmtBits;

    SourceLocation IdentLoc;
  };

  class AttributedStmtBitfields {
    friend class ASTStmtReader;
    friend class AttributedStmt;

    unsigned : NumStmtBits;

    /// Number of attributes.
    unsigned NumAttrs : 32 - NumStmtBits;

    /// The location of the attribute.
    SourceLocation AttrLoc;
  };

  class IfStmtBitfields {
    friend class ASTStmtReader;
    friend class IfStmt;

    unsigned : NumStmtBits;

    /// True if this if statement is a constexpr if.
    unsigned IsConstexpr : 1;

    /// True if this if statement has storage for an else statement.
    unsigned HasElse : 1;

    /// True if this if statement has storage for a variable declaration.
    unsigned HasVar : 1;

    /// True if this if statement has storage for an init statement.
    unsigned HasInit : 1;

    /// The location of the "if".
    SourceLocation IfLoc;
  };

  class SwitchStmtBitfields {
    friend class SwitchStmt;

    unsigned : NumStmtBits;

    /// True if the SwitchStmt has storage for an init statement.
    unsigned HasInit : 1;

    /// True if the SwitchStmt has storage for a condition variable.
    unsigned HasVar : 1;

    /// If the SwitchStmt is a switch on an enum value, records whether all
    /// the enum values were covered by CaseStmts.  The coverage information
    /// value is meant to be a hint for possible clients.
    unsigned AllEnumCasesCovered : 1;

    /// The location of the "switch".
    SourceLocation SwitchLoc;
  };

  class WhileStmtBitfields {
    friend class ASTStmtReader;
    friend class WhileStmt;

    unsigned : NumStmtBits;

    /// True if the WhileStmt has storage for a condition variable.
    unsigned HasVar : 1;

    /// The location of the "while".
    SourceLocation WhileLoc;
  };

  class DoStmtBitfields {
    friend class DoStmt;

    unsigned : NumStmtBits;

    /// The location of the "do".
    SourceLocation DoLoc;
  };

  class ForStmtBitfields {
    friend class ForStmt;

    unsigned : NumStmtBits;

    /// The location of the "for".
    SourceLocation ForLoc;
  };

  class GotoStmtBitfields {
    friend class GotoStmt;
    friend class IndirectGotoStmt;

    unsigned : NumStmtBits;

    /// The location of the "goto".
    SourceLocation GotoLoc;
  };

  class ContinueStmtBitfields {
    friend class ContinueStmt;

    unsigned : NumStmtBits;

    /// The location of the "continue".
    SourceLocation ContinueLoc;
  };

  class BreakStmtBitfields {
    friend class BreakStmt;

    unsigned : NumStmtBits;

    /// The location of the "break".
    SourceLocation BreakLoc;
  };

  class ReturnStmtBitfields {
    friend class ReturnStmt;

    unsigned : NumStmtBits;

    /// True if this ReturnStmt has storage for an NRVO candidate.
    unsigned HasNRVOCandidate : 1;

    /// The location of the "return".
    SourceLocation RetLoc;
  };

  class SwitchCaseBitfields {
    friend class SwitchCase;
    friend class CaseStmt;

    unsigned : NumStmtBits;

    /// Used by CaseStmt to store whether it is a case statement
    /// of the form case LHS ... RHS (a GNU extension).
    unsigned CaseStmtIsGNURange : 1;

    /// The location of the "case" or "default" keyword.
    SourceLocation KeywordLoc;
  };

  //===--- Expression bitfields classes ---===//

  class ExprBitfields {
    friend class ASTStmtReader; // deserialization
    friend class AtomicExpr; // ctor
    friend class BlockDeclRefExpr; // ctor
    friend class CallExpr; // ctor
    friend class CXXConstructExpr; // ctor
    friend class CXXDependentScopeMemberExpr; // ctor
    friend class CXXNewExpr; // ctor
    friend class CXXUnresolvedConstructExpr; // ctor
    friend class DeclRefExpr; // computeDependence
    friend class DependentScopeDeclRefExpr; // ctor
    friend class DesignatedInitExpr; // ctor
    friend class Expr;
    friend class InitListExpr; // ctor
    friend class ObjCArrayLiteral; // ctor
    friend class ObjCDictionaryLiteral; // ctor
    friend class ObjCMessageExpr; // ctor
    friend class OffsetOfExpr; // ctor
    friend class OpaqueValueExpr; // ctor
    friend class OverloadExpr; // ctor
    friend class ParenListExpr; // ctor
    friend class PseudoObjectExpr; // ctor
    friend class ShuffleVectorExpr; // ctor

    unsigned : NumStmtBits;

    unsigned ValueKind : 2;
    unsigned ObjectKind : 3;
    unsigned TypeDependent : 1;
    unsigned ValueDependent : 1;
    unsigned InstantiationDependent : 1;
    unsigned ContainsUnexpandedParameterPack : 1;
  };
  enum { NumExprBits = NumStmtBits + 9 };

  class PredefinedExprBitfields {
    friend class ASTStmtReader;
    friend class PredefinedExpr;

    unsigned : NumExprBits;

    /// The kind of this PredefinedExpr. One of the enumeration values
    /// in PredefinedExpr::IdentKind.
    unsigned Kind : 4;

    /// True if this PredefinedExpr has a trailing "StringLiteral *"
    /// for the predefined identifier.
    unsigned HasFunctionName : 1;

    /// The location of this PredefinedExpr.
    SourceLocation Loc;
  };

  class DeclRefExprBitfields {
    friend class ASTStmtReader; // deserialization
    friend class DeclRefExpr;

    unsigned : NumExprBits;

    unsigned HasQualifier : 1;
    unsigned HasTemplateKWAndArgsInfo : 1;
    unsigned HasFoundDecl : 1;
    unsigned HadMultipleCandidates : 1;
    unsigned RefersToEnclosingVariableOrCapture : 1;

    /// The location of the declaration name itself.
    SourceLocation Loc;
  };

  enum APFloatSemantics {
    IEEEhalf,
    IEEEsingle,
    IEEEdouble,
    x87DoubleExtended,
    IEEEquad,
    PPCDoubleDouble
  };

  class FloatingLiteralBitfields {
    friend class FloatingLiteral;

    unsigned : NumExprBits;

    unsigned Semantics : 3; // Provides semantics for APFloat construction
    unsigned IsExact : 1;
  };

  class StringLiteralBitfields {
    friend class ASTStmtReader;
    friend class StringLiteral;

    unsigned : NumExprBits;

    /// The kind of this string literal.
    /// One of the enumeration values of StringLiteral::StringKind.
    unsigned Kind : 3;

    /// The width of a single character in bytes. Only values of 1, 2,
    /// and 4 bytes are supported. StringLiteral::mapCharByteWidth maps
    /// the target + string kind to the appropriate CharByteWidth.
    unsigned CharByteWidth : 3;

    unsigned IsPascal : 1;

    /// The number of concatenated token this string is made of.
    /// This is the number of trailing SourceLocation.
    unsigned NumConcatenated;
  };

  class CharacterLiteralBitfields {
    friend class CharacterLiteral;

    unsigned : NumExprBits;

    unsigned Kind : 3;
  };

  class UnaryOperatorBitfields {
    friend class UnaryOperator;

    unsigned : NumExprBits;

    unsigned Opc : 5;
    unsigned CanOverflow : 1;

    SourceLocation Loc;
  };

  class UnaryExprOrTypeTraitExprBitfields {
    friend class UnaryExprOrTypeTraitExpr;

    unsigned : NumExprBits;

    unsigned Kind : 3;
    unsigned IsType : 1; // true if operand is a type, false if an expression.
  };

  class ArraySubscriptExprBitfields {
    friend class ArraySubscriptExpr;

    unsigned : NumExprBits;

    SourceLocation RBracketLoc;
  };

  class CallExprBitfields {
    friend class CallExpr;

    unsigned : NumExprBits;

    unsigned NumPreArgs : 1;

    /// True if the callee of the call expression was found using ADL.
    unsigned UsesADL : 1;

    /// Padding used to align OffsetToTrailingObjects to a byte multiple.
    unsigned : 24 - 2 - NumExprBits;

    /// The offset in bytes from the this pointer to the start of the
    /// trailing objects belonging to CallExpr. Intentionally byte sized
    /// for faster access.
    unsigned OffsetToTrailingObjects : 8;
  };
  enum { NumCallExprBits = 32 };

  class MemberExprBitfields {
    friend class MemberExpr;

    unsigned : NumExprBits;

    /// IsArrow - True if this is "X->F", false if this is "X.F".
    unsigned IsArrow : 1;

    /// True if this member expression used a nested-name-specifier to
    /// refer to the member, e.g., "x->Base::f", or found its member via
    /// a using declaration.  When true, a MemberExprNameQualifier
    /// structure is allocated immediately after the MemberExpr.
    unsigned HasQualifierOrFoundDecl : 1;

    /// True if this member expression specified a template keyword
    /// and/or a template argument list explicitly, e.g., x->f<int>,
    /// x->template f, x->template f<int>.
    /// When true, an ASTTemplateKWAndArgsInfo structure and its
    /// TemplateArguments (if any) are present.
    unsigned HasTemplateKWAndArgsInfo : 1;

    /// True if this member expression refers to a method that
    /// was resolved from an overloaded set having size greater than 1.
    unsigned HadMultipleCandidates : 1;

    /// This is the location of the -> or . in the expression.
    SourceLocation OperatorLoc;
  };

  class CastExprBitfields {
    friend class CastExpr;
    friend class ImplicitCastExpr;

    unsigned : NumExprBits;

    unsigned Kind : 6;
    unsigned PartOfExplicitCast : 1; // Only set for ImplicitCastExpr.

    /// The number of CXXBaseSpecifiers in the cast. 14 bits would be enough
    /// here. ([implimits] Direct and indirect base classes [16384]).
    unsigned BasePathSize;
  };

  class BinaryOperatorBitfields {
    friend class BinaryOperator;

    unsigned : NumExprBits;

    unsigned Opc : 6;

    /// This is only meaningful for operations on floating point
    /// types and 0 otherwise.
    unsigned FPFeatures : 3;

    SourceLocation OpLoc;
  };

  class InitListExprBitfields {
    friend class InitListExpr;

    unsigned : NumExprBits;

    /// Whether this initializer list originally had a GNU array-range
    /// designator in it. This is a temporary marker used by CodeGen.
    unsigned HadArrayRangeDesignator : 1;
  };

  class ParenListExprBitfields {
    friend class ASTStmtReader;
    friend class ParenListExpr;

    unsigned : NumExprBits;

    /// The number of expressions in the paren list.
    unsigned NumExprs;
  };

  class PseudoObjectExprBitfields {
    friend class ASTStmtReader; // deserialization
    friend class PseudoObjectExpr;

    unsigned : NumExprBits;

    // These don't need to be particularly wide, because they're
    // strictly limited by the forms of expressions we permit.
    unsigned NumSubExprs : 8;
    unsigned ResultIndex : 32 - 8 - NumExprBits;
  };

  //===--- C++ Expression bitfields classes ---===//

  class CXXOperatorCallExprBitfields {
    friend class ASTStmtReader;
    friend class CXXOperatorCallExpr;

    unsigned : NumCallExprBits;

    /// The kind of this overloaded operator. One of the enumerator
    /// value of OverloadedOperatorKind.
    unsigned OperatorKind : 6;

    // Only meaningful for floating point types.
    unsigned FPFeatures : 3;
  };

  class CXXBoolLiteralExprBitfields {
    friend class CXXBoolLiteralExpr;

    unsigned : NumExprBits;

    /// The value of the boolean literal.
    unsigned Value : 1;

    /// The location of the boolean literal.
    SourceLocation Loc;
  };

  class CXXNullPtrLiteralExprBitfields {
    friend class CXXNullPtrLiteralExpr;

    unsigned : NumExprBits;

    /// The location of the null pointer literal.
    SourceLocation Loc;
  };

  class CXXThisExprBitfields {
    friend class CXXThisExpr;

    unsigned : NumExprBits;

    /// Whether this is an implicit "this".
    unsigned IsImplicit : 1;

    /// The location of the "this".
    SourceLocation Loc;
  };

  class CXXThrowExprBitfields {
    friend class ASTStmtReader;
    friend class CXXThrowExpr;

    unsigned : NumExprBits;

    /// Whether the thrown variable (if any) is in scope.
    unsigned IsThrownVariableInScope : 1;

    /// The location of the "throw".
    SourceLocation ThrowLoc;
  };

  class CXXDefaultArgExprBitfields {
    friend class ASTStmtReader;
    friend class CXXDefaultArgExpr;

    unsigned : NumExprBits;

    /// The location where the default argument expression was used.
    SourceLocation Loc;
  };

  class CXXDefaultInitExprBitfields {
    friend class ASTStmtReader;
    friend class CXXDefaultInitExpr;

    unsigned : NumExprBits;

    /// The location where the default initializer expression was used.
    SourceLocation Loc;
  };

  class CXXScalarValueInitExprBitfields {
    friend class ASTStmtReader;
    friend class CXXScalarValueInitExpr;

    unsigned : NumExprBits;

    SourceLocation RParenLoc;
  };

  class CXXNewExprBitfields {
    friend class ASTStmtReader;
    friend class ASTStmtWriter;
    friend class CXXNewExpr;

    unsigned : NumExprBits;

    /// Was the usage ::new, i.e. is the global new to be used?
    unsigned IsGlobalNew : 1;

    /// Do we allocate an array? If so, the first trailing "Stmt *" is the
    /// size expression.
    unsigned IsArray : 1;

    /// Should the alignment be passed to the allocation function?
    unsigned ShouldPassAlignment : 1;

    /// If this is an array allocation, does the usual deallocation
    /// function for the allocated type want to know the allocated size?
    unsigned UsualArrayDeleteWantsSize : 1;

    /// What kind of initializer do we have? Could be none, parens, or braces.
    /// In storage, we distinguish between "none, and no initializer expr", and
    /// "none, but an implicit initializer expr".
    unsigned StoredInitializationStyle : 2;

    /// True if the allocated type was expressed as a parenthesized type-id.
    unsigned IsParenTypeId : 1;

    /// The number of placement new arguments.
    unsigned NumPlacementArgs;
  };

  class CXXDeleteExprBitfields {
    friend class ASTStmtReader;
    friend class CXXDeleteExpr;

    unsigned : NumExprBits;

    /// Is this a forced global delete, i.e. "::delete"?
    unsigned GlobalDelete : 1;

    /// Is this the array form of delete, i.e. "delete[]"?
    unsigned ArrayForm : 1;

    /// ArrayFormAsWritten can be different from ArrayForm if 'delete' is
    /// applied to pointer-to-array type (ArrayFormAsWritten will be false
    /// while ArrayForm will be true).
    unsigned ArrayFormAsWritten : 1;

    /// Does the usual deallocation function for the element type require
    /// a size_t argument?
    unsigned UsualArrayDeleteWantsSize : 1;

    /// Location of the expression.
    SourceLocation Loc;
  };

  class TypeTraitExprBitfields {
    friend class ASTStmtReader;
    friend class ASTStmtWriter;
    friend class TypeTraitExpr;

    unsigned : NumExprBits;

    /// The kind of type trait, which is a value of a TypeTrait enumerator.
    unsigned Kind : 8;

    /// If this expression is not value-dependent, this indicates whether
    /// the trait evaluated true or false.
    unsigned Value : 1;

    /// The number of arguments to this type trait.
    unsigned NumArgs : 32 - 8 - 1 - NumExprBits;
  };

  class DependentScopeDeclRefExprBitfields {
    friend class ASTStmtReader;
    friend class ASTStmtWriter;
    friend class DependentScopeDeclRefExpr;

    unsigned : NumExprBits;

    /// Whether the name includes info for explicit template
    /// keyword and arguments.
    unsigned HasTemplateKWAndArgsInfo : 1;
  };

  class CXXConstructExprBitfields {
    friend class ASTStmtReader;
    friend class CXXConstructExpr;

    unsigned : NumExprBits;

    unsigned Elidable : 1;
    unsigned HadMultipleCandidates : 1;
    unsigned ListInitialization : 1;
    unsigned StdInitListInitialization : 1;
    unsigned ZeroInitialization : 1;
    unsigned ConstructionKind : 3;

    SourceLocation Loc;
  };

  class ExprWithCleanupsBitfields {
    friend class ASTStmtReader; // deserialization
    friend class ExprWithCleanups;

    unsigned : NumExprBits;

    // When false, it must not have side effects.
    unsigned CleanupsHaveSideEffects : 1;

    unsigned NumObjects : 32 - 1 - NumExprBits;
  };

  class CXXUnresolvedConstructExprBitfields {
    friend class ASTStmtReader;
    friend class CXXUnresolvedConstructExpr;

    unsigned : NumExprBits;

    /// The number of arguments used to construct the type.
    unsigned NumArgs;
  };

  class CXXDependentScopeMemberExprBitfields {
    friend class ASTStmtReader;
    friend class CXXDependentScopeMemberExpr;

    unsigned : NumExprBits;

    /// Whether this member expression used the '->' operator or
    /// the '.' operator.
    unsigned IsArrow : 1;

    /// Whether this member expression has info for explicit template
    /// keyword and arguments.
    unsigned HasTemplateKWAndArgsInfo : 1;

    /// See getFirstQualifierFoundInScope() and the comment listing
    /// the trailing objects.
    unsigned HasFirstQualifierFoundInScope : 1;

    /// The location of the '->' or '.' operator.
    SourceLocation OperatorLoc;
  };

  class OverloadExprBitfields {
    friend class ASTStmtReader;
    friend class OverloadExpr;

    unsigned : NumExprBits;

    /// Whether the name includes info for explicit template
    /// keyword and arguments.
    unsigned HasTemplateKWAndArgsInfo : 1;

    /// Padding used by the derived classes to store various bits. If you
    /// need to add some data here, shrink this padding and add your data
    /// above. NumOverloadExprBits also needs to be updated.
    unsigned : 32 - NumExprBits - 1;

    /// The number of results.
    unsigned NumResults;
  };
  enum { NumOverloadExprBits = NumExprBits + 1 };

  class UnresolvedLookupExprBitfields {
    friend class ASTStmtReader;
    friend class UnresolvedLookupExpr;

    unsigned : NumOverloadExprBits;

    /// True if these lookup results should be extended by
    /// argument-dependent lookup if this is the operand of a function call.
    unsigned RequiresADL : 1;

    /// True if these lookup results are overloaded.  This is pretty trivially
    /// rederivable if we urgently need to kill this field.
    unsigned Overloaded : 1;
  };
  static_assert(sizeof(UnresolvedLookupExprBitfields) <= 4,
                "UnresolvedLookupExprBitfields must be <= than 4 bytes to"
                "avoid trashing OverloadExprBitfields::NumResults!");

  class UnresolvedMemberExprBitfields {
    friend class ASTStmtReader;
    friend class UnresolvedMemberExpr;

    unsigned : NumOverloadExprBits;

    /// Whether this member expression used the '->' operator or
    /// the '.' operator.
    unsigned IsArrow : 1;

    /// Whether the lookup results contain an unresolved using declaration.
    unsigned HasUnresolvedUsing : 1;
  };
  static_assert(sizeof(UnresolvedMemberExprBitfields) <= 4,
                "UnresolvedMemberExprBitfields must be <= than 4 bytes to"
                "avoid trashing OverloadExprBitfields::NumResults!");

  class CXXNoexceptExprBitfields {
    friend class ASTStmtReader;
    friend class CXXNoexceptExpr;

    unsigned : NumExprBits;

    unsigned Value : 1;
  };

  class SubstNonTypeTemplateParmExprBitfields {
    friend class ASTStmtReader;
    friend class SubstNonTypeTemplateParmExpr;

    unsigned : NumExprBits;

    /// The location of the non-type template parameter reference.
    SourceLocation NameLoc;
  };

  //===--- C++ Coroutines TS bitfields classes ---===//

  class CoawaitExprBitfields {
    friend class CoawaitExpr;

    unsigned : NumExprBits;

    unsigned IsImplicit : 1;
  };

  //===--- Obj-C Expression bitfields classes ---===//

  class ObjCIndirectCopyRestoreExprBitfields {
    friend class ObjCIndirectCopyRestoreExpr;

    unsigned : NumExprBits;

    unsigned ShouldCopy : 1;
  };

  //===--- Clang Extensions bitfields classes ---===//

  class OpaqueValueExprBitfields {
    friend class ASTStmtReader;
    friend class OpaqueValueExpr;

    unsigned : NumExprBits;

    /// The OVE is a unique semantic reference to its source expression if this
    /// bit is set to true.
    unsigned IsUnique : 1;

    SourceLocation Loc;
  };

  union {
    // Same order as in StmtNodes.td.
    // Statements
    StmtBitfields StmtBits;
    NullStmtBitfields NullStmtBits;
    CompoundStmtBitfields CompoundStmtBits;
    LabelStmtBitfields LabelStmtBits;
    AttributedStmtBitfields AttributedStmtBits;
    IfStmtBitfields IfStmtBits;
    SwitchStmtBitfields SwitchStmtBits;
    WhileStmtBitfields WhileStmtBits;
    DoStmtBitfields DoStmtBits;
    ForStmtBitfields ForStmtBits;
    GotoStmtBitfields GotoStmtBits;
    ContinueStmtBitfields ContinueStmtBits;
    BreakStmtBitfields BreakStmtBits;
    ReturnStmtBitfields ReturnStmtBits;
    SwitchCaseBitfields SwitchCaseBits;

    // Expressions
    ExprBitfields ExprBits;
    PredefinedExprBitfields PredefinedExprBits;
    DeclRefExprBitfields DeclRefExprBits;
    FloatingLiteralBitfields FloatingLiteralBits;
    StringLiteralBitfields StringLiteralBits;
    CharacterLiteralBitfields CharacterLiteralBits;
    UnaryOperatorBitfields UnaryOperatorBits;
    UnaryExprOrTypeTraitExprBitfields UnaryExprOrTypeTraitExprBits;
    ArraySubscriptExprBitfields ArraySubscriptExprBits;
    CallExprBitfields CallExprBits;
    MemberExprBitfields MemberExprBits;
    CastExprBitfields CastExprBits;
    BinaryOperatorBitfields BinaryOperatorBits;
    InitListExprBitfields InitListExprBits;
    ParenListExprBitfields ParenListExprBits;
    PseudoObjectExprBitfields PseudoObjectExprBits;

    // C++ Expressions
    CXXOperatorCallExprBitfields CXXOperatorCallExprBits;
    CXXBoolLiteralExprBitfields CXXBoolLiteralExprBits;
    CXXNullPtrLiteralExprBitfields CXXNullPtrLiteralExprBits;
    CXXThisExprBitfields CXXThisExprBits;
    CXXThrowExprBitfields CXXThrowExprBits;
    CXXDefaultArgExprBitfields CXXDefaultArgExprBits;
    CXXDefaultInitExprBitfields CXXDefaultInitExprBits;
    CXXScalarValueInitExprBitfields CXXScalarValueInitExprBits;
    CXXNewExprBitfields CXXNewExprBits;
    CXXDeleteExprBitfields CXXDeleteExprBits;
    TypeTraitExprBitfields TypeTraitExprBits;
    DependentScopeDeclRefExprBitfields DependentScopeDeclRefExprBits;
    CXXConstructExprBitfields CXXConstructExprBits;
    ExprWithCleanupsBitfields ExprWithCleanupsBits;
    CXXUnresolvedConstructExprBitfields CXXUnresolvedConstructExprBits;
    CXXDependentScopeMemberExprBitfields CXXDependentScopeMemberExprBits;
    OverloadExprBitfields OverloadExprBits;
    UnresolvedLookupExprBitfields UnresolvedLookupExprBits;
    UnresolvedMemberExprBitfields UnresolvedMemberExprBits;
    CXXNoexceptExprBitfields CXXNoexceptExprBits;
    SubstNonTypeTemplateParmExprBitfields SubstNonTypeTemplateParmExprBits;

    // C++ Coroutines TS expressions
    CoawaitExprBitfields CoawaitBits;

    // Obj-C Expressions
    ObjCIndirectCopyRestoreExprBitfields ObjCIndirectCopyRestoreExprBits;

    // Clang Extensions
    OpaqueValueExprBitfields OpaqueValueExprBits;
  };

public:
  // Only allow allocation of Stmts using the allocator in ASTContext
  // or by doing a placement new.
  void* operator new(size_t bytes, const ASTContext& C,
                     unsigned alignment = 8);

  void* operator new(size_t bytes, const ASTContext* C,
                     unsigned alignment = 8) {
    return operator new(bytes, *C, alignment);
  }

  void *operator new(size_t bytes, void *mem) noexcept { return mem; }

  void operator delete(void *, const ASTContext &, unsigned) noexcept {}
  void operator delete(void *, const ASTContext *, unsigned) noexcept {}
  void operator delete(void *, size_t) noexcept {}
  void operator delete(void *, void *) noexcept {}

public:
  /// A placeholder type used to construct an empty shell of a
  /// type, that will be filled in later (e.g., by some
  /// de-serialization).
  struct EmptyShell {};

protected:
  /// Iterator for iterating over Stmt * arrays that contain only Expr *
  ///
  /// This is needed because AST nodes use Stmt* arrays to store
  /// references to children (to be compatible with StmtIterator).
  struct ExprIterator
      : llvm::iterator_adaptor_base<ExprIterator, Stmt **,
                                    std::random_access_iterator_tag, Expr *> {
    ExprIterator() : iterator_adaptor_base(nullptr) {}
    ExprIterator(Stmt **I) : iterator_adaptor_base(I) {}

    reference operator*() const {
      assert((*I)->getStmtClass() >= firstExprConstant &&
             (*I)->getStmtClass() <= lastExprConstant);
      return *reinterpret_cast<Expr **>(I);
    }
  };

  /// Const iterator for iterating over Stmt * arrays that contain only Expr *
  struct ConstExprIterator
      : llvm::iterator_adaptor_base<ConstExprIterator, const Stmt *const *,
                                    std::random_access_iterator_tag,
                                    const Expr *const> {
    ConstExprIterator() : iterator_adaptor_base(nullptr) {}
    ConstExprIterator(const Stmt *const *I) : iterator_adaptor_base(I) {}

    reference operator*() const {
      assert((*I)->getStmtClass() >= firstExprConstant &&
             (*I)->getStmtClass() <= lastExprConstant);
      return *reinterpret_cast<const Expr *const *>(I);
    }
  };

private:
  /// Whether statistic collection is enabled.
  static bool StatisticsEnabled;

protected:
  /// Construct an empty statement.
  explicit Stmt(StmtClass SC, EmptyShell) : Stmt(SC) {}

public:
  Stmt(StmtClass SC) {
    static_assert(sizeof(*this) <= 8,
                  "changing bitfields changed sizeof(Stmt)");
    static_assert(sizeof(*this) % alignof(void *) == 0,
                  "Insufficient alignment!");
    StmtBits.sClass = SC;
    if (StatisticsEnabled) Stmt::addStmtClass(SC);
  }

  StmtClass getStmtClass() const {
    return static_cast<StmtClass>(StmtBits.sClass);
  }

  const char *getStmtClassName() const;

  /// SourceLocation tokens are not useful in isolation - they are low level
  /// value objects created/interpreted by SourceManager. We assume AST
  /// clients will have a pointer to the respective SourceManager.
  SourceRange getSourceRange() const LLVM_READONLY;
  SourceLocation getBeginLoc() const LLVM_READONLY;
  SourceLocation getEndLoc() const LLVM_READONLY;

  // global temp stats (until we have a per-module visitor)
  static void addStmtClass(const StmtClass s);
  static void EnableStatistics();
  static void PrintStats();

  /// Dumps the specified AST fragment and all subtrees to
  /// \c llvm::errs().
  void dump() const;
  void dump(SourceManager &SM) const;
  void dump(raw_ostream &OS, SourceManager &SM) const;
  void dump(raw_ostream &OS) const;

  /// \return Unique reproducible object identifier
  int64_t getID(const ASTContext &Context) const;

  /// dumpColor - same as dump(), but forces color highlighting.
  void dumpColor() const;

  /// dumpPretty/printPretty - These two methods do a "pretty print" of the AST
  /// back to its original source language syntax.
  void dumpPretty(const ASTContext &Context) const;
  void printPretty(raw_ostream &OS, PrinterHelper *Helper,
                   const PrintingPolicy &Policy, unsigned Indentation = 0,
                   StringRef NewlineSymbol = "\n",
                   const ASTContext *Context = nullptr) const;

  /// viewAST - Visualize an AST rooted at this Stmt* using GraphViz.  Only
  ///   works on systems with GraphViz (Mac OS X) or dot+gv installed.
  void viewAST() const;

  /// Skip past any implicit AST nodes which might surround this
  /// statement, such as ExprWithCleanups or ImplicitCastExpr nodes.
  Stmt *IgnoreImplicit();
  const Stmt *IgnoreImplicit() const {
    return const_cast<Stmt *>(this)->IgnoreImplicit();
  }

  /// Skip no-op (attributed, compound) container stmts and skip captured
  /// stmt at the top, if \a IgnoreCaptured is true.
  Stmt *IgnoreContainers(bool IgnoreCaptured = false);
  const Stmt *IgnoreContainers(bool IgnoreCaptured = false) const {
    return const_cast<Stmt *>(this)->IgnoreContainers(IgnoreCaptured);
  }

  const Stmt *stripLabelLikeStatements() const;
  Stmt *stripLabelLikeStatements() {
    return const_cast<Stmt*>(
      const_cast<const Stmt*>(this)->stripLabelLikeStatements());
  }

  /// Child Iterators: All subclasses must implement 'children'
  /// to permit easy iteration over the substatements/subexpessions of an
  /// AST node.  This permits easy iteration over all nodes in the AST.
  using child_iterator = StmtIterator;
  using const_child_iterator = ConstStmtIterator;

  using child_range = llvm::iterator_range<child_iterator>;
  using const_child_range = llvm::iterator_range<const_child_iterator>;

  child_range children();

  const_child_range children() const {
    auto Children = const_cast<Stmt *>(this)->children();
    return const_child_range(Children.begin(), Children.end());
  }

  child_iterator child_begin() { return children().begin(); }
  child_iterator child_end() { return children().end(); }

  const_child_iterator child_begin() const { return children().begin(); }
  const_child_iterator child_end() const { return children().end(); }

  /// Produce a unique representation of the given statement.
  ///
  /// \param ID once the profiling operation is complete, will contain
  /// the unique representation of the given statement.
  ///
  /// \param Context the AST context in which the statement resides
  ///
  /// \param Canonical whether the profile should be based on the canonical
  /// representation of this statement (e.g., where non-type template
  /// parameters are identified by index/level rather than their
  /// declaration pointers) or the exact representation of the statement as
  /// written in the source.
  void Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context,
               bool Canonical) const;

  /// Calculate a unique representation for a statement that is
  /// stable across compiler invocations.
  ///
  /// \param ID profile information will be stored in ID.
  ///
  /// \param Hash an ODRHash object which will be called where pointers would
  /// have been used in the Profile function.
  void ProcessODRHash(llvm::FoldingSetNodeID &ID, ODRHash& Hash) const;
};

/// DeclStmt - Adaptor class for mixing declarations with statements and
/// expressions. For example, CompoundStmt mixes statements, expressions
/// and declarations (variables, types). Another example is ForStmt, where
/// the first statement can be an expression or a declaration.
class DeclStmt : public Stmt {
  DeclGroupRef DG;
  SourceLocation StartLoc, EndLoc;

public:
  DeclStmt(DeclGroupRef dg, SourceLocation startLoc, SourceLocation endLoc)
      : Stmt(DeclStmtClass), DG(dg), StartLoc(startLoc), EndLoc(endLoc) {}

  /// Build an empty declaration statement.
  explicit DeclStmt(EmptyShell Empty) : Stmt(DeclStmtClass, Empty) {}

  /// isSingleDecl - This method returns true if this DeclStmt refers
  /// to a single Decl.
  bool isSingleDecl() const { return DG.isSingleDecl(); }

  const Decl *getSingleDecl() const { return DG.getSingleDecl(); }
  Decl *getSingleDecl() { return DG.getSingleDecl(); }

  const DeclGroupRef getDeclGroup() const { return DG; }
  DeclGroupRef getDeclGroup() { return DG; }
  void setDeclGroup(DeclGroupRef DGR) { DG = DGR; }

  void setStartLoc(SourceLocation L) { StartLoc = L; }
  SourceLocation getEndLoc() const { return EndLoc; }
  void setEndLoc(SourceLocation L) { EndLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return StartLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DeclStmtClass;
  }

  // Iterators over subexpressions.
  child_range children() {
    return child_range(child_iterator(DG.begin(), DG.end()),
                       child_iterator(DG.end(), DG.end()));
  }

  using decl_iterator = DeclGroupRef::iterator;
  using const_decl_iterator = DeclGroupRef::const_iterator;
  using decl_range = llvm::iterator_range<decl_iterator>;
  using decl_const_range = llvm::iterator_range<const_decl_iterator>;

  decl_range decls() { return decl_range(decl_begin(), decl_end()); }

  decl_const_range decls() const {
    return decl_const_range(decl_begin(), decl_end());
  }

  decl_iterator decl_begin() { return DG.begin(); }
  decl_iterator decl_end() { return DG.end(); }
  const_decl_iterator decl_begin() const { return DG.begin(); }
  const_decl_iterator decl_end() const { return DG.end(); }

  using reverse_decl_iterator = std::reverse_iterator<decl_iterator>;

  reverse_decl_iterator decl_rbegin() {
    return reverse_decl_iterator(decl_end());
  }

  reverse_decl_iterator decl_rend() {
    return reverse_decl_iterator(decl_begin());
  }
};

/// NullStmt - This is the null statement ";": C99 6.8.3p3.
///
class NullStmt : public Stmt {
public:
  NullStmt(SourceLocation L, bool hasLeadingEmptyMacro = false)
      : Stmt(NullStmtClass) {
    NullStmtBits.HasLeadingEmptyMacro = hasLeadingEmptyMacro;
    setSemiLoc(L);
  }

  /// Build an empty null statement.
  explicit NullStmt(EmptyShell Empty) : Stmt(NullStmtClass, Empty) {}

  SourceLocation getSemiLoc() const { return NullStmtBits.SemiLoc; }
  void setSemiLoc(SourceLocation L) { NullStmtBits.SemiLoc = L; }

  bool hasLeadingEmptyMacro() const {
    return NullStmtBits.HasLeadingEmptyMacro;
  }

  SourceLocation getBeginLoc() const { return getSemiLoc(); }
  SourceLocation getEndLoc() const { return getSemiLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == NullStmtClass;
  }

  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
};

/// CompoundStmt - This represents a group of statements like { stmt stmt }.
class CompoundStmt final : public Stmt,
                           private llvm::TrailingObjects<CompoundStmt, Stmt *> {
  friend class ASTStmtReader;
  friend TrailingObjects;

  /// The location of the closing "}". LBraceLoc is stored in CompoundStmtBits.
  SourceLocation RBraceLoc;

  CompoundStmt(ArrayRef<Stmt *> Stmts, SourceLocation LB, SourceLocation RB);
  explicit CompoundStmt(EmptyShell Empty) : Stmt(CompoundStmtClass, Empty) {}

  void setStmts(ArrayRef<Stmt *> Stmts);

public:
  static CompoundStmt *Create(const ASTContext &C, ArrayRef<Stmt *> Stmts,
                              SourceLocation LB, SourceLocation RB);

  // Build an empty compound statement with a location.
  explicit CompoundStmt(SourceLocation Loc)
      : Stmt(CompoundStmtClass), RBraceLoc(Loc) {
    CompoundStmtBits.NumStmts = 0;
    CompoundStmtBits.LBraceLoc = Loc;
  }

  // Build an empty compound statement.
  static CompoundStmt *CreateEmpty(const ASTContext &C, unsigned NumStmts);

  bool body_empty() const { return CompoundStmtBits.NumStmts == 0; }
  unsigned size() const { return CompoundStmtBits.NumStmts; }

  using body_iterator = Stmt **;
  using body_range = llvm::iterator_range<body_iterator>;

  body_range body() { return body_range(body_begin(), body_end()); }
  body_iterator body_begin() { return getTrailingObjects<Stmt *>(); }
  body_iterator body_end() { return body_begin() + size(); }
  Stmt *body_front() { return !body_empty() ? body_begin()[0] : nullptr; }

  Stmt *body_back() {
    return !body_empty() ? body_begin()[size() - 1] : nullptr;
  }

  void setLastStmt(Stmt *S) {
    assert(!body_empty() && "setLastStmt");
    body_begin()[size() - 1] = S;
  }

  using const_body_iterator = Stmt *const *;
  using body_const_range = llvm::iterator_range<const_body_iterator>;

  body_const_range body() const {
    return body_const_range(body_begin(), body_end());
  }

  const_body_iterator body_begin() const {
    return getTrailingObjects<Stmt *>();
  }

  const_body_iterator body_end() const { return body_begin() + size(); }

  const Stmt *body_front() const {
    return !body_empty() ? body_begin()[0] : nullptr;
  }

  const Stmt *body_back() const {
    return !body_empty() ? body_begin()[size() - 1] : nullptr;
  }

  using reverse_body_iterator = std::reverse_iterator<body_iterator>;

  reverse_body_iterator body_rbegin() {
    return reverse_body_iterator(body_end());
  }

  reverse_body_iterator body_rend() {
    return reverse_body_iterator(body_begin());
  }

  using const_reverse_body_iterator =
      std::reverse_iterator<const_body_iterator>;

  const_reverse_body_iterator body_rbegin() const {
    return const_reverse_body_iterator(body_end());
  }

  const_reverse_body_iterator body_rend() const {
    return const_reverse_body_iterator(body_begin());
  }

  SourceLocation getBeginLoc() const { return CompoundStmtBits.LBraceLoc; }
  SourceLocation getEndLoc() const { return RBraceLoc; }

  SourceLocation getLBracLoc() const { return CompoundStmtBits.LBraceLoc; }
  SourceLocation getRBracLoc() const { return RBraceLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CompoundStmtClass;
  }

  // Iterators
  child_range children() { return child_range(body_begin(), body_end()); }

  const_child_range children() const {
    return const_child_range(body_begin(), body_end());
  }
};

// SwitchCase is the base class for CaseStmt and DefaultStmt,
class SwitchCase : public Stmt {
protected:
  /// The location of the ":".
  SourceLocation ColonLoc;

  // The location of the "case" or "default" keyword. Stored in SwitchCaseBits.
  // SourceLocation KeywordLoc;

  /// A pointer to the following CaseStmt or DefaultStmt class,
  /// used by SwitchStmt.
  SwitchCase *NextSwitchCase = nullptr;

  SwitchCase(StmtClass SC, SourceLocation KWLoc, SourceLocation ColonLoc)
      : Stmt(SC), ColonLoc(ColonLoc) {
    setKeywordLoc(KWLoc);
  }

  SwitchCase(StmtClass SC, EmptyShell) : Stmt(SC) {}

public:
  const SwitchCase *getNextSwitchCase() const { return NextSwitchCase; }
  SwitchCase *getNextSwitchCase() { return NextSwitchCase; }
  void setNextSwitchCase(SwitchCase *SC) { NextSwitchCase = SC; }

  SourceLocation getKeywordLoc() const { return SwitchCaseBits.KeywordLoc; }
  void setKeywordLoc(SourceLocation L) { SwitchCaseBits.KeywordLoc = L; }
  SourceLocation getColonLoc() const { return ColonLoc; }
  void setColonLoc(SourceLocation L) { ColonLoc = L; }

  inline Stmt *getSubStmt();
  const Stmt *getSubStmt() const {
    return const_cast<SwitchCase *>(this)->getSubStmt();
  }

  SourceLocation getBeginLoc() const { return getKeywordLoc(); }
  inline SourceLocation getEndLoc() const LLVM_READONLY;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CaseStmtClass ||
           T->getStmtClass() == DefaultStmtClass;
  }
};

/// CaseStmt - Represent a case statement. It can optionally be a GNU case
/// statement of the form LHS ... RHS representing a range of cases.
class CaseStmt final
    : public SwitchCase,
      private llvm::TrailingObjects<CaseStmt, Stmt *, SourceLocation> {
  friend TrailingObjects;

  // CaseStmt is followed by several trailing objects, some of which optional.
  // Note that it would be more convenient to put the optional trailing objects
  // at the end but this would impact children().
  // The trailing objects are in order:
  //
  // * A "Stmt *" for the LHS of the case statement. Always present.
  //
  // * A "Stmt *" for the RHS of the case statement. This is a GNU extension
  //   which allow ranges in cases statement of the form LHS ... RHS.
  //   Present if and only if caseStmtIsGNURange() is true.
  //
  // * A "Stmt *" for the substatement of the case statement. Always present.
  //
  // * A SourceLocation for the location of the ... if this is a case statement
  //   with a range. Present if and only if caseStmtIsGNURange() is true.
  enum { LhsOffset = 0, SubStmtOffsetFromRhs = 1 };
  enum { NumMandatoryStmtPtr = 2 };

  unsigned numTrailingObjects(OverloadToken<Stmt *>) const {
    return NumMandatoryStmtPtr + caseStmtIsGNURange();
  }

  unsigned numTrailingObjects(OverloadToken<SourceLocation>) const {
    return caseStmtIsGNURange();
  }

  unsigned lhsOffset() const { return LhsOffset; }
  unsigned rhsOffset() const { return LhsOffset + caseStmtIsGNURange(); }
  unsigned subStmtOffset() const { return rhsOffset() + SubStmtOffsetFromRhs; }

  /// Build a case statement assuming that the storage for the
  /// trailing objects has been properly allocated.
  CaseStmt(Expr *lhs, Expr *rhs, SourceLocation caseLoc,
           SourceLocation ellipsisLoc, SourceLocation colonLoc)
      : SwitchCase(CaseStmtClass, caseLoc, colonLoc) {
    // Handle GNU case statements of the form LHS ... RHS.
    bool IsGNURange = rhs != nullptr;
    SwitchCaseBits.CaseStmtIsGNURange = IsGNURange;
    setLHS(lhs);
    setSubStmt(nullptr);
    if (IsGNURange) {
      setRHS(rhs);
      setEllipsisLoc(ellipsisLoc);
    }
  }

  /// Build an empty switch case statement.
  explicit CaseStmt(EmptyShell Empty, bool CaseStmtIsGNURange)
      : SwitchCase(CaseStmtClass, Empty) {
    SwitchCaseBits.CaseStmtIsGNURange = CaseStmtIsGNURange;
  }

public:
  /// Build a case statement.
  static CaseStmt *Create(const ASTContext &Ctx, Expr *lhs, Expr *rhs,
                          SourceLocation caseLoc, SourceLocation ellipsisLoc,
                          SourceLocation colonLoc);

  /// Build an empty case statement.
  static CaseStmt *CreateEmpty(const ASTContext &Ctx, bool CaseStmtIsGNURange);

  /// True if this case statement is of the form case LHS ... RHS, which
  /// is a GNU extension. In this case the RHS can be obtained with getRHS()
  /// and the location of the ellipsis can be obtained with getEllipsisLoc().
  bool caseStmtIsGNURange() const { return SwitchCaseBits.CaseStmtIsGNURange; }

  SourceLocation getCaseLoc() const { return getKeywordLoc(); }
  void setCaseLoc(SourceLocation L) { setKeywordLoc(L); }

  /// Get the location of the ... in a case statement of the form LHS ... RHS.
  SourceLocation getEllipsisLoc() const {
    return caseStmtIsGNURange() ? *getTrailingObjects<SourceLocation>()
                                : SourceLocation();
  }

  /// Set the location of the ... in a case statement of the form LHS ... RHS.
  /// Assert that this case statement is of this form.
  void setEllipsisLoc(SourceLocation L) {
    assert(
        caseStmtIsGNURange() &&
        "setEllipsisLoc but this is not a case stmt of the form LHS ... RHS!");
    *getTrailingObjects<SourceLocation>() = L;
  }

  Expr *getLHS() {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[lhsOffset()]);
  }

  const Expr *getLHS() const {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[lhsOffset()]);
  }

  void setLHS(Expr *Val) {
    getTrailingObjects<Stmt *>()[lhsOffset()] = reinterpret_cast<Stmt *>(Val);
  }

  Expr *getRHS() {
    return caseStmtIsGNURange() ? reinterpret_cast<Expr *>(
                                      getTrailingObjects<Stmt *>()[rhsOffset()])
                                : nullptr;
  }

  const Expr *getRHS() const {
    return caseStmtIsGNURange() ? reinterpret_cast<Expr *>(
                                      getTrailingObjects<Stmt *>()[rhsOffset()])
                                : nullptr;
  }

  void setRHS(Expr *Val) {
    assert(caseStmtIsGNURange() &&
           "setRHS but this is not a case stmt of the form LHS ... RHS!");
    getTrailingObjects<Stmt *>()[rhsOffset()] = reinterpret_cast<Stmt *>(Val);
  }

  Stmt *getSubStmt() { return getTrailingObjects<Stmt *>()[subStmtOffset()]; }
  const Stmt *getSubStmt() const {
    return getTrailingObjects<Stmt *>()[subStmtOffset()];
  }

  void setSubStmt(Stmt *S) {
    getTrailingObjects<Stmt *>()[subStmtOffset()] = S;
  }

  SourceLocation getBeginLoc() const { return getKeywordLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    // Handle deeply nested case statements with iteration instead of recursion.
    const CaseStmt *CS = this;
    while (const auto *CS2 = dyn_cast<CaseStmt>(CS->getSubStmt()))
      CS = CS2;

    return CS->getSubStmt()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CaseStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(getTrailingObjects<Stmt *>(),
                       getTrailingObjects<Stmt *>() +
                           numTrailingObjects(OverloadToken<Stmt *>()));
  }
};

class DefaultStmt : public SwitchCase {
  Stmt *SubStmt;

public:
  DefaultStmt(SourceLocation DL, SourceLocation CL, Stmt *substmt)
      : SwitchCase(DefaultStmtClass, DL, CL), SubStmt(substmt) {}

  /// Build an empty default statement.
  explicit DefaultStmt(EmptyShell Empty)
      : SwitchCase(DefaultStmtClass, Empty) {}

  Stmt *getSubStmt() { return SubStmt; }
  const Stmt *getSubStmt() const { return SubStmt; }
  void setSubStmt(Stmt *S) { SubStmt = S; }

  SourceLocation getDefaultLoc() const { return getKeywordLoc(); }
  void setDefaultLoc(SourceLocation L) { setKeywordLoc(L); }

  SourceLocation getBeginLoc() const { return getKeywordLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return SubStmt->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DefaultStmtClass;
  }

  // Iterators
  child_range children() { return child_range(&SubStmt, &SubStmt + 1); }
};

SourceLocation SwitchCase::getEndLoc() const {
  if (const auto *CS = dyn_cast<CaseStmt>(this))
    return CS->getEndLoc();
  else if (const auto *DS = dyn_cast<DefaultStmt>(this))
    return DS->getEndLoc();
  llvm_unreachable("SwitchCase is neither a CaseStmt nor a DefaultStmt!");
}

Stmt *SwitchCase::getSubStmt() {
  if (auto *CS = dyn_cast<CaseStmt>(this))
    return CS->getSubStmt();
  else if (auto *DS = dyn_cast<DefaultStmt>(this))
    return DS->getSubStmt();
  llvm_unreachable("SwitchCase is neither a CaseStmt nor a DefaultStmt!");
}

/// LabelStmt - Represents a label, which has a substatement.  For example:
///    foo: return;
class LabelStmt : public Stmt {
  LabelDecl *TheDecl;
  Stmt *SubStmt;

public:
  /// Build a label statement.
  LabelStmt(SourceLocation IL, LabelDecl *D, Stmt *substmt)
      : Stmt(LabelStmtClass), TheDecl(D), SubStmt(substmt) {
    setIdentLoc(IL);
  }

  /// Build an empty label statement.
  explicit LabelStmt(EmptyShell Empty) : Stmt(LabelStmtClass, Empty) {}

  SourceLocation getIdentLoc() const { return LabelStmtBits.IdentLoc; }
  void setIdentLoc(SourceLocation L) { LabelStmtBits.IdentLoc = L; }

  LabelDecl *getDecl() const { return TheDecl; }
  void setDecl(LabelDecl *D) { TheDecl = D; }

  const char *getName() const;
  Stmt *getSubStmt() { return SubStmt; }

  const Stmt *getSubStmt() const { return SubStmt; }
  void setSubStmt(Stmt *SS) { SubStmt = SS; }

  SourceLocation getBeginLoc() const { return getIdentLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return SubStmt->getEndLoc();}

  child_range children() { return child_range(&SubStmt, &SubStmt + 1); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == LabelStmtClass;
  }
};

/// Represents an attribute applied to a statement.
///
/// Represents an attribute applied to a statement. For example:
///   [[omp::for(...)]] for (...) { ... }
class AttributedStmt final
    : public Stmt,
      private llvm::TrailingObjects<AttributedStmt, const Attr *> {
  friend class ASTStmtReader;
  friend TrailingObjects;

  Stmt *SubStmt;

  AttributedStmt(SourceLocation Loc, ArrayRef<const Attr *> Attrs,
                 Stmt *SubStmt)
      : Stmt(AttributedStmtClass), SubStmt(SubStmt) {
    AttributedStmtBits.NumAttrs = Attrs.size();
    AttributedStmtBits.AttrLoc = Loc;
    std::copy(Attrs.begin(), Attrs.end(), getAttrArrayPtr());
  }

  explicit AttributedStmt(EmptyShell Empty, unsigned NumAttrs)
      : Stmt(AttributedStmtClass, Empty) {
    AttributedStmtBits.NumAttrs = NumAttrs;
    AttributedStmtBits.AttrLoc = SourceLocation{};
    std::fill_n(getAttrArrayPtr(), NumAttrs, nullptr);
  }

  const Attr *const *getAttrArrayPtr() const {
    return getTrailingObjects<const Attr *>();
  }
  const Attr **getAttrArrayPtr() { return getTrailingObjects<const Attr *>(); }

public:
  static AttributedStmt *Create(const ASTContext &C, SourceLocation Loc,
                                ArrayRef<const Attr *> Attrs, Stmt *SubStmt);

  // Build an empty attributed statement.
  static AttributedStmt *CreateEmpty(const ASTContext &C, unsigned NumAttrs);

  SourceLocation getAttrLoc() const { return AttributedStmtBits.AttrLoc; }
  ArrayRef<const Attr *> getAttrs() const {
    return llvm::makeArrayRef(getAttrArrayPtr(), AttributedStmtBits.NumAttrs);
  }

  Stmt *getSubStmt() { return SubStmt; }
  const Stmt *getSubStmt() const { return SubStmt; }

  SourceLocation getBeginLoc() const { return getAttrLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return SubStmt->getEndLoc();}

  child_range children() { return child_range(&SubStmt, &SubStmt + 1); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == AttributedStmtClass;
  }
};

/// IfStmt - This represents an if/then/else.
class IfStmt final
    : public Stmt,
      private llvm::TrailingObjects<IfStmt, Stmt *, SourceLocation> {
  friend TrailingObjects;

  // IfStmt is followed by several trailing objects, some of which optional.
  // Note that it would be more convenient to put the optional trailing
  // objects at then end but this would change the order of the children.
  // The trailing objects are in order:
  //
  // * A "Stmt *" for the init statement.
  //    Present if and only if hasInitStorage().
  //
  // * A "Stmt *" for the condition variable.
  //    Present if and only if hasVarStorage(). This is in fact a "DeclStmt *".
  //
  // * A "Stmt *" for the condition.
  //    Always present. This is in fact a "Expr *".
  //
  // * A "Stmt *" for the then statement.
  //    Always present.
  //
  // * A "Stmt *" for the else statement.
  //    Present if and only if hasElseStorage().
  //
  // * A "SourceLocation" for the location of the "else".
  //    Present if and only if hasElseStorage().
  enum { InitOffset = 0, ThenOffsetFromCond = 1, ElseOffsetFromCond = 2 };
  enum { NumMandatoryStmtPtr = 2 };

  unsigned numTrailingObjects(OverloadToken<Stmt *>) const {
    return NumMandatoryStmtPtr + hasElseStorage() + hasVarStorage() +
           hasInitStorage();
  }

  unsigned numTrailingObjects(OverloadToken<SourceLocation>) const {
    return hasElseStorage();
  }

  unsigned initOffset() const { return InitOffset; }
  unsigned varOffset() const { return InitOffset + hasInitStorage(); }
  unsigned condOffset() const {
    return InitOffset + hasInitStorage() + hasVarStorage();
  }
  unsigned thenOffset() const { return condOffset() + ThenOffsetFromCond; }
  unsigned elseOffset() const { return condOffset() + ElseOffsetFromCond; }

  /// Build an if/then/else statement.
  IfStmt(const ASTContext &Ctx, SourceLocation IL, bool IsConstexpr, Stmt *Init,
         VarDecl *Var, Expr *Cond, Stmt *Then, SourceLocation EL, Stmt *Else);

  /// Build an empty if/then/else statement.
  explicit IfStmt(EmptyShell Empty, bool HasElse, bool HasVar, bool HasInit);

public:
  /// Create an IfStmt.
  static IfStmt *Create(const ASTContext &Ctx, SourceLocation IL,
                        bool IsConstexpr, Stmt *Init, VarDecl *Var, Expr *Cond,
                        Stmt *Then, SourceLocation EL = SourceLocation(),
                        Stmt *Else = nullptr);

  /// Create an empty IfStmt optionally with storage for an else statement,
  /// condition variable and init expression.
  static IfStmt *CreateEmpty(const ASTContext &Ctx, bool HasElse, bool HasVar,
                             bool HasInit);

  /// True if this IfStmt has the storage for an init statement.
  bool hasInitStorage() const { return IfStmtBits.HasInit; }

  /// True if this IfStmt has storage for a variable declaration.
  bool hasVarStorage() const { return IfStmtBits.HasVar; }

  /// True if this IfStmt has storage for an else statement.
  bool hasElseStorage() const { return IfStmtBits.HasElse; }

  Expr *getCond() {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[condOffset()]);
  }

  const Expr *getCond() const {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[condOffset()]);
  }

  void setCond(Expr *Cond) {
    getTrailingObjects<Stmt *>()[condOffset()] = reinterpret_cast<Stmt *>(Cond);
  }

  Stmt *getThen() { return getTrailingObjects<Stmt *>()[thenOffset()]; }
  const Stmt *getThen() const {
    return getTrailingObjects<Stmt *>()[thenOffset()];
  }

  void setThen(Stmt *Then) {
    getTrailingObjects<Stmt *>()[thenOffset()] = Then;
  }

  Stmt *getElse() {
    return hasElseStorage() ? getTrailingObjects<Stmt *>()[elseOffset()]
                            : nullptr;
  }

  const Stmt *getElse() const {
    return hasElseStorage() ? getTrailingObjects<Stmt *>()[elseOffset()]
                            : nullptr;
  }

  void setElse(Stmt *Else) {
    assert(hasElseStorage() &&
           "This if statement has no storage for an else statement!");
    getTrailingObjects<Stmt *>()[elseOffset()] = Else;
  }

  /// Retrieve the variable declared in this "if" statement, if any.
  ///
  /// In the following example, "x" is the condition variable.
  /// \code
  /// if (int x = foo()) {
  ///   printf("x is %d", x);
  /// }
  /// \endcode
  VarDecl *getConditionVariable();
  const VarDecl *getConditionVariable() const {
    return const_cast<IfStmt *>(this)->getConditionVariable();
  }

  /// Set the condition variable for this if statement.
  /// The if statement must have storage for the condition variable.
  void setConditionVariable(const ASTContext &Ctx, VarDecl *V);

  /// If this IfStmt has a condition variable, return the faux DeclStmt
  /// associated with the creation of that condition variable.
  DeclStmt *getConditionVariableDeclStmt() {
    return hasVarStorage() ? static_cast<DeclStmt *>(
                                 getTrailingObjects<Stmt *>()[varOffset()])
                           : nullptr;
  }

  const DeclStmt *getConditionVariableDeclStmt() const {
    return hasVarStorage() ? static_cast<DeclStmt *>(
                                 getTrailingObjects<Stmt *>()[varOffset()])
                           : nullptr;
  }

  Stmt *getInit() {
    return hasInitStorage() ? getTrailingObjects<Stmt *>()[initOffset()]
                            : nullptr;
  }

  const Stmt *getInit() const {
    return hasInitStorage() ? getTrailingObjects<Stmt *>()[initOffset()]
                            : nullptr;
  }

  void setInit(Stmt *Init) {
    assert(hasInitStorage() &&
           "This if statement has no storage for an init statement!");
    getTrailingObjects<Stmt *>()[initOffset()] = Init;
  }

  SourceLocation getIfLoc() const { return IfStmtBits.IfLoc; }
  void setIfLoc(SourceLocation IfLoc) { IfStmtBits.IfLoc = IfLoc; }

  SourceLocation getElseLoc() const {
    return hasElseStorage() ? *getTrailingObjects<SourceLocation>()
                            : SourceLocation();
  }

  void setElseLoc(SourceLocation ElseLoc) {
    assert(hasElseStorage() &&
           "This if statement has no storage for an else statement!");
    *getTrailingObjects<SourceLocation>() = ElseLoc;
  }

  bool isConstexpr() const { return IfStmtBits.IsConstexpr; }
  void setConstexpr(bool C) { IfStmtBits.IsConstexpr = C; }

  bool isObjCAvailabilityCheck() const;

  SourceLocation getBeginLoc() const { return getIfLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    if (getElse())
      return getElse()->getEndLoc();
    return getThen()->getEndLoc();
  }

  // Iterators over subexpressions.  The iterators will include iterating
  // over the initialization expression referenced by the condition variable.
  child_range children() {
    return child_range(getTrailingObjects<Stmt *>(),
                       getTrailingObjects<Stmt *>() +
                           numTrailingObjects(OverloadToken<Stmt *>()));
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == IfStmtClass;
  }
};

/// SwitchStmt - This represents a 'switch' stmt.
class SwitchStmt final : public Stmt,
                         private llvm::TrailingObjects<SwitchStmt, Stmt *> {
  friend TrailingObjects;

  /// Points to a linked list of case and default statements.
  SwitchCase *FirstCase;

  // SwitchStmt is followed by several trailing objects,
  // some of which optional. Note that it would be more convenient to
  // put the optional trailing objects at the end but this would change
  // the order in children().
  // The trailing objects are in order:
  //
  // * A "Stmt *" for the init statement.
  //    Present if and only if hasInitStorage().
  //
  // * A "Stmt *" for the condition variable.
  //    Present if and only if hasVarStorage(). This is in fact a "DeclStmt *".
  //
  // * A "Stmt *" for the condition.
  //    Always present. This is in fact an "Expr *".
  //
  // * A "Stmt *" for the body.
  //    Always present.
  enum { InitOffset = 0, BodyOffsetFromCond = 1 };
  enum { NumMandatoryStmtPtr = 2 };

  unsigned numTrailingObjects(OverloadToken<Stmt *>) const {
    return NumMandatoryStmtPtr + hasInitStorage() + hasVarStorage();
  }

  unsigned initOffset() const { return InitOffset; }
  unsigned varOffset() const { return InitOffset + hasInitStorage(); }
  unsigned condOffset() const {
    return InitOffset + hasInitStorage() + hasVarStorage();
  }
  unsigned bodyOffset() const { return condOffset() + BodyOffsetFromCond; }

  /// Build a switch statement.
  SwitchStmt(const ASTContext &Ctx, Stmt *Init, VarDecl *Var, Expr *Cond);

  /// Build a empty switch statement.
  explicit SwitchStmt(EmptyShell Empty, bool HasInit, bool HasVar);

public:
  /// Create a switch statement.
  static SwitchStmt *Create(const ASTContext &Ctx, Stmt *Init, VarDecl *Var,
                            Expr *Cond);

  /// Create an empty switch statement optionally with storage for
  /// an init expression and a condition variable.
  static SwitchStmt *CreateEmpty(const ASTContext &Ctx, bool HasInit,
                                 bool HasVar);

  /// True if this SwitchStmt has storage for an init statement.
  bool hasInitStorage() const { return SwitchStmtBits.HasInit; }

  /// True if this SwitchStmt has storage for a condition variable.
  bool hasVarStorage() const { return SwitchStmtBits.HasVar; }

  Expr *getCond() {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[condOffset()]);
  }

  const Expr *getCond() const {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[condOffset()]);
  }

  void setCond(Expr *Cond) {
    getTrailingObjects<Stmt *>()[condOffset()] = reinterpret_cast<Stmt *>(Cond);
  }

  Stmt *getBody() { return getTrailingObjects<Stmt *>()[bodyOffset()]; }
  const Stmt *getBody() const {
    return getTrailingObjects<Stmt *>()[bodyOffset()];
  }

  void setBody(Stmt *Body) {
    getTrailingObjects<Stmt *>()[bodyOffset()] = Body;
  }

  Stmt *getInit() {
    return hasInitStorage() ? getTrailingObjects<Stmt *>()[initOffset()]
                            : nullptr;
  }

  const Stmt *getInit() const {
    return hasInitStorage() ? getTrailingObjects<Stmt *>()[initOffset()]
                            : nullptr;
  }

  void setInit(Stmt *Init) {
    assert(hasInitStorage() &&
           "This switch statement has no storage for an init statement!");
    getTrailingObjects<Stmt *>()[initOffset()] = Init;
  }

  /// Retrieve the variable declared in this "switch" statement, if any.
  ///
  /// In the following example, "x" is the condition variable.
  /// \code
  /// switch (int x = foo()) {
  ///   case 0: break;
  ///   // ...
  /// }
  /// \endcode
  VarDecl *getConditionVariable();
  const VarDecl *getConditionVariable() const {
    return const_cast<SwitchStmt *>(this)->getConditionVariable();
  }

  /// Set the condition variable in this switch statement.
  /// The switch statement must have storage for it.
  void setConditionVariable(const ASTContext &Ctx, VarDecl *VD);

  /// If this SwitchStmt has a condition variable, return the faux DeclStmt
  /// associated with the creation of that condition variable.
  DeclStmt *getConditionVariableDeclStmt() {
    return hasVarStorage() ? static_cast<DeclStmt *>(
                                 getTrailingObjects<Stmt *>()[varOffset()])
                           : nullptr;
  }

  const DeclStmt *getConditionVariableDeclStmt() const {
    return hasVarStorage() ? static_cast<DeclStmt *>(
                                 getTrailingObjects<Stmt *>()[varOffset()])
                           : nullptr;
  }

  SwitchCase *getSwitchCaseList() { return FirstCase; }
  const SwitchCase *getSwitchCaseList() const { return FirstCase; }
  void setSwitchCaseList(SwitchCase *SC) { FirstCase = SC; }

  SourceLocation getSwitchLoc() const { return SwitchStmtBits.SwitchLoc; }
  void setSwitchLoc(SourceLocation L) { SwitchStmtBits.SwitchLoc = L; }

  void setBody(Stmt *S, SourceLocation SL) {
    setBody(S);
    setSwitchLoc(SL);
  }

  void addSwitchCase(SwitchCase *SC) {
    assert(!SC->getNextSwitchCase() &&
           "case/default already added to a switch");
    SC->setNextSwitchCase(FirstCase);
    FirstCase = SC;
  }

  /// Set a flag in the SwitchStmt indicating that if the 'switch (X)' is a
  /// switch over an enum value then all cases have been explicitly covered.
  void setAllEnumCasesCovered() { SwitchStmtBits.AllEnumCasesCovered = true; }

  /// Returns true if the SwitchStmt is a switch of an enum value and all cases
  /// have been explicitly covered.
  bool isAllEnumCasesCovered() const {
    return SwitchStmtBits.AllEnumCasesCovered;
  }

  SourceLocation getBeginLoc() const { return getSwitchLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getBody() ? getBody()->getEndLoc()
                     : reinterpret_cast<const Stmt *>(getCond())->getEndLoc();
  }

  // Iterators
  child_range children() {
    return child_range(getTrailingObjects<Stmt *>(),
                       getTrailingObjects<Stmt *>() +
                           numTrailingObjects(OverloadToken<Stmt *>()));
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SwitchStmtClass;
  }
};

/// WhileStmt - This represents a 'while' stmt.
class WhileStmt final : public Stmt,
                        private llvm::TrailingObjects<WhileStmt, Stmt *> {
  friend TrailingObjects;

  // WhileStmt is followed by several trailing objects,
  // some of which optional. Note that it would be more
  // convenient to put the optional trailing object at the end
  // but this would affect children().
  // The trailing objects are in order:
  //
  // * A "Stmt *" for the condition variable.
  //    Present if and only if hasVarStorage(). This is in fact a "DeclStmt *".
  //
  // * A "Stmt *" for the condition.
  //    Always present. This is in fact an "Expr *".
  //
  // * A "Stmt *" for the body.
  //    Always present.
  //
  enum { VarOffset = 0, BodyOffsetFromCond = 1 };
  enum { NumMandatoryStmtPtr = 2 };

  unsigned varOffset() const { return VarOffset; }
  unsigned condOffset() const { return VarOffset + hasVarStorage(); }
  unsigned bodyOffset() const { return condOffset() + BodyOffsetFromCond; }

  unsigned numTrailingObjects(OverloadToken<Stmt *>) const {
    return NumMandatoryStmtPtr + hasVarStorage();
  }

  /// Build a while statement.
  WhileStmt(const ASTContext &Ctx, VarDecl *Var, Expr *Cond, Stmt *Body,
            SourceLocation WL);

  /// Build an empty while statement.
  explicit WhileStmt(EmptyShell Empty, bool HasVar);

public:
  /// Create a while statement.
  static WhileStmt *Create(const ASTContext &Ctx, VarDecl *Var, Expr *Cond,
                           Stmt *Body, SourceLocation WL);

  /// Create an empty while statement optionally with storage for
  /// a condition variable.
  static WhileStmt *CreateEmpty(const ASTContext &Ctx, bool HasVar);

  /// True if this WhileStmt has storage for a condition variable.
  bool hasVarStorage() const { return WhileStmtBits.HasVar; }

  Expr *getCond() {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[condOffset()]);
  }

  const Expr *getCond() const {
    return reinterpret_cast<Expr *>(getTrailingObjects<Stmt *>()[condOffset()]);
  }

  void setCond(Expr *Cond) {
    getTrailingObjects<Stmt *>()[condOffset()] = reinterpret_cast<Stmt *>(Cond);
  }

  Stmt *getBody() { return getTrailingObjects<Stmt *>()[bodyOffset()]; }
  const Stmt *getBody() const {
    return getTrailingObjects<Stmt *>()[bodyOffset()];
  }

  void setBody(Stmt *Body) {
    getTrailingObjects<Stmt *>()[bodyOffset()] = Body;
  }

  /// Retrieve the variable declared in this "while" statement, if any.
  ///
  /// In the following example, "x" is the condition variable.
  /// \code
  /// while (int x = random()) {
  ///   // ...
  /// }
  /// \endcode
  VarDecl *getConditionVariable();
  const VarDecl *getConditionVariable() const {
    return const_cast<WhileStmt *>(this)->getConditionVariable();
  }

  /// Set the condition variable of this while statement.
  /// The while statement must have storage for it.
  void setConditionVariable(const ASTContext &Ctx, VarDecl *V);

  /// If this WhileStmt has a condition variable, return the faux DeclStmt
  /// associated with the creation of that condition variable.
  DeclStmt *getConditionVariableDeclStmt() {
    return hasVarStorage() ? static_cast<DeclStmt *>(
                                 getTrailingObjects<Stmt *>()[varOffset()])
                           : nullptr;
  }

  const DeclStmt *getConditionVariableDeclStmt() const {
    return hasVarStorage() ? static_cast<DeclStmt *>(
                                 getTrailingObjects<Stmt *>()[varOffset()])
                           : nullptr;
  }

  SourceLocation getWhileLoc() const { return WhileStmtBits.WhileLoc; }
  void setWhileLoc(SourceLocation L) { WhileStmtBits.WhileLoc = L; }

  SourceLocation getBeginLoc() const { return getWhileLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return getBody()->getEndLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == WhileStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(getTrailingObjects<Stmt *>(),
                       getTrailingObjects<Stmt *>() +
                           numTrailingObjects(OverloadToken<Stmt *>()));
  }
};

/// DoStmt - This represents a 'do/while' stmt.
class DoStmt : public Stmt {
  enum { BODY, COND, END_EXPR };
  Stmt *SubExprs[END_EXPR];
  SourceLocation WhileLoc;
  SourceLocation RParenLoc; // Location of final ')' in do stmt condition.

public:
  DoStmt(Stmt *Body, Expr *Cond, SourceLocation DL, SourceLocation WL,
         SourceLocation RP)
      : Stmt(DoStmtClass), WhileLoc(WL), RParenLoc(RP) {
    setCond(Cond);
    setBody(Body);
    setDoLoc(DL);
  }

  /// Build an empty do-while statement.
  explicit DoStmt(EmptyShell Empty) : Stmt(DoStmtClass, Empty) {}

  Expr *getCond() { return reinterpret_cast<Expr *>(SubExprs[COND]); }
  const Expr *getCond() const {
    return reinterpret_cast<Expr *>(SubExprs[COND]);
  }

  void setCond(Expr *Cond) { SubExprs[COND] = reinterpret_cast<Stmt *>(Cond); }

  Stmt *getBody() { return SubExprs[BODY]; }
  const Stmt *getBody() const { return SubExprs[BODY]; }
  void setBody(Stmt *Body) { SubExprs[BODY] = Body; }

  SourceLocation getDoLoc() const { return DoStmtBits.DoLoc; }
  void setDoLoc(SourceLocation L) { DoStmtBits.DoLoc = L; }
  SourceLocation getWhileLoc() const { return WhileLoc; }
  void setWhileLoc(SourceLocation L) { WhileLoc = L; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const { return getDoLoc(); }
  SourceLocation getEndLoc() const { return getRParenLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DoStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0] + END_EXPR);
  }
};

/// ForStmt - This represents a 'for (init;cond;inc)' stmt.  Note that any of
/// the init/cond/inc parts of the ForStmt will be null if they were not
/// specified in the source.
class ForStmt : public Stmt {
  enum { INIT, CONDVAR, COND, INC, BODY, END_EXPR };
  Stmt* SubExprs[END_EXPR]; // SubExprs[INIT] is an expression or declstmt.
  SourceLocation LParenLoc, RParenLoc;

public:
  ForStmt(const ASTContext &C, Stmt *Init, Expr *Cond, VarDecl *condVar,
          Expr *Inc, Stmt *Body, SourceLocation FL, SourceLocation LP,
          SourceLocation RP);

  /// Build an empty for statement.
  explicit ForStmt(EmptyShell Empty) : Stmt(ForStmtClass, Empty) {}

  Stmt *getInit() { return SubExprs[INIT]; }

  /// Retrieve the variable declared in this "for" statement, if any.
  ///
  /// In the following example, "y" is the condition variable.
  /// \code
  /// for (int x = random(); int y = mangle(x); ++x) {
  ///   // ...
  /// }
  /// \endcode
  VarDecl *getConditionVariable() const;
  void setConditionVariable(const ASTContext &C, VarDecl *V);

  /// If this ForStmt has a condition variable, return the faux DeclStmt
  /// associated with the creation of that condition variable.
  const DeclStmt *getConditionVariableDeclStmt() const {
    return reinterpret_cast<DeclStmt*>(SubExprs[CONDVAR]);
  }

  Expr *getCond() { return reinterpret_cast<Expr*>(SubExprs[COND]); }
  Expr *getInc()  { return reinterpret_cast<Expr*>(SubExprs[INC]); }
  Stmt *getBody() { return SubExprs[BODY]; }

  const Stmt *getInit() const { return SubExprs[INIT]; }
  const Expr *getCond() const { return reinterpret_cast<Expr*>(SubExprs[COND]);}
  const Expr *getInc()  const { return reinterpret_cast<Expr*>(SubExprs[INC]); }
  const Stmt *getBody() const { return SubExprs[BODY]; }

  void setInit(Stmt *S) { SubExprs[INIT] = S; }
  void setCond(Expr *E) { SubExprs[COND] = reinterpret_cast<Stmt*>(E); }
  void setInc(Expr *E) { SubExprs[INC] = reinterpret_cast<Stmt*>(E); }
  void setBody(Stmt *S) { SubExprs[BODY] = S; }

  SourceLocation getForLoc() const { return ForStmtBits.ForLoc; }
  void setForLoc(SourceLocation L) { ForStmtBits.ForLoc = L; }
  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation L) { LParenLoc = L; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceLocation getBeginLoc() const { return getForLoc(); }
  SourceLocation getEndLoc() const { return getBody()->getEndLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ForStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0]+END_EXPR);
  }
};

/// GotoStmt - This represents a direct goto.
class GotoStmt : public Stmt {
  LabelDecl *Label;
  SourceLocation LabelLoc;

public:
  GotoStmt(LabelDecl *label, SourceLocation GL, SourceLocation LL)
      : Stmt(GotoStmtClass), Label(label), LabelLoc(LL) {
    setGotoLoc(GL);
  }

  /// Build an empty goto statement.
  explicit GotoStmt(EmptyShell Empty) : Stmt(GotoStmtClass, Empty) {}

  LabelDecl *getLabel() const { return Label; }
  void setLabel(LabelDecl *D) { Label = D; }

  SourceLocation getGotoLoc() const { return GotoStmtBits.GotoLoc; }
  void setGotoLoc(SourceLocation L) { GotoStmtBits.GotoLoc = L; }
  SourceLocation getLabelLoc() const { return LabelLoc; }
  void setLabelLoc(SourceLocation L) { LabelLoc = L; }

  SourceLocation getBeginLoc() const { return getGotoLoc(); }
  SourceLocation getEndLoc() const { return getLabelLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == GotoStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
};

/// IndirectGotoStmt - This represents an indirect goto.
class IndirectGotoStmt : public Stmt {
  SourceLocation StarLoc;
  Stmt *Target;

public:
  IndirectGotoStmt(SourceLocation gotoLoc, SourceLocation starLoc, Expr *target)
      : Stmt(IndirectGotoStmtClass), StarLoc(starLoc) {
    setTarget(target);
    setGotoLoc(gotoLoc);
  }

  /// Build an empty indirect goto statement.
  explicit IndirectGotoStmt(EmptyShell Empty)
      : Stmt(IndirectGotoStmtClass, Empty) {}

  void setGotoLoc(SourceLocation L) { GotoStmtBits.GotoLoc = L; }
  SourceLocation getGotoLoc() const { return GotoStmtBits.GotoLoc; }
  void setStarLoc(SourceLocation L) { StarLoc = L; }
  SourceLocation getStarLoc() const { return StarLoc; }

  Expr *getTarget() { return reinterpret_cast<Expr *>(Target); }
  const Expr *getTarget() const {
    return reinterpret_cast<const Expr *>(Target);
  }
  void setTarget(Expr *E) { Target = reinterpret_cast<Stmt *>(E); }

  /// getConstantTarget - Returns the fixed target of this indirect
  /// goto, if one exists.
  LabelDecl *getConstantTarget();
  const LabelDecl *getConstantTarget() const {
    return const_cast<IndirectGotoStmt *>(this)->getConstantTarget();
  }

  SourceLocation getBeginLoc() const { return getGotoLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Target->getEndLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == IndirectGotoStmtClass;
  }

  // Iterators
  child_range children() { return child_range(&Target, &Target + 1); }
};

/// ContinueStmt - This represents a continue.
class ContinueStmt : public Stmt {
public:
  ContinueStmt(SourceLocation CL) : Stmt(ContinueStmtClass) {
    setContinueLoc(CL);
  }

  /// Build an empty continue statement.
  explicit ContinueStmt(EmptyShell Empty) : Stmt(ContinueStmtClass, Empty) {}

  SourceLocation getContinueLoc() const { return ContinueStmtBits.ContinueLoc; }
  void setContinueLoc(SourceLocation L) { ContinueStmtBits.ContinueLoc = L; }

  SourceLocation getBeginLoc() const { return getContinueLoc(); }
  SourceLocation getEndLoc() const { return getContinueLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ContinueStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
};

/// BreakStmt - This represents a break.
class BreakStmt : public Stmt {
public:
  BreakStmt(SourceLocation BL) : Stmt(BreakStmtClass) {
    setBreakLoc(BL);
  }

  /// Build an empty break statement.
  explicit BreakStmt(EmptyShell Empty) : Stmt(BreakStmtClass, Empty) {}

  SourceLocation getBreakLoc() const { return BreakStmtBits.BreakLoc; }
  void setBreakLoc(SourceLocation L) { BreakStmtBits.BreakLoc = L; }

  SourceLocation getBeginLoc() const { return getBreakLoc(); }
  SourceLocation getEndLoc() const { return getBreakLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == BreakStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
};

/// ReturnStmt - This represents a return, optionally of an expression:
///   return;
///   return 4;
///
/// Note that GCC allows return with no argument in a function declared to
/// return a value, and it allows returning a value in functions declared to
/// return void.  We explicitly model this in the AST, which means you can't
/// depend on the return type of the function and the presence of an argument.
class ReturnStmt final
    : public Stmt,
      private llvm::TrailingObjects<ReturnStmt, const VarDecl *> {
  friend TrailingObjects;

  /// The return expression.
  Stmt *RetExpr;

  // ReturnStmt is followed optionally by a trailing "const VarDecl *"
  // for the NRVO candidate. Present if and only if hasNRVOCandidate().

  /// True if this ReturnStmt has storage for an NRVO candidate.
  bool hasNRVOCandidate() const { return ReturnStmtBits.HasNRVOCandidate; }

  unsigned numTrailingObjects(OverloadToken<const VarDecl *>) const {
    return hasNRVOCandidate();
  }

  /// Build a return statement.
  ReturnStmt(SourceLocation RL, Expr *E, const VarDecl *NRVOCandidate);

  /// Build an empty return statement.
  explicit ReturnStmt(EmptyShell Empty, bool HasNRVOCandidate);

public:
  /// Create a return statement.
  static ReturnStmt *Create(const ASTContext &Ctx, SourceLocation RL, Expr *E,
                            const VarDecl *NRVOCandidate);

  /// Create an empty return statement, optionally with
  /// storage for an NRVO candidate.
  static ReturnStmt *CreateEmpty(const ASTContext &Ctx, bool HasNRVOCandidate);

  Expr *getRetValue() { return reinterpret_cast<Expr *>(RetExpr); }
  const Expr *getRetValue() const { return reinterpret_cast<Expr *>(RetExpr); }
  void setRetValue(Expr *E) { RetExpr = reinterpret_cast<Stmt *>(E); }

  /// Retrieve the variable that might be used for the named return
  /// value optimization.
  ///
  /// The optimization itself can only be performed if the variable is
  /// also marked as an NRVO object.
  const VarDecl *getNRVOCandidate() const {
    return hasNRVOCandidate() ? *getTrailingObjects<const VarDecl *>()
                              : nullptr;
  }

  /// Set the variable that might be used for the named return value
  /// optimization. The return statement must have storage for it,
  /// which is the case if and only if hasNRVOCandidate() is true.
  void setNRVOCandidate(const VarDecl *Var) {
    assert(hasNRVOCandidate() &&
           "This return statement has no storage for an NRVO candidate!");
    *getTrailingObjects<const VarDecl *>() = Var;
  }

  SourceLocation getReturnLoc() const { return ReturnStmtBits.RetLoc; }
  void setReturnLoc(SourceLocation L) { ReturnStmtBits.RetLoc = L; }

  SourceLocation getBeginLoc() const { return getReturnLoc(); }
  SourceLocation getEndLoc() const LLVM_READONLY {
    return RetExpr ? RetExpr->getEndLoc() : getReturnLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ReturnStmtClass;
  }

  // Iterators
  child_range children() {
    if (RetExpr)
      return child_range(&RetExpr, &RetExpr + 1);
    return child_range(child_iterator(), child_iterator());
  }
};

/// AsmStmt is the base class for GCCAsmStmt and MSAsmStmt.
class AsmStmt : public Stmt {
protected:
  friend class ASTStmtReader;

  SourceLocation AsmLoc;

  /// True if the assembly statement does not have any input or output
  /// operands.
  bool IsSimple;

  /// If true, treat this inline assembly as having side effects.
  /// This assembly statement should not be optimized, deleted or moved.
  bool IsVolatile;

  unsigned NumOutputs;
  unsigned NumInputs;
  unsigned NumClobbers;

  Stmt **Exprs = nullptr;

  AsmStmt(StmtClass SC, SourceLocation asmloc, bool issimple, bool isvolatile,
          unsigned numoutputs, unsigned numinputs, unsigned numclobbers)
      : Stmt (SC), AsmLoc(asmloc), IsSimple(issimple), IsVolatile(isvolatile),
        NumOutputs(numoutputs), NumInputs(numinputs),
        NumClobbers(numclobbers) {}

public:
  /// Build an empty inline-assembly statement.
  explicit AsmStmt(StmtClass SC, EmptyShell Empty) : Stmt(SC, Empty) {}

  SourceLocation getAsmLoc() const { return AsmLoc; }
  void setAsmLoc(SourceLocation L) { AsmLoc = L; }

  bool isSimple() const { return IsSimple; }
  void setSimple(bool V) { IsSimple = V; }

  bool isVolatile() const { return IsVolatile; }
  void setVolatile(bool V) { IsVolatile = V; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return {}; }
  SourceLocation getEndLoc() const LLVM_READONLY { return {}; }

  //===--- Asm String Analysis ---===//

  /// Assemble final IR asm string.
  std::string generateAsmString(const ASTContext &C) const;

  //===--- Output operands ---===//

  unsigned getNumOutputs() const { return NumOutputs; }

  /// getOutputConstraint - Return the constraint string for the specified
  /// output operand.  All output constraints are known to be non-empty (either
  /// '=' or '+').
  StringRef getOutputConstraint(unsigned i) const;

  /// isOutputPlusConstraint - Return true if the specified output constraint
  /// is a "+" constraint (which is both an input and an output) or false if it
  /// is an "=" constraint (just an output).
  bool isOutputPlusConstraint(unsigned i) const {
    return getOutputConstraint(i)[0] == '+';
  }

  const Expr *getOutputExpr(unsigned i) const;

  /// getNumPlusOperands - Return the number of output operands that have a "+"
  /// constraint.
  unsigned getNumPlusOperands() const;

  //===--- Input operands ---===//

  unsigned getNumInputs() const { return NumInputs; }

  /// getInputConstraint - Return the specified input constraint.  Unlike output
  /// constraints, these can be empty.
  StringRef getInputConstraint(unsigned i) const;

  const Expr *getInputExpr(unsigned i) const;

  //===--- Other ---===//

  unsigned getNumClobbers() const { return NumClobbers; }
  StringRef getClobber(unsigned i) const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == GCCAsmStmtClass ||
      T->getStmtClass() == MSAsmStmtClass;
  }

  // Input expr iterators.

  using inputs_iterator = ExprIterator;
  using const_inputs_iterator = ConstExprIterator;
  using inputs_range = llvm::iterator_range<inputs_iterator>;
  using inputs_const_range = llvm::iterator_range<const_inputs_iterator>;

  inputs_iterator begin_inputs() {
    return &Exprs[0] + NumOutputs;
  }

  inputs_iterator end_inputs() {
    return &Exprs[0] + NumOutputs + NumInputs;
  }

  inputs_range inputs() { return inputs_range(begin_inputs(), end_inputs()); }

  const_inputs_iterator begin_inputs() const {
    return &Exprs[0] + NumOutputs;
  }

  const_inputs_iterator end_inputs() const {
    return &Exprs[0] + NumOutputs + NumInputs;
  }

  inputs_const_range inputs() const {
    return inputs_const_range(begin_inputs(), end_inputs());
  }

  // Output expr iterators.

  using outputs_iterator = ExprIterator;
  using const_outputs_iterator = ConstExprIterator;
  using outputs_range = llvm::iterator_range<outputs_iterator>;
  using outputs_const_range = llvm::iterator_range<const_outputs_iterator>;

  outputs_iterator begin_outputs() {
    return &Exprs[0];
  }

  outputs_iterator end_outputs() {
    return &Exprs[0] + NumOutputs;
  }

  outputs_range outputs() {
    return outputs_range(begin_outputs(), end_outputs());
  }

  const_outputs_iterator begin_outputs() const {
    return &Exprs[0];
  }

  const_outputs_iterator end_outputs() const {
    return &Exprs[0] + NumOutputs;
  }

  outputs_const_range outputs() const {
    return outputs_const_range(begin_outputs(), end_outputs());
  }

  child_range children() {
    return child_range(&Exprs[0], &Exprs[0] + NumOutputs + NumInputs);
  }
};

/// This represents a GCC inline-assembly statement extension.
class GCCAsmStmt : public AsmStmt {
  friend class ASTStmtReader;

  SourceLocation RParenLoc;
  StringLiteral *AsmStr;

  // FIXME: If we wanted to, we could allocate all of these in one big array.
  StringLiteral **Constraints = nullptr;
  StringLiteral **Clobbers = nullptr;
  IdentifierInfo **Names = nullptr;

public:
  GCCAsmStmt(const ASTContext &C, SourceLocation asmloc, bool issimple,
             bool isvolatile, unsigned numoutputs, unsigned numinputs,
             IdentifierInfo **names, StringLiteral **constraints, Expr **exprs,
             StringLiteral *asmstr, unsigned numclobbers,
             StringLiteral **clobbers, SourceLocation rparenloc);

  /// Build an empty inline-assembly statement.
  explicit GCCAsmStmt(EmptyShell Empty) : AsmStmt(GCCAsmStmtClass, Empty) {}

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  //===--- Asm String Analysis ---===//

  const StringLiteral *getAsmString() const { return AsmStr; }
  StringLiteral *getAsmString() { return AsmStr; }
  void setAsmString(StringLiteral *E) { AsmStr = E; }

  /// AsmStringPiece - this is part of a decomposed asm string specification
  /// (for use with the AnalyzeAsmString function below).  An asm string is
  /// considered to be a concatenation of these parts.
  class AsmStringPiece {
  public:
    enum Kind {
      String,  // String in .ll asm string form, "$" -> "$$" and "%%" -> "%".
      Operand  // Operand reference, with optional modifier %c4.
    };

  private:
    Kind MyKind;
    std::string Str;
    unsigned OperandNo;

    // Source range for operand references.
    CharSourceRange Range;

  public:
    AsmStringPiece(const std::string &S) : MyKind(String), Str(S) {}
    AsmStringPiece(unsigned OpNo, const std::string &S, SourceLocation Begin,
                   SourceLocation End)
        : MyKind(Operand), Str(S), OperandNo(OpNo),
          Range(CharSourceRange::getCharRange(Begin, End)) {}

    bool isString() const { return MyKind == String; }
    bool isOperand() const { return MyKind == Operand; }

    const std::string &getString() const { return Str; }

    unsigned getOperandNo() const {
      assert(isOperand());
      return OperandNo;
    }

    CharSourceRange getRange() const {
      assert(isOperand() && "Range is currently used only for Operands.");
      return Range;
    }

    /// getModifier - Get the modifier for this operand, if present.  This
    /// returns '\0' if there was no modifier.
    char getModifier() const;
  };

  /// AnalyzeAsmString - Analyze the asm string of the current asm, decomposing
  /// it into pieces.  If the asm string is erroneous, emit errors and return
  /// true, otherwise return false.  This handles canonicalization and
  /// translation of strings from GCC syntax to LLVM IR syntax, and handles
  //// flattening of named references like %[foo] to Operand AsmStringPiece's.
  unsigned AnalyzeAsmString(SmallVectorImpl<AsmStringPiece> &Pieces,
                            const ASTContext &C, unsigned &DiagOffs) const;

  /// Assemble final IR asm string.
  std::string generateAsmString(const ASTContext &C) const;

  //===--- Output operands ---===//

  IdentifierInfo *getOutputIdentifier(unsigned i) const { return Names[i]; }

  StringRef getOutputName(unsigned i) const {
    if (IdentifierInfo *II = getOutputIdentifier(i))
      return II->getName();

    return {};
  }

  StringRef getOutputConstraint(unsigned i) const;

  const StringLiteral *getOutputConstraintLiteral(unsigned i) const {
    return Constraints[i];
  }
  StringLiteral *getOutputConstraintLiteral(unsigned i) {
    return Constraints[i];
  }

  Expr *getOutputExpr(unsigned i);

  const Expr *getOutputExpr(unsigned i) const {
    return const_cast<GCCAsmStmt*>(this)->getOutputExpr(i);
  }

  //===--- Input operands ---===//

  IdentifierInfo *getInputIdentifier(unsigned i) const {
    return Names[i + NumOutputs];
  }

  StringRef getInputName(unsigned i) const {
    if (IdentifierInfo *II = getInputIdentifier(i))
      return II->getName();

    return {};
  }

  StringRef getInputConstraint(unsigned i) const;

  const StringLiteral *getInputConstraintLiteral(unsigned i) const {
    return Constraints[i + NumOutputs];
  }
  StringLiteral *getInputConstraintLiteral(unsigned i) {
    return Constraints[i + NumOutputs];
  }

  Expr *getInputExpr(unsigned i);
  void setInputExpr(unsigned i, Expr *E);

  const Expr *getInputExpr(unsigned i) const {
    return const_cast<GCCAsmStmt*>(this)->getInputExpr(i);
  }

private:
  void setOutputsAndInputsAndClobbers(const ASTContext &C,
                                      IdentifierInfo **Names,
                                      StringLiteral **Constraints,
                                      Stmt **Exprs,
                                      unsigned NumOutputs,
                                      unsigned NumInputs,
                                      StringLiteral **Clobbers,
                                      unsigned NumClobbers);

public:
  //===--- Other ---===//

  /// getNamedOperand - Given a symbolic operand reference like %[foo],
  /// translate this into a numeric value needed to reference the same operand.
  /// This returns -1 if the operand name is invalid.
  int getNamedOperand(StringRef SymbolicName) const;

  StringRef getClobber(unsigned i) const;

  StringLiteral *getClobberStringLiteral(unsigned i) { return Clobbers[i]; }
  const StringLiteral *getClobberStringLiteral(unsigned i) const {
    return Clobbers[i];
  }

  SourceLocation getBeginLoc() const LLVM_READONLY { return AsmLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return RParenLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == GCCAsmStmtClass;
  }
};

/// This represents a Microsoft inline-assembly statement extension.
class MSAsmStmt : public AsmStmt {
  friend class ASTStmtReader;

  SourceLocation LBraceLoc, EndLoc;
  StringRef AsmStr;

  unsigned NumAsmToks = 0;

  Token *AsmToks = nullptr;
  StringRef *Constraints = nullptr;
  StringRef *Clobbers = nullptr;

public:
  MSAsmStmt(const ASTContext &C, SourceLocation asmloc,
            SourceLocation lbraceloc, bool issimple, bool isvolatile,
            ArrayRef<Token> asmtoks, unsigned numoutputs, unsigned numinputs,
            ArrayRef<StringRef> constraints,
            ArrayRef<Expr*> exprs, StringRef asmstr,
            ArrayRef<StringRef> clobbers, SourceLocation endloc);

  /// Build an empty MS-style inline-assembly statement.
  explicit MSAsmStmt(EmptyShell Empty) : AsmStmt(MSAsmStmtClass, Empty) {}

  SourceLocation getLBraceLoc() const { return LBraceLoc; }
  void setLBraceLoc(SourceLocation L) { LBraceLoc = L; }
  SourceLocation getEndLoc() const { return EndLoc; }
  void setEndLoc(SourceLocation L) { EndLoc = L; }

  bool hasBraces() const { return LBraceLoc.isValid(); }

  unsigned getNumAsmToks() { return NumAsmToks; }
  Token *getAsmToks() { return AsmToks; }

  //===--- Asm String Analysis ---===//
  StringRef getAsmString() const { return AsmStr; }

  /// Assemble final IR asm string.
  std::string generateAsmString(const ASTContext &C) const;

  //===--- Output operands ---===//

  StringRef getOutputConstraint(unsigned i) const {
    assert(i < NumOutputs);
    return Constraints[i];
  }

  Expr *getOutputExpr(unsigned i);

  const Expr *getOutputExpr(unsigned i) const {
    return const_cast<MSAsmStmt*>(this)->getOutputExpr(i);
  }

  //===--- Input operands ---===//

  StringRef getInputConstraint(unsigned i) const {
    assert(i < NumInputs);
    return Constraints[i + NumOutputs];
  }

  Expr *getInputExpr(unsigned i);
  void setInputExpr(unsigned i, Expr *E);

  const Expr *getInputExpr(unsigned i) const {
    return const_cast<MSAsmStmt*>(this)->getInputExpr(i);
  }

  //===--- Other ---===//

  ArrayRef<StringRef> getAllConstraints() const {
    return llvm::makeArrayRef(Constraints, NumInputs + NumOutputs);
  }

  ArrayRef<StringRef> getClobbers() const {
    return llvm::makeArrayRef(Clobbers, NumClobbers);
  }

  ArrayRef<Expr*> getAllExprs() const {
    return llvm::makeArrayRef(reinterpret_cast<Expr**>(Exprs),
                              NumInputs + NumOutputs);
  }

  StringRef getClobber(unsigned i) const { return getClobbers()[i]; }

private:
  void initialize(const ASTContext &C, StringRef AsmString,
                  ArrayRef<Token> AsmToks, ArrayRef<StringRef> Constraints,
                  ArrayRef<Expr*> Exprs, ArrayRef<StringRef> Clobbers);

public:
  SourceLocation getBeginLoc() const LLVM_READONLY { return AsmLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == MSAsmStmtClass;
  }

  child_range children() {
    return child_range(&Exprs[0], &Exprs[NumInputs + NumOutputs]);
  }
};

class SEHExceptStmt : public Stmt {
  friend class ASTReader;
  friend class ASTStmtReader;

  SourceLocation  Loc;
  Stmt *Children[2];

  enum { FILTER_EXPR, BLOCK };

  SEHExceptStmt(SourceLocation Loc, Expr *FilterExpr, Stmt *Block);
  explicit SEHExceptStmt(EmptyShell E) : Stmt(SEHExceptStmtClass, E) {}

public:
  static SEHExceptStmt* Create(const ASTContext &C,
                               SourceLocation ExceptLoc,
                               Expr *FilterExpr,
                               Stmt *Block);

  SourceLocation getBeginLoc() const LLVM_READONLY { return getExceptLoc(); }

  SourceLocation getExceptLoc() const { return Loc; }
  SourceLocation getEndLoc() const { return getBlock()->getEndLoc(); }

  Expr *getFilterExpr() const {
    return reinterpret_cast<Expr*>(Children[FILTER_EXPR]);
  }

  CompoundStmt *getBlock() const {
    return cast<CompoundStmt>(Children[BLOCK]);
  }

  child_range children() {
    return child_range(Children, Children+2);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SEHExceptStmtClass;
  }
};

class SEHFinallyStmt : public Stmt {
  friend class ASTReader;
  friend class ASTStmtReader;

  SourceLocation  Loc;
  Stmt *Block;

  SEHFinallyStmt(SourceLocation Loc, Stmt *Block);
  explicit SEHFinallyStmt(EmptyShell E) : Stmt(SEHFinallyStmtClass, E) {}

public:
  static SEHFinallyStmt* Create(const ASTContext &C,
                                SourceLocation FinallyLoc,
                                Stmt *Block);

  SourceLocation getBeginLoc() const LLVM_READONLY { return getFinallyLoc(); }

  SourceLocation getFinallyLoc() const { return Loc; }
  SourceLocation getEndLoc() const { return Block->getEndLoc(); }

  CompoundStmt *getBlock() const { return cast<CompoundStmt>(Block); }

  child_range children() {
    return child_range(&Block,&Block+1);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SEHFinallyStmtClass;
  }
};

class SEHTryStmt : public Stmt {
  friend class ASTReader;
  friend class ASTStmtReader;

  bool IsCXXTry;
  SourceLocation  TryLoc;
  Stmt *Children[2];

  enum { TRY = 0, HANDLER = 1 };

  SEHTryStmt(bool isCXXTry, // true if 'try' otherwise '__try'
             SourceLocation TryLoc,
             Stmt *TryBlock,
             Stmt *Handler);

  explicit SEHTryStmt(EmptyShell E) : Stmt(SEHTryStmtClass, E) {}

public:
  static SEHTryStmt* Create(const ASTContext &C, bool isCXXTry,
                            SourceLocation TryLoc, Stmt *TryBlock,
                            Stmt *Handler);

  SourceLocation getBeginLoc() const LLVM_READONLY { return getTryLoc(); }

  SourceLocation getTryLoc() const { return TryLoc; }
  SourceLocation getEndLoc() const { return Children[HANDLER]->getEndLoc(); }

  bool getIsCXXTry() const { return IsCXXTry; }

  CompoundStmt* getTryBlock() const {
    return cast<CompoundStmt>(Children[TRY]);
  }

  Stmt *getHandler() const { return Children[HANDLER]; }

  /// Returns 0 if not defined
  SEHExceptStmt  *getExceptHandler() const;
  SEHFinallyStmt *getFinallyHandler() const;

  child_range children() {
    return child_range(Children, Children+2);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SEHTryStmtClass;
  }
};

/// Represents a __leave statement.
class SEHLeaveStmt : public Stmt {
  SourceLocation LeaveLoc;

public:
  explicit SEHLeaveStmt(SourceLocation LL)
      : Stmt(SEHLeaveStmtClass), LeaveLoc(LL) {}

  /// Build an empty __leave statement.
  explicit SEHLeaveStmt(EmptyShell Empty) : Stmt(SEHLeaveStmtClass, Empty) {}

  SourceLocation getLeaveLoc() const { return LeaveLoc; }
  void setLeaveLoc(SourceLocation L) { LeaveLoc = L; }

  SourceLocation getBeginLoc() const LLVM_READONLY { return LeaveLoc; }
  SourceLocation getEndLoc() const LLVM_READONLY { return LeaveLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == SEHLeaveStmtClass;
  }

  // Iterators
  child_range children() {
    return child_range(child_iterator(), child_iterator());
  }
};

/// This captures a statement into a function. For example, the following
/// pragma annotated compound statement can be represented as a CapturedStmt,
/// and this compound statement is the body of an anonymous outlined function.
/// @code
/// #pragma omp parallel
/// {
///   compute();
/// }
/// @endcode
class CapturedStmt : public Stmt {
public:
  /// The different capture forms: by 'this', by reference, capture for
  /// variable-length array type etc.
  enum VariableCaptureKind {
    VCK_This,
    VCK_ByRef,
    VCK_ByCopy,
    VCK_VLAType,
  };

  /// Describes the capture of either a variable, or 'this', or
  /// variable-length array type.
  class Capture {
    llvm::PointerIntPair<VarDecl *, 2, VariableCaptureKind> VarAndKind;
    SourceLocation Loc;

  public:
    friend class ASTStmtReader;

    /// Create a new capture.
    ///
    /// \param Loc The source location associated with this capture.
    ///
    /// \param Kind The kind of capture (this, ByRef, ...).
    ///
    /// \param Var The variable being captured, or null if capturing this.
    Capture(SourceLocation Loc, VariableCaptureKind Kind,
            VarDecl *Var = nullptr);

    /// Determine the kind of capture.
    VariableCaptureKind getCaptureKind() const;

    /// Retrieve the source location at which the variable or 'this' was
    /// first used.
    SourceLocation getLocation() const { return Loc; }

    /// Determine whether this capture handles the C++ 'this' pointer.
    bool capturesThis() const { return getCaptureKind() == VCK_This; }

    /// Determine whether this capture handles a variable (by reference).
    bool capturesVariable() const { return getCaptureKind() == VCK_ByRef; }

    /// Determine whether this capture handles a variable by copy.
    bool capturesVariableByCopy() const {
      return getCaptureKind() == VCK_ByCopy;
    }

    /// Determine whether this capture handles a variable-length array
    /// type.
    bool capturesVariableArrayType() const {
      return getCaptureKind() == VCK_VLAType;
    }

    /// Retrieve the declaration of the variable being captured.
    ///
    /// This operation is only valid if this capture captures a variable.
    VarDecl *getCapturedVar() const;
  };

private:
  /// The number of variable captured, including 'this'.
  unsigned NumCaptures;

  /// The pointer part is the implicit the outlined function and the
  /// int part is the captured region kind, 'CR_Default' etc.
  llvm::PointerIntPair<CapturedDecl *, 2, CapturedRegionKind> CapDeclAndKind;

  /// The record for captured variables, a RecordDecl or CXXRecordDecl.
  RecordDecl *TheRecordDecl = nullptr;

  /// Construct a captured statement.
  CapturedStmt(Stmt *S, CapturedRegionKind Kind, ArrayRef<Capture> Captures,
               ArrayRef<Expr *> CaptureInits, CapturedDecl *CD, RecordDecl *RD);

  /// Construct an empty captured statement.
  CapturedStmt(EmptyShell Empty, unsigned NumCaptures);

  Stmt **getStoredStmts() { return reinterpret_cast<Stmt **>(this + 1); }

  Stmt *const *getStoredStmts() const {
    return reinterpret_cast<Stmt *const *>(this + 1);
  }

  Capture *getStoredCaptures() const;

  void setCapturedStmt(Stmt *S) { getStoredStmts()[NumCaptures] = S; }

public:
  friend class ASTStmtReader;

  static CapturedStmt *Create(const ASTContext &Context, Stmt *S,
                              CapturedRegionKind Kind,
                              ArrayRef<Capture> Captures,
                              ArrayRef<Expr *> CaptureInits,
                              CapturedDecl *CD, RecordDecl *RD);

  static CapturedStmt *CreateDeserialized(const ASTContext &Context,
                                          unsigned NumCaptures);

  /// Retrieve the statement being captured.
  Stmt *getCapturedStmt() { return getStoredStmts()[NumCaptures]; }
  const Stmt *getCapturedStmt() const { return getStoredStmts()[NumCaptures]; }

  /// Retrieve the outlined function declaration.
  CapturedDecl *getCapturedDecl();
  const CapturedDecl *getCapturedDecl() const;

  /// Set the outlined function declaration.
  void setCapturedDecl(CapturedDecl *D);

  /// Retrieve the captured region kind.
  CapturedRegionKind getCapturedRegionKind() const;

  /// Set the captured region kind.
  void setCapturedRegionKind(CapturedRegionKind Kind);

  /// Retrieve the record declaration for captured variables.
  const RecordDecl *getCapturedRecordDecl() const { return TheRecordDecl; }

  /// Set the record declaration for captured variables.
  void setCapturedRecordDecl(RecordDecl *D) {
    assert(D && "null RecordDecl");
    TheRecordDecl = D;
  }

  /// True if this variable has been captured.
  bool capturesVariable(const VarDecl *Var) const;

  /// An iterator that walks over the captures.
  using capture_iterator = Capture *;
  using const_capture_iterator = const Capture *;
  using capture_range = llvm::iterator_range<capture_iterator>;
  using capture_const_range = llvm::iterator_range<const_capture_iterator>;

  capture_range captures() {
    return capture_range(capture_begin(), capture_end());
  }
  capture_const_range captures() const {
    return capture_const_range(capture_begin(), capture_end());
  }

  /// Retrieve an iterator pointing to the first capture.
  capture_iterator capture_begin() { return getStoredCaptures(); }
  const_capture_iterator capture_begin() const { return getStoredCaptures(); }

  /// Retrieve an iterator pointing past the end of the sequence of
  /// captures.
  capture_iterator capture_end() const {
    return getStoredCaptures() + NumCaptures;
  }

  /// Retrieve the number of captures, including 'this'.
  unsigned capture_size() const { return NumCaptures; }

  /// Iterator that walks over the capture initialization arguments.
  using capture_init_iterator = Expr **;
  using capture_init_range = llvm::iterator_range<capture_init_iterator>;

  /// Const iterator that walks over the capture initialization
  /// arguments.
  using const_capture_init_iterator = Expr *const *;
  using const_capture_init_range =
      llvm::iterator_range<const_capture_init_iterator>;

  capture_init_range capture_inits() {
    return capture_init_range(capture_init_begin(), capture_init_end());
  }

  const_capture_init_range capture_inits() const {
    return const_capture_init_range(capture_init_begin(), capture_init_end());
  }

  /// Retrieve the first initialization argument.
  capture_init_iterator capture_init_begin() {
    return reinterpret_cast<Expr **>(getStoredStmts());
  }

  const_capture_init_iterator capture_init_begin() const {
    return reinterpret_cast<Expr *const *>(getStoredStmts());
  }

  /// Retrieve the iterator pointing one past the last initialization
  /// argument.
  capture_init_iterator capture_init_end() {
    return capture_init_begin() + NumCaptures;
  }

  const_capture_init_iterator capture_init_end() const {
    return capture_init_begin() + NumCaptures;
  }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getCapturedStmt()->getBeginLoc();
  }

  SourceLocation getEndLoc() const LLVM_READONLY {
    return getCapturedStmt()->getEndLoc();
  }

  SourceRange getSourceRange() const LLVM_READONLY {
    return getCapturedStmt()->getSourceRange();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CapturedStmtClass;
  }

  child_range children();
};

} // namespace clang

#endif // LLVM_CLANG_AST_STMT_H
