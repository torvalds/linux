//===--- Mangle.h - Mangle C++ Names ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines the C++ name mangling interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_MANGLE_H
#define LLVM_CLANG_AST_MANGLE_H

#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/Basic/ABI.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Casting.h"

namespace llvm {
  class raw_ostream;
}

namespace clang {
  class ASTContext;
  class BlockDecl;
  class CXXConstructorDecl;
  class CXXDestructorDecl;
  class CXXMethodDecl;
  class FunctionDecl;
  struct MethodVFTableLocation;
  class NamedDecl;
  class ObjCMethodDecl;
  class StringLiteral;
  struct ThisAdjustment;
  struct ThunkInfo;
  class VarDecl;

/// MangleContext - Context for tracking state which persists across multiple
/// calls to the C++ name mangler.
class MangleContext {
public:
  enum ManglerKind {
    MK_Itanium,
    MK_Microsoft
  };

private:
  virtual void anchor();

  ASTContext &Context;
  DiagnosticsEngine &Diags;
  const ManglerKind Kind;

  llvm::DenseMap<const BlockDecl*, unsigned> GlobalBlockIds;
  llvm::DenseMap<const BlockDecl*, unsigned> LocalBlockIds;
  llvm::DenseMap<const TagDecl*, uint64_t> AnonStructIds;

public:
  ManglerKind getKind() const { return Kind; }

  explicit MangleContext(ASTContext &Context,
                         DiagnosticsEngine &Diags,
                         ManglerKind Kind)
      : Context(Context), Diags(Diags), Kind(Kind) {}

  virtual ~MangleContext() { }

  ASTContext &getASTContext() const { return Context; }

  DiagnosticsEngine &getDiags() const { return Diags; }

  virtual void startNewFunction() { LocalBlockIds.clear(); }

  unsigned getBlockId(const BlockDecl *BD, bool Local) {
    llvm::DenseMap<const BlockDecl *, unsigned> &BlockIds
      = Local? LocalBlockIds : GlobalBlockIds;
    std::pair<llvm::DenseMap<const BlockDecl *, unsigned>::iterator, bool>
      Result = BlockIds.insert(std::make_pair(BD, BlockIds.size()));
    return Result.first->second;
  }

  uint64_t getAnonymousStructId(const TagDecl *TD) {
    std::pair<llvm::DenseMap<const TagDecl *, uint64_t>::iterator, bool>
        Result = AnonStructIds.insert(std::make_pair(TD, AnonStructIds.size()));
    return Result.first->second;
  }

  /// @name Mangler Entry Points
  /// @{

  bool shouldMangleDeclName(const NamedDecl *D);
  virtual bool shouldMangleCXXName(const NamedDecl *D) = 0;
  virtual bool shouldMangleStringLiteral(const StringLiteral *SL) = 0;

  // FIXME: consider replacing raw_ostream & with something like SmallString &.
  void mangleName(const NamedDecl *D, raw_ostream &);
  virtual void mangleCXXName(const NamedDecl *D, raw_ostream &) = 0;
  virtual void mangleThunk(const CXXMethodDecl *MD,
                          const ThunkInfo &Thunk,
                          raw_ostream &) = 0;
  virtual void mangleCXXDtorThunk(const CXXDestructorDecl *DD, CXXDtorType Type,
                                  const ThisAdjustment &ThisAdjustment,
                                  raw_ostream &) = 0;
  virtual void mangleReferenceTemporary(const VarDecl *D,
                                        unsigned ManglingNumber,
                                        raw_ostream &) = 0;
  virtual void mangleCXXRTTI(QualType T, raw_ostream &) = 0;
  virtual void mangleCXXRTTIName(QualType T, raw_ostream &) = 0;
  virtual void mangleCXXCtor(const CXXConstructorDecl *D, CXXCtorType Type,
                             raw_ostream &) = 0;
  virtual void mangleCXXDtor(const CXXDestructorDecl *D, CXXDtorType Type,
                             raw_ostream &) = 0;
  virtual void mangleStringLiteral(const StringLiteral *SL, raw_ostream &) = 0;

  void mangleGlobalBlock(const BlockDecl *BD,
                         const NamedDecl *ID,
                         raw_ostream &Out);
  void mangleCtorBlock(const CXXConstructorDecl *CD, CXXCtorType CT,
                       const BlockDecl *BD, raw_ostream &Out);
  void mangleDtorBlock(const CXXDestructorDecl *CD, CXXDtorType DT,
                       const BlockDecl *BD, raw_ostream &Out);
  void mangleBlock(const DeclContext *DC, const BlockDecl *BD,
                   raw_ostream &Out);

  void mangleObjCMethodNameWithoutSize(const ObjCMethodDecl *MD, raw_ostream &);
  void mangleObjCMethodName(const ObjCMethodDecl *MD, raw_ostream &);

  virtual void mangleStaticGuardVariable(const VarDecl *D, raw_ostream &) = 0;

  virtual void mangleDynamicInitializer(const VarDecl *D, raw_ostream &) = 0;

  virtual void mangleDynamicAtExitDestructor(const VarDecl *D,
                                             raw_ostream &) = 0;

  virtual void mangleSEHFilterExpression(const NamedDecl *EnclosingDecl,
                                         raw_ostream &Out) = 0;

  virtual void mangleSEHFinallyBlock(const NamedDecl *EnclosingDecl,
                                     raw_ostream &Out) = 0;

  /// Generates a unique string for an externally visible type for use with TBAA
  /// or type uniquing.
  /// TODO: Extend this to internal types by generating names that are unique
  /// across translation units so it can be used with LTO.
  virtual void mangleTypeName(QualType T, raw_ostream &) = 0;

  /// @}
};

class ItaniumMangleContext : public MangleContext {
public:
  explicit ItaniumMangleContext(ASTContext &C, DiagnosticsEngine &D)
      : MangleContext(C, D, MK_Itanium) {}

  virtual void mangleCXXVTable(const CXXRecordDecl *RD, raw_ostream &) = 0;
  virtual void mangleCXXVTT(const CXXRecordDecl *RD, raw_ostream &) = 0;
  virtual void mangleCXXCtorVTable(const CXXRecordDecl *RD, int64_t Offset,
                                   const CXXRecordDecl *Type,
                                   raw_ostream &) = 0;
  virtual void mangleItaniumThreadLocalInit(const VarDecl *D,
                                            raw_ostream &) = 0;
  virtual void mangleItaniumThreadLocalWrapper(const VarDecl *D,
                                               raw_ostream &) = 0;

  virtual void mangleCXXCtorComdat(const CXXConstructorDecl *D,
                                   raw_ostream &) = 0;
  virtual void mangleCXXDtorComdat(const CXXDestructorDecl *D,
                                   raw_ostream &) = 0;

  static bool classof(const MangleContext *C) {
    return C->getKind() == MK_Itanium;
  }

  static ItaniumMangleContext *create(ASTContext &Context,
                                      DiagnosticsEngine &Diags);
};

class MicrosoftMangleContext : public MangleContext {
public:
  explicit MicrosoftMangleContext(ASTContext &C, DiagnosticsEngine &D)
      : MangleContext(C, D, MK_Microsoft) {}

  /// Mangle vftable symbols.  Only a subset of the bases along the path
  /// to the vftable are included in the name.  It's up to the caller to pick
  /// them correctly.
  virtual void mangleCXXVFTable(const CXXRecordDecl *Derived,
                                ArrayRef<const CXXRecordDecl *> BasePath,
                                raw_ostream &Out) = 0;

  /// Mangle vbtable symbols.  Only a subset of the bases along the path
  /// to the vbtable are included in the name.  It's up to the caller to pick
  /// them correctly.
  virtual void mangleCXXVBTable(const CXXRecordDecl *Derived,
                                ArrayRef<const CXXRecordDecl *> BasePath,
                                raw_ostream &Out) = 0;

  virtual void mangleThreadSafeStaticGuardVariable(const VarDecl *VD,
                                                   unsigned GuardNum,
                                                   raw_ostream &Out) = 0;

  virtual void mangleVirtualMemPtrThunk(const CXXMethodDecl *MD,
                                        const MethodVFTableLocation &ML,
                                        raw_ostream &Out) = 0;

  virtual void mangleCXXVirtualDisplacementMap(const CXXRecordDecl *SrcRD,
                                               const CXXRecordDecl *DstRD,
                                               raw_ostream &Out) = 0;

  virtual void mangleCXXThrowInfo(QualType T, bool IsConst, bool IsVolatile,
                                  bool IsUnaligned, uint32_t NumEntries,
                                  raw_ostream &Out) = 0;

  virtual void mangleCXXCatchableTypeArray(QualType T, uint32_t NumEntries,
                                           raw_ostream &Out) = 0;

  virtual void mangleCXXCatchableType(QualType T, const CXXConstructorDecl *CD,
                                      CXXCtorType CT, uint32_t Size,
                                      uint32_t NVOffset, int32_t VBPtrOffset,
                                      uint32_t VBIndex, raw_ostream &Out) = 0;

  virtual void mangleCXXRTTIBaseClassDescriptor(
      const CXXRecordDecl *Derived, uint32_t NVOffset, int32_t VBPtrOffset,
      uint32_t VBTableOffset, uint32_t Flags, raw_ostream &Out) = 0;

  virtual void mangleCXXRTTIBaseClassArray(const CXXRecordDecl *Derived,
                                           raw_ostream &Out) = 0;
  virtual void
  mangleCXXRTTIClassHierarchyDescriptor(const CXXRecordDecl *Derived,
                                        raw_ostream &Out) = 0;

  virtual void
  mangleCXXRTTICompleteObjectLocator(const CXXRecordDecl *Derived,
                                     ArrayRef<const CXXRecordDecl *> BasePath,
                                     raw_ostream &Out) = 0;

  static bool classof(const MangleContext *C) {
    return C->getKind() == MK_Microsoft;
  }

  static MicrosoftMangleContext *create(ASTContext &Context,
                                        DiagnosticsEngine &Diags);
};
}

#endif
