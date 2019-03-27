//===--- ASTMatchersMacros.h - Structural query framework -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Defines macros that enable us to define new matchers in a single place.
//  Since a matcher is a function which returns a Matcher<T> object, where
//  T is the type of the actual implementation of the matcher, the macros allow
//  us to write matchers like functions and take care of the definition of the
//  class boilerplate.
//
//  Note that when you define a matcher with an AST_MATCHER* macro, only the
//  function which creates the matcher goes into the current namespace - the
//  class that implements the actual matcher, which gets returned by the
//  generator function, is put into the 'internal' namespace. This allows us
//  to only have the functions (which is all the user cares about) in the
//  'ast_matchers' namespace and hide the boilerplate.
//
//  To define a matcher in user code, put it into your own namespace. This would
//  help to prevent ODR violations in case a matcher with the same name is
//  defined in multiple translation units:
//
//  namespace my_matchers {
//  AST_MATCHER_P(clang::MemberExpr, Member,
//                clang::ast_matchers::internal::Matcher<clang::ValueDecl>,
//                InnerMatcher) {
//    return InnerMatcher.matches(*Node.getMemberDecl(), Finder, Builder);
//  }
//  } // namespace my_matchers
//
//  Alternatively, an unnamed namespace may be used:
//
//  namespace clang {
//  namespace ast_matchers {
//  namespace {
//  AST_MATCHER_P(MemberExpr, Member,
//                internal::Matcher<ValueDecl>, InnerMatcher) {
//    return InnerMatcher.matches(*Node.getMemberDecl(), Finder, Builder);
//  }
//  } // namespace
//  } // namespace ast_matchers
//  } // namespace clang
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ASTMATCHERS_ASTMATCHERSMACROS_H
#define LLVM_CLANG_ASTMATCHERS_ASTMATCHERSMACROS_H

/// AST_MATCHER_FUNCTION(ReturnType, DefineMatcher) { ... }
/// defines a zero parameter function named DefineMatcher() that returns a
/// ReturnType object.
#define AST_MATCHER_FUNCTION(ReturnType, DefineMatcher)                        \
  inline ReturnType DefineMatcher##_getInstance();                             \
  inline ReturnType DefineMatcher() {                                          \
    return ::clang::ast_matchers::internal::MemoizedMatcher<                   \
        ReturnType, DefineMatcher##_getInstance>::getInstance();               \
  }                                                                            \
  inline ReturnType DefineMatcher##_getInstance()

/// AST_MATCHER_FUNCTION_P(ReturnType, DefineMatcher, ParamType, Param) {
/// ... }
/// defines a single-parameter function named DefineMatcher() that returns a
/// ReturnType object.
///
/// The code between the curly braces has access to the following variables:
///
///   Param:                 the parameter passed to the function; its type
///                          is ParamType.
///
/// The code should return an instance of ReturnType.
#define AST_MATCHER_FUNCTION_P(ReturnType, DefineMatcher, ParamType, Param)    \
  AST_MATCHER_FUNCTION_P_OVERLOAD(ReturnType, DefineMatcher, ParamType, Param, \
                                  0)
#define AST_MATCHER_FUNCTION_P_OVERLOAD(ReturnType, DefineMatcher, ParamType,  \
                                        Param, OverloadId)                     \
  inline ReturnType DefineMatcher(ParamType const &Param);                     \
  typedef ReturnType (&DefineMatcher##_Type##OverloadId)(ParamType const &);   \
  inline ReturnType DefineMatcher(ParamType const &Param)

/// AST_MATCHER(Type, DefineMatcher) { ... }
/// defines a zero parameter function named DefineMatcher() that returns a
/// Matcher<Type> object.
///
/// The code between the curly braces has access to the following variables:
///
///   Node:                  the AST node being matched; its type is Type.
///   Finder:                an ASTMatchFinder*.
///   Builder:               a BoundNodesTreeBuilder*.
///
/// The code should return true if 'Node' matches.
#define AST_MATCHER(Type, DefineMatcher)                                       \
  namespace internal {                                                         \
  class matcher_##DefineMatcher##Matcher                                       \
      : public ::clang::ast_matchers::internal::MatcherInterface<Type> {       \
  public:                                                                      \
    explicit matcher_##DefineMatcher##Matcher() = default;                     \
    bool matches(const Type &Node,                                             \
                 ::clang::ast_matchers::internal::ASTMatchFinder *Finder,      \
                 ::clang::ast_matchers::internal::BoundNodesTreeBuilder        \
                     *Builder) const override;                                 \
  };                                                                           \
  }                                                                            \
  inline ::clang::ast_matchers::internal::Matcher<Type> DefineMatcher() {      \
    return ::clang::ast_matchers::internal::makeMatcher(                       \
        new internal::matcher_##DefineMatcher##Matcher());                     \
  }                                                                            \
  inline bool internal::matcher_##DefineMatcher##Matcher::matches(             \
      const Type &Node,                                                        \
      ::clang::ast_matchers::internal::ASTMatchFinder *Finder,                 \
      ::clang::ast_matchers::internal::BoundNodesTreeBuilder *Builder) const

/// AST_MATCHER_P(Type, DefineMatcher, ParamType, Param) { ... }
/// defines a single-parameter function named DefineMatcher() that returns a
/// Matcher<Type> object.
///
/// The code between the curly braces has access to the following variables:
///
///   Node:                  the AST node being matched; its type is Type.
///   Param:                 the parameter passed to the function; its type
///                          is ParamType.
///   Finder:                an ASTMatchFinder*.
///   Builder:               a BoundNodesTreeBuilder*.
///
/// The code should return true if 'Node' matches.
#define AST_MATCHER_P(Type, DefineMatcher, ParamType, Param)                   \
  AST_MATCHER_P_OVERLOAD(Type, DefineMatcher, ParamType, Param, 0)

#define AST_MATCHER_P_OVERLOAD(Type, DefineMatcher, ParamType, Param,          \
                               OverloadId)                                     \
  namespace internal {                                                         \
  class matcher_##DefineMatcher##OverloadId##Matcher                           \
      : public ::clang::ast_matchers::internal::MatcherInterface<Type> {       \
  public:                                                                      \
    explicit matcher_##DefineMatcher##OverloadId##Matcher(                     \
        ParamType const &A##Param)                                             \
        : Param(A##Param) {}                                                   \
    bool matches(const Type &Node,                                             \
                 ::clang::ast_matchers::internal::ASTMatchFinder *Finder,      \
                 ::clang::ast_matchers::internal::BoundNodesTreeBuilder        \
                     *Builder) const override;                                 \
                                                                               \
  private:                                                                     \
    ParamType const Param;                                                     \
  };                                                                           \
  }                                                                            \
  inline ::clang::ast_matchers::internal::Matcher<Type> DefineMatcher(         \
      ParamType const &Param) {                                                \
    return ::clang::ast_matchers::internal::makeMatcher(                       \
        new internal::matcher_##DefineMatcher##OverloadId##Matcher(Param));    \
  }                                                                            \
  typedef ::clang::ast_matchers::internal::Matcher<Type>(                      \
      &DefineMatcher##_Type##OverloadId)(ParamType const &Param);              \
  inline bool internal::matcher_##DefineMatcher##OverloadId##Matcher::matches( \
      const Type &Node,                                                        \
      ::clang::ast_matchers::internal::ASTMatchFinder *Finder,                 \
      ::clang::ast_matchers::internal::BoundNodesTreeBuilder *Builder) const

/// AST_MATCHER_P2(
///     Type, DefineMatcher, ParamType1, Param1, ParamType2, Param2) { ... }
/// defines a two-parameter function named DefineMatcher() that returns a
/// Matcher<Type> object.
///
/// The code between the curly braces has access to the following variables:
///
///   Node:                  the AST node being matched; its type is Type.
///   Param1, Param2:        the parameters passed to the function; their types
///                          are ParamType1 and ParamType2.
///   Finder:                an ASTMatchFinder*.
///   Builder:               a BoundNodesTreeBuilder*.
///
/// The code should return true if 'Node' matches.
#define AST_MATCHER_P2(Type, DefineMatcher, ParamType1, Param1, ParamType2,    \
                       Param2)                                                 \
  AST_MATCHER_P2_OVERLOAD(Type, DefineMatcher, ParamType1, Param1, ParamType2, \
                          Param2, 0)

#define AST_MATCHER_P2_OVERLOAD(Type, DefineMatcher, ParamType1, Param1,       \
                                ParamType2, Param2, OverloadId)                \
  namespace internal {                                                         \
  class matcher_##DefineMatcher##OverloadId##Matcher                           \
      : public ::clang::ast_matchers::internal::MatcherInterface<Type> {       \
  public:                                                                      \
    matcher_##DefineMatcher##OverloadId##Matcher(ParamType1 const &A##Param1,  \
                                                 ParamType2 const &A##Param2)  \
        : Param1(A##Param1), Param2(A##Param2) {}                              \
    bool matches(const Type &Node,                                             \
                 ::clang::ast_matchers::internal::ASTMatchFinder *Finder,      \
                 ::clang::ast_matchers::internal::BoundNodesTreeBuilder        \
                     *Builder) const override;                                 \
                                                                               \
  private:                                                                     \
    ParamType1 const Param1;                                                   \
    ParamType2 const Param2;                                                   \
  };                                                                           \
  }                                                                            \
  inline ::clang::ast_matchers::internal::Matcher<Type> DefineMatcher(         \
      ParamType1 const &Param1, ParamType2 const &Param2) {                    \
    return ::clang::ast_matchers::internal::makeMatcher(                       \
        new internal::matcher_##DefineMatcher##OverloadId##Matcher(Param1,     \
                                                                   Param2));   \
  }                                                                            \
  typedef ::clang::ast_matchers::internal::Matcher<Type>(                      \
      &DefineMatcher##_Type##OverloadId)(ParamType1 const &Param1,             \
                                         ParamType2 const &Param2);            \
  inline bool internal::matcher_##DefineMatcher##OverloadId##Matcher::matches( \
      const Type &Node,                                                        \
      ::clang::ast_matchers::internal::ASTMatchFinder *Finder,                 \
      ::clang::ast_matchers::internal::BoundNodesTreeBuilder *Builder) const

/// Construct a type-list to be passed to the AST_POLYMORPHIC_MATCHER*
///   macros.
///
/// You can't pass something like \c TypeList<Foo, Bar> to a macro, because it
/// will look at that as two arguments. However, you can pass
/// \c void(TypeList<Foo, Bar>), which works thanks to the parenthesis.
/// The \c PolymorphicMatcherWithParam* classes will unpack the function type to
/// extract the TypeList object.
#define AST_POLYMORPHIC_SUPPORTED_TYPES(...)                                   \
  void(::clang::ast_matchers::internal::TypeList<__VA_ARGS__>)

/// AST_POLYMORPHIC_MATCHER(DefineMatcher) { ... }
/// defines a single-parameter function named DefineMatcher() that is
/// polymorphic in the return type.
///
/// The variables are the same as for AST_MATCHER, but NodeType will be deduced
/// from the calling context.
#define AST_POLYMORPHIC_MATCHER(DefineMatcher, ReturnTypesF)                   \
  namespace internal {                                                         \
  template <typename NodeType>                                                 \
  class matcher_##DefineMatcher##Matcher                                       \
      : public ::clang::ast_matchers::internal::MatcherInterface<NodeType> {   \
  public:                                                                      \
    bool matches(const NodeType &Node,                                         \
                 ::clang::ast_matchers::internal::ASTMatchFinder *Finder,      \
                 ::clang::ast_matchers::internal::BoundNodesTreeBuilder        \
                     *Builder) const override;                                 \
  };                                                                           \
  }                                                                            \
  inline ::clang::ast_matchers::internal::PolymorphicMatcherWithParam0<        \
      internal::matcher_##DefineMatcher##Matcher, ReturnTypesF>                \
  DefineMatcher() {                                                            \
    return ::clang::ast_matchers::internal::PolymorphicMatcherWithParam0<      \
        internal::matcher_##DefineMatcher##Matcher, ReturnTypesF>();           \
  }                                                                            \
  template <typename NodeType>                                                 \
  bool internal::matcher_##DefineMatcher##Matcher<NodeType>::matches(          \
      const NodeType &Node,                                                    \
      ::clang::ast_matchers::internal::ASTMatchFinder *Finder,                 \
      ::clang::ast_matchers::internal::BoundNodesTreeBuilder *Builder) const

/// AST_POLYMORPHIC_MATCHER_P(DefineMatcher, ParamType, Param) { ... }
/// defines a single-parameter function named DefineMatcher() that is
/// polymorphic in the return type.
///
/// The variables are the same as for
/// AST_MATCHER_P, with the addition of NodeType, which specifies the node type
/// of the matcher Matcher<NodeType> returned by the function matcher().
///
/// FIXME: Pull out common code with above macro?
#define AST_POLYMORPHIC_MATCHER_P(DefineMatcher, ReturnTypesF, ParamType,      \
                                  Param)                                       \
  AST_POLYMORPHIC_MATCHER_P_OVERLOAD(DefineMatcher, ReturnTypesF, ParamType,   \
                                     Param, 0)

#define AST_POLYMORPHIC_MATCHER_P_OVERLOAD(DefineMatcher, ReturnTypesF,        \
                                           ParamType, Param, OverloadId)       \
  namespace internal {                                                         \
  template <typename NodeType, typename ParamT>                                \
  class matcher_##DefineMatcher##OverloadId##Matcher                           \
      : public ::clang::ast_matchers::internal::MatcherInterface<NodeType> {   \
  public:                                                                      \
    explicit matcher_##DefineMatcher##OverloadId##Matcher(                     \
        ParamType const &A##Param)                                             \
        : Param(A##Param) {}                                                   \
    bool matches(const NodeType &Node,                                         \
                 ::clang::ast_matchers::internal::ASTMatchFinder *Finder,      \
                 ::clang::ast_matchers::internal::BoundNodesTreeBuilder        \
                     *Builder) const override;                                 \
                                                                               \
  private:                                                                     \
    ParamType const Param;                                                     \
  };                                                                           \
  }                                                                            \
  inline ::clang::ast_matchers::internal::PolymorphicMatcherWithParam1<        \
      internal::matcher_##DefineMatcher##OverloadId##Matcher, ParamType,       \
      ReturnTypesF>                                                            \
  DefineMatcher(ParamType const &Param) {                                      \
    return ::clang::ast_matchers::internal::PolymorphicMatcherWithParam1<      \
        internal::matcher_##DefineMatcher##OverloadId##Matcher, ParamType,     \
        ReturnTypesF>(Param);                                                  \
  }                                                                            \
  typedef ::clang::ast_matchers::internal::PolymorphicMatcherWithParam1<       \
      internal::matcher_##DefineMatcher##OverloadId##Matcher, ParamType,       \
      ReturnTypesF>(&DefineMatcher##_Type##OverloadId)(                        \
      ParamType const &Param);                                                 \
  template <typename NodeType, typename ParamT>                                \
  bool internal::                                                              \
      matcher_##DefineMatcher##OverloadId##Matcher<NodeType, ParamT>::matches( \
          const NodeType &Node,                                                \
          ::clang::ast_matchers::internal::ASTMatchFinder *Finder,             \
          ::clang::ast_matchers::internal::BoundNodesTreeBuilder *Builder)     \
          const

/// AST_POLYMORPHIC_MATCHER_P2(
///     DefineMatcher, ParamType1, Param1, ParamType2, Param2) { ... }
/// defines a two-parameter function named matcher() that is polymorphic in
/// the return type.
///
/// The variables are the same as for AST_MATCHER_P2, with the
/// addition of NodeType, which specifies the node type of the matcher
/// Matcher<NodeType> returned by the function DefineMatcher().
#define AST_POLYMORPHIC_MATCHER_P2(DefineMatcher, ReturnTypesF, ParamType1,    \
                                   Param1, ParamType2, Param2)                 \
  AST_POLYMORPHIC_MATCHER_P2_OVERLOAD(DefineMatcher, ReturnTypesF, ParamType1, \
                                      Param1, ParamType2, Param2, 0)

#define AST_POLYMORPHIC_MATCHER_P2_OVERLOAD(DefineMatcher, ReturnTypesF,       \
                                            ParamType1, Param1, ParamType2,    \
                                            Param2, OverloadId)                \
  namespace internal {                                                         \
  template <typename NodeType, typename ParamT1, typename ParamT2>             \
  class matcher_##DefineMatcher##OverloadId##Matcher                           \
      : public ::clang::ast_matchers::internal::MatcherInterface<NodeType> {   \
  public:                                                                      \
    matcher_##DefineMatcher##OverloadId##Matcher(ParamType1 const &A##Param1,  \
                                                 ParamType2 const &A##Param2)  \
        : Param1(A##Param1), Param2(A##Param2) {}                              \
    bool matches(const NodeType &Node,                                         \
                 ::clang::ast_matchers::internal::ASTMatchFinder *Finder,      \
                 ::clang::ast_matchers::internal::BoundNodesTreeBuilder        \
                     *Builder) const override;                                 \
                                                                               \
  private:                                                                     \
    ParamType1 const Param1;                                                   \
    ParamType2 const Param2;                                                   \
  };                                                                           \
  }                                                                            \
  inline ::clang::ast_matchers::internal::PolymorphicMatcherWithParam2<        \
      internal::matcher_##DefineMatcher##OverloadId##Matcher, ParamType1,      \
      ParamType2, ReturnTypesF>                                                \
  DefineMatcher(ParamType1 const &Param1, ParamType2 const &Param2) {          \
    return ::clang::ast_matchers::internal::PolymorphicMatcherWithParam2<      \
        internal::matcher_##DefineMatcher##OverloadId##Matcher, ParamType1,    \
        ParamType2, ReturnTypesF>(Param1, Param2);                             \
  }                                                                            \
  typedef ::clang::ast_matchers::internal::PolymorphicMatcherWithParam2<       \
      internal::matcher_##DefineMatcher##OverloadId##Matcher, ParamType1,      \
      ParamType2, ReturnTypesF>(&DefineMatcher##_Type##OverloadId)(            \
      ParamType1 const &Param1, ParamType2 const &Param2);                     \
  template <typename NodeType, typename ParamT1, typename ParamT2>             \
  bool internal::matcher_##DefineMatcher##OverloadId##Matcher<                 \
      NodeType, ParamT1, ParamT2>::                                            \
      matches(const NodeType &Node,                                            \
              ::clang::ast_matchers::internal::ASTMatchFinder *Finder,         \
              ::clang::ast_matchers::internal::BoundNodesTreeBuilder *Builder) \
          const

// FIXME: add a matcher for TypeLoc derived classes using its custom casting
// API (no longer dyn_cast) if/when we need such matching

#define AST_TYPE_TRAVERSE_MATCHER_DECL(MatcherName, FunctionName,              \
                                       ReturnTypesF)                           \
  namespace internal {                                                         \
  template <typename T> struct TypeMatcher##MatcherName##Getter {              \
    static QualType (T::*value())() const { return &T::FunctionName; }         \
  };                                                                           \
  }                                                                            \
  extern const ::clang::ast_matchers::internal::                               \
      TypeTraversePolymorphicMatcher<                                          \
          QualType,                                                            \
          ::clang::ast_matchers::internal::TypeMatcher##MatcherName##Getter,   \
          ::clang::ast_matchers::internal::TypeTraverseMatcher,                \
          ReturnTypesF>::Func MatcherName

#define AST_TYPE_TRAVERSE_MATCHER_DEF(MatcherName, ReturnTypesF)               \
  const ::clang::ast_matchers::internal::TypeTraversePolymorphicMatcher<       \
      QualType,                                                                \
      ::clang::ast_matchers::internal::TypeMatcher##MatcherName##Getter,       \
      ::clang::ast_matchers::internal::TypeTraverseMatcher,                    \
      ReturnTypesF>::Func MatcherName

/// AST_TYPE_TRAVERSE_MATCHER(MatcherName, FunctionName) defines
/// the matcher \c MatcherName that can be used to traverse from one \c Type
/// to another.
///
/// For a specific \c SpecificType, the traversal is done using
/// \c SpecificType::FunctionName. The existence of such a function determines
/// whether a corresponding matcher can be used on \c SpecificType.
#define AST_TYPE_TRAVERSE_MATCHER(MatcherName, FunctionName, ReturnTypesF)     \
  namespace internal {                                                         \
  template <typename T> struct TypeMatcher##MatcherName##Getter {              \
    static QualType (T::*value())() const { return &T::FunctionName; }         \
  };                                                                           \
  }                                                                            \
  const ::clang::ast_matchers::internal::TypeTraversePolymorphicMatcher<       \
      QualType,                                                                \
      ::clang::ast_matchers::internal::TypeMatcher##MatcherName##Getter,       \
      ::clang::ast_matchers::internal::TypeTraverseMatcher,                    \
      ReturnTypesF>::Func MatcherName

#define AST_TYPELOC_TRAVERSE_MATCHER_DECL(MatcherName, FunctionName,           \
                                          ReturnTypesF)                        \
  namespace internal {                                                         \
  template <typename T> struct TypeLocMatcher##MatcherName##Getter {           \
    static TypeLoc (T::*value())() const { return &T::FunctionName##Loc; }     \
  };                                                                           \
  }                                                                            \
  extern const ::clang::ast_matchers::internal::                               \
      TypeTraversePolymorphicMatcher<                                          \
          TypeLoc,                                                             \
          ::clang::ast_matchers::internal::                                    \
              TypeLocMatcher##MatcherName##Getter,                             \
          ::clang::ast_matchers::internal::TypeLocTraverseMatcher,             \
          ReturnTypesF>::Func MatcherName##Loc;                                \
  AST_TYPE_TRAVERSE_MATCHER_DECL(MatcherName, FunctionName##Type, ReturnTypesF)

#define AST_TYPELOC_TRAVERSE_MATCHER_DEF(MatcherName, ReturnTypesF)            \
  const ::clang::ast_matchers::internal::TypeTraversePolymorphicMatcher<       \
      TypeLoc,                                                                 \
      ::clang::ast_matchers::internal::TypeLocMatcher##MatcherName##Getter,    \
      ::clang::ast_matchers::internal::TypeLocTraverseMatcher,                 \
      ReturnTypesF>::Func MatcherName##Loc;                                    \
  AST_TYPE_TRAVERSE_MATCHER_DEF(MatcherName, ReturnTypesF)

/// AST_TYPELOC_TRAVERSE_MATCHER(MatcherName, FunctionName) works
/// identical to \c AST_TYPE_TRAVERSE_MATCHER but operates on \c TypeLocs.
#define AST_TYPELOC_TRAVERSE_MATCHER(MatcherName, FunctionName, ReturnTypesF)  \
  namespace internal {                                                         \
  template <typename T> struct TypeLocMatcher##MatcherName##Getter {           \
    static TypeLoc (T::*value())() const { return &T::FunctionName##Loc; }     \
  };                                                                           \
  }                                                                            \
  const ::clang::ast_matchers::internal::TypeTraversePolymorphicMatcher<       \
      TypeLoc,                                                                 \
      ::clang::ast_matchers::internal::TypeLocMatcher##MatcherName##Getter,    \
      ::clang::ast_matchers::internal::TypeLocTraverseMatcher,                 \
      ReturnTypesF>::Func MatcherName##Loc;                                    \
  AST_TYPE_TRAVERSE_MATCHER(MatcherName, FunctionName##Type, ReturnTypesF)

#endif // LLVM_CLANG_ASTMATCHERS_ASTMATCHERSMACROS_H
