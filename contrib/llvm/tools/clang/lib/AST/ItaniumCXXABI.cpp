//===------- ItaniumCXXABI.cpp - AST support for the Itanium C++ ABI ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides C++ AST support targeting the Itanium C++ ABI, which is
// documented at:
//  http://www.codesourcery.com/public/cxx-abi/abi.html
//  http://www.codesourcery.com/public/cxx-abi/abi-eh.html
//
// It also supports the closely-related ARM C++ ABI, documented at:
// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0041c/IHI0041C_cppabi.pdf
//
//===----------------------------------------------------------------------===//

#include "CXXABI.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/MangleNumberingContext.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/iterator.h"

using namespace clang;

namespace {

/// According to Itanium C++ ABI 5.1.2:
/// the name of an anonymous union is considered to be
/// the name of the first named data member found by a pre-order,
/// depth-first, declaration-order walk of the data members of
/// the anonymous union.
/// If there is no such data member (i.e., if all of the data members
/// in the union are unnamed), then there is no way for a program to
/// refer to the anonymous union, and there is therefore no need to mangle its name.
///
/// Returns the name of anonymous union VarDecl or nullptr if it is not found.
static const IdentifierInfo *findAnonymousUnionVarDeclName(const VarDecl& VD) {
  const RecordType *RT = VD.getType()->getAs<RecordType>();
  assert(RT && "type of VarDecl is expected to be RecordType.");
  assert(RT->getDecl()->isUnion() && "RecordType is expected to be a union.");
  if (const FieldDecl *FD = RT->getDecl()->findFirstNamedDataMember()) {
    return FD->getIdentifier();
  }

  return nullptr;
}

/// The name of a decomposition declaration.
struct DecompositionDeclName {
  using BindingArray = ArrayRef<const BindingDecl*>;

  /// Representative example of a set of bindings with these names.
  BindingArray Bindings;

  /// Iterators over the sequence of identifiers in the name.
  struct Iterator
      : llvm::iterator_adaptor_base<Iterator, BindingArray::const_iterator,
                                    std::random_access_iterator_tag,
                                    const IdentifierInfo *> {
    Iterator(BindingArray::const_iterator It) : iterator_adaptor_base(It) {}
    const IdentifierInfo *operator*() const {
      return (*this->I)->getIdentifier();
    }
  };
  Iterator begin() const { return Iterator(Bindings.begin()); }
  Iterator end() const { return Iterator(Bindings.end()); }
};
}

namespace llvm {
template<>
struct DenseMapInfo<DecompositionDeclName> {
  using ArrayInfo = llvm::DenseMapInfo<ArrayRef<const BindingDecl*>>;
  using IdentInfo = llvm::DenseMapInfo<const IdentifierInfo*>;
  static DecompositionDeclName getEmptyKey() {
    return {ArrayInfo::getEmptyKey()};
  }
  static DecompositionDeclName getTombstoneKey() {
    return {ArrayInfo::getTombstoneKey()};
  }
  static unsigned getHashValue(DecompositionDeclName Key) {
    assert(!isEqual(Key, getEmptyKey()) && !isEqual(Key, getTombstoneKey()));
    return llvm::hash_combine_range(Key.begin(), Key.end());
  }
  static bool isEqual(DecompositionDeclName LHS, DecompositionDeclName RHS) {
    if (ArrayInfo::isEqual(LHS.Bindings, ArrayInfo::getEmptyKey()))
      return ArrayInfo::isEqual(RHS.Bindings, ArrayInfo::getEmptyKey());
    if (ArrayInfo::isEqual(LHS.Bindings, ArrayInfo::getTombstoneKey()))
      return ArrayInfo::isEqual(RHS.Bindings, ArrayInfo::getTombstoneKey());
    return LHS.Bindings.size() == RHS.Bindings.size() &&
           std::equal(LHS.begin(), LHS.end(), RHS.begin());
  }
};
}

namespace {

/// Keeps track of the mangled names of lambda expressions and block
/// literals within a particular context.
class ItaniumNumberingContext : public MangleNumberingContext {
  llvm::DenseMap<const Type *, unsigned> ManglingNumbers;
  llvm::DenseMap<const IdentifierInfo *, unsigned> VarManglingNumbers;
  llvm::DenseMap<const IdentifierInfo *, unsigned> TagManglingNumbers;
  llvm::DenseMap<DecompositionDeclName, unsigned>
      DecompsitionDeclManglingNumbers;

public:
  unsigned getManglingNumber(const CXXMethodDecl *CallOperator) override {
    const FunctionProtoType *Proto =
        CallOperator->getType()->getAs<FunctionProtoType>();
    ASTContext &Context = CallOperator->getASTContext();

    FunctionProtoType::ExtProtoInfo EPI;
    EPI.Variadic = Proto->isVariadic();
    QualType Key =
        Context.getFunctionType(Context.VoidTy, Proto->getParamTypes(), EPI);
    Key = Context.getCanonicalType(Key);
    return ++ManglingNumbers[Key->castAs<FunctionProtoType>()];
  }

  unsigned getManglingNumber(const BlockDecl *BD) override {
    const Type *Ty = nullptr;
    return ++ManglingNumbers[Ty];
  }

  unsigned getStaticLocalNumber(const VarDecl *VD) override {
    return 0;
  }

  /// Variable decls are numbered by identifier.
  unsigned getManglingNumber(const VarDecl *VD, unsigned) override {
    if (auto *DD = dyn_cast<DecompositionDecl>(VD)) {
      DecompositionDeclName Name{DD->bindings()};
      return ++DecompsitionDeclManglingNumbers[Name];
    }

    const IdentifierInfo *Identifier = VD->getIdentifier();
    if (!Identifier) {
      // VarDecl without an identifier represents an anonymous union
      // declaration.
      Identifier = findAnonymousUnionVarDeclName(*VD);
    }
    return ++VarManglingNumbers[Identifier];
  }

  unsigned getManglingNumber(const TagDecl *TD, unsigned) override {
    return ++TagManglingNumbers[TD->getIdentifier()];
  }
};

class ItaniumCXXABI : public CXXABI {
protected:
  ASTContext &Context;
public:
  ItaniumCXXABI(ASTContext &Ctx) : Context(Ctx) { }

  MemberPointerInfo
  getMemberPointerInfo(const MemberPointerType *MPT) const override {
    const TargetInfo &Target = Context.getTargetInfo();
    TargetInfo::IntType PtrDiff = Target.getPtrDiffType(0);
    MemberPointerInfo MPI;
    MPI.Width = Target.getTypeWidth(PtrDiff);
    MPI.Align = Target.getTypeAlign(PtrDiff);
    MPI.HasPadding = false;
    if (MPT->isMemberFunctionPointer())
      MPI.Width *= 2;
    return MPI;
  }

  CallingConv getDefaultMethodCallConv(bool isVariadic) const override {
    const llvm::Triple &T = Context.getTargetInfo().getTriple();
    if (!isVariadic && T.isWindowsGNUEnvironment() &&
        T.getArch() == llvm::Triple::x86)
      return CC_X86ThisCall;
    return CC_C;
  }

  // We cheat and just check that the class has a vtable pointer, and that it's
  // only big enough to have a vtable pointer and nothing more (or less).
  bool isNearlyEmpty(const CXXRecordDecl *RD) const override {

    // Check that the class has a vtable pointer.
    if (!RD->isDynamicClass())
      return false;

    const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);
    CharUnits PointerSize =
      Context.toCharUnitsFromBits(Context.getTargetInfo().getPointerWidth(0));
    return Layout.getNonVirtualSize() == PointerSize;
  }

  const CXXConstructorDecl *
  getCopyConstructorForExceptionObject(CXXRecordDecl *RD) override {
    return nullptr;
  }

  void addCopyConstructorForExceptionObject(CXXRecordDecl *RD,
                                            CXXConstructorDecl *CD) override {}

  void addTypedefNameForUnnamedTagDecl(TagDecl *TD,
                                       TypedefNameDecl *DD) override {}

  TypedefNameDecl *getTypedefNameForUnnamedTagDecl(const TagDecl *TD) override {
    return nullptr;
  }

  void addDeclaratorForUnnamedTagDecl(TagDecl *TD,
                                      DeclaratorDecl *DD) override {}

  DeclaratorDecl *getDeclaratorForUnnamedTagDecl(const TagDecl *TD) override {
    return nullptr;
  }

  std::unique_ptr<MangleNumberingContext>
  createMangleNumberingContext() const override {
    return llvm::make_unique<ItaniumNumberingContext>();
  }
};
}

CXXABI *clang::CreateItaniumCXXABI(ASTContext &Ctx) {
  return new ItaniumCXXABI(Ctx);
}
