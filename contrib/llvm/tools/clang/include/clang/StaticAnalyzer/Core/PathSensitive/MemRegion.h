//==- MemRegion.h - Abstract memory regions for static analysis -*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines MemRegion and its subclasses.  MemRegion defines a
//  partially-typed abstraction of memory useful for path-sensitive dataflow
//  analyses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_MEMREGION_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_MEMREGION_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace clang {

class AnalysisDeclContext;
class CXXRecordDecl;
class Decl;
class LocationContext;
class StackFrameContext;

namespace ento {

class CodeTextRegion;
class MemRegion;
class MemRegionManager;
class MemSpaceRegion;
class SValBuilder;
class SymbolicRegion;
class VarRegion;

/// Represent a region's offset within the top level base region.
class RegionOffset {
  /// The base region.
  const MemRegion *R = nullptr;

  /// The bit offset within the base region. Can be negative.
  int64_t Offset;

public:
  // We're using a const instead of an enumeration due to the size required;
  // Visual Studio will only create enumerations of size int, not long long.
  static const int64_t Symbolic = std::numeric_limits<int64_t>::max();

  RegionOffset() = default;
  RegionOffset(const MemRegion *r, int64_t off) : R(r), Offset(off) {}

  const MemRegion *getRegion() const { return R; }

  bool hasSymbolicOffset() const { return Offset == Symbolic; }

  int64_t getOffset() const {
    assert(!hasSymbolicOffset());
    return Offset;
  }

  bool isValid() const { return R; }
};

//===----------------------------------------------------------------------===//
// Base region classes.
//===----------------------------------------------------------------------===//

/// MemRegion - The root abstract class for all memory regions.
class MemRegion : public llvm::FoldingSetNode {
public:
  enum Kind {
#define REGION(Id, Parent) Id ## Kind,
#define REGION_RANGE(Id, First, Last) BEGIN_##Id = First, END_##Id = Last,
#include "clang/StaticAnalyzer/Core/PathSensitive/Regions.def"
  };

private:
  const Kind kind;
  mutable Optional<RegionOffset> cachedOffset;

protected:
  MemRegion(Kind k) : kind(k) {}
  virtual ~MemRegion();

public:
  ASTContext &getContext() const;

  virtual void Profile(llvm::FoldingSetNodeID& ID) const = 0;

  virtual MemRegionManager* getMemRegionManager() const = 0;

  const MemSpaceRegion *getMemorySpace() const;

  const MemRegion *getBaseRegion() const;

  /// Recursively retrieve the region of the most derived class instance of
  /// regions of C++ base class instances.
  const MemRegion *getMostDerivedObjectRegion() const;

  /// Check if the region is a subregion of the given region.
  /// Each region is a subregion of itself.
  virtual bool isSubRegionOf(const MemRegion *R) const;

  const MemRegion *StripCasts(bool StripBaseAndDerivedCasts = true) const;

  /// If this is a symbolic region, returns the region. Otherwise,
  /// goes up the base chain looking for the first symbolic base region.
  const SymbolicRegion *getSymbolicBase() const;

  bool hasGlobalsOrParametersStorage() const;

  bool hasStackStorage() const;

  bool hasStackNonParametersStorage() const;

  bool hasStackParametersStorage() const;

  /// Compute the offset within the top level memory object.
  RegionOffset getAsOffset() const;

  /// Get a string representation of a region for debug use.
  std::string getString() const;

  virtual void dumpToStream(raw_ostream &os) const;

  void dump() const;

  /// Returns true if this region can be printed in a user-friendly way.
  virtual bool canPrintPretty() const;

  /// Print the region for use in diagnostics.
  virtual void printPretty(raw_ostream &os) const;

  /// Returns true if this region's textual representation can be used
  /// as part of a larger expression.
  virtual bool canPrintPrettyAsExpr() const;

  /// Print the region as expression.
  ///
  /// When this region represents a subexpression, the method is for printing
  /// an expression containing it.
  virtual void printPrettyAsExpr(raw_ostream &os) const;

  Kind getKind() const { return kind; }

  template<typename RegionTy> const RegionTy* getAs() const;

  virtual bool isBoundable() const { return false; }

  /// Get descriptive name for memory region. The name is obtained from
  /// the variable/field declaration retrieved from the memory region.
  /// Regions that point to an element of an array are returned as: "arr[0]".
  /// Regions that point to a struct are returned as: "st.var".
  //
  /// \param UseQuotes Set if the name should be quoted.
  ///
  /// \returns variable name for memory region
  std::string getDescriptiveName(bool UseQuotes = true) const;

  /// Retrieve source range from memory region. The range retrieval
  /// is based on the decl obtained from the memory region.
  /// For a VarRegion the range of the base region is returned.
  /// For a FieldRegion the range of the field is returned.
  /// If no declaration is found, an empty source range is returned.
  /// The client is responsible for checking if the returned range is valid.
  ///
  /// \returns source range for declaration retrieved from memory region
  SourceRange sourceRange() const;
};

/// MemSpaceRegion - A memory region that represents a "memory space";
///  for example, the set of global variables, the stack frame, etc.
class MemSpaceRegion : public MemRegion {
protected:
  MemRegionManager *Mgr;

  MemSpaceRegion(MemRegionManager *mgr, Kind k) : MemRegion(k), Mgr(mgr) {
    assert(classof(this));
    assert(mgr);
  }

  MemRegionManager* getMemRegionManager() const override { return Mgr; }

public:
  bool isBoundable() const override { return false; }

  void Profile(llvm::FoldingSetNodeID &ID) const override;

  static bool classof(const MemRegion *R) {
    Kind k = R->getKind();
    return k >= BEGIN_MEMSPACES && k <= END_MEMSPACES;
  }
};

/// CodeSpaceRegion - The memory space that holds the executable code of
/// functions and blocks.
class CodeSpaceRegion : public MemSpaceRegion {
  friend class MemRegionManager;

  CodeSpaceRegion(MemRegionManager *mgr)
      : MemSpaceRegion(mgr, CodeSpaceRegionKind) {}

public:
  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion *R) {
    return R->getKind() == CodeSpaceRegionKind;
  }
};

class GlobalsSpaceRegion : public MemSpaceRegion {
  virtual void anchor();

protected:
  GlobalsSpaceRegion(MemRegionManager *mgr, Kind k) : MemSpaceRegion(mgr, k) {
    assert(classof(this));
  }

public:
  static bool classof(const MemRegion *R) {
    Kind k = R->getKind();
    return k >= BEGIN_GLOBAL_MEMSPACES && k <= END_GLOBAL_MEMSPACES;
  }
};

/// The region of the static variables within the current CodeTextRegion
/// scope.
///
/// Currently, only the static locals are placed there, so we know that these
/// variables do not get invalidated by calls to other functions.
class StaticGlobalSpaceRegion : public GlobalsSpaceRegion {
  friend class MemRegionManager;

  const CodeTextRegion *CR;

  StaticGlobalSpaceRegion(MemRegionManager *mgr, const CodeTextRegion *cr)
      : GlobalsSpaceRegion(mgr, StaticGlobalSpaceRegionKind), CR(cr) {
    assert(cr);
  }

public:
  void Profile(llvm::FoldingSetNodeID &ID) const override;

  void dumpToStream(raw_ostream &os) const override;

  const CodeTextRegion *getCodeRegion() const { return CR; }

  static bool classof(const MemRegion *R) {
    return R->getKind() == StaticGlobalSpaceRegionKind;
  }
};

/// The region for all the non-static global variables.
///
/// This class is further split into subclasses for efficient implementation of
/// invalidating a set of related global values as is done in
/// RegionStoreManager::invalidateRegions (instead of finding all the dependent
/// globals, we invalidate the whole parent region).
class NonStaticGlobalSpaceRegion : public GlobalsSpaceRegion {
  void anchor() override;

protected:
  NonStaticGlobalSpaceRegion(MemRegionManager *mgr, Kind k)
      : GlobalsSpaceRegion(mgr, k) {
    assert(classof(this));
  }

public:
  static bool classof(const MemRegion *R) {
    Kind k = R->getKind();
    return k >= BEGIN_NON_STATIC_GLOBAL_MEMSPACES &&
           k <= END_NON_STATIC_GLOBAL_MEMSPACES;
  }
};

/// The region containing globals which are defined in system/external
/// headers and are considered modifiable by system calls (ex: errno).
class GlobalSystemSpaceRegion : public NonStaticGlobalSpaceRegion {
  friend class MemRegionManager;

  GlobalSystemSpaceRegion(MemRegionManager *mgr)
      : NonStaticGlobalSpaceRegion(mgr, GlobalSystemSpaceRegionKind) {}

public:
  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion *R) {
    return R->getKind() == GlobalSystemSpaceRegionKind;
  }
};

/// The region containing globals which are considered not to be modified
/// or point to data which could be modified as a result of a function call
/// (system or internal). Ex: Const global scalars would be modeled as part of
/// this region. This region also includes most system globals since they have
/// low chance of being modified.
class GlobalImmutableSpaceRegion : public NonStaticGlobalSpaceRegion {
  friend class MemRegionManager;

  GlobalImmutableSpaceRegion(MemRegionManager *mgr)
      : NonStaticGlobalSpaceRegion(mgr, GlobalImmutableSpaceRegionKind) {}

public:
  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion *R) {
    return R->getKind() == GlobalImmutableSpaceRegionKind;
  }
};

/// The region containing globals which can be modified by calls to
/// "internally" defined functions - (for now just) functions other then system
/// calls.
class GlobalInternalSpaceRegion : public NonStaticGlobalSpaceRegion {
  friend class MemRegionManager;

  GlobalInternalSpaceRegion(MemRegionManager *mgr)
      : NonStaticGlobalSpaceRegion(mgr, GlobalInternalSpaceRegionKind) {}

public:
  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion *R) {
    return R->getKind() == GlobalInternalSpaceRegionKind;
  }
};

class HeapSpaceRegion : public MemSpaceRegion {
  friend class MemRegionManager;

  HeapSpaceRegion(MemRegionManager *mgr)
      : MemSpaceRegion(mgr, HeapSpaceRegionKind) {}

public:
  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion *R) {
    return R->getKind() == HeapSpaceRegionKind;
  }
};

class UnknownSpaceRegion : public MemSpaceRegion {
  friend class MemRegionManager;

  UnknownSpaceRegion(MemRegionManager *mgr)
      : MemSpaceRegion(mgr, UnknownSpaceRegionKind) {}

public:
  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion *R) {
    return R->getKind() == UnknownSpaceRegionKind;
  }
};

class StackSpaceRegion : public MemSpaceRegion {
  virtual void anchor();

  const StackFrameContext *SFC;

protected:
  StackSpaceRegion(MemRegionManager *mgr, Kind k, const StackFrameContext *sfc)
      : MemSpaceRegion(mgr, k), SFC(sfc) {
    assert(classof(this));
    assert(sfc);
  }

public:
  const StackFrameContext *getStackFrame() const { return SFC; }

  void Profile(llvm::FoldingSetNodeID &ID) const override;

  static bool classof(const MemRegion *R) {
    Kind k = R->getKind();
    return k >= BEGIN_STACK_MEMSPACES && k <= END_STACK_MEMSPACES;
  }
};

class StackLocalsSpaceRegion : public StackSpaceRegion {
  friend class MemRegionManager;

  StackLocalsSpaceRegion(MemRegionManager *mgr, const StackFrameContext *sfc)
      : StackSpaceRegion(mgr, StackLocalsSpaceRegionKind, sfc) {}

public:
  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion *R) {
    return R->getKind() == StackLocalsSpaceRegionKind;
  }
};

class StackArgumentsSpaceRegion : public StackSpaceRegion {
private:
  friend class MemRegionManager;

  StackArgumentsSpaceRegion(MemRegionManager *mgr, const StackFrameContext *sfc)
      : StackSpaceRegion(mgr, StackArgumentsSpaceRegionKind, sfc) {}

public:
  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion *R) {
    return R->getKind() == StackArgumentsSpaceRegionKind;
  }
};

/// SubRegion - A region that subsets another larger region.  Most regions
///  are subclasses of SubRegion.
class SubRegion : public MemRegion {
  virtual void anchor();

protected:
  const MemRegion* superRegion;

  SubRegion(const MemRegion *sReg, Kind k) : MemRegion(k), superRegion(sReg) {
    assert(classof(this));
    assert(sReg);
  }

public:
  const MemRegion* getSuperRegion() const {
    return superRegion;
  }

  /// getExtent - Returns the size of the region in bytes.
  virtual DefinedOrUnknownSVal getExtent(SValBuilder &svalBuilder) const {
    return UnknownVal();
  }

  MemRegionManager* getMemRegionManager() const override;

  bool isSubRegionOf(const MemRegion* R) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() > END_MEMSPACES;
  }
};

//===----------------------------------------------------------------------===//
// MemRegion subclasses.
//===----------------------------------------------------------------------===//

/// AllocaRegion - A region that represents an untyped blob of bytes created
///  by a call to 'alloca'.
class AllocaRegion : public SubRegion {
  friend class MemRegionManager;

  // Block counter. Used to distinguish different pieces of memory allocated by
  // alloca at the same call site.
  unsigned Cnt;

  const Expr *Ex;

  AllocaRegion(const Expr *ex, unsigned cnt, const MemSpaceRegion *superRegion)
      : SubRegion(superRegion, AllocaRegionKind), Cnt(cnt), Ex(ex) {
    assert(Ex);
  }

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const Expr *Ex,
                            unsigned Cnt, const MemRegion *superRegion);

public:
  const Expr *getExpr() const { return Ex; }

  bool isBoundable() const override { return true; }

  DefinedOrUnknownSVal getExtent(SValBuilder &svalBuilder) const override;

  void Profile(llvm::FoldingSetNodeID& ID) const override;

  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == AllocaRegionKind;
  }
};

/// TypedRegion - An abstract class representing regions that are typed.
class TypedRegion : public SubRegion {
  void anchor() override;

protected:
  TypedRegion(const MemRegion *sReg, Kind k) : SubRegion(sReg, k) {
    assert(classof(this));
  }

public:
  virtual QualType getLocationType() const = 0;

  QualType getDesugaredLocationType(ASTContext &Context) const {
    return getLocationType().getDesugaredType(Context);
  }

  bool isBoundable() const override { return true; }

  static bool classof(const MemRegion* R) {
    unsigned k = R->getKind();
    return k >= BEGIN_TYPED_REGIONS && k <= END_TYPED_REGIONS;
  }
};

/// TypedValueRegion - An abstract class representing regions having a typed value.
class TypedValueRegion : public TypedRegion {
  void anchor() override;

protected:
  TypedValueRegion(const MemRegion* sReg, Kind k) : TypedRegion(sReg, k) {
    assert(classof(this));
  }

public:
  virtual QualType getValueType() const = 0;

  QualType getLocationType() const override {
    // FIXME: We can possibly optimize this later to cache this value.
    QualType T = getValueType();
    ASTContext &ctx = getContext();
    if (T->getAs<ObjCObjectType>())
      return ctx.getObjCObjectPointerType(T);
    return ctx.getPointerType(getValueType());
  }

  QualType getDesugaredValueType(ASTContext &Context) const {
    QualType T = getValueType();
    return T.getTypePtrOrNull() ? T.getDesugaredType(Context) : T;
  }

  DefinedOrUnknownSVal getExtent(SValBuilder &svalBuilder) const override;

  static bool classof(const MemRegion* R) {
    unsigned k = R->getKind();
    return k >= BEGIN_TYPED_VALUE_REGIONS && k <= END_TYPED_VALUE_REGIONS;
  }
};

class CodeTextRegion : public TypedRegion {
  void anchor() override;

protected:
  CodeTextRegion(const MemSpaceRegion *sreg, Kind k) : TypedRegion(sreg, k) {
    assert(classof(this));
  }

public:
  bool isBoundable() const override { return false; }

  static bool classof(const MemRegion* R) {
    Kind k = R->getKind();
    return k >= BEGIN_CODE_TEXT_REGIONS && k <= END_CODE_TEXT_REGIONS;
  }
};

/// FunctionCodeRegion - A region that represents code texts of function.
class FunctionCodeRegion : public CodeTextRegion {
  friend class MemRegionManager;

  const NamedDecl *FD;

  FunctionCodeRegion(const NamedDecl *fd, const CodeSpaceRegion* sreg)
      : CodeTextRegion(sreg, FunctionCodeRegionKind), FD(fd) {
    assert(isa<ObjCMethodDecl>(fd) || isa<FunctionDecl>(fd));
  }

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const NamedDecl *FD,
                            const MemRegion*);

public:
  QualType getLocationType() const override {
    const ASTContext &Ctx = getContext();
    if (const auto *D = dyn_cast<FunctionDecl>(FD)) {
      return Ctx.getPointerType(D->getType());
    }

    assert(isa<ObjCMethodDecl>(FD));
    assert(false && "Getting the type of ObjCMethod is not supported yet");

    // TODO: We might want to return a different type here (ex: id (*ty)(...))
    //       depending on how it is used.
    return {};
  }

  const NamedDecl *getDecl() const {
    return FD;
  }

  void dumpToStream(raw_ostream &os) const override;

  void Profile(llvm::FoldingSetNodeID& ID) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == FunctionCodeRegionKind;
  }
};

/// BlockCodeRegion - A region that represents code texts of blocks (closures).
///  Blocks are represented with two kinds of regions.  BlockCodeRegions
///  represent the "code", while BlockDataRegions represent instances of blocks,
///  which correspond to "code+data".  The distinction is important, because
///  like a closure a block captures the values of externally referenced
///  variables.
class BlockCodeRegion : public CodeTextRegion {
  friend class MemRegionManager;

  const BlockDecl *BD;
  AnalysisDeclContext *AC;
  CanQualType locTy;

  BlockCodeRegion(const BlockDecl *bd, CanQualType lTy,
                  AnalysisDeclContext *ac, const CodeSpaceRegion* sreg)
      : CodeTextRegion(sreg, BlockCodeRegionKind), BD(bd), AC(ac), locTy(lTy) {
    assert(bd);
    assert(ac);
    assert(lTy->getTypePtr()->isBlockPointerType());
  }

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const BlockDecl *BD,
                            CanQualType, const AnalysisDeclContext*,
                            const MemRegion*);

public:
  QualType getLocationType() const override {
    return locTy;
  }

  const BlockDecl *getDecl() const {
    return BD;
  }

  AnalysisDeclContext *getAnalysisDeclContext() const { return AC; }

  void dumpToStream(raw_ostream &os) const override;

  void Profile(llvm::FoldingSetNodeID& ID) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == BlockCodeRegionKind;
  }
};

/// BlockDataRegion - A region that represents a block instance.
///  Blocks are represented with two kinds of regions.  BlockCodeRegions
///  represent the "code", while BlockDataRegions represent instances of blocks,
///  which correspond to "code+data".  The distinction is important, because
///  like a closure a block captures the values of externally referenced
///  variables.
class BlockDataRegion : public TypedRegion {
  friend class MemRegionManager;

  const BlockCodeRegion *BC;
  const LocationContext *LC; // Can be null
  unsigned BlockCount;
  void *ReferencedVars = nullptr;
  void *OriginalVars = nullptr;

  BlockDataRegion(const BlockCodeRegion *bc, const LocationContext *lc,
                  unsigned count, const MemSpaceRegion *sreg)
      : TypedRegion(sreg, BlockDataRegionKind), BC(bc), LC(lc),
        BlockCount(count) {
    assert(bc);
    assert(lc);
    assert(isa<GlobalImmutableSpaceRegion>(sreg) ||
           isa<StackLocalsSpaceRegion>(sreg) ||
           isa<UnknownSpaceRegion>(sreg));
  }

  static void ProfileRegion(llvm::FoldingSetNodeID&, const BlockCodeRegion *,
                            const LocationContext *, unsigned,
                            const MemRegion *);

public:
  const BlockCodeRegion *getCodeRegion() const { return BC; }

  const BlockDecl *getDecl() const { return BC->getDecl(); }

  QualType getLocationType() const override { return BC->getLocationType(); }

  class referenced_vars_iterator {
    const MemRegion * const *R;
    const MemRegion * const *OriginalR;

  public:
    explicit referenced_vars_iterator(const MemRegion * const *r,
                                      const MemRegion * const *originalR)
        : R(r), OriginalR(originalR) {}

    const VarRegion *getCapturedRegion() const {
      return cast<VarRegion>(*R);
    }

    const VarRegion *getOriginalRegion() const {
      return cast<VarRegion>(*OriginalR);
    }

    bool operator==(const referenced_vars_iterator &I) const {
      assert((R == nullptr) == (I.R == nullptr));
      return I.R == R;
    }

    bool operator!=(const referenced_vars_iterator &I) const {
      assert((R == nullptr) == (I.R == nullptr));
      return I.R != R;
    }

    referenced_vars_iterator &operator++() {
      ++R;
      ++OriginalR;
      return *this;
    }
  };

  /// Return the original region for a captured region, if
  /// one exists.
  const VarRegion *getOriginalRegion(const VarRegion *VR) const;

  referenced_vars_iterator referenced_vars_begin() const;
  referenced_vars_iterator referenced_vars_end() const;

  void dumpToStream(raw_ostream &os) const override;

  void Profile(llvm::FoldingSetNodeID& ID) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == BlockDataRegionKind;
  }

private:
  void LazyInitializeReferencedVars();
  std::pair<const VarRegion *, const VarRegion *>
  getCaptureRegions(const VarDecl *VD);
};

/// SymbolicRegion - A special, "non-concrete" region. Unlike other region
///  classes, SymbolicRegion represents a region that serves as an alias for
///  either a real region, a NULL pointer, etc.  It essentially is used to
///  map the concept of symbolic values into the domain of regions.  Symbolic
///  regions do not need to be typed.
class SymbolicRegion : public SubRegion {
  friend class MemRegionManager;

  const SymbolRef sym;

  SymbolicRegion(const SymbolRef s, const MemSpaceRegion *sreg)
      : SubRegion(sreg, SymbolicRegionKind), sym(s) {
    // Because pointer arithmetic is represented by ElementRegion layers,
    // the base symbol here should not contain any arithmetic.
    assert(s && isa<SymbolData>(s));
    assert(s->getType()->isAnyPointerType() ||
           s->getType()->isReferenceType() ||
           s->getType()->isBlockPointerType());

    // populateWorklistFromSymbol() relies on this assertion, and needs to be
    // updated if more cases are introduced.
    assert(isa<UnknownSpaceRegion>(sreg) || isa<HeapSpaceRegion>(sreg));
  }

public:
  SymbolRef getSymbol() const { return sym; }

  bool isBoundable() const override { return true; }

  DefinedOrUnknownSVal getExtent(SValBuilder &svalBuilder) const override;

  void Profile(llvm::FoldingSetNodeID& ID) const override;

  static void ProfileRegion(llvm::FoldingSetNodeID& ID,
                            SymbolRef sym,
                            const MemRegion* superRegion);

  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == SymbolicRegionKind;
  }
};

/// StringRegion - Region associated with a StringLiteral.
class StringRegion : public TypedValueRegion {
  friend class MemRegionManager;

  const StringLiteral *Str;

  StringRegion(const StringLiteral *str, const GlobalInternalSpaceRegion *sreg)
      : TypedValueRegion(sreg, StringRegionKind), Str(str) {
    assert(str);
  }

  static void ProfileRegion(llvm::FoldingSetNodeID &ID,
                            const StringLiteral *Str,
                            const MemRegion *superRegion);

public:
  const StringLiteral *getStringLiteral() const { return Str; }

  QualType getValueType() const override { return Str->getType(); }

  DefinedOrUnknownSVal getExtent(SValBuilder &svalBuilder) const override;

  bool isBoundable() const override { return false; }

  void Profile(llvm::FoldingSetNodeID& ID) const override {
    ProfileRegion(ID, Str, superRegion);
  }

  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == StringRegionKind;
  }
};

/// The region associated with an ObjCStringLiteral.
class ObjCStringRegion : public TypedValueRegion {
  friend class MemRegionManager;

  const ObjCStringLiteral *Str;

  ObjCStringRegion(const ObjCStringLiteral *str,
                   const GlobalInternalSpaceRegion *sreg)
      : TypedValueRegion(sreg, ObjCStringRegionKind), Str(str) {
    assert(str);
  }

  static void ProfileRegion(llvm::FoldingSetNodeID &ID,
                            const ObjCStringLiteral *Str,
                            const MemRegion *superRegion);

public:
  const ObjCStringLiteral *getObjCStringLiteral() const { return Str; }

  QualType getValueType() const override { return Str->getType(); }

  bool isBoundable() const override { return false; }

  void Profile(llvm::FoldingSetNodeID& ID) const override {
    ProfileRegion(ID, Str, superRegion);
  }

  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == ObjCStringRegionKind;
  }
};

/// CompoundLiteralRegion - A memory region representing a compound literal.
///   Compound literals are essentially temporaries that are stack allocated
///   or in the global constant pool.
class CompoundLiteralRegion : public TypedValueRegion {
  friend class MemRegionManager;

  const CompoundLiteralExpr *CL;

  CompoundLiteralRegion(const CompoundLiteralExpr *cl,
                        const MemSpaceRegion *sReg)
      : TypedValueRegion(sReg, CompoundLiteralRegionKind), CL(cl) {
    assert(cl);
    assert(isa<GlobalInternalSpaceRegion>(sReg) ||
           isa<StackLocalsSpaceRegion>(sReg));
  }

  static void ProfileRegion(llvm::FoldingSetNodeID& ID,
                            const CompoundLiteralExpr *CL,
                            const MemRegion* superRegion);

public:
  QualType getValueType() const override { return CL->getType(); }

  bool isBoundable() const override { return !CL->isFileScope(); }

  void Profile(llvm::FoldingSetNodeID& ID) const override;

  void dumpToStream(raw_ostream &os) const override;

  const CompoundLiteralExpr *getLiteralExpr() const { return CL; }

  static bool classof(const MemRegion* R) {
    return R->getKind() == CompoundLiteralRegionKind;
  }
};

class DeclRegion : public TypedValueRegion {
protected:
  const ValueDecl *D;

  DeclRegion(const ValueDecl *d, const MemRegion *sReg, Kind k)
      : TypedValueRegion(sReg, k), D(d) {
    assert(classof(this));
    assert(d);
  }

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const Decl *D,
                      const MemRegion* superRegion, Kind k);

public:
  const ValueDecl *getDecl() const { return D; }
  void Profile(llvm::FoldingSetNodeID& ID) const override;

  static bool classof(const MemRegion* R) {
    unsigned k = R->getKind();
    return k >= BEGIN_DECL_REGIONS && k <= END_DECL_REGIONS;
  }
};

class VarRegion : public DeclRegion {
  friend class MemRegionManager;

  // Constructors and private methods.
  VarRegion(const VarDecl *vd, const MemRegion *sReg)
      : DeclRegion(vd, sReg, VarRegionKind) {
    // VarRegion appears in unknown space when it's a block variable as seen
    // from a block using it, when this block is analyzed at top-level.
    // Other block variables appear within block data regions,
    // which, unlike everything else on this list, are not memory spaces.
    assert(isa<GlobalsSpaceRegion>(sReg) || isa<StackSpaceRegion>(sReg) ||
           isa<BlockDataRegion>(sReg) || isa<UnknownSpaceRegion>(sReg));
  }

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const VarDecl *VD,
                            const MemRegion *superRegion) {
    DeclRegion::ProfileRegion(ID, VD, superRegion, VarRegionKind);
  }

public:
  void Profile(llvm::FoldingSetNodeID& ID) const override;

  const VarDecl *getDecl() const { return cast<VarDecl>(D); }

  const StackFrameContext *getStackFrame() const;

  QualType getValueType() const override {
    // FIXME: We can cache this if needed.
    return getDecl()->getType();
  }

  void dumpToStream(raw_ostream &os) const override;

  bool canPrintPrettyAsExpr() const override;

  void printPrettyAsExpr(raw_ostream &os) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == VarRegionKind;
  }
};

/// CXXThisRegion - Represents the region for the implicit 'this' parameter
///  in a call to a C++ method.  This region doesn't represent the object
///  referred to by 'this', but rather 'this' itself.
class CXXThisRegion : public TypedValueRegion {
  friend class MemRegionManager;

  CXXThisRegion(const PointerType *thisPointerTy,
                const StackArgumentsSpaceRegion *sReg)
      : TypedValueRegion(sReg, CXXThisRegionKind),
        ThisPointerTy(thisPointerTy) {
    assert(ThisPointerTy->getPointeeType()->getAsCXXRecordDecl() &&
           "Invalid region type!");
  }

  static void ProfileRegion(llvm::FoldingSetNodeID &ID,
                            const PointerType *PT,
                            const MemRegion *sReg);

public:
  void Profile(llvm::FoldingSetNodeID &ID) const override;

  QualType getValueType() const override {
    return QualType(ThisPointerTy, 0);
  }

  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == CXXThisRegionKind;
  }

private:
  const PointerType *ThisPointerTy;
};

class FieldRegion : public DeclRegion {
  friend class MemRegionManager;

  FieldRegion(const FieldDecl *fd, const SubRegion* sReg)
      : DeclRegion(fd, sReg, FieldRegionKind) {}

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const FieldDecl *FD,
                            const MemRegion* superRegion) {
    DeclRegion::ProfileRegion(ID, FD, superRegion, FieldRegionKind);
  }

public:
  const FieldDecl *getDecl() const { return cast<FieldDecl>(D); }

  QualType getValueType() const override {
    // FIXME: We can cache this if needed.
    return getDecl()->getType();
  }

  DefinedOrUnknownSVal getExtent(SValBuilder &svalBuilder) const override;

  void dumpToStream(raw_ostream &os) const override;

  bool canPrintPretty() const override;
  void printPretty(raw_ostream &os) const override;
  bool canPrintPrettyAsExpr() const override;
  void printPrettyAsExpr(raw_ostream &os) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == FieldRegionKind;
  }
};

class ObjCIvarRegion : public DeclRegion {
  friend class MemRegionManager;

  ObjCIvarRegion(const ObjCIvarDecl *ivd, const SubRegion *sReg);

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, const ObjCIvarDecl *ivd,
                            const MemRegion* superRegion);

public:
  const ObjCIvarDecl *getDecl() const;
  QualType getValueType() const override;

  bool canPrintPrettyAsExpr() const override;
  void printPrettyAsExpr(raw_ostream &os) const override;

  void dumpToStream(raw_ostream &os) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == ObjCIvarRegionKind;
  }
};

//===----------------------------------------------------------------------===//
// Auxiliary data classes for use with MemRegions.
//===----------------------------------------------------------------------===//

class RegionRawOffset {
  friend class ElementRegion;

  const MemRegion *Region;
  CharUnits Offset;

  RegionRawOffset(const MemRegion* reg, CharUnits offset = CharUnits::Zero())
      : Region(reg), Offset(offset) {}

public:
  // FIXME: Eventually support symbolic offsets.
  CharUnits getOffset() const { return Offset; }
  const MemRegion *getRegion() const { return Region; }

  void dumpToStream(raw_ostream &os) const;
  void dump() const;
};

/// ElementRegion is used to represent both array elements and casts.
class ElementRegion : public TypedValueRegion {
  friend class MemRegionManager;

  QualType ElementType;
  NonLoc Index;

  ElementRegion(QualType elementType, NonLoc Idx, const SubRegion *sReg)
      : TypedValueRegion(sReg, ElementRegionKind), ElementType(elementType),
        Index(Idx) {
    assert((!Idx.getAs<nonloc::ConcreteInt>() ||
            Idx.castAs<nonloc::ConcreteInt>().getValue().isSigned()) &&
           "The index must be signed");
    assert(!elementType.isNull() && !elementType->isVoidType() &&
           "Invalid region type!");
  }

  static void ProfileRegion(llvm::FoldingSetNodeID& ID, QualType elementType,
                            SVal Idx, const MemRegion* superRegion);

public:
  NonLoc getIndex() const { return Index; }

  QualType getValueType() const override { return ElementType; }

  QualType getElementType() const { return ElementType; }

  /// Compute the offset within the array. The array might also be a subobject.
  RegionRawOffset getAsArrayOffset() const;

  void dumpToStream(raw_ostream &os) const override;

  void Profile(llvm::FoldingSetNodeID& ID) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == ElementRegionKind;
  }
};

// C++ temporary object associated with an expression.
class CXXTempObjectRegion : public TypedValueRegion {
  friend class MemRegionManager;

  Expr const *Ex;

  CXXTempObjectRegion(Expr const *E, MemSpaceRegion const *sReg)
      : TypedValueRegion(sReg, CXXTempObjectRegionKind), Ex(E) {
    assert(E);
    assert(isa<StackLocalsSpaceRegion>(sReg) ||
           isa<GlobalInternalSpaceRegion>(sReg));
  }

  static void ProfileRegion(llvm::FoldingSetNodeID &ID,
                            Expr const *E, const MemRegion *sReg);

public:
  const Expr *getExpr() const { return Ex; }

  QualType getValueType() const override { return Ex->getType(); }

  void dumpToStream(raw_ostream &os) const override;

  void Profile(llvm::FoldingSetNodeID &ID) const override;

  static bool classof(const MemRegion* R) {
    return R->getKind() == CXXTempObjectRegionKind;
  }
};

// CXXBaseObjectRegion represents a base object within a C++ object. It is
// identified by the base class declaration and the region of its parent object.
class CXXBaseObjectRegion : public TypedValueRegion {
  friend class MemRegionManager;

  llvm::PointerIntPair<const CXXRecordDecl *, 1, bool> Data;

  CXXBaseObjectRegion(const CXXRecordDecl *RD, bool IsVirtual,
                      const SubRegion *SReg)
      : TypedValueRegion(SReg, CXXBaseObjectRegionKind), Data(RD, IsVirtual) {
    assert(RD);
  }

  static void ProfileRegion(llvm::FoldingSetNodeID &ID, const CXXRecordDecl *RD,
                            bool IsVirtual, const MemRegion *SReg);

public:
  const CXXRecordDecl *getDecl() const { return Data.getPointer(); }
  bool isVirtual() const { return Data.getInt(); }

  QualType getValueType() const override;

  void dumpToStream(raw_ostream &os) const override;

  void Profile(llvm::FoldingSetNodeID &ID) const override;

  bool canPrintPrettyAsExpr() const override;

  void printPrettyAsExpr(raw_ostream &os) const override;

  static bool classof(const MemRegion *region) {
    return region->getKind() == CXXBaseObjectRegionKind;
  }
};

// CXXDerivedObjectRegion represents a derived-class object that surrounds
// a C++ object. It is identified by the derived class declaration and the
// region of its parent object. It is a bit counter-intuitive (but not otherwise
// unseen) that this region represents a larger segment of memory that its
// super-region.
class CXXDerivedObjectRegion : public TypedValueRegion {
  friend class MemRegionManager;

  const CXXRecordDecl *DerivedD;

  CXXDerivedObjectRegion(const CXXRecordDecl *DerivedD, const SubRegion *SReg)
      : TypedValueRegion(SReg, CXXDerivedObjectRegionKind), DerivedD(DerivedD) {
    assert(DerivedD);
    // In case of a concrete region, it should always be possible to model
    // the base-to-derived cast by undoing a previous derived-to-base cast,
    // otherwise the cast is most likely ill-formed.
    assert(SReg->getSymbolicBase() &&
           "Should have unwrapped a base region instead!");
  }

  static void ProfileRegion(llvm::FoldingSetNodeID &ID, const CXXRecordDecl *RD,
                            const MemRegion *SReg);

public:
  const CXXRecordDecl *getDecl() const { return DerivedD; }

  QualType getValueType() const override;

  void dumpToStream(raw_ostream &os) const override;

  void Profile(llvm::FoldingSetNodeID &ID) const override;

  bool canPrintPrettyAsExpr() const override;

  void printPrettyAsExpr(raw_ostream &os) const override;

  static bool classof(const MemRegion *region) {
    return region->getKind() == CXXDerivedObjectRegionKind;
  }
};

template<typename RegionTy>
const RegionTy* MemRegion::getAs() const {
  if (const auto *RT = dyn_cast<RegionTy>(this))
    return RT;

  return nullptr;
}

//===----------------------------------------------------------------------===//
// MemRegionManager - Factory object for creating regions.
//===----------------------------------------------------------------------===//

class MemRegionManager {
  ASTContext &C;
  llvm::BumpPtrAllocator& A;
  llvm::FoldingSet<MemRegion> Regions;

  GlobalInternalSpaceRegion *InternalGlobals = nullptr;
  GlobalSystemSpaceRegion *SystemGlobals = nullptr;
  GlobalImmutableSpaceRegion *ImmutableGlobals = nullptr;

  llvm::DenseMap<const StackFrameContext *, StackLocalsSpaceRegion *>
    StackLocalsSpaceRegions;
  llvm::DenseMap<const StackFrameContext *, StackArgumentsSpaceRegion *>
    StackArgumentsSpaceRegions;
  llvm::DenseMap<const CodeTextRegion *, StaticGlobalSpaceRegion *>
    StaticsGlobalSpaceRegions;

  HeapSpaceRegion *heap = nullptr;
  UnknownSpaceRegion *unknown = nullptr;
  CodeSpaceRegion *code = nullptr;

public:
  MemRegionManager(ASTContext &c, llvm::BumpPtrAllocator &a) : C(c), A(a) {}
  ~MemRegionManager();

  ASTContext &getContext() { return C; }

  llvm::BumpPtrAllocator &getAllocator() { return A; }

  /// getStackLocalsRegion - Retrieve the memory region associated with the
  ///  specified stack frame.
  const StackLocalsSpaceRegion *
  getStackLocalsRegion(const StackFrameContext *STC);

  /// getStackArgumentsRegion - Retrieve the memory region associated with
  ///  function/method arguments of the specified stack frame.
  const StackArgumentsSpaceRegion *
  getStackArgumentsRegion(const StackFrameContext *STC);

  /// getGlobalsRegion - Retrieve the memory region associated with
  ///  global variables.
  const GlobalsSpaceRegion *getGlobalsRegion(
      MemRegion::Kind K = MemRegion::GlobalInternalSpaceRegionKind,
      const CodeTextRegion *R = nullptr);

  /// getHeapRegion - Retrieve the memory region associated with the
  ///  generic "heap".
  const HeapSpaceRegion *getHeapRegion();

  /// getUnknownRegion - Retrieve the memory region associated with unknown
  /// memory space.
  const UnknownSpaceRegion *getUnknownRegion();

  const CodeSpaceRegion *getCodeRegion();

  /// getAllocaRegion - Retrieve a region associated with a call to alloca().
  const AllocaRegion *getAllocaRegion(const Expr *Ex, unsigned Cnt,
                                      const LocationContext *LC);

  /// getCompoundLiteralRegion - Retrieve the region associated with a
  ///  given CompoundLiteral.
  const CompoundLiteralRegion*
  getCompoundLiteralRegion(const CompoundLiteralExpr *CL,
                           const LocationContext *LC);

  /// getCXXThisRegion - Retrieve the [artificial] region associated with the
  ///  parameter 'this'.
  const CXXThisRegion *getCXXThisRegion(QualType thisPointerTy,
                                        const LocationContext *LC);

  /// Retrieve or create a "symbolic" memory region.
  const SymbolicRegion* getSymbolicRegion(SymbolRef Sym);

  /// Return a unique symbolic region belonging to heap memory space.
  const SymbolicRegion *getSymbolicHeapRegion(SymbolRef sym);

  const StringRegion *getStringRegion(const StringLiteral *Str);

  const ObjCStringRegion *getObjCStringRegion(const ObjCStringLiteral *Str);

  /// getVarRegion - Retrieve or create the memory region associated with
  ///  a specified VarDecl and LocationContext.
  const VarRegion* getVarRegion(const VarDecl *D, const LocationContext *LC);

  /// getVarRegion - Retrieve or create the memory region associated with
  ///  a specified VarDecl and super region.
  const VarRegion *getVarRegion(const VarDecl *D, const MemRegion *superR);

  /// getElementRegion - Retrieve the memory region associated with the
  ///  associated element type, index, and super region.
  const ElementRegion *getElementRegion(QualType elementType, NonLoc Idx,
                                        const SubRegion *superRegion,
                                        ASTContext &Ctx);

  const ElementRegion *getElementRegionWithSuper(const ElementRegion *ER,
                                                 const SubRegion *superRegion) {
    return getElementRegion(ER->getElementType(), ER->getIndex(),
                            superRegion, ER->getContext());
  }

  /// getFieldRegion - Retrieve or create the memory region associated with
  ///  a specified FieldDecl.  'superRegion' corresponds to the containing
  ///  memory region (which typically represents the memory representing
  ///  a structure or class).
  const FieldRegion *getFieldRegion(const FieldDecl *fd,
                                    const SubRegion* superRegion);

  const FieldRegion *getFieldRegionWithSuper(const FieldRegion *FR,
                                             const SubRegion *superRegion) {
    return getFieldRegion(FR->getDecl(), superRegion);
  }

  /// getObjCIvarRegion - Retrieve or create the memory region associated with
  ///   a specified Objective-c instance variable.  'superRegion' corresponds
  ///   to the containing region (which typically represents the Objective-C
  ///   object).
  const ObjCIvarRegion *getObjCIvarRegion(const ObjCIvarDecl *ivd,
                                          const SubRegion* superRegion);

  const CXXTempObjectRegion *getCXXTempObjectRegion(Expr const *Ex,
                                                    LocationContext const *LC);

  /// Create a CXXBaseObjectRegion with the given base class for region
  /// \p Super.
  ///
  /// The type of \p Super is assumed be a class deriving from \p BaseClass.
  const CXXBaseObjectRegion *
  getCXXBaseObjectRegion(const CXXRecordDecl *BaseClass, const SubRegion *Super,
                         bool IsVirtual);

  /// Create a CXXBaseObjectRegion with the same CXXRecordDecl but a different
  /// super region.
  const CXXBaseObjectRegion *
  getCXXBaseObjectRegionWithSuper(const CXXBaseObjectRegion *baseReg,
                                  const SubRegion *superRegion) {
    return getCXXBaseObjectRegion(baseReg->getDecl(), superRegion,
                                  baseReg->isVirtual());
  }

  /// Create a CXXDerivedObjectRegion with the given derived class for region
  /// \p Super. This should not be used for casting an existing
  /// CXXBaseObjectRegion back to the derived type; instead, CXXBaseObjectRegion
  /// should be removed.
  const CXXDerivedObjectRegion *
  getCXXDerivedObjectRegion(const CXXRecordDecl *BaseClass,
                            const SubRegion *Super);

  const FunctionCodeRegion *getFunctionCodeRegion(const NamedDecl *FD);
  const BlockCodeRegion *getBlockCodeRegion(const BlockDecl *BD,
                                            CanQualType locTy,
                                            AnalysisDeclContext *AC);

  /// getBlockDataRegion - Get the memory region associated with an instance
  ///  of a block.  Unlike many other MemRegions, the LocationContext*
  ///  argument is allowed to be NULL for cases where we have no known
  ///  context.
  const BlockDataRegion *getBlockDataRegion(const BlockCodeRegion *bc,
                                            const LocationContext *lc,
                                            unsigned blockCount);

  /// Create a CXXTempObjectRegion for temporaries which are lifetime-extended
  /// by static references. This differs from getCXXTempObjectRegion in the
  /// super-region used.
  const CXXTempObjectRegion *getCXXStaticTempObjectRegion(const Expr *Ex);

private:
  template <typename RegionTy, typename SuperTy,
            typename Arg1Ty>
  RegionTy* getSubRegion(const Arg1Ty arg1,
                         const SuperTy* superRegion);

  template <typename RegionTy, typename SuperTy,
            typename Arg1Ty, typename Arg2Ty>
  RegionTy* getSubRegion(const Arg1Ty arg1, const Arg2Ty arg2,
                         const SuperTy* superRegion);

  template <typename RegionTy, typename SuperTy,
            typename Arg1Ty, typename Arg2Ty, typename Arg3Ty>
  RegionTy* getSubRegion(const Arg1Ty arg1, const Arg2Ty arg2,
                         const Arg3Ty arg3,
                         const SuperTy* superRegion);

  template <typename REG>
  const REG* LazyAllocate(REG*& region);

  template <typename REG, typename ARG>
  const REG* LazyAllocate(REG*& region, ARG a);
};

//===----------------------------------------------------------------------===//
// Out-of-line member definitions.
//===----------------------------------------------------------------------===//

inline ASTContext &MemRegion::getContext() const {
  return getMemRegionManager()->getContext();
}

//===----------------------------------------------------------------------===//
// Means for storing region/symbol handling traits.
//===----------------------------------------------------------------------===//

/// Information about invalidation for a particular region/symbol.
class RegionAndSymbolInvalidationTraits {
  using StorageTypeForKinds = unsigned char;

  llvm::DenseMap<const MemRegion *, StorageTypeForKinds> MRTraitsMap;
  llvm::DenseMap<SymbolRef, StorageTypeForKinds> SymTraitsMap;

  using const_region_iterator =
      llvm::DenseMap<const MemRegion *, StorageTypeForKinds>::const_iterator;
  using const_symbol_iterator =
      llvm::DenseMap<SymbolRef, StorageTypeForKinds>::const_iterator;

public:
  /// Describes different invalidation traits.
  enum InvalidationKinds {
    /// Tells that a region's contents is not changed.
    TK_PreserveContents = 0x1,

    /// Suppress pointer-escaping of a region.
    TK_SuppressEscape = 0x2,

    // Do not invalidate super region.
    TK_DoNotInvalidateSuperRegion = 0x4,

    /// When applied to a MemSpaceRegion, indicates the entire memory space
    /// should be invalidated.
    TK_EntireMemSpace = 0x8

    // Do not forget to extend StorageTypeForKinds if number of traits exceed
    // the number of bits StorageTypeForKinds can store.
  };

  void setTrait(SymbolRef Sym, InvalidationKinds IK);
  void setTrait(const MemRegion *MR, InvalidationKinds IK);
  bool hasTrait(SymbolRef Sym, InvalidationKinds IK) const;
  bool hasTrait(const MemRegion *MR, InvalidationKinds IK) const;
};

//===----------------------------------------------------------------------===//
// Pretty-printing regions.
//===----------------------------------------------------------------------===//
inline raw_ostream &operator<<(raw_ostream &os, const MemRegion *R) {
  R->dumpToStream(os);
  return os;
}

} // namespace ento

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_MEMREGION_H
