//===- DelayedDiagnostic.h - Delayed declarator diagnostics -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the classes clang::DelayedDiagnostic and
/// clang::AccessedEntity.
///
/// DelayedDiangostic is used to record diagnostics that are being
/// conditionally produced during declarator parsing.  Certain kinds of
/// diagnostics -- notably deprecation and access control -- are suppressed
/// based on semantic properties of the parsed declaration that aren't known
/// until it is fully parsed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_DELAYEDDIAGNOSTIC_H
#define LLVM_CLANG_SEMA_DELAYEDDIAGNOSTIC_H

#include "clang/AST/DeclAccessPair.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstddef>
#include <utility>

namespace clang {

class ObjCInterfaceDecl;
class ObjCPropertyDecl;

namespace sema {

/// A declaration being accessed, together with information about how
/// it was accessed.
class AccessedEntity {
public:
  /// A member declaration found through lookup.  The target is the
  /// member.
  enum MemberNonce { Member };

  /// A hierarchy (base-to-derived or derived-to-base) conversion.
  /// The target is the base class.
  enum BaseNonce { Base };

  AccessedEntity(PartialDiagnostic::StorageAllocator &Allocator,
                 MemberNonce _,
                 CXXRecordDecl *NamingClass,
                 DeclAccessPair FoundDecl,
                 QualType BaseObjectType)
      : Access(FoundDecl.getAccess()), IsMember(true),
        Target(FoundDecl.getDecl()), NamingClass(NamingClass),
        BaseObjectType(BaseObjectType), Diag(0, Allocator) {
  }

  AccessedEntity(PartialDiagnostic::StorageAllocator &Allocator,
                 BaseNonce _,
                 CXXRecordDecl *BaseClass,
                 CXXRecordDecl *DerivedClass,
                 AccessSpecifier Access)
      : Access(Access), IsMember(false), Target(BaseClass),
        NamingClass(DerivedClass), Diag(0, Allocator) {}

  bool isMemberAccess() const { return IsMember; }

  bool isQuiet() const { return Diag.getDiagID() == 0; }

  AccessSpecifier getAccess() const { return AccessSpecifier(Access); }

  // These apply to member decls...
  NamedDecl *getTargetDecl() const { return Target; }
  CXXRecordDecl *getNamingClass() const { return NamingClass; }

  // ...and these apply to hierarchy conversions.
  CXXRecordDecl *getBaseClass() const {
    assert(!IsMember); return cast<CXXRecordDecl>(Target);
  }
  CXXRecordDecl *getDerivedClass() const { return NamingClass; }

  /// Retrieves the base object type, important when accessing
  /// an instance member.
  QualType getBaseObjectType() const { return BaseObjectType; }

  /// Sets a diagnostic to be performed.  The diagnostic is given
  /// four (additional) arguments:
  ///   %0 - 0 if the entity was private, 1 if protected
  ///   %1 - the DeclarationName of the entity
  ///   %2 - the TypeDecl type of the naming class
  ///   %3 - the TypeDecl type of the declaring class
  void setDiag(const PartialDiagnostic &PDiag) {
    assert(isQuiet() && "partial diagnostic already defined");
    Diag = PDiag;
  }
  PartialDiagnostic &setDiag(unsigned DiagID) {
    assert(isQuiet() && "partial diagnostic already defined");
    assert(DiagID && "creating null diagnostic");
    Diag.Reset(DiagID);
    return Diag;
  }
  const PartialDiagnostic &getDiag() const {
    return Diag;
  }

private:
  unsigned Access : 2;
  unsigned IsMember : 1;
  NamedDecl *Target;
  CXXRecordDecl *NamingClass;
  QualType BaseObjectType;
  PartialDiagnostic Diag;
};

/// A diagnostic message which has been conditionally emitted pending
/// the complete parsing of the current declaration.
class DelayedDiagnostic {
public:
  enum DDKind : unsigned char { Availability, Access, ForbiddenType };

  DDKind Kind;
  bool Triggered;

  SourceLocation Loc;

  void Destroy();

  static DelayedDiagnostic makeAvailability(AvailabilityResult AR,
                                            ArrayRef<SourceLocation> Locs,
                                            const NamedDecl *ReferringDecl,
                                            const NamedDecl *OffendingDecl,
                                            const ObjCInterfaceDecl *UnknownObjCClass,
                                            const ObjCPropertyDecl  *ObjCProperty,
                                            StringRef Msg,
                                            bool ObjCPropertyAccess);

  static DelayedDiagnostic makeAccess(SourceLocation Loc,
                                      const AccessedEntity &Entity) {
    DelayedDiagnostic DD;
    DD.Kind = Access;
    DD.Triggered = false;
    DD.Loc = Loc;
    new (&DD.getAccessData()) AccessedEntity(Entity);
    return DD;
  }

  static DelayedDiagnostic makeForbiddenType(SourceLocation loc,
                                             unsigned diagnostic,
                                             QualType type,
                                             unsigned argument) {
    DelayedDiagnostic DD;
    DD.Kind = ForbiddenType;
    DD.Triggered = false;
    DD.Loc = loc;
    DD.ForbiddenTypeData.Diagnostic = diagnostic;
    DD.ForbiddenTypeData.OperandType = type.getAsOpaquePtr();
    DD.ForbiddenTypeData.Argument = argument;
    return DD;
  }

  AccessedEntity &getAccessData() {
    assert(Kind == Access && "Not an access diagnostic.");
    return *reinterpret_cast<AccessedEntity*>(AccessData);
  }
  const AccessedEntity &getAccessData() const {
    assert(Kind == Access && "Not an access diagnostic.");
    return *reinterpret_cast<const AccessedEntity*>(AccessData);
  }

  const NamedDecl *getAvailabilityReferringDecl() const {
    assert(Kind == Availability && "Not an availability diagnostic.");
    return AvailabilityData.ReferringDecl;
  }

  const NamedDecl *getAvailabilityOffendingDecl() const {
    return AvailabilityData.OffendingDecl;
  }

  StringRef getAvailabilityMessage() const {
    assert(Kind == Availability && "Not an availability diagnostic.");
    return StringRef(AvailabilityData.Message, AvailabilityData.MessageLen);
  }

  ArrayRef<SourceLocation> getAvailabilitySelectorLocs() const {
    assert(Kind == Availability && "Not an availability diagnostic.");
    return llvm::makeArrayRef(AvailabilityData.SelectorLocs,
                              AvailabilityData.NumSelectorLocs);
  }

  AvailabilityResult getAvailabilityResult() const {
    assert(Kind == Availability && "Not an availability diagnostic.");
    return AvailabilityData.AR;
  }

  /// The diagnostic ID to emit.  Used like so:
  ///   Diag(diag.Loc, diag.getForbiddenTypeDiagnostic())
  ///     << diag.getForbiddenTypeOperand()
  ///     << diag.getForbiddenTypeArgument();
  unsigned getForbiddenTypeDiagnostic() const {
    assert(Kind == ForbiddenType && "not a forbidden-type diagnostic");
    return ForbiddenTypeData.Diagnostic;
  }

  unsigned getForbiddenTypeArgument() const {
    assert(Kind == ForbiddenType && "not a forbidden-type diagnostic");
    return ForbiddenTypeData.Argument;
  }

  QualType getForbiddenTypeOperand() const {
    assert(Kind == ForbiddenType && "not a forbidden-type diagnostic");
    return QualType::getFromOpaquePtr(ForbiddenTypeData.OperandType);
  }

  const ObjCInterfaceDecl *getUnknownObjCClass() const {
    return AvailabilityData.UnknownObjCClass;
  }

  const ObjCPropertyDecl *getObjCProperty() const {
    return AvailabilityData.ObjCProperty;
  }

  bool getObjCPropertyAccess() const {
    return AvailabilityData.ObjCPropertyAccess;
  }

private:
  struct AD {
    const NamedDecl *ReferringDecl;
    const NamedDecl *OffendingDecl;
    const ObjCInterfaceDecl *UnknownObjCClass;
    const ObjCPropertyDecl  *ObjCProperty;
    const char *Message;
    size_t MessageLen;
    SourceLocation *SelectorLocs;
    size_t NumSelectorLocs;
    AvailabilityResult AR;
    bool ObjCPropertyAccess;
  };

  struct FTD {
    unsigned Diagnostic;
    unsigned Argument;
    void *OperandType;
  };

  union {
    struct AD AvailabilityData;
    struct FTD ForbiddenTypeData;

    /// Access control.
    char AccessData[sizeof(AccessedEntity)];
  };
};

/// A collection of diagnostics which were delayed.
class DelayedDiagnosticPool {
  const DelayedDiagnosticPool *Parent;
  SmallVector<DelayedDiagnostic, 4> Diagnostics;

public:
  DelayedDiagnosticPool(const DelayedDiagnosticPool *parent) : Parent(parent) {}

  DelayedDiagnosticPool(const DelayedDiagnosticPool &) = delete;
  DelayedDiagnosticPool &operator=(const DelayedDiagnosticPool &) = delete;

  DelayedDiagnosticPool(DelayedDiagnosticPool &&Other)
      : Parent(Other.Parent), Diagnostics(std::move(Other.Diagnostics)) {
    Other.Diagnostics.clear();
  }

  DelayedDiagnosticPool &operator=(DelayedDiagnosticPool &&Other) {
    Parent = Other.Parent;
    Diagnostics = std::move(Other.Diagnostics);
    Other.Diagnostics.clear();
    return *this;
  }

  ~DelayedDiagnosticPool() {
    for (SmallVectorImpl<DelayedDiagnostic>::iterator
           i = Diagnostics.begin(), e = Diagnostics.end(); i != e; ++i)
      i->Destroy();
  }

  const DelayedDiagnosticPool *getParent() const { return Parent; }

  /// Does this pool, or any of its ancestors, contain any diagnostics?
  bool empty() const {
    return (Diagnostics.empty() && (!Parent || Parent->empty()));
  }

  /// Add a diagnostic to this pool.
  void add(const DelayedDiagnostic &diag) {
    Diagnostics.push_back(diag);
  }

  /// Steal the diagnostics from the given pool.
  void steal(DelayedDiagnosticPool &pool) {
    if (pool.Diagnostics.empty()) return;

    if (Diagnostics.empty()) {
      Diagnostics = std::move(pool.Diagnostics);
    } else {
      Diagnostics.append(pool.pool_begin(), pool.pool_end());
    }
    pool.Diagnostics.clear();
  }

  using pool_iterator = SmallVectorImpl<DelayedDiagnostic>::const_iterator;

  pool_iterator pool_begin() const { return Diagnostics.begin(); }
  pool_iterator pool_end() const { return Diagnostics.end(); }
  bool pool_empty() const { return Diagnostics.empty(); }
};

} // namespace clang

/// Add a diagnostic to the current delay pool.
inline void Sema::DelayedDiagnostics::add(const sema::DelayedDiagnostic &diag) {
  assert(shouldDelayDiagnostics() && "trying to delay without pool");
  CurPool->add(diag);
}

} // namespace clang

#endif // LLVM_CLANG_SEMA_DELAYEDDIAGNOSTIC_H
