//===----- CGCXXABI.cpp - Interface to C++ ABIs ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides an abstract class for C++ code generation. Concrete subclasses
// of this implement code generation for specific C++ ABIs.
//
//===----------------------------------------------------------------------===//

#include "CGCXXABI.h"
#include "CGCleanup.h"

using namespace clang;
using namespace CodeGen;

CGCXXABI::~CGCXXABI() { }

void CGCXXABI::ErrorUnsupportedABI(CodeGenFunction &CGF, StringRef S) {
  DiagnosticsEngine &Diags = CGF.CGM.getDiags();
  unsigned DiagID = Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                          "cannot yet compile %0 in this ABI");
  Diags.Report(CGF.getContext().getFullLoc(CGF.CurCodeDecl->getLocation()),
               DiagID)
    << S;
}

bool CGCXXABI::canCopyArgument(const CXXRecordDecl *RD) const {
  // We can only copy the argument if there exists at least one trivial,
  // non-deleted copy or move constructor.
  return RD->canPassInRegisters();
}

llvm::Constant *CGCXXABI::GetBogusMemberPointer(QualType T) {
  return llvm::Constant::getNullValue(CGM.getTypes().ConvertType(T));
}

llvm::Type *
CGCXXABI::ConvertMemberPointerType(const MemberPointerType *MPT) {
  return CGM.getTypes().ConvertType(CGM.getContext().getPointerDiffType());
}

CGCallee CGCXXABI::EmitLoadOfMemberFunctionPointer(
    CodeGenFunction &CGF, const Expr *E, Address This,
    llvm::Value *&ThisPtrForCall,
    llvm::Value *MemPtr, const MemberPointerType *MPT) {
  ErrorUnsupportedABI(CGF, "calls through member pointers");

  ThisPtrForCall = This.getPointer();
  const FunctionProtoType *FPT =
    MPT->getPointeeType()->getAs<FunctionProtoType>();
  const CXXRecordDecl *RD =
    cast<CXXRecordDecl>(MPT->getClass()->getAs<RecordType>()->getDecl());
  llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(
      CGM.getTypes().arrangeCXXMethodType(RD, FPT, /*FD=*/nullptr));
  llvm::Constant *FnPtr = llvm::Constant::getNullValue(FTy->getPointerTo());
  return CGCallee::forDirect(FnPtr, FPT);
}

llvm::Value *
CGCXXABI::EmitMemberDataPointerAddress(CodeGenFunction &CGF, const Expr *E,
                                       Address Base, llvm::Value *MemPtr,
                                       const MemberPointerType *MPT) {
  ErrorUnsupportedABI(CGF, "loads of member pointers");
  llvm::Type *Ty = CGF.ConvertType(MPT->getPointeeType())
                         ->getPointerTo(Base.getAddressSpace());
  return llvm::Constant::getNullValue(Ty);
}

llvm::Value *CGCXXABI::EmitMemberPointerConversion(CodeGenFunction &CGF,
                                                   const CastExpr *E,
                                                   llvm::Value *Src) {
  ErrorUnsupportedABI(CGF, "member function pointer conversions");
  return GetBogusMemberPointer(E->getType());
}

llvm::Constant *CGCXXABI::EmitMemberPointerConversion(const CastExpr *E,
                                                      llvm::Constant *Src) {
  return GetBogusMemberPointer(E->getType());
}

llvm::Value *
CGCXXABI::EmitMemberPointerComparison(CodeGenFunction &CGF,
                                      llvm::Value *L,
                                      llvm::Value *R,
                                      const MemberPointerType *MPT,
                                      bool Inequality) {
  ErrorUnsupportedABI(CGF, "member function pointer comparison");
  return CGF.Builder.getFalse();
}

llvm::Value *
CGCXXABI::EmitMemberPointerIsNotNull(CodeGenFunction &CGF,
                                     llvm::Value *MemPtr,
                                     const MemberPointerType *MPT) {
  ErrorUnsupportedABI(CGF, "member function pointer null testing");
  return CGF.Builder.getFalse();
}

llvm::Constant *
CGCXXABI::EmitNullMemberPointer(const MemberPointerType *MPT) {
  return GetBogusMemberPointer(QualType(MPT, 0));
}

llvm::Constant *CGCXXABI::EmitMemberFunctionPointer(const CXXMethodDecl *MD) {
  return GetBogusMemberPointer(CGM.getContext().getMemberPointerType(
      MD->getType(), MD->getParent()->getTypeForDecl()));
}

llvm::Constant *CGCXXABI::EmitMemberDataPointer(const MemberPointerType *MPT,
                                                CharUnits offset) {
  return GetBogusMemberPointer(QualType(MPT, 0));
}

llvm::Constant *CGCXXABI::EmitMemberPointer(const APValue &MP, QualType MPT) {
  return GetBogusMemberPointer(MPT);
}

bool CGCXXABI::isZeroInitializable(const MemberPointerType *MPT) {
  // Fake answer.
  return true;
}

void CGCXXABI::buildThisParam(CodeGenFunction &CGF, FunctionArgList &params) {
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(CGF.CurGD.getDecl());

  // FIXME: I'm not entirely sure I like using a fake decl just for code
  // generation. Maybe we can come up with a better way?
  auto *ThisDecl = ImplicitParamDecl::Create(
      CGM.getContext(), nullptr, MD->getLocation(),
      &CGM.getContext().Idents.get("this"), MD->getThisType(),
      ImplicitParamDecl::CXXThis);
  params.push_back(ThisDecl);
  CGF.CXXABIThisDecl = ThisDecl;

  // Compute the presumed alignment of 'this', which basically comes
  // down to whether we know it's a complete object or not.
  auto &Layout = CGF.getContext().getASTRecordLayout(MD->getParent());
  if (MD->getParent()->getNumVBases() == 0 || // avoid vcall in common case
      MD->getParent()->hasAttr<FinalAttr>() ||
      !isThisCompleteObject(CGF.CurGD)) {
    CGF.CXXABIThisAlignment = Layout.getAlignment();
  } else {
    CGF.CXXABIThisAlignment = Layout.getNonVirtualAlignment();
  }
}

llvm::Value *CGCXXABI::loadIncomingCXXThis(CodeGenFunction &CGF) {
  return CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(getThisDecl(CGF)),
                                "this");
}

void CGCXXABI::setCXXABIThisValue(CodeGenFunction &CGF, llvm::Value *ThisPtr) {
  /// Initialize the 'this' slot.
  assert(getThisDecl(CGF) && "no 'this' variable for function");
  CGF.CXXABIThisValue = ThisPtr;
}

void CGCXXABI::EmitReturnFromThunk(CodeGenFunction &CGF,
                                   RValue RV, QualType ResultType) {
  CGF.EmitReturnOfRValue(RV, ResultType);
}

CharUnits CGCXXABI::GetArrayCookieSize(const CXXNewExpr *expr) {
  if (!requiresArrayCookie(expr))
    return CharUnits::Zero();
  return getArrayCookieSizeImpl(expr->getAllocatedType());
}

CharUnits CGCXXABI::getArrayCookieSizeImpl(QualType elementType) {
  // BOGUS
  return CharUnits::Zero();
}

Address CGCXXABI::InitializeArrayCookie(CodeGenFunction &CGF,
                                        Address NewPtr,
                                        llvm::Value *NumElements,
                                        const CXXNewExpr *expr,
                                        QualType ElementType) {
  // Should never be called.
  ErrorUnsupportedABI(CGF, "array cookie initialization");
  return Address::invalid();
}

bool CGCXXABI::requiresArrayCookie(const CXXDeleteExpr *expr,
                                   QualType elementType) {
  // If the class's usual deallocation function takes two arguments,
  // it needs a cookie.
  if (expr->doesUsualArrayDeleteWantSize())
    return true;

  return elementType.isDestructedType();
}

bool CGCXXABI::requiresArrayCookie(const CXXNewExpr *expr) {
  // If the class's usual deallocation function takes two arguments,
  // it needs a cookie.
  if (expr->doesUsualArrayDeleteWantSize())
    return true;

  return expr->getAllocatedType().isDestructedType();
}

void CGCXXABI::ReadArrayCookie(CodeGenFunction &CGF, Address ptr,
                               const CXXDeleteExpr *expr, QualType eltTy,
                               llvm::Value *&numElements,
                               llvm::Value *&allocPtr, CharUnits &cookieSize) {
  // Derive a char* in the same address space as the pointer.
  ptr = CGF.Builder.CreateElementBitCast(ptr, CGF.Int8Ty);

  // If we don't need an array cookie, bail out early.
  if (!requiresArrayCookie(expr, eltTy)) {
    allocPtr = ptr.getPointer();
    numElements = nullptr;
    cookieSize = CharUnits::Zero();
    return;
  }

  cookieSize = getArrayCookieSizeImpl(eltTy);
  Address allocAddr =
    CGF.Builder.CreateConstInBoundsByteGEP(ptr, -cookieSize);
  allocPtr = allocAddr.getPointer();
  numElements = readArrayCookieImpl(CGF, allocAddr, cookieSize);
}

llvm::Value *CGCXXABI::readArrayCookieImpl(CodeGenFunction &CGF,
                                           Address ptr,
                                           CharUnits cookieSize) {
  ErrorUnsupportedABI(CGF, "reading a new[] cookie");
  return llvm::ConstantInt::get(CGF.SizeTy, 0);
}

/// Returns the adjustment, in bytes, required for the given
/// member-pointer operation.  Returns null if no adjustment is
/// required.
llvm::Constant *CGCXXABI::getMemberPointerAdjustment(const CastExpr *E) {
  assert(E->getCastKind() == CK_DerivedToBaseMemberPointer ||
         E->getCastKind() == CK_BaseToDerivedMemberPointer);

  QualType derivedType;
  if (E->getCastKind() == CK_DerivedToBaseMemberPointer)
    derivedType = E->getSubExpr()->getType();
  else
    derivedType = E->getType();

  const CXXRecordDecl *derivedClass =
    derivedType->castAs<MemberPointerType>()->getClass()->getAsCXXRecordDecl();

  return CGM.GetNonVirtualBaseClassOffset(derivedClass,
                                          E->path_begin(),
                                          E->path_end());
}

CharUnits CGCXXABI::getMemberPointerPathAdjustment(const APValue &MP) {
  // TODO: Store base specifiers in APValue member pointer paths so we can
  // easily reuse CGM.GetNonVirtualBaseClassOffset().
  const ValueDecl *MPD = MP.getMemberPointerDecl();
  CharUnits ThisAdjustment = CharUnits::Zero();
  ArrayRef<const CXXRecordDecl*> Path = MP.getMemberPointerPath();
  bool DerivedMember = MP.isMemberPointerToDerivedMember();
  const CXXRecordDecl *RD = cast<CXXRecordDecl>(MPD->getDeclContext());
  for (unsigned I = 0, N = Path.size(); I != N; ++I) {
    const CXXRecordDecl *Base = RD;
    const CXXRecordDecl *Derived = Path[I];
    if (DerivedMember)
      std::swap(Base, Derived);
    ThisAdjustment +=
      getContext().getASTRecordLayout(Derived).getBaseClassOffset(Base);
    RD = Path[I];
  }
  if (DerivedMember)
    ThisAdjustment = -ThisAdjustment;
  return ThisAdjustment;
}

llvm::BasicBlock *
CGCXXABI::EmitCtorCompleteObjectHandler(CodeGenFunction &CGF,
                                        const CXXRecordDecl *RD) {
  if (CGM.getTarget().getCXXABI().hasConstructorVariants())
    llvm_unreachable("shouldn't be called in this ABI");

  ErrorUnsupportedABI(CGF, "complete object detection in ctor");
  return nullptr;
}

void CGCXXABI::setCXXDestructorDLLStorage(llvm::GlobalValue *GV,
                                          const CXXDestructorDecl *Dtor,
                                          CXXDtorType DT) const {
  // Assume the base C++ ABI has no special rules for destructor variants.
  CGM.setDLLImportDLLExport(GV, Dtor);
}

llvm::GlobalValue::LinkageTypes CGCXXABI::getCXXDestructorLinkage(
    GVALinkage Linkage, const CXXDestructorDecl *Dtor, CXXDtorType DT) const {
  // Delegate back to CGM by default.
  return CGM.getLLVMLinkageForDeclarator(Dtor, Linkage,
                                         /*isConstantVariable=*/false);
}

bool CGCXXABI::NeedsVTTParameter(GlobalDecl GD) {
  return false;
}

llvm::CallInst *
CGCXXABI::emitTerminateForUnexpectedException(CodeGenFunction &CGF,
                                              llvm::Value *Exn) {
  // Just call std::terminate and ignore the violating exception.
  return CGF.EmitNounwindRuntimeCall(CGF.CGM.getTerminateFn());
}

CatchTypeInfo CGCXXABI::getCatchAllTypeInfo() {
  return CatchTypeInfo{nullptr, 0};
}

std::vector<CharUnits> CGCXXABI::getVBPtrOffsets(const CXXRecordDecl *RD) {
  return std::vector<CharUnits>();
}
