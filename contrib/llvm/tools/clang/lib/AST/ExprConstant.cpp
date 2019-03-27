//===--- ExprConstant.cpp - Expression Constant Evaluator -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Expr constant evaluator.
//
// Constant expression evaluation produces four main results:
//
//  * A success/failure flag indicating whether constant folding was successful.
//    This is the 'bool' return value used by most of the code in this file. A
//    'false' return value indicates that constant folding has failed, and any
//    appropriate diagnostic has already been produced.
//
//  * An evaluated result, valid only if constant folding has not failed.
//
//  * A flag indicating if evaluation encountered (unevaluated) side-effects.
//    These arise in cases such as (sideEffect(), 0) and (sideEffect() || 1),
//    where it is possible to determine the evaluated result regardless.
//
//  * A set of notes indicating why the evaluation was not a constant expression
//    (under the C++11 / C++1y rules only, at the moment), or, if folding failed
//    too, why the expression could not be folded.
//
// If we are checking for a potential constant expression, failure to constant
// fold a potential constant sub-expression will be indicated by a 'false'
// return value (the expression could not be folded) and no diagnostic (the
// expression is not necessarily non-constant).
//
//===----------------------------------------------------------------------===//

#include "clang/AST/APValue.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OSLog.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>
#include <functional>

#define DEBUG_TYPE "exprconstant"

using namespace clang;
using llvm::APSInt;
using llvm::APFloat;

static bool IsGlobalLValue(APValue::LValueBase B);

namespace {
  struct LValue;
  struct CallStackFrame;
  struct EvalInfo;

  static QualType getType(APValue::LValueBase B) {
    if (!B) return QualType();
    if (const ValueDecl *D = B.dyn_cast<const ValueDecl*>()) {
      // FIXME: It's unclear where we're supposed to take the type from, and
      // this actually matters for arrays of unknown bound. Eg:
      //
      // extern int arr[]; void f() { extern int arr[3]; };
      // constexpr int *p = &arr[1]; // valid?
      //
      // For now, we take the array bound from the most recent declaration.
      for (auto *Redecl = cast<ValueDecl>(D->getMostRecentDecl()); Redecl;
           Redecl = cast_or_null<ValueDecl>(Redecl->getPreviousDecl())) {
        QualType T = Redecl->getType();
        if (!T->isIncompleteArrayType())
          return T;
      }
      return D->getType();
    }

    const Expr *Base = B.get<const Expr*>();

    // For a materialized temporary, the type of the temporary we materialized
    // may not be the type of the expression.
    if (const MaterializeTemporaryExpr *MTE =
            dyn_cast<MaterializeTemporaryExpr>(Base)) {
      SmallVector<const Expr *, 2> CommaLHSs;
      SmallVector<SubobjectAdjustment, 2> Adjustments;
      const Expr *Temp = MTE->GetTemporaryExpr();
      const Expr *Inner = Temp->skipRValueSubobjectAdjustments(CommaLHSs,
                                                               Adjustments);
      // Keep any cv-qualifiers from the reference if we generated a temporary
      // for it directly. Otherwise use the type after adjustment.
      if (!Adjustments.empty())
        return Inner->getType();
    }

    return Base->getType();
  }

  /// Get an LValue path entry, which is known to not be an array index, as a
  /// field or base class.
  static
  APValue::BaseOrMemberType getAsBaseOrMember(APValue::LValuePathEntry E) {
    APValue::BaseOrMemberType Value;
    Value.setFromOpaqueValue(E.BaseOrMember);
    return Value;
  }

  /// Get an LValue path entry, which is known to not be an array index, as a
  /// field declaration.
  static const FieldDecl *getAsField(APValue::LValuePathEntry E) {
    return dyn_cast<FieldDecl>(getAsBaseOrMember(E).getPointer());
  }
  /// Get an LValue path entry, which is known to not be an array index, as a
  /// base class declaration.
  static const CXXRecordDecl *getAsBaseClass(APValue::LValuePathEntry E) {
    return dyn_cast<CXXRecordDecl>(getAsBaseOrMember(E).getPointer());
  }
  /// Determine whether this LValue path entry for a base class names a virtual
  /// base class.
  static bool isVirtualBaseClass(APValue::LValuePathEntry E) {
    return getAsBaseOrMember(E).getInt();
  }

  /// Given a CallExpr, try to get the alloc_size attribute. May return null.
  static const AllocSizeAttr *getAllocSizeAttr(const CallExpr *CE) {
    const FunctionDecl *Callee = CE->getDirectCallee();
    return Callee ? Callee->getAttr<AllocSizeAttr>() : nullptr;
  }

  /// Attempts to unwrap a CallExpr (with an alloc_size attribute) from an Expr.
  /// This will look through a single cast.
  ///
  /// Returns null if we couldn't unwrap a function with alloc_size.
  static const CallExpr *tryUnwrapAllocSizeCall(const Expr *E) {
    if (!E->getType()->isPointerType())
      return nullptr;

    E = E->IgnoreParens();
    // If we're doing a variable assignment from e.g. malloc(N), there will
    // probably be a cast of some kind. In exotic cases, we might also see a
    // top-level ExprWithCleanups. Ignore them either way.
    if (const auto *FE = dyn_cast<FullExpr>(E))
      E = FE->getSubExpr()->IgnoreParens();

    if (const auto *Cast = dyn_cast<CastExpr>(E))
      E = Cast->getSubExpr()->IgnoreParens();

    if (const auto *CE = dyn_cast<CallExpr>(E))
      return getAllocSizeAttr(CE) ? CE : nullptr;
    return nullptr;
  }

  /// Determines whether or not the given Base contains a call to a function
  /// with the alloc_size attribute.
  static bool isBaseAnAllocSizeCall(APValue::LValueBase Base) {
    const auto *E = Base.dyn_cast<const Expr *>();
    return E && E->getType()->isPointerType() && tryUnwrapAllocSizeCall(E);
  }

  /// The bound to claim that an array of unknown bound has.
  /// The value in MostDerivedArraySize is undefined in this case. So, set it
  /// to an arbitrary value that's likely to loudly break things if it's used.
  static const uint64_t AssumedSizeForUnsizedArray =
      std::numeric_limits<uint64_t>::max() / 2;

  /// Determines if an LValue with the given LValueBase will have an unsized
  /// array in its designator.
  /// Find the path length and type of the most-derived subobject in the given
  /// path, and find the size of the containing array, if any.
  static unsigned
  findMostDerivedSubobject(ASTContext &Ctx, APValue::LValueBase Base,
                           ArrayRef<APValue::LValuePathEntry> Path,
                           uint64_t &ArraySize, QualType &Type, bool &IsArray,
                           bool &FirstEntryIsUnsizedArray) {
    // This only accepts LValueBases from APValues, and APValues don't support
    // arrays that lack size info.
    assert(!isBaseAnAllocSizeCall(Base) &&
           "Unsized arrays shouldn't appear here");
    unsigned MostDerivedLength = 0;
    Type = getType(Base);

    for (unsigned I = 0, N = Path.size(); I != N; ++I) {
      if (Type->isArrayType()) {
        const ArrayType *AT = Ctx.getAsArrayType(Type);
        Type = AT->getElementType();
        MostDerivedLength = I + 1;
        IsArray = true;

        if (auto *CAT = dyn_cast<ConstantArrayType>(AT)) {
          ArraySize = CAT->getSize().getZExtValue();
        } else {
          assert(I == 0 && "unexpected unsized array designator");
          FirstEntryIsUnsizedArray = true;
          ArraySize = AssumedSizeForUnsizedArray;
        }
      } else if (Type->isAnyComplexType()) {
        const ComplexType *CT = Type->castAs<ComplexType>();
        Type = CT->getElementType();
        ArraySize = 2;
        MostDerivedLength = I + 1;
        IsArray = true;
      } else if (const FieldDecl *FD = getAsField(Path[I])) {
        Type = FD->getType();
        ArraySize = 0;
        MostDerivedLength = I + 1;
        IsArray = false;
      } else {
        // Path[I] describes a base class.
        ArraySize = 0;
        IsArray = false;
      }
    }
    return MostDerivedLength;
  }

  // The order of this enum is important for diagnostics.
  enum CheckSubobjectKind {
    CSK_Base, CSK_Derived, CSK_Field, CSK_ArrayToPointer, CSK_ArrayIndex,
    CSK_This, CSK_Real, CSK_Imag
  };

  /// A path from a glvalue to a subobject of that glvalue.
  struct SubobjectDesignator {
    /// True if the subobject was named in a manner not supported by C++11. Such
    /// lvalues can still be folded, but they are not core constant expressions
    /// and we cannot perform lvalue-to-rvalue conversions on them.
    unsigned Invalid : 1;

    /// Is this a pointer one past the end of an object?
    unsigned IsOnePastTheEnd : 1;

    /// Indicator of whether the first entry is an unsized array.
    unsigned FirstEntryIsAnUnsizedArray : 1;

    /// Indicator of whether the most-derived object is an array element.
    unsigned MostDerivedIsArrayElement : 1;

    /// The length of the path to the most-derived object of which this is a
    /// subobject.
    unsigned MostDerivedPathLength : 28;

    /// The size of the array of which the most-derived object is an element.
    /// This will always be 0 if the most-derived object is not an array
    /// element. 0 is not an indicator of whether or not the most-derived object
    /// is an array, however, because 0-length arrays are allowed.
    ///
    /// If the current array is an unsized array, the value of this is
    /// undefined.
    uint64_t MostDerivedArraySize;

    /// The type of the most derived object referred to by this address.
    QualType MostDerivedType;

    typedef APValue::LValuePathEntry PathEntry;

    /// The entries on the path from the glvalue to the designated subobject.
    SmallVector<PathEntry, 8> Entries;

    SubobjectDesignator() : Invalid(true) {}

    explicit SubobjectDesignator(QualType T)
        : Invalid(false), IsOnePastTheEnd(false),
          FirstEntryIsAnUnsizedArray(false), MostDerivedIsArrayElement(false),
          MostDerivedPathLength(0), MostDerivedArraySize(0),
          MostDerivedType(T) {}

    SubobjectDesignator(ASTContext &Ctx, const APValue &V)
        : Invalid(!V.isLValue() || !V.hasLValuePath()), IsOnePastTheEnd(false),
          FirstEntryIsAnUnsizedArray(false), MostDerivedIsArrayElement(false),
          MostDerivedPathLength(0), MostDerivedArraySize(0) {
      assert(V.isLValue() && "Non-LValue used to make an LValue designator?");
      if (!Invalid) {
        IsOnePastTheEnd = V.isLValueOnePastTheEnd();
        ArrayRef<PathEntry> VEntries = V.getLValuePath();
        Entries.insert(Entries.end(), VEntries.begin(), VEntries.end());
        if (V.getLValueBase()) {
          bool IsArray = false;
          bool FirstIsUnsizedArray = false;
          MostDerivedPathLength = findMostDerivedSubobject(
              Ctx, V.getLValueBase(), V.getLValuePath(), MostDerivedArraySize,
              MostDerivedType, IsArray, FirstIsUnsizedArray);
          MostDerivedIsArrayElement = IsArray;
          FirstEntryIsAnUnsizedArray = FirstIsUnsizedArray;
        }
      }
    }

    void setInvalid() {
      Invalid = true;
      Entries.clear();
    }

    /// Determine whether the most derived subobject is an array without a
    /// known bound.
    bool isMostDerivedAnUnsizedArray() const {
      assert(!Invalid && "Calling this makes no sense on invalid designators");
      return Entries.size() == 1 && FirstEntryIsAnUnsizedArray;
    }

    /// Determine what the most derived array's size is. Results in an assertion
    /// failure if the most derived array lacks a size.
    uint64_t getMostDerivedArraySize() const {
      assert(!isMostDerivedAnUnsizedArray() && "Unsized array has no size");
      return MostDerivedArraySize;
    }

    /// Determine whether this is a one-past-the-end pointer.
    bool isOnePastTheEnd() const {
      assert(!Invalid);
      if (IsOnePastTheEnd)
        return true;
      if (!isMostDerivedAnUnsizedArray() && MostDerivedIsArrayElement &&
          Entries[MostDerivedPathLength - 1].ArrayIndex == MostDerivedArraySize)
        return true;
      return false;
    }

    /// Get the range of valid index adjustments in the form
    ///   {maximum value that can be subtracted from this pointer,
    ///    maximum value that can be added to this pointer}
    std::pair<uint64_t, uint64_t> validIndexAdjustments() {
      if (Invalid || isMostDerivedAnUnsizedArray())
        return {0, 0};

      // [expr.add]p4: For the purposes of these operators, a pointer to a
      // nonarray object behaves the same as a pointer to the first element of
      // an array of length one with the type of the object as its element type.
      bool IsArray = MostDerivedPathLength == Entries.size() &&
                     MostDerivedIsArrayElement;
      uint64_t ArrayIndex =
          IsArray ? Entries.back().ArrayIndex : (uint64_t)IsOnePastTheEnd;
      uint64_t ArraySize =
          IsArray ? getMostDerivedArraySize() : (uint64_t)1;
      return {ArrayIndex, ArraySize - ArrayIndex};
    }

    /// Check that this refers to a valid subobject.
    bool isValidSubobject() const {
      if (Invalid)
        return false;
      return !isOnePastTheEnd();
    }
    /// Check that this refers to a valid subobject, and if not, produce a
    /// relevant diagnostic and set the designator as invalid.
    bool checkSubobject(EvalInfo &Info, const Expr *E, CheckSubobjectKind CSK);

    /// Get the type of the designated object.
    QualType getType(ASTContext &Ctx) const {
      assert(!Invalid && "invalid designator has no subobject type");
      return MostDerivedPathLength == Entries.size()
                 ? MostDerivedType
                 : Ctx.getRecordType(getAsBaseClass(Entries.back()));
    }

    /// Update this designator to refer to the first element within this array.
    void addArrayUnchecked(const ConstantArrayType *CAT) {
      PathEntry Entry;
      Entry.ArrayIndex = 0;
      Entries.push_back(Entry);

      // This is a most-derived object.
      MostDerivedType = CAT->getElementType();
      MostDerivedIsArrayElement = true;
      MostDerivedArraySize = CAT->getSize().getZExtValue();
      MostDerivedPathLength = Entries.size();
    }
    /// Update this designator to refer to the first element within the array of
    /// elements of type T. This is an array of unknown size.
    void addUnsizedArrayUnchecked(QualType ElemTy) {
      PathEntry Entry;
      Entry.ArrayIndex = 0;
      Entries.push_back(Entry);

      MostDerivedType = ElemTy;
      MostDerivedIsArrayElement = true;
      // The value in MostDerivedArraySize is undefined in this case. So, set it
      // to an arbitrary value that's likely to loudly break things if it's
      // used.
      MostDerivedArraySize = AssumedSizeForUnsizedArray;
      MostDerivedPathLength = Entries.size();
    }
    /// Update this designator to refer to the given base or member of this
    /// object.
    void addDeclUnchecked(const Decl *D, bool Virtual = false) {
      PathEntry Entry;
      APValue::BaseOrMemberType Value(D, Virtual);
      Entry.BaseOrMember = Value.getOpaqueValue();
      Entries.push_back(Entry);

      // If this isn't a base class, it's a new most-derived object.
      if (const FieldDecl *FD = dyn_cast<FieldDecl>(D)) {
        MostDerivedType = FD->getType();
        MostDerivedIsArrayElement = false;
        MostDerivedArraySize = 0;
        MostDerivedPathLength = Entries.size();
      }
    }
    /// Update this designator to refer to the given complex component.
    void addComplexUnchecked(QualType EltTy, bool Imag) {
      PathEntry Entry;
      Entry.ArrayIndex = Imag;
      Entries.push_back(Entry);

      // This is technically a most-derived object, though in practice this
      // is unlikely to matter.
      MostDerivedType = EltTy;
      MostDerivedIsArrayElement = true;
      MostDerivedArraySize = 2;
      MostDerivedPathLength = Entries.size();
    }
    void diagnoseUnsizedArrayPointerArithmetic(EvalInfo &Info, const Expr *E);
    void diagnosePointerArithmetic(EvalInfo &Info, const Expr *E,
                                   const APSInt &N);
    /// Add N to the address of this subobject.
    void adjustIndex(EvalInfo &Info, const Expr *E, APSInt N) {
      if (Invalid || !N) return;
      uint64_t TruncatedN = N.extOrTrunc(64).getZExtValue();
      if (isMostDerivedAnUnsizedArray()) {
        diagnoseUnsizedArrayPointerArithmetic(Info, E);
        // Can't verify -- trust that the user is doing the right thing (or if
        // not, trust that the caller will catch the bad behavior).
        // FIXME: Should we reject if this overflows, at least?
        Entries.back().ArrayIndex += TruncatedN;
        return;
      }

      // [expr.add]p4: For the purposes of these operators, a pointer to a
      // nonarray object behaves the same as a pointer to the first element of
      // an array of length one with the type of the object as its element type.
      bool IsArray = MostDerivedPathLength == Entries.size() &&
                     MostDerivedIsArrayElement;
      uint64_t ArrayIndex =
          IsArray ? Entries.back().ArrayIndex : (uint64_t)IsOnePastTheEnd;
      uint64_t ArraySize =
          IsArray ? getMostDerivedArraySize() : (uint64_t)1;

      if (N < -(int64_t)ArrayIndex || N > ArraySize - ArrayIndex) {
        // Calculate the actual index in a wide enough type, so we can include
        // it in the note.
        N = N.extend(std::max<unsigned>(N.getBitWidth() + 1, 65));
        (llvm::APInt&)N += ArrayIndex;
        assert(N.ugt(ArraySize) && "bounds check failed for in-bounds index");
        diagnosePointerArithmetic(Info, E, N);
        setInvalid();
        return;
      }

      ArrayIndex += TruncatedN;
      assert(ArrayIndex <= ArraySize &&
             "bounds check succeeded for out-of-bounds index");

      if (IsArray)
        Entries.back().ArrayIndex = ArrayIndex;
      else
        IsOnePastTheEnd = (ArrayIndex != 0);
    }
  };

  /// A stack frame in the constexpr call stack.
  struct CallStackFrame {
    EvalInfo &Info;

    /// Parent - The caller of this stack frame.
    CallStackFrame *Caller;

    /// Callee - The function which was called.
    const FunctionDecl *Callee;

    /// This - The binding for the this pointer in this call, if any.
    const LValue *This;

    /// Arguments - Parameter bindings for this function call, indexed by
    /// parameters' function scope indices.
    APValue *Arguments;

    // Note that we intentionally use std::map here so that references to
    // values are stable.
    typedef std::pair<const void *, unsigned> MapKeyTy;
    typedef std::map<MapKeyTy, APValue> MapTy;
    /// Temporaries - Temporary lvalues materialized within this stack frame.
    MapTy Temporaries;

    /// CallLoc - The location of the call expression for this call.
    SourceLocation CallLoc;

    /// Index - The call index of this call.
    unsigned Index;

    /// The stack of integers for tracking version numbers for temporaries.
    SmallVector<unsigned, 2> TempVersionStack = {1};
    unsigned CurTempVersion = TempVersionStack.back();

    unsigned getTempVersion() const { return TempVersionStack.back(); }

    void pushTempVersion() {
      TempVersionStack.push_back(++CurTempVersion);
    }

    void popTempVersion() {
      TempVersionStack.pop_back();
    }

    // FIXME: Adding this to every 'CallStackFrame' may have a nontrivial impact
    // on the overall stack usage of deeply-recursing constexpr evaluations.
    // (We should cache this map rather than recomputing it repeatedly.)
    // But let's try this and see how it goes; we can look into caching the map
    // as a later change.

    /// LambdaCaptureFields - Mapping from captured variables/this to
    /// corresponding data members in the closure class.
    llvm::DenseMap<const VarDecl *, FieldDecl *> LambdaCaptureFields;
    FieldDecl *LambdaThisCaptureField;

    CallStackFrame(EvalInfo &Info, SourceLocation CallLoc,
                   const FunctionDecl *Callee, const LValue *This,
                   APValue *Arguments);
    ~CallStackFrame();

    // Return the temporary for Key whose version number is Version.
    APValue *getTemporary(const void *Key, unsigned Version) {
      MapKeyTy KV(Key, Version);
      auto LB = Temporaries.lower_bound(KV);
      if (LB != Temporaries.end() && LB->first == KV)
        return &LB->second;
      // Pair (Key,Version) wasn't found in the map. Check that no elements
      // in the map have 'Key' as their key.
      assert((LB == Temporaries.end() || LB->first.first != Key) &&
             (LB == Temporaries.begin() || std::prev(LB)->first.first != Key) &&
             "Element with key 'Key' found in map");
      return nullptr;
    }

    // Return the current temporary for Key in the map.
    APValue *getCurrentTemporary(const void *Key) {
      auto UB = Temporaries.upper_bound(MapKeyTy(Key, UINT_MAX));
      if (UB != Temporaries.begin() && std::prev(UB)->first.first == Key)
        return &std::prev(UB)->second;
      return nullptr;
    }

    // Return the version number of the current temporary for Key.
    unsigned getCurrentTemporaryVersion(const void *Key) const {
      auto UB = Temporaries.upper_bound(MapKeyTy(Key, UINT_MAX));
      if (UB != Temporaries.begin() && std::prev(UB)->first.first == Key)
        return std::prev(UB)->first.second;
      return 0;
    }

    APValue &createTemporary(const void *Key, bool IsLifetimeExtended);
  };

  /// Temporarily override 'this'.
  class ThisOverrideRAII {
  public:
    ThisOverrideRAII(CallStackFrame &Frame, const LValue *NewThis, bool Enable)
        : Frame(Frame), OldThis(Frame.This) {
      if (Enable)
        Frame.This = NewThis;
    }
    ~ThisOverrideRAII() {
      Frame.This = OldThis;
    }
  private:
    CallStackFrame &Frame;
    const LValue *OldThis;
  };

  /// A partial diagnostic which we might know in advance that we are not going
  /// to emit.
  class OptionalDiagnostic {
    PartialDiagnostic *Diag;

  public:
    explicit OptionalDiagnostic(PartialDiagnostic *Diag = nullptr)
      : Diag(Diag) {}

    template<typename T>
    OptionalDiagnostic &operator<<(const T &v) {
      if (Diag)
        *Diag << v;
      return *this;
    }

    OptionalDiagnostic &operator<<(const APSInt &I) {
      if (Diag) {
        SmallVector<char, 32> Buffer;
        I.toString(Buffer);
        *Diag << StringRef(Buffer.data(), Buffer.size());
      }
      return *this;
    }

    OptionalDiagnostic &operator<<(const APFloat &F) {
      if (Diag) {
        // FIXME: Force the precision of the source value down so we don't
        // print digits which are usually useless (we don't really care here if
        // we truncate a digit by accident in edge cases).  Ideally,
        // APFloat::toString would automatically print the shortest
        // representation which rounds to the correct value, but it's a bit
        // tricky to implement.
        unsigned precision =
            llvm::APFloat::semanticsPrecision(F.getSemantics());
        precision = (precision * 59 + 195) / 196;
        SmallVector<char, 32> Buffer;
        F.toString(Buffer, precision);
        *Diag << StringRef(Buffer.data(), Buffer.size());
      }
      return *this;
    }
  };

  /// A cleanup, and a flag indicating whether it is lifetime-extended.
  class Cleanup {
    llvm::PointerIntPair<APValue*, 1, bool> Value;

  public:
    Cleanup(APValue *Val, bool IsLifetimeExtended)
        : Value(Val, IsLifetimeExtended) {}

    bool isLifetimeExtended() const { return Value.getInt(); }
    void endLifetime() {
      *Value.getPointer() = APValue();
    }
  };

  /// EvalInfo - This is a private struct used by the evaluator to capture
  /// information about a subexpression as it is folded.  It retains information
  /// about the AST context, but also maintains information about the folded
  /// expression.
  ///
  /// If an expression could be evaluated, it is still possible it is not a C
  /// "integer constant expression" or constant expression.  If not, this struct
  /// captures information about how and why not.
  ///
  /// One bit of information passed *into* the request for constant folding
  /// indicates whether the subexpression is "evaluated" or not according to C
  /// rules.  For example, the RHS of (0 && foo()) is not evaluated.  We can
  /// evaluate the expression regardless of what the RHS is, but C only allows
  /// certain things in certain situations.
  struct EvalInfo {
    ASTContext &Ctx;

    /// EvalStatus - Contains information about the evaluation.
    Expr::EvalStatus &EvalStatus;

    /// CurrentCall - The top of the constexpr call stack.
    CallStackFrame *CurrentCall;

    /// CallStackDepth - The number of calls in the call stack right now.
    unsigned CallStackDepth;

    /// NextCallIndex - The next call index to assign.
    unsigned NextCallIndex;

    /// StepsLeft - The remaining number of evaluation steps we're permitted
    /// to perform. This is essentially a limit for the number of statements
    /// we will evaluate.
    unsigned StepsLeft;

    /// BottomFrame - The frame in which evaluation started. This must be
    /// initialized after CurrentCall and CallStackDepth.
    CallStackFrame BottomFrame;

    /// A stack of values whose lifetimes end at the end of some surrounding
    /// evaluation frame.
    llvm::SmallVector<Cleanup, 16> CleanupStack;

    /// EvaluatingDecl - This is the declaration whose initializer is being
    /// evaluated, if any.
    APValue::LValueBase EvaluatingDecl;

    /// EvaluatingDeclValue - This is the value being constructed for the
    /// declaration whose initializer is being evaluated, if any.
    APValue *EvaluatingDeclValue;

    /// EvaluatingObject - Pair of the AST node that an lvalue represents and
    /// the call index that that lvalue was allocated in.
    typedef std::pair<APValue::LValueBase, std::pair<unsigned, unsigned>>
        EvaluatingObject;

    /// EvaluatingConstructors - Set of objects that are currently being
    /// constructed.
    llvm::DenseSet<EvaluatingObject> EvaluatingConstructors;

    struct EvaluatingConstructorRAII {
      EvalInfo &EI;
      EvaluatingObject Object;
      bool DidInsert;
      EvaluatingConstructorRAII(EvalInfo &EI, EvaluatingObject Object)
          : EI(EI), Object(Object) {
        DidInsert = EI.EvaluatingConstructors.insert(Object).second;
      }
      ~EvaluatingConstructorRAII() {
        if (DidInsert) EI.EvaluatingConstructors.erase(Object);
      }
    };

    bool isEvaluatingConstructor(APValue::LValueBase Decl, unsigned CallIndex,
                                 unsigned Version) {
      return EvaluatingConstructors.count(
          EvaluatingObject(Decl, {CallIndex, Version}));
    }

    /// The current array initialization index, if we're performing array
    /// initialization.
    uint64_t ArrayInitIndex = -1;

    /// HasActiveDiagnostic - Was the previous diagnostic stored? If so, further
    /// notes attached to it will also be stored, otherwise they will not be.
    bool HasActiveDiagnostic;

    /// Have we emitted a diagnostic explaining why we couldn't constant
    /// fold (not just why it's not strictly a constant expression)?
    bool HasFoldFailureDiagnostic;

    /// Whether or not we're currently speculatively evaluating.
    bool IsSpeculativelyEvaluating;

    /// Whether or not we're in a context where the front end requires a
    /// constant value.
    bool InConstantContext;

    enum EvaluationMode {
      /// Evaluate as a constant expression. Stop if we find that the expression
      /// is not a constant expression.
      EM_ConstantExpression,

      /// Evaluate as a potential constant expression. Keep going if we hit a
      /// construct that we can't evaluate yet (because we don't yet know the
      /// value of something) but stop if we hit something that could never be
      /// a constant expression.
      EM_PotentialConstantExpression,

      /// Fold the expression to a constant. Stop if we hit a side-effect that
      /// we can't model.
      EM_ConstantFold,

      /// Evaluate the expression looking for integer overflow and similar
      /// issues. Don't worry about side-effects, and try to visit all
      /// subexpressions.
      EM_EvaluateForOverflow,

      /// Evaluate in any way we know how. Don't worry about side-effects that
      /// can't be modeled.
      EM_IgnoreSideEffects,

      /// Evaluate as a constant expression. Stop if we find that the expression
      /// is not a constant expression. Some expressions can be retried in the
      /// optimizer if we don't constant fold them here, but in an unevaluated
      /// context we try to fold them immediately since the optimizer never
      /// gets a chance to look at it.
      EM_ConstantExpressionUnevaluated,

      /// Evaluate as a potential constant expression. Keep going if we hit a
      /// construct that we can't evaluate yet (because we don't yet know the
      /// value of something) but stop if we hit something that could never be
      /// a constant expression. Some expressions can be retried in the
      /// optimizer if we don't constant fold them here, but in an unevaluated
      /// context we try to fold them immediately since the optimizer never
      /// gets a chance to look at it.
      EM_PotentialConstantExpressionUnevaluated,
    } EvalMode;

    /// Are we checking whether the expression is a potential constant
    /// expression?
    bool checkingPotentialConstantExpression() const {
      return EvalMode == EM_PotentialConstantExpression ||
             EvalMode == EM_PotentialConstantExpressionUnevaluated;
    }

    /// Are we checking an expression for overflow?
    // FIXME: We should check for any kind of undefined or suspicious behavior
    // in such constructs, not just overflow.
    bool checkingForOverflow() { return EvalMode == EM_EvaluateForOverflow; }

    EvalInfo(const ASTContext &C, Expr::EvalStatus &S, EvaluationMode Mode)
      : Ctx(const_cast<ASTContext &>(C)), EvalStatus(S), CurrentCall(nullptr),
        CallStackDepth(0), NextCallIndex(1),
        StepsLeft(getLangOpts().ConstexprStepLimit),
        BottomFrame(*this, SourceLocation(), nullptr, nullptr, nullptr),
        EvaluatingDecl((const ValueDecl *)nullptr),
        EvaluatingDeclValue(nullptr), HasActiveDiagnostic(false),
        HasFoldFailureDiagnostic(false), IsSpeculativelyEvaluating(false),
        InConstantContext(false), EvalMode(Mode) {}

    void setEvaluatingDecl(APValue::LValueBase Base, APValue &Value) {
      EvaluatingDecl = Base;
      EvaluatingDeclValue = &Value;
      EvaluatingConstructors.insert({Base, {0, 0}});
    }

    const LangOptions &getLangOpts() const { return Ctx.getLangOpts(); }

    bool CheckCallLimit(SourceLocation Loc) {
      // Don't perform any constexpr calls (other than the call we're checking)
      // when checking a potential constant expression.
      if (checkingPotentialConstantExpression() && CallStackDepth > 1)
        return false;
      if (NextCallIndex == 0) {
        // NextCallIndex has wrapped around.
        FFDiag(Loc, diag::note_constexpr_call_limit_exceeded);
        return false;
      }
      if (CallStackDepth <= getLangOpts().ConstexprCallDepth)
        return true;
      FFDiag(Loc, diag::note_constexpr_depth_limit_exceeded)
        << getLangOpts().ConstexprCallDepth;
      return false;
    }

    CallStackFrame *getCallFrame(unsigned CallIndex) {
      assert(CallIndex && "no call index in getCallFrame");
      // We will eventually hit BottomFrame, which has Index 1, so Frame can't
      // be null in this loop.
      CallStackFrame *Frame = CurrentCall;
      while (Frame->Index > CallIndex)
        Frame = Frame->Caller;
      return (Frame->Index == CallIndex) ? Frame : nullptr;
    }

    bool nextStep(const Stmt *S) {
      if (!StepsLeft) {
        FFDiag(S->getBeginLoc(), diag::note_constexpr_step_limit_exceeded);
        return false;
      }
      --StepsLeft;
      return true;
    }

  private:
    /// Add a diagnostic to the diagnostics list.
    PartialDiagnostic &addDiag(SourceLocation Loc, diag::kind DiagId) {
      PartialDiagnostic PD(DiagId, Ctx.getDiagAllocator());
      EvalStatus.Diag->push_back(std::make_pair(Loc, PD));
      return EvalStatus.Diag->back().second;
    }

    /// Add notes containing a call stack to the current point of evaluation.
    void addCallStack(unsigned Limit);

  private:
    OptionalDiagnostic Diag(SourceLocation Loc, diag::kind DiagId,
                            unsigned ExtraNotes, bool IsCCEDiag) {

      if (EvalStatus.Diag) {
        // If we have a prior diagnostic, it will be noting that the expression
        // isn't a constant expression. This diagnostic is more important,
        // unless we require this evaluation to produce a constant expression.
        //
        // FIXME: We might want to show both diagnostics to the user in
        // EM_ConstantFold mode.
        if (!EvalStatus.Diag->empty()) {
          switch (EvalMode) {
          case EM_ConstantFold:
          case EM_IgnoreSideEffects:
          case EM_EvaluateForOverflow:
            if (!HasFoldFailureDiagnostic)
              break;
            // We've already failed to fold something. Keep that diagnostic.
            LLVM_FALLTHROUGH;
          case EM_ConstantExpression:
          case EM_PotentialConstantExpression:
          case EM_ConstantExpressionUnevaluated:
          case EM_PotentialConstantExpressionUnevaluated:
            HasActiveDiagnostic = false;
            return OptionalDiagnostic();
          }
        }

        unsigned CallStackNotes = CallStackDepth - 1;
        unsigned Limit = Ctx.getDiagnostics().getConstexprBacktraceLimit();
        if (Limit)
          CallStackNotes = std::min(CallStackNotes, Limit + 1);
        if (checkingPotentialConstantExpression())
          CallStackNotes = 0;

        HasActiveDiagnostic = true;
        HasFoldFailureDiagnostic = !IsCCEDiag;
        EvalStatus.Diag->clear();
        EvalStatus.Diag->reserve(1 + ExtraNotes + CallStackNotes);
        addDiag(Loc, DiagId);
        if (!checkingPotentialConstantExpression())
          addCallStack(Limit);
        return OptionalDiagnostic(&(*EvalStatus.Diag)[0].second);
      }
      HasActiveDiagnostic = false;
      return OptionalDiagnostic();
    }
  public:
    // Diagnose that the evaluation could not be folded (FF => FoldFailure)
    OptionalDiagnostic
    FFDiag(SourceLocation Loc,
          diag::kind DiagId = diag::note_invalid_subexpr_in_const_expr,
          unsigned ExtraNotes = 0) {
      return Diag(Loc, DiagId, ExtraNotes, false);
    }

    OptionalDiagnostic FFDiag(const Expr *E, diag::kind DiagId
                              = diag::note_invalid_subexpr_in_const_expr,
                            unsigned ExtraNotes = 0) {
      if (EvalStatus.Diag)
        return Diag(E->getExprLoc(), DiagId, ExtraNotes, /*IsCCEDiag*/false);
      HasActiveDiagnostic = false;
      return OptionalDiagnostic();
    }

    /// Diagnose that the evaluation does not produce a C++11 core constant
    /// expression.
    ///
    /// FIXME: Stop evaluating if we're in EM_ConstantExpression or
    /// EM_PotentialConstantExpression mode and we produce one of these.
    OptionalDiagnostic CCEDiag(SourceLocation Loc, diag::kind DiagId
                                 = diag::note_invalid_subexpr_in_const_expr,
                               unsigned ExtraNotes = 0) {
      // Don't override a previous diagnostic. Don't bother collecting
      // diagnostics if we're evaluating for overflow.
      if (!EvalStatus.Diag || !EvalStatus.Diag->empty()) {
        HasActiveDiagnostic = false;
        return OptionalDiagnostic();
      }
      return Diag(Loc, DiagId, ExtraNotes, true);
    }
    OptionalDiagnostic CCEDiag(const Expr *E, diag::kind DiagId
                                 = diag::note_invalid_subexpr_in_const_expr,
                               unsigned ExtraNotes = 0) {
      return CCEDiag(E->getExprLoc(), DiagId, ExtraNotes);
    }
    /// Add a note to a prior diagnostic.
    OptionalDiagnostic Note(SourceLocation Loc, diag::kind DiagId) {
      if (!HasActiveDiagnostic)
        return OptionalDiagnostic();
      return OptionalDiagnostic(&addDiag(Loc, DiagId));
    }

    /// Add a stack of notes to a prior diagnostic.
    void addNotes(ArrayRef<PartialDiagnosticAt> Diags) {
      if (HasActiveDiagnostic) {
        EvalStatus.Diag->insert(EvalStatus.Diag->end(),
                                Diags.begin(), Diags.end());
      }
    }

    /// Should we continue evaluation after encountering a side-effect that we
    /// couldn't model?
    bool keepEvaluatingAfterSideEffect() {
      switch (EvalMode) {
      case EM_PotentialConstantExpression:
      case EM_PotentialConstantExpressionUnevaluated:
      case EM_EvaluateForOverflow:
      case EM_IgnoreSideEffects:
        return true;

      case EM_ConstantExpression:
      case EM_ConstantExpressionUnevaluated:
      case EM_ConstantFold:
        return false;
      }
      llvm_unreachable("Missed EvalMode case");
    }

    /// Note that we have had a side-effect, and determine whether we should
    /// keep evaluating.
    bool noteSideEffect() {
      EvalStatus.HasSideEffects = true;
      return keepEvaluatingAfterSideEffect();
    }

    /// Should we continue evaluation after encountering undefined behavior?
    bool keepEvaluatingAfterUndefinedBehavior() {
      switch (EvalMode) {
      case EM_EvaluateForOverflow:
      case EM_IgnoreSideEffects:
      case EM_ConstantFold:
        return true;

      case EM_PotentialConstantExpression:
      case EM_PotentialConstantExpressionUnevaluated:
      case EM_ConstantExpression:
      case EM_ConstantExpressionUnevaluated:
        return false;
      }
      llvm_unreachable("Missed EvalMode case");
    }

    /// Note that we hit something that was technically undefined behavior, but
    /// that we can evaluate past it (such as signed overflow or floating-point
    /// division by zero.)
    bool noteUndefinedBehavior() {
      EvalStatus.HasUndefinedBehavior = true;
      return keepEvaluatingAfterUndefinedBehavior();
    }

    /// Should we continue evaluation as much as possible after encountering a
    /// construct which can't be reduced to a value?
    bool keepEvaluatingAfterFailure() {
      if (!StepsLeft)
        return false;

      switch (EvalMode) {
      case EM_PotentialConstantExpression:
      case EM_PotentialConstantExpressionUnevaluated:
      case EM_EvaluateForOverflow:
        return true;

      case EM_ConstantExpression:
      case EM_ConstantExpressionUnevaluated:
      case EM_ConstantFold:
      case EM_IgnoreSideEffects:
        return false;
      }
      llvm_unreachable("Missed EvalMode case");
    }

    /// Notes that we failed to evaluate an expression that other expressions
    /// directly depend on, and determine if we should keep evaluating. This
    /// should only be called if we actually intend to keep evaluating.
    ///
    /// Call noteSideEffect() instead if we may be able to ignore the value that
    /// we failed to evaluate, e.g. if we failed to evaluate Foo() in:
    ///
    /// (Foo(), 1)      // use noteSideEffect
    /// (Foo() || true) // use noteSideEffect
    /// Foo() + 1       // use noteFailure
    LLVM_NODISCARD bool noteFailure() {
      // Failure when evaluating some expression often means there is some
      // subexpression whose evaluation was skipped. Therefore, (because we
      // don't track whether we skipped an expression when unwinding after an
      // evaluation failure) every evaluation failure that bubbles up from a
      // subexpression implies that a side-effect has potentially happened. We
      // skip setting the HasSideEffects flag to true until we decide to
      // continue evaluating after that point, which happens here.
      bool KeepGoing = keepEvaluatingAfterFailure();
      EvalStatus.HasSideEffects |= KeepGoing;
      return KeepGoing;
    }

    class ArrayInitLoopIndex {
      EvalInfo &Info;
      uint64_t OuterIndex;

    public:
      ArrayInitLoopIndex(EvalInfo &Info)
          : Info(Info), OuterIndex(Info.ArrayInitIndex) {
        Info.ArrayInitIndex = 0;
      }
      ~ArrayInitLoopIndex() { Info.ArrayInitIndex = OuterIndex; }

      operator uint64_t&() { return Info.ArrayInitIndex; }
    };
  };

  /// Object used to treat all foldable expressions as constant expressions.
  struct FoldConstant {
    EvalInfo &Info;
    bool Enabled;
    bool HadNoPriorDiags;
    EvalInfo::EvaluationMode OldMode;

    explicit FoldConstant(EvalInfo &Info, bool Enabled)
      : Info(Info),
        Enabled(Enabled),
        HadNoPriorDiags(Info.EvalStatus.Diag &&
                        Info.EvalStatus.Diag->empty() &&
                        !Info.EvalStatus.HasSideEffects),
        OldMode(Info.EvalMode) {
      if (Enabled &&
          (Info.EvalMode == EvalInfo::EM_ConstantExpression ||
           Info.EvalMode == EvalInfo::EM_ConstantExpressionUnevaluated))
        Info.EvalMode = EvalInfo::EM_ConstantFold;
    }
    void keepDiagnostics() { Enabled = false; }
    ~FoldConstant() {
      if (Enabled && HadNoPriorDiags && !Info.EvalStatus.Diag->empty() &&
          !Info.EvalStatus.HasSideEffects)
        Info.EvalStatus.Diag->clear();
      Info.EvalMode = OldMode;
    }
  };

  /// RAII object used to set the current evaluation mode to ignore
  /// side-effects.
  struct IgnoreSideEffectsRAII {
    EvalInfo &Info;
    EvalInfo::EvaluationMode OldMode;
    explicit IgnoreSideEffectsRAII(EvalInfo &Info)
        : Info(Info), OldMode(Info.EvalMode) {
      if (!Info.checkingPotentialConstantExpression())
        Info.EvalMode = EvalInfo::EM_IgnoreSideEffects;
    }

    ~IgnoreSideEffectsRAII() { Info.EvalMode = OldMode; }
  };

  /// RAII object used to optionally suppress diagnostics and side-effects from
  /// a speculative evaluation.
  class SpeculativeEvaluationRAII {
    EvalInfo *Info = nullptr;
    Expr::EvalStatus OldStatus;
    bool OldIsSpeculativelyEvaluating;

    void moveFromAndCancel(SpeculativeEvaluationRAII &&Other) {
      Info = Other.Info;
      OldStatus = Other.OldStatus;
      OldIsSpeculativelyEvaluating = Other.OldIsSpeculativelyEvaluating;
      Other.Info = nullptr;
    }

    void maybeRestoreState() {
      if (!Info)
        return;

      Info->EvalStatus = OldStatus;
      Info->IsSpeculativelyEvaluating = OldIsSpeculativelyEvaluating;
    }

  public:
    SpeculativeEvaluationRAII() = default;

    SpeculativeEvaluationRAII(
        EvalInfo &Info, SmallVectorImpl<PartialDiagnosticAt> *NewDiag = nullptr)
        : Info(&Info), OldStatus(Info.EvalStatus),
          OldIsSpeculativelyEvaluating(Info.IsSpeculativelyEvaluating) {
      Info.EvalStatus.Diag = NewDiag;
      Info.IsSpeculativelyEvaluating = true;
    }

    SpeculativeEvaluationRAII(const SpeculativeEvaluationRAII &Other) = delete;
    SpeculativeEvaluationRAII(SpeculativeEvaluationRAII &&Other) {
      moveFromAndCancel(std::move(Other));
    }

    SpeculativeEvaluationRAII &operator=(SpeculativeEvaluationRAII &&Other) {
      maybeRestoreState();
      moveFromAndCancel(std::move(Other));
      return *this;
    }

    ~SpeculativeEvaluationRAII() { maybeRestoreState(); }
  };

  /// RAII object wrapping a full-expression or block scope, and handling
  /// the ending of the lifetime of temporaries created within it.
  template<bool IsFullExpression>
  class ScopeRAII {
    EvalInfo &Info;
    unsigned OldStackSize;
  public:
    ScopeRAII(EvalInfo &Info)
        : Info(Info), OldStackSize(Info.CleanupStack.size()) {
      // Push a new temporary version. This is needed to distinguish between
      // temporaries created in different iterations of a loop.
      Info.CurrentCall->pushTempVersion();
    }
    ~ScopeRAII() {
      // Body moved to a static method to encourage the compiler to inline away
      // instances of this class.
      cleanup(Info, OldStackSize);
      Info.CurrentCall->popTempVersion();
    }
  private:
    static void cleanup(EvalInfo &Info, unsigned OldStackSize) {
      unsigned NewEnd = OldStackSize;
      for (unsigned I = OldStackSize, N = Info.CleanupStack.size();
           I != N; ++I) {
        if (IsFullExpression && Info.CleanupStack[I].isLifetimeExtended()) {
          // Full-expression cleanup of a lifetime-extended temporary: nothing
          // to do, just move this cleanup to the right place in the stack.
          std::swap(Info.CleanupStack[I], Info.CleanupStack[NewEnd]);
          ++NewEnd;
        } else {
          // End the lifetime of the object.
          Info.CleanupStack[I].endLifetime();
        }
      }
      Info.CleanupStack.erase(Info.CleanupStack.begin() + NewEnd,
                              Info.CleanupStack.end());
    }
  };
  typedef ScopeRAII<false> BlockScopeRAII;
  typedef ScopeRAII<true> FullExpressionRAII;
}

bool SubobjectDesignator::checkSubobject(EvalInfo &Info, const Expr *E,
                                         CheckSubobjectKind CSK) {
  if (Invalid)
    return false;
  if (isOnePastTheEnd()) {
    Info.CCEDiag(E, diag::note_constexpr_past_end_subobject)
      << CSK;
    setInvalid();
    return false;
  }
  // Note, we do not diagnose if isMostDerivedAnUnsizedArray(), because there
  // must actually be at least one array element; even a VLA cannot have a
  // bound of zero. And if our index is nonzero, we already had a CCEDiag.
  return true;
}

void SubobjectDesignator::diagnoseUnsizedArrayPointerArithmetic(EvalInfo &Info,
                                                                const Expr *E) {
  Info.CCEDiag(E, diag::note_constexpr_unsized_array_indexed);
  // Do not set the designator as invalid: we can represent this situation,
  // and correct handling of __builtin_object_size requires us to do so.
}

void SubobjectDesignator::diagnosePointerArithmetic(EvalInfo &Info,
                                                    const Expr *E,
                                                    const APSInt &N) {
  // If we're complaining, we must be able to statically determine the size of
  // the most derived array.
  if (MostDerivedPathLength == Entries.size() && MostDerivedIsArrayElement)
    Info.CCEDiag(E, diag::note_constexpr_array_index)
      << N << /*array*/ 0
      << static_cast<unsigned>(getMostDerivedArraySize());
  else
    Info.CCEDiag(E, diag::note_constexpr_array_index)
      << N << /*non-array*/ 1;
  setInvalid();
}

CallStackFrame::CallStackFrame(EvalInfo &Info, SourceLocation CallLoc,
                               const FunctionDecl *Callee, const LValue *This,
                               APValue *Arguments)
    : Info(Info), Caller(Info.CurrentCall), Callee(Callee), This(This),
      Arguments(Arguments), CallLoc(CallLoc), Index(Info.NextCallIndex++) {
  Info.CurrentCall = this;
  ++Info.CallStackDepth;
}

CallStackFrame::~CallStackFrame() {
  assert(Info.CurrentCall == this && "calls retired out of order");
  --Info.CallStackDepth;
  Info.CurrentCall = Caller;
}

APValue &CallStackFrame::createTemporary(const void *Key,
                                         bool IsLifetimeExtended) {
  unsigned Version = Info.CurrentCall->getTempVersion();
  APValue &Result = Temporaries[MapKeyTy(Key, Version)];
  assert(Result.isUninit() && "temporary created multiple times");
  Info.CleanupStack.push_back(Cleanup(&Result, IsLifetimeExtended));
  return Result;
}

static void describeCall(CallStackFrame *Frame, raw_ostream &Out);

void EvalInfo::addCallStack(unsigned Limit) {
  // Determine which calls to skip, if any.
  unsigned ActiveCalls = CallStackDepth - 1;
  unsigned SkipStart = ActiveCalls, SkipEnd = SkipStart;
  if (Limit && Limit < ActiveCalls) {
    SkipStart = Limit / 2 + Limit % 2;
    SkipEnd = ActiveCalls - Limit / 2;
  }

  // Walk the call stack and add the diagnostics.
  unsigned CallIdx = 0;
  for (CallStackFrame *Frame = CurrentCall; Frame != &BottomFrame;
       Frame = Frame->Caller, ++CallIdx) {
    // Skip this call?
    if (CallIdx >= SkipStart && CallIdx < SkipEnd) {
      if (CallIdx == SkipStart) {
        // Note that we're skipping calls.
        addDiag(Frame->CallLoc, diag::note_constexpr_calls_suppressed)
          << unsigned(ActiveCalls - Limit);
      }
      continue;
    }

    // Use a different note for an inheriting constructor, because from the
    // user's perspective it's not really a function at all.
    if (auto *CD = dyn_cast_or_null<CXXConstructorDecl>(Frame->Callee)) {
      if (CD->isInheritingConstructor()) {
        addDiag(Frame->CallLoc, diag::note_constexpr_inherited_ctor_call_here)
          << CD->getParent();
        continue;
      }
    }

    SmallVector<char, 128> Buffer;
    llvm::raw_svector_ostream Out(Buffer);
    describeCall(Frame, Out);
    addDiag(Frame->CallLoc, diag::note_constexpr_call_here) << Out.str();
  }
}

/// Kinds of access we can perform on an object, for diagnostics.
enum AccessKinds {
  AK_Read,
  AK_Assign,
  AK_Increment,
  AK_Decrement
};

namespace {
  struct ComplexValue {
  private:
    bool IsInt;

  public:
    APSInt IntReal, IntImag;
    APFloat FloatReal, FloatImag;

    ComplexValue() : FloatReal(APFloat::Bogus()), FloatImag(APFloat::Bogus()) {}

    void makeComplexFloat() { IsInt = false; }
    bool isComplexFloat() const { return !IsInt; }
    APFloat &getComplexFloatReal() { return FloatReal; }
    APFloat &getComplexFloatImag() { return FloatImag; }

    void makeComplexInt() { IsInt = true; }
    bool isComplexInt() const { return IsInt; }
    APSInt &getComplexIntReal() { return IntReal; }
    APSInt &getComplexIntImag() { return IntImag; }

    void moveInto(APValue &v) const {
      if (isComplexFloat())
        v = APValue(FloatReal, FloatImag);
      else
        v = APValue(IntReal, IntImag);
    }
    void setFrom(const APValue &v) {
      assert(v.isComplexFloat() || v.isComplexInt());
      if (v.isComplexFloat()) {
        makeComplexFloat();
        FloatReal = v.getComplexFloatReal();
        FloatImag = v.getComplexFloatImag();
      } else {
        makeComplexInt();
        IntReal = v.getComplexIntReal();
        IntImag = v.getComplexIntImag();
      }
    }
  };

  struct LValue {
    APValue::LValueBase Base;
    CharUnits Offset;
    SubobjectDesignator Designator;
    bool IsNullPtr : 1;
    bool InvalidBase : 1;

    const APValue::LValueBase getLValueBase() const { return Base; }
    CharUnits &getLValueOffset() { return Offset; }
    const CharUnits &getLValueOffset() const { return Offset; }
    SubobjectDesignator &getLValueDesignator() { return Designator; }
    const SubobjectDesignator &getLValueDesignator() const { return Designator;}
    bool isNullPointer() const { return IsNullPtr;}

    unsigned getLValueCallIndex() const { return Base.getCallIndex(); }
    unsigned getLValueVersion() const { return Base.getVersion(); }

    void moveInto(APValue &V) const {
      if (Designator.Invalid)
        V = APValue(Base, Offset, APValue::NoLValuePath(), IsNullPtr);
      else {
        assert(!InvalidBase && "APValues can't handle invalid LValue bases");
        V = APValue(Base, Offset, Designator.Entries,
                    Designator.IsOnePastTheEnd, IsNullPtr);
      }
    }
    void setFrom(ASTContext &Ctx, const APValue &V) {
      assert(V.isLValue() && "Setting LValue from a non-LValue?");
      Base = V.getLValueBase();
      Offset = V.getLValueOffset();
      InvalidBase = false;
      Designator = SubobjectDesignator(Ctx, V);
      IsNullPtr = V.isNullPointer();
    }

    void set(APValue::LValueBase B, bool BInvalid = false) {
#ifndef NDEBUG
      // We only allow a few types of invalid bases. Enforce that here.
      if (BInvalid) {
        const auto *E = B.get<const Expr *>();
        assert((isa<MemberExpr>(E) || tryUnwrapAllocSizeCall(E)) &&
               "Unexpected type of invalid base");
      }
#endif

      Base = B;
      Offset = CharUnits::fromQuantity(0);
      InvalidBase = BInvalid;
      Designator = SubobjectDesignator(getType(B));
      IsNullPtr = false;
    }

    void setNull(QualType PointerTy, uint64_t TargetVal) {
      Base = (Expr *)nullptr;
      Offset = CharUnits::fromQuantity(TargetVal);
      InvalidBase = false;
      Designator = SubobjectDesignator(PointerTy->getPointeeType());
      IsNullPtr = true;
    }

    void setInvalid(APValue::LValueBase B, unsigned I = 0) {
      set(B, true);
    }

  private:
    // Check that this LValue is not based on a null pointer. If it is, produce
    // a diagnostic and mark the designator as invalid.
    template <typename GenDiagType>
    bool checkNullPointerDiagnosingWith(const GenDiagType &GenDiag) {
      if (Designator.Invalid)
        return false;
      if (IsNullPtr) {
        GenDiag();
        Designator.setInvalid();
        return false;
      }
      return true;
    }

  public:
    bool checkNullPointer(EvalInfo &Info, const Expr *E,
                          CheckSubobjectKind CSK) {
      return checkNullPointerDiagnosingWith([&Info, E, CSK] {
        Info.CCEDiag(E, diag::note_constexpr_null_subobject) << CSK;
      });
    }

    bool checkNullPointerForFoldAccess(EvalInfo &Info, const Expr *E,
                                       AccessKinds AK) {
      return checkNullPointerDiagnosingWith([&Info, E, AK] {
        Info.FFDiag(E, diag::note_constexpr_access_null) << AK;
      });
    }

    // Check this LValue refers to an object. If not, set the designator to be
    // invalid and emit a diagnostic.
    bool checkSubobject(EvalInfo &Info, const Expr *E, CheckSubobjectKind CSK) {
      return (CSK == CSK_ArrayToPointer || checkNullPointer(Info, E, CSK)) &&
             Designator.checkSubobject(Info, E, CSK);
    }

    void addDecl(EvalInfo &Info, const Expr *E,
                 const Decl *D, bool Virtual = false) {
      if (checkSubobject(Info, E, isa<FieldDecl>(D) ? CSK_Field : CSK_Base))
        Designator.addDeclUnchecked(D, Virtual);
    }
    void addUnsizedArray(EvalInfo &Info, const Expr *E, QualType ElemTy) {
      if (!Designator.Entries.empty()) {
        Info.CCEDiag(E, diag::note_constexpr_unsupported_unsized_array);
        Designator.setInvalid();
        return;
      }
      if (checkSubobject(Info, E, CSK_ArrayToPointer)) {
        assert(getType(Base)->isPointerType() || getType(Base)->isArrayType());
        Designator.FirstEntryIsAnUnsizedArray = true;
        Designator.addUnsizedArrayUnchecked(ElemTy);
      }
    }
    void addArray(EvalInfo &Info, const Expr *E, const ConstantArrayType *CAT) {
      if (checkSubobject(Info, E, CSK_ArrayToPointer))
        Designator.addArrayUnchecked(CAT);
    }
    void addComplex(EvalInfo &Info, const Expr *E, QualType EltTy, bool Imag) {
      if (checkSubobject(Info, E, Imag ? CSK_Imag : CSK_Real))
        Designator.addComplexUnchecked(EltTy, Imag);
    }
    void clearIsNullPointer() {
      IsNullPtr = false;
    }
    void adjustOffsetAndIndex(EvalInfo &Info, const Expr *E,
                              const APSInt &Index, CharUnits ElementSize) {
      // An index of 0 has no effect. (In C, adding 0 to a null pointer is UB,
      // but we're not required to diagnose it and it's valid in C++.)
      if (!Index)
        return;

      // Compute the new offset in the appropriate width, wrapping at 64 bits.
      // FIXME: When compiling for a 32-bit target, we should use 32-bit
      // offsets.
      uint64_t Offset64 = Offset.getQuantity();
      uint64_t ElemSize64 = ElementSize.getQuantity();
      uint64_t Index64 = Index.extOrTrunc(64).getZExtValue();
      Offset = CharUnits::fromQuantity(Offset64 + ElemSize64 * Index64);

      if (checkNullPointer(Info, E, CSK_ArrayIndex))
        Designator.adjustIndex(Info, E, Index);
      clearIsNullPointer();
    }
    void adjustOffset(CharUnits N) {
      Offset += N;
      if (N.getQuantity())
        clearIsNullPointer();
    }
  };

  struct MemberPtr {
    MemberPtr() {}
    explicit MemberPtr(const ValueDecl *Decl) :
      DeclAndIsDerivedMember(Decl, false), Path() {}

    /// The member or (direct or indirect) field referred to by this member
    /// pointer, or 0 if this is a null member pointer.
    const ValueDecl *getDecl() const {
      return DeclAndIsDerivedMember.getPointer();
    }
    /// Is this actually a member of some type derived from the relevant class?
    bool isDerivedMember() const {
      return DeclAndIsDerivedMember.getInt();
    }
    /// Get the class which the declaration actually lives in.
    const CXXRecordDecl *getContainingRecord() const {
      return cast<CXXRecordDecl>(
          DeclAndIsDerivedMember.getPointer()->getDeclContext());
    }

    void moveInto(APValue &V) const {
      V = APValue(getDecl(), isDerivedMember(), Path);
    }
    void setFrom(const APValue &V) {
      assert(V.isMemberPointer());
      DeclAndIsDerivedMember.setPointer(V.getMemberPointerDecl());
      DeclAndIsDerivedMember.setInt(V.isMemberPointerToDerivedMember());
      Path.clear();
      ArrayRef<const CXXRecordDecl*> P = V.getMemberPointerPath();
      Path.insert(Path.end(), P.begin(), P.end());
    }

    /// DeclAndIsDerivedMember - The member declaration, and a flag indicating
    /// whether the member is a member of some class derived from the class type
    /// of the member pointer.
    llvm::PointerIntPair<const ValueDecl*, 1, bool> DeclAndIsDerivedMember;
    /// Path - The path of base/derived classes from the member declaration's
    /// class (exclusive) to the class type of the member pointer (inclusive).
    SmallVector<const CXXRecordDecl*, 4> Path;

    /// Perform a cast towards the class of the Decl (either up or down the
    /// hierarchy).
    bool castBack(const CXXRecordDecl *Class) {
      assert(!Path.empty());
      const CXXRecordDecl *Expected;
      if (Path.size() >= 2)
        Expected = Path[Path.size() - 2];
      else
        Expected = getContainingRecord();
      if (Expected->getCanonicalDecl() != Class->getCanonicalDecl()) {
        // C++11 [expr.static.cast]p12: In a conversion from (D::*) to (B::*),
        // if B does not contain the original member and is not a base or
        // derived class of the class containing the original member, the result
        // of the cast is undefined.
        // C++11 [conv.mem]p2 does not cover this case for a cast from (B::*) to
        // (D::*). We consider that to be a language defect.
        return false;
      }
      Path.pop_back();
      return true;
    }
    /// Perform a base-to-derived member pointer cast.
    bool castToDerived(const CXXRecordDecl *Derived) {
      if (!getDecl())
        return true;
      if (!isDerivedMember()) {
        Path.push_back(Derived);
        return true;
      }
      if (!castBack(Derived))
        return false;
      if (Path.empty())
        DeclAndIsDerivedMember.setInt(false);
      return true;
    }
    /// Perform a derived-to-base member pointer cast.
    bool castToBase(const CXXRecordDecl *Base) {
      if (!getDecl())
        return true;
      if (Path.empty())
        DeclAndIsDerivedMember.setInt(true);
      if (isDerivedMember()) {
        Path.push_back(Base);
        return true;
      }
      return castBack(Base);
    }
  };

  /// Compare two member pointers, which are assumed to be of the same type.
  static bool operator==(const MemberPtr &LHS, const MemberPtr &RHS) {
    if (!LHS.getDecl() || !RHS.getDecl())
      return !LHS.getDecl() && !RHS.getDecl();
    if (LHS.getDecl()->getCanonicalDecl() != RHS.getDecl()->getCanonicalDecl())
      return false;
    return LHS.Path == RHS.Path;
  }
}

static bool Evaluate(APValue &Result, EvalInfo &Info, const Expr *E);
static bool EvaluateInPlace(APValue &Result, EvalInfo &Info,
                            const LValue &This, const Expr *E,
                            bool AllowNonLiteralTypes = false);
static bool EvaluateLValue(const Expr *E, LValue &Result, EvalInfo &Info,
                           bool InvalidBaseOK = false);
static bool EvaluatePointer(const Expr *E, LValue &Result, EvalInfo &Info,
                            bool InvalidBaseOK = false);
static bool EvaluateMemberPointer(const Expr *E, MemberPtr &Result,
                                  EvalInfo &Info);
static bool EvaluateTemporary(const Expr *E, LValue &Result, EvalInfo &Info);
static bool EvaluateInteger(const Expr *E, APSInt &Result, EvalInfo &Info);
static bool EvaluateIntegerOrLValue(const Expr *E, APValue &Result,
                                    EvalInfo &Info);
static bool EvaluateFloat(const Expr *E, APFloat &Result, EvalInfo &Info);
static bool EvaluateComplex(const Expr *E, ComplexValue &Res, EvalInfo &Info);
static bool EvaluateAtomic(const Expr *E, const LValue *This, APValue &Result,
                           EvalInfo &Info);
static bool EvaluateAsRValue(EvalInfo &Info, const Expr *E, APValue &Result);

//===----------------------------------------------------------------------===//
// Misc utilities
//===----------------------------------------------------------------------===//

/// A helper function to create a temporary and set an LValue.
template <class KeyTy>
static APValue &createTemporary(const KeyTy *Key, bool IsLifetimeExtended,
                                LValue &LV, CallStackFrame &Frame) {
  LV.set({Key, Frame.Info.CurrentCall->Index,
          Frame.Info.CurrentCall->getTempVersion()});
  return Frame.createTemporary(Key, IsLifetimeExtended);
}

/// Negate an APSInt in place, converting it to a signed form if necessary, and
/// preserving its value (by extending by up to one bit as needed).
static void negateAsSigned(APSInt &Int) {
  if (Int.isUnsigned() || Int.isMinSignedValue()) {
    Int = Int.extend(Int.getBitWidth() + 1);
    Int.setIsSigned(true);
  }
  Int = -Int;
}

/// Produce a string describing the given constexpr call.
static void describeCall(CallStackFrame *Frame, raw_ostream &Out) {
  unsigned ArgIndex = 0;
  bool IsMemberCall = isa<CXXMethodDecl>(Frame->Callee) &&
                      !isa<CXXConstructorDecl>(Frame->Callee) &&
                      cast<CXXMethodDecl>(Frame->Callee)->isInstance();

  if (!IsMemberCall)
    Out << *Frame->Callee << '(';

  if (Frame->This && IsMemberCall) {
    APValue Val;
    Frame->This->moveInto(Val);
    Val.printPretty(Out, Frame->Info.Ctx,
                    Frame->This->Designator.MostDerivedType);
    // FIXME: Add parens around Val if needed.
    Out << "->" << *Frame->Callee << '(';
    IsMemberCall = false;
  }

  for (FunctionDecl::param_const_iterator I = Frame->Callee->param_begin(),
       E = Frame->Callee->param_end(); I != E; ++I, ++ArgIndex) {
    if (ArgIndex > (unsigned)IsMemberCall)
      Out << ", ";

    const ParmVarDecl *Param = *I;
    const APValue &Arg = Frame->Arguments[ArgIndex];
    Arg.printPretty(Out, Frame->Info.Ctx, Param->getType());

    if (ArgIndex == 0 && IsMemberCall)
      Out << "->" << *Frame->Callee << '(';
  }

  Out << ')';
}

/// Evaluate an expression to see if it had side-effects, and discard its
/// result.
/// \return \c true if the caller should keep evaluating.
static bool EvaluateIgnoredValue(EvalInfo &Info, const Expr *E) {
  APValue Scratch;
  if (!Evaluate(Scratch, Info, E))
    // We don't need the value, but we might have skipped a side effect here.
    return Info.noteSideEffect();
  return true;
}

/// Should this call expression be treated as a string literal?
static bool IsStringLiteralCall(const CallExpr *E) {
  unsigned Builtin = E->getBuiltinCallee();
  return (Builtin == Builtin::BI__builtin___CFStringMakeConstantString ||
          Builtin == Builtin::BI__builtin___NSStringMakeConstantString);
}

static bool IsGlobalLValue(APValue::LValueBase B) {
  // C++11 [expr.const]p3 An address constant expression is a prvalue core
  // constant expression of pointer type that evaluates to...

  // ... a null pointer value, or a prvalue core constant expression of type
  // std::nullptr_t.
  if (!B) return true;

  if (const ValueDecl *D = B.dyn_cast<const ValueDecl*>()) {
    // ... the address of an object with static storage duration,
    if (const VarDecl *VD = dyn_cast<VarDecl>(D))
      return VD->hasGlobalStorage();
    // ... the address of a function,
    return isa<FunctionDecl>(D);
  }

  const Expr *E = B.get<const Expr*>();
  switch (E->getStmtClass()) {
  default:
    return false;
  case Expr::CompoundLiteralExprClass: {
    const CompoundLiteralExpr *CLE = cast<CompoundLiteralExpr>(E);
    return CLE->isFileScope() && CLE->isLValue();
  }
  case Expr::MaterializeTemporaryExprClass:
    // A materialized temporary might have been lifetime-extended to static
    // storage duration.
    return cast<MaterializeTemporaryExpr>(E)->getStorageDuration() == SD_Static;
  // A string literal has static storage duration.
  case Expr::StringLiteralClass:
  case Expr::PredefinedExprClass:
  case Expr::ObjCStringLiteralClass:
  case Expr::ObjCEncodeExprClass:
  case Expr::CXXTypeidExprClass:
  case Expr::CXXUuidofExprClass:
    return true;
  case Expr::CallExprClass:
    return IsStringLiteralCall(cast<CallExpr>(E));
  // For GCC compatibility, &&label has static storage duration.
  case Expr::AddrLabelExprClass:
    return true;
  // A Block literal expression may be used as the initialization value for
  // Block variables at global or local static scope.
  case Expr::BlockExprClass:
    return !cast<BlockExpr>(E)->getBlockDecl()->hasCaptures();
  case Expr::ImplicitValueInitExprClass:
    // FIXME:
    // We can never form an lvalue with an implicit value initialization as its
    // base through expression evaluation, so these only appear in one case: the
    // implicit variable declaration we invent when checking whether a constexpr
    // constructor can produce a constant expression. We must assume that such
    // an expression might be a global lvalue.
    return true;
  }
}

static const ValueDecl *GetLValueBaseDecl(const LValue &LVal) {
  return LVal.Base.dyn_cast<const ValueDecl*>();
}

static bool IsLiteralLValue(const LValue &Value) {
  if (Value.getLValueCallIndex())
    return false;
  const Expr *E = Value.Base.dyn_cast<const Expr*>();
  return E && !isa<MaterializeTemporaryExpr>(E);
}

static bool IsWeakLValue(const LValue &Value) {
  const ValueDecl *Decl = GetLValueBaseDecl(Value);
  return Decl && Decl->isWeak();
}

static bool isZeroSized(const LValue &Value) {
  const ValueDecl *Decl = GetLValueBaseDecl(Value);
  if (Decl && isa<VarDecl>(Decl)) {
    QualType Ty = Decl->getType();
    if (Ty->isArrayType())
      return Ty->isIncompleteType() ||
             Decl->getASTContext().getTypeSize(Ty) == 0;
  }
  return false;
}

static bool HasSameBase(const LValue &A, const LValue &B) {
  if (!A.getLValueBase())
    return !B.getLValueBase();
  if (!B.getLValueBase())
    return false;

  if (A.getLValueBase().getOpaqueValue() !=
      B.getLValueBase().getOpaqueValue()) {
    const Decl *ADecl = GetLValueBaseDecl(A);
    if (!ADecl)
      return false;
    const Decl *BDecl = GetLValueBaseDecl(B);
    if (!BDecl || ADecl->getCanonicalDecl() != BDecl->getCanonicalDecl())
      return false;
  }

  return IsGlobalLValue(A.getLValueBase()) ||
         (A.getLValueCallIndex() == B.getLValueCallIndex() &&
          A.getLValueVersion() == B.getLValueVersion());
}

static void NoteLValueLocation(EvalInfo &Info, APValue::LValueBase Base) {
  assert(Base && "no location for a null lvalue");
  const ValueDecl *VD = Base.dyn_cast<const ValueDecl*>();
  if (VD)
    Info.Note(VD->getLocation(), diag::note_declared_at);
  else
    Info.Note(Base.get<const Expr*>()->getExprLoc(),
              diag::note_constexpr_temporary_here);
}

/// Check that this reference or pointer core constant expression is a valid
/// value for an address or reference constant expression. Return true if we
/// can fold this expression, whether or not it's a constant expression.
static bool CheckLValueConstantExpression(EvalInfo &Info, SourceLocation Loc,
                                          QualType Type, const LValue &LVal,
                                          Expr::ConstExprUsage Usage) {
  bool IsReferenceType = Type->isReferenceType();

  APValue::LValueBase Base = LVal.getLValueBase();
  const SubobjectDesignator &Designator = LVal.getLValueDesignator();

  // Check that the object is a global. Note that the fake 'this' object we
  // manufacture when checking potential constant expressions is conservatively
  // assumed to be global here.
  if (!IsGlobalLValue(Base)) {
    if (Info.getLangOpts().CPlusPlus11) {
      const ValueDecl *VD = Base.dyn_cast<const ValueDecl*>();
      Info.FFDiag(Loc, diag::note_constexpr_non_global, 1)
        << IsReferenceType << !Designator.Entries.empty()
        << !!VD << VD;
      NoteLValueLocation(Info, Base);
    } else {
      Info.FFDiag(Loc);
    }
    // Don't allow references to temporaries to escape.
    return false;
  }
  assert((Info.checkingPotentialConstantExpression() ||
          LVal.getLValueCallIndex() == 0) &&
         "have call index for global lvalue");

  if (const ValueDecl *VD = Base.dyn_cast<const ValueDecl*>()) {
    if (const VarDecl *Var = dyn_cast<const VarDecl>(VD)) {
      // Check if this is a thread-local variable.
      if (Var->getTLSKind())
        return false;

      // A dllimport variable never acts like a constant.
      if (Usage == Expr::EvaluateForCodeGen && Var->hasAttr<DLLImportAttr>())
        return false;
    }
    if (const auto *FD = dyn_cast<const FunctionDecl>(VD)) {
      // __declspec(dllimport) must be handled very carefully:
      // We must never initialize an expression with the thunk in C++.
      // Doing otherwise would allow the same id-expression to yield
      // different addresses for the same function in different translation
      // units.  However, this means that we must dynamically initialize the
      // expression with the contents of the import address table at runtime.
      //
      // The C language has no notion of ODR; furthermore, it has no notion of
      // dynamic initialization.  This means that we are permitted to
      // perform initialization with the address of the thunk.
      if (Info.getLangOpts().CPlusPlus && Usage == Expr::EvaluateForCodeGen &&
          FD->hasAttr<DLLImportAttr>())
        return false;
    }
  }

  // Allow address constant expressions to be past-the-end pointers. This is
  // an extension: the standard requires them to point to an object.
  if (!IsReferenceType)
    return true;

  // A reference constant expression must refer to an object.
  if (!Base) {
    // FIXME: diagnostic
    Info.CCEDiag(Loc);
    return true;
  }

  // Does this refer one past the end of some object?
  if (!Designator.Invalid && Designator.isOnePastTheEnd()) {
    const ValueDecl *VD = Base.dyn_cast<const ValueDecl*>();
    Info.FFDiag(Loc, diag::note_constexpr_past_end, 1)
      << !Designator.Entries.empty() << !!VD << VD;
    NoteLValueLocation(Info, Base);
  }

  return true;
}

/// Member pointers are constant expressions unless they point to a
/// non-virtual dllimport member function.
static bool CheckMemberPointerConstantExpression(EvalInfo &Info,
                                                 SourceLocation Loc,
                                                 QualType Type,
                                                 const APValue &Value,
                                                 Expr::ConstExprUsage Usage) {
  const ValueDecl *Member = Value.getMemberPointerDecl();
  const auto *FD = dyn_cast_or_null<CXXMethodDecl>(Member);
  if (!FD)
    return true;
  return Usage == Expr::EvaluateForMangling || FD->isVirtual() ||
         !FD->hasAttr<DLLImportAttr>();
}

/// Check that this core constant expression is of literal type, and if not,
/// produce an appropriate diagnostic.
static bool CheckLiteralType(EvalInfo &Info, const Expr *E,
                             const LValue *This = nullptr) {
  if (!E->isRValue() || E->getType()->isLiteralType(Info.Ctx))
    return true;

  // C++1y: A constant initializer for an object o [...] may also invoke
  // constexpr constructors for o and its subobjects even if those objects
  // are of non-literal class types.
  //
  // C++11 missed this detail for aggregates, so classes like this:
  //   struct foo_t { union { int i; volatile int j; } u; };
  // are not (obviously) initializable like so:
  //   __attribute__((__require_constant_initialization__))
  //   static const foo_t x = {{0}};
  // because "i" is a subobject with non-literal initialization (due to the
  // volatile member of the union). See:
  //   http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_active.html#1677
  // Therefore, we use the C++1y behavior.
  if (This && Info.EvaluatingDecl == This->getLValueBase())
    return true;

  // Prvalue constant expressions must be of literal types.
  if (Info.getLangOpts().CPlusPlus11)
    Info.FFDiag(E, diag::note_constexpr_nonliteral)
      << E->getType();
  else
    Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
  return false;
}

/// Check that this core constant expression value is a valid value for a
/// constant expression. If not, report an appropriate diagnostic. Does not
/// check that the expression is of literal type.
static bool
CheckConstantExpression(EvalInfo &Info, SourceLocation DiagLoc, QualType Type,
                        const APValue &Value,
                        Expr::ConstExprUsage Usage = Expr::EvaluateForCodeGen) {
  if (Value.isUninit()) {
    Info.FFDiag(DiagLoc, diag::note_constexpr_uninitialized)
      << true << Type;
    return false;
  }

  // We allow _Atomic(T) to be initialized from anything that T can be
  // initialized from.
  if (const AtomicType *AT = Type->getAs<AtomicType>())
    Type = AT->getValueType();

  // Core issue 1454: For a literal constant expression of array or class type,
  // each subobject of its value shall have been initialized by a constant
  // expression.
  if (Value.isArray()) {
    QualType EltTy = Type->castAsArrayTypeUnsafe()->getElementType();
    for (unsigned I = 0, N = Value.getArrayInitializedElts(); I != N; ++I) {
      if (!CheckConstantExpression(Info, DiagLoc, EltTy,
                                   Value.getArrayInitializedElt(I), Usage))
        return false;
    }
    if (!Value.hasArrayFiller())
      return true;
    return CheckConstantExpression(Info, DiagLoc, EltTy, Value.getArrayFiller(),
                                   Usage);
  }
  if (Value.isUnion() && Value.getUnionField()) {
    return CheckConstantExpression(Info, DiagLoc,
                                   Value.getUnionField()->getType(),
                                   Value.getUnionValue(), Usage);
  }
  if (Value.isStruct()) {
    RecordDecl *RD = Type->castAs<RecordType>()->getDecl();
    if (const CXXRecordDecl *CD = dyn_cast<CXXRecordDecl>(RD)) {
      unsigned BaseIndex = 0;
      for (const CXXBaseSpecifier &BS : CD->bases()) {
        if (!CheckConstantExpression(Info, DiagLoc, BS.getType(),
                                     Value.getStructBase(BaseIndex), Usage))
          return false;
        ++BaseIndex;
      }
    }
    for (const auto *I : RD->fields()) {
      if (I->isUnnamedBitfield())
        continue;

      if (!CheckConstantExpression(Info, DiagLoc, I->getType(),
                                   Value.getStructField(I->getFieldIndex()),
                                   Usage))
        return false;
    }
  }

  if (Value.isLValue()) {
    LValue LVal;
    LVal.setFrom(Info.Ctx, Value);
    return CheckLValueConstantExpression(Info, DiagLoc, Type, LVal, Usage);
  }

  if (Value.isMemberPointer())
    return CheckMemberPointerConstantExpression(Info, DiagLoc, Type, Value, Usage);

  // Everything else is fine.
  return true;
}

static bool EvalPointerValueAsBool(const APValue &Value, bool &Result) {
  // A null base expression indicates a null pointer.  These are always
  // evaluatable, and they are false unless the offset is zero.
  if (!Value.getLValueBase()) {
    Result = !Value.getLValueOffset().isZero();
    return true;
  }

  // We have a non-null base.  These are generally known to be true, but if it's
  // a weak declaration it can be null at runtime.
  Result = true;
  const ValueDecl *Decl = Value.getLValueBase().dyn_cast<const ValueDecl*>();
  return !Decl || !Decl->isWeak();
}

static bool HandleConversionToBool(const APValue &Val, bool &Result) {
  switch (Val.getKind()) {
  case APValue::Uninitialized:
    return false;
  case APValue::Int:
    Result = Val.getInt().getBoolValue();
    return true;
  case APValue::Float:
    Result = !Val.getFloat().isZero();
    return true;
  case APValue::ComplexInt:
    Result = Val.getComplexIntReal().getBoolValue() ||
             Val.getComplexIntImag().getBoolValue();
    return true;
  case APValue::ComplexFloat:
    Result = !Val.getComplexFloatReal().isZero() ||
             !Val.getComplexFloatImag().isZero();
    return true;
  case APValue::LValue:
    return EvalPointerValueAsBool(Val, Result);
  case APValue::MemberPointer:
    Result = Val.getMemberPointerDecl();
    return true;
  case APValue::Vector:
  case APValue::Array:
  case APValue::Struct:
  case APValue::Union:
  case APValue::AddrLabelDiff:
    return false;
  }

  llvm_unreachable("unknown APValue kind");
}

static bool EvaluateAsBooleanCondition(const Expr *E, bool &Result,
                                       EvalInfo &Info) {
  assert(E->isRValue() && "missing lvalue-to-rvalue conv in bool condition");
  APValue Val;
  if (!Evaluate(Val, Info, E))
    return false;
  return HandleConversionToBool(Val, Result);
}

template<typename T>
static bool HandleOverflow(EvalInfo &Info, const Expr *E,
                           const T &SrcValue, QualType DestType) {
  Info.CCEDiag(E, diag::note_constexpr_overflow)
    << SrcValue << DestType;
  return Info.noteUndefinedBehavior();
}

static bool HandleFloatToIntCast(EvalInfo &Info, const Expr *E,
                                 QualType SrcType, const APFloat &Value,
                                 QualType DestType, APSInt &Result) {
  unsigned DestWidth = Info.Ctx.getIntWidth(DestType);
  // Determine whether we are converting to unsigned or signed.
  bool DestSigned = DestType->isSignedIntegerOrEnumerationType();

  Result = APSInt(DestWidth, !DestSigned);
  bool ignored;
  if (Value.convertToInteger(Result, llvm::APFloat::rmTowardZero, &ignored)
      & APFloat::opInvalidOp)
    return HandleOverflow(Info, E, Value, DestType);
  return true;
}

static bool HandleFloatToFloatCast(EvalInfo &Info, const Expr *E,
                                   QualType SrcType, QualType DestType,
                                   APFloat &Result) {
  APFloat Value = Result;
  bool ignored;
  if (Result.convert(Info.Ctx.getFloatTypeSemantics(DestType),
                     APFloat::rmNearestTiesToEven, &ignored)
      & APFloat::opOverflow)
    return HandleOverflow(Info, E, Value, DestType);
  return true;
}

static APSInt HandleIntToIntCast(EvalInfo &Info, const Expr *E,
                                 QualType DestType, QualType SrcType,
                                 const APSInt &Value) {
  unsigned DestWidth = Info.Ctx.getIntWidth(DestType);
  // Figure out if this is a truncate, extend or noop cast.
  // If the input is signed, do a sign extend, noop, or truncate.
  APSInt Result = Value.extOrTrunc(DestWidth);
  Result.setIsUnsigned(DestType->isUnsignedIntegerOrEnumerationType());
  if (DestType->isBooleanType())
    Result = Value.getBoolValue();
  return Result;
}

static bool HandleIntToFloatCast(EvalInfo &Info, const Expr *E,
                                 QualType SrcType, const APSInt &Value,
                                 QualType DestType, APFloat &Result) {
  Result = APFloat(Info.Ctx.getFloatTypeSemantics(DestType), 1);
  if (Result.convertFromAPInt(Value, Value.isSigned(),
                              APFloat::rmNearestTiesToEven)
      & APFloat::opOverflow)
    return HandleOverflow(Info, E, Value, DestType);
  return true;
}

static bool truncateBitfieldValue(EvalInfo &Info, const Expr *E,
                                  APValue &Value, const FieldDecl *FD) {
  assert(FD->isBitField() && "truncateBitfieldValue on non-bitfield");

  if (!Value.isInt()) {
    // Trying to store a pointer-cast-to-integer into a bitfield.
    // FIXME: In this case, we should provide the diagnostic for casting
    // a pointer to an integer.
    assert(Value.isLValue() && "integral value neither int nor lvalue?");
    Info.FFDiag(E);
    return false;
  }

  APSInt &Int = Value.getInt();
  unsigned OldBitWidth = Int.getBitWidth();
  unsigned NewBitWidth = FD->getBitWidthValue(Info.Ctx);
  if (NewBitWidth < OldBitWidth)
    Int = Int.trunc(NewBitWidth).extend(OldBitWidth);
  return true;
}

static bool EvalAndBitcastToAPInt(EvalInfo &Info, const Expr *E,
                                  llvm::APInt &Res) {
  APValue SVal;
  if (!Evaluate(SVal, Info, E))
    return false;
  if (SVal.isInt()) {
    Res = SVal.getInt();
    return true;
  }
  if (SVal.isFloat()) {
    Res = SVal.getFloat().bitcastToAPInt();
    return true;
  }
  if (SVal.isVector()) {
    QualType VecTy = E->getType();
    unsigned VecSize = Info.Ctx.getTypeSize(VecTy);
    QualType EltTy = VecTy->castAs<VectorType>()->getElementType();
    unsigned EltSize = Info.Ctx.getTypeSize(EltTy);
    bool BigEndian = Info.Ctx.getTargetInfo().isBigEndian();
    Res = llvm::APInt::getNullValue(VecSize);
    for (unsigned i = 0; i < SVal.getVectorLength(); i++) {
      APValue &Elt = SVal.getVectorElt(i);
      llvm::APInt EltAsInt;
      if (Elt.isInt()) {
        EltAsInt = Elt.getInt();
      } else if (Elt.isFloat()) {
        EltAsInt = Elt.getFloat().bitcastToAPInt();
      } else {
        // Don't try to handle vectors of anything other than int or float
        // (not sure if it's possible to hit this case).
        Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
        return false;
      }
      unsigned BaseEltSize = EltAsInt.getBitWidth();
      if (BigEndian)
        Res |= EltAsInt.zextOrTrunc(VecSize).rotr(i*EltSize+BaseEltSize);
      else
        Res |= EltAsInt.zextOrTrunc(VecSize).rotl(i*EltSize);
    }
    return true;
  }
  // Give up if the input isn't an int, float, or vector.  For example, we
  // reject "(v4i16)(intptr_t)&a".
  Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
  return false;
}

/// Perform the given integer operation, which is known to need at most BitWidth
/// bits, and check for overflow in the original type (if that type was not an
/// unsigned type).
template<typename Operation>
static bool CheckedIntArithmetic(EvalInfo &Info, const Expr *E,
                                 const APSInt &LHS, const APSInt &RHS,
                                 unsigned BitWidth, Operation Op,
                                 APSInt &Result) {
  if (LHS.isUnsigned()) {
    Result = Op(LHS, RHS);
    return true;
  }

  APSInt Value(Op(LHS.extend(BitWidth), RHS.extend(BitWidth)), false);
  Result = Value.trunc(LHS.getBitWidth());
  if (Result.extend(BitWidth) != Value) {
    if (Info.checkingForOverflow())
      Info.Ctx.getDiagnostics().Report(E->getExprLoc(),
                                       diag::warn_integer_constant_overflow)
          << Result.toString(10) << E->getType();
    else
      return HandleOverflow(Info, E, Value, E->getType());
  }
  return true;
}

/// Perform the given binary integer operation.
static bool handleIntIntBinOp(EvalInfo &Info, const Expr *E, const APSInt &LHS,
                              BinaryOperatorKind Opcode, APSInt RHS,
                              APSInt &Result) {
  switch (Opcode) {
  default:
    Info.FFDiag(E);
    return false;
  case BO_Mul:
    return CheckedIntArithmetic(Info, E, LHS, RHS, LHS.getBitWidth() * 2,
                                std::multiplies<APSInt>(), Result);
  case BO_Add:
    return CheckedIntArithmetic(Info, E, LHS, RHS, LHS.getBitWidth() + 1,
                                std::plus<APSInt>(), Result);
  case BO_Sub:
    return CheckedIntArithmetic(Info, E, LHS, RHS, LHS.getBitWidth() + 1,
                                std::minus<APSInt>(), Result);
  case BO_And: Result = LHS & RHS; return true;
  case BO_Xor: Result = LHS ^ RHS; return true;
  case BO_Or:  Result = LHS | RHS; return true;
  case BO_Div:
  case BO_Rem:
    if (RHS == 0) {
      Info.FFDiag(E, diag::note_expr_divide_by_zero);
      return false;
    }
    Result = (Opcode == BO_Rem ? LHS % RHS : LHS / RHS);
    // Check for overflow case: INT_MIN / -1 or INT_MIN % -1. APSInt supports
    // this operation and gives the two's complement result.
    if (RHS.isNegative() && RHS.isAllOnesValue() &&
        LHS.isSigned() && LHS.isMinSignedValue())
      return HandleOverflow(Info, E, -LHS.extend(LHS.getBitWidth() + 1),
                            E->getType());
    return true;
  case BO_Shl: {
    if (Info.getLangOpts().OpenCL)
      // OpenCL 6.3j: shift values are effectively % word size of LHS.
      RHS &= APSInt(llvm::APInt(RHS.getBitWidth(),
                    static_cast<uint64_t>(LHS.getBitWidth() - 1)),
                    RHS.isUnsigned());
    else if (RHS.isSigned() && RHS.isNegative()) {
      // During constant-folding, a negative shift is an opposite shift. Such
      // a shift is not a constant expression.
      Info.CCEDiag(E, diag::note_constexpr_negative_shift) << RHS;
      RHS = -RHS;
      goto shift_right;
    }
  shift_left:
    // C++11 [expr.shift]p1: Shift width must be less than the bit width of
    // the shifted type.
    unsigned SA = (unsigned) RHS.getLimitedValue(LHS.getBitWidth()-1);
    if (SA != RHS) {
      Info.CCEDiag(E, diag::note_constexpr_large_shift)
        << RHS << E->getType() << LHS.getBitWidth();
    } else if (LHS.isSigned()) {
      // C++11 [expr.shift]p2: A signed left shift must have a non-negative
      // operand, and must not overflow the corresponding unsigned type.
      if (LHS.isNegative())
        Info.CCEDiag(E, diag::note_constexpr_lshift_of_negative) << LHS;
      else if (LHS.countLeadingZeros() < SA)
        Info.CCEDiag(E, diag::note_constexpr_lshift_discards);
    }
    Result = LHS << SA;
    return true;
  }
  case BO_Shr: {
    if (Info.getLangOpts().OpenCL)
      // OpenCL 6.3j: shift values are effectively % word size of LHS.
      RHS &= APSInt(llvm::APInt(RHS.getBitWidth(),
                    static_cast<uint64_t>(LHS.getBitWidth() - 1)),
                    RHS.isUnsigned());
    else if (RHS.isSigned() && RHS.isNegative()) {
      // During constant-folding, a negative shift is an opposite shift. Such a
      // shift is not a constant expression.
      Info.CCEDiag(E, diag::note_constexpr_negative_shift) << RHS;
      RHS = -RHS;
      goto shift_left;
    }
  shift_right:
    // C++11 [expr.shift]p1: Shift width must be less than the bit width of the
    // shifted type.
    unsigned SA = (unsigned) RHS.getLimitedValue(LHS.getBitWidth()-1);
    if (SA != RHS)
      Info.CCEDiag(E, diag::note_constexpr_large_shift)
        << RHS << E->getType() << LHS.getBitWidth();
    Result = LHS >> SA;
    return true;
  }

  case BO_LT: Result = LHS < RHS; return true;
  case BO_GT: Result = LHS > RHS; return true;
  case BO_LE: Result = LHS <= RHS; return true;
  case BO_GE: Result = LHS >= RHS; return true;
  case BO_EQ: Result = LHS == RHS; return true;
  case BO_NE: Result = LHS != RHS; return true;
  case BO_Cmp:
    llvm_unreachable("BO_Cmp should be handled elsewhere");
  }
}

/// Perform the given binary floating-point operation, in-place, on LHS.
static bool handleFloatFloatBinOp(EvalInfo &Info, const Expr *E,
                                  APFloat &LHS, BinaryOperatorKind Opcode,
                                  const APFloat &RHS) {
  switch (Opcode) {
  default:
    Info.FFDiag(E);
    return false;
  case BO_Mul:
    LHS.multiply(RHS, APFloat::rmNearestTiesToEven);
    break;
  case BO_Add:
    LHS.add(RHS, APFloat::rmNearestTiesToEven);
    break;
  case BO_Sub:
    LHS.subtract(RHS, APFloat::rmNearestTiesToEven);
    break;
  case BO_Div:
    LHS.divide(RHS, APFloat::rmNearestTiesToEven);
    break;
  }

  if (LHS.isInfinity() || LHS.isNaN()) {
    Info.CCEDiag(E, diag::note_constexpr_float_arithmetic) << LHS.isNaN();
    return Info.noteUndefinedBehavior();
  }
  return true;
}

/// Cast an lvalue referring to a base subobject to a derived class, by
/// truncating the lvalue's path to the given length.
static bool CastToDerivedClass(EvalInfo &Info, const Expr *E, LValue &Result,
                               const RecordDecl *TruncatedType,
                               unsigned TruncatedElements) {
  SubobjectDesignator &D = Result.Designator;

  // Check we actually point to a derived class object.
  if (TruncatedElements == D.Entries.size())
    return true;
  assert(TruncatedElements >= D.MostDerivedPathLength &&
         "not casting to a derived class");
  if (!Result.checkSubobject(Info, E, CSK_Derived))
    return false;

  // Truncate the path to the subobject, and remove any derived-to-base offsets.
  const RecordDecl *RD = TruncatedType;
  for (unsigned I = TruncatedElements, N = D.Entries.size(); I != N; ++I) {
    if (RD->isInvalidDecl()) return false;
    const ASTRecordLayout &Layout = Info.Ctx.getASTRecordLayout(RD);
    const CXXRecordDecl *Base = getAsBaseClass(D.Entries[I]);
    if (isVirtualBaseClass(D.Entries[I]))
      Result.Offset -= Layout.getVBaseClassOffset(Base);
    else
      Result.Offset -= Layout.getBaseClassOffset(Base);
    RD = Base;
  }
  D.Entries.resize(TruncatedElements);
  return true;
}

static bool HandleLValueDirectBase(EvalInfo &Info, const Expr *E, LValue &Obj,
                                   const CXXRecordDecl *Derived,
                                   const CXXRecordDecl *Base,
                                   const ASTRecordLayout *RL = nullptr) {
  if (!RL) {
    if (Derived->isInvalidDecl()) return false;
    RL = &Info.Ctx.getASTRecordLayout(Derived);
  }

  Obj.getLValueOffset() += RL->getBaseClassOffset(Base);
  Obj.addDecl(Info, E, Base, /*Virtual*/ false);
  return true;
}

static bool HandleLValueBase(EvalInfo &Info, const Expr *E, LValue &Obj,
                             const CXXRecordDecl *DerivedDecl,
                             const CXXBaseSpecifier *Base) {
  const CXXRecordDecl *BaseDecl = Base->getType()->getAsCXXRecordDecl();

  if (!Base->isVirtual())
    return HandleLValueDirectBase(Info, E, Obj, DerivedDecl, BaseDecl);

  SubobjectDesignator &D = Obj.Designator;
  if (D.Invalid)
    return false;

  // Extract most-derived object and corresponding type.
  DerivedDecl = D.MostDerivedType->getAsCXXRecordDecl();
  if (!CastToDerivedClass(Info, E, Obj, DerivedDecl, D.MostDerivedPathLength))
    return false;

  // Find the virtual base class.
  if (DerivedDecl->isInvalidDecl()) return false;
  const ASTRecordLayout &Layout = Info.Ctx.getASTRecordLayout(DerivedDecl);
  Obj.getLValueOffset() += Layout.getVBaseClassOffset(BaseDecl);
  Obj.addDecl(Info, E, BaseDecl, /*Virtual*/ true);
  return true;
}

static bool HandleLValueBasePath(EvalInfo &Info, const CastExpr *E,
                                 QualType Type, LValue &Result) {
  for (CastExpr::path_const_iterator PathI = E->path_begin(),
                                     PathE = E->path_end();
       PathI != PathE; ++PathI) {
    if (!HandleLValueBase(Info, E, Result, Type->getAsCXXRecordDecl(),
                          *PathI))
      return false;
    Type = (*PathI)->getType();
  }
  return true;
}

/// Update LVal to refer to the given field, which must be a member of the type
/// currently described by LVal.
static bool HandleLValueMember(EvalInfo &Info, const Expr *E, LValue &LVal,
                               const FieldDecl *FD,
                               const ASTRecordLayout *RL = nullptr) {
  if (!RL) {
    if (FD->getParent()->isInvalidDecl()) return false;
    RL = &Info.Ctx.getASTRecordLayout(FD->getParent());
  }

  unsigned I = FD->getFieldIndex();
  LVal.adjustOffset(Info.Ctx.toCharUnitsFromBits(RL->getFieldOffset(I)));
  LVal.addDecl(Info, E, FD);
  return true;
}

/// Update LVal to refer to the given indirect field.
static bool HandleLValueIndirectMember(EvalInfo &Info, const Expr *E,
                                       LValue &LVal,
                                       const IndirectFieldDecl *IFD) {
  for (const auto *C : IFD->chain())
    if (!HandleLValueMember(Info, E, LVal, cast<FieldDecl>(C)))
      return false;
  return true;
}

/// Get the size of the given type in char units.
static bool HandleSizeof(EvalInfo &Info, SourceLocation Loc,
                         QualType Type, CharUnits &Size) {
  // sizeof(void), __alignof__(void), sizeof(function) = 1 as a gcc
  // extension.
  if (Type->isVoidType() || Type->isFunctionType()) {
    Size = CharUnits::One();
    return true;
  }

  if (Type->isDependentType()) {
    Info.FFDiag(Loc);
    return false;
  }

  if (!Type->isConstantSizeType()) {
    // sizeof(vla) is not a constantexpr: C99 6.5.3.4p2.
    // FIXME: Better diagnostic.
    Info.FFDiag(Loc);
    return false;
  }

  Size = Info.Ctx.getTypeSizeInChars(Type);
  return true;
}

/// Update a pointer value to model pointer arithmetic.
/// \param Info - Information about the ongoing evaluation.
/// \param E - The expression being evaluated, for diagnostic purposes.
/// \param LVal - The pointer value to be updated.
/// \param EltTy - The pointee type represented by LVal.
/// \param Adjustment - The adjustment, in objects of type EltTy, to add.
static bool HandleLValueArrayAdjustment(EvalInfo &Info, const Expr *E,
                                        LValue &LVal, QualType EltTy,
                                        APSInt Adjustment) {
  CharUnits SizeOfPointee;
  if (!HandleSizeof(Info, E->getExprLoc(), EltTy, SizeOfPointee))
    return false;

  LVal.adjustOffsetAndIndex(Info, E, Adjustment, SizeOfPointee);
  return true;
}

static bool HandleLValueArrayAdjustment(EvalInfo &Info, const Expr *E,
                                        LValue &LVal, QualType EltTy,
                                        int64_t Adjustment) {
  return HandleLValueArrayAdjustment(Info, E, LVal, EltTy,
                                     APSInt::get(Adjustment));
}

/// Update an lvalue to refer to a component of a complex number.
/// \param Info - Information about the ongoing evaluation.
/// \param LVal - The lvalue to be updated.
/// \param EltTy - The complex number's component type.
/// \param Imag - False for the real component, true for the imaginary.
static bool HandleLValueComplexElement(EvalInfo &Info, const Expr *E,
                                       LValue &LVal, QualType EltTy,
                                       bool Imag) {
  if (Imag) {
    CharUnits SizeOfComponent;
    if (!HandleSizeof(Info, E->getExprLoc(), EltTy, SizeOfComponent))
      return false;
    LVal.Offset += SizeOfComponent;
  }
  LVal.addComplex(Info, E, EltTy, Imag);
  return true;
}

static bool handleLValueToRValueConversion(EvalInfo &Info, const Expr *Conv,
                                           QualType Type, const LValue &LVal,
                                           APValue &RVal);

/// Try to evaluate the initializer for a variable declaration.
///
/// \param Info   Information about the ongoing evaluation.
/// \param E      An expression to be used when printing diagnostics.
/// \param VD     The variable whose initializer should be obtained.
/// \param Frame  The frame in which the variable was created. Must be null
///               if this variable is not local to the evaluation.
/// \param Result Filled in with a pointer to the value of the variable.
static bool evaluateVarDeclInit(EvalInfo &Info, const Expr *E,
                                const VarDecl *VD, CallStackFrame *Frame,
                                APValue *&Result, const LValue *LVal) {

  // If this is a parameter to an active constexpr function call, perform
  // argument substitution.
  if (const ParmVarDecl *PVD = dyn_cast<ParmVarDecl>(VD)) {
    // Assume arguments of a potential constant expression are unknown
    // constant expressions.
    if (Info.checkingPotentialConstantExpression())
      return false;
    if (!Frame || !Frame->Arguments) {
      Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
      return false;
    }
    Result = &Frame->Arguments[PVD->getFunctionScopeIndex()];
    return true;
  }

  // If this is a local variable, dig out its value.
  if (Frame) {
    Result = LVal ? Frame->getTemporary(VD, LVal->getLValueVersion())
                  : Frame->getCurrentTemporary(VD);
    if (!Result) {
      // Assume variables referenced within a lambda's call operator that were
      // not declared within the call operator are captures and during checking
      // of a potential constant expression, assume they are unknown constant
      // expressions.
      assert(isLambdaCallOperator(Frame->Callee) &&
             (VD->getDeclContext() != Frame->Callee || VD->isInitCapture()) &&
             "missing value for local variable");
      if (Info.checkingPotentialConstantExpression())
        return false;
      // FIXME: implement capture evaluation during constant expr evaluation.
      Info.FFDiag(E->getBeginLoc(),
                  diag::note_unimplemented_constexpr_lambda_feature_ast)
          << "captures not currently allowed";
      return false;
    }
    return true;
  }

  // Dig out the initializer, and use the declaration which it's attached to.
  const Expr *Init = VD->getAnyInitializer(VD);
  if (!Init || Init->isValueDependent()) {
    // If we're checking a potential constant expression, the variable could be
    // initialized later.
    if (!Info.checkingPotentialConstantExpression())
      Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
    return false;
  }

  // If we're currently evaluating the initializer of this declaration, use that
  // in-flight value.
  if (Info.EvaluatingDecl.dyn_cast<const ValueDecl*>() == VD) {
    Result = Info.EvaluatingDeclValue;
    return true;
  }

  // Never evaluate the initializer of a weak variable. We can't be sure that
  // this is the definition which will be used.
  if (VD->isWeak()) {
    Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
    return false;
  }

  // Check that we can fold the initializer. In C++, we will have already done
  // this in the cases where it matters for conformance.
  SmallVector<PartialDiagnosticAt, 8> Notes;
  if (!VD->evaluateValue(Notes)) {
    Info.FFDiag(E, diag::note_constexpr_var_init_non_constant,
              Notes.size() + 1) << VD;
    Info.Note(VD->getLocation(), diag::note_declared_at);
    Info.addNotes(Notes);
    return false;
  } else if (!VD->checkInitIsICE()) {
    Info.CCEDiag(E, diag::note_constexpr_var_init_non_constant,
                 Notes.size() + 1) << VD;
    Info.Note(VD->getLocation(), diag::note_declared_at);
    Info.addNotes(Notes);
  }

  Result = VD->getEvaluatedValue();
  return true;
}

static bool IsConstNonVolatile(QualType T) {
  Qualifiers Quals = T.getQualifiers();
  return Quals.hasConst() && !Quals.hasVolatile();
}

/// Get the base index of the given base class within an APValue representing
/// the given derived class.
static unsigned getBaseIndex(const CXXRecordDecl *Derived,
                             const CXXRecordDecl *Base) {
  Base = Base->getCanonicalDecl();
  unsigned Index = 0;
  for (CXXRecordDecl::base_class_const_iterator I = Derived->bases_begin(),
         E = Derived->bases_end(); I != E; ++I, ++Index) {
    if (I->getType()->getAsCXXRecordDecl()->getCanonicalDecl() == Base)
      return Index;
  }

  llvm_unreachable("base class missing from derived class's bases list");
}

/// Extract the value of a character from a string literal.
static APSInt extractStringLiteralCharacter(EvalInfo &Info, const Expr *Lit,
                                            uint64_t Index) {
  // FIXME: Support MakeStringConstant
  if (const auto *ObjCEnc = dyn_cast<ObjCEncodeExpr>(Lit)) {
    std::string Str;
    Info.Ctx.getObjCEncodingForType(ObjCEnc->getEncodedType(), Str);
    assert(Index <= Str.size() && "Index too large");
    return APSInt::getUnsigned(Str.c_str()[Index]);
  }

  if (auto PE = dyn_cast<PredefinedExpr>(Lit))
    Lit = PE->getFunctionName();
  const StringLiteral *S = cast<StringLiteral>(Lit);
  const ConstantArrayType *CAT =
      Info.Ctx.getAsConstantArrayType(S->getType());
  assert(CAT && "string literal isn't an array");
  QualType CharType = CAT->getElementType();
  assert(CharType->isIntegerType() && "unexpected character type");

  APSInt Value(S->getCharByteWidth() * Info.Ctx.getCharWidth(),
               CharType->isUnsignedIntegerType());
  if (Index < S->getLength())
    Value = S->getCodeUnit(Index);
  return Value;
}

// Expand a string literal into an array of characters.
static void expandStringLiteral(EvalInfo &Info, const Expr *Lit,
                                APValue &Result) {
  const StringLiteral *S = cast<StringLiteral>(Lit);
  const ConstantArrayType *CAT =
      Info.Ctx.getAsConstantArrayType(S->getType());
  assert(CAT && "string literal isn't an array");
  QualType CharType = CAT->getElementType();
  assert(CharType->isIntegerType() && "unexpected character type");

  unsigned Elts = CAT->getSize().getZExtValue();
  Result = APValue(APValue::UninitArray(),
                   std::min(S->getLength(), Elts), Elts);
  APSInt Value(S->getCharByteWidth() * Info.Ctx.getCharWidth(),
               CharType->isUnsignedIntegerType());
  if (Result.hasArrayFiller())
    Result.getArrayFiller() = APValue(Value);
  for (unsigned I = 0, N = Result.getArrayInitializedElts(); I != N; ++I) {
    Value = S->getCodeUnit(I);
    Result.getArrayInitializedElt(I) = APValue(Value);
  }
}

// Expand an array so that it has more than Index filled elements.
static void expandArray(APValue &Array, unsigned Index) {
  unsigned Size = Array.getArraySize();
  assert(Index < Size);

  // Always at least double the number of elements for which we store a value.
  unsigned OldElts = Array.getArrayInitializedElts();
  unsigned NewElts = std::max(Index+1, OldElts * 2);
  NewElts = std::min(Size, std::max(NewElts, 8u));

  // Copy the data across.
  APValue NewValue(APValue::UninitArray(), NewElts, Size);
  for (unsigned I = 0; I != OldElts; ++I)
    NewValue.getArrayInitializedElt(I).swap(Array.getArrayInitializedElt(I));
  for (unsigned I = OldElts; I != NewElts; ++I)
    NewValue.getArrayInitializedElt(I) = Array.getArrayFiller();
  if (NewValue.hasArrayFiller())
    NewValue.getArrayFiller() = Array.getArrayFiller();
  Array.swap(NewValue);
}

/// Determine whether a type would actually be read by an lvalue-to-rvalue
/// conversion. If it's of class type, we may assume that the copy operation
/// is trivial. Note that this is never true for a union type with fields
/// (because the copy always "reads" the active member) and always true for
/// a non-class type.
static bool isReadByLvalueToRvalueConversion(QualType T) {
  CXXRecordDecl *RD = T->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
  if (!RD || (RD->isUnion() && !RD->field_empty()))
    return true;
  if (RD->isEmpty())
    return false;

  for (auto *Field : RD->fields())
    if (isReadByLvalueToRvalueConversion(Field->getType()))
      return true;

  for (auto &BaseSpec : RD->bases())
    if (isReadByLvalueToRvalueConversion(BaseSpec.getType()))
      return true;

  return false;
}

/// Diagnose an attempt to read from any unreadable field within the specified
/// type, which might be a class type.
static bool diagnoseUnreadableFields(EvalInfo &Info, const Expr *E,
                                     QualType T) {
  CXXRecordDecl *RD = T->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
  if (!RD)
    return false;

  if (!RD->hasMutableFields())
    return false;

  for (auto *Field : RD->fields()) {
    // If we're actually going to read this field in some way, then it can't
    // be mutable. If we're in a union, then assigning to a mutable field
    // (even an empty one) can change the active member, so that's not OK.
    // FIXME: Add core issue number for the union case.
    if (Field->isMutable() &&
        (RD->isUnion() || isReadByLvalueToRvalueConversion(Field->getType()))) {
      Info.FFDiag(E, diag::note_constexpr_ltor_mutable, 1) << Field;
      Info.Note(Field->getLocation(), diag::note_declared_at);
      return true;
    }

    if (diagnoseUnreadableFields(Info, E, Field->getType()))
      return true;
  }

  for (auto &BaseSpec : RD->bases())
    if (diagnoseUnreadableFields(Info, E, BaseSpec.getType()))
      return true;

  // All mutable fields were empty, and thus not actually read.
  return false;
}

namespace {
/// A handle to a complete object (an object that is not a subobject of
/// another object).
struct CompleteObject {
  /// The value of the complete object.
  APValue *Value;
  /// The type of the complete object.
  QualType Type;
  bool LifetimeStartedInEvaluation;

  CompleteObject() : Value(nullptr) {}
  CompleteObject(APValue *Value, QualType Type,
                 bool LifetimeStartedInEvaluation)
      : Value(Value), Type(Type),
        LifetimeStartedInEvaluation(LifetimeStartedInEvaluation) {
    assert(Value && "missing value for complete object");
  }

  explicit operator bool() const { return Value; }
};
} // end anonymous namespace

/// Find the designated sub-object of an rvalue.
template<typename SubobjectHandler>
typename SubobjectHandler::result_type
findSubobject(EvalInfo &Info, const Expr *E, const CompleteObject &Obj,
              const SubobjectDesignator &Sub, SubobjectHandler &handler) {
  if (Sub.Invalid)
    // A diagnostic will have already been produced.
    return handler.failed();
  if (Sub.isOnePastTheEnd() || Sub.isMostDerivedAnUnsizedArray()) {
    if (Info.getLangOpts().CPlusPlus11)
      Info.FFDiag(E, Sub.isOnePastTheEnd()
                         ? diag::note_constexpr_access_past_end
                         : diag::note_constexpr_access_unsized_array)
          << handler.AccessKind;
    else
      Info.FFDiag(E);
    return handler.failed();
  }

  APValue *O = Obj.Value;
  QualType ObjType = Obj.Type;
  const FieldDecl *LastField = nullptr;
  const bool MayReadMutableMembers =
      Obj.LifetimeStartedInEvaluation && Info.getLangOpts().CPlusPlus14;

  // Walk the designator's path to find the subobject.
  for (unsigned I = 0, N = Sub.Entries.size(); /**/; ++I) {
    if (O->isUninit()) {
      if (!Info.checkingPotentialConstantExpression())
        Info.FFDiag(E, diag::note_constexpr_access_uninit) << handler.AccessKind;
      return handler.failed();
    }

    if (I == N) {
      // If we are reading an object of class type, there may still be more
      // things we need to check: if there are any mutable subobjects, we
      // cannot perform this read. (This only happens when performing a trivial
      // copy or assignment.)
      if (ObjType->isRecordType() && handler.AccessKind == AK_Read &&
          !MayReadMutableMembers && diagnoseUnreadableFields(Info, E, ObjType))
        return handler.failed();

      if (!handler.found(*O, ObjType))
        return false;

      // If we modified a bit-field, truncate it to the right width.
      if (handler.AccessKind != AK_Read &&
          LastField && LastField->isBitField() &&
          !truncateBitfieldValue(Info, E, *O, LastField))
        return false;

      return true;
    }

    LastField = nullptr;
    if (ObjType->isArrayType()) {
      // Next subobject is an array element.
      const ConstantArrayType *CAT = Info.Ctx.getAsConstantArrayType(ObjType);
      assert(CAT && "vla in literal type?");
      uint64_t Index = Sub.Entries[I].ArrayIndex;
      if (CAT->getSize().ule(Index)) {
        // Note, it should not be possible to form a pointer with a valid
        // designator which points more than one past the end of the array.
        if (Info.getLangOpts().CPlusPlus11)
          Info.FFDiag(E, diag::note_constexpr_access_past_end)
            << handler.AccessKind;
        else
          Info.FFDiag(E);
        return handler.failed();
      }

      ObjType = CAT->getElementType();

      // An array object is represented as either an Array APValue or as an
      // LValue which refers to a string literal.
      if (O->isLValue()) {
        assert(I == N - 1 && "extracting subobject of character?");
        assert(!O->hasLValuePath() || O->getLValuePath().empty());
        if (handler.AccessKind != AK_Read)
          expandStringLiteral(Info, O->getLValueBase().get<const Expr *>(),
                              *O);
        else
          return handler.foundString(*O, ObjType, Index);
      }

      if (O->getArrayInitializedElts() > Index)
        O = &O->getArrayInitializedElt(Index);
      else if (handler.AccessKind != AK_Read) {
        expandArray(*O, Index);
        O = &O->getArrayInitializedElt(Index);
      } else
        O = &O->getArrayFiller();
    } else if (ObjType->isAnyComplexType()) {
      // Next subobject is a complex number.
      uint64_t Index = Sub.Entries[I].ArrayIndex;
      if (Index > 1) {
        if (Info.getLangOpts().CPlusPlus11)
          Info.FFDiag(E, diag::note_constexpr_access_past_end)
            << handler.AccessKind;
        else
          Info.FFDiag(E);
        return handler.failed();
      }

      bool WasConstQualified = ObjType.isConstQualified();
      ObjType = ObjType->castAs<ComplexType>()->getElementType();
      if (WasConstQualified)
        ObjType.addConst();

      assert(I == N - 1 && "extracting subobject of scalar?");
      if (O->isComplexInt()) {
        return handler.found(Index ? O->getComplexIntImag()
                                   : O->getComplexIntReal(), ObjType);
      } else {
        assert(O->isComplexFloat());
        return handler.found(Index ? O->getComplexFloatImag()
                                   : O->getComplexFloatReal(), ObjType);
      }
    } else if (const FieldDecl *Field = getAsField(Sub.Entries[I])) {
      // In C++14 onwards, it is permitted to read a mutable member whose
      // lifetime began within the evaluation.
      // FIXME: Should we also allow this in C++11?
      if (Field->isMutable() && handler.AccessKind == AK_Read &&
          !MayReadMutableMembers) {
        Info.FFDiag(E, diag::note_constexpr_ltor_mutable, 1)
          << Field;
        Info.Note(Field->getLocation(), diag::note_declared_at);
        return handler.failed();
      }

      // Next subobject is a class, struct or union field.
      RecordDecl *RD = ObjType->castAs<RecordType>()->getDecl();
      if (RD->isUnion()) {
        const FieldDecl *UnionField = O->getUnionField();
        if (!UnionField ||
            UnionField->getCanonicalDecl() != Field->getCanonicalDecl()) {
          Info.FFDiag(E, diag::note_constexpr_access_inactive_union_member)
            << handler.AccessKind << Field << !UnionField << UnionField;
          return handler.failed();
        }
        O = &O->getUnionValue();
      } else
        O = &O->getStructField(Field->getFieldIndex());

      bool WasConstQualified = ObjType.isConstQualified();
      ObjType = Field->getType();
      if (WasConstQualified && !Field->isMutable())
        ObjType.addConst();

      if (ObjType.isVolatileQualified()) {
        if (Info.getLangOpts().CPlusPlus) {
          // FIXME: Include a description of the path to the volatile subobject.
          Info.FFDiag(E, diag::note_constexpr_access_volatile_obj, 1)
            << handler.AccessKind << 2 << Field;
          Info.Note(Field->getLocation(), diag::note_declared_at);
        } else {
          Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
        }
        return handler.failed();
      }

      LastField = Field;
    } else {
      // Next subobject is a base class.
      const CXXRecordDecl *Derived = ObjType->getAsCXXRecordDecl();
      const CXXRecordDecl *Base = getAsBaseClass(Sub.Entries[I]);
      O = &O->getStructBase(getBaseIndex(Derived, Base));

      bool WasConstQualified = ObjType.isConstQualified();
      ObjType = Info.Ctx.getRecordType(Base);
      if (WasConstQualified)
        ObjType.addConst();
    }
  }
}

namespace {
struct ExtractSubobjectHandler {
  EvalInfo &Info;
  APValue &Result;

  static const AccessKinds AccessKind = AK_Read;

  typedef bool result_type;
  bool failed() { return false; }
  bool found(APValue &Subobj, QualType SubobjType) {
    Result = Subobj;
    return true;
  }
  bool found(APSInt &Value, QualType SubobjType) {
    Result = APValue(Value);
    return true;
  }
  bool found(APFloat &Value, QualType SubobjType) {
    Result = APValue(Value);
    return true;
  }
  bool foundString(APValue &Subobj, QualType SubobjType, uint64_t Character) {
    Result = APValue(extractStringLiteralCharacter(
        Info, Subobj.getLValueBase().get<const Expr *>(), Character));
    return true;
  }
};
} // end anonymous namespace

const AccessKinds ExtractSubobjectHandler::AccessKind;

/// Extract the designated sub-object of an rvalue.
static bool extractSubobject(EvalInfo &Info, const Expr *E,
                             const CompleteObject &Obj,
                             const SubobjectDesignator &Sub,
                             APValue &Result) {
  ExtractSubobjectHandler Handler = { Info, Result };
  return findSubobject(Info, E, Obj, Sub, Handler);
}

namespace {
struct ModifySubobjectHandler {
  EvalInfo &Info;
  APValue &NewVal;
  const Expr *E;

  typedef bool result_type;
  static const AccessKinds AccessKind = AK_Assign;

  bool checkConst(QualType QT) {
    // Assigning to a const object has undefined behavior.
    if (QT.isConstQualified()) {
      Info.FFDiag(E, diag::note_constexpr_modify_const_type) << QT;
      return false;
    }
    return true;
  }

  bool failed() { return false; }
  bool found(APValue &Subobj, QualType SubobjType) {
    if (!checkConst(SubobjType))
      return false;
    // We've been given ownership of NewVal, so just swap it in.
    Subobj.swap(NewVal);
    return true;
  }
  bool found(APSInt &Value, QualType SubobjType) {
    if (!checkConst(SubobjType))
      return false;
    if (!NewVal.isInt()) {
      // Maybe trying to write a cast pointer value into a complex?
      Info.FFDiag(E);
      return false;
    }
    Value = NewVal.getInt();
    return true;
  }
  bool found(APFloat &Value, QualType SubobjType) {
    if (!checkConst(SubobjType))
      return false;
    Value = NewVal.getFloat();
    return true;
  }
  bool foundString(APValue &Subobj, QualType SubobjType, uint64_t Character) {
    llvm_unreachable("shouldn't encounter string elements with ExpandArrays");
  }
};
} // end anonymous namespace

const AccessKinds ModifySubobjectHandler::AccessKind;

/// Update the designated sub-object of an rvalue to the given value.
static bool modifySubobject(EvalInfo &Info, const Expr *E,
                            const CompleteObject &Obj,
                            const SubobjectDesignator &Sub,
                            APValue &NewVal) {
  ModifySubobjectHandler Handler = { Info, NewVal, E };
  return findSubobject(Info, E, Obj, Sub, Handler);
}

/// Find the position where two subobject designators diverge, or equivalently
/// the length of the common initial subsequence.
static unsigned FindDesignatorMismatch(QualType ObjType,
                                       const SubobjectDesignator &A,
                                       const SubobjectDesignator &B,
                                       bool &WasArrayIndex) {
  unsigned I = 0, N = std::min(A.Entries.size(), B.Entries.size());
  for (/**/; I != N; ++I) {
    if (!ObjType.isNull() &&
        (ObjType->isArrayType() || ObjType->isAnyComplexType())) {
      // Next subobject is an array element.
      if (A.Entries[I].ArrayIndex != B.Entries[I].ArrayIndex) {
        WasArrayIndex = true;
        return I;
      }
      if (ObjType->isAnyComplexType())
        ObjType = ObjType->castAs<ComplexType>()->getElementType();
      else
        ObjType = ObjType->castAsArrayTypeUnsafe()->getElementType();
    } else {
      if (A.Entries[I].BaseOrMember != B.Entries[I].BaseOrMember) {
        WasArrayIndex = false;
        return I;
      }
      if (const FieldDecl *FD = getAsField(A.Entries[I]))
        // Next subobject is a field.
        ObjType = FD->getType();
      else
        // Next subobject is a base class.
        ObjType = QualType();
    }
  }
  WasArrayIndex = false;
  return I;
}

/// Determine whether the given subobject designators refer to elements of the
/// same array object.
static bool AreElementsOfSameArray(QualType ObjType,
                                   const SubobjectDesignator &A,
                                   const SubobjectDesignator &B) {
  if (A.Entries.size() != B.Entries.size())
    return false;

  bool IsArray = A.MostDerivedIsArrayElement;
  if (IsArray && A.MostDerivedPathLength != A.Entries.size())
    // A is a subobject of the array element.
    return false;

  // If A (and B) designates an array element, the last entry will be the array
  // index. That doesn't have to match. Otherwise, we're in the 'implicit array
  // of length 1' case, and the entire path must match.
  bool WasArrayIndex;
  unsigned CommonLength = FindDesignatorMismatch(ObjType, A, B, WasArrayIndex);
  return CommonLength >= A.Entries.size() - IsArray;
}

/// Find the complete object to which an LValue refers.
static CompleteObject findCompleteObject(EvalInfo &Info, const Expr *E,
                                         AccessKinds AK, const LValue &LVal,
                                         QualType LValType) {
  if (!LVal.Base) {
    Info.FFDiag(E, diag::note_constexpr_access_null) << AK;
    return CompleteObject();
  }

  CallStackFrame *Frame = nullptr;
  if (LVal.getLValueCallIndex()) {
    Frame = Info.getCallFrame(LVal.getLValueCallIndex());
    if (!Frame) {
      Info.FFDiag(E, diag::note_constexpr_lifetime_ended, 1)
        << AK << LVal.Base.is<const ValueDecl*>();
      NoteLValueLocation(Info, LVal.Base);
      return CompleteObject();
    }
  }

  // C++11 DR1311: An lvalue-to-rvalue conversion on a volatile-qualified type
  // is not a constant expression (even if the object is non-volatile). We also
  // apply this rule to C++98, in order to conform to the expected 'volatile'
  // semantics.
  if (LValType.isVolatileQualified()) {
    if (Info.getLangOpts().CPlusPlus)
      Info.FFDiag(E, diag::note_constexpr_access_volatile_type)
        << AK << LValType;
    else
      Info.FFDiag(E);
    return CompleteObject();
  }

  // Compute value storage location and type of base object.
  APValue *BaseVal = nullptr;
  QualType BaseType = getType(LVal.Base);
  bool LifetimeStartedInEvaluation = Frame;

  if (const ValueDecl *D = LVal.Base.dyn_cast<const ValueDecl*>()) {
    // In C++98, const, non-volatile integers initialized with ICEs are ICEs.
    // In C++11, constexpr, non-volatile variables initialized with constant
    // expressions are constant expressions too. Inside constexpr functions,
    // parameters are constant expressions even if they're non-const.
    // In C++1y, objects local to a constant expression (those with a Frame) are
    // both readable and writable inside constant expressions.
    // In C, such things can also be folded, although they are not ICEs.
    const VarDecl *VD = dyn_cast<VarDecl>(D);
    if (VD) {
      if (const VarDecl *VDef = VD->getDefinition(Info.Ctx))
        VD = VDef;
    }
    if (!VD || VD->isInvalidDecl()) {
      Info.FFDiag(E);
      return CompleteObject();
    }

    // Accesses of volatile-qualified objects are not allowed.
    if (BaseType.isVolatileQualified()) {
      if (Info.getLangOpts().CPlusPlus) {
        Info.FFDiag(E, diag::note_constexpr_access_volatile_obj, 1)
          << AK << 1 << VD;
        Info.Note(VD->getLocation(), diag::note_declared_at);
      } else {
        Info.FFDiag(E);
      }
      return CompleteObject();
    }

    // Unless we're looking at a local variable or argument in a constexpr call,
    // the variable we're reading must be const.
    if (!Frame) {
      if (Info.getLangOpts().CPlusPlus14 &&
          VD == Info.EvaluatingDecl.dyn_cast<const ValueDecl *>()) {
        // OK, we can read and modify an object if we're in the process of
        // evaluating its initializer, because its lifetime began in this
        // evaluation.
      } else if (AK != AK_Read) {
        // All the remaining cases only permit reading.
        Info.FFDiag(E, diag::note_constexpr_modify_global);
        return CompleteObject();
      } else if (VD->isConstexpr()) {
        // OK, we can read this variable.
      } else if (BaseType->isIntegralOrEnumerationType()) {
        // In OpenCL if a variable is in constant address space it is a const value.
        if (!(BaseType.isConstQualified() ||
              (Info.getLangOpts().OpenCL &&
               BaseType.getAddressSpace() == LangAS::opencl_constant))) {
          if (Info.getLangOpts().CPlusPlus) {
            Info.FFDiag(E, diag::note_constexpr_ltor_non_const_int, 1) << VD;
            Info.Note(VD->getLocation(), diag::note_declared_at);
          } else {
            Info.FFDiag(E);
          }
          return CompleteObject();
        }
      } else if (BaseType->isFloatingType() && BaseType.isConstQualified()) {
        // We support folding of const floating-point types, in order to make
        // static const data members of such types (supported as an extension)
        // more useful.
        if (Info.getLangOpts().CPlusPlus11) {
          Info.CCEDiag(E, diag::note_constexpr_ltor_non_constexpr, 1) << VD;
          Info.Note(VD->getLocation(), diag::note_declared_at);
        } else {
          Info.CCEDiag(E);
        }
      } else if (BaseType.isConstQualified() && VD->hasDefinition(Info.Ctx)) {
        Info.CCEDiag(E, diag::note_constexpr_ltor_non_constexpr) << VD;
        // Keep evaluating to see what we can do.
      } else {
        // FIXME: Allow folding of values of any literal type in all languages.
        if (Info.checkingPotentialConstantExpression() &&
            VD->getType().isConstQualified() && !VD->hasDefinition(Info.Ctx)) {
          // The definition of this variable could be constexpr. We can't
          // access it right now, but may be able to in future.
        } else if (Info.getLangOpts().CPlusPlus11) {
          Info.FFDiag(E, diag::note_constexpr_ltor_non_constexpr, 1) << VD;
          Info.Note(VD->getLocation(), diag::note_declared_at);
        } else {
          Info.FFDiag(E);
        }
        return CompleteObject();
      }
    }

    if (!evaluateVarDeclInit(Info, E, VD, Frame, BaseVal, &LVal))
      return CompleteObject();
  } else {
    const Expr *Base = LVal.Base.dyn_cast<const Expr*>();

    if (!Frame) {
      if (const MaterializeTemporaryExpr *MTE =
              dyn_cast<MaterializeTemporaryExpr>(Base)) {
        assert(MTE->getStorageDuration() == SD_Static &&
               "should have a frame for a non-global materialized temporary");

        // Per C++1y [expr.const]p2:
        //  an lvalue-to-rvalue conversion [is not allowed unless it applies to]
        //   - a [...] glvalue of integral or enumeration type that refers to
        //     a non-volatile const object [...]
        //   [...]
        //   - a [...] glvalue of literal type that refers to a non-volatile
        //     object whose lifetime began within the evaluation of e.
        //
        // C++11 misses the 'began within the evaluation of e' check and
        // instead allows all temporaries, including things like:
        //   int &&r = 1;
        //   int x = ++r;
        //   constexpr int k = r;
        // Therefore we use the C++14 rules in C++11 too.
        const ValueDecl *VD = Info.EvaluatingDecl.dyn_cast<const ValueDecl*>();
        const ValueDecl *ED = MTE->getExtendingDecl();
        if (!(BaseType.isConstQualified() &&
              BaseType->isIntegralOrEnumerationType()) &&
            !(VD && VD->getCanonicalDecl() == ED->getCanonicalDecl())) {
          Info.FFDiag(E, diag::note_constexpr_access_static_temporary, 1) << AK;
          Info.Note(MTE->getExprLoc(), diag::note_constexpr_temporary_here);
          return CompleteObject();
        }

        BaseVal = Info.Ctx.getMaterializedTemporaryValue(MTE, false);
        assert(BaseVal && "got reference to unevaluated temporary");
        LifetimeStartedInEvaluation = true;
      } else {
        Info.FFDiag(E);
        return CompleteObject();
      }
    } else {
      BaseVal = Frame->getTemporary(Base, LVal.Base.getVersion());
      assert(BaseVal && "missing value for temporary");
    }

    // Volatile temporary objects cannot be accessed in constant expressions.
    if (BaseType.isVolatileQualified()) {
      if (Info.getLangOpts().CPlusPlus) {
        Info.FFDiag(E, diag::note_constexpr_access_volatile_obj, 1)
          << AK << 0;
        Info.Note(Base->getExprLoc(), diag::note_constexpr_temporary_here);
      } else {
        Info.FFDiag(E);
      }
      return CompleteObject();
    }
  }

  // During the construction of an object, it is not yet 'const'.
  // FIXME: This doesn't do quite the right thing for const subobjects of the
  // object under construction.
  if (Info.isEvaluatingConstructor(LVal.getLValueBase(),
                                   LVal.getLValueCallIndex(),
                                   LVal.getLValueVersion())) {
    BaseType = Info.Ctx.getCanonicalType(BaseType);
    BaseType.removeLocalConst();
    LifetimeStartedInEvaluation = true;
  }

  // In C++14, we can't safely access any mutable state when we might be
  // evaluating after an unmodeled side effect.
  //
  // FIXME: Not all local state is mutable. Allow local constant subobjects
  // to be read here (but take care with 'mutable' fields).
  if ((Frame && Info.getLangOpts().CPlusPlus14 &&
       Info.EvalStatus.HasSideEffects) ||
      (AK != AK_Read && Info.IsSpeculativelyEvaluating))
    return CompleteObject();

  return CompleteObject(BaseVal, BaseType, LifetimeStartedInEvaluation);
}

/// Perform an lvalue-to-rvalue conversion on the given glvalue. This
/// can also be used for 'lvalue-to-lvalue' conversions for looking up the
/// glvalue referred to by an entity of reference type.
///
/// \param Info - Information about the ongoing evaluation.
/// \param Conv - The expression for which we are performing the conversion.
///               Used for diagnostics.
/// \param Type - The type of the glvalue (before stripping cv-qualifiers in the
///               case of a non-class type).
/// \param LVal - The glvalue on which we are attempting to perform this action.
/// \param RVal - The produced value will be placed here.
static bool handleLValueToRValueConversion(EvalInfo &Info, const Expr *Conv,
                                           QualType Type,
                                           const LValue &LVal, APValue &RVal) {
  if (LVal.Designator.Invalid)
    return false;

  // Check for special cases where there is no existing APValue to look at.
  const Expr *Base = LVal.Base.dyn_cast<const Expr*>();
  if (Base && !LVal.getLValueCallIndex() && !Type.isVolatileQualified()) {
    if (const CompoundLiteralExpr *CLE = dyn_cast<CompoundLiteralExpr>(Base)) {
      // In C99, a CompoundLiteralExpr is an lvalue, and we defer evaluating the
      // initializer until now for such expressions. Such an expression can't be
      // an ICE in C, so this only matters for fold.
      if (Type.isVolatileQualified()) {
        Info.FFDiag(Conv);
        return false;
      }
      APValue Lit;
      if (!Evaluate(Lit, Info, CLE->getInitializer()))
        return false;
      CompleteObject LitObj(&Lit, Base->getType(), false);
      return extractSubobject(Info, Conv, LitObj, LVal.Designator, RVal);
    } else if (isa<StringLiteral>(Base) || isa<PredefinedExpr>(Base)) {
      // We represent a string literal array as an lvalue pointing at the
      // corresponding expression, rather than building an array of chars.
      // FIXME: Support ObjCEncodeExpr, MakeStringConstant
      APValue Str(Base, CharUnits::Zero(), APValue::NoLValuePath(), 0);
      CompleteObject StrObj(&Str, Base->getType(), false);
      return extractSubobject(Info, Conv, StrObj, LVal.Designator, RVal);
    }
  }

  CompleteObject Obj = findCompleteObject(Info, Conv, AK_Read, LVal, Type);
  return Obj && extractSubobject(Info, Conv, Obj, LVal.Designator, RVal);
}

/// Perform an assignment of Val to LVal. Takes ownership of Val.
static bool handleAssignment(EvalInfo &Info, const Expr *E, const LValue &LVal,
                             QualType LValType, APValue &Val) {
  if (LVal.Designator.Invalid)
    return false;

  if (!Info.getLangOpts().CPlusPlus14) {
    Info.FFDiag(E);
    return false;
  }

  CompleteObject Obj = findCompleteObject(Info, E, AK_Assign, LVal, LValType);
  return Obj && modifySubobject(Info, E, Obj, LVal.Designator, Val);
}

namespace {
struct CompoundAssignSubobjectHandler {
  EvalInfo &Info;
  const Expr *E;
  QualType PromotedLHSType;
  BinaryOperatorKind Opcode;
  const APValue &RHS;

  static const AccessKinds AccessKind = AK_Assign;

  typedef bool result_type;

  bool checkConst(QualType QT) {
    // Assigning to a const object has undefined behavior.
    if (QT.isConstQualified()) {
      Info.FFDiag(E, diag::note_constexpr_modify_const_type) << QT;
      return false;
    }
    return true;
  }

  bool failed() { return false; }
  bool found(APValue &Subobj, QualType SubobjType) {
    switch (Subobj.getKind()) {
    case APValue::Int:
      return found(Subobj.getInt(), SubobjType);
    case APValue::Float:
      return found(Subobj.getFloat(), SubobjType);
    case APValue::ComplexInt:
    case APValue::ComplexFloat:
      // FIXME: Implement complex compound assignment.
      Info.FFDiag(E);
      return false;
    case APValue::LValue:
      return foundPointer(Subobj, SubobjType);
    default:
      // FIXME: can this happen?
      Info.FFDiag(E);
      return false;
    }
  }
  bool found(APSInt &Value, QualType SubobjType) {
    if (!checkConst(SubobjType))
      return false;

    if (!SubobjType->isIntegerType()) {
      // We don't support compound assignment on integer-cast-to-pointer
      // values.
      Info.FFDiag(E);
      return false;
    }

    if (RHS.isInt()) {
      APSInt LHS =
          HandleIntToIntCast(Info, E, PromotedLHSType, SubobjType, Value);
      if (!handleIntIntBinOp(Info, E, LHS, Opcode, RHS.getInt(), LHS))
        return false;
      Value = HandleIntToIntCast(Info, E, SubobjType, PromotedLHSType, LHS);
      return true;
    } else if (RHS.isFloat()) {
      APFloat FValue(0.0);
      return HandleIntToFloatCast(Info, E, SubobjType, Value, PromotedLHSType,
                                  FValue) &&
             handleFloatFloatBinOp(Info, E, FValue, Opcode, RHS.getFloat()) &&
             HandleFloatToIntCast(Info, E, PromotedLHSType, FValue, SubobjType,
                                  Value);
    }

    Info.FFDiag(E);
    return false;
  }
  bool found(APFloat &Value, QualType SubobjType) {
    return checkConst(SubobjType) &&
           HandleFloatToFloatCast(Info, E, SubobjType, PromotedLHSType,
                                  Value) &&
           handleFloatFloatBinOp(Info, E, Value, Opcode, RHS.getFloat()) &&
           HandleFloatToFloatCast(Info, E, PromotedLHSType, SubobjType, Value);
  }
  bool foundPointer(APValue &Subobj, QualType SubobjType) {
    if (!checkConst(SubobjType))
      return false;

    QualType PointeeType;
    if (const PointerType *PT = SubobjType->getAs<PointerType>())
      PointeeType = PT->getPointeeType();

    if (PointeeType.isNull() || !RHS.isInt() ||
        (Opcode != BO_Add && Opcode != BO_Sub)) {
      Info.FFDiag(E);
      return false;
    }

    APSInt Offset = RHS.getInt();
    if (Opcode == BO_Sub)
      negateAsSigned(Offset);

    LValue LVal;
    LVal.setFrom(Info.Ctx, Subobj);
    if (!HandleLValueArrayAdjustment(Info, E, LVal, PointeeType, Offset))
      return false;
    LVal.moveInto(Subobj);
    return true;
  }
  bool foundString(APValue &Subobj, QualType SubobjType, uint64_t Character) {
    llvm_unreachable("shouldn't encounter string elements here");
  }
};
} // end anonymous namespace

const AccessKinds CompoundAssignSubobjectHandler::AccessKind;

/// Perform a compound assignment of LVal <op>= RVal.
static bool handleCompoundAssignment(
    EvalInfo &Info, const Expr *E,
    const LValue &LVal, QualType LValType, QualType PromotedLValType,
    BinaryOperatorKind Opcode, const APValue &RVal) {
  if (LVal.Designator.Invalid)
    return false;

  if (!Info.getLangOpts().CPlusPlus14) {
    Info.FFDiag(E);
    return false;
  }

  CompleteObject Obj = findCompleteObject(Info, E, AK_Assign, LVal, LValType);
  CompoundAssignSubobjectHandler Handler = { Info, E, PromotedLValType, Opcode,
                                             RVal };
  return Obj && findSubobject(Info, E, Obj, LVal.Designator, Handler);
}

namespace {
struct IncDecSubobjectHandler {
  EvalInfo &Info;
  const UnaryOperator *E;
  AccessKinds AccessKind;
  APValue *Old;

  typedef bool result_type;

  bool checkConst(QualType QT) {
    // Assigning to a const object has undefined behavior.
    if (QT.isConstQualified()) {
      Info.FFDiag(E, diag::note_constexpr_modify_const_type) << QT;
      return false;
    }
    return true;
  }

  bool failed() { return false; }
  bool found(APValue &Subobj, QualType SubobjType) {
    // Stash the old value. Also clear Old, so we don't clobber it later
    // if we're post-incrementing a complex.
    if (Old) {
      *Old = Subobj;
      Old = nullptr;
    }

    switch (Subobj.getKind()) {
    case APValue::Int:
      return found(Subobj.getInt(), SubobjType);
    case APValue::Float:
      return found(Subobj.getFloat(), SubobjType);
    case APValue::ComplexInt:
      return found(Subobj.getComplexIntReal(),
                   SubobjType->castAs<ComplexType>()->getElementType()
                     .withCVRQualifiers(SubobjType.getCVRQualifiers()));
    case APValue::ComplexFloat:
      return found(Subobj.getComplexFloatReal(),
                   SubobjType->castAs<ComplexType>()->getElementType()
                     .withCVRQualifiers(SubobjType.getCVRQualifiers()));
    case APValue::LValue:
      return foundPointer(Subobj, SubobjType);
    default:
      // FIXME: can this happen?
      Info.FFDiag(E);
      return false;
    }
  }
  bool found(APSInt &Value, QualType SubobjType) {
    if (!checkConst(SubobjType))
      return false;

    if (!SubobjType->isIntegerType()) {
      // We don't support increment / decrement on integer-cast-to-pointer
      // values.
      Info.FFDiag(E);
      return false;
    }

    if (Old) *Old = APValue(Value);

    // bool arithmetic promotes to int, and the conversion back to bool
    // doesn't reduce mod 2^n, so special-case it.
    if (SubobjType->isBooleanType()) {
      if (AccessKind == AK_Increment)
        Value = 1;
      else
        Value = !Value;
      return true;
    }

    bool WasNegative = Value.isNegative();
    if (AccessKind == AK_Increment) {
      ++Value;

      if (!WasNegative && Value.isNegative() && E->canOverflow()) {
        APSInt ActualValue(Value, /*IsUnsigned*/true);
        return HandleOverflow(Info, E, ActualValue, SubobjType);
      }
    } else {
      --Value;

      if (WasNegative && !Value.isNegative() && E->canOverflow()) {
        unsigned BitWidth = Value.getBitWidth();
        APSInt ActualValue(Value.sext(BitWidth + 1), /*IsUnsigned*/false);
        ActualValue.setBit(BitWidth);
        return HandleOverflow(Info, E, ActualValue, SubobjType);
      }
    }
    return true;
  }
  bool found(APFloat &Value, QualType SubobjType) {
    if (!checkConst(SubobjType))
      return false;

    if (Old) *Old = APValue(Value);

    APFloat One(Value.getSemantics(), 1);
    if (AccessKind == AK_Increment)
      Value.add(One, APFloat::rmNearestTiesToEven);
    else
      Value.subtract(One, APFloat::rmNearestTiesToEven);
    return true;
  }
  bool foundPointer(APValue &Subobj, QualType SubobjType) {
    if (!checkConst(SubobjType))
      return false;

    QualType PointeeType;
    if (const PointerType *PT = SubobjType->getAs<PointerType>())
      PointeeType = PT->getPointeeType();
    else {
      Info.FFDiag(E);
      return false;
    }

    LValue LVal;
    LVal.setFrom(Info.Ctx, Subobj);
    if (!HandleLValueArrayAdjustment(Info, E, LVal, PointeeType,
                                     AccessKind == AK_Increment ? 1 : -1))
      return false;
    LVal.moveInto(Subobj);
    return true;
  }
  bool foundString(APValue &Subobj, QualType SubobjType, uint64_t Character) {
    llvm_unreachable("shouldn't encounter string elements here");
  }
};
} // end anonymous namespace

/// Perform an increment or decrement on LVal.
static bool handleIncDec(EvalInfo &Info, const Expr *E, const LValue &LVal,
                         QualType LValType, bool IsIncrement, APValue *Old) {
  if (LVal.Designator.Invalid)
    return false;

  if (!Info.getLangOpts().CPlusPlus14) {
    Info.FFDiag(E);
    return false;
  }

  AccessKinds AK = IsIncrement ? AK_Increment : AK_Decrement;
  CompleteObject Obj = findCompleteObject(Info, E, AK, LVal, LValType);
  IncDecSubobjectHandler Handler = {Info, cast<UnaryOperator>(E), AK, Old};
  return Obj && findSubobject(Info, E, Obj, LVal.Designator, Handler);
}

/// Build an lvalue for the object argument of a member function call.
static bool EvaluateObjectArgument(EvalInfo &Info, const Expr *Object,
                                   LValue &This) {
  if (Object->getType()->isPointerType())
    return EvaluatePointer(Object, This, Info);

  if (Object->isGLValue())
    return EvaluateLValue(Object, This, Info);

  if (Object->getType()->isLiteralType(Info.Ctx))
    return EvaluateTemporary(Object, This, Info);

  Info.FFDiag(Object, diag::note_constexpr_nonliteral) << Object->getType();
  return false;
}

/// HandleMemberPointerAccess - Evaluate a member access operation and build an
/// lvalue referring to the result.
///
/// \param Info - Information about the ongoing evaluation.
/// \param LV - An lvalue referring to the base of the member pointer.
/// \param RHS - The member pointer expression.
/// \param IncludeMember - Specifies whether the member itself is included in
///        the resulting LValue subobject designator. This is not possible when
///        creating a bound member function.
/// \return The field or method declaration to which the member pointer refers,
///         or 0 if evaluation fails.
static const ValueDecl *HandleMemberPointerAccess(EvalInfo &Info,
                                                  QualType LVType,
                                                  LValue &LV,
                                                  const Expr *RHS,
                                                  bool IncludeMember = true) {
  MemberPtr MemPtr;
  if (!EvaluateMemberPointer(RHS, MemPtr, Info))
    return nullptr;

  // C++11 [expr.mptr.oper]p6: If the second operand is the null pointer to
  // member value, the behavior is undefined.
  if (!MemPtr.getDecl()) {
    // FIXME: Specific diagnostic.
    Info.FFDiag(RHS);
    return nullptr;
  }

  if (MemPtr.isDerivedMember()) {
    // This is a member of some derived class. Truncate LV appropriately.
    // The end of the derived-to-base path for the base object must match the
    // derived-to-base path for the member pointer.
    if (LV.Designator.MostDerivedPathLength + MemPtr.Path.size() >
        LV.Designator.Entries.size()) {
      Info.FFDiag(RHS);
      return nullptr;
    }
    unsigned PathLengthToMember =
        LV.Designator.Entries.size() - MemPtr.Path.size();
    for (unsigned I = 0, N = MemPtr.Path.size(); I != N; ++I) {
      const CXXRecordDecl *LVDecl = getAsBaseClass(
          LV.Designator.Entries[PathLengthToMember + I]);
      const CXXRecordDecl *MPDecl = MemPtr.Path[I];
      if (LVDecl->getCanonicalDecl() != MPDecl->getCanonicalDecl()) {
        Info.FFDiag(RHS);
        return nullptr;
      }
    }

    // Truncate the lvalue to the appropriate derived class.
    if (!CastToDerivedClass(Info, RHS, LV, MemPtr.getContainingRecord(),
                            PathLengthToMember))
      return nullptr;
  } else if (!MemPtr.Path.empty()) {
    // Extend the LValue path with the member pointer's path.
    LV.Designator.Entries.reserve(LV.Designator.Entries.size() +
                                  MemPtr.Path.size() + IncludeMember);

    // Walk down to the appropriate base class.
    if (const PointerType *PT = LVType->getAs<PointerType>())
      LVType = PT->getPointeeType();
    const CXXRecordDecl *RD = LVType->getAsCXXRecordDecl();
    assert(RD && "member pointer access on non-class-type expression");
    // The first class in the path is that of the lvalue.
    for (unsigned I = 1, N = MemPtr.Path.size(); I != N; ++I) {
      const CXXRecordDecl *Base = MemPtr.Path[N - I - 1];
      if (!HandleLValueDirectBase(Info, RHS, LV, RD, Base))
        return nullptr;
      RD = Base;
    }
    // Finally cast to the class containing the member.
    if (!HandleLValueDirectBase(Info, RHS, LV, RD,
                                MemPtr.getContainingRecord()))
      return nullptr;
  }

  // Add the member. Note that we cannot build bound member functions here.
  if (IncludeMember) {
    if (const FieldDecl *FD = dyn_cast<FieldDecl>(MemPtr.getDecl())) {
      if (!HandleLValueMember(Info, RHS, LV, FD))
        return nullptr;
    } else if (const IndirectFieldDecl *IFD =
                 dyn_cast<IndirectFieldDecl>(MemPtr.getDecl())) {
      if (!HandleLValueIndirectMember(Info, RHS, LV, IFD))
        return nullptr;
    } else {
      llvm_unreachable("can't construct reference to bound member function");
    }
  }

  return MemPtr.getDecl();
}

static const ValueDecl *HandleMemberPointerAccess(EvalInfo &Info,
                                                  const BinaryOperator *BO,
                                                  LValue &LV,
                                                  bool IncludeMember = true) {
  assert(BO->getOpcode() == BO_PtrMemD || BO->getOpcode() == BO_PtrMemI);

  if (!EvaluateObjectArgument(Info, BO->getLHS(), LV)) {
    if (Info.noteFailure()) {
      MemberPtr MemPtr;
      EvaluateMemberPointer(BO->getRHS(), MemPtr, Info);
    }
    return nullptr;
  }

  return HandleMemberPointerAccess(Info, BO->getLHS()->getType(), LV,
                                   BO->getRHS(), IncludeMember);
}

/// HandleBaseToDerivedCast - Apply the given base-to-derived cast operation on
/// the provided lvalue, which currently refers to the base object.
static bool HandleBaseToDerivedCast(EvalInfo &Info, const CastExpr *E,
                                    LValue &Result) {
  SubobjectDesignator &D = Result.Designator;
  if (D.Invalid || !Result.checkNullPointer(Info, E, CSK_Derived))
    return false;

  QualType TargetQT = E->getType();
  if (const PointerType *PT = TargetQT->getAs<PointerType>())
    TargetQT = PT->getPointeeType();

  // Check this cast lands within the final derived-to-base subobject path.
  if (D.MostDerivedPathLength + E->path_size() > D.Entries.size()) {
    Info.CCEDiag(E, diag::note_constexpr_invalid_downcast)
      << D.MostDerivedType << TargetQT;
    return false;
  }

  // Check the type of the final cast. We don't need to check the path,
  // since a cast can only be formed if the path is unique.
  unsigned NewEntriesSize = D.Entries.size() - E->path_size();
  const CXXRecordDecl *TargetType = TargetQT->getAsCXXRecordDecl();
  const CXXRecordDecl *FinalType;
  if (NewEntriesSize == D.MostDerivedPathLength)
    FinalType = D.MostDerivedType->getAsCXXRecordDecl();
  else
    FinalType = getAsBaseClass(D.Entries[NewEntriesSize - 1]);
  if (FinalType->getCanonicalDecl() != TargetType->getCanonicalDecl()) {
    Info.CCEDiag(E, diag::note_constexpr_invalid_downcast)
      << D.MostDerivedType << TargetQT;
    return false;
  }

  // Truncate the lvalue to the appropriate derived class.
  return CastToDerivedClass(Info, E, Result, TargetType, NewEntriesSize);
}

namespace {
enum EvalStmtResult {
  /// Evaluation failed.
  ESR_Failed,
  /// Hit a 'return' statement.
  ESR_Returned,
  /// Evaluation succeeded.
  ESR_Succeeded,
  /// Hit a 'continue' statement.
  ESR_Continue,
  /// Hit a 'break' statement.
  ESR_Break,
  /// Still scanning for 'case' or 'default' statement.
  ESR_CaseNotFound
};
}

static bool EvaluateVarDecl(EvalInfo &Info, const VarDecl *VD) {
  // We don't need to evaluate the initializer for a static local.
  if (!VD->hasLocalStorage())
    return true;

  LValue Result;
  APValue &Val = createTemporary(VD, true, Result, *Info.CurrentCall);

  const Expr *InitE = VD->getInit();
  if (!InitE) {
    Info.FFDiag(VD->getBeginLoc(), diag::note_constexpr_uninitialized)
        << false << VD->getType();
    Val = APValue();
    return false;
  }

  if (InitE->isValueDependent())
    return false;

  if (!EvaluateInPlace(Val, Info, Result, InitE)) {
    // Wipe out any partially-computed value, to allow tracking that this
    // evaluation failed.
    Val = APValue();
    return false;
  }

  return true;
}

static bool EvaluateDecl(EvalInfo &Info, const Decl *D) {
  bool OK = true;

  if (const VarDecl *VD = dyn_cast<VarDecl>(D))
    OK &= EvaluateVarDecl(Info, VD);

  if (const DecompositionDecl *DD = dyn_cast<DecompositionDecl>(D))
    for (auto *BD : DD->bindings())
      if (auto *VD = BD->getHoldingVar())
        OK &= EvaluateDecl(Info, VD);

  return OK;
}


/// Evaluate a condition (either a variable declaration or an expression).
static bool EvaluateCond(EvalInfo &Info, const VarDecl *CondDecl,
                         const Expr *Cond, bool &Result) {
  FullExpressionRAII Scope(Info);
  if (CondDecl && !EvaluateDecl(Info, CondDecl))
    return false;
  return EvaluateAsBooleanCondition(Cond, Result, Info);
}

namespace {
/// A location where the result (returned value) of evaluating a
/// statement should be stored.
struct StmtResult {
  /// The APValue that should be filled in with the returned value.
  APValue &Value;
  /// The location containing the result, if any (used to support RVO).
  const LValue *Slot;
};

struct TempVersionRAII {
  CallStackFrame &Frame;

  TempVersionRAII(CallStackFrame &Frame) : Frame(Frame) {
    Frame.pushTempVersion();
  }

  ~TempVersionRAII() {
    Frame.popTempVersion();
  }
};

}

static EvalStmtResult EvaluateStmt(StmtResult &Result, EvalInfo &Info,
                                   const Stmt *S,
                                   const SwitchCase *SC = nullptr);

/// Evaluate the body of a loop, and translate the result as appropriate.
static EvalStmtResult EvaluateLoopBody(StmtResult &Result, EvalInfo &Info,
                                       const Stmt *Body,
                                       const SwitchCase *Case = nullptr) {
  BlockScopeRAII Scope(Info);
  switch (EvalStmtResult ESR = EvaluateStmt(Result, Info, Body, Case)) {
  case ESR_Break:
    return ESR_Succeeded;
  case ESR_Succeeded:
  case ESR_Continue:
    return ESR_Continue;
  case ESR_Failed:
  case ESR_Returned:
  case ESR_CaseNotFound:
    return ESR;
  }
  llvm_unreachable("Invalid EvalStmtResult!");
}

/// Evaluate a switch statement.
static EvalStmtResult EvaluateSwitch(StmtResult &Result, EvalInfo &Info,
                                     const SwitchStmt *SS) {
  BlockScopeRAII Scope(Info);

  // Evaluate the switch condition.
  APSInt Value;
  {
    FullExpressionRAII Scope(Info);
    if (const Stmt *Init = SS->getInit()) {
      EvalStmtResult ESR = EvaluateStmt(Result, Info, Init);
      if (ESR != ESR_Succeeded)
        return ESR;
    }
    if (SS->getConditionVariable() &&
        !EvaluateDecl(Info, SS->getConditionVariable()))
      return ESR_Failed;
    if (!EvaluateInteger(SS->getCond(), Value, Info))
      return ESR_Failed;
  }

  // Find the switch case corresponding to the value of the condition.
  // FIXME: Cache this lookup.
  const SwitchCase *Found = nullptr;
  for (const SwitchCase *SC = SS->getSwitchCaseList(); SC;
       SC = SC->getNextSwitchCase()) {
    if (isa<DefaultStmt>(SC)) {
      Found = SC;
      continue;
    }

    const CaseStmt *CS = cast<CaseStmt>(SC);
    APSInt LHS = CS->getLHS()->EvaluateKnownConstInt(Info.Ctx);
    APSInt RHS = CS->getRHS() ? CS->getRHS()->EvaluateKnownConstInt(Info.Ctx)
                              : LHS;
    if (LHS <= Value && Value <= RHS) {
      Found = SC;
      break;
    }
  }

  if (!Found)
    return ESR_Succeeded;

  // Search the switch body for the switch case and evaluate it from there.
  switch (EvalStmtResult ESR = EvaluateStmt(Result, Info, SS->getBody(), Found)) {
  case ESR_Break:
    return ESR_Succeeded;
  case ESR_Succeeded:
  case ESR_Continue:
  case ESR_Failed:
  case ESR_Returned:
    return ESR;
  case ESR_CaseNotFound:
    // This can only happen if the switch case is nested within a statement
    // expression. We have no intention of supporting that.
    Info.FFDiag(Found->getBeginLoc(),
                diag::note_constexpr_stmt_expr_unsupported);
    return ESR_Failed;
  }
  llvm_unreachable("Invalid EvalStmtResult!");
}

// Evaluate a statement.
static EvalStmtResult EvaluateStmt(StmtResult &Result, EvalInfo &Info,
                                   const Stmt *S, const SwitchCase *Case) {
  if (!Info.nextStep(S))
    return ESR_Failed;

  // If we're hunting down a 'case' or 'default' label, recurse through
  // substatements until we hit the label.
  if (Case) {
    // FIXME: We don't start the lifetime of objects whose initialization we
    // jump over. However, such objects must be of class type with a trivial
    // default constructor that initialize all subobjects, so must be empty,
    // so this almost never matters.
    switch (S->getStmtClass()) {
    case Stmt::CompoundStmtClass:
      // FIXME: Precompute which substatement of a compound statement we
      // would jump to, and go straight there rather than performing a
      // linear scan each time.
    case Stmt::LabelStmtClass:
    case Stmt::AttributedStmtClass:
    case Stmt::DoStmtClass:
      break;

    case Stmt::CaseStmtClass:
    case Stmt::DefaultStmtClass:
      if (Case == S)
        Case = nullptr;
      break;

    case Stmt::IfStmtClass: {
      // FIXME: Precompute which side of an 'if' we would jump to, and go
      // straight there rather than scanning both sides.
      const IfStmt *IS = cast<IfStmt>(S);

      // Wrap the evaluation in a block scope, in case it's a DeclStmt
      // preceded by our switch label.
      BlockScopeRAII Scope(Info);

      EvalStmtResult ESR = EvaluateStmt(Result, Info, IS->getThen(), Case);
      if (ESR != ESR_CaseNotFound || !IS->getElse())
        return ESR;
      return EvaluateStmt(Result, Info, IS->getElse(), Case);
    }

    case Stmt::WhileStmtClass: {
      EvalStmtResult ESR =
          EvaluateLoopBody(Result, Info, cast<WhileStmt>(S)->getBody(), Case);
      if (ESR != ESR_Continue)
        return ESR;
      break;
    }

    case Stmt::ForStmtClass: {
      const ForStmt *FS = cast<ForStmt>(S);
      EvalStmtResult ESR =
          EvaluateLoopBody(Result, Info, FS->getBody(), Case);
      if (ESR != ESR_Continue)
        return ESR;
      if (FS->getInc()) {
        FullExpressionRAII IncScope(Info);
        if (!EvaluateIgnoredValue(Info, FS->getInc()))
          return ESR_Failed;
      }
      break;
    }

    case Stmt::DeclStmtClass:
      // FIXME: If the variable has initialization that can't be jumped over,
      // bail out of any immediately-surrounding compound-statement too.
    default:
      return ESR_CaseNotFound;
    }
  }

  switch (S->getStmtClass()) {
  default:
    if (const Expr *E = dyn_cast<Expr>(S)) {
      // Don't bother evaluating beyond an expression-statement which couldn't
      // be evaluated.
      FullExpressionRAII Scope(Info);
      if (!EvaluateIgnoredValue(Info, E))
        return ESR_Failed;
      return ESR_Succeeded;
    }

    Info.FFDiag(S->getBeginLoc());
    return ESR_Failed;

  case Stmt::NullStmtClass:
    return ESR_Succeeded;

  case Stmt::DeclStmtClass: {
    const DeclStmt *DS = cast<DeclStmt>(S);
    for (const auto *DclIt : DS->decls()) {
      // Each declaration initialization is its own full-expression.
      // FIXME: This isn't quite right; if we're performing aggregate
      // initialization, each braced subexpression is its own full-expression.
      FullExpressionRAII Scope(Info);
      if (!EvaluateDecl(Info, DclIt) && !Info.noteFailure())
        return ESR_Failed;
    }
    return ESR_Succeeded;
  }

  case Stmt::ReturnStmtClass: {
    const Expr *RetExpr = cast<ReturnStmt>(S)->getRetValue();
    FullExpressionRAII Scope(Info);
    if (RetExpr &&
        !(Result.Slot
              ? EvaluateInPlace(Result.Value, Info, *Result.Slot, RetExpr)
              : Evaluate(Result.Value, Info, RetExpr)))
      return ESR_Failed;
    return ESR_Returned;
  }

  case Stmt::CompoundStmtClass: {
    BlockScopeRAII Scope(Info);

    const CompoundStmt *CS = cast<CompoundStmt>(S);
    for (const auto *BI : CS->body()) {
      EvalStmtResult ESR = EvaluateStmt(Result, Info, BI, Case);
      if (ESR == ESR_Succeeded)
        Case = nullptr;
      else if (ESR != ESR_CaseNotFound)
        return ESR;
    }
    return Case ? ESR_CaseNotFound : ESR_Succeeded;
  }

  case Stmt::IfStmtClass: {
    const IfStmt *IS = cast<IfStmt>(S);

    // Evaluate the condition, as either a var decl or as an expression.
    BlockScopeRAII Scope(Info);
    if (const Stmt *Init = IS->getInit()) {
      EvalStmtResult ESR = EvaluateStmt(Result, Info, Init);
      if (ESR != ESR_Succeeded)
        return ESR;
    }
    bool Cond;
    if (!EvaluateCond(Info, IS->getConditionVariable(), IS->getCond(), Cond))
      return ESR_Failed;

    if (const Stmt *SubStmt = Cond ? IS->getThen() : IS->getElse()) {
      EvalStmtResult ESR = EvaluateStmt(Result, Info, SubStmt);
      if (ESR != ESR_Succeeded)
        return ESR;
    }
    return ESR_Succeeded;
  }

  case Stmt::WhileStmtClass: {
    const WhileStmt *WS = cast<WhileStmt>(S);
    while (true) {
      BlockScopeRAII Scope(Info);
      bool Continue;
      if (!EvaluateCond(Info, WS->getConditionVariable(), WS->getCond(),
                        Continue))
        return ESR_Failed;
      if (!Continue)
        break;

      EvalStmtResult ESR = EvaluateLoopBody(Result, Info, WS->getBody());
      if (ESR != ESR_Continue)
        return ESR;
    }
    return ESR_Succeeded;
  }

  case Stmt::DoStmtClass: {
    const DoStmt *DS = cast<DoStmt>(S);
    bool Continue;
    do {
      EvalStmtResult ESR = EvaluateLoopBody(Result, Info, DS->getBody(), Case);
      if (ESR != ESR_Continue)
        return ESR;
      Case = nullptr;

      FullExpressionRAII CondScope(Info);
      if (!EvaluateAsBooleanCondition(DS->getCond(), Continue, Info))
        return ESR_Failed;
    } while (Continue);
    return ESR_Succeeded;
  }

  case Stmt::ForStmtClass: {
    const ForStmt *FS = cast<ForStmt>(S);
    BlockScopeRAII Scope(Info);
    if (FS->getInit()) {
      EvalStmtResult ESR = EvaluateStmt(Result, Info, FS->getInit());
      if (ESR != ESR_Succeeded)
        return ESR;
    }
    while (true) {
      BlockScopeRAII Scope(Info);
      bool Continue = true;
      if (FS->getCond() && !EvaluateCond(Info, FS->getConditionVariable(),
                                         FS->getCond(), Continue))
        return ESR_Failed;
      if (!Continue)
        break;

      EvalStmtResult ESR = EvaluateLoopBody(Result, Info, FS->getBody());
      if (ESR != ESR_Continue)
        return ESR;

      if (FS->getInc()) {
        FullExpressionRAII IncScope(Info);
        if (!EvaluateIgnoredValue(Info, FS->getInc()))
          return ESR_Failed;
      }
    }
    return ESR_Succeeded;
  }

  case Stmt::CXXForRangeStmtClass: {
    const CXXForRangeStmt *FS = cast<CXXForRangeStmt>(S);
    BlockScopeRAII Scope(Info);

    // Evaluate the init-statement if present.
    if (FS->getInit()) {
      EvalStmtResult ESR = EvaluateStmt(Result, Info, FS->getInit());
      if (ESR != ESR_Succeeded)
        return ESR;
    }

    // Initialize the __range variable.
    EvalStmtResult ESR = EvaluateStmt(Result, Info, FS->getRangeStmt());
    if (ESR != ESR_Succeeded)
      return ESR;

    // Create the __begin and __end iterators.
    ESR = EvaluateStmt(Result, Info, FS->getBeginStmt());
    if (ESR != ESR_Succeeded)
      return ESR;
    ESR = EvaluateStmt(Result, Info, FS->getEndStmt());
    if (ESR != ESR_Succeeded)
      return ESR;

    while (true) {
      // Condition: __begin != __end.
      {
        bool Continue = true;
        FullExpressionRAII CondExpr(Info);
        if (!EvaluateAsBooleanCondition(FS->getCond(), Continue, Info))
          return ESR_Failed;
        if (!Continue)
          break;
      }

      // User's variable declaration, initialized by *__begin.
      BlockScopeRAII InnerScope(Info);
      ESR = EvaluateStmt(Result, Info, FS->getLoopVarStmt());
      if (ESR != ESR_Succeeded)
        return ESR;

      // Loop body.
      ESR = EvaluateLoopBody(Result, Info, FS->getBody());
      if (ESR != ESR_Continue)
        return ESR;

      // Increment: ++__begin
      if (!EvaluateIgnoredValue(Info, FS->getInc()))
        return ESR_Failed;
    }

    return ESR_Succeeded;
  }

  case Stmt::SwitchStmtClass:
    return EvaluateSwitch(Result, Info, cast<SwitchStmt>(S));

  case Stmt::ContinueStmtClass:
    return ESR_Continue;

  case Stmt::BreakStmtClass:
    return ESR_Break;

  case Stmt::LabelStmtClass:
    return EvaluateStmt(Result, Info, cast<LabelStmt>(S)->getSubStmt(), Case);

  case Stmt::AttributedStmtClass:
    // As a general principle, C++11 attributes can be ignored without
    // any semantic impact.
    return EvaluateStmt(Result, Info, cast<AttributedStmt>(S)->getSubStmt(),
                        Case);

  case Stmt::CaseStmtClass:
  case Stmt::DefaultStmtClass:
    return EvaluateStmt(Result, Info, cast<SwitchCase>(S)->getSubStmt(), Case);
  case Stmt::CXXTryStmtClass:
    // Evaluate try blocks by evaluating all sub statements.
    return EvaluateStmt(Result, Info, cast<CXXTryStmt>(S)->getTryBlock(), Case);
  }
}

/// CheckTrivialDefaultConstructor - Check whether a constructor is a trivial
/// default constructor. If so, we'll fold it whether or not it's marked as
/// constexpr. If it is marked as constexpr, we will never implicitly define it,
/// so we need special handling.
static bool CheckTrivialDefaultConstructor(EvalInfo &Info, SourceLocation Loc,
                                           const CXXConstructorDecl *CD,
                                           bool IsValueInitialization) {
  if (!CD->isTrivial() || !CD->isDefaultConstructor())
    return false;

  // Value-initialization does not call a trivial default constructor, so such a
  // call is a core constant expression whether or not the constructor is
  // constexpr.
  if (!CD->isConstexpr() && !IsValueInitialization) {
    if (Info.getLangOpts().CPlusPlus11) {
      // FIXME: If DiagDecl is an implicitly-declared special member function,
      // we should be much more explicit about why it's not constexpr.
      Info.CCEDiag(Loc, diag::note_constexpr_invalid_function, 1)
        << /*IsConstexpr*/0 << /*IsConstructor*/1 << CD;
      Info.Note(CD->getLocation(), diag::note_declared_at);
    } else {
      Info.CCEDiag(Loc, diag::note_invalid_subexpr_in_const_expr);
    }
  }
  return true;
}

/// CheckConstexprFunction - Check that a function can be called in a constant
/// expression.
static bool CheckConstexprFunction(EvalInfo &Info, SourceLocation CallLoc,
                                   const FunctionDecl *Declaration,
                                   const FunctionDecl *Definition,
                                   const Stmt *Body) {
  // Potential constant expressions can contain calls to declared, but not yet
  // defined, constexpr functions.
  if (Info.checkingPotentialConstantExpression() && !Definition &&
      Declaration->isConstexpr())
    return false;

  // Bail out if the function declaration itself is invalid.  We will
  // have produced a relevant diagnostic while parsing it, so just
  // note the problematic sub-expression.
  if (Declaration->isInvalidDecl()) {
    Info.FFDiag(CallLoc, diag::note_invalid_subexpr_in_const_expr);
    return false;
  }

  // Can we evaluate this function call?
  if (Definition && Definition->isConstexpr() &&
      !Definition->isInvalidDecl() && Body)
    return true;

  if (Info.getLangOpts().CPlusPlus11) {
    const FunctionDecl *DiagDecl = Definition ? Definition : Declaration;

    // If this function is not constexpr because it is an inherited
    // non-constexpr constructor, diagnose that directly.
    auto *CD = dyn_cast<CXXConstructorDecl>(DiagDecl);
    if (CD && CD->isInheritingConstructor()) {
      auto *Inherited = CD->getInheritedConstructor().getConstructor();
      if (!Inherited->isConstexpr())
        DiagDecl = CD = Inherited;
    }

    // FIXME: If DiagDecl is an implicitly-declared special member function
    // or an inheriting constructor, we should be much more explicit about why
    // it's not constexpr.
    if (CD && CD->isInheritingConstructor())
      Info.FFDiag(CallLoc, diag::note_constexpr_invalid_inhctor, 1)
        << CD->getInheritedConstructor().getConstructor()->getParent();
    else
      Info.FFDiag(CallLoc, diag::note_constexpr_invalid_function, 1)
        << DiagDecl->isConstexpr() << (bool)CD << DiagDecl;
    Info.Note(DiagDecl->getLocation(), diag::note_declared_at);
  } else {
    Info.FFDiag(CallLoc, diag::note_invalid_subexpr_in_const_expr);
  }
  return false;
}

/// Determine if a class has any fields that might need to be copied by a
/// trivial copy or move operation.
static bool hasFields(const CXXRecordDecl *RD) {
  if (!RD || RD->isEmpty())
    return false;
  for (auto *FD : RD->fields()) {
    if (FD->isUnnamedBitfield())
      continue;
    return true;
  }
  for (auto &Base : RD->bases())
    if (hasFields(Base.getType()->getAsCXXRecordDecl()))
      return true;
  return false;
}

namespace {
typedef SmallVector<APValue, 8> ArgVector;
}

/// EvaluateArgs - Evaluate the arguments to a function call.
static bool EvaluateArgs(ArrayRef<const Expr*> Args, ArgVector &ArgValues,
                         EvalInfo &Info) {
  bool Success = true;
  for (ArrayRef<const Expr*>::iterator I = Args.begin(), E = Args.end();
       I != E; ++I) {
    if (!Evaluate(ArgValues[I - Args.begin()], Info, *I)) {
      // If we're checking for a potential constant expression, evaluate all
      // initializers even if some of them fail.
      if (!Info.noteFailure())
        return false;
      Success = false;
    }
  }
  return Success;
}

/// Evaluate a function call.
static bool HandleFunctionCall(SourceLocation CallLoc,
                               const FunctionDecl *Callee, const LValue *This,
                               ArrayRef<const Expr*> Args, const Stmt *Body,
                               EvalInfo &Info, APValue &Result,
                               const LValue *ResultSlot) {
  ArgVector ArgValues(Args.size());
  if (!EvaluateArgs(Args, ArgValues, Info))
    return false;

  if (!Info.CheckCallLimit(CallLoc))
    return false;

  CallStackFrame Frame(Info, CallLoc, Callee, This, ArgValues.data());

  // For a trivial copy or move assignment, perform an APValue copy. This is
  // essential for unions, where the operations performed by the assignment
  // operator cannot be represented as statements.
  //
  // Skip this for non-union classes with no fields; in that case, the defaulted
  // copy/move does not actually read the object.
  const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(Callee);
  if (MD && MD->isDefaulted() &&
      (MD->getParent()->isUnion() ||
       (MD->isTrivial() && hasFields(MD->getParent())))) {
    assert(This &&
           (MD->isCopyAssignmentOperator() || MD->isMoveAssignmentOperator()));
    LValue RHS;
    RHS.setFrom(Info.Ctx, ArgValues[0]);
    APValue RHSValue;
    if (!handleLValueToRValueConversion(Info, Args[0], Args[0]->getType(),
                                        RHS, RHSValue))
      return false;
    if (!handleAssignment(Info, Args[0], *This, MD->getThisType(),
                          RHSValue))
      return false;
    This->moveInto(Result);
    return true;
  } else if (MD && isLambdaCallOperator(MD)) {
    // We're in a lambda; determine the lambda capture field maps unless we're
    // just constexpr checking a lambda's call operator. constexpr checking is
    // done before the captures have been added to the closure object (unless
    // we're inferring constexpr-ness), so we don't have access to them in this
    // case. But since we don't need the captures to constexpr check, we can
    // just ignore them.
    if (!Info.checkingPotentialConstantExpression())
      MD->getParent()->getCaptureFields(Frame.LambdaCaptureFields,
                                        Frame.LambdaThisCaptureField);
  }

  StmtResult Ret = {Result, ResultSlot};
  EvalStmtResult ESR = EvaluateStmt(Ret, Info, Body);
  if (ESR == ESR_Succeeded) {
    if (Callee->getReturnType()->isVoidType())
      return true;
    Info.FFDiag(Callee->getEndLoc(), diag::note_constexpr_no_return);
  }
  return ESR == ESR_Returned;
}

/// Evaluate a constructor call.
static bool HandleConstructorCall(const Expr *E, const LValue &This,
                                  APValue *ArgValues,
                                  const CXXConstructorDecl *Definition,
                                  EvalInfo &Info, APValue &Result) {
  SourceLocation CallLoc = E->getExprLoc();
  if (!Info.CheckCallLimit(CallLoc))
    return false;

  const CXXRecordDecl *RD = Definition->getParent();
  if (RD->getNumVBases()) {
    Info.FFDiag(CallLoc, diag::note_constexpr_virtual_base) << RD;
    return false;
  }

  EvalInfo::EvaluatingConstructorRAII EvalObj(
      Info, {This.getLValueBase(),
             {This.getLValueCallIndex(), This.getLValueVersion()}});
  CallStackFrame Frame(Info, CallLoc, Definition, &This, ArgValues);

  // FIXME: Creating an APValue just to hold a nonexistent return value is
  // wasteful.
  APValue RetVal;
  StmtResult Ret = {RetVal, nullptr};

  // If it's a delegating constructor, delegate.
  if (Definition->isDelegatingConstructor()) {
    CXXConstructorDecl::init_const_iterator I = Definition->init_begin();
    {
      FullExpressionRAII InitScope(Info);
      if (!EvaluateInPlace(Result, Info, This, (*I)->getInit()))
        return false;
    }
    return EvaluateStmt(Ret, Info, Definition->getBody()) != ESR_Failed;
  }

  // For a trivial copy or move constructor, perform an APValue copy. This is
  // essential for unions (or classes with anonymous union members), where the
  // operations performed by the constructor cannot be represented by
  // ctor-initializers.
  //
  // Skip this for empty non-union classes; we should not perform an
  // lvalue-to-rvalue conversion on them because their copy constructor does not
  // actually read them.
  if (Definition->isDefaulted() && Definition->isCopyOrMoveConstructor() &&
      (Definition->getParent()->isUnion() ||
       (Definition->isTrivial() && hasFields(Definition->getParent())))) {
    LValue RHS;
    RHS.setFrom(Info.Ctx, ArgValues[0]);
    return handleLValueToRValueConversion(
        Info, E, Definition->getParamDecl(0)->getType().getNonReferenceType(),
        RHS, Result);
  }

  // Reserve space for the struct members.
  if (!RD->isUnion() && Result.isUninit())
    Result = APValue(APValue::UninitStruct(), RD->getNumBases(),
                     std::distance(RD->field_begin(), RD->field_end()));

  if (RD->isInvalidDecl()) return false;
  const ASTRecordLayout &Layout = Info.Ctx.getASTRecordLayout(RD);

  // A scope for temporaries lifetime-extended by reference members.
  BlockScopeRAII LifetimeExtendedScope(Info);

  bool Success = true;
  unsigned BasesSeen = 0;
#ifndef NDEBUG
  CXXRecordDecl::base_class_const_iterator BaseIt = RD->bases_begin();
#endif
  for (const auto *I : Definition->inits()) {
    LValue Subobject = This;
    LValue SubobjectParent = This;
    APValue *Value = &Result;

    // Determine the subobject to initialize.
    FieldDecl *FD = nullptr;
    if (I->isBaseInitializer()) {
      QualType BaseType(I->getBaseClass(), 0);
#ifndef NDEBUG
      // Non-virtual base classes are initialized in the order in the class
      // definition. We have already checked for virtual base classes.
      assert(!BaseIt->isVirtual() && "virtual base for literal type");
      assert(Info.Ctx.hasSameType(BaseIt->getType(), BaseType) &&
             "base class initializers not in expected order");
      ++BaseIt;
#endif
      if (!HandleLValueDirectBase(Info, I->getInit(), Subobject, RD,
                                  BaseType->getAsCXXRecordDecl(), &Layout))
        return false;
      Value = &Result.getStructBase(BasesSeen++);
    } else if ((FD = I->getMember())) {
      if (!HandleLValueMember(Info, I->getInit(), Subobject, FD, &Layout))
        return false;
      if (RD->isUnion()) {
        Result = APValue(FD);
        Value = &Result.getUnionValue();
      } else {
        Value = &Result.getStructField(FD->getFieldIndex());
      }
    } else if (IndirectFieldDecl *IFD = I->getIndirectMember()) {
      // Walk the indirect field decl's chain to find the object to initialize,
      // and make sure we've initialized every step along it.
      auto IndirectFieldChain = IFD->chain();
      for (auto *C : IndirectFieldChain) {
        FD = cast<FieldDecl>(C);
        CXXRecordDecl *CD = cast<CXXRecordDecl>(FD->getParent());
        // Switch the union field if it differs. This happens if we had
        // preceding zero-initialization, and we're now initializing a union
        // subobject other than the first.
        // FIXME: In this case, the values of the other subobjects are
        // specified, since zero-initialization sets all padding bits to zero.
        if (Value->isUninit() ||
            (Value->isUnion() && Value->getUnionField() != FD)) {
          if (CD->isUnion())
            *Value = APValue(FD);
          else
            *Value = APValue(APValue::UninitStruct(), CD->getNumBases(),
                             std::distance(CD->field_begin(), CD->field_end()));
        }
        // Store Subobject as its parent before updating it for the last element
        // in the chain.
        if (C == IndirectFieldChain.back())
          SubobjectParent = Subobject;
        if (!HandleLValueMember(Info, I->getInit(), Subobject, FD))
          return false;
        if (CD->isUnion())
          Value = &Value->getUnionValue();
        else
          Value = &Value->getStructField(FD->getFieldIndex());
      }
    } else {
      llvm_unreachable("unknown base initializer kind");
    }

    // Need to override This for implicit field initializers as in this case
    // This refers to innermost anonymous struct/union containing initializer,
    // not to currently constructed class.
    const Expr *Init = I->getInit();
    ThisOverrideRAII ThisOverride(*Info.CurrentCall, &SubobjectParent,
                                  isa<CXXDefaultInitExpr>(Init));
    FullExpressionRAII InitScope(Info);
    if (!EvaluateInPlace(*Value, Info, Subobject, Init) ||
        (FD && FD->isBitField() &&
         !truncateBitfieldValue(Info, Init, *Value, FD))) {
      // If we're checking for a potential constant expression, evaluate all
      // initializers even if some of them fail.
      if (!Info.noteFailure())
        return false;
      Success = false;
    }
  }

  return Success &&
         EvaluateStmt(Ret, Info, Definition->getBody()) != ESR_Failed;
}

static bool HandleConstructorCall(const Expr *E, const LValue &This,
                                  ArrayRef<const Expr*> Args,
                                  const CXXConstructorDecl *Definition,
                                  EvalInfo &Info, APValue &Result) {
  ArgVector ArgValues(Args.size());
  if (!EvaluateArgs(Args, ArgValues, Info))
    return false;

  return HandleConstructorCall(E, This, ArgValues.data(), Definition,
                               Info, Result);
}

//===----------------------------------------------------------------------===//
// Generic Evaluation
//===----------------------------------------------------------------------===//
namespace {

template <class Derived>
class ExprEvaluatorBase
  : public ConstStmtVisitor<Derived, bool> {
private:
  Derived &getDerived() { return static_cast<Derived&>(*this); }
  bool DerivedSuccess(const APValue &V, const Expr *E) {
    return getDerived().Success(V, E);
  }
  bool DerivedZeroInitialization(const Expr *E) {
    return getDerived().ZeroInitialization(E);
  }

  // Check whether a conditional operator with a non-constant condition is a
  // potential constant expression. If neither arm is a potential constant
  // expression, then the conditional operator is not either.
  template<typename ConditionalOperator>
  void CheckPotentialConstantConditional(const ConditionalOperator *E) {
    assert(Info.checkingPotentialConstantExpression());

    // Speculatively evaluate both arms.
    SmallVector<PartialDiagnosticAt, 8> Diag;
    {
      SpeculativeEvaluationRAII Speculate(Info, &Diag);
      StmtVisitorTy::Visit(E->getFalseExpr());
      if (Diag.empty())
        return;
    }

    {
      SpeculativeEvaluationRAII Speculate(Info, &Diag);
      Diag.clear();
      StmtVisitorTy::Visit(E->getTrueExpr());
      if (Diag.empty())
        return;
    }

    Error(E, diag::note_constexpr_conditional_never_const);
  }


  template<typename ConditionalOperator>
  bool HandleConditionalOperator(const ConditionalOperator *E) {
    bool BoolResult;
    if (!EvaluateAsBooleanCondition(E->getCond(), BoolResult, Info)) {
      if (Info.checkingPotentialConstantExpression() && Info.noteFailure()) {
        CheckPotentialConstantConditional(E);
        return false;
      }
      if (Info.noteFailure()) {
        StmtVisitorTy::Visit(E->getTrueExpr());
        StmtVisitorTy::Visit(E->getFalseExpr());
      }
      return false;
    }

    Expr *EvalExpr = BoolResult ? E->getTrueExpr() : E->getFalseExpr();
    return StmtVisitorTy::Visit(EvalExpr);
  }

protected:
  EvalInfo &Info;
  typedef ConstStmtVisitor<Derived, bool> StmtVisitorTy;
  typedef ExprEvaluatorBase ExprEvaluatorBaseTy;

  OptionalDiagnostic CCEDiag(const Expr *E, diag::kind D) {
    return Info.CCEDiag(E, D);
  }

  bool ZeroInitialization(const Expr *E) { return Error(E); }

public:
  ExprEvaluatorBase(EvalInfo &Info) : Info(Info) {}

  EvalInfo &getEvalInfo() { return Info; }

  /// Report an evaluation error. This should only be called when an error is
  /// first discovered. When propagating an error, just return false.
  bool Error(const Expr *E, diag::kind D) {
    Info.FFDiag(E, D);
    return false;
  }
  bool Error(const Expr *E) {
    return Error(E, diag::note_invalid_subexpr_in_const_expr);
  }

  bool VisitStmt(const Stmt *) {
    llvm_unreachable("Expression evaluator should not be called on stmts");
  }
  bool VisitExpr(const Expr *E) {
    return Error(E);
  }

  bool VisitConstantExpr(const ConstantExpr *E)
    { return StmtVisitorTy::Visit(E->getSubExpr()); }
  bool VisitParenExpr(const ParenExpr *E)
    { return StmtVisitorTy::Visit(E->getSubExpr()); }
  bool VisitUnaryExtension(const UnaryOperator *E)
    { return StmtVisitorTy::Visit(E->getSubExpr()); }
  bool VisitUnaryPlus(const UnaryOperator *E)
    { return StmtVisitorTy::Visit(E->getSubExpr()); }
  bool VisitChooseExpr(const ChooseExpr *E)
    { return StmtVisitorTy::Visit(E->getChosenSubExpr()); }
  bool VisitGenericSelectionExpr(const GenericSelectionExpr *E)
    { return StmtVisitorTy::Visit(E->getResultExpr()); }
  bool VisitSubstNonTypeTemplateParmExpr(const SubstNonTypeTemplateParmExpr *E)
    { return StmtVisitorTy::Visit(E->getReplacement()); }
  bool VisitCXXDefaultArgExpr(const CXXDefaultArgExpr *E) {
    TempVersionRAII RAII(*Info.CurrentCall);
    return StmtVisitorTy::Visit(E->getExpr());
  }
  bool VisitCXXDefaultInitExpr(const CXXDefaultInitExpr *E) {
    TempVersionRAII RAII(*Info.CurrentCall);
    // The initializer may not have been parsed yet, or might be erroneous.
    if (!E->getExpr())
      return Error(E);
    return StmtVisitorTy::Visit(E->getExpr());
  }
  // We cannot create any objects for which cleanups are required, so there is
  // nothing to do here; all cleanups must come from unevaluated subexpressions.
  bool VisitExprWithCleanups(const ExprWithCleanups *E)
    { return StmtVisitorTy::Visit(E->getSubExpr()); }

  bool VisitCXXReinterpretCastExpr(const CXXReinterpretCastExpr *E) {
    CCEDiag(E, diag::note_constexpr_invalid_cast) << 0;
    return static_cast<Derived*>(this)->VisitCastExpr(E);
  }
  bool VisitCXXDynamicCastExpr(const CXXDynamicCastExpr *E) {
    CCEDiag(E, diag::note_constexpr_invalid_cast) << 1;
    return static_cast<Derived*>(this)->VisitCastExpr(E);
  }

  bool VisitBinaryOperator(const BinaryOperator *E) {
    switch (E->getOpcode()) {
    default:
      return Error(E);

    case BO_Comma:
      VisitIgnoredValue(E->getLHS());
      return StmtVisitorTy::Visit(E->getRHS());

    case BO_PtrMemD:
    case BO_PtrMemI: {
      LValue Obj;
      if (!HandleMemberPointerAccess(Info, E, Obj))
        return false;
      APValue Result;
      if (!handleLValueToRValueConversion(Info, E, E->getType(), Obj, Result))
        return false;
      return DerivedSuccess(Result, E);
    }
    }
  }

  bool VisitBinaryConditionalOperator(const BinaryConditionalOperator *E) {
    // Evaluate and cache the common expression. We treat it as a temporary,
    // even though it's not quite the same thing.
    if (!Evaluate(Info.CurrentCall->createTemporary(E->getOpaqueValue(), false),
                  Info, E->getCommon()))
      return false;

    return HandleConditionalOperator(E);
  }

  bool VisitConditionalOperator(const ConditionalOperator *E) {
    bool IsBcpCall = false;
    // If the condition (ignoring parens) is a __builtin_constant_p call,
    // the result is a constant expression if it can be folded without
    // side-effects. This is an important GNU extension. See GCC PR38377
    // for discussion.
    if (const CallExpr *CallCE =
          dyn_cast<CallExpr>(E->getCond()->IgnoreParenCasts()))
      if (CallCE->getBuiltinCallee() == Builtin::BI__builtin_constant_p)
        IsBcpCall = true;

    // Always assume __builtin_constant_p(...) ? ... : ... is a potential
    // constant expression; we can't check whether it's potentially foldable.
    if (Info.checkingPotentialConstantExpression() && IsBcpCall)
      return false;

    FoldConstant Fold(Info, IsBcpCall);
    if (!HandleConditionalOperator(E)) {
      Fold.keepDiagnostics();
      return false;
    }

    return true;
  }

  bool VisitOpaqueValueExpr(const OpaqueValueExpr *E) {
    if (APValue *Value = Info.CurrentCall->getCurrentTemporary(E))
      return DerivedSuccess(*Value, E);

    const Expr *Source = E->getSourceExpr();
    if (!Source)
      return Error(E);
    if (Source == E) { // sanity checking.
      assert(0 && "OpaqueValueExpr recursively refers to itself");
      return Error(E);
    }
    return StmtVisitorTy::Visit(Source);
  }

  bool VisitCallExpr(const CallExpr *E) {
    APValue Result;
    if (!handleCallExpr(E, Result, nullptr))
      return false;
    return DerivedSuccess(Result, E);
  }

  bool handleCallExpr(const CallExpr *E, APValue &Result,
                     const LValue *ResultSlot) {
    const Expr *Callee = E->getCallee()->IgnoreParens();
    QualType CalleeType = Callee->getType();

    const FunctionDecl *FD = nullptr;
    LValue *This = nullptr, ThisVal;
    auto Args = llvm::makeArrayRef(E->getArgs(), E->getNumArgs());
    bool HasQualifier = false;

    // Extract function decl and 'this' pointer from the callee.
    if (CalleeType->isSpecificBuiltinType(BuiltinType::BoundMember)) {
      const ValueDecl *Member = nullptr;
      if (const MemberExpr *ME = dyn_cast<MemberExpr>(Callee)) {
        // Explicit bound member calls, such as x.f() or p->g();
        if (!EvaluateObjectArgument(Info, ME->getBase(), ThisVal))
          return false;
        Member = ME->getMemberDecl();
        This = &ThisVal;
        HasQualifier = ME->hasQualifier();
      } else if (const BinaryOperator *BE = dyn_cast<BinaryOperator>(Callee)) {
        // Indirect bound member calls ('.*' or '->*').
        Member = HandleMemberPointerAccess(Info, BE, ThisVal, false);
        if (!Member) return false;
        This = &ThisVal;
      } else
        return Error(Callee);

      FD = dyn_cast<FunctionDecl>(Member);
      if (!FD)
        return Error(Callee);
    } else if (CalleeType->isFunctionPointerType()) {
      LValue Call;
      if (!EvaluatePointer(Callee, Call, Info))
        return false;

      if (!Call.getLValueOffset().isZero())
        return Error(Callee);
      FD = dyn_cast_or_null<FunctionDecl>(
                             Call.getLValueBase().dyn_cast<const ValueDecl*>());
      if (!FD)
        return Error(Callee);
      // Don't call function pointers which have been cast to some other type.
      // Per DR (no number yet), the caller and callee can differ in noexcept.
      if (!Info.Ctx.hasSameFunctionTypeIgnoringExceptionSpec(
        CalleeType->getPointeeType(), FD->getType())) {
        return Error(E);
      }

      // Overloaded operator calls to member functions are represented as normal
      // calls with '*this' as the first argument.
      const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD);
      if (MD && !MD->isStatic()) {
        // FIXME: When selecting an implicit conversion for an overloaded
        // operator delete, we sometimes try to evaluate calls to conversion
        // operators without a 'this' parameter!
        if (Args.empty())
          return Error(E);

        if (!EvaluateObjectArgument(Info, Args[0], ThisVal))
          return false;
        This = &ThisVal;
        Args = Args.slice(1);
      } else if (MD && MD->isLambdaStaticInvoker()) {
        // Map the static invoker for the lambda back to the call operator.
        // Conveniently, we don't have to slice out the 'this' argument (as is
        // being done for the non-static case), since a static member function
        // doesn't have an implicit argument passed in.
        const CXXRecordDecl *ClosureClass = MD->getParent();
        assert(
            ClosureClass->captures_begin() == ClosureClass->captures_end() &&
            "Number of captures must be zero for conversion to function-ptr");

        const CXXMethodDecl *LambdaCallOp =
            ClosureClass->getLambdaCallOperator();

        // Set 'FD', the function that will be called below, to the call
        // operator.  If the closure object represents a generic lambda, find
        // the corresponding specialization of the call operator.

        if (ClosureClass->isGenericLambda()) {
          assert(MD->isFunctionTemplateSpecialization() &&
                 "A generic lambda's static-invoker function must be a "
                 "template specialization");
          const TemplateArgumentList *TAL = MD->getTemplateSpecializationArgs();
          FunctionTemplateDecl *CallOpTemplate =
              LambdaCallOp->getDescribedFunctionTemplate();
          void *InsertPos = nullptr;
          FunctionDecl *CorrespondingCallOpSpecialization =
              CallOpTemplate->findSpecialization(TAL->asArray(), InsertPos);
          assert(CorrespondingCallOpSpecialization &&
                 "We must always have a function call operator specialization "
                 "that corresponds to our static invoker specialization");
          FD = cast<CXXMethodDecl>(CorrespondingCallOpSpecialization);
        } else
          FD = LambdaCallOp;
      }


    } else
      return Error(E);

    if (This && !This->checkSubobject(Info, E, CSK_This))
      return false;

    // DR1358 allows virtual constexpr functions in some cases. Don't allow
    // calls to such functions in constant expressions.
    if (This && !HasQualifier &&
        isa<CXXMethodDecl>(FD) && cast<CXXMethodDecl>(FD)->isVirtual())
      return Error(E, diag::note_constexpr_virtual_call);

    const FunctionDecl *Definition = nullptr;
    Stmt *Body = FD->getBody(Definition);

    if (!CheckConstexprFunction(Info, E->getExprLoc(), FD, Definition, Body) ||
        !HandleFunctionCall(E->getExprLoc(), Definition, This, Args, Body, Info,
                            Result, ResultSlot))
      return false;

    return true;
  }

  bool VisitCompoundLiteralExpr(const CompoundLiteralExpr *E) {
    return StmtVisitorTy::Visit(E->getInitializer());
  }
  bool VisitInitListExpr(const InitListExpr *E) {
    if (E->getNumInits() == 0)
      return DerivedZeroInitialization(E);
    if (E->getNumInits() == 1)
      return StmtVisitorTy::Visit(E->getInit(0));
    return Error(E);
  }
  bool VisitImplicitValueInitExpr(const ImplicitValueInitExpr *E) {
    return DerivedZeroInitialization(E);
  }
  bool VisitCXXScalarValueInitExpr(const CXXScalarValueInitExpr *E) {
    return DerivedZeroInitialization(E);
  }
  bool VisitCXXNullPtrLiteralExpr(const CXXNullPtrLiteralExpr *E) {
    return DerivedZeroInitialization(E);
  }

  /// A member expression where the object is a prvalue is itself a prvalue.
  bool VisitMemberExpr(const MemberExpr *E) {
    assert(!E->isArrow() && "missing call to bound member function?");

    APValue Val;
    if (!Evaluate(Val, Info, E->getBase()))
      return false;

    QualType BaseTy = E->getBase()->getType();

    const FieldDecl *FD = dyn_cast<FieldDecl>(E->getMemberDecl());
    if (!FD) return Error(E);
    assert(!FD->getType()->isReferenceType() && "prvalue reference?");
    assert(BaseTy->castAs<RecordType>()->getDecl()->getCanonicalDecl() ==
           FD->getParent()->getCanonicalDecl() && "record / field mismatch");

    CompleteObject Obj(&Val, BaseTy, true);
    SubobjectDesignator Designator(BaseTy);
    Designator.addDeclUnchecked(FD);

    APValue Result;
    return extractSubobject(Info, E, Obj, Designator, Result) &&
           DerivedSuccess(Result, E);
  }

  bool VisitCastExpr(const CastExpr *E) {
    switch (E->getCastKind()) {
    default:
      break;

    case CK_AtomicToNonAtomic: {
      APValue AtomicVal;
      // This does not need to be done in place even for class/array types:
      // atomic-to-non-atomic conversion implies copying the object
      // representation.
      if (!Evaluate(AtomicVal, Info, E->getSubExpr()))
        return false;
      return DerivedSuccess(AtomicVal, E);
    }

    case CK_NoOp:
    case CK_UserDefinedConversion:
      return StmtVisitorTy::Visit(E->getSubExpr());

    case CK_LValueToRValue: {
      LValue LVal;
      if (!EvaluateLValue(E->getSubExpr(), LVal, Info))
        return false;
      APValue RVal;
      // Note, we use the subexpression's type in order to retain cv-qualifiers.
      if (!handleLValueToRValueConversion(Info, E, E->getSubExpr()->getType(),
                                          LVal, RVal))
        return false;
      return DerivedSuccess(RVal, E);
    }
    }

    return Error(E);
  }

  bool VisitUnaryPostInc(const UnaryOperator *UO) {
    return VisitUnaryPostIncDec(UO);
  }
  bool VisitUnaryPostDec(const UnaryOperator *UO) {
    return VisitUnaryPostIncDec(UO);
  }
  bool VisitUnaryPostIncDec(const UnaryOperator *UO) {
    if (!Info.getLangOpts().CPlusPlus14 && !Info.keepEvaluatingAfterFailure())
      return Error(UO);

    LValue LVal;
    if (!EvaluateLValue(UO->getSubExpr(), LVal, Info))
      return false;
    APValue RVal;
    if (!handleIncDec(this->Info, UO, LVal, UO->getSubExpr()->getType(),
                      UO->isIncrementOp(), &RVal))
      return false;
    return DerivedSuccess(RVal, UO);
  }

  bool VisitStmtExpr(const StmtExpr *E) {
    // We will have checked the full-expressions inside the statement expression
    // when they were completed, and don't need to check them again now.
    if (Info.checkingForOverflow())
      return Error(E);

    BlockScopeRAII Scope(Info);
    const CompoundStmt *CS = E->getSubStmt();
    if (CS->body_empty())
      return true;

    for (CompoundStmt::const_body_iterator BI = CS->body_begin(),
                                           BE = CS->body_end();
         /**/; ++BI) {
      if (BI + 1 == BE) {
        const Expr *FinalExpr = dyn_cast<Expr>(*BI);
        if (!FinalExpr) {
          Info.FFDiag((*BI)->getBeginLoc(),
                      diag::note_constexpr_stmt_expr_unsupported);
          return false;
        }
        return this->Visit(FinalExpr);
      }

      APValue ReturnValue;
      StmtResult Result = { ReturnValue, nullptr };
      EvalStmtResult ESR = EvaluateStmt(Result, Info, *BI);
      if (ESR != ESR_Succeeded) {
        // FIXME: If the statement-expression terminated due to 'return',
        // 'break', or 'continue', it would be nice to propagate that to
        // the outer statement evaluation rather than bailing out.
        if (ESR != ESR_Failed)
          Info.FFDiag((*BI)->getBeginLoc(),
                      diag::note_constexpr_stmt_expr_unsupported);
        return false;
      }
    }

    llvm_unreachable("Return from function from the loop above.");
  }

  /// Visit a value which is evaluated, but whose value is ignored.
  void VisitIgnoredValue(const Expr *E) {
    EvaluateIgnoredValue(Info, E);
  }

  /// Potentially visit a MemberExpr's base expression.
  void VisitIgnoredBaseExpression(const Expr *E) {
    // While MSVC doesn't evaluate the base expression, it does diagnose the
    // presence of side-effecting behavior.
    if (Info.getLangOpts().MSVCCompat && !E->HasSideEffects(Info.Ctx))
      return;
    VisitIgnoredValue(E);
  }
};

} // namespace

//===----------------------------------------------------------------------===//
// Common base class for lvalue and temporary evaluation.
//===----------------------------------------------------------------------===//
namespace {
template<class Derived>
class LValueExprEvaluatorBase
  : public ExprEvaluatorBase<Derived> {
protected:
  LValue &Result;
  bool InvalidBaseOK;
  typedef LValueExprEvaluatorBase LValueExprEvaluatorBaseTy;
  typedef ExprEvaluatorBase<Derived> ExprEvaluatorBaseTy;

  bool Success(APValue::LValueBase B) {
    Result.set(B);
    return true;
  }

  bool evaluatePointer(const Expr *E, LValue &Result) {
    return EvaluatePointer(E, Result, this->Info, InvalidBaseOK);
  }

public:
  LValueExprEvaluatorBase(EvalInfo &Info, LValue &Result, bool InvalidBaseOK)
      : ExprEvaluatorBaseTy(Info), Result(Result),
        InvalidBaseOK(InvalidBaseOK) {}

  bool Success(const APValue &V, const Expr *E) {
    Result.setFrom(this->Info.Ctx, V);
    return true;
  }

  bool VisitMemberExpr(const MemberExpr *E) {
    // Handle non-static data members.
    QualType BaseTy;
    bool EvalOK;
    if (E->isArrow()) {
      EvalOK = evaluatePointer(E->getBase(), Result);
      BaseTy = E->getBase()->getType()->castAs<PointerType>()->getPointeeType();
    } else if (E->getBase()->isRValue()) {
      assert(E->getBase()->getType()->isRecordType());
      EvalOK = EvaluateTemporary(E->getBase(), Result, this->Info);
      BaseTy = E->getBase()->getType();
    } else {
      EvalOK = this->Visit(E->getBase());
      BaseTy = E->getBase()->getType();
    }
    if (!EvalOK) {
      if (!InvalidBaseOK)
        return false;
      Result.setInvalid(E);
      return true;
    }

    const ValueDecl *MD = E->getMemberDecl();
    if (const FieldDecl *FD = dyn_cast<FieldDecl>(E->getMemberDecl())) {
      assert(BaseTy->getAs<RecordType>()->getDecl()->getCanonicalDecl() ==
             FD->getParent()->getCanonicalDecl() && "record / field mismatch");
      (void)BaseTy;
      if (!HandleLValueMember(this->Info, E, Result, FD))
        return false;
    } else if (const IndirectFieldDecl *IFD = dyn_cast<IndirectFieldDecl>(MD)) {
      if (!HandleLValueIndirectMember(this->Info, E, Result, IFD))
        return false;
    } else
      return this->Error(E);

    if (MD->getType()->isReferenceType()) {
      APValue RefValue;
      if (!handleLValueToRValueConversion(this->Info, E, MD->getType(), Result,
                                          RefValue))
        return false;
      return Success(RefValue, E);
    }
    return true;
  }

  bool VisitBinaryOperator(const BinaryOperator *E) {
    switch (E->getOpcode()) {
    default:
      return ExprEvaluatorBaseTy::VisitBinaryOperator(E);

    case BO_PtrMemD:
    case BO_PtrMemI:
      return HandleMemberPointerAccess(this->Info, E, Result);
    }
  }

  bool VisitCastExpr(const CastExpr *E) {
    switch (E->getCastKind()) {
    default:
      return ExprEvaluatorBaseTy::VisitCastExpr(E);

    case CK_DerivedToBase:
    case CK_UncheckedDerivedToBase:
      if (!this->Visit(E->getSubExpr()))
        return false;

      // Now figure out the necessary offset to add to the base LV to get from
      // the derived class to the base class.
      return HandleLValueBasePath(this->Info, E, E->getSubExpr()->getType(),
                                  Result);
    }
  }
};
}

//===----------------------------------------------------------------------===//
// LValue Evaluation
//
// This is used for evaluating lvalues (in C and C++), xvalues (in C++11),
// function designators (in C), decl references to void objects (in C), and
// temporaries (if building with -Wno-address-of-temporary).
//
// LValue evaluation produces values comprising a base expression of one of the
// following types:
// - Declarations
//  * VarDecl
//  * FunctionDecl
// - Literals
//  * CompoundLiteralExpr in C (and in global scope in C++)
//  * StringLiteral
//  * CXXTypeidExpr
//  * PredefinedExpr
//  * ObjCStringLiteralExpr
//  * ObjCEncodeExpr
//  * AddrLabelExpr
//  * BlockExpr
//  * CallExpr for a MakeStringConstant builtin
// - Locals and temporaries
//  * MaterializeTemporaryExpr
//  * Any Expr, with a CallIndex indicating the function in which the temporary
//    was evaluated, for cases where the MaterializeTemporaryExpr is missing
//    from the AST (FIXME).
//  * A MaterializeTemporaryExpr that has static storage duration, with no
//    CallIndex, for a lifetime-extended temporary.
// plus an offset in bytes.
//===----------------------------------------------------------------------===//
namespace {
class LValueExprEvaluator
  : public LValueExprEvaluatorBase<LValueExprEvaluator> {
public:
  LValueExprEvaluator(EvalInfo &Info, LValue &Result, bool InvalidBaseOK) :
    LValueExprEvaluatorBaseTy(Info, Result, InvalidBaseOK) {}

  bool VisitVarDecl(const Expr *E, const VarDecl *VD);
  bool VisitUnaryPreIncDec(const UnaryOperator *UO);

  bool VisitDeclRefExpr(const DeclRefExpr *E);
  bool VisitPredefinedExpr(const PredefinedExpr *E) { return Success(E); }
  bool VisitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *E);
  bool VisitCompoundLiteralExpr(const CompoundLiteralExpr *E);
  bool VisitMemberExpr(const MemberExpr *E);
  bool VisitStringLiteral(const StringLiteral *E) { return Success(E); }
  bool VisitObjCEncodeExpr(const ObjCEncodeExpr *E) { return Success(E); }
  bool VisitCXXTypeidExpr(const CXXTypeidExpr *E);
  bool VisitCXXUuidofExpr(const CXXUuidofExpr *E);
  bool VisitArraySubscriptExpr(const ArraySubscriptExpr *E);
  bool VisitUnaryDeref(const UnaryOperator *E);
  bool VisitUnaryReal(const UnaryOperator *E);
  bool VisitUnaryImag(const UnaryOperator *E);
  bool VisitUnaryPreInc(const UnaryOperator *UO) {
    return VisitUnaryPreIncDec(UO);
  }
  bool VisitUnaryPreDec(const UnaryOperator *UO) {
    return VisitUnaryPreIncDec(UO);
  }
  bool VisitBinAssign(const BinaryOperator *BO);
  bool VisitCompoundAssignOperator(const CompoundAssignOperator *CAO);

  bool VisitCastExpr(const CastExpr *E) {
    switch (E->getCastKind()) {
    default:
      return LValueExprEvaluatorBaseTy::VisitCastExpr(E);

    case CK_LValueBitCast:
      this->CCEDiag(E, diag::note_constexpr_invalid_cast) << 2;
      if (!Visit(E->getSubExpr()))
        return false;
      Result.Designator.setInvalid();
      return true;

    case CK_BaseToDerived:
      if (!Visit(E->getSubExpr()))
        return false;
      return HandleBaseToDerivedCast(Info, E, Result);
    }
  }
};
} // end anonymous namespace

/// Evaluate an expression as an lvalue. This can be legitimately called on
/// expressions which are not glvalues, in three cases:
///  * function designators in C, and
///  * "extern void" objects
///  * @selector() expressions in Objective-C
static bool EvaluateLValue(const Expr *E, LValue &Result, EvalInfo &Info,
                           bool InvalidBaseOK) {
  assert(E->isGLValue() || E->getType()->isFunctionType() ||
         E->getType()->isVoidType() || isa<ObjCSelectorExpr>(E));
  return LValueExprEvaluator(Info, Result, InvalidBaseOK).Visit(E);
}

bool LValueExprEvaluator::VisitDeclRefExpr(const DeclRefExpr *E) {
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(E->getDecl()))
    return Success(FD);
  if (const VarDecl *VD = dyn_cast<VarDecl>(E->getDecl()))
    return VisitVarDecl(E, VD);
  if (const BindingDecl *BD = dyn_cast<BindingDecl>(E->getDecl()))
    return Visit(BD->getBinding());
  return Error(E);
}


bool LValueExprEvaluator::VisitVarDecl(const Expr *E, const VarDecl *VD) {

  // If we are within a lambda's call operator, check whether the 'VD' referred
  // to within 'E' actually represents a lambda-capture that maps to a
  // data-member/field within the closure object, and if so, evaluate to the
  // field or what the field refers to.
  if (Info.CurrentCall && isLambdaCallOperator(Info.CurrentCall->Callee) &&
      isa<DeclRefExpr>(E) &&
      cast<DeclRefExpr>(E)->refersToEnclosingVariableOrCapture()) {
    // We don't always have a complete capture-map when checking or inferring if
    // the function call operator meets the requirements of a constexpr function
    // - but we don't need to evaluate the captures to determine constexprness
    // (dcl.constexpr C++17).
    if (Info.checkingPotentialConstantExpression())
      return false;

    if (auto *FD = Info.CurrentCall->LambdaCaptureFields.lookup(VD)) {
      // Start with 'Result' referring to the complete closure object...
      Result = *Info.CurrentCall->This;
      // ... then update it to refer to the field of the closure object
      // that represents the capture.
      if (!HandleLValueMember(Info, E, Result, FD))
        return false;
      // And if the field is of reference type, update 'Result' to refer to what
      // the field refers to.
      if (FD->getType()->isReferenceType()) {
        APValue RVal;
        if (!handleLValueToRValueConversion(Info, E, FD->getType(), Result,
                                            RVal))
          return false;
        Result.setFrom(Info.Ctx, RVal);
      }
      return true;
    }
  }
  CallStackFrame *Frame = nullptr;
  if (VD->hasLocalStorage() && Info.CurrentCall->Index > 1) {
    // Only if a local variable was declared in the function currently being
    // evaluated, do we expect to be able to find its value in the current
    // frame. (Otherwise it was likely declared in an enclosing context and
    // could either have a valid evaluatable value (for e.g. a constexpr
    // variable) or be ill-formed (and trigger an appropriate evaluation
    // diagnostic)).
    if (Info.CurrentCall->Callee &&
        Info.CurrentCall->Callee->Equals(VD->getDeclContext())) {
      Frame = Info.CurrentCall;
    }
  }

  if (!VD->getType()->isReferenceType()) {
    if (Frame) {
      Result.set({VD, Frame->Index,
                  Info.CurrentCall->getCurrentTemporaryVersion(VD)});
      return true;
    }
    return Success(VD);
  }

  APValue *V;
  if (!evaluateVarDeclInit(Info, E, VD, Frame, V, nullptr))
    return false;
  if (V->isUninit()) {
    if (!Info.checkingPotentialConstantExpression())
      Info.FFDiag(E, diag::note_constexpr_use_uninit_reference);
    return false;
  }
  return Success(*V, E);
}

bool LValueExprEvaluator::VisitMaterializeTemporaryExpr(
    const MaterializeTemporaryExpr *E) {
  // Walk through the expression to find the materialized temporary itself.
  SmallVector<const Expr *, 2> CommaLHSs;
  SmallVector<SubobjectAdjustment, 2> Adjustments;
  const Expr *Inner = E->GetTemporaryExpr()->
      skipRValueSubobjectAdjustments(CommaLHSs, Adjustments);

  // If we passed any comma operators, evaluate their LHSs.
  for (unsigned I = 0, N = CommaLHSs.size(); I != N; ++I)
    if (!EvaluateIgnoredValue(Info, CommaLHSs[I]))
      return false;

  // A materialized temporary with static storage duration can appear within the
  // result of a constant expression evaluation, so we need to preserve its
  // value for use outside this evaluation.
  APValue *Value;
  if (E->getStorageDuration() == SD_Static) {
    Value = Info.Ctx.getMaterializedTemporaryValue(E, true);
    *Value = APValue();
    Result.set(E);
  } else {
    Value = &createTemporary(E, E->getStorageDuration() == SD_Automatic, Result,
                             *Info.CurrentCall);
  }

  QualType Type = Inner->getType();

  // Materialize the temporary itself.
  if (!EvaluateInPlace(*Value, Info, Result, Inner) ||
      (E->getStorageDuration() == SD_Static &&
       !CheckConstantExpression(Info, E->getExprLoc(), Type, *Value))) {
    *Value = APValue();
    return false;
  }

  // Adjust our lvalue to refer to the desired subobject.
  for (unsigned I = Adjustments.size(); I != 0; /**/) {
    --I;
    switch (Adjustments[I].Kind) {
    case SubobjectAdjustment::DerivedToBaseAdjustment:
      if (!HandleLValueBasePath(Info, Adjustments[I].DerivedToBase.BasePath,
                                Type, Result))
        return false;
      Type = Adjustments[I].DerivedToBase.BasePath->getType();
      break;

    case SubobjectAdjustment::FieldAdjustment:
      if (!HandleLValueMember(Info, E, Result, Adjustments[I].Field))
        return false;
      Type = Adjustments[I].Field->getType();
      break;

    case SubobjectAdjustment::MemberPointerAdjustment:
      if (!HandleMemberPointerAccess(this->Info, Type, Result,
                                     Adjustments[I].Ptr.RHS))
        return false;
      Type = Adjustments[I].Ptr.MPT->getPointeeType();
      break;
    }
  }

  return true;
}

bool
LValueExprEvaluator::VisitCompoundLiteralExpr(const CompoundLiteralExpr *E) {
  assert((!Info.getLangOpts().CPlusPlus || E->isFileScope()) &&
         "lvalue compound literal in c++?");
  // Defer visiting the literal until the lvalue-to-rvalue conversion. We can
  // only see this when folding in C, so there's no standard to follow here.
  return Success(E);
}

bool LValueExprEvaluator::VisitCXXTypeidExpr(const CXXTypeidExpr *E) {
  if (!E->isPotentiallyEvaluated())
    return Success(E);

  Info.FFDiag(E, diag::note_constexpr_typeid_polymorphic)
    << E->getExprOperand()->getType()
    << E->getExprOperand()->getSourceRange();
  return false;
}

bool LValueExprEvaluator::VisitCXXUuidofExpr(const CXXUuidofExpr *E) {
  return Success(E);
}

bool LValueExprEvaluator::VisitMemberExpr(const MemberExpr *E) {
  // Handle static data members.
  if (const VarDecl *VD = dyn_cast<VarDecl>(E->getMemberDecl())) {
    VisitIgnoredBaseExpression(E->getBase());
    return VisitVarDecl(E, VD);
  }

  // Handle static member functions.
  if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(E->getMemberDecl())) {
    if (MD->isStatic()) {
      VisitIgnoredBaseExpression(E->getBase());
      return Success(MD);
    }
  }

  // Handle non-static data members.
  return LValueExprEvaluatorBaseTy::VisitMemberExpr(E);
}

bool LValueExprEvaluator::VisitArraySubscriptExpr(const ArraySubscriptExpr *E) {
  // FIXME: Deal with vectors as array subscript bases.
  if (E->getBase()->getType()->isVectorType())
    return Error(E);

  bool Success = true;
  if (!evaluatePointer(E->getBase(), Result)) {
    if (!Info.noteFailure())
      return false;
    Success = false;
  }

  APSInt Index;
  if (!EvaluateInteger(E->getIdx(), Index, Info))
    return false;

  return Success &&
         HandleLValueArrayAdjustment(Info, E, Result, E->getType(), Index);
}

bool LValueExprEvaluator::VisitUnaryDeref(const UnaryOperator *E) {
  return evaluatePointer(E->getSubExpr(), Result);
}

bool LValueExprEvaluator::VisitUnaryReal(const UnaryOperator *E) {
  if (!Visit(E->getSubExpr()))
    return false;
  // __real is a no-op on scalar lvalues.
  if (E->getSubExpr()->getType()->isAnyComplexType())
    HandleLValueComplexElement(Info, E, Result, E->getType(), false);
  return true;
}

bool LValueExprEvaluator::VisitUnaryImag(const UnaryOperator *E) {
  assert(E->getSubExpr()->getType()->isAnyComplexType() &&
         "lvalue __imag__ on scalar?");
  if (!Visit(E->getSubExpr()))
    return false;
  HandleLValueComplexElement(Info, E, Result, E->getType(), true);
  return true;
}

bool LValueExprEvaluator::VisitUnaryPreIncDec(const UnaryOperator *UO) {
  if (!Info.getLangOpts().CPlusPlus14 && !Info.keepEvaluatingAfterFailure())
    return Error(UO);

  if (!this->Visit(UO->getSubExpr()))
    return false;

  return handleIncDec(
      this->Info, UO, Result, UO->getSubExpr()->getType(),
      UO->isIncrementOp(), nullptr);
}

bool LValueExprEvaluator::VisitCompoundAssignOperator(
    const CompoundAssignOperator *CAO) {
  if (!Info.getLangOpts().CPlusPlus14 && !Info.keepEvaluatingAfterFailure())
    return Error(CAO);

  APValue RHS;

  // The overall lvalue result is the result of evaluating the LHS.
  if (!this->Visit(CAO->getLHS())) {
    if (Info.noteFailure())
      Evaluate(RHS, this->Info, CAO->getRHS());
    return false;
  }

  if (!Evaluate(RHS, this->Info, CAO->getRHS()))
    return false;

  return handleCompoundAssignment(
      this->Info, CAO,
      Result, CAO->getLHS()->getType(), CAO->getComputationLHSType(),
      CAO->getOpForCompoundAssignment(CAO->getOpcode()), RHS);
}

bool LValueExprEvaluator::VisitBinAssign(const BinaryOperator *E) {
  if (!Info.getLangOpts().CPlusPlus14 && !Info.keepEvaluatingAfterFailure())
    return Error(E);

  APValue NewVal;

  if (!this->Visit(E->getLHS())) {
    if (Info.noteFailure())
      Evaluate(NewVal, this->Info, E->getRHS());
    return false;
  }

  if (!Evaluate(NewVal, this->Info, E->getRHS()))
    return false;

  return handleAssignment(this->Info, E, Result, E->getLHS()->getType(),
                          NewVal);
}

//===----------------------------------------------------------------------===//
// Pointer Evaluation
//===----------------------------------------------------------------------===//

/// Attempts to compute the number of bytes available at the pointer
/// returned by a function with the alloc_size attribute. Returns true if we
/// were successful. Places an unsigned number into `Result`.
///
/// This expects the given CallExpr to be a call to a function with an
/// alloc_size attribute.
static bool getBytesReturnedByAllocSizeCall(const ASTContext &Ctx,
                                            const CallExpr *Call,
                                            llvm::APInt &Result) {
  const AllocSizeAttr *AllocSize = getAllocSizeAttr(Call);

  assert(AllocSize && AllocSize->getElemSizeParam().isValid());
  unsigned SizeArgNo = AllocSize->getElemSizeParam().getASTIndex();
  unsigned BitsInSizeT = Ctx.getTypeSize(Ctx.getSizeType());
  if (Call->getNumArgs() <= SizeArgNo)
    return false;

  auto EvaluateAsSizeT = [&](const Expr *E, APSInt &Into) {
    Expr::EvalResult ExprResult;
    if (!E->EvaluateAsInt(ExprResult, Ctx, Expr::SE_AllowSideEffects))
      return false;
    Into = ExprResult.Val.getInt();
    if (Into.isNegative() || !Into.isIntN(BitsInSizeT))
      return false;
    Into = Into.zextOrSelf(BitsInSizeT);
    return true;
  };

  APSInt SizeOfElem;
  if (!EvaluateAsSizeT(Call->getArg(SizeArgNo), SizeOfElem))
    return false;

  if (!AllocSize->getNumElemsParam().isValid()) {
    Result = std::move(SizeOfElem);
    return true;
  }

  APSInt NumberOfElems;
  unsigned NumArgNo = AllocSize->getNumElemsParam().getASTIndex();
  if (!EvaluateAsSizeT(Call->getArg(NumArgNo), NumberOfElems))
    return false;

  bool Overflow;
  llvm::APInt BytesAvailable = SizeOfElem.umul_ov(NumberOfElems, Overflow);
  if (Overflow)
    return false;

  Result = std::move(BytesAvailable);
  return true;
}

/// Convenience function. LVal's base must be a call to an alloc_size
/// function.
static bool getBytesReturnedByAllocSizeCall(const ASTContext &Ctx,
                                            const LValue &LVal,
                                            llvm::APInt &Result) {
  assert(isBaseAnAllocSizeCall(LVal.getLValueBase()) &&
         "Can't get the size of a non alloc_size function");
  const auto *Base = LVal.getLValueBase().get<const Expr *>();
  const CallExpr *CE = tryUnwrapAllocSizeCall(Base);
  return getBytesReturnedByAllocSizeCall(Ctx, CE, Result);
}

/// Attempts to evaluate the given LValueBase as the result of a call to
/// a function with the alloc_size attribute. If it was possible to do so, this
/// function will return true, make Result's Base point to said function call,
/// and mark Result's Base as invalid.
static bool evaluateLValueAsAllocSize(EvalInfo &Info, APValue::LValueBase Base,
                                      LValue &Result) {
  if (Base.isNull())
    return false;

  // Because we do no form of static analysis, we only support const variables.
  //
  // Additionally, we can't support parameters, nor can we support static
  // variables (in the latter case, use-before-assign isn't UB; in the former,
  // we have no clue what they'll be assigned to).
  const auto *VD =
      dyn_cast_or_null<VarDecl>(Base.dyn_cast<const ValueDecl *>());
  if (!VD || !VD->isLocalVarDecl() || !VD->getType().isConstQualified())
    return false;

  const Expr *Init = VD->getAnyInitializer();
  if (!Init)
    return false;

  const Expr *E = Init->IgnoreParens();
  if (!tryUnwrapAllocSizeCall(E))
    return false;

  // Store E instead of E unwrapped so that the type of the LValue's base is
  // what the user wanted.
  Result.setInvalid(E);

  QualType Pointee = E->getType()->castAs<PointerType>()->getPointeeType();
  Result.addUnsizedArray(Info, E, Pointee);
  return true;
}

namespace {
class PointerExprEvaluator
  : public ExprEvaluatorBase<PointerExprEvaluator> {
  LValue &Result;
  bool InvalidBaseOK;

  bool Success(const Expr *E) {
    Result.set(E);
    return true;
  }

  bool evaluateLValue(const Expr *E, LValue &Result) {
    return EvaluateLValue(E, Result, Info, InvalidBaseOK);
  }

  bool evaluatePointer(const Expr *E, LValue &Result) {
    return EvaluatePointer(E, Result, Info, InvalidBaseOK);
  }

  bool visitNonBuiltinCallExpr(const CallExpr *E);
public:

  PointerExprEvaluator(EvalInfo &info, LValue &Result, bool InvalidBaseOK)
      : ExprEvaluatorBaseTy(info), Result(Result),
        InvalidBaseOK(InvalidBaseOK) {}

  bool Success(const APValue &V, const Expr *E) {
    Result.setFrom(Info.Ctx, V);
    return true;
  }
  bool ZeroInitialization(const Expr *E) {
    auto TargetVal = Info.Ctx.getTargetNullPointerValue(E->getType());
    Result.setNull(E->getType(), TargetVal);
    return true;
  }

  bool VisitBinaryOperator(const BinaryOperator *E);
  bool VisitCastExpr(const CastExpr* E);
  bool VisitUnaryAddrOf(const UnaryOperator *E);
  bool VisitObjCStringLiteral(const ObjCStringLiteral *E)
      { return Success(E); }
  bool VisitObjCBoxedExpr(const ObjCBoxedExpr *E) {
    if (Info.noteFailure())
      EvaluateIgnoredValue(Info, E->getSubExpr());
    return Error(E);
  }
  bool VisitAddrLabelExpr(const AddrLabelExpr *E)
      { return Success(E); }
  bool VisitCallExpr(const CallExpr *E);
  bool VisitBuiltinCallExpr(const CallExpr *E, unsigned BuiltinOp);
  bool VisitBlockExpr(const BlockExpr *E) {
    if (!E->getBlockDecl()->hasCaptures())
      return Success(E);
    return Error(E);
  }
  bool VisitCXXThisExpr(const CXXThisExpr *E) {
    // Can't look at 'this' when checking a potential constant expression.
    if (Info.checkingPotentialConstantExpression())
      return false;
    if (!Info.CurrentCall->This) {
      if (Info.getLangOpts().CPlusPlus11)
        Info.FFDiag(E, diag::note_constexpr_this) << E->isImplicit();
      else
        Info.FFDiag(E);
      return false;
    }
    Result = *Info.CurrentCall->This;
    // If we are inside a lambda's call operator, the 'this' expression refers
    // to the enclosing '*this' object (either by value or reference) which is
    // either copied into the closure object's field that represents the '*this'
    // or refers to '*this'.
    if (isLambdaCallOperator(Info.CurrentCall->Callee)) {
      // Update 'Result' to refer to the data member/field of the closure object
      // that represents the '*this' capture.
      if (!HandleLValueMember(Info, E, Result,
                             Info.CurrentCall->LambdaThisCaptureField))
        return false;
      // If we captured '*this' by reference, replace the field with its referent.
      if (Info.CurrentCall->LambdaThisCaptureField->getType()
              ->isPointerType()) {
        APValue RVal;
        if (!handleLValueToRValueConversion(Info, E, E->getType(), Result,
                                            RVal))
          return false;

        Result.setFrom(Info.Ctx, RVal);
      }
    }
    return true;
  }

  // FIXME: Missing: @protocol, @selector
};
} // end anonymous namespace

static bool EvaluatePointer(const Expr* E, LValue& Result, EvalInfo &Info,
                            bool InvalidBaseOK) {
  assert(E->isRValue() && E->getType()->hasPointerRepresentation());
  return PointerExprEvaluator(Info, Result, InvalidBaseOK).Visit(E);
}

bool PointerExprEvaluator::VisitBinaryOperator(const BinaryOperator *E) {
  if (E->getOpcode() != BO_Add &&
      E->getOpcode() != BO_Sub)
    return ExprEvaluatorBaseTy::VisitBinaryOperator(E);

  const Expr *PExp = E->getLHS();
  const Expr *IExp = E->getRHS();
  if (IExp->getType()->isPointerType())
    std::swap(PExp, IExp);

  bool EvalPtrOK = evaluatePointer(PExp, Result);
  if (!EvalPtrOK && !Info.noteFailure())
    return false;

  llvm::APSInt Offset;
  if (!EvaluateInteger(IExp, Offset, Info) || !EvalPtrOK)
    return false;

  if (E->getOpcode() == BO_Sub)
    negateAsSigned(Offset);

  QualType Pointee = PExp->getType()->castAs<PointerType>()->getPointeeType();
  return HandleLValueArrayAdjustment(Info, E, Result, Pointee, Offset);
}

bool PointerExprEvaluator::VisitUnaryAddrOf(const UnaryOperator *E) {
  return evaluateLValue(E->getSubExpr(), Result);
}

bool PointerExprEvaluator::VisitCastExpr(const CastExpr *E) {
  const Expr *SubExpr = E->getSubExpr();

  switch (E->getCastKind()) {
  default:
    break;

  case CK_BitCast:
  case CK_CPointerToObjCPointerCast:
  case CK_BlockPointerToObjCPointerCast:
  case CK_AnyPointerToBlockPointerCast:
  case CK_AddressSpaceConversion:
    if (!Visit(SubExpr))
      return false;
    // Bitcasts to cv void* are static_casts, not reinterpret_casts, so are
    // permitted in constant expressions in C++11. Bitcasts from cv void* are
    // also static_casts, but we disallow them as a resolution to DR1312.
    if (!E->getType()->isVoidPointerType()) {
      Result.Designator.setInvalid();
      if (SubExpr->getType()->isVoidPointerType())
        CCEDiag(E, diag::note_constexpr_invalid_cast)
          << 3 << SubExpr->getType();
      else
        CCEDiag(E, diag::note_constexpr_invalid_cast) << 2;
    }
    if (E->getCastKind() == CK_AddressSpaceConversion && Result.IsNullPtr)
      ZeroInitialization(E);
    return true;

  case CK_DerivedToBase:
  case CK_UncheckedDerivedToBase:
    if (!evaluatePointer(E->getSubExpr(), Result))
      return false;
    if (!Result.Base && Result.Offset.isZero())
      return true;

    // Now figure out the necessary offset to add to the base LV to get from
    // the derived class to the base class.
    return HandleLValueBasePath(Info, E, E->getSubExpr()->getType()->
                                  castAs<PointerType>()->getPointeeType(),
                                Result);

  case CK_BaseToDerived:
    if (!Visit(E->getSubExpr()))
      return false;
    if (!Result.Base && Result.Offset.isZero())
      return true;
    return HandleBaseToDerivedCast(Info, E, Result);

  case CK_NullToPointer:
    VisitIgnoredValue(E->getSubExpr());
    return ZeroInitialization(E);

  case CK_IntegralToPointer: {
    CCEDiag(E, diag::note_constexpr_invalid_cast) << 2;

    APValue Value;
    if (!EvaluateIntegerOrLValue(SubExpr, Value, Info))
      break;

    if (Value.isInt()) {
      unsigned Size = Info.Ctx.getTypeSize(E->getType());
      uint64_t N = Value.getInt().extOrTrunc(Size).getZExtValue();
      Result.Base = (Expr*)nullptr;
      Result.InvalidBase = false;
      Result.Offset = CharUnits::fromQuantity(N);
      Result.Designator.setInvalid();
      Result.IsNullPtr = false;
      return true;
    } else {
      // Cast is of an lvalue, no need to change value.
      Result.setFrom(Info.Ctx, Value);
      return true;
    }
  }

  case CK_ArrayToPointerDecay: {
    if (SubExpr->isGLValue()) {
      if (!evaluateLValue(SubExpr, Result))
        return false;
    } else {
      APValue &Value = createTemporary(SubExpr, false, Result,
                                       *Info.CurrentCall);
      if (!EvaluateInPlace(Value, Info, Result, SubExpr))
        return false;
    }
    // The result is a pointer to the first element of the array.
    auto *AT = Info.Ctx.getAsArrayType(SubExpr->getType());
    if (auto *CAT = dyn_cast<ConstantArrayType>(AT))
      Result.addArray(Info, E, CAT);
    else
      Result.addUnsizedArray(Info, E, AT->getElementType());
    return true;
  }

  case CK_FunctionToPointerDecay:
    return evaluateLValue(SubExpr, Result);

  case CK_LValueToRValue: {
    LValue LVal;
    if (!evaluateLValue(E->getSubExpr(), LVal))
      return false;

    APValue RVal;
    // Note, we use the subexpression's type in order to retain cv-qualifiers.
    if (!handleLValueToRValueConversion(Info, E, E->getSubExpr()->getType(),
                                        LVal, RVal))
      return InvalidBaseOK &&
             evaluateLValueAsAllocSize(Info, LVal.Base, Result);
    return Success(RVal, E);
  }
  }

  return ExprEvaluatorBaseTy::VisitCastExpr(E);
}

static CharUnits GetAlignOfType(EvalInfo &Info, QualType T,
                                UnaryExprOrTypeTrait ExprKind) {
  // C++ [expr.alignof]p3:
  //     When alignof is applied to a reference type, the result is the
  //     alignment of the referenced type.
  if (const ReferenceType *Ref = T->getAs<ReferenceType>())
    T = Ref->getPointeeType();

  if (T.getQualifiers().hasUnaligned())
    return CharUnits::One();

  const bool AlignOfReturnsPreferred =
      Info.Ctx.getLangOpts().getClangABICompat() <= LangOptions::ClangABI::Ver7;

  // __alignof is defined to return the preferred alignment.
  // Before 8, clang returned the preferred alignment for alignof and _Alignof
  // as well.
  if (ExprKind == UETT_PreferredAlignOf || AlignOfReturnsPreferred)
    return Info.Ctx.toCharUnitsFromBits(
      Info.Ctx.getPreferredTypeAlign(T.getTypePtr()));
  // alignof and _Alignof are defined to return the ABI alignment.
  else if (ExprKind == UETT_AlignOf)
    return Info.Ctx.getTypeAlignInChars(T.getTypePtr());
  else
    llvm_unreachable("GetAlignOfType on a non-alignment ExprKind");
}

static CharUnits GetAlignOfExpr(EvalInfo &Info, const Expr *E,
                                UnaryExprOrTypeTrait ExprKind) {
  E = E->IgnoreParens();

  // The kinds of expressions that we have special-case logic here for
  // should be kept up to date with the special checks for those
  // expressions in Sema.

  // alignof decl is always accepted, even if it doesn't make sense: we default
  // to 1 in those cases.
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    return Info.Ctx.getDeclAlign(DRE->getDecl(),
                                 /*RefAsPointee*/true);

  if (const MemberExpr *ME = dyn_cast<MemberExpr>(E))
    return Info.Ctx.getDeclAlign(ME->getMemberDecl(),
                                 /*RefAsPointee*/true);

  return GetAlignOfType(Info, E->getType(), ExprKind);
}

// To be clear: this happily visits unsupported builtins. Better name welcomed.
bool PointerExprEvaluator::visitNonBuiltinCallExpr(const CallExpr *E) {
  if (ExprEvaluatorBaseTy::VisitCallExpr(E))
    return true;

  if (!(InvalidBaseOK && getAllocSizeAttr(E)))
    return false;

  Result.setInvalid(E);
  QualType PointeeTy = E->getType()->castAs<PointerType>()->getPointeeType();
  Result.addUnsizedArray(Info, E, PointeeTy);
  return true;
}

bool PointerExprEvaluator::VisitCallExpr(const CallExpr *E) {
  if (IsStringLiteralCall(E))
    return Success(E);

  if (unsigned BuiltinOp = E->getBuiltinCallee())
    return VisitBuiltinCallExpr(E, BuiltinOp);

  return visitNonBuiltinCallExpr(E);
}

bool PointerExprEvaluator::VisitBuiltinCallExpr(const CallExpr *E,
                                                unsigned BuiltinOp) {
  switch (BuiltinOp) {
  case Builtin::BI__builtin_addressof:
    return evaluateLValue(E->getArg(0), Result);
  case Builtin::BI__builtin_assume_aligned: {
    // We need to be very careful here because: if the pointer does not have the
    // asserted alignment, then the behavior is undefined, and undefined
    // behavior is non-constant.
    if (!evaluatePointer(E->getArg(0), Result))
      return false;

    LValue OffsetResult(Result);
    APSInt Alignment;
    if (!EvaluateInteger(E->getArg(1), Alignment, Info))
      return false;
    CharUnits Align = CharUnits::fromQuantity(Alignment.getZExtValue());

    if (E->getNumArgs() > 2) {
      APSInt Offset;
      if (!EvaluateInteger(E->getArg(2), Offset, Info))
        return false;

      int64_t AdditionalOffset = -Offset.getZExtValue();
      OffsetResult.Offset += CharUnits::fromQuantity(AdditionalOffset);
    }

    // If there is a base object, then it must have the correct alignment.
    if (OffsetResult.Base) {
      CharUnits BaseAlignment;
      if (const ValueDecl *VD =
          OffsetResult.Base.dyn_cast<const ValueDecl*>()) {
        BaseAlignment = Info.Ctx.getDeclAlign(VD);
      } else {
        BaseAlignment = GetAlignOfExpr(
            Info, OffsetResult.Base.get<const Expr *>(), UETT_AlignOf);
      }

      if (BaseAlignment < Align) {
        Result.Designator.setInvalid();
        // FIXME: Add support to Diagnostic for long / long long.
        CCEDiag(E->getArg(0),
                diag::note_constexpr_baa_insufficient_alignment) << 0
          << (unsigned)BaseAlignment.getQuantity()
          << (unsigned)Align.getQuantity();
        return false;
      }
    }

    // The offset must also have the correct alignment.
    if (OffsetResult.Offset.alignTo(Align) != OffsetResult.Offset) {
      Result.Designator.setInvalid();

      (OffsetResult.Base
           ? CCEDiag(E->getArg(0),
                     diag::note_constexpr_baa_insufficient_alignment) << 1
           : CCEDiag(E->getArg(0),
                     diag::note_constexpr_baa_value_insufficient_alignment))
        << (int)OffsetResult.Offset.getQuantity()
        << (unsigned)Align.getQuantity();
      return false;
    }

    return true;
  }
  case Builtin::BI__builtin_launder:
    return evaluatePointer(E->getArg(0), Result);
  case Builtin::BIstrchr:
  case Builtin::BIwcschr:
  case Builtin::BImemchr:
  case Builtin::BIwmemchr:
    if (Info.getLangOpts().CPlusPlus11)
      Info.CCEDiag(E, diag::note_constexpr_invalid_function)
        << /*isConstexpr*/0 << /*isConstructor*/0
        << (std::string("'") + Info.Ctx.BuiltinInfo.getName(BuiltinOp) + "'");
    else
      Info.CCEDiag(E, diag::note_invalid_subexpr_in_const_expr);
    LLVM_FALLTHROUGH;
  case Builtin::BI__builtin_strchr:
  case Builtin::BI__builtin_wcschr:
  case Builtin::BI__builtin_memchr:
  case Builtin::BI__builtin_char_memchr:
  case Builtin::BI__builtin_wmemchr: {
    if (!Visit(E->getArg(0)))
      return false;
    APSInt Desired;
    if (!EvaluateInteger(E->getArg(1), Desired, Info))
      return false;
    uint64_t MaxLength = uint64_t(-1);
    if (BuiltinOp != Builtin::BIstrchr &&
        BuiltinOp != Builtin::BIwcschr &&
        BuiltinOp != Builtin::BI__builtin_strchr &&
        BuiltinOp != Builtin::BI__builtin_wcschr) {
      APSInt N;
      if (!EvaluateInteger(E->getArg(2), N, Info))
        return false;
      MaxLength = N.getExtValue();
    }
    // We cannot find the value if there are no candidates to match against.
    if (MaxLength == 0u)
      return ZeroInitialization(E);
    if (!Result.checkNullPointerForFoldAccess(Info, E, AK_Read) ||
        Result.Designator.Invalid)
      return false;
    QualType CharTy = Result.Designator.getType(Info.Ctx);
    bool IsRawByte = BuiltinOp == Builtin::BImemchr ||
                     BuiltinOp == Builtin::BI__builtin_memchr;
    assert(IsRawByte ||
           Info.Ctx.hasSameUnqualifiedType(
               CharTy, E->getArg(0)->getType()->getPointeeType()));
    // Pointers to const void may point to objects of incomplete type.
    if (IsRawByte && CharTy->isIncompleteType()) {
      Info.FFDiag(E, diag::note_constexpr_ltor_incomplete_type) << CharTy;
      return false;
    }
    // Give up on byte-oriented matching against multibyte elements.
    // FIXME: We can compare the bytes in the correct order.
    if (IsRawByte && Info.Ctx.getTypeSizeInChars(CharTy) != CharUnits::One())
      return false;
    // Figure out what value we're actually looking for (after converting to
    // the corresponding unsigned type if necessary).
    uint64_t DesiredVal;
    bool StopAtNull = false;
    switch (BuiltinOp) {
    case Builtin::BIstrchr:
    case Builtin::BI__builtin_strchr:
      // strchr compares directly to the passed integer, and therefore
      // always fails if given an int that is not a char.
      if (!APSInt::isSameValue(HandleIntToIntCast(Info, E, CharTy,
                                                  E->getArg(1)->getType(),
                                                  Desired),
                               Desired))
        return ZeroInitialization(E);
      StopAtNull = true;
      LLVM_FALLTHROUGH;
    case Builtin::BImemchr:
    case Builtin::BI__builtin_memchr:
    case Builtin::BI__builtin_char_memchr:
      // memchr compares by converting both sides to unsigned char. That's also
      // correct for strchr if we get this far (to cope with plain char being
      // unsigned in the strchr case).
      DesiredVal = Desired.trunc(Info.Ctx.getCharWidth()).getZExtValue();
      break;

    case Builtin::BIwcschr:
    case Builtin::BI__builtin_wcschr:
      StopAtNull = true;
      LLVM_FALLTHROUGH;
    case Builtin::BIwmemchr:
    case Builtin::BI__builtin_wmemchr:
      // wcschr and wmemchr are given a wchar_t to look for. Just use it.
      DesiredVal = Desired.getZExtValue();
      break;
    }

    for (; MaxLength; --MaxLength) {
      APValue Char;
      if (!handleLValueToRValueConversion(Info, E, CharTy, Result, Char) ||
          !Char.isInt())
        return false;
      if (Char.getInt().getZExtValue() == DesiredVal)
        return true;
      if (StopAtNull && !Char.getInt())
        break;
      if (!HandleLValueArrayAdjustment(Info, E, Result, CharTy, 1))
        return false;
    }
    // Not found: return nullptr.
    return ZeroInitialization(E);
  }

  case Builtin::BImemcpy:
  case Builtin::BImemmove:
  case Builtin::BIwmemcpy:
  case Builtin::BIwmemmove:
    if (Info.getLangOpts().CPlusPlus11)
      Info.CCEDiag(E, diag::note_constexpr_invalid_function)
        << /*isConstexpr*/0 << /*isConstructor*/0
        << (std::string("'") + Info.Ctx.BuiltinInfo.getName(BuiltinOp) + "'");
    else
      Info.CCEDiag(E, diag::note_invalid_subexpr_in_const_expr);
    LLVM_FALLTHROUGH;
  case Builtin::BI__builtin_memcpy:
  case Builtin::BI__builtin_memmove:
  case Builtin::BI__builtin_wmemcpy:
  case Builtin::BI__builtin_wmemmove: {
    bool WChar = BuiltinOp == Builtin::BIwmemcpy ||
                 BuiltinOp == Builtin::BIwmemmove ||
                 BuiltinOp == Builtin::BI__builtin_wmemcpy ||
                 BuiltinOp == Builtin::BI__builtin_wmemmove;
    bool Move = BuiltinOp == Builtin::BImemmove ||
                BuiltinOp == Builtin::BIwmemmove ||
                BuiltinOp == Builtin::BI__builtin_memmove ||
                BuiltinOp == Builtin::BI__builtin_wmemmove;

    // The result of mem* is the first argument.
    if (!Visit(E->getArg(0)))
      return false;
    LValue Dest = Result;

    LValue Src;
    if (!EvaluatePointer(E->getArg(1), Src, Info))
      return false;

    APSInt N;
    if (!EvaluateInteger(E->getArg(2), N, Info))
      return false;
    assert(!N.isSigned() && "memcpy and friends take an unsigned size");

    // If the size is zero, we treat this as always being a valid no-op.
    // (Even if one of the src and dest pointers is null.)
    if (!N)
      return true;

    // Otherwise, if either of the operands is null, we can't proceed. Don't
    // try to determine the type of the copied objects, because there aren't
    // any.
    if (!Src.Base || !Dest.Base) {
      APValue Val;
      (!Src.Base ? Src : Dest).moveInto(Val);
      Info.FFDiag(E, diag::note_constexpr_memcpy_null)
          << Move << WChar << !!Src.Base
          << Val.getAsString(Info.Ctx, E->getArg(0)->getType());
      return false;
    }
    if (Src.Designator.Invalid || Dest.Designator.Invalid)
      return false;

    // We require that Src and Dest are both pointers to arrays of
    // trivially-copyable type. (For the wide version, the designator will be
    // invalid if the designated object is not a wchar_t.)
    QualType T = Dest.Designator.getType(Info.Ctx);
    QualType SrcT = Src.Designator.getType(Info.Ctx);
    if (!Info.Ctx.hasSameUnqualifiedType(T, SrcT)) {
      Info.FFDiag(E, diag::note_constexpr_memcpy_type_pun) << Move << SrcT << T;
      return false;
    }
    if (T->isIncompleteType()) {
      Info.FFDiag(E, diag::note_constexpr_memcpy_incomplete_type) << Move << T;
      return false;
    }
    if (!T.isTriviallyCopyableType(Info.Ctx)) {
      Info.FFDiag(E, diag::note_constexpr_memcpy_nontrivial) << Move << T;
      return false;
    }

    // Figure out how many T's we're copying.
    uint64_t TSize = Info.Ctx.getTypeSizeInChars(T).getQuantity();
    if (!WChar) {
      uint64_t Remainder;
      llvm::APInt OrigN = N;
      llvm::APInt::udivrem(OrigN, TSize, N, Remainder);
      if (Remainder) {
        Info.FFDiag(E, diag::note_constexpr_memcpy_unsupported)
            << Move << WChar << 0 << T << OrigN.toString(10, /*Signed*/false)
            << (unsigned)TSize;
        return false;
      }
    }

    // Check that the copying will remain within the arrays, just so that we
    // can give a more meaningful diagnostic. This implicitly also checks that
    // N fits into 64 bits.
    uint64_t RemainingSrcSize = Src.Designator.validIndexAdjustments().second;
    uint64_t RemainingDestSize = Dest.Designator.validIndexAdjustments().second;
    if (N.ugt(RemainingSrcSize) || N.ugt(RemainingDestSize)) {
      Info.FFDiag(E, diag::note_constexpr_memcpy_unsupported)
          << Move << WChar << (N.ugt(RemainingSrcSize) ? 1 : 2) << T
          << N.toString(10, /*Signed*/false);
      return false;
    }
    uint64_t NElems = N.getZExtValue();
    uint64_t NBytes = NElems * TSize;

    // Check for overlap.
    int Direction = 1;
    if (HasSameBase(Src, Dest)) {
      uint64_t SrcOffset = Src.getLValueOffset().getQuantity();
      uint64_t DestOffset = Dest.getLValueOffset().getQuantity();
      if (DestOffset >= SrcOffset && DestOffset - SrcOffset < NBytes) {
        // Dest is inside the source region.
        if (!Move) {
          Info.FFDiag(E, diag::note_constexpr_memcpy_overlap) << WChar;
          return false;
        }
        // For memmove and friends, copy backwards.
        if (!HandleLValueArrayAdjustment(Info, E, Src, T, NElems - 1) ||
            !HandleLValueArrayAdjustment(Info, E, Dest, T, NElems - 1))
          return false;
        Direction = -1;
      } else if (!Move && SrcOffset >= DestOffset &&
                 SrcOffset - DestOffset < NBytes) {
        // Src is inside the destination region for memcpy: invalid.
        Info.FFDiag(E, diag::note_constexpr_memcpy_overlap) << WChar;
        return false;
      }
    }

    while (true) {
      APValue Val;
      if (!handleLValueToRValueConversion(Info, E, T, Src, Val) ||
          !handleAssignment(Info, E, Dest, T, Val))
        return false;
      // Do not iterate past the last element; if we're copying backwards, that
      // might take us off the start of the array.
      if (--NElems == 0)
        return true;
      if (!HandleLValueArrayAdjustment(Info, E, Src, T, Direction) ||
          !HandleLValueArrayAdjustment(Info, E, Dest, T, Direction))
        return false;
    }
  }

  default:
    return visitNonBuiltinCallExpr(E);
  }
}

//===----------------------------------------------------------------------===//
// Member Pointer Evaluation
//===----------------------------------------------------------------------===//

namespace {
class MemberPointerExprEvaluator
  : public ExprEvaluatorBase<MemberPointerExprEvaluator> {
  MemberPtr &Result;

  bool Success(const ValueDecl *D) {
    Result = MemberPtr(D);
    return true;
  }
public:

  MemberPointerExprEvaluator(EvalInfo &Info, MemberPtr &Result)
    : ExprEvaluatorBaseTy(Info), Result(Result) {}

  bool Success(const APValue &V, const Expr *E) {
    Result.setFrom(V);
    return true;
  }
  bool ZeroInitialization(const Expr *E) {
    return Success((const ValueDecl*)nullptr);
  }

  bool VisitCastExpr(const CastExpr *E);
  bool VisitUnaryAddrOf(const UnaryOperator *E);
};
} // end anonymous namespace

static bool EvaluateMemberPointer(const Expr *E, MemberPtr &Result,
                                  EvalInfo &Info) {
  assert(E->isRValue() && E->getType()->isMemberPointerType());
  return MemberPointerExprEvaluator(Info, Result).Visit(E);
}

bool MemberPointerExprEvaluator::VisitCastExpr(const CastExpr *E) {
  switch (E->getCastKind()) {
  default:
    return ExprEvaluatorBaseTy::VisitCastExpr(E);

  case CK_NullToMemberPointer:
    VisitIgnoredValue(E->getSubExpr());
    return ZeroInitialization(E);

  case CK_BaseToDerivedMemberPointer: {
    if (!Visit(E->getSubExpr()))
      return false;
    if (E->path_empty())
      return true;
    // Base-to-derived member pointer casts store the path in derived-to-base
    // order, so iterate backwards. The CXXBaseSpecifier also provides us with
    // the wrong end of the derived->base arc, so stagger the path by one class.
    typedef std::reverse_iterator<CastExpr::path_const_iterator> ReverseIter;
    for (ReverseIter PathI(E->path_end() - 1), PathE(E->path_begin());
         PathI != PathE; ++PathI) {
      assert(!(*PathI)->isVirtual() && "memptr cast through vbase");
      const CXXRecordDecl *Derived = (*PathI)->getType()->getAsCXXRecordDecl();
      if (!Result.castToDerived(Derived))
        return Error(E);
    }
    const Type *FinalTy = E->getType()->castAs<MemberPointerType>()->getClass();
    if (!Result.castToDerived(FinalTy->getAsCXXRecordDecl()))
      return Error(E);
    return true;
  }

  case CK_DerivedToBaseMemberPointer:
    if (!Visit(E->getSubExpr()))
      return false;
    for (CastExpr::path_const_iterator PathI = E->path_begin(),
         PathE = E->path_end(); PathI != PathE; ++PathI) {
      assert(!(*PathI)->isVirtual() && "memptr cast through vbase");
      const CXXRecordDecl *Base = (*PathI)->getType()->getAsCXXRecordDecl();
      if (!Result.castToBase(Base))
        return Error(E);
    }
    return true;
  }
}

bool MemberPointerExprEvaluator::VisitUnaryAddrOf(const UnaryOperator *E) {
  // C++11 [expr.unary.op]p3 has very strict rules on how the address of a
  // member can be formed.
  return Success(cast<DeclRefExpr>(E->getSubExpr())->getDecl());
}

//===----------------------------------------------------------------------===//
// Record Evaluation
//===----------------------------------------------------------------------===//

namespace {
  class RecordExprEvaluator
  : public ExprEvaluatorBase<RecordExprEvaluator> {
    const LValue &This;
    APValue &Result;
  public:

    RecordExprEvaluator(EvalInfo &info, const LValue &This, APValue &Result)
      : ExprEvaluatorBaseTy(info), This(This), Result(Result) {}

    bool Success(const APValue &V, const Expr *E) {
      Result = V;
      return true;
    }
    bool ZeroInitialization(const Expr *E) {
      return ZeroInitialization(E, E->getType());
    }
    bool ZeroInitialization(const Expr *E, QualType T);

    bool VisitCallExpr(const CallExpr *E) {
      return handleCallExpr(E, Result, &This);
    }
    bool VisitCastExpr(const CastExpr *E);
    bool VisitInitListExpr(const InitListExpr *E);
    bool VisitCXXConstructExpr(const CXXConstructExpr *E) {
      return VisitCXXConstructExpr(E, E->getType());
    }
    bool VisitLambdaExpr(const LambdaExpr *E);
    bool VisitCXXInheritedCtorInitExpr(const CXXInheritedCtorInitExpr *E);
    bool VisitCXXConstructExpr(const CXXConstructExpr *E, QualType T);
    bool VisitCXXStdInitializerListExpr(const CXXStdInitializerListExpr *E);

    bool VisitBinCmp(const BinaryOperator *E);
  };
}

/// Perform zero-initialization on an object of non-union class type.
/// C++11 [dcl.init]p5:
///  To zero-initialize an object or reference of type T means:
///    [...]
///    -- if T is a (possibly cv-qualified) non-union class type,
///       each non-static data member and each base-class subobject is
///       zero-initialized
static bool HandleClassZeroInitialization(EvalInfo &Info, const Expr *E,
                                          const RecordDecl *RD,
                                          const LValue &This, APValue &Result) {
  assert(!RD->isUnion() && "Expected non-union class type");
  const CXXRecordDecl *CD = dyn_cast<CXXRecordDecl>(RD);
  Result = APValue(APValue::UninitStruct(), CD ? CD->getNumBases() : 0,
                   std::distance(RD->field_begin(), RD->field_end()));

  if (RD->isInvalidDecl()) return false;
  const ASTRecordLayout &Layout = Info.Ctx.getASTRecordLayout(RD);

  if (CD) {
    unsigned Index = 0;
    for (CXXRecordDecl::base_class_const_iterator I = CD->bases_begin(),
           End = CD->bases_end(); I != End; ++I, ++Index) {
      const CXXRecordDecl *Base = I->getType()->getAsCXXRecordDecl();
      LValue Subobject = This;
      if (!HandleLValueDirectBase(Info, E, Subobject, CD, Base, &Layout))
        return false;
      if (!HandleClassZeroInitialization(Info, E, Base, Subobject,
                                         Result.getStructBase(Index)))
        return false;
    }
  }

  for (const auto *I : RD->fields()) {
    // -- if T is a reference type, no initialization is performed.
    if (I->getType()->isReferenceType())
      continue;

    LValue Subobject = This;
    if (!HandleLValueMember(Info, E, Subobject, I, &Layout))
      return false;

    ImplicitValueInitExpr VIE(I->getType());
    if (!EvaluateInPlace(
          Result.getStructField(I->getFieldIndex()), Info, Subobject, &VIE))
      return false;
  }

  return true;
}

bool RecordExprEvaluator::ZeroInitialization(const Expr *E, QualType T) {
  const RecordDecl *RD = T->castAs<RecordType>()->getDecl();
  if (RD->isInvalidDecl()) return false;
  if (RD->isUnion()) {
    // C++11 [dcl.init]p5: If T is a (possibly cv-qualified) union type, the
    // object's first non-static named data member is zero-initialized
    RecordDecl::field_iterator I = RD->field_begin();
    if (I == RD->field_end()) {
      Result = APValue((const FieldDecl*)nullptr);
      return true;
    }

    LValue Subobject = This;
    if (!HandleLValueMember(Info, E, Subobject, *I))
      return false;
    Result = APValue(*I);
    ImplicitValueInitExpr VIE(I->getType());
    return EvaluateInPlace(Result.getUnionValue(), Info, Subobject, &VIE);
  }

  if (isa<CXXRecordDecl>(RD) && cast<CXXRecordDecl>(RD)->getNumVBases()) {
    Info.FFDiag(E, diag::note_constexpr_virtual_base) << RD;
    return false;
  }

  return HandleClassZeroInitialization(Info, E, RD, This, Result);
}

bool RecordExprEvaluator::VisitCastExpr(const CastExpr *E) {
  switch (E->getCastKind()) {
  default:
    return ExprEvaluatorBaseTy::VisitCastExpr(E);

  case CK_ConstructorConversion:
    return Visit(E->getSubExpr());

  case CK_DerivedToBase:
  case CK_UncheckedDerivedToBase: {
    APValue DerivedObject;
    if (!Evaluate(DerivedObject, Info, E->getSubExpr()))
      return false;
    if (!DerivedObject.isStruct())
      return Error(E->getSubExpr());

    // Derived-to-base rvalue conversion: just slice off the derived part.
    APValue *Value = &DerivedObject;
    const CXXRecordDecl *RD = E->getSubExpr()->getType()->getAsCXXRecordDecl();
    for (CastExpr::path_const_iterator PathI = E->path_begin(),
         PathE = E->path_end(); PathI != PathE; ++PathI) {
      assert(!(*PathI)->isVirtual() && "record rvalue with virtual base");
      const CXXRecordDecl *Base = (*PathI)->getType()->getAsCXXRecordDecl();
      Value = &Value->getStructBase(getBaseIndex(RD, Base));
      RD = Base;
    }
    Result = *Value;
    return true;
  }
  }
}

bool RecordExprEvaluator::VisitInitListExpr(const InitListExpr *E) {
  if (E->isTransparent())
    return Visit(E->getInit(0));

  const RecordDecl *RD = E->getType()->castAs<RecordType>()->getDecl();
  if (RD->isInvalidDecl()) return false;
  const ASTRecordLayout &Layout = Info.Ctx.getASTRecordLayout(RD);

  if (RD->isUnion()) {
    const FieldDecl *Field = E->getInitializedFieldInUnion();
    Result = APValue(Field);
    if (!Field)
      return true;

    // If the initializer list for a union does not contain any elements, the
    // first element of the union is value-initialized.
    // FIXME: The element should be initialized from an initializer list.
    //        Is this difference ever observable for initializer lists which
    //        we don't build?
    ImplicitValueInitExpr VIE(Field->getType());
    const Expr *InitExpr = E->getNumInits() ? E->getInit(0) : &VIE;

    LValue Subobject = This;
    if (!HandleLValueMember(Info, InitExpr, Subobject, Field, &Layout))
      return false;

    // Temporarily override This, in case there's a CXXDefaultInitExpr in here.
    ThisOverrideRAII ThisOverride(*Info.CurrentCall, &This,
                                  isa<CXXDefaultInitExpr>(InitExpr));

    return EvaluateInPlace(Result.getUnionValue(), Info, Subobject, InitExpr);
  }

  auto *CXXRD = dyn_cast<CXXRecordDecl>(RD);
  if (Result.isUninit())
    Result = APValue(APValue::UninitStruct(), CXXRD ? CXXRD->getNumBases() : 0,
                     std::distance(RD->field_begin(), RD->field_end()));
  unsigned ElementNo = 0;
  bool Success = true;

  // Initialize base classes.
  if (CXXRD) {
    for (const auto &Base : CXXRD->bases()) {
      assert(ElementNo < E->getNumInits() && "missing init for base class");
      const Expr *Init = E->getInit(ElementNo);

      LValue Subobject = This;
      if (!HandleLValueBase(Info, Init, Subobject, CXXRD, &Base))
        return false;

      APValue &FieldVal = Result.getStructBase(ElementNo);
      if (!EvaluateInPlace(FieldVal, Info, Subobject, Init)) {
        if (!Info.noteFailure())
          return false;
        Success = false;
      }
      ++ElementNo;
    }
  }

  // Initialize members.
  for (const auto *Field : RD->fields()) {
    // Anonymous bit-fields are not considered members of the class for
    // purposes of aggregate initialization.
    if (Field->isUnnamedBitfield())
      continue;

    LValue Subobject = This;

    bool HaveInit = ElementNo < E->getNumInits();

    // FIXME: Diagnostics here should point to the end of the initializer
    // list, not the start.
    if (!HandleLValueMember(Info, HaveInit ? E->getInit(ElementNo) : E,
                            Subobject, Field, &Layout))
      return false;

    // Perform an implicit value-initialization for members beyond the end of
    // the initializer list.
    ImplicitValueInitExpr VIE(HaveInit ? Info.Ctx.IntTy : Field->getType());
    const Expr *Init = HaveInit ? E->getInit(ElementNo++) : &VIE;

    // Temporarily override This, in case there's a CXXDefaultInitExpr in here.
    ThisOverrideRAII ThisOverride(*Info.CurrentCall, &This,
                                  isa<CXXDefaultInitExpr>(Init));

    APValue &FieldVal = Result.getStructField(Field->getFieldIndex());
    if (!EvaluateInPlace(FieldVal, Info, Subobject, Init) ||
        (Field->isBitField() && !truncateBitfieldValue(Info, Init,
                                                       FieldVal, Field))) {
      if (!Info.noteFailure())
        return false;
      Success = false;
    }
  }

  return Success;
}

bool RecordExprEvaluator::VisitCXXConstructExpr(const CXXConstructExpr *E,
                                                QualType T) {
  // Note that E's type is not necessarily the type of our class here; we might
  // be initializing an array element instead.
  const CXXConstructorDecl *FD = E->getConstructor();
  if (FD->isInvalidDecl() || FD->getParent()->isInvalidDecl()) return false;

  bool ZeroInit = E->requiresZeroInitialization();
  if (CheckTrivialDefaultConstructor(Info, E->getExprLoc(), FD, ZeroInit)) {
    // If we've already performed zero-initialization, we're already done.
    if (!Result.isUninit())
      return true;

    // We can get here in two different ways:
    //  1) We're performing value-initialization, and should zero-initialize
    //     the object, or
    //  2) We're performing default-initialization of an object with a trivial
    //     constexpr default constructor, in which case we should start the
    //     lifetimes of all the base subobjects (there can be no data member
    //     subobjects in this case) per [basic.life]p1.
    // Either way, ZeroInitialization is appropriate.
    return ZeroInitialization(E, T);
  }

  const FunctionDecl *Definition = nullptr;
  auto Body = FD->getBody(Definition);

  if (!CheckConstexprFunction(Info, E->getExprLoc(), FD, Definition, Body))
    return false;

  // Avoid materializing a temporary for an elidable copy/move constructor.
  if (E->isElidable() && !ZeroInit)
    if (const MaterializeTemporaryExpr *ME
          = dyn_cast<MaterializeTemporaryExpr>(E->getArg(0)))
      return Visit(ME->GetTemporaryExpr());

  if (ZeroInit && !ZeroInitialization(E, T))
    return false;

  auto Args = llvm::makeArrayRef(E->getArgs(), E->getNumArgs());
  return HandleConstructorCall(E, This, Args,
                               cast<CXXConstructorDecl>(Definition), Info,
                               Result);
}

bool RecordExprEvaluator::VisitCXXInheritedCtorInitExpr(
    const CXXInheritedCtorInitExpr *E) {
  if (!Info.CurrentCall) {
    assert(Info.checkingPotentialConstantExpression());
    return false;
  }

  const CXXConstructorDecl *FD = E->getConstructor();
  if (FD->isInvalidDecl() || FD->getParent()->isInvalidDecl())
    return false;

  const FunctionDecl *Definition = nullptr;
  auto Body = FD->getBody(Definition);

  if (!CheckConstexprFunction(Info, E->getExprLoc(), FD, Definition, Body))
    return false;

  return HandleConstructorCall(E, This, Info.CurrentCall->Arguments,
                               cast<CXXConstructorDecl>(Definition), Info,
                               Result);
}

bool RecordExprEvaluator::VisitCXXStdInitializerListExpr(
    const CXXStdInitializerListExpr *E) {
  const ConstantArrayType *ArrayType =
      Info.Ctx.getAsConstantArrayType(E->getSubExpr()->getType());

  LValue Array;
  if (!EvaluateLValue(E->getSubExpr(), Array, Info))
    return false;

  // Get a pointer to the first element of the array.
  Array.addArray(Info, E, ArrayType);

  // FIXME: Perform the checks on the field types in SemaInit.
  RecordDecl *Record = E->getType()->castAs<RecordType>()->getDecl();
  RecordDecl::field_iterator Field = Record->field_begin();
  if (Field == Record->field_end())
    return Error(E);

  // Start pointer.
  if (!Field->getType()->isPointerType() ||
      !Info.Ctx.hasSameType(Field->getType()->getPointeeType(),
                            ArrayType->getElementType()))
    return Error(E);

  // FIXME: What if the initializer_list type has base classes, etc?
  Result = APValue(APValue::UninitStruct(), 0, 2);
  Array.moveInto(Result.getStructField(0));

  if (++Field == Record->field_end())
    return Error(E);

  if (Field->getType()->isPointerType() &&
      Info.Ctx.hasSameType(Field->getType()->getPointeeType(),
                           ArrayType->getElementType())) {
    // End pointer.
    if (!HandleLValueArrayAdjustment(Info, E, Array,
                                     ArrayType->getElementType(),
                                     ArrayType->getSize().getZExtValue()))
      return false;
    Array.moveInto(Result.getStructField(1));
  } else if (Info.Ctx.hasSameType(Field->getType(), Info.Ctx.getSizeType()))
    // Length.
    Result.getStructField(1) = APValue(APSInt(ArrayType->getSize()));
  else
    return Error(E);

  if (++Field != Record->field_end())
    return Error(E);

  return true;
}

bool RecordExprEvaluator::VisitLambdaExpr(const LambdaExpr *E) {
  const CXXRecordDecl *ClosureClass = E->getLambdaClass();
  if (ClosureClass->isInvalidDecl()) return false;

  if (Info.checkingPotentialConstantExpression()) return true;

  const size_t NumFields =
      std::distance(ClosureClass->field_begin(), ClosureClass->field_end());

  assert(NumFields == (size_t)std::distance(E->capture_init_begin(),
                                            E->capture_init_end()) &&
         "The number of lambda capture initializers should equal the number of "
         "fields within the closure type");

  Result = APValue(APValue::UninitStruct(), /*NumBases*/0, NumFields);
  // Iterate through all the lambda's closure object's fields and initialize
  // them.
  auto *CaptureInitIt = E->capture_init_begin();
  const LambdaCapture *CaptureIt = ClosureClass->captures_begin();
  bool Success = true;
  for (const auto *Field : ClosureClass->fields()) {
    assert(CaptureInitIt != E->capture_init_end());
    // Get the initializer for this field
    Expr *const CurFieldInit = *CaptureInitIt++;

    // If there is no initializer, either this is a VLA or an error has
    // occurred.
    if (!CurFieldInit)
      return Error(E);

    APValue &FieldVal = Result.getStructField(Field->getFieldIndex());
    if (!EvaluateInPlace(FieldVal, Info, This, CurFieldInit)) {
      if (!Info.keepEvaluatingAfterFailure())
        return false;
      Success = false;
    }
    ++CaptureIt;
  }
  return Success;
}

static bool EvaluateRecord(const Expr *E, const LValue &This,
                           APValue &Result, EvalInfo &Info) {
  assert(E->isRValue() && E->getType()->isRecordType() &&
         "can't evaluate expression as a record rvalue");
  return RecordExprEvaluator(Info, This, Result).Visit(E);
}

//===----------------------------------------------------------------------===//
// Temporary Evaluation
//
// Temporaries are represented in the AST as rvalues, but generally behave like
// lvalues. The full-object of which the temporary is a subobject is implicitly
// materialized so that a reference can bind to it.
//===----------------------------------------------------------------------===//
namespace {
class TemporaryExprEvaluator
  : public LValueExprEvaluatorBase<TemporaryExprEvaluator> {
public:
  TemporaryExprEvaluator(EvalInfo &Info, LValue &Result) :
    LValueExprEvaluatorBaseTy(Info, Result, false) {}

  /// Visit an expression which constructs the value of this temporary.
  bool VisitConstructExpr(const Expr *E) {
    APValue &Value = createTemporary(E, false, Result, *Info.CurrentCall);
    return EvaluateInPlace(Value, Info, Result, E);
  }

  bool VisitCastExpr(const CastExpr *E) {
    switch (E->getCastKind()) {
    default:
      return LValueExprEvaluatorBaseTy::VisitCastExpr(E);

    case CK_ConstructorConversion:
      return VisitConstructExpr(E->getSubExpr());
    }
  }
  bool VisitInitListExpr(const InitListExpr *E) {
    return VisitConstructExpr(E);
  }
  bool VisitCXXConstructExpr(const CXXConstructExpr *E) {
    return VisitConstructExpr(E);
  }
  bool VisitCallExpr(const CallExpr *E) {
    return VisitConstructExpr(E);
  }
  bool VisitCXXStdInitializerListExpr(const CXXStdInitializerListExpr *E) {
    return VisitConstructExpr(E);
  }
  bool VisitLambdaExpr(const LambdaExpr *E) {
    return VisitConstructExpr(E);
  }
};
} // end anonymous namespace

/// Evaluate an expression of record type as a temporary.
static bool EvaluateTemporary(const Expr *E, LValue &Result, EvalInfo &Info) {
  assert(E->isRValue() && E->getType()->isRecordType());
  return TemporaryExprEvaluator(Info, Result).Visit(E);
}

//===----------------------------------------------------------------------===//
// Vector Evaluation
//===----------------------------------------------------------------------===//

namespace {
  class VectorExprEvaluator
  : public ExprEvaluatorBase<VectorExprEvaluator> {
    APValue &Result;
  public:

    VectorExprEvaluator(EvalInfo &info, APValue &Result)
      : ExprEvaluatorBaseTy(info), Result(Result) {}

    bool Success(ArrayRef<APValue> V, const Expr *E) {
      assert(V.size() == E->getType()->castAs<VectorType>()->getNumElements());
      // FIXME: remove this APValue copy.
      Result = APValue(V.data(), V.size());
      return true;
    }
    bool Success(const APValue &V, const Expr *E) {
      assert(V.isVector());
      Result = V;
      return true;
    }
    bool ZeroInitialization(const Expr *E);

    bool VisitUnaryReal(const UnaryOperator *E)
      { return Visit(E->getSubExpr()); }
    bool VisitCastExpr(const CastExpr* E);
    bool VisitInitListExpr(const InitListExpr *E);
    bool VisitUnaryImag(const UnaryOperator *E);
    // FIXME: Missing: unary -, unary ~, binary add/sub/mul/div,
    //                 binary comparisons, binary and/or/xor,
    //                 shufflevector, ExtVectorElementExpr
  };
} // end anonymous namespace

static bool EvaluateVector(const Expr* E, APValue& Result, EvalInfo &Info) {
  assert(E->isRValue() && E->getType()->isVectorType() &&"not a vector rvalue");
  return VectorExprEvaluator(Info, Result).Visit(E);
}

bool VectorExprEvaluator::VisitCastExpr(const CastExpr *E) {
  const VectorType *VTy = E->getType()->castAs<VectorType>();
  unsigned NElts = VTy->getNumElements();

  const Expr *SE = E->getSubExpr();
  QualType SETy = SE->getType();

  switch (E->getCastKind()) {
  case CK_VectorSplat: {
    APValue Val = APValue();
    if (SETy->isIntegerType()) {
      APSInt IntResult;
      if (!EvaluateInteger(SE, IntResult, Info))
        return false;
      Val = APValue(std::move(IntResult));
    } else if (SETy->isRealFloatingType()) {
      APFloat FloatResult(0.0);
      if (!EvaluateFloat(SE, FloatResult, Info))
        return false;
      Val = APValue(std::move(FloatResult));
    } else {
      return Error(E);
    }

    // Splat and create vector APValue.
    SmallVector<APValue, 4> Elts(NElts, Val);
    return Success(Elts, E);
  }
  case CK_BitCast: {
    // Evaluate the operand into an APInt we can extract from.
    llvm::APInt SValInt;
    if (!EvalAndBitcastToAPInt(Info, SE, SValInt))
      return false;
    // Extract the elements
    QualType EltTy = VTy->getElementType();
    unsigned EltSize = Info.Ctx.getTypeSize(EltTy);
    bool BigEndian = Info.Ctx.getTargetInfo().isBigEndian();
    SmallVector<APValue, 4> Elts;
    if (EltTy->isRealFloatingType()) {
      const llvm::fltSemantics &Sem = Info.Ctx.getFloatTypeSemantics(EltTy);
      unsigned FloatEltSize = EltSize;
      if (&Sem == &APFloat::x87DoubleExtended())
        FloatEltSize = 80;
      for (unsigned i = 0; i < NElts; i++) {
        llvm::APInt Elt;
        if (BigEndian)
          Elt = SValInt.rotl(i*EltSize+FloatEltSize).trunc(FloatEltSize);
        else
          Elt = SValInt.rotr(i*EltSize).trunc(FloatEltSize);
        Elts.push_back(APValue(APFloat(Sem, Elt)));
      }
    } else if (EltTy->isIntegerType()) {
      for (unsigned i = 0; i < NElts; i++) {
        llvm::APInt Elt;
        if (BigEndian)
          Elt = SValInt.rotl(i*EltSize+EltSize).zextOrTrunc(EltSize);
        else
          Elt = SValInt.rotr(i*EltSize).zextOrTrunc(EltSize);
        Elts.push_back(APValue(APSInt(Elt, EltTy->isSignedIntegerType())));
      }
    } else {
      return Error(E);
    }
    return Success(Elts, E);
  }
  default:
    return ExprEvaluatorBaseTy::VisitCastExpr(E);
  }
}

bool
VectorExprEvaluator::VisitInitListExpr(const InitListExpr *E) {
  const VectorType *VT = E->getType()->castAs<VectorType>();
  unsigned NumInits = E->getNumInits();
  unsigned NumElements = VT->getNumElements();

  QualType EltTy = VT->getElementType();
  SmallVector<APValue, 4> Elements;

  // The number of initializers can be less than the number of
  // vector elements. For OpenCL, this can be due to nested vector
  // initialization. For GCC compatibility, missing trailing elements
  // should be initialized with zeroes.
  unsigned CountInits = 0, CountElts = 0;
  while (CountElts < NumElements) {
    // Handle nested vector initialization.
    if (CountInits < NumInits
        && E->getInit(CountInits)->getType()->isVectorType()) {
      APValue v;
      if (!EvaluateVector(E->getInit(CountInits), v, Info))
        return Error(E);
      unsigned vlen = v.getVectorLength();
      for (unsigned j = 0; j < vlen; j++)
        Elements.push_back(v.getVectorElt(j));
      CountElts += vlen;
    } else if (EltTy->isIntegerType()) {
      llvm::APSInt sInt(32);
      if (CountInits < NumInits) {
        if (!EvaluateInteger(E->getInit(CountInits), sInt, Info))
          return false;
      } else // trailing integer zero.
        sInt = Info.Ctx.MakeIntValue(0, EltTy);
      Elements.push_back(APValue(sInt));
      CountElts++;
    } else {
      llvm::APFloat f(0.0);
      if (CountInits < NumInits) {
        if (!EvaluateFloat(E->getInit(CountInits), f, Info))
          return false;
      } else // trailing float zero.
        f = APFloat::getZero(Info.Ctx.getFloatTypeSemantics(EltTy));
      Elements.push_back(APValue(f));
      CountElts++;
    }
    CountInits++;
  }
  return Success(Elements, E);
}

bool
VectorExprEvaluator::ZeroInitialization(const Expr *E) {
  const VectorType *VT = E->getType()->getAs<VectorType>();
  QualType EltTy = VT->getElementType();
  APValue ZeroElement;
  if (EltTy->isIntegerType())
    ZeroElement = APValue(Info.Ctx.MakeIntValue(0, EltTy));
  else
    ZeroElement =
        APValue(APFloat::getZero(Info.Ctx.getFloatTypeSemantics(EltTy)));

  SmallVector<APValue, 4> Elements(VT->getNumElements(), ZeroElement);
  return Success(Elements, E);
}

bool VectorExprEvaluator::VisitUnaryImag(const UnaryOperator *E) {
  VisitIgnoredValue(E->getSubExpr());
  return ZeroInitialization(E);
}

//===----------------------------------------------------------------------===//
// Array Evaluation
//===----------------------------------------------------------------------===//

namespace {
  class ArrayExprEvaluator
  : public ExprEvaluatorBase<ArrayExprEvaluator> {
    const LValue &This;
    APValue &Result;
  public:

    ArrayExprEvaluator(EvalInfo &Info, const LValue &This, APValue &Result)
      : ExprEvaluatorBaseTy(Info), This(This), Result(Result) {}

    bool Success(const APValue &V, const Expr *E) {
      assert((V.isArray() || V.isLValue()) &&
             "expected array or string literal");
      Result = V;
      return true;
    }

    bool ZeroInitialization(const Expr *E) {
      const ConstantArrayType *CAT =
          Info.Ctx.getAsConstantArrayType(E->getType());
      if (!CAT)
        return Error(E);

      Result = APValue(APValue::UninitArray(), 0,
                       CAT->getSize().getZExtValue());
      if (!Result.hasArrayFiller()) return true;

      // Zero-initialize all elements.
      LValue Subobject = This;
      Subobject.addArray(Info, E, CAT);
      ImplicitValueInitExpr VIE(CAT->getElementType());
      return EvaluateInPlace(Result.getArrayFiller(), Info, Subobject, &VIE);
    }

    bool VisitCallExpr(const CallExpr *E) {
      return handleCallExpr(E, Result, &This);
    }
    bool VisitInitListExpr(const InitListExpr *E);
    bool VisitArrayInitLoopExpr(const ArrayInitLoopExpr *E);
    bool VisitCXXConstructExpr(const CXXConstructExpr *E);
    bool VisitCXXConstructExpr(const CXXConstructExpr *E,
                               const LValue &Subobject,
                               APValue *Value, QualType Type);
  };
} // end anonymous namespace

static bool EvaluateArray(const Expr *E, const LValue &This,
                          APValue &Result, EvalInfo &Info) {
  assert(E->isRValue() && E->getType()->isArrayType() && "not an array rvalue");
  return ArrayExprEvaluator(Info, This, Result).Visit(E);
}

// Return true iff the given array filler may depend on the element index.
static bool MaybeElementDependentArrayFiller(const Expr *FillerExpr) {
  // For now, just whitelist non-class value-initialization and initialization
  // lists comprised of them.
  if (isa<ImplicitValueInitExpr>(FillerExpr))
    return false;
  if (const InitListExpr *ILE = dyn_cast<InitListExpr>(FillerExpr)) {
    for (unsigned I = 0, E = ILE->getNumInits(); I != E; ++I) {
      if (MaybeElementDependentArrayFiller(ILE->getInit(I)))
        return true;
    }
    return false;
  }
  return true;
}

bool ArrayExprEvaluator::VisitInitListExpr(const InitListExpr *E) {
  const ConstantArrayType *CAT = Info.Ctx.getAsConstantArrayType(E->getType());
  if (!CAT)
    return Error(E);

  // C++11 [dcl.init.string]p1: A char array [...] can be initialized by [...]
  // an appropriately-typed string literal enclosed in braces.
  if (E->isStringLiteralInit()) {
    LValue LV;
    if (!EvaluateLValue(E->getInit(0), LV, Info))
      return false;
    APValue Val;
    LV.moveInto(Val);
    return Success(Val, E);
  }

  bool Success = true;

  assert((!Result.isArray() || Result.getArrayInitializedElts() == 0) &&
         "zero-initialized array shouldn't have any initialized elts");
  APValue Filler;
  if (Result.isArray() && Result.hasArrayFiller())
    Filler = Result.getArrayFiller();

  unsigned NumEltsToInit = E->getNumInits();
  unsigned NumElts = CAT->getSize().getZExtValue();
  const Expr *FillerExpr = E->hasArrayFiller() ? E->getArrayFiller() : nullptr;

  // If the initializer might depend on the array index, run it for each
  // array element.
  if (NumEltsToInit != NumElts && MaybeElementDependentArrayFiller(FillerExpr))
    NumEltsToInit = NumElts;

  LLVM_DEBUG(llvm::dbgs() << "The number of elements to initialize: "
                          << NumEltsToInit << ".\n");

  Result = APValue(APValue::UninitArray(), NumEltsToInit, NumElts);

  // If the array was previously zero-initialized, preserve the
  // zero-initialized values.
  if (!Filler.isUninit()) {
    for (unsigned I = 0, E = Result.getArrayInitializedElts(); I != E; ++I)
      Result.getArrayInitializedElt(I) = Filler;
    if (Result.hasArrayFiller())
      Result.getArrayFiller() = Filler;
  }

  LValue Subobject = This;
  Subobject.addArray(Info, E, CAT);
  for (unsigned Index = 0; Index != NumEltsToInit; ++Index) {
    const Expr *Init =
        Index < E->getNumInits() ? E->getInit(Index) : FillerExpr;
    if (!EvaluateInPlace(Result.getArrayInitializedElt(Index),
                         Info, Subobject, Init) ||
        !HandleLValueArrayAdjustment(Info, Init, Subobject,
                                     CAT->getElementType(), 1)) {
      if (!Info.noteFailure())
        return false;
      Success = false;
    }
  }

  if (!Result.hasArrayFiller())
    return Success;

  // If we get here, we have a trivial filler, which we can just evaluate
  // once and splat over the rest of the array elements.
  assert(FillerExpr && "no array filler for incomplete init list");
  return EvaluateInPlace(Result.getArrayFiller(), Info, Subobject,
                         FillerExpr) && Success;
}

bool ArrayExprEvaluator::VisitArrayInitLoopExpr(const ArrayInitLoopExpr *E) {
  if (E->getCommonExpr() &&
      !Evaluate(Info.CurrentCall->createTemporary(E->getCommonExpr(), false),
                Info, E->getCommonExpr()->getSourceExpr()))
    return false;

  auto *CAT = cast<ConstantArrayType>(E->getType()->castAsArrayTypeUnsafe());

  uint64_t Elements = CAT->getSize().getZExtValue();
  Result = APValue(APValue::UninitArray(), Elements, Elements);

  LValue Subobject = This;
  Subobject.addArray(Info, E, CAT);

  bool Success = true;
  for (EvalInfo::ArrayInitLoopIndex Index(Info); Index != Elements; ++Index) {
    if (!EvaluateInPlace(Result.getArrayInitializedElt(Index),
                         Info, Subobject, E->getSubExpr()) ||
        !HandleLValueArrayAdjustment(Info, E, Subobject,
                                     CAT->getElementType(), 1)) {
      if (!Info.noteFailure())
        return false;
      Success = false;
    }
  }

  return Success;
}

bool ArrayExprEvaluator::VisitCXXConstructExpr(const CXXConstructExpr *E) {
  return VisitCXXConstructExpr(E, This, &Result, E->getType());
}

bool ArrayExprEvaluator::VisitCXXConstructExpr(const CXXConstructExpr *E,
                                               const LValue &Subobject,
                                               APValue *Value,
                                               QualType Type) {
  bool HadZeroInit = !Value->isUninit();

  if (const ConstantArrayType *CAT = Info.Ctx.getAsConstantArrayType(Type)) {
    unsigned N = CAT->getSize().getZExtValue();

    // Preserve the array filler if we had prior zero-initialization.
    APValue Filler =
      HadZeroInit && Value->hasArrayFiller() ? Value->getArrayFiller()
                                             : APValue();

    *Value = APValue(APValue::UninitArray(), N, N);

    if (HadZeroInit)
      for (unsigned I = 0; I != N; ++I)
        Value->getArrayInitializedElt(I) = Filler;

    // Initialize the elements.
    LValue ArrayElt = Subobject;
    ArrayElt.addArray(Info, E, CAT);
    for (unsigned I = 0; I != N; ++I)
      if (!VisitCXXConstructExpr(E, ArrayElt, &Value->getArrayInitializedElt(I),
                                 CAT->getElementType()) ||
          !HandleLValueArrayAdjustment(Info, E, ArrayElt,
                                       CAT->getElementType(), 1))
        return false;

    return true;
  }

  if (!Type->isRecordType())
    return Error(E);

  return RecordExprEvaluator(Info, Subobject, *Value)
             .VisitCXXConstructExpr(E, Type);
}

//===----------------------------------------------------------------------===//
// Integer Evaluation
//
// As a GNU extension, we support casting pointers to sufficiently-wide integer
// types and back in constant folding. Integer values are thus represented
// either as an integer-valued APValue, or as an lvalue-valued APValue.
//===----------------------------------------------------------------------===//

namespace {
class IntExprEvaluator
        : public ExprEvaluatorBase<IntExprEvaluator> {
  APValue &Result;
public:
  IntExprEvaluator(EvalInfo &info, APValue &result)
      : ExprEvaluatorBaseTy(info), Result(result) {}

  bool Success(const llvm::APSInt &SI, const Expr *E, APValue &Result) {
    assert(E->getType()->isIntegralOrEnumerationType() &&
           "Invalid evaluation result.");
    assert(SI.isSigned() == E->getType()->isSignedIntegerOrEnumerationType() &&
           "Invalid evaluation result.");
    assert(SI.getBitWidth() == Info.Ctx.getIntWidth(E->getType()) &&
           "Invalid evaluation result.");
    Result = APValue(SI);
    return true;
  }
  bool Success(const llvm::APSInt &SI, const Expr *E) {
    return Success(SI, E, Result);
  }

  bool Success(const llvm::APInt &I, const Expr *E, APValue &Result) {
    assert(E->getType()->isIntegralOrEnumerationType() &&
           "Invalid evaluation result.");
    assert(I.getBitWidth() == Info.Ctx.getIntWidth(E->getType()) &&
           "Invalid evaluation result.");
    Result = APValue(APSInt(I));
    Result.getInt().setIsUnsigned(
                            E->getType()->isUnsignedIntegerOrEnumerationType());
    return true;
  }
  bool Success(const llvm::APInt &I, const Expr *E) {
    return Success(I, E, Result);
  }

  bool Success(uint64_t Value, const Expr *E, APValue &Result) {
    assert(E->getType()->isIntegralOrEnumerationType() &&
           "Invalid evaluation result.");
    Result = APValue(Info.Ctx.MakeIntValue(Value, E->getType()));
    return true;
  }
  bool Success(uint64_t Value, const Expr *E) {
    return Success(Value, E, Result);
  }

  bool Success(CharUnits Size, const Expr *E) {
    return Success(Size.getQuantity(), E);
  }

  bool Success(const APValue &V, const Expr *E) {
    if (V.isLValue() || V.isAddrLabelDiff()) {
      Result = V;
      return true;
    }
    return Success(V.getInt(), E);
  }

  bool ZeroInitialization(const Expr *E) { return Success(0, E); }

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  bool VisitConstantExpr(const ConstantExpr *E);

  bool VisitIntegerLiteral(const IntegerLiteral *E) {
    return Success(E->getValue(), E);
  }
  bool VisitCharacterLiteral(const CharacterLiteral *E) {
    return Success(E->getValue(), E);
  }

  bool CheckReferencedDecl(const Expr *E, const Decl *D);
  bool VisitDeclRefExpr(const DeclRefExpr *E) {
    if (CheckReferencedDecl(E, E->getDecl()))
      return true;

    return ExprEvaluatorBaseTy::VisitDeclRefExpr(E);
  }
  bool VisitMemberExpr(const MemberExpr *E) {
    if (CheckReferencedDecl(E, E->getMemberDecl())) {
      VisitIgnoredBaseExpression(E->getBase());
      return true;
    }

    return ExprEvaluatorBaseTy::VisitMemberExpr(E);
  }

  bool VisitCallExpr(const CallExpr *E);
  bool VisitBuiltinCallExpr(const CallExpr *E, unsigned BuiltinOp);
  bool VisitBinaryOperator(const BinaryOperator *E);
  bool VisitOffsetOfExpr(const OffsetOfExpr *E);
  bool VisitUnaryOperator(const UnaryOperator *E);

  bool VisitCastExpr(const CastExpr* E);
  bool VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *E);

  bool VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *E) {
    return Success(E->getValue(), E);
  }

  bool VisitObjCBoolLiteralExpr(const ObjCBoolLiteralExpr *E) {
    return Success(E->getValue(), E);
  }

  bool VisitArrayInitIndexExpr(const ArrayInitIndexExpr *E) {
    if (Info.ArrayInitIndex == uint64_t(-1)) {
      // We were asked to evaluate this subexpression independent of the
      // enclosing ArrayInitLoopExpr. We can't do that.
      Info.FFDiag(E);
      return false;
    }
    return Success(Info.ArrayInitIndex, E);
  }

  // Note, GNU defines __null as an integer, not a pointer.
  bool VisitGNUNullExpr(const GNUNullExpr *E) {
    return ZeroInitialization(E);
  }

  bool VisitTypeTraitExpr(const TypeTraitExpr *E) {
    return Success(E->getValue(), E);
  }

  bool VisitArrayTypeTraitExpr(const ArrayTypeTraitExpr *E) {
    return Success(E->getValue(), E);
  }

  bool VisitExpressionTraitExpr(const ExpressionTraitExpr *E) {
    return Success(E->getValue(), E);
  }

  bool VisitUnaryReal(const UnaryOperator *E);
  bool VisitUnaryImag(const UnaryOperator *E);

  bool VisitCXXNoexceptExpr(const CXXNoexceptExpr *E);
  bool VisitSizeOfPackExpr(const SizeOfPackExpr *E);

  // FIXME: Missing: array subscript of vector, member of vector
};

class FixedPointExprEvaluator
    : public ExprEvaluatorBase<FixedPointExprEvaluator> {
  APValue &Result;

 public:
  FixedPointExprEvaluator(EvalInfo &info, APValue &result)
      : ExprEvaluatorBaseTy(info), Result(result) {}

  bool Success(const llvm::APSInt &SI, const Expr *E, APValue &Result) {
    assert(E->getType()->isFixedPointType() && "Invalid evaluation result.");
    assert(SI.isSigned() == E->getType()->isSignedFixedPointType() &&
           "Invalid evaluation result.");
    assert(SI.getBitWidth() == Info.Ctx.getIntWidth(E->getType()) &&
           "Invalid evaluation result.");
    Result = APValue(SI);
    return true;
  }
  bool Success(const llvm::APSInt &SI, const Expr *E) {
    return Success(SI, E, Result);
  }

  bool Success(const llvm::APInt &I, const Expr *E, APValue &Result) {
    assert(E->getType()->isFixedPointType() && "Invalid evaluation result.");
    assert(I.getBitWidth() == Info.Ctx.getIntWidth(E->getType()) &&
           "Invalid evaluation result.");
    Result = APValue(APSInt(I));
    Result.getInt().setIsUnsigned(E->getType()->isUnsignedFixedPointType());
    return true;
  }
  bool Success(const llvm::APInt &I, const Expr *E) {
    return Success(I, E, Result);
  }

  bool Success(uint64_t Value, const Expr *E, APValue &Result) {
    assert(E->getType()->isFixedPointType() && "Invalid evaluation result.");
    Result = APValue(Info.Ctx.MakeIntValue(Value, E->getType()));
    return true;
  }
  bool Success(uint64_t Value, const Expr *E) {
    return Success(Value, E, Result);
  }

  bool Success(CharUnits Size, const Expr *E) {
    return Success(Size.getQuantity(), E);
  }

  bool Success(const APValue &V, const Expr *E) {
    if (V.isLValue() || V.isAddrLabelDiff()) {
      Result = V;
      return true;
    }
    return Success(V.getInt(), E);
  }

  bool ZeroInitialization(const Expr *E) { return Success(0, E); }

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  bool VisitFixedPointLiteral(const FixedPointLiteral *E) {
    return Success(E->getValue(), E);
  }

  bool VisitUnaryOperator(const UnaryOperator *E);
};
} // end anonymous namespace

/// EvaluateIntegerOrLValue - Evaluate an rvalue integral-typed expression, and
/// produce either the integer value or a pointer.
///
/// GCC has a heinous extension which folds casts between pointer types and
/// pointer-sized integral types. We support this by allowing the evaluation of
/// an integer rvalue to produce a pointer (represented as an lvalue) instead.
/// Some simple arithmetic on such values is supported (they are treated much
/// like char*).
static bool EvaluateIntegerOrLValue(const Expr *E, APValue &Result,
                                    EvalInfo &Info) {
  assert(E->isRValue() && E->getType()->isIntegralOrEnumerationType());
  return IntExprEvaluator(Info, Result).Visit(E);
}

static bool EvaluateInteger(const Expr *E, APSInt &Result, EvalInfo &Info) {
  APValue Val;
  if (!EvaluateIntegerOrLValue(E, Val, Info))
    return false;
  if (!Val.isInt()) {
    // FIXME: It would be better to produce the diagnostic for casting
    //        a pointer to an integer.
    Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
    return false;
  }
  Result = Val.getInt();
  return true;
}

/// Check whether the given declaration can be directly converted to an integral
/// rvalue. If not, no diagnostic is produced; there are other things we can
/// try.
bool IntExprEvaluator::CheckReferencedDecl(const Expr* E, const Decl* D) {
  // Enums are integer constant exprs.
  if (const EnumConstantDecl *ECD = dyn_cast<EnumConstantDecl>(D)) {
    // Check for signedness/width mismatches between E type and ECD value.
    bool SameSign = (ECD->getInitVal().isSigned()
                     == E->getType()->isSignedIntegerOrEnumerationType());
    bool SameWidth = (ECD->getInitVal().getBitWidth()
                      == Info.Ctx.getIntWidth(E->getType()));
    if (SameSign && SameWidth)
      return Success(ECD->getInitVal(), E);
    else {
      // Get rid of mismatch (otherwise Success assertions will fail)
      // by computing a new value matching the type of E.
      llvm::APSInt Val = ECD->getInitVal();
      if (!SameSign)
        Val.setIsSigned(!ECD->getInitVal().isSigned());
      if (!SameWidth)
        Val = Val.extOrTrunc(Info.Ctx.getIntWidth(E->getType()));
      return Success(Val, E);
    }
  }
  return false;
}

/// Values returned by __builtin_classify_type, chosen to match the values
/// produced by GCC's builtin.
enum class GCCTypeClass {
  None = -1,
  Void = 0,
  Integer = 1,
  // GCC reserves 2 for character types, but instead classifies them as
  // integers.
  Enum = 3,
  Bool = 4,
  Pointer = 5,
  // GCC reserves 6 for references, but appears to never use it (because
  // expressions never have reference type, presumably).
  PointerToDataMember = 7,
  RealFloat = 8,
  Complex = 9,
  // GCC reserves 10 for functions, but does not use it since GCC version 6 due
  // to decay to pointer. (Prior to version 6 it was only used in C++ mode).
  // GCC claims to reserve 11 for pointers to member functions, but *actually*
  // uses 12 for that purpose, same as for a class or struct. Maybe it
  // internally implements a pointer to member as a struct?  Who knows.
  PointerToMemberFunction = 12, // Not a bug, see above.
  ClassOrStruct = 12,
  Union = 13,
  // GCC reserves 14 for arrays, but does not use it since GCC version 6 due to
  // decay to pointer. (Prior to version 6 it was only used in C++ mode).
  // GCC reserves 15 for strings, but actually uses 5 (pointer) for string
  // literals.
};

/// EvaluateBuiltinClassifyType - Evaluate __builtin_classify_type the same way
/// as GCC.
static GCCTypeClass
EvaluateBuiltinClassifyType(QualType T, const LangOptions &LangOpts) {
  assert(!T->isDependentType() && "unexpected dependent type");

  QualType CanTy = T.getCanonicalType();
  const BuiltinType *BT = dyn_cast<BuiltinType>(CanTy);

  switch (CanTy->getTypeClass()) {
#define TYPE(ID, BASE)
#define DEPENDENT_TYPE(ID, BASE) case Type::ID:
#define NON_CANONICAL_TYPE(ID, BASE) case Type::ID:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(ID, BASE) case Type::ID:
#include "clang/AST/TypeNodes.def"
  case Type::Auto:
  case Type::DeducedTemplateSpecialization:
      llvm_unreachable("unexpected non-canonical or dependent type");

  case Type::Builtin:
    switch (BT->getKind()) {
#define BUILTIN_TYPE(ID, SINGLETON_ID)
#define SIGNED_TYPE(ID, SINGLETON_ID) \
    case BuiltinType::ID: return GCCTypeClass::Integer;
#define FLOATING_TYPE(ID, SINGLETON_ID) \
    case BuiltinType::ID: return GCCTypeClass::RealFloat;
#define PLACEHOLDER_TYPE(ID, SINGLETON_ID) \
    case BuiltinType::ID: break;
#include "clang/AST/BuiltinTypes.def"
    case BuiltinType::Void:
      return GCCTypeClass::Void;

    case BuiltinType::Bool:
      return GCCTypeClass::Bool;

    case BuiltinType::Char_U:
    case BuiltinType::UChar:
    case BuiltinType::WChar_U:
    case BuiltinType::Char8:
    case BuiltinType::Char16:
    case BuiltinType::Char32:
    case BuiltinType::UShort:
    case BuiltinType::UInt:
    case BuiltinType::ULong:
    case BuiltinType::ULongLong:
    case BuiltinType::UInt128:
      return GCCTypeClass::Integer;

    case BuiltinType::UShortAccum:
    case BuiltinType::UAccum:
    case BuiltinType::ULongAccum:
    case BuiltinType::UShortFract:
    case BuiltinType::UFract:
    case BuiltinType::ULongFract:
    case BuiltinType::SatUShortAccum:
    case BuiltinType::SatUAccum:
    case BuiltinType::SatULongAccum:
    case BuiltinType::SatUShortFract:
    case BuiltinType::SatUFract:
    case BuiltinType::SatULongFract:
      return GCCTypeClass::None;

    case BuiltinType::NullPtr:

    case BuiltinType::ObjCId:
    case BuiltinType::ObjCClass:
    case BuiltinType::ObjCSel:
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
    case BuiltinType::Id:
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
    case BuiltinType::Id:
#include "clang/Basic/OpenCLExtensionTypes.def"
    case BuiltinType::OCLSampler:
    case BuiltinType::OCLEvent:
    case BuiltinType::OCLClkEvent:
    case BuiltinType::OCLQueue:
    case BuiltinType::OCLReserveID:
      return GCCTypeClass::None;

    case BuiltinType::Dependent:
      llvm_unreachable("unexpected dependent type");
    };
    llvm_unreachable("unexpected placeholder type");

  case Type::Enum:
    return LangOpts.CPlusPlus ? GCCTypeClass::Enum : GCCTypeClass::Integer;

  case Type::Pointer:
  case Type::ConstantArray:
  case Type::VariableArray:
  case Type::IncompleteArray:
  case Type::FunctionNoProto:
  case Type::FunctionProto:
    return GCCTypeClass::Pointer;

  case Type::MemberPointer:
    return CanTy->isMemberDataPointerType()
               ? GCCTypeClass::PointerToDataMember
               : GCCTypeClass::PointerToMemberFunction;

  case Type::Complex:
    return GCCTypeClass::Complex;

  case Type::Record:
    return CanTy->isUnionType() ? GCCTypeClass::Union
                                : GCCTypeClass::ClassOrStruct;

  case Type::Atomic:
    // GCC classifies _Atomic T the same as T.
    return EvaluateBuiltinClassifyType(
        CanTy->castAs<AtomicType>()->getValueType(), LangOpts);

  case Type::BlockPointer:
  case Type::Vector:
  case Type::ExtVector:
  case Type::ObjCObject:
  case Type::ObjCInterface:
  case Type::ObjCObjectPointer:
  case Type::Pipe:
    // GCC classifies vectors as None. We follow its lead and classify all
    // other types that don't fit into the regular classification the same way.
    return GCCTypeClass::None;

  case Type::LValueReference:
  case Type::RValueReference:
    llvm_unreachable("invalid type for expression");
  }

  llvm_unreachable("unexpected type class");
}

/// EvaluateBuiltinClassifyType - Evaluate __builtin_classify_type the same way
/// as GCC.
static GCCTypeClass
EvaluateBuiltinClassifyType(const CallExpr *E, const LangOptions &LangOpts) {
  // If no argument was supplied, default to None. This isn't
  // ideal, however it is what gcc does.
  if (E->getNumArgs() == 0)
    return GCCTypeClass::None;

  // FIXME: Bizarrely, GCC treats a call with more than one argument as not
  // being an ICE, but still folds it to a constant using the type of the first
  // argument.
  return EvaluateBuiltinClassifyType(E->getArg(0)->getType(), LangOpts);
}

/// EvaluateBuiltinConstantPForLValue - Determine the result of
/// __builtin_constant_p when applied to the given lvalue.
///
/// An lvalue is only "constant" if it is a pointer or reference to the first
/// character of a string literal.
template<typename LValue>
static bool EvaluateBuiltinConstantPForLValue(const LValue &LV) {
  const Expr *E = LV.getLValueBase().template dyn_cast<const Expr*>();
  return E && isa<StringLiteral>(E) && LV.getLValueOffset().isZero();
}

/// EvaluateBuiltinConstantP - Evaluate __builtin_constant_p as similarly to
/// GCC as we can manage.
static bool EvaluateBuiltinConstantP(ASTContext &Ctx, const Expr *Arg) {
  QualType ArgType = Arg->getType();

  // __builtin_constant_p always has one operand. The rules which gcc follows
  // are not precisely documented, but are as follows:
  //
  //  - If the operand is of integral, floating, complex or enumeration type,
  //    and can be folded to a known value of that type, it returns 1.
  //  - If the operand and can be folded to a pointer to the first character
  //    of a string literal (or such a pointer cast to an integral type), it
  //    returns 1.
  //
  // Otherwise, it returns 0.
  //
  // FIXME: GCC also intends to return 1 for literals of aggregate types, but
  // its support for this does not currently work.
  if (ArgType->isIntegralOrEnumerationType()) {
    Expr::EvalResult Result;
    if (!Arg->EvaluateAsRValue(Result, Ctx) || Result.HasSideEffects)
      return false;

    APValue &V = Result.Val;
    if (V.getKind() == APValue::Int)
      return true;
    if (V.getKind() == APValue::LValue)
      return EvaluateBuiltinConstantPForLValue(V);
  } else if (ArgType->isFloatingType() || ArgType->isAnyComplexType()) {
    return Arg->isEvaluatable(Ctx);
  } else if (ArgType->isPointerType() || Arg->isGLValue()) {
    LValue LV;
    Expr::EvalStatus Status;
    EvalInfo Info(Ctx, Status, EvalInfo::EM_ConstantFold);
    if ((Arg->isGLValue() ? EvaluateLValue(Arg, LV, Info)
                          : EvaluatePointer(Arg, LV, Info)) &&
        !Status.HasSideEffects)
      return EvaluateBuiltinConstantPForLValue(LV);
  }

  // Anything else isn't considered to be sufficiently constant.
  return false;
}

/// Retrieves the "underlying object type" of the given expression,
/// as used by __builtin_object_size.
static QualType getObjectType(APValue::LValueBase B) {
  if (const ValueDecl *D = B.dyn_cast<const ValueDecl*>()) {
    if (const VarDecl *VD = dyn_cast<VarDecl>(D))
      return VD->getType();
  } else if (const Expr *E = B.get<const Expr*>()) {
    if (isa<CompoundLiteralExpr>(E))
      return E->getType();
  }

  return QualType();
}

/// A more selective version of E->IgnoreParenCasts for
/// tryEvaluateBuiltinObjectSize. This ignores some casts/parens that serve only
/// to change the type of E.
/// Ex. For E = `(short*)((char*)(&foo))`, returns `&foo`
///
/// Always returns an RValue with a pointer representation.
static const Expr *ignorePointerCastsAndParens(const Expr *E) {
  assert(E->isRValue() && E->getType()->hasPointerRepresentation());

  auto *NoParens = E->IgnoreParens();
  auto *Cast = dyn_cast<CastExpr>(NoParens);
  if (Cast == nullptr)
    return NoParens;

  // We only conservatively allow a few kinds of casts, because this code is
  // inherently a simple solution that seeks to support the common case.
  auto CastKind = Cast->getCastKind();
  if (CastKind != CK_NoOp && CastKind != CK_BitCast &&
      CastKind != CK_AddressSpaceConversion)
    return NoParens;

  auto *SubExpr = Cast->getSubExpr();
  if (!SubExpr->getType()->hasPointerRepresentation() || !SubExpr->isRValue())
    return NoParens;
  return ignorePointerCastsAndParens(SubExpr);
}

/// Checks to see if the given LValue's Designator is at the end of the LValue's
/// record layout. e.g.
///   struct { struct { int a, b; } fst, snd; } obj;
///   obj.fst   // no
///   obj.snd   // yes
///   obj.fst.a // no
///   obj.fst.b // no
///   obj.snd.a // no
///   obj.snd.b // yes
///
/// Please note: this function is specialized for how __builtin_object_size
/// views "objects".
///
/// If this encounters an invalid RecordDecl or otherwise cannot determine the
/// correct result, it will always return true.
static bool isDesignatorAtObjectEnd(const ASTContext &Ctx, const LValue &LVal) {
  assert(!LVal.Designator.Invalid);

  auto IsLastOrInvalidFieldDecl = [&Ctx](const FieldDecl *FD, bool &Invalid) {
    const RecordDecl *Parent = FD->getParent();
    Invalid = Parent->isInvalidDecl();
    if (Invalid || Parent->isUnion())
      return true;
    const ASTRecordLayout &Layout = Ctx.getASTRecordLayout(Parent);
    return FD->getFieldIndex() + 1 == Layout.getFieldCount();
  };

  auto &Base = LVal.getLValueBase();
  if (auto *ME = dyn_cast_or_null<MemberExpr>(Base.dyn_cast<const Expr *>())) {
    if (auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
      bool Invalid;
      if (!IsLastOrInvalidFieldDecl(FD, Invalid))
        return Invalid;
    } else if (auto *IFD = dyn_cast<IndirectFieldDecl>(ME->getMemberDecl())) {
      for (auto *FD : IFD->chain()) {
        bool Invalid;
        if (!IsLastOrInvalidFieldDecl(cast<FieldDecl>(FD), Invalid))
          return Invalid;
      }
    }
  }

  unsigned I = 0;
  QualType BaseType = getType(Base);
  if (LVal.Designator.FirstEntryIsAnUnsizedArray) {
    // If we don't know the array bound, conservatively assume we're looking at
    // the final array element.
    ++I;
    if (BaseType->isIncompleteArrayType())
      BaseType = Ctx.getAsArrayType(BaseType)->getElementType();
    else
      BaseType = BaseType->castAs<PointerType>()->getPointeeType();
  }

  for (unsigned E = LVal.Designator.Entries.size(); I != E; ++I) {
    const auto &Entry = LVal.Designator.Entries[I];
    if (BaseType->isArrayType()) {
      // Because __builtin_object_size treats arrays as objects, we can ignore
      // the index iff this is the last array in the Designator.
      if (I + 1 == E)
        return true;
      const auto *CAT = cast<ConstantArrayType>(Ctx.getAsArrayType(BaseType));
      uint64_t Index = Entry.ArrayIndex;
      if (Index + 1 != CAT->getSize())
        return false;
      BaseType = CAT->getElementType();
    } else if (BaseType->isAnyComplexType()) {
      const auto *CT = BaseType->castAs<ComplexType>();
      uint64_t Index = Entry.ArrayIndex;
      if (Index != 1)
        return false;
      BaseType = CT->getElementType();
    } else if (auto *FD = getAsField(Entry)) {
      bool Invalid;
      if (!IsLastOrInvalidFieldDecl(FD, Invalid))
        return Invalid;
      BaseType = FD->getType();
    } else {
      assert(getAsBaseClass(Entry) && "Expecting cast to a base class");
      return false;
    }
  }
  return true;
}

/// Tests to see if the LValue has a user-specified designator (that isn't
/// necessarily valid). Note that this always returns 'true' if the LValue has
/// an unsized array as its first designator entry, because there's currently no
/// way to tell if the user typed *foo or foo[0].
static bool refersToCompleteObject(const LValue &LVal) {
  if (LVal.Designator.Invalid)
    return false;

  if (!LVal.Designator.Entries.empty())
    return LVal.Designator.isMostDerivedAnUnsizedArray();

  if (!LVal.InvalidBase)
    return true;

  // If `E` is a MemberExpr, then the first part of the designator is hiding in
  // the LValueBase.
  const auto *E = LVal.Base.dyn_cast<const Expr *>();
  return !E || !isa<MemberExpr>(E);
}

/// Attempts to detect a user writing into a piece of memory that's impossible
/// to figure out the size of by just using types.
static bool isUserWritingOffTheEnd(const ASTContext &Ctx, const LValue &LVal) {
  const SubobjectDesignator &Designator = LVal.Designator;
  // Notes:
  // - Users can only write off of the end when we have an invalid base. Invalid
  //   bases imply we don't know where the memory came from.
  // - We used to be a bit more aggressive here; we'd only be conservative if
  //   the array at the end was flexible, or if it had 0 or 1 elements. This
  //   broke some common standard library extensions (PR30346), but was
  //   otherwise seemingly fine. It may be useful to reintroduce this behavior
  //   with some sort of whitelist. OTOH, it seems that GCC is always
  //   conservative with the last element in structs (if it's an array), so our
  //   current behavior is more compatible than a whitelisting approach would
  //   be.
  return LVal.InvalidBase &&
         Designator.Entries.size() == Designator.MostDerivedPathLength &&
         Designator.MostDerivedIsArrayElement &&
         isDesignatorAtObjectEnd(Ctx, LVal);
}

/// Converts the given APInt to CharUnits, assuming the APInt is unsigned.
/// Fails if the conversion would cause loss of precision.
static bool convertUnsignedAPIntToCharUnits(const llvm::APInt &Int,
                                            CharUnits &Result) {
  auto CharUnitsMax = std::numeric_limits<CharUnits::QuantityType>::max();
  if (Int.ugt(CharUnitsMax))
    return false;
  Result = CharUnits::fromQuantity(Int.getZExtValue());
  return true;
}

/// Helper for tryEvaluateBuiltinObjectSize -- Given an LValue, this will
/// determine how many bytes exist from the beginning of the object to either
/// the end of the current subobject, or the end of the object itself, depending
/// on what the LValue looks like + the value of Type.
///
/// If this returns false, the value of Result is undefined.
static bool determineEndOffset(EvalInfo &Info, SourceLocation ExprLoc,
                               unsigned Type, const LValue &LVal,
                               CharUnits &EndOffset) {
  bool DetermineForCompleteObject = refersToCompleteObject(LVal);

  auto CheckedHandleSizeof = [&](QualType Ty, CharUnits &Result) {
    if (Ty.isNull() || Ty->isIncompleteType() || Ty->isFunctionType())
      return false;
    return HandleSizeof(Info, ExprLoc, Ty, Result);
  };

  // We want to evaluate the size of the entire object. This is a valid fallback
  // for when Type=1 and the designator is invalid, because we're asked for an
  // upper-bound.
  if (!(Type & 1) || LVal.Designator.Invalid || DetermineForCompleteObject) {
    // Type=3 wants a lower bound, so we can't fall back to this.
    if (Type == 3 && !DetermineForCompleteObject)
      return false;

    llvm::APInt APEndOffset;
    if (isBaseAnAllocSizeCall(LVal.getLValueBase()) &&
        getBytesReturnedByAllocSizeCall(Info.Ctx, LVal, APEndOffset))
      return convertUnsignedAPIntToCharUnits(APEndOffset, EndOffset);

    if (LVal.InvalidBase)
      return false;

    QualType BaseTy = getObjectType(LVal.getLValueBase());
    return CheckedHandleSizeof(BaseTy, EndOffset);
  }

  // We want to evaluate the size of a subobject.
  const SubobjectDesignator &Designator = LVal.Designator;

  // The following is a moderately common idiom in C:
  //
  // struct Foo { int a; char c[1]; };
  // struct Foo *F = (struct Foo *)malloc(sizeof(struct Foo) + strlen(Bar));
  // strcpy(&F->c[0], Bar);
  //
  // In order to not break too much legacy code, we need to support it.
  if (isUserWritingOffTheEnd(Info.Ctx, LVal)) {
    // If we can resolve this to an alloc_size call, we can hand that back,
    // because we know for certain how many bytes there are to write to.
    llvm::APInt APEndOffset;
    if (isBaseAnAllocSizeCall(LVal.getLValueBase()) &&
        getBytesReturnedByAllocSizeCall(Info.Ctx, LVal, APEndOffset))
      return convertUnsignedAPIntToCharUnits(APEndOffset, EndOffset);

    // If we cannot determine the size of the initial allocation, then we can't
    // given an accurate upper-bound. However, we are still able to give
    // conservative lower-bounds for Type=3.
    if (Type == 1)
      return false;
  }

  CharUnits BytesPerElem;
  if (!CheckedHandleSizeof(Designator.MostDerivedType, BytesPerElem))
    return false;

  // According to the GCC documentation, we want the size of the subobject
  // denoted by the pointer. But that's not quite right -- what we actually
  // want is the size of the immediately-enclosing array, if there is one.
  int64_t ElemsRemaining;
  if (Designator.MostDerivedIsArrayElement &&
      Designator.Entries.size() == Designator.MostDerivedPathLength) {
    uint64_t ArraySize = Designator.getMostDerivedArraySize();
    uint64_t ArrayIndex = Designator.Entries.back().ArrayIndex;
    ElemsRemaining = ArraySize <= ArrayIndex ? 0 : ArraySize - ArrayIndex;
  } else {
    ElemsRemaining = Designator.isOnePastTheEnd() ? 0 : 1;
  }

  EndOffset = LVal.getLValueOffset() + BytesPerElem * ElemsRemaining;
  return true;
}

/// Tries to evaluate the __builtin_object_size for @p E. If successful,
/// returns true and stores the result in @p Size.
///
/// If @p WasError is non-null, this will report whether the failure to evaluate
/// is to be treated as an Error in IntExprEvaluator.
static bool tryEvaluateBuiltinObjectSize(const Expr *E, unsigned Type,
                                         EvalInfo &Info, uint64_t &Size) {
  // Determine the denoted object.
  LValue LVal;
  {
    // The operand of __builtin_object_size is never evaluated for side-effects.
    // If there are any, but we can determine the pointed-to object anyway, then
    // ignore the side-effects.
    SpeculativeEvaluationRAII SpeculativeEval(Info);
    IgnoreSideEffectsRAII Fold(Info);

    if (E->isGLValue()) {
      // It's possible for us to be given GLValues if we're called via
      // Expr::tryEvaluateObjectSize.
      APValue RVal;
      if (!EvaluateAsRValue(Info, E, RVal))
        return false;
      LVal.setFrom(Info.Ctx, RVal);
    } else if (!EvaluatePointer(ignorePointerCastsAndParens(E), LVal, Info,
                                /*InvalidBaseOK=*/true))
      return false;
  }

  // If we point to before the start of the object, there are no accessible
  // bytes.
  if (LVal.getLValueOffset().isNegative()) {
    Size = 0;
    return true;
  }

  CharUnits EndOffset;
  if (!determineEndOffset(Info, E->getExprLoc(), Type, LVal, EndOffset))
    return false;

  // If we've fallen outside of the end offset, just pretend there's nothing to
  // write to/read from.
  if (EndOffset <= LVal.getLValueOffset())
    Size = 0;
  else
    Size = (EndOffset - LVal.getLValueOffset()).getQuantity();
  return true;
}

bool IntExprEvaluator::VisitConstantExpr(const ConstantExpr *E) {
  llvm::SaveAndRestore<bool> InConstantContext(Info.InConstantContext, true);
  return ExprEvaluatorBaseTy::VisitConstantExpr(E);
}

bool IntExprEvaluator::VisitCallExpr(const CallExpr *E) {
  if (unsigned BuiltinOp = E->getBuiltinCallee())
    return VisitBuiltinCallExpr(E, BuiltinOp);

  return ExprEvaluatorBaseTy::VisitCallExpr(E);
}

bool IntExprEvaluator::VisitBuiltinCallExpr(const CallExpr *E,
                                            unsigned BuiltinOp) {
  switch (unsigned BuiltinOp = E->getBuiltinCallee()) {
  default:
    return ExprEvaluatorBaseTy::VisitCallExpr(E);

  case Builtin::BI__builtin_object_size: {
    // The type was checked when we built the expression.
    unsigned Type =
        E->getArg(1)->EvaluateKnownConstInt(Info.Ctx).getZExtValue();
    assert(Type <= 3 && "unexpected type");

    uint64_t Size;
    if (tryEvaluateBuiltinObjectSize(E->getArg(0), Type, Info, Size))
      return Success(Size, E);

    if (E->getArg(0)->HasSideEffects(Info.Ctx))
      return Success((Type & 2) ? 0 : -1, E);

    // Expression had no side effects, but we couldn't statically determine the
    // size of the referenced object.
    switch (Info.EvalMode) {
    case EvalInfo::EM_ConstantExpression:
    case EvalInfo::EM_PotentialConstantExpression:
    case EvalInfo::EM_ConstantFold:
    case EvalInfo::EM_EvaluateForOverflow:
    case EvalInfo::EM_IgnoreSideEffects:
      // Leave it to IR generation.
      return Error(E);
    case EvalInfo::EM_ConstantExpressionUnevaluated:
    case EvalInfo::EM_PotentialConstantExpressionUnevaluated:
      // Reduce it to a constant now.
      return Success((Type & 2) ? 0 : -1, E);
    }

    llvm_unreachable("unexpected EvalMode");
  }

  case Builtin::BI__builtin_os_log_format_buffer_size: {
    analyze_os_log::OSLogBufferLayout Layout;
    analyze_os_log::computeOSLogBufferLayout(Info.Ctx, E, Layout);
    return Success(Layout.size().getQuantity(), E);
  }

  case Builtin::BI__builtin_bswap16:
  case Builtin::BI__builtin_bswap32:
  case Builtin::BI__builtin_bswap64: {
    APSInt Val;
    if (!EvaluateInteger(E->getArg(0), Val, Info))
      return false;

    return Success(Val.byteSwap(), E);
  }

  case Builtin::BI__builtin_classify_type:
    return Success((int)EvaluateBuiltinClassifyType(E, Info.getLangOpts()), E);

  case Builtin::BI__builtin_clrsb:
  case Builtin::BI__builtin_clrsbl:
  case Builtin::BI__builtin_clrsbll: {
    APSInt Val;
    if (!EvaluateInteger(E->getArg(0), Val, Info))
      return false;

    return Success(Val.getBitWidth() - Val.getMinSignedBits(), E);
  }

  case Builtin::BI__builtin_clz:
  case Builtin::BI__builtin_clzl:
  case Builtin::BI__builtin_clzll:
  case Builtin::BI__builtin_clzs: {
    APSInt Val;
    if (!EvaluateInteger(E->getArg(0), Val, Info))
      return false;
    if (!Val)
      return Error(E);

    return Success(Val.countLeadingZeros(), E);
  }

  case Builtin::BI__builtin_constant_p: {
    auto Arg = E->getArg(0);
    if (EvaluateBuiltinConstantP(Info.Ctx, Arg))
      return Success(true, E);
    auto ArgTy = Arg->IgnoreImplicit()->getType();
    if (!Info.InConstantContext && !Arg->HasSideEffects(Info.Ctx) &&
        !ArgTy->isAggregateType() && !ArgTy->isPointerType()) {
      // We can delay calculation of __builtin_constant_p until after
      // inlining. Note: This diagnostic won't be shown to the user.
      Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
      return false;
    }
    return Success(false, E);
  }

  case Builtin::BI__builtin_ctz:
  case Builtin::BI__builtin_ctzl:
  case Builtin::BI__builtin_ctzll:
  case Builtin::BI__builtin_ctzs: {
    APSInt Val;
    if (!EvaluateInteger(E->getArg(0), Val, Info))
      return false;
    if (!Val)
      return Error(E);

    return Success(Val.countTrailingZeros(), E);
  }

  case Builtin::BI__builtin_eh_return_data_regno: {
    int Operand = E->getArg(0)->EvaluateKnownConstInt(Info.Ctx).getZExtValue();
    Operand = Info.Ctx.getTargetInfo().getEHDataRegisterNumber(Operand);
    return Success(Operand, E);
  }

  case Builtin::BI__builtin_expect:
    return Visit(E->getArg(0));

  case Builtin::BI__builtin_ffs:
  case Builtin::BI__builtin_ffsl:
  case Builtin::BI__builtin_ffsll: {
    APSInt Val;
    if (!EvaluateInteger(E->getArg(0), Val, Info))
      return false;

    unsigned N = Val.countTrailingZeros();
    return Success(N == Val.getBitWidth() ? 0 : N + 1, E);
  }

  case Builtin::BI__builtin_fpclassify: {
    APFloat Val(0.0);
    if (!EvaluateFloat(E->getArg(5), Val, Info))
      return false;
    unsigned Arg;
    switch (Val.getCategory()) {
    case APFloat::fcNaN: Arg = 0; break;
    case APFloat::fcInfinity: Arg = 1; break;
    case APFloat::fcNormal: Arg = Val.isDenormal() ? 3 : 2; break;
    case APFloat::fcZero: Arg = 4; break;
    }
    return Visit(E->getArg(Arg));
  }

  case Builtin::BI__builtin_isinf_sign: {
    APFloat Val(0.0);
    return EvaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isInfinity() ? (Val.isNegative() ? -1 : 1) : 0, E);
  }

  case Builtin::BI__builtin_isinf: {
    APFloat Val(0.0);
    return EvaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isInfinity() ? 1 : 0, E);
  }

  case Builtin::BI__builtin_isfinite: {
    APFloat Val(0.0);
    return EvaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isFinite() ? 1 : 0, E);
  }

  case Builtin::BI__builtin_isnan: {
    APFloat Val(0.0);
    return EvaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isNaN() ? 1 : 0, E);
  }

  case Builtin::BI__builtin_isnormal: {
    APFloat Val(0.0);
    return EvaluateFloat(E->getArg(0), Val, Info) &&
           Success(Val.isNormal() ? 1 : 0, E);
  }

  case Builtin::BI__builtin_parity:
  case Builtin::BI__builtin_parityl:
  case Builtin::BI__builtin_parityll: {
    APSInt Val;
    if (!EvaluateInteger(E->getArg(0), Val, Info))
      return false;

    return Success(Val.countPopulation() % 2, E);
  }

  case Builtin::BI__builtin_popcount:
  case Builtin::BI__builtin_popcountl:
  case Builtin::BI__builtin_popcountll: {
    APSInt Val;
    if (!EvaluateInteger(E->getArg(0), Val, Info))
      return false;

    return Success(Val.countPopulation(), E);
  }

  case Builtin::BIstrlen:
  case Builtin::BIwcslen:
    // A call to strlen is not a constant expression.
    if (Info.getLangOpts().CPlusPlus11)
      Info.CCEDiag(E, diag::note_constexpr_invalid_function)
        << /*isConstexpr*/0 << /*isConstructor*/0
        << (std::string("'") + Info.Ctx.BuiltinInfo.getName(BuiltinOp) + "'");
    else
      Info.CCEDiag(E, diag::note_invalid_subexpr_in_const_expr);
    LLVM_FALLTHROUGH;
  case Builtin::BI__builtin_strlen:
  case Builtin::BI__builtin_wcslen: {
    // As an extension, we support __builtin_strlen() as a constant expression,
    // and support folding strlen() to a constant.
    LValue String;
    if (!EvaluatePointer(E->getArg(0), String, Info))
      return false;

    QualType CharTy = E->getArg(0)->getType()->getPointeeType();

    // Fast path: if it's a string literal, search the string value.
    if (const StringLiteral *S = dyn_cast_or_null<StringLiteral>(
            String.getLValueBase().dyn_cast<const Expr *>())) {
      // The string literal may have embedded null characters. Find the first
      // one and truncate there.
      StringRef Str = S->getBytes();
      int64_t Off = String.Offset.getQuantity();
      if (Off >= 0 && (uint64_t)Off <= (uint64_t)Str.size() &&
          S->getCharByteWidth() == 1 &&
          // FIXME: Add fast-path for wchar_t too.
          Info.Ctx.hasSameUnqualifiedType(CharTy, Info.Ctx.CharTy)) {
        Str = Str.substr(Off);

        StringRef::size_type Pos = Str.find(0);
        if (Pos != StringRef::npos)
          Str = Str.substr(0, Pos);

        return Success(Str.size(), E);
      }

      // Fall through to slow path to issue appropriate diagnostic.
    }

    // Slow path: scan the bytes of the string looking for the terminating 0.
    for (uint64_t Strlen = 0; /**/; ++Strlen) {
      APValue Char;
      if (!handleLValueToRValueConversion(Info, E, CharTy, String, Char) ||
          !Char.isInt())
        return false;
      if (!Char.getInt())
        return Success(Strlen, E);
      if (!HandleLValueArrayAdjustment(Info, E, String, CharTy, 1))
        return false;
    }
  }

  case Builtin::BIstrcmp:
  case Builtin::BIwcscmp:
  case Builtin::BIstrncmp:
  case Builtin::BIwcsncmp:
  case Builtin::BImemcmp:
  case Builtin::BIwmemcmp:
    // A call to strlen is not a constant expression.
    if (Info.getLangOpts().CPlusPlus11)
      Info.CCEDiag(E, diag::note_constexpr_invalid_function)
        << /*isConstexpr*/0 << /*isConstructor*/0
        << (std::string("'") + Info.Ctx.BuiltinInfo.getName(BuiltinOp) + "'");
    else
      Info.CCEDiag(E, diag::note_invalid_subexpr_in_const_expr);
    LLVM_FALLTHROUGH;
  case Builtin::BI__builtin_strcmp:
  case Builtin::BI__builtin_wcscmp:
  case Builtin::BI__builtin_strncmp:
  case Builtin::BI__builtin_wcsncmp:
  case Builtin::BI__builtin_memcmp:
  case Builtin::BI__builtin_wmemcmp: {
    LValue String1, String2;
    if (!EvaluatePointer(E->getArg(0), String1, Info) ||
        !EvaluatePointer(E->getArg(1), String2, Info))
      return false;

    uint64_t MaxLength = uint64_t(-1);
    if (BuiltinOp != Builtin::BIstrcmp &&
        BuiltinOp != Builtin::BIwcscmp &&
        BuiltinOp != Builtin::BI__builtin_strcmp &&
        BuiltinOp != Builtin::BI__builtin_wcscmp) {
      APSInt N;
      if (!EvaluateInteger(E->getArg(2), N, Info))
        return false;
      MaxLength = N.getExtValue();
    }

    // Empty substrings compare equal by definition.
    if (MaxLength == 0u)
      return Success(0, E);

    if (!String1.checkNullPointerForFoldAccess(Info, E, AK_Read) ||
        !String2.checkNullPointerForFoldAccess(Info, E, AK_Read) ||
        String1.Designator.Invalid || String2.Designator.Invalid)
      return false;

    QualType CharTy1 = String1.Designator.getType(Info.Ctx);
    QualType CharTy2 = String2.Designator.getType(Info.Ctx);

    bool IsRawByte = BuiltinOp == Builtin::BImemcmp ||
                     BuiltinOp == Builtin::BI__builtin_memcmp;

    assert(IsRawByte ||
           (Info.Ctx.hasSameUnqualifiedType(
                CharTy1, E->getArg(0)->getType()->getPointeeType()) &&
            Info.Ctx.hasSameUnqualifiedType(CharTy1, CharTy2)));

    const auto &ReadCurElems = [&](APValue &Char1, APValue &Char2) {
      return handleLValueToRValueConversion(Info, E, CharTy1, String1, Char1) &&
             handleLValueToRValueConversion(Info, E, CharTy2, String2, Char2) &&
             Char1.isInt() && Char2.isInt();
    };
    const auto &AdvanceElems = [&] {
      return HandleLValueArrayAdjustment(Info, E, String1, CharTy1, 1) &&
             HandleLValueArrayAdjustment(Info, E, String2, CharTy2, 1);
    };

    if (IsRawByte) {
      uint64_t BytesRemaining = MaxLength;
      // Pointers to const void may point to objects of incomplete type.
      if (CharTy1->isIncompleteType()) {
        Info.FFDiag(E, diag::note_constexpr_ltor_incomplete_type) << CharTy1;
        return false;
      }
      if (CharTy2->isIncompleteType()) {
        Info.FFDiag(E, diag::note_constexpr_ltor_incomplete_type) << CharTy2;
        return false;
      }
      uint64_t CharTy1Width{Info.Ctx.getTypeSize(CharTy1)};
      CharUnits CharTy1Size = Info.Ctx.toCharUnitsFromBits(CharTy1Width);
      // Give up on comparing between elements with disparate widths.
      if (CharTy1Size != Info.Ctx.getTypeSizeInChars(CharTy2))
        return false;
      uint64_t BytesPerElement = CharTy1Size.getQuantity();
      assert(BytesRemaining && "BytesRemaining should not be zero: the "
                               "following loop considers at least one element");
      while (true) {
        APValue Char1, Char2;
        if (!ReadCurElems(Char1, Char2))
          return false;
        // We have compatible in-memory widths, but a possible type and
        // (for `bool`) internal representation mismatch.
        // Assuming two's complement representation, including 0 for `false` and
        // 1 for `true`, we can check an appropriate number of elements for
        // equality even if they are not byte-sized.
        APSInt Char1InMem = Char1.getInt().extOrTrunc(CharTy1Width);
        APSInt Char2InMem = Char2.getInt().extOrTrunc(CharTy1Width);
        if (Char1InMem.ne(Char2InMem)) {
          // If the elements are byte-sized, then we can produce a three-way
          // comparison result in a straightforward manner.
          if (BytesPerElement == 1u) {
            // memcmp always compares unsigned chars.
            return Success(Char1InMem.ult(Char2InMem) ? -1 : 1, E);
          }
          // The result is byte-order sensitive, and we have multibyte elements.
          // FIXME: We can compare the remaining bytes in the correct order.
          return false;
        }
        if (!AdvanceElems())
          return false;
        if (BytesRemaining <= BytesPerElement)
          break;
        BytesRemaining -= BytesPerElement;
      }
      // Enough elements are equal to account for the memcmp limit.
      return Success(0, E);
    }

    bool StopAtNull = (BuiltinOp != Builtin::BImemcmp &&
                       BuiltinOp != Builtin::BIwmemcmp &&
                       BuiltinOp != Builtin::BI__builtin_memcmp &&
                       BuiltinOp != Builtin::BI__builtin_wmemcmp);
    bool IsWide = BuiltinOp == Builtin::BIwcscmp ||
                  BuiltinOp == Builtin::BIwcsncmp ||
                  BuiltinOp == Builtin::BIwmemcmp ||
                  BuiltinOp == Builtin::BI__builtin_wcscmp ||
                  BuiltinOp == Builtin::BI__builtin_wcsncmp ||
                  BuiltinOp == Builtin::BI__builtin_wmemcmp;

    for (; MaxLength; --MaxLength) {
      APValue Char1, Char2;
      if (!ReadCurElems(Char1, Char2))
        return false;
      if (Char1.getInt() != Char2.getInt()) {
        if (IsWide) // wmemcmp compares with wchar_t signedness.
          return Success(Char1.getInt() < Char2.getInt() ? -1 : 1, E);
        // memcmp always compares unsigned chars.
        return Success(Char1.getInt().ult(Char2.getInt()) ? -1 : 1, E);
      }
      if (StopAtNull && !Char1.getInt())
        return Success(0, E);
      assert(!(StopAtNull && !Char2.getInt()));
      if (!AdvanceElems())
        return false;
    }
    // We hit the strncmp / memcmp limit.
    return Success(0, E);
  }

  case Builtin::BI__atomic_always_lock_free:
  case Builtin::BI__atomic_is_lock_free:
  case Builtin::BI__c11_atomic_is_lock_free: {
    APSInt SizeVal;
    if (!EvaluateInteger(E->getArg(0), SizeVal, Info))
      return false;

    // For __atomic_is_lock_free(sizeof(_Atomic(T))), if the size is a power
    // of two less than the maximum inline atomic width, we know it is
    // lock-free.  If the size isn't a power of two, or greater than the
    // maximum alignment where we promote atomics, we know it is not lock-free
    // (at least not in the sense of atomic_is_lock_free).  Otherwise,
    // the answer can only be determined at runtime; for example, 16-byte
    // atomics have lock-free implementations on some, but not all,
    // x86-64 processors.

    // Check power-of-two.
    CharUnits Size = CharUnits::fromQuantity(SizeVal.getZExtValue());
    if (Size.isPowerOfTwo()) {
      // Check against inlining width.
      unsigned InlineWidthBits =
          Info.Ctx.getTargetInfo().getMaxAtomicInlineWidth();
      if (Size <= Info.Ctx.toCharUnitsFromBits(InlineWidthBits)) {
        if (BuiltinOp == Builtin::BI__c11_atomic_is_lock_free ||
            Size == CharUnits::One() ||
            E->getArg(1)->isNullPointerConstant(Info.Ctx,
                                                Expr::NPC_NeverValueDependent))
          // OK, we will inline appropriately-aligned operations of this size,
          // and _Atomic(T) is appropriately-aligned.
          return Success(1, E);

        QualType PointeeType = E->getArg(1)->IgnoreImpCasts()->getType()->
          castAs<PointerType>()->getPointeeType();
        if (!PointeeType->isIncompleteType() &&
            Info.Ctx.getTypeAlignInChars(PointeeType) >= Size) {
          // OK, we will inline operations on this object.
          return Success(1, E);
        }
      }
    }

    return BuiltinOp == Builtin::BI__atomic_always_lock_free ?
        Success(0, E) : Error(E);
  }
  case Builtin::BIomp_is_initial_device:
    // We can decide statically which value the runtime would return if called.
    return Success(Info.getLangOpts().OpenMPIsDevice ? 0 : 1, E);
  case Builtin::BI__builtin_add_overflow:
  case Builtin::BI__builtin_sub_overflow:
  case Builtin::BI__builtin_mul_overflow:
  case Builtin::BI__builtin_sadd_overflow:
  case Builtin::BI__builtin_uadd_overflow:
  case Builtin::BI__builtin_uaddl_overflow:
  case Builtin::BI__builtin_uaddll_overflow:
  case Builtin::BI__builtin_usub_overflow:
  case Builtin::BI__builtin_usubl_overflow:
  case Builtin::BI__builtin_usubll_overflow:
  case Builtin::BI__builtin_umul_overflow:
  case Builtin::BI__builtin_umull_overflow:
  case Builtin::BI__builtin_umulll_overflow:
  case Builtin::BI__builtin_saddl_overflow:
  case Builtin::BI__builtin_saddll_overflow:
  case Builtin::BI__builtin_ssub_overflow:
  case Builtin::BI__builtin_ssubl_overflow:
  case Builtin::BI__builtin_ssubll_overflow:
  case Builtin::BI__builtin_smul_overflow:
  case Builtin::BI__builtin_smull_overflow:
  case Builtin::BI__builtin_smulll_overflow: {
    LValue ResultLValue;
    APSInt LHS, RHS;

    QualType ResultType = E->getArg(2)->getType()->getPointeeType();
    if (!EvaluateInteger(E->getArg(0), LHS, Info) ||
        !EvaluateInteger(E->getArg(1), RHS, Info) ||
        !EvaluatePointer(E->getArg(2), ResultLValue, Info))
      return false;

    APSInt Result;
    bool DidOverflow = false;

    // If the types don't have to match, enlarge all 3 to the largest of them.
    if (BuiltinOp == Builtin::BI__builtin_add_overflow ||
        BuiltinOp == Builtin::BI__builtin_sub_overflow ||
        BuiltinOp == Builtin::BI__builtin_mul_overflow) {
      bool IsSigned = LHS.isSigned() || RHS.isSigned() ||
                      ResultType->isSignedIntegerOrEnumerationType();
      bool AllSigned = LHS.isSigned() && RHS.isSigned() &&
                      ResultType->isSignedIntegerOrEnumerationType();
      uint64_t LHSSize = LHS.getBitWidth();
      uint64_t RHSSize = RHS.getBitWidth();
      uint64_t ResultSize = Info.Ctx.getTypeSize(ResultType);
      uint64_t MaxBits = std::max(std::max(LHSSize, RHSSize), ResultSize);

      // Add an additional bit if the signedness isn't uniformly agreed to. We
      // could do this ONLY if there is a signed and an unsigned that both have
      // MaxBits, but the code to check that is pretty nasty.  The issue will be
      // caught in the shrink-to-result later anyway.
      if (IsSigned && !AllSigned)
        ++MaxBits;

      LHS = APSInt(IsSigned ? LHS.sextOrSelf(MaxBits) : LHS.zextOrSelf(MaxBits),
                   !IsSigned);
      RHS = APSInt(IsSigned ? RHS.sextOrSelf(MaxBits) : RHS.zextOrSelf(MaxBits),
                   !IsSigned);
      Result = APSInt(MaxBits, !IsSigned);
    }

    // Find largest int.
    switch (BuiltinOp) {
    default:
      llvm_unreachable("Invalid value for BuiltinOp");
    case Builtin::BI__builtin_add_overflow:
    case Builtin::BI__builtin_sadd_overflow:
    case Builtin::BI__builtin_saddl_overflow:
    case Builtin::BI__builtin_saddll_overflow:
    case Builtin::BI__builtin_uadd_overflow:
    case Builtin::BI__builtin_uaddl_overflow:
    case Builtin::BI__builtin_uaddll_overflow:
      Result = LHS.isSigned() ? LHS.sadd_ov(RHS, DidOverflow)
                              : LHS.uadd_ov(RHS, DidOverflow);
      break;
    case Builtin::BI__builtin_sub_overflow:
    case Builtin::BI__builtin_ssub_overflow:
    case Builtin::BI__builtin_ssubl_overflow:
    case Builtin::BI__builtin_ssubll_overflow:
    case Builtin::BI__builtin_usub_overflow:
    case Builtin::BI__builtin_usubl_overflow:
    case Builtin::BI__builtin_usubll_overflow:
      Result = LHS.isSigned() ? LHS.ssub_ov(RHS, DidOverflow)
                              : LHS.usub_ov(RHS, DidOverflow);
      break;
    case Builtin::BI__builtin_mul_overflow:
    case Builtin::BI__builtin_smul_overflow:
    case Builtin::BI__builtin_smull_overflow:
    case Builtin::BI__builtin_smulll_overflow:
    case Builtin::BI__builtin_umul_overflow:
    case Builtin::BI__builtin_umull_overflow:
    case Builtin::BI__builtin_umulll_overflow:
      Result = LHS.isSigned() ? LHS.smul_ov(RHS, DidOverflow)
                              : LHS.umul_ov(RHS, DidOverflow);
      break;
    }

    // In the case where multiple sizes are allowed, truncate and see if
    // the values are the same.
    if (BuiltinOp == Builtin::BI__builtin_add_overflow ||
        BuiltinOp == Builtin::BI__builtin_sub_overflow ||
        BuiltinOp == Builtin::BI__builtin_mul_overflow) {
      // APSInt doesn't have a TruncOrSelf, so we use extOrTrunc instead,
      // since it will give us the behavior of a TruncOrSelf in the case where
      // its parameter <= its size.  We previously set Result to be at least the
      // type-size of the result, so getTypeSize(ResultType) <= Result.BitWidth
      // will work exactly like TruncOrSelf.
      APSInt Temp = Result.extOrTrunc(Info.Ctx.getTypeSize(ResultType));
      Temp.setIsSigned(ResultType->isSignedIntegerOrEnumerationType());

      if (!APSInt::isSameValue(Temp, Result))
        DidOverflow = true;
      Result = Temp;
    }

    APValue APV{Result};
    if (!handleAssignment(Info, E, ResultLValue, ResultType, APV))
      return false;
    return Success(DidOverflow, E);
  }
  }
}

/// Determine whether this is a pointer past the end of the complete
/// object referred to by the lvalue.
static bool isOnePastTheEndOfCompleteObject(const ASTContext &Ctx,
                                            const LValue &LV) {
  // A null pointer can be viewed as being "past the end" but we don't
  // choose to look at it that way here.
  if (!LV.getLValueBase())
    return false;

  // If the designator is valid and refers to a subobject, we're not pointing
  // past the end.
  if (!LV.getLValueDesignator().Invalid &&
      !LV.getLValueDesignator().isOnePastTheEnd())
    return false;

  // A pointer to an incomplete type might be past-the-end if the type's size is
  // zero.  We cannot tell because the type is incomplete.
  QualType Ty = getType(LV.getLValueBase());
  if (Ty->isIncompleteType())
    return true;

  // We're a past-the-end pointer if we point to the byte after the object,
  // no matter what our type or path is.
  auto Size = Ctx.getTypeSizeInChars(Ty);
  return LV.getLValueOffset() == Size;
}

namespace {

/// Data recursive integer evaluator of certain binary operators.
///
/// We use a data recursive algorithm for binary operators so that we are able
/// to handle extreme cases of chained binary operators without causing stack
/// overflow.
class DataRecursiveIntBinOpEvaluator {
  struct EvalResult {
    APValue Val;
    bool Failed;

    EvalResult() : Failed(false) { }

    void swap(EvalResult &RHS) {
      Val.swap(RHS.Val);
      Failed = RHS.Failed;
      RHS.Failed = false;
    }
  };

  struct Job {
    const Expr *E;
    EvalResult LHSResult; // meaningful only for binary operator expression.
    enum { AnyExprKind, BinOpKind, BinOpVisitedLHSKind } Kind;

    Job() = default;
    Job(Job &&) = default;

    void startSpeculativeEval(EvalInfo &Info) {
      SpecEvalRAII = SpeculativeEvaluationRAII(Info);
    }

  private:
    SpeculativeEvaluationRAII SpecEvalRAII;
  };

  SmallVector<Job, 16> Queue;

  IntExprEvaluator &IntEval;
  EvalInfo &Info;
  APValue &FinalResult;

public:
  DataRecursiveIntBinOpEvaluator(IntExprEvaluator &IntEval, APValue &Result)
    : IntEval(IntEval), Info(IntEval.getEvalInfo()), FinalResult(Result) { }

  /// True if \param E is a binary operator that we are going to handle
  /// data recursively.
  /// We handle binary operators that are comma, logical, or that have operands
  /// with integral or enumeration type.
  static bool shouldEnqueue(const BinaryOperator *E) {
    return E->getOpcode() == BO_Comma || E->isLogicalOp() ||
           (E->isRValue() && E->getType()->isIntegralOrEnumerationType() &&
            E->getLHS()->getType()->isIntegralOrEnumerationType() &&
            E->getRHS()->getType()->isIntegralOrEnumerationType());
  }

  bool Traverse(const BinaryOperator *E) {
    enqueue(E);
    EvalResult PrevResult;
    while (!Queue.empty())
      process(PrevResult);

    if (PrevResult.Failed) return false;

    FinalResult.swap(PrevResult.Val);
    return true;
  }

private:
  bool Success(uint64_t Value, const Expr *E, APValue &Result) {
    return IntEval.Success(Value, E, Result);
  }
  bool Success(const APSInt &Value, const Expr *E, APValue &Result) {
    return IntEval.Success(Value, E, Result);
  }
  bool Error(const Expr *E) {
    return IntEval.Error(E);
  }
  bool Error(const Expr *E, diag::kind D) {
    return IntEval.Error(E, D);
  }

  OptionalDiagnostic CCEDiag(const Expr *E, diag::kind D) {
    return Info.CCEDiag(E, D);
  }

  // Returns true if visiting the RHS is necessary, false otherwise.
  bool VisitBinOpLHSOnly(EvalResult &LHSResult, const BinaryOperator *E,
                         bool &SuppressRHSDiags);

  bool VisitBinOp(const EvalResult &LHSResult, const EvalResult &RHSResult,
                  const BinaryOperator *E, APValue &Result);

  void EvaluateExpr(const Expr *E, EvalResult &Result) {
    Result.Failed = !Evaluate(Result.Val, Info, E);
    if (Result.Failed)
      Result.Val = APValue();
  }

  void process(EvalResult &Result);

  void enqueue(const Expr *E) {
    E = E->IgnoreParens();
    Queue.resize(Queue.size()+1);
    Queue.back().E = E;
    Queue.back().Kind = Job::AnyExprKind;
  }
};

}

bool DataRecursiveIntBinOpEvaluator::
       VisitBinOpLHSOnly(EvalResult &LHSResult, const BinaryOperator *E,
                         bool &SuppressRHSDiags) {
  if (E->getOpcode() == BO_Comma) {
    // Ignore LHS but note if we could not evaluate it.
    if (LHSResult.Failed)
      return Info.noteSideEffect();
    return true;
  }

  if (E->isLogicalOp()) {
    bool LHSAsBool;
    if (!LHSResult.Failed && HandleConversionToBool(LHSResult.Val, LHSAsBool)) {
      // We were able to evaluate the LHS, see if we can get away with not
      // evaluating the RHS: 0 && X -> 0, 1 || X -> 1
      if (LHSAsBool == (E->getOpcode() == BO_LOr)) {
        Success(LHSAsBool, E, LHSResult.Val);
        return false; // Ignore RHS
      }
    } else {
      LHSResult.Failed = true;

      // Since we weren't able to evaluate the left hand side, it
      // might have had side effects.
      if (!Info.noteSideEffect())
        return false;

      // We can't evaluate the LHS; however, sometimes the result
      // is determined by the RHS: X && 0 -> 0, X || 1 -> 1.
      // Don't ignore RHS and suppress diagnostics from this arm.
      SuppressRHSDiags = true;
    }

    return true;
  }

  assert(E->getLHS()->getType()->isIntegralOrEnumerationType() &&
         E->getRHS()->getType()->isIntegralOrEnumerationType());

  if (LHSResult.Failed && !Info.noteFailure())
    return false; // Ignore RHS;

  return true;
}

static void addOrSubLValueAsInteger(APValue &LVal, const APSInt &Index,
                                    bool IsSub) {
  // Compute the new offset in the appropriate width, wrapping at 64 bits.
  // FIXME: When compiling for a 32-bit target, we should use 32-bit
  // offsets.
  assert(!LVal.hasLValuePath() && "have designator for integer lvalue");
  CharUnits &Offset = LVal.getLValueOffset();
  uint64_t Offset64 = Offset.getQuantity();
  uint64_t Index64 = Index.extOrTrunc(64).getZExtValue();
  Offset = CharUnits::fromQuantity(IsSub ? Offset64 - Index64
                                         : Offset64 + Index64);
}

bool DataRecursiveIntBinOpEvaluator::
       VisitBinOp(const EvalResult &LHSResult, const EvalResult &RHSResult,
                  const BinaryOperator *E, APValue &Result) {
  if (E->getOpcode() == BO_Comma) {
    if (RHSResult.Failed)
      return false;
    Result = RHSResult.Val;
    return true;
  }

  if (E->isLogicalOp()) {
    bool lhsResult, rhsResult;
    bool LHSIsOK = HandleConversionToBool(LHSResult.Val, lhsResult);
    bool RHSIsOK = HandleConversionToBool(RHSResult.Val, rhsResult);

    if (LHSIsOK) {
      if (RHSIsOK) {
        if (E->getOpcode() == BO_LOr)
          return Success(lhsResult || rhsResult, E, Result);
        else
          return Success(lhsResult && rhsResult, E, Result);
      }
    } else {
      if (RHSIsOK) {
        // We can't evaluate the LHS; however, sometimes the result
        // is determined by the RHS: X && 0 -> 0, X || 1 -> 1.
        if (rhsResult == (E->getOpcode() == BO_LOr))
          return Success(rhsResult, E, Result);
      }
    }

    return false;
  }

  assert(E->getLHS()->getType()->isIntegralOrEnumerationType() &&
         E->getRHS()->getType()->isIntegralOrEnumerationType());

  if (LHSResult.Failed || RHSResult.Failed)
    return false;

  const APValue &LHSVal = LHSResult.Val;
  const APValue &RHSVal = RHSResult.Val;

  // Handle cases like (unsigned long)&a + 4.
  if (E->isAdditiveOp() && LHSVal.isLValue() && RHSVal.isInt()) {
    Result = LHSVal;
    addOrSubLValueAsInteger(Result, RHSVal.getInt(), E->getOpcode() == BO_Sub);
    return true;
  }

  // Handle cases like 4 + (unsigned long)&a
  if (E->getOpcode() == BO_Add &&
      RHSVal.isLValue() && LHSVal.isInt()) {
    Result = RHSVal;
    addOrSubLValueAsInteger(Result, LHSVal.getInt(), /*IsSub*/false);
    return true;
  }

  if (E->getOpcode() == BO_Sub && LHSVal.isLValue() && RHSVal.isLValue()) {
    // Handle (intptr_t)&&A - (intptr_t)&&B.
    if (!LHSVal.getLValueOffset().isZero() ||
        !RHSVal.getLValueOffset().isZero())
      return false;
    const Expr *LHSExpr = LHSVal.getLValueBase().dyn_cast<const Expr*>();
    const Expr *RHSExpr = RHSVal.getLValueBase().dyn_cast<const Expr*>();
    if (!LHSExpr || !RHSExpr)
      return false;
    const AddrLabelExpr *LHSAddrExpr = dyn_cast<AddrLabelExpr>(LHSExpr);
    const AddrLabelExpr *RHSAddrExpr = dyn_cast<AddrLabelExpr>(RHSExpr);
    if (!LHSAddrExpr || !RHSAddrExpr)
      return false;
    // Make sure both labels come from the same function.
    if (LHSAddrExpr->getLabel()->getDeclContext() !=
        RHSAddrExpr->getLabel()->getDeclContext())
      return false;
    Result = APValue(LHSAddrExpr, RHSAddrExpr);
    return true;
  }

  // All the remaining cases expect both operands to be an integer
  if (!LHSVal.isInt() || !RHSVal.isInt())
    return Error(E);

  // Set up the width and signedness manually, in case it can't be deduced
  // from the operation we're performing.
  // FIXME: Don't do this in the cases where we can deduce it.
  APSInt Value(Info.Ctx.getIntWidth(E->getType()),
               E->getType()->isUnsignedIntegerOrEnumerationType());
  if (!handleIntIntBinOp(Info, E, LHSVal.getInt(), E->getOpcode(),
                         RHSVal.getInt(), Value))
    return false;
  return Success(Value, E, Result);
}

void DataRecursiveIntBinOpEvaluator::process(EvalResult &Result) {
  Job &job = Queue.back();

  switch (job.Kind) {
    case Job::AnyExprKind: {
      if (const BinaryOperator *Bop = dyn_cast<BinaryOperator>(job.E)) {
        if (shouldEnqueue(Bop)) {
          job.Kind = Job::BinOpKind;
          enqueue(Bop->getLHS());
          return;
        }
      }

      EvaluateExpr(job.E, Result);
      Queue.pop_back();
      return;
    }

    case Job::BinOpKind: {
      const BinaryOperator *Bop = cast<BinaryOperator>(job.E);
      bool SuppressRHSDiags = false;
      if (!VisitBinOpLHSOnly(Result, Bop, SuppressRHSDiags)) {
        Queue.pop_back();
        return;
      }
      if (SuppressRHSDiags)
        job.startSpeculativeEval(Info);
      job.LHSResult.swap(Result);
      job.Kind = Job::BinOpVisitedLHSKind;
      enqueue(Bop->getRHS());
      return;
    }

    case Job::BinOpVisitedLHSKind: {
      const BinaryOperator *Bop = cast<BinaryOperator>(job.E);
      EvalResult RHS;
      RHS.swap(Result);
      Result.Failed = !VisitBinOp(job.LHSResult, RHS, Bop, Result.Val);
      Queue.pop_back();
      return;
    }
  }

  llvm_unreachable("Invalid Job::Kind!");
}

namespace {
/// Used when we determine that we should fail, but can keep evaluating prior to
/// noting that we had a failure.
class DelayedNoteFailureRAII {
  EvalInfo &Info;
  bool NoteFailure;

public:
  DelayedNoteFailureRAII(EvalInfo &Info, bool NoteFailure = true)
      : Info(Info), NoteFailure(NoteFailure) {}
  ~DelayedNoteFailureRAII() {
    if (NoteFailure) {
      bool ContinueAfterFailure = Info.noteFailure();
      (void)ContinueAfterFailure;
      assert(ContinueAfterFailure &&
             "Shouldn't have kept evaluating on failure.");
    }
  }
};
}

template <class SuccessCB, class AfterCB>
static bool
EvaluateComparisonBinaryOperator(EvalInfo &Info, const BinaryOperator *E,
                                 SuccessCB &&Success, AfterCB &&DoAfter) {
  assert(E->isComparisonOp() && "expected comparison operator");
  assert((E->getOpcode() == BO_Cmp ||
          E->getType()->isIntegralOrEnumerationType()) &&
         "unsupported binary expression evaluation");
  auto Error = [&](const Expr *E) {
    Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
    return false;
  };

  using CCR = ComparisonCategoryResult;
  bool IsRelational = E->isRelationalOp();
  bool IsEquality = E->isEqualityOp();
  if (E->getOpcode() == BO_Cmp) {
    const ComparisonCategoryInfo &CmpInfo =
        Info.Ctx.CompCategories.getInfoForType(E->getType());
    IsRelational = CmpInfo.isOrdered();
    IsEquality = CmpInfo.isEquality();
  }

  QualType LHSTy = E->getLHS()->getType();
  QualType RHSTy = E->getRHS()->getType();

  if (LHSTy->isIntegralOrEnumerationType() &&
      RHSTy->isIntegralOrEnumerationType()) {
    APSInt LHS, RHS;
    bool LHSOK = EvaluateInteger(E->getLHS(), LHS, Info);
    if (!LHSOK && !Info.noteFailure())
      return false;
    if (!EvaluateInteger(E->getRHS(), RHS, Info) || !LHSOK)
      return false;
    if (LHS < RHS)
      return Success(CCR::Less, E);
    if (LHS > RHS)
      return Success(CCR::Greater, E);
    return Success(CCR::Equal, E);
  }

  if (LHSTy->isAnyComplexType() || RHSTy->isAnyComplexType()) {
    ComplexValue LHS, RHS;
    bool LHSOK;
    if (E->isAssignmentOp()) {
      LValue LV;
      EvaluateLValue(E->getLHS(), LV, Info);
      LHSOK = false;
    } else if (LHSTy->isRealFloatingType()) {
      LHSOK = EvaluateFloat(E->getLHS(), LHS.FloatReal, Info);
      if (LHSOK) {
        LHS.makeComplexFloat();
        LHS.FloatImag = APFloat(LHS.FloatReal.getSemantics());
      }
    } else {
      LHSOK = EvaluateComplex(E->getLHS(), LHS, Info);
    }
    if (!LHSOK && !Info.noteFailure())
      return false;

    if (E->getRHS()->getType()->isRealFloatingType()) {
      if (!EvaluateFloat(E->getRHS(), RHS.FloatReal, Info) || !LHSOK)
        return false;
      RHS.makeComplexFloat();
      RHS.FloatImag = APFloat(RHS.FloatReal.getSemantics());
    } else if (!EvaluateComplex(E->getRHS(), RHS, Info) || !LHSOK)
      return false;

    if (LHS.isComplexFloat()) {
      APFloat::cmpResult CR_r =
        LHS.getComplexFloatReal().compare(RHS.getComplexFloatReal());
      APFloat::cmpResult CR_i =
        LHS.getComplexFloatImag().compare(RHS.getComplexFloatImag());
      bool IsEqual = CR_r == APFloat::cmpEqual && CR_i == APFloat::cmpEqual;
      return Success(IsEqual ? CCR::Equal : CCR::Nonequal, E);
    } else {
      assert(IsEquality && "invalid complex comparison");
      bool IsEqual = LHS.getComplexIntReal() == RHS.getComplexIntReal() &&
                     LHS.getComplexIntImag() == RHS.getComplexIntImag();
      return Success(IsEqual ? CCR::Equal : CCR::Nonequal, E);
    }
  }

  if (LHSTy->isRealFloatingType() &&
      RHSTy->isRealFloatingType()) {
    APFloat RHS(0.0), LHS(0.0);

    bool LHSOK = EvaluateFloat(E->getRHS(), RHS, Info);
    if (!LHSOK && !Info.noteFailure())
      return false;

    if (!EvaluateFloat(E->getLHS(), LHS, Info) || !LHSOK)
      return false;

    assert(E->isComparisonOp() && "Invalid binary operator!");
    auto GetCmpRes = [&]() {
      switch (LHS.compare(RHS)) {
      case APFloat::cmpEqual:
        return CCR::Equal;
      case APFloat::cmpLessThan:
        return CCR::Less;
      case APFloat::cmpGreaterThan:
        return CCR::Greater;
      case APFloat::cmpUnordered:
        return CCR::Unordered;
      }
      llvm_unreachable("Unrecognised APFloat::cmpResult enum");
    };
    return Success(GetCmpRes(), E);
  }

  if (LHSTy->isPointerType() && RHSTy->isPointerType()) {
    LValue LHSValue, RHSValue;

    bool LHSOK = EvaluatePointer(E->getLHS(), LHSValue, Info);
    if (!LHSOK && !Info.noteFailure())
      return false;

    if (!EvaluatePointer(E->getRHS(), RHSValue, Info) || !LHSOK)
      return false;

    // Reject differing bases from the normal codepath; we special-case
    // comparisons to null.
    if (!HasSameBase(LHSValue, RHSValue)) {
      // Inequalities and subtractions between unrelated pointers have
      // unspecified or undefined behavior.
      if (!IsEquality)
        return Error(E);
      // A constant address may compare equal to the address of a symbol.
      // The one exception is that address of an object cannot compare equal
      // to a null pointer constant.
      if ((!LHSValue.Base && !LHSValue.Offset.isZero()) ||
          (!RHSValue.Base && !RHSValue.Offset.isZero()))
        return Error(E);
      // It's implementation-defined whether distinct literals will have
      // distinct addresses. In clang, the result of such a comparison is
      // unspecified, so it is not a constant expression. However, we do know
      // that the address of a literal will be non-null.
      if ((IsLiteralLValue(LHSValue) || IsLiteralLValue(RHSValue)) &&
          LHSValue.Base && RHSValue.Base)
        return Error(E);
      // We can't tell whether weak symbols will end up pointing to the same
      // object.
      if (IsWeakLValue(LHSValue) || IsWeakLValue(RHSValue))
        return Error(E);
      // We can't compare the address of the start of one object with the
      // past-the-end address of another object, per C++ DR1652.
      if ((LHSValue.Base && LHSValue.Offset.isZero() &&
           isOnePastTheEndOfCompleteObject(Info.Ctx, RHSValue)) ||
          (RHSValue.Base && RHSValue.Offset.isZero() &&
           isOnePastTheEndOfCompleteObject(Info.Ctx, LHSValue)))
        return Error(E);
      // We can't tell whether an object is at the same address as another
      // zero sized object.
      if ((RHSValue.Base && isZeroSized(LHSValue)) ||
          (LHSValue.Base && isZeroSized(RHSValue)))
        return Error(E);
      return Success(CCR::Nonequal, E);
    }

    const CharUnits &LHSOffset = LHSValue.getLValueOffset();
    const CharUnits &RHSOffset = RHSValue.getLValueOffset();

    SubobjectDesignator &LHSDesignator = LHSValue.getLValueDesignator();
    SubobjectDesignator &RHSDesignator = RHSValue.getLValueDesignator();

    // C++11 [expr.rel]p3:
    //   Pointers to void (after pointer conversions) can be compared, with a
    //   result defined as follows: If both pointers represent the same
    //   address or are both the null pointer value, the result is true if the
    //   operator is <= or >= and false otherwise; otherwise the result is
    //   unspecified.
    // We interpret this as applying to pointers to *cv* void.
    if (LHSTy->isVoidPointerType() && LHSOffset != RHSOffset && IsRelational)
      Info.CCEDiag(E, diag::note_constexpr_void_comparison);

    // C++11 [expr.rel]p2:
    // - If two pointers point to non-static data members of the same object,
    //   or to subobjects or array elements fo such members, recursively, the
    //   pointer to the later declared member compares greater provided the
    //   two members have the same access control and provided their class is
    //   not a union.
    //   [...]
    // - Otherwise pointer comparisons are unspecified.
    if (!LHSDesignator.Invalid && !RHSDesignator.Invalid && IsRelational) {
      bool WasArrayIndex;
      unsigned Mismatch = FindDesignatorMismatch(
          getType(LHSValue.Base), LHSDesignator, RHSDesignator, WasArrayIndex);
      // At the point where the designators diverge, the comparison has a
      // specified value if:
      //  - we are comparing array indices
      //  - we are comparing fields of a union, or fields with the same access
      // Otherwise, the result is unspecified and thus the comparison is not a
      // constant expression.
      if (!WasArrayIndex && Mismatch < LHSDesignator.Entries.size() &&
          Mismatch < RHSDesignator.Entries.size()) {
        const FieldDecl *LF = getAsField(LHSDesignator.Entries[Mismatch]);
        const FieldDecl *RF = getAsField(RHSDesignator.Entries[Mismatch]);
        if (!LF && !RF)
          Info.CCEDiag(E, diag::note_constexpr_pointer_comparison_base_classes);
        else if (!LF)
          Info.CCEDiag(E, diag::note_constexpr_pointer_comparison_base_field)
              << getAsBaseClass(LHSDesignator.Entries[Mismatch])
              << RF->getParent() << RF;
        else if (!RF)
          Info.CCEDiag(E, diag::note_constexpr_pointer_comparison_base_field)
              << getAsBaseClass(RHSDesignator.Entries[Mismatch])
              << LF->getParent() << LF;
        else if (!LF->getParent()->isUnion() &&
                 LF->getAccess() != RF->getAccess())
          Info.CCEDiag(E,
                       diag::note_constexpr_pointer_comparison_differing_access)
              << LF << LF->getAccess() << RF << RF->getAccess()
              << LF->getParent();
      }
    }

    // The comparison here must be unsigned, and performed with the same
    // width as the pointer.
    unsigned PtrSize = Info.Ctx.getTypeSize(LHSTy);
    uint64_t CompareLHS = LHSOffset.getQuantity();
    uint64_t CompareRHS = RHSOffset.getQuantity();
    assert(PtrSize <= 64 && "Unexpected pointer width");
    uint64_t Mask = ~0ULL >> (64 - PtrSize);
    CompareLHS &= Mask;
    CompareRHS &= Mask;

    // If there is a base and this is a relational operator, we can only
    // compare pointers within the object in question; otherwise, the result
    // depends on where the object is located in memory.
    if (!LHSValue.Base.isNull() && IsRelational) {
      QualType BaseTy = getType(LHSValue.Base);
      if (BaseTy->isIncompleteType())
        return Error(E);
      CharUnits Size = Info.Ctx.getTypeSizeInChars(BaseTy);
      uint64_t OffsetLimit = Size.getQuantity();
      if (CompareLHS > OffsetLimit || CompareRHS > OffsetLimit)
        return Error(E);
    }

    if (CompareLHS < CompareRHS)
      return Success(CCR::Less, E);
    if (CompareLHS > CompareRHS)
      return Success(CCR::Greater, E);
    return Success(CCR::Equal, E);
  }

  if (LHSTy->isMemberPointerType()) {
    assert(IsEquality && "unexpected member pointer operation");
    assert(RHSTy->isMemberPointerType() && "invalid comparison");

    MemberPtr LHSValue, RHSValue;

    bool LHSOK = EvaluateMemberPointer(E->getLHS(), LHSValue, Info);
    if (!LHSOK && !Info.noteFailure())
      return false;

    if (!EvaluateMemberPointer(E->getRHS(), RHSValue, Info) || !LHSOK)
      return false;

    // C++11 [expr.eq]p2:
    //   If both operands are null, they compare equal. Otherwise if only one is
    //   null, they compare unequal.
    if (!LHSValue.getDecl() || !RHSValue.getDecl()) {
      bool Equal = !LHSValue.getDecl() && !RHSValue.getDecl();
      return Success(Equal ? CCR::Equal : CCR::Nonequal, E);
    }

    //   Otherwise if either is a pointer to a virtual member function, the
    //   result is unspecified.
    if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(LHSValue.getDecl()))
      if (MD->isVirtual())
        Info.CCEDiag(E, diag::note_constexpr_compare_virtual_mem_ptr) << MD;
    if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(RHSValue.getDecl()))
      if (MD->isVirtual())
        Info.CCEDiag(E, diag::note_constexpr_compare_virtual_mem_ptr) << MD;

    //   Otherwise they compare equal if and only if they would refer to the
    //   same member of the same most derived object or the same subobject if
    //   they were dereferenced with a hypothetical object of the associated
    //   class type.
    bool Equal = LHSValue == RHSValue;
    return Success(Equal ? CCR::Equal : CCR::Nonequal, E);
  }

  if (LHSTy->isNullPtrType()) {
    assert(E->isComparisonOp() && "unexpected nullptr operation");
    assert(RHSTy->isNullPtrType() && "missing pointer conversion");
    // C++11 [expr.rel]p4, [expr.eq]p3: If two operands of type std::nullptr_t
    // are compared, the result is true of the operator is <=, >= or ==, and
    // false otherwise.
    return Success(CCR::Equal, E);
  }

  return DoAfter();
}

bool RecordExprEvaluator::VisitBinCmp(const BinaryOperator *E) {
  if (!CheckLiteralType(Info, E))
    return false;

  auto OnSuccess = [&](ComparisonCategoryResult ResKind,
                       const BinaryOperator *E) {
    // Evaluation succeeded. Lookup the information for the comparison category
    // type and fetch the VarDecl for the result.
    const ComparisonCategoryInfo &CmpInfo =
        Info.Ctx.CompCategories.getInfoForType(E->getType());
    const VarDecl *VD =
        CmpInfo.getValueInfo(CmpInfo.makeWeakResult(ResKind))->VD;
    // Check and evaluate the result as a constant expression.
    LValue LV;
    LV.set(VD);
    if (!handleLValueToRValueConversion(Info, E, E->getType(), LV, Result))
      return false;
    return CheckConstantExpression(Info, E->getExprLoc(), E->getType(), Result);
  };
  return EvaluateComparisonBinaryOperator(Info, E, OnSuccess, [&]() {
    return ExprEvaluatorBaseTy::VisitBinCmp(E);
  });
}

bool IntExprEvaluator::VisitBinaryOperator(const BinaryOperator *E) {
  // We don't call noteFailure immediately because the assignment happens after
  // we evaluate LHS and RHS.
  if (!Info.keepEvaluatingAfterFailure() && E->isAssignmentOp())
    return Error(E);

  DelayedNoteFailureRAII MaybeNoteFailureLater(Info, E->isAssignmentOp());
  if (DataRecursiveIntBinOpEvaluator::shouldEnqueue(E))
    return DataRecursiveIntBinOpEvaluator(*this, Result).Traverse(E);

  assert((!E->getLHS()->getType()->isIntegralOrEnumerationType() ||
          !E->getRHS()->getType()->isIntegralOrEnumerationType()) &&
         "DataRecursiveIntBinOpEvaluator should have handled integral types");

  if (E->isComparisonOp()) {
    // Evaluate builtin binary comparisons by evaluating them as C++2a three-way
    // comparisons and then translating the result.
    auto OnSuccess = [&](ComparisonCategoryResult ResKind,
                         const BinaryOperator *E) {
      using CCR = ComparisonCategoryResult;
      bool IsEqual   = ResKind == CCR::Equal,
           IsLess    = ResKind == CCR::Less,
           IsGreater = ResKind == CCR::Greater;
      auto Op = E->getOpcode();
      switch (Op) {
      default:
        llvm_unreachable("unsupported binary operator");
      case BO_EQ:
      case BO_NE:
        return Success(IsEqual == (Op == BO_EQ), E);
      case BO_LT: return Success(IsLess, E);
      case BO_GT: return Success(IsGreater, E);
      case BO_LE: return Success(IsEqual || IsLess, E);
      case BO_GE: return Success(IsEqual || IsGreater, E);
      }
    };
    return EvaluateComparisonBinaryOperator(Info, E, OnSuccess, [&]() {
      return ExprEvaluatorBaseTy::VisitBinaryOperator(E);
    });
  }

  QualType LHSTy = E->getLHS()->getType();
  QualType RHSTy = E->getRHS()->getType();

  if (LHSTy->isPointerType() && RHSTy->isPointerType() &&
      E->getOpcode() == BO_Sub) {
    LValue LHSValue, RHSValue;

    bool LHSOK = EvaluatePointer(E->getLHS(), LHSValue, Info);
    if (!LHSOK && !Info.noteFailure())
      return false;

    if (!EvaluatePointer(E->getRHS(), RHSValue, Info) || !LHSOK)
      return false;

    // Reject differing bases from the normal codepath; we special-case
    // comparisons to null.
    if (!HasSameBase(LHSValue, RHSValue)) {
      // Handle &&A - &&B.
      if (!LHSValue.Offset.isZero() || !RHSValue.Offset.isZero())
        return Error(E);
      const Expr *LHSExpr = LHSValue.Base.dyn_cast<const Expr *>();
      const Expr *RHSExpr = RHSValue.Base.dyn_cast<const Expr *>();
      if (!LHSExpr || !RHSExpr)
        return Error(E);
      const AddrLabelExpr *LHSAddrExpr = dyn_cast<AddrLabelExpr>(LHSExpr);
      const AddrLabelExpr *RHSAddrExpr = dyn_cast<AddrLabelExpr>(RHSExpr);
      if (!LHSAddrExpr || !RHSAddrExpr)
        return Error(E);
      // Make sure both labels come from the same function.
      if (LHSAddrExpr->getLabel()->getDeclContext() !=
          RHSAddrExpr->getLabel()->getDeclContext())
        return Error(E);
      return Success(APValue(LHSAddrExpr, RHSAddrExpr), E);
    }
    const CharUnits &LHSOffset = LHSValue.getLValueOffset();
    const CharUnits &RHSOffset = RHSValue.getLValueOffset();

    SubobjectDesignator &LHSDesignator = LHSValue.getLValueDesignator();
    SubobjectDesignator &RHSDesignator = RHSValue.getLValueDesignator();

    // C++11 [expr.add]p6:
    //   Unless both pointers point to elements of the same array object, or
    //   one past the last element of the array object, the behavior is
    //   undefined.
    if (!LHSDesignator.Invalid && !RHSDesignator.Invalid &&
        !AreElementsOfSameArray(getType(LHSValue.Base), LHSDesignator,
                                RHSDesignator))
      Info.CCEDiag(E, diag::note_constexpr_pointer_subtraction_not_same_array);

    QualType Type = E->getLHS()->getType();
    QualType ElementType = Type->getAs<PointerType>()->getPointeeType();

    CharUnits ElementSize;
    if (!HandleSizeof(Info, E->getExprLoc(), ElementType, ElementSize))
      return false;

    // As an extension, a type may have zero size (empty struct or union in
    // C, array of zero length). Pointer subtraction in such cases has
    // undefined behavior, so is not constant.
    if (ElementSize.isZero()) {
      Info.FFDiag(E, diag::note_constexpr_pointer_subtraction_zero_size)
          << ElementType;
      return false;
    }

    // FIXME: LLVM and GCC both compute LHSOffset - RHSOffset at runtime,
    // and produce incorrect results when it overflows. Such behavior
    // appears to be non-conforming, but is common, so perhaps we should
    // assume the standard intended for such cases to be undefined behavior
    // and check for them.

    // Compute (LHSOffset - RHSOffset) / Size carefully, checking for
    // overflow in the final conversion to ptrdiff_t.
    APSInt LHS(llvm::APInt(65, (int64_t)LHSOffset.getQuantity(), true), false);
    APSInt RHS(llvm::APInt(65, (int64_t)RHSOffset.getQuantity(), true), false);
    APSInt ElemSize(llvm::APInt(65, (int64_t)ElementSize.getQuantity(), true),
                    false);
    APSInt TrueResult = (LHS - RHS) / ElemSize;
    APSInt Result = TrueResult.trunc(Info.Ctx.getIntWidth(E->getType()));

    if (Result.extend(65) != TrueResult &&
        !HandleOverflow(Info, E, TrueResult, E->getType()))
      return false;
    return Success(Result, E);
  }

  return ExprEvaluatorBaseTy::VisitBinaryOperator(E);
}

/// VisitUnaryExprOrTypeTraitExpr - Evaluate a sizeof, alignof or vec_step with
/// a result as the expression's type.
bool IntExprEvaluator::VisitUnaryExprOrTypeTraitExpr(
                                    const UnaryExprOrTypeTraitExpr *E) {
  switch(E->getKind()) {
  case UETT_PreferredAlignOf:
  case UETT_AlignOf: {
    if (E->isArgumentType())
      return Success(GetAlignOfType(Info, E->getArgumentType(), E->getKind()),
                     E);
    else
      return Success(GetAlignOfExpr(Info, E->getArgumentExpr(), E->getKind()),
                     E);
  }

  case UETT_VecStep: {
    QualType Ty = E->getTypeOfArgument();

    if (Ty->isVectorType()) {
      unsigned n = Ty->castAs<VectorType>()->getNumElements();

      // The vec_step built-in functions that take a 3-component
      // vector return 4. (OpenCL 1.1 spec 6.11.12)
      if (n == 3)
        n = 4;

      return Success(n, E);
    } else
      return Success(1, E);
  }

  case UETT_SizeOf: {
    QualType SrcTy = E->getTypeOfArgument();
    // C++ [expr.sizeof]p2: "When applied to a reference or a reference type,
    //   the result is the size of the referenced type."
    if (const ReferenceType *Ref = SrcTy->getAs<ReferenceType>())
      SrcTy = Ref->getPointeeType();

    CharUnits Sizeof;
    if (!HandleSizeof(Info, E->getExprLoc(), SrcTy, Sizeof))
      return false;
    return Success(Sizeof, E);
  }
  case UETT_OpenMPRequiredSimdAlign:
    assert(E->isArgumentType());
    return Success(
        Info.Ctx.toCharUnitsFromBits(
                    Info.Ctx.getOpenMPDefaultSimdAlign(E->getArgumentType()))
            .getQuantity(),
        E);
  }

  llvm_unreachable("unknown expr/type trait");
}

bool IntExprEvaluator::VisitOffsetOfExpr(const OffsetOfExpr *OOE) {
  CharUnits Result;
  unsigned n = OOE->getNumComponents();
  if (n == 0)
    return Error(OOE);
  QualType CurrentType = OOE->getTypeSourceInfo()->getType();
  for (unsigned i = 0; i != n; ++i) {
    OffsetOfNode ON = OOE->getComponent(i);
    switch (ON.getKind()) {
    case OffsetOfNode::Array: {
      const Expr *Idx = OOE->getIndexExpr(ON.getArrayExprIndex());
      APSInt IdxResult;
      if (!EvaluateInteger(Idx, IdxResult, Info))
        return false;
      const ArrayType *AT = Info.Ctx.getAsArrayType(CurrentType);
      if (!AT)
        return Error(OOE);
      CurrentType = AT->getElementType();
      CharUnits ElementSize = Info.Ctx.getTypeSizeInChars(CurrentType);
      Result += IdxResult.getSExtValue() * ElementSize;
      break;
    }

    case OffsetOfNode::Field: {
      FieldDecl *MemberDecl = ON.getField();
      const RecordType *RT = CurrentType->getAs<RecordType>();
      if (!RT)
        return Error(OOE);
      RecordDecl *RD = RT->getDecl();
      if (RD->isInvalidDecl()) return false;
      const ASTRecordLayout &RL = Info.Ctx.getASTRecordLayout(RD);
      unsigned i = MemberDecl->getFieldIndex();
      assert(i < RL.getFieldCount() && "offsetof field in wrong type");
      Result += Info.Ctx.toCharUnitsFromBits(RL.getFieldOffset(i));
      CurrentType = MemberDecl->getType().getNonReferenceType();
      break;
    }

    case OffsetOfNode::Identifier:
      llvm_unreachable("dependent __builtin_offsetof");

    case OffsetOfNode::Base: {
      CXXBaseSpecifier *BaseSpec = ON.getBase();
      if (BaseSpec->isVirtual())
        return Error(OOE);

      // Find the layout of the class whose base we are looking into.
      const RecordType *RT = CurrentType->getAs<RecordType>();
      if (!RT)
        return Error(OOE);
      RecordDecl *RD = RT->getDecl();
      if (RD->isInvalidDecl()) return false;
      const ASTRecordLayout &RL = Info.Ctx.getASTRecordLayout(RD);

      // Find the base class itself.
      CurrentType = BaseSpec->getType();
      const RecordType *BaseRT = CurrentType->getAs<RecordType>();
      if (!BaseRT)
        return Error(OOE);

      // Add the offset to the base.
      Result += RL.getBaseClassOffset(cast<CXXRecordDecl>(BaseRT->getDecl()));
      break;
    }
    }
  }
  return Success(Result, OOE);
}

bool IntExprEvaluator::VisitUnaryOperator(const UnaryOperator *E) {
  switch (E->getOpcode()) {
  default:
    // Address, indirect, pre/post inc/dec, etc are not valid constant exprs.
    // See C99 6.6p3.
    return Error(E);
  case UO_Extension:
    // FIXME: Should extension allow i-c-e extension expressions in its scope?
    // If so, we could clear the diagnostic ID.
    return Visit(E->getSubExpr());
  case UO_Plus:
    // The result is just the value.
    return Visit(E->getSubExpr());
  case UO_Minus: {
    if (!Visit(E->getSubExpr()))
      return false;
    if (!Result.isInt()) return Error(E);
    const APSInt &Value = Result.getInt();
    if (Value.isSigned() && Value.isMinSignedValue() && E->canOverflow() &&
        !HandleOverflow(Info, E, -Value.extend(Value.getBitWidth() + 1),
                        E->getType()))
      return false;
    return Success(-Value, E);
  }
  case UO_Not: {
    if (!Visit(E->getSubExpr()))
      return false;
    if (!Result.isInt()) return Error(E);
    return Success(~Result.getInt(), E);
  }
  case UO_LNot: {
    bool bres;
    if (!EvaluateAsBooleanCondition(E->getSubExpr(), bres, Info))
      return false;
    return Success(!bres, E);
  }
  }
}

/// HandleCast - This is used to evaluate implicit or explicit casts where the
/// result type is integer.
bool IntExprEvaluator::VisitCastExpr(const CastExpr *E) {
  const Expr *SubExpr = E->getSubExpr();
  QualType DestType = E->getType();
  QualType SrcType = SubExpr->getType();

  switch (E->getCastKind()) {
  case CK_BaseToDerived:
  case CK_DerivedToBase:
  case CK_UncheckedDerivedToBase:
  case CK_Dynamic:
  case CK_ToUnion:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_NullToPointer:
  case CK_NullToMemberPointer:
  case CK_BaseToDerivedMemberPointer:
  case CK_DerivedToBaseMemberPointer:
  case CK_ReinterpretMemberPointer:
  case CK_ConstructorConversion:
  case CK_IntegralToPointer:
  case CK_ToVoid:
  case CK_VectorSplat:
  case CK_IntegralToFloating:
  case CK_FloatingCast:
  case CK_CPointerToObjCPointerCast:
  case CK_BlockPointerToObjCPointerCast:
  case CK_AnyPointerToBlockPointerCast:
  case CK_ObjCObjectLValueCast:
  case CK_FloatingRealToComplex:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_BuiltinFnToFnPtr:
  case CK_ZeroToOCLOpaqueType:
  case CK_NonAtomicToAtomic:
  case CK_AddressSpaceConversion:
  case CK_IntToOCLSampler:
  case CK_FixedPointCast:
    llvm_unreachable("invalid cast kind for integral value");

  case CK_BitCast:
  case CK_Dependent:
  case CK_LValueBitCast:
  case CK_ARCProduceObject:
  case CK_ARCConsumeObject:
  case CK_ARCReclaimReturnedObject:
  case CK_ARCExtendBlockObject:
  case CK_CopyAndAutoreleaseBlockObject:
    return Error(E);

  case CK_UserDefinedConversion:
  case CK_LValueToRValue:
  case CK_AtomicToNonAtomic:
  case CK_NoOp:
    return ExprEvaluatorBaseTy::VisitCastExpr(E);

  case CK_MemberPointerToBoolean:
  case CK_PointerToBoolean:
  case CK_IntegralToBoolean:
  case CK_FloatingToBoolean:
  case CK_BooleanToSignedIntegral:
  case CK_FloatingComplexToBoolean:
  case CK_IntegralComplexToBoolean: {
    bool BoolResult;
    if (!EvaluateAsBooleanCondition(SubExpr, BoolResult, Info))
      return false;
    uint64_t IntResult = BoolResult;
    if (BoolResult && E->getCastKind() == CK_BooleanToSignedIntegral)
      IntResult = (uint64_t)-1;
    return Success(IntResult, E);
  }

  case CK_FixedPointToBoolean: {
    // Unsigned padding does not affect this.
    APValue Val;
    if (!Evaluate(Val, Info, SubExpr))
      return false;
    return Success(Val.getInt().getBoolValue(), E);
  }

  case CK_IntegralCast: {
    if (!Visit(SubExpr))
      return false;

    if (!Result.isInt()) {
      // Allow casts of address-of-label differences if they are no-ops
      // or narrowing.  (The narrowing case isn't actually guaranteed to
      // be constant-evaluatable except in some narrow cases which are hard
      // to detect here.  We let it through on the assumption the user knows
      // what they are doing.)
      if (Result.isAddrLabelDiff())
        return Info.Ctx.getTypeSize(DestType) <= Info.Ctx.getTypeSize(SrcType);
      // Only allow casts of lvalues if they are lossless.
      return Info.Ctx.getTypeSize(DestType) == Info.Ctx.getTypeSize(SrcType);
    }

    return Success(HandleIntToIntCast(Info, E, DestType, SrcType,
                                      Result.getInt()), E);
  }

  case CK_PointerToIntegral: {
    CCEDiag(E, diag::note_constexpr_invalid_cast) << 2;

    LValue LV;
    if (!EvaluatePointer(SubExpr, LV, Info))
      return false;

    if (LV.getLValueBase()) {
      // Only allow based lvalue casts if they are lossless.
      // FIXME: Allow a larger integer size than the pointer size, and allow
      // narrowing back down to pointer width in subsequent integral casts.
      // FIXME: Check integer type's active bits, not its type size.
      if (Info.Ctx.getTypeSize(DestType) != Info.Ctx.getTypeSize(SrcType))
        return Error(E);

      LV.Designator.setInvalid();
      LV.moveInto(Result);
      return true;
    }

    APSInt AsInt;
    APValue V;
    LV.moveInto(V);
    if (!V.toIntegralConstant(AsInt, SrcType, Info.Ctx))
      llvm_unreachable("Can't cast this!");

    return Success(HandleIntToIntCast(Info, E, DestType, SrcType, AsInt), E);
  }

  case CK_IntegralComplexToReal: {
    ComplexValue C;
    if (!EvaluateComplex(SubExpr, C, Info))
      return false;
    return Success(C.getComplexIntReal(), E);
  }

  case CK_FloatingToIntegral: {
    APFloat F(0.0);
    if (!EvaluateFloat(SubExpr, F, Info))
      return false;

    APSInt Value;
    if (!HandleFloatToIntCast(Info, E, SrcType, F, DestType, Value))
      return false;
    return Success(Value, E);
  }
  }

  llvm_unreachable("unknown cast resulting in integral value");
}

bool IntExprEvaluator::VisitUnaryReal(const UnaryOperator *E) {
  if (E->getSubExpr()->getType()->isAnyComplexType()) {
    ComplexValue LV;
    if (!EvaluateComplex(E->getSubExpr(), LV, Info))
      return false;
    if (!LV.isComplexInt())
      return Error(E);
    return Success(LV.getComplexIntReal(), E);
  }

  return Visit(E->getSubExpr());
}

bool IntExprEvaluator::VisitUnaryImag(const UnaryOperator *E) {
  if (E->getSubExpr()->getType()->isComplexIntegerType()) {
    ComplexValue LV;
    if (!EvaluateComplex(E->getSubExpr(), LV, Info))
      return false;
    if (!LV.isComplexInt())
      return Error(E);
    return Success(LV.getComplexIntImag(), E);
  }

  VisitIgnoredValue(E->getSubExpr());
  return Success(0, E);
}

bool IntExprEvaluator::VisitSizeOfPackExpr(const SizeOfPackExpr *E) {
  return Success(E->getPackLength(), E);
}

bool IntExprEvaluator::VisitCXXNoexceptExpr(const CXXNoexceptExpr *E) {
  return Success(E->getValue(), E);
}

bool FixedPointExprEvaluator::VisitUnaryOperator(const UnaryOperator *E) {
  switch (E->getOpcode()) {
    default:
      // Invalid unary operators
      return Error(E);
    case UO_Plus:
      // The result is just the value.
      return Visit(E->getSubExpr());
    case UO_Minus: {
      if (!Visit(E->getSubExpr())) return false;
      if (!Result.isInt()) return Error(E);
      const APSInt &Value = Result.getInt();
      if (Value.isSigned() && Value.isMinSignedValue() && E->canOverflow()) {
        SmallString<64> S;
        FixedPointValueToString(S, Value,
                                Info.Ctx.getTypeInfo(E->getType()).Width);
        Info.CCEDiag(E, diag::note_constexpr_overflow) << S << E->getType();
        if (Info.noteUndefinedBehavior()) return false;
      }
      return Success(-Value, E);
    }
    case UO_LNot: {
      bool bres;
      if (!EvaluateAsBooleanCondition(E->getSubExpr(), bres, Info))
        return false;
      return Success(!bres, E);
    }
  }
}

//===----------------------------------------------------------------------===//
// Float Evaluation
//===----------------------------------------------------------------------===//

namespace {
class FloatExprEvaluator
  : public ExprEvaluatorBase<FloatExprEvaluator> {
  APFloat &Result;
public:
  FloatExprEvaluator(EvalInfo &info, APFloat &result)
    : ExprEvaluatorBaseTy(info), Result(result) {}

  bool Success(const APValue &V, const Expr *e) {
    Result = V.getFloat();
    return true;
  }

  bool ZeroInitialization(const Expr *E) {
    Result = APFloat::getZero(Info.Ctx.getFloatTypeSemantics(E->getType()));
    return true;
  }

  bool VisitCallExpr(const CallExpr *E);

  bool VisitUnaryOperator(const UnaryOperator *E);
  bool VisitBinaryOperator(const BinaryOperator *E);
  bool VisitFloatingLiteral(const FloatingLiteral *E);
  bool VisitCastExpr(const CastExpr *E);

  bool VisitUnaryReal(const UnaryOperator *E);
  bool VisitUnaryImag(const UnaryOperator *E);

  // FIXME: Missing: array subscript of vector, member of vector
};
} // end anonymous namespace

static bool EvaluateFloat(const Expr* E, APFloat& Result, EvalInfo &Info) {
  assert(E->isRValue() && E->getType()->isRealFloatingType());
  return FloatExprEvaluator(Info, Result).Visit(E);
}

static bool TryEvaluateBuiltinNaN(const ASTContext &Context,
                                  QualType ResultTy,
                                  const Expr *Arg,
                                  bool SNaN,
                                  llvm::APFloat &Result) {
  const StringLiteral *S = dyn_cast<StringLiteral>(Arg->IgnoreParenCasts());
  if (!S) return false;

  const llvm::fltSemantics &Sem = Context.getFloatTypeSemantics(ResultTy);

  llvm::APInt fill;

  // Treat empty strings as if they were zero.
  if (S->getString().empty())
    fill = llvm::APInt(32, 0);
  else if (S->getString().getAsInteger(0, fill))
    return false;

  if (Context.getTargetInfo().isNan2008()) {
    if (SNaN)
      Result = llvm::APFloat::getSNaN(Sem, false, &fill);
    else
      Result = llvm::APFloat::getQNaN(Sem, false, &fill);
  } else {
    // Prior to IEEE 754-2008, architectures were allowed to choose whether
    // the first bit of their significand was set for qNaN or sNaN. MIPS chose
    // a different encoding to what became a standard in 2008, and for pre-
    // 2008 revisions, MIPS interpreted sNaN-2008 as qNan and qNaN-2008 as
    // sNaN. This is now known as "legacy NaN" encoding.
    if (SNaN)
      Result = llvm::APFloat::getQNaN(Sem, false, &fill);
    else
      Result = llvm::APFloat::getSNaN(Sem, false, &fill);
  }

  return true;
}

bool FloatExprEvaluator::VisitCallExpr(const CallExpr *E) {
  switch (E->getBuiltinCallee()) {
  default:
    return ExprEvaluatorBaseTy::VisitCallExpr(E);

  case Builtin::BI__builtin_huge_val:
  case Builtin::BI__builtin_huge_valf:
  case Builtin::BI__builtin_huge_vall:
  case Builtin::BI__builtin_huge_valf128:
  case Builtin::BI__builtin_inf:
  case Builtin::BI__builtin_inff:
  case Builtin::BI__builtin_infl:
  case Builtin::BI__builtin_inff128: {
    const llvm::fltSemantics &Sem =
      Info.Ctx.getFloatTypeSemantics(E->getType());
    Result = llvm::APFloat::getInf(Sem);
    return true;
  }

  case Builtin::BI__builtin_nans:
  case Builtin::BI__builtin_nansf:
  case Builtin::BI__builtin_nansl:
  case Builtin::BI__builtin_nansf128:
    if (!TryEvaluateBuiltinNaN(Info.Ctx, E->getType(), E->getArg(0),
                               true, Result))
      return Error(E);
    return true;

  case Builtin::BI__builtin_nan:
  case Builtin::BI__builtin_nanf:
  case Builtin::BI__builtin_nanl:
  case Builtin::BI__builtin_nanf128:
    // If this is __builtin_nan() turn this into a nan, otherwise we
    // can't constant fold it.
    if (!TryEvaluateBuiltinNaN(Info.Ctx, E->getType(), E->getArg(0),
                               false, Result))
      return Error(E);
    return true;

  case Builtin::BI__builtin_fabs:
  case Builtin::BI__builtin_fabsf:
  case Builtin::BI__builtin_fabsl:
  case Builtin::BI__builtin_fabsf128:
    if (!EvaluateFloat(E->getArg(0), Result, Info))
      return false;

    if (Result.isNegative())
      Result.changeSign();
    return true;

  // FIXME: Builtin::BI__builtin_powi
  // FIXME: Builtin::BI__builtin_powif
  // FIXME: Builtin::BI__builtin_powil

  case Builtin::BI__builtin_copysign:
  case Builtin::BI__builtin_copysignf:
  case Builtin::BI__builtin_copysignl:
  case Builtin::BI__builtin_copysignf128: {
    APFloat RHS(0.);
    if (!EvaluateFloat(E->getArg(0), Result, Info) ||
        !EvaluateFloat(E->getArg(1), RHS, Info))
      return false;
    Result.copySign(RHS);
    return true;
  }
  }
}

bool FloatExprEvaluator::VisitUnaryReal(const UnaryOperator *E) {
  if (E->getSubExpr()->getType()->isAnyComplexType()) {
    ComplexValue CV;
    if (!EvaluateComplex(E->getSubExpr(), CV, Info))
      return false;
    Result = CV.FloatReal;
    return true;
  }

  return Visit(E->getSubExpr());
}

bool FloatExprEvaluator::VisitUnaryImag(const UnaryOperator *E) {
  if (E->getSubExpr()->getType()->isAnyComplexType()) {
    ComplexValue CV;
    if (!EvaluateComplex(E->getSubExpr(), CV, Info))
      return false;
    Result = CV.FloatImag;
    return true;
  }

  VisitIgnoredValue(E->getSubExpr());
  const llvm::fltSemantics &Sem = Info.Ctx.getFloatTypeSemantics(E->getType());
  Result = llvm::APFloat::getZero(Sem);
  return true;
}

bool FloatExprEvaluator::VisitUnaryOperator(const UnaryOperator *E) {
  switch (E->getOpcode()) {
  default: return Error(E);
  case UO_Plus:
    return EvaluateFloat(E->getSubExpr(), Result, Info);
  case UO_Minus:
    if (!EvaluateFloat(E->getSubExpr(), Result, Info))
      return false;
    Result.changeSign();
    return true;
  }
}

bool FloatExprEvaluator::VisitBinaryOperator(const BinaryOperator *E) {
  if (E->isPtrMemOp() || E->isAssignmentOp() || E->getOpcode() == BO_Comma)
    return ExprEvaluatorBaseTy::VisitBinaryOperator(E);

  APFloat RHS(0.0);
  bool LHSOK = EvaluateFloat(E->getLHS(), Result, Info);
  if (!LHSOK && !Info.noteFailure())
    return false;
  return EvaluateFloat(E->getRHS(), RHS, Info) && LHSOK &&
         handleFloatFloatBinOp(Info, E, Result, E->getOpcode(), RHS);
}

bool FloatExprEvaluator::VisitFloatingLiteral(const FloatingLiteral *E) {
  Result = E->getValue();
  return true;
}

bool FloatExprEvaluator::VisitCastExpr(const CastExpr *E) {
  const Expr* SubExpr = E->getSubExpr();

  switch (E->getCastKind()) {
  default:
    return ExprEvaluatorBaseTy::VisitCastExpr(E);

  case CK_IntegralToFloating: {
    APSInt IntResult;
    return EvaluateInteger(SubExpr, IntResult, Info) &&
           HandleIntToFloatCast(Info, E, SubExpr->getType(), IntResult,
                                E->getType(), Result);
  }

  case CK_FloatingCast: {
    if (!Visit(SubExpr))
      return false;
    return HandleFloatToFloatCast(Info, E, SubExpr->getType(), E->getType(),
                                  Result);
  }

  case CK_FloatingComplexToReal: {
    ComplexValue V;
    if (!EvaluateComplex(SubExpr, V, Info))
      return false;
    Result = V.getComplexFloatReal();
    return true;
  }
  }
}

//===----------------------------------------------------------------------===//
// Complex Evaluation (for float and integer)
//===----------------------------------------------------------------------===//

namespace {
class ComplexExprEvaluator
  : public ExprEvaluatorBase<ComplexExprEvaluator> {
  ComplexValue &Result;

public:
  ComplexExprEvaluator(EvalInfo &info, ComplexValue &Result)
    : ExprEvaluatorBaseTy(info), Result(Result) {}

  bool Success(const APValue &V, const Expr *e) {
    Result.setFrom(V);
    return true;
  }

  bool ZeroInitialization(const Expr *E);

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  bool VisitImaginaryLiteral(const ImaginaryLiteral *E);
  bool VisitCastExpr(const CastExpr *E);
  bool VisitBinaryOperator(const BinaryOperator *E);
  bool VisitUnaryOperator(const UnaryOperator *E);
  bool VisitInitListExpr(const InitListExpr *E);
};
} // end anonymous namespace

static bool EvaluateComplex(const Expr *E, ComplexValue &Result,
                            EvalInfo &Info) {
  assert(E->isRValue() && E->getType()->isAnyComplexType());
  return ComplexExprEvaluator(Info, Result).Visit(E);
}

bool ComplexExprEvaluator::ZeroInitialization(const Expr *E) {
  QualType ElemTy = E->getType()->castAs<ComplexType>()->getElementType();
  if (ElemTy->isRealFloatingType()) {
    Result.makeComplexFloat();
    APFloat Zero = APFloat::getZero(Info.Ctx.getFloatTypeSemantics(ElemTy));
    Result.FloatReal = Zero;
    Result.FloatImag = Zero;
  } else {
    Result.makeComplexInt();
    APSInt Zero = Info.Ctx.MakeIntValue(0, ElemTy);
    Result.IntReal = Zero;
    Result.IntImag = Zero;
  }
  return true;
}

bool ComplexExprEvaluator::VisitImaginaryLiteral(const ImaginaryLiteral *E) {
  const Expr* SubExpr = E->getSubExpr();

  if (SubExpr->getType()->isRealFloatingType()) {
    Result.makeComplexFloat();
    APFloat &Imag = Result.FloatImag;
    if (!EvaluateFloat(SubExpr, Imag, Info))
      return false;

    Result.FloatReal = APFloat(Imag.getSemantics());
    return true;
  } else {
    assert(SubExpr->getType()->isIntegerType() &&
           "Unexpected imaginary literal.");

    Result.makeComplexInt();
    APSInt &Imag = Result.IntImag;
    if (!EvaluateInteger(SubExpr, Imag, Info))
      return false;

    Result.IntReal = APSInt(Imag.getBitWidth(), !Imag.isSigned());
    return true;
  }
}

bool ComplexExprEvaluator::VisitCastExpr(const CastExpr *E) {

  switch (E->getCastKind()) {
  case CK_BitCast:
  case CK_BaseToDerived:
  case CK_DerivedToBase:
  case CK_UncheckedDerivedToBase:
  case CK_Dynamic:
  case CK_ToUnion:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_NullToPointer:
  case CK_NullToMemberPointer:
  case CK_BaseToDerivedMemberPointer:
  case CK_DerivedToBaseMemberPointer:
  case CK_MemberPointerToBoolean:
  case CK_ReinterpretMemberPointer:
  case CK_ConstructorConversion:
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
  case CK_PointerToBoolean:
  case CK_ToVoid:
  case CK_VectorSplat:
  case CK_IntegralCast:
  case CK_BooleanToSignedIntegral:
  case CK_IntegralToBoolean:
  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
  case CK_FloatingToBoolean:
  case CK_FloatingCast:
  case CK_CPointerToObjCPointerCast:
  case CK_BlockPointerToObjCPointerCast:
  case CK_AnyPointerToBlockPointerCast:
  case CK_ObjCObjectLValueCast:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexToBoolean:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexToBoolean:
  case CK_ARCProduceObject:
  case CK_ARCConsumeObject:
  case CK_ARCReclaimReturnedObject:
  case CK_ARCExtendBlockObject:
  case CK_CopyAndAutoreleaseBlockObject:
  case CK_BuiltinFnToFnPtr:
  case CK_ZeroToOCLOpaqueType:
  case CK_NonAtomicToAtomic:
  case CK_AddressSpaceConversion:
  case CK_IntToOCLSampler:
  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
    llvm_unreachable("invalid cast kind for complex value");

  case CK_LValueToRValue:
  case CK_AtomicToNonAtomic:
  case CK_NoOp:
    return ExprEvaluatorBaseTy::VisitCastExpr(E);

  case CK_Dependent:
  case CK_LValueBitCast:
  case CK_UserDefinedConversion:
    return Error(E);

  case CK_FloatingRealToComplex: {
    APFloat &Real = Result.FloatReal;
    if (!EvaluateFloat(E->getSubExpr(), Real, Info))
      return false;

    Result.makeComplexFloat();
    Result.FloatImag = APFloat(Real.getSemantics());
    return true;
  }

  case CK_FloatingComplexCast: {
    if (!Visit(E->getSubExpr()))
      return false;

    QualType To = E->getType()->getAs<ComplexType>()->getElementType();
    QualType From
      = E->getSubExpr()->getType()->getAs<ComplexType>()->getElementType();

    return HandleFloatToFloatCast(Info, E, From, To, Result.FloatReal) &&
           HandleFloatToFloatCast(Info, E, From, To, Result.FloatImag);
  }

  case CK_FloatingComplexToIntegralComplex: {
    if (!Visit(E->getSubExpr()))
      return false;

    QualType To = E->getType()->getAs<ComplexType>()->getElementType();
    QualType From
      = E->getSubExpr()->getType()->getAs<ComplexType>()->getElementType();
    Result.makeComplexInt();
    return HandleFloatToIntCast(Info, E, From, Result.FloatReal,
                                To, Result.IntReal) &&
           HandleFloatToIntCast(Info, E, From, Result.FloatImag,
                                To, Result.IntImag);
  }

  case CK_IntegralRealToComplex: {
    APSInt &Real = Result.IntReal;
    if (!EvaluateInteger(E->getSubExpr(), Real, Info))
      return false;

    Result.makeComplexInt();
    Result.IntImag = APSInt(Real.getBitWidth(), !Real.isSigned());
    return true;
  }

  case CK_IntegralComplexCast: {
    if (!Visit(E->getSubExpr()))
      return false;

    QualType To = E->getType()->getAs<ComplexType>()->getElementType();
    QualType From
      = E->getSubExpr()->getType()->getAs<ComplexType>()->getElementType();

    Result.IntReal = HandleIntToIntCast(Info, E, To, From, Result.IntReal);
    Result.IntImag = HandleIntToIntCast(Info, E, To, From, Result.IntImag);
    return true;
  }

  case CK_IntegralComplexToFloatingComplex: {
    if (!Visit(E->getSubExpr()))
      return false;

    QualType To = E->getType()->castAs<ComplexType>()->getElementType();
    QualType From
      = E->getSubExpr()->getType()->castAs<ComplexType>()->getElementType();
    Result.makeComplexFloat();
    return HandleIntToFloatCast(Info, E, From, Result.IntReal,
                                To, Result.FloatReal) &&
           HandleIntToFloatCast(Info, E, From, Result.IntImag,
                                To, Result.FloatImag);
  }
  }

  llvm_unreachable("unknown cast resulting in complex value");
}

bool ComplexExprEvaluator::VisitBinaryOperator(const BinaryOperator *E) {
  if (E->isPtrMemOp() || E->isAssignmentOp() || E->getOpcode() == BO_Comma)
    return ExprEvaluatorBaseTy::VisitBinaryOperator(E);

  // Track whether the LHS or RHS is real at the type system level. When this is
  // the case we can simplify our evaluation strategy.
  bool LHSReal = false, RHSReal = false;

  bool LHSOK;
  if (E->getLHS()->getType()->isRealFloatingType()) {
    LHSReal = true;
    APFloat &Real = Result.FloatReal;
    LHSOK = EvaluateFloat(E->getLHS(), Real, Info);
    if (LHSOK) {
      Result.makeComplexFloat();
      Result.FloatImag = APFloat(Real.getSemantics());
    }
  } else {
    LHSOK = Visit(E->getLHS());
  }
  if (!LHSOK && !Info.noteFailure())
    return false;

  ComplexValue RHS;
  if (E->getRHS()->getType()->isRealFloatingType()) {
    RHSReal = true;
    APFloat &Real = RHS.FloatReal;
    if (!EvaluateFloat(E->getRHS(), Real, Info) || !LHSOK)
      return false;
    RHS.makeComplexFloat();
    RHS.FloatImag = APFloat(Real.getSemantics());
  } else if (!EvaluateComplex(E->getRHS(), RHS, Info) || !LHSOK)
    return false;

  assert(!(LHSReal && RHSReal) &&
         "Cannot have both operands of a complex operation be real.");
  switch (E->getOpcode()) {
  default: return Error(E);
  case BO_Add:
    if (Result.isComplexFloat()) {
      Result.getComplexFloatReal().add(RHS.getComplexFloatReal(),
                                       APFloat::rmNearestTiesToEven);
      if (LHSReal)
        Result.getComplexFloatImag() = RHS.getComplexFloatImag();
      else if (!RHSReal)
        Result.getComplexFloatImag().add(RHS.getComplexFloatImag(),
                                         APFloat::rmNearestTiesToEven);
    } else {
      Result.getComplexIntReal() += RHS.getComplexIntReal();
      Result.getComplexIntImag() += RHS.getComplexIntImag();
    }
    break;
  case BO_Sub:
    if (Result.isComplexFloat()) {
      Result.getComplexFloatReal().subtract(RHS.getComplexFloatReal(),
                                            APFloat::rmNearestTiesToEven);
      if (LHSReal) {
        Result.getComplexFloatImag() = RHS.getComplexFloatImag();
        Result.getComplexFloatImag().changeSign();
      } else if (!RHSReal) {
        Result.getComplexFloatImag().subtract(RHS.getComplexFloatImag(),
                                              APFloat::rmNearestTiesToEven);
      }
    } else {
      Result.getComplexIntReal() -= RHS.getComplexIntReal();
      Result.getComplexIntImag() -= RHS.getComplexIntImag();
    }
    break;
  case BO_Mul:
    if (Result.isComplexFloat()) {
      // This is an implementation of complex multiplication according to the
      // constraints laid out in C11 Annex G. The implementation uses the
      // following naming scheme:
      //   (a + ib) * (c + id)
      ComplexValue LHS = Result;
      APFloat &A = LHS.getComplexFloatReal();
      APFloat &B = LHS.getComplexFloatImag();
      APFloat &C = RHS.getComplexFloatReal();
      APFloat &D = RHS.getComplexFloatImag();
      APFloat &ResR = Result.getComplexFloatReal();
      APFloat &ResI = Result.getComplexFloatImag();
      if (LHSReal) {
        assert(!RHSReal && "Cannot have two real operands for a complex op!");
        ResR = A * C;
        ResI = A * D;
      } else if (RHSReal) {
        ResR = C * A;
        ResI = C * B;
      } else {
        // In the fully general case, we need to handle NaNs and infinities
        // robustly.
        APFloat AC = A * C;
        APFloat BD = B * D;
        APFloat AD = A * D;
        APFloat BC = B * C;
        ResR = AC - BD;
        ResI = AD + BC;
        if (ResR.isNaN() && ResI.isNaN()) {
          bool Recalc = false;
          if (A.isInfinity() || B.isInfinity()) {
            A = APFloat::copySign(
                APFloat(A.getSemantics(), A.isInfinity() ? 1 : 0), A);
            B = APFloat::copySign(
                APFloat(B.getSemantics(), B.isInfinity() ? 1 : 0), B);
            if (C.isNaN())
              C = APFloat::copySign(APFloat(C.getSemantics()), C);
            if (D.isNaN())
              D = APFloat::copySign(APFloat(D.getSemantics()), D);
            Recalc = true;
          }
          if (C.isInfinity() || D.isInfinity()) {
            C = APFloat::copySign(
                APFloat(C.getSemantics(), C.isInfinity() ? 1 : 0), C);
            D = APFloat::copySign(
                APFloat(D.getSemantics(), D.isInfinity() ? 1 : 0), D);
            if (A.isNaN())
              A = APFloat::copySign(APFloat(A.getSemantics()), A);
            if (B.isNaN())
              B = APFloat::copySign(APFloat(B.getSemantics()), B);
            Recalc = true;
          }
          if (!Recalc && (AC.isInfinity() || BD.isInfinity() ||
                          AD.isInfinity() || BC.isInfinity())) {
            if (A.isNaN())
              A = APFloat::copySign(APFloat(A.getSemantics()), A);
            if (B.isNaN())
              B = APFloat::copySign(APFloat(B.getSemantics()), B);
            if (C.isNaN())
              C = APFloat::copySign(APFloat(C.getSemantics()), C);
            if (D.isNaN())
              D = APFloat::copySign(APFloat(D.getSemantics()), D);
            Recalc = true;
          }
          if (Recalc) {
            ResR = APFloat::getInf(A.getSemantics()) * (A * C - B * D);
            ResI = APFloat::getInf(A.getSemantics()) * (A * D + B * C);
          }
        }
      }
    } else {
      ComplexValue LHS = Result;
      Result.getComplexIntReal() =
        (LHS.getComplexIntReal() * RHS.getComplexIntReal() -
         LHS.getComplexIntImag() * RHS.getComplexIntImag());
      Result.getComplexIntImag() =
        (LHS.getComplexIntReal() * RHS.getComplexIntImag() +
         LHS.getComplexIntImag() * RHS.getComplexIntReal());
    }
    break;
  case BO_Div:
    if (Result.isComplexFloat()) {
      // This is an implementation of complex division according to the
      // constraints laid out in C11 Annex G. The implementation uses the
      // following naming scheme:
      //   (a + ib) / (c + id)
      ComplexValue LHS = Result;
      APFloat &A = LHS.getComplexFloatReal();
      APFloat &B = LHS.getComplexFloatImag();
      APFloat &C = RHS.getComplexFloatReal();
      APFloat &D = RHS.getComplexFloatImag();
      APFloat &ResR = Result.getComplexFloatReal();
      APFloat &ResI = Result.getComplexFloatImag();
      if (RHSReal) {
        ResR = A / C;
        ResI = B / C;
      } else {
        if (LHSReal) {
          // No real optimizations we can do here, stub out with zero.
          B = APFloat::getZero(A.getSemantics());
        }
        int DenomLogB = 0;
        APFloat MaxCD = maxnum(abs(C), abs(D));
        if (MaxCD.isFinite()) {
          DenomLogB = ilogb(MaxCD);
          C = scalbn(C, -DenomLogB, APFloat::rmNearestTiesToEven);
          D = scalbn(D, -DenomLogB, APFloat::rmNearestTiesToEven);
        }
        APFloat Denom = C * C + D * D;
        ResR = scalbn((A * C + B * D) / Denom, -DenomLogB,
                      APFloat::rmNearestTiesToEven);
        ResI = scalbn((B * C - A * D) / Denom, -DenomLogB,
                      APFloat::rmNearestTiesToEven);
        if (ResR.isNaN() && ResI.isNaN()) {
          if (Denom.isPosZero() && (!A.isNaN() || !B.isNaN())) {
            ResR = APFloat::getInf(ResR.getSemantics(), C.isNegative()) * A;
            ResI = APFloat::getInf(ResR.getSemantics(), C.isNegative()) * B;
          } else if ((A.isInfinity() || B.isInfinity()) && C.isFinite() &&
                     D.isFinite()) {
            A = APFloat::copySign(
                APFloat(A.getSemantics(), A.isInfinity() ? 1 : 0), A);
            B = APFloat::copySign(
                APFloat(B.getSemantics(), B.isInfinity() ? 1 : 0), B);
            ResR = APFloat::getInf(ResR.getSemantics()) * (A * C + B * D);
            ResI = APFloat::getInf(ResI.getSemantics()) * (B * C - A * D);
          } else if (MaxCD.isInfinity() && A.isFinite() && B.isFinite()) {
            C = APFloat::copySign(
                APFloat(C.getSemantics(), C.isInfinity() ? 1 : 0), C);
            D = APFloat::copySign(
                APFloat(D.getSemantics(), D.isInfinity() ? 1 : 0), D);
            ResR = APFloat::getZero(ResR.getSemantics()) * (A * C + B * D);
            ResI = APFloat::getZero(ResI.getSemantics()) * (B * C - A * D);
          }
        }
      }
    } else {
      if (RHS.getComplexIntReal() == 0 && RHS.getComplexIntImag() == 0)
        return Error(E, diag::note_expr_divide_by_zero);

      ComplexValue LHS = Result;
      APSInt Den = RHS.getComplexIntReal() * RHS.getComplexIntReal() +
        RHS.getComplexIntImag() * RHS.getComplexIntImag();
      Result.getComplexIntReal() =
        (LHS.getComplexIntReal() * RHS.getComplexIntReal() +
         LHS.getComplexIntImag() * RHS.getComplexIntImag()) / Den;
      Result.getComplexIntImag() =
        (LHS.getComplexIntImag() * RHS.getComplexIntReal() -
         LHS.getComplexIntReal() * RHS.getComplexIntImag()) / Den;
    }
    break;
  }

  return true;
}

bool ComplexExprEvaluator::VisitUnaryOperator(const UnaryOperator *E) {
  // Get the operand value into 'Result'.
  if (!Visit(E->getSubExpr()))
    return false;

  switch (E->getOpcode()) {
  default:
    return Error(E);
  case UO_Extension:
    return true;
  case UO_Plus:
    // The result is always just the subexpr.
    return true;
  case UO_Minus:
    if (Result.isComplexFloat()) {
      Result.getComplexFloatReal().changeSign();
      Result.getComplexFloatImag().changeSign();
    }
    else {
      Result.getComplexIntReal() = -Result.getComplexIntReal();
      Result.getComplexIntImag() = -Result.getComplexIntImag();
    }
    return true;
  case UO_Not:
    if (Result.isComplexFloat())
      Result.getComplexFloatImag().changeSign();
    else
      Result.getComplexIntImag() = -Result.getComplexIntImag();
    return true;
  }
}

bool ComplexExprEvaluator::VisitInitListExpr(const InitListExpr *E) {
  if (E->getNumInits() == 2) {
    if (E->getType()->isComplexType()) {
      Result.makeComplexFloat();
      if (!EvaluateFloat(E->getInit(0), Result.FloatReal, Info))
        return false;
      if (!EvaluateFloat(E->getInit(1), Result.FloatImag, Info))
        return false;
    } else {
      Result.makeComplexInt();
      if (!EvaluateInteger(E->getInit(0), Result.IntReal, Info))
        return false;
      if (!EvaluateInteger(E->getInit(1), Result.IntImag, Info))
        return false;
    }
    return true;
  }
  return ExprEvaluatorBaseTy::VisitInitListExpr(E);
}

//===----------------------------------------------------------------------===//
// Atomic expression evaluation, essentially just handling the NonAtomicToAtomic
// implicit conversion.
//===----------------------------------------------------------------------===//

namespace {
class AtomicExprEvaluator :
    public ExprEvaluatorBase<AtomicExprEvaluator> {
  const LValue *This;
  APValue &Result;
public:
  AtomicExprEvaluator(EvalInfo &Info, const LValue *This, APValue &Result)
      : ExprEvaluatorBaseTy(Info), This(This), Result(Result) {}

  bool Success(const APValue &V, const Expr *E) {
    Result = V;
    return true;
  }

  bool ZeroInitialization(const Expr *E) {
    ImplicitValueInitExpr VIE(
        E->getType()->castAs<AtomicType>()->getValueType());
    // For atomic-qualified class (and array) types in C++, initialize the
    // _Atomic-wrapped subobject directly, in-place.
    return This ? EvaluateInPlace(Result, Info, *This, &VIE)
                : Evaluate(Result, Info, &VIE);
  }

  bool VisitCastExpr(const CastExpr *E) {
    switch (E->getCastKind()) {
    default:
      return ExprEvaluatorBaseTy::VisitCastExpr(E);
    case CK_NonAtomicToAtomic:
      return This ? EvaluateInPlace(Result, Info, *This, E->getSubExpr())
                  : Evaluate(Result, Info, E->getSubExpr());
    }
  }
};
} // end anonymous namespace

static bool EvaluateAtomic(const Expr *E, const LValue *This, APValue &Result,
                           EvalInfo &Info) {
  assert(E->isRValue() && E->getType()->isAtomicType());
  return AtomicExprEvaluator(Info, This, Result).Visit(E);
}

//===----------------------------------------------------------------------===//
// Void expression evaluation, primarily for a cast to void on the LHS of a
// comma operator
//===----------------------------------------------------------------------===//

namespace {
class VoidExprEvaluator
  : public ExprEvaluatorBase<VoidExprEvaluator> {
public:
  VoidExprEvaluator(EvalInfo &Info) : ExprEvaluatorBaseTy(Info) {}

  bool Success(const APValue &V, const Expr *e) { return true; }

  bool ZeroInitialization(const Expr *E) { return true; }

  bool VisitCastExpr(const CastExpr *E) {
    switch (E->getCastKind()) {
    default:
      return ExprEvaluatorBaseTy::VisitCastExpr(E);
    case CK_ToVoid:
      VisitIgnoredValue(E->getSubExpr());
      return true;
    }
  }

  bool VisitCallExpr(const CallExpr *E) {
    switch (E->getBuiltinCallee()) {
    default:
      return ExprEvaluatorBaseTy::VisitCallExpr(E);
    case Builtin::BI__assume:
    case Builtin::BI__builtin_assume:
      // The argument is not evaluated!
      return true;
    }
  }
};
} // end anonymous namespace

static bool EvaluateVoid(const Expr *E, EvalInfo &Info) {
  assert(E->isRValue() && E->getType()->isVoidType());
  return VoidExprEvaluator(Info).Visit(E);
}

//===----------------------------------------------------------------------===//
// Top level Expr::EvaluateAsRValue method.
//===----------------------------------------------------------------------===//

static bool Evaluate(APValue &Result, EvalInfo &Info, const Expr *E) {
  // In C, function designators are not lvalues, but we evaluate them as if they
  // are.
  QualType T = E->getType();
  if (E->isGLValue() || T->isFunctionType()) {
    LValue LV;
    if (!EvaluateLValue(E, LV, Info))
      return false;
    LV.moveInto(Result);
  } else if (T->isVectorType()) {
    if (!EvaluateVector(E, Result, Info))
      return false;
  } else if (T->isIntegralOrEnumerationType()) {
    if (!IntExprEvaluator(Info, Result).Visit(E))
      return false;
  } else if (T->hasPointerRepresentation()) {
    LValue LV;
    if (!EvaluatePointer(E, LV, Info))
      return false;
    LV.moveInto(Result);
  } else if (T->isRealFloatingType()) {
    llvm::APFloat F(0.0);
    if (!EvaluateFloat(E, F, Info))
      return false;
    Result = APValue(F);
  } else if (T->isAnyComplexType()) {
    ComplexValue C;
    if (!EvaluateComplex(E, C, Info))
      return false;
    C.moveInto(Result);
  } else if (T->isFixedPointType()) {
    if (!FixedPointExprEvaluator(Info, Result).Visit(E)) return false;
  } else if (T->isMemberPointerType()) {
    MemberPtr P;
    if (!EvaluateMemberPointer(E, P, Info))
      return false;
    P.moveInto(Result);
    return true;
  } else if (T->isArrayType()) {
    LValue LV;
    APValue &Value = createTemporary(E, false, LV, *Info.CurrentCall);
    if (!EvaluateArray(E, LV, Value, Info))
      return false;
    Result = Value;
  } else if (T->isRecordType()) {
    LValue LV;
    APValue &Value = createTemporary(E, false, LV, *Info.CurrentCall);
    if (!EvaluateRecord(E, LV, Value, Info))
      return false;
    Result = Value;
  } else if (T->isVoidType()) {
    if (!Info.getLangOpts().CPlusPlus11)
      Info.CCEDiag(E, diag::note_constexpr_nonliteral)
        << E->getType();
    if (!EvaluateVoid(E, Info))
      return false;
  } else if (T->isAtomicType()) {
    QualType Unqual = T.getAtomicUnqualifiedType();
    if (Unqual->isArrayType() || Unqual->isRecordType()) {
      LValue LV;
      APValue &Value = createTemporary(E, false, LV, *Info.CurrentCall);
      if (!EvaluateAtomic(E, &LV, Value, Info))
        return false;
    } else {
      if (!EvaluateAtomic(E, nullptr, Result, Info))
        return false;
    }
  } else if (Info.getLangOpts().CPlusPlus11) {
    Info.FFDiag(E, diag::note_constexpr_nonliteral) << E->getType();
    return false;
  } else {
    Info.FFDiag(E, diag::note_invalid_subexpr_in_const_expr);
    return false;
  }

  return true;
}

/// EvaluateInPlace - Evaluate an expression in-place in an APValue. In some
/// cases, the in-place evaluation is essential, since later initializers for
/// an object can indirectly refer to subobjects which were initialized earlier.
static bool EvaluateInPlace(APValue &Result, EvalInfo &Info, const LValue &This,
                            const Expr *E, bool AllowNonLiteralTypes) {
  assert(!E->isValueDependent());

  if (!AllowNonLiteralTypes && !CheckLiteralType(Info, E, &This))
    return false;

  if (E->isRValue()) {
    // Evaluate arrays and record types in-place, so that later initializers can
    // refer to earlier-initialized members of the object.
    QualType T = E->getType();
    if (T->isArrayType())
      return EvaluateArray(E, This, Result, Info);
    else if (T->isRecordType())
      return EvaluateRecord(E, This, Result, Info);
    else if (T->isAtomicType()) {
      QualType Unqual = T.getAtomicUnqualifiedType();
      if (Unqual->isArrayType() || Unqual->isRecordType())
        return EvaluateAtomic(E, &This, Result, Info);
    }
  }

  // For any other type, in-place evaluation is unimportant.
  return Evaluate(Result, Info, E);
}

/// EvaluateAsRValue - Try to evaluate this expression, performing an implicit
/// lvalue-to-rvalue cast if it is an lvalue.
static bool EvaluateAsRValue(EvalInfo &Info, const Expr *E, APValue &Result) {
  if (E->getType().isNull())
    return false;

  if (!CheckLiteralType(Info, E))
    return false;

  if (!::Evaluate(Result, Info, E))
    return false;

  if (E->isGLValue()) {
    LValue LV;
    LV.setFrom(Info.Ctx, Result);
    if (!handleLValueToRValueConversion(Info, E, E->getType(), LV, Result))
      return false;
  }

  // Check this core constant expression is a constant expression.
  return CheckConstantExpression(Info, E->getExprLoc(), E->getType(), Result);
}

static bool FastEvaluateAsRValue(const Expr *Exp, Expr::EvalResult &Result,
                                 const ASTContext &Ctx, bool &IsConst) {
  // Fast-path evaluations of integer literals, since we sometimes see files
  // containing vast quantities of these.
  if (const IntegerLiteral *L = dyn_cast<IntegerLiteral>(Exp)) {
    Result.Val = APValue(APSInt(L->getValue(),
                                L->getType()->isUnsignedIntegerType()));
    IsConst = true;
    return true;
  }

  // This case should be rare, but we need to check it before we check on
  // the type below.
  if (Exp->getType().isNull()) {
    IsConst = false;
    return true;
  }

  // FIXME: Evaluating values of large array and record types can cause
  // performance problems. Only do so in C++11 for now.
  if (Exp->isRValue() && (Exp->getType()->isArrayType() ||
                          Exp->getType()->isRecordType()) &&
      !Ctx.getLangOpts().CPlusPlus11) {
    IsConst = false;
    return true;
  }
  return false;
}

static bool hasUnacceptableSideEffect(Expr::EvalStatus &Result,
                                      Expr::SideEffectsKind SEK) {
  return (SEK < Expr::SE_AllowSideEffects && Result.HasSideEffects) ||
         (SEK < Expr::SE_AllowUndefinedBehavior && Result.HasUndefinedBehavior);
}

static bool EvaluateAsRValue(const Expr *E, Expr::EvalResult &Result,
                             const ASTContext &Ctx, EvalInfo &Info) {
  bool IsConst;
  if (FastEvaluateAsRValue(E, Result, Ctx, IsConst))
    return IsConst;

  return EvaluateAsRValue(Info, E, Result.Val);
}

static bool EvaluateAsInt(const Expr *E, Expr::EvalResult &ExprResult,
                          const ASTContext &Ctx,
                          Expr::SideEffectsKind AllowSideEffects,
                          EvalInfo &Info) {
  if (!E->getType()->isIntegralOrEnumerationType())
    return false;

  if (!::EvaluateAsRValue(E, ExprResult, Ctx, Info) ||
      !ExprResult.Val.isInt() ||
      hasUnacceptableSideEffect(ExprResult, AllowSideEffects))
    return false;

  return true;
}

/// EvaluateAsRValue - Return true if this is a constant which we can fold using
/// any crazy technique (that has nothing to do with language standards) that
/// we want to.  If this function returns true, it returns the folded constant
/// in Result. If this expression is a glvalue, an lvalue-to-rvalue conversion
/// will be applied to the result.
bool Expr::EvaluateAsRValue(EvalResult &Result, const ASTContext &Ctx,
                            bool InConstantContext) const {
  EvalInfo Info(Ctx, Result, EvalInfo::EM_IgnoreSideEffects);
  Info.InConstantContext = InConstantContext;
  return ::EvaluateAsRValue(this, Result, Ctx, Info);
}

bool Expr::EvaluateAsBooleanCondition(bool &Result,
                                      const ASTContext &Ctx) const {
  EvalResult Scratch;
  return EvaluateAsRValue(Scratch, Ctx) &&
         HandleConversionToBool(Scratch.Val, Result);
}

bool Expr::EvaluateAsInt(EvalResult &Result, const ASTContext &Ctx,
                         SideEffectsKind AllowSideEffects) const {
  EvalInfo Info(Ctx, Result, EvalInfo::EM_IgnoreSideEffects);
  return ::EvaluateAsInt(this, Result, Ctx, AllowSideEffects, Info);
}

bool Expr::EvaluateAsFloat(APFloat &Result, const ASTContext &Ctx,
                           SideEffectsKind AllowSideEffects) const {
  if (!getType()->isRealFloatingType())
    return false;

  EvalResult ExprResult;
  if (!EvaluateAsRValue(ExprResult, Ctx) || !ExprResult.Val.isFloat() ||
      hasUnacceptableSideEffect(ExprResult, AllowSideEffects))
    return false;

  Result = ExprResult.Val.getFloat();
  return true;
}

bool Expr::EvaluateAsLValue(EvalResult &Result, const ASTContext &Ctx) const {
  EvalInfo Info(Ctx, Result, EvalInfo::EM_ConstantFold);

  LValue LV;
  if (!EvaluateLValue(this, LV, Info) || Result.HasSideEffects ||
      !CheckLValueConstantExpression(Info, getExprLoc(),
                                     Ctx.getLValueReferenceType(getType()), LV,
                                     Expr::EvaluateForCodeGen))
    return false;

  LV.moveInto(Result.Val);
  return true;
}

bool Expr::EvaluateAsConstantExpr(EvalResult &Result, ConstExprUsage Usage,
                                  const ASTContext &Ctx) const {
  EvalInfo::EvaluationMode EM = EvalInfo::EM_ConstantExpression;
  EvalInfo Info(Ctx, Result, EM);
  Info.InConstantContext = true;
  if (!::Evaluate(Result.Val, Info, this))
    return false;

  return CheckConstantExpression(Info, getExprLoc(), getType(), Result.Val,
                                 Usage);
}

bool Expr::EvaluateAsInitializer(APValue &Value, const ASTContext &Ctx,
                                 const VarDecl *VD,
                            SmallVectorImpl<PartialDiagnosticAt> &Notes) const {
  // FIXME: Evaluating initializers for large array and record types can cause
  // performance problems. Only do so in C++11 for now.
  if (isRValue() && (getType()->isArrayType() || getType()->isRecordType()) &&
      !Ctx.getLangOpts().CPlusPlus11)
    return false;

  Expr::EvalStatus EStatus;
  EStatus.Diag = &Notes;

  EvalInfo InitInfo(Ctx, EStatus, VD->isConstexpr()
                                      ? EvalInfo::EM_ConstantExpression
                                      : EvalInfo::EM_ConstantFold);
  InitInfo.setEvaluatingDecl(VD, Value);
  InitInfo.InConstantContext = true;

  LValue LVal;
  LVal.set(VD);

  // C++11 [basic.start.init]p2:
  //  Variables with static storage duration or thread storage duration shall be
  //  zero-initialized before any other initialization takes place.
  // This behavior is not present in C.
  if (Ctx.getLangOpts().CPlusPlus && !VD->hasLocalStorage() &&
      !VD->getType()->isReferenceType()) {
    ImplicitValueInitExpr VIE(VD->getType());
    if (!EvaluateInPlace(Value, InitInfo, LVal, &VIE,
                         /*AllowNonLiteralTypes=*/true))
      return false;
  }

  if (!EvaluateInPlace(Value, InitInfo, LVal, this,
                       /*AllowNonLiteralTypes=*/true) ||
      EStatus.HasSideEffects)
    return false;

  return CheckConstantExpression(InitInfo, VD->getLocation(), VD->getType(),
                                 Value);
}

/// isEvaluatable - Call EvaluateAsRValue to see if this expression can be
/// constant folded, but discard the result.
bool Expr::isEvaluatable(const ASTContext &Ctx, SideEffectsKind SEK) const {
  EvalResult Result;
  return EvaluateAsRValue(Result, Ctx, /* in constant context */ true) &&
         !hasUnacceptableSideEffect(Result, SEK);
}

APSInt Expr::EvaluateKnownConstInt(const ASTContext &Ctx,
                    SmallVectorImpl<PartialDiagnosticAt> *Diag) const {
  EvalResult EVResult;
  EVResult.Diag = Diag;
  EvalInfo Info(Ctx, EVResult, EvalInfo::EM_IgnoreSideEffects);
  Info.InConstantContext = true;

  bool Result = ::EvaluateAsRValue(this, EVResult, Ctx, Info);
  (void)Result;
  assert(Result && "Could not evaluate expression");
  assert(EVResult.Val.isInt() && "Expression did not evaluate to integer");

  return EVResult.Val.getInt();
}

APSInt Expr::EvaluateKnownConstIntCheckOverflow(
    const ASTContext &Ctx, SmallVectorImpl<PartialDiagnosticAt> *Diag) const {
  EvalResult EVResult;
  EVResult.Diag = Diag;
  EvalInfo Info(Ctx, EVResult, EvalInfo::EM_EvaluateForOverflow);
  Info.InConstantContext = true;

  bool Result = ::EvaluateAsRValue(Info, this, EVResult.Val);
  (void)Result;
  assert(Result && "Could not evaluate expression");
  assert(EVResult.Val.isInt() && "Expression did not evaluate to integer");

  return EVResult.Val.getInt();
}

void Expr::EvaluateForOverflow(const ASTContext &Ctx) const {
  bool IsConst;
  EvalResult EVResult;
  if (!FastEvaluateAsRValue(this, EVResult, Ctx, IsConst)) {
    EvalInfo Info(Ctx, EVResult, EvalInfo::EM_EvaluateForOverflow);
    (void)::EvaluateAsRValue(Info, this, EVResult.Val);
  }
}

bool Expr::EvalResult::isGlobalLValue() const {
  assert(Val.isLValue());
  return IsGlobalLValue(Val.getLValueBase());
}


/// isIntegerConstantExpr - this recursive routine will test if an expression is
/// an integer constant expression.

/// FIXME: Pass up a reason why! Invalid operation in i-c-e, division by zero,
/// comma, etc

// CheckICE - This function does the fundamental ICE checking: the returned
// ICEDiag contains an ICEKind indicating whether the expression is an ICE,
// and a (possibly null) SourceLocation indicating the location of the problem.
//
// Note that to reduce code duplication, this helper does no evaluation
// itself; the caller checks whether the expression is evaluatable, and
// in the rare cases where CheckICE actually cares about the evaluated
// value, it calls into Evaluate.

namespace {

enum ICEKind {
  /// This expression is an ICE.
  IK_ICE,
  /// This expression is not an ICE, but if it isn't evaluated, it's
  /// a legal subexpression for an ICE. This return value is used to handle
  /// the comma operator in C99 mode, and non-constant subexpressions.
  IK_ICEIfUnevaluated,
  /// This expression is not an ICE, and is not a legal subexpression for one.
  IK_NotICE
};

struct ICEDiag {
  ICEKind Kind;
  SourceLocation Loc;

  ICEDiag(ICEKind IK, SourceLocation l) : Kind(IK), Loc(l) {}
};

}

static ICEDiag NoDiag() { return ICEDiag(IK_ICE, SourceLocation()); }

static ICEDiag Worst(ICEDiag A, ICEDiag B) { return A.Kind >= B.Kind ? A : B; }

static ICEDiag CheckEvalInICE(const Expr* E, const ASTContext &Ctx) {
  Expr::EvalResult EVResult;
  Expr::EvalStatus Status;
  EvalInfo Info(Ctx, Status, EvalInfo::EM_ConstantExpression);

  Info.InConstantContext = true;
  if (!::EvaluateAsRValue(E, EVResult, Ctx, Info) || EVResult.HasSideEffects ||
      !EVResult.Val.isInt())
    return ICEDiag(IK_NotICE, E->getBeginLoc());

  return NoDiag();
}

static ICEDiag CheckICE(const Expr* E, const ASTContext &Ctx) {
  assert(!E->isValueDependent() && "Should not see value dependent exprs!");
  if (!E->getType()->isIntegralOrEnumerationType())
    return ICEDiag(IK_NotICE, E->getBeginLoc());

  switch (E->getStmtClass()) {
#define ABSTRACT_STMT(Node)
#define STMT(Node, Base) case Expr::Node##Class:
#define EXPR(Node, Base)
#include "clang/AST/StmtNodes.inc"
  case Expr::PredefinedExprClass:
  case Expr::FloatingLiteralClass:
  case Expr::ImaginaryLiteralClass:
  case Expr::StringLiteralClass:
  case Expr::ArraySubscriptExprClass:
  case Expr::OMPArraySectionExprClass:
  case Expr::MemberExprClass:
  case Expr::CompoundAssignOperatorClass:
  case Expr::CompoundLiteralExprClass:
  case Expr::ExtVectorElementExprClass:
  case Expr::DesignatedInitExprClass:
  case Expr::ArrayInitLoopExprClass:
  case Expr::ArrayInitIndexExprClass:
  case Expr::NoInitExprClass:
  case Expr::DesignatedInitUpdateExprClass:
  case Expr::ImplicitValueInitExprClass:
  case Expr::ParenListExprClass:
  case Expr::VAArgExprClass:
  case Expr::AddrLabelExprClass:
  case Expr::StmtExprClass:
  case Expr::CXXMemberCallExprClass:
  case Expr::CUDAKernelCallExprClass:
  case Expr::CXXDynamicCastExprClass:
  case Expr::CXXTypeidExprClass:
  case Expr::CXXUuidofExprClass:
  case Expr::MSPropertyRefExprClass:
  case Expr::MSPropertySubscriptExprClass:
  case Expr::CXXNullPtrLiteralExprClass:
  case Expr::UserDefinedLiteralClass:
  case Expr::CXXThisExprClass:
  case Expr::CXXThrowExprClass:
  case Expr::CXXNewExprClass:
  case Expr::CXXDeleteExprClass:
  case Expr::CXXPseudoDestructorExprClass:
  case Expr::UnresolvedLookupExprClass:
  case Expr::TypoExprClass:
  case Expr::DependentScopeDeclRefExprClass:
  case Expr::CXXConstructExprClass:
  case Expr::CXXInheritedCtorInitExprClass:
  case Expr::CXXStdInitializerListExprClass:
  case Expr::CXXBindTemporaryExprClass:
  case Expr::ExprWithCleanupsClass:
  case Expr::CXXTemporaryObjectExprClass:
  case Expr::CXXUnresolvedConstructExprClass:
  case Expr::CXXDependentScopeMemberExprClass:
  case Expr::UnresolvedMemberExprClass:
  case Expr::ObjCStringLiteralClass:
  case Expr::ObjCBoxedExprClass:
  case Expr::ObjCArrayLiteralClass:
  case Expr::ObjCDictionaryLiteralClass:
  case Expr::ObjCEncodeExprClass:
  case Expr::ObjCMessageExprClass:
  case Expr::ObjCSelectorExprClass:
  case Expr::ObjCProtocolExprClass:
  case Expr::ObjCIvarRefExprClass:
  case Expr::ObjCPropertyRefExprClass:
  case Expr::ObjCSubscriptRefExprClass:
  case Expr::ObjCIsaExprClass:
  case Expr::ObjCAvailabilityCheckExprClass:
  case Expr::ShuffleVectorExprClass:
  case Expr::ConvertVectorExprClass:
  case Expr::BlockExprClass:
  case Expr::NoStmtClass:
  case Expr::OpaqueValueExprClass:
  case Expr::PackExpansionExprClass:
  case Expr::SubstNonTypeTemplateParmPackExprClass:
  case Expr::FunctionParmPackExprClass:
  case Expr::AsTypeExprClass:
  case Expr::ObjCIndirectCopyRestoreExprClass:
  case Expr::MaterializeTemporaryExprClass:
  case Expr::PseudoObjectExprClass:
  case Expr::AtomicExprClass:
  case Expr::LambdaExprClass:
  case Expr::CXXFoldExprClass:
  case Expr::CoawaitExprClass:
  case Expr::DependentCoawaitExprClass:
  case Expr::CoyieldExprClass:
    return ICEDiag(IK_NotICE, E->getBeginLoc());

  case Expr::InitListExprClass: {
    // C++03 [dcl.init]p13: If T is a scalar type, then a declaration of the
    // form "T x = { a };" is equivalent to "T x = a;".
    // Unless we're initializing a reference, T is a scalar as it is known to be
    // of integral or enumeration type.
    if (E->isRValue())
      if (cast<InitListExpr>(E)->getNumInits() == 1)
        return CheckICE(cast<InitListExpr>(E)->getInit(0), Ctx);
    return ICEDiag(IK_NotICE, E->getBeginLoc());
  }

  case Expr::SizeOfPackExprClass:
  case Expr::GNUNullExprClass:
    // GCC considers the GNU __null value to be an integral constant expression.
    return NoDiag();

  case Expr::SubstNonTypeTemplateParmExprClass:
    return
      CheckICE(cast<SubstNonTypeTemplateParmExpr>(E)->getReplacement(), Ctx);

  case Expr::ConstantExprClass:
    return CheckICE(cast<ConstantExpr>(E)->getSubExpr(), Ctx);

  case Expr::ParenExprClass:
    return CheckICE(cast<ParenExpr>(E)->getSubExpr(), Ctx);
  case Expr::GenericSelectionExprClass:
    return CheckICE(cast<GenericSelectionExpr>(E)->getResultExpr(), Ctx);
  case Expr::IntegerLiteralClass:
  case Expr::FixedPointLiteralClass:
  case Expr::CharacterLiteralClass:
  case Expr::ObjCBoolLiteralExprClass:
  case Expr::CXXBoolLiteralExprClass:
  case Expr::CXXScalarValueInitExprClass:
  case Expr::TypeTraitExprClass:
  case Expr::ArrayTypeTraitExprClass:
  case Expr::ExpressionTraitExprClass:
  case Expr::CXXNoexceptExprClass:
    return NoDiag();
  case Expr::CallExprClass:
  case Expr::CXXOperatorCallExprClass: {
    // C99 6.6/3 allows function calls within unevaluated subexpressions of
    // constant expressions, but they can never be ICEs because an ICE cannot
    // contain an operand of (pointer to) function type.
    const CallExpr *CE = cast<CallExpr>(E);
    if (CE->getBuiltinCallee())
      return CheckEvalInICE(E, Ctx);
    return ICEDiag(IK_NotICE, E->getBeginLoc());
  }
  case Expr::DeclRefExprClass: {
    if (isa<EnumConstantDecl>(cast<DeclRefExpr>(E)->getDecl()))
      return NoDiag();
    const ValueDecl *D = cast<DeclRefExpr>(E)->getDecl();
    if (Ctx.getLangOpts().CPlusPlus &&
        D && IsConstNonVolatile(D->getType())) {
      // Parameter variables are never constants.  Without this check,
      // getAnyInitializer() can find a default argument, which leads
      // to chaos.
      if (isa<ParmVarDecl>(D))
        return ICEDiag(IK_NotICE, cast<DeclRefExpr>(E)->getLocation());

      // C++ 7.1.5.1p2
      //   A variable of non-volatile const-qualified integral or enumeration
      //   type initialized by an ICE can be used in ICEs.
      if (const VarDecl *Dcl = dyn_cast<VarDecl>(D)) {
        if (!Dcl->getType()->isIntegralOrEnumerationType())
          return ICEDiag(IK_NotICE, cast<DeclRefExpr>(E)->getLocation());

        const VarDecl *VD;
        // Look for a declaration of this variable that has an initializer, and
        // check whether it is an ICE.
        if (Dcl->getAnyInitializer(VD) && VD->checkInitIsICE())
          return NoDiag();
        else
          return ICEDiag(IK_NotICE, cast<DeclRefExpr>(E)->getLocation());
      }
    }
    return ICEDiag(IK_NotICE, E->getBeginLoc());
  }
  case Expr::UnaryOperatorClass: {
    const UnaryOperator *Exp = cast<UnaryOperator>(E);
    switch (Exp->getOpcode()) {
    case UO_PostInc:
    case UO_PostDec:
    case UO_PreInc:
    case UO_PreDec:
    case UO_AddrOf:
    case UO_Deref:
    case UO_Coawait:
      // C99 6.6/3 allows increment and decrement within unevaluated
      // subexpressions of constant expressions, but they can never be ICEs
      // because an ICE cannot contain an lvalue operand.
      return ICEDiag(IK_NotICE, E->getBeginLoc());
    case UO_Extension:
    case UO_LNot:
    case UO_Plus:
    case UO_Minus:
    case UO_Not:
    case UO_Real:
    case UO_Imag:
      return CheckICE(Exp->getSubExpr(), Ctx);
    }
    llvm_unreachable("invalid unary operator class");
  }
  case Expr::OffsetOfExprClass: {
    // Note that per C99, offsetof must be an ICE. And AFAIK, using
    // EvaluateAsRValue matches the proposed gcc behavior for cases like
    // "offsetof(struct s{int x[4];}, x[1.0])".  This doesn't affect
    // compliance: we should warn earlier for offsetof expressions with
    // array subscripts that aren't ICEs, and if the array subscripts
    // are ICEs, the value of the offsetof must be an integer constant.
    return CheckEvalInICE(E, Ctx);
  }
  case Expr::UnaryExprOrTypeTraitExprClass: {
    const UnaryExprOrTypeTraitExpr *Exp = cast<UnaryExprOrTypeTraitExpr>(E);
    if ((Exp->getKind() ==  UETT_SizeOf) &&
        Exp->getTypeOfArgument()->isVariableArrayType())
      return ICEDiag(IK_NotICE, E->getBeginLoc());
    return NoDiag();
  }
  case Expr::BinaryOperatorClass: {
    const BinaryOperator *Exp = cast<BinaryOperator>(E);
    switch (Exp->getOpcode()) {
    case BO_PtrMemD:
    case BO_PtrMemI:
    case BO_Assign:
    case BO_MulAssign:
    case BO_DivAssign:
    case BO_RemAssign:
    case BO_AddAssign:
    case BO_SubAssign:
    case BO_ShlAssign:
    case BO_ShrAssign:
    case BO_AndAssign:
    case BO_XorAssign:
    case BO_OrAssign:
      // C99 6.6/3 allows assignments within unevaluated subexpressions of
      // constant expressions, but they can never be ICEs because an ICE cannot
      // contain an lvalue operand.
      return ICEDiag(IK_NotICE, E->getBeginLoc());

    case BO_Mul:
    case BO_Div:
    case BO_Rem:
    case BO_Add:
    case BO_Sub:
    case BO_Shl:
    case BO_Shr:
    case BO_LT:
    case BO_GT:
    case BO_LE:
    case BO_GE:
    case BO_EQ:
    case BO_NE:
    case BO_And:
    case BO_Xor:
    case BO_Or:
    case BO_Comma:
    case BO_Cmp: {
      ICEDiag LHSResult = CheckICE(Exp->getLHS(), Ctx);
      ICEDiag RHSResult = CheckICE(Exp->getRHS(), Ctx);
      if (Exp->getOpcode() == BO_Div ||
          Exp->getOpcode() == BO_Rem) {
        // EvaluateAsRValue gives an error for undefined Div/Rem, so make sure
        // we don't evaluate one.
        if (LHSResult.Kind == IK_ICE && RHSResult.Kind == IK_ICE) {
          llvm::APSInt REval = Exp->getRHS()->EvaluateKnownConstInt(Ctx);
          if (REval == 0)
            return ICEDiag(IK_ICEIfUnevaluated, E->getBeginLoc());
          if (REval.isSigned() && REval.isAllOnesValue()) {
            llvm::APSInt LEval = Exp->getLHS()->EvaluateKnownConstInt(Ctx);
            if (LEval.isMinSignedValue())
              return ICEDiag(IK_ICEIfUnevaluated, E->getBeginLoc());
          }
        }
      }
      if (Exp->getOpcode() == BO_Comma) {
        if (Ctx.getLangOpts().C99) {
          // C99 6.6p3 introduces a strange edge case: comma can be in an ICE
          // if it isn't evaluated.
          if (LHSResult.Kind == IK_ICE && RHSResult.Kind == IK_ICE)
            return ICEDiag(IK_ICEIfUnevaluated, E->getBeginLoc());
        } else {
          // In both C89 and C++, commas in ICEs are illegal.
          return ICEDiag(IK_NotICE, E->getBeginLoc());
        }
      }
      return Worst(LHSResult, RHSResult);
    }
    case BO_LAnd:
    case BO_LOr: {
      ICEDiag LHSResult = CheckICE(Exp->getLHS(), Ctx);
      ICEDiag RHSResult = CheckICE(Exp->getRHS(), Ctx);
      if (LHSResult.Kind == IK_ICE && RHSResult.Kind == IK_ICEIfUnevaluated) {
        // Rare case where the RHS has a comma "side-effect"; we need
        // to actually check the condition to see whether the side
        // with the comma is evaluated.
        if ((Exp->getOpcode() == BO_LAnd) !=
            (Exp->getLHS()->EvaluateKnownConstInt(Ctx) == 0))
          return RHSResult;
        return NoDiag();
      }

      return Worst(LHSResult, RHSResult);
    }
    }
    llvm_unreachable("invalid binary operator kind");
  }
  case Expr::ImplicitCastExprClass:
  case Expr::CStyleCastExprClass:
  case Expr::CXXFunctionalCastExprClass:
  case Expr::CXXStaticCastExprClass:
  case Expr::CXXReinterpretCastExprClass:
  case Expr::CXXConstCastExprClass:
  case Expr::ObjCBridgedCastExprClass: {
    const Expr *SubExpr = cast<CastExpr>(E)->getSubExpr();
    if (isa<ExplicitCastExpr>(E)) {
      if (const FloatingLiteral *FL
            = dyn_cast<FloatingLiteral>(SubExpr->IgnoreParenImpCasts())) {
        unsigned DestWidth = Ctx.getIntWidth(E->getType());
        bool DestSigned = E->getType()->isSignedIntegerOrEnumerationType();
        APSInt IgnoredVal(DestWidth, !DestSigned);
        bool Ignored;
        // If the value does not fit in the destination type, the behavior is
        // undefined, so we are not required to treat it as a constant
        // expression.
        if (FL->getValue().convertToInteger(IgnoredVal,
                                            llvm::APFloat::rmTowardZero,
                                            &Ignored) & APFloat::opInvalidOp)
          return ICEDiag(IK_NotICE, E->getBeginLoc());
        return NoDiag();
      }
    }
    switch (cast<CastExpr>(E)->getCastKind()) {
    case CK_LValueToRValue:
    case CK_AtomicToNonAtomic:
    case CK_NonAtomicToAtomic:
    case CK_NoOp:
    case CK_IntegralToBoolean:
    case CK_IntegralCast:
      return CheckICE(SubExpr, Ctx);
    default:
      return ICEDiag(IK_NotICE, E->getBeginLoc());
    }
  }
  case Expr::BinaryConditionalOperatorClass: {
    const BinaryConditionalOperator *Exp = cast<BinaryConditionalOperator>(E);
    ICEDiag CommonResult = CheckICE(Exp->getCommon(), Ctx);
    if (CommonResult.Kind == IK_NotICE) return CommonResult;
    ICEDiag FalseResult = CheckICE(Exp->getFalseExpr(), Ctx);
    if (FalseResult.Kind == IK_NotICE) return FalseResult;
    if (CommonResult.Kind == IK_ICEIfUnevaluated) return CommonResult;
    if (FalseResult.Kind == IK_ICEIfUnevaluated &&
        Exp->getCommon()->EvaluateKnownConstInt(Ctx) != 0) return NoDiag();
    return FalseResult;
  }
  case Expr::ConditionalOperatorClass: {
    const ConditionalOperator *Exp = cast<ConditionalOperator>(E);
    // If the condition (ignoring parens) is a __builtin_constant_p call,
    // then only the true side is actually considered in an integer constant
    // expression, and it is fully evaluated.  This is an important GNU
    // extension.  See GCC PR38377 for discussion.
    if (const CallExpr *CallCE
        = dyn_cast<CallExpr>(Exp->getCond()->IgnoreParenCasts()))
      if (CallCE->getBuiltinCallee() == Builtin::BI__builtin_constant_p)
        return CheckEvalInICE(E, Ctx);
    ICEDiag CondResult = CheckICE(Exp->getCond(), Ctx);
    if (CondResult.Kind == IK_NotICE)
      return CondResult;

    ICEDiag TrueResult = CheckICE(Exp->getTrueExpr(), Ctx);
    ICEDiag FalseResult = CheckICE(Exp->getFalseExpr(), Ctx);

    if (TrueResult.Kind == IK_NotICE)
      return TrueResult;
    if (FalseResult.Kind == IK_NotICE)
      return FalseResult;
    if (CondResult.Kind == IK_ICEIfUnevaluated)
      return CondResult;
    if (TrueResult.Kind == IK_ICE && FalseResult.Kind == IK_ICE)
      return NoDiag();
    // Rare case where the diagnostics depend on which side is evaluated
    // Note that if we get here, CondResult is 0, and at least one of
    // TrueResult and FalseResult is non-zero.
    if (Exp->getCond()->EvaluateKnownConstInt(Ctx) == 0)
      return FalseResult;
    return TrueResult;
  }
  case Expr::CXXDefaultArgExprClass:
    return CheckICE(cast<CXXDefaultArgExpr>(E)->getExpr(), Ctx);
  case Expr::CXXDefaultInitExprClass:
    return CheckICE(cast<CXXDefaultInitExpr>(E)->getExpr(), Ctx);
  case Expr::ChooseExprClass: {
    return CheckICE(cast<ChooseExpr>(E)->getChosenSubExpr(), Ctx);
  }
  }

  llvm_unreachable("Invalid StmtClass!");
}

/// Evaluate an expression as a C++11 integral constant expression.
static bool EvaluateCPlusPlus11IntegralConstantExpr(const ASTContext &Ctx,
                                                    const Expr *E,
                                                    llvm::APSInt *Value,
                                                    SourceLocation *Loc) {
  if (!E->getType()->isIntegralOrUnscopedEnumerationType()) {
    if (Loc) *Loc = E->getExprLoc();
    return false;
  }

  APValue Result;
  if (!E->isCXX11ConstantExpr(Ctx, &Result, Loc))
    return false;

  if (!Result.isInt()) {
    if (Loc) *Loc = E->getExprLoc();
    return false;
  }

  if (Value) *Value = Result.getInt();
  return true;
}

bool Expr::isIntegerConstantExpr(const ASTContext &Ctx,
                                 SourceLocation *Loc) const {
  if (Ctx.getLangOpts().CPlusPlus11)
    return EvaluateCPlusPlus11IntegralConstantExpr(Ctx, this, nullptr, Loc);

  ICEDiag D = CheckICE(this, Ctx);
  if (D.Kind != IK_ICE) {
    if (Loc) *Loc = D.Loc;
    return false;
  }
  return true;
}

bool Expr::isIntegerConstantExpr(llvm::APSInt &Value, const ASTContext &Ctx,
                                 SourceLocation *Loc, bool isEvaluated) const {
  if (Ctx.getLangOpts().CPlusPlus11)
    return EvaluateCPlusPlus11IntegralConstantExpr(Ctx, this, &Value, Loc);

  if (!isIntegerConstantExpr(Ctx, Loc))
    return false;

  // The only possible side-effects here are due to UB discovered in the
  // evaluation (for instance, INT_MAX + 1). In such a case, we are still
  // required to treat the expression as an ICE, so we produce the folded
  // value.
  EvalResult ExprResult;
  Expr::EvalStatus Status;
  EvalInfo Info(Ctx, Status, EvalInfo::EM_IgnoreSideEffects);
  Info.InConstantContext = true;

  if (!::EvaluateAsInt(this, ExprResult, Ctx, SE_AllowSideEffects, Info))
    llvm_unreachable("ICE cannot be evaluated!");

  Value = ExprResult.Val.getInt();
  return true;
}

bool Expr::isCXX98IntegralConstantExpr(const ASTContext &Ctx) const {
  return CheckICE(this, Ctx).Kind == IK_ICE;
}

bool Expr::isCXX11ConstantExpr(const ASTContext &Ctx, APValue *Result,
                               SourceLocation *Loc) const {
  // We support this checking in C++98 mode in order to diagnose compatibility
  // issues.
  assert(Ctx.getLangOpts().CPlusPlus);

  // Build evaluation settings.
  Expr::EvalStatus Status;
  SmallVector<PartialDiagnosticAt, 8> Diags;
  Status.Diag = &Diags;
  EvalInfo Info(Ctx, Status, EvalInfo::EM_ConstantExpression);

  APValue Scratch;
  bool IsConstExpr = ::EvaluateAsRValue(Info, this, Result ? *Result : Scratch);

  if (!Diags.empty()) {
    IsConstExpr = false;
    if (Loc) *Loc = Diags[0].first;
  } else if (!IsConstExpr) {
    // FIXME: This shouldn't happen.
    if (Loc) *Loc = getExprLoc();
  }

  return IsConstExpr;
}

bool Expr::EvaluateWithSubstitution(APValue &Value, ASTContext &Ctx,
                                    const FunctionDecl *Callee,
                                    ArrayRef<const Expr*> Args,
                                    const Expr *This) const {
  Expr::EvalStatus Status;
  EvalInfo Info(Ctx, Status, EvalInfo::EM_ConstantExpressionUnevaluated);
  Info.InConstantContext = true;

  LValue ThisVal;
  const LValue *ThisPtr = nullptr;
  if (This) {
#ifndef NDEBUG
    auto *MD = dyn_cast<CXXMethodDecl>(Callee);
    assert(MD && "Don't provide `this` for non-methods.");
    assert(!MD->isStatic() && "Don't provide `this` for static methods.");
#endif
    if (EvaluateObjectArgument(Info, This, ThisVal))
      ThisPtr = &ThisVal;
    if (Info.EvalStatus.HasSideEffects)
      return false;
  }

  ArgVector ArgValues(Args.size());
  for (ArrayRef<const Expr*>::iterator I = Args.begin(), E = Args.end();
       I != E; ++I) {
    if ((*I)->isValueDependent() ||
        !Evaluate(ArgValues[I - Args.begin()], Info, *I))
      // If evaluation fails, throw away the argument entirely.
      ArgValues[I - Args.begin()] = APValue();
    if (Info.EvalStatus.HasSideEffects)
      return false;
  }

  // Build fake call to Callee.
  CallStackFrame Frame(Info, Callee->getLocation(), Callee, ThisPtr,
                       ArgValues.data());
  return Evaluate(Value, Info, this) && !Info.EvalStatus.HasSideEffects;
}

bool Expr::isPotentialConstantExpr(const FunctionDecl *FD,
                                   SmallVectorImpl<
                                     PartialDiagnosticAt> &Diags) {
  // FIXME: It would be useful to check constexpr function templates, but at the
  // moment the constant expression evaluator cannot cope with the non-rigorous
  // ASTs which we build for dependent expressions.
  if (FD->isDependentContext())
    return true;

  Expr::EvalStatus Status;
  Status.Diag = &Diags;

  EvalInfo Info(FD->getASTContext(), Status,
                EvalInfo::EM_PotentialConstantExpression);
  Info.InConstantContext = true;

  const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD);
  const CXXRecordDecl *RD = MD ? MD->getParent()->getCanonicalDecl() : nullptr;

  // Fabricate an arbitrary expression on the stack and pretend that it
  // is a temporary being used as the 'this' pointer.
  LValue This;
  ImplicitValueInitExpr VIE(RD ? Info.Ctx.getRecordType(RD) : Info.Ctx.IntTy);
  This.set({&VIE, Info.CurrentCall->Index});

  ArrayRef<const Expr*> Args;

  APValue Scratch;
  if (const CXXConstructorDecl *CD = dyn_cast<CXXConstructorDecl>(FD)) {
    // Evaluate the call as a constant initializer, to allow the construction
    // of objects of non-literal types.
    Info.setEvaluatingDecl(This.getLValueBase(), Scratch);
    HandleConstructorCall(&VIE, This, Args, CD, Info, Scratch);
  } else {
    SourceLocation Loc = FD->getLocation();
    HandleFunctionCall(Loc, FD, (MD && MD->isInstance()) ? &This : nullptr,
                       Args, FD->getBody(), Info, Scratch, nullptr);
  }

  return Diags.empty();
}

bool Expr::isPotentialConstantExprUnevaluated(Expr *E,
                                              const FunctionDecl *FD,
                                              SmallVectorImpl<
                                                PartialDiagnosticAt> &Diags) {
  Expr::EvalStatus Status;
  Status.Diag = &Diags;

  EvalInfo Info(FD->getASTContext(), Status,
                EvalInfo::EM_PotentialConstantExpressionUnevaluated);
  Info.InConstantContext = true;

  // Fabricate a call stack frame to give the arguments a plausible cover story.
  ArrayRef<const Expr*> Args;
  ArgVector ArgValues(0);
  bool Success = EvaluateArgs(Args, ArgValues, Info);
  (void)Success;
  assert(Success &&
         "Failed to set up arguments for potential constant evaluation");
  CallStackFrame Frame(Info, SourceLocation(), FD, nullptr, ArgValues.data());

  APValue ResultScratch;
  Evaluate(ResultScratch, Info, E);
  return Diags.empty();
}

bool Expr::tryEvaluateObjectSize(uint64_t &Result, ASTContext &Ctx,
                                 unsigned Type) const {
  if (!getType()->isPointerType())
    return false;

  Expr::EvalStatus Status;
  EvalInfo Info(Ctx, Status, EvalInfo::EM_ConstantFold);
  return tryEvaluateBuiltinObjectSize(this, Type, Info, Result);
}
