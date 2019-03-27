//===- Overload.h - C++ Overloading -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the data structures and types used in C++
// overload resolution.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_OVERLOAD_H
#define LLVM_CLANG_SEMA_OVERLOAD_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclAccessPair.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/SemaFixItUtils.h"
#include "clang/Sema/TemplateDeduction.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace clang {

class APValue;
class ASTContext;
class Sema;

  /// OverloadingResult - Capture the result of performing overload
  /// resolution.
  enum OverloadingResult {
    /// Overload resolution succeeded.
    OR_Success,

    /// No viable function found.
    OR_No_Viable_Function,

    /// Ambiguous candidates found.
    OR_Ambiguous,

    /// Succeeded, but refers to a deleted function.
    OR_Deleted
  };

  enum OverloadCandidateDisplayKind {
    /// Requests that all candidates be shown.  Viable candidates will
    /// be printed first.
    OCD_AllCandidates,

    /// Requests that only viable candidates be shown.
    OCD_ViableCandidates
  };

  /// ImplicitConversionKind - The kind of implicit conversion used to
  /// convert an argument to a parameter's type. The enumerator values
  /// match with the table titled 'Conversions' in [over.ics.scs] and are listed
  /// such that better conversion kinds have smaller values.
  enum ImplicitConversionKind {
    /// Identity conversion (no conversion)
    ICK_Identity = 0,

    /// Lvalue-to-rvalue conversion (C++ [conv.lval])
    ICK_Lvalue_To_Rvalue,

    /// Array-to-pointer conversion (C++ [conv.array])
    ICK_Array_To_Pointer,

    /// Function-to-pointer (C++ [conv.array])
    ICK_Function_To_Pointer,

    /// Function pointer conversion (C++17 [conv.fctptr])
    ICK_Function_Conversion,

    /// Qualification conversions (C++ [conv.qual])
    ICK_Qualification,

    /// Integral promotions (C++ [conv.prom])
    ICK_Integral_Promotion,

    /// Floating point promotions (C++ [conv.fpprom])
    ICK_Floating_Promotion,

    /// Complex promotions (Clang extension)
    ICK_Complex_Promotion,

    /// Integral conversions (C++ [conv.integral])
    ICK_Integral_Conversion,

    /// Floating point conversions (C++ [conv.double]
    ICK_Floating_Conversion,

    /// Complex conversions (C99 6.3.1.6)
    ICK_Complex_Conversion,

    /// Floating-integral conversions (C++ [conv.fpint])
    ICK_Floating_Integral,

    /// Pointer conversions (C++ [conv.ptr])
    ICK_Pointer_Conversion,

    /// Pointer-to-member conversions (C++ [conv.mem])
    ICK_Pointer_Member,

    /// Boolean conversions (C++ [conv.bool])
    ICK_Boolean_Conversion,

    /// Conversions between compatible types in C99
    ICK_Compatible_Conversion,

    /// Derived-to-base (C++ [over.best.ics])
    ICK_Derived_To_Base,

    /// Vector conversions
    ICK_Vector_Conversion,

    /// A vector splat from an arithmetic type
    ICK_Vector_Splat,

    /// Complex-real conversions (C99 6.3.1.7)
    ICK_Complex_Real,

    /// Block Pointer conversions
    ICK_Block_Pointer_Conversion,

    /// Transparent Union Conversions
    ICK_TransparentUnionConversion,

    /// Objective-C ARC writeback conversion
    ICK_Writeback_Conversion,

    /// Zero constant to event (OpenCL1.2 6.12.10)
    ICK_Zero_Event_Conversion,

    /// Zero constant to queue
    ICK_Zero_Queue_Conversion,

    /// Conversions allowed in C, but not C++
    ICK_C_Only_Conversion,

    /// C-only conversion between pointers with incompatible types
    ICK_Incompatible_Pointer_Conversion,

    /// The number of conversion kinds
    ICK_Num_Conversion_Kinds,
  };

  /// ImplicitConversionRank - The rank of an implicit conversion
  /// kind. The enumerator values match with Table 9 of (C++
  /// 13.3.3.1.1) and are listed such that better conversion ranks
  /// have smaller values.
  enum ImplicitConversionRank {
    /// Exact Match
    ICR_Exact_Match = 0,

    /// Promotion
    ICR_Promotion,

    /// Conversion
    ICR_Conversion,

    /// OpenCL Scalar Widening
    ICR_OCL_Scalar_Widening,

    /// Complex <-> Real conversion
    ICR_Complex_Real_Conversion,

    /// ObjC ARC writeback conversion
    ICR_Writeback_Conversion,

    /// Conversion only allowed in the C standard (e.g. void* to char*).
    ICR_C_Conversion,

    /// Conversion not allowed by the C standard, but that we accept as an
    /// extension anyway.
    ICR_C_Conversion_Extension
  };

  ImplicitConversionRank GetConversionRank(ImplicitConversionKind Kind);

  /// NarrowingKind - The kind of narrowing conversion being performed by a
  /// standard conversion sequence according to C++11 [dcl.init.list]p7.
  enum NarrowingKind {
    /// Not a narrowing conversion.
    NK_Not_Narrowing,

    /// A narrowing conversion by virtue of the source and destination types.
    NK_Type_Narrowing,

    /// A narrowing conversion, because a constant expression got narrowed.
    NK_Constant_Narrowing,

    /// A narrowing conversion, because a non-constant-expression variable might
    /// have got narrowed.
    NK_Variable_Narrowing,

    /// Cannot tell whether this is a narrowing conversion because the
    /// expression is value-dependent.
    NK_Dependent_Narrowing,
  };

  /// StandardConversionSequence - represents a standard conversion
  /// sequence (C++ 13.3.3.1.1). A standard conversion sequence
  /// contains between zero and three conversions. If a particular
  /// conversion is not needed, it will be set to the identity conversion
  /// (ICK_Identity). Note that the three conversions are
  /// specified as separate members (rather than in an array) so that
  /// we can keep the size of a standard conversion sequence to a
  /// single word.
  class StandardConversionSequence {
  public:
    /// First -- The first conversion can be an lvalue-to-rvalue
    /// conversion, array-to-pointer conversion, or
    /// function-to-pointer conversion.
    ImplicitConversionKind First : 8;

    /// Second - The second conversion can be an integral promotion,
    /// floating point promotion, integral conversion, floating point
    /// conversion, floating-integral conversion, pointer conversion,
    /// pointer-to-member conversion, or boolean conversion.
    ImplicitConversionKind Second : 8;

    /// Third - The third conversion can be a qualification conversion
    /// or a function conversion.
    ImplicitConversionKind Third : 8;

    /// Whether this is the deprecated conversion of a
    /// string literal to a pointer to non-const character data
    /// (C++ 4.2p2).
    unsigned DeprecatedStringLiteralToCharPtr : 1;

    /// Whether the qualification conversion involves a change in the
    /// Objective-C lifetime (for automatic reference counting).
    unsigned QualificationIncludesObjCLifetime : 1;

    /// IncompatibleObjC - Whether this is an Objective-C conversion
    /// that we should warn about (if we actually use it).
    unsigned IncompatibleObjC : 1;

    /// ReferenceBinding - True when this is a reference binding
    /// (C++ [over.ics.ref]).
    unsigned ReferenceBinding : 1;

    /// DirectBinding - True when this is a reference binding that is a
    /// direct binding (C++ [dcl.init.ref]).
    unsigned DirectBinding : 1;

    /// Whether this is an lvalue reference binding (otherwise, it's
    /// an rvalue reference binding).
    unsigned IsLvalueReference : 1;

    /// Whether we're binding to a function lvalue.
    unsigned BindsToFunctionLvalue : 1;

    /// Whether we're binding to an rvalue.
    unsigned BindsToRvalue : 1;

    /// Whether this binds an implicit object argument to a
    /// non-static member function without a ref-qualifier.
    unsigned BindsImplicitObjectArgumentWithoutRefQualifier : 1;

    /// Whether this binds a reference to an object with a different
    /// Objective-C lifetime qualifier.
    unsigned ObjCLifetimeConversionBinding : 1;

    /// FromType - The type that this conversion is converting
    /// from. This is an opaque pointer that can be translated into a
    /// QualType.
    void *FromTypePtr;

    /// ToType - The types that this conversion is converting to in
    /// each step. This is an opaque pointer that can be translated
    /// into a QualType.
    void *ToTypePtrs[3];

    /// CopyConstructor - The copy constructor that is used to perform
    /// this conversion, when the conversion is actually just the
    /// initialization of an object via copy constructor. Such
    /// conversions are either identity conversions or derived-to-base
    /// conversions.
    CXXConstructorDecl *CopyConstructor;
    DeclAccessPair FoundCopyConstructor;

    void setFromType(QualType T) { FromTypePtr = T.getAsOpaquePtr(); }

    void setToType(unsigned Idx, QualType T) {
      assert(Idx < 3 && "To type index is out of range");
      ToTypePtrs[Idx] = T.getAsOpaquePtr();
    }

    void setAllToTypes(QualType T) {
      ToTypePtrs[0] = T.getAsOpaquePtr();
      ToTypePtrs[1] = ToTypePtrs[0];
      ToTypePtrs[2] = ToTypePtrs[0];
    }

    QualType getFromType() const {
      return QualType::getFromOpaquePtr(FromTypePtr);
    }

    QualType getToType(unsigned Idx) const {
      assert(Idx < 3 && "To type index is out of range");
      return QualType::getFromOpaquePtr(ToTypePtrs[Idx]);
    }

    void setAsIdentityConversion();

    bool isIdentityConversion() const {
      return Second == ICK_Identity && Third == ICK_Identity;
    }

    ImplicitConversionRank getRank() const;
    NarrowingKind
    getNarrowingKind(ASTContext &Context, const Expr *Converted,
                     APValue &ConstantValue, QualType &ConstantType,
                     bool IgnoreFloatToIntegralConversion = false) const;
    bool isPointerConversionToBool() const;
    bool isPointerConversionToVoidPointer(ASTContext& Context) const;
    void dump() const;
  };

  /// UserDefinedConversionSequence - Represents a user-defined
  /// conversion sequence (C++ 13.3.3.1.2).
  struct UserDefinedConversionSequence {
    /// Represents the standard conversion that occurs before
    /// the actual user-defined conversion.
    ///
    /// C++11 13.3.3.1.2p1:
    ///   If the user-defined conversion is specified by a constructor
    ///   (12.3.1), the initial standard conversion sequence converts
    ///   the source type to the type required by the argument of the
    ///   constructor. If the user-defined conversion is specified by
    ///   a conversion function (12.3.2), the initial standard
    ///   conversion sequence converts the source type to the implicit
    ///   object parameter of the conversion function.
    StandardConversionSequence Before;

    /// EllipsisConversion - When this is true, it means user-defined
    /// conversion sequence starts with a ... (ellipsis) conversion, instead of
    /// a standard conversion. In this case, 'Before' field must be ignored.
    // FIXME. I much rather put this as the first field. But there seems to be
    // a gcc code gen. bug which causes a crash in a test. Putting it here seems
    // to work around the crash.
    bool EllipsisConversion : 1;

    /// HadMultipleCandidates - When this is true, it means that the
    /// conversion function was resolved from an overloaded set having
    /// size greater than 1.
    bool HadMultipleCandidates : 1;

    /// After - Represents the standard conversion that occurs after
    /// the actual user-defined conversion.
    StandardConversionSequence After;

    /// ConversionFunction - The function that will perform the
    /// user-defined conversion. Null if the conversion is an
    /// aggregate initialization from an initializer list.
    FunctionDecl* ConversionFunction;

    /// The declaration that we found via name lookup, which might be
    /// the same as \c ConversionFunction or it might be a using declaration
    /// that refers to \c ConversionFunction.
    DeclAccessPair FoundConversionFunction;

    void dump() const;
  };

  /// Represents an ambiguous user-defined conversion sequence.
  struct AmbiguousConversionSequence {
    using ConversionSet =
        SmallVector<std::pair<NamedDecl *, FunctionDecl *>, 4>;

    void *FromTypePtr;
    void *ToTypePtr;
    char Buffer[sizeof(ConversionSet)];

    QualType getFromType() const {
      return QualType::getFromOpaquePtr(FromTypePtr);
    }

    QualType getToType() const {
      return QualType::getFromOpaquePtr(ToTypePtr);
    }

    void setFromType(QualType T) { FromTypePtr = T.getAsOpaquePtr(); }
    void setToType(QualType T) { ToTypePtr = T.getAsOpaquePtr(); }

    ConversionSet &conversions() {
      return *reinterpret_cast<ConversionSet*>(Buffer);
    }

    const ConversionSet &conversions() const {
      return *reinterpret_cast<const ConversionSet*>(Buffer);
    }

    void addConversion(NamedDecl *Found, FunctionDecl *D) {
      conversions().push_back(std::make_pair(Found, D));
    }

    using iterator = ConversionSet::iterator;

    iterator begin() { return conversions().begin(); }
    iterator end() { return conversions().end(); }

    using const_iterator = ConversionSet::const_iterator;

    const_iterator begin() const { return conversions().begin(); }
    const_iterator end() const { return conversions().end(); }

    void construct();
    void destruct();
    void copyFrom(const AmbiguousConversionSequence &);
  };

  /// BadConversionSequence - Records information about an invalid
  /// conversion sequence.
  struct BadConversionSequence {
    enum FailureKind {
      no_conversion,
      unrelated_class,
      bad_qualifiers,
      lvalue_ref_to_rvalue,
      rvalue_ref_to_lvalue
    };

    // This can be null, e.g. for implicit object arguments.
    Expr *FromExpr;

    FailureKind Kind;

  private:
    // The type we're converting from (an opaque QualType).
    void *FromTy;

    // The type we're converting to (an opaque QualType).
    void *ToTy;

  public:
    void init(FailureKind K, Expr *From, QualType To) {
      init(K, From->getType(), To);
      FromExpr = From;
    }

    void init(FailureKind K, QualType From, QualType To) {
      Kind = K;
      FromExpr = nullptr;
      setFromType(From);
      setToType(To);
    }

    QualType getFromType() const { return QualType::getFromOpaquePtr(FromTy); }
    QualType getToType() const { return QualType::getFromOpaquePtr(ToTy); }

    void setFromExpr(Expr *E) {
      FromExpr = E;
      setFromType(E->getType());
    }

    void setFromType(QualType T) { FromTy = T.getAsOpaquePtr(); }
    void setToType(QualType T) { ToTy = T.getAsOpaquePtr(); }
  };

  /// ImplicitConversionSequence - Represents an implicit conversion
  /// sequence, which may be a standard conversion sequence
  /// (C++ 13.3.3.1.1), user-defined conversion sequence (C++ 13.3.3.1.2),
  /// or an ellipsis conversion sequence (C++ 13.3.3.1.3).
  class ImplicitConversionSequence {
  public:
    /// Kind - The kind of implicit conversion sequence. BadConversion
    /// specifies that there is no conversion from the source type to
    /// the target type.  AmbiguousConversion represents the unique
    /// ambiguous conversion (C++0x [over.best.ics]p10).
    enum Kind {
      StandardConversion = 0,
      UserDefinedConversion,
      AmbiguousConversion,
      EllipsisConversion,
      BadConversion
    };

  private:
    enum {
      Uninitialized = BadConversion + 1
    };

    /// ConversionKind - The kind of implicit conversion sequence.
    unsigned ConversionKind : 30;

    /// Whether the target is really a std::initializer_list, and the
    /// sequence only represents the worst element conversion.
    unsigned StdInitializerListElement : 1;

    void setKind(Kind K) {
      destruct();
      ConversionKind = K;
    }

    void destruct() {
      if (ConversionKind == AmbiguousConversion) Ambiguous.destruct();
    }

  public:
    union {
      /// When ConversionKind == StandardConversion, provides the
      /// details of the standard conversion sequence.
      StandardConversionSequence Standard;

      /// When ConversionKind == UserDefinedConversion, provides the
      /// details of the user-defined conversion sequence.
      UserDefinedConversionSequence UserDefined;

      /// When ConversionKind == AmbiguousConversion, provides the
      /// details of the ambiguous conversion.
      AmbiguousConversionSequence Ambiguous;

      /// When ConversionKind == BadConversion, provides the details
      /// of the bad conversion.
      BadConversionSequence Bad;
    };

    ImplicitConversionSequence()
        : ConversionKind(Uninitialized), StdInitializerListElement(false) {
      Standard.setAsIdentityConversion();
    }

    ImplicitConversionSequence(const ImplicitConversionSequence &Other)
        : ConversionKind(Other.ConversionKind),
          StdInitializerListElement(Other.StdInitializerListElement) {
      switch (ConversionKind) {
      case Uninitialized: break;
      case StandardConversion: Standard = Other.Standard; break;
      case UserDefinedConversion: UserDefined = Other.UserDefined; break;
      case AmbiguousConversion: Ambiguous.copyFrom(Other.Ambiguous); break;
      case EllipsisConversion: break;
      case BadConversion: Bad = Other.Bad; break;
      }
    }

    ImplicitConversionSequence &
    operator=(const ImplicitConversionSequence &Other) {
      destruct();
      new (this) ImplicitConversionSequence(Other);
      return *this;
    }

    ~ImplicitConversionSequence() {
      destruct();
    }

    Kind getKind() const {
      assert(isInitialized() && "querying uninitialized conversion");
      return Kind(ConversionKind);
    }

    /// Return a ranking of the implicit conversion sequence
    /// kind, where smaller ranks represent better conversion
    /// sequences.
    ///
    /// In particular, this routine gives user-defined conversion
    /// sequences and ambiguous conversion sequences the same rank,
    /// per C++ [over.best.ics]p10.
    unsigned getKindRank() const {
      switch (getKind()) {
      case StandardConversion:
        return 0;

      case UserDefinedConversion:
      case AmbiguousConversion:
        return 1;

      case EllipsisConversion:
        return 2;

      case BadConversion:
        return 3;
      }

      llvm_unreachable("Invalid ImplicitConversionSequence::Kind!");
    }

    bool isBad() const { return getKind() == BadConversion; }
    bool isStandard() const { return getKind() == StandardConversion; }
    bool isEllipsis() const { return getKind() == EllipsisConversion; }
    bool isAmbiguous() const { return getKind() == AmbiguousConversion; }
    bool isUserDefined() const { return getKind() == UserDefinedConversion; }
    bool isFailure() const { return isBad() || isAmbiguous(); }

    /// Determines whether this conversion sequence has been
    /// initialized.  Most operations should never need to query
    /// uninitialized conversions and should assert as above.
    bool isInitialized() const { return ConversionKind != Uninitialized; }

    /// Sets this sequence as a bad conversion for an explicit argument.
    void setBad(BadConversionSequence::FailureKind Failure,
                Expr *FromExpr, QualType ToType) {
      setKind(BadConversion);
      Bad.init(Failure, FromExpr, ToType);
    }

    /// Sets this sequence as a bad conversion for an implicit argument.
    void setBad(BadConversionSequence::FailureKind Failure,
                QualType FromType, QualType ToType) {
      setKind(BadConversion);
      Bad.init(Failure, FromType, ToType);
    }

    void setStandard() { setKind(StandardConversion); }
    void setEllipsis() { setKind(EllipsisConversion); }
    void setUserDefined() { setKind(UserDefinedConversion); }

    void setAmbiguous() {
      if (ConversionKind == AmbiguousConversion) return;
      ConversionKind = AmbiguousConversion;
      Ambiguous.construct();
    }

    void setAsIdentityConversion(QualType T) {
      setStandard();
      Standard.setAsIdentityConversion();
      Standard.setFromType(T);
      Standard.setAllToTypes(T);
    }

    /// Whether the target is really a std::initializer_list, and the
    /// sequence only represents the worst element conversion.
    bool isStdInitializerListElement() const {
      return StdInitializerListElement;
    }

    void setStdInitializerListElement(bool V = true) {
      StdInitializerListElement = V;
    }

    // The result of a comparison between implicit conversion
    // sequences. Use Sema::CompareImplicitConversionSequences to
    // actually perform the comparison.
    enum CompareKind {
      Better = -1,
      Indistinguishable = 0,
      Worse = 1
    };

    void DiagnoseAmbiguousConversion(Sema &S,
                                     SourceLocation CaretLoc,
                                     const PartialDiagnostic &PDiag) const;

    void dump() const;
  };

  enum OverloadFailureKind {
    ovl_fail_too_many_arguments,
    ovl_fail_too_few_arguments,
    ovl_fail_bad_conversion,
    ovl_fail_bad_deduction,

    /// This conversion candidate was not considered because it
    /// duplicates the work of a trivial or derived-to-base
    /// conversion.
    ovl_fail_trivial_conversion,

    /// This conversion candidate was not considered because it is
    /// an illegal instantiation of a constructor temploid: it is
    /// callable with one argument, we only have one argument, and
    /// its first parameter type is exactly the type of the class.
    ///
    /// Defining such a constructor directly is illegal, and
    /// template-argument deduction is supposed to ignore such
    /// instantiations, but we can still get one with the right
    /// kind of implicit instantiation.
    ovl_fail_illegal_constructor,

    /// This conversion candidate is not viable because its result
    /// type is not implicitly convertible to the desired type.
    ovl_fail_bad_final_conversion,

    /// This conversion function template specialization candidate is not
    /// viable because the final conversion was not an exact match.
    ovl_fail_final_conversion_not_exact,

    /// (CUDA) This candidate was not viable because the callee
    /// was not accessible from the caller's target (i.e. host->device,
    /// global->host, device->host).
    ovl_fail_bad_target,

    /// This candidate function was not viable because an enable_if
    /// attribute disabled it.
    ovl_fail_enable_if,

    /// This candidate was not viable because its address could not be taken.
    ovl_fail_addr_not_available,

    /// This candidate was not viable because its OpenCL extension is disabled.
    ovl_fail_ext_disabled,

    /// This inherited constructor is not viable because it would slice the
    /// argument.
    ovl_fail_inhctor_slice,

    /// This candidate was not viable because it is a non-default multiversioned
    /// function.
    ovl_non_default_multiversion_function,
  };

  /// A list of implicit conversion sequences for the arguments of an
  /// OverloadCandidate.
  using ConversionSequenceList =
      llvm::MutableArrayRef<ImplicitConversionSequence>;

  /// OverloadCandidate - A single candidate in an overload set (C++ 13.3).
  struct OverloadCandidate {
    /// Function - The actual function that this candidate
    /// represents. When NULL, this is a built-in candidate
    /// (C++ [over.oper]) or a surrogate for a conversion to a
    /// function pointer or reference (C++ [over.call.object]).
    FunctionDecl *Function;

    /// FoundDecl - The original declaration that was looked up /
    /// invented / otherwise found, together with its access.
    /// Might be a UsingShadowDecl or a FunctionTemplateDecl.
    DeclAccessPair FoundDecl;

    /// BuiltinParamTypes - Provides the parameter types of a built-in overload
    /// candidate. Only valid when Function is NULL.
    QualType BuiltinParamTypes[3];

    /// Surrogate - The conversion function for which this candidate
    /// is a surrogate, but only if IsSurrogate is true.
    CXXConversionDecl *Surrogate;

    /// The conversion sequences used to convert the function arguments
    /// to the function parameters.
    ConversionSequenceList Conversions;

    /// The FixIt hints which can be used to fix the Bad candidate.
    ConversionFixItGenerator Fix;

    /// Viable - True to indicate that this overload candidate is viable.
    bool Viable : 1;

    /// IsSurrogate - True to indicate that this candidate is a
    /// surrogate for a conversion to a function pointer or reference
    /// (C++ [over.call.object]).
    bool IsSurrogate : 1;

    /// IgnoreObjectArgument - True to indicate that the first
    /// argument's conversion, which for this function represents the
    /// implicit object argument, should be ignored. This will be true
    /// when the candidate is a static member function (where the
    /// implicit object argument is just a placeholder) or a
    /// non-static member function when the call doesn't have an
    /// object argument.
    bool IgnoreObjectArgument : 1;

    /// True if the candidate was found using ADL.
    CallExpr::ADLCallKind IsADLCandidate : 1;

    /// FailureKind - The reason why this candidate is not viable.
    /// Actually an OverloadFailureKind.
    unsigned char FailureKind;

    /// The number of call arguments that were explicitly provided,
    /// to be used while performing partial ordering of function templates.
    unsigned ExplicitCallArguments;

    union {
      DeductionFailureInfo DeductionFailure;

      /// FinalConversion - For a conversion function (where Function is
      /// a CXXConversionDecl), the standard conversion that occurs
      /// after the call to the overload candidate to convert the result
      /// of calling the conversion function to the required type.
      StandardConversionSequence FinalConversion;
    };

    /// hasAmbiguousConversion - Returns whether this overload
    /// candidate requires an ambiguous conversion or not.
    bool hasAmbiguousConversion() const {
      for (auto &C : Conversions) {
        if (!C.isInitialized()) return false;
        if (C.isAmbiguous()) return true;
      }
      return false;
    }

    bool TryToFixBadConversion(unsigned Idx, Sema &S) {
      bool CanFix = Fix.tryToFixConversion(
                      Conversions[Idx].Bad.FromExpr,
                      Conversions[Idx].Bad.getFromType(),
                      Conversions[Idx].Bad.getToType(), S);

      // If at least one conversion fails, the candidate cannot be fixed.
      if (!CanFix)
        Fix.clear();

      return CanFix;
    }

    unsigned getNumParams() const {
      if (IsSurrogate) {
        auto STy = Surrogate->getConversionType();
        while (STy->isPointerType() || STy->isReferenceType())
          STy = STy->getPointeeType();
        return STy->getAs<FunctionProtoType>()->getNumParams();
      }
      if (Function)
        return Function->getNumParams();
      return ExplicitCallArguments;
    }

  private:
    friend class OverloadCandidateSet;
    OverloadCandidate() : IsADLCandidate(CallExpr::NotADL) {}
  };

  /// OverloadCandidateSet - A set of overload candidates, used in C++
  /// overload resolution (C++ 13.3).
  class OverloadCandidateSet {
  public:
    enum CandidateSetKind {
      /// Normal lookup.
      CSK_Normal,

      /// C++ [over.match.oper]:
      /// Lookup of operator function candidates in a call using operator
      /// syntax. Candidates that have no parameters of class type will be
      /// skipped unless there is a parameter of (reference to) enum type and
      /// the corresponding argument is of the same enum type.
      CSK_Operator,

      /// C++ [over.match.copy]:
      /// Copy-initialization of an object of class type by user-defined
      /// conversion.
      CSK_InitByUserDefinedConversion,

      /// C++ [over.match.ctor], [over.match.list]
      /// Initialization of an object of class type by constructor,
      /// using either a parenthesized or braced list of arguments.
      CSK_InitByConstructor,
    };

  private:
    SmallVector<OverloadCandidate, 16> Candidates;
    llvm::SmallPtrSet<Decl *, 16> Functions;

    // Allocator for ConversionSequenceLists. We store the first few of these
    // inline to avoid allocation for small sets.
    llvm::BumpPtrAllocator SlabAllocator;

    SourceLocation Loc;
    CandidateSetKind Kind;

    constexpr static unsigned NumInlineBytes =
        24 * sizeof(ImplicitConversionSequence);
    unsigned NumInlineBytesUsed = 0;
    llvm::AlignedCharArray<alignof(void *), NumInlineBytes> InlineSpace;

    /// If we have space, allocates from inline storage. Otherwise, allocates
    /// from the slab allocator.
    /// FIXME: It would probably be nice to have a SmallBumpPtrAllocator
    /// instead.
    /// FIXME: Now that this only allocates ImplicitConversionSequences, do we
    /// want to un-generalize this?
    template <typename T>
    T *slabAllocate(unsigned N) {
      // It's simpler if this doesn't need to consider alignment.
      static_assert(alignof(T) == alignof(void *),
                    "Only works for pointer-aligned types.");
      static_assert(std::is_trivial<T>::value ||
                        std::is_same<ImplicitConversionSequence, T>::value,
                    "Add destruction logic to OverloadCandidateSet::clear().");

      unsigned NBytes = sizeof(T) * N;
      if (NBytes > NumInlineBytes - NumInlineBytesUsed)
        return SlabAllocator.Allocate<T>(N);
      char *FreeSpaceStart = InlineSpace.buffer + NumInlineBytesUsed;
      assert(uintptr_t(FreeSpaceStart) % alignof(void *) == 0 &&
             "Misaligned storage!");

      NumInlineBytesUsed += NBytes;
      return reinterpret_cast<T *>(FreeSpaceStart);
    }

    void destroyCandidates();

  public:
    OverloadCandidateSet(SourceLocation Loc, CandidateSetKind CSK)
        : Loc(Loc), Kind(CSK) {}
    OverloadCandidateSet(const OverloadCandidateSet &) = delete;
    OverloadCandidateSet &operator=(const OverloadCandidateSet &) = delete;
    ~OverloadCandidateSet() { destroyCandidates(); }

    SourceLocation getLocation() const { return Loc; }
    CandidateSetKind getKind() const { return Kind; }

    /// Determine when this overload candidate will be new to the
    /// overload set.
    bool isNewCandidate(Decl *F) {
      return Functions.insert(F->getCanonicalDecl()).second;
    }

    /// Clear out all of the candidates.
    void clear(CandidateSetKind CSK);

    using iterator = SmallVectorImpl<OverloadCandidate>::iterator;

    iterator begin() { return Candidates.begin(); }
    iterator end() { return Candidates.end(); }

    size_t size() const { return Candidates.size(); }
    bool empty() const { return Candidates.empty(); }

    /// Allocate storage for conversion sequences for NumConversions
    /// conversions.
    ConversionSequenceList
    allocateConversionSequences(unsigned NumConversions) {
      ImplicitConversionSequence *Conversions =
          slabAllocate<ImplicitConversionSequence>(NumConversions);

      // Construct the new objects.
      for (unsigned I = 0; I != NumConversions; ++I)
        new (&Conversions[I]) ImplicitConversionSequence();

      return ConversionSequenceList(Conversions, NumConversions);
    }

    /// Add a new candidate with NumConversions conversion sequence slots
    /// to the overload set.
    OverloadCandidate &addCandidate(unsigned NumConversions = 0,
                                    ConversionSequenceList Conversions = None) {
      assert((Conversions.empty() || Conversions.size() == NumConversions) &&
             "preallocated conversion sequence has wrong length");

      Candidates.push_back(OverloadCandidate());
      OverloadCandidate &C = Candidates.back();
      C.Conversions = Conversions.empty()
                          ? allocateConversionSequences(NumConversions)
                          : Conversions;
      return C;
    }

    /// Find the best viable function on this overload set, if it exists.
    OverloadingResult BestViableFunction(Sema &S, SourceLocation Loc,
                                         OverloadCandidateSet::iterator& Best);

    void NoteCandidates(Sema &S,
                        OverloadCandidateDisplayKind OCD,
                        ArrayRef<Expr *> Args,
                        StringRef Opc = "",
                        SourceLocation Loc = SourceLocation(),
                        llvm::function_ref<bool(OverloadCandidate&)> Filter =
                          [](OverloadCandidate&) { return true; });
  };

  bool isBetterOverloadCandidate(Sema &S,
                                 const OverloadCandidate &Cand1,
                                 const OverloadCandidate &Cand2,
                                 SourceLocation Loc,
                                 OverloadCandidateSet::CandidateSetKind Kind);

  struct ConstructorInfo {
    DeclAccessPair FoundDecl;
    CXXConstructorDecl *Constructor;
    FunctionTemplateDecl *ConstructorTmpl;

    explicit operator bool() const { return Constructor; }
  };

  // FIXME: Add an AddOverloadCandidate / AddTemplateOverloadCandidate overload
  // that takes one of these.
  inline ConstructorInfo getConstructorInfo(NamedDecl *ND) {
    if (isa<UsingDecl>(ND))
      return ConstructorInfo{};

    // For constructors, the access check is performed against the underlying
    // declaration, not the found declaration.
    auto *D = ND->getUnderlyingDecl();
    ConstructorInfo Info = {DeclAccessPair::make(ND, D->getAccess()), nullptr,
                            nullptr};
    Info.ConstructorTmpl = dyn_cast<FunctionTemplateDecl>(D);
    if (Info.ConstructorTmpl)
      D = Info.ConstructorTmpl->getTemplatedDecl();
    Info.Constructor = dyn_cast<CXXConstructorDecl>(D);
    return Info;
  }

} // namespace clang

#endif // LLVM_CLANG_SEMA_OVERLOAD_H
