//===- TemplateName.cpp - C++ Template Name Representation ----------------===//
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

#include "clang/AST/TemplateName.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/TemplateBase.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <string>

using namespace clang;

TemplateArgument
SubstTemplateTemplateParmPackStorage::getArgumentPack() const {
  return TemplateArgument(llvm::makeArrayRef(Arguments, size()));
}

void SubstTemplateTemplateParmStorage::Profile(llvm::FoldingSetNodeID &ID) {
  Profile(ID, Parameter, Replacement);
}

void SubstTemplateTemplateParmStorage::Profile(llvm::FoldingSetNodeID &ID,
                                           TemplateTemplateParmDecl *parameter,
                                               TemplateName replacement) {
  ID.AddPointer(parameter);
  ID.AddPointer(replacement.getAsVoidPointer());
}

void SubstTemplateTemplateParmPackStorage::Profile(llvm::FoldingSetNodeID &ID,
                                                   ASTContext &Context) {
  Profile(ID, Context, Parameter, getArgumentPack());
}

void SubstTemplateTemplateParmPackStorage::Profile(llvm::FoldingSetNodeID &ID,
                                                   ASTContext &Context,
                                           TemplateTemplateParmDecl *Parameter,
                                             const TemplateArgument &ArgPack) {
  ID.AddPointer(Parameter);
  ArgPack.Profile(ID, Context);
}

TemplateName::TemplateName(void *Ptr) {
  Storage = StorageType::getFromOpaqueValue(Ptr);
}

TemplateName::TemplateName(TemplateDecl *Template) : Storage(Template) {}
TemplateName::TemplateName(OverloadedTemplateStorage *Storage)
    : Storage(Storage) {}
TemplateName::TemplateName(SubstTemplateTemplateParmStorage *Storage)
    : Storage(Storage) {}
TemplateName::TemplateName(SubstTemplateTemplateParmPackStorage *Storage)
    : Storage(Storage) {}
TemplateName::TemplateName(QualifiedTemplateName *Qual) : Storage(Qual) {}
TemplateName::TemplateName(DependentTemplateName *Dep) : Storage(Dep) {}

bool TemplateName::isNull() const { return Storage.isNull(); }

TemplateName::NameKind TemplateName::getKind() const {
  if (Storage.is<TemplateDecl *>())
    return Template;
  if (Storage.is<DependentTemplateName *>())
    return DependentTemplate;
  if (Storage.is<QualifiedTemplateName *>())
    return QualifiedTemplate;

  UncommonTemplateNameStorage *uncommon
    = Storage.get<UncommonTemplateNameStorage*>();
  if (uncommon->getAsOverloadedStorage())
    return OverloadedTemplate;
  if (uncommon->getAsSubstTemplateTemplateParm())
    return SubstTemplateTemplateParm;
  return SubstTemplateTemplateParmPack;
}

TemplateDecl *TemplateName::getAsTemplateDecl() const {
  if (TemplateDecl *Template = Storage.dyn_cast<TemplateDecl *>())
    return Template;

  if (QualifiedTemplateName *QTN = getAsQualifiedTemplateName())
    return QTN->getTemplateDecl();

  if (SubstTemplateTemplateParmStorage *sub = getAsSubstTemplateTemplateParm())
    return sub->getReplacement().getAsTemplateDecl();

  return nullptr;
}

OverloadedTemplateStorage *TemplateName::getAsOverloadedTemplate() const {
  if (UncommonTemplateNameStorage *Uncommon =
          Storage.dyn_cast<UncommonTemplateNameStorage *>())
    return Uncommon->getAsOverloadedStorage();

  return nullptr;
}

SubstTemplateTemplateParmStorage *
TemplateName::getAsSubstTemplateTemplateParm() const {
  if (UncommonTemplateNameStorage *uncommon =
          Storage.dyn_cast<UncommonTemplateNameStorage *>())
    return uncommon->getAsSubstTemplateTemplateParm();

  return nullptr;
}

SubstTemplateTemplateParmPackStorage *
TemplateName::getAsSubstTemplateTemplateParmPack() const {
  if (UncommonTemplateNameStorage *Uncommon =
          Storage.dyn_cast<UncommonTemplateNameStorage *>())
    return Uncommon->getAsSubstTemplateTemplateParmPack();

  return nullptr;
}

QualifiedTemplateName *TemplateName::getAsQualifiedTemplateName() const {
  return Storage.dyn_cast<QualifiedTemplateName *>();
}

DependentTemplateName *TemplateName::getAsDependentTemplateName() const {
  return Storage.dyn_cast<DependentTemplateName *>();
}

TemplateName TemplateName::getNameToSubstitute() const {
  TemplateDecl *Decl = getAsTemplateDecl();

  // Substituting a dependent template name: preserve it as written.
  if (!Decl)
    return *this;

  // If we have a template declaration, use the most recent non-friend
  // declaration of that template.
  Decl = cast<TemplateDecl>(Decl->getMostRecentDecl());
  while (Decl->getFriendObjectKind()) {
    Decl = cast<TemplateDecl>(Decl->getPreviousDecl());
    assert(Decl && "all declarations of template are friends");
  }
  return TemplateName(Decl);
}

bool TemplateName::isDependent() const {
  if (TemplateDecl *Template = getAsTemplateDecl()) {
    if (isa<TemplateTemplateParmDecl>(Template))
      return true;
    // FIXME: Hack, getDeclContext() can be null if Template is still
    // initializing due to PCH reading, so we check it before using it.
    // Should probably modify TemplateSpecializationType to allow constructing
    // it without the isDependent() checking.
    return Template->getDeclContext() &&
           Template->getDeclContext()->isDependentContext();
  }

  assert(!getAsOverloadedTemplate() &&
         "overloaded templates shouldn't survive to here");

  return true;
}

bool TemplateName::isInstantiationDependent() const {
  if (QualifiedTemplateName *QTN = getAsQualifiedTemplateName()) {
    if (QTN->getQualifier()->isInstantiationDependent())
      return true;
  }

  return isDependent();
}

bool TemplateName::containsUnexpandedParameterPack() const {
  if (QualifiedTemplateName *QTN = getAsQualifiedTemplateName()) {
    if (QTN->getQualifier()->containsUnexpandedParameterPack())
      return true;
  }

  if (TemplateDecl *Template = getAsTemplateDecl()) {
    if (TemplateTemplateParmDecl *TTP
                                  = dyn_cast<TemplateTemplateParmDecl>(Template))
      return TTP->isParameterPack();

    return false;
  }

  if (DependentTemplateName *DTN = getAsDependentTemplateName())
    return DTN->getQualifier() &&
      DTN->getQualifier()->containsUnexpandedParameterPack();

  return getAsSubstTemplateTemplateParmPack() != nullptr;
}

void
TemplateName::print(raw_ostream &OS, const PrintingPolicy &Policy,
                    bool SuppressNNS) const {
  if (TemplateDecl *Template = Storage.dyn_cast<TemplateDecl *>())
    OS << *Template;
  else if (QualifiedTemplateName *QTN = getAsQualifiedTemplateName()) {
    if (!SuppressNNS)
      QTN->getQualifier()->print(OS, Policy);
    if (QTN->hasTemplateKeyword())
      OS << "template ";
    OS << *QTN->getDecl();
  } else if (DependentTemplateName *DTN = getAsDependentTemplateName()) {
    if (!SuppressNNS && DTN->getQualifier())
      DTN->getQualifier()->print(OS, Policy);
    OS << "template ";

    if (DTN->isIdentifier())
      OS << DTN->getIdentifier()->getName();
    else
      OS << "operator " << getOperatorSpelling(DTN->getOperator());
  } else if (SubstTemplateTemplateParmStorage *subst
               = getAsSubstTemplateTemplateParm()) {
    subst->getReplacement().print(OS, Policy, SuppressNNS);
  } else if (SubstTemplateTemplateParmPackStorage *SubstPack
                                        = getAsSubstTemplateTemplateParmPack())
    OS << *SubstPack->getParameterPack();
  else {
    OverloadedTemplateStorage *OTS = getAsOverloadedTemplate();
    (*OTS->begin())->printName(OS);
  }
}

const DiagnosticBuilder &clang::operator<<(const DiagnosticBuilder &DB,
                                           TemplateName N) {
  std::string NameStr;
  llvm::raw_string_ostream OS(NameStr);
  LangOptions LO;
  LO.CPlusPlus = true;
  LO.Bool = true;
  OS << '\'';
  N.print(OS, PrintingPolicy(LO));
  OS << '\'';
  OS.flush();
  return DB << NameStr;
}

void TemplateName::dump(raw_ostream &OS) const {
  LangOptions LO;  // FIXME!
  LO.CPlusPlus = true;
  LO.Bool = true;
  print(OS, PrintingPolicy(LO));
}

LLVM_DUMP_METHOD void TemplateName::dump() const {
  dump(llvm::errs());
}
