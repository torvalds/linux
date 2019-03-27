//===--- Lookup.cpp - Framework for clang refactoring tools ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines helper methods for clang tools performing name lookup.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Core/Lookup.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclarationName.h"
using namespace clang;
using namespace clang::tooling;

// Gets all namespaces that \p Context is in as a vector (ignoring anonymous
// namespaces). The inner namespaces come before outer namespaces in the vector.
// For example, if the context is in the following namespace:
//    `namespace a { namespace b { namespace c ( ... ) } }`,
// the vector will be `{c, b, a}`.
static llvm::SmallVector<const NamespaceDecl *, 4>
getAllNamedNamespaces(const DeclContext *Context) {
  llvm::SmallVector<const NamespaceDecl *, 4> Namespaces;
  auto GetNextNamedNamespace = [](const DeclContext *Context) {
    // Look past non-namespaces and anonymous namespaces on FromContext.
    while (Context && (!isa<NamespaceDecl>(Context) ||
                       cast<NamespaceDecl>(Context)->isAnonymousNamespace()))
      Context = Context->getParent();
    return Context;
  };
  for (Context = GetNextNamedNamespace(Context); Context != nullptr;
       Context = GetNextNamedNamespace(Context->getParent()))
    Namespaces.push_back(cast<NamespaceDecl>(Context));
  return Namespaces;
}

// Returns true if the context in which the type is used and the context in
// which the type is declared are the same semantical namespace but different
// lexical namespaces.
static bool
usingFromDifferentCanonicalNamespace(const DeclContext *FromContext,
                                     const DeclContext *UseContext) {
  // We can skip anonymous namespace because:
  // 1. `FromContext` and `UseContext` must be in the same anonymous namespaces
  // since referencing across anonymous namespaces is not possible.
  // 2. If `FromContext` and `UseContext` are in the same anonymous namespace,
  // the function will still return `false` as expected.
  llvm::SmallVector<const NamespaceDecl *, 4> FromNamespaces =
      getAllNamedNamespaces(FromContext);
  llvm::SmallVector<const NamespaceDecl *, 4> UseNamespaces =
      getAllNamedNamespaces(UseContext);
  // If `UseContext` has fewer level of nested namespaces, it cannot be in the
  // same canonical namespace as the `FromContext`.
  if (UseNamespaces.size() < FromNamespaces.size())
    return false;
  unsigned Diff = UseNamespaces.size() - FromNamespaces.size();
  auto FromIter = FromNamespaces.begin();
  // Only compare `FromNamespaces` with namespaces in `UseNamespaces` that can
  // collide, i.e. the top N namespaces where N is the number of namespaces in
  // `FromNamespaces`.
  auto UseIter = UseNamespaces.begin() + Diff;
  for (; FromIter != FromNamespaces.end() && UseIter != UseNamespaces.end();
       ++FromIter, ++UseIter) {
    // Literally the same namespace, not a collision.
    if (*FromIter == *UseIter)
      return false;
    // Now check the names. If they match we have a different canonical
    // namespace with the same name.
    if (cast<NamespaceDecl>(*FromIter)->getDeclName() ==
        cast<NamespaceDecl>(*UseIter)->getDeclName())
      return true;
  }
  assert(FromIter == FromNamespaces.end() && UseIter == UseNamespaces.end());
  return false;
}

static StringRef getBestNamespaceSubstr(const DeclContext *DeclA,
                                        StringRef NewName,
                                        bool HadLeadingColonColon) {
  while (true) {
    while (DeclA && !isa<NamespaceDecl>(DeclA))
      DeclA = DeclA->getParent();

    // Fully qualified it is! Leave :: in place if it's there already.
    if (!DeclA)
      return HadLeadingColonColon ? NewName : NewName.substr(2);

    // Otherwise strip off redundant namespace qualifications from the new name.
    // We use the fully qualified name of the namespace and remove that part
    // from NewName if it has an identical prefix.
    std::string NS =
        "::" + cast<NamespaceDecl>(DeclA)->getQualifiedNameAsString() + "::";
    if (NewName.startswith(NS))
      return NewName.substr(NS.size());

    // No match yet. Strip of a namespace from the end of the chain and try
    // again. This allows to get optimal qualifications even if the old and new
    // decl only share common namespaces at a higher level.
    DeclA = DeclA->getParent();
  }
}

/// Check if the name specifier begins with a written "::".
static bool isFullyQualified(const NestedNameSpecifier *NNS) {
  while (NNS) {
    if (NNS->getKind() == NestedNameSpecifier::Global)
      return true;
    NNS = NNS->getPrefix();
  }
  return false;
}

// Returns true if spelling symbol \p QName as \p Spelling in \p UseContext is
// ambiguous. For example, if QName is "::y::bar" and the spelling is "y::bar"
// in `UseContext` "a" that contains a nested namespace "a::y", then "y::bar"
// can be resolved to ::a::y::bar, which can cause compile error.
// FIXME: consider using namespaces.
static bool isAmbiguousNameInScope(StringRef Spelling, StringRef QName,
                                   const DeclContext &UseContext) {
  assert(QName.startswith("::"));
  if (Spelling.startswith("::"))
    return false;

  // Lookup the first component of Spelling in all enclosing namespaces and
  // check if there is any existing symbols with the same name but in different
  // scope.
  StringRef Head = Spelling.split("::").first;

  llvm::SmallVector<const NamespaceDecl *, 4> UseNamespaces =
      getAllNamedNamespaces(&UseContext);
  auto &AST = UseContext.getParentASTContext();
  StringRef TrimmedQName = QName.substr(2);
  for (const auto *NS : UseNamespaces) {
    auto LookupRes = NS->lookup(DeclarationName(&AST.Idents.get(Head)));
    if (!LookupRes.empty()) {
      for (const NamedDecl *Res : LookupRes)
        if (!TrimmedQName.startswith(Res->getQualifiedNameAsString()))
          return true;
    }
  }
  return false;
}

std::string tooling::replaceNestedName(const NestedNameSpecifier *Use,
                                       const DeclContext *UseContext,
                                       const NamedDecl *FromDecl,
                                       StringRef ReplacementString) {
  assert(ReplacementString.startswith("::") &&
         "Expected fully-qualified name!");

  // We can do a raw name replacement when we are not inside the namespace for
  // the original class/function and it is not in the global namespace.  The
  // assumption is that outside the original namespace we must have a using
  // statement that makes this work out and that other parts of this refactor
  // will automatically fix using statements to point to the new class/function.
  // However, if the `FromDecl` is a class forward declaration, the reference is
  // still considered as referring to the original definition, so we can't do a
  // raw name replacement in this case.
  const bool class_name_only = !Use;
  const bool in_global_namespace =
      isa<TranslationUnitDecl>(FromDecl->getDeclContext());
  const bool is_class_forward_decl =
      isa<CXXRecordDecl>(FromDecl) &&
      !cast<CXXRecordDecl>(FromDecl)->isCompleteDefinition();
  if (class_name_only && !in_global_namespace && !is_class_forward_decl &&
      !usingFromDifferentCanonicalNamespace(FromDecl->getDeclContext(),
                                            UseContext)) {
    auto Pos = ReplacementString.rfind("::");
    return Pos != StringRef::npos ? ReplacementString.substr(Pos + 2)
                                  : ReplacementString;
  }
  // We did not match this because of a using statement, so we will need to
  // figure out how good a namespace match we have with our destination type.
  // We work backwards (from most specific possible namespace to least
  // specific).
  StringRef Suggested = getBestNamespaceSubstr(UseContext, ReplacementString,
                                               isFullyQualified(Use));
  // Use the fully qualified name if the suggested name is ambiguous.
  // FIXME: consider re-shortening the name until the name is not ambiguous. We
  // are not doing this because ambiguity is pretty bad and we should not try to
  // be clever in handling such cases. Making this noticeable to users seems to
  // be a better option.
  return isAmbiguousNameInScope(Suggested, ReplacementString, *UseContext)
             ? ReplacementString
             : Suggested;
}
