//===--- SanitizerMetadata.cpp - Blacklist for sanitizers -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Class which emits metadata consumed by sanitizer instrumentation passes.
//
//===----------------------------------------------------------------------===//
#include "SanitizerMetadata.h"
#include "CodeGenModule.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"

using namespace clang;
using namespace CodeGen;

SanitizerMetadata::SanitizerMetadata(CodeGenModule &CGM) : CGM(CGM) {}

void SanitizerMetadata::reportGlobalToASan(llvm::GlobalVariable *GV,
                                           SourceLocation Loc, StringRef Name,
                                           QualType Ty, bool IsDynInit,
                                           bool IsBlacklisted) {
  if (!CGM.getLangOpts().Sanitize.hasOneOf(SanitizerKind::Address |
                                           SanitizerKind::KernelAddress |
                                           SanitizerKind::HWAddress |
                                           SanitizerKind::KernelHWAddress))
    return;
  IsDynInit &= !CGM.isInSanitizerBlacklist(GV, Loc, Ty, "init");
  IsBlacklisted |= CGM.isInSanitizerBlacklist(GV, Loc, Ty);

  llvm::Metadata *LocDescr = nullptr;
  llvm::Metadata *GlobalName = nullptr;
  llvm::LLVMContext &VMContext = CGM.getLLVMContext();
  if (!IsBlacklisted) {
    // Don't generate source location and global name if it is blacklisted -
    // it won't be instrumented anyway.
    LocDescr = getLocationMetadata(Loc);
    if (!Name.empty())
      GlobalName = llvm::MDString::get(VMContext, Name);
  }

  llvm::Metadata *GlobalMetadata[] = {
      llvm::ConstantAsMetadata::get(GV), LocDescr, GlobalName,
      llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(llvm::Type::getInt1Ty(VMContext), IsDynInit)),
      llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
          llvm::Type::getInt1Ty(VMContext), IsBlacklisted))};

  llvm::MDNode *ThisGlobal = llvm::MDNode::get(VMContext, GlobalMetadata);
  llvm::NamedMDNode *AsanGlobals =
      CGM.getModule().getOrInsertNamedMetadata("llvm.asan.globals");
  AsanGlobals->addOperand(ThisGlobal);
}

void SanitizerMetadata::reportGlobalToASan(llvm::GlobalVariable *GV,
                                           const VarDecl &D, bool IsDynInit) {
  if (!CGM.getLangOpts().Sanitize.hasOneOf(SanitizerKind::Address |
                                           SanitizerKind::KernelAddress |
                                           SanitizerKind::HWAddress |
                                           SanitizerKind::KernelHWAddress))
    return;
  std::string QualName;
  llvm::raw_string_ostream OS(QualName);
  D.printQualifiedName(OS);

  bool IsBlacklisted = false;
  for (auto Attr : D.specific_attrs<NoSanitizeAttr>())
    if (Attr->getMask() & SanitizerKind::Address)
      IsBlacklisted = true;
  reportGlobalToASan(GV, D.getLocation(), OS.str(), D.getType(), IsDynInit,
                     IsBlacklisted);
}

void SanitizerMetadata::disableSanitizerForGlobal(llvm::GlobalVariable *GV) {
  // For now, just make sure the global is not modified by the ASan
  // instrumentation.
  if (CGM.getLangOpts().Sanitize.hasOneOf(SanitizerKind::Address |
                                          SanitizerKind::KernelAddress |
                                          SanitizerKind::HWAddress |
                                          SanitizerKind::KernelHWAddress))
    reportGlobalToASan(GV, SourceLocation(), "", QualType(), false, true);
}

void SanitizerMetadata::disableSanitizerForInstruction(llvm::Instruction *I) {
  I->setMetadata(CGM.getModule().getMDKindID("nosanitize"),
                 llvm::MDNode::get(CGM.getLLVMContext(), None));
}

llvm::MDNode *SanitizerMetadata::getLocationMetadata(SourceLocation Loc) {
  PresumedLoc PLoc = CGM.getContext().getSourceManager().getPresumedLoc(Loc);
  if (!PLoc.isValid())
    return nullptr;
  llvm::LLVMContext &VMContext = CGM.getLLVMContext();
  llvm::Metadata *LocMetadata[] = {
      llvm::MDString::get(VMContext, PLoc.getFilename()),
      llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
          llvm::Type::getInt32Ty(VMContext), PLoc.getLine())),
      llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
          llvm::Type::getInt32Ty(VMContext), PLoc.getColumn())),
  };
  return llvm::MDNode::get(VMContext, LocMetadata);
}
