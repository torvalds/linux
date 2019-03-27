//===- ASTMatchersInternal.h - Structural query framework -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Implements the base layer of the matcher framework.
//
//  Matchers are methods that return a Matcher<T> which provides a method
//  Matches(...) which is a predicate on an AST node. The Matches method's
//  parameters define the context of the match, which allows matchers to recurse
//  or store the current node as bound to a specific string, so that it can be
//  retrieved later.
//
//  In general, matchers have two parts:
//  1. A function Matcher<T> MatcherName(<arguments>) which returns a Matcher<T>
//     based on the arguments and optionally on template type deduction based
//     on the arguments. Matcher<T>s form an implicit reverse hierarchy
//     to clang's AST class hierarchy, meaning that you can use a Matcher<Base>
//     everywhere a Matcher<Derived> is required.
//  2. An implementation of a class derived from MatcherInterface<T>.
//
//  The matcher functions are defined in ASTMatchers.h. To make it possible
//  to implement both the matcher function and the implementation of the matcher
//  interface in one place, ASTMatcherMacros.h defines macros that allow
//  implementing a matcher in a single place.
//
//  This file contains the base classes needed to construct the actual matchers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ASTMATCHERS_ASTMATCHERSINTERNAL_H
#define LLVM_CLANG_ASTMATCHERS_ASTMATCHERSINTERNAL_H

#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ManagedStatic.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace clang {

class ASTContext;

namespace ast_matchers {

class BoundNodes;

namespace internal {

/// Variadic function object.
///
/// Most of the functions below that use VariadicFunction could be implemented
/// using plain C++11 variadic functions, but the function object allows us to
/// capture it on the dynamic matcher registry.
template <typename ResultT, typename ArgT,
          ResultT (*Func)(ArrayRef<const ArgT *>)>
struct VariadicFunction {
  ResultT operator()() const { return Func(None); }

  template <typename... ArgsT>
  ResultT operator()(const ArgT &Arg1, const ArgsT &... Args) const {
    return Execute(Arg1, static_cast<const ArgT &>(Args)...);
  }

  // We also allow calls with an already created array, in case the caller
  // already had it.
  ResultT operator()(ArrayRef<ArgT> Args) const {
    SmallVector<const ArgT*, 8> InnerArgs;
    for (const ArgT &Arg : Args)
      InnerArgs.push_back(&Arg);
    return Func(InnerArgs);
  }

private:
  // Trampoline function to allow for implicit conversions to take place
  // before we make the array.
  template <typename... ArgsT> ResultT Execute(const ArgsT &... Args) const {
    const ArgT *const ArgsArray[] = {&Args...};
    return Func(ArrayRef<const ArgT *>(ArgsArray, sizeof...(ArgsT)));
  }
};

/// Unifies obtaining the underlying type of a regular node through
/// `getType` and a TypedefNameDecl node through `getUnderlyingType`.
inline QualType getUnderlyingType(const Expr &Node) { return Node.getType(); }

inline QualType getUnderlyingType(const ValueDecl &Node) {
  return Node.getType();
}
inline QualType getUnderlyingType(const TypedefNameDecl &Node) {
  return Node.getUnderlyingType();
}
inline QualType getUnderlyingType(const FriendDecl &Node) {
  if (const TypeSourceInfo *TSI = Node.getFriendType())
    return TSI->getType();
  return QualType();
}

/// Unifies obtaining the FunctionProtoType pointer from both
/// FunctionProtoType and FunctionDecl nodes..
inline const FunctionProtoType *
getFunctionProtoType(const FunctionProtoType &Node) {
  return &Node;
}

inline const FunctionProtoType *getFunctionProtoType(const FunctionDecl &Node) {
  return Node.getType()->getAs<FunctionProtoType>();
}

/// Internal version of BoundNodes. Holds all the bound nodes.
class BoundNodesMap {
public:
  /// Adds \c Node to the map with key \c ID.
  ///
  /// The node's base type should be in NodeBaseType or it will be unaccessible.
  void addNode(StringRef ID, const ast_type_traits::DynTypedNode& DynNode) {
    NodeMap[ID] = DynNode;
  }

  /// Returns the AST node bound to \c ID.
  ///
  /// Returns NULL if there was no node bound to \c ID or if there is a node but
  /// it cannot be converted to the specified type.
  template <typename T>
  const T *getNodeAs(StringRef ID) const {
    IDToNodeMap::const_iterator It = NodeMap.find(ID);
    if (It == NodeMap.end()) {
      return nullptr;
    }
    return It->second.get<T>();
  }

  ast_type_traits::DynTypedNode getNode(StringRef ID) const {
    IDToNodeMap::const_iterator It = NodeMap.find(ID);
    if (It == NodeMap.end()) {
      return ast_type_traits::DynTypedNode();
    }
    return It->second;
  }

  /// Imposes an order on BoundNodesMaps.
  bool operator<(const BoundNodesMap &Other) const {
    return NodeMap < Other.NodeMap;
  }

  /// A map from IDs to the bound nodes.
  ///
  /// Note that we're using std::map here, as for memoization:
  /// - we need a comparison operator
  /// - we need an assignment operator
  using IDToNodeMap = std::map<std::string, ast_type_traits::DynTypedNode>;

  const IDToNodeMap &getMap() const {
    return NodeMap;
  }

  /// Returns \c true if this \c BoundNodesMap can be compared, i.e. all
  /// stored nodes have memoization data.
  bool isComparable() const {
    for (const auto &IDAndNode : NodeMap) {
      if (!IDAndNode.second.getMemoizationData())
        return false;
    }
    return true;
  }

private:
  IDToNodeMap NodeMap;
};

/// Creates BoundNodesTree objects.
///
/// The tree builder is used during the matching process to insert the bound
/// nodes from the Id matcher.
class BoundNodesTreeBuilder {
public:
  /// A visitor interface to visit all BoundNodes results for a
  /// BoundNodesTree.
  class Visitor {
  public:
    virtual ~Visitor() = default;

    /// Called multiple times during a single call to VisitMatches(...).
    ///
    /// 'BoundNodesView' contains the bound nodes for a single match.
    virtual void visitMatch(const BoundNodes& BoundNodesView) = 0;
  };

  /// Add a binding from an id to a node.
  void setBinding(StringRef Id, const ast_type_traits::DynTypedNode &DynNode) {
    if (Bindings.empty())
      Bindings.emplace_back();
    for (BoundNodesMap &Binding : Bindings)
      Binding.addNode(Id, DynNode);
  }

  /// Adds a branch in the tree.
  void addMatch(const BoundNodesTreeBuilder &Bindings);

  /// Visits all matches that this BoundNodesTree represents.
  ///
  /// The ownership of 'ResultVisitor' remains at the caller.
  void visitMatches(Visitor* ResultVisitor);

  template <typename ExcludePredicate>
  bool removeBindings(const ExcludePredicate &Predicate) {
    Bindings.erase(std::remove_if(Bindings.begin(), Bindings.end(), Predicate),
                   Bindings.end());
    return !Bindings.empty();
  }

  /// Imposes an order on BoundNodesTreeBuilders.
  bool operator<(const BoundNodesTreeBuilder &Other) const {
    return Bindings < Other.Bindings;
  }

  /// Returns \c true if this \c BoundNodesTreeBuilder can be compared,
  /// i.e. all stored node maps have memoization data.
  bool isComparable() const {
    for (const BoundNodesMap &NodesMap : Bindings) {
      if (!NodesMap.isComparable())
        return false;
    }
    return true;
  }

private:
  SmallVector<BoundNodesMap, 1> Bindings;
};

class ASTMatchFinder;

/// Generic interface for all matchers.
///
/// Used by the implementation of Matcher<T> and DynTypedMatcher.
/// In general, implement MatcherInterface<T> or SingleNodeMatcherInterface<T>
/// instead.
class DynMatcherInterface
    : public llvm::ThreadSafeRefCountedBase<DynMatcherInterface> {
public:
  virtual ~DynMatcherInterface() = default;

  /// Returns true if \p DynNode can be matched.
  ///
  /// May bind \p DynNode to an ID via \p Builder, or recurse into
  /// the AST via \p Finder.
  virtual bool dynMatches(const ast_type_traits::DynTypedNode &DynNode,
                          ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const = 0;
};

/// Generic interface for matchers on an AST node of type T.
///
/// Implement this if your matcher may need to inspect the children or
/// descendants of the node or bind matched nodes to names. If you are
/// writing a simple matcher that only inspects properties of the
/// current node and doesn't care about its children or descendants,
/// implement SingleNodeMatcherInterface instead.
template <typename T>
class MatcherInterface : public DynMatcherInterface {
public:
  /// Returns true if 'Node' can be matched.
  ///
  /// May bind 'Node' to an ID via 'Builder', or recurse into
  /// the AST via 'Finder'.
  virtual bool matches(const T &Node,
                       ASTMatchFinder *Finder,
                       BoundNodesTreeBuilder *Builder) const = 0;

  bool dynMatches(const ast_type_traits::DynTypedNode &DynNode,
                  ASTMatchFinder *Finder,
                  BoundNodesTreeBuilder *Builder) const override {
    return matches(DynNode.getUnchecked<T>(), Finder, Builder);
  }
};

/// Interface for matchers that only evaluate properties on a single
/// node.
template <typename T>
class SingleNodeMatcherInterface : public MatcherInterface<T> {
public:
  /// Returns true if the matcher matches the provided node.
  ///
  /// A subclass must implement this instead of Matches().
  virtual bool matchesNode(const T &Node) const = 0;

private:
  /// Implements MatcherInterface::Matches.
  bool matches(const T &Node,
               ASTMatchFinder * /* Finder */,
               BoundNodesTreeBuilder * /*  Builder */) const override {
    return matchesNode(Node);
  }
};

template <typename> class Matcher;

/// Matcher that works on a \c DynTypedNode.
///
/// It is constructed from a \c Matcher<T> object and redirects most calls to
/// underlying matcher.
/// It checks whether the \c DynTypedNode is convertible into the type of the
/// underlying matcher and then do the actual match on the actual node, or
/// return false if it is not convertible.
class DynTypedMatcher {
public:
  /// Takes ownership of the provided implementation pointer.
  template <typename T>
  DynTypedMatcher(MatcherInterface<T> *Implementation)
      : SupportedKind(ast_type_traits::ASTNodeKind::getFromNodeKind<T>()),
        RestrictKind(SupportedKind), Implementation(Implementation) {}

  /// Construct from a variadic function.
  enum VariadicOperator {
    /// Matches nodes for which all provided matchers match.
    VO_AllOf,

    /// Matches nodes for which at least one of the provided matchers
    /// matches.
    VO_AnyOf,

    /// Matches nodes for which at least one of the provided matchers
    /// matches, but doesn't stop at the first match.
    VO_EachOf,

    /// Matches nodes that do not match the provided matcher.
    ///
    /// Uses the variadic matcher interface, but fails if
    /// InnerMatchers.size() != 1.
    VO_UnaryNot
  };

  static DynTypedMatcher
  constructVariadic(VariadicOperator Op,
                    ast_type_traits::ASTNodeKind SupportedKind,
                    std::vector<DynTypedMatcher> InnerMatchers);

  /// Get a "true" matcher for \p NodeKind.
  ///
  /// It only checks that the node is of the right kind.
  static DynTypedMatcher trueMatcher(ast_type_traits::ASTNodeKind NodeKind);

  void setAllowBind(bool AB) { AllowBind = AB; }

  /// Check whether this matcher could ever match a node of kind \p Kind.
  /// \return \c false if this matcher will never match such a node. Otherwise,
  /// return \c true.
  bool canMatchNodesOfKind(ast_type_traits::ASTNodeKind Kind) const;

  /// Return a matcher that points to the same implementation, but
  ///   restricts the node types for \p Kind.
  DynTypedMatcher dynCastTo(const ast_type_traits::ASTNodeKind Kind) const;

  /// Returns true if the matcher matches the given \c DynNode.
  bool matches(const ast_type_traits::DynTypedNode &DynNode,
               ASTMatchFinder *Finder, BoundNodesTreeBuilder *Builder) const;

  /// Same as matches(), but skips the kind check.
  ///
  /// It is faster, but the caller must ensure the node is valid for the
  /// kind of this matcher.
  bool matchesNoKindCheck(const ast_type_traits::DynTypedNode &DynNode,
                          ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const;

  /// Bind the specified \p ID to the matcher.
  /// \return A new matcher with the \p ID bound to it if this matcher supports
  ///   binding. Otherwise, returns an empty \c Optional<>.
  llvm::Optional<DynTypedMatcher> tryBind(StringRef ID) const;

  /// Returns a unique \p ID for the matcher.
  ///
  /// Casting a Matcher<T> to Matcher<U> creates a matcher that has the
  /// same \c Implementation pointer, but different \c RestrictKind. We need to
  /// include both in the ID to make it unique.
  ///
  /// \c MatcherIDType supports operator< and provides strict weak ordering.
  using MatcherIDType = std::pair<ast_type_traits::ASTNodeKind, uint64_t>;
  MatcherIDType getID() const {
    /// FIXME: Document the requirements this imposes on matcher
    /// implementations (no new() implementation_ during a Matches()).
    return std::make_pair(RestrictKind,
                          reinterpret_cast<uint64_t>(Implementation.get()));
  }

  /// Returns the type this matcher works on.
  ///
  /// \c matches() will always return false unless the node passed is of this
  /// or a derived type.
  ast_type_traits::ASTNodeKind getSupportedKind() const {
    return SupportedKind;
  }

  /// Returns \c true if the passed \c DynTypedMatcher can be converted
  ///   to a \c Matcher<T>.
  ///
  /// This method verifies that the underlying matcher in \c Other can process
  /// nodes of types T.
  template <typename T> bool canConvertTo() const {
    return canConvertTo(ast_type_traits::ASTNodeKind::getFromNodeKind<T>());
  }
  bool canConvertTo(ast_type_traits::ASTNodeKind To) const;

  /// Construct a \c Matcher<T> interface around the dynamic matcher.
  ///
  /// This method asserts that \c canConvertTo() is \c true. Callers
  /// should call \c canConvertTo() first to make sure that \c this is
  /// compatible with T.
  template <typename T> Matcher<T> convertTo() const {
    assert(canConvertTo<T>());
    return unconditionalConvertTo<T>();
  }

  /// Same as \c convertTo(), but does not check that the underlying
  ///   matcher can handle a value of T.
  ///
  /// If it is not compatible, then this matcher will never match anything.
  template <typename T> Matcher<T> unconditionalConvertTo() const;

private:
 DynTypedMatcher(ast_type_traits::ASTNodeKind SupportedKind,
                 ast_type_traits::ASTNodeKind RestrictKind,
                 IntrusiveRefCntPtr<DynMatcherInterface> Implementation)
     : SupportedKind(SupportedKind), RestrictKind(RestrictKind),
       Implementation(std::move(Implementation)) {}

  bool AllowBind = false;
  ast_type_traits::ASTNodeKind SupportedKind;

  /// A potentially stricter node kind.
  ///
  /// It allows to perform implicit and dynamic cast of matchers without
  /// needing to change \c Implementation.
  ast_type_traits::ASTNodeKind RestrictKind;
  IntrusiveRefCntPtr<DynMatcherInterface> Implementation;
};

/// Wrapper base class for a wrapping matcher.
///
/// This is just a container for a DynTypedMatcher that can be used as a base
/// class for another matcher.
template <typename T>
class WrapperMatcherInterface : public MatcherInterface<T> {
protected:
  explicit WrapperMatcherInterface(DynTypedMatcher &&InnerMatcher)
      : InnerMatcher(std::move(InnerMatcher)) {}

  const DynTypedMatcher InnerMatcher;
};

/// Wrapper of a MatcherInterface<T> *that allows copying.
///
/// A Matcher<Base> can be used anywhere a Matcher<Derived> is
/// required. This establishes an is-a relationship which is reverse
/// to the AST hierarchy. In other words, Matcher<T> is contravariant
/// with respect to T. The relationship is built via a type conversion
/// operator rather than a type hierarchy to be able to templatize the
/// type hierarchy instead of spelling it out.
template <typename T>
class Matcher {
public:
  /// Takes ownership of the provided implementation pointer.
  explicit Matcher(MatcherInterface<T> *Implementation)
      : Implementation(Implementation) {}

  /// Implicitly converts \c Other to a Matcher<T>.
  ///
  /// Requires \c T to be derived from \c From.
  template <typename From>
  Matcher(const Matcher<From> &Other,
          typename std::enable_if<std::is_base_of<From, T>::value &&
                               !std::is_same<From, T>::value>::type * = nullptr)
      : Implementation(restrictMatcher(Other.Implementation)) {
    assert(Implementation.getSupportedKind().isSame(
        ast_type_traits::ASTNodeKind::getFromNodeKind<T>()));
  }

  /// Implicitly converts \c Matcher<Type> to \c Matcher<QualType>.
  ///
  /// The resulting matcher is not strict, i.e. ignores qualifiers.
  template <typename TypeT>
  Matcher(const Matcher<TypeT> &Other,
          typename std::enable_if<
            std::is_same<T, QualType>::value &&
            std::is_same<TypeT, Type>::value>::type* = nullptr)
      : Implementation(new TypeToQualType<TypeT>(Other)) {}

  /// Convert \c this into a \c Matcher<T> by applying dyn_cast<> to the
  /// argument.
  /// \c To must be a base class of \c T.
  template <typename To>
  Matcher<To> dynCastTo() const {
    static_assert(std::is_base_of<To, T>::value, "Invalid dynCast call.");
    return Matcher<To>(Implementation);
  }

  /// Forwards the call to the underlying MatcherInterface<T> pointer.
  bool matches(const T &Node,
               ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const {
    return Implementation.matches(ast_type_traits::DynTypedNode::create(Node),
                                  Finder, Builder);
  }

  /// Returns an ID that uniquely identifies the matcher.
  DynTypedMatcher::MatcherIDType getID() const {
    return Implementation.getID();
  }

  /// Extract the dynamic matcher.
  ///
  /// The returned matcher keeps the same restrictions as \c this and remembers
  /// that it is meant to support nodes of type \c T.
  operator DynTypedMatcher() const { return Implementation; }

  /// Allows the conversion of a \c Matcher<Type> to a \c
  /// Matcher<QualType>.
  ///
  /// Depending on the constructor argument, the matcher is either strict, i.e.
  /// does only matches in the absence of qualifiers, or not, i.e. simply
  /// ignores any qualifiers.
  template <typename TypeT>
  class TypeToQualType : public WrapperMatcherInterface<QualType> {
  public:
    TypeToQualType(const Matcher<TypeT> &InnerMatcher)
        : TypeToQualType::WrapperMatcherInterface(InnerMatcher) {}

    bool matches(const QualType &Node, ASTMatchFinder *Finder,
                 BoundNodesTreeBuilder *Builder) const override {
      if (Node.isNull())
        return false;
      return this->InnerMatcher.matches(
          ast_type_traits::DynTypedNode::create(*Node), Finder, Builder);
    }
  };

private:
  // For Matcher<T> <=> Matcher<U> conversions.
  template <typename U> friend class Matcher;

  // For DynTypedMatcher::unconditionalConvertTo<T>.
  friend class DynTypedMatcher;

  static DynTypedMatcher restrictMatcher(const DynTypedMatcher &Other) {
    return Other.dynCastTo(ast_type_traits::ASTNodeKind::getFromNodeKind<T>());
  }

  explicit Matcher(const DynTypedMatcher &Implementation)
      : Implementation(restrictMatcher(Implementation)) {
    assert(this->Implementation.getSupportedKind()
               .isSame(ast_type_traits::ASTNodeKind::getFromNodeKind<T>()));
  }

  DynTypedMatcher Implementation;
};  // class Matcher

/// A convenient helper for creating a Matcher<T> without specifying
/// the template type argument.
template <typename T>
inline Matcher<T> makeMatcher(MatcherInterface<T> *Implementation) {
  return Matcher<T>(Implementation);
}

/// Specialization of the conversion functions for QualType.
///
/// This specialization provides the Matcher<Type>->Matcher<QualType>
/// conversion that the static API does.
template <>
inline Matcher<QualType> DynTypedMatcher::convertTo<QualType>() const {
  assert(canConvertTo<QualType>());
  const ast_type_traits::ASTNodeKind SourceKind = getSupportedKind();
  if (SourceKind.isSame(
          ast_type_traits::ASTNodeKind::getFromNodeKind<Type>())) {
    // We support implicit conversion from Matcher<Type> to Matcher<QualType>
    return unconditionalConvertTo<Type>();
  }
  return unconditionalConvertTo<QualType>();
}

/// Finds the first node in a range that matches the given matcher.
template <typename MatcherT, typename IteratorT>
bool matchesFirstInRange(const MatcherT &Matcher, IteratorT Start,
                         IteratorT End, ASTMatchFinder *Finder,
                         BoundNodesTreeBuilder *Builder) {
  for (IteratorT I = Start; I != End; ++I) {
    BoundNodesTreeBuilder Result(*Builder);
    if (Matcher.matches(*I, Finder, &Result)) {
      *Builder = std::move(Result);
      return true;
    }
  }
  return false;
}

/// Finds the first node in a pointer range that matches the given
/// matcher.
template <typename MatcherT, typename IteratorT>
bool matchesFirstInPointerRange(const MatcherT &Matcher, IteratorT Start,
                                IteratorT End, ASTMatchFinder *Finder,
                                BoundNodesTreeBuilder *Builder) {
  for (IteratorT I = Start; I != End; ++I) {
    BoundNodesTreeBuilder Result(*Builder);
    if (Matcher.matches(**I, Finder, &Result)) {
      *Builder = std::move(Result);
      return true;
    }
  }
  return false;
}

// Metafunction to determine if type T has a member called getDecl.
template <typename Ty>
class has_getDecl {
  using yes = char[1];
  using no = char[2];

  template <typename Inner>
  static yes& test(Inner *I, decltype(I->getDecl()) * = nullptr);

  template <typename>
  static no& test(...);

public:
  static const bool value = sizeof(test<Ty>(nullptr)) == sizeof(yes);
};

/// Matches overloaded operators with a specific name.
///
/// The type argument ArgT is not used by this matcher but is used by
/// PolymorphicMatcherWithParam1 and should be StringRef.
template <typename T, typename ArgT>
class HasOverloadedOperatorNameMatcher : public SingleNodeMatcherInterface<T> {
  static_assert(std::is_same<T, CXXOperatorCallExpr>::value ||
                std::is_base_of<FunctionDecl, T>::value,
                "unsupported class for matcher");
  static_assert(std::is_same<ArgT, StringRef>::value,
                "argument type must be StringRef");

public:
  explicit HasOverloadedOperatorNameMatcher(const StringRef Name)
      : SingleNodeMatcherInterface<T>(), Name(Name) {}

  bool matchesNode(const T &Node) const override {
    return matchesSpecialized(Node);
  }

private:

  /// CXXOperatorCallExpr exist only for calls to overloaded operators
  /// so this function returns true if the call is to an operator of the given
  /// name.
  bool matchesSpecialized(const CXXOperatorCallExpr &Node) const {
    return getOperatorSpelling(Node.getOperator()) == Name;
  }

  /// Returns true only if CXXMethodDecl represents an overloaded
  /// operator and has the given operator name.
  bool matchesSpecialized(const FunctionDecl &Node) const {
    return Node.isOverloadedOperator() &&
           getOperatorSpelling(Node.getOverloadedOperator()) == Name;
  }

  std::string Name;
};

/// Matches named declarations with a specific name.
///
/// See \c hasName() and \c hasAnyName() in ASTMatchers.h for details.
class HasNameMatcher : public SingleNodeMatcherInterface<NamedDecl> {
 public:
  explicit HasNameMatcher(std::vector<std::string> Names);

  bool matchesNode(const NamedDecl &Node) const override;

 private:
  /// Unqualified match routine.
  ///
  /// It is much faster than the full match, but it only works for unqualified
  /// matches.
  bool matchesNodeUnqualified(const NamedDecl &Node) const;

  /// Full match routine
  ///
  /// Fast implementation for the simple case of a named declaration at
  /// namespace or RecordDecl scope.
  /// It is slower than matchesNodeUnqualified, but faster than
  /// matchesNodeFullSlow.
  bool matchesNodeFullFast(const NamedDecl &Node) const;

  /// Full match routine
  ///
  /// It generates the fully qualified name of the declaration (which is
  /// expensive) before trying to match.
  /// It is slower but simple and works on all cases.
  bool matchesNodeFullSlow(const NamedDecl &Node) const;

  const bool UseUnqualifiedMatch;
  const std::vector<std::string> Names;
};

/// Trampoline function to use VariadicFunction<> to construct a
///        HasNameMatcher.
Matcher<NamedDecl> hasAnyNameFunc(ArrayRef<const StringRef *> NameRefs);

/// Trampoline function to use VariadicFunction<> to construct a
///        hasAnySelector matcher.
Matcher<ObjCMessageExpr> hasAnySelectorFunc(
    ArrayRef<const StringRef *> NameRefs);

/// Matches declarations for QualType and CallExpr.
///
/// Type argument DeclMatcherT is required by PolymorphicMatcherWithParam1 but
/// not actually used.
template <typename T, typename DeclMatcherT>
class HasDeclarationMatcher : public WrapperMatcherInterface<T> {
  static_assert(std::is_same<DeclMatcherT, Matcher<Decl>>::value,
                "instantiated with wrong types");

public:
  explicit HasDeclarationMatcher(const Matcher<Decl> &InnerMatcher)
      : HasDeclarationMatcher::WrapperMatcherInterface(InnerMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return matchesSpecialized(Node, Finder, Builder);
  }

private:
  /// Forwards to matching on the underlying type of the QualType.
  bool matchesSpecialized(const QualType &Node, ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    if (Node.isNull())
      return false;

    return matchesSpecialized(*Node, Finder, Builder);
  }

  /// Finds the best declaration for a type and returns whether the inner
  /// matcher matches on it.
  bool matchesSpecialized(const Type &Node, ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    // DeducedType does not have declarations of its own, so
    // match the deduced type instead.
    const Type *EffectiveType = &Node;
    if (const auto *S = dyn_cast<DeducedType>(&Node)) {
      EffectiveType = S->getDeducedType().getTypePtrOrNull();
      if (!EffectiveType)
        return false;
    }

    // First, for any types that have a declaration, extract the declaration and
    // match on it.
    if (const auto *S = dyn_cast<TagType>(EffectiveType)) {
      return matchesDecl(S->getDecl(), Finder, Builder);
    }
    if (const auto *S = dyn_cast<InjectedClassNameType>(EffectiveType)) {
      return matchesDecl(S->getDecl(), Finder, Builder);
    }
    if (const auto *S = dyn_cast<TemplateTypeParmType>(EffectiveType)) {
      return matchesDecl(S->getDecl(), Finder, Builder);
    }
    if (const auto *S = dyn_cast<TypedefType>(EffectiveType)) {
      return matchesDecl(S->getDecl(), Finder, Builder);
    }
    if (const auto *S = dyn_cast<UnresolvedUsingType>(EffectiveType)) {
      return matchesDecl(S->getDecl(), Finder, Builder);
    }
    if (const auto *S = dyn_cast<ObjCObjectType>(EffectiveType)) {
      return matchesDecl(S->getInterface(), Finder, Builder);
    }

    // A SubstTemplateTypeParmType exists solely to mark a type substitution
    // on the instantiated template. As users usually want to match the
    // template parameter on the uninitialized template, we can always desugar
    // one level without loss of expressivness.
    // For example, given:
    //   template<typename T> struct X { T t; } class A {}; X<A> a;
    // The following matcher will match, which otherwise would not:
    //   fieldDecl(hasType(pointerType())).
    if (const auto *S = dyn_cast<SubstTemplateTypeParmType>(EffectiveType)) {
      return matchesSpecialized(S->getReplacementType(), Finder, Builder);
    }

    // For template specialization types, we want to match the template
    // declaration, as long as the type is still dependent, and otherwise the
    // declaration of the instantiated tag type.
    if (const auto *S = dyn_cast<TemplateSpecializationType>(EffectiveType)) {
      if (!S->isTypeAlias() && S->isSugared()) {
        // If the template is non-dependent, we want to match the instantiated
        // tag type.
        // For example, given:
        //   template<typename T> struct X {}; X<int> a;
        // The following matcher will match, which otherwise would not:
        //   templateSpecializationType(hasDeclaration(cxxRecordDecl())).
        return matchesSpecialized(*S->desugar(), Finder, Builder);
      }
      // If the template is dependent or an alias, match the template
      // declaration.
      return matchesDecl(S->getTemplateName().getAsTemplateDecl(), Finder,
                         Builder);
    }

    // FIXME: We desugar elaborated types. This makes the assumption that users
    // do never want to match on whether a type is elaborated - there are
    // arguments for both sides; for now, continue desugaring.
    if (const auto *S = dyn_cast<ElaboratedType>(EffectiveType)) {
      return matchesSpecialized(S->desugar(), Finder, Builder);
    }
    return false;
  }

  /// Extracts the Decl the DeclRefExpr references and returns whether
  /// the inner matcher matches on it.
  bool matchesSpecialized(const DeclRefExpr &Node, ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getDecl(), Finder, Builder);
  }

  /// Extracts the Decl of the callee of a CallExpr and returns whether
  /// the inner matcher matches on it.
  bool matchesSpecialized(const CallExpr &Node, ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getCalleeDecl(), Finder, Builder);
  }

  /// Extracts the Decl of the constructor call and returns whether the
  /// inner matcher matches on it.
  bool matchesSpecialized(const CXXConstructExpr &Node,
                          ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getConstructor(), Finder, Builder);
  }

  bool matchesSpecialized(const ObjCIvarRefExpr &Node,
                          ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getDecl(), Finder, Builder);
  }

  /// Extracts the operator new of the new call and returns whether the
  /// inner matcher matches on it.
  bool matchesSpecialized(const CXXNewExpr &Node,
                          ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getOperatorNew(), Finder, Builder);
  }

  /// Extracts the \c ValueDecl a \c MemberExpr refers to and returns
  /// whether the inner matcher matches on it.
  bool matchesSpecialized(const MemberExpr &Node,
                          ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getMemberDecl(), Finder, Builder);
  }

  /// Extracts the \c LabelDecl a \c AddrLabelExpr refers to and returns
  /// whether the inner matcher matches on it.
  bool matchesSpecialized(const AddrLabelExpr &Node,
                          ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getLabel(), Finder, Builder);
  }

  /// Extracts the declaration of a LabelStmt and returns whether the
  /// inner matcher matches on it.
  bool matchesSpecialized(const LabelStmt &Node, ASTMatchFinder *Finder,
                          BoundNodesTreeBuilder *Builder) const {
    return matchesDecl(Node.getDecl(), Finder, Builder);
  }

  /// Returns whether the inner matcher \c Node. Returns false if \c Node
  /// is \c NULL.
  bool matchesDecl(const Decl *Node, ASTMatchFinder *Finder,
                   BoundNodesTreeBuilder *Builder) const {
    return Node != nullptr &&
           this->InnerMatcher.matches(
               ast_type_traits::DynTypedNode::create(*Node), Finder, Builder);
  }
};

/// IsBaseType<T>::value is true if T is a "base" type in the AST
/// node class hierarchies.
template <typename T>
struct IsBaseType {
  static const bool value =
      std::is_same<T, Decl>::value ||
      std::is_same<T, Stmt>::value ||
      std::is_same<T, QualType>::value ||
      std::is_same<T, Type>::value ||
      std::is_same<T, TypeLoc>::value ||
      std::is_same<T, NestedNameSpecifier>::value ||
      std::is_same<T, NestedNameSpecifierLoc>::value ||
      std::is_same<T, CXXCtorInitializer>::value;
};
template <typename T>
const bool IsBaseType<T>::value;

/// Interface that allows matchers to traverse the AST.
/// FIXME: Find a better name.
///
/// This provides three entry methods for each base node type in the AST:
/// - \c matchesChildOf:
///   Matches a matcher on every child node of the given node. Returns true
///   if at least one child node could be matched.
/// - \c matchesDescendantOf:
///   Matches a matcher on all descendant nodes of the given node. Returns true
///   if at least one descendant matched.
/// - \c matchesAncestorOf:
///   Matches a matcher on all ancestors of the given node. Returns true if
///   at least one ancestor matched.
///
/// FIXME: Currently we only allow Stmt and Decl nodes to start a traversal.
/// In the future, we want to implement this for all nodes for which it makes
/// sense. In the case of matchesAncestorOf, we'll want to implement it for
/// all nodes, as all nodes have ancestors.
class ASTMatchFinder {
public:
  /// Defines how we descend a level in the AST when we pass
  /// through expressions.
  enum TraversalKind {
    /// Will traverse any child nodes.
    TK_AsIs,

    /// Will not traverse implicit casts and parentheses.
    TK_IgnoreImplicitCastsAndParentheses
  };

  /// Defines how bindings are processed on recursive matches.
  enum BindKind {
    /// Stop at the first match and only bind the first match.
    BK_First,

    /// Create results for all combinations of bindings that match.
    BK_All
  };

  /// Defines which ancestors are considered for a match.
  enum AncestorMatchMode {
    /// All ancestors.
    AMM_All,

    /// Direct parent only.
    AMM_ParentOnly
  };

  virtual ~ASTMatchFinder() = default;

  /// Returns true if the given class is directly or indirectly derived
  /// from a base type matching \c base.
  ///
  /// A class is considered to be also derived from itself.
  virtual bool classIsDerivedFrom(const CXXRecordDecl *Declaration,
                                  const Matcher<NamedDecl> &Base,
                                  BoundNodesTreeBuilder *Builder) = 0;

  template <typename T>
  bool matchesChildOf(const T &Node,
                      const DynTypedMatcher &Matcher,
                      BoundNodesTreeBuilder *Builder,
                      TraversalKind Traverse,
                      BindKind Bind) {
    static_assert(std::is_base_of<Decl, T>::value ||
                  std::is_base_of<Stmt, T>::value ||
                  std::is_base_of<NestedNameSpecifier, T>::value ||
                  std::is_base_of<NestedNameSpecifierLoc, T>::value ||
                  std::is_base_of<TypeLoc, T>::value ||
                  std::is_base_of<QualType, T>::value,
                  "unsupported type for recursive matching");
    return matchesChildOf(ast_type_traits::DynTypedNode::create(Node),
                          Matcher, Builder, Traverse, Bind);
  }

  template <typename T>
  bool matchesDescendantOf(const T &Node,
                           const DynTypedMatcher &Matcher,
                           BoundNodesTreeBuilder *Builder,
                           BindKind Bind) {
    static_assert(std::is_base_of<Decl, T>::value ||
                  std::is_base_of<Stmt, T>::value ||
                  std::is_base_of<NestedNameSpecifier, T>::value ||
                  std::is_base_of<NestedNameSpecifierLoc, T>::value ||
                  std::is_base_of<TypeLoc, T>::value ||
                  std::is_base_of<QualType, T>::value,
                  "unsupported type for recursive matching");
    return matchesDescendantOf(ast_type_traits::DynTypedNode::create(Node),
                               Matcher, Builder, Bind);
  }

  // FIXME: Implement support for BindKind.
  template <typename T>
  bool matchesAncestorOf(const T &Node,
                         const DynTypedMatcher &Matcher,
                         BoundNodesTreeBuilder *Builder,
                         AncestorMatchMode MatchMode) {
    static_assert(std::is_base_of<Decl, T>::value ||
                      std::is_base_of<NestedNameSpecifierLoc, T>::value ||
                      std::is_base_of<Stmt, T>::value ||
                      std::is_base_of<TypeLoc, T>::value,
                  "type not allowed for recursive matching");
    return matchesAncestorOf(ast_type_traits::DynTypedNode::create(Node),
                             Matcher, Builder, MatchMode);
  }

  virtual ASTContext &getASTContext() const = 0;

protected:
  virtual bool matchesChildOf(const ast_type_traits::DynTypedNode &Node,
                              const DynTypedMatcher &Matcher,
                              BoundNodesTreeBuilder *Builder,
                              TraversalKind Traverse,
                              BindKind Bind) = 0;

  virtual bool matchesDescendantOf(const ast_type_traits::DynTypedNode &Node,
                                   const DynTypedMatcher &Matcher,
                                   BoundNodesTreeBuilder *Builder,
                                   BindKind Bind) = 0;

  virtual bool matchesAncestorOf(const ast_type_traits::DynTypedNode &Node,
                                 const DynTypedMatcher &Matcher,
                                 BoundNodesTreeBuilder *Builder,
                                 AncestorMatchMode MatchMode) = 0;
};

/// A type-list implementation.
///
/// A "linked list" of types, accessible by using the ::head and ::tail
/// typedefs.
template <typename... Ts> struct TypeList {}; // Empty sentinel type list.

template <typename T1, typename... Ts> struct TypeList<T1, Ts...> {
  /// The first type on the list.
  using head = T1;

  /// A sublist with the tail. ie everything but the head.
  ///
  /// This type is used to do recursion. TypeList<>/EmptyTypeList indicates the
  /// end of the list.
  using tail = TypeList<Ts...>;
};

/// The empty type list.
using EmptyTypeList = TypeList<>;

/// Helper meta-function to determine if some type \c T is present or
///   a parent type in the list.
template <typename AnyTypeList, typename T>
struct TypeListContainsSuperOf {
  static const bool value =
      std::is_base_of<typename AnyTypeList::head, T>::value ||
      TypeListContainsSuperOf<typename AnyTypeList::tail, T>::value;
};
template <typename T>
struct TypeListContainsSuperOf<EmptyTypeList, T> {
  static const bool value = false;
};

/// A "type list" that contains all types.
///
/// Useful for matchers like \c anything and \c unless.
using AllNodeBaseTypes =
    TypeList<Decl, Stmt, NestedNameSpecifier, NestedNameSpecifierLoc, QualType,
             Type, TypeLoc, CXXCtorInitializer>;

/// Helper meta-function to extract the argument out of a function of
///   type void(Arg).
///
/// See AST_POLYMORPHIC_SUPPORTED_TYPES for details.
template <class T> struct ExtractFunctionArgMeta;
template <class T> struct ExtractFunctionArgMeta<void(T)> {
  using type = T;
};

/// Default type lists for ArgumentAdaptingMatcher matchers.
using AdaptativeDefaultFromTypes = AllNodeBaseTypes;
using AdaptativeDefaultToTypes =
    TypeList<Decl, Stmt, NestedNameSpecifier, NestedNameSpecifierLoc, TypeLoc,
             QualType>;

/// All types that are supported by HasDeclarationMatcher above.
using HasDeclarationSupportedTypes =
    TypeList<CallExpr, CXXConstructExpr, CXXNewExpr, DeclRefExpr, EnumType,
             ElaboratedType, InjectedClassNameType, LabelStmt, AddrLabelExpr,
             MemberExpr, QualType, RecordType, TagType,
             TemplateSpecializationType, TemplateTypeParmType, TypedefType,
             UnresolvedUsingType, ObjCIvarRefExpr>;

/// Converts a \c Matcher<T> to a matcher of desired type \c To by
/// "adapting" a \c To into a \c T.
///
/// The \c ArgumentAdapterT argument specifies how the adaptation is done.
///
/// For example:
///   \c ArgumentAdaptingMatcher<HasMatcher, T>(InnerMatcher);
/// Given that \c InnerMatcher is of type \c Matcher<T>, this returns a matcher
/// that is convertible into any matcher of type \c To by constructing
/// \c HasMatcher<To, T>(InnerMatcher).
///
/// If a matcher does not need knowledge about the inner type, prefer to use
/// PolymorphicMatcherWithParam1.
template <template <typename ToArg, typename FromArg> class ArgumentAdapterT,
          typename FromTypes = AdaptativeDefaultFromTypes,
          typename ToTypes = AdaptativeDefaultToTypes>
struct ArgumentAdaptingMatcherFunc {
  template <typename T> class Adaptor {
  public:
    explicit Adaptor(const Matcher<T> &InnerMatcher)
        : InnerMatcher(InnerMatcher) {}

    using ReturnTypes = ToTypes;

    template <typename To> operator Matcher<To>() const {
      return Matcher<To>(new ArgumentAdapterT<To, T>(InnerMatcher));
    }

  private:
    const Matcher<T> InnerMatcher;
  };

  template <typename T>
  static Adaptor<T> create(const Matcher<T> &InnerMatcher) {
    return Adaptor<T>(InnerMatcher);
  }

  template <typename T>
  Adaptor<T> operator()(const Matcher<T> &InnerMatcher) const {
    return create(InnerMatcher);
  }
};

/// A PolymorphicMatcherWithParamN<MatcherT, P1, ..., PN> object can be
/// created from N parameters p1, ..., pN (of type P1, ..., PN) and
/// used as a Matcher<T> where a MatcherT<T, P1, ..., PN>(p1, ..., pN)
/// can be constructed.
///
/// For example:
/// - PolymorphicMatcherWithParam0<IsDefinitionMatcher>()
///   creates an object that can be used as a Matcher<T> for any type T
///   where an IsDefinitionMatcher<T>() can be constructed.
/// - PolymorphicMatcherWithParam1<ValueEqualsMatcher, int>(42)
///   creates an object that can be used as a Matcher<T> for any type T
///   where a ValueEqualsMatcher<T, int>(42) can be constructed.
template <template <typename T> class MatcherT,
          typename ReturnTypesF = void(AllNodeBaseTypes)>
class PolymorphicMatcherWithParam0 {
public:
  using ReturnTypes = typename ExtractFunctionArgMeta<ReturnTypesF>::type;

  template <typename T>
  operator Matcher<T>() const {
    static_assert(TypeListContainsSuperOf<ReturnTypes, T>::value,
                  "right polymorphic conversion");
    return Matcher<T>(new MatcherT<T>());
  }
};

template <template <typename T, typename P1> class MatcherT,
          typename P1,
          typename ReturnTypesF = void(AllNodeBaseTypes)>
class PolymorphicMatcherWithParam1 {
public:
  explicit PolymorphicMatcherWithParam1(const P1 &Param1)
      : Param1(Param1) {}

  using ReturnTypes = typename ExtractFunctionArgMeta<ReturnTypesF>::type;

  template <typename T>
  operator Matcher<T>() const {
    static_assert(TypeListContainsSuperOf<ReturnTypes, T>::value,
                  "right polymorphic conversion");
    return Matcher<T>(new MatcherT<T, P1>(Param1));
  }

private:
  const P1 Param1;
};

template <template <typename T, typename P1, typename P2> class MatcherT,
          typename P1, typename P2,
          typename ReturnTypesF = void(AllNodeBaseTypes)>
class PolymorphicMatcherWithParam2 {
public:
  PolymorphicMatcherWithParam2(const P1 &Param1, const P2 &Param2)
      : Param1(Param1), Param2(Param2) {}

  using ReturnTypes = typename ExtractFunctionArgMeta<ReturnTypesF>::type;

  template <typename T>
  operator Matcher<T>() const {
    static_assert(TypeListContainsSuperOf<ReturnTypes, T>::value,
                  "right polymorphic conversion");
    return Matcher<T>(new MatcherT<T, P1, P2>(Param1, Param2));
  }

private:
  const P1 Param1;
  const P2 Param2;
};

/// Matches any instance of the given NodeType.
///
/// This is useful when a matcher syntactically requires a child matcher,
/// but the context doesn't care. See for example: anything().
class TrueMatcher {
public:
  using ReturnTypes = AllNodeBaseTypes;

  template <typename T>
  operator Matcher<T>() const {
    return DynTypedMatcher::trueMatcher(
               ast_type_traits::ASTNodeKind::getFromNodeKind<T>())
        .template unconditionalConvertTo<T>();
  }
};

/// A Matcher that allows binding the node it matches to an id.
///
/// BindableMatcher provides a \a bind() method that allows binding the
/// matched node to an id if the match was successful.
template <typename T>
class BindableMatcher : public Matcher<T> {
public:
  explicit BindableMatcher(const Matcher<T> &M) : Matcher<T>(M) {}
  explicit BindableMatcher(MatcherInterface<T> *Implementation)
    : Matcher<T>(Implementation) {}

  /// Returns a matcher that will bind the matched node on a match.
  ///
  /// The returned matcher is equivalent to this matcher, but will
  /// bind the matched node on a match.
  Matcher<T> bind(StringRef ID) const {
    return DynTypedMatcher(*this)
        .tryBind(ID)
        ->template unconditionalConvertTo<T>();
  }

  /// Same as Matcher<T>'s conversion operator, but enables binding on
  /// the returned matcher.
  operator DynTypedMatcher() const {
    DynTypedMatcher Result = static_cast<const Matcher<T>&>(*this);
    Result.setAllowBind(true);
    return Result;
  }
};

/// Matches nodes of type T that have child nodes of type ChildT for
/// which a specified child matcher matches.
///
/// ChildT must be an AST base type.
template <typename T, typename ChildT>
class HasMatcher : public WrapperMatcherInterface<T> {
public:
  explicit HasMatcher(const Matcher<ChildT> &ChildMatcher)
      : HasMatcher::WrapperMatcherInterface(ChildMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return Finder->matchesChildOf(Node, this->InnerMatcher, Builder,
                                  ASTMatchFinder::TK_AsIs,
                                  ASTMatchFinder::BK_First);
  }
};

/// Matches nodes of type T that have child nodes of type ChildT for
/// which a specified child matcher matches. ChildT must be an AST base
/// type.
/// As opposed to the HasMatcher, the ForEachMatcher will produce a match
/// for each child that matches.
template <typename T, typename ChildT>
class ForEachMatcher : public WrapperMatcherInterface<T> {
  static_assert(IsBaseType<ChildT>::value,
                "for each only accepts base type matcher");

 public:
   explicit ForEachMatcher(const Matcher<ChildT> &ChildMatcher)
       : ForEachMatcher::WrapperMatcherInterface(ChildMatcher) {}

  bool matches(const T& Node, ASTMatchFinder* Finder,
               BoundNodesTreeBuilder* Builder) const override {
    return Finder->matchesChildOf(
        Node, this->InnerMatcher, Builder,
        ASTMatchFinder::TK_IgnoreImplicitCastsAndParentheses,
        ASTMatchFinder::BK_All);
  }
};

/// VariadicOperatorMatcher related types.
/// @{

/// Polymorphic matcher object that uses a \c
/// DynTypedMatcher::VariadicOperator operator.
///
/// Input matchers can have any type (including other polymorphic matcher
/// types), and the actual Matcher<T> is generated on demand with an implicit
/// coversion operator.
template <typename... Ps> class VariadicOperatorMatcher {
public:
  VariadicOperatorMatcher(DynTypedMatcher::VariadicOperator Op, Ps &&... Params)
      : Op(Op), Params(std::forward<Ps>(Params)...) {}

  template <typename T> operator Matcher<T>() const {
    return DynTypedMatcher::constructVariadic(
               Op, ast_type_traits::ASTNodeKind::getFromNodeKind<T>(),
               getMatchers<T>(llvm::index_sequence_for<Ps...>()))
        .template unconditionalConvertTo<T>();
  }

private:
  // Helper method to unpack the tuple into a vector.
  template <typename T, std::size_t... Is>
  std::vector<DynTypedMatcher> getMatchers(llvm::index_sequence<Is...>) const {
    return {Matcher<T>(std::get<Is>(Params))...};
  }

  const DynTypedMatcher::VariadicOperator Op;
  std::tuple<Ps...> Params;
};

/// Overloaded function object to generate VariadicOperatorMatcher
///   objects from arbitrary matchers.
template <unsigned MinCount, unsigned MaxCount>
struct VariadicOperatorMatcherFunc {
  DynTypedMatcher::VariadicOperator Op;

  template <typename... Ms>
  VariadicOperatorMatcher<Ms...> operator()(Ms &&... Ps) const {
    static_assert(MinCount <= sizeof...(Ms) && sizeof...(Ms) <= MaxCount,
                  "invalid number of parameters for variadic matcher");
    return VariadicOperatorMatcher<Ms...>(Op, std::forward<Ms>(Ps)...);
  }
};

/// @}

template <typename T>
inline Matcher<T> DynTypedMatcher::unconditionalConvertTo() const {
  return Matcher<T>(*this);
}

/// Creates a Matcher<T> that matches if all inner matchers match.
template<typename T>
BindableMatcher<T> makeAllOfComposite(
    ArrayRef<const Matcher<T> *> InnerMatchers) {
  // For the size() == 0 case, we return a "true" matcher.
  if (InnerMatchers.empty()) {
    return BindableMatcher<T>(TrueMatcher());
  }
  // For the size() == 1 case, we simply return that one matcher.
  // No need to wrap it in a variadic operation.
  if (InnerMatchers.size() == 1) {
    return BindableMatcher<T>(*InnerMatchers[0]);
  }

  using PI = llvm::pointee_iterator<const Matcher<T> *const *>;

  std::vector<DynTypedMatcher> DynMatchers(PI(InnerMatchers.begin()),
                                           PI(InnerMatchers.end()));
  return BindableMatcher<T>(
      DynTypedMatcher::constructVariadic(
          DynTypedMatcher::VO_AllOf,
          ast_type_traits::ASTNodeKind::getFromNodeKind<T>(),
          std::move(DynMatchers))
          .template unconditionalConvertTo<T>());
}

/// Creates a Matcher<T> that matches if
/// T is dyn_cast'able into InnerT and all inner matchers match.
///
/// Returns BindableMatcher, as matchers that use dyn_cast have
/// the same object both to match on and to run submatchers on,
/// so there is no ambiguity with what gets bound.
template<typename T, typename InnerT>
BindableMatcher<T> makeDynCastAllOfComposite(
    ArrayRef<const Matcher<InnerT> *> InnerMatchers) {
  return BindableMatcher<T>(
      makeAllOfComposite(InnerMatchers).template dynCastTo<T>());
}

/// Matches nodes of type T that have at least one descendant node of
/// type DescendantT for which the given inner matcher matches.
///
/// DescendantT must be an AST base type.
template <typename T, typename DescendantT>
class HasDescendantMatcher : public WrapperMatcherInterface<T> {
  static_assert(IsBaseType<DescendantT>::value,
                "has descendant only accepts base type matcher");

public:
  explicit HasDescendantMatcher(const Matcher<DescendantT> &DescendantMatcher)
      : HasDescendantMatcher::WrapperMatcherInterface(DescendantMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return Finder->matchesDescendantOf(Node, this->InnerMatcher, Builder,
                                       ASTMatchFinder::BK_First);
  }
};

/// Matches nodes of type \c T that have a parent node of type \c ParentT
/// for which the given inner matcher matches.
///
/// \c ParentT must be an AST base type.
template <typename T, typename ParentT>
class HasParentMatcher : public WrapperMatcherInterface<T> {
  static_assert(IsBaseType<ParentT>::value,
                "has parent only accepts base type matcher");

public:
  explicit HasParentMatcher(const Matcher<ParentT> &ParentMatcher)
      : HasParentMatcher::WrapperMatcherInterface(ParentMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return Finder->matchesAncestorOf(Node, this->InnerMatcher, Builder,
                                     ASTMatchFinder::AMM_ParentOnly);
  }
};

/// Matches nodes of type \c T that have at least one ancestor node of
/// type \c AncestorT for which the given inner matcher matches.
///
/// \c AncestorT must be an AST base type.
template <typename T, typename AncestorT>
class HasAncestorMatcher : public WrapperMatcherInterface<T> {
  static_assert(IsBaseType<AncestorT>::value,
                "has ancestor only accepts base type matcher");

public:
  explicit HasAncestorMatcher(const Matcher<AncestorT> &AncestorMatcher)
      : HasAncestorMatcher::WrapperMatcherInterface(AncestorMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return Finder->matchesAncestorOf(Node, this->InnerMatcher, Builder,
                                     ASTMatchFinder::AMM_All);
  }
};

/// Matches nodes of type T that have at least one descendant node of
/// type DescendantT for which the given inner matcher matches.
///
/// DescendantT must be an AST base type.
/// As opposed to HasDescendantMatcher, ForEachDescendantMatcher will match
/// for each descendant node that matches instead of only for the first.
template <typename T, typename DescendantT>
class ForEachDescendantMatcher : public WrapperMatcherInterface<T> {
  static_assert(IsBaseType<DescendantT>::value,
                "for each descendant only accepts base type matcher");

public:
  explicit ForEachDescendantMatcher(
      const Matcher<DescendantT> &DescendantMatcher)
      : ForEachDescendantMatcher::WrapperMatcherInterface(DescendantMatcher) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    return Finder->matchesDescendantOf(Node, this->InnerMatcher, Builder,
                                       ASTMatchFinder::BK_All);
  }
};

/// Matches on nodes that have a getValue() method if getValue() equals
/// the value the ValueEqualsMatcher was constructed with.
template <typename T, typename ValueT>
class ValueEqualsMatcher : public SingleNodeMatcherInterface<T> {
  static_assert(std::is_base_of<CharacterLiteral, T>::value ||
                std::is_base_of<CXXBoolLiteralExpr, T>::value ||
                std::is_base_of<FloatingLiteral, T>::value ||
                std::is_base_of<IntegerLiteral, T>::value,
                "the node must have a getValue method");

public:
  explicit ValueEqualsMatcher(const ValueT &ExpectedValue)
      : ExpectedValue(ExpectedValue) {}

  bool matchesNode(const T &Node) const override {
    return Node.getValue() == ExpectedValue;
  }

private:
  const ValueT ExpectedValue;
};

/// Template specializations to easily write matchers for floating point
/// literals.
template <>
inline bool ValueEqualsMatcher<FloatingLiteral, double>::matchesNode(
    const FloatingLiteral &Node) const {
  if ((&Node.getSemantics()) == &llvm::APFloat::IEEEsingle())
    return Node.getValue().convertToFloat() == ExpectedValue;
  if ((&Node.getSemantics()) == &llvm::APFloat::IEEEdouble())
    return Node.getValue().convertToDouble() == ExpectedValue;
  return false;
}
template <>
inline bool ValueEqualsMatcher<FloatingLiteral, float>::matchesNode(
    const FloatingLiteral &Node) const {
  if ((&Node.getSemantics()) == &llvm::APFloat::IEEEsingle())
    return Node.getValue().convertToFloat() == ExpectedValue;
  if ((&Node.getSemantics()) == &llvm::APFloat::IEEEdouble())
    return Node.getValue().convertToDouble() == ExpectedValue;
  return false;
}
template <>
inline bool ValueEqualsMatcher<FloatingLiteral, llvm::APFloat>::matchesNode(
    const FloatingLiteral &Node) const {
  return ExpectedValue.compare(Node.getValue()) == llvm::APFloat::cmpEqual;
}

/// A VariadicDynCastAllOfMatcher<SourceT, TargetT> object is a
/// variadic functor that takes a number of Matcher<TargetT> and returns a
/// Matcher<SourceT> that matches TargetT nodes that are matched by all of the
/// given matchers, if SourceT can be dynamically casted into TargetT.
///
/// For example:
///   const VariadicDynCastAllOfMatcher<
///       Decl, CXXRecordDecl> record;
/// Creates a functor record(...) that creates a Matcher<Decl> given
/// a variable number of arguments of type Matcher<CXXRecordDecl>.
/// The returned matcher matches if the given Decl can by dynamically
/// casted to CXXRecordDecl and all given matchers match.
template <typename SourceT, typename TargetT>
class VariadicDynCastAllOfMatcher
    : public VariadicFunction<BindableMatcher<SourceT>, Matcher<TargetT>,
                              makeDynCastAllOfComposite<SourceT, TargetT>> {
public:
  VariadicDynCastAllOfMatcher() {}
};

/// A \c VariadicAllOfMatcher<T> object is a variadic functor that takes
/// a number of \c Matcher<T> and returns a \c Matcher<T> that matches \c T
/// nodes that are matched by all of the given matchers.
///
/// For example:
///   const VariadicAllOfMatcher<NestedNameSpecifier> nestedNameSpecifier;
/// Creates a functor nestedNameSpecifier(...) that creates a
/// \c Matcher<NestedNameSpecifier> given a variable number of arguments of type
/// \c Matcher<NestedNameSpecifier>.
/// The returned matcher matches if all given matchers match.
template <typename T>
class VariadicAllOfMatcher
    : public VariadicFunction<BindableMatcher<T>, Matcher<T>,
                              makeAllOfComposite<T>> {
public:
  VariadicAllOfMatcher() {}
};

/// Matches nodes of type \c TLoc for which the inner
/// \c Matcher<T> matches.
template <typename TLoc, typename T>
class LocMatcher : public WrapperMatcherInterface<TLoc> {
public:
  explicit LocMatcher(const Matcher<T> &InnerMatcher)
      : LocMatcher::WrapperMatcherInterface(InnerMatcher) {}

  bool matches(const TLoc &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    if (!Node)
      return false;
    return this->InnerMatcher.matches(extract(Node), Finder, Builder);
  }

private:
  static ast_type_traits::DynTypedNode
  extract(const NestedNameSpecifierLoc &Loc) {
    return ast_type_traits::DynTypedNode::create(*Loc.getNestedNameSpecifier());
  }
};

/// Matches \c TypeLocs based on an inner matcher matching a certain
/// \c QualType.
///
/// Used to implement the \c loc() matcher.
class TypeLocTypeMatcher : public WrapperMatcherInterface<TypeLoc> {
public:
  explicit TypeLocTypeMatcher(const Matcher<QualType> &InnerMatcher)
      : TypeLocTypeMatcher::WrapperMatcherInterface(InnerMatcher) {}

  bool matches(const TypeLoc &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    if (!Node)
      return false;
    return this->InnerMatcher.matches(
        ast_type_traits::DynTypedNode::create(Node.getType()), Finder, Builder);
  }
};

/// Matches nodes of type \c T for which the inner matcher matches on a
/// another node of type \c T that can be reached using a given traverse
/// function.
template <typename T>
class TypeTraverseMatcher : public WrapperMatcherInterface<T> {
public:
  explicit TypeTraverseMatcher(const Matcher<QualType> &InnerMatcher,
                               QualType (T::*TraverseFunction)() const)
      : TypeTraverseMatcher::WrapperMatcherInterface(InnerMatcher),
        TraverseFunction(TraverseFunction) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    QualType NextNode = (Node.*TraverseFunction)();
    if (NextNode.isNull())
      return false;
    return this->InnerMatcher.matches(
        ast_type_traits::DynTypedNode::create(NextNode), Finder, Builder);
  }

private:
  QualType (T::*TraverseFunction)() const;
};

/// Matches nodes of type \c T in a ..Loc hierarchy, for which the inner
/// matcher matches on a another node of type \c T that can be reached using a
/// given traverse function.
template <typename T>
class TypeLocTraverseMatcher : public WrapperMatcherInterface<T> {
public:
  explicit TypeLocTraverseMatcher(const Matcher<TypeLoc> &InnerMatcher,
                                  TypeLoc (T::*TraverseFunction)() const)
      : TypeLocTraverseMatcher::WrapperMatcherInterface(InnerMatcher),
        TraverseFunction(TraverseFunction) {}

  bool matches(const T &Node, ASTMatchFinder *Finder,
               BoundNodesTreeBuilder *Builder) const override {
    TypeLoc NextNode = (Node.*TraverseFunction)();
    if (!NextNode)
      return false;
    return this->InnerMatcher.matches(
        ast_type_traits::DynTypedNode::create(NextNode), Finder, Builder);
  }

private:
  TypeLoc (T::*TraverseFunction)() const;
};

/// Converts a \c Matcher<InnerT> to a \c Matcher<OuterT>, where
/// \c OuterT is any type that is supported by \c Getter.
///
/// \code Getter<OuterT>::value() \endcode returns a
/// \code InnerTBase (OuterT::*)() \endcode, which is used to adapt a \c OuterT
/// object into a \c InnerT
template <typename InnerTBase,
          template <typename OuterT> class Getter,
          template <typename OuterT> class MatcherImpl,
          typename ReturnTypesF>
class TypeTraversePolymorphicMatcher {
private:
  using Self = TypeTraversePolymorphicMatcher<InnerTBase, Getter, MatcherImpl,
                                              ReturnTypesF>;

  static Self create(ArrayRef<const Matcher<InnerTBase> *> InnerMatchers);

public:
  using ReturnTypes = typename ExtractFunctionArgMeta<ReturnTypesF>::type;

  explicit TypeTraversePolymorphicMatcher(
      ArrayRef<const Matcher<InnerTBase> *> InnerMatchers)
      : InnerMatcher(makeAllOfComposite(InnerMatchers)) {}

  template <typename OuterT> operator Matcher<OuterT>() const {
    return Matcher<OuterT>(
        new MatcherImpl<OuterT>(InnerMatcher, Getter<OuterT>::value()));
  }

  struct Func
      : public VariadicFunction<Self, Matcher<InnerTBase>, &Self::create> {
    Func() {}
  };

private:
  const Matcher<InnerTBase> InnerMatcher;
};

/// A simple memoizer of T(*)() functions.
///
/// It will call the passed 'Func' template parameter at most once.
/// Used to support AST_MATCHER_FUNCTION() macro.
template <typename Matcher, Matcher (*Func)()> class MemoizedMatcher {
  struct Wrapper {
    Wrapper() : M(Func()) {}

    Matcher M;
  };

public:
  static const Matcher &getInstance() {
    static llvm::ManagedStatic<Wrapper> Instance;
    return Instance->M;
  }
};

// Define the create() method out of line to silence a GCC warning about
// the struct "Func" having greater visibility than its base, which comes from
// using the flag -fvisibility-inlines-hidden.
template <typename InnerTBase, template <typename OuterT> class Getter,
          template <typename OuterT> class MatcherImpl, typename ReturnTypesF>
TypeTraversePolymorphicMatcher<InnerTBase, Getter, MatcherImpl, ReturnTypesF>
TypeTraversePolymorphicMatcher<
    InnerTBase, Getter, MatcherImpl,
    ReturnTypesF>::create(ArrayRef<const Matcher<InnerTBase> *> InnerMatchers) {
  return Self(InnerMatchers);
}

// FIXME: unify ClassTemplateSpecializationDecl and TemplateSpecializationType's
// APIs for accessing the template argument list.
inline ArrayRef<TemplateArgument>
getTemplateSpecializationArgs(const ClassTemplateSpecializationDecl &D) {
  return D.getTemplateArgs().asArray();
}

inline ArrayRef<TemplateArgument>
getTemplateSpecializationArgs(const TemplateSpecializationType &T) {
  return llvm::makeArrayRef(T.getArgs(), T.getNumArgs());
}

inline ArrayRef<TemplateArgument>
getTemplateSpecializationArgs(const FunctionDecl &FD) {
  if (const auto* TemplateArgs = FD.getTemplateSpecializationArgs())
    return TemplateArgs->asArray();
  return ArrayRef<TemplateArgument>();
}

struct NotEqualsBoundNodePredicate {
  bool operator()(const internal::BoundNodesMap &Nodes) const {
    return Nodes.getNode(ID) != Node;
  }

  std::string ID;
  ast_type_traits::DynTypedNode Node;
};

template <typename Ty>
struct GetBodyMatcher {
  static const Stmt *get(const Ty &Node) {
    return Node.getBody();
  }
};

template <>
inline const Stmt *GetBodyMatcher<FunctionDecl>::get(const FunctionDecl &Node) {
  return Node.doesThisDeclarationHaveABody() ? Node.getBody() : nullptr;
}

template <typename Ty>
struct HasSizeMatcher {
  static bool hasSize(const Ty &Node, unsigned int N) {
    return Node.getSize() == N;
  }
};

template <>
inline bool HasSizeMatcher<StringLiteral>::hasSize(
    const StringLiteral &Node, unsigned int N) {
  return Node.getLength() == N;
}

template <typename Ty>
struct GetSourceExpressionMatcher {
  static const Expr *get(const Ty &Node) {
    return Node.getSubExpr();
  }
};

template <>
inline const Expr *GetSourceExpressionMatcher<OpaqueValueExpr>::get(
    const OpaqueValueExpr &Node) {
  return Node.getSourceExpr();
}

template <typename Ty>
struct CompoundStmtMatcher {
  static const CompoundStmt *get(const Ty &Node) {
    return &Node;
  }
};

template <>
inline const CompoundStmt *
CompoundStmtMatcher<StmtExpr>::get(const StmtExpr &Node) {
  return Node.getSubStmt();
}

} // namespace internal

} // namespace ast_matchers

} // namespace clang

#endif // LLVM_CLANG_ASTMATCHERS_ASTMATCHERSINTERNAL_H
