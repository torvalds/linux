//===- SemaTemplate.h - C++ Templates ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===//
//
// This file provides types used in the semantic analysis of C++ templates.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_TEMPLATE_H
#define LLVM_CLANG_SEMA_TEMPLATE_H

#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <utility>

namespace clang {

class ASTContext;
class BindingDecl;
class CXXMethodDecl;
class Decl;
class DeclaratorDecl;
class DeclContext;
class EnumDecl;
class FunctionDecl;
class NamedDecl;
class ParmVarDecl;
class TagDecl;
class TypedefNameDecl;
class TypeSourceInfo;
class VarDecl;

  /// Data structure that captures multiple levels of template argument
  /// lists for use in template instantiation.
  ///
  /// Multiple levels of template arguments occur when instantiating the
  /// definitions of member templates. For example:
  ///
  /// \code
  /// template<typename T>
  /// struct X {
  ///   template<T Value>
  ///   struct Y {
  ///     void f();
  ///   };
  /// };
  /// \endcode
  ///
  /// When instantiating X<int>::Y<17>::f, the multi-level template argument
  /// list will contain a template argument list (int) at depth 0 and a
  /// template argument list (17) at depth 1.
  class MultiLevelTemplateArgumentList {
    /// The template argument list at a certain template depth
    using ArgList = ArrayRef<TemplateArgument>;

    /// The template argument lists, stored from the innermost template
    /// argument list (first) to the outermost template argument list (last).
    SmallVector<ArgList, 4> TemplateArgumentLists;

    /// The number of outer levels of template arguments that are not
    /// being substituted.
    unsigned NumRetainedOuterLevels = 0;

  public:
    /// Construct an empty set of template argument lists.
    MultiLevelTemplateArgumentList() = default;

    /// Construct a single-level template argument list.
    explicit
    MultiLevelTemplateArgumentList(const TemplateArgumentList &TemplateArgs) {
      addOuterTemplateArguments(&TemplateArgs);
    }

    /// Determine the number of levels in this template argument
    /// list.
    unsigned getNumLevels() const {
      return TemplateArgumentLists.size() + NumRetainedOuterLevels;
    }

    /// Determine the number of substituted levels in this template
    /// argument list.
    unsigned getNumSubstitutedLevels() const {
      return TemplateArgumentLists.size();
    }

    /// Retrieve the template argument at a given depth and index.
    const TemplateArgument &operator()(unsigned Depth, unsigned Index) const {
      assert(NumRetainedOuterLevels <= Depth && Depth < getNumLevels());
      assert(Index < TemplateArgumentLists[getNumLevels() - Depth - 1].size());
      return TemplateArgumentLists[getNumLevels() - Depth - 1][Index];
    }

    /// Determine whether there is a non-NULL template argument at the
    /// given depth and index.
    ///
    /// There must exist a template argument list at the given depth.
    bool hasTemplateArgument(unsigned Depth, unsigned Index) const {
      assert(Depth < getNumLevels());

      if (Depth < NumRetainedOuterLevels)
        return false;

      if (Index >= TemplateArgumentLists[getNumLevels() - Depth - 1].size())
        return false;

      return !(*this)(Depth, Index).isNull();
    }

    /// Clear out a specific template argument.
    void setArgument(unsigned Depth, unsigned Index,
                     TemplateArgument Arg) {
      assert(NumRetainedOuterLevels <= Depth && Depth < getNumLevels());
      assert(Index < TemplateArgumentLists[getNumLevels() - Depth - 1].size());
      const_cast<TemplateArgument&>(
                TemplateArgumentLists[getNumLevels() - Depth - 1][Index])
        = Arg;
    }

    /// Add a new outermost level to the multi-level template argument
    /// list.
    void addOuterTemplateArguments(const TemplateArgumentList *TemplateArgs) {
      addOuterTemplateArguments(ArgList(TemplateArgs->data(),
                                        TemplateArgs->size()));
    }

    /// Add a new outmost level to the multi-level template argument
    /// list.
    void addOuterTemplateArguments(ArgList Args) {
      assert(!NumRetainedOuterLevels &&
             "substituted args outside retained args?");
      TemplateArgumentLists.push_back(Args);
    }

    /// Add an outermost level that we are not substituting. We have no
    /// arguments at this level, and do not remove it from the depth of inner
    /// template parameters that we instantiate.
    void addOuterRetainedLevel() {
      ++NumRetainedOuterLevels;
    }

    /// Retrieve the innermost template argument list.
    const ArgList &getInnermost() const {
      return TemplateArgumentLists.front();
    }
  };

  /// The context in which partial ordering of function templates occurs.
  enum TPOC {
    /// Partial ordering of function templates for a function call.
    TPOC_Call,

    /// Partial ordering of function templates for a call to a
    /// conversion function.
    TPOC_Conversion,

    /// Partial ordering of function templates in other contexts, e.g.,
    /// taking the address of a function template or matching a function
    /// template specialization to a function template.
    TPOC_Other
  };

  // This is lame but unavoidable in a world without forward
  // declarations of enums.  The alternatives are to either pollute
  // Sema.h (by including this file) or sacrifice type safety (by
  // making Sema.h declare things as enums).
  class TemplatePartialOrderingContext {
    TPOC Value;

  public:
    TemplatePartialOrderingContext(TPOC Value) : Value(Value) {}

    operator TPOC() const { return Value; }
  };

  /// Captures a template argument whose value has been deduced
  /// via c++ template argument deduction.
  class DeducedTemplateArgument : public TemplateArgument {
    /// For a non-type template argument, whether the value was
    /// deduced from an array bound.
    bool DeducedFromArrayBound = false;

  public:
    DeducedTemplateArgument() = default;

    DeducedTemplateArgument(const TemplateArgument &Arg,
                            bool DeducedFromArrayBound = false)
        : TemplateArgument(Arg), DeducedFromArrayBound(DeducedFromArrayBound) {}

    /// Construct an integral non-type template argument that
    /// has been deduced, possibly from an array bound.
    DeducedTemplateArgument(ASTContext &Ctx,
                            const llvm::APSInt &Value,
                            QualType ValueType,
                            bool DeducedFromArrayBound)
        : TemplateArgument(Ctx, Value, ValueType),
          DeducedFromArrayBound(DeducedFromArrayBound) {}

    /// For a non-type template argument, determine whether the
    /// template argument was deduced from an array bound.
    bool wasDeducedFromArrayBound() const { return DeducedFromArrayBound; }

    /// Specify whether the given non-type template argument
    /// was deduced from an array bound.
    void setDeducedFromArrayBound(bool Deduced) {
      DeducedFromArrayBound = Deduced;
    }
  };

  /// A stack-allocated class that identifies which local
  /// variable declaration instantiations are present in this scope.
  ///
  /// A new instance of this class type will be created whenever we
  /// instantiate a new function declaration, which will have its own
  /// set of parameter declarations.
  class LocalInstantiationScope {
  public:
    /// A set of declarations.
    using DeclArgumentPack = SmallVector<ParmVarDecl *, 4>;

  private:
    /// Reference to the semantic analysis that is performing
    /// this template instantiation.
    Sema &SemaRef;

    using LocalDeclsMap =
        llvm::SmallDenseMap<const Decl *,
                            llvm::PointerUnion<Decl *, DeclArgumentPack *>, 4>;

    /// A mapping from local declarations that occur
    /// within a template to their instantiations.
    ///
    /// This mapping is used during instantiation to keep track of,
    /// e.g., function parameter and variable declarations. For example,
    /// given:
    ///
    /// \code
    ///   template<typename T> T add(T x, T y) { return x + y; }
    /// \endcode
    ///
    /// when we instantiate add<int>, we will introduce a mapping from
    /// the ParmVarDecl for 'x' that occurs in the template to the
    /// instantiated ParmVarDecl for 'x'.
    ///
    /// For a parameter pack, the local instantiation scope may contain a
    /// set of instantiated parameters. This is stored as a DeclArgumentPack
    /// pointer.
    LocalDeclsMap LocalDecls;

    /// The set of argument packs we've allocated.
    SmallVector<DeclArgumentPack *, 1> ArgumentPacks;

    /// The outer scope, which contains local variable
    /// definitions from some other instantiation (that may not be
    /// relevant to this particular scope).
    LocalInstantiationScope *Outer;

    /// Whether we have already exited this scope.
    bool Exited = false;

    /// Whether to combine this scope with the outer scope, such that
    /// lookup will search our outer scope.
    bool CombineWithOuterScope;

    /// If non-NULL, the template parameter pack that has been
    /// partially substituted per C++0x [temp.arg.explicit]p9.
    NamedDecl *PartiallySubstitutedPack = nullptr;

    /// If \c PartiallySubstitutedPack is non-null, the set of
    /// explicitly-specified template arguments in that pack.
    const TemplateArgument *ArgsInPartiallySubstitutedPack;

    /// If \c PartiallySubstitutedPack, the number of
    /// explicitly-specified template arguments in
    /// ArgsInPartiallySubstitutedPack.
    unsigned NumArgsInPartiallySubstitutedPack;

  public:
    LocalInstantiationScope(Sema &SemaRef, bool CombineWithOuterScope = false)
        : SemaRef(SemaRef), Outer(SemaRef.CurrentInstantiationScope),
          CombineWithOuterScope(CombineWithOuterScope) {
      SemaRef.CurrentInstantiationScope = this;
    }

    LocalInstantiationScope(const LocalInstantiationScope &) = delete;
    LocalInstantiationScope &
    operator=(const LocalInstantiationScope &) = delete;

    ~LocalInstantiationScope() {
      Exit();
    }

    const Sema &getSema() const { return SemaRef; }

    /// Exit this local instantiation scope early.
    void Exit() {
      if (Exited)
        return;

      for (unsigned I = 0, N = ArgumentPacks.size(); I != N; ++I)
        delete ArgumentPacks[I];

      SemaRef.CurrentInstantiationScope = Outer;
      Exited = true;
    }

    /// Clone this scope, and all outer scopes, down to the given
    /// outermost scope.
    LocalInstantiationScope *cloneScopes(LocalInstantiationScope *Outermost) {
      if (this == Outermost) return this;

      // Save the current scope from SemaRef since the LocalInstantiationScope
      // will overwrite it on construction
      LocalInstantiationScope *oldScope = SemaRef.CurrentInstantiationScope;

      LocalInstantiationScope *newScope =
        new LocalInstantiationScope(SemaRef, CombineWithOuterScope);

      newScope->Outer = nullptr;
      if (Outer)
        newScope->Outer = Outer->cloneScopes(Outermost);

      newScope->PartiallySubstitutedPack = PartiallySubstitutedPack;
      newScope->ArgsInPartiallySubstitutedPack = ArgsInPartiallySubstitutedPack;
      newScope->NumArgsInPartiallySubstitutedPack =
        NumArgsInPartiallySubstitutedPack;

      for (LocalDeclsMap::iterator I = LocalDecls.begin(), E = LocalDecls.end();
           I != E; ++I) {
        const Decl *D = I->first;
        llvm::PointerUnion<Decl *, DeclArgumentPack *> &Stored =
          newScope->LocalDecls[D];
        if (I->second.is<Decl *>()) {
          Stored = I->second.get<Decl *>();
        } else {
          DeclArgumentPack *OldPack = I->second.get<DeclArgumentPack *>();
          DeclArgumentPack *NewPack = new DeclArgumentPack(*OldPack);
          Stored = NewPack;
          newScope->ArgumentPacks.push_back(NewPack);
        }
      }
      // Restore the saved scope to SemaRef
      SemaRef.CurrentInstantiationScope = oldScope;
      return newScope;
    }

    /// deletes the given scope, and all otuer scopes, down to the
    /// given outermost scope.
    static void deleteScopes(LocalInstantiationScope *Scope,
                             LocalInstantiationScope *Outermost) {
      while (Scope && Scope != Outermost) {
        LocalInstantiationScope *Out = Scope->Outer;
        delete Scope;
        Scope = Out;
      }
    }

    /// Find the instantiation of the declaration D within the current
    /// instantiation scope.
    ///
    /// \param D The declaration whose instantiation we are searching for.
    ///
    /// \returns A pointer to the declaration or argument pack of declarations
    /// to which the declaration \c D is instantiated, if found. Otherwise,
    /// returns NULL.
    llvm::PointerUnion<Decl *, DeclArgumentPack *> *
    findInstantiationOf(const Decl *D);

    void InstantiatedLocal(const Decl *D, Decl *Inst);
    void InstantiatedLocalPackArg(const Decl *D, ParmVarDecl *Inst);
    void MakeInstantiatedLocalArgPack(const Decl *D);

    /// Note that the given parameter pack has been partially substituted
    /// via explicit specification of template arguments
    /// (C++0x [temp.arg.explicit]p9).
    ///
    /// \param Pack The parameter pack, which will always be a template
    /// parameter pack.
    ///
    /// \param ExplicitArgs The explicitly-specified template arguments provided
    /// for this parameter pack.
    ///
    /// \param NumExplicitArgs The number of explicitly-specified template
    /// arguments provided for this parameter pack.
    void SetPartiallySubstitutedPack(NamedDecl *Pack,
                                     const TemplateArgument *ExplicitArgs,
                                     unsigned NumExplicitArgs);

    /// Reset the partially-substituted pack when it is no longer of
    /// interest.
    void ResetPartiallySubstitutedPack() {
      assert(PartiallySubstitutedPack && "No partially-substituted pack");
      PartiallySubstitutedPack = nullptr;
      ArgsInPartiallySubstitutedPack = nullptr;
      NumArgsInPartiallySubstitutedPack = 0;
    }

    /// Retrieve the partially-substitued template parameter pack.
    ///
    /// If there is no partially-substituted parameter pack, returns NULL.
    NamedDecl *
    getPartiallySubstitutedPack(const TemplateArgument **ExplicitArgs = nullptr,
                                unsigned *NumExplicitArgs = nullptr) const;
  };

  class TemplateDeclInstantiator
    : public DeclVisitor<TemplateDeclInstantiator, Decl *>
  {
    Sema &SemaRef;
    Sema::ArgumentPackSubstitutionIndexRAII SubstIndex;
    DeclContext *Owner;
    const MultiLevelTemplateArgumentList &TemplateArgs;
    Sema::LateInstantiatedAttrVec* LateAttrs = nullptr;
    LocalInstantiationScope *StartingScope = nullptr;

    /// A list of out-of-line class template partial
    /// specializations that will need to be instantiated after the
    /// enclosing class's instantiation is complete.
    SmallVector<std::pair<ClassTemplateDecl *,
                                ClassTemplatePartialSpecializationDecl *>, 4>
      OutOfLinePartialSpecs;

    /// A list of out-of-line variable template partial
    /// specializations that will need to be instantiated after the
    /// enclosing variable's instantiation is complete.
    /// FIXME: Verify that this is needed.
    SmallVector<
        std::pair<VarTemplateDecl *, VarTemplatePartialSpecializationDecl *>, 4>
    OutOfLineVarPartialSpecs;

  public:
    TemplateDeclInstantiator(Sema &SemaRef, DeclContext *Owner,
                             const MultiLevelTemplateArgumentList &TemplateArgs)
        : SemaRef(SemaRef),
          SubstIndex(SemaRef, SemaRef.ArgumentPackSubstitutionIndex),
          Owner(Owner), TemplateArgs(TemplateArgs) {}

// Define all the decl visitors using DeclNodes.inc
#define DECL(DERIVED, BASE) \
    Decl *Visit ## DERIVED ## Decl(DERIVED ## Decl *D);
#define ABSTRACT_DECL(DECL)

// Decls which never appear inside a class or function.
#define OBJCCONTAINER(DERIVED, BASE)
#define FILESCOPEASM(DERIVED, BASE)
#define IMPORT(DERIVED, BASE)
#define EXPORT(DERIVED, BASE)
#define LINKAGESPEC(DERIVED, BASE)
#define OBJCCOMPATIBLEALIAS(DERIVED, BASE)
#define OBJCMETHOD(DERIVED, BASE)
#define OBJCTYPEPARAM(DERIVED, BASE)
#define OBJCIVAR(DERIVED, BASE)
#define OBJCPROPERTY(DERIVED, BASE)
#define OBJCPROPERTYIMPL(DERIVED, BASE)
#define EMPTY(DERIVED, BASE)

// Decls which use special-case instantiation code.
#define BLOCK(DERIVED, BASE)
#define CAPTURED(DERIVED, BASE)
#define IMPLICITPARAM(DERIVED, BASE)

#include "clang/AST/DeclNodes.inc"

    // A few supplemental visitor functions.
    Decl *VisitCXXMethodDecl(CXXMethodDecl *D,
                             TemplateParameterList *TemplateParams,
                             bool IsClassScopeSpecialization = false);
    Decl *VisitFunctionDecl(FunctionDecl *D,
                            TemplateParameterList *TemplateParams);
    Decl *VisitDecl(Decl *D);
    Decl *VisitVarDecl(VarDecl *D, bool InstantiatingVarTemplate,
                       ArrayRef<BindingDecl *> *Bindings = nullptr);

    // Enable late instantiation of attributes.  Late instantiated attributes
    // will be stored in LA.
    void enableLateAttributeInstantiation(Sema::LateInstantiatedAttrVec *LA) {
      LateAttrs = LA;
      StartingScope = SemaRef.CurrentInstantiationScope;
    }

    // Disable late instantiation of attributes.
    void disableLateAttributeInstantiation() {
      LateAttrs = nullptr;
      StartingScope = nullptr;
    }

    LocalInstantiationScope *getStartingScope() const { return StartingScope; }

    using delayed_partial_spec_iterator = SmallVectorImpl<std::pair<
      ClassTemplateDecl *, ClassTemplatePartialSpecializationDecl *>>::iterator;

    using delayed_var_partial_spec_iterator = SmallVectorImpl<std::pair<
        VarTemplateDecl *, VarTemplatePartialSpecializationDecl *>>::iterator;

    /// Return an iterator to the beginning of the set of
    /// "delayed" partial specializations, which must be passed to
    /// InstantiateClassTemplatePartialSpecialization once the class
    /// definition has been completed.
    delayed_partial_spec_iterator delayed_partial_spec_begin() {
      return OutOfLinePartialSpecs.begin();
    }

    delayed_var_partial_spec_iterator delayed_var_partial_spec_begin() {
      return OutOfLineVarPartialSpecs.begin();
    }

    /// Return an iterator to the end of the set of
    /// "delayed" partial specializations, which must be passed to
    /// InstantiateClassTemplatePartialSpecialization once the class
    /// definition has been completed.
    delayed_partial_spec_iterator delayed_partial_spec_end() {
      return OutOfLinePartialSpecs.end();
    }

    delayed_var_partial_spec_iterator delayed_var_partial_spec_end() {
      return OutOfLineVarPartialSpecs.end();
    }

    // Helper functions for instantiating methods.
    TypeSourceInfo *SubstFunctionType(FunctionDecl *D,
                             SmallVectorImpl<ParmVarDecl *> &Params);
    bool InitFunctionInstantiation(FunctionDecl *New, FunctionDecl *Tmpl);
    bool InitMethodInstantiation(CXXMethodDecl *New, CXXMethodDecl *Tmpl);

    TemplateParameterList *
      SubstTemplateParams(TemplateParameterList *List);

    bool SubstQualifier(const DeclaratorDecl *OldDecl,
                        DeclaratorDecl *NewDecl);
    bool SubstQualifier(const TagDecl *OldDecl,
                        TagDecl *NewDecl);

    Decl *VisitVarTemplateSpecializationDecl(
        VarTemplateDecl *VarTemplate, VarDecl *FromVar, void *InsertPos,
        const TemplateArgumentListInfo &TemplateArgsInfo,
        ArrayRef<TemplateArgument> Converted);

    Decl *InstantiateTypedefNameDecl(TypedefNameDecl *D, bool IsTypeAlias);
    ClassTemplatePartialSpecializationDecl *
    InstantiateClassTemplatePartialSpecialization(
                                              ClassTemplateDecl *ClassTemplate,
                           ClassTemplatePartialSpecializationDecl *PartialSpec);
    VarTemplatePartialSpecializationDecl *
    InstantiateVarTemplatePartialSpecialization(
        VarTemplateDecl *VarTemplate,
        VarTemplatePartialSpecializationDecl *PartialSpec);
    void InstantiateEnumDefinition(EnumDecl *Enum, EnumDecl *Pattern);

  private:
    template<typename T>
    Decl *instantiateUnresolvedUsingDecl(T *D,
                                         bool InstantiatingPackElement = false);
  };

} // namespace clang

#endif // LLVM_CLANG_SEMA_TEMPLATE_H
