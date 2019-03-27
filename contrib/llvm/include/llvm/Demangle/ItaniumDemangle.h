//===------------------------- ItaniumDemangle.h ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEMANGLE_ITANIUMDEMANGLE_H
#define LLVM_DEMANGLE_ITANIUMDEMANGLE_H

// FIXME: (possibly) incomplete list of features that clang mangles that this
// file does not yet support:
//   - C++ modules TS

#include "llvm/Demangle/Compiler.h"
#include "llvm/Demangle/StringView.h"
#include "llvm/Demangle/Utility.h"

#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <utility>

#define FOR_EACH_NODE_KIND(X) \
    X(NodeArrayNode) \
    X(DotSuffix) \
    X(VendorExtQualType) \
    X(QualType) \
    X(ConversionOperatorType) \
    X(PostfixQualifiedType) \
    X(ElaboratedTypeSpefType) \
    X(NameType) \
    X(AbiTagAttr) \
    X(EnableIfAttr) \
    X(ObjCProtoName) \
    X(PointerType) \
    X(ReferenceType) \
    X(PointerToMemberType) \
    X(ArrayType) \
    X(FunctionType) \
    X(NoexceptSpec) \
    X(DynamicExceptionSpec) \
    X(FunctionEncoding) \
    X(LiteralOperator) \
    X(SpecialName) \
    X(CtorVtableSpecialName) \
    X(QualifiedName) \
    X(NestedName) \
    X(LocalName) \
    X(VectorType) \
    X(PixelVectorType) \
    X(ParameterPack) \
    X(TemplateArgumentPack) \
    X(ParameterPackExpansion) \
    X(TemplateArgs) \
    X(ForwardTemplateReference) \
    X(NameWithTemplateArgs) \
    X(GlobalQualifiedName) \
    X(StdQualifiedName) \
    X(ExpandedSpecialSubstitution) \
    X(SpecialSubstitution) \
    X(CtorDtorName) \
    X(DtorName) \
    X(UnnamedTypeName) \
    X(ClosureTypeName) \
    X(StructuredBindingName) \
    X(BinaryExpr) \
    X(ArraySubscriptExpr) \
    X(PostfixExpr) \
    X(ConditionalExpr) \
    X(MemberExpr) \
    X(EnclosingExpr) \
    X(CastExpr) \
    X(SizeofParamPackExpr) \
    X(CallExpr) \
    X(NewExpr) \
    X(DeleteExpr) \
    X(PrefixExpr) \
    X(FunctionParam) \
    X(ConversionExpr) \
    X(InitListExpr) \
    X(FoldExpr) \
    X(ThrowExpr) \
    X(BoolExpr) \
    X(IntegerCastExpr) \
    X(IntegerLiteral) \
    X(FloatLiteral) \
    X(DoubleLiteral) \
    X(LongDoubleLiteral) \
    X(BracedExpr) \
    X(BracedRangeExpr)

namespace llvm {
namespace itanium_demangle {
// Base class of all AST nodes. The AST is built by the parser, then is
// traversed by the printLeft/Right functions to produce a demangled string.
class Node {
public:
  enum Kind : unsigned char {
#define ENUMERATOR(NodeKind) K ## NodeKind,
    FOR_EACH_NODE_KIND(ENUMERATOR)
#undef ENUMERATOR
  };

  /// Three-way bool to track a cached value. Unknown is possible if this node
  /// has an unexpanded parameter pack below it that may affect this cache.
  enum class Cache : unsigned char { Yes, No, Unknown, };

private:
  Kind K;

  // FIXME: Make these protected.
public:
  /// Tracks if this node has a component on its right side, in which case we
  /// need to call printRight.
  Cache RHSComponentCache;

  /// Track if this node is a (possibly qualified) array type. This can affect
  /// how we format the output string.
  Cache ArrayCache;

  /// Track if this node is a (possibly qualified) function type. This can
  /// affect how we format the output string.
  Cache FunctionCache;

public:
  Node(Kind K_, Cache RHSComponentCache_ = Cache::No,
       Cache ArrayCache_ = Cache::No, Cache FunctionCache_ = Cache::No)
      : K(K_), RHSComponentCache(RHSComponentCache_), ArrayCache(ArrayCache_),
        FunctionCache(FunctionCache_) {}

  /// Visit the most-derived object corresponding to this object.
  template<typename Fn> void visit(Fn F) const;

  // The following function is provided by all derived classes:
  //
  // Call F with arguments that, when passed to the constructor of this node,
  // would construct an equivalent node.
  //template<typename Fn> void match(Fn F) const;

  bool hasRHSComponent(OutputStream &S) const {
    if (RHSComponentCache != Cache::Unknown)
      return RHSComponentCache == Cache::Yes;
    return hasRHSComponentSlow(S);
  }

  bool hasArray(OutputStream &S) const {
    if (ArrayCache != Cache::Unknown)
      return ArrayCache == Cache::Yes;
    return hasArraySlow(S);
  }

  bool hasFunction(OutputStream &S) const {
    if (FunctionCache != Cache::Unknown)
      return FunctionCache == Cache::Yes;
    return hasFunctionSlow(S);
  }

  Kind getKind() const { return K; }

  virtual bool hasRHSComponentSlow(OutputStream &) const { return false; }
  virtual bool hasArraySlow(OutputStream &) const { return false; }
  virtual bool hasFunctionSlow(OutputStream &) const { return false; }

  // Dig through "glue" nodes like ParameterPack and ForwardTemplateReference to
  // get at a node that actually represents some concrete syntax.
  virtual const Node *getSyntaxNode(OutputStream &) const {
    return this;
  }

  void print(OutputStream &S) const {
    printLeft(S);
    if (RHSComponentCache != Cache::No)
      printRight(S);
  }

  // Print the "left" side of this Node into OutputStream.
  virtual void printLeft(OutputStream &) const = 0;

  // Print the "right". This distinction is necessary to represent C++ types
  // that appear on the RHS of their subtype, such as arrays or functions.
  // Since most types don't have such a component, provide a default
  // implementation.
  virtual void printRight(OutputStream &) const {}

  virtual StringView getBaseName() const { return StringView(); }

  // Silence compiler warnings, this dtor will never be called.
  virtual ~Node() = default;

#ifndef NDEBUG
  LLVM_DUMP_METHOD void dump() const;
#endif
};

class NodeArray {
  Node **Elements;
  size_t NumElements;

public:
  NodeArray() : Elements(nullptr), NumElements(0) {}
  NodeArray(Node **Elements_, size_t NumElements_)
      : Elements(Elements_), NumElements(NumElements_) {}

  bool empty() const { return NumElements == 0; }
  size_t size() const { return NumElements; }

  Node **begin() const { return Elements; }
  Node **end() const { return Elements + NumElements; }

  Node *operator[](size_t Idx) const { return Elements[Idx]; }

  void printWithComma(OutputStream &S) const {
    bool FirstElement = true;
    for (size_t Idx = 0; Idx != NumElements; ++Idx) {
      size_t BeforeComma = S.getCurrentPosition();
      if (!FirstElement)
        S += ", ";
      size_t AfterComma = S.getCurrentPosition();
      Elements[Idx]->print(S);

      // Elements[Idx] is an empty parameter pack expansion, we should erase the
      // comma we just printed.
      if (AfterComma == S.getCurrentPosition()) {
        S.setCurrentPosition(BeforeComma);
        continue;
      }

      FirstElement = false;
    }
  }
};

struct NodeArrayNode : Node {
  NodeArray Array;
  NodeArrayNode(NodeArray Array_) : Node(KNodeArrayNode), Array(Array_) {}

  template<typename Fn> void match(Fn F) const { F(Array); }

  void printLeft(OutputStream &S) const override {
    Array.printWithComma(S);
  }
};

class DotSuffix final : public Node {
  const Node *Prefix;
  const StringView Suffix;

public:
  DotSuffix(const Node *Prefix_, StringView Suffix_)
      : Node(KDotSuffix), Prefix(Prefix_), Suffix(Suffix_) {}

  template<typename Fn> void match(Fn F) const { F(Prefix, Suffix); }

  void printLeft(OutputStream &s) const override {
    Prefix->print(s);
    s += " (";
    s += Suffix;
    s += ")";
  }
};

class VendorExtQualType final : public Node {
  const Node *Ty;
  StringView Ext;

public:
  VendorExtQualType(const Node *Ty_, StringView Ext_)
      : Node(KVendorExtQualType), Ty(Ty_), Ext(Ext_) {}

  template<typename Fn> void match(Fn F) const { F(Ty, Ext); }

  void printLeft(OutputStream &S) const override {
    Ty->print(S);
    S += " ";
    S += Ext;
  }
};

enum FunctionRefQual : unsigned char {
  FrefQualNone,
  FrefQualLValue,
  FrefQualRValue,
};

enum Qualifiers {
  QualNone = 0,
  QualConst = 0x1,
  QualVolatile = 0x2,
  QualRestrict = 0x4,
};

inline Qualifiers operator|=(Qualifiers &Q1, Qualifiers Q2) {
  return Q1 = static_cast<Qualifiers>(Q1 | Q2);
}

class QualType : public Node {
protected:
  const Qualifiers Quals;
  const Node *Child;

  void printQuals(OutputStream &S) const {
    if (Quals & QualConst)
      S += " const";
    if (Quals & QualVolatile)
      S += " volatile";
    if (Quals & QualRestrict)
      S += " restrict";
  }

public:
  QualType(const Node *Child_, Qualifiers Quals_)
      : Node(KQualType, Child_->RHSComponentCache,
             Child_->ArrayCache, Child_->FunctionCache),
        Quals(Quals_), Child(Child_) {}

  template<typename Fn> void match(Fn F) const { F(Child, Quals); }

  bool hasRHSComponentSlow(OutputStream &S) const override {
    return Child->hasRHSComponent(S);
  }
  bool hasArraySlow(OutputStream &S) const override {
    return Child->hasArray(S);
  }
  bool hasFunctionSlow(OutputStream &S) const override {
    return Child->hasFunction(S);
  }

  void printLeft(OutputStream &S) const override {
    Child->printLeft(S);
    printQuals(S);
  }

  void printRight(OutputStream &S) const override { Child->printRight(S); }
};

class ConversionOperatorType final : public Node {
  const Node *Ty;

public:
  ConversionOperatorType(const Node *Ty_)
      : Node(KConversionOperatorType), Ty(Ty_) {}

  template<typename Fn> void match(Fn F) const { F(Ty); }

  void printLeft(OutputStream &S) const override {
    S += "operator ";
    Ty->print(S);
  }
};

class PostfixQualifiedType final : public Node {
  const Node *Ty;
  const StringView Postfix;

public:
  PostfixQualifiedType(Node *Ty_, StringView Postfix_)
      : Node(KPostfixQualifiedType), Ty(Ty_), Postfix(Postfix_) {}

  template<typename Fn> void match(Fn F) const { F(Ty, Postfix); }

  void printLeft(OutputStream &s) const override {
    Ty->printLeft(s);
    s += Postfix;
  }
};

class NameType final : public Node {
  const StringView Name;

public:
  NameType(StringView Name_) : Node(KNameType), Name(Name_) {}

  template<typename Fn> void match(Fn F) const { F(Name); }

  StringView getName() const { return Name; }
  StringView getBaseName() const override { return Name; }

  void printLeft(OutputStream &s) const override { s += Name; }
};

class ElaboratedTypeSpefType : public Node {
  StringView Kind;
  Node *Child;
public:
  ElaboratedTypeSpefType(StringView Kind_, Node *Child_)
      : Node(KElaboratedTypeSpefType), Kind(Kind_), Child(Child_) {}

  template<typename Fn> void match(Fn F) const { F(Kind, Child); }

  void printLeft(OutputStream &S) const override {
    S += Kind;
    S += ' ';
    Child->print(S);
  }
};

struct AbiTagAttr : Node {
  Node *Base;
  StringView Tag;

  AbiTagAttr(Node* Base_, StringView Tag_)
      : Node(KAbiTagAttr, Base_->RHSComponentCache,
             Base_->ArrayCache, Base_->FunctionCache),
        Base(Base_), Tag(Tag_) {}

  template<typename Fn> void match(Fn F) const { F(Base, Tag); }

  void printLeft(OutputStream &S) const override {
    Base->printLeft(S);
    S += "[abi:";
    S += Tag;
    S += "]";
  }
};

class EnableIfAttr : public Node {
  NodeArray Conditions;
public:
  EnableIfAttr(NodeArray Conditions_)
      : Node(KEnableIfAttr), Conditions(Conditions_) {}

  template<typename Fn> void match(Fn F) const { F(Conditions); }

  void printLeft(OutputStream &S) const override {
    S += " [enable_if:";
    Conditions.printWithComma(S);
    S += ']';
  }
};

class ObjCProtoName : public Node {
  const Node *Ty;
  StringView Protocol;

  friend class PointerType;

public:
  ObjCProtoName(const Node *Ty_, StringView Protocol_)
      : Node(KObjCProtoName), Ty(Ty_), Protocol(Protocol_) {}

  template<typename Fn> void match(Fn F) const { F(Ty, Protocol); }

  bool isObjCObject() const {
    return Ty->getKind() == KNameType &&
           static_cast<const NameType *>(Ty)->getName() == "objc_object";
  }

  void printLeft(OutputStream &S) const override {
    Ty->print(S);
    S += "<";
    S += Protocol;
    S += ">";
  }
};

class PointerType final : public Node {
  const Node *Pointee;

public:
  PointerType(const Node *Pointee_)
      : Node(KPointerType, Pointee_->RHSComponentCache),
        Pointee(Pointee_) {}

  template<typename Fn> void match(Fn F) const { F(Pointee); }

  bool hasRHSComponentSlow(OutputStream &S) const override {
    return Pointee->hasRHSComponent(S);
  }

  void printLeft(OutputStream &s) const override {
    // We rewrite objc_object<SomeProtocol>* into id<SomeProtocol>.
    if (Pointee->getKind() != KObjCProtoName ||
        !static_cast<const ObjCProtoName *>(Pointee)->isObjCObject()) {
      Pointee->printLeft(s);
      if (Pointee->hasArray(s))
        s += " ";
      if (Pointee->hasArray(s) || Pointee->hasFunction(s))
        s += "(";
      s += "*";
    } else {
      const auto *objcProto = static_cast<const ObjCProtoName *>(Pointee);
      s += "id<";
      s += objcProto->Protocol;
      s += ">";
    }
  }

  void printRight(OutputStream &s) const override {
    if (Pointee->getKind() != KObjCProtoName ||
        !static_cast<const ObjCProtoName *>(Pointee)->isObjCObject()) {
      if (Pointee->hasArray(s) || Pointee->hasFunction(s))
        s += ")";
      Pointee->printRight(s);
    }
  }
};

enum class ReferenceKind {
  LValue,
  RValue,
};

// Represents either a LValue or an RValue reference type.
class ReferenceType : public Node {
  const Node *Pointee;
  ReferenceKind RK;

  mutable bool Printing = false;

  // Dig through any refs to refs, collapsing the ReferenceTypes as we go. The
  // rule here is rvalue ref to rvalue ref collapses to a rvalue ref, and any
  // other combination collapses to a lvalue ref.
  std::pair<ReferenceKind, const Node *> collapse(OutputStream &S) const {
    auto SoFar = std::make_pair(RK, Pointee);
    for (;;) {
      const Node *SN = SoFar.second->getSyntaxNode(S);
      if (SN->getKind() != KReferenceType)
        break;
      auto *RT = static_cast<const ReferenceType *>(SN);
      SoFar.second = RT->Pointee;
      SoFar.first = std::min(SoFar.first, RT->RK);
    }
    return SoFar;
  }

public:
  ReferenceType(const Node *Pointee_, ReferenceKind RK_)
      : Node(KReferenceType, Pointee_->RHSComponentCache),
        Pointee(Pointee_), RK(RK_) {}

  template<typename Fn> void match(Fn F) const { F(Pointee, RK); }

  bool hasRHSComponentSlow(OutputStream &S) const override {
    return Pointee->hasRHSComponent(S);
  }

  void printLeft(OutputStream &s) const override {
    if (Printing)
      return;
    SwapAndRestore<bool> SavePrinting(Printing, true);
    std::pair<ReferenceKind, const Node *> Collapsed = collapse(s);
    Collapsed.second->printLeft(s);
    if (Collapsed.second->hasArray(s))
      s += " ";
    if (Collapsed.second->hasArray(s) || Collapsed.second->hasFunction(s))
      s += "(";

    s += (Collapsed.first == ReferenceKind::LValue ? "&" : "&&");
  }
  void printRight(OutputStream &s) const override {
    if (Printing)
      return;
    SwapAndRestore<bool> SavePrinting(Printing, true);
    std::pair<ReferenceKind, const Node *> Collapsed = collapse(s);
    if (Collapsed.second->hasArray(s) || Collapsed.second->hasFunction(s))
      s += ")";
    Collapsed.second->printRight(s);
  }
};

class PointerToMemberType final : public Node {
  const Node *ClassType;
  const Node *MemberType;

public:
  PointerToMemberType(const Node *ClassType_, const Node *MemberType_)
      : Node(KPointerToMemberType, MemberType_->RHSComponentCache),
        ClassType(ClassType_), MemberType(MemberType_) {}

  template<typename Fn> void match(Fn F) const { F(ClassType, MemberType); }

  bool hasRHSComponentSlow(OutputStream &S) const override {
    return MemberType->hasRHSComponent(S);
  }

  void printLeft(OutputStream &s) const override {
    MemberType->printLeft(s);
    if (MemberType->hasArray(s) || MemberType->hasFunction(s))
      s += "(";
    else
      s += " ";
    ClassType->print(s);
    s += "::*";
  }

  void printRight(OutputStream &s) const override {
    if (MemberType->hasArray(s) || MemberType->hasFunction(s))
      s += ")";
    MemberType->printRight(s);
  }
};

class NodeOrString {
  const void *First;
  const void *Second;

public:
  /* implicit */ NodeOrString(StringView Str) {
    const char *FirstChar = Str.begin();
    const char *SecondChar = Str.end();
    if (SecondChar == nullptr) {
      assert(FirstChar == SecondChar);
      ++FirstChar, ++SecondChar;
    }
    First = static_cast<const void *>(FirstChar);
    Second = static_cast<const void *>(SecondChar);
  }

  /* implicit */ NodeOrString(Node *N)
      : First(static_cast<const void *>(N)), Second(nullptr) {}
  NodeOrString() : First(nullptr), Second(nullptr) {}

  bool isString() const { return Second && First; }
  bool isNode() const { return First && !Second; }
  bool isEmpty() const { return !First && !Second; }

  StringView asString() const {
    assert(isString());
    return StringView(static_cast<const char *>(First),
                      static_cast<const char *>(Second));
  }

  const Node *asNode() const {
    assert(isNode());
    return static_cast<const Node *>(First);
  }
};

class ArrayType final : public Node {
  const Node *Base;
  NodeOrString Dimension;

public:
  ArrayType(const Node *Base_, NodeOrString Dimension_)
      : Node(KArrayType,
             /*RHSComponentCache=*/Cache::Yes,
             /*ArrayCache=*/Cache::Yes),
        Base(Base_), Dimension(Dimension_) {}

  template<typename Fn> void match(Fn F) const { F(Base, Dimension); }

  bool hasRHSComponentSlow(OutputStream &) const override { return true; }
  bool hasArraySlow(OutputStream &) const override { return true; }

  void printLeft(OutputStream &S) const override { Base->printLeft(S); }

  void printRight(OutputStream &S) const override {
    if (S.back() != ']')
      S += " ";
    S += "[";
    if (Dimension.isString())
      S += Dimension.asString();
    else if (Dimension.isNode())
      Dimension.asNode()->print(S);
    S += "]";
    Base->printRight(S);
  }
};

class FunctionType final : public Node {
  const Node *Ret;
  NodeArray Params;
  Qualifiers CVQuals;
  FunctionRefQual RefQual;
  const Node *ExceptionSpec;

public:
  FunctionType(const Node *Ret_, NodeArray Params_, Qualifiers CVQuals_,
               FunctionRefQual RefQual_, const Node *ExceptionSpec_)
      : Node(KFunctionType,
             /*RHSComponentCache=*/Cache::Yes, /*ArrayCache=*/Cache::No,
             /*FunctionCache=*/Cache::Yes),
        Ret(Ret_), Params(Params_), CVQuals(CVQuals_), RefQual(RefQual_),
        ExceptionSpec(ExceptionSpec_) {}

  template<typename Fn> void match(Fn F) const {
    F(Ret, Params, CVQuals, RefQual, ExceptionSpec);
  }

  bool hasRHSComponentSlow(OutputStream &) const override { return true; }
  bool hasFunctionSlow(OutputStream &) const override { return true; }

  // Handle C++'s ... quirky decl grammar by using the left & right
  // distinction. Consider:
  //   int (*f(float))(char) {}
  // f is a function that takes a float and returns a pointer to a function
  // that takes a char and returns an int. If we're trying to print f, start
  // by printing out the return types's left, then print our parameters, then
  // finally print right of the return type.
  void printLeft(OutputStream &S) const override {
    Ret->printLeft(S);
    S += " ";
  }

  void printRight(OutputStream &S) const override {
    S += "(";
    Params.printWithComma(S);
    S += ")";
    Ret->printRight(S);

    if (CVQuals & QualConst)
      S += " const";
    if (CVQuals & QualVolatile)
      S += " volatile";
    if (CVQuals & QualRestrict)
      S += " restrict";

    if (RefQual == FrefQualLValue)
      S += " &";
    else if (RefQual == FrefQualRValue)
      S += " &&";

    if (ExceptionSpec != nullptr) {
      S += ' ';
      ExceptionSpec->print(S);
    }
  }
};

class NoexceptSpec : public Node {
  const Node *E;
public:
  NoexceptSpec(const Node *E_) : Node(KNoexceptSpec), E(E_) {}

  template<typename Fn> void match(Fn F) const { F(E); }

  void printLeft(OutputStream &S) const override {
    S += "noexcept(";
    E->print(S);
    S += ")";
  }
};

class DynamicExceptionSpec : public Node {
  NodeArray Types;
public:
  DynamicExceptionSpec(NodeArray Types_)
      : Node(KDynamicExceptionSpec), Types(Types_) {}

  template<typename Fn> void match(Fn F) const { F(Types); }

  void printLeft(OutputStream &S) const override {
    S += "throw(";
    Types.printWithComma(S);
    S += ')';
  }
};

class FunctionEncoding final : public Node {
  const Node *Ret;
  const Node *Name;
  NodeArray Params;
  const Node *Attrs;
  Qualifiers CVQuals;
  FunctionRefQual RefQual;

public:
  FunctionEncoding(const Node *Ret_, const Node *Name_, NodeArray Params_,
                   const Node *Attrs_, Qualifiers CVQuals_,
                   FunctionRefQual RefQual_)
      : Node(KFunctionEncoding,
             /*RHSComponentCache=*/Cache::Yes, /*ArrayCache=*/Cache::No,
             /*FunctionCache=*/Cache::Yes),
        Ret(Ret_), Name(Name_), Params(Params_), Attrs(Attrs_),
        CVQuals(CVQuals_), RefQual(RefQual_) {}

  template<typename Fn> void match(Fn F) const {
    F(Ret, Name, Params, Attrs, CVQuals, RefQual);
  }

  Qualifiers getCVQuals() const { return CVQuals; }
  FunctionRefQual getRefQual() const { return RefQual; }
  NodeArray getParams() const { return Params; }
  const Node *getReturnType() const { return Ret; }

  bool hasRHSComponentSlow(OutputStream &) const override { return true; }
  bool hasFunctionSlow(OutputStream &) const override { return true; }

  const Node *getName() const { return Name; }

  void printLeft(OutputStream &S) const override {
    if (Ret) {
      Ret->printLeft(S);
      if (!Ret->hasRHSComponent(S))
        S += " ";
    }
    Name->print(S);
  }

  void printRight(OutputStream &S) const override {
    S += "(";
    Params.printWithComma(S);
    S += ")";
    if (Ret)
      Ret->printRight(S);

    if (CVQuals & QualConst)
      S += " const";
    if (CVQuals & QualVolatile)
      S += " volatile";
    if (CVQuals & QualRestrict)
      S += " restrict";

    if (RefQual == FrefQualLValue)
      S += " &";
    else if (RefQual == FrefQualRValue)
      S += " &&";

    if (Attrs != nullptr)
      Attrs->print(S);
  }
};

class LiteralOperator : public Node {
  const Node *OpName;

public:
  LiteralOperator(const Node *OpName_)
      : Node(KLiteralOperator), OpName(OpName_) {}

  template<typename Fn> void match(Fn F) const { F(OpName); }

  void printLeft(OutputStream &S) const override {
    S += "operator\"\" ";
    OpName->print(S);
  }
};

class SpecialName final : public Node {
  const StringView Special;
  const Node *Child;

public:
  SpecialName(StringView Special_, const Node *Child_)
      : Node(KSpecialName), Special(Special_), Child(Child_) {}

  template<typename Fn> void match(Fn F) const { F(Special, Child); }

  void printLeft(OutputStream &S) const override {
    S += Special;
    Child->print(S);
  }
};

class CtorVtableSpecialName final : public Node {
  const Node *FirstType;
  const Node *SecondType;

public:
  CtorVtableSpecialName(const Node *FirstType_, const Node *SecondType_)
      : Node(KCtorVtableSpecialName),
        FirstType(FirstType_), SecondType(SecondType_) {}

  template<typename Fn> void match(Fn F) const { F(FirstType, SecondType); }

  void printLeft(OutputStream &S) const override {
    S += "construction vtable for ";
    FirstType->print(S);
    S += "-in-";
    SecondType->print(S);
  }
};

struct NestedName : Node {
  Node *Qual;
  Node *Name;

  NestedName(Node *Qual_, Node *Name_)
      : Node(KNestedName), Qual(Qual_), Name(Name_) {}

  template<typename Fn> void match(Fn F) const { F(Qual, Name); }

  StringView getBaseName() const override { return Name->getBaseName(); }

  void printLeft(OutputStream &S) const override {
    Qual->print(S);
    S += "::";
    Name->print(S);
  }
};

struct LocalName : Node {
  Node *Encoding;
  Node *Entity;

  LocalName(Node *Encoding_, Node *Entity_)
      : Node(KLocalName), Encoding(Encoding_), Entity(Entity_) {}

  template<typename Fn> void match(Fn F) const { F(Encoding, Entity); }

  void printLeft(OutputStream &S) const override {
    Encoding->print(S);
    S += "::";
    Entity->print(S);
  }
};

class QualifiedName final : public Node {
  // qualifier::name
  const Node *Qualifier;
  const Node *Name;

public:
  QualifiedName(const Node *Qualifier_, const Node *Name_)
      : Node(KQualifiedName), Qualifier(Qualifier_), Name(Name_) {}

  template<typename Fn> void match(Fn F) const { F(Qualifier, Name); }

  StringView getBaseName() const override { return Name->getBaseName(); }

  void printLeft(OutputStream &S) const override {
    Qualifier->print(S);
    S += "::";
    Name->print(S);
  }
};

class VectorType final : public Node {
  const Node *BaseType;
  const NodeOrString Dimension;

public:
  VectorType(const Node *BaseType_, NodeOrString Dimension_)
      : Node(KVectorType), BaseType(BaseType_),
        Dimension(Dimension_) {}

  template<typename Fn> void match(Fn F) const { F(BaseType, Dimension); }

  void printLeft(OutputStream &S) const override {
    BaseType->print(S);
    S += " vector[";
    if (Dimension.isNode())
      Dimension.asNode()->print(S);
    else if (Dimension.isString())
      S += Dimension.asString();
    S += "]";
  }
};

class PixelVectorType final : public Node {
  const NodeOrString Dimension;

public:
  PixelVectorType(NodeOrString Dimension_)
      : Node(KPixelVectorType), Dimension(Dimension_) {}

  template<typename Fn> void match(Fn F) const { F(Dimension); }

  void printLeft(OutputStream &S) const override {
    // FIXME: This should demangle as "vector pixel".
    S += "pixel vector[";
    S += Dimension.asString();
    S += "]";
  }
};

/// An unexpanded parameter pack (either in the expression or type context). If
/// this AST is correct, this node will have a ParameterPackExpansion node above
/// it.
///
/// This node is created when some <template-args> are found that apply to an
/// <encoding>, and is stored in the TemplateParams table. In order for this to
/// appear in the final AST, it has to referenced via a <template-param> (ie,
/// T_).
class ParameterPack final : public Node {
  NodeArray Data;

  // Setup OutputStream for a pack expansion unless we're already expanding one.
  void initializePackExpansion(OutputStream &S) const {
    if (S.CurrentPackMax == std::numeric_limits<unsigned>::max()) {
      S.CurrentPackMax = static_cast<unsigned>(Data.size());
      S.CurrentPackIndex = 0;
    }
  }

public:
  ParameterPack(NodeArray Data_) : Node(KParameterPack), Data(Data_) {
    ArrayCache = FunctionCache = RHSComponentCache = Cache::Unknown;
    if (std::all_of(Data.begin(), Data.end(), [](Node* P) {
          return P->ArrayCache == Cache::No;
        }))
      ArrayCache = Cache::No;
    if (std::all_of(Data.begin(), Data.end(), [](Node* P) {
          return P->FunctionCache == Cache::No;
        }))
      FunctionCache = Cache::No;
    if (std::all_of(Data.begin(), Data.end(), [](Node* P) {
          return P->RHSComponentCache == Cache::No;
        }))
      RHSComponentCache = Cache::No;
  }

  template<typename Fn> void match(Fn F) const { F(Data); }

  bool hasRHSComponentSlow(OutputStream &S) const override {
    initializePackExpansion(S);
    size_t Idx = S.CurrentPackIndex;
    return Idx < Data.size() && Data[Idx]->hasRHSComponent(S);
  }
  bool hasArraySlow(OutputStream &S) const override {
    initializePackExpansion(S);
    size_t Idx = S.CurrentPackIndex;
    return Idx < Data.size() && Data[Idx]->hasArray(S);
  }
  bool hasFunctionSlow(OutputStream &S) const override {
    initializePackExpansion(S);
    size_t Idx = S.CurrentPackIndex;
    return Idx < Data.size() && Data[Idx]->hasFunction(S);
  }
  const Node *getSyntaxNode(OutputStream &S) const override {
    initializePackExpansion(S);
    size_t Idx = S.CurrentPackIndex;
    return Idx < Data.size() ? Data[Idx]->getSyntaxNode(S) : this;
  }

  void printLeft(OutputStream &S) const override {
    initializePackExpansion(S);
    size_t Idx = S.CurrentPackIndex;
    if (Idx < Data.size())
      Data[Idx]->printLeft(S);
  }
  void printRight(OutputStream &S) const override {
    initializePackExpansion(S);
    size_t Idx = S.CurrentPackIndex;
    if (Idx < Data.size())
      Data[Idx]->printRight(S);
  }
};

/// A variadic template argument. This node represents an occurrence of
/// J<something>E in some <template-args>. It isn't itself unexpanded, unless
/// one of it's Elements is. The parser inserts a ParameterPack into the
/// TemplateParams table if the <template-args> this pack belongs to apply to an
/// <encoding>.
class TemplateArgumentPack final : public Node {
  NodeArray Elements;
public:
  TemplateArgumentPack(NodeArray Elements_)
      : Node(KTemplateArgumentPack), Elements(Elements_) {}

  template<typename Fn> void match(Fn F) const { F(Elements); }

  NodeArray getElements() const { return Elements; }

  void printLeft(OutputStream &S) const override {
    Elements.printWithComma(S);
  }
};

/// A pack expansion. Below this node, there are some unexpanded ParameterPacks
/// which each have Child->ParameterPackSize elements.
class ParameterPackExpansion final : public Node {
  const Node *Child;

public:
  ParameterPackExpansion(const Node *Child_)
      : Node(KParameterPackExpansion), Child(Child_) {}

  template<typename Fn> void match(Fn F) const { F(Child); }

  const Node *getChild() const { return Child; }

  void printLeft(OutputStream &S) const override {
    constexpr unsigned Max = std::numeric_limits<unsigned>::max();
    SwapAndRestore<unsigned> SavePackIdx(S.CurrentPackIndex, Max);
    SwapAndRestore<unsigned> SavePackMax(S.CurrentPackMax, Max);
    size_t StreamPos = S.getCurrentPosition();

    // Print the first element in the pack. If Child contains a ParameterPack,
    // it will set up S.CurrentPackMax and print the first element.
    Child->print(S);

    // No ParameterPack was found in Child. This can occur if we've found a pack
    // expansion on a <function-param>.
    if (S.CurrentPackMax == Max) {
      S += "...";
      return;
    }

    // We found a ParameterPack, but it has no elements. Erase whatever we may
    // of printed.
    if (S.CurrentPackMax == 0) {
      S.setCurrentPosition(StreamPos);
      return;
    }

    // Else, iterate through the rest of the elements in the pack.
    for (unsigned I = 1, E = S.CurrentPackMax; I < E; ++I) {
      S += ", ";
      S.CurrentPackIndex = I;
      Child->print(S);
    }
  }
};

class TemplateArgs final : public Node {
  NodeArray Params;

public:
  TemplateArgs(NodeArray Params_) : Node(KTemplateArgs), Params(Params_) {}

  template<typename Fn> void match(Fn F) const { F(Params); }

  NodeArray getParams() { return Params; }

  void printLeft(OutputStream &S) const override {
    S += "<";
    Params.printWithComma(S);
    if (S.back() == '>')
      S += " ";
    S += ">";
  }
};

/// A forward-reference to a template argument that was not known at the point
/// where the template parameter name was parsed in a mangling.
///
/// This is created when demangling the name of a specialization of a
/// conversion function template:
///
/// \code
/// struct A {
///   template<typename T> operator T*();
/// };
/// \endcode
///
/// When demangling a specialization of the conversion function template, we
/// encounter the name of the template (including the \c T) before we reach
/// the template argument list, so we cannot substitute the parameter name
/// for the corresponding argument while parsing. Instead, we create a
/// \c ForwardTemplateReference node that is resolved after we parse the
/// template arguments.
struct ForwardTemplateReference : Node {
  size_t Index;
  Node *Ref = nullptr;

  // If we're currently printing this node. It is possible (though invalid) for
  // a forward template reference to refer to itself via a substitution. This
  // creates a cyclic AST, which will stack overflow printing. To fix this, bail
  // out if more than one print* function is active.
  mutable bool Printing = false;

  ForwardTemplateReference(size_t Index_)
      : Node(KForwardTemplateReference, Cache::Unknown, Cache::Unknown,
             Cache::Unknown),
        Index(Index_) {}

  // We don't provide a matcher for these, because the value of the node is
  // not determined by its construction parameters, and it generally needs
  // special handling.
  template<typename Fn> void match(Fn F) const = delete;

  bool hasRHSComponentSlow(OutputStream &S) const override {
    if (Printing)
      return false;
    SwapAndRestore<bool> SavePrinting(Printing, true);
    return Ref->hasRHSComponent(S);
  }
  bool hasArraySlow(OutputStream &S) const override {
    if (Printing)
      return false;
    SwapAndRestore<bool> SavePrinting(Printing, true);
    return Ref->hasArray(S);
  }
  bool hasFunctionSlow(OutputStream &S) const override {
    if (Printing)
      return false;
    SwapAndRestore<bool> SavePrinting(Printing, true);
    return Ref->hasFunction(S);
  }
  const Node *getSyntaxNode(OutputStream &S) const override {
    if (Printing)
      return this;
    SwapAndRestore<bool> SavePrinting(Printing, true);
    return Ref->getSyntaxNode(S);
  }

  void printLeft(OutputStream &S) const override {
    if (Printing)
      return;
    SwapAndRestore<bool> SavePrinting(Printing, true);
    Ref->printLeft(S);
  }
  void printRight(OutputStream &S) const override {
    if (Printing)
      return;
    SwapAndRestore<bool> SavePrinting(Printing, true);
    Ref->printRight(S);
  }
};

struct NameWithTemplateArgs : Node {
  // name<template_args>
  Node *Name;
  Node *TemplateArgs;

  NameWithTemplateArgs(Node *Name_, Node *TemplateArgs_)
      : Node(KNameWithTemplateArgs), Name(Name_), TemplateArgs(TemplateArgs_) {}

  template<typename Fn> void match(Fn F) const { F(Name, TemplateArgs); }

  StringView getBaseName() const override { return Name->getBaseName(); }

  void printLeft(OutputStream &S) const override {
    Name->print(S);
    TemplateArgs->print(S);
  }
};

class GlobalQualifiedName final : public Node {
  Node *Child;

public:
  GlobalQualifiedName(Node* Child_)
      : Node(KGlobalQualifiedName), Child(Child_) {}

  template<typename Fn> void match(Fn F) const { F(Child); }

  StringView getBaseName() const override { return Child->getBaseName(); }

  void printLeft(OutputStream &S) const override {
    S += "::";
    Child->print(S);
  }
};

struct StdQualifiedName : Node {
  Node *Child;

  StdQualifiedName(Node *Child_) : Node(KStdQualifiedName), Child(Child_) {}

  template<typename Fn> void match(Fn F) const { F(Child); }

  StringView getBaseName() const override { return Child->getBaseName(); }

  void printLeft(OutputStream &S) const override {
    S += "std::";
    Child->print(S);
  }
};

enum class SpecialSubKind {
  allocator,
  basic_string,
  string,
  istream,
  ostream,
  iostream,
};

class ExpandedSpecialSubstitution final : public Node {
  SpecialSubKind SSK;

public:
  ExpandedSpecialSubstitution(SpecialSubKind SSK_)
      : Node(KExpandedSpecialSubstitution), SSK(SSK_) {}

  template<typename Fn> void match(Fn F) const { F(SSK); }

  StringView getBaseName() const override {
    switch (SSK) {
    case SpecialSubKind::allocator:
      return StringView("allocator");
    case SpecialSubKind::basic_string:
      return StringView("basic_string");
    case SpecialSubKind::string:
      return StringView("basic_string");
    case SpecialSubKind::istream:
      return StringView("basic_istream");
    case SpecialSubKind::ostream:
      return StringView("basic_ostream");
    case SpecialSubKind::iostream:
      return StringView("basic_iostream");
    }
    LLVM_BUILTIN_UNREACHABLE;
  }

  void printLeft(OutputStream &S) const override {
    switch (SSK) {
    case SpecialSubKind::allocator:
      S += "std::allocator";
      break;
    case SpecialSubKind::basic_string:
      S += "std::basic_string";
      break;
    case SpecialSubKind::string:
      S += "std::basic_string<char, std::char_traits<char>, "
           "std::allocator<char> >";
      break;
    case SpecialSubKind::istream:
      S += "std::basic_istream<char, std::char_traits<char> >";
      break;
    case SpecialSubKind::ostream:
      S += "std::basic_ostream<char, std::char_traits<char> >";
      break;
    case SpecialSubKind::iostream:
      S += "std::basic_iostream<char, std::char_traits<char> >";
      break;
    }
  }
};

class SpecialSubstitution final : public Node {
public:
  SpecialSubKind SSK;

  SpecialSubstitution(SpecialSubKind SSK_)
      : Node(KSpecialSubstitution), SSK(SSK_) {}

  template<typename Fn> void match(Fn F) const { F(SSK); }

  StringView getBaseName() const override {
    switch (SSK) {
    case SpecialSubKind::allocator:
      return StringView("allocator");
    case SpecialSubKind::basic_string:
      return StringView("basic_string");
    case SpecialSubKind::string:
      return StringView("string");
    case SpecialSubKind::istream:
      return StringView("istream");
    case SpecialSubKind::ostream:
      return StringView("ostream");
    case SpecialSubKind::iostream:
      return StringView("iostream");
    }
    LLVM_BUILTIN_UNREACHABLE;
  }

  void printLeft(OutputStream &S) const override {
    switch (SSK) {
    case SpecialSubKind::allocator:
      S += "std::allocator";
      break;
    case SpecialSubKind::basic_string:
      S += "std::basic_string";
      break;
    case SpecialSubKind::string:
      S += "std::string";
      break;
    case SpecialSubKind::istream:
      S += "std::istream";
      break;
    case SpecialSubKind::ostream:
      S += "std::ostream";
      break;
    case SpecialSubKind::iostream:
      S += "std::iostream";
      break;
    }
  }
};

class CtorDtorName final : public Node {
  const Node *Basename;
  const bool IsDtor;
  const int Variant;

public:
  CtorDtorName(const Node *Basename_, bool IsDtor_, int Variant_)
      : Node(KCtorDtorName), Basename(Basename_), IsDtor(IsDtor_),
        Variant(Variant_) {}

  template<typename Fn> void match(Fn F) const { F(Basename, IsDtor, Variant); }

  void printLeft(OutputStream &S) const override {
    if (IsDtor)
      S += "~";
    S += Basename->getBaseName();
  }
};

class DtorName : public Node {
  const Node *Base;

public:
  DtorName(const Node *Base_) : Node(KDtorName), Base(Base_) {}

  template<typename Fn> void match(Fn F) const { F(Base); }

  void printLeft(OutputStream &S) const override {
    S += "~";
    Base->printLeft(S);
  }
};

class UnnamedTypeName : public Node {
  const StringView Count;

public:
  UnnamedTypeName(StringView Count_) : Node(KUnnamedTypeName), Count(Count_) {}

  template<typename Fn> void match(Fn F) const { F(Count); }

  void printLeft(OutputStream &S) const override {
    S += "'unnamed";
    S += Count;
    S += "\'";
  }
};

class ClosureTypeName : public Node {
  NodeArray Params;
  StringView Count;

public:
  ClosureTypeName(NodeArray Params_, StringView Count_)
      : Node(KClosureTypeName), Params(Params_), Count(Count_) {}

  template<typename Fn> void match(Fn F) const { F(Params, Count); }

  void printLeft(OutputStream &S) const override {
    S += "\'lambda";
    S += Count;
    S += "\'(";
    Params.printWithComma(S);
    S += ")";
  }
};

class StructuredBindingName : public Node {
  NodeArray Bindings;
public:
  StructuredBindingName(NodeArray Bindings_)
      : Node(KStructuredBindingName), Bindings(Bindings_) {}

  template<typename Fn> void match(Fn F) const { F(Bindings); }

  void printLeft(OutputStream &S) const override {
    S += '[';
    Bindings.printWithComma(S);
    S += ']';
  }
};

// -- Expression Nodes --

class BinaryExpr : public Node {
  const Node *LHS;
  const StringView InfixOperator;
  const Node *RHS;

public:
  BinaryExpr(const Node *LHS_, StringView InfixOperator_, const Node *RHS_)
      : Node(KBinaryExpr), LHS(LHS_), InfixOperator(InfixOperator_), RHS(RHS_) {
  }

  template<typename Fn> void match(Fn F) const { F(LHS, InfixOperator, RHS); }

  void printLeft(OutputStream &S) const override {
    // might be a template argument expression, then we need to disambiguate
    // with parens.
    if (InfixOperator == ">")
      S += "(";

    S += "(";
    LHS->print(S);
    S += ") ";
    S += InfixOperator;
    S += " (";
    RHS->print(S);
    S += ")";

    if (InfixOperator == ">")
      S += ")";
  }
};

class ArraySubscriptExpr : public Node {
  const Node *Op1;
  const Node *Op2;

public:
  ArraySubscriptExpr(const Node *Op1_, const Node *Op2_)
      : Node(KArraySubscriptExpr), Op1(Op1_), Op2(Op2_) {}

  template<typename Fn> void match(Fn F) const { F(Op1, Op2); }

  void printLeft(OutputStream &S) const override {
    S += "(";
    Op1->print(S);
    S += ")[";
    Op2->print(S);
    S += "]";
  }
};

class PostfixExpr : public Node {
  const Node *Child;
  const StringView Operator;

public:
  PostfixExpr(const Node *Child_, StringView Operator_)
      : Node(KPostfixExpr), Child(Child_), Operator(Operator_) {}

  template<typename Fn> void match(Fn F) const { F(Child, Operator); }

  void printLeft(OutputStream &S) const override {
    S += "(";
    Child->print(S);
    S += ")";
    S += Operator;
  }
};

class ConditionalExpr : public Node {
  const Node *Cond;
  const Node *Then;
  const Node *Else;

public:
  ConditionalExpr(const Node *Cond_, const Node *Then_, const Node *Else_)
      : Node(KConditionalExpr), Cond(Cond_), Then(Then_), Else(Else_) {}

  template<typename Fn> void match(Fn F) const { F(Cond, Then, Else); }

  void printLeft(OutputStream &S) const override {
    S += "(";
    Cond->print(S);
    S += ") ? (";
    Then->print(S);
    S += ") : (";
    Else->print(S);
    S += ")";
  }
};

class MemberExpr : public Node {
  const Node *LHS;
  const StringView Kind;
  const Node *RHS;

public:
  MemberExpr(const Node *LHS_, StringView Kind_, const Node *RHS_)
      : Node(KMemberExpr), LHS(LHS_), Kind(Kind_), RHS(RHS_) {}

  template<typename Fn> void match(Fn F) const { F(LHS, Kind, RHS); }

  void printLeft(OutputStream &S) const override {
    LHS->print(S);
    S += Kind;
    RHS->print(S);
  }
};

class EnclosingExpr : public Node {
  const StringView Prefix;
  const Node *Infix;
  const StringView Postfix;

public:
  EnclosingExpr(StringView Prefix_, Node *Infix_, StringView Postfix_)
      : Node(KEnclosingExpr), Prefix(Prefix_), Infix(Infix_),
        Postfix(Postfix_) {}

  template<typename Fn> void match(Fn F) const { F(Prefix, Infix, Postfix); }

  void printLeft(OutputStream &S) const override {
    S += Prefix;
    Infix->print(S);
    S += Postfix;
  }
};

class CastExpr : public Node {
  // cast_kind<to>(from)
  const StringView CastKind;
  const Node *To;
  const Node *From;

public:
  CastExpr(StringView CastKind_, const Node *To_, const Node *From_)
      : Node(KCastExpr), CastKind(CastKind_), To(To_), From(From_) {}

  template<typename Fn> void match(Fn F) const { F(CastKind, To, From); }

  void printLeft(OutputStream &S) const override {
    S += CastKind;
    S += "<";
    To->printLeft(S);
    S += ">(";
    From->printLeft(S);
    S += ")";
  }
};

class SizeofParamPackExpr : public Node {
  const Node *Pack;

public:
  SizeofParamPackExpr(const Node *Pack_)
      : Node(KSizeofParamPackExpr), Pack(Pack_) {}

  template<typename Fn> void match(Fn F) const { F(Pack); }

  void printLeft(OutputStream &S) const override {
    S += "sizeof...(";
    ParameterPackExpansion PPE(Pack);
    PPE.printLeft(S);
    S += ")";
  }
};

class CallExpr : public Node {
  const Node *Callee;
  NodeArray Args;

public:
  CallExpr(const Node *Callee_, NodeArray Args_)
      : Node(KCallExpr), Callee(Callee_), Args(Args_) {}

  template<typename Fn> void match(Fn F) const { F(Callee, Args); }

  void printLeft(OutputStream &S) const override {
    Callee->print(S);
    S += "(";
    Args.printWithComma(S);
    S += ")";
  }
};

class NewExpr : public Node {
  // new (expr_list) type(init_list)
  NodeArray ExprList;
  Node *Type;
  NodeArray InitList;
  bool IsGlobal; // ::operator new ?
  bool IsArray;  // new[] ?
public:
  NewExpr(NodeArray ExprList_, Node *Type_, NodeArray InitList_, bool IsGlobal_,
          bool IsArray_)
      : Node(KNewExpr), ExprList(ExprList_), Type(Type_), InitList(InitList_),
        IsGlobal(IsGlobal_), IsArray(IsArray_) {}

  template<typename Fn> void match(Fn F) const {
    F(ExprList, Type, InitList, IsGlobal, IsArray);
  }

  void printLeft(OutputStream &S) const override {
    if (IsGlobal)
      S += "::operator ";
    S += "new";
    if (IsArray)
      S += "[]";
    S += ' ';
    if (!ExprList.empty()) {
      S += "(";
      ExprList.printWithComma(S);
      S += ")";
    }
    Type->print(S);
    if (!InitList.empty()) {
      S += "(";
      InitList.printWithComma(S);
      S += ")";
    }

  }
};

class DeleteExpr : public Node {
  Node *Op;
  bool IsGlobal;
  bool IsArray;

public:
  DeleteExpr(Node *Op_, bool IsGlobal_, bool IsArray_)
      : Node(KDeleteExpr), Op(Op_), IsGlobal(IsGlobal_), IsArray(IsArray_) {}

  template<typename Fn> void match(Fn F) const { F(Op, IsGlobal, IsArray); }

  void printLeft(OutputStream &S) const override {
    if (IsGlobal)
      S += "::";
    S += "delete";
    if (IsArray)
      S += "[] ";
    Op->print(S);
  }
};

class PrefixExpr : public Node {
  StringView Prefix;
  Node *Child;

public:
  PrefixExpr(StringView Prefix_, Node *Child_)
      : Node(KPrefixExpr), Prefix(Prefix_), Child(Child_) {}

  template<typename Fn> void match(Fn F) const { F(Prefix, Child); }

  void printLeft(OutputStream &S) const override {
    S += Prefix;
    S += "(";
    Child->print(S);
    S += ")";
  }
};

class FunctionParam : public Node {
  StringView Number;

public:
  FunctionParam(StringView Number_) : Node(KFunctionParam), Number(Number_) {}

  template<typename Fn> void match(Fn F) const { F(Number); }

  void printLeft(OutputStream &S) const override {
    S += "fp";
    S += Number;
  }
};

class ConversionExpr : public Node {
  const Node *Type;
  NodeArray Expressions;

public:
  ConversionExpr(const Node *Type_, NodeArray Expressions_)
      : Node(KConversionExpr), Type(Type_), Expressions(Expressions_) {}

  template<typename Fn> void match(Fn F) const { F(Type, Expressions); }

  void printLeft(OutputStream &S) const override {
    S += "(";
    Type->print(S);
    S += ")(";
    Expressions.printWithComma(S);
    S += ")";
  }
};

class InitListExpr : public Node {
  const Node *Ty;
  NodeArray Inits;
public:
  InitListExpr(const Node *Ty_, NodeArray Inits_)
      : Node(KInitListExpr), Ty(Ty_), Inits(Inits_) {}

  template<typename Fn> void match(Fn F) const { F(Ty, Inits); }

  void printLeft(OutputStream &S) const override {
    if (Ty)
      Ty->print(S);
    S += '{';
    Inits.printWithComma(S);
    S += '}';
  }
};

class BracedExpr : public Node {
  const Node *Elem;
  const Node *Init;
  bool IsArray;
public:
  BracedExpr(const Node *Elem_, const Node *Init_, bool IsArray_)
      : Node(KBracedExpr), Elem(Elem_), Init(Init_), IsArray(IsArray_) {}

  template<typename Fn> void match(Fn F) const { F(Elem, Init, IsArray); }

  void printLeft(OutputStream &S) const override {
    if (IsArray) {
      S += '[';
      Elem->print(S);
      S += ']';
    } else {
      S += '.';
      Elem->print(S);
    }
    if (Init->getKind() != KBracedExpr && Init->getKind() != KBracedRangeExpr)
      S += " = ";
    Init->print(S);
  }
};

class BracedRangeExpr : public Node {
  const Node *First;
  const Node *Last;
  const Node *Init;
public:
  BracedRangeExpr(const Node *First_, const Node *Last_, const Node *Init_)
      : Node(KBracedRangeExpr), First(First_), Last(Last_), Init(Init_) {}

  template<typename Fn> void match(Fn F) const { F(First, Last, Init); }

  void printLeft(OutputStream &S) const override {
    S += '[';
    First->print(S);
    S += " ... ";
    Last->print(S);
    S += ']';
    if (Init->getKind() != KBracedExpr && Init->getKind() != KBracedRangeExpr)
      S += " = ";
    Init->print(S);
  }
};

class FoldExpr : public Node {
  const Node *Pack, *Init;
  StringView OperatorName;
  bool IsLeftFold;

public:
  FoldExpr(bool IsLeftFold_, StringView OperatorName_, const Node *Pack_,
           const Node *Init_)
      : Node(KFoldExpr), Pack(Pack_), Init(Init_), OperatorName(OperatorName_),
        IsLeftFold(IsLeftFold_) {}

  template<typename Fn> void match(Fn F) const {
    F(IsLeftFold, OperatorName, Pack, Init);
  }

  void printLeft(OutputStream &S) const override {
    auto PrintPack = [&] {
      S += '(';
      ParameterPackExpansion(Pack).print(S);
      S += ')';
    };

    S += '(';

    if (IsLeftFold) {
      // init op ... op pack
      if (Init != nullptr) {
        Init->print(S);
        S += ' ';
        S += OperatorName;
        S += ' ';
      }
      // ... op pack
      S += "... ";
      S += OperatorName;
      S += ' ';
      PrintPack();
    } else { // !IsLeftFold
      // pack op ...
      PrintPack();
      S += ' ';
      S += OperatorName;
      S += " ...";
      // pack op ... op init
      if (Init != nullptr) {
        S += ' ';
        S += OperatorName;
        S += ' ';
        Init->print(S);
      }
    }
    S += ')';
  }
};

class ThrowExpr : public Node {
  const Node *Op;

public:
  ThrowExpr(const Node *Op_) : Node(KThrowExpr), Op(Op_) {}

  template<typename Fn> void match(Fn F) const { F(Op); }

  void printLeft(OutputStream &S) const override {
    S += "throw ";
    Op->print(S);
  }
};

class BoolExpr : public Node {
  bool Value;

public:
  BoolExpr(bool Value_) : Node(KBoolExpr), Value(Value_) {}

  template<typename Fn> void match(Fn F) const { F(Value); }

  void printLeft(OutputStream &S) const override {
    S += Value ? StringView("true") : StringView("false");
  }
};

class IntegerCastExpr : public Node {
  // ty(integer)
  const Node *Ty;
  StringView Integer;

public:
  IntegerCastExpr(const Node *Ty_, StringView Integer_)
      : Node(KIntegerCastExpr), Ty(Ty_), Integer(Integer_) {}

  template<typename Fn> void match(Fn F) const { F(Ty, Integer); }

  void printLeft(OutputStream &S) const override {
    S += "(";
    Ty->print(S);
    S += ")";
    S += Integer;
  }
};

class IntegerLiteral : public Node {
  StringView Type;
  StringView Value;

public:
  IntegerLiteral(StringView Type_, StringView Value_)
      : Node(KIntegerLiteral), Type(Type_), Value(Value_) {}

  template<typename Fn> void match(Fn F) const { F(Type, Value); }

  void printLeft(OutputStream &S) const override {
    if (Type.size() > 3) {
      S += "(";
      S += Type;
      S += ")";
    }

    if (Value[0] == 'n') {
      S += "-";
      S += Value.dropFront(1);
    } else
      S += Value;

    if (Type.size() <= 3)
      S += Type;
  }
};

template <class Float> struct FloatData;

namespace float_literal_impl {
constexpr Node::Kind getFloatLiteralKind(float *) {
  return Node::KFloatLiteral;
}
constexpr Node::Kind getFloatLiteralKind(double *) {
  return Node::KDoubleLiteral;
}
constexpr Node::Kind getFloatLiteralKind(long double *) {
  return Node::KLongDoubleLiteral;
}
}

template <class Float> class FloatLiteralImpl : public Node {
  const StringView Contents;

  static constexpr Kind KindForClass =
      float_literal_impl::getFloatLiteralKind((Float *)nullptr);

public:
  FloatLiteralImpl(StringView Contents_)
      : Node(KindForClass), Contents(Contents_) {}

  template<typename Fn> void match(Fn F) const { F(Contents); }

  void printLeft(OutputStream &s) const override {
    const char *first = Contents.begin();
    const char *last = Contents.end() + 1;

    const size_t N = FloatData<Float>::mangled_size;
    if (static_cast<std::size_t>(last - first) > N) {
      last = first + N;
      union {
        Float value;
        char buf[sizeof(Float)];
      };
      const char *t = first;
      char *e = buf;
      for (; t != last; ++t, ++e) {
        unsigned d1 = isdigit(*t) ? static_cast<unsigned>(*t - '0')
                                  : static_cast<unsigned>(*t - 'a' + 10);
        ++t;
        unsigned d0 = isdigit(*t) ? static_cast<unsigned>(*t - '0')
                                  : static_cast<unsigned>(*t - 'a' + 10);
        *e = static_cast<char>((d1 << 4) + d0);
      }
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      std::reverse(buf, e);
#endif
      char num[FloatData<Float>::max_demangled_size] = {0};
      int n = snprintf(num, sizeof(num), FloatData<Float>::spec, value);
      s += StringView(num, num + n);
    }
  }
};

using FloatLiteral = FloatLiteralImpl<float>;
using DoubleLiteral = FloatLiteralImpl<double>;
using LongDoubleLiteral = FloatLiteralImpl<long double>;

/// Visit the node. Calls \c F(P), where \c P is the node cast to the
/// appropriate derived class.
template<typename Fn>
void Node::visit(Fn F) const {
  switch (K) {
#define CASE(X) case K ## X: return F(static_cast<const X*>(this));
    FOR_EACH_NODE_KIND(CASE)
#undef CASE
  }
  assert(0 && "unknown mangling node kind");
}

/// Determine the kind of a node from its type.
template<typename NodeT> struct NodeKind;
#define SPECIALIZATION(X) \
  template<> struct NodeKind<X> { \
    static constexpr Node::Kind Kind = Node::K##X; \
    static constexpr const char *name() { return #X; } \
  };
FOR_EACH_NODE_KIND(SPECIALIZATION)
#undef SPECIALIZATION

#undef FOR_EACH_NODE_KIND

template <class T, size_t N>
class PODSmallVector {
  static_assert(std::is_pod<T>::value,
                "T is required to be a plain old data type");

  T* First;
  T* Last;
  T* Cap;
  T Inline[N];

  bool isInline() const { return First == Inline; }

  void clearInline() {
    First = Inline;
    Last = Inline;
    Cap = Inline + N;
  }

  void reserve(size_t NewCap) {
    size_t S = size();
    if (isInline()) {
      auto* Tmp = static_cast<T*>(std::malloc(NewCap * sizeof(T)));
      if (Tmp == nullptr)
        std::terminate();
      std::copy(First, Last, Tmp);
      First = Tmp;
    } else {
      First = static_cast<T*>(std::realloc(First, NewCap * sizeof(T)));
      if (First == nullptr)
        std::terminate();
    }
    Last = First + S;
    Cap = First + NewCap;
  }

public:
  PODSmallVector() : First(Inline), Last(First), Cap(Inline + N) {}

  PODSmallVector(const PODSmallVector&) = delete;
  PODSmallVector& operator=(const PODSmallVector&) = delete;

  PODSmallVector(PODSmallVector&& Other) : PODSmallVector() {
    if (Other.isInline()) {
      std::copy(Other.begin(), Other.end(), First);
      Last = First + Other.size();
      Other.clear();
      return;
    }

    First = Other.First;
    Last = Other.Last;
    Cap = Other.Cap;
    Other.clearInline();
  }

  PODSmallVector& operator=(PODSmallVector&& Other) {
    if (Other.isInline()) {
      if (!isInline()) {
        std::free(First);
        clearInline();
      }
      std::copy(Other.begin(), Other.end(), First);
      Last = First + Other.size();
      Other.clear();
      return *this;
    }

    if (isInline()) {
      First = Other.First;
      Last = Other.Last;
      Cap = Other.Cap;
      Other.clearInline();
      return *this;
    }

    std::swap(First, Other.First);
    std::swap(Last, Other.Last);
    std::swap(Cap, Other.Cap);
    Other.clear();
    return *this;
  }

  void push_back(const T& Elem) {
    if (Last == Cap)
      reserve(size() * 2);
    *Last++ = Elem;
  }

  void pop_back() {
    assert(Last != First && "Popping empty vector!");
    --Last;
  }

  void dropBack(size_t Index) {
    assert(Index <= size() && "dropBack() can't expand!");
    Last = First + Index;
  }

  T* begin() { return First; }
  T* end() { return Last; }

  bool empty() const { return First == Last; }
  size_t size() const { return static_cast<size_t>(Last - First); }
  T& back() {
    assert(Last != First && "Calling back() on empty vector!");
    return *(Last - 1);
  }
  T& operator[](size_t Index) {
    assert(Index < size() && "Invalid access!");
    return *(begin() + Index);
  }
  void clear() { Last = First; }

  ~PODSmallVector() {
    if (!isInline())
      std::free(First);
  }
};

template <typename Derived, typename Alloc> struct AbstractManglingParser {
  const char *First;
  const char *Last;

  // Name stack, this is used by the parser to hold temporary names that were
  // parsed. The parser collapses multiple names into new nodes to construct
  // the AST. Once the parser is finished, names.size() == 1.
  PODSmallVector<Node *, 32> Names;

  // Substitution table. Itanium supports name substitutions as a means of
  // compression. The string "S42_" refers to the 44nd entry (base-36) in this
  // table.
  PODSmallVector<Node *, 32> Subs;

  // Template parameter table. Like the above, but referenced like "T42_".
  // This has a smaller size compared to Subs and Names because it can be
  // stored on the stack.
  PODSmallVector<Node *, 8> TemplateParams;

  // Set of unresolved forward <template-param> references. These can occur in a
  // conversion operator's type, and are resolved in the enclosing <encoding>.
  PODSmallVector<ForwardTemplateReference *, 4> ForwardTemplateRefs;

  bool TryToParseTemplateArgs = true;
  bool PermitForwardTemplateReferences = false;
  bool ParsingLambdaParams = false;

  Alloc ASTAllocator;

  AbstractManglingParser(const char *First_, const char *Last_)
      : First(First_), Last(Last_) {}

  Derived &getDerived() { return static_cast<Derived &>(*this); }

  void reset(const char *First_, const char *Last_) {
    First = First_;
    Last = Last_;
    Names.clear();
    Subs.clear();
    TemplateParams.clear();
    ParsingLambdaParams = false;
    TryToParseTemplateArgs = true;
    PermitForwardTemplateReferences = false;
    ASTAllocator.reset();
  }

  template <class T, class... Args> Node *make(Args &&... args) {
    return ASTAllocator.template makeNode<T>(std::forward<Args>(args)...);
  }

  template <class It> NodeArray makeNodeArray(It begin, It end) {
    size_t sz = static_cast<size_t>(end - begin);
    void *mem = ASTAllocator.allocateNodeArray(sz);
    Node **data = new (mem) Node *[sz];
    std::copy(begin, end, data);
    return NodeArray(data, sz);
  }

  NodeArray popTrailingNodeArray(size_t FromPosition) {
    assert(FromPosition <= Names.size());
    NodeArray res =
        makeNodeArray(Names.begin() + (long)FromPosition, Names.end());
    Names.dropBack(FromPosition);
    return res;
  }

  bool consumeIf(StringView S) {
    if (StringView(First, Last).startsWith(S)) {
      First += S.size();
      return true;
    }
    return false;
  }

  bool consumeIf(char C) {
    if (First != Last && *First == C) {
      ++First;
      return true;
    }
    return false;
  }

  char consume() { return First != Last ? *First++ : '\0'; }

  char look(unsigned Lookahead = 0) {
    if (static_cast<size_t>(Last - First) <= Lookahead)
      return '\0';
    return First[Lookahead];
  }

  size_t numLeft() const { return static_cast<size_t>(Last - First); }

  StringView parseNumber(bool AllowNegative = false);
  Qualifiers parseCVQualifiers();
  bool parsePositiveInteger(size_t *Out);
  StringView parseBareSourceName();

  bool parseSeqId(size_t *Out);
  Node *parseSubstitution();
  Node *parseTemplateParam();
  Node *parseTemplateArgs(bool TagTemplates = false);
  Node *parseTemplateArg();

  /// Parse the <expr> production.
  Node *parseExpr();
  Node *parsePrefixExpr(StringView Kind);
  Node *parseBinaryExpr(StringView Kind);
  Node *parseIntegerLiteral(StringView Lit);
  Node *parseExprPrimary();
  template <class Float> Node *parseFloatingLiteral();
  Node *parseFunctionParam();
  Node *parseNewExpr();
  Node *parseConversionExpr();
  Node *parseBracedExpr();
  Node *parseFoldExpr();

  /// Parse the <type> production.
  Node *parseType();
  Node *parseFunctionType();
  Node *parseVectorType();
  Node *parseDecltype();
  Node *parseArrayType();
  Node *parsePointerToMemberType();
  Node *parseClassEnumType();
  Node *parseQualifiedType();

  Node *parseEncoding();
  bool parseCallOffset();
  Node *parseSpecialName();

  /// Holds some extra information about a <name> that is being parsed. This
  /// information is only pertinent if the <name> refers to an <encoding>.
  struct NameState {
    bool CtorDtorConversion = false;
    bool EndsWithTemplateArgs = false;
    Qualifiers CVQualifiers = QualNone;
    FunctionRefQual ReferenceQualifier = FrefQualNone;
    size_t ForwardTemplateRefsBegin;

    NameState(AbstractManglingParser *Enclosing)
        : ForwardTemplateRefsBegin(Enclosing->ForwardTemplateRefs.size()) {}
  };

  bool resolveForwardTemplateRefs(NameState &State) {
    size_t I = State.ForwardTemplateRefsBegin;
    size_t E = ForwardTemplateRefs.size();
    for (; I < E; ++I) {
      size_t Idx = ForwardTemplateRefs[I]->Index;
      if (Idx >= TemplateParams.size())
        return true;
      ForwardTemplateRefs[I]->Ref = TemplateParams[Idx];
    }
    ForwardTemplateRefs.dropBack(State.ForwardTemplateRefsBegin);
    return false;
  }

  /// Parse the <name> production>
  Node *parseName(NameState *State = nullptr);
  Node *parseLocalName(NameState *State);
  Node *parseOperatorName(NameState *State);
  Node *parseUnqualifiedName(NameState *State);
  Node *parseUnnamedTypeName(NameState *State);
  Node *parseSourceName(NameState *State);
  Node *parseUnscopedName(NameState *State);
  Node *parseNestedName(NameState *State);
  Node *parseCtorDtorName(Node *&SoFar, NameState *State);

  Node *parseAbiTags(Node *N);

  /// Parse the <unresolved-name> production.
  Node *parseUnresolvedName();
  Node *parseSimpleId();
  Node *parseBaseUnresolvedName();
  Node *parseUnresolvedType();
  Node *parseDestructorName();

  /// Top-level entry point into the parser.
  Node *parse();
};

const char* parse_discriminator(const char* first, const char* last);

// <name> ::= <nested-name> // N
//        ::= <local-name> # See Scope Encoding below  // Z
//        ::= <unscoped-template-name> <template-args>
//        ::= <unscoped-name>
//
// <unscoped-template-name> ::= <unscoped-name>
//                          ::= <substitution>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseName(NameState *State) {
  consumeIf('L'); // extension

  if (look() == 'N')
    return getDerived().parseNestedName(State);
  if (look() == 'Z')
    return getDerived().parseLocalName(State);

  //        ::= <unscoped-template-name> <template-args>
  if (look() == 'S' && look(1) != 't') {
    Node *S = getDerived().parseSubstitution();
    if (S == nullptr)
      return nullptr;
    if (look() != 'I')
      return nullptr;
    Node *TA = getDerived().parseTemplateArgs(State != nullptr);
    if (TA == nullptr)
      return nullptr;
    if (State) State->EndsWithTemplateArgs = true;
    return make<NameWithTemplateArgs>(S, TA);
  }

  Node *N = getDerived().parseUnscopedName(State);
  if (N == nullptr)
    return nullptr;
  //        ::= <unscoped-template-name> <template-args>
  if (look() == 'I') {
    Subs.push_back(N);
    Node *TA = getDerived().parseTemplateArgs(State != nullptr);
    if (TA == nullptr)
      return nullptr;
    if (State) State->EndsWithTemplateArgs = true;
    return make<NameWithTemplateArgs>(N, TA);
  }
  //        ::= <unscoped-name>
  return N;
}

// <local-name> := Z <function encoding> E <entity name> [<discriminator>]
//              := Z <function encoding> E s [<discriminator>]
//              := Z <function encoding> Ed [ <parameter number> ] _ <entity name>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseLocalName(NameState *State) {
  if (!consumeIf('Z'))
    return nullptr;
  Node *Encoding = getDerived().parseEncoding();
  if (Encoding == nullptr || !consumeIf('E'))
    return nullptr;

  if (consumeIf('s')) {
    First = parse_discriminator(First, Last);
    auto *StringLitName = make<NameType>("string literal");
    if (!StringLitName)
      return nullptr;
    return make<LocalName>(Encoding, StringLitName);
  }

  if (consumeIf('d')) {
    parseNumber(true);
    if (!consumeIf('_'))
      return nullptr;
    Node *N = getDerived().parseName(State);
    if (N == nullptr)
      return nullptr;
    return make<LocalName>(Encoding, N);
  }

  Node *Entity = getDerived().parseName(State);
  if (Entity == nullptr)
    return nullptr;
  First = parse_discriminator(First, Last);
  return make<LocalName>(Encoding, Entity);
}

// <unscoped-name> ::= <unqualified-name>
//                 ::= St <unqualified-name>   # ::std::
// extension       ::= StL<unqualified-name>
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseUnscopedName(NameState *State) {
  if (consumeIf("StL") || consumeIf("St")) {
    Node *R = getDerived().parseUnqualifiedName(State);
    if (R == nullptr)
      return nullptr;
    return make<StdQualifiedName>(R);
  }
  return getDerived().parseUnqualifiedName(State);
}

// <unqualified-name> ::= <operator-name> [abi-tags]
//                    ::= <ctor-dtor-name>
//                    ::= <source-name>
//                    ::= <unnamed-type-name>
//                    ::= DC <source-name>+ E      # structured binding declaration
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseUnqualifiedName(NameState *State) {
  // <ctor-dtor-name>s are special-cased in parseNestedName().
  Node *Result;
  if (look() == 'U')
    Result = getDerived().parseUnnamedTypeName(State);
  else if (look() >= '1' && look() <= '9')
    Result = getDerived().parseSourceName(State);
  else if (consumeIf("DC")) {
    size_t BindingsBegin = Names.size();
    do {
      Node *Binding = getDerived().parseSourceName(State);
      if (Binding == nullptr)
        return nullptr;
      Names.push_back(Binding);
    } while (!consumeIf('E'));
    Result = make<StructuredBindingName>(popTrailingNodeArray(BindingsBegin));
  } else
    Result = getDerived().parseOperatorName(State);
  if (Result != nullptr)
    Result = getDerived().parseAbiTags(Result);
  return Result;
}

// <unnamed-type-name> ::= Ut [<nonnegative number>] _
//                     ::= <closure-type-name>
//
// <closure-type-name> ::= Ul <lambda-sig> E [ <nonnegative number> ] _
//
// <lambda-sig> ::= <parameter type>+  # Parameter types or "v" if the lambda has no parameters
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseUnnamedTypeName(NameState *) {
  if (consumeIf("Ut")) {
    StringView Count = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<UnnamedTypeName>(Count);
  }
  if (consumeIf("Ul")) {
    NodeArray Params;
    SwapAndRestore<bool> SwapParams(ParsingLambdaParams, true);
    if (!consumeIf("vE")) {
      size_t ParamsBegin = Names.size();
      do {
        Node *P = getDerived().parseType();
        if (P == nullptr)
          return nullptr;
        Names.push_back(P);
      } while (!consumeIf('E'));
      Params = popTrailingNodeArray(ParamsBegin);
    }
    StringView Count = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<ClosureTypeName>(Params, Count);
  }
  return nullptr;
}

// <source-name> ::= <positive length number> <identifier>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSourceName(NameState *) {
  size_t Length = 0;
  if (parsePositiveInteger(&Length))
    return nullptr;
  if (numLeft() < Length || Length == 0)
    return nullptr;
  StringView Name(First, First + Length);
  First += Length;
  if (Name.startsWith("_GLOBAL__N"))
    return make<NameType>("(anonymous namespace)");
  return make<NameType>(Name);
}

//   <operator-name> ::= aa    # &&
//                   ::= ad    # & (unary)
//                   ::= an    # &
//                   ::= aN    # &=
//                   ::= aS    # =
//                   ::= cl    # ()
//                   ::= cm    # ,
//                   ::= co    # ~
//                   ::= cv <type>    # (cast)
//                   ::= da    # delete[]
//                   ::= de    # * (unary)
//                   ::= dl    # delete
//                   ::= dv    # /
//                   ::= dV    # /=
//                   ::= eo    # ^
//                   ::= eO    # ^=
//                   ::= eq    # ==
//                   ::= ge    # >=
//                   ::= gt    # >
//                   ::= ix    # []
//                   ::= le    # <=
//                   ::= li <source-name>  # operator ""
//                   ::= ls    # <<
//                   ::= lS    # <<=
//                   ::= lt    # <
//                   ::= mi    # -
//                   ::= mI    # -=
//                   ::= ml    # *
//                   ::= mL    # *=
//                   ::= mm    # -- (postfix in <expression> context)
//                   ::= na    # new[]
//                   ::= ne    # !=
//                   ::= ng    # - (unary)
//                   ::= nt    # !
//                   ::= nw    # new
//                   ::= oo    # ||
//                   ::= or    # |
//                   ::= oR    # |=
//                   ::= pm    # ->*
//                   ::= pl    # +
//                   ::= pL    # +=
//                   ::= pp    # ++ (postfix in <expression> context)
//                   ::= ps    # + (unary)
//                   ::= pt    # ->
//                   ::= qu    # ?
//                   ::= rm    # %
//                   ::= rM    # %=
//                   ::= rs    # >>
//                   ::= rS    # >>=
//                   ::= ss    # <=> C++2a
//                   ::= v <digit> <source-name>        # vendor extended operator
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseOperatorName(NameState *State) {
  switch (look()) {
  case 'a':
    switch (look(1)) {
    case 'a':
      First += 2;
      return make<NameType>("operator&&");
    case 'd':
    case 'n':
      First += 2;
      return make<NameType>("operator&");
    case 'N':
      First += 2;
      return make<NameType>("operator&=");
    case 'S':
      First += 2;
      return make<NameType>("operator=");
    }
    return nullptr;
  case 'c':
    switch (look(1)) {
    case 'l':
      First += 2;
      return make<NameType>("operator()");
    case 'm':
      First += 2;
      return make<NameType>("operator,");
    case 'o':
      First += 2;
      return make<NameType>("operator~");
    //                   ::= cv <type>    # (cast)
    case 'v': {
      First += 2;
      SwapAndRestore<bool> SaveTemplate(TryToParseTemplateArgs, false);
      // If we're parsing an encoding, State != nullptr and the conversion
      // operators' <type> could have a <template-param> that refers to some
      // <template-arg>s further ahead in the mangled name.
      SwapAndRestore<bool> SavePermit(PermitForwardTemplateReferences,
                                      PermitForwardTemplateReferences ||
                                          State != nullptr);
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      if (State) State->CtorDtorConversion = true;
      return make<ConversionOperatorType>(Ty);
    }
    }
    return nullptr;
  case 'd':
    switch (look(1)) {
    case 'a':
      First += 2;
      return make<NameType>("operator delete[]");
    case 'e':
      First += 2;
      return make<NameType>("operator*");
    case 'l':
      First += 2;
      return make<NameType>("operator delete");
    case 'v':
      First += 2;
      return make<NameType>("operator/");
    case 'V':
      First += 2;
      return make<NameType>("operator/=");
    }
    return nullptr;
  case 'e':
    switch (look(1)) {
    case 'o':
      First += 2;
      return make<NameType>("operator^");
    case 'O':
      First += 2;
      return make<NameType>("operator^=");
    case 'q':
      First += 2;
      return make<NameType>("operator==");
    }
    return nullptr;
  case 'g':
    switch (look(1)) {
    case 'e':
      First += 2;
      return make<NameType>("operator>=");
    case 't':
      First += 2;
      return make<NameType>("operator>");
    }
    return nullptr;
  case 'i':
    if (look(1) == 'x') {
      First += 2;
      return make<NameType>("operator[]");
    }
    return nullptr;
  case 'l':
    switch (look(1)) {
    case 'e':
      First += 2;
      return make<NameType>("operator<=");
    //                   ::= li <source-name>  # operator ""
    case 'i': {
      First += 2;
      Node *SN = getDerived().parseSourceName(State);
      if (SN == nullptr)
        return nullptr;
      return make<LiteralOperator>(SN);
    }
    case 's':
      First += 2;
      return make<NameType>("operator<<");
    case 'S':
      First += 2;
      return make<NameType>("operator<<=");
    case 't':
      First += 2;
      return make<NameType>("operator<");
    }
    return nullptr;
  case 'm':
    switch (look(1)) {
    case 'i':
      First += 2;
      return make<NameType>("operator-");
    case 'I':
      First += 2;
      return make<NameType>("operator-=");
    case 'l':
      First += 2;
      return make<NameType>("operator*");
    case 'L':
      First += 2;
      return make<NameType>("operator*=");
    case 'm':
      First += 2;
      return make<NameType>("operator--");
    }
    return nullptr;
  case 'n':
    switch (look(1)) {
    case 'a':
      First += 2;
      return make<NameType>("operator new[]");
    case 'e':
      First += 2;
      return make<NameType>("operator!=");
    case 'g':
      First += 2;
      return make<NameType>("operator-");
    case 't':
      First += 2;
      return make<NameType>("operator!");
    case 'w':
      First += 2;
      return make<NameType>("operator new");
    }
    return nullptr;
  case 'o':
    switch (look(1)) {
    case 'o':
      First += 2;
      return make<NameType>("operator||");
    case 'r':
      First += 2;
      return make<NameType>("operator|");
    case 'R':
      First += 2;
      return make<NameType>("operator|=");
    }
    return nullptr;
  case 'p':
    switch (look(1)) {
    case 'm':
      First += 2;
      return make<NameType>("operator->*");
    case 'l':
      First += 2;
      return make<NameType>("operator+");
    case 'L':
      First += 2;
      return make<NameType>("operator+=");
    case 'p':
      First += 2;
      return make<NameType>("operator++");
    case 's':
      First += 2;
      return make<NameType>("operator+");
    case 't':
      First += 2;
      return make<NameType>("operator->");
    }
    return nullptr;
  case 'q':
    if (look(1) == 'u') {
      First += 2;
      return make<NameType>("operator?");
    }
    return nullptr;
  case 'r':
    switch (look(1)) {
    case 'm':
      First += 2;
      return make<NameType>("operator%");
    case 'M':
      First += 2;
      return make<NameType>("operator%=");
    case 's':
      First += 2;
      return make<NameType>("operator>>");
    case 'S':
      First += 2;
      return make<NameType>("operator>>=");
    }
    return nullptr;
  case 's':
    if (look(1) == 's') {
      First += 2;
      return make<NameType>("operator<=>");
    }
    return nullptr;
  // ::= v <digit> <source-name>        # vendor extended operator
  case 'v':
    if (std::isdigit(look(1))) {
      First += 2;
      Node *SN = getDerived().parseSourceName(State);
      if (SN == nullptr)
        return nullptr;
      return make<ConversionOperatorType>(SN);
    }
    return nullptr;
  }
  return nullptr;
}

// <ctor-dtor-name> ::= C1  # complete object constructor
//                  ::= C2  # base object constructor
//                  ::= C3  # complete object allocating constructor
//   extension      ::= C5    # ?
//                  ::= D0  # deleting destructor
//                  ::= D1  # complete object destructor
//                  ::= D2  # base object destructor
//   extension      ::= D5    # ?
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseCtorDtorName(Node *&SoFar,
                                                          NameState *State) {
  if (SoFar->getKind() == Node::KSpecialSubstitution) {
    auto SSK = static_cast<SpecialSubstitution *>(SoFar)->SSK;
    switch (SSK) {
    case SpecialSubKind::string:
    case SpecialSubKind::istream:
    case SpecialSubKind::ostream:
    case SpecialSubKind::iostream:
      SoFar = make<ExpandedSpecialSubstitution>(SSK);
      if (!SoFar)
        return nullptr;
      break;
    default:
      break;
    }
  }

  if (consumeIf('C')) {
    bool IsInherited = consumeIf('I');
    if (look() != '1' && look() != '2' && look() != '3' && look() != '5')
      return nullptr;
    int Variant = look() - '0';
    ++First;
    if (State) State->CtorDtorConversion = true;
    if (IsInherited) {
      if (getDerived().parseName(State) == nullptr)
        return nullptr;
    }
    return make<CtorDtorName>(SoFar, false, Variant);
  }

  if (look() == 'D' &&
      (look(1) == '0' || look(1) == '1' || look(1) == '2' || look(1) == '5')) {
    int Variant = look(1) - '0';
    First += 2;
    if (State) State->CtorDtorConversion = true;
    return make<CtorDtorName>(SoFar, true, Variant);
  }

  return nullptr;
}

// <nested-name> ::= N [<CV-Qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
//               ::= N [<CV-Qualifiers>] [<ref-qualifier>] <template-prefix> <template-args> E
//
// <prefix> ::= <prefix> <unqualified-name>
//          ::= <template-prefix> <template-args>
//          ::= <template-param>
//          ::= <decltype>
//          ::= # empty
//          ::= <substitution>
//          ::= <prefix> <data-member-prefix>
//  extension ::= L
//
// <data-member-prefix> := <member source-name> [<template-args>] M
//
// <template-prefix> ::= <prefix> <template unqualified-name>
//                   ::= <template-param>
//                   ::= <substitution>
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseNestedName(NameState *State) {
  if (!consumeIf('N'))
    return nullptr;

  Qualifiers CVTmp = parseCVQualifiers();
  if (State) State->CVQualifiers = CVTmp;

  if (consumeIf('O')) {
    if (State) State->ReferenceQualifier = FrefQualRValue;
  } else if (consumeIf('R')) {
    if (State) State->ReferenceQualifier = FrefQualLValue;
  } else
    if (State) State->ReferenceQualifier = FrefQualNone;

  Node *SoFar = nullptr;
  auto PushComponent = [&](Node *Comp) {
    if (!Comp) return false;
    if (SoFar) SoFar = make<NestedName>(SoFar, Comp);
    else       SoFar = Comp;
    if (State) State->EndsWithTemplateArgs = false;
    return SoFar != nullptr;
  };

  if (consumeIf("St")) {
    SoFar = make<NameType>("std");
    if (!SoFar)
      return nullptr;
  }

  while (!consumeIf('E')) {
    consumeIf('L'); // extension

    // <data-member-prefix> := <member source-name> [<template-args>] M
    if (consumeIf('M')) {
      if (SoFar == nullptr)
        return nullptr;
      continue;
    }

    //          ::= <template-param>
    if (look() == 'T') {
      if (!PushComponent(getDerived().parseTemplateParam()))
        return nullptr;
      Subs.push_back(SoFar);
      continue;
    }

    //          ::= <template-prefix> <template-args>
    if (look() == 'I') {
      Node *TA = getDerived().parseTemplateArgs(State != nullptr);
      if (TA == nullptr || SoFar == nullptr)
        return nullptr;
      SoFar = make<NameWithTemplateArgs>(SoFar, TA);
      if (!SoFar)
        return nullptr;
      if (State) State->EndsWithTemplateArgs = true;
      Subs.push_back(SoFar);
      continue;
    }

    //          ::= <decltype>
    if (look() == 'D' && (look(1) == 't' || look(1) == 'T')) {
      if (!PushComponent(getDerived().parseDecltype()))
        return nullptr;
      Subs.push_back(SoFar);
      continue;
    }

    //          ::= <substitution>
    if (look() == 'S' && look(1) != 't') {
      Node *S = getDerived().parseSubstitution();
      if (!PushComponent(S))
        return nullptr;
      if (SoFar != S)
        Subs.push_back(S);
      continue;
    }

    // Parse an <unqualified-name> thats actually a <ctor-dtor-name>.
    if (look() == 'C' || (look() == 'D' && look(1) != 'C')) {
      if (SoFar == nullptr)
        return nullptr;
      if (!PushComponent(getDerived().parseCtorDtorName(SoFar, State)))
        return nullptr;
      SoFar = getDerived().parseAbiTags(SoFar);
      if (SoFar == nullptr)
        return nullptr;
      Subs.push_back(SoFar);
      continue;
    }

    //          ::= <prefix> <unqualified-name>
    if (!PushComponent(getDerived().parseUnqualifiedName(State)))
      return nullptr;
    Subs.push_back(SoFar);
  }

  if (SoFar == nullptr || Subs.empty())
    return nullptr;

  Subs.pop_back();
  return SoFar;
}

// <simple-id> ::= <source-name> [ <template-args> ]
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSimpleId() {
  Node *SN = getDerived().parseSourceName(/*NameState=*/nullptr);
  if (SN == nullptr)
    return nullptr;
  if (look() == 'I') {
    Node *TA = getDerived().parseTemplateArgs();
    if (TA == nullptr)
      return nullptr;
    return make<NameWithTemplateArgs>(SN, TA);
  }
  return SN;
}

// <destructor-name> ::= <unresolved-type>  # e.g., ~T or ~decltype(f())
//                   ::= <simple-id>        # e.g., ~A<2*N>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseDestructorName() {
  Node *Result;
  if (std::isdigit(look()))
    Result = getDerived().parseSimpleId();
  else
    Result = getDerived().parseUnresolvedType();
  if (Result == nullptr)
    return nullptr;
  return make<DtorName>(Result);
}

// <unresolved-type> ::= <template-param>
//                   ::= <decltype>
//                   ::= <substitution>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseUnresolvedType() {
  if (look() == 'T') {
    Node *TP = getDerived().parseTemplateParam();
    if (TP == nullptr)
      return nullptr;
    Subs.push_back(TP);
    return TP;
  }
  if (look() == 'D') {
    Node *DT = getDerived().parseDecltype();
    if (DT == nullptr)
      return nullptr;
    Subs.push_back(DT);
    return DT;
  }
  return getDerived().parseSubstitution();
}

// <base-unresolved-name> ::= <simple-id>                                # unresolved name
//          extension     ::= <operator-name>                            # unresolved operator-function-id
//          extension     ::= <operator-name> <template-args>            # unresolved operator template-id
//                        ::= on <operator-name>                         # unresolved operator-function-id
//                        ::= on <operator-name> <template-args>         # unresolved operator template-id
//                        ::= dn <destructor-name>                       # destructor or pseudo-destructor;
//                                                                         # e.g. ~X or ~X<N-1>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseBaseUnresolvedName() {
  if (std::isdigit(look()))
    return getDerived().parseSimpleId();

  if (consumeIf("dn"))
    return getDerived().parseDestructorName();

  consumeIf("on");

  Node *Oper = getDerived().parseOperatorName(/*NameState=*/nullptr);
  if (Oper == nullptr)
    return nullptr;
  if (look() == 'I') {
    Node *TA = getDerived().parseTemplateArgs();
    if (TA == nullptr)
      return nullptr;
    return make<NameWithTemplateArgs>(Oper, TA);
  }
  return Oper;
}

// <unresolved-name>
//  extension        ::= srN <unresolved-type> [<template-args>] <unresolved-qualifier-level>* E <base-unresolved-name>
//                   ::= [gs] <base-unresolved-name>                     # x or (with "gs") ::x
//                   ::= [gs] sr <unresolved-qualifier-level>+ E <base-unresolved-name>
//                                                                       # A::x, N::y, A<T>::z; "gs" means leading "::"
//                   ::= sr <unresolved-type> <base-unresolved-name>     # T::x / decltype(p)::x
//  extension        ::= sr <unresolved-type> <template-args> <base-unresolved-name>
//                                                                       # T::N::x /decltype(p)::N::x
//  (ignored)        ::= srN <unresolved-type>  <unresolved-qualifier-level>+ E <base-unresolved-name>
//
// <unresolved-qualifier-level> ::= <simple-id>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseUnresolvedName() {
  Node *SoFar = nullptr;

  // srN <unresolved-type> [<template-args>] <unresolved-qualifier-level>* E <base-unresolved-name>
  // srN <unresolved-type>                   <unresolved-qualifier-level>+ E <base-unresolved-name>
  if (consumeIf("srN")) {
    SoFar = getDerived().parseUnresolvedType();
    if (SoFar == nullptr)
      return nullptr;

    if (look() == 'I') {
      Node *TA = getDerived().parseTemplateArgs();
      if (TA == nullptr)
        return nullptr;
      SoFar = make<NameWithTemplateArgs>(SoFar, TA);
      if (!SoFar)
        return nullptr;
    }

    while (!consumeIf('E')) {
      Node *Qual = getDerived().parseSimpleId();
      if (Qual == nullptr)
        return nullptr;
      SoFar = make<QualifiedName>(SoFar, Qual);
      if (!SoFar)
        return nullptr;
    }

    Node *Base = getDerived().parseBaseUnresolvedName();
    if (Base == nullptr)
      return nullptr;
    return make<QualifiedName>(SoFar, Base);
  }

  bool Global = consumeIf("gs");

  // [gs] <base-unresolved-name>                     # x or (with "gs") ::x
  if (!consumeIf("sr")) {
    SoFar = getDerived().parseBaseUnresolvedName();
    if (SoFar == nullptr)
      return nullptr;
    if (Global)
      SoFar = make<GlobalQualifiedName>(SoFar);
    return SoFar;
  }

  // [gs] sr <unresolved-qualifier-level>+ E   <base-unresolved-name>
  if (std::isdigit(look())) {
    do {
      Node *Qual = getDerived().parseSimpleId();
      if (Qual == nullptr)
        return nullptr;
      if (SoFar)
        SoFar = make<QualifiedName>(SoFar, Qual);
      else if (Global)
        SoFar = make<GlobalQualifiedName>(Qual);
      else
        SoFar = Qual;
      if (!SoFar)
        return nullptr;
    } while (!consumeIf('E'));
  }
  //      sr <unresolved-type>                 <base-unresolved-name>
  //      sr <unresolved-type> <template-args> <base-unresolved-name>
  else {
    SoFar = getDerived().parseUnresolvedType();
    if (SoFar == nullptr)
      return nullptr;

    if (look() == 'I') {
      Node *TA = getDerived().parseTemplateArgs();
      if (TA == nullptr)
        return nullptr;
      SoFar = make<NameWithTemplateArgs>(SoFar, TA);
      if (!SoFar)
        return nullptr;
    }
  }

  assert(SoFar != nullptr);

  Node *Base = getDerived().parseBaseUnresolvedName();
  if (Base == nullptr)
    return nullptr;
  return make<QualifiedName>(SoFar, Base);
}

// <abi-tags> ::= <abi-tag> [<abi-tags>]
// <abi-tag> ::= B <source-name>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseAbiTags(Node *N) {
  while (consumeIf('B')) {
    StringView SN = parseBareSourceName();
    if (SN.empty())
      return nullptr;
    N = make<AbiTagAttr>(N, SN);
    if (!N)
      return nullptr;
  }
  return N;
}

// <number> ::= [n] <non-negative decimal integer>
template <typename Alloc, typename Derived>
StringView
AbstractManglingParser<Alloc, Derived>::parseNumber(bool AllowNegative) {
  const char *Tmp = First;
  if (AllowNegative)
    consumeIf('n');
  if (numLeft() == 0 || !std::isdigit(*First))
    return StringView();
  while (numLeft() != 0 && std::isdigit(*First))
    ++First;
  return StringView(Tmp, First);
}

// <positive length number> ::= [0-9]*
template <typename Alloc, typename Derived>
bool AbstractManglingParser<Alloc, Derived>::parsePositiveInteger(size_t *Out) {
  *Out = 0;
  if (look() < '0' || look() > '9')
    return true;
  while (look() >= '0' && look() <= '9') {
    *Out *= 10;
    *Out += static_cast<size_t>(consume() - '0');
  }
  return false;
}

template <typename Alloc, typename Derived>
StringView AbstractManglingParser<Alloc, Derived>::parseBareSourceName() {
  size_t Int = 0;
  if (parsePositiveInteger(&Int) || numLeft() < Int)
    return StringView();
  StringView R(First, First + Int);
  First += Int;
  return R;
}

// <function-type> ::= [<CV-qualifiers>] [<exception-spec>] [Dx] F [Y] <bare-function-type> [<ref-qualifier>] E
//
// <exception-spec> ::= Do                # non-throwing exception-specification (e.g., noexcept, throw())
//                  ::= DO <expression> E # computed (instantiation-dependent) noexcept
//                  ::= Dw <type>+ E      # dynamic exception specification with instantiation-dependent types
//
// <ref-qualifier> ::= R                   # & ref-qualifier
// <ref-qualifier> ::= O                   # && ref-qualifier
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseFunctionType() {
  Qualifiers CVQuals = parseCVQualifiers();

  Node *ExceptionSpec = nullptr;
  if (consumeIf("Do")) {
    ExceptionSpec = make<NameType>("noexcept");
    if (!ExceptionSpec)
      return nullptr;
  } else if (consumeIf("DO")) {
    Node *E = getDerived().parseExpr();
    if (E == nullptr || !consumeIf('E'))
      return nullptr;
    ExceptionSpec = make<NoexceptSpec>(E);
    if (!ExceptionSpec)
      return nullptr;
  } else if (consumeIf("Dw")) {
    size_t SpecsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *T = getDerived().parseType();
      if (T == nullptr)
        return nullptr;
      Names.push_back(T);
    }
    ExceptionSpec =
      make<DynamicExceptionSpec>(popTrailingNodeArray(SpecsBegin));
    if (!ExceptionSpec)
      return nullptr;
  }

  consumeIf("Dx"); // transaction safe

  if (!consumeIf('F'))
    return nullptr;
  consumeIf('Y'); // extern "C"
  Node *ReturnType = getDerived().parseType();
  if (ReturnType == nullptr)
    return nullptr;

  FunctionRefQual ReferenceQualifier = FrefQualNone;
  size_t ParamsBegin = Names.size();
  while (true) {
    if (consumeIf('E'))
      break;
    if (consumeIf('v'))
      continue;
    if (consumeIf("RE")) {
      ReferenceQualifier = FrefQualLValue;
      break;
    }
    if (consumeIf("OE")) {
      ReferenceQualifier = FrefQualRValue;
      break;
    }
    Node *T = getDerived().parseType();
    if (T == nullptr)
      return nullptr;
    Names.push_back(T);
  }

  NodeArray Params = popTrailingNodeArray(ParamsBegin);
  return make<FunctionType>(ReturnType, Params, CVQuals,
                            ReferenceQualifier, ExceptionSpec);
}

// extension:
// <vector-type>           ::= Dv <positive dimension number> _ <extended element type>
//                         ::= Dv [<dimension expression>] _ <element type>
// <extended element type> ::= <element type>
//                         ::= p # AltiVec vector pixel
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseVectorType() {
  if (!consumeIf("Dv"))
    return nullptr;
  if (look() >= '1' && look() <= '9') {
    StringView DimensionNumber = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    if (consumeIf('p'))
      return make<PixelVectorType>(DimensionNumber);
    Node *ElemType = getDerived().parseType();
    if (ElemType == nullptr)
      return nullptr;
    return make<VectorType>(ElemType, DimensionNumber);
  }

  if (!consumeIf('_')) {
    Node *DimExpr = getDerived().parseExpr();
    if (!DimExpr)
      return nullptr;
    if (!consumeIf('_'))
      return nullptr;
    Node *ElemType = getDerived().parseType();
    if (!ElemType)
      return nullptr;
    return make<VectorType>(ElemType, DimExpr);
  }
  Node *ElemType = getDerived().parseType();
  if (!ElemType)
    return nullptr;
  return make<VectorType>(ElemType, StringView());
}

// <decltype>  ::= Dt <expression> E  # decltype of an id-expression or class member access (C++0x)
//             ::= DT <expression> E  # decltype of an expression (C++0x)
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseDecltype() {
  if (!consumeIf('D'))
    return nullptr;
  if (!consumeIf('t') && !consumeIf('T'))
    return nullptr;
  Node *E = getDerived().parseExpr();
  if (E == nullptr)
    return nullptr;
  if (!consumeIf('E'))
    return nullptr;
  return make<EnclosingExpr>("decltype(", E, ")");
}

// <array-type> ::= A <positive dimension number> _ <element type>
//              ::= A [<dimension expression>] _ <element type>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseArrayType() {
  if (!consumeIf('A'))
    return nullptr;

  NodeOrString Dimension;

  if (std::isdigit(look())) {
    Dimension = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
  } else if (!consumeIf('_')) {
    Node *DimExpr = getDerived().parseExpr();
    if (DimExpr == nullptr)
      return nullptr;
    if (!consumeIf('_'))
      return nullptr;
    Dimension = DimExpr;
  }

  Node *Ty = getDerived().parseType();
  if (Ty == nullptr)
    return nullptr;
  return make<ArrayType>(Ty, Dimension);
}

// <pointer-to-member-type> ::= M <class type> <member type>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parsePointerToMemberType() {
  if (!consumeIf('M'))
    return nullptr;
  Node *ClassType = getDerived().parseType();
  if (ClassType == nullptr)
    return nullptr;
  Node *MemberType = getDerived().parseType();
  if (MemberType == nullptr)
    return nullptr;
  return make<PointerToMemberType>(ClassType, MemberType);
}

// <class-enum-type> ::= <name>     # non-dependent type name, dependent type name, or dependent typename-specifier
//                   ::= Ts <name>  # dependent elaborated type specifier using 'struct' or 'class'
//                   ::= Tu <name>  # dependent elaborated type specifier using 'union'
//                   ::= Te <name>  # dependent elaborated type specifier using 'enum'
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseClassEnumType() {
  StringView ElabSpef;
  if (consumeIf("Ts"))
    ElabSpef = "struct";
  else if (consumeIf("Tu"))
    ElabSpef = "union";
  else if (consumeIf("Te"))
    ElabSpef = "enum";

  Node *Name = getDerived().parseName();
  if (Name == nullptr)
    return nullptr;

  if (!ElabSpef.empty())
    return make<ElaboratedTypeSpefType>(ElabSpef, Name);

  return Name;
}

// <qualified-type>     ::= <qualifiers> <type>
// <qualifiers> ::= <extended-qualifier>* <CV-qualifiers>
// <extended-qualifier> ::= U <source-name> [<template-args>] # vendor extended type qualifier
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseQualifiedType() {
  if (consumeIf('U')) {
    StringView Qual = parseBareSourceName();
    if (Qual.empty())
      return nullptr;

    // FIXME parse the optional <template-args> here!

    // extension            ::= U <objc-name> <objc-type>  # objc-type<identifier>
    if (Qual.startsWith("objcproto")) {
      StringView ProtoSourceName = Qual.dropFront(std::strlen("objcproto"));
      StringView Proto;
      {
        SwapAndRestore<const char *> SaveFirst(First, ProtoSourceName.begin()),
                                     SaveLast(Last, ProtoSourceName.end());
        Proto = parseBareSourceName();
      }
      if (Proto.empty())
        return nullptr;
      Node *Child = getDerived().parseQualifiedType();
      if (Child == nullptr)
        return nullptr;
      return make<ObjCProtoName>(Child, Proto);
    }

    Node *Child = getDerived().parseQualifiedType();
    if (Child == nullptr)
      return nullptr;
    return make<VendorExtQualType>(Child, Qual);
  }

  Qualifiers Quals = parseCVQualifiers();
  Node *Ty = getDerived().parseType();
  if (Ty == nullptr)
    return nullptr;
  if (Quals != QualNone)
    Ty = make<QualType>(Ty, Quals);
  return Ty;
}

// <type>      ::= <builtin-type>
//             ::= <qualified-type>
//             ::= <function-type>
//             ::= <class-enum-type>
//             ::= <array-type>
//             ::= <pointer-to-member-type>
//             ::= <template-param>
//             ::= <template-template-param> <template-args>
//             ::= <decltype>
//             ::= P <type>        # pointer
//             ::= R <type>        # l-value reference
//             ::= O <type>        # r-value reference (C++11)
//             ::= C <type>        # complex pair (C99)
//             ::= G <type>        # imaginary (C99)
//             ::= <substitution>  # See Compression below
// extension   ::= U <objc-name> <objc-type>  # objc-type<identifier>
// extension   ::= <vector-type> # <vector-type> starts with Dv
//
// <objc-name> ::= <k0 number> objcproto <k1 number> <identifier>  # k0 = 9 + <number of digits in k1> + k1
// <objc-type> ::= <source-name>  # PU<11+>objcproto 11objc_object<source-name> 11objc_object -> id<source-name>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseType() {
  Node *Result = nullptr;

  switch (look()) {
  //             ::= <qualified-type>
  case 'r':
  case 'V':
  case 'K': {
    unsigned AfterQuals = 0;
    if (look(AfterQuals) == 'r') ++AfterQuals;
    if (look(AfterQuals) == 'V') ++AfterQuals;
    if (look(AfterQuals) == 'K') ++AfterQuals;

    if (look(AfterQuals) == 'F' ||
        (look(AfterQuals) == 'D' &&
         (look(AfterQuals + 1) == 'o' || look(AfterQuals + 1) == 'O' ||
          look(AfterQuals + 1) == 'w' || look(AfterQuals + 1) == 'x'))) {
      Result = getDerived().parseFunctionType();
      break;
    }
    LLVM_FALLTHROUGH;
  }
  case 'U': {
    Result = getDerived().parseQualifiedType();
    break;
  }
  // <builtin-type> ::= v    # void
  case 'v':
    ++First;
    return make<NameType>("void");
  //                ::= w    # wchar_t
  case 'w':
    ++First;
    return make<NameType>("wchar_t");
  //                ::= b    # bool
  case 'b':
    ++First;
    return make<NameType>("bool");
  //                ::= c    # char
  case 'c':
    ++First;
    return make<NameType>("char");
  //                ::= a    # signed char
  case 'a':
    ++First;
    return make<NameType>("signed char");
  //                ::= h    # unsigned char
  case 'h':
    ++First;
    return make<NameType>("unsigned char");
  //                ::= s    # short
  case 's':
    ++First;
    return make<NameType>("short");
  //                ::= t    # unsigned short
  case 't':
    ++First;
    return make<NameType>("unsigned short");
  //                ::= i    # int
  case 'i':
    ++First;
    return make<NameType>("int");
  //                ::= j    # unsigned int
  case 'j':
    ++First;
    return make<NameType>("unsigned int");
  //                ::= l    # long
  case 'l':
    ++First;
    return make<NameType>("long");
  //                ::= m    # unsigned long
  case 'm':
    ++First;
    return make<NameType>("unsigned long");
  //                ::= x    # long long, __int64
  case 'x':
    ++First;
    return make<NameType>("long long");
  //                ::= y    # unsigned long long, __int64
  case 'y':
    ++First;
    return make<NameType>("unsigned long long");
  //                ::= n    # __int128
  case 'n':
    ++First;
    return make<NameType>("__int128");
  //                ::= o    # unsigned __int128
  case 'o':
    ++First;
    return make<NameType>("unsigned __int128");
  //                ::= f    # float
  case 'f':
    ++First;
    return make<NameType>("float");
  //                ::= d    # double
  case 'd':
    ++First;
    return make<NameType>("double");
  //                ::= e    # long double, __float80
  case 'e':
    ++First;
    return make<NameType>("long double");
  //                ::= g    # __float128
  case 'g':
    ++First;
    return make<NameType>("__float128");
  //                ::= z    # ellipsis
  case 'z':
    ++First;
    return make<NameType>("...");

  // <builtin-type> ::= u <source-name>    # vendor extended type
  case 'u': {
    ++First;
    StringView Res = parseBareSourceName();
    if (Res.empty())
      return nullptr;
    return make<NameType>(Res);
  }
  case 'D':
    switch (look(1)) {
    //                ::= Dd   # IEEE 754r decimal floating point (64 bits)
    case 'd':
      First += 2;
      return make<NameType>("decimal64");
    //                ::= De   # IEEE 754r decimal floating point (128 bits)
    case 'e':
      First += 2;
      return make<NameType>("decimal128");
    //                ::= Df   # IEEE 754r decimal floating point (32 bits)
    case 'f':
      First += 2;
      return make<NameType>("decimal32");
    //                ::= Dh   # IEEE 754r half-precision floating point (16 bits)
    case 'h':
      First += 2;
      return make<NameType>("decimal16");
    //                ::= Di   # char32_t
    case 'i':
      First += 2;
      return make<NameType>("char32_t");
    //                ::= Ds   # char16_t
    case 's':
      First += 2;
      return make<NameType>("char16_t");
    //                ::= Da   # auto (in dependent new-expressions)
    case 'a':
      First += 2;
      return make<NameType>("auto");
    //                ::= Dc   # decltype(auto)
    case 'c':
      First += 2;
      return make<NameType>("decltype(auto)");
    //                ::= Dn   # std::nullptr_t (i.e., decltype(nullptr))
    case 'n':
      First += 2;
      return make<NameType>("std::nullptr_t");

    //             ::= <decltype>
    case 't':
    case 'T': {
      Result = getDerived().parseDecltype();
      break;
    }
    // extension   ::= <vector-type> # <vector-type> starts with Dv
    case 'v': {
      Result = getDerived().parseVectorType();
      break;
    }
    //           ::= Dp <type>       # pack expansion (C++0x)
    case 'p': {
      First += 2;
      Node *Child = getDerived().parseType();
      if (!Child)
        return nullptr;
      Result = make<ParameterPackExpansion>(Child);
      break;
    }
    // Exception specifier on a function type.
    case 'o':
    case 'O':
    case 'w':
    // Transaction safe function type.
    case 'x':
      Result = getDerived().parseFunctionType();
      break;
    }
    break;
  //             ::= <function-type>
  case 'F': {
    Result = getDerived().parseFunctionType();
    break;
  }
  //             ::= <array-type>
  case 'A': {
    Result = getDerived().parseArrayType();
    break;
  }
  //             ::= <pointer-to-member-type>
  case 'M': {
    Result = getDerived().parsePointerToMemberType();
    break;
  }
  //             ::= <template-param>
  case 'T': {
    // This could be an elaborate type specifier on a <class-enum-type>.
    if (look(1) == 's' || look(1) == 'u' || look(1) == 'e') {
      Result = getDerived().parseClassEnumType();
      break;
    }

    Result = getDerived().parseTemplateParam();
    if (Result == nullptr)
      return nullptr;

    // Result could be either of:
    //   <type>        ::= <template-param>
    //   <type>        ::= <template-template-param> <template-args>
    //
    //   <template-template-param> ::= <template-param>
    //                             ::= <substitution>
    //
    // If this is followed by some <template-args>, and we're permitted to
    // parse them, take the second production.

    if (TryToParseTemplateArgs && look() == 'I') {
      Node *TA = getDerived().parseTemplateArgs();
      if (TA == nullptr)
        return nullptr;
      Result = make<NameWithTemplateArgs>(Result, TA);
    }
    break;
  }
  //             ::= P <type>        # pointer
  case 'P': {
    ++First;
    Node *Ptr = getDerived().parseType();
    if (Ptr == nullptr)
      return nullptr;
    Result = make<PointerType>(Ptr);
    break;
  }
  //             ::= R <type>        # l-value reference
  case 'R': {
    ++First;
    Node *Ref = getDerived().parseType();
    if (Ref == nullptr)
      return nullptr;
    Result = make<ReferenceType>(Ref, ReferenceKind::LValue);
    break;
  }
  //             ::= O <type>        # r-value reference (C++11)
  case 'O': {
    ++First;
    Node *Ref = getDerived().parseType();
    if (Ref == nullptr)
      return nullptr;
    Result = make<ReferenceType>(Ref, ReferenceKind::RValue);
    break;
  }
  //             ::= C <type>        # complex pair (C99)
  case 'C': {
    ++First;
    Node *P = getDerived().parseType();
    if (P == nullptr)
      return nullptr;
    Result = make<PostfixQualifiedType>(P, " complex");
    break;
  }
  //             ::= G <type>        # imaginary (C99)
  case 'G': {
    ++First;
    Node *P = getDerived().parseType();
    if (P == nullptr)
      return P;
    Result = make<PostfixQualifiedType>(P, " imaginary");
    break;
  }
  //             ::= <substitution>  # See Compression below
  case 'S': {
    if (look(1) && look(1) != 't') {
      Node *Sub = getDerived().parseSubstitution();
      if (Sub == nullptr)
        return nullptr;

      // Sub could be either of:
      //   <type>        ::= <substitution>
      //   <type>        ::= <template-template-param> <template-args>
      //
      //   <template-template-param> ::= <template-param>
      //                             ::= <substitution>
      //
      // If this is followed by some <template-args>, and we're permitted to
      // parse them, take the second production.

      if (TryToParseTemplateArgs && look() == 'I') {
        Node *TA = getDerived().parseTemplateArgs();
        if (TA == nullptr)
          return nullptr;
        Result = make<NameWithTemplateArgs>(Sub, TA);
        break;
      }

      // If all we parsed was a substitution, don't re-insert into the
      // substitution table.
      return Sub;
    }
    LLVM_FALLTHROUGH;
  }
  //        ::= <class-enum-type>
  default: {
    Result = getDerived().parseClassEnumType();
    break;
  }
  }

  // If we parsed a type, insert it into the substitution table. Note that all
  // <builtin-type>s and <substitution>s have already bailed out, because they
  // don't get substitutions.
  if (Result != nullptr)
    Subs.push_back(Result);
  return Result;
}

template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parsePrefixExpr(StringView Kind) {
  Node *E = getDerived().parseExpr();
  if (E == nullptr)
    return nullptr;
  return make<PrefixExpr>(Kind, E);
}

template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseBinaryExpr(StringView Kind) {
  Node *LHS = getDerived().parseExpr();
  if (LHS == nullptr)
    return nullptr;
  Node *RHS = getDerived().parseExpr();
  if (RHS == nullptr)
    return nullptr;
  return make<BinaryExpr>(LHS, Kind, RHS);
}

template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseIntegerLiteral(StringView Lit) {
  StringView Tmp = parseNumber(true);
  if (!Tmp.empty() && consumeIf('E'))
    return make<IntegerLiteral>(Lit, Tmp);
  return nullptr;
}

// <CV-Qualifiers> ::= [r] [V] [K]
template <typename Alloc, typename Derived>
Qualifiers AbstractManglingParser<Alloc, Derived>::parseCVQualifiers() {
  Qualifiers CVR = QualNone;
  if (consumeIf('r'))
    CVR |= QualRestrict;
  if (consumeIf('V'))
    CVR |= QualVolatile;
  if (consumeIf('K'))
    CVR |= QualConst;
  return CVR;
}

// <function-param> ::= fp <top-level CV-Qualifiers> _                                     # L == 0, first parameter
//                  ::= fp <top-level CV-Qualifiers> <parameter-2 non-negative number> _   # L == 0, second and later parameters
//                  ::= fL <L-1 non-negative number> p <top-level CV-Qualifiers> _         # L > 0, first parameter
//                  ::= fL <L-1 non-negative number> p <top-level CV-Qualifiers> <parameter-2 non-negative number> _   # L > 0, second and later parameters
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseFunctionParam() {
  if (consumeIf("fp")) {
    parseCVQualifiers();
    StringView Num = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<FunctionParam>(Num);
  }
  if (consumeIf("fL")) {
    if (parseNumber().empty())
      return nullptr;
    if (!consumeIf('p'))
      return nullptr;
    parseCVQualifiers();
    StringView Num = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<FunctionParam>(Num);
  }
  return nullptr;
}

// [gs] nw <expression>* _ <type> E                     # new (expr-list) type
// [gs] nw <expression>* _ <type> <initializer>         # new (expr-list) type (init)
// [gs] na <expression>* _ <type> E                     # new[] (expr-list) type
// [gs] na <expression>* _ <type> <initializer>         # new[] (expr-list) type (init)
// <initializer> ::= pi <expression>* E                 # parenthesized initialization
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseNewExpr() {
  bool Global = consumeIf("gs");
  bool IsArray = look(1) == 'a';
  if (!consumeIf("nw") && !consumeIf("na"))
    return nullptr;
  size_t Exprs = Names.size();
  while (!consumeIf('_')) {
    Node *Ex = getDerived().parseExpr();
    if (Ex == nullptr)
      return nullptr;
    Names.push_back(Ex);
  }
  NodeArray ExprList = popTrailingNodeArray(Exprs);
  Node *Ty = getDerived().parseType();
  if (Ty == nullptr)
    return Ty;
  if (consumeIf("pi")) {
    size_t InitsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *Init = getDerived().parseExpr();
      if (Init == nullptr)
        return Init;
      Names.push_back(Init);
    }
    NodeArray Inits = popTrailingNodeArray(InitsBegin);
    return make<NewExpr>(ExprList, Ty, Inits, Global, IsArray);
  } else if (!consumeIf('E'))
    return nullptr;
  return make<NewExpr>(ExprList, Ty, NodeArray(), Global, IsArray);
}

// cv <type> <expression>                               # conversion with one argument
// cv <type> _ <expression>* E                          # conversion with a different number of arguments
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseConversionExpr() {
  if (!consumeIf("cv"))
    return nullptr;
  Node *Ty;
  {
    SwapAndRestore<bool> SaveTemp(TryToParseTemplateArgs, false);
    Ty = getDerived().parseType();
  }

  if (Ty == nullptr)
    return nullptr;

  if (consumeIf('_')) {
    size_t ExprsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *E = getDerived().parseExpr();
      if (E == nullptr)
        return E;
      Names.push_back(E);
    }
    NodeArray Exprs = popTrailingNodeArray(ExprsBegin);
    return make<ConversionExpr>(Ty, Exprs);
  }

  Node *E[1] = {getDerived().parseExpr()};
  if (E[0] == nullptr)
    return nullptr;
  return make<ConversionExpr>(Ty, makeNodeArray(E, E + 1));
}

// <expr-primary> ::= L <type> <value number> E                          # integer literal
//                ::= L <type> <value float> E                           # floating literal
//                ::= L <string type> E                                  # string literal
//                ::= L <nullptr type> E                                 # nullptr literal (i.e., "LDnE")
// FIXME:         ::= L <type> <real-part float> _ <imag-part float> E   # complex floating point literal (C 2000)
//                ::= L <mangled-name> E                                 # external name
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseExprPrimary() {
  if (!consumeIf('L'))
    return nullptr;
  switch (look()) {
  case 'w':
    ++First;
    return getDerived().parseIntegerLiteral("wchar_t");
  case 'b':
    if (consumeIf("b0E"))
      return make<BoolExpr>(0);
    if (consumeIf("b1E"))
      return make<BoolExpr>(1);
    return nullptr;
  case 'c':
    ++First;
    return getDerived().parseIntegerLiteral("char");
  case 'a':
    ++First;
    return getDerived().parseIntegerLiteral("signed char");
  case 'h':
    ++First;
    return getDerived().parseIntegerLiteral("unsigned char");
  case 's':
    ++First;
    return getDerived().parseIntegerLiteral("short");
  case 't':
    ++First;
    return getDerived().parseIntegerLiteral("unsigned short");
  case 'i':
    ++First;
    return getDerived().parseIntegerLiteral("");
  case 'j':
    ++First;
    return getDerived().parseIntegerLiteral("u");
  case 'l':
    ++First;
    return getDerived().parseIntegerLiteral("l");
  case 'm':
    ++First;
    return getDerived().parseIntegerLiteral("ul");
  case 'x':
    ++First;
    return getDerived().parseIntegerLiteral("ll");
  case 'y':
    ++First;
    return getDerived().parseIntegerLiteral("ull");
  case 'n':
    ++First;
    return getDerived().parseIntegerLiteral("__int128");
  case 'o':
    ++First;
    return getDerived().parseIntegerLiteral("unsigned __int128");
  case 'f':
    ++First;
    return getDerived().template parseFloatingLiteral<float>();
  case 'd':
    ++First;
    return getDerived().template parseFloatingLiteral<double>();
  case 'e':
    ++First;
    return getDerived().template parseFloatingLiteral<long double>();
  case '_':
    if (consumeIf("_Z")) {
      Node *R = getDerived().parseEncoding();
      if (R != nullptr && consumeIf('E'))
        return R;
    }
    return nullptr;
  case 'T':
    // Invalid mangled name per
    //   http://sourcerytools.com/pipermail/cxx-abi-dev/2011-August/002422.html
    return nullptr;
  default: {
    // might be named type
    Node *T = getDerived().parseType();
    if (T == nullptr)
      return nullptr;
    StringView N = parseNumber();
    if (!N.empty()) {
      if (!consumeIf('E'))
        return nullptr;
      return make<IntegerCastExpr>(T, N);
    }
    if (consumeIf('E'))
      return T;
    return nullptr;
  }
  }
}

// <braced-expression> ::= <expression>
//                     ::= di <field source-name> <braced-expression>    # .name = expr
//                     ::= dx <index expression> <braced-expression>     # [expr] = expr
//                     ::= dX <range begin expression> <range end expression> <braced-expression>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseBracedExpr() {
  if (look() == 'd') {
    switch (look(1)) {
    case 'i': {
      First += 2;
      Node *Field = getDerived().parseSourceName(/*NameState=*/nullptr);
      if (Field == nullptr)
        return nullptr;
      Node *Init = getDerived().parseBracedExpr();
      if (Init == nullptr)
        return nullptr;
      return make<BracedExpr>(Field, Init, /*isArray=*/false);
    }
    case 'x': {
      First += 2;
      Node *Index = getDerived().parseExpr();
      if (Index == nullptr)
        return nullptr;
      Node *Init = getDerived().parseBracedExpr();
      if (Init == nullptr)
        return nullptr;
      return make<BracedExpr>(Index, Init, /*isArray=*/true);
    }
    case 'X': {
      First += 2;
      Node *RangeBegin = getDerived().parseExpr();
      if (RangeBegin == nullptr)
        return nullptr;
      Node *RangeEnd = getDerived().parseExpr();
      if (RangeEnd == nullptr)
        return nullptr;
      Node *Init = getDerived().parseBracedExpr();
      if (Init == nullptr)
        return nullptr;
      return make<BracedRangeExpr>(RangeBegin, RangeEnd, Init);
    }
    }
  }
  return getDerived().parseExpr();
}

// (not yet in the spec)
// <fold-expr> ::= fL <binary-operator-name> <expression> <expression>
//             ::= fR <binary-operator-name> <expression> <expression>
//             ::= fl <binary-operator-name> <expression>
//             ::= fr <binary-operator-name> <expression>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseFoldExpr() {
  if (!consumeIf('f'))
    return nullptr;

  char FoldKind = look();
  bool IsLeftFold, HasInitializer;
  HasInitializer = FoldKind == 'L' || FoldKind == 'R';
  if (FoldKind == 'l' || FoldKind == 'L')
    IsLeftFold = true;
  else if (FoldKind == 'r' || FoldKind == 'R')
    IsLeftFold = false;
  else
    return nullptr;
  ++First;

  // FIXME: This map is duplicated in parseOperatorName and parseExpr.
  StringView OperatorName;
  if      (consumeIf("aa")) OperatorName = "&&";
  else if (consumeIf("an")) OperatorName = "&";
  else if (consumeIf("aN")) OperatorName = "&=";
  else if (consumeIf("aS")) OperatorName = "=";
  else if (consumeIf("cm")) OperatorName = ",";
  else if (consumeIf("ds")) OperatorName = ".*";
  else if (consumeIf("dv")) OperatorName = "/";
  else if (consumeIf("dV")) OperatorName = "/=";
  else if (consumeIf("eo")) OperatorName = "^";
  else if (consumeIf("eO")) OperatorName = "^=";
  else if (consumeIf("eq")) OperatorName = "==";
  else if (consumeIf("ge")) OperatorName = ">=";
  else if (consumeIf("gt")) OperatorName = ">";
  else if (consumeIf("le")) OperatorName = "<=";
  else if (consumeIf("ls")) OperatorName = "<<";
  else if (consumeIf("lS")) OperatorName = "<<=";
  else if (consumeIf("lt")) OperatorName = "<";
  else if (consumeIf("mi")) OperatorName = "-";
  else if (consumeIf("mI")) OperatorName = "-=";
  else if (consumeIf("ml")) OperatorName = "*";
  else if (consumeIf("mL")) OperatorName = "*=";
  else if (consumeIf("ne")) OperatorName = "!=";
  else if (consumeIf("oo")) OperatorName = "||";
  else if (consumeIf("or")) OperatorName = "|";
  else if (consumeIf("oR")) OperatorName = "|=";
  else if (consumeIf("pl")) OperatorName = "+";
  else if (consumeIf("pL")) OperatorName = "+=";
  else if (consumeIf("rm")) OperatorName = "%";
  else if (consumeIf("rM")) OperatorName = "%=";
  else if (consumeIf("rs")) OperatorName = ">>";
  else if (consumeIf("rS")) OperatorName = ">>=";
  else return nullptr;

  Node *Pack = getDerived().parseExpr(), *Init = nullptr;
  if (Pack == nullptr)
    return nullptr;
  if (HasInitializer) {
    Init = getDerived().parseExpr();
    if (Init == nullptr)
      return nullptr;
  }

  if (IsLeftFold && Init)
    std::swap(Pack, Init);

  return make<FoldExpr>(IsLeftFold, OperatorName, Pack, Init);
}

// <expression> ::= <unary operator-name> <expression>
//              ::= <binary operator-name> <expression> <expression>
//              ::= <ternary operator-name> <expression> <expression> <expression>
//              ::= cl <expression>+ E                                   # call
//              ::= cv <type> <expression>                               # conversion with one argument
//              ::= cv <type> _ <expression>* E                          # conversion with a different number of arguments
//              ::= [gs] nw <expression>* _ <type> E                     # new (expr-list) type
//              ::= [gs] nw <expression>* _ <type> <initializer>         # new (expr-list) type (init)
//              ::= [gs] na <expression>* _ <type> E                     # new[] (expr-list) type
//              ::= [gs] na <expression>* _ <type> <initializer>         # new[] (expr-list) type (init)
//              ::= [gs] dl <expression>                                 # delete expression
//              ::= [gs] da <expression>                                 # delete[] expression
//              ::= pp_ <expression>                                     # prefix ++
//              ::= mm_ <expression>                                     # prefix --
//              ::= ti <type>                                            # typeid (type)
//              ::= te <expression>                                      # typeid (expression)
//              ::= dc <type> <expression>                               # dynamic_cast<type> (expression)
//              ::= sc <type> <expression>                               # static_cast<type> (expression)
//              ::= cc <type> <expression>                               # const_cast<type> (expression)
//              ::= rc <type> <expression>                               # reinterpret_cast<type> (expression)
//              ::= st <type>                                            # sizeof (a type)
//              ::= sz <expression>                                      # sizeof (an expression)
//              ::= at <type>                                            # alignof (a type)
//              ::= az <expression>                                      # alignof (an expression)
//              ::= nx <expression>                                      # noexcept (expression)
//              ::= <template-param>
//              ::= <function-param>
//              ::= dt <expression> <unresolved-name>                    # expr.name
//              ::= pt <expression> <unresolved-name>                    # expr->name
//              ::= ds <expression> <expression>                         # expr.*expr
//              ::= sZ <template-param>                                  # size of a parameter pack
//              ::= sZ <function-param>                                  # size of a function parameter pack
//              ::= sP <template-arg>* E                                 # sizeof...(T), size of a captured template parameter pack from an alias template
//              ::= sp <expression>                                      # pack expansion
//              ::= tw <expression>                                      # throw expression
//              ::= tr                                                   # throw with no operand (rethrow)
//              ::= <unresolved-name>                                    # f(p), N::f(p), ::f(p),
//                                                                       # freestanding dependent name (e.g., T::x),
//                                                                       # objectless nonstatic member reference
//              ::= fL <binary-operator-name> <expression> <expression>
//              ::= fR <binary-operator-name> <expression> <expression>
//              ::= fl <binary-operator-name> <expression>
//              ::= fr <binary-operator-name> <expression>
//              ::= <expr-primary>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseExpr() {
  bool Global = consumeIf("gs");
  if (numLeft() < 2)
    return nullptr;

  switch (*First) {
  case 'L':
    return getDerived().parseExprPrimary();
  case 'T':
    return getDerived().parseTemplateParam();
  case 'f': {
    // Disambiguate a fold expression from a <function-param>.
    if (look(1) == 'p' || (look(1) == 'L' && std::isdigit(look(2))))
      return getDerived().parseFunctionParam();
    return getDerived().parseFoldExpr();
  }
  case 'a':
    switch (First[1]) {
    case 'a':
      First += 2;
      return getDerived().parseBinaryExpr("&&");
    case 'd':
      First += 2;
      return getDerived().parsePrefixExpr("&");
    case 'n':
      First += 2;
      return getDerived().parseBinaryExpr("&");
    case 'N':
      First += 2;
      return getDerived().parseBinaryExpr("&=");
    case 'S':
      First += 2;
      return getDerived().parseBinaryExpr("=");
    case 't': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<EnclosingExpr>("alignof (", Ty, ")");
    }
    case 'z': {
      First += 2;
      Node *Ty = getDerived().parseExpr();
      if (Ty == nullptr)
        return nullptr;
      return make<EnclosingExpr>("alignof (", Ty, ")");
    }
    }
    return nullptr;
  case 'c':
    switch (First[1]) {
    // cc <type> <expression>                               # const_cast<type>(expression)
    case 'c': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return Ty;
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return Ex;
      return make<CastExpr>("const_cast", Ty, Ex);
    }
    // cl <expression>+ E                                   # call
    case 'l': {
      First += 2;
      Node *Callee = getDerived().parseExpr();
      if (Callee == nullptr)
        return Callee;
      size_t ExprsBegin = Names.size();
      while (!consumeIf('E')) {
        Node *E = getDerived().parseExpr();
        if (E == nullptr)
          return E;
        Names.push_back(E);
      }
      return make<CallExpr>(Callee, popTrailingNodeArray(ExprsBegin));
    }
    case 'm':
      First += 2;
      return getDerived().parseBinaryExpr(",");
    case 'o':
      First += 2;
      return getDerived().parsePrefixExpr("~");
    case 'v':
      return getDerived().parseConversionExpr();
    }
    return nullptr;
  case 'd':
    switch (First[1]) {
    case 'a': {
      First += 2;
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return Ex;
      return make<DeleteExpr>(Ex, Global, /*is_array=*/true);
    }
    case 'c': {
      First += 2;
      Node *T = getDerived().parseType();
      if (T == nullptr)
        return T;
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return Ex;
      return make<CastExpr>("dynamic_cast", T, Ex);
    }
    case 'e':
      First += 2;
      return getDerived().parsePrefixExpr("*");
    case 'l': {
      First += 2;
      Node *E = getDerived().parseExpr();
      if (E == nullptr)
        return E;
      return make<DeleteExpr>(E, Global, /*is_array=*/false);
    }
    case 'n':
      return getDerived().parseUnresolvedName();
    case 's': {
      First += 2;
      Node *LHS = getDerived().parseExpr();
      if (LHS == nullptr)
        return nullptr;
      Node *RHS = getDerived().parseExpr();
      if (RHS == nullptr)
        return nullptr;
      return make<MemberExpr>(LHS, ".*", RHS);
    }
    case 't': {
      First += 2;
      Node *LHS = getDerived().parseExpr();
      if (LHS == nullptr)
        return LHS;
      Node *RHS = getDerived().parseExpr();
      if (RHS == nullptr)
        return nullptr;
      return make<MemberExpr>(LHS, ".", RHS);
    }
    case 'v':
      First += 2;
      return getDerived().parseBinaryExpr("/");
    case 'V':
      First += 2;
      return getDerived().parseBinaryExpr("/=");
    }
    return nullptr;
  case 'e':
    switch (First[1]) {
    case 'o':
      First += 2;
      return getDerived().parseBinaryExpr("^");
    case 'O':
      First += 2;
      return getDerived().parseBinaryExpr("^=");
    case 'q':
      First += 2;
      return getDerived().parseBinaryExpr("==");
    }
    return nullptr;
  case 'g':
    switch (First[1]) {
    case 'e':
      First += 2;
      return getDerived().parseBinaryExpr(">=");
    case 't':
      First += 2;
      return getDerived().parseBinaryExpr(">");
    }
    return nullptr;
  case 'i':
    switch (First[1]) {
    case 'x': {
      First += 2;
      Node *Base = getDerived().parseExpr();
      if (Base == nullptr)
        return nullptr;
      Node *Index = getDerived().parseExpr();
      if (Index == nullptr)
        return Index;
      return make<ArraySubscriptExpr>(Base, Index);
    }
    case 'l': {
      First += 2;
      size_t InitsBegin = Names.size();
      while (!consumeIf('E')) {
        Node *E = getDerived().parseBracedExpr();
        if (E == nullptr)
          return nullptr;
        Names.push_back(E);
      }
      return make<InitListExpr>(nullptr, popTrailingNodeArray(InitsBegin));
    }
    }
    return nullptr;
  case 'l':
    switch (First[1]) {
    case 'e':
      First += 2;
      return getDerived().parseBinaryExpr("<=");
    case 's':
      First += 2;
      return getDerived().parseBinaryExpr("<<");
    case 'S':
      First += 2;
      return getDerived().parseBinaryExpr("<<=");
    case 't':
      First += 2;
      return getDerived().parseBinaryExpr("<");
    }
    return nullptr;
  case 'm':
    switch (First[1]) {
    case 'i':
      First += 2;
      return getDerived().parseBinaryExpr("-");
    case 'I':
      First += 2;
      return getDerived().parseBinaryExpr("-=");
    case 'l':
      First += 2;
      return getDerived().parseBinaryExpr("*");
    case 'L':
      First += 2;
      return getDerived().parseBinaryExpr("*=");
    case 'm':
      First += 2;
      if (consumeIf('_'))
        return getDerived().parsePrefixExpr("--");
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return nullptr;
      return make<PostfixExpr>(Ex, "--");
    }
    return nullptr;
  case 'n':
    switch (First[1]) {
    case 'a':
    case 'w':
      return getDerived().parseNewExpr();
    case 'e':
      First += 2;
      return getDerived().parseBinaryExpr("!=");
    case 'g':
      First += 2;
      return getDerived().parsePrefixExpr("-");
    case 't':
      First += 2;
      return getDerived().parsePrefixExpr("!");
    case 'x':
      First += 2;
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return Ex;
      return make<EnclosingExpr>("noexcept (", Ex, ")");
    }
    return nullptr;
  case 'o':
    switch (First[1]) {
    case 'n':
      return getDerived().parseUnresolvedName();
    case 'o':
      First += 2;
      return getDerived().parseBinaryExpr("||");
    case 'r':
      First += 2;
      return getDerived().parseBinaryExpr("|");
    case 'R':
      First += 2;
      return getDerived().parseBinaryExpr("|=");
    }
    return nullptr;
  case 'p':
    switch (First[1]) {
    case 'm':
      First += 2;
      return getDerived().parseBinaryExpr("->*");
    case 'l':
      First += 2;
      return getDerived().parseBinaryExpr("+");
    case 'L':
      First += 2;
      return getDerived().parseBinaryExpr("+=");
    case 'p': {
      First += 2;
      if (consumeIf('_'))
        return getDerived().parsePrefixExpr("++");
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return Ex;
      return make<PostfixExpr>(Ex, "++");
    }
    case 's':
      First += 2;
      return getDerived().parsePrefixExpr("+");
    case 't': {
      First += 2;
      Node *L = getDerived().parseExpr();
      if (L == nullptr)
        return nullptr;
      Node *R = getDerived().parseExpr();
      if (R == nullptr)
        return nullptr;
      return make<MemberExpr>(L, "->", R);
    }
    }
    return nullptr;
  case 'q':
    if (First[1] == 'u') {
      First += 2;
      Node *Cond = getDerived().parseExpr();
      if (Cond == nullptr)
        return nullptr;
      Node *LHS = getDerived().parseExpr();
      if (LHS == nullptr)
        return nullptr;
      Node *RHS = getDerived().parseExpr();
      if (RHS == nullptr)
        return nullptr;
      return make<ConditionalExpr>(Cond, LHS, RHS);
    }
    return nullptr;
  case 'r':
    switch (First[1]) {
    case 'c': {
      First += 2;
      Node *T = getDerived().parseType();
      if (T == nullptr)
        return T;
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return Ex;
      return make<CastExpr>("reinterpret_cast", T, Ex);
    }
    case 'm':
      First += 2;
      return getDerived().parseBinaryExpr("%");
    case 'M':
      First += 2;
      return getDerived().parseBinaryExpr("%=");
    case 's':
      First += 2;
      return getDerived().parseBinaryExpr(">>");
    case 'S':
      First += 2;
      return getDerived().parseBinaryExpr(">>=");
    }
    return nullptr;
  case 's':
    switch (First[1]) {
    case 'c': {
      First += 2;
      Node *T = getDerived().parseType();
      if (T == nullptr)
        return T;
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return Ex;
      return make<CastExpr>("static_cast", T, Ex);
    }
    case 'p': {
      First += 2;
      Node *Child = getDerived().parseExpr();
      if (Child == nullptr)
        return nullptr;
      return make<ParameterPackExpansion>(Child);
    }
    case 'r':
      return getDerived().parseUnresolvedName();
    case 't': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return Ty;
      return make<EnclosingExpr>("sizeof (", Ty, ")");
    }
    case 'z': {
      First += 2;
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return Ex;
      return make<EnclosingExpr>("sizeof (", Ex, ")");
    }
    case 'Z':
      First += 2;
      if (look() == 'T') {
        Node *R = getDerived().parseTemplateParam();
        if (R == nullptr)
          return nullptr;
        return make<SizeofParamPackExpr>(R);
      } else if (look() == 'f') {
        Node *FP = getDerived().parseFunctionParam();
        if (FP == nullptr)
          return nullptr;
        return make<EnclosingExpr>("sizeof... (", FP, ")");
      }
      return nullptr;
    case 'P': {
      First += 2;
      size_t ArgsBegin = Names.size();
      while (!consumeIf('E')) {
        Node *Arg = getDerived().parseTemplateArg();
        if (Arg == nullptr)
          return nullptr;
        Names.push_back(Arg);
      }
      auto *Pack = make<NodeArrayNode>(popTrailingNodeArray(ArgsBegin));
      if (!Pack)
        return nullptr;
      return make<EnclosingExpr>("sizeof... (", Pack, ")");
    }
    }
    return nullptr;
  case 't':
    switch (First[1]) {
    case 'e': {
      First += 2;
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return Ex;
      return make<EnclosingExpr>("typeid (", Ex, ")");
    }
    case 'i': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return Ty;
      return make<EnclosingExpr>("typeid (", Ty, ")");
    }
    case 'l': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      size_t InitsBegin = Names.size();
      while (!consumeIf('E')) {
        Node *E = getDerived().parseBracedExpr();
        if (E == nullptr)
          return nullptr;
        Names.push_back(E);
      }
      return make<InitListExpr>(Ty, popTrailingNodeArray(InitsBegin));
    }
    case 'r':
      First += 2;
      return make<NameType>("throw");
    case 'w': {
      First += 2;
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return nullptr;
      return make<ThrowExpr>(Ex);
    }
    }
    return nullptr;
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    return getDerived().parseUnresolvedName();
  }
  return nullptr;
}

// <call-offset> ::= h <nv-offset> _
//               ::= v <v-offset> _
//
// <nv-offset> ::= <offset number>
//               # non-virtual base override
//
// <v-offset>  ::= <offset number> _ <virtual offset number>
//               # virtual base override, with vcall offset
template <typename Alloc, typename Derived>
bool AbstractManglingParser<Alloc, Derived>::parseCallOffset() {
  // Just scan through the call offset, we never add this information into the
  // output.
  if (consumeIf('h'))
    return parseNumber(true).empty() || !consumeIf('_');
  if (consumeIf('v'))
    return parseNumber(true).empty() || !consumeIf('_') ||
           parseNumber(true).empty() || !consumeIf('_');
  return true;
}

// <special-name> ::= TV <type>    # virtual table
//                ::= TT <type>    # VTT structure (construction vtable index)
//                ::= TI <type>    # typeinfo structure
//                ::= TS <type>    # typeinfo name (null-terminated byte string)
//                ::= Tc <call-offset> <call-offset> <base encoding>
//                    # base is the nominal target function of thunk
//                    # first call-offset is 'this' adjustment
//                    # second call-offset is result adjustment
//                ::= T <call-offset> <base encoding>
//                    # base is the nominal target function of thunk
//                ::= GV <object name> # Guard variable for one-time initialization
//                                     # No <type>
//                ::= TW <object name> # Thread-local wrapper
//                ::= TH <object name> # Thread-local initialization
//                ::= GR <object name> _             # First temporary
//                ::= GR <object name> <seq-id> _    # Subsequent temporaries
//      extension ::= TC <first type> <number> _ <second type> # construction vtable for second-in-first
//      extension ::= GR <object name> # reference temporary for object
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSpecialName() {
  switch (look()) {
  case 'T':
    switch (look(1)) {
    // TV <type>    # virtual table
    case 'V': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<SpecialName>("vtable for ", Ty);
    }
    // TT <type>    # VTT structure (construction vtable index)
    case 'T': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<SpecialName>("VTT for ", Ty);
    }
    // TI <type>    # typeinfo structure
    case 'I': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<SpecialName>("typeinfo for ", Ty);
    }
    // TS <type>    # typeinfo name (null-terminated byte string)
    case 'S': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<SpecialName>("typeinfo name for ", Ty);
    }
    // Tc <call-offset> <call-offset> <base encoding>
    case 'c': {
      First += 2;
      if (parseCallOffset() || parseCallOffset())
        return nullptr;
      Node *Encoding = getDerived().parseEncoding();
      if (Encoding == nullptr)
        return nullptr;
      return make<SpecialName>("covariant return thunk to ", Encoding);
    }
    // extension ::= TC <first type> <number> _ <second type>
    //               # construction vtable for second-in-first
    case 'C': {
      First += 2;
      Node *FirstType = getDerived().parseType();
      if (FirstType == nullptr)
        return nullptr;
      if (parseNumber(true).empty() || !consumeIf('_'))
        return nullptr;
      Node *SecondType = getDerived().parseType();
      if (SecondType == nullptr)
        return nullptr;
      return make<CtorVtableSpecialName>(SecondType, FirstType);
    }
    // TW <object name> # Thread-local wrapper
    case 'W': {
      First += 2;
      Node *Name = getDerived().parseName();
      if (Name == nullptr)
        return nullptr;
      return make<SpecialName>("thread-local wrapper routine for ", Name);
    }
    // TH <object name> # Thread-local initialization
    case 'H': {
      First += 2;
      Node *Name = getDerived().parseName();
      if (Name == nullptr)
        return nullptr;
      return make<SpecialName>("thread-local initialization routine for ", Name);
    }
    // T <call-offset> <base encoding>
    default: {
      ++First;
      bool IsVirt = look() == 'v';
      if (parseCallOffset())
        return nullptr;
      Node *BaseEncoding = getDerived().parseEncoding();
      if (BaseEncoding == nullptr)
        return nullptr;
      if (IsVirt)
        return make<SpecialName>("virtual thunk to ", BaseEncoding);
      else
        return make<SpecialName>("non-virtual thunk to ", BaseEncoding);
    }
    }
  case 'G':
    switch (look(1)) {
    // GV <object name> # Guard variable for one-time initialization
    case 'V': {
      First += 2;
      Node *Name = getDerived().parseName();
      if (Name == nullptr)
        return nullptr;
      return make<SpecialName>("guard variable for ", Name);
    }
    // GR <object name> # reference temporary for object
    // GR <object name> _             # First temporary
    // GR <object name> <seq-id> _    # Subsequent temporaries
    case 'R': {
      First += 2;
      Node *Name = getDerived().parseName();
      if (Name == nullptr)
        return nullptr;
      size_t Count;
      bool ParsedSeqId = !parseSeqId(&Count);
      if (!consumeIf('_') && ParsedSeqId)
        return nullptr;
      return make<SpecialName>("reference temporary for ", Name);
    }
    }
  }
  return nullptr;
}

// <encoding> ::= <function name> <bare-function-type>
//            ::= <data name>
//            ::= <special-name>
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseEncoding() {
  if (look() == 'G' || look() == 'T')
    return getDerived().parseSpecialName();

  auto IsEndOfEncoding = [&] {
    // The set of chars that can potentially follow an <encoding> (none of which
    // can start a <type>). Enumerating these allows us to avoid speculative
    // parsing.
    return numLeft() == 0 || look() == 'E' || look() == '.' || look() == '_';
  };

  NameState NameInfo(this);
  Node *Name = getDerived().parseName(&NameInfo);
  if (Name == nullptr)
    return nullptr;

  if (resolveForwardTemplateRefs(NameInfo))
    return nullptr;

  if (IsEndOfEncoding())
    return Name;

  Node *Attrs = nullptr;
  if (consumeIf("Ua9enable_ifI")) {
    size_t BeforeArgs = Names.size();
    while (!consumeIf('E')) {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
    }
    Attrs = make<EnableIfAttr>(popTrailingNodeArray(BeforeArgs));
    if (!Attrs)
      return nullptr;
  }

  Node *ReturnType = nullptr;
  if (!NameInfo.CtorDtorConversion && NameInfo.EndsWithTemplateArgs) {
    ReturnType = getDerived().parseType();
    if (ReturnType == nullptr)
      return nullptr;
  }

  if (consumeIf('v'))
    return make<FunctionEncoding>(ReturnType, Name, NodeArray(),
                                  Attrs, NameInfo.CVQualifiers,
                                  NameInfo.ReferenceQualifier);

  size_t ParamsBegin = Names.size();
  do {
    Node *Ty = getDerived().parseType();
    if (Ty == nullptr)
      return nullptr;
    Names.push_back(Ty);
  } while (!IsEndOfEncoding());

  return make<FunctionEncoding>(ReturnType, Name,
                                popTrailingNodeArray(ParamsBegin),
                                Attrs, NameInfo.CVQualifiers,
                                NameInfo.ReferenceQualifier);
}

template <class Float>
struct FloatData;

template <>
struct FloatData<float>
{
    static const size_t mangled_size = 8;
    static const size_t max_demangled_size = 24;
    static constexpr const char* spec = "%af";
};

template <>
struct FloatData<double>
{
    static const size_t mangled_size = 16;
    static const size_t max_demangled_size = 32;
    static constexpr const char* spec = "%a";
};

template <>
struct FloatData<long double>
{
#if defined(__mips__) && defined(__mips_n64) || defined(__aarch64__) || \
    defined(__wasm__)
    static const size_t mangled_size = 32;
#elif defined(__arm__) || defined(__mips__) || defined(__hexagon__)
    static const size_t mangled_size = 16;
#else
    static const size_t mangled_size = 20;  // May need to be adjusted to 16 or 24 on other platforms
#endif
    static const size_t max_demangled_size = 40;
    static constexpr const char *spec = "%LaL";
};

template <typename Alloc, typename Derived>
template <class Float>
Node *AbstractManglingParser<Alloc, Derived>::parseFloatingLiteral() {
  const size_t N = FloatData<Float>::mangled_size;
  if (numLeft() <= N)
    return nullptr;
  StringView Data(First, First + N);
  for (char C : Data)
    if (!std::isxdigit(C))
      return nullptr;
  First += N;
  if (!consumeIf('E'))
    return nullptr;
  return make<FloatLiteralImpl<Float>>(Data);
}

// <seq-id> ::= <0-9A-Z>+
template <typename Alloc, typename Derived>
bool AbstractManglingParser<Alloc, Derived>::parseSeqId(size_t *Out) {
  if (!(look() >= '0' && look() <= '9') &&
      !(look() >= 'A' && look() <= 'Z'))
    return true;

  size_t Id = 0;
  while (true) {
    if (look() >= '0' && look() <= '9') {
      Id *= 36;
      Id += static_cast<size_t>(look() - '0');
    } else if (look() >= 'A' && look() <= 'Z') {
      Id *= 36;
      Id += static_cast<size_t>(look() - 'A') + 10;
    } else {
      *Out = Id;
      return false;
    }
    ++First;
  }
}

// <substitution> ::= S <seq-id> _
//                ::= S_
// <substitution> ::= Sa # ::std::allocator
// <substitution> ::= Sb # ::std::basic_string
// <substitution> ::= Ss # ::std::basic_string < char,
//                                               ::std::char_traits<char>,
//                                               ::std::allocator<char> >
// <substitution> ::= Si # ::std::basic_istream<char,  std::char_traits<char> >
// <substitution> ::= So # ::std::basic_ostream<char,  std::char_traits<char> >
// <substitution> ::= Sd # ::std::basic_iostream<char, std::char_traits<char> >
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSubstitution() {
  if (!consumeIf('S'))
    return nullptr;

  if (std::islower(look())) {
    Node *SpecialSub;
    switch (look()) {
    case 'a':
      ++First;
      SpecialSub = make<SpecialSubstitution>(SpecialSubKind::allocator);
      break;
    case 'b':
      ++First;
      SpecialSub = make<SpecialSubstitution>(SpecialSubKind::basic_string);
      break;
    case 's':
      ++First;
      SpecialSub = make<SpecialSubstitution>(SpecialSubKind::string);
      break;
    case 'i':
      ++First;
      SpecialSub = make<SpecialSubstitution>(SpecialSubKind::istream);
      break;
    case 'o':
      ++First;
      SpecialSub = make<SpecialSubstitution>(SpecialSubKind::ostream);
      break;
    case 'd':
      ++First;
      SpecialSub = make<SpecialSubstitution>(SpecialSubKind::iostream);
      break;
    default:
      return nullptr;
    }
    if (!SpecialSub)
      return nullptr;
    // Itanium C++ ABI 5.1.2: If a name that would use a built-in <substitution>
    // has ABI tags, the tags are appended to the substitution; the result is a
    // substitutable component.
    Node *WithTags = getDerived().parseAbiTags(SpecialSub);
    if (WithTags != SpecialSub) {
      Subs.push_back(WithTags);
      SpecialSub = WithTags;
    }
    return SpecialSub;
  }

  //                ::= S_
  if (consumeIf('_')) {
    if (Subs.empty())
      return nullptr;
    return Subs[0];
  }

  //                ::= S <seq-id> _
  size_t Index = 0;
  if (parseSeqId(&Index))
    return nullptr;
  ++Index;
  if (!consumeIf('_') || Index >= Subs.size())
    return nullptr;
  return Subs[Index];
}

// <template-param> ::= T_    # first template parameter
//                  ::= T <parameter-2 non-negative number> _
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseTemplateParam() {
  if (!consumeIf('T'))
    return nullptr;

  size_t Index = 0;
  if (!consumeIf('_')) {
    if (parsePositiveInteger(&Index))
      return nullptr;
    ++Index;
    if (!consumeIf('_'))
      return nullptr;
  }

  // Itanium ABI 5.1.8: In a generic lambda, uses of auto in the parameter list
  // are mangled as the corresponding artificial template type parameter.
  if (ParsingLambdaParams)
    return make<NameType>("auto");

  // If we're in a context where this <template-param> refers to a
  // <template-arg> further ahead in the mangled name (currently just conversion
  // operator types), then we should only look it up in the right context.
  if (PermitForwardTemplateReferences) {
    Node *ForwardRef = make<ForwardTemplateReference>(Index);
    if (!ForwardRef)
      return nullptr;
    assert(ForwardRef->getKind() == Node::KForwardTemplateReference);
    ForwardTemplateRefs.push_back(
        static_cast<ForwardTemplateReference *>(ForwardRef));
    return ForwardRef;
  }

  if (Index >= TemplateParams.size())
    return nullptr;
  return TemplateParams[Index];
}

// <template-arg> ::= <type>                    # type or template
//                ::= X <expression> E          # expression
//                ::= <expr-primary>            # simple expressions
//                ::= J <template-arg>* E       # argument pack
//                ::= LZ <encoding> E           # extension
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseTemplateArg() {
  switch (look()) {
  case 'X': {
    ++First;
    Node *Arg = getDerived().parseExpr();
    if (Arg == nullptr || !consumeIf('E'))
      return nullptr;
    return Arg;
  }
  case 'J': {
    ++First;
    size_t ArgsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
    }
    NodeArray Args = popTrailingNodeArray(ArgsBegin);
    return make<TemplateArgumentPack>(Args);
  }
  case 'L': {
    //                ::= LZ <encoding> E           # extension
    if (look(1) == 'Z') {
      First += 2;
      Node *Arg = getDerived().parseEncoding();
      if (Arg == nullptr || !consumeIf('E'))
        return nullptr;
      return Arg;
    }
    //                ::= <expr-primary>            # simple expressions
    return getDerived().parseExprPrimary();
  }
  default:
    return getDerived().parseType();
  }
}

// <template-args> ::= I <template-arg>* E
//     extension, the abi says <template-arg>+
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseTemplateArgs(bool TagTemplates) {
  if (!consumeIf('I'))
    return nullptr;

  // <template-params> refer to the innermost <template-args>. Clear out any
  // outer args that we may have inserted into TemplateParams.
  if (TagTemplates)
    TemplateParams.clear();

  size_t ArgsBegin = Names.size();
  while (!consumeIf('E')) {
    if (TagTemplates) {
      auto OldParams = std::move(TemplateParams);
      Node *Arg = getDerived().parseTemplateArg();
      TemplateParams = std::move(OldParams);
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
      Node *TableEntry = Arg;
      if (Arg->getKind() == Node::KTemplateArgumentPack) {
        TableEntry = make<ParameterPack>(
            static_cast<TemplateArgumentPack*>(TableEntry)->getElements());
        if (!TableEntry)
          return nullptr;
      }
      TemplateParams.push_back(TableEntry);
    } else {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
    }
  }
  return make<TemplateArgs>(popTrailingNodeArray(ArgsBegin));
}

// <mangled-name> ::= _Z <encoding>
//                ::= <type>
// extension      ::= ___Z <encoding> _block_invoke
// extension      ::= ___Z <encoding> _block_invoke<decimal-digit>+
// extension      ::= ___Z <encoding> _block_invoke_<decimal-digit>+
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parse() {
  if (consumeIf("_Z")) {
    Node *Encoding = getDerived().parseEncoding();
    if (Encoding == nullptr)
      return nullptr;
    if (look() == '.') {
      Encoding = make<DotSuffix>(Encoding, StringView(First, Last));
      First = Last;
    }
    if (numLeft() != 0)
      return nullptr;
    return Encoding;
  }

  if (consumeIf("___Z")) {
    Node *Encoding = getDerived().parseEncoding();
    if (Encoding == nullptr || !consumeIf("_block_invoke"))
      return nullptr;
    bool RequireNumber = consumeIf('_');
    if (parseNumber().empty() && RequireNumber)
      return nullptr;
    if (look() == '.')
      First = Last;
    if (numLeft() != 0)
      return nullptr;
    return make<SpecialName>("invocation function for block in ", Encoding);
  }

  Node *Ty = getDerived().parseType();
  if (numLeft() != 0)
    return nullptr;
  return Ty;
}

template <typename Alloc>
struct ManglingParser : AbstractManglingParser<ManglingParser<Alloc>, Alloc> {
  using AbstractManglingParser<ManglingParser<Alloc>,
                               Alloc>::AbstractManglingParser;
};

}  // namespace itanium_demangle
}  // namespace llvm

#endif // LLVM_DEMANGLE_ITANIUMDEMANGLE_H
