//===- CodegenNameGenerator.cpp - Codegen name generation -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Determines the name that the symbol will get for code generation.
//
//===----------------------------------------------------------------------===//

#include "clang/Index/CodegenNameGenerator.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/VTableBuilder.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::index;

struct CodegenNameGenerator::Implementation {
  std::unique_ptr<MangleContext> MC;
  llvm::DataLayout DL;

  Implementation(ASTContext &Ctx)
    : MC(Ctx.createMangleContext()),
      DL(Ctx.getTargetInfo().getDataLayout()) {}

  bool writeName(const Decl *D, raw_ostream &OS) {
    // First apply frontend mangling.
    SmallString<128> FrontendBuf;
    llvm::raw_svector_ostream FrontendBufOS(FrontendBuf);
    if (auto *FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->isDependentContext())
        return true;
      if (writeFuncOrVarName(FD, FrontendBufOS))
        return true;
    } else if (auto *VD = dyn_cast<VarDecl>(D)) {
      if (writeFuncOrVarName(VD, FrontendBufOS))
        return true;
    } else if (auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
      MC->mangleObjCMethodNameWithoutSize(MD, OS);
      return false;
    } else if (auto *ID = dyn_cast<ObjCInterfaceDecl>(D)) {
      writeObjCClassName(ID, FrontendBufOS);
    } else {
      return true;
    }

    // Now apply backend mangling.
    llvm::Mangler::getNameWithPrefix(OS, FrontendBufOS.str(), DL);
    return false;
  }

  std::string getName(const Decl *D) {
    std::string Name;
    {
      llvm::raw_string_ostream OS(Name);
      writeName(D, OS);
    }
    return Name;
  }

  enum ObjCKind {
    ObjCClass,
    ObjCMetaclass,
  };

  std::vector<std::string> getAllManglings(const ObjCContainerDecl *OCD) {
    StringRef ClassName;
    if (const auto *OID = dyn_cast<ObjCInterfaceDecl>(OCD))
      ClassName = OID->getObjCRuntimeNameAsString();
    else if (const auto *OID = dyn_cast<ObjCImplementationDecl>(OCD))
      ClassName = OID->getObjCRuntimeNameAsString();

    if (ClassName.empty())
      return {};

    auto Mangle = [&](ObjCKind Kind, StringRef ClassName) -> std::string {
      SmallString<40> Mangled;
      auto Prefix = getClassSymbolPrefix(Kind, OCD->getASTContext());
      llvm::Mangler::getNameWithPrefix(Mangled, Prefix + ClassName, DL);
      return Mangled.str();
    };

    return {
      Mangle(ObjCClass, ClassName),
      Mangle(ObjCMetaclass, ClassName),
    };
  }

  std::vector<std::string> getAllManglings(const Decl *D) {
    if (const auto *OCD = dyn_cast<ObjCContainerDecl>(D))
      return getAllManglings(OCD);

    if (!(isa<CXXRecordDecl>(D) || isa<CXXMethodDecl>(D)))
      return {};

    const NamedDecl *ND = cast<NamedDecl>(D);

    ASTContext &Ctx = ND->getASTContext();
    std::unique_ptr<MangleContext> M(Ctx.createMangleContext());

    std::vector<std::string> Manglings;

    auto hasDefaultCXXMethodCC = [](ASTContext &C, const CXXMethodDecl *MD) {
      auto DefaultCC = C.getDefaultCallingConvention(/*IsVariadic=*/false,
                                                     /*IsCSSMethod=*/true);
      auto CC = MD->getType()->getAs<FunctionProtoType>()->getCallConv();
      return CC == DefaultCC;
    };

    if (const auto *CD = dyn_cast_or_null<CXXConstructorDecl>(ND)) {
      Manglings.emplace_back(getMangledStructor(CD, Ctor_Base));

      if (Ctx.getTargetInfo().getCXXABI().isItaniumFamily())
        if (!CD->getParent()->isAbstract())
          Manglings.emplace_back(getMangledStructor(CD, Ctor_Complete));

      if (Ctx.getTargetInfo().getCXXABI().isMicrosoft())
        if (CD->hasAttr<DLLExportAttr>() && CD->isDefaultConstructor())
          if (!(hasDefaultCXXMethodCC(Ctx, CD) && CD->getNumParams() == 0))
            Manglings.emplace_back(getMangledStructor(CD, Ctor_DefaultClosure));
    } else if (const auto *DD = dyn_cast_or_null<CXXDestructorDecl>(ND)) {
      Manglings.emplace_back(getMangledStructor(DD, Dtor_Base));
      if (Ctx.getTargetInfo().getCXXABI().isItaniumFamily()) {
        Manglings.emplace_back(getMangledStructor(DD, Dtor_Complete));
        if (DD->isVirtual())
          Manglings.emplace_back(getMangledStructor(DD, Dtor_Deleting));
      }
    } else if (const auto *MD = dyn_cast_or_null<CXXMethodDecl>(ND)) {
      Manglings.emplace_back(getName(ND));
      if (MD->isVirtual())
        if (const auto *TIV = Ctx.getVTableContext()->getThunkInfo(MD))
          for (const auto &T : *TIV)
            Manglings.emplace_back(getMangledThunk(MD, T));
    }

    return Manglings;
  }

private:
  bool writeFuncOrVarName(const NamedDecl *D, raw_ostream &OS) {
    if (MC->shouldMangleDeclName(D)) {
      if (const auto *CtorD = dyn_cast<CXXConstructorDecl>(D))
        MC->mangleCXXCtor(CtorD, Ctor_Complete, OS);
      else if (const auto *DtorD = dyn_cast<CXXDestructorDecl>(D))
        MC->mangleCXXDtor(DtorD, Dtor_Complete, OS);
      else
        MC->mangleName(D, OS);
      return false;
    } else {
      IdentifierInfo *II = D->getIdentifier();
      if (!II)
        return true;
      OS << II->getName();
      return false;
    }
  }

  void writeObjCClassName(const ObjCInterfaceDecl *D, raw_ostream &OS) {
    OS << getClassSymbolPrefix(ObjCClass, D->getASTContext());
    OS << D->getObjCRuntimeNameAsString();
  }

  static StringRef getClassSymbolPrefix(ObjCKind Kind, const ASTContext &Context) {
    if (Context.getLangOpts().ObjCRuntime.isGNUFamily())
      return Kind == ObjCMetaclass ? "_OBJC_METACLASS_" : "_OBJC_CLASS_";
    return Kind == ObjCMetaclass ? "OBJC_METACLASS_$_" : "OBJC_CLASS_$_";
  }

  std::string getMangledStructor(const NamedDecl *ND, unsigned StructorType) {
    std::string FrontendBuf;
    llvm::raw_string_ostream FOS(FrontendBuf);

    if (const auto *CD = dyn_cast_or_null<CXXConstructorDecl>(ND))
      MC->mangleCXXCtor(CD, static_cast<CXXCtorType>(StructorType), FOS);
    else if (const auto *DD = dyn_cast_or_null<CXXDestructorDecl>(ND))
      MC->mangleCXXDtor(DD, static_cast<CXXDtorType>(StructorType), FOS);

    std::string BackendBuf;
    llvm::raw_string_ostream BOS(BackendBuf);

    llvm::Mangler::getNameWithPrefix(BOS, FOS.str(), DL);

    return BOS.str();
  }

  std::string getMangledThunk(const CXXMethodDecl *MD, const ThunkInfo &T) {
    std::string FrontendBuf;
    llvm::raw_string_ostream FOS(FrontendBuf);

    MC->mangleThunk(MD, T, FOS);

    std::string BackendBuf;
    llvm::raw_string_ostream BOS(BackendBuf);

    llvm::Mangler::getNameWithPrefix(BOS, FOS.str(), DL);

    return BOS.str();
  }
};

CodegenNameGenerator::CodegenNameGenerator(ASTContext &Ctx)
  : Impl(new Implementation(Ctx)) {
}

CodegenNameGenerator::~CodegenNameGenerator() {
}

bool CodegenNameGenerator::writeName(const Decl *D, raw_ostream &OS) {
  return Impl->writeName(D, OS);
}

std::string CodegenNameGenerator::getName(const Decl *D) {
  return Impl->getName(D);
}

std::vector<std::string> CodegenNameGenerator::getAllManglings(const Decl *D) {
  return Impl->getAllManglings(D);
}
