//===- TemplateName.h - C++ Template Name Representation --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TemplateName interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_TEMPLATENAME_H
#define LLVM_CLANG_AST_TEMPLATENAME_H

#include "clang/AST/NestedNameSpecifier.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <cassert>

namespace clang {

class ASTContext;
class DependentTemplateName;
class DiagnosticBuilder;
class IdentifierInfo;
class NamedDecl;
class NestedNameSpecifier;
enum OverloadedOperatorKind : int;
class OverloadedTemplateStorage;
struct PrintingPolicy;
class QualifiedTemplateName;
class SubstTemplateTemplateParmPackStorage;
class SubstTemplateTemplateParmStorage;
class TemplateArgument;
class TemplateDecl;
class TemplateTemplateParmDecl;

/// Implementation class used to describe either a set of overloaded
/// template names or an already-substituted template template parameter pack.
class UncommonTemplateNameStorage {
protected:
  enum Kind {
    Overloaded,
    SubstTemplateTemplateParm,
    SubstTemplateTemplateParmPack
  };

  struct BitsTag {
    /// A Kind.
    unsigned Kind : 2;

    /// The number of stored templates or template arguments,
    /// depending on which subclass we have.
    unsigned Size : 30;
  };

  union {
    struct BitsTag Bits;
    void *PointerAlignment;
  };

  UncommonTemplateNameStorage(Kind kind, unsigned size) {
    Bits.Kind = kind;
    Bits.Size = size;
  }

public:
  unsigned size() const { return Bits.Size; }

  OverloadedTemplateStorage *getAsOverloadedStorage()  {
    return Bits.Kind == Overloaded
             ? reinterpret_cast<OverloadedTemplateStorage *>(this)
             : nullptr;
  }

  SubstTemplateTemplateParmStorage *getAsSubstTemplateTemplateParm() {
    return Bits.Kind == SubstTemplateTemplateParm
             ? reinterpret_cast<SubstTemplateTemplateParmStorage *>(this)
             : nullptr;
  }

  SubstTemplateTemplateParmPackStorage *getAsSubstTemplateTemplateParmPack() {
    return Bits.Kind == SubstTemplateTemplateParmPack
             ? reinterpret_cast<SubstTemplateTemplateParmPackStorage *>(this)
             : nullptr;
  }
};

/// A structure for storing the information associated with an
/// overloaded template name.
class OverloadedTemplateStorage : public UncommonTemplateNameStorage {
  friend class ASTContext;

  OverloadedTemplateStorage(unsigned size)
      : UncommonTemplateNameStorage(Overloaded, size) {}

  NamedDecl **getStorage() {
    return reinterpret_cast<NamedDecl **>(this + 1);
  }
  NamedDecl * const *getStorage() const {
    return reinterpret_cast<NamedDecl *const *>(this + 1);
  }

public:
  using iterator = NamedDecl *const *;

  iterator begin() const { return getStorage(); }
  iterator end() const { return getStorage() + size(); }
};

/// A structure for storing an already-substituted template template
/// parameter pack.
///
/// This kind of template names occurs when the parameter pack has been
/// provided with a template template argument pack in a context where its
/// enclosing pack expansion could not be fully expanded.
class SubstTemplateTemplateParmPackStorage
  : public UncommonTemplateNameStorage, public llvm::FoldingSetNode
{
  TemplateTemplateParmDecl *Parameter;
  const TemplateArgument *Arguments;

public:
  SubstTemplateTemplateParmPackStorage(TemplateTemplateParmDecl *Parameter,
                                       unsigned Size,
                                       const TemplateArgument *Arguments)
      : UncommonTemplateNameStorage(SubstTemplateTemplateParmPack, Size),
        Parameter(Parameter), Arguments(Arguments) {}

  /// Retrieve the template template parameter pack being substituted.
  TemplateTemplateParmDecl *getParameterPack() const {
    return Parameter;
  }

  /// Retrieve the template template argument pack with which this
  /// parameter was substituted.
  TemplateArgument getArgumentPack() const;

  void Profile(llvm::FoldingSetNodeID &ID, ASTContext &Context);

  static void Profile(llvm::FoldingSetNodeID &ID,
                      ASTContext &Context,
                      TemplateTemplateParmDecl *Parameter,
                      const TemplateArgument &ArgPack);
};

/// Represents a C++ template name within the type system.
///
/// A C++ template name refers to a template within the C++ type
/// system. In most cases, a template name is simply a reference to a
/// class template, e.g.
///
/// \code
/// template<typename T> class X { };
///
/// X<int> xi;
/// \endcode
///
/// Here, the 'X' in \c X<int> is a template name that refers to the
/// declaration of the class template X, above. Template names can
/// also refer to function templates, C++0x template aliases, etc.
///
/// Some template names are dependent. For example, consider:
///
/// \code
/// template<typename MetaFun, typename T1, typename T2> struct apply2 {
///   typedef typename MetaFun::template apply<T1, T2>::type type;
/// };
/// \endcode
///
/// Here, "apply" is treated as a template name within the typename
/// specifier in the typedef. "apply" is a nested template, and can
/// only be understood in the context of
class TemplateName {
  using StorageType =
      llvm::PointerUnion4<TemplateDecl *, UncommonTemplateNameStorage *,
                          QualifiedTemplateName *, DependentTemplateName *>;

  StorageType Storage;

  explicit TemplateName(void *Ptr);

public:
  // Kind of name that is actually stored.
  enum NameKind {
    /// A single template declaration.
    Template,

    /// A set of overloaded template declarations.
    OverloadedTemplate,

    /// A qualified template name, where the qualification is kept
    /// to describe the source code as written.
    QualifiedTemplate,

    /// A dependent template name that has not been resolved to a
    /// template (or set of templates).
    DependentTemplate,

    /// A template template parameter that has been substituted
    /// for some other template name.
    SubstTemplateTemplateParm,

    /// A template template parameter pack that has been substituted for
    /// a template template argument pack, but has not yet been expanded into
    /// individual arguments.
    SubstTemplateTemplateParmPack
  };

  TemplateName() = default;
  explicit TemplateName(TemplateDecl *Template);
  explicit TemplateName(OverloadedTemplateStorage *Storage);
  explicit TemplateName(SubstTemplateTemplateParmStorage *Storage);
  explicit TemplateName(SubstTemplateTemplateParmPackStorage *Storage);
  explicit TemplateName(QualifiedTemplateName *Qual);
  explicit TemplateName(DependentTemplateName *Dep);

  /// Determine whether this template name is NULL.
  bool isNull() const;

  // Get the kind of name that is actually stored.
  NameKind getKind() const;

  /// Retrieve the underlying template declaration that
  /// this template name refers to, if known.
  ///
  /// \returns The template declaration that this template name refers
  /// to, if any. If the template name does not refer to a specific
  /// declaration because it is a dependent name, or if it refers to a
  /// set of function templates, returns NULL.
  TemplateDecl *getAsTemplateDecl() const;

  /// Retrieve the underlying, overloaded function template
  // declarations that this template name refers to, if known.
  ///
  /// \returns The set of overloaded function templates that this template
  /// name refers to, if known. If the template name does not refer to a
  /// specific set of function templates because it is a dependent name or
  /// refers to a single template, returns NULL.
  OverloadedTemplateStorage *getAsOverloadedTemplate() const;

  /// Retrieve the substituted template template parameter, if
  /// known.
  ///
  /// \returns The storage for the substituted template template parameter,
  /// if known. Otherwise, returns NULL.
  SubstTemplateTemplateParmStorage *getAsSubstTemplateTemplateParm() const;

  /// Retrieve the substituted template template parameter pack, if
  /// known.
  ///
  /// \returns The storage for the substituted template template parameter pack,
  /// if known. Otherwise, returns NULL.
  SubstTemplateTemplateParmPackStorage *
  getAsSubstTemplateTemplateParmPack() const;

  /// Retrieve the underlying qualified template name
  /// structure, if any.
  QualifiedTemplateName *getAsQualifiedTemplateName() const;

  /// Retrieve the underlying dependent template name
  /// structure, if any.
  DependentTemplateName *getAsDependentTemplateName() const;

  TemplateName getUnderlying() const;

  /// Get the template name to substitute when this template name is used as a
  /// template template argument. This refers to the most recent declaration of
  /// the template, including any default template arguments.
  TemplateName getNameToSubstitute() const;

  /// Determines whether this is a dependent template name.
  bool isDependent() const;

  /// Determines whether this is a template name that somehow
  /// depends on a template parameter.
  bool isInstantiationDependent() const;

  /// Determines whether this template name contains an
  /// unexpanded parameter pack (for C++0x variadic templates).
  bool containsUnexpandedParameterPack() const;

  /// Print the template name.
  ///
  /// \param OS the output stream to which the template name will be
  /// printed.
  ///
  /// \param SuppressNNS if true, don't print the
  /// nested-name-specifier that precedes the template name (if it has
  /// one).
  void print(raw_ostream &OS, const PrintingPolicy &Policy,
             bool SuppressNNS = false) const;

  /// Debugging aid that dumps the template name.
  void dump(raw_ostream &OS) const;

  /// Debugging aid that dumps the template name to standard
  /// error.
  void dump() const;

  void Profile(llvm::FoldingSetNodeID &ID) {
    ID.AddPointer(Storage.getOpaqueValue());
  }

  /// Retrieve the template name as a void pointer.
  void *getAsVoidPointer() const { return Storage.getOpaqueValue(); }

  /// Build a template name from a void pointer.
  static TemplateName getFromVoidPointer(void *Ptr) {
    return TemplateName(Ptr);
  }
};

/// Insertion operator for diagnostics.  This allows sending TemplateName's
/// into a diagnostic with <<.
const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                    TemplateName N);

/// A structure for storing the information associated with a
/// substituted template template parameter.
class SubstTemplateTemplateParmStorage
  : public UncommonTemplateNameStorage, public llvm::FoldingSetNode {
  friend class ASTContext;

  TemplateTemplateParmDecl *Parameter;
  TemplateName Replacement;

  SubstTemplateTemplateParmStorage(TemplateTemplateParmDecl *parameter,
                                   TemplateName replacement)
      : UncommonTemplateNameStorage(SubstTemplateTemplateParm, 0),
        Parameter(parameter), Replacement(replacement) {}

public:
  TemplateTemplateParmDecl *getParameter() const { return Parameter; }
  TemplateName getReplacement() const { return Replacement; }

  void Profile(llvm::FoldingSetNodeID &ID);

  static void Profile(llvm::FoldingSetNodeID &ID,
                      TemplateTemplateParmDecl *parameter,
                      TemplateName replacement);
};

inline TemplateName TemplateName::getUnderlying() const {
  if (SubstTemplateTemplateParmStorage *subst
        = getAsSubstTemplateTemplateParm())
    return subst->getReplacement().getUnderlying();
  return *this;
}

/// Represents a template name that was expressed as a
/// qualified name.
///
/// This kind of template name refers to a template name that was
/// preceded by a nested name specifier, e.g., \c std::vector. Here,
/// the nested name specifier is "std::" and the template name is the
/// declaration for "vector". The QualifiedTemplateName class is only
/// used to provide "sugar" for template names that were expressed
/// with a qualified name, and has no semantic meaning. In this
/// manner, it is to TemplateName what ElaboratedType is to Type,
/// providing extra syntactic sugar for downstream clients.
class QualifiedTemplateName : public llvm::FoldingSetNode {
  friend class ASTContext;

  /// The nested name specifier that qualifies the template name.
  ///
  /// The bit is used to indicate whether the "template" keyword was
  /// present before the template name itself. Note that the
  /// "template" keyword is always redundant in this case (otherwise,
  /// the template name would be a dependent name and we would express
  /// this name with DependentTemplateName).
  llvm::PointerIntPair<NestedNameSpecifier *, 1> Qualifier;

  /// The template declaration or set of overloaded function templates
  /// that this qualified name refers to.
  TemplateDecl *Template;

  QualifiedTemplateName(NestedNameSpecifier *NNS, bool TemplateKeyword,
                        TemplateDecl *Template)
      : Qualifier(NNS, TemplateKeyword? 1 : 0), Template(Template) {}

public:
  /// Return the nested name specifier that qualifies this name.
  NestedNameSpecifier *getQualifier() const { return Qualifier.getPointer(); }

  /// Whether the template name was prefixed by the "template"
  /// keyword.
  bool hasTemplateKeyword() const { return Qualifier.getInt(); }

  /// The template declaration that this qualified name refers
  /// to.
  TemplateDecl *getDecl() const { return Template; }

  /// The template declaration to which this qualified name
  /// refers.
  TemplateDecl *getTemplateDecl() const { return Template; }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getQualifier(), hasTemplateKeyword(), getTemplateDecl());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, NestedNameSpecifier *NNS,
                      bool TemplateKeyword, TemplateDecl *Template) {
    ID.AddPointer(NNS);
    ID.AddBoolean(TemplateKeyword);
    ID.AddPointer(Template);
  }
};

/// Represents a dependent template name that cannot be
/// resolved prior to template instantiation.
///
/// This kind of template name refers to a dependent template name,
/// including its nested name specifier (if any). For example,
/// DependentTemplateName can refer to "MetaFun::template apply",
/// where "MetaFun::" is the nested name specifier and "apply" is the
/// template name referenced. The "template" keyword is implied.
class DependentTemplateName : public llvm::FoldingSetNode {
  friend class ASTContext;

  /// The nested name specifier that qualifies the template
  /// name.
  ///
  /// The bit stored in this qualifier describes whether the \c Name field
  /// is interpreted as an IdentifierInfo pointer (when clear) or as an
  /// overloaded operator kind (when set).
  llvm::PointerIntPair<NestedNameSpecifier *, 1, bool> Qualifier;

  /// The dependent template name.
  union {
    /// The identifier template name.
    ///
    /// Only valid when the bit on \c Qualifier is clear.
    const IdentifierInfo *Identifier;

    /// The overloaded operator name.
    ///
    /// Only valid when the bit on \c Qualifier is set.
    OverloadedOperatorKind Operator;
  };

  /// The canonical template name to which this dependent
  /// template name refers.
  ///
  /// The canonical template name for a dependent template name is
  /// another dependent template name whose nested name specifier is
  /// canonical.
  TemplateName CanonicalTemplateName;

  DependentTemplateName(NestedNameSpecifier *Qualifier,
                        const IdentifierInfo *Identifier)
      : Qualifier(Qualifier, false), Identifier(Identifier),
        CanonicalTemplateName(this) {}

  DependentTemplateName(NestedNameSpecifier *Qualifier,
                        const IdentifierInfo *Identifier,
                        TemplateName Canon)
      : Qualifier(Qualifier, false), Identifier(Identifier),
        CanonicalTemplateName(Canon) {}

  DependentTemplateName(NestedNameSpecifier *Qualifier,
                        OverloadedOperatorKind Operator)
      : Qualifier(Qualifier, true), Operator(Operator),
        CanonicalTemplateName(this) {}

  DependentTemplateName(NestedNameSpecifier *Qualifier,
                        OverloadedOperatorKind Operator,
                        TemplateName Canon)
       : Qualifier(Qualifier, true), Operator(Operator),
         CanonicalTemplateName(Canon) {}

public:
  /// Return the nested name specifier that qualifies this name.
  NestedNameSpecifier *getQualifier() const { return Qualifier.getPointer(); }

  /// Determine whether this template name refers to an identifier.
  bool isIdentifier() const { return !Qualifier.getInt(); }

  /// Returns the identifier to which this template name refers.
  const IdentifierInfo *getIdentifier() const {
    assert(isIdentifier() && "Template name isn't an identifier?");
    return Identifier;
  }

  /// Determine whether this template name refers to an overloaded
  /// operator.
  bool isOverloadedOperator() const { return Qualifier.getInt(); }

  /// Return the overloaded operator to which this template name refers.
  OverloadedOperatorKind getOperator() const {
    assert(isOverloadedOperator() &&
           "Template name isn't an overloaded operator?");
    return Operator;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    if (isIdentifier())
      Profile(ID, getQualifier(), getIdentifier());
    else
      Profile(ID, getQualifier(), getOperator());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, NestedNameSpecifier *NNS,
                      const IdentifierInfo *Identifier) {
    ID.AddPointer(NNS);
    ID.AddBoolean(false);
    ID.AddPointer(Identifier);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, NestedNameSpecifier *NNS,
                      OverloadedOperatorKind Operator) {
    ID.AddPointer(NNS);
    ID.AddBoolean(true);
    ID.AddInteger(Operator);
  }
};

} // namespace clang.

namespace llvm {

/// The clang::TemplateName class is effectively a pointer.
template<>
struct PointerLikeTypeTraits<clang::TemplateName> {
  static inline void *getAsVoidPointer(clang::TemplateName TN) {
    return TN.getAsVoidPointer();
  }

  static inline clang::TemplateName getFromVoidPointer(void *Ptr) {
    return clang::TemplateName::getFromVoidPointer(Ptr);
  }

  // No bits are available!
  enum { NumLowBitsAvailable = 0 };
};

} // namespace llvm.

#endif // LLVM_CLANG_AST_TEMPLATENAME_H
