//===--- CGCXX.cpp - Emit LLVM Code for declarations ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with C++ code generation.
//
//===----------------------------------------------------------------------===//

// We might split this into multiple files if it gets too unwieldy

#include "CodeGenModule.h"
#include "CGCXXABI.h"
#include "CodeGenFunction.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Basic/CodeGenOptions.h"
#include "llvm/ADT/StringExtras.h"
using namespace clang;
using namespace CodeGen;


/// Try to emit a base destructor as an alias to its primary
/// base-class destructor.
bool CodeGenModule::TryEmitBaseDestructorAsAlias(const CXXDestructorDecl *D) {
  if (!getCodeGenOpts().CXXCtorDtorAliases)
    return true;

  // Producing an alias to a base class ctor/dtor can degrade debug quality
  // as the debugger cannot tell them apart.
  if (getCodeGenOpts().OptimizationLevel == 0)
    return true;

  // If sanitizing memory to check for use-after-dtor, do not emit as
  //  an alias, unless this class owns no members.
  if (getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
      !D->getParent()->field_empty())
    return true;

  // If the destructor doesn't have a trivial body, we have to emit it
  // separately.
  if (!D->hasTrivialBody())
    return true;

  const CXXRecordDecl *Class = D->getParent();

  // We are going to instrument this destructor, so give up even if it is
  // currently empty.
  if (Class->mayInsertExtraPadding())
    return true;

  // If we need to manipulate a VTT parameter, give up.
  if (Class->getNumVBases()) {
    // Extra Credit:  passing extra parameters is perfectly safe
    // in many calling conventions, so only bail out if the ctor's
    // calling convention is nonstandard.
    return true;
  }

  // If any field has a non-trivial destructor, we have to emit the
  // destructor separately.
  for (const auto *I : Class->fields())
    if (I->getType().isDestructedType())
      return true;

  // Try to find a unique base class with a non-trivial destructor.
  const CXXRecordDecl *UniqueBase = nullptr;
  for (const auto &I : Class->bases()) {

    // We're in the base destructor, so skip virtual bases.
    if (I.isVirtual()) continue;

    // Skip base classes with trivial destructors.
    const auto *Base =
        cast<CXXRecordDecl>(I.getType()->getAs<RecordType>()->getDecl());
    if (Base->hasTrivialDestructor()) continue;

    // If we've already found a base class with a non-trivial
    // destructor, give up.
    if (UniqueBase) return true;
    UniqueBase = Base;
  }

  // If we didn't find any bases with a non-trivial destructor, then
  // the base destructor is actually effectively trivial, which can
  // happen if it was needlessly user-defined or if there are virtual
  // bases with non-trivial destructors.
  if (!UniqueBase)
    return true;

  // If the base is at a non-zero offset, give up.
  const ASTRecordLayout &ClassLayout = Context.getASTRecordLayout(Class);
  if (!ClassLayout.getBaseClassOffset(UniqueBase).isZero())
    return true;

  // Give up if the calling conventions don't match. We could update the call,
  // but it is probably not worth it.
  const CXXDestructorDecl *BaseD = UniqueBase->getDestructor();
  if (BaseD->getType()->getAs<FunctionType>()->getCallConv() !=
      D->getType()->getAs<FunctionType>()->getCallConv())
    return true;

  GlobalDecl AliasDecl(D, Dtor_Base);
  GlobalDecl TargetDecl(BaseD, Dtor_Base);

  // The alias will use the linkage of the referent.  If we can't
  // support aliases with that linkage, fail.
  llvm::GlobalValue::LinkageTypes Linkage = getFunctionLinkage(AliasDecl);

  // We can't use an alias if the linkage is not valid for one.
  if (!llvm::GlobalAlias::isValidLinkage(Linkage))
    return true;

  llvm::GlobalValue::LinkageTypes TargetLinkage =
      getFunctionLinkage(TargetDecl);

  // Check if we have it already.
  StringRef MangledName = getMangledName(AliasDecl);
  llvm::GlobalValue *Entry = GetGlobalValue(MangledName);
  if (Entry && !Entry->isDeclaration())
    return false;
  if (Replacements.count(MangledName))
    return false;

  // Derive the type for the alias.
  llvm::Type *AliasValueType = getTypes().GetFunctionType(AliasDecl);
  llvm::PointerType *AliasType = AliasValueType->getPointerTo();

  // Find the referent.  Some aliases might require a bitcast, in
  // which case the caller is responsible for ensuring the soundness
  // of these semantics.
  auto *Ref = cast<llvm::GlobalValue>(GetAddrOfGlobal(TargetDecl));
  llvm::Constant *Aliasee = Ref;
  if (Ref->getType() != AliasType)
    Aliasee = llvm::ConstantExpr::getBitCast(Ref, AliasType);

  // Instead of creating as alias to a linkonce_odr, replace all of the uses
  // of the aliasee.
  if (llvm::GlobalValue::isDiscardableIfUnused(Linkage) &&
      !(TargetLinkage == llvm::GlobalValue::AvailableExternallyLinkage &&
        TargetDecl.getDecl()->hasAttr<AlwaysInlineAttr>())) {
    // FIXME: An extern template instantiation will create functions with
    // linkage "AvailableExternally". In libc++, some classes also define
    // members with attribute "AlwaysInline" and expect no reference to
    // be generated. It is desirable to reenable this optimisation after
    // corresponding LLVM changes.
    addReplacement(MangledName, Aliasee);
    return false;
  }

  // If we have a weak, non-discardable alias (weak, weak_odr), like an extern
  // template instantiation or a dllexported class, avoid forming it on COFF.
  // A COFF weak external alias cannot satisfy a normal undefined symbol
  // reference from another TU. The other TU must also mark the referenced
  // symbol as weak, which we cannot rely on.
  if (llvm::GlobalValue::isWeakForLinker(Linkage) &&
      getTriple().isOSBinFormatCOFF()) {
    return true;
  }

  // If we don't have a definition for the destructor yet or the definition is
  // avaialable_externally, don't emit an alias.  We can't emit aliases to
  // declarations; that's just not how aliases work.
  if (Ref->isDeclarationForLinker())
    return true;

  // Don't create an alias to a linker weak symbol. This avoids producing
  // different COMDATs in different TUs. Another option would be to
  // output the alias both for weak_odr and linkonce_odr, but that
  // requires explicit comdat support in the IL.
  if (llvm::GlobalValue::isWeakForLinker(TargetLinkage))
    return true;

  // Create the alias with no name.
  auto *Alias = llvm::GlobalAlias::create(AliasValueType, 0, Linkage, "",
                                          Aliasee, &getModule());

  // Destructors are always unnamed_addr.
  Alias->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  // Switch any previous uses to the alias.
  if (Entry) {
    assert(Entry->getType() == AliasType &&
           "declaration exists with different type");
    Alias->takeName(Entry);
    Entry->replaceAllUsesWith(Alias);
    Entry->eraseFromParent();
  } else {
    Alias->setName(MangledName);
  }

  // Finally, set up the alias with its proper name and attributes.
  SetCommonAttributes(AliasDecl, Alias);

  return false;
}

llvm::Function *CodeGenModule::codegenCXXStructor(const CXXMethodDecl *MD,
                                                  StructorType Type) {
  const CGFunctionInfo &FnInfo =
      getTypes().arrangeCXXStructorDeclaration(MD, Type);
  auto *Fn = cast<llvm::Function>(
      getAddrOfCXXStructor(MD, Type, &FnInfo, /*FnType=*/nullptr,
                           /*DontDefer=*/true, ForDefinition));

  GlobalDecl GD;
  if (const auto *DD = dyn_cast<CXXDestructorDecl>(MD)) {
    GD = GlobalDecl(DD, toCXXDtorType(Type));
  } else {
    const auto *CD = cast<CXXConstructorDecl>(MD);
    GD = GlobalDecl(CD, toCXXCtorType(Type));
  }

  setFunctionLinkage(GD, Fn);

  CodeGenFunction(*this).GenerateCode(GD, Fn, FnInfo);
  setNonAliasAttributes(GD, Fn);
  SetLLVMFunctionAttributesForDefinition(MD, Fn);
  return Fn;
}

llvm::Constant *CodeGenModule::getAddrOfCXXStructor(
    const CXXMethodDecl *MD, StructorType Type, const CGFunctionInfo *FnInfo,
    llvm::FunctionType *FnType, bool DontDefer,
    ForDefinition_t IsForDefinition) {
  GlobalDecl GD;
  if (auto *CD = dyn_cast<CXXConstructorDecl>(MD)) {
    GD = GlobalDecl(CD, toCXXCtorType(Type));
  } else {
    // Always alias equivalent complete destructors to base destructors in the
    // MS ABI.
    if (getTarget().getCXXABI().isMicrosoft() &&
        Type == StructorType::Complete && MD->getParent()->getNumVBases() == 0)
      Type = StructorType::Base;
    GD = GlobalDecl(cast<CXXDestructorDecl>(MD), toCXXDtorType(Type));
  }

  if (!FnType) {
    if (!FnInfo)
      FnInfo = &getTypes().arrangeCXXStructorDeclaration(MD, Type);
    FnType = getTypes().GetFunctionType(*FnInfo);
  }

  return GetOrCreateLLVMFunction(
      getMangledName(GD), FnType, GD, /*ForVTable=*/false, DontDefer,
      /*isThunk=*/false, /*ExtraAttrs=*/llvm::AttributeList(), IsForDefinition);
}

static CGCallee BuildAppleKextVirtualCall(CodeGenFunction &CGF,
                                          GlobalDecl GD,
                                          llvm::Type *Ty,
                                          const CXXRecordDecl *RD) {
  assert(!CGF.CGM.getTarget().getCXXABI().isMicrosoft() &&
         "No kext in Microsoft ABI");
  CodeGenModule &CGM = CGF.CGM;
  llvm::Value *VTable = CGM.getCXXABI().getAddrOfVTable(RD, CharUnits());
  Ty = Ty->getPointerTo()->getPointerTo();
  VTable = CGF.Builder.CreateBitCast(VTable, Ty);
  assert(VTable && "BuildVirtualCall = kext vtbl pointer is null");
  uint64_t VTableIndex = CGM.getItaniumVTableContext().getMethodVTableIndex(GD);
  const VTableLayout &VTLayout = CGM.getItaniumVTableContext().getVTableLayout(RD);
  VTableLayout::AddressPointLocation AddressPoint =
      VTLayout.getAddressPoint(BaseSubobject(RD, CharUnits::Zero()));
  VTableIndex += VTLayout.getVTableOffset(AddressPoint.VTableIndex) +
                 AddressPoint.AddressPointIndex;
  llvm::Value *VFuncPtr =
    CGF.Builder.CreateConstInBoundsGEP1_64(VTable, VTableIndex, "vfnkxt");
  llvm::Value *VFunc =
    CGF.Builder.CreateAlignedLoad(VFuncPtr, CGF.PointerAlignInBytes);
  CGCallee Callee(GD, VFunc);
  return Callee;
}

/// BuildAppleKextVirtualCall - This routine is to support gcc's kext ABI making
/// indirect call to virtual functions. It makes the call through indexing
/// into the vtable.
CGCallee
CodeGenFunction::BuildAppleKextVirtualCall(const CXXMethodDecl *MD,
                                           NestedNameSpecifier *Qual,
                                           llvm::Type *Ty) {
  assert((Qual->getKind() == NestedNameSpecifier::TypeSpec) &&
         "BuildAppleKextVirtualCall - bad Qual kind");

  const Type *QTy = Qual->getAsType();
  QualType T = QualType(QTy, 0);
  const RecordType *RT = T->getAs<RecordType>();
  assert(RT && "BuildAppleKextVirtualCall - Qual type must be record");
  const auto *RD = cast<CXXRecordDecl>(RT->getDecl());

  if (const auto *DD = dyn_cast<CXXDestructorDecl>(MD))
    return BuildAppleKextVirtualDestructorCall(DD, Dtor_Complete, RD);

  return ::BuildAppleKextVirtualCall(*this, MD, Ty, RD);
}

/// BuildVirtualCall - This routine makes indirect vtable call for
/// call to virtual destructors. It returns 0 if it could not do it.
CGCallee
CodeGenFunction::BuildAppleKextVirtualDestructorCall(
                                            const CXXDestructorDecl *DD,
                                            CXXDtorType Type,
                                            const CXXRecordDecl *RD) {
  assert(DD->isVirtual() && Type != Dtor_Base);
  // Compute the function type we're calling.
  const CGFunctionInfo &FInfo = CGM.getTypes().arrangeCXXStructorDeclaration(
      DD, StructorType::Complete);
  llvm::Type *Ty = CGM.getTypes().GetFunctionType(FInfo);
  return ::BuildAppleKextVirtualCall(*this, GlobalDecl(DD, Type), Ty, RD);
}
