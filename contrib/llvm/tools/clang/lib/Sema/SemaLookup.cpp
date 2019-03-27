//===--------------------- SemaLookup.cpp - Name Lookup  ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements name lookup for C, C++, Objective-C, and
//  Objective-C++.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclLookups.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/TemplateDeduction.h"
#include "clang/Sema/TypoCorrection.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/edit_distance.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <iterator>
#include <list>
#include <set>
#include <utility>
#include <vector>

using namespace clang;
using namespace sema;

namespace {
  class UnqualUsingEntry {
    const DeclContext *Nominated;
    const DeclContext *CommonAncestor;

  public:
    UnqualUsingEntry(const DeclContext *Nominated,
                     const DeclContext *CommonAncestor)
      : Nominated(Nominated), CommonAncestor(CommonAncestor) {
    }

    const DeclContext *getCommonAncestor() const {
      return CommonAncestor;
    }

    const DeclContext *getNominatedNamespace() const {
      return Nominated;
    }

    // Sort by the pointer value of the common ancestor.
    struct Comparator {
      bool operator()(const UnqualUsingEntry &L, const UnqualUsingEntry &R) {
        return L.getCommonAncestor() < R.getCommonAncestor();
      }

      bool operator()(const UnqualUsingEntry &E, const DeclContext *DC) {
        return E.getCommonAncestor() < DC;
      }

      bool operator()(const DeclContext *DC, const UnqualUsingEntry &E) {
        return DC < E.getCommonAncestor();
      }
    };
  };

  /// A collection of using directives, as used by C++ unqualified
  /// lookup.
  class UnqualUsingDirectiveSet {
    Sema &SemaRef;

    typedef SmallVector<UnqualUsingEntry, 8> ListTy;

    ListTy list;
    llvm::SmallPtrSet<DeclContext*, 8> visited;

  public:
    UnqualUsingDirectiveSet(Sema &SemaRef) : SemaRef(SemaRef) {}

    void visitScopeChain(Scope *S, Scope *InnermostFileScope) {
      // C++ [namespace.udir]p1:
      //   During unqualified name lookup, the names appear as if they
      //   were declared in the nearest enclosing namespace which contains
      //   both the using-directive and the nominated namespace.
      DeclContext *InnermostFileDC = InnermostFileScope->getEntity();
      assert(InnermostFileDC && InnermostFileDC->isFileContext());

      for (; S; S = S->getParent()) {
        // C++ [namespace.udir]p1:
        //   A using-directive shall not appear in class scope, but may
        //   appear in namespace scope or in block scope.
        DeclContext *Ctx = S->getEntity();
        if (Ctx && Ctx->isFileContext()) {
          visit(Ctx, Ctx);
        } else if (!Ctx || Ctx->isFunctionOrMethod()) {
          for (auto *I : S->using_directives())
            if (SemaRef.isVisible(I))
              visit(I, InnermostFileDC);
        }
      }
    }

    // Visits a context and collect all of its using directives
    // recursively.  Treats all using directives as if they were
    // declared in the context.
    //
    // A given context is only every visited once, so it is important
    // that contexts be visited from the inside out in order to get
    // the effective DCs right.
    void visit(DeclContext *DC, DeclContext *EffectiveDC) {
      if (!visited.insert(DC).second)
        return;

      addUsingDirectives(DC, EffectiveDC);
    }

    // Visits a using directive and collects all of its using
    // directives recursively.  Treats all using directives as if they
    // were declared in the effective DC.
    void visit(UsingDirectiveDecl *UD, DeclContext *EffectiveDC) {
      DeclContext *NS = UD->getNominatedNamespace();
      if (!visited.insert(NS).second)
        return;

      addUsingDirective(UD, EffectiveDC);
      addUsingDirectives(NS, EffectiveDC);
    }

    // Adds all the using directives in a context (and those nominated
    // by its using directives, transitively) as if they appeared in
    // the given effective context.
    void addUsingDirectives(DeclContext *DC, DeclContext *EffectiveDC) {
      SmallVector<DeclContext*, 4> queue;
      while (true) {
        for (auto UD : DC->using_directives()) {
          DeclContext *NS = UD->getNominatedNamespace();
          if (SemaRef.isVisible(UD) && visited.insert(NS).second) {
            addUsingDirective(UD, EffectiveDC);
            queue.push_back(NS);
          }
        }

        if (queue.empty())
          return;

        DC = queue.pop_back_val();
      }
    }

    // Add a using directive as if it had been declared in the given
    // context.  This helps implement C++ [namespace.udir]p3:
    //   The using-directive is transitive: if a scope contains a
    //   using-directive that nominates a second namespace that itself
    //   contains using-directives, the effect is as if the
    //   using-directives from the second namespace also appeared in
    //   the first.
    void addUsingDirective(UsingDirectiveDecl *UD, DeclContext *EffectiveDC) {
      // Find the common ancestor between the effective context and
      // the nominated namespace.
      DeclContext *Common = UD->getNominatedNamespace();
      while (!Common->Encloses(EffectiveDC))
        Common = Common->getParent();
      Common = Common->getPrimaryContext();

      list.push_back(UnqualUsingEntry(UD->getNominatedNamespace(), Common));
    }

    void done() { llvm::sort(list, UnqualUsingEntry::Comparator()); }

    typedef ListTy::const_iterator const_iterator;

    const_iterator begin() const { return list.begin(); }
    const_iterator end() const { return list.end(); }

    llvm::iterator_range<const_iterator>
    getNamespacesFor(DeclContext *DC) const {
      return llvm::make_range(std::equal_range(begin(), end(),
                                               DC->getPrimaryContext(),
                                               UnqualUsingEntry::Comparator()));
    }
  };
} // end anonymous namespace

// Retrieve the set of identifier namespaces that correspond to a
// specific kind of name lookup.
static inline unsigned getIDNS(Sema::LookupNameKind NameKind,
                               bool CPlusPlus,
                               bool Redeclaration) {
  unsigned IDNS = 0;
  switch (NameKind) {
  case Sema::LookupObjCImplicitSelfParam:
  case Sema::LookupOrdinaryName:
  case Sema::LookupRedeclarationWithLinkage:
  case Sema::LookupLocalFriendName:
    IDNS = Decl::IDNS_Ordinary;
    if (CPlusPlus) {
      IDNS |= Decl::IDNS_Tag | Decl::IDNS_Member | Decl::IDNS_Namespace;
      if (Redeclaration)
        IDNS |= Decl::IDNS_TagFriend | Decl::IDNS_OrdinaryFriend;
    }
    if (Redeclaration)
      IDNS |= Decl::IDNS_LocalExtern;
    break;

  case Sema::LookupOperatorName:
    // Operator lookup is its own crazy thing;  it is not the same
    // as (e.g.) looking up an operator name for redeclaration.
    assert(!Redeclaration && "cannot do redeclaration operator lookup");
    IDNS = Decl::IDNS_NonMemberOperator;
    break;

  case Sema::LookupTagName:
    if (CPlusPlus) {
      IDNS = Decl::IDNS_Type;

      // When looking for a redeclaration of a tag name, we add:
      // 1) TagFriend to find undeclared friend decls
      // 2) Namespace because they can't "overload" with tag decls.
      // 3) Tag because it includes class templates, which can't
      //    "overload" with tag decls.
      if (Redeclaration)
        IDNS |= Decl::IDNS_Tag | Decl::IDNS_TagFriend | Decl::IDNS_Namespace;
    } else {
      IDNS = Decl::IDNS_Tag;
    }
    break;

  case Sema::LookupLabel:
    IDNS = Decl::IDNS_Label;
    break;

  case Sema::LookupMemberName:
    IDNS = Decl::IDNS_Member;
    if (CPlusPlus)
      IDNS |= Decl::IDNS_Tag | Decl::IDNS_Ordinary;
    break;

  case Sema::LookupNestedNameSpecifierName:
    IDNS = Decl::IDNS_Type | Decl::IDNS_Namespace;
    break;

  case Sema::LookupNamespaceName:
    IDNS = Decl::IDNS_Namespace;
    break;

  case Sema::LookupUsingDeclName:
    assert(Redeclaration && "should only be used for redecl lookup");
    IDNS = Decl::IDNS_Ordinary | Decl::IDNS_Tag | Decl::IDNS_Member |
           Decl::IDNS_Using | Decl::IDNS_TagFriend | Decl::IDNS_OrdinaryFriend |
           Decl::IDNS_LocalExtern;
    break;

  case Sema::LookupObjCProtocolName:
    IDNS = Decl::IDNS_ObjCProtocol;
    break;

  case Sema::LookupOMPReductionName:
    IDNS = Decl::IDNS_OMPReduction;
    break;

  case Sema::LookupAnyName:
    IDNS = Decl::IDNS_Ordinary | Decl::IDNS_Tag | Decl::IDNS_Member
      | Decl::IDNS_Using | Decl::IDNS_Namespace | Decl::IDNS_ObjCProtocol
      | Decl::IDNS_Type;
    break;
  }
  return IDNS;
}

void LookupResult::configure() {
  IDNS = getIDNS(LookupKind, getSema().getLangOpts().CPlusPlus,
                 isForRedeclaration());

  // If we're looking for one of the allocation or deallocation
  // operators, make sure that the implicitly-declared new and delete
  // operators can be found.
  switch (NameInfo.getName().getCXXOverloadedOperator()) {
  case OO_New:
  case OO_Delete:
  case OO_Array_New:
  case OO_Array_Delete:
    getSema().DeclareGlobalNewDelete();
    break;

  default:
    break;
  }

  // Compiler builtins are always visible, regardless of where they end
  // up being declared.
  if (IdentifierInfo *Id = NameInfo.getName().getAsIdentifierInfo()) {
    if (unsigned BuiltinID = Id->getBuiltinID()) {
      if (!getSema().Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID))
        AllowHidden = true;
    }
  }
}

bool LookupResult::sanity() const {
  // This function is never called by NDEBUG builds.
  assert(ResultKind != NotFound || Decls.size() == 0);
  assert(ResultKind != Found || Decls.size() == 1);
  assert(ResultKind != FoundOverloaded || Decls.size() > 1 ||
         (Decls.size() == 1 &&
          isa<FunctionTemplateDecl>((*begin())->getUnderlyingDecl())));
  assert(ResultKind != FoundUnresolvedValue || sanityCheckUnresolved());
  assert(ResultKind != Ambiguous || Decls.size() > 1 ||
         (Decls.size() == 1 && (Ambiguity == AmbiguousBaseSubobjects ||
                                Ambiguity == AmbiguousBaseSubobjectTypes)));
  assert((Paths != nullptr) == (ResultKind == Ambiguous &&
                                (Ambiguity == AmbiguousBaseSubobjectTypes ||
                                 Ambiguity == AmbiguousBaseSubobjects)));
  return true;
}

// Necessary because CXXBasePaths is not complete in Sema.h
void LookupResult::deletePaths(CXXBasePaths *Paths) {
  delete Paths;
}

/// Get a representative context for a declaration such that two declarations
/// will have the same context if they were found within the same scope.
static DeclContext *getContextForScopeMatching(Decl *D) {
  // For function-local declarations, use that function as the context. This
  // doesn't account for scopes within the function; the caller must deal with
  // those.
  DeclContext *DC = D->getLexicalDeclContext();
  if (DC->isFunctionOrMethod())
    return DC;

  // Otherwise, look at the semantic context of the declaration. The
  // declaration must have been found there.
  return D->getDeclContext()->getRedeclContext();
}

/// Determine whether \p D is a better lookup result than \p Existing,
/// given that they declare the same entity.
static bool isPreferredLookupResult(Sema &S, Sema::LookupNameKind Kind,
                                    NamedDecl *D, NamedDecl *Existing) {
  // When looking up redeclarations of a using declaration, prefer a using
  // shadow declaration over any other declaration of the same entity.
  if (Kind == Sema::LookupUsingDeclName && isa<UsingShadowDecl>(D) &&
      !isa<UsingShadowDecl>(Existing))
    return true;

  auto *DUnderlying = D->getUnderlyingDecl();
  auto *EUnderlying = Existing->getUnderlyingDecl();

  // If they have different underlying declarations, prefer a typedef over the
  // original type (this happens when two type declarations denote the same
  // type), per a generous reading of C++ [dcl.typedef]p3 and p4. The typedef
  // might carry additional semantic information, such as an alignment override.
  // However, per C++ [dcl.typedef]p5, when looking up a tag name, prefer a tag
  // declaration over a typedef.
  if (DUnderlying->getCanonicalDecl() != EUnderlying->getCanonicalDecl()) {
    assert(isa<TypeDecl>(DUnderlying) && isa<TypeDecl>(EUnderlying));
    bool HaveTag = isa<TagDecl>(EUnderlying);
    bool WantTag = Kind == Sema::LookupTagName;
    return HaveTag != WantTag;
  }

  // Pick the function with more default arguments.
  // FIXME: In the presence of ambiguous default arguments, we should keep both,
  //        so we can diagnose the ambiguity if the default argument is needed.
  //        See C++ [over.match.best]p3.
  if (auto *DFD = dyn_cast<FunctionDecl>(DUnderlying)) {
    auto *EFD = cast<FunctionDecl>(EUnderlying);
    unsigned DMin = DFD->getMinRequiredArguments();
    unsigned EMin = EFD->getMinRequiredArguments();
    // If D has more default arguments, it is preferred.
    if (DMin != EMin)
      return DMin < EMin;
    // FIXME: When we track visibility for default function arguments, check
    // that we pick the declaration with more visible default arguments.
  }

  // Pick the template with more default template arguments.
  if (auto *DTD = dyn_cast<TemplateDecl>(DUnderlying)) {
    auto *ETD = cast<TemplateDecl>(EUnderlying);
    unsigned DMin = DTD->getTemplateParameters()->getMinRequiredArguments();
    unsigned EMin = ETD->getTemplateParameters()->getMinRequiredArguments();
    // If D has more default arguments, it is preferred. Note that default
    // arguments (and their visibility) is monotonically increasing across the
    // redeclaration chain, so this is a quick proxy for "is more recent".
    if (DMin != EMin)
      return DMin < EMin;
    // If D has more *visible* default arguments, it is preferred. Note, an
    // earlier default argument being visible does not imply that a later
    // default argument is visible, so we can't just check the first one.
    for (unsigned I = DMin, N = DTD->getTemplateParameters()->size();
        I != N; ++I) {
      if (!S.hasVisibleDefaultArgument(
              ETD->getTemplateParameters()->getParam(I)) &&
          S.hasVisibleDefaultArgument(
              DTD->getTemplateParameters()->getParam(I)))
        return true;
    }
  }

  // VarDecl can have incomplete array types, prefer the one with more complete
  // array type.
  if (VarDecl *DVD = dyn_cast<VarDecl>(DUnderlying)) {
    VarDecl *EVD = cast<VarDecl>(EUnderlying);
    if (EVD->getType()->isIncompleteType() &&
        !DVD->getType()->isIncompleteType()) {
      // Prefer the decl with a more complete type if visible.
      return S.isVisible(DVD);
    }
    return false; // Avoid picking up a newer decl, just because it was newer.
  }

  // For most kinds of declaration, it doesn't really matter which one we pick.
  if (!isa<FunctionDecl>(DUnderlying) && !isa<VarDecl>(DUnderlying)) {
    // If the existing declaration is hidden, prefer the new one. Otherwise,
    // keep what we've got.
    return !S.isVisible(Existing);
  }

  // Pick the newer declaration; it might have a more precise type.
  for (Decl *Prev = DUnderlying->getPreviousDecl(); Prev;
       Prev = Prev->getPreviousDecl())
    if (Prev == EUnderlying)
      return true;
  return false;
}

/// Determine whether \p D can hide a tag declaration.
static bool canHideTag(NamedDecl *D) {
  // C++ [basic.scope.declarative]p4:
  //   Given a set of declarations in a single declarative region [...]
  //   exactly one declaration shall declare a class name or enumeration name
  //   that is not a typedef name and the other declarations shall all refer to
  //   the same variable, non-static data member, or enumerator, or all refer
  //   to functions and function templates; in this case the class name or
  //   enumeration name is hidden.
  // C++ [basic.scope.hiding]p2:
  //   A class name or enumeration name can be hidden by the name of a
  //   variable, data member, function, or enumerator declared in the same
  //   scope.
  // An UnresolvedUsingValueDecl always instantiates to one of these.
  D = D->getUnderlyingDecl();
  return isa<VarDecl>(D) || isa<EnumConstantDecl>(D) || isa<FunctionDecl>(D) ||
         isa<FunctionTemplateDecl>(D) || isa<FieldDecl>(D) ||
         isa<UnresolvedUsingValueDecl>(D);
}

/// Resolves the result kind of this lookup.
void LookupResult::resolveKind() {
  unsigned N = Decls.size();

  // Fast case: no possible ambiguity.
  if (N == 0) {
    assert(ResultKind == NotFound ||
           ResultKind == NotFoundInCurrentInstantiation);
    return;
  }

  // If there's a single decl, we need to examine it to decide what
  // kind of lookup this is.
  if (N == 1) {
    NamedDecl *D = (*Decls.begin())->getUnderlyingDecl();
    if (isa<FunctionTemplateDecl>(D))
      ResultKind = FoundOverloaded;
    else if (isa<UnresolvedUsingValueDecl>(D))
      ResultKind = FoundUnresolvedValue;
    return;
  }

  // Don't do any extra resolution if we've already resolved as ambiguous.
  if (ResultKind == Ambiguous) return;

  llvm::SmallDenseMap<NamedDecl*, unsigned, 16> Unique;
  llvm::SmallDenseMap<QualType, unsigned, 16> UniqueTypes;

  bool Ambiguous = false;
  bool HasTag = false, HasFunction = false;
  bool HasFunctionTemplate = false, HasUnresolved = false;
  NamedDecl *HasNonFunction = nullptr;

  llvm::SmallVector<NamedDecl*, 4> EquivalentNonFunctions;

  unsigned UniqueTagIndex = 0;

  unsigned I = 0;
  while (I < N) {
    NamedDecl *D = Decls[I]->getUnderlyingDecl();
    D = cast<NamedDecl>(D->getCanonicalDecl());

    // Ignore an invalid declaration unless it's the only one left.
    if (D->isInvalidDecl() && !(I == 0 && N == 1)) {
      Decls[I] = Decls[--N];
      continue;
    }

    llvm::Optional<unsigned> ExistingI;

    // Redeclarations of types via typedef can occur both within a scope
    // and, through using declarations and directives, across scopes. There is
    // no ambiguity if they all refer to the same type, so unique based on the
    // canonical type.
    if (TypeDecl *TD = dyn_cast<TypeDecl>(D)) {
      QualType T = getSema().Context.getTypeDeclType(TD);
      auto UniqueResult = UniqueTypes.insert(
          std::make_pair(getSema().Context.getCanonicalType(T), I));
      if (!UniqueResult.second) {
        // The type is not unique.
        ExistingI = UniqueResult.first->second;
      }
    }

    // For non-type declarations, check for a prior lookup result naming this
    // canonical declaration.
    if (!ExistingI) {
      auto UniqueResult = Unique.insert(std::make_pair(D, I));
      if (!UniqueResult.second) {
        // We've seen this entity before.
        ExistingI = UniqueResult.first->second;
      }
    }

    if (ExistingI) {
      // This is not a unique lookup result. Pick one of the results and
      // discard the other.
      if (isPreferredLookupResult(getSema(), getLookupKind(), Decls[I],
                                  Decls[*ExistingI]))
        Decls[*ExistingI] = Decls[I];
      Decls[I] = Decls[--N];
      continue;
    }

    // Otherwise, do some decl type analysis and then continue.

    if (isa<UnresolvedUsingValueDecl>(D)) {
      HasUnresolved = true;
    } else if (isa<TagDecl>(D)) {
      if (HasTag)
        Ambiguous = true;
      UniqueTagIndex = I;
      HasTag = true;
    } else if (isa<FunctionTemplateDecl>(D)) {
      HasFunction = true;
      HasFunctionTemplate = true;
    } else if (isa<FunctionDecl>(D)) {
      HasFunction = true;
    } else {
      if (HasNonFunction) {
        // If we're about to create an ambiguity between two declarations that
        // are equivalent, but one is an internal linkage declaration from one
        // module and the other is an internal linkage declaration from another
        // module, just skip it.
        if (getSema().isEquivalentInternalLinkageDeclaration(HasNonFunction,
                                                             D)) {
          EquivalentNonFunctions.push_back(D);
          Decls[I] = Decls[--N];
          continue;
        }

        Ambiguous = true;
      }
      HasNonFunction = D;
    }
    I++;
  }

  // C++ [basic.scope.hiding]p2:
  //   A class name or enumeration name can be hidden by the name of
  //   an object, function, or enumerator declared in the same
  //   scope. If a class or enumeration name and an object, function,
  //   or enumerator are declared in the same scope (in any order)
  //   with the same name, the class or enumeration name is hidden
  //   wherever the object, function, or enumerator name is visible.
  // But it's still an error if there are distinct tag types found,
  // even if they're not visible. (ref?)
  if (N > 1 && HideTags && HasTag && !Ambiguous &&
      (HasFunction || HasNonFunction || HasUnresolved)) {
    NamedDecl *OtherDecl = Decls[UniqueTagIndex ? 0 : N - 1];
    if (isa<TagDecl>(Decls[UniqueTagIndex]->getUnderlyingDecl()) &&
        getContextForScopeMatching(Decls[UniqueTagIndex])->Equals(
            getContextForScopeMatching(OtherDecl)) &&
        canHideTag(OtherDecl))
      Decls[UniqueTagIndex] = Decls[--N];
    else
      Ambiguous = true;
  }

  // FIXME: This diagnostic should really be delayed until we're done with
  // the lookup result, in case the ambiguity is resolved by the caller.
  if (!EquivalentNonFunctions.empty() && !Ambiguous)
    getSema().diagnoseEquivalentInternalLinkageDeclarations(
        getNameLoc(), HasNonFunction, EquivalentNonFunctions);

  Decls.set_size(N);

  if (HasNonFunction && (HasFunction || HasUnresolved))
    Ambiguous = true;

  if (Ambiguous)
    setAmbiguous(LookupResult::AmbiguousReference);
  else if (HasUnresolved)
    ResultKind = LookupResult::FoundUnresolvedValue;
  else if (N > 1 || HasFunctionTemplate)
    ResultKind = LookupResult::FoundOverloaded;
  else
    ResultKind = LookupResult::Found;
}

void LookupResult::addDeclsFromBasePaths(const CXXBasePaths &P) {
  CXXBasePaths::const_paths_iterator I, E;
  for (I = P.begin(), E = P.end(); I != E; ++I)
    for (DeclContext::lookup_iterator DI = I->Decls.begin(),
         DE = I->Decls.end(); DI != DE; ++DI)
      addDecl(*DI);
}

void LookupResult::setAmbiguousBaseSubobjects(CXXBasePaths &P) {
  Paths = new CXXBasePaths;
  Paths->swap(P);
  addDeclsFromBasePaths(*Paths);
  resolveKind();
  setAmbiguous(AmbiguousBaseSubobjects);
}

void LookupResult::setAmbiguousBaseSubobjectTypes(CXXBasePaths &P) {
  Paths = new CXXBasePaths;
  Paths->swap(P);
  addDeclsFromBasePaths(*Paths);
  resolveKind();
  setAmbiguous(AmbiguousBaseSubobjectTypes);
}

void LookupResult::print(raw_ostream &Out) {
  Out << Decls.size() << " result(s)";
  if (isAmbiguous()) Out << ", ambiguous";
  if (Paths) Out << ", base paths present";

  for (iterator I = begin(), E = end(); I != E; ++I) {
    Out << "\n";
    (*I)->print(Out, 2);
  }
}

LLVM_DUMP_METHOD void LookupResult::dump() {
  llvm::errs() << "lookup results for " << getLookupName().getAsString()
               << ":\n";
  for (NamedDecl *D : *this)
    D->dump();
}

/// Lookup a builtin function, when name lookup would otherwise
/// fail.
static bool LookupBuiltin(Sema &S, LookupResult &R) {
  Sema::LookupNameKind NameKind = R.getLookupKind();

  // If we didn't find a use of this identifier, and if the identifier
  // corresponds to a compiler builtin, create the decl object for the builtin
  // now, injecting it into translation unit scope, and return it.
  if (NameKind == Sema::LookupOrdinaryName ||
      NameKind == Sema::LookupRedeclarationWithLinkage) {
    IdentifierInfo *II = R.getLookupName().getAsIdentifierInfo();
    if (II) {
      if (S.getLangOpts().CPlusPlus && NameKind == Sema::LookupOrdinaryName) {
        if (II == S.getASTContext().getMakeIntegerSeqName()) {
          R.addDecl(S.getASTContext().getMakeIntegerSeqDecl());
          return true;
        } else if (II == S.getASTContext().getTypePackElementName()) {
          R.addDecl(S.getASTContext().getTypePackElementDecl());
          return true;
        }
      }

      // If this is a builtin on this (or all) targets, create the decl.
      if (unsigned BuiltinID = II->getBuiltinID()) {
        // In C++ and OpenCL (spec v1.2 s6.9.f), we don't have any predefined
        // library functions like 'malloc'. Instead, we'll just error.
        if ((S.getLangOpts().CPlusPlus || S.getLangOpts().OpenCL) &&
            S.Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID))
          return false;

        if (NamedDecl *D = S.LazilyCreateBuiltin((IdentifierInfo *)II,
                                                 BuiltinID, S.TUScope,
                                                 R.isForRedeclaration(),
                                                 R.getNameLoc())) {
          R.addDecl(D);
          return true;
        }
      }
    }
  }

  return false;
}

/// Determine whether we can declare a special member function within
/// the class at this point.
static bool CanDeclareSpecialMemberFunction(const CXXRecordDecl *Class) {
  // We need to have a definition for the class.
  if (!Class->getDefinition() || Class->isDependentContext())
    return false;

  // We can't be in the middle of defining the class.
  return !Class->isBeingDefined();
}

void Sema::ForceDeclarationOfImplicitMembers(CXXRecordDecl *Class) {
  if (!CanDeclareSpecialMemberFunction(Class))
    return;

  // If the default constructor has not yet been declared, do so now.
  if (Class->needsImplicitDefaultConstructor())
    DeclareImplicitDefaultConstructor(Class);

  // If the copy constructor has not yet been declared, do so now.
  if (Class->needsImplicitCopyConstructor())
    DeclareImplicitCopyConstructor(Class);

  // If the copy assignment operator has not yet been declared, do so now.
  if (Class->needsImplicitCopyAssignment())
    DeclareImplicitCopyAssignment(Class);

  if (getLangOpts().CPlusPlus11) {
    // If the move constructor has not yet been declared, do so now.
    if (Class->needsImplicitMoveConstructor())
      DeclareImplicitMoveConstructor(Class);

    // If the move assignment operator has not yet been declared, do so now.
    if (Class->needsImplicitMoveAssignment())
      DeclareImplicitMoveAssignment(Class);
  }

  // If the destructor has not yet been declared, do so now.
  if (Class->needsImplicitDestructor())
    DeclareImplicitDestructor(Class);
}

/// Determine whether this is the name of an implicitly-declared
/// special member function.
static bool isImplicitlyDeclaredMemberFunctionName(DeclarationName Name) {
  switch (Name.getNameKind()) {
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
    return true;

  case DeclarationName::CXXOperatorName:
    return Name.getCXXOverloadedOperator() == OO_Equal;

  default:
    break;
  }

  return false;
}

/// If there are any implicit member functions with the given name
/// that need to be declared in the given declaration context, do so.
static void DeclareImplicitMemberFunctionsWithName(Sema &S,
                                                   DeclarationName Name,
                                                   SourceLocation Loc,
                                                   const DeclContext *DC) {
  if (!DC)
    return;

  switch (Name.getNameKind()) {
  case DeclarationName::CXXConstructorName:
    if (const CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(DC))
      if (Record->getDefinition() && CanDeclareSpecialMemberFunction(Record)) {
        CXXRecordDecl *Class = const_cast<CXXRecordDecl *>(Record);
        if (Record->needsImplicitDefaultConstructor())
          S.DeclareImplicitDefaultConstructor(Class);
        if (Record->needsImplicitCopyConstructor())
          S.DeclareImplicitCopyConstructor(Class);
        if (S.getLangOpts().CPlusPlus11 &&
            Record->needsImplicitMoveConstructor())
          S.DeclareImplicitMoveConstructor(Class);
      }
    break;

  case DeclarationName::CXXDestructorName:
    if (const CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(DC))
      if (Record->getDefinition() && Record->needsImplicitDestructor() &&
          CanDeclareSpecialMemberFunction(Record))
        S.DeclareImplicitDestructor(const_cast<CXXRecordDecl *>(Record));
    break;

  case DeclarationName::CXXOperatorName:
    if (Name.getCXXOverloadedOperator() != OO_Equal)
      break;

    if (const CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(DC)) {
      if (Record->getDefinition() && CanDeclareSpecialMemberFunction(Record)) {
        CXXRecordDecl *Class = const_cast<CXXRecordDecl *>(Record);
        if (Record->needsImplicitCopyAssignment())
          S.DeclareImplicitCopyAssignment(Class);
        if (S.getLangOpts().CPlusPlus11 &&
            Record->needsImplicitMoveAssignment())
          S.DeclareImplicitMoveAssignment(Class);
      }
    }
    break;

  case DeclarationName::CXXDeductionGuideName:
    S.DeclareImplicitDeductionGuides(Name.getCXXDeductionGuideTemplate(), Loc);
    break;

  default:
    break;
  }
}

// Adds all qualifying matches for a name within a decl context to the
// given lookup result.  Returns true if any matches were found.
static bool LookupDirect(Sema &S, LookupResult &R, const DeclContext *DC) {
  bool Found = false;

  // Lazily declare C++ special member functions.
  if (S.getLangOpts().CPlusPlus)
    DeclareImplicitMemberFunctionsWithName(S, R.getLookupName(), R.getNameLoc(),
                                           DC);

  // Perform lookup into this declaration context.
  DeclContext::lookup_result DR = DC->lookup(R.getLookupName());
  for (NamedDecl *D : DR) {
    if ((D = R.getAcceptableDecl(D))) {
      R.addDecl(D);
      Found = true;
    }
  }

  if (!Found && DC->isTranslationUnit() && LookupBuiltin(S, R))
    return true;

  if (R.getLookupName().getNameKind()
        != DeclarationName::CXXConversionFunctionName ||
      R.getLookupName().getCXXNameType()->isDependentType() ||
      !isa<CXXRecordDecl>(DC))
    return Found;

  // C++ [temp.mem]p6:
  //   A specialization of a conversion function template is not found by
  //   name lookup. Instead, any conversion function templates visible in the
  //   context of the use are considered. [...]
  const CXXRecordDecl *Record = cast<CXXRecordDecl>(DC);
  if (!Record->isCompleteDefinition())
    return Found;

  // For conversion operators, 'operator auto' should only match
  // 'operator auto'.  Since 'auto' is not a type, it shouldn't be considered
  // as a candidate for template substitution.
  auto *ContainedDeducedType =
      R.getLookupName().getCXXNameType()->getContainedDeducedType();
  if (R.getLookupName().getNameKind() ==
          DeclarationName::CXXConversionFunctionName &&
      ContainedDeducedType && ContainedDeducedType->isUndeducedType())
    return Found;

  for (CXXRecordDecl::conversion_iterator U = Record->conversion_begin(),
         UEnd = Record->conversion_end(); U != UEnd; ++U) {
    FunctionTemplateDecl *ConvTemplate = dyn_cast<FunctionTemplateDecl>(*U);
    if (!ConvTemplate)
      continue;

    // When we're performing lookup for the purposes of redeclaration, just
    // add the conversion function template. When we deduce template
    // arguments for specializations, we'll end up unifying the return
    // type of the new declaration with the type of the function template.
    if (R.isForRedeclaration()) {
      R.addDecl(ConvTemplate);
      Found = true;
      continue;
    }

    // C++ [temp.mem]p6:
    //   [...] For each such operator, if argument deduction succeeds
    //   (14.9.2.3), the resulting specialization is used as if found by
    //   name lookup.
    //
    // When referencing a conversion function for any purpose other than
    // a redeclaration (such that we'll be building an expression with the
    // result), perform template argument deduction and place the
    // specialization into the result set. We do this to avoid forcing all
    // callers to perform special deduction for conversion functions.
    TemplateDeductionInfo Info(R.getNameLoc());
    FunctionDecl *Specialization = nullptr;

    const FunctionProtoType *ConvProto
      = ConvTemplate->getTemplatedDecl()->getType()->getAs<FunctionProtoType>();
    assert(ConvProto && "Nonsensical conversion function template type");

    // Compute the type of the function that we would expect the conversion
    // function to have, if it were to match the name given.
    // FIXME: Calling convention!
    FunctionProtoType::ExtProtoInfo EPI = ConvProto->getExtProtoInfo();
    EPI.ExtInfo = EPI.ExtInfo.withCallingConv(CC_C);
    EPI.ExceptionSpec = EST_None;
    QualType ExpectedType
      = R.getSema().Context.getFunctionType(R.getLookupName().getCXXNameType(),
                                            None, EPI);

    // Perform template argument deduction against the type that we would
    // expect the function to have.
    if (R.getSema().DeduceTemplateArguments(ConvTemplate, nullptr, ExpectedType,
                                            Specialization, Info)
          == Sema::TDK_Success) {
      R.addDecl(Specialization);
      Found = true;
    }
  }

  return Found;
}

// Performs C++ unqualified lookup into the given file context.
static bool
CppNamespaceLookup(Sema &S, LookupResult &R, ASTContext &Context,
                   DeclContext *NS, UnqualUsingDirectiveSet &UDirs) {

  assert(NS && NS->isFileContext() && "CppNamespaceLookup() requires namespace!");

  // Perform direct name lookup into the LookupCtx.
  bool Found = LookupDirect(S, R, NS);

  // Perform direct name lookup into the namespaces nominated by the
  // using directives whose common ancestor is this namespace.
  for (const UnqualUsingEntry &UUE : UDirs.getNamespacesFor(NS))
    if (LookupDirect(S, R, UUE.getNominatedNamespace()))
      Found = true;

  R.resolveKind();

  return Found;
}

static bool isNamespaceOrTranslationUnitScope(Scope *S) {
  if (DeclContext *Ctx = S->getEntity())
    return Ctx->isFileContext();
  return false;
}

// Find the next outer declaration context from this scope. This
// routine actually returns the semantic outer context, which may
// differ from the lexical context (encoded directly in the Scope
// stack) when we are parsing a member of a class template. In this
// case, the second element of the pair will be true, to indicate that
// name lookup should continue searching in this semantic context when
// it leaves the current template parameter scope.
static std::pair<DeclContext *, bool> findOuterContext(Scope *S) {
  DeclContext *DC = S->getEntity();
  DeclContext *Lexical = nullptr;
  for (Scope *OuterS = S->getParent(); OuterS;
       OuterS = OuterS->getParent()) {
    if (OuterS->getEntity()) {
      Lexical = OuterS->getEntity();
      break;
    }
  }

  // C++ [temp.local]p8:
  //   In the definition of a member of a class template that appears
  //   outside of the namespace containing the class template
  //   definition, the name of a template-parameter hides the name of
  //   a member of this namespace.
  //
  // Example:
  //
  //   namespace N {
  //     class C { };
  //
  //     template<class T> class B {
  //       void f(T);
  //     };
  //   }
  //
  //   template<class C> void N::B<C>::f(C) {
  //     C b;  // C is the template parameter, not N::C
  //   }
  //
  // In this example, the lexical context we return is the
  // TranslationUnit, while the semantic context is the namespace N.
  if (!Lexical || !DC || !S->getParent() ||
      !S->getParent()->isTemplateParamScope())
    return std::make_pair(Lexical, false);

  // Find the outermost template parameter scope.
  // For the example, this is the scope for the template parameters of
  // template<class C>.
  Scope *OutermostTemplateScope = S->getParent();
  while (OutermostTemplateScope->getParent() &&
         OutermostTemplateScope->getParent()->isTemplateParamScope())
    OutermostTemplateScope = OutermostTemplateScope->getParent();

  // Find the namespace context in which the original scope occurs. In
  // the example, this is namespace N.
  DeclContext *Semantic = DC;
  while (!Semantic->isFileContext())
    Semantic = Semantic->getParent();

  // Find the declaration context just outside of the template
  // parameter scope. This is the context in which the template is
  // being lexically declaration (a namespace context). In the
  // example, this is the global scope.
  if (Lexical->isFileContext() && !Lexical->Equals(Semantic) &&
      Lexical->Encloses(Semantic))
    return std::make_pair(Semantic, true);

  return std::make_pair(Lexical, false);
}

namespace {
/// An RAII object to specify that we want to find block scope extern
/// declarations.
struct FindLocalExternScope {
  FindLocalExternScope(LookupResult &R)
      : R(R), OldFindLocalExtern(R.getIdentifierNamespace() &
                                 Decl::IDNS_LocalExtern) {
    R.setFindLocalExtern(R.getIdentifierNamespace() &
                         (Decl::IDNS_Ordinary | Decl::IDNS_NonMemberOperator));
  }
  void restore() {
    R.setFindLocalExtern(OldFindLocalExtern);
  }
  ~FindLocalExternScope() {
    restore();
  }
  LookupResult &R;
  bool OldFindLocalExtern;
};
} // end anonymous namespace

bool Sema::CppLookupName(LookupResult &R, Scope *S) {
  assert(getLangOpts().CPlusPlus && "Can perform only C++ lookup");

  DeclarationName Name = R.getLookupName();
  Sema::LookupNameKind NameKind = R.getLookupKind();

  // If this is the name of an implicitly-declared special member function,
  // go through the scope stack to implicitly declare
  if (isImplicitlyDeclaredMemberFunctionName(Name)) {
    for (Scope *PreS = S; PreS; PreS = PreS->getParent())
      if (DeclContext *DC = PreS->getEntity())
        DeclareImplicitMemberFunctionsWithName(*this, Name, R.getNameLoc(), DC);
  }

  // Implicitly declare member functions with the name we're looking for, if in
  // fact we are in a scope where it matters.

  Scope *Initial = S;
  IdentifierResolver::iterator
    I = IdResolver.begin(Name),
    IEnd = IdResolver.end();

  // First we lookup local scope.
  // We don't consider using-directives, as per 7.3.4.p1 [namespace.udir]
  // ...During unqualified name lookup (3.4.1), the names appear as if
  // they were declared in the nearest enclosing namespace which contains
  // both the using-directive and the nominated namespace.
  // [Note: in this context, "contains" means "contains directly or
  // indirectly".
  //
  // For example:
  // namespace A { int i; }
  // void foo() {
  //   int i;
  //   {
  //     using namespace A;
  //     ++i; // finds local 'i', A::i appears at global scope
  //   }
  // }
  //
  UnqualUsingDirectiveSet UDirs(*this);
  bool VisitedUsingDirectives = false;
  bool LeftStartingScope = false;
  DeclContext *OutsideOfTemplateParamDC = nullptr;

  // When performing a scope lookup, we want to find local extern decls.
  FindLocalExternScope FindLocals(R);

  for (; S && !isNamespaceOrTranslationUnitScope(S); S = S->getParent()) {
    DeclContext *Ctx = S->getEntity();
    bool SearchNamespaceScope = true;
    // Check whether the IdResolver has anything in this scope.
    for (; I != IEnd && S->isDeclScope(*I); ++I) {
      if (NamedDecl *ND = R.getAcceptableDecl(*I)) {
        if (NameKind == LookupRedeclarationWithLinkage &&
            !(*I)->isTemplateParameter()) {
          // If it's a template parameter, we still find it, so we can diagnose
          // the invalid redeclaration.

          // Determine whether this (or a previous) declaration is
          // out-of-scope.
          if (!LeftStartingScope && !Initial->isDeclScope(*I))
            LeftStartingScope = true;

          // If we found something outside of our starting scope that
          // does not have linkage, skip it.
          if (LeftStartingScope && !((*I)->hasLinkage())) {
            R.setShadowed();
            continue;
          }
        } else {
          // We found something in this scope, we should not look at the
          // namespace scope
          SearchNamespaceScope = false;
        }
        R.addDecl(ND);
      }
    }
    if (!SearchNamespaceScope) {
      R.resolveKind();
      if (S->isClassScope())
        if (CXXRecordDecl *Record = dyn_cast_or_null<CXXRecordDecl>(Ctx))
          R.setNamingClass(Record);
      return true;
    }

    if (NameKind == LookupLocalFriendName && !S->isClassScope()) {
      // C++11 [class.friend]p11:
      //   If a friend declaration appears in a local class and the name
      //   specified is an unqualified name, a prior declaration is
      //   looked up without considering scopes that are outside the
      //   innermost enclosing non-class scope.
      return false;
    }

    if (!Ctx && S->isTemplateParamScope() && OutsideOfTemplateParamDC &&
        S->getParent() && !S->getParent()->isTemplateParamScope()) {
      // We've just searched the last template parameter scope and
      // found nothing, so look into the contexts between the
      // lexical and semantic declaration contexts returned by
      // findOuterContext(). This implements the name lookup behavior
      // of C++ [temp.local]p8.
      Ctx = OutsideOfTemplateParamDC;
      OutsideOfTemplateParamDC = nullptr;
    }

    if (Ctx) {
      DeclContext *OuterCtx;
      bool SearchAfterTemplateScope;
      std::tie(OuterCtx, SearchAfterTemplateScope) = findOuterContext(S);
      if (SearchAfterTemplateScope)
        OutsideOfTemplateParamDC = OuterCtx;

      for (; Ctx && !Ctx->Equals(OuterCtx); Ctx = Ctx->getLookupParent()) {
        // We do not directly look into transparent contexts, since
        // those entities will be found in the nearest enclosing
        // non-transparent context.
        if (Ctx->isTransparentContext())
          continue;

        // We do not look directly into function or method contexts,
        // since all of the local variables and parameters of the
        // function/method are present within the Scope.
        if (Ctx->isFunctionOrMethod()) {
          // If we have an Objective-C instance method, look for ivars
          // in the corresponding interface.
          if (ObjCMethodDecl *Method = dyn_cast<ObjCMethodDecl>(Ctx)) {
            if (Method->isInstanceMethod() && Name.getAsIdentifierInfo())
              if (ObjCInterfaceDecl *Class = Method->getClassInterface()) {
                ObjCInterfaceDecl *ClassDeclared;
                if (ObjCIvarDecl *Ivar = Class->lookupInstanceVariable(
                                                 Name.getAsIdentifierInfo(),
                                                             ClassDeclared)) {
                  if (NamedDecl *ND = R.getAcceptableDecl(Ivar)) {
                    R.addDecl(ND);
                    R.resolveKind();
                    return true;
                  }
                }
              }
          }

          continue;
        }

        // If this is a file context, we need to perform unqualified name
        // lookup considering using directives.
        if (Ctx->isFileContext()) {
          // If we haven't handled using directives yet, do so now.
          if (!VisitedUsingDirectives) {
            // Add using directives from this context up to the top level.
            for (DeclContext *UCtx = Ctx; UCtx; UCtx = UCtx->getParent()) {
              if (UCtx->isTransparentContext())
                continue;

              UDirs.visit(UCtx, UCtx);
            }

            // Find the innermost file scope, so we can add using directives
            // from local scopes.
            Scope *InnermostFileScope = S;
            while (InnermostFileScope &&
                   !isNamespaceOrTranslationUnitScope(InnermostFileScope))
              InnermostFileScope = InnermostFileScope->getParent();
            UDirs.visitScopeChain(Initial, InnermostFileScope);

            UDirs.done();

            VisitedUsingDirectives = true;
          }

          if (CppNamespaceLookup(*this, R, Context, Ctx, UDirs)) {
            R.resolveKind();
            return true;
          }

          continue;
        }

        // Perform qualified name lookup into this context.
        // FIXME: In some cases, we know that every name that could be found by
        // this qualified name lookup will also be on the identifier chain. For
        // example, inside a class without any base classes, we never need to
        // perform qualified lookup because all of the members are on top of the
        // identifier chain.
        if (LookupQualifiedName(R, Ctx, /*InUnqualifiedLookup=*/true))
          return true;
      }
    }
  }

  // Stop if we ran out of scopes.
  // FIXME:  This really, really shouldn't be happening.
  if (!S) return false;

  // If we are looking for members, no need to look into global/namespace scope.
  if (NameKind == LookupMemberName)
    return false;

  // Collect UsingDirectiveDecls in all scopes, and recursively all
  // nominated namespaces by those using-directives.
  //
  // FIXME: Cache this sorted list in Scope structure, and DeclContext, so we
  // don't build it for each lookup!
  if (!VisitedUsingDirectives) {
    UDirs.visitScopeChain(Initial, S);
    UDirs.done();
  }

  // If we're not performing redeclaration lookup, do not look for local
  // extern declarations outside of a function scope.
  if (!R.isForRedeclaration())
    FindLocals.restore();

  // Lookup namespace scope, and global scope.
  // Unqualified name lookup in C++ requires looking into scopes
  // that aren't strictly lexical, and therefore we walk through the
  // context as well as walking through the scopes.
  for (; S; S = S->getParent()) {
    // Check whether the IdResolver has anything in this scope.
    bool Found = false;
    for (; I != IEnd && S->isDeclScope(*I); ++I) {
      if (NamedDecl *ND = R.getAcceptableDecl(*I)) {
        // We found something.  Look for anything else in our scope
        // with this same name and in an acceptable identifier
        // namespace, so that we can construct an overload set if we
        // need to.
        Found = true;
        R.addDecl(ND);
      }
    }

    if (Found && S->isTemplateParamScope()) {
      R.resolveKind();
      return true;
    }

    DeclContext *Ctx = S->getEntity();
    if (!Ctx && S->isTemplateParamScope() && OutsideOfTemplateParamDC &&
        S->getParent() && !S->getParent()->isTemplateParamScope()) {
      // We've just searched the last template parameter scope and
      // found nothing, so look into the contexts between the
      // lexical and semantic declaration contexts returned by
      // findOuterContext(). This implements the name lookup behavior
      // of C++ [temp.local]p8.
      Ctx = OutsideOfTemplateParamDC;
      OutsideOfTemplateParamDC = nullptr;
    }

    if (Ctx) {
      DeclContext *OuterCtx;
      bool SearchAfterTemplateScope;
      std::tie(OuterCtx, SearchAfterTemplateScope) = findOuterContext(S);
      if (SearchAfterTemplateScope)
        OutsideOfTemplateParamDC = OuterCtx;

      for (; Ctx && !Ctx->Equals(OuterCtx); Ctx = Ctx->getLookupParent()) {
        // We do not directly look into transparent contexts, since
        // those entities will be found in the nearest enclosing
        // non-transparent context.
        if (Ctx->isTransparentContext())
          continue;

        // If we have a context, and it's not a context stashed in the
        // template parameter scope for an out-of-line definition, also
        // look into that context.
        if (!(Found && S->isTemplateParamScope())) {
          assert(Ctx->isFileContext() &&
              "We should have been looking only at file context here already.");

          // Look into context considering using-directives.
          if (CppNamespaceLookup(*this, R, Context, Ctx, UDirs))
            Found = true;
        }

        if (Found) {
          R.resolveKind();
          return true;
        }

        if (R.isForRedeclaration() && !Ctx->isTransparentContext())
          return false;
      }
    }

    if (R.isForRedeclaration() && Ctx && !Ctx->isTransparentContext())
      return false;
  }

  return !R.empty();
}

void Sema::makeMergedDefinitionVisible(NamedDecl *ND) {
  if (auto *M = getCurrentModule())
    Context.mergeDefinitionIntoModule(ND, M);
  else
    // We're not building a module; just make the definition visible.
    ND->setVisibleDespiteOwningModule();

  // If ND is a template declaration, make the template parameters
  // visible too. They're not (necessarily) within a mergeable DeclContext.
  if (auto *TD = dyn_cast<TemplateDecl>(ND))
    for (auto *Param : *TD->getTemplateParameters())
      makeMergedDefinitionVisible(Param);
}

/// Find the module in which the given declaration was defined.
static Module *getDefiningModule(Sema &S, Decl *Entity) {
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(Entity)) {
    // If this function was instantiated from a template, the defining module is
    // the module containing the pattern.
    if (FunctionDecl *Pattern = FD->getTemplateInstantiationPattern())
      Entity = Pattern;
  } else if (CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(Entity)) {
    if (CXXRecordDecl *Pattern = RD->getTemplateInstantiationPattern())
      Entity = Pattern;
  } else if (EnumDecl *ED = dyn_cast<EnumDecl>(Entity)) {
    if (auto *Pattern = ED->getTemplateInstantiationPattern())
      Entity = Pattern;
  } else if (VarDecl *VD = dyn_cast<VarDecl>(Entity)) {
    if (VarDecl *Pattern = VD->getTemplateInstantiationPattern())
      Entity = Pattern;
  }

  // Walk up to the containing context. That might also have been instantiated
  // from a template.
  DeclContext *Context = Entity->getLexicalDeclContext();
  if (Context->isFileContext())
    return S.getOwningModule(Entity);
  return getDefiningModule(S, cast<Decl>(Context));
}

llvm::DenseSet<Module*> &Sema::getLookupModules() {
  unsigned N = CodeSynthesisContexts.size();
  for (unsigned I = CodeSynthesisContextLookupModules.size();
       I != N; ++I) {
    Module *M = getDefiningModule(*this, CodeSynthesisContexts[I].Entity);
    if (M && !LookupModulesCache.insert(M).second)
      M = nullptr;
    CodeSynthesisContextLookupModules.push_back(M);
  }
  return LookupModulesCache;
}

/// Determine whether the module M is part of the current module from the
/// perspective of a module-private visibility check.
static bool isInCurrentModule(const Module *M, const LangOptions &LangOpts) {
  // If M is the global module fragment of a module that we've not yet finished
  // parsing, then it must be part of the current module.
  return M->getTopLevelModuleName() == LangOpts.CurrentModule ||
         (M->Kind == Module::GlobalModuleFragment && !M->Parent);
}

bool Sema::hasVisibleMergedDefinition(NamedDecl *Def) {
  for (const Module *Merged : Context.getModulesWithMergedDefinition(Def))
    if (isModuleVisible(Merged))
      return true;
  return false;
}

bool Sema::hasMergedDefinitionInCurrentModule(NamedDecl *Def) {
  for (const Module *Merged : Context.getModulesWithMergedDefinition(Def))
    if (isInCurrentModule(Merged, getLangOpts()))
      return true;
  return false;
}

template<typename ParmDecl>
static bool
hasVisibleDefaultArgument(Sema &S, const ParmDecl *D,
                          llvm::SmallVectorImpl<Module *> *Modules) {
  if (!D->hasDefaultArgument())
    return false;

  while (D) {
    auto &DefaultArg = D->getDefaultArgStorage();
    if (!DefaultArg.isInherited() && S.isVisible(D))
      return true;

    if (!DefaultArg.isInherited() && Modules) {
      auto *NonConstD = const_cast<ParmDecl*>(D);
      Modules->push_back(S.getOwningModule(NonConstD));
    }

    // If there was a previous default argument, maybe its parameter is visible.
    D = DefaultArg.getInheritedFrom();
  }
  return false;
}

bool Sema::hasVisibleDefaultArgument(const NamedDecl *D,
                                     llvm::SmallVectorImpl<Module *> *Modules) {
  if (auto *P = dyn_cast<TemplateTypeParmDecl>(D))
    return ::hasVisibleDefaultArgument(*this, P, Modules);
  if (auto *P = dyn_cast<NonTypeTemplateParmDecl>(D))
    return ::hasVisibleDefaultArgument(*this, P, Modules);
  return ::hasVisibleDefaultArgument(*this, cast<TemplateTemplateParmDecl>(D),
                                     Modules);
}

template<typename Filter>
static bool hasVisibleDeclarationImpl(Sema &S, const NamedDecl *D,
                                      llvm::SmallVectorImpl<Module *> *Modules,
                                      Filter F) {
  bool HasFilteredRedecls = false;

  for (auto *Redecl : D->redecls()) {
    auto *R = cast<NamedDecl>(Redecl);
    if (!F(R))
      continue;

    if (S.isVisible(R))
      return true;

    HasFilteredRedecls = true;

    if (Modules)
      Modules->push_back(R->getOwningModule());
  }

  // Only return false if there is at least one redecl that is not filtered out.
  if (HasFilteredRedecls)
    return false;

  return true;
}

bool Sema::hasVisibleExplicitSpecialization(
    const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules) {
  return hasVisibleDeclarationImpl(*this, D, Modules, [](const NamedDecl *D) {
    if (auto *RD = dyn_cast<CXXRecordDecl>(D))
      return RD->getTemplateSpecializationKind() == TSK_ExplicitSpecialization;
    if (auto *FD = dyn_cast<FunctionDecl>(D))
      return FD->getTemplateSpecializationKind() == TSK_ExplicitSpecialization;
    if (auto *VD = dyn_cast<VarDecl>(D))
      return VD->getTemplateSpecializationKind() == TSK_ExplicitSpecialization;
    llvm_unreachable("unknown explicit specialization kind");
  });
}

bool Sema::hasVisibleMemberSpecialization(
    const NamedDecl *D, llvm::SmallVectorImpl<Module *> *Modules) {
  assert(isa<CXXRecordDecl>(D->getDeclContext()) &&
         "not a member specialization");
  return hasVisibleDeclarationImpl(*this, D, Modules, [](const NamedDecl *D) {
    // If the specialization is declared at namespace scope, then it's a member
    // specialization declaration. If it's lexically inside the class
    // definition then it was instantiated.
    //
    // FIXME: This is a hack. There should be a better way to determine this.
    // FIXME: What about MS-style explicit specializations declared within a
    //        class definition?
    return D->getLexicalDeclContext()->isFileContext();
  });
}

/// Determine whether a declaration is visible to name lookup.
///
/// This routine determines whether the declaration D is visible in the current
/// lookup context, taking into account the current template instantiation
/// stack. During template instantiation, a declaration is visible if it is
/// visible from a module containing any entity on the template instantiation
/// path (by instantiating a template, you allow it to see the declarations that
/// your module can see, including those later on in your module).
bool LookupResult::isVisibleSlow(Sema &SemaRef, NamedDecl *D) {
  assert(D->isHidden() && "should not call this: not in slow case");

  Module *DeclModule = SemaRef.getOwningModule(D);
  assert(DeclModule && "hidden decl has no owning module");

  // If the owning module is visible, the decl is visible.
  if (SemaRef.isModuleVisible(DeclModule, D->isModulePrivate()))
    return true;

  // Determine whether a decl context is a file context for the purpose of
  // visibility. This looks through some (export and linkage spec) transparent
  // contexts, but not others (enums).
  auto IsEffectivelyFileContext = [](const DeclContext *DC) {
    return DC->isFileContext() || isa<LinkageSpecDecl>(DC) ||
           isa<ExportDecl>(DC);
  };

  // If this declaration is not at namespace scope
  // then it is visible if its lexical parent has a visible definition.
  DeclContext *DC = D->getLexicalDeclContext();
  if (DC && !IsEffectivelyFileContext(DC)) {
    // For a parameter, check whether our current template declaration's
    // lexical context is visible, not whether there's some other visible
    // definition of it, because parameters aren't "within" the definition.
    //
    // In C++ we need to check for a visible definition due to ODR merging,
    // and in C we must not because each declaration of a function gets its own
    // set of declarations for tags in prototype scope.
    bool VisibleWithinParent;
    if (D->isTemplateParameter() || isa<ParmVarDecl>(D) ||
        (isa<FunctionDecl>(DC) && !SemaRef.getLangOpts().CPlusPlus))
      VisibleWithinParent = isVisible(SemaRef, cast<NamedDecl>(DC));
    else if (D->isModulePrivate()) {
      // A module-private declaration is only visible if an enclosing lexical
      // parent was merged with another definition in the current module.
      VisibleWithinParent = false;
      do {
        if (SemaRef.hasMergedDefinitionInCurrentModule(cast<NamedDecl>(DC))) {
          VisibleWithinParent = true;
          break;
        }
        DC = DC->getLexicalParent();
      } while (!IsEffectivelyFileContext(DC));
    } else {
      VisibleWithinParent = SemaRef.hasVisibleDefinition(cast<NamedDecl>(DC));
    }

    if (VisibleWithinParent && SemaRef.CodeSynthesisContexts.empty() &&
        // FIXME: Do something better in this case.
        !SemaRef.getLangOpts().ModulesLocalVisibility) {
      // Cache the fact that this declaration is implicitly visible because
      // its parent has a visible definition.
      D->setVisibleDespiteOwningModule();
    }
    return VisibleWithinParent;
  }

  return false;
}

bool Sema::isModuleVisible(const Module *M, bool ModulePrivate) {
  // The module might be ordinarily visible. For a module-private query, that
  // means it is part of the current module. For any other query, that means it
  // is in our visible module set.
  if (ModulePrivate) {
    if (isInCurrentModule(M, getLangOpts()))
      return true;
  } else {
    if (VisibleModules.isVisible(M))
      return true;
  }

  // Otherwise, it might be visible by virtue of the query being within a
  // template instantiation or similar that is permitted to look inside M.

  // Find the extra places where we need to look.
  const auto &LookupModules = getLookupModules();
  if (LookupModules.empty())
    return false;

  // If our lookup set contains the module, it's visible.
  if (LookupModules.count(M))
    return true;

  // For a module-private query, that's everywhere we get to look.
  if (ModulePrivate)
    return false;

  // Check whether M is transitively exported to an import of the lookup set.
  return llvm::any_of(LookupModules, [&](const Module *LookupM) {
    return LookupM->isModuleVisible(M);
  });
}

bool Sema::isVisibleSlow(const NamedDecl *D) {
  return LookupResult::isVisible(*this, const_cast<NamedDecl*>(D));
}

bool Sema::shouldLinkPossiblyHiddenDecl(LookupResult &R, const NamedDecl *New) {
  // FIXME: If there are both visible and hidden declarations, we need to take
  // into account whether redeclaration is possible. Example:
  //
  // Non-imported module:
  //   int f(T);        // #1
  // Some TU:
  //   static int f(U); // #2, not a redeclaration of #1
  //   int f(T);        // #3, finds both, should link with #1 if T != U, but
  //                    // with #2 if T == U; neither should be ambiguous.
  for (auto *D : R) {
    if (isVisible(D))
      return true;
    assert(D->isExternallyDeclarable() &&
           "should not have hidden, non-externally-declarable result here");
  }

  // This function is called once "New" is essentially complete, but before a
  // previous declaration is attached. We can't query the linkage of "New" in
  // general, because attaching the previous declaration can change the
  // linkage of New to match the previous declaration.
  //
  // However, because we've just determined that there is no *visible* prior
  // declaration, we can compute the linkage here. There are two possibilities:
  //
  //  * This is not a redeclaration; it's safe to compute the linkage now.
  //
  //  * This is a redeclaration of a prior declaration that is externally
  //    redeclarable. In that case, the linkage of the declaration is not
  //    changed by attaching the prior declaration, because both are externally
  //    declarable (and thus ExternalLinkage or VisibleNoLinkage).
  //
  // FIXME: This is subtle and fragile.
  return New->isExternallyDeclarable();
}

/// Retrieve the visible declaration corresponding to D, if any.
///
/// This routine determines whether the declaration D is visible in the current
/// module, with the current imports. If not, it checks whether any
/// redeclaration of D is visible, and if so, returns that declaration.
///
/// \returns D, or a visible previous declaration of D, whichever is more recent
/// and visible. If no declaration of D is visible, returns null.
static NamedDecl *findAcceptableDecl(Sema &SemaRef, NamedDecl *D,
                                     unsigned IDNS) {
  assert(!LookupResult::isVisible(SemaRef, D) && "not in slow case");

  for (auto RD : D->redecls()) {
    // Don't bother with extra checks if we already know this one isn't visible.
    if (RD == D)
      continue;

    auto ND = cast<NamedDecl>(RD);
    // FIXME: This is wrong in the case where the previous declaration is not
    // visible in the same scope as D. This needs to be done much more
    // carefully.
    if (ND->isInIdentifierNamespace(IDNS) &&
        LookupResult::isVisible(SemaRef, ND))
      return ND;
  }

  return nullptr;
}

bool Sema::hasVisibleDeclarationSlow(const NamedDecl *D,
                                     llvm::SmallVectorImpl<Module *> *Modules) {
  assert(!isVisible(D) && "not in slow case");
  return hasVisibleDeclarationImpl(*this, D, Modules,
                                   [](const NamedDecl *) { return true; });
}

NamedDecl *LookupResult::getAcceptableDeclSlow(NamedDecl *D) const {
  if (auto *ND = dyn_cast<NamespaceDecl>(D)) {
    // Namespaces are a bit of a special case: we expect there to be a lot of
    // redeclarations of some namespaces, all declarations of a namespace are
    // essentially interchangeable, all declarations are found by name lookup
    // if any is, and namespaces are never looked up during template
    // instantiation. So we benefit from caching the check in this case, and
    // it is correct to do so.
    auto *Key = ND->getCanonicalDecl();
    if (auto *Acceptable = getSema().VisibleNamespaceCache.lookup(Key))
      return Acceptable;
    auto *Acceptable = isVisible(getSema(), Key)
                           ? Key
                           : findAcceptableDecl(getSema(), Key, IDNS);
    if (Acceptable)
      getSema().VisibleNamespaceCache.insert(std::make_pair(Key, Acceptable));
    return Acceptable;
  }

  return findAcceptableDecl(getSema(), D, IDNS);
}

/// Perform unqualified name lookup starting from a given
/// scope.
///
/// Unqualified name lookup (C++ [basic.lookup.unqual], C99 6.2.1) is
/// used to find names within the current scope. For example, 'x' in
/// @code
/// int x;
/// int f() {
///   return x; // unqualified name look finds 'x' in the global scope
/// }
/// @endcode
///
/// Different lookup criteria can find different names. For example, a
/// particular scope can have both a struct and a function of the same
/// name, and each can be found by certain lookup criteria. For more
/// information about lookup criteria, see the documentation for the
/// class LookupCriteria.
///
/// @param S        The scope from which unqualified name lookup will
/// begin. If the lookup criteria permits, name lookup may also search
/// in the parent scopes.
///
/// @param [in,out] R Specifies the lookup to perform (e.g., the name to
/// look up and the lookup kind), and is updated with the results of lookup
/// including zero or more declarations and possibly additional information
/// used to diagnose ambiguities.
///
/// @returns \c true if lookup succeeded and false otherwise.
bool Sema::LookupName(LookupResult &R, Scope *S, bool AllowBuiltinCreation) {
  DeclarationName Name = R.getLookupName();
  if (!Name) return false;

  LookupNameKind NameKind = R.getLookupKind();

  if (!getLangOpts().CPlusPlus) {
    // Unqualified name lookup in C/Objective-C is purely lexical, so
    // search in the declarations attached to the name.
    if (NameKind == Sema::LookupRedeclarationWithLinkage) {
      // Find the nearest non-transparent declaration scope.
      while (!(S->getFlags() & Scope::DeclScope) ||
             (S->getEntity() && S->getEntity()->isTransparentContext()))
        S = S->getParent();
    }

    // When performing a scope lookup, we want to find local extern decls.
    FindLocalExternScope FindLocals(R);

    // Scan up the scope chain looking for a decl that matches this
    // identifier that is in the appropriate namespace.  This search
    // should not take long, as shadowing of names is uncommon, and
    // deep shadowing is extremely uncommon.
    bool LeftStartingScope = false;

    for (IdentifierResolver::iterator I = IdResolver.begin(Name),
                                   IEnd = IdResolver.end();
         I != IEnd; ++I)
      if (NamedDecl *D = R.getAcceptableDecl(*I)) {
        if (NameKind == LookupRedeclarationWithLinkage) {
          // Determine whether this (or a previous) declaration is
          // out-of-scope.
          if (!LeftStartingScope && !S->isDeclScope(*I))
            LeftStartingScope = true;

          // If we found something outside of our starting scope that
          // does not have linkage, skip it.
          if (LeftStartingScope && !((*I)->hasLinkage())) {
            R.setShadowed();
            continue;
          }
        }
        else if (NameKind == LookupObjCImplicitSelfParam &&
                 !isa<ImplicitParamDecl>(*I))
          continue;

        R.addDecl(D);

        // Check whether there are any other declarations with the same name
        // and in the same scope.
        if (I != IEnd) {
          // Find the scope in which this declaration was declared (if it
          // actually exists in a Scope).
          while (S && !S->isDeclScope(D))
            S = S->getParent();

          // If the scope containing the declaration is the translation unit,
          // then we'll need to perform our checks based on the matching
          // DeclContexts rather than matching scopes.
          if (S && isNamespaceOrTranslationUnitScope(S))
            S = nullptr;

          // Compute the DeclContext, if we need it.
          DeclContext *DC = nullptr;
          if (!S)
            DC = (*I)->getDeclContext()->getRedeclContext();

          IdentifierResolver::iterator LastI = I;
          for (++LastI; LastI != IEnd; ++LastI) {
            if (S) {
              // Match based on scope.
              if (!S->isDeclScope(*LastI))
                break;
            } else {
              // Match based on DeclContext.
              DeclContext *LastDC
                = (*LastI)->getDeclContext()->getRedeclContext();
              if (!LastDC->Equals(DC))
                break;
            }

            // If the declaration is in the right namespace and visible, add it.
            if (NamedDecl *LastD = R.getAcceptableDecl(*LastI))
              R.addDecl(LastD);
          }

          R.resolveKind();
        }

        return true;
      }
  } else {
    // Perform C++ unqualified name lookup.
    if (CppLookupName(R, S))
      return true;
  }

  // If we didn't find a use of this identifier, and if the identifier
  // corresponds to a compiler builtin, create the decl object for the builtin
  // now, injecting it into translation unit scope, and return it.
  if (AllowBuiltinCreation && LookupBuiltin(*this, R))
    return true;

  // If we didn't find a use of this identifier, the ExternalSource
  // may be able to handle the situation.
  // Note: some lookup failures are expected!
  // See e.g. R.isForRedeclaration().
  return (ExternalSource && ExternalSource->LookupUnqualified(R, S));
}

/// Perform qualified name lookup in the namespaces nominated by
/// using directives by the given context.
///
/// C++98 [namespace.qual]p2:
///   Given X::m (where X is a user-declared namespace), or given \::m
///   (where X is the global namespace), let S be the set of all
///   declarations of m in X and in the transitive closure of all
///   namespaces nominated by using-directives in X and its used
///   namespaces, except that using-directives are ignored in any
///   namespace, including X, directly containing one or more
///   declarations of m. No namespace is searched more than once in
///   the lookup of a name. If S is the empty set, the program is
///   ill-formed. Otherwise, if S has exactly one member, or if the
///   context of the reference is a using-declaration
///   (namespace.udecl), S is the required set of declarations of
///   m. Otherwise if the use of m is not one that allows a unique
///   declaration to be chosen from S, the program is ill-formed.
///
/// C++98 [namespace.qual]p5:
///   During the lookup of a qualified namespace member name, if the
///   lookup finds more than one declaration of the member, and if one
///   declaration introduces a class name or enumeration name and the
///   other declarations either introduce the same object, the same
///   enumerator or a set of functions, the non-type name hides the
///   class or enumeration name if and only if the declarations are
///   from the same namespace; otherwise (the declarations are from
///   different namespaces), the program is ill-formed.
static bool LookupQualifiedNameInUsingDirectives(Sema &S, LookupResult &R,
                                                 DeclContext *StartDC) {
  assert(StartDC->isFileContext() && "start context is not a file context");

  // We have not yet looked into these namespaces, much less added
  // their "using-children" to the queue.
  SmallVector<NamespaceDecl*, 8> Queue;

  // We have at least added all these contexts to the queue.
  llvm::SmallPtrSet<DeclContext*, 8> Visited;
  Visited.insert(StartDC);

  // We have already looked into the initial namespace; seed the queue
  // with its using-children.
  for (auto *I : StartDC->using_directives()) {
    NamespaceDecl *ND = I->getNominatedNamespace()->getOriginalNamespace();
    if (S.isVisible(I) && Visited.insert(ND).second)
      Queue.push_back(ND);
  }

  // The easiest way to implement the restriction in [namespace.qual]p5
  // is to check whether any of the individual results found a tag
  // and, if so, to declare an ambiguity if the final result is not
  // a tag.
  bool FoundTag = false;
  bool FoundNonTag = false;

  LookupResult LocalR(LookupResult::Temporary, R);

  bool Found = false;
  while (!Queue.empty()) {
    NamespaceDecl *ND = Queue.pop_back_val();

    // We go through some convolutions here to avoid copying results
    // between LookupResults.
    bool UseLocal = !R.empty();
    LookupResult &DirectR = UseLocal ? LocalR : R;
    bool FoundDirect = LookupDirect(S, DirectR, ND);

    if (FoundDirect) {
      // First do any local hiding.
      DirectR.resolveKind();

      // If the local result is a tag, remember that.
      if (DirectR.isSingleTagDecl())
        FoundTag = true;
      else
        FoundNonTag = true;

      // Append the local results to the total results if necessary.
      if (UseLocal) {
        R.addAllDecls(LocalR);
        LocalR.clear();
      }
    }

    // If we find names in this namespace, ignore its using directives.
    if (FoundDirect) {
      Found = true;
      continue;
    }

    for (auto I : ND->using_directives()) {
      NamespaceDecl *Nom = I->getNominatedNamespace();
      if (S.isVisible(I) && Visited.insert(Nom).second)
        Queue.push_back(Nom);
    }
  }

  if (Found) {
    if (FoundTag && FoundNonTag)
      R.setAmbiguousQualifiedTagHiding();
    else
      R.resolveKind();
  }

  return Found;
}

/// Callback that looks for any member of a class with the given name.
static bool LookupAnyMember(const CXXBaseSpecifier *Specifier,
                            CXXBasePath &Path, DeclarationName Name) {
  RecordDecl *BaseRecord = Specifier->getType()->getAs<RecordType>()->getDecl();

  Path.Decls = BaseRecord->lookup(Name);
  return !Path.Decls.empty();
}

/// Determine whether the given set of member declarations contains only
/// static members, nested types, and enumerators.
template<typename InputIterator>
static bool HasOnlyStaticMembers(InputIterator First, InputIterator Last) {
  Decl *D = (*First)->getUnderlyingDecl();
  if (isa<VarDecl>(D) || isa<TypeDecl>(D) || isa<EnumConstantDecl>(D))
    return true;

  if (isa<CXXMethodDecl>(D)) {
    // Determine whether all of the methods are static.
    bool AllMethodsAreStatic = true;
    for(; First != Last; ++First) {
      D = (*First)->getUnderlyingDecl();

      if (!isa<CXXMethodDecl>(D)) {
        assert(isa<TagDecl>(D) && "Non-function must be a tag decl");
        break;
      }

      if (!cast<CXXMethodDecl>(D)->isStatic()) {
        AllMethodsAreStatic = false;
        break;
      }
    }

    if (AllMethodsAreStatic)
      return true;
  }

  return false;
}

/// Perform qualified name lookup into a given context.
///
/// Qualified name lookup (C++ [basic.lookup.qual]) is used to find
/// names when the context of those names is explicit specified, e.g.,
/// "std::vector" or "x->member", or as part of unqualified name lookup.
///
/// Different lookup criteria can find different names. For example, a
/// particular scope can have both a struct and a function of the same
/// name, and each can be found by certain lookup criteria. For more
/// information about lookup criteria, see the documentation for the
/// class LookupCriteria.
///
/// \param R captures both the lookup criteria and any lookup results found.
///
/// \param LookupCtx The context in which qualified name lookup will
/// search. If the lookup criteria permits, name lookup may also search
/// in the parent contexts or (for C++ classes) base classes.
///
/// \param InUnqualifiedLookup true if this is qualified name lookup that
/// occurs as part of unqualified name lookup.
///
/// \returns true if lookup succeeded, false if it failed.
bool Sema::LookupQualifiedName(LookupResult &R, DeclContext *LookupCtx,
                               bool InUnqualifiedLookup) {
  assert(LookupCtx && "Sema::LookupQualifiedName requires a lookup context");

  if (!R.getLookupName())
    return false;

  // Make sure that the declaration context is complete.
  assert((!isa<TagDecl>(LookupCtx) ||
          LookupCtx->isDependentContext() ||
          cast<TagDecl>(LookupCtx)->isCompleteDefinition() ||
          cast<TagDecl>(LookupCtx)->isBeingDefined()) &&
         "Declaration context must already be complete!");

  struct QualifiedLookupInScope {
    bool oldVal;
    DeclContext *Context;
    // Set flag in DeclContext informing debugger that we're looking for qualified name
    QualifiedLookupInScope(DeclContext *ctx) : Context(ctx) {
      oldVal = ctx->setUseQualifiedLookup();
    }
    ~QualifiedLookupInScope() {
      Context->setUseQualifiedLookup(oldVal);
    }
  } QL(LookupCtx);

  if (LookupDirect(*this, R, LookupCtx)) {
    R.resolveKind();
    if (isa<CXXRecordDecl>(LookupCtx))
      R.setNamingClass(cast<CXXRecordDecl>(LookupCtx));
    return true;
  }

  // Don't descend into implied contexts for redeclarations.
  // C++98 [namespace.qual]p6:
  //   In a declaration for a namespace member in which the
  //   declarator-id is a qualified-id, given that the qualified-id
  //   for the namespace member has the form
  //     nested-name-specifier unqualified-id
  //   the unqualified-id shall name a member of the namespace
  //   designated by the nested-name-specifier.
  // See also [class.mfct]p5 and [class.static.data]p2.
  if (R.isForRedeclaration())
    return false;

  // If this is a namespace, look it up in the implied namespaces.
  if (LookupCtx->isFileContext())
    return LookupQualifiedNameInUsingDirectives(*this, R, LookupCtx);

  // If this isn't a C++ class, we aren't allowed to look into base
  // classes, we're done.
  CXXRecordDecl *LookupRec = dyn_cast<CXXRecordDecl>(LookupCtx);
  if (!LookupRec || !LookupRec->getDefinition())
    return false;

  // If we're performing qualified name lookup into a dependent class,
  // then we are actually looking into a current instantiation. If we have any
  // dependent base classes, then we either have to delay lookup until
  // template instantiation time (at which point all bases will be available)
  // or we have to fail.
  if (!InUnqualifiedLookup && LookupRec->isDependentContext() &&
      LookupRec->hasAnyDependentBases()) {
    R.setNotFoundInCurrentInstantiation();
    return false;
  }

  // Perform lookup into our base classes.
  CXXBasePaths Paths;
  Paths.setOrigin(LookupRec);

  // Look for this member in our base classes
  bool (*BaseCallback)(const CXXBaseSpecifier *Specifier, CXXBasePath &Path,
                       DeclarationName Name) = nullptr;
  switch (R.getLookupKind()) {
    case LookupObjCImplicitSelfParam:
    case LookupOrdinaryName:
    case LookupMemberName:
    case LookupRedeclarationWithLinkage:
    case LookupLocalFriendName:
      BaseCallback = &CXXRecordDecl::FindOrdinaryMember;
      break;

    case LookupTagName:
      BaseCallback = &CXXRecordDecl::FindTagMember;
      break;

    case LookupAnyName:
      BaseCallback = &LookupAnyMember;
      break;

    case LookupOMPReductionName:
      BaseCallback = &CXXRecordDecl::FindOMPReductionMember;
      break;

    case LookupUsingDeclName:
      // This lookup is for redeclarations only.

    case LookupOperatorName:
    case LookupNamespaceName:
    case LookupObjCProtocolName:
    case LookupLabel:
      // These lookups will never find a member in a C++ class (or base class).
      return false;

    case LookupNestedNameSpecifierName:
      BaseCallback = &CXXRecordDecl::FindNestedNameSpecifierMember;
      break;
  }

  DeclarationName Name = R.getLookupName();
  if (!LookupRec->lookupInBases(
          [=](const CXXBaseSpecifier *Specifier, CXXBasePath &Path) {
            return BaseCallback(Specifier, Path, Name);
          },
          Paths))
    return false;

  R.setNamingClass(LookupRec);

  // C++ [class.member.lookup]p2:
  //   [...] If the resulting set of declarations are not all from
  //   sub-objects of the same type, or the set has a nonstatic member
  //   and includes members from distinct sub-objects, there is an
  //   ambiguity and the program is ill-formed. Otherwise that set is
  //   the result of the lookup.
  QualType SubobjectType;
  int SubobjectNumber = 0;
  AccessSpecifier SubobjectAccess = AS_none;

  for (CXXBasePaths::paths_iterator Path = Paths.begin(), PathEnd = Paths.end();
       Path != PathEnd; ++Path) {
    const CXXBasePathElement &PathElement = Path->back();

    // Pick the best (i.e. most permissive i.e. numerically lowest) access
    // across all paths.
    SubobjectAccess = std::min(SubobjectAccess, Path->Access);

    // Determine whether we're looking at a distinct sub-object or not.
    if (SubobjectType.isNull()) {
      // This is the first subobject we've looked at. Record its type.
      SubobjectType = Context.getCanonicalType(PathElement.Base->getType());
      SubobjectNumber = PathElement.SubobjectNumber;
      continue;
    }

    if (SubobjectType
                 != Context.getCanonicalType(PathElement.Base->getType())) {
      // We found members of the given name in two subobjects of
      // different types. If the declaration sets aren't the same, this
      // lookup is ambiguous.
      if (HasOnlyStaticMembers(Path->Decls.begin(), Path->Decls.end())) {
        CXXBasePaths::paths_iterator FirstPath = Paths.begin();
        DeclContext::lookup_iterator FirstD = FirstPath->Decls.begin();
        DeclContext::lookup_iterator CurrentD = Path->Decls.begin();

        while (FirstD != FirstPath->Decls.end() &&
               CurrentD != Path->Decls.end()) {
         if ((*FirstD)->getUnderlyingDecl()->getCanonicalDecl() !=
             (*CurrentD)->getUnderlyingDecl()->getCanonicalDecl())
           break;

          ++FirstD;
          ++CurrentD;
        }

        if (FirstD == FirstPath->Decls.end() &&
            CurrentD == Path->Decls.end())
          continue;
      }

      R.setAmbiguousBaseSubobjectTypes(Paths);
      return true;
    }

    if (SubobjectNumber != PathElement.SubobjectNumber) {
      // We have a different subobject of the same type.

      // C++ [class.member.lookup]p5:
      //   A static member, a nested type or an enumerator defined in
      //   a base class T can unambiguously be found even if an object
      //   has more than one base class subobject of type T.
      if (HasOnlyStaticMembers(Path->Decls.begin(), Path->Decls.end()))
        continue;

      // We have found a nonstatic member name in multiple, distinct
      // subobjects. Name lookup is ambiguous.
      R.setAmbiguousBaseSubobjects(Paths);
      return true;
    }
  }

  // Lookup in a base class succeeded; return these results.

  for (auto *D : Paths.front().Decls) {
    AccessSpecifier AS = CXXRecordDecl::MergeAccess(SubobjectAccess,
                                                    D->getAccess());
    R.addDecl(D, AS);
  }
  R.resolveKind();
  return true;
}

/// Performs qualified name lookup or special type of lookup for
/// "__super::" scope specifier.
///
/// This routine is a convenience overload meant to be called from contexts
/// that need to perform a qualified name lookup with an optional C++ scope
/// specifier that might require special kind of lookup.
///
/// \param R captures both the lookup criteria and any lookup results found.
///
/// \param LookupCtx The context in which qualified name lookup will
/// search.
///
/// \param SS An optional C++ scope-specifier.
///
/// \returns true if lookup succeeded, false if it failed.
bool Sema::LookupQualifiedName(LookupResult &R, DeclContext *LookupCtx,
                               CXXScopeSpec &SS) {
  auto *NNS = SS.getScopeRep();
  if (NNS && NNS->getKind() == NestedNameSpecifier::Super)
    return LookupInSuper(R, NNS->getAsRecordDecl());
  else

    return LookupQualifiedName(R, LookupCtx);
}

/// Performs name lookup for a name that was parsed in the
/// source code, and may contain a C++ scope specifier.
///
/// This routine is a convenience routine meant to be called from
/// contexts that receive a name and an optional C++ scope specifier
/// (e.g., "N::M::x"). It will then perform either qualified or
/// unqualified name lookup (with LookupQualifiedName or LookupName,
/// respectively) on the given name and return those results. It will
/// perform a special type of lookup for "__super::" scope specifier.
///
/// @param S        The scope from which unqualified name lookup will
/// begin.
///
/// @param SS       An optional C++ scope-specifier, e.g., "::N::M".
///
/// @param EnteringContext Indicates whether we are going to enter the
/// context of the scope-specifier SS (if present).
///
/// @returns True if any decls were found (but possibly ambiguous)
bool Sema::LookupParsedName(LookupResult &R, Scope *S, CXXScopeSpec *SS,
                            bool AllowBuiltinCreation, bool EnteringContext) {
  if (SS && SS->isInvalid()) {
    // When the scope specifier is invalid, don't even look for
    // anything.
    return false;
  }

  if (SS && SS->isSet()) {
    NestedNameSpecifier *NNS = SS->getScopeRep();
    if (NNS->getKind() == NestedNameSpecifier::Super)
      return LookupInSuper(R, NNS->getAsRecordDecl());

    if (DeclContext *DC = computeDeclContext(*SS, EnteringContext)) {
      // We have resolved the scope specifier to a particular declaration
      // contex, and will perform name lookup in that context.
      if (!DC->isDependentContext() && RequireCompleteDeclContext(*SS, DC))
        return false;

      R.setContextRange(SS->getRange());
      return LookupQualifiedName(R, DC);
    }

    // We could not resolve the scope specified to a specific declaration
    // context, which means that SS refers to an unknown specialization.
    // Name lookup can't find anything in this case.
    R.setNotFoundInCurrentInstantiation();
    R.setContextRange(SS->getRange());
    return false;
  }

  // Perform unqualified name lookup starting in the given scope.
  return LookupName(R, S, AllowBuiltinCreation);
}

/// Perform qualified name lookup into all base classes of the given
/// class.
///
/// \param R captures both the lookup criteria and any lookup results found.
///
/// \param Class The context in which qualified name lookup will
/// search. Name lookup will search in all base classes merging the results.
///
/// @returns True if any decls were found (but possibly ambiguous)
bool Sema::LookupInSuper(LookupResult &R, CXXRecordDecl *Class) {
  // The access-control rules we use here are essentially the rules for
  // doing a lookup in Class that just magically skipped the direct
  // members of Class itself.  That is, the naming class is Class, and the
  // access includes the access of the base.
  for (const auto &BaseSpec : Class->bases()) {
    CXXRecordDecl *RD = cast<CXXRecordDecl>(
        BaseSpec.getType()->castAs<RecordType>()->getDecl());
    LookupResult Result(*this, R.getLookupNameInfo(), R.getLookupKind());
    Result.setBaseObjectType(Context.getRecordType(Class));
    LookupQualifiedName(Result, RD);

    // Copy the lookup results into the target, merging the base's access into
    // the path access.
    for (auto I = Result.begin(), E = Result.end(); I != E; ++I) {
      R.addDecl(I.getDecl(),
                CXXRecordDecl::MergeAccess(BaseSpec.getAccessSpecifier(),
                                           I.getAccess()));
    }

    Result.suppressDiagnostics();
  }

  R.resolveKind();
  R.setNamingClass(Class);

  return !R.empty();
}

/// Produce a diagnostic describing the ambiguity that resulted
/// from name lookup.
///
/// \param Result The result of the ambiguous lookup to be diagnosed.
void Sema::DiagnoseAmbiguousLookup(LookupResult &Result) {
  assert(Result.isAmbiguous() && "Lookup result must be ambiguous");

  DeclarationName Name = Result.getLookupName();
  SourceLocation NameLoc = Result.getNameLoc();
  SourceRange LookupRange = Result.getContextRange();

  switch (Result.getAmbiguityKind()) {
  case LookupResult::AmbiguousBaseSubobjects: {
    CXXBasePaths *Paths = Result.getBasePaths();
    QualType SubobjectType = Paths->front().back().Base->getType();
    Diag(NameLoc, diag::err_ambiguous_member_multiple_subobjects)
      << Name << SubobjectType << getAmbiguousPathsDisplayString(*Paths)
      << LookupRange;

    DeclContext::lookup_iterator Found = Paths->front().Decls.begin();
    while (isa<CXXMethodDecl>(*Found) &&
           cast<CXXMethodDecl>(*Found)->isStatic())
      ++Found;

    Diag((*Found)->getLocation(), diag::note_ambiguous_member_found);
    break;
  }

  case LookupResult::AmbiguousBaseSubobjectTypes: {
    Diag(NameLoc, diag::err_ambiguous_member_multiple_subobject_types)
      << Name << LookupRange;

    CXXBasePaths *Paths = Result.getBasePaths();
    std::set<Decl *> DeclsPrinted;
    for (CXXBasePaths::paths_iterator Path = Paths->begin(),
                                      PathEnd = Paths->end();
         Path != PathEnd; ++Path) {
      Decl *D = Path->Decls.front();
      if (DeclsPrinted.insert(D).second)
        Diag(D->getLocation(), diag::note_ambiguous_member_found);
    }
    break;
  }

  case LookupResult::AmbiguousTagHiding: {
    Diag(NameLoc, diag::err_ambiguous_tag_hiding) << Name << LookupRange;

    llvm::SmallPtrSet<NamedDecl*, 8> TagDecls;

    for (auto *D : Result)
      if (TagDecl *TD = dyn_cast<TagDecl>(D)) {
        TagDecls.insert(TD);
        Diag(TD->getLocation(), diag::note_hidden_tag);
      }

    for (auto *D : Result)
      if (!isa<TagDecl>(D))
        Diag(D->getLocation(), diag::note_hiding_object);

    // For recovery purposes, go ahead and implement the hiding.
    LookupResult::Filter F = Result.makeFilter();
    while (F.hasNext()) {
      if (TagDecls.count(F.next()))
        F.erase();
    }
    F.done();
    break;
  }

  case LookupResult::AmbiguousReference: {
    Diag(NameLoc, diag::err_ambiguous_reference) << Name << LookupRange;

    for (auto *D : Result)
      Diag(D->getLocation(), diag::note_ambiguous_candidate) << D;
    break;
  }
  }
}

namespace {
  struct AssociatedLookup {
    AssociatedLookup(Sema &S, SourceLocation InstantiationLoc,
                     Sema::AssociatedNamespaceSet &Namespaces,
                     Sema::AssociatedClassSet &Classes)
      : S(S), Namespaces(Namespaces), Classes(Classes),
        InstantiationLoc(InstantiationLoc) {
    }

    Sema &S;
    Sema::AssociatedNamespaceSet &Namespaces;
    Sema::AssociatedClassSet &Classes;
    SourceLocation InstantiationLoc;
  };
} // end anonymous namespace

static void
addAssociatedClassesAndNamespaces(AssociatedLookup &Result, QualType T);

static void CollectEnclosingNamespace(Sema::AssociatedNamespaceSet &Namespaces,
                                      DeclContext *Ctx) {
  // Add the associated namespace for this class.

  // We don't use DeclContext::getEnclosingNamespaceContext() as this may
  // be a locally scoped record.

  // We skip out of inline namespaces. The innermost non-inline namespace
  // contains all names of all its nested inline namespaces anyway, so we can
  // replace the entire inline namespace tree with its root.
  while (Ctx->isRecord() || Ctx->isTransparentContext() ||
         Ctx->isInlineNamespace())
    Ctx = Ctx->getParent();

  if (Ctx->isFileContext())
    Namespaces.insert(Ctx->getPrimaryContext());
}

// Add the associated classes and namespaces for argument-dependent
// lookup that involves a template argument (C++ [basic.lookup.koenig]p2).
static void
addAssociatedClassesAndNamespaces(AssociatedLookup &Result,
                                  const TemplateArgument &Arg) {
  // C++ [basic.lookup.koenig]p2, last bullet:
  //   -- [...] ;
  switch (Arg.getKind()) {
    case TemplateArgument::Null:
      break;

    case TemplateArgument::Type:
      // [...] the namespaces and classes associated with the types of the
      // template arguments provided for template type parameters (excluding
      // template template parameters)
      addAssociatedClassesAndNamespaces(Result, Arg.getAsType());
      break;

    case TemplateArgument::Template:
    case TemplateArgument::TemplateExpansion: {
      // [...] the namespaces in which any template template arguments are
      // defined; and the classes in which any member templates used as
      // template template arguments are defined.
      TemplateName Template = Arg.getAsTemplateOrTemplatePattern();
      if (ClassTemplateDecl *ClassTemplate
                 = dyn_cast<ClassTemplateDecl>(Template.getAsTemplateDecl())) {
        DeclContext *Ctx = ClassTemplate->getDeclContext();
        if (CXXRecordDecl *EnclosingClass = dyn_cast<CXXRecordDecl>(Ctx))
          Result.Classes.insert(EnclosingClass);
        // Add the associated namespace for this class.
        CollectEnclosingNamespace(Result.Namespaces, Ctx);
      }
      break;
    }

    case TemplateArgument::Declaration:
    case TemplateArgument::Integral:
    case TemplateArgument::Expression:
    case TemplateArgument::NullPtr:
      // [Note: non-type template arguments do not contribute to the set of
      //  associated namespaces. ]
      break;

    case TemplateArgument::Pack:
      for (const auto &P : Arg.pack_elements())
        addAssociatedClassesAndNamespaces(Result, P);
      break;
  }
}

// Add the associated classes and namespaces for
// argument-dependent lookup with an argument of class type
// (C++ [basic.lookup.koenig]p2).
static void
addAssociatedClassesAndNamespaces(AssociatedLookup &Result,
                                  CXXRecordDecl *Class) {

  // Just silently ignore anything whose name is __va_list_tag.
  if (Class->getDeclName() == Result.S.VAListTagName)
    return;

  // C++ [basic.lookup.koenig]p2:
  //   [...]
  //     -- If T is a class type (including unions), its associated
  //        classes are: the class itself; the class of which it is a
  //        member, if any; and its direct and indirect base
  //        classes. Its associated namespaces are the namespaces in
  //        which its associated classes are defined.

  // Add the class of which it is a member, if any.
  DeclContext *Ctx = Class->getDeclContext();
  if (CXXRecordDecl *EnclosingClass = dyn_cast<CXXRecordDecl>(Ctx))
    Result.Classes.insert(EnclosingClass);
  // Add the associated namespace for this class.
  CollectEnclosingNamespace(Result.Namespaces, Ctx);

  // Add the class itself. If we've already seen this class, we don't
  // need to visit base classes.
  //
  // FIXME: That's not correct, we may have added this class only because it
  // was the enclosing class of another class, and in that case we won't have
  // added its base classes yet.
  if (!Result.Classes.insert(Class))
    return;

  // -- If T is a template-id, its associated namespaces and classes are
  //    the namespace in which the template is defined; for member
  //    templates, the member template's class; the namespaces and classes
  //    associated with the types of the template arguments provided for
  //    template type parameters (excluding template template parameters); the
  //    namespaces in which any template template arguments are defined; and
  //    the classes in which any member templates used as template template
  //    arguments are defined. [Note: non-type template arguments do not
  //    contribute to the set of associated namespaces. ]
  if (ClassTemplateSpecializationDecl *Spec
        = dyn_cast<ClassTemplateSpecializationDecl>(Class)) {
    DeclContext *Ctx = Spec->getSpecializedTemplate()->getDeclContext();
    if (CXXRecordDecl *EnclosingClass = dyn_cast<CXXRecordDecl>(Ctx))
      Result.Classes.insert(EnclosingClass);
    // Add the associated namespace for this class.
    CollectEnclosingNamespace(Result.Namespaces, Ctx);

    const TemplateArgumentList &TemplateArgs = Spec->getTemplateArgs();
    for (unsigned I = 0, N = TemplateArgs.size(); I != N; ++I)
      addAssociatedClassesAndNamespaces(Result, TemplateArgs[I]);
  }

  // Only recurse into base classes for complete types.
  if (!Result.S.isCompleteType(Result.InstantiationLoc,
                               Result.S.Context.getRecordType(Class)))
    return;

  // Add direct and indirect base classes along with their associated
  // namespaces.
  SmallVector<CXXRecordDecl *, 32> Bases;
  Bases.push_back(Class);
  while (!Bases.empty()) {
    // Pop this class off the stack.
    Class = Bases.pop_back_val();

    // Visit the base classes.
    for (const auto &Base : Class->bases()) {
      const RecordType *BaseType = Base.getType()->getAs<RecordType>();
      // In dependent contexts, we do ADL twice, and the first time around,
      // the base type might be a dependent TemplateSpecializationType, or a
      // TemplateTypeParmType. If that happens, simply ignore it.
      // FIXME: If we want to support export, we probably need to add the
      // namespace of the template in a TemplateSpecializationType, or even
      // the classes and namespaces of known non-dependent arguments.
      if (!BaseType)
        continue;
      CXXRecordDecl *BaseDecl = cast<CXXRecordDecl>(BaseType->getDecl());
      if (Result.Classes.insert(BaseDecl)) {
        // Find the associated namespace for this base class.
        DeclContext *BaseCtx = BaseDecl->getDeclContext();
        CollectEnclosingNamespace(Result.Namespaces, BaseCtx);

        // Make sure we visit the bases of this base class.
        if (BaseDecl->bases_begin() != BaseDecl->bases_end())
          Bases.push_back(BaseDecl);
      }
    }
  }
}

// Add the associated classes and namespaces for
// argument-dependent lookup with an argument of type T
// (C++ [basic.lookup.koenig]p2).
static void
addAssociatedClassesAndNamespaces(AssociatedLookup &Result, QualType Ty) {
  // C++ [basic.lookup.koenig]p2:
  //
  //   For each argument type T in the function call, there is a set
  //   of zero or more associated namespaces and a set of zero or more
  //   associated classes to be considered. The sets of namespaces and
  //   classes is determined entirely by the types of the function
  //   arguments (and the namespace of any template template
  //   argument). Typedef names and using-declarations used to specify
  //   the types do not contribute to this set. The sets of namespaces
  //   and classes are determined in the following way:

  SmallVector<const Type *, 16> Queue;
  const Type *T = Ty->getCanonicalTypeInternal().getTypePtr();

  while (true) {
    switch (T->getTypeClass()) {

#define TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) case Type::Class:
#define ABSTRACT_TYPE(Class, Base)
#include "clang/AST/TypeNodes.def"
      // T is canonical.  We can also ignore dependent types because
      // we don't need to do ADL at the definition point, but if we
      // wanted to implement template export (or if we find some other
      // use for associated classes and namespaces...) this would be
      // wrong.
      break;

    //    -- If T is a pointer to U or an array of U, its associated
    //       namespaces and classes are those associated with U.
    case Type::Pointer:
      T = cast<PointerType>(T)->getPointeeType().getTypePtr();
      continue;
    case Type::ConstantArray:
    case Type::IncompleteArray:
    case Type::VariableArray:
      T = cast<ArrayType>(T)->getElementType().getTypePtr();
      continue;

    //     -- If T is a fundamental type, its associated sets of
    //        namespaces and classes are both empty.
    case Type::Builtin:
      break;

    //     -- If T is a class type (including unions), its associated
    //        classes are: the class itself; the class of which it is a
    //        member, if any; and its direct and indirect base
    //        classes. Its associated namespaces are the namespaces in
    //        which its associated classes are defined.
    case Type::Record: {
      CXXRecordDecl *Class =
          cast<CXXRecordDecl>(cast<RecordType>(T)->getDecl());
      addAssociatedClassesAndNamespaces(Result, Class);
      break;
    }

    //     -- If T is an enumeration type, its associated namespace is
    //        the namespace in which it is defined. If it is class
    //        member, its associated class is the member's class; else
    //        it has no associated class.
    case Type::Enum: {
      EnumDecl *Enum = cast<EnumType>(T)->getDecl();

      DeclContext *Ctx = Enum->getDeclContext();
      if (CXXRecordDecl *EnclosingClass = dyn_cast<CXXRecordDecl>(Ctx))
        Result.Classes.insert(EnclosingClass);

      // Add the associated namespace for this class.
      CollectEnclosingNamespace(Result.Namespaces, Ctx);

      break;
    }

    //     -- If T is a function type, its associated namespaces and
    //        classes are those associated with the function parameter
    //        types and those associated with the return type.
    case Type::FunctionProto: {
      const FunctionProtoType *Proto = cast<FunctionProtoType>(T);
      for (const auto &Arg : Proto->param_types())
        Queue.push_back(Arg.getTypePtr());
      // fallthrough
      LLVM_FALLTHROUGH;
    }
    case Type::FunctionNoProto: {
      const FunctionType *FnType = cast<FunctionType>(T);
      T = FnType->getReturnType().getTypePtr();
      continue;
    }

    //     -- If T is a pointer to a member function of a class X, its
    //        associated namespaces and classes are those associated
    //        with the function parameter types and return type,
    //        together with those associated with X.
    //
    //     -- If T is a pointer to a data member of class X, its
    //        associated namespaces and classes are those associated
    //        with the member type together with those associated with
    //        X.
    case Type::MemberPointer: {
      const MemberPointerType *MemberPtr = cast<MemberPointerType>(T);

      // Queue up the class type into which this points.
      Queue.push_back(MemberPtr->getClass());

      // And directly continue with the pointee type.
      T = MemberPtr->getPointeeType().getTypePtr();
      continue;
    }

    // As an extension, treat this like a normal pointer.
    case Type::BlockPointer:
      T = cast<BlockPointerType>(T)->getPointeeType().getTypePtr();
      continue;

    // References aren't covered by the standard, but that's such an
    // obvious defect that we cover them anyway.
    case Type::LValueReference:
    case Type::RValueReference:
      T = cast<ReferenceType>(T)->getPointeeType().getTypePtr();
      continue;

    // These are fundamental types.
    case Type::Vector:
    case Type::ExtVector:
    case Type::Complex:
      break;

    // Non-deduced auto types only get here for error cases.
    case Type::Auto:
    case Type::DeducedTemplateSpecialization:
      break;

    // If T is an Objective-C object or interface type, or a pointer to an
    // object or interface type, the associated namespace is the global
    // namespace.
    case Type::ObjCObject:
    case Type::ObjCInterface:
    case Type::ObjCObjectPointer:
      Result.Namespaces.insert(Result.S.Context.getTranslationUnitDecl());
      break;

    // Atomic types are just wrappers; use the associations of the
    // contained type.
    case Type::Atomic:
      T = cast<AtomicType>(T)->getValueType().getTypePtr();
      continue;
    case Type::Pipe:
      T = cast<PipeType>(T)->getElementType().getTypePtr();
      continue;
    }

    if (Queue.empty())
      break;
    T = Queue.pop_back_val();
  }
}

/// Find the associated classes and namespaces for
/// argument-dependent lookup for a call with the given set of
/// arguments.
///
/// This routine computes the sets of associated classes and associated
/// namespaces searched by argument-dependent lookup
/// (C++ [basic.lookup.argdep]) for a given set of arguments.
void Sema::FindAssociatedClassesAndNamespaces(
    SourceLocation InstantiationLoc, ArrayRef<Expr *> Args,
    AssociatedNamespaceSet &AssociatedNamespaces,
    AssociatedClassSet &AssociatedClasses) {
  AssociatedNamespaces.clear();
  AssociatedClasses.clear();

  AssociatedLookup Result(*this, InstantiationLoc,
                          AssociatedNamespaces, AssociatedClasses);

  // C++ [basic.lookup.koenig]p2:
  //   For each argument type T in the function call, there is a set
  //   of zero or more associated namespaces and a set of zero or more
  //   associated classes to be considered. The sets of namespaces and
  //   classes is determined entirely by the types of the function
  //   arguments (and the namespace of any template template
  //   argument).
  for (unsigned ArgIdx = 0; ArgIdx != Args.size(); ++ArgIdx) {
    Expr *Arg = Args[ArgIdx];

    if (Arg->getType() != Context.OverloadTy) {
      addAssociatedClassesAndNamespaces(Result, Arg->getType());
      continue;
    }

    // [...] In addition, if the argument is the name or address of a
    // set of overloaded functions and/or function templates, its
    // associated classes and namespaces are the union of those
    // associated with each of the members of the set: the namespace
    // in which the function or function template is defined and the
    // classes and namespaces associated with its (non-dependent)
    // parameter types and return type.
    Arg = Arg->IgnoreParens();
    if (UnaryOperator *unaryOp = dyn_cast<UnaryOperator>(Arg))
      if (unaryOp->getOpcode() == UO_AddrOf)
        Arg = unaryOp->getSubExpr();

    UnresolvedLookupExpr *ULE = dyn_cast<UnresolvedLookupExpr>(Arg);
    if (!ULE) continue;

    for (const auto *D : ULE->decls()) {
      // Look through any using declarations to find the underlying function.
      const FunctionDecl *FDecl = D->getUnderlyingDecl()->getAsFunction();

      // Add the classes and namespaces associated with the parameter
      // types and return type of this function.
      addAssociatedClassesAndNamespaces(Result, FDecl->getType());
    }
  }
}

NamedDecl *Sema::LookupSingleName(Scope *S, DeclarationName Name,
                                  SourceLocation Loc,
                                  LookupNameKind NameKind,
                                  RedeclarationKind Redecl) {
  LookupResult R(*this, Name, Loc, NameKind, Redecl);
  LookupName(R, S);
  return R.getAsSingle<NamedDecl>();
}

/// Find the protocol with the given name, if any.
ObjCProtocolDecl *Sema::LookupProtocol(IdentifierInfo *II,
                                       SourceLocation IdLoc,
                                       RedeclarationKind Redecl) {
  Decl *D = LookupSingleName(TUScope, II, IdLoc,
                             LookupObjCProtocolName, Redecl);
  return cast_or_null<ObjCProtocolDecl>(D);
}

void Sema::LookupOverloadedOperatorName(OverloadedOperatorKind Op, Scope *S,
                                        QualType T1, QualType T2,
                                        UnresolvedSetImpl &Functions) {
  // C++ [over.match.oper]p3:
  //     -- The set of non-member candidates is the result of the
  //        unqualified lookup of operator@ in the context of the
  //        expression according to the usual rules for name lookup in
  //        unqualified function calls (3.4.2) except that all member
  //        functions are ignored.
  DeclarationName OpName = Context.DeclarationNames.getCXXOperatorName(Op);
  LookupResult Operators(*this, OpName, SourceLocation(), LookupOperatorName);
  LookupName(Operators, S);

  assert(!Operators.isAmbiguous() && "Operator lookup cannot be ambiguous");
  Functions.append(Operators.begin(), Operators.end());
}

Sema::SpecialMemberOverloadResult Sema::LookupSpecialMember(CXXRecordDecl *RD,
                                                           CXXSpecialMember SM,
                                                           bool ConstArg,
                                                           bool VolatileArg,
                                                           bool RValueThis,
                                                           bool ConstThis,
                                                           bool VolatileThis) {
  assert(CanDeclareSpecialMemberFunction(RD) &&
         "doing special member lookup into record that isn't fully complete");
  RD = RD->getDefinition();
  if (RValueThis || ConstThis || VolatileThis)
    assert((SM == CXXCopyAssignment || SM == CXXMoveAssignment) &&
           "constructors and destructors always have unqualified lvalue this");
  if (ConstArg || VolatileArg)
    assert((SM != CXXDefaultConstructor && SM != CXXDestructor) &&
           "parameter-less special members can't have qualified arguments");

  // FIXME: Get the caller to pass in a location for the lookup.
  SourceLocation LookupLoc = RD->getLocation();

  llvm::FoldingSetNodeID ID;
  ID.AddPointer(RD);
  ID.AddInteger(SM);
  ID.AddInteger(ConstArg);
  ID.AddInteger(VolatileArg);
  ID.AddInteger(RValueThis);
  ID.AddInteger(ConstThis);
  ID.AddInteger(VolatileThis);

  void *InsertPoint;
  SpecialMemberOverloadResultEntry *Result =
    SpecialMemberCache.FindNodeOrInsertPos(ID, InsertPoint);

  // This was already cached
  if (Result)
    return *Result;

  Result = BumpAlloc.Allocate<SpecialMemberOverloadResultEntry>();
  Result = new (Result) SpecialMemberOverloadResultEntry(ID);
  SpecialMemberCache.InsertNode(Result, InsertPoint);

  if (SM == CXXDestructor) {
    if (RD->needsImplicitDestructor())
      DeclareImplicitDestructor(RD);
    CXXDestructorDecl *DD = RD->getDestructor();
    assert(DD && "record without a destructor");
    Result->setMethod(DD);
    Result->setKind(DD->isDeleted() ?
                    SpecialMemberOverloadResult::NoMemberOrDeleted :
                    SpecialMemberOverloadResult::Success);
    return *Result;
  }

  // Prepare for overload resolution. Here we construct a synthetic argument
  // if necessary and make sure that implicit functions are declared.
  CanQualType CanTy = Context.getCanonicalType(Context.getTagDeclType(RD));
  DeclarationName Name;
  Expr *Arg = nullptr;
  unsigned NumArgs;

  QualType ArgType = CanTy;
  ExprValueKind VK = VK_LValue;

  if (SM == CXXDefaultConstructor) {
    Name = Context.DeclarationNames.getCXXConstructorName(CanTy);
    NumArgs = 0;
    if (RD->needsImplicitDefaultConstructor())
      DeclareImplicitDefaultConstructor(RD);
  } else {
    if (SM == CXXCopyConstructor || SM == CXXMoveConstructor) {
      Name = Context.DeclarationNames.getCXXConstructorName(CanTy);
      if (RD->needsImplicitCopyConstructor())
        DeclareImplicitCopyConstructor(RD);
      if (getLangOpts().CPlusPlus11 && RD->needsImplicitMoveConstructor())
        DeclareImplicitMoveConstructor(RD);
    } else {
      Name = Context.DeclarationNames.getCXXOperatorName(OO_Equal);
      if (RD->needsImplicitCopyAssignment())
        DeclareImplicitCopyAssignment(RD);
      if (getLangOpts().CPlusPlus11 && RD->needsImplicitMoveAssignment())
        DeclareImplicitMoveAssignment(RD);
    }

    if (ConstArg)
      ArgType.addConst();
    if (VolatileArg)
      ArgType.addVolatile();

    // This isn't /really/ specified by the standard, but it's implied
    // we should be working from an RValue in the case of move to ensure
    // that we prefer to bind to rvalue references, and an LValue in the
    // case of copy to ensure we don't bind to rvalue references.
    // Possibly an XValue is actually correct in the case of move, but
    // there is no semantic difference for class types in this restricted
    // case.
    if (SM == CXXCopyConstructor || SM == CXXCopyAssignment)
      VK = VK_LValue;
    else
      VK = VK_RValue;
  }

  OpaqueValueExpr FakeArg(LookupLoc, ArgType, VK);

  if (SM != CXXDefaultConstructor) {
    NumArgs = 1;
    Arg = &FakeArg;
  }

  // Create the object argument
  QualType ThisTy = CanTy;
  if (ConstThis)
    ThisTy.addConst();
  if (VolatileThis)
    ThisTy.addVolatile();
  Expr::Classification Classification =
    OpaqueValueExpr(LookupLoc, ThisTy,
                    RValueThis ? VK_RValue : VK_LValue).Classify(Context);

  // Now we perform lookup on the name we computed earlier and do overload
  // resolution. Lookup is only performed directly into the class since there
  // will always be a (possibly implicit) declaration to shadow any others.
  OverloadCandidateSet OCS(LookupLoc, OverloadCandidateSet::CSK_Normal);
  DeclContext::lookup_result R = RD->lookup(Name);

  if (R.empty()) {
    // We might have no default constructor because we have a lambda's closure
    // type, rather than because there's some other declared constructor.
    // Every class has a copy/move constructor, copy/move assignment, and
    // destructor.
    assert(SM == CXXDefaultConstructor &&
           "lookup for a constructor or assignment operator was empty");
    Result->setMethod(nullptr);
    Result->setKind(SpecialMemberOverloadResult::NoMemberOrDeleted);
    return *Result;
  }

  // Copy the candidates as our processing of them may load new declarations
  // from an external source and invalidate lookup_result.
  SmallVector<NamedDecl *, 8> Candidates(R.begin(), R.end());

  for (NamedDecl *CandDecl : Candidates) {
    if (CandDecl->isInvalidDecl())
      continue;

    DeclAccessPair Cand = DeclAccessPair::make(CandDecl, AS_public);
    auto CtorInfo = getConstructorInfo(Cand);
    if (CXXMethodDecl *M = dyn_cast<CXXMethodDecl>(Cand->getUnderlyingDecl())) {
      if (SM == CXXCopyAssignment || SM == CXXMoveAssignment)
        AddMethodCandidate(M, Cand, RD, ThisTy, Classification,
                           llvm::makeArrayRef(&Arg, NumArgs), OCS, true);
      else if (CtorInfo)
        AddOverloadCandidate(CtorInfo.Constructor, CtorInfo.FoundDecl,
                             llvm::makeArrayRef(&Arg, NumArgs), OCS, true);
      else
        AddOverloadCandidate(M, Cand, llvm::makeArrayRef(&Arg, NumArgs), OCS,
                             true);
    } else if (FunctionTemplateDecl *Tmpl =
                 dyn_cast<FunctionTemplateDecl>(Cand->getUnderlyingDecl())) {
      if (SM == CXXCopyAssignment || SM == CXXMoveAssignment)
        AddMethodTemplateCandidate(
            Tmpl, Cand, RD, nullptr, ThisTy, Classification,
            llvm::makeArrayRef(&Arg, NumArgs), OCS, true);
      else if (CtorInfo)
        AddTemplateOverloadCandidate(
            CtorInfo.ConstructorTmpl, CtorInfo.FoundDecl, nullptr,
            llvm::makeArrayRef(&Arg, NumArgs), OCS, true);
      else
        AddTemplateOverloadCandidate(
            Tmpl, Cand, nullptr, llvm::makeArrayRef(&Arg, NumArgs), OCS, true);
    } else {
      assert(isa<UsingDecl>(Cand.getDecl()) &&
             "illegal Kind of operator = Decl");
    }
  }

  OverloadCandidateSet::iterator Best;
  switch (OCS.BestViableFunction(*this, LookupLoc, Best)) {
    case OR_Success:
      Result->setMethod(cast<CXXMethodDecl>(Best->Function));
      Result->setKind(SpecialMemberOverloadResult::Success);
      break;

    case OR_Deleted:
      Result->setMethod(cast<CXXMethodDecl>(Best->Function));
      Result->setKind(SpecialMemberOverloadResult::NoMemberOrDeleted);
      break;

    case OR_Ambiguous:
      Result->setMethod(nullptr);
      Result->setKind(SpecialMemberOverloadResult::Ambiguous);
      break;

    case OR_No_Viable_Function:
      Result->setMethod(nullptr);
      Result->setKind(SpecialMemberOverloadResult::NoMemberOrDeleted);
      break;
  }

  return *Result;
}

/// Look up the default constructor for the given class.
CXXConstructorDecl *Sema::LookupDefaultConstructor(CXXRecordDecl *Class) {
  SpecialMemberOverloadResult Result =
    LookupSpecialMember(Class, CXXDefaultConstructor, false, false, false,
                        false, false);

  return cast_or_null<CXXConstructorDecl>(Result.getMethod());
}

/// Look up the copying constructor for the given class.
CXXConstructorDecl *Sema::LookupCopyingConstructor(CXXRecordDecl *Class,
                                                   unsigned Quals) {
  assert(!(Quals & ~(Qualifiers::Const | Qualifiers::Volatile)) &&
         "non-const, non-volatile qualifiers for copy ctor arg");
  SpecialMemberOverloadResult Result =
    LookupSpecialMember(Class, CXXCopyConstructor, Quals & Qualifiers::Const,
                        Quals & Qualifiers::Volatile, false, false, false);

  return cast_or_null<CXXConstructorDecl>(Result.getMethod());
}

/// Look up the moving constructor for the given class.
CXXConstructorDecl *Sema::LookupMovingConstructor(CXXRecordDecl *Class,
                                                  unsigned Quals) {
  SpecialMemberOverloadResult Result =
    LookupSpecialMember(Class, CXXMoveConstructor, Quals & Qualifiers::Const,
                        Quals & Qualifiers::Volatile, false, false, false);

  return cast_or_null<CXXConstructorDecl>(Result.getMethod());
}

/// Look up the constructors for the given class.
DeclContext::lookup_result Sema::LookupConstructors(CXXRecordDecl *Class) {
  // If the implicit constructors have not yet been declared, do so now.
  if (CanDeclareSpecialMemberFunction(Class)) {
    if (Class->needsImplicitDefaultConstructor())
      DeclareImplicitDefaultConstructor(Class);
    if (Class->needsImplicitCopyConstructor())
      DeclareImplicitCopyConstructor(Class);
    if (getLangOpts().CPlusPlus11 && Class->needsImplicitMoveConstructor())
      DeclareImplicitMoveConstructor(Class);
  }

  CanQualType T = Context.getCanonicalType(Context.getTypeDeclType(Class));
  DeclarationName Name = Context.DeclarationNames.getCXXConstructorName(T);
  return Class->lookup(Name);
}

/// Look up the copying assignment operator for the given class.
CXXMethodDecl *Sema::LookupCopyingAssignment(CXXRecordDecl *Class,
                                             unsigned Quals, bool RValueThis,
                                             unsigned ThisQuals) {
  assert(!(Quals & ~(Qualifiers::Const | Qualifiers::Volatile)) &&
         "non-const, non-volatile qualifiers for copy assignment arg");
  assert(!(ThisQuals & ~(Qualifiers::Const | Qualifiers::Volatile)) &&
         "non-const, non-volatile qualifiers for copy assignment this");
  SpecialMemberOverloadResult Result =
    LookupSpecialMember(Class, CXXCopyAssignment, Quals & Qualifiers::Const,
                        Quals & Qualifiers::Volatile, RValueThis,
                        ThisQuals & Qualifiers::Const,
                        ThisQuals & Qualifiers::Volatile);

  return Result.getMethod();
}

/// Look up the moving assignment operator for the given class.
CXXMethodDecl *Sema::LookupMovingAssignment(CXXRecordDecl *Class,
                                            unsigned Quals,
                                            bool RValueThis,
                                            unsigned ThisQuals) {
  assert(!(ThisQuals & ~(Qualifiers::Const | Qualifiers::Volatile)) &&
         "non-const, non-volatile qualifiers for copy assignment this");
  SpecialMemberOverloadResult Result =
    LookupSpecialMember(Class, CXXMoveAssignment, Quals & Qualifiers::Const,
                        Quals & Qualifiers::Volatile, RValueThis,
                        ThisQuals & Qualifiers::Const,
                        ThisQuals & Qualifiers::Volatile);

  return Result.getMethod();
}

/// Look for the destructor of the given class.
///
/// During semantic analysis, this routine should be used in lieu of
/// CXXRecordDecl::getDestructor().
///
/// \returns The destructor for this class.
CXXDestructorDecl *Sema::LookupDestructor(CXXRecordDecl *Class) {
  return cast<CXXDestructorDecl>(LookupSpecialMember(Class, CXXDestructor,
                                                     false, false, false,
                                                     false, false).getMethod());
}

/// LookupLiteralOperator - Determine which literal operator should be used for
/// a user-defined literal, per C++11 [lex.ext].
///
/// Normal overload resolution is not used to select which literal operator to
/// call for a user-defined literal. Look up the provided literal operator name,
/// and filter the results to the appropriate set for the given argument types.
Sema::LiteralOperatorLookupResult
Sema::LookupLiteralOperator(Scope *S, LookupResult &R,
                            ArrayRef<QualType> ArgTys,
                            bool AllowRaw, bool AllowTemplate,
                            bool AllowStringTemplate, bool DiagnoseMissing) {
  LookupName(R, S);
  assert(R.getResultKind() != LookupResult::Ambiguous &&
         "literal operator lookup can't be ambiguous");

  // Filter the lookup results appropriately.
  LookupResult::Filter F = R.makeFilter();

  bool FoundRaw = false;
  bool FoundTemplate = false;
  bool FoundStringTemplate = false;
  bool FoundExactMatch = false;

  while (F.hasNext()) {
    Decl *D = F.next();
    if (UsingShadowDecl *USD = dyn_cast<UsingShadowDecl>(D))
      D = USD->getTargetDecl();

    // If the declaration we found is invalid, skip it.
    if (D->isInvalidDecl()) {
      F.erase();
      continue;
    }

    bool IsRaw = false;
    bool IsTemplate = false;
    bool IsStringTemplate = false;
    bool IsExactMatch = false;

    if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->getNumParams() == 1 &&
          FD->getParamDecl(0)->getType()->getAs<PointerType>())
        IsRaw = true;
      else if (FD->getNumParams() == ArgTys.size()) {
        IsExactMatch = true;
        for (unsigned ArgIdx = 0; ArgIdx != ArgTys.size(); ++ArgIdx) {
          QualType ParamTy = FD->getParamDecl(ArgIdx)->getType();
          if (!Context.hasSameUnqualifiedType(ArgTys[ArgIdx], ParamTy)) {
            IsExactMatch = false;
            break;
          }
        }
      }
    }
    if (FunctionTemplateDecl *FD = dyn_cast<FunctionTemplateDecl>(D)) {
      TemplateParameterList *Params = FD->getTemplateParameters();
      if (Params->size() == 1)
        IsTemplate = true;
      else
        IsStringTemplate = true;
    }

    if (IsExactMatch) {
      FoundExactMatch = true;
      AllowRaw = false;
      AllowTemplate = false;
      AllowStringTemplate = false;
      if (FoundRaw || FoundTemplate || FoundStringTemplate) {
        // Go through again and remove the raw and template decls we've
        // already found.
        F.restart();
        FoundRaw = FoundTemplate = FoundStringTemplate = false;
      }
    } else if (AllowRaw && IsRaw) {
      FoundRaw = true;
    } else if (AllowTemplate && IsTemplate) {
      FoundTemplate = true;
    } else if (AllowStringTemplate && IsStringTemplate) {
      FoundStringTemplate = true;
    } else {
      F.erase();
    }
  }

  F.done();

  // C++11 [lex.ext]p3, p4: If S contains a literal operator with a matching
  // parameter type, that is used in preference to a raw literal operator
  // or literal operator template.
  if (FoundExactMatch)
    return LOLR_Cooked;

  // C++11 [lex.ext]p3, p4: S shall contain a raw literal operator or a literal
  // operator template, but not both.
  if (FoundRaw && FoundTemplate) {
    Diag(R.getNameLoc(), diag::err_ovl_ambiguous_call) << R.getLookupName();
    for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I)
      NoteOverloadCandidate(*I, (*I)->getUnderlyingDecl()->getAsFunction());
    return LOLR_Error;
  }

  if (FoundRaw)
    return LOLR_Raw;

  if (FoundTemplate)
    return LOLR_Template;

  if (FoundStringTemplate)
    return LOLR_StringTemplate;

  // Didn't find anything we could use.
  if (DiagnoseMissing) {
    Diag(R.getNameLoc(), diag::err_ovl_no_viable_literal_operator)
        << R.getLookupName() << (int)ArgTys.size() << ArgTys[0]
        << (ArgTys.size() == 2 ? ArgTys[1] : QualType()) << AllowRaw
        << (AllowTemplate || AllowStringTemplate);
    return LOLR_Error;
  }

  return LOLR_ErrorNoDiagnostic;
}

void ADLResult::insert(NamedDecl *New) {
  NamedDecl *&Old = Decls[cast<NamedDecl>(New->getCanonicalDecl())];

  // If we haven't yet seen a decl for this key, or the last decl
  // was exactly this one, we're done.
  if (Old == nullptr || Old == New) {
    Old = New;
    return;
  }

  // Otherwise, decide which is a more recent redeclaration.
  FunctionDecl *OldFD = Old->getAsFunction();
  FunctionDecl *NewFD = New->getAsFunction();

  FunctionDecl *Cursor = NewFD;
  while (true) {
    Cursor = Cursor->getPreviousDecl();

    // If we got to the end without finding OldFD, OldFD is the newer
    // declaration;  leave things as they are.
    if (!Cursor) return;

    // If we do find OldFD, then NewFD is newer.
    if (Cursor == OldFD) break;

    // Otherwise, keep looking.
  }

  Old = New;
}

void Sema::ArgumentDependentLookup(DeclarationName Name, SourceLocation Loc,
                                   ArrayRef<Expr *> Args, ADLResult &Result) {
  // Find all of the associated namespaces and classes based on the
  // arguments we have.
  AssociatedNamespaceSet AssociatedNamespaces;
  AssociatedClassSet AssociatedClasses;
  FindAssociatedClassesAndNamespaces(Loc, Args,
                                     AssociatedNamespaces,
                                     AssociatedClasses);

  // C++ [basic.lookup.argdep]p3:
  //   Let X be the lookup set produced by unqualified lookup (3.4.1)
  //   and let Y be the lookup set produced by argument dependent
  //   lookup (defined as follows). If X contains [...] then Y is
  //   empty. Otherwise Y is the set of declarations found in the
  //   namespaces associated with the argument types as described
  //   below. The set of declarations found by the lookup of the name
  //   is the union of X and Y.
  //
  // Here, we compute Y and add its members to the overloaded
  // candidate set.
  for (auto *NS : AssociatedNamespaces) {
    //   When considering an associated namespace, the lookup is the
    //   same as the lookup performed when the associated namespace is
    //   used as a qualifier (3.4.3.2) except that:
    //
    //     -- Any using-directives in the associated namespace are
    //        ignored.
    //
    //     -- Any namespace-scope friend functions declared in
    //        associated classes are visible within their respective
    //        namespaces even if they are not visible during an ordinary
    //        lookup (11.4).
    DeclContext::lookup_result R = NS->lookup(Name);
    for (auto *D : R) {
      auto *Underlying = D;
      if (auto *USD = dyn_cast<UsingShadowDecl>(D))
        Underlying = USD->getTargetDecl();

      if (!isa<FunctionDecl>(Underlying) &&
          !isa<FunctionTemplateDecl>(Underlying))
        continue;

      // The declaration is visible to argument-dependent lookup if either
      // it's ordinarily visible or declared as a friend in an associated
      // class.
      bool Visible = false;
      for (D = D->getMostRecentDecl(); D;
           D = cast_or_null<NamedDecl>(D->getPreviousDecl())) {
        if (D->getIdentifierNamespace() & Decl::IDNS_Ordinary) {
          if (isVisible(D)) {
            Visible = true;
            break;
          }
        } else if (D->getFriendObjectKind()) {
          auto *RD = cast<CXXRecordDecl>(D->getLexicalDeclContext());
          if (AssociatedClasses.count(RD) && isVisible(D)) {
            Visible = true;
            break;
          }
        }
      }

      // FIXME: Preserve D as the FoundDecl.
      if (Visible)
        Result.insert(Underlying);
    }
  }
}

//----------------------------------------------------------------------------
// Search for all visible declarations.
//----------------------------------------------------------------------------
VisibleDeclConsumer::~VisibleDeclConsumer() { }

bool VisibleDeclConsumer::includeHiddenDecls() const { return false; }

namespace {

class ShadowContextRAII;

class VisibleDeclsRecord {
public:
  /// An entry in the shadow map, which is optimized to store a
  /// single declaration (the common case) but can also store a list
  /// of declarations.
  typedef llvm::TinyPtrVector<NamedDecl*> ShadowMapEntry;

private:
  /// A mapping from declaration names to the declarations that have
  /// this name within a particular scope.
  typedef llvm::DenseMap<DeclarationName, ShadowMapEntry> ShadowMap;

  /// A list of shadow maps, which is used to model name hiding.
  std::list<ShadowMap> ShadowMaps;

  /// The declaration contexts we have already visited.
  llvm::SmallPtrSet<DeclContext *, 8> VisitedContexts;

  friend class ShadowContextRAII;

public:
  /// Determine whether we have already visited this context
  /// (and, if not, note that we are going to visit that context now).
  bool visitedContext(DeclContext *Ctx) {
    return !VisitedContexts.insert(Ctx).second;
  }

  bool alreadyVisitedContext(DeclContext *Ctx) {
    return VisitedContexts.count(Ctx);
  }

  /// Determine whether the given declaration is hidden in the
  /// current scope.
  ///
  /// \returns the declaration that hides the given declaration, or
  /// NULL if no such declaration exists.
  NamedDecl *checkHidden(NamedDecl *ND);

  /// Add a declaration to the current shadow map.
  void add(NamedDecl *ND) {
    ShadowMaps.back()[ND->getDeclName()].push_back(ND);
  }
};

/// RAII object that records when we've entered a shadow context.
class ShadowContextRAII {
  VisibleDeclsRecord &Visible;

  typedef VisibleDeclsRecord::ShadowMap ShadowMap;

public:
  ShadowContextRAII(VisibleDeclsRecord &Visible) : Visible(Visible) {
    Visible.ShadowMaps.emplace_back();
  }

  ~ShadowContextRAII() {
    Visible.ShadowMaps.pop_back();
  }
};

} // end anonymous namespace

NamedDecl *VisibleDeclsRecord::checkHidden(NamedDecl *ND) {
  unsigned IDNS = ND->getIdentifierNamespace();
  std::list<ShadowMap>::reverse_iterator SM = ShadowMaps.rbegin();
  for (std::list<ShadowMap>::reverse_iterator SMEnd = ShadowMaps.rend();
       SM != SMEnd; ++SM) {
    ShadowMap::iterator Pos = SM->find(ND->getDeclName());
    if (Pos == SM->end())
      continue;

    for (auto *D : Pos->second) {
      // A tag declaration does not hide a non-tag declaration.
      if (D->hasTagIdentifierNamespace() &&
          (IDNS & (Decl::IDNS_Member | Decl::IDNS_Ordinary |
                   Decl::IDNS_ObjCProtocol)))
        continue;

      // Protocols are in distinct namespaces from everything else.
      if (((D->getIdentifierNamespace() & Decl::IDNS_ObjCProtocol)
           || (IDNS & Decl::IDNS_ObjCProtocol)) &&
          D->getIdentifierNamespace() != IDNS)
        continue;

      // Functions and function templates in the same scope overload
      // rather than hide.  FIXME: Look for hiding based on function
      // signatures!
      if (D->getUnderlyingDecl()->isFunctionOrFunctionTemplate() &&
          ND->getUnderlyingDecl()->isFunctionOrFunctionTemplate() &&
          SM == ShadowMaps.rbegin())
        continue;

      // A shadow declaration that's created by a resolved using declaration
      // is not hidden by the same using declaration.
      if (isa<UsingShadowDecl>(ND) && isa<UsingDecl>(D) &&
          cast<UsingShadowDecl>(ND)->getUsingDecl() == D)
        continue;

      // We've found a declaration that hides this one.
      return D;
    }
  }

  return nullptr;
}

static void LookupVisibleDecls(DeclContext *Ctx, LookupResult &Result,
                               bool QualifiedNameLookup,
                               bool InBaseClass,
                               VisibleDeclConsumer &Consumer,
                               VisibleDeclsRecord &Visited,
                               bool IncludeDependentBases,
                               bool LoadExternal) {
  if (!Ctx)
    return;

  // Make sure we don't visit the same context twice.
  if (Visited.visitedContext(Ctx->getPrimaryContext()))
    return;

  Consumer.EnteredContext(Ctx);

  // Outside C++, lookup results for the TU live on identifiers.
  if (isa<TranslationUnitDecl>(Ctx) &&
      !Result.getSema().getLangOpts().CPlusPlus) {
    auto &S = Result.getSema();
    auto &Idents = S.Context.Idents;

    // Ensure all external identifiers are in the identifier table.
    if (LoadExternal)
      if (IdentifierInfoLookup *External = Idents.getExternalIdentifierLookup()) {
        std::unique_ptr<IdentifierIterator> Iter(External->getIdentifiers());
        for (StringRef Name = Iter->Next(); !Name.empty(); Name = Iter->Next())
          Idents.get(Name);
      }

    // Walk all lookup results in the TU for each identifier.
    for (const auto &Ident : Idents) {
      for (auto I = S.IdResolver.begin(Ident.getValue()),
                E = S.IdResolver.end();
           I != E; ++I) {
        if (S.IdResolver.isDeclInScope(*I, Ctx)) {
          if (NamedDecl *ND = Result.getAcceptableDecl(*I)) {
            Consumer.FoundDecl(ND, Visited.checkHidden(ND), Ctx, InBaseClass);
            Visited.add(ND);
          }
        }
      }
    }

    return;
  }

  if (CXXRecordDecl *Class = dyn_cast<CXXRecordDecl>(Ctx))
    Result.getSema().ForceDeclarationOfImplicitMembers(Class);

  // We sometimes skip loading namespace-level results (they tend to be huge).
  bool Load = LoadExternal ||
              !(isa<TranslationUnitDecl>(Ctx) || isa<NamespaceDecl>(Ctx));
  // Enumerate all of the results in this context.
  for (DeclContextLookupResult R :
       Load ? Ctx->lookups()
            : Ctx->noload_lookups(/*PreserveInternalState=*/false)) {
    for (auto *D : R) {
      if (auto *ND = Result.getAcceptableDecl(D)) {
        Consumer.FoundDecl(ND, Visited.checkHidden(ND), Ctx, InBaseClass);
        Visited.add(ND);
      }
    }
  }

  // Traverse using directives for qualified name lookup.
  if (QualifiedNameLookup) {
    ShadowContextRAII Shadow(Visited);
    for (auto I : Ctx->using_directives()) {
      if (!Result.getSema().isVisible(I))
        continue;
      LookupVisibleDecls(I->getNominatedNamespace(), Result,
                         QualifiedNameLookup, InBaseClass, Consumer, Visited,
                         IncludeDependentBases, LoadExternal);
    }
  }

  // Traverse the contexts of inherited C++ classes.
  if (CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(Ctx)) {
    if (!Record->hasDefinition())
      return;

    for (const auto &B : Record->bases()) {
      QualType BaseType = B.getType();

      RecordDecl *RD;
      if (BaseType->isDependentType()) {
        if (!IncludeDependentBases) {
          // Don't look into dependent bases, because name lookup can't look
          // there anyway.
          continue;
        }
        const auto *TST = BaseType->getAs<TemplateSpecializationType>();
        if (!TST)
          continue;
        TemplateName TN = TST->getTemplateName();
        const auto *TD =
            dyn_cast_or_null<ClassTemplateDecl>(TN.getAsTemplateDecl());
        if (!TD)
          continue;
        RD = TD->getTemplatedDecl();
      } else {
        const auto *Record = BaseType->getAs<RecordType>();
        if (!Record)
          continue;
        RD = Record->getDecl();
      }

      // FIXME: It would be nice to be able to determine whether referencing
      // a particular member would be ambiguous. For example, given
      //
      //   struct A { int member; };
      //   struct B { int member; };
      //   struct C : A, B { };
      //
      //   void f(C *c) { c->### }
      //
      // accessing 'member' would result in an ambiguity. However, we
      // could be smart enough to qualify the member with the base
      // class, e.g.,
      //
      //   c->B::member
      //
      // or
      //
      //   c->A::member

      // Find results in this base class (and its bases).
      ShadowContextRAII Shadow(Visited);
      LookupVisibleDecls(RD, Result, QualifiedNameLookup, /*InBaseClass=*/true,
                         Consumer, Visited, IncludeDependentBases,
                         LoadExternal);
    }
  }

  // Traverse the contexts of Objective-C classes.
  if (ObjCInterfaceDecl *IFace = dyn_cast<ObjCInterfaceDecl>(Ctx)) {
    // Traverse categories.
    for (auto *Cat : IFace->visible_categories()) {
      ShadowContextRAII Shadow(Visited);
      LookupVisibleDecls(Cat, Result, QualifiedNameLookup, false, Consumer,
                         Visited, IncludeDependentBases, LoadExternal);
    }

    // Traverse protocols.
    for (auto *I : IFace->all_referenced_protocols()) {
      ShadowContextRAII Shadow(Visited);
      LookupVisibleDecls(I, Result, QualifiedNameLookup, false, Consumer,
                         Visited, IncludeDependentBases, LoadExternal);
    }

    // Traverse the superclass.
    if (IFace->getSuperClass()) {
      ShadowContextRAII Shadow(Visited);
      LookupVisibleDecls(IFace->getSuperClass(), Result, QualifiedNameLookup,
                         true, Consumer, Visited, IncludeDependentBases,
                         LoadExternal);
    }

    // If there is an implementation, traverse it. We do this to find
    // synthesized ivars.
    if (IFace->getImplementation()) {
      ShadowContextRAII Shadow(Visited);
      LookupVisibleDecls(IFace->getImplementation(), Result,
                         QualifiedNameLookup, InBaseClass, Consumer, Visited,
                         IncludeDependentBases, LoadExternal);
    }
  } else if (ObjCProtocolDecl *Protocol = dyn_cast<ObjCProtocolDecl>(Ctx)) {
    for (auto *I : Protocol->protocols()) {
      ShadowContextRAII Shadow(Visited);
      LookupVisibleDecls(I, Result, QualifiedNameLookup, false, Consumer,
                         Visited, IncludeDependentBases, LoadExternal);
    }
  } else if (ObjCCategoryDecl *Category = dyn_cast<ObjCCategoryDecl>(Ctx)) {
    for (auto *I : Category->protocols()) {
      ShadowContextRAII Shadow(Visited);
      LookupVisibleDecls(I, Result, QualifiedNameLookup, false, Consumer,
                         Visited, IncludeDependentBases, LoadExternal);
    }

    // If there is an implementation, traverse it.
    if (Category->getImplementation()) {
      ShadowContextRAII Shadow(Visited);
      LookupVisibleDecls(Category->getImplementation(), Result,
                         QualifiedNameLookup, true, Consumer, Visited,
                         IncludeDependentBases, LoadExternal);
    }
  }
}

static void LookupVisibleDecls(Scope *S, LookupResult &Result,
                               UnqualUsingDirectiveSet &UDirs,
                               VisibleDeclConsumer &Consumer,
                               VisibleDeclsRecord &Visited,
                               bool LoadExternal) {
  if (!S)
    return;

  if (!S->getEntity() ||
      (!S->getParent() &&
       !Visited.alreadyVisitedContext(S->getEntity())) ||
      (S->getEntity())->isFunctionOrMethod()) {
    FindLocalExternScope FindLocals(Result);
    // Walk through the declarations in this Scope. The consumer might add new
    // decls to the scope as part of deserialization, so make a copy first.
    SmallVector<Decl *, 8> ScopeDecls(S->decls().begin(), S->decls().end());
    for (Decl *D : ScopeDecls) {
      if (NamedDecl *ND = dyn_cast<NamedDecl>(D))
        if ((ND = Result.getAcceptableDecl(ND))) {
          Consumer.FoundDecl(ND, Visited.checkHidden(ND), nullptr, false);
          Visited.add(ND);
        }
    }
  }

  // FIXME: C++ [temp.local]p8
  DeclContext *Entity = nullptr;
  if (S->getEntity()) {
    // Look into this scope's declaration context, along with any of its
    // parent lookup contexts (e.g., enclosing classes), up to the point
    // where we hit the context stored in the next outer scope.
    Entity = S->getEntity();
    DeclContext *OuterCtx = findOuterContext(S).first; // FIXME

    for (DeclContext *Ctx = Entity; Ctx && !Ctx->Equals(OuterCtx);
         Ctx = Ctx->getLookupParent()) {
      if (ObjCMethodDecl *Method = dyn_cast<ObjCMethodDecl>(Ctx)) {
        if (Method->isInstanceMethod()) {
          // For instance methods, look for ivars in the method's interface.
          LookupResult IvarResult(Result.getSema(), Result.getLookupName(),
                                  Result.getNameLoc(), Sema::LookupMemberName);
          if (ObjCInterfaceDecl *IFace = Method->getClassInterface()) {
            LookupVisibleDecls(IFace, IvarResult, /*QualifiedNameLookup=*/false,
                               /*InBaseClass=*/false, Consumer, Visited,
                               /*IncludeDependentBases=*/false, LoadExternal);
          }
        }

        // We've already performed all of the name lookup that we need
        // to for Objective-C methods; the next context will be the
        // outer scope.
        break;
      }

      if (Ctx->isFunctionOrMethod())
        continue;

      LookupVisibleDecls(Ctx, Result, /*QualifiedNameLookup=*/false,
                         /*InBaseClass=*/false, Consumer, Visited,
                         /*IncludeDependentBases=*/false, LoadExternal);
    }
  } else if (!S->getParent()) {
    // Look into the translation unit scope. We walk through the translation
    // unit's declaration context, because the Scope itself won't have all of
    // the declarations if we loaded a precompiled header.
    // FIXME: We would like the translation unit's Scope object to point to the
    // translation unit, so we don't need this special "if" branch. However,
    // doing so would force the normal C++ name-lookup code to look into the
    // translation unit decl when the IdentifierInfo chains would suffice.
    // Once we fix that problem (which is part of a more general "don't look
    // in DeclContexts unless we have to" optimization), we can eliminate this.
    Entity = Result.getSema().Context.getTranslationUnitDecl();
    LookupVisibleDecls(Entity, Result, /*QualifiedNameLookup=*/false,
                       /*InBaseClass=*/false, Consumer, Visited,
                       /*IncludeDependentBases=*/false, LoadExternal);
  }

  if (Entity) {
    // Lookup visible declarations in any namespaces found by using
    // directives.
    for (const UnqualUsingEntry &UUE : UDirs.getNamespacesFor(Entity))
      LookupVisibleDecls(const_cast<DeclContext *>(UUE.getNominatedNamespace()),
                         Result, /*QualifiedNameLookup=*/false,
                         /*InBaseClass=*/false, Consumer, Visited,
                         /*IncludeDependentBases=*/false, LoadExternal);
  }

  // Lookup names in the parent scope.
  ShadowContextRAII Shadow(Visited);
  LookupVisibleDecls(S->getParent(), Result, UDirs, Consumer, Visited,
                     LoadExternal);
}

void Sema::LookupVisibleDecls(Scope *S, LookupNameKind Kind,
                              VisibleDeclConsumer &Consumer,
                              bool IncludeGlobalScope, bool LoadExternal) {
  // Determine the set of using directives available during
  // unqualified name lookup.
  Scope *Initial = S;
  UnqualUsingDirectiveSet UDirs(*this);
  if (getLangOpts().CPlusPlus) {
    // Find the first namespace or translation-unit scope.
    while (S && !isNamespaceOrTranslationUnitScope(S))
      S = S->getParent();

    UDirs.visitScopeChain(Initial, S);
  }
  UDirs.done();

  // Look for visible declarations.
  LookupResult Result(*this, DeclarationName(), SourceLocation(), Kind);
  Result.setAllowHidden(Consumer.includeHiddenDecls());
  VisibleDeclsRecord Visited;
  if (!IncludeGlobalScope)
    Visited.visitedContext(Context.getTranslationUnitDecl());
  ShadowContextRAII Shadow(Visited);
  ::LookupVisibleDecls(Initial, Result, UDirs, Consumer, Visited, LoadExternal);
}

void Sema::LookupVisibleDecls(DeclContext *Ctx, LookupNameKind Kind,
                              VisibleDeclConsumer &Consumer,
                              bool IncludeGlobalScope,
                              bool IncludeDependentBases, bool LoadExternal) {
  LookupResult Result(*this, DeclarationName(), SourceLocation(), Kind);
  Result.setAllowHidden(Consumer.includeHiddenDecls());
  VisibleDeclsRecord Visited;
  if (!IncludeGlobalScope)
    Visited.visitedContext(Context.getTranslationUnitDecl());
  ShadowContextRAII Shadow(Visited);
  ::LookupVisibleDecls(Ctx, Result, /*QualifiedNameLookup=*/true,
                       /*InBaseClass=*/false, Consumer, Visited,
                       IncludeDependentBases, LoadExternal);
}

/// LookupOrCreateLabel - Do a name lookup of a label with the specified name.
/// If GnuLabelLoc is a valid source location, then this is a definition
/// of an __label__ label name, otherwise it is a normal label definition
/// or use.
LabelDecl *Sema::LookupOrCreateLabel(IdentifierInfo *II, SourceLocation Loc,
                                     SourceLocation GnuLabelLoc) {
  // Do a lookup to see if we have a label with this name already.
  NamedDecl *Res = nullptr;

  if (GnuLabelLoc.isValid()) {
    // Local label definitions always shadow existing labels.
    Res = LabelDecl::Create(Context, CurContext, Loc, II, GnuLabelLoc);
    Scope *S = CurScope;
    PushOnScopeChains(Res, S, true);
    return cast<LabelDecl>(Res);
  }

  // Not a GNU local label.
  Res = LookupSingleName(CurScope, II, Loc, LookupLabel, NotForRedeclaration);
  // If we found a label, check to see if it is in the same context as us.
  // When in a Block, we don't want to reuse a label in an enclosing function.
  if (Res && Res->getDeclContext() != CurContext)
    Res = nullptr;
  if (!Res) {
    // If not forward referenced or defined already, create the backing decl.
    Res = LabelDecl::Create(Context, CurContext, Loc, II);
    Scope *S = CurScope->getFnParent();
    assert(S && "Not in a function?");
    PushOnScopeChains(Res, S, true);
  }
  return cast<LabelDecl>(Res);
}

//===----------------------------------------------------------------------===//
// Typo correction
//===----------------------------------------------------------------------===//

static bool isCandidateViable(CorrectionCandidateCallback &CCC,
                              TypoCorrection &Candidate) {
  Candidate.setCallbackDistance(CCC.RankCandidate(Candidate));
  return Candidate.getEditDistance(false) != TypoCorrection::InvalidDistance;
}

static void LookupPotentialTypoResult(Sema &SemaRef,
                                      LookupResult &Res,
                                      IdentifierInfo *Name,
                                      Scope *S, CXXScopeSpec *SS,
                                      DeclContext *MemberContext,
                                      bool EnteringContext,
                                      bool isObjCIvarLookup,
                                      bool FindHidden);

/// Check whether the declarations found for a typo correction are
/// visible. Set the correction's RequiresImport flag to true if none of the
/// declarations are visible, false otherwise.
static void checkCorrectionVisibility(Sema &SemaRef, TypoCorrection &TC) {
  TypoCorrection::decl_iterator DI = TC.begin(), DE = TC.end();

  for (/**/; DI != DE; ++DI)
    if (!LookupResult::isVisible(SemaRef, *DI))
      break;
  // No filtering needed if all decls are visible.
  if (DI == DE) {
    TC.setRequiresImport(false);
    return;
  }

  llvm::SmallVector<NamedDecl*, 4> NewDecls(TC.begin(), DI);
  bool AnyVisibleDecls = !NewDecls.empty();

  for (/**/; DI != DE; ++DI) {
    if (LookupResult::isVisible(SemaRef, *DI)) {
      if (!AnyVisibleDecls) {
        // Found a visible decl, discard all hidden ones.
        AnyVisibleDecls = true;
        NewDecls.clear();
      }
      NewDecls.push_back(*DI);
    } else if (!AnyVisibleDecls && !(*DI)->isModulePrivate())
      NewDecls.push_back(*DI);
  }

  if (NewDecls.empty())
    TC = TypoCorrection();
  else {
    TC.setCorrectionDecls(NewDecls);
    TC.setRequiresImport(!AnyVisibleDecls);
  }
}

// Fill the supplied vector with the IdentifierInfo pointers for each piece of
// the given NestedNameSpecifier (i.e. given a NestedNameSpecifier "foo::bar::",
// fill the vector with the IdentifierInfo pointers for "foo" and "bar").
static void getNestedNameSpecifierIdentifiers(
    NestedNameSpecifier *NNS,
    SmallVectorImpl<const IdentifierInfo*> &Identifiers) {
  if (NestedNameSpecifier *Prefix = NNS->getPrefix())
    getNestedNameSpecifierIdentifiers(Prefix, Identifiers);
  else
    Identifiers.clear();

  const IdentifierInfo *II = nullptr;

  switch (NNS->getKind()) {
  case NestedNameSpecifier::Identifier:
    II = NNS->getAsIdentifier();
    break;

  case NestedNameSpecifier::Namespace:
    if (NNS->getAsNamespace()->isAnonymousNamespace())
      return;
    II = NNS->getAsNamespace()->getIdentifier();
    break;

  case NestedNameSpecifier::NamespaceAlias:
    II = NNS->getAsNamespaceAlias()->getIdentifier();
    break;

  case NestedNameSpecifier::TypeSpecWithTemplate:
  case NestedNameSpecifier::TypeSpec:
    II = QualType(NNS->getAsType(), 0).getBaseTypeIdentifier();
    break;

  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Super:
    return;
  }

  if (II)
    Identifiers.push_back(II);
}

void TypoCorrectionConsumer::FoundDecl(NamedDecl *ND, NamedDecl *Hiding,
                                       DeclContext *Ctx, bool InBaseClass) {
  // Don't consider hidden names for typo correction.
  if (Hiding)
    return;

  // Only consider entities with identifiers for names, ignoring
  // special names (constructors, overloaded operators, selectors,
  // etc.).
  IdentifierInfo *Name = ND->getIdentifier();
  if (!Name)
    return;

  // Only consider visible declarations and declarations from modules with
  // names that exactly match.
  if (!LookupResult::isVisible(SemaRef, ND) && Name != Typo)
    return;

  FoundName(Name->getName());
}

void TypoCorrectionConsumer::FoundName(StringRef Name) {
  // Compute the edit distance between the typo and the name of this
  // entity, and add the identifier to the list of results.
  addName(Name, nullptr);
}

void TypoCorrectionConsumer::addKeywordResult(StringRef Keyword) {
  // Compute the edit distance between the typo and this keyword,
  // and add the keyword to the list of results.
  addName(Keyword, nullptr, nullptr, true);
}

void TypoCorrectionConsumer::addName(StringRef Name, NamedDecl *ND,
                                     NestedNameSpecifier *NNS, bool isKeyword) {
  // Use a simple length-based heuristic to determine the minimum possible
  // edit distance. If the minimum isn't good enough, bail out early.
  StringRef TypoStr = Typo->getName();
  unsigned MinED = abs((int)Name.size() - (int)TypoStr.size());
  if (MinED && TypoStr.size() / MinED < 3)
    return;

  // Compute an upper bound on the allowable edit distance, so that the
  // edit-distance algorithm can short-circuit.
  unsigned UpperBound = (TypoStr.size() + 2) / 3;
  unsigned ED = TypoStr.edit_distance(Name, true, UpperBound);
  if (ED > UpperBound) return;

  TypoCorrection TC(&SemaRef.Context.Idents.get(Name), ND, NNS, ED);
  if (isKeyword) TC.makeKeyword();
  TC.setCorrectionRange(nullptr, Result.getLookupNameInfo());
  addCorrection(TC);
}

static const unsigned MaxTypoDistanceResultSets = 5;

void TypoCorrectionConsumer::addCorrection(TypoCorrection Correction) {
  StringRef TypoStr = Typo->getName();
  StringRef Name = Correction.getCorrectionAsIdentifierInfo()->getName();

  // For very short typos, ignore potential corrections that have a different
  // base identifier from the typo or which have a normalized edit distance
  // longer than the typo itself.
  if (TypoStr.size() < 3 &&
      (Name != TypoStr || Correction.getEditDistance(true) > TypoStr.size()))
    return;

  // If the correction is resolved but is not viable, ignore it.
  if (Correction.isResolved()) {
    checkCorrectionVisibility(SemaRef, Correction);
    if (!Correction || !isCandidateViable(*CorrectionValidator, Correction))
      return;
  }

  TypoResultList &CList =
      CorrectionResults[Correction.getEditDistance(false)][Name];

  if (!CList.empty() && !CList.back().isResolved())
    CList.pop_back();
  if (NamedDecl *NewND = Correction.getCorrectionDecl()) {
    std::string CorrectionStr = Correction.getAsString(SemaRef.getLangOpts());
    for (TypoResultList::iterator RI = CList.begin(), RIEnd = CList.end();
         RI != RIEnd; ++RI) {
      // If the Correction refers to a decl already in the result list,
      // replace the existing result if the string representation of Correction
      // comes before the current result alphabetically, then stop as there is
      // nothing more to be done to add Correction to the candidate set.
      if (RI->getCorrectionDecl() == NewND) {
        if (CorrectionStr < RI->getAsString(SemaRef.getLangOpts()))
          *RI = Correction;
        return;
      }
    }
  }
  if (CList.empty() || Correction.isResolved())
    CList.push_back(Correction);

  while (CorrectionResults.size() > MaxTypoDistanceResultSets)
    CorrectionResults.erase(std::prev(CorrectionResults.end()));
}

void TypoCorrectionConsumer::addNamespaces(
    const llvm::MapVector<NamespaceDecl *, bool> &KnownNamespaces) {
  SearchNamespaces = true;

  for (auto KNPair : KnownNamespaces)
    Namespaces.addNameSpecifier(KNPair.first);

  bool SSIsTemplate = false;
  if (NestedNameSpecifier *NNS =
          (SS && SS->isValid()) ? SS->getScopeRep() : nullptr) {
    if (const Type *T = NNS->getAsType())
      SSIsTemplate = T->getTypeClass() == Type::TemplateSpecialization;
  }
  // Do not transform this into an iterator-based loop. The loop body can
  // trigger the creation of further types (through lazy deserialization) and
  // invalid iterators into this list.
  auto &Types = SemaRef.getASTContext().getTypes();
  for (unsigned I = 0; I != Types.size(); ++I) {
    const auto *TI = Types[I];
    if (CXXRecordDecl *CD = TI->getAsCXXRecordDecl()) {
      CD = CD->getCanonicalDecl();
      if (!CD->isDependentType() && !CD->isAnonymousStructOrUnion() &&
          !CD->isUnion() && CD->getIdentifier() &&
          (SSIsTemplate || !isa<ClassTemplateSpecializationDecl>(CD)) &&
          (CD->isBeingDefined() || CD->isCompleteDefinition()))
        Namespaces.addNameSpecifier(CD);
    }
  }
}

const TypoCorrection &TypoCorrectionConsumer::getNextCorrection() {
  if (++CurrentTCIndex < ValidatedCorrections.size())
    return ValidatedCorrections[CurrentTCIndex];

  CurrentTCIndex = ValidatedCorrections.size();
  while (!CorrectionResults.empty()) {
    auto DI = CorrectionResults.begin();
    if (DI->second.empty()) {
      CorrectionResults.erase(DI);
      continue;
    }

    auto RI = DI->second.begin();
    if (RI->second.empty()) {
      DI->second.erase(RI);
      performQualifiedLookups();
      continue;
    }

    TypoCorrection TC = RI->second.pop_back_val();
    if (TC.isResolved() || TC.requiresImport() || resolveCorrection(TC)) {
      ValidatedCorrections.push_back(TC);
      return ValidatedCorrections[CurrentTCIndex];
    }
  }
  return ValidatedCorrections[0];  // The empty correction.
}

bool TypoCorrectionConsumer::resolveCorrection(TypoCorrection &Candidate) {
  IdentifierInfo *Name = Candidate.getCorrectionAsIdentifierInfo();
  DeclContext *TempMemberContext = MemberContext;
  CXXScopeSpec *TempSS = SS.get();
retry_lookup:
  LookupPotentialTypoResult(SemaRef, Result, Name, S, TempSS, TempMemberContext,
                            EnteringContext,
                            CorrectionValidator->IsObjCIvarLookup,
                            Name == Typo && !Candidate.WillReplaceSpecifier());
  switch (Result.getResultKind()) {
  case LookupResult::NotFound:
  case LookupResult::NotFoundInCurrentInstantiation:
  case LookupResult::FoundUnresolvedValue:
    if (TempSS) {
      // Immediately retry the lookup without the given CXXScopeSpec
      TempSS = nullptr;
      Candidate.WillReplaceSpecifier(true);
      goto retry_lookup;
    }
    if (TempMemberContext) {
      if (SS && !TempSS)
        TempSS = SS.get();
      TempMemberContext = nullptr;
      goto retry_lookup;
    }
    if (SearchNamespaces)
      QualifiedResults.push_back(Candidate);
    break;

  case LookupResult::Ambiguous:
    // We don't deal with ambiguities.
    break;

  case LookupResult::Found:
  case LookupResult::FoundOverloaded:
    // Store all of the Decls for overloaded symbols
    for (auto *TRD : Result)
      Candidate.addCorrectionDecl(TRD);
    checkCorrectionVisibility(SemaRef, Candidate);
    if (!isCandidateViable(*CorrectionValidator, Candidate)) {
      if (SearchNamespaces)
        QualifiedResults.push_back(Candidate);
      break;
    }
    Candidate.setCorrectionRange(SS.get(), Result.getLookupNameInfo());
    return true;
  }
  return false;
}

void TypoCorrectionConsumer::performQualifiedLookups() {
  unsigned TypoLen = Typo->getName().size();
  for (const TypoCorrection &QR : QualifiedResults) {
    for (const auto &NSI : Namespaces) {
      DeclContext *Ctx = NSI.DeclCtx;
      const Type *NSType = NSI.NameSpecifier->getAsType();

      // If the current NestedNameSpecifier refers to a class and the
      // current correction candidate is the name of that class, then skip
      // it as it is unlikely a qualified version of the class' constructor
      // is an appropriate correction.
      if (CXXRecordDecl *NSDecl = NSType ? NSType->getAsCXXRecordDecl() :
                                           nullptr) {
        if (NSDecl->getIdentifier() == QR.getCorrectionAsIdentifierInfo())
          continue;
      }

      TypoCorrection TC(QR);
      TC.ClearCorrectionDecls();
      TC.setCorrectionSpecifier(NSI.NameSpecifier);
      TC.setQualifierDistance(NSI.EditDistance);
      TC.setCallbackDistance(0); // Reset the callback distance

      // If the current correction candidate and namespace combination are
      // too far away from the original typo based on the normalized edit
      // distance, then skip performing a qualified name lookup.
      unsigned TmpED = TC.getEditDistance(true);
      if (QR.getCorrectionAsIdentifierInfo() != Typo && TmpED &&
          TypoLen / TmpED < 3)
        continue;

      Result.clear();
      Result.setLookupName(QR.getCorrectionAsIdentifierInfo());
      if (!SemaRef.LookupQualifiedName(Result, Ctx))
        continue;

      // Any corrections added below will be validated in subsequent
      // iterations of the main while() loop over the Consumer's contents.
      switch (Result.getResultKind()) {
      case LookupResult::Found:
      case LookupResult::FoundOverloaded: {
        if (SS && SS->isValid()) {
          std::string NewQualified = TC.getAsString(SemaRef.getLangOpts());
          std::string OldQualified;
          llvm::raw_string_ostream OldOStream(OldQualified);
          SS->getScopeRep()->print(OldOStream, SemaRef.getPrintingPolicy());
          OldOStream << Typo->getName();
          // If correction candidate would be an identical written qualified
          // identifier, then the existing CXXScopeSpec probably included a
          // typedef that didn't get accounted for properly.
          if (OldOStream.str() == NewQualified)
            break;
        }
        for (LookupResult::iterator TRD = Result.begin(), TRDEnd = Result.end();
             TRD != TRDEnd; ++TRD) {
          if (SemaRef.CheckMemberAccess(TC.getCorrectionRange().getBegin(),
                                        NSType ? NSType->getAsCXXRecordDecl()
                                               : nullptr,
                                        TRD.getPair()) == Sema::AR_accessible)
            TC.addCorrectionDecl(*TRD);
        }
        if (TC.isResolved()) {
          TC.setCorrectionRange(SS.get(), Result.getLookupNameInfo());
          addCorrection(TC);
        }
        break;
      }
      case LookupResult::NotFound:
      case LookupResult::NotFoundInCurrentInstantiation:
      case LookupResult::Ambiguous:
      case LookupResult::FoundUnresolvedValue:
        break;
      }
    }
  }
  QualifiedResults.clear();
}

TypoCorrectionConsumer::NamespaceSpecifierSet::NamespaceSpecifierSet(
    ASTContext &Context, DeclContext *CurContext, CXXScopeSpec *CurScopeSpec)
    : Context(Context), CurContextChain(buildContextChain(CurContext)) {
  if (NestedNameSpecifier *NNS =
          CurScopeSpec ? CurScopeSpec->getScopeRep() : nullptr) {
    llvm::raw_string_ostream SpecifierOStream(CurNameSpecifier);
    NNS->print(SpecifierOStream, Context.getPrintingPolicy());

    getNestedNameSpecifierIdentifiers(NNS, CurNameSpecifierIdentifiers);
  }
  // Build the list of identifiers that would be used for an absolute
  // (from the global context) NestedNameSpecifier referring to the current
  // context.
  for (DeclContext *C : llvm::reverse(CurContextChain)) {
    if (auto *ND = dyn_cast_or_null<NamespaceDecl>(C))
      CurContextIdentifiers.push_back(ND->getIdentifier());
  }

  // Add the global context as a NestedNameSpecifier
  SpecifierInfo SI = {cast<DeclContext>(Context.getTranslationUnitDecl()),
                      NestedNameSpecifier::GlobalSpecifier(Context), 1};
  DistanceMap[1].push_back(SI);
}

auto TypoCorrectionConsumer::NamespaceSpecifierSet::buildContextChain(
    DeclContext *Start) -> DeclContextList {
  assert(Start && "Building a context chain from a null context");
  DeclContextList Chain;
  for (DeclContext *DC = Start->getPrimaryContext(); DC != nullptr;
       DC = DC->getLookupParent()) {
    NamespaceDecl *ND = dyn_cast_or_null<NamespaceDecl>(DC);
    if (!DC->isInlineNamespace() && !DC->isTransparentContext() &&
        !(ND && ND->isAnonymousNamespace()))
      Chain.push_back(DC->getPrimaryContext());
  }
  return Chain;
}

unsigned
TypoCorrectionConsumer::NamespaceSpecifierSet::buildNestedNameSpecifier(
    DeclContextList &DeclChain, NestedNameSpecifier *&NNS) {
  unsigned NumSpecifiers = 0;
  for (DeclContext *C : llvm::reverse(DeclChain)) {
    if (auto *ND = dyn_cast_or_null<NamespaceDecl>(C)) {
      NNS = NestedNameSpecifier::Create(Context, NNS, ND);
      ++NumSpecifiers;
    } else if (auto *RD = dyn_cast_or_null<RecordDecl>(C)) {
      NNS = NestedNameSpecifier::Create(Context, NNS, RD->isTemplateDecl(),
                                        RD->getTypeForDecl());
      ++NumSpecifiers;
    }
  }
  return NumSpecifiers;
}

void TypoCorrectionConsumer::NamespaceSpecifierSet::addNameSpecifier(
    DeclContext *Ctx) {
  NestedNameSpecifier *NNS = nullptr;
  unsigned NumSpecifiers = 0;
  DeclContextList NamespaceDeclChain(buildContextChain(Ctx));
  DeclContextList FullNamespaceDeclChain(NamespaceDeclChain);

  // Eliminate common elements from the two DeclContext chains.
  for (DeclContext *C : llvm::reverse(CurContextChain)) {
    if (NamespaceDeclChain.empty() || NamespaceDeclChain.back() != C)
      break;
    NamespaceDeclChain.pop_back();
  }

  // Build the NestedNameSpecifier from what is left of the NamespaceDeclChain
  NumSpecifiers = buildNestedNameSpecifier(NamespaceDeclChain, NNS);

  // Add an explicit leading '::' specifier if needed.
  if (NamespaceDeclChain.empty()) {
    // Rebuild the NestedNameSpecifier as a globally-qualified specifier.
    NNS = NestedNameSpecifier::GlobalSpecifier(Context);
    NumSpecifiers =
        buildNestedNameSpecifier(FullNamespaceDeclChain, NNS);
  } else if (NamedDecl *ND =
                 dyn_cast_or_null<NamedDecl>(NamespaceDeclChain.back())) {
    IdentifierInfo *Name = ND->getIdentifier();
    bool SameNameSpecifier = false;
    if (std::find(CurNameSpecifierIdentifiers.begin(),
                  CurNameSpecifierIdentifiers.end(),
                  Name) != CurNameSpecifierIdentifiers.end()) {
      std::string NewNameSpecifier;
      llvm::raw_string_ostream SpecifierOStream(NewNameSpecifier);
      SmallVector<const IdentifierInfo *, 4> NewNameSpecifierIdentifiers;
      getNestedNameSpecifierIdentifiers(NNS, NewNameSpecifierIdentifiers);
      NNS->print(SpecifierOStream, Context.getPrintingPolicy());
      SpecifierOStream.flush();
      SameNameSpecifier = NewNameSpecifier == CurNameSpecifier;
    }
    if (SameNameSpecifier ||
        std::find(CurContextIdentifiers.begin(), CurContextIdentifiers.end(),
                  Name) != CurContextIdentifiers.end()) {
      // Rebuild the NestedNameSpecifier as a globally-qualified specifier.
      NNS = NestedNameSpecifier::GlobalSpecifier(Context);
      NumSpecifiers =
          buildNestedNameSpecifier(FullNamespaceDeclChain, NNS);
    }
  }

  // If the built NestedNameSpecifier would be replacing an existing
  // NestedNameSpecifier, use the number of component identifiers that
  // would need to be changed as the edit distance instead of the number
  // of components in the built NestedNameSpecifier.
  if (NNS && !CurNameSpecifierIdentifiers.empty()) {
    SmallVector<const IdentifierInfo*, 4> NewNameSpecifierIdentifiers;
    getNestedNameSpecifierIdentifiers(NNS, NewNameSpecifierIdentifiers);
    NumSpecifiers = llvm::ComputeEditDistance(
        llvm::makeArrayRef(CurNameSpecifierIdentifiers),
        llvm::makeArrayRef(NewNameSpecifierIdentifiers));
  }

  SpecifierInfo SI = {Ctx, NNS, NumSpecifiers};
  DistanceMap[NumSpecifiers].push_back(SI);
}

/// Perform name lookup for a possible result for typo correction.
static void LookupPotentialTypoResult(Sema &SemaRef,
                                      LookupResult &Res,
                                      IdentifierInfo *Name,
                                      Scope *S, CXXScopeSpec *SS,
                                      DeclContext *MemberContext,
                                      bool EnteringContext,
                                      bool isObjCIvarLookup,
                                      bool FindHidden) {
  Res.suppressDiagnostics();
  Res.clear();
  Res.setLookupName(Name);
  Res.setAllowHidden(FindHidden);
  if (MemberContext) {
    if (ObjCInterfaceDecl *Class = dyn_cast<ObjCInterfaceDecl>(MemberContext)) {
      if (isObjCIvarLookup) {
        if (ObjCIvarDecl *Ivar = Class->lookupInstanceVariable(Name)) {
          Res.addDecl(Ivar);
          Res.resolveKind();
          return;
        }
      }

      if (ObjCPropertyDecl *Prop = Class->FindPropertyDeclaration(
              Name, ObjCPropertyQueryKind::OBJC_PR_query_instance)) {
        Res.addDecl(Prop);
        Res.resolveKind();
        return;
      }
    }

    SemaRef.LookupQualifiedName(Res, MemberContext);
    return;
  }

  SemaRef.LookupParsedName(Res, S, SS, /*AllowBuiltinCreation=*/false,
                           EnteringContext);

  // Fake ivar lookup; this should really be part of
  // LookupParsedName.
  if (ObjCMethodDecl *Method = SemaRef.getCurMethodDecl()) {
    if (Method->isInstanceMethod() && Method->getClassInterface() &&
        (Res.empty() ||
         (Res.isSingleResult() &&
          Res.getFoundDecl()->isDefinedOutsideFunctionOrMethod()))) {
       if (ObjCIvarDecl *IV
             = Method->getClassInterface()->lookupInstanceVariable(Name)) {
         Res.addDecl(IV);
         Res.resolveKind();
       }
     }
  }
}

/// Add keywords to the consumer as possible typo corrections.
static void AddKeywordsToConsumer(Sema &SemaRef,
                                  TypoCorrectionConsumer &Consumer,
                                  Scope *S, CorrectionCandidateCallback &CCC,
                                  bool AfterNestedNameSpecifier) {
  if (AfterNestedNameSpecifier) {
    // For 'X::', we know exactly which keywords can appear next.
    Consumer.addKeywordResult("template");
    if (CCC.WantExpressionKeywords)
      Consumer.addKeywordResult("operator");
    return;
  }

  if (CCC.WantObjCSuper)
    Consumer.addKeywordResult("super");

  if (CCC.WantTypeSpecifiers) {
    // Add type-specifier keywords to the set of results.
    static const char *const CTypeSpecs[] = {
      "char", "const", "double", "enum", "float", "int", "long", "short",
      "signed", "struct", "union", "unsigned", "void", "volatile",
      "_Complex", "_Imaginary",
      // storage-specifiers as well
      "extern", "inline", "static", "typedef"
    };

    const unsigned NumCTypeSpecs = llvm::array_lengthof(CTypeSpecs);
    for (unsigned I = 0; I != NumCTypeSpecs; ++I)
      Consumer.addKeywordResult(CTypeSpecs[I]);

    if (SemaRef.getLangOpts().C99)
      Consumer.addKeywordResult("restrict");
    if (SemaRef.getLangOpts().Bool || SemaRef.getLangOpts().CPlusPlus)
      Consumer.addKeywordResult("bool");
    else if (SemaRef.getLangOpts().C99)
      Consumer.addKeywordResult("_Bool");

    if (SemaRef.getLangOpts().CPlusPlus) {
      Consumer.addKeywordResult("class");
      Consumer.addKeywordResult("typename");
      Consumer.addKeywordResult("wchar_t");

      if (SemaRef.getLangOpts().CPlusPlus11) {
        Consumer.addKeywordResult("char16_t");
        Consumer.addKeywordResult("char32_t");
        Consumer.addKeywordResult("constexpr");
        Consumer.addKeywordResult("decltype");
        Consumer.addKeywordResult("thread_local");
      }
    }

    if (SemaRef.getLangOpts().GNUKeywords)
      Consumer.addKeywordResult("typeof");
  } else if (CCC.WantFunctionLikeCasts) {
    static const char *const CastableTypeSpecs[] = {
      "char", "double", "float", "int", "long", "short",
      "signed", "unsigned", "void"
    };
    for (auto *kw : CastableTypeSpecs)
      Consumer.addKeywordResult(kw);
  }

  if (CCC.WantCXXNamedCasts && SemaRef.getLangOpts().CPlusPlus) {
    Consumer.addKeywordResult("const_cast");
    Consumer.addKeywordResult("dynamic_cast");
    Consumer.addKeywordResult("reinterpret_cast");
    Consumer.addKeywordResult("static_cast");
  }

  if (CCC.WantExpressionKeywords) {
    Consumer.addKeywordResult("sizeof");
    if (SemaRef.getLangOpts().Bool || SemaRef.getLangOpts().CPlusPlus) {
      Consumer.addKeywordResult("false");
      Consumer.addKeywordResult("true");
    }

    if (SemaRef.getLangOpts().CPlusPlus) {
      static const char *const CXXExprs[] = {
        "delete", "new", "operator", "throw", "typeid"
      };
      const unsigned NumCXXExprs = llvm::array_lengthof(CXXExprs);
      for (unsigned I = 0; I != NumCXXExprs; ++I)
        Consumer.addKeywordResult(CXXExprs[I]);

      if (isa<CXXMethodDecl>(SemaRef.CurContext) &&
          cast<CXXMethodDecl>(SemaRef.CurContext)->isInstance())
        Consumer.addKeywordResult("this");

      if (SemaRef.getLangOpts().CPlusPlus11) {
        Consumer.addKeywordResult("alignof");
        Consumer.addKeywordResult("nullptr");
      }
    }

    if (SemaRef.getLangOpts().C11) {
      // FIXME: We should not suggest _Alignof if the alignof macro
      // is present.
      Consumer.addKeywordResult("_Alignof");
    }
  }

  if (CCC.WantRemainingKeywords) {
    if (SemaRef.getCurFunctionOrMethodDecl() || SemaRef.getCurBlock()) {
      // Statements.
      static const char *const CStmts[] = {
        "do", "else", "for", "goto", "if", "return", "switch", "while" };
      const unsigned NumCStmts = llvm::array_lengthof(CStmts);
      for (unsigned I = 0; I != NumCStmts; ++I)
        Consumer.addKeywordResult(CStmts[I]);

      if (SemaRef.getLangOpts().CPlusPlus) {
        Consumer.addKeywordResult("catch");
        Consumer.addKeywordResult("try");
      }

      if (S && S->getBreakParent())
        Consumer.addKeywordResult("break");

      if (S && S->getContinueParent())
        Consumer.addKeywordResult("continue");

      if (SemaRef.getCurFunction() &&
          !SemaRef.getCurFunction()->SwitchStack.empty()) {
        Consumer.addKeywordResult("case");
        Consumer.addKeywordResult("default");
      }
    } else {
      if (SemaRef.getLangOpts().CPlusPlus) {
        Consumer.addKeywordResult("namespace");
        Consumer.addKeywordResult("template");
      }

      if (S && S->isClassScope()) {
        Consumer.addKeywordResult("explicit");
        Consumer.addKeywordResult("friend");
        Consumer.addKeywordResult("mutable");
        Consumer.addKeywordResult("private");
        Consumer.addKeywordResult("protected");
        Consumer.addKeywordResult("public");
        Consumer.addKeywordResult("virtual");
      }
    }

    if (SemaRef.getLangOpts().CPlusPlus) {
      Consumer.addKeywordResult("using");

      if (SemaRef.getLangOpts().CPlusPlus11)
        Consumer.addKeywordResult("static_assert");
    }
  }
}

std::unique_ptr<TypoCorrectionConsumer> Sema::makeTypoCorrectionConsumer(
    const DeclarationNameInfo &TypoName, Sema::LookupNameKind LookupKind,
    Scope *S, CXXScopeSpec *SS,
    std::unique_ptr<CorrectionCandidateCallback> CCC,
    DeclContext *MemberContext, bool EnteringContext,
    const ObjCObjectPointerType *OPT, bool ErrorRecovery) {

  if (Diags.hasFatalErrorOccurred() || !getLangOpts().SpellChecking ||
      DisableTypoCorrection)
    return nullptr;

  // In Microsoft mode, don't perform typo correction in a template member
  // function dependent context because it interferes with the "lookup into
  // dependent bases of class templates" feature.
  if (getLangOpts().MSVCCompat && CurContext->isDependentContext() &&
      isa<CXXMethodDecl>(CurContext))
    return nullptr;

  // We only attempt to correct typos for identifiers.
  IdentifierInfo *Typo = TypoName.getName().getAsIdentifierInfo();
  if (!Typo)
    return nullptr;

  // If the scope specifier itself was invalid, don't try to correct
  // typos.
  if (SS && SS->isInvalid())
    return nullptr;

  // Never try to correct typos during any kind of code synthesis.
  if (!CodeSynthesisContexts.empty())
    return nullptr;

  // Don't try to correct 'super'.
  if (S && S->isInObjcMethodScope() && Typo == getSuperIdentifier())
    return nullptr;

  // Abort if typo correction already failed for this specific typo.
  IdentifierSourceLocations::iterator locs = TypoCorrectionFailures.find(Typo);
  if (locs != TypoCorrectionFailures.end() &&
      locs->second.count(TypoName.getLoc()))
    return nullptr;

  // Don't try to correct the identifier "vector" when in AltiVec mode.
  // TODO: Figure out why typo correction misbehaves in this case, fix it, and
  // remove this workaround.
  if ((getLangOpts().AltiVec || getLangOpts().ZVector) && Typo->isStr("vector"))
    return nullptr;

  // Provide a stop gap for files that are just seriously broken.  Trying
  // to correct all typos can turn into a HUGE performance penalty, causing
  // some files to take minutes to get rejected by the parser.
  unsigned Limit = getDiagnostics().getDiagnosticOptions().SpellCheckingLimit;
  if (Limit && TyposCorrected >= Limit)
    return nullptr;
  ++TyposCorrected;

  // If we're handling a missing symbol error, using modules, and the
  // special search all modules option is used, look for a missing import.
  if (ErrorRecovery && getLangOpts().Modules &&
      getLangOpts().ModulesSearchAll) {
    // The following has the side effect of loading the missing module.
    getModuleLoader().lookupMissingImports(Typo->getName(),
                                           TypoName.getBeginLoc());
  }

  CorrectionCandidateCallback &CCCRef = *CCC;
  auto Consumer = llvm::make_unique<TypoCorrectionConsumer>(
      *this, TypoName, LookupKind, S, SS, std::move(CCC), MemberContext,
      EnteringContext);

  // Perform name lookup to find visible, similarly-named entities.
  bool IsUnqualifiedLookup = false;
  DeclContext *QualifiedDC = MemberContext;
  if (MemberContext) {
    LookupVisibleDecls(MemberContext, LookupKind, *Consumer);

    // Look in qualified interfaces.
    if (OPT) {
      for (auto *I : OPT->quals())
        LookupVisibleDecls(I, LookupKind, *Consumer);
    }
  } else if (SS && SS->isSet()) {
    QualifiedDC = computeDeclContext(*SS, EnteringContext);
    if (!QualifiedDC)
      return nullptr;

    LookupVisibleDecls(QualifiedDC, LookupKind, *Consumer);
  } else {
    IsUnqualifiedLookup = true;
  }

  // Determine whether we are going to search in the various namespaces for
  // corrections.
  bool SearchNamespaces
    = getLangOpts().CPlusPlus &&
      (IsUnqualifiedLookup || (SS && SS->isSet()));

  if (IsUnqualifiedLookup || SearchNamespaces) {
    // For unqualified lookup, look through all of the names that we have
    // seen in this translation unit.
    // FIXME: Re-add the ability to skip very unlikely potential corrections.
    for (const auto &I : Context.Idents)
      Consumer->FoundName(I.getKey());

    // Walk through identifiers in external identifier sources.
    // FIXME: Re-add the ability to skip very unlikely potential corrections.
    if (IdentifierInfoLookup *External
                            = Context.Idents.getExternalIdentifierLookup()) {
      std::unique_ptr<IdentifierIterator> Iter(External->getIdentifiers());
      do {
        StringRef Name = Iter->Next();
        if (Name.empty())
          break;

        Consumer->FoundName(Name);
      } while (true);
    }
  }

  AddKeywordsToConsumer(*this, *Consumer, S, CCCRef, SS && SS->isNotEmpty());

  // Build the NestedNameSpecifiers for the KnownNamespaces, if we're going
  // to search those namespaces.
  if (SearchNamespaces) {
    // Load any externally-known namespaces.
    if (ExternalSource && !LoadedExternalKnownNamespaces) {
      SmallVector<NamespaceDecl *, 4> ExternalKnownNamespaces;
      LoadedExternalKnownNamespaces = true;
      ExternalSource->ReadKnownNamespaces(ExternalKnownNamespaces);
      for (auto *N : ExternalKnownNamespaces)
        KnownNamespaces[N] = true;
    }

    Consumer->addNamespaces(KnownNamespaces);
  }

  return Consumer;
}

/// Try to "correct" a typo in the source code by finding
/// visible declarations whose names are similar to the name that was
/// present in the source code.
///
/// \param TypoName the \c DeclarationNameInfo structure that contains
/// the name that was present in the source code along with its location.
///
/// \param LookupKind the name-lookup criteria used to search for the name.
///
/// \param S the scope in which name lookup occurs.
///
/// \param SS the nested-name-specifier that precedes the name we're
/// looking for, if present.
///
/// \param CCC A CorrectionCandidateCallback object that provides further
/// validation of typo correction candidates. It also provides flags for
/// determining the set of keywords permitted.
///
/// \param MemberContext if non-NULL, the context in which to look for
/// a member access expression.
///
/// \param EnteringContext whether we're entering the context described by
/// the nested-name-specifier SS.
///
/// \param OPT when non-NULL, the search for visible declarations will
/// also walk the protocols in the qualified interfaces of \p OPT.
///
/// \returns a \c TypoCorrection containing the corrected name if the typo
/// along with information such as the \c NamedDecl where the corrected name
/// was declared, and any additional \c NestedNameSpecifier needed to access
/// it (C++ only). The \c TypoCorrection is empty if there is no correction.
TypoCorrection Sema::CorrectTypo(const DeclarationNameInfo &TypoName,
                                 Sema::LookupNameKind LookupKind,
                                 Scope *S, CXXScopeSpec *SS,
                                 std::unique_ptr<CorrectionCandidateCallback> CCC,
                                 CorrectTypoKind Mode,
                                 DeclContext *MemberContext,
                                 bool EnteringContext,
                                 const ObjCObjectPointerType *OPT,
                                 bool RecordFailure) {
  assert(CCC && "CorrectTypo requires a CorrectionCandidateCallback");

  // Always let the ExternalSource have the first chance at correction, even
  // if we would otherwise have given up.
  if (ExternalSource) {
    if (TypoCorrection Correction = ExternalSource->CorrectTypo(
        TypoName, LookupKind, S, SS, *CCC, MemberContext, EnteringContext, OPT))
      return Correction;
  }

  // Ugly hack equivalent to CTC == CTC_ObjCMessageReceiver;
  // WantObjCSuper is only true for CTC_ObjCMessageReceiver and for
  // some instances of CTC_Unknown, while WantRemainingKeywords is true
  // for CTC_Unknown but not for CTC_ObjCMessageReceiver.
  bool ObjCMessageReceiver = CCC->WantObjCSuper && !CCC->WantRemainingKeywords;

  IdentifierInfo *Typo = TypoName.getName().getAsIdentifierInfo();
  auto Consumer = makeTypoCorrectionConsumer(
      TypoName, LookupKind, S, SS, std::move(CCC), MemberContext,
      EnteringContext, OPT, Mode == CTK_ErrorRecovery);

  if (!Consumer)
    return TypoCorrection();

  // If we haven't found anything, we're done.
  if (Consumer->empty())
    return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

  // Make sure the best edit distance (prior to adding any namespace qualifiers)
  // is not more that about a third of the length of the typo's identifier.
  unsigned ED = Consumer->getBestEditDistance(true);
  unsigned TypoLen = Typo->getName().size();
  if (ED > 0 && TypoLen / ED < 3)
    return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

  TypoCorrection BestTC = Consumer->getNextCorrection();
  TypoCorrection SecondBestTC = Consumer->getNextCorrection();
  if (!BestTC)
    return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

  ED = BestTC.getEditDistance();

  if (TypoLen >= 3 && ED > 0 && TypoLen / ED < 3) {
    // If this was an unqualified lookup and we believe the callback
    // object wouldn't have filtered out possible corrections, note
    // that no correction was found.
    return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);
  }

  // If only a single name remains, return that result.
  if (!SecondBestTC ||
      SecondBestTC.getEditDistance(false) > BestTC.getEditDistance(false)) {
    const TypoCorrection &Result = BestTC;

    // Don't correct to a keyword that's the same as the typo; the keyword
    // wasn't actually in scope.
    if (ED == 0 && Result.isKeyword())
      return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

    TypoCorrection TC = Result;
    TC.setCorrectionRange(SS, TypoName);
    checkCorrectionVisibility(*this, TC);
    return TC;
  } else if (SecondBestTC && ObjCMessageReceiver) {
    // Prefer 'super' when we're completing in a message-receiver
    // context.

    if (BestTC.getCorrection().getAsString() != "super") {
      if (SecondBestTC.getCorrection().getAsString() == "super")
        BestTC = SecondBestTC;
      else if ((*Consumer)["super"].front().isKeyword())
        BestTC = (*Consumer)["super"].front();
    }
    // Don't correct to a keyword that's the same as the typo; the keyword
    // wasn't actually in scope.
    if (BestTC.getEditDistance() == 0 ||
        BestTC.getCorrection().getAsString() != "super")
      return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

    BestTC.setCorrectionRange(SS, TypoName);
    return BestTC;
  }

  // Record the failure's location if needed and return an empty correction. If
  // this was an unqualified lookup and we believe the callback object did not
  // filter out possible corrections, also cache the failure for the typo.
  return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure && !SecondBestTC);
}

/// Try to "correct" a typo in the source code by finding
/// visible declarations whose names are similar to the name that was
/// present in the source code.
///
/// \param TypoName the \c DeclarationNameInfo structure that contains
/// the name that was present in the source code along with its location.
///
/// \param LookupKind the name-lookup criteria used to search for the name.
///
/// \param S the scope in which name lookup occurs.
///
/// \param SS the nested-name-specifier that precedes the name we're
/// looking for, if present.
///
/// \param CCC A CorrectionCandidateCallback object that provides further
/// validation of typo correction candidates. It also provides flags for
/// determining the set of keywords permitted.
///
/// \param TDG A TypoDiagnosticGenerator functor that will be used to print
/// diagnostics when the actual typo correction is attempted.
///
/// \param TRC A TypoRecoveryCallback functor that will be used to build an
/// Expr from a typo correction candidate.
///
/// \param MemberContext if non-NULL, the context in which to look for
/// a member access expression.
///
/// \param EnteringContext whether we're entering the context described by
/// the nested-name-specifier SS.
///
/// \param OPT when non-NULL, the search for visible declarations will
/// also walk the protocols in the qualified interfaces of \p OPT.
///
/// \returns a new \c TypoExpr that will later be replaced in the AST with an
/// Expr representing the result of performing typo correction, or nullptr if
/// typo correction is not possible. If nullptr is returned, no diagnostics will
/// be emitted and it is the responsibility of the caller to emit any that are
/// needed.
TypoExpr *Sema::CorrectTypoDelayed(
    const DeclarationNameInfo &TypoName, Sema::LookupNameKind LookupKind,
    Scope *S, CXXScopeSpec *SS,
    std::unique_ptr<CorrectionCandidateCallback> CCC,
    TypoDiagnosticGenerator TDG, TypoRecoveryCallback TRC, CorrectTypoKind Mode,
    DeclContext *MemberContext, bool EnteringContext,
    const ObjCObjectPointerType *OPT) {
  assert(CCC && "CorrectTypoDelayed requires a CorrectionCandidateCallback");

  auto Consumer = makeTypoCorrectionConsumer(
      TypoName, LookupKind, S, SS, std::move(CCC), MemberContext,
      EnteringContext, OPT, Mode == CTK_ErrorRecovery);

  // Give the external sema source a chance to correct the typo.
  TypoCorrection ExternalTypo;
  if (ExternalSource && Consumer) {
    ExternalTypo = ExternalSource->CorrectTypo(
        TypoName, LookupKind, S, SS, *Consumer->getCorrectionValidator(),
        MemberContext, EnteringContext, OPT);
    if (ExternalTypo)
      Consumer->addCorrection(ExternalTypo);
  }

  if (!Consumer || Consumer->empty())
    return nullptr;

  // Make sure the best edit distance (prior to adding any namespace qualifiers)
  // is not more that about a third of the length of the typo's identifier.
  unsigned ED = Consumer->getBestEditDistance(true);
  IdentifierInfo *Typo = TypoName.getName().getAsIdentifierInfo();
  if (!ExternalTypo && ED > 0 && Typo->getName().size() / ED < 3)
    return nullptr;

  ExprEvalContexts.back().NumTypos++;
  return createDelayedTypo(std::move(Consumer), std::move(TDG), std::move(TRC));
}

void TypoCorrection::addCorrectionDecl(NamedDecl *CDecl) {
  if (!CDecl) return;

  if (isKeyword())
    CorrectionDecls.clear();

  CorrectionDecls.push_back(CDecl);

  if (!CorrectionName)
    CorrectionName = CDecl->getDeclName();
}

std::string TypoCorrection::getAsString(const LangOptions &LO) const {
  if (CorrectionNameSpec) {
    std::string tmpBuffer;
    llvm::raw_string_ostream PrefixOStream(tmpBuffer);
    CorrectionNameSpec->print(PrefixOStream, PrintingPolicy(LO));
    PrefixOStream << CorrectionName;
    return PrefixOStream.str();
  }

  return CorrectionName.getAsString();
}

bool CorrectionCandidateCallback::ValidateCandidate(
    const TypoCorrection &candidate) {
  if (!candidate.isResolved())
    return true;

  if (candidate.isKeyword())
    return WantTypeSpecifiers || WantExpressionKeywords || WantCXXNamedCasts ||
           WantRemainingKeywords || WantObjCSuper;

  bool HasNonType = false;
  bool HasStaticMethod = false;
  bool HasNonStaticMethod = false;
  for (Decl *D : candidate) {
    if (FunctionTemplateDecl *FTD = dyn_cast<FunctionTemplateDecl>(D))
      D = FTD->getTemplatedDecl();
    if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(D)) {
      if (Method->isStatic())
        HasStaticMethod = true;
      else
        HasNonStaticMethod = true;
    }
    if (!isa<TypeDecl>(D))
      HasNonType = true;
  }

  if (IsAddressOfOperand && HasNonStaticMethod && !HasStaticMethod &&
      !candidate.getCorrectionSpecifier())
    return false;

  return WantTypeSpecifiers || HasNonType;
}

FunctionCallFilterCCC::FunctionCallFilterCCC(Sema &SemaRef, unsigned NumArgs,
                                             bool HasExplicitTemplateArgs,
                                             MemberExpr *ME)
    : NumArgs(NumArgs), HasExplicitTemplateArgs(HasExplicitTemplateArgs),
      CurContext(SemaRef.CurContext), MemberFn(ME) {
  WantTypeSpecifiers = false;
  WantFunctionLikeCasts = SemaRef.getLangOpts().CPlusPlus && NumArgs == 1;
  WantRemainingKeywords = false;
}

bool FunctionCallFilterCCC::ValidateCandidate(const TypoCorrection &candidate) {
  if (!candidate.getCorrectionDecl())
    return candidate.isKeyword();

  for (auto *C : candidate) {
    FunctionDecl *FD = nullptr;
    NamedDecl *ND = C->getUnderlyingDecl();
    if (FunctionTemplateDecl *FTD = dyn_cast<FunctionTemplateDecl>(ND))
      FD = FTD->getTemplatedDecl();
    if (!HasExplicitTemplateArgs && !FD) {
      if (!(FD = dyn_cast<FunctionDecl>(ND)) && isa<ValueDecl>(ND)) {
        // If the Decl is neither a function nor a template function,
        // determine if it is a pointer or reference to a function. If so,
        // check against the number of arguments expected for the pointee.
        QualType ValType = cast<ValueDecl>(ND)->getType();
        if (ValType.isNull())
          continue;
        if (ValType->isAnyPointerType() || ValType->isReferenceType())
          ValType = ValType->getPointeeType();
        if (const FunctionProtoType *FPT = ValType->getAs<FunctionProtoType>())
          if (FPT->getNumParams() == NumArgs)
            return true;
      }
    }

    // Skip the current candidate if it is not a FunctionDecl or does not accept
    // the current number of arguments.
    if (!FD || !(FD->getNumParams() >= NumArgs &&
                 FD->getMinRequiredArguments() <= NumArgs))
      continue;

    // If the current candidate is a non-static C++ method, skip the candidate
    // unless the method being corrected--or the current DeclContext, if the
    // function being corrected is not a method--is a method in the same class
    // or a descendent class of the candidate's parent class.
    if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD)) {
      if (MemberFn || !MD->isStatic()) {
        CXXMethodDecl *CurMD =
            MemberFn
                ? dyn_cast_or_null<CXXMethodDecl>(MemberFn->getMemberDecl())
                : dyn_cast_or_null<CXXMethodDecl>(CurContext);
        CXXRecordDecl *CurRD =
            CurMD ? CurMD->getParent()->getCanonicalDecl() : nullptr;
        CXXRecordDecl *RD = MD->getParent()->getCanonicalDecl();
        if (!CurRD || (CurRD != RD && !CurRD->isDerivedFrom(RD)))
          continue;
      }
    }
    return true;
  }
  return false;
}

void Sema::diagnoseTypo(const TypoCorrection &Correction,
                        const PartialDiagnostic &TypoDiag,
                        bool ErrorRecovery) {
  diagnoseTypo(Correction, TypoDiag, PDiag(diag::note_previous_decl),
               ErrorRecovery);
}

/// Find which declaration we should import to provide the definition of
/// the given declaration.
static NamedDecl *getDefinitionToImport(NamedDecl *D) {
  if (VarDecl *VD = dyn_cast<VarDecl>(D))
    return VD->getDefinition();
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
    return FD->getDefinition();
  if (TagDecl *TD = dyn_cast<TagDecl>(D))
    return TD->getDefinition();
  if (ObjCInterfaceDecl *ID = dyn_cast<ObjCInterfaceDecl>(D))
    return ID->getDefinition();
  if (ObjCProtocolDecl *PD = dyn_cast<ObjCProtocolDecl>(D))
    return PD->getDefinition();
  if (TemplateDecl *TD = dyn_cast<TemplateDecl>(D))
    return getDefinitionToImport(TD->getTemplatedDecl());
  return nullptr;
}

void Sema::diagnoseMissingImport(SourceLocation Loc, NamedDecl *Decl,
                                 MissingImportKind MIK, bool Recover) {
  // Suggest importing a module providing the definition of this entity, if
  // possible.
  NamedDecl *Def = getDefinitionToImport(Decl);
  if (!Def)
    Def = Decl;

  Module *Owner = getOwningModule(Def);
  assert(Owner && "definition of hidden declaration is not in a module");

  llvm::SmallVector<Module*, 8> OwningModules;
  OwningModules.push_back(Owner);
  auto Merged = Context.getModulesWithMergedDefinition(Def);
  OwningModules.insert(OwningModules.end(), Merged.begin(), Merged.end());

  diagnoseMissingImport(Loc, Decl, Decl->getLocation(), OwningModules, MIK,
                        Recover);
}

/// Get a "quoted.h" or <angled.h> include path to use in a diagnostic
/// suggesting the addition of a #include of the specified file.
static std::string getIncludeStringForHeader(Preprocessor &PP,
                                             const FileEntry *E) {
  bool IsSystem;
  auto Path =
      PP.getHeaderSearchInfo().suggestPathToFileForDiagnostics(E, &IsSystem);
  return (IsSystem ? '<' : '"') + Path + (IsSystem ? '>' : '"');
}

void Sema::diagnoseMissingImport(SourceLocation UseLoc, NamedDecl *Decl,
                                 SourceLocation DeclLoc,
                                 ArrayRef<Module *> Modules,
                                 MissingImportKind MIK, bool Recover) {
  assert(!Modules.empty());

  // Weed out duplicates from module list.
  llvm::SmallVector<Module*, 8> UniqueModules;
  llvm::SmallDenseSet<Module*, 8> UniqueModuleSet;
  for (auto *M : Modules)
    if (UniqueModuleSet.insert(M).second)
      UniqueModules.push_back(M);
  Modules = UniqueModules;

  if (Modules.size() > 1) {
    std::string ModuleList;
    unsigned N = 0;
    for (Module *M : Modules) {
      ModuleList += "\n        ";
      if (++N == 5 && N != Modules.size()) {
        ModuleList += "[...]";
        break;
      }
      ModuleList += M->getFullModuleName();
    }

    Diag(UseLoc, diag::err_module_unimported_use_multiple)
      << (int)MIK << Decl << ModuleList;
  } else if (const FileEntry *E = PP.getModuleHeaderToIncludeForDiagnostics(
                 UseLoc, Modules[0], DeclLoc)) {
    // The right way to make the declaration visible is to include a header;
    // suggest doing so.
    //
    // FIXME: Find a smart place to suggest inserting a #include, and add
    // a FixItHint there.
    Diag(UseLoc, diag::err_module_unimported_use_header)
      << (int)MIK << Decl << Modules[0]->getFullModuleName()
      << getIncludeStringForHeader(PP, E);
  } else {
    // FIXME: Add a FixItHint that imports the corresponding module.
    Diag(UseLoc, diag::err_module_unimported_use)
      << (int)MIK << Decl << Modules[0]->getFullModuleName();
  }

  unsigned DiagID;
  switch (MIK) {
  case MissingImportKind::Declaration:
    DiagID = diag::note_previous_declaration;
    break;
  case MissingImportKind::Definition:
    DiagID = diag::note_previous_definition;
    break;
  case MissingImportKind::DefaultArgument:
    DiagID = diag::note_default_argument_declared_here;
    break;
  case MissingImportKind::ExplicitSpecialization:
    DiagID = diag::note_explicit_specialization_declared_here;
    break;
  case MissingImportKind::PartialSpecialization:
    DiagID = diag::note_partial_specialization_declared_here;
    break;
  }
  Diag(DeclLoc, DiagID);

  // Try to recover by implicitly importing this module.
  if (Recover)
    createImplicitModuleImportForErrorRecovery(UseLoc, Modules[0]);
}

/// Diagnose a successfully-corrected typo. Separated from the correction
/// itself to allow external validation of the result, etc.
///
/// \param Correction The result of performing typo correction.
/// \param TypoDiag The diagnostic to produce. This will have the corrected
///        string added to it (and usually also a fixit).
/// \param PrevNote A note to use when indicating the location of the entity to
///        which we are correcting. Will have the correction string added to it.
/// \param ErrorRecovery If \c true (the default), the caller is going to
///        recover from the typo as if the corrected string had been typed.
///        In this case, \c PDiag must be an error, and we will attach a fixit
///        to it.
void Sema::diagnoseTypo(const TypoCorrection &Correction,
                        const PartialDiagnostic &TypoDiag,
                        const PartialDiagnostic &PrevNote,
                        bool ErrorRecovery) {
  std::string CorrectedStr = Correction.getAsString(getLangOpts());
  std::string CorrectedQuotedStr = Correction.getQuoted(getLangOpts());
  FixItHint FixTypo = FixItHint::CreateReplacement(
      Correction.getCorrectionRange(), CorrectedStr);

  // Maybe we're just missing a module import.
  if (Correction.requiresImport()) {
    NamedDecl *Decl = Correction.getFoundDecl();
    assert(Decl && "import required but no declaration to import");

    diagnoseMissingImport(Correction.getCorrectionRange().getBegin(), Decl,
                          MissingImportKind::Declaration, ErrorRecovery);
    return;
  }

  Diag(Correction.getCorrectionRange().getBegin(), TypoDiag)
    << CorrectedQuotedStr << (ErrorRecovery ? FixTypo : FixItHint());

  NamedDecl *ChosenDecl =
      Correction.isKeyword() ? nullptr : Correction.getFoundDecl();
  if (PrevNote.getDiagID() && ChosenDecl)
    Diag(ChosenDecl->getLocation(), PrevNote)
      << CorrectedQuotedStr << (ErrorRecovery ? FixItHint() : FixTypo);

  // Add any extra diagnostics.
  for (const PartialDiagnostic &PD : Correction.getExtraDiagnostics())
    Diag(Correction.getCorrectionRange().getBegin(), PD);
}

TypoExpr *Sema::createDelayedTypo(std::unique_ptr<TypoCorrectionConsumer> TCC,
                                  TypoDiagnosticGenerator TDG,
                                  TypoRecoveryCallback TRC) {
  assert(TCC && "createDelayedTypo requires a valid TypoCorrectionConsumer");
  auto TE = new (Context) TypoExpr(Context.DependentTy);
  auto &State = DelayedTypos[TE];
  State.Consumer = std::move(TCC);
  State.DiagHandler = std::move(TDG);
  State.RecoveryHandler = std::move(TRC);
  return TE;
}

const Sema::TypoExprState &Sema::getTypoExprState(TypoExpr *TE) const {
  auto Entry = DelayedTypos.find(TE);
  assert(Entry != DelayedTypos.end() &&
         "Failed to get the state for a TypoExpr!");
  return Entry->second;
}

void Sema::clearDelayedTypo(TypoExpr *TE) {
  DelayedTypos.erase(TE);
}

void Sema::ActOnPragmaDump(Scope *S, SourceLocation IILoc, IdentifierInfo *II) {
  DeclarationNameInfo Name(II, IILoc);
  LookupResult R(*this, Name, LookupAnyName, Sema::NotForRedeclaration);
  R.suppressDiagnostics();
  R.setHideTags(false);
  LookupName(R, S);
  R.dump();
}
