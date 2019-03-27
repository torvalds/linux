//===--- IndexSymbol.cpp - Types and functions for indexing symbols -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Index/IndexSymbol.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/Lex/MacroInfo.h"

using namespace clang;
using namespace clang::index;

/// \returns true if \c D is a subclass of 'XCTestCase'.
static bool isUnitTestCase(const ObjCInterfaceDecl *D) {
  if (!D)
    return false;
  while (const ObjCInterfaceDecl *SuperD = D->getSuperClass()) {
    if (SuperD->getName() == "XCTestCase")
      return true;
    D = SuperD;
  }
  return false;
}

/// \returns true if \c D is in a subclass of 'XCTestCase', returns void, has
/// no parameters, and its name starts with 'test'.
static bool isUnitTest(const ObjCMethodDecl *D) {
  if (!D->parameters().empty())
    return false;
  if (!D->getReturnType()->isVoidType())
    return false;
  if (!D->getSelector().getNameForSlot(0).startswith("test"))
    return false;
  return isUnitTestCase(D->getClassInterface());
}

static void checkForIBOutlets(const Decl *D, SymbolPropertySet &PropSet) {
  if (D->hasAttr<IBOutletAttr>()) {
    PropSet |= (SymbolPropertySet)SymbolProperty::IBAnnotated;
  } else if (D->hasAttr<IBOutletCollectionAttr>()) {
    PropSet |= (SymbolPropertySet)SymbolProperty::IBAnnotated;
    PropSet |= (SymbolPropertySet)SymbolProperty::IBOutletCollection;
  }
}

bool index::isFunctionLocalSymbol(const Decl *D) {
  assert(D);

  if (isa<ParmVarDecl>(D))
    return true;

  if (isa<TemplateTemplateParmDecl>(D))
    return true;

  if (isa<ObjCTypeParamDecl>(D))
    return true;

  if (isa<UsingDirectiveDecl>(D))
    return false;
  if (!D->getParentFunctionOrMethod())
    return false;

  if (const NamedDecl *ND = dyn_cast<NamedDecl>(D)) {
    switch (ND->getFormalLinkage()) {
      case NoLinkage:
      case InternalLinkage:
        return true;
      case VisibleNoLinkage:
      case UniqueExternalLinkage:
      case ModuleInternalLinkage:
        llvm_unreachable("Not a sema linkage");
      case ModuleLinkage:
      case ExternalLinkage:
        return false;
    }
  }

  return true;
}

SymbolInfo index::getSymbolInfo(const Decl *D) {
  assert(D);
  SymbolInfo Info;
  Info.Kind = SymbolKind::Unknown;
  Info.SubKind = SymbolSubKind::None;
  Info.Properties = SymbolPropertySet();
  Info.Lang = SymbolLanguage::C;

  if (isFunctionLocalSymbol(D)) {
    Info.Properties |= (SymbolPropertySet)SymbolProperty::Local;
  }
  if (isa<ObjCProtocolDecl>(D->getDeclContext())) {
    Info.Properties |= (SymbolPropertySet)SymbolProperty::ProtocolInterface;
  }

  if (const TagDecl *TD = dyn_cast<TagDecl>(D)) {
    switch (TD->getTagKind()) {
    case TTK_Struct:
      Info.Kind = SymbolKind::Struct; break;
    case TTK_Union:
      Info.Kind = SymbolKind::Union; break;
    case TTK_Class:
      Info.Kind = SymbolKind::Class;
      Info.Lang = SymbolLanguage::CXX;
      break;
    case TTK_Interface:
      Info.Kind = SymbolKind::Protocol;
      Info.Lang = SymbolLanguage::CXX;
      break;
    case TTK_Enum:
      Info.Kind = SymbolKind::Enum; break;
    }

    if (const CXXRecordDecl *CXXRec = dyn_cast<CXXRecordDecl>(D)) {
      if (!CXXRec->isCLike()) {
        Info.Lang = SymbolLanguage::CXX;
        if (CXXRec->getDescribedClassTemplate()) {
          Info.Properties |= (SymbolPropertySet)SymbolProperty::Generic;
        }
      }
    }

    if (isa<ClassTemplatePartialSpecializationDecl>(D)) {
      Info.Properties |= (SymbolPropertySet)SymbolProperty::Generic;
      Info.Properties |=
          (SymbolPropertySet)SymbolProperty::TemplatePartialSpecialization;
    } else if (isa<ClassTemplateSpecializationDecl>(D)) {
      Info.Properties |= (SymbolPropertySet)SymbolProperty::Generic;
      Info.Properties |=
          (SymbolPropertySet)SymbolProperty::TemplateSpecialization;
    }

  } else if (auto *VD = dyn_cast<VarDecl>(D)) {
    Info.Kind = SymbolKind::Variable;
    if (isa<ParmVarDecl>(D)) {
      Info.Kind = SymbolKind::Parameter;
    } else if (isa<CXXRecordDecl>(D->getDeclContext())) {
      Info.Kind = SymbolKind::StaticProperty;
      Info.Lang = SymbolLanguage::CXX;
    }

    if (isa<VarTemplatePartialSpecializationDecl>(D)) {
      Info.Lang = SymbolLanguage::CXX;
      Info.Properties |= (SymbolPropertySet)SymbolProperty::Generic;
      Info.Properties |=
          (SymbolPropertySet)SymbolProperty::TemplatePartialSpecialization;
    } else if (isa<VarTemplateSpecializationDecl>(D)) {
      Info.Lang = SymbolLanguage::CXX;
      Info.Properties |= (SymbolPropertySet)SymbolProperty::Generic;
      Info.Properties |=
          (SymbolPropertySet)SymbolProperty::TemplateSpecialization;
    } else if (VD->getDescribedVarTemplate()) {
      Info.Lang = SymbolLanguage::CXX;
      Info.Properties |= (SymbolPropertySet)SymbolProperty::Generic;
    }

  } else {
    switch (D->getKind()) {
    case Decl::Import:
      Info.Kind = SymbolKind::Module;
      break;
    case Decl::Typedef:
      Info.Kind = SymbolKind::TypeAlias; break; // Lang = C
    case Decl::Function:
      Info.Kind = SymbolKind::Function;
      break;
    case Decl::Field:
      Info.Kind = SymbolKind::Field;
      if (const CXXRecordDecl *
            CXXRec = dyn_cast<CXXRecordDecl>(D->getDeclContext())) {
        if (!CXXRec->isCLike())
          Info.Lang = SymbolLanguage::CXX;
      }
      break;
    case Decl::EnumConstant:
      Info.Kind = SymbolKind::EnumConstant; break;
    case Decl::ObjCInterface:
    case Decl::ObjCImplementation: {
      Info.Kind = SymbolKind::Class;
      Info.Lang = SymbolLanguage::ObjC;
      const ObjCInterfaceDecl *ClsD = dyn_cast<ObjCInterfaceDecl>(D);
      if (!ClsD)
        ClsD = cast<ObjCImplementationDecl>(D)->getClassInterface();
      if (isUnitTestCase(ClsD))
        Info.Properties |= (SymbolPropertySet)SymbolProperty::UnitTest;
      break;
    }
    case Decl::ObjCProtocol:
      Info.Kind = SymbolKind::Protocol;
      Info.Lang = SymbolLanguage::ObjC;
      break;
    case Decl::ObjCCategory:
    case Decl::ObjCCategoryImpl: {
      Info.Kind = SymbolKind::Extension;
      Info.Lang = SymbolLanguage::ObjC;
      const ObjCInterfaceDecl *ClsD = nullptr;
      if (auto *CatD = dyn_cast<ObjCCategoryDecl>(D))
        ClsD = CatD->getClassInterface();
      else
        ClsD = cast<ObjCCategoryImplDecl>(D)->getClassInterface();
      if (isUnitTestCase(ClsD))
        Info.Properties |= (SymbolPropertySet)SymbolProperty::UnitTest;
      break;
    }
    case Decl::ObjCMethod: {
      const ObjCMethodDecl *MD = cast<ObjCMethodDecl>(D);
      Info.Kind = MD->isInstanceMethod() ? SymbolKind::InstanceMethod : SymbolKind::ClassMethod;
      if (MD->isPropertyAccessor()) {
        if (MD->param_size())
          Info.SubKind = SymbolSubKind::AccessorSetter;
        else
          Info.SubKind = SymbolSubKind::AccessorGetter;
      }
      Info.Lang = SymbolLanguage::ObjC;
      if (isUnitTest(MD))
        Info.Properties |= (SymbolPropertySet)SymbolProperty::UnitTest;
      if (D->hasAttr<IBActionAttr>())
        Info.Properties |= (SymbolPropertySet)SymbolProperty::IBAnnotated;
      break;
    }
    case Decl::ObjCProperty:
      Info.Kind = SymbolKind::InstanceProperty;
      Info.Lang = SymbolLanguage::ObjC;
      checkForIBOutlets(D, Info.Properties);
      if (auto *Annot = D->getAttr<AnnotateAttr>()) {
        if (Annot->getAnnotation() == "gk_inspectable")
          Info.Properties |= (SymbolPropertySet)SymbolProperty::GKInspectable;
      }
      break;
    case Decl::ObjCIvar:
      Info.Kind = SymbolKind::Field;
      Info.Lang = SymbolLanguage::ObjC;
      checkForIBOutlets(D, Info.Properties);
      break;
    case Decl::Namespace:
      Info.Kind = SymbolKind::Namespace;
      Info.Lang = SymbolLanguage::CXX;
      break;
    case Decl::NamespaceAlias:
      Info.Kind = SymbolKind::NamespaceAlias;
      Info.Lang = SymbolLanguage::CXX;
      break;
    case Decl::CXXConstructor: {
      Info.Kind = SymbolKind::Constructor;
      Info.Lang = SymbolLanguage::CXX;
      auto *CD = cast<CXXConstructorDecl>(D);
      if (CD->isCopyConstructor())
        Info.SubKind = SymbolSubKind::CXXCopyConstructor;
      else if (CD->isMoveConstructor())
        Info.SubKind = SymbolSubKind::CXXMoveConstructor;
      break;
    }
    case Decl::CXXDestructor:
      Info.Kind = SymbolKind::Destructor;
      Info.Lang = SymbolLanguage::CXX;
      break;
    case Decl::CXXConversion:
      Info.Kind = SymbolKind::ConversionFunction;
      Info.Lang = SymbolLanguage::CXX;
      break;
    case Decl::CXXMethod: {
      const CXXMethodDecl *MD = cast<CXXMethodDecl>(D);
      if (MD->isStatic())
        Info.Kind = SymbolKind::StaticMethod;
      else
        Info.Kind = SymbolKind::InstanceMethod;
      Info.Lang = SymbolLanguage::CXX;
      break;
    }
    case Decl::ClassTemplate:
      Info.Kind = SymbolKind::Class;
      Info.Properties |= (SymbolPropertySet)SymbolProperty::Generic;
      Info.Lang = SymbolLanguage::CXX;
      break;
    case Decl::FunctionTemplate:
      Info.Kind = SymbolKind::Function;
      Info.Properties |= (SymbolPropertySet)SymbolProperty::Generic;
      Info.Lang = SymbolLanguage::CXX;
      if (const CXXMethodDecl *MD = dyn_cast_or_null<CXXMethodDecl>(
                           cast<FunctionTemplateDecl>(D)->getTemplatedDecl())) {
        if (isa<CXXConstructorDecl>(MD))
          Info.Kind = SymbolKind::Constructor;
        else if (isa<CXXDestructorDecl>(MD))
          Info.Kind = SymbolKind::Destructor;
        else if (isa<CXXConversionDecl>(MD))
          Info.Kind = SymbolKind::ConversionFunction;
        else {
          if (MD->isStatic())
            Info.Kind = SymbolKind::StaticMethod;
          else
            Info.Kind = SymbolKind::InstanceMethod;
        }
      }
      break;
    case Decl::TypeAliasTemplate:
      Info.Kind = SymbolKind::TypeAlias;
      Info.Lang = SymbolLanguage::CXX;
      Info.Properties |= (SymbolPropertySet)SymbolProperty::Generic;
      break;
    case Decl::TypeAlias:
      Info.Kind = SymbolKind::TypeAlias;
      Info.Lang = SymbolLanguage::CXX;
      break;
    case Decl::UnresolvedUsingTypename:
      Info.Kind = SymbolKind::Using;
      Info.SubKind = SymbolSubKind::UsingTypename;
      Info.Lang = SymbolLanguage::CXX;
      Info.Properties |= (SymbolPropertySet)SymbolProperty::Generic;
      break;
    case Decl::UnresolvedUsingValue:
      Info.Kind = SymbolKind::Using;
      Info.SubKind = SymbolSubKind::UsingValue;
      Info.Lang = SymbolLanguage::CXX;
      Info.Properties |= (SymbolPropertySet)SymbolProperty::Generic;
      break;
    case Decl::Binding:
      Info.Kind = SymbolKind::Variable;
      Info.Lang = SymbolLanguage::CXX;
      break;
    default:
      break;
    }
  }

  if (Info.Kind == SymbolKind::Unknown)
    return Info;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->getTemplatedKind() ==
          FunctionDecl::TK_FunctionTemplateSpecialization) {
      Info.Properties |= (SymbolPropertySet)SymbolProperty::Generic;
      Info.Properties |=
          (SymbolPropertySet)SymbolProperty::TemplateSpecialization;
    }
  }

  if (Info.Properties & (SymbolPropertySet)SymbolProperty::Generic)
    Info.Lang = SymbolLanguage::CXX;

  if (auto *attr = D->getExternalSourceSymbolAttr()) {
    if (attr->getLanguage() == "Swift")
      Info.Lang = SymbolLanguage::Swift;
  }

  return Info;
}

SymbolInfo index::getSymbolInfoForMacro(const MacroInfo &) {
  SymbolInfo Info;
  Info.Kind = SymbolKind::Macro;
  Info.SubKind = SymbolSubKind::None;
  Info.Properties = SymbolPropertySet();
  Info.Lang = SymbolLanguage::C;
  return Info;
}

bool index::applyForEachSymbolRoleInterruptible(SymbolRoleSet Roles,
                                   llvm::function_ref<bool(SymbolRole)> Fn) {
#define APPLY_FOR_ROLE(Role) \
  if (Roles & (unsigned)SymbolRole::Role) \
    if (!Fn(SymbolRole::Role)) \
      return false;

  APPLY_FOR_ROLE(Declaration);
  APPLY_FOR_ROLE(Definition);
  APPLY_FOR_ROLE(Reference);
  APPLY_FOR_ROLE(Read);
  APPLY_FOR_ROLE(Write);
  APPLY_FOR_ROLE(Call);
  APPLY_FOR_ROLE(Dynamic);
  APPLY_FOR_ROLE(AddressOf);
  APPLY_FOR_ROLE(Implicit);
  APPLY_FOR_ROLE(Undefinition);
  APPLY_FOR_ROLE(RelationChildOf);
  APPLY_FOR_ROLE(RelationBaseOf);
  APPLY_FOR_ROLE(RelationOverrideOf);
  APPLY_FOR_ROLE(RelationReceivedBy);
  APPLY_FOR_ROLE(RelationCalledBy);
  APPLY_FOR_ROLE(RelationExtendedBy);
  APPLY_FOR_ROLE(RelationAccessorOf);
  APPLY_FOR_ROLE(RelationContainedBy);
  APPLY_FOR_ROLE(RelationIBTypeOf);
  APPLY_FOR_ROLE(RelationSpecializationOf);

#undef APPLY_FOR_ROLE

  return true;
}

void index::applyForEachSymbolRole(SymbolRoleSet Roles,
                                   llvm::function_ref<void(SymbolRole)> Fn) {
  applyForEachSymbolRoleInterruptible(Roles, [&](SymbolRole r) -> bool {
    Fn(r);
    return true;
  });
}

void index::printSymbolRoles(SymbolRoleSet Roles, raw_ostream &OS) {
  bool VisitedOnce = false;
  applyForEachSymbolRole(Roles, [&](SymbolRole Role) {
    if (VisitedOnce)
      OS << ',';
    else
      VisitedOnce = true;
    switch (Role) {
    case SymbolRole::Declaration: OS << "Decl"; break;
    case SymbolRole::Definition: OS << "Def"; break;
    case SymbolRole::Reference: OS << "Ref"; break;
    case SymbolRole::Read: OS << "Read"; break;
    case SymbolRole::Write: OS << "Writ"; break;
    case SymbolRole::Call: OS << "Call"; break;
    case SymbolRole::Dynamic: OS << "Dyn"; break;
    case SymbolRole::AddressOf: OS << "Addr"; break;
    case SymbolRole::Implicit: OS << "Impl"; break;
    case SymbolRole::Undefinition: OS << "Undef"; break;
    case SymbolRole::RelationChildOf: OS << "RelChild"; break;
    case SymbolRole::RelationBaseOf: OS << "RelBase"; break;
    case SymbolRole::RelationOverrideOf: OS << "RelOver"; break;
    case SymbolRole::RelationReceivedBy: OS << "RelRec"; break;
    case SymbolRole::RelationCalledBy: OS << "RelCall"; break;
    case SymbolRole::RelationExtendedBy: OS << "RelExt"; break;
    case SymbolRole::RelationAccessorOf: OS << "RelAcc"; break;
    case SymbolRole::RelationContainedBy: OS << "RelCont"; break;
    case SymbolRole::RelationIBTypeOf: OS << "RelIBType"; break;
    case SymbolRole::RelationSpecializationOf: OS << "RelSpecialization"; break;
    }
  });
}

bool index::printSymbolName(const Decl *D, const LangOptions &LO,
                            raw_ostream &OS) {
  if (auto *ND = dyn_cast<NamedDecl>(D)) {
    PrintingPolicy Policy(LO);
    // Forward references can have different template argument names. Suppress
    // the template argument names in constructors to make their name more
    // stable.
    Policy.SuppressTemplateArgsInCXXConstructors = true;
    DeclarationName DeclName = ND->getDeclName();
    if (DeclName.isEmpty())
      return true;
    DeclName.print(OS, Policy);
    return false;
  } else {
    return true;
  }
}

StringRef index::getSymbolKindString(SymbolKind K) {
  switch (K) {
  case SymbolKind::Unknown: return "<unknown>";
  case SymbolKind::Module: return "module";
  case SymbolKind::Namespace: return "namespace";
  case SymbolKind::NamespaceAlias: return "namespace-alias";
  case SymbolKind::Macro: return "macro";
  case SymbolKind::Enum: return "enum";
  case SymbolKind::Struct: return "struct";
  case SymbolKind::Class: return "class";
  case SymbolKind::Protocol: return "protocol";
  case SymbolKind::Extension: return "extension";
  case SymbolKind::Union: return "union";
  case SymbolKind::TypeAlias: return "type-alias";
  case SymbolKind::Function: return "function";
  case SymbolKind::Variable: return "variable";
  case SymbolKind::Field: return "field";
  case SymbolKind::EnumConstant: return "enumerator";
  case SymbolKind::InstanceMethod: return "instance-method";
  case SymbolKind::ClassMethod: return "class-method";
  case SymbolKind::StaticMethod: return "static-method";
  case SymbolKind::InstanceProperty: return "instance-property";
  case SymbolKind::ClassProperty: return "class-property";
  case SymbolKind::StaticProperty: return "static-property";
  case SymbolKind::Constructor: return "constructor";
  case SymbolKind::Destructor: return "destructor";
  case SymbolKind::ConversionFunction: return "coversion-func";
  case SymbolKind::Parameter: return "param";
  case SymbolKind::Using: return "using";
  }
  llvm_unreachable("invalid symbol kind");
}

StringRef index::getSymbolSubKindString(SymbolSubKind K) {
  switch (K) {
  case SymbolSubKind::None: return "<none>";
  case SymbolSubKind::CXXCopyConstructor: return "cxx-copy-ctor";
  case SymbolSubKind::CXXMoveConstructor: return "cxx-move-ctor";
  case SymbolSubKind::AccessorGetter: return "acc-get";
  case SymbolSubKind::AccessorSetter: return "acc-set";
  case SymbolSubKind::UsingTypename: return "using-typename";
  case SymbolSubKind::UsingValue: return "using-value";
  }
  llvm_unreachable("invalid symbol subkind");
}

StringRef index::getSymbolLanguageString(SymbolLanguage K) {
  switch (K) {
  case SymbolLanguage::C: return "C";
  case SymbolLanguage::ObjC: return "ObjC";
  case SymbolLanguage::CXX: return "C++";
  case SymbolLanguage::Swift: return "Swift";
  }
  llvm_unreachable("invalid symbol language kind");
}

void index::applyForEachSymbolProperty(SymbolPropertySet Props,
                                  llvm::function_ref<void(SymbolProperty)> Fn) {
#define APPLY_FOR_PROPERTY(K)                                                  \
  if (Props & (SymbolPropertySet)SymbolProperty::K)                            \
  Fn(SymbolProperty::K)

  APPLY_FOR_PROPERTY(Generic);
  APPLY_FOR_PROPERTY(TemplatePartialSpecialization);
  APPLY_FOR_PROPERTY(TemplateSpecialization);
  APPLY_FOR_PROPERTY(UnitTest);
  APPLY_FOR_PROPERTY(IBAnnotated);
  APPLY_FOR_PROPERTY(IBOutletCollection);
  APPLY_FOR_PROPERTY(GKInspectable);
  APPLY_FOR_PROPERTY(Local);
  APPLY_FOR_PROPERTY(ProtocolInterface);

#undef APPLY_FOR_PROPERTY
}

void index::printSymbolProperties(SymbolPropertySet Props, raw_ostream &OS) {
  bool VisitedOnce = false;
  applyForEachSymbolProperty(Props, [&](SymbolProperty Prop) {
    if (VisitedOnce)
      OS << ',';
    else
      VisitedOnce = true;
    switch (Prop) {
    case SymbolProperty::Generic: OS << "Gen"; break;
    case SymbolProperty::TemplatePartialSpecialization: OS << "TPS"; break;
    case SymbolProperty::TemplateSpecialization: OS << "TS"; break;
    case SymbolProperty::UnitTest: OS << "test"; break;
    case SymbolProperty::IBAnnotated: OS << "IB"; break;
    case SymbolProperty::IBOutletCollection: OS << "IBColl"; break;
    case SymbolProperty::GKInspectable: OS << "GKI"; break;
    case SymbolProperty::Local: OS << "local"; break;
    case SymbolProperty::ProtocolInterface: OS << "protocol"; break;
    }
  });
}
