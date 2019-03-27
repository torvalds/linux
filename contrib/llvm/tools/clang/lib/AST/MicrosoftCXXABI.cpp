//===------- MicrosoftCXXABI.cpp - AST support for the Microsoft C++ ABI --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides C++ AST support targeting the Microsoft Visual C++
// ABI.
//
//===----------------------------------------------------------------------===//

#include "CXXABI.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/MangleNumberingContext.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/Basic/TargetInfo.h"

using namespace clang;

namespace {

/// Numbers things which need to correspond across multiple TUs.
/// Typically these are things like static locals, lambdas, or blocks.
class MicrosoftNumberingContext : public MangleNumberingContext {
  llvm::DenseMap<const Type *, unsigned> ManglingNumbers;
  unsigned LambdaManglingNumber;
  unsigned StaticLocalNumber;
  unsigned StaticThreadlocalNumber;

public:
  MicrosoftNumberingContext()
      : MangleNumberingContext(), LambdaManglingNumber(0),
        StaticLocalNumber(0), StaticThreadlocalNumber(0) {}

  unsigned getManglingNumber(const CXXMethodDecl *CallOperator) override {
    return ++LambdaManglingNumber;
  }

  unsigned getManglingNumber(const BlockDecl *BD) override {
    const Type *Ty = nullptr;
    return ++ManglingNumbers[Ty];
  }

  unsigned getStaticLocalNumber(const VarDecl *VD) override {
    if (VD->getTLSKind())
      return ++StaticThreadlocalNumber;
    return ++StaticLocalNumber;
  }

  unsigned getManglingNumber(const VarDecl *VD,
                             unsigned MSLocalManglingNumber) override {
    return MSLocalManglingNumber;
  }

  unsigned getManglingNumber(const TagDecl *TD,
                             unsigned MSLocalManglingNumber) override {
    return MSLocalManglingNumber;
  }
};

class MicrosoftCXXABI : public CXXABI {
  ASTContext &Context;
  llvm::SmallDenseMap<CXXRecordDecl *, CXXConstructorDecl *> RecordToCopyCtor;

  llvm::SmallDenseMap<TagDecl *, DeclaratorDecl *>
      UnnamedTagDeclToDeclaratorDecl;
  llvm::SmallDenseMap<TagDecl *, TypedefNameDecl *>
      UnnamedTagDeclToTypedefNameDecl;

public:
  MicrosoftCXXABI(ASTContext &Ctx) : Context(Ctx) { }

  MemberPointerInfo
  getMemberPointerInfo(const MemberPointerType *MPT) const override;

  CallingConv getDefaultMethodCallConv(bool isVariadic) const override {
    if (!isVariadic &&
        Context.getTargetInfo().getTriple().getArch() == llvm::Triple::x86)
      return CC_X86ThisCall;
    return CC_C;
  }

  bool isNearlyEmpty(const CXXRecordDecl *RD) const override {
    llvm_unreachable("unapplicable to the MS ABI");
  }

  const CXXConstructorDecl *
  getCopyConstructorForExceptionObject(CXXRecordDecl *RD) override {
    return RecordToCopyCtor[RD];
  }

  void
  addCopyConstructorForExceptionObject(CXXRecordDecl *RD,
                                       CXXConstructorDecl *CD) override {
    assert(CD != nullptr);
    assert(RecordToCopyCtor[RD] == nullptr || RecordToCopyCtor[RD] == CD);
    RecordToCopyCtor[RD] = CD;
  }

  void addTypedefNameForUnnamedTagDecl(TagDecl *TD,
                                       TypedefNameDecl *DD) override {
    TD = TD->getCanonicalDecl();
    DD = DD->getCanonicalDecl();
    TypedefNameDecl *&I = UnnamedTagDeclToTypedefNameDecl[TD];
    if (!I)
      I = DD;
  }

  TypedefNameDecl *getTypedefNameForUnnamedTagDecl(const TagDecl *TD) override {
    return UnnamedTagDeclToTypedefNameDecl.lookup(
        const_cast<TagDecl *>(TD->getCanonicalDecl()));
  }

  void addDeclaratorForUnnamedTagDecl(TagDecl *TD,
                                      DeclaratorDecl *DD) override {
    TD = TD->getCanonicalDecl();
    DD = cast<DeclaratorDecl>(DD->getCanonicalDecl());
    DeclaratorDecl *&I = UnnamedTagDeclToDeclaratorDecl[TD];
    if (!I)
      I = DD;
  }

  DeclaratorDecl *getDeclaratorForUnnamedTagDecl(const TagDecl *TD) override {
    return UnnamedTagDeclToDeclaratorDecl.lookup(
        const_cast<TagDecl *>(TD->getCanonicalDecl()));
  }

  std::unique_ptr<MangleNumberingContext>
  createMangleNumberingContext() const override {
    return llvm::make_unique<MicrosoftNumberingContext>();
  }
};
}

// getNumBases() seems to only give us the number of direct bases, and not the
// total.  This function tells us if we inherit from anybody that uses MI, or if
// we have a non-primary base class, which uses the multiple inheritance model.
static bool usesMultipleInheritanceModel(const CXXRecordDecl *RD) {
  while (RD->getNumBases() > 0) {
    if (RD->getNumBases() > 1)
      return true;
    assert(RD->getNumBases() == 1);
    const CXXRecordDecl *Base =
        RD->bases_begin()->getType()->getAsCXXRecordDecl();
    if (RD->isPolymorphic() && !Base->isPolymorphic())
      return true;
    RD = Base;
  }
  return false;
}

MSInheritanceAttr::Spelling CXXRecordDecl::calculateInheritanceModel() const {
  if (!hasDefinition() || isParsingBaseSpecifiers())
    return MSInheritanceAttr::Keyword_unspecified_inheritance;
  if (getNumVBases() > 0)
    return MSInheritanceAttr::Keyword_virtual_inheritance;
  if (usesMultipleInheritanceModel(this))
    return MSInheritanceAttr::Keyword_multiple_inheritance;
  return MSInheritanceAttr::Keyword_single_inheritance;
}

MSInheritanceAttr::Spelling
CXXRecordDecl::getMSInheritanceModel() const {
  MSInheritanceAttr *IA = getAttr<MSInheritanceAttr>();
  assert(IA && "Expected MSInheritanceAttr on the CXXRecordDecl!");
  return IA->getSemanticSpelling();
}

MSVtorDispAttr::Mode CXXRecordDecl::getMSVtorDispMode() const {
  if (MSVtorDispAttr *VDA = getAttr<MSVtorDispAttr>())
    return VDA->getVtorDispMode();
  return MSVtorDispAttr::Mode(getASTContext().getLangOpts().VtorDispMode);
}

// Returns the number of pointer and integer slots used to represent a member
// pointer in the MS C++ ABI.
//
// Member function pointers have the following general form;  however, fields
// are dropped as permitted (under the MSVC interpretation) by the inheritance
// model of the actual class.
//
//   struct {
//     // A pointer to the member function to call.  If the member function is
//     // virtual, this will be a thunk that forwards to the appropriate vftable
//     // slot.
//     void *FunctionPointerOrVirtualThunk;
//
//     // An offset to add to the address of the vbtable pointer after
//     // (possibly) selecting the virtual base but before resolving and calling
//     // the function.
//     // Only needed if the class has any virtual bases or bases at a non-zero
//     // offset.
//     int NonVirtualBaseAdjustment;
//
//     // The offset of the vb-table pointer within the object.  Only needed for
//     // incomplete types.
//     int VBPtrOffset;
//
//     // An offset within the vb-table that selects the virtual base containing
//     // the member.  Loading from this offset produces a new offset that is
//     // added to the address of the vb-table pointer to produce the base.
//     int VirtualBaseAdjustmentOffset;
//   };
static std::pair<unsigned, unsigned>
getMSMemberPointerSlots(const MemberPointerType *MPT) {
  const CXXRecordDecl *RD = MPT->getMostRecentCXXRecordDecl();
  MSInheritanceAttr::Spelling Inheritance = RD->getMSInheritanceModel();
  unsigned Ptrs = 0;
  unsigned Ints = 0;
  if (MPT->isMemberFunctionPointer())
    Ptrs = 1;
  else
    Ints = 1;
  if (MSInheritanceAttr::hasNVOffsetField(MPT->isMemberFunctionPointer(),
                                          Inheritance))
    Ints++;
  if (MSInheritanceAttr::hasVBPtrOffsetField(Inheritance))
    Ints++;
  if (MSInheritanceAttr::hasVBTableOffsetField(Inheritance))
    Ints++;
  return std::make_pair(Ptrs, Ints);
}

CXXABI::MemberPointerInfo MicrosoftCXXABI::getMemberPointerInfo(
    const MemberPointerType *MPT) const {
  // The nominal struct is laid out with pointers followed by ints and aligned
  // to a pointer width if any are present and an int width otherwise.
  const TargetInfo &Target = Context.getTargetInfo();
  unsigned PtrSize = Target.getPointerWidth(0);
  unsigned IntSize = Target.getIntWidth();

  unsigned Ptrs, Ints;
  std::tie(Ptrs, Ints) = getMSMemberPointerSlots(MPT);
  MemberPointerInfo MPI;
  MPI.HasPadding = false;
  MPI.Width = Ptrs * PtrSize + Ints * IntSize;

  // When MSVC does x86_32 record layout, it aligns aggregate member pointers to
  // 8 bytes.  However, __alignof usually returns 4 for data memptrs and 8 for
  // function memptrs.
  if (Ptrs + Ints > 1 && Target.getTriple().isArch32Bit())
    MPI.Align = 64;
  else if (Ptrs)
    MPI.Align = Target.getPointerAlign(0);
  else
    MPI.Align = Target.getIntAlign();

  if (Target.getTriple().isArch64Bit()) {
    MPI.Width = llvm::alignTo(MPI.Width, MPI.Align);
    MPI.HasPadding = MPI.Width != (Ptrs * PtrSize + Ints * IntSize);
  }
  return MPI;
}

CXXABI *clang::CreateMicrosoftCXXABI(ASTContext &Ctx) {
  return new MicrosoftCXXABI(Ctx);
}

