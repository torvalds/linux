//===--- ParsedTemplate.h - Template Parsing Data Types ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file provides data structures that store the parsed representation of
//  templates.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_PARSEDTEMPLATE_H
#define LLVM_CLANG_SEMA_PARSEDTEMPLATE_H

#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TemplateKinds.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Ownership.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <cstdlib>
#include <new>

namespace clang {
  /// Represents the parsed form of a C++ template argument.
  class ParsedTemplateArgument {
  public:
    /// Describes the kind of template argument that was parsed.
    enum KindType {
      /// A template type parameter, stored as a type.
      Type,
      /// A non-type template parameter, stored as an expression.
      NonType,
      /// A template template argument, stored as a template name.
      Template
    };

    /// Build an empty template argument.
    ///
    /// This template argument is invalid.
    ParsedTemplateArgument() : Kind(Type), Arg(nullptr) { }

    /// Create a template type argument or non-type template argument.
    ///
    /// \param Arg the template type argument or non-type template argument.
    /// \param Loc the location of the type.
    ParsedTemplateArgument(KindType Kind, void *Arg, SourceLocation Loc)
      : Kind(Kind), Arg(Arg), Loc(Loc) { }

    /// Create a template template argument.
    ///
    /// \param SS the C++ scope specifier that precedes the template name, if
    /// any.
    ///
    /// \param Template the template to which this template template
    /// argument refers.
    ///
    /// \param TemplateLoc the location of the template name.
    ParsedTemplateArgument(const CXXScopeSpec &SS,
                           ParsedTemplateTy Template,
                           SourceLocation TemplateLoc)
      : Kind(ParsedTemplateArgument::Template),
        Arg(Template.getAsOpaquePtr()),
        SS(SS), Loc(TemplateLoc), EllipsisLoc() { }

    /// Determine whether the given template argument is invalid.
    bool isInvalid() const { return Arg == nullptr; }

    /// Determine what kind of template argument we have.
    KindType getKind() const { return Kind; }

    /// Retrieve the template type argument's type.
    ParsedType getAsType() const {
      assert(Kind == Type && "Not a template type argument");
      return ParsedType::getFromOpaquePtr(Arg);
    }

    /// Retrieve the non-type template argument's expression.
    Expr *getAsExpr() const {
      assert(Kind == NonType && "Not a non-type template argument");
      return static_cast<Expr*>(Arg);
    }

    /// Retrieve the template template argument's template name.
    ParsedTemplateTy getAsTemplate() const {
      assert(Kind == Template && "Not a template template argument");
      return ParsedTemplateTy::getFromOpaquePtr(Arg);
    }

    /// Retrieve the location of the template argument.
    SourceLocation getLocation() const { return Loc; }

    /// Retrieve the nested-name-specifier that precedes the template
    /// name in a template template argument.
    const CXXScopeSpec &getScopeSpec() const {
      assert(Kind == Template &&
             "Only template template arguments can have a scope specifier");
      return SS;
    }

    /// Retrieve the location of the ellipsis that makes a template
    /// template argument into a pack expansion.
    SourceLocation getEllipsisLoc() const {
      assert(Kind == Template &&
             "Only template template arguments can have an ellipsis");
      return EllipsisLoc;
    }

    /// Retrieve a pack expansion of the given template template
    /// argument.
    ///
    /// \param EllipsisLoc The location of the ellipsis.
    ParsedTemplateArgument getTemplatePackExpansion(
                                              SourceLocation EllipsisLoc) const;

  private:
    KindType Kind;

    /// The actual template argument representation, which may be
    /// an \c Sema::TypeTy* (for a type), an Expr* (for an
    /// expression), or an Sema::TemplateTy (for a template).
    void *Arg;

    /// The nested-name-specifier that can accompany a template template
    /// argument.
    CXXScopeSpec SS;

    /// the location of the template argument.
    SourceLocation Loc;

    /// The ellipsis location that can accompany a template template
    /// argument (turning it into a template template argument expansion).
    SourceLocation EllipsisLoc;
  };

  /// Information about a template-id annotation
  /// token.
  ///
  /// A template-id annotation token contains the template declaration,
  /// template arguments, whether those template arguments were types,
  /// expressions, or template names, and the source locations for important
  /// tokens. All of the information about template arguments is allocated
  /// directly after this structure.
  struct TemplateIdAnnotation final
      : private llvm::TrailingObjects<TemplateIdAnnotation,
                                      ParsedTemplateArgument> {
    friend TrailingObjects;
    /// The nested-name-specifier that precedes the template name.
    CXXScopeSpec SS;

    /// TemplateKWLoc - The location of the template keyword.
    /// For e.g. typename T::template Y<U>
    SourceLocation TemplateKWLoc;

    /// TemplateNameLoc - The location of the template name within the
    /// source.
    SourceLocation TemplateNameLoc;

    /// FIXME: Temporarily stores the name of a specialization
    IdentifierInfo *Name;

    /// FIXME: Temporarily stores the overloaded operator kind.
    OverloadedOperatorKind Operator;

    /// The declaration of the template corresponding to the
    /// template-name.
    ParsedTemplateTy Template;

    /// The kind of template that Template refers to.
    TemplateNameKind Kind;

    /// The location of the '<' before the template argument
    /// list.
    SourceLocation LAngleLoc;

    /// The location of the '>' after the template argument
    /// list.
    SourceLocation RAngleLoc;

    /// NumArgs - The number of template arguments.
    unsigned NumArgs;

    /// Retrieves a pointer to the template arguments
    ParsedTemplateArgument *getTemplateArgs() {
      return getTrailingObjects<ParsedTemplateArgument>();
    }

    /// Creates a new TemplateIdAnnotation with NumArgs arguments and
    /// appends it to List.
    static TemplateIdAnnotation *
    Create(CXXScopeSpec SS, SourceLocation TemplateKWLoc,
           SourceLocation TemplateNameLoc, IdentifierInfo *Name,
           OverloadedOperatorKind OperatorKind,
           ParsedTemplateTy OpaqueTemplateName, TemplateNameKind TemplateKind,
           SourceLocation LAngleLoc, SourceLocation RAngleLoc,
           ArrayRef<ParsedTemplateArgument> TemplateArgs,
           SmallVectorImpl<TemplateIdAnnotation *> &CleanupList) {
      TemplateIdAnnotation *TemplateId = new (llvm::safe_malloc(
          totalSizeToAlloc<ParsedTemplateArgument>(TemplateArgs.size())))
          TemplateIdAnnotation(SS, TemplateKWLoc, TemplateNameLoc, Name,
                               OperatorKind, OpaqueTemplateName, TemplateKind,
                               LAngleLoc, RAngleLoc, TemplateArgs);
      CleanupList.push_back(TemplateId);
      return TemplateId;
    }

    void Destroy() {
      std::for_each(
          getTemplateArgs(), getTemplateArgs() + NumArgs,
          [](ParsedTemplateArgument &A) { A.~ParsedTemplateArgument(); });
      this->~TemplateIdAnnotation();
      free(this);
    }
  private:
    TemplateIdAnnotation(const TemplateIdAnnotation &) = delete;

    TemplateIdAnnotation(CXXScopeSpec SS, SourceLocation TemplateKWLoc,
                         SourceLocation TemplateNameLoc, IdentifierInfo *Name,
                         OverloadedOperatorKind OperatorKind,
                         ParsedTemplateTy OpaqueTemplateName,
                         TemplateNameKind TemplateKind,
                         SourceLocation LAngleLoc, SourceLocation RAngleLoc,
                         ArrayRef<ParsedTemplateArgument> TemplateArgs) noexcept
        : SS(SS), TemplateKWLoc(TemplateKWLoc),
          TemplateNameLoc(TemplateNameLoc), Name(Name), Operator(OperatorKind),
          Template(OpaqueTemplateName), Kind(TemplateKind),
          LAngleLoc(LAngleLoc), RAngleLoc(RAngleLoc),
          NumArgs(TemplateArgs.size()) {

      std::uninitialized_copy(TemplateArgs.begin(), TemplateArgs.end(),
                              getTemplateArgs());
    }
    ~TemplateIdAnnotation() = default;
  };

  /// Retrieves the range of the given template parameter lists.
  SourceRange getTemplateParamsRange(TemplateParameterList const *const *Params,
                                     unsigned NumParams);
} // end namespace clang

#endif // LLVM_CLANG_SEMA_PARSEDTEMPLATE_H
