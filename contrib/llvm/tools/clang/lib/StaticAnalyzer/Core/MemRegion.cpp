//===- MemRegion.cpp - Abstract memory regions for static analysis --------===//
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

#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/Support/BumpVector.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CheckedArithmetic.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

using namespace clang;
using namespace ento;

#define DEBUG_TYPE "MemRegion"

//===----------------------------------------------------------------------===//
// MemRegion Construction.
//===----------------------------------------------------------------------===//

template <typename RegionTy, typename SuperTy, typename Arg1Ty>
RegionTy* MemRegionManager::getSubRegion(const Arg1Ty arg1,
                                         const SuperTy *superRegion) {
  llvm::FoldingSetNodeID ID;
  RegionTy::ProfileRegion(ID, arg1, superRegion);
  void *InsertPos;
  auto *R = cast_or_null<RegionTy>(Regions.FindNodeOrInsertPos(ID, InsertPos));

  if (!R) {
    R = A.Allocate<RegionTy>();
    new (R) RegionTy(arg1, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

template <typename RegionTy, typename SuperTy, typename Arg1Ty, typename Arg2Ty>
RegionTy* MemRegionManager::getSubRegion(const Arg1Ty arg1, const Arg2Ty arg2,
                                         const SuperTy *superRegion) {
  llvm::FoldingSetNodeID ID;
  RegionTy::ProfileRegion(ID, arg1, arg2, superRegion);
  void *InsertPos;
  auto *R = cast_or_null<RegionTy>(Regions.FindNodeOrInsertPos(ID, InsertPos));

  if (!R) {
    R = A.Allocate<RegionTy>();
    new (R) RegionTy(arg1, arg2, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

template <typename RegionTy, typename SuperTy,
          typename Arg1Ty, typename Arg2Ty, typename Arg3Ty>
RegionTy* MemRegionManager::getSubRegion(const Arg1Ty arg1, const Arg2Ty arg2,
                                         const Arg3Ty arg3,
                                         const SuperTy *superRegion) {
  llvm::FoldingSetNodeID ID;
  RegionTy::ProfileRegion(ID, arg1, arg2, arg3, superRegion);
  void *InsertPos;
  auto *R = cast_or_null<RegionTy>(Regions.FindNodeOrInsertPos(ID, InsertPos));

  if (!R) {
    R = A.Allocate<RegionTy>();
    new (R) RegionTy(arg1, arg2, arg3, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

//===----------------------------------------------------------------------===//
// Object destruction.
//===----------------------------------------------------------------------===//

MemRegion::~MemRegion() = default;

// All regions and their data are BumpPtrAllocated.  No need to call their
// destructors.
MemRegionManager::~MemRegionManager() = default;

//===----------------------------------------------------------------------===//
// Basic methods.
//===----------------------------------------------------------------------===//

bool SubRegion::isSubRegionOf(const MemRegion* R) const {
  const MemRegion* r = this;
  do {
    if (r == R)
      return true;
    if (const auto *sr = dyn_cast<SubRegion>(r))
      r = sr->getSuperRegion();
    else
      break;
  } while (r != nullptr);
  return false;
}

MemRegionManager* SubRegion::getMemRegionManager() const {
  const SubRegion* r = this;
  do {
    const MemRegion *superRegion = r->getSuperRegion();
    if (const auto *sr = dyn_cast<SubRegion>(superRegion)) {
      r = sr;
      continue;
    }
    return superRegion->getMemRegionManager();
  } while (true);
}

const StackFrameContext *VarRegion::getStackFrame() const {
  const auto *SSR = dyn_cast<StackSpaceRegion>(getMemorySpace());
  return SSR ? SSR->getStackFrame() : nullptr;
}

//===----------------------------------------------------------------------===//
// Region extents.
//===----------------------------------------------------------------------===//

DefinedOrUnknownSVal TypedValueRegion::getExtent(SValBuilder &svalBuilder) const {
  ASTContext &Ctx = svalBuilder.getContext();
  QualType T = getDesugaredValueType(Ctx);

  if (isa<VariableArrayType>(T))
    return nonloc::SymbolVal(svalBuilder.getSymbolManager().getExtentSymbol(this));
  if (T->isIncompleteType())
    return UnknownVal();

  CharUnits size = Ctx.getTypeSizeInChars(T);
  QualType sizeTy = svalBuilder.getArrayIndexType();
  return svalBuilder.makeIntVal(size.getQuantity(), sizeTy);
}

DefinedOrUnknownSVal FieldRegion::getExtent(SValBuilder &svalBuilder) const {
  // Force callers to deal with bitfields explicitly.
  if (getDecl()->isBitField())
    return UnknownVal();

  DefinedOrUnknownSVal Extent = DeclRegion::getExtent(svalBuilder);

  // A zero-length array at the end of a struct often stands for dynamically-
  // allocated extra memory.
  if (Extent.isZeroConstant()) {
    QualType T = getDesugaredValueType(svalBuilder.getContext());

    if (isa<ConstantArrayType>(T))
      return UnknownVal();
  }

  return Extent;
}

DefinedOrUnknownSVal AllocaRegion::getExtent(SValBuilder &svalBuilder) const {
  return nonloc::SymbolVal(svalBuilder.getSymbolManager().getExtentSymbol(this));
}

DefinedOrUnknownSVal SymbolicRegion::getExtent(SValBuilder &svalBuilder) const {
  return nonloc::SymbolVal(svalBuilder.getSymbolManager().getExtentSymbol(this));
}

DefinedOrUnknownSVal StringRegion::getExtent(SValBuilder &svalBuilder) const {
  return svalBuilder.makeIntVal(getStringLiteral()->getByteLength()+1,
                                svalBuilder.getArrayIndexType());
}

ObjCIvarRegion::ObjCIvarRegion(const ObjCIvarDecl *ivd, const SubRegion *sReg)
    : DeclRegion(ivd, sReg, ObjCIvarRegionKind) {}

const ObjCIvarDecl *ObjCIvarRegion::getDecl() const {
  return cast<ObjCIvarDecl>(D);
}

QualType ObjCIvarRegion::getValueType() const {
  return getDecl()->getType();
}

QualType CXXBaseObjectRegion::getValueType() const {
  return QualType(getDecl()->getTypeForDecl(), 0);
}

QualType CXXDerivedObjectRegion::getValueType() const {
  return QualType(getDecl()->getTypeForDecl(), 0);
}

//===----------------------------------------------------------------------===//
// FoldingSet profiling.
//===----------------------------------------------------------------------===//

void MemSpaceRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.AddInteger(static_cast<unsigned>(getKind()));
}

void StackSpaceRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.AddInteger(static_cast<unsigned>(getKind()));
  ID.AddPointer(getStackFrame());
}

void StaticGlobalSpaceRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.AddInteger(static_cast<unsigned>(getKind()));
  ID.AddPointer(getCodeRegion());
}

void StringRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                 const StringLiteral *Str,
                                 const MemRegion *superRegion) {
  ID.AddInteger(static_cast<unsigned>(StringRegionKind));
  ID.AddPointer(Str);
  ID.AddPointer(superRegion);
}

void ObjCStringRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                     const ObjCStringLiteral *Str,
                                     const MemRegion *superRegion) {
  ID.AddInteger(static_cast<unsigned>(ObjCStringRegionKind));
  ID.AddPointer(Str);
  ID.AddPointer(superRegion);
}

void AllocaRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                 const Expr *Ex, unsigned cnt,
                                 const MemRegion *superRegion) {
  ID.AddInteger(static_cast<unsigned>(AllocaRegionKind));
  ID.AddPointer(Ex);
  ID.AddInteger(cnt);
  ID.AddPointer(superRegion);
}

void AllocaRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  ProfileRegion(ID, Ex, Cnt, superRegion);
}

void CompoundLiteralRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  CompoundLiteralRegion::ProfileRegion(ID, CL, superRegion);
}

void CompoundLiteralRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                          const CompoundLiteralExpr *CL,
                                          const MemRegion* superRegion) {
  ID.AddInteger(static_cast<unsigned>(CompoundLiteralRegionKind));
  ID.AddPointer(CL);
  ID.AddPointer(superRegion);
}

void CXXThisRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                  const PointerType *PT,
                                  const MemRegion *sRegion) {
  ID.AddInteger(static_cast<unsigned>(CXXThisRegionKind));
  ID.AddPointer(PT);
  ID.AddPointer(sRegion);
}

void CXXThisRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  CXXThisRegion::ProfileRegion(ID, ThisPointerTy, superRegion);
}

void ObjCIvarRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                   const ObjCIvarDecl *ivd,
                                   const MemRegion* superRegion) {
  DeclRegion::ProfileRegion(ID, ivd, superRegion, ObjCIvarRegionKind);
}

void DeclRegion::ProfileRegion(llvm::FoldingSetNodeID& ID, const Decl *D,
                               const MemRegion* superRegion, Kind k) {
  ID.AddInteger(static_cast<unsigned>(k));
  ID.AddPointer(D);
  ID.AddPointer(superRegion);
}

void DeclRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  DeclRegion::ProfileRegion(ID, D, superRegion, getKind());
}

void VarRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  VarRegion::ProfileRegion(ID, getDecl(), superRegion);
}

void SymbolicRegion::ProfileRegion(llvm::FoldingSetNodeID& ID, SymbolRef sym,
                                   const MemRegion *sreg) {
  ID.AddInteger(static_cast<unsigned>(MemRegion::SymbolicRegionKind));
  ID.Add(sym);
  ID.AddPointer(sreg);
}

void SymbolicRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  SymbolicRegion::ProfileRegion(ID, sym, getSuperRegion());
}

void ElementRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                  QualType ElementType, SVal Idx,
                                  const MemRegion* superRegion) {
  ID.AddInteger(MemRegion::ElementRegionKind);
  ID.Add(ElementType);
  ID.AddPointer(superRegion);
  Idx.Profile(ID);
}

void ElementRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  ElementRegion::ProfileRegion(ID, ElementType, Index, superRegion);
}

void FunctionCodeRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                       const NamedDecl *FD,
                                       const MemRegion*) {
  ID.AddInteger(MemRegion::FunctionCodeRegionKind);
  ID.AddPointer(FD);
}

void FunctionCodeRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  FunctionCodeRegion::ProfileRegion(ID, FD, superRegion);
}

void BlockCodeRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                    const BlockDecl *BD, CanQualType,
                                    const AnalysisDeclContext *AC,
                                    const MemRegion*) {
  ID.AddInteger(MemRegion::BlockCodeRegionKind);
  ID.AddPointer(BD);
}

void BlockCodeRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  BlockCodeRegion::ProfileRegion(ID, BD, locTy, AC, superRegion);
}

void BlockDataRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                    const BlockCodeRegion *BC,
                                    const LocationContext *LC,
                                    unsigned BlkCount,
                                    const MemRegion *sReg) {
  ID.AddInteger(MemRegion::BlockDataRegionKind);
  ID.AddPointer(BC);
  ID.AddPointer(LC);
  ID.AddInteger(BlkCount);
  ID.AddPointer(sReg);
}

void BlockDataRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  BlockDataRegion::ProfileRegion(ID, BC, LC, BlockCount, getSuperRegion());
}

void CXXTempObjectRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                        Expr const *Ex,
                                        const MemRegion *sReg) {
  ID.AddPointer(Ex);
  ID.AddPointer(sReg);
}

void CXXTempObjectRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, Ex, getSuperRegion());
}

void CXXBaseObjectRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                        const CXXRecordDecl *RD,
                                        bool IsVirtual,
                                        const MemRegion *SReg) {
  ID.AddPointer(RD);
  ID.AddBoolean(IsVirtual);
  ID.AddPointer(SReg);
}

void CXXBaseObjectRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, getDecl(), isVirtual(), superRegion);
}

void CXXDerivedObjectRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                           const CXXRecordDecl *RD,
                                           const MemRegion *SReg) {
  ID.AddPointer(RD);
  ID.AddPointer(SReg);
}

void CXXDerivedObjectRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, getDecl(), superRegion);
}

//===----------------------------------------------------------------------===//
// Region anchors.
//===----------------------------------------------------------------------===//

void GlobalsSpaceRegion::anchor() {}

void NonStaticGlobalSpaceRegion::anchor() {}

void StackSpaceRegion::anchor() {}

void TypedRegion::anchor() {}

void TypedValueRegion::anchor() {}

void CodeTextRegion::anchor() {}

void SubRegion::anchor() {}

//===----------------------------------------------------------------------===//
// Region pretty-printing.
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void MemRegion::dump() const {
  dumpToStream(llvm::errs());
}

std::string MemRegion::getString() const {
  std::string s;
  llvm::raw_string_ostream os(s);
  dumpToStream(os);
  return os.str();
}

void MemRegion::dumpToStream(raw_ostream &os) const {
  os << "<Unknown Region>";
}

void AllocaRegion::dumpToStream(raw_ostream &os) const {
  os << "alloca{S" << Ex->getID(getContext()) << ',' << Cnt << '}';
}

void FunctionCodeRegion::dumpToStream(raw_ostream &os) const {
  os << "code{" << getDecl()->getDeclName().getAsString() << '}';
}

void BlockCodeRegion::dumpToStream(raw_ostream &os) const {
  os << "block_code{" << static_cast<const void *>(this) << '}';
}

void BlockDataRegion::dumpToStream(raw_ostream &os) const {
  os << "block_data{" << BC;
  os << "; ";
  for (BlockDataRegion::referenced_vars_iterator
         I = referenced_vars_begin(),
         E = referenced_vars_end(); I != E; ++I)
    os << "(" << I.getCapturedRegion() << "<-" <<
                 I.getOriginalRegion() << ") ";
  os << '}';
}

void CompoundLiteralRegion::dumpToStream(raw_ostream &os) const {
  // FIXME: More elaborate pretty-printing.
  os << "{ S" << CL->getID(getContext()) <<  " }";
}

void CXXTempObjectRegion::dumpToStream(raw_ostream &os) const {
  os << "temp_object{" << getValueType().getAsString() << ", "
     << "S" << Ex->getID(getContext()) << '}';
}

void CXXBaseObjectRegion::dumpToStream(raw_ostream &os) const {
  os << "Base{" << superRegion << ',' << getDecl()->getName() << '}';
}

void CXXDerivedObjectRegion::dumpToStream(raw_ostream &os) const {
  os << "Derived{" << superRegion << ',' << getDecl()->getName() << '}';
}

void CXXThisRegion::dumpToStream(raw_ostream &os) const {
  os << "this";
}

void ElementRegion::dumpToStream(raw_ostream &os) const {
  os << "Element{" << superRegion << ','
     << Index << ',' << getElementType().getAsString() << '}';
}

void FieldRegion::dumpToStream(raw_ostream &os) const {
  os << superRegion << "->" << *getDecl();
}

void ObjCIvarRegion::dumpToStream(raw_ostream &os) const {
  os << "Ivar{" << superRegion << ',' << *getDecl() << '}';
}

void StringRegion::dumpToStream(raw_ostream &os) const {
  assert(Str != nullptr && "Expecting non-null StringLiteral");
  Str->printPretty(os, nullptr, PrintingPolicy(getContext().getLangOpts()));
}

void ObjCStringRegion::dumpToStream(raw_ostream &os) const {
  assert(Str != nullptr && "Expecting non-null ObjCStringLiteral");
  Str->printPretty(os, nullptr, PrintingPolicy(getContext().getLangOpts()));
}

void SymbolicRegion::dumpToStream(raw_ostream &os) const {
  if (isa<HeapSpaceRegion>(getSuperRegion()))
    os << "Heap";
  os << "SymRegion{" << sym << '}';
}

void VarRegion::dumpToStream(raw_ostream &os) const {
  const auto *VD = cast<VarDecl>(D);
  if (const IdentifierInfo *ID = VD->getIdentifier())
    os << ID->getName();
  else
    os << "VarRegion{D" << VD->getID() << '}';
}

LLVM_DUMP_METHOD void RegionRawOffset::dump() const {
  dumpToStream(llvm::errs());
}

void RegionRawOffset::dumpToStream(raw_ostream &os) const {
  os << "raw_offset{" << getRegion() << ',' << getOffset().getQuantity() << '}';
}

void CodeSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "CodeSpaceRegion";
}

void StaticGlobalSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "StaticGlobalsMemSpace{" << CR << '}';
}

void GlobalInternalSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "GlobalInternalSpaceRegion";
}

void GlobalSystemSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "GlobalSystemSpaceRegion";
}

void GlobalImmutableSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "GlobalImmutableSpaceRegion";
}

void HeapSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "HeapSpaceRegion";
}

void UnknownSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "UnknownSpaceRegion";
}

void StackArgumentsSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "StackArgumentsSpaceRegion";
}

void StackLocalsSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "StackLocalsSpaceRegion";
}

bool MemRegion::canPrintPretty() const {
  return canPrintPrettyAsExpr();
}

bool MemRegion::canPrintPrettyAsExpr() const {
  return false;
}

void MemRegion::printPretty(raw_ostream &os) const {
  assert(canPrintPretty() && "This region cannot be printed pretty.");
  os << "'";
  printPrettyAsExpr(os);
  os << "'";
}

void MemRegion::printPrettyAsExpr(raw_ostream &) const {
  llvm_unreachable("This region cannot be printed pretty.");
}

bool VarRegion::canPrintPrettyAsExpr() const {
  return true;
}

void VarRegion::printPrettyAsExpr(raw_ostream &os) const {
  os << getDecl()->getName();
}

bool ObjCIvarRegion::canPrintPrettyAsExpr() const {
  return true;
}

void ObjCIvarRegion::printPrettyAsExpr(raw_ostream &os) const {
  os << getDecl()->getName();
}

bool FieldRegion::canPrintPretty() const {
  return true;
}

bool FieldRegion::canPrintPrettyAsExpr() const {
  return superRegion->canPrintPrettyAsExpr();
}

void FieldRegion::printPrettyAsExpr(raw_ostream &os) const {
  assert(canPrintPrettyAsExpr());
  superRegion->printPrettyAsExpr(os);
  os << "." << getDecl()->getName();
}

void FieldRegion::printPretty(raw_ostream &os) const {
  if (canPrintPrettyAsExpr()) {
    os << "\'";
    printPrettyAsExpr(os);
    os << "'";
  } else {
    os << "field " << "\'" << getDecl()->getName() << "'";
  }
}

bool CXXBaseObjectRegion::canPrintPrettyAsExpr() const {
  return superRegion->canPrintPrettyAsExpr();
}

void CXXBaseObjectRegion::printPrettyAsExpr(raw_ostream &os) const {
  superRegion->printPrettyAsExpr(os);
}

bool CXXDerivedObjectRegion::canPrintPrettyAsExpr() const {
  return superRegion->canPrintPrettyAsExpr();
}

void CXXDerivedObjectRegion::printPrettyAsExpr(raw_ostream &os) const {
  superRegion->printPrettyAsExpr(os);
}

std::string MemRegion::getDescriptiveName(bool UseQuotes) const {
  std::string VariableName;
  std::string ArrayIndices;
  const MemRegion *R = this;
  SmallString<50> buf;
  llvm::raw_svector_ostream os(buf);

  // Obtain array indices to add them to the variable name.
  const ElementRegion *ER = nullptr;
  while ((ER = R->getAs<ElementRegion>())) {
    // Index is a ConcreteInt.
    if (auto CI = ER->getIndex().getAs<nonloc::ConcreteInt>()) {
      llvm::SmallString<2> Idx;
      CI->getValue().toString(Idx);
      ArrayIndices = (llvm::Twine("[") + Idx.str() + "]" + ArrayIndices).str();
    }
    // If not a ConcreteInt, try to obtain the variable
    // name by calling 'getDescriptiveName' recursively.
    else {
      std::string Idx = ER->getDescriptiveName(false);
      if (!Idx.empty()) {
        ArrayIndices = (llvm::Twine("[") + Idx + "]" + ArrayIndices).str();
      }
    }
    R = ER->getSuperRegion();
  }

  // Get variable name.
  if (R && R->canPrintPrettyAsExpr()) {
    R->printPrettyAsExpr(os);
    if (UseQuotes)
      return (llvm::Twine("'") + os.str() + ArrayIndices + "'").str();
    else
      return (llvm::Twine(os.str()) + ArrayIndices).str();
  }

  return VariableName;
}

SourceRange MemRegion::sourceRange() const {
  const auto *const VR = dyn_cast<VarRegion>(this->getBaseRegion());
  const auto *const FR = dyn_cast<FieldRegion>(this);

  // Check for more specific regions first.
  // FieldRegion
  if (FR) {
    return FR->getDecl()->getSourceRange();
  }
  // VarRegion
  else if (VR) {
    return VR->getDecl()->getSourceRange();
  }
  // Return invalid source range (can be checked by client).
  else
    return {};
}

//===----------------------------------------------------------------------===//
// MemRegionManager methods.
//===----------------------------------------------------------------------===//

template <typename REG>
const REG *MemRegionManager::LazyAllocate(REG*& region) {
  if (!region) {
    region = A.Allocate<REG>();
    new (region) REG(this);
  }

  return region;
}

template <typename REG, typename ARG>
const REG *MemRegionManager::LazyAllocate(REG*& region, ARG a) {
  if (!region) {
    region = A.Allocate<REG>();
    new (region) REG(this, a);
  }

  return region;
}

const StackLocalsSpaceRegion*
MemRegionManager::getStackLocalsRegion(const StackFrameContext *STC) {
  assert(STC);
  StackLocalsSpaceRegion *&R = StackLocalsSpaceRegions[STC];

  if (R)
    return R;

  R = A.Allocate<StackLocalsSpaceRegion>();
  new (R) StackLocalsSpaceRegion(this, STC);
  return R;
}

const StackArgumentsSpaceRegion *
MemRegionManager::getStackArgumentsRegion(const StackFrameContext *STC) {
  assert(STC);
  StackArgumentsSpaceRegion *&R = StackArgumentsSpaceRegions[STC];

  if (R)
    return R;

  R = A.Allocate<StackArgumentsSpaceRegion>();
  new (R) StackArgumentsSpaceRegion(this, STC);
  return R;
}

const GlobalsSpaceRegion
*MemRegionManager::getGlobalsRegion(MemRegion::Kind K,
                                    const CodeTextRegion *CR) {
  if (!CR) {
    if (K == MemRegion::GlobalSystemSpaceRegionKind)
      return LazyAllocate(SystemGlobals);
    if (K == MemRegion::GlobalImmutableSpaceRegionKind)
      return LazyAllocate(ImmutableGlobals);
    assert(K == MemRegion::GlobalInternalSpaceRegionKind);
    return LazyAllocate(InternalGlobals);
  }

  assert(K == MemRegion::StaticGlobalSpaceRegionKind);
  StaticGlobalSpaceRegion *&R = StaticsGlobalSpaceRegions[CR];
  if (R)
    return R;

  R = A.Allocate<StaticGlobalSpaceRegion>();
  new (R) StaticGlobalSpaceRegion(this, CR);
  return R;
}

const HeapSpaceRegion *MemRegionManager::getHeapRegion() {
  return LazyAllocate(heap);
}

const UnknownSpaceRegion *MemRegionManager::getUnknownRegion() {
  return LazyAllocate(unknown);
}

const CodeSpaceRegion *MemRegionManager::getCodeRegion() {
  return LazyAllocate(code);
}

//===----------------------------------------------------------------------===//
// Constructing regions.
//===----------------------------------------------------------------------===//

const StringRegion *MemRegionManager::getStringRegion(const StringLiteral *Str){
  return getSubRegion<StringRegion>(
      Str, cast<GlobalInternalSpaceRegion>(getGlobalsRegion()));
}

const ObjCStringRegion *
MemRegionManager::getObjCStringRegion(const ObjCStringLiteral *Str){
  return getSubRegion<ObjCStringRegion>(
      Str, cast<GlobalInternalSpaceRegion>(getGlobalsRegion()));
}

/// Look through a chain of LocationContexts to either find the
/// StackFrameContext that matches a DeclContext, or find a VarRegion
/// for a variable captured by a block.
static llvm::PointerUnion<const StackFrameContext *, const VarRegion *>
getStackOrCaptureRegionForDeclContext(const LocationContext *LC,
                                      const DeclContext *DC,
                                      const VarDecl *VD) {
  while (LC) {
    if (const auto *SFC = dyn_cast<StackFrameContext>(LC)) {
      if (cast<DeclContext>(SFC->getDecl()) == DC)
        return SFC;
    }
    if (const auto *BC = dyn_cast<BlockInvocationContext>(LC)) {
      const auto *BR =
          static_cast<const BlockDataRegion *>(BC->getContextData());
      // FIXME: This can be made more efficient.
      for (BlockDataRegion::referenced_vars_iterator
           I = BR->referenced_vars_begin(),
           E = BR->referenced_vars_end(); I != E; ++I) {
        const VarRegion *VR = I.getOriginalRegion();
        if (VR->getDecl() == VD)
          return cast<VarRegion>(I.getCapturedRegion());
      }
    }

    LC = LC->getParent();
  }
  return (const StackFrameContext *)nullptr;
}

const VarRegion* MemRegionManager::getVarRegion(const VarDecl *D,
                                                const LocationContext *LC) {
  const MemRegion *sReg = nullptr;

  if (D->hasGlobalStorage() && !D->isStaticLocal()) {

    // First handle the globals defined in system headers.
    if (C.getSourceManager().isInSystemHeader(D->getLocation())) {
      // Whitelist the system globals which often DO GET modified, assume the
      // rest are immutable.
      if (D->getName().find("errno") != StringRef::npos)
        sReg = getGlobalsRegion(MemRegion::GlobalSystemSpaceRegionKind);
      else
        sReg = getGlobalsRegion(MemRegion::GlobalImmutableSpaceRegionKind);

    // Treat other globals as GlobalInternal unless they are constants.
    } else {
      QualType GQT = D->getType();
      const Type *GT = GQT.getTypePtrOrNull();
      // TODO: We could walk the complex types here and see if everything is
      // constified.
      if (GT && GQT.isConstQualified() && GT->isArithmeticType())
        sReg = getGlobalsRegion(MemRegion::GlobalImmutableSpaceRegionKind);
      else
        sReg = getGlobalsRegion();
    }

  // Finally handle static locals.
  } else {
    // FIXME: Once we implement scope handling, we will need to properly lookup
    // 'D' to the proper LocationContext.
    const DeclContext *DC = D->getDeclContext();
    llvm::PointerUnion<const StackFrameContext *, const VarRegion *> V =
      getStackOrCaptureRegionForDeclContext(LC, DC, D);

    if (V.is<const VarRegion*>())
      return V.get<const VarRegion*>();

    const auto *STC = V.get<const StackFrameContext *>();

    if (!STC) {
      // FIXME: Assign a more sensible memory space to static locals
      // we see from within blocks that we analyze as top-level declarations.
      sReg = getUnknownRegion();
    } else {
      if (D->hasLocalStorage()) {
        sReg = isa<ParmVarDecl>(D) || isa<ImplicitParamDecl>(D)
               ? static_cast<const MemRegion*>(getStackArgumentsRegion(STC))
               : static_cast<const MemRegion*>(getStackLocalsRegion(STC));
      }
      else {
        assert(D->isStaticLocal());
        const Decl *STCD = STC->getDecl();
        if (isa<FunctionDecl>(STCD) || isa<ObjCMethodDecl>(STCD))
          sReg = getGlobalsRegion(MemRegion::StaticGlobalSpaceRegionKind,
                                  getFunctionCodeRegion(cast<NamedDecl>(STCD)));
        else if (const auto *BD = dyn_cast<BlockDecl>(STCD)) {
          // FIXME: The fallback type here is totally bogus -- though it should
          // never be queried, it will prevent uniquing with the real
          // BlockCodeRegion. Ideally we'd fix the AST so that we always had a
          // signature.
          QualType T;
          if (const TypeSourceInfo *TSI = BD->getSignatureAsWritten())
            T = TSI->getType();
          if (T.isNull())
            T = getContext().VoidTy;
          if (!T->getAs<FunctionType>())
            T = getContext().getFunctionNoProtoType(T);
          T = getContext().getBlockPointerType(T);

          const BlockCodeRegion *BTR =
            getBlockCodeRegion(BD, C.getCanonicalType(T),
                               STC->getAnalysisDeclContext());
          sReg = getGlobalsRegion(MemRegion::StaticGlobalSpaceRegionKind,
                                  BTR);
        }
        else {
          sReg = getGlobalsRegion();
        }
      }
    }
  }

  return getSubRegion<VarRegion>(D, sReg);
}

const VarRegion *MemRegionManager::getVarRegion(const VarDecl *D,
                                                const MemRegion *superR) {
  return getSubRegion<VarRegion>(D, superR);
}

const BlockDataRegion *
MemRegionManager::getBlockDataRegion(const BlockCodeRegion *BC,
                                     const LocationContext *LC,
                                     unsigned blockCount) {
  const MemSpaceRegion *sReg = nullptr;
  const BlockDecl *BD = BC->getDecl();
  if (!BD->hasCaptures()) {
    // This handles 'static' blocks.
    sReg = getGlobalsRegion(MemRegion::GlobalImmutableSpaceRegionKind);
  }
  else {
    if (LC) {
      // FIXME: Once we implement scope handling, we want the parent region
      // to be the scope.
      const StackFrameContext *STC = LC->getStackFrame();
      assert(STC);
      sReg = getStackLocalsRegion(STC);
    }
    else {
      // We allow 'LC' to be NULL for cases where want BlockDataRegions
      // without context-sensitivity.
      sReg = getUnknownRegion();
    }
  }

  return getSubRegion<BlockDataRegion>(BC, LC, blockCount, sReg);
}

const CXXTempObjectRegion *
MemRegionManager::getCXXStaticTempObjectRegion(const Expr *Ex) {
  return getSubRegion<CXXTempObjectRegion>(
      Ex, getGlobalsRegion(MemRegion::GlobalInternalSpaceRegionKind, nullptr));
}

const CompoundLiteralRegion*
MemRegionManager::getCompoundLiteralRegion(const CompoundLiteralExpr *CL,
                                           const LocationContext *LC) {
  const MemSpaceRegion *sReg = nullptr;

  if (CL->isFileScope())
    sReg = getGlobalsRegion();
  else {
    const StackFrameContext *STC = LC->getStackFrame();
    assert(STC);
    sReg = getStackLocalsRegion(STC);
  }

  return getSubRegion<CompoundLiteralRegion>(CL, sReg);
}

const ElementRegion*
MemRegionManager::getElementRegion(QualType elementType, NonLoc Idx,
                                   const SubRegion* superRegion,
                                   ASTContext &Ctx){
  QualType T = Ctx.getCanonicalType(elementType).getUnqualifiedType();

  llvm::FoldingSetNodeID ID;
  ElementRegion::ProfileRegion(ID, T, Idx, superRegion);

  void *InsertPos;
  MemRegion* data = Regions.FindNodeOrInsertPos(ID, InsertPos);
  auto *R = cast_or_null<ElementRegion>(data);

  if (!R) {
    R = A.Allocate<ElementRegion>();
    new (R) ElementRegion(T, Idx, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

const FunctionCodeRegion *
MemRegionManager::getFunctionCodeRegion(const NamedDecl *FD) {
  return getSubRegion<FunctionCodeRegion>(FD, getCodeRegion());
}

const BlockCodeRegion *
MemRegionManager::getBlockCodeRegion(const BlockDecl *BD, CanQualType locTy,
                                     AnalysisDeclContext *AC) {
  return getSubRegion<BlockCodeRegion>(BD, locTy, AC, getCodeRegion());
}

/// getSymbolicRegion - Retrieve or create a "symbolic" memory region.
const SymbolicRegion *MemRegionManager::getSymbolicRegion(SymbolRef sym) {
  return getSubRegion<SymbolicRegion>(sym, getUnknownRegion());
}

const SymbolicRegion *MemRegionManager::getSymbolicHeapRegion(SymbolRef Sym) {
  return getSubRegion<SymbolicRegion>(Sym, getHeapRegion());
}

const FieldRegion*
MemRegionManager::getFieldRegion(const FieldDecl *d,
                                 const SubRegion* superRegion){
  return getSubRegion<FieldRegion>(d, superRegion);
}

const ObjCIvarRegion*
MemRegionManager::getObjCIvarRegion(const ObjCIvarDecl *d,
                                    const SubRegion* superRegion) {
  return getSubRegion<ObjCIvarRegion>(d, superRegion);
}

const CXXTempObjectRegion*
MemRegionManager::getCXXTempObjectRegion(Expr const *E,
                                         LocationContext const *LC) {
  const StackFrameContext *SFC = LC->getStackFrame();
  assert(SFC);
  return getSubRegion<CXXTempObjectRegion>(E, getStackLocalsRegion(SFC));
}

/// Checks whether \p BaseClass is a valid virtual or direct non-virtual base
/// class of the type of \p Super.
static bool isValidBaseClass(const CXXRecordDecl *BaseClass,
                             const TypedValueRegion *Super,
                             bool IsVirtual) {
  BaseClass = BaseClass->getCanonicalDecl();

  const CXXRecordDecl *Class = Super->getValueType()->getAsCXXRecordDecl();
  if (!Class)
    return true;

  if (IsVirtual)
    return Class->isVirtuallyDerivedFrom(BaseClass);

  for (const auto &I : Class->bases()) {
    if (I.getType()->getAsCXXRecordDecl()->getCanonicalDecl() == BaseClass)
      return true;
  }

  return false;
}

const CXXBaseObjectRegion *
MemRegionManager::getCXXBaseObjectRegion(const CXXRecordDecl *RD,
                                         const SubRegion *Super,
                                         bool IsVirtual) {
  if (isa<TypedValueRegion>(Super)) {
    assert(isValidBaseClass(RD, dyn_cast<TypedValueRegion>(Super), IsVirtual));
    (void)&isValidBaseClass;

    if (IsVirtual) {
      // Virtual base regions should not be layered, since the layout rules
      // are different.
      while (const auto *Base = dyn_cast<CXXBaseObjectRegion>(Super))
        Super = cast<SubRegion>(Base->getSuperRegion());
      assert(Super && !isa<MemSpaceRegion>(Super));
    }
  }

  return getSubRegion<CXXBaseObjectRegion>(RD, IsVirtual, Super);
}

const CXXDerivedObjectRegion *
MemRegionManager::getCXXDerivedObjectRegion(const CXXRecordDecl *RD,
                                            const SubRegion *Super) {
  return getSubRegion<CXXDerivedObjectRegion>(RD, Super);
}

const CXXThisRegion*
MemRegionManager::getCXXThisRegion(QualType thisPointerTy,
                                   const LocationContext *LC) {
  const auto *PT = thisPointerTy->getAs<PointerType>();
  assert(PT);
  // Inside the body of the operator() of a lambda a this expr might refer to an
  // object in one of the parent location contexts.
  const auto *D = dyn_cast<CXXMethodDecl>(LC->getDecl());
  // FIXME: when operator() of lambda is analyzed as a top level function and
  // 'this' refers to a this to the enclosing scope, there is no right region to
  // return.
  while (!LC->inTopFrame() && (!D || D->isStatic() ||
                               PT != D->getThisType()->getAs<PointerType>())) {
    LC = LC->getParent();
    D = dyn_cast<CXXMethodDecl>(LC->getDecl());
  }
  const StackFrameContext *STC = LC->getStackFrame();
  assert(STC);
  return getSubRegion<CXXThisRegion>(PT, getStackArgumentsRegion(STC));
}

const AllocaRegion*
MemRegionManager::getAllocaRegion(const Expr *E, unsigned cnt,
                                  const LocationContext *LC) {
  const StackFrameContext *STC = LC->getStackFrame();
  assert(STC);
  return getSubRegion<AllocaRegion>(E, cnt, getStackLocalsRegion(STC));
}

const MemSpaceRegion *MemRegion::getMemorySpace() const {
  const MemRegion *R = this;
  const auto *SR = dyn_cast<SubRegion>(this);

  while (SR) {
    R = SR->getSuperRegion();
    SR = dyn_cast<SubRegion>(R);
  }

  return dyn_cast<MemSpaceRegion>(R);
}

bool MemRegion::hasStackStorage() const {
  return isa<StackSpaceRegion>(getMemorySpace());
}

bool MemRegion::hasStackNonParametersStorage() const {
  return isa<StackLocalsSpaceRegion>(getMemorySpace());
}

bool MemRegion::hasStackParametersStorage() const {
  return isa<StackArgumentsSpaceRegion>(getMemorySpace());
}

bool MemRegion::hasGlobalsOrParametersStorage() const {
  const MemSpaceRegion *MS = getMemorySpace();
  return isa<StackArgumentsSpaceRegion>(MS) ||
         isa<GlobalsSpaceRegion>(MS);
}

// getBaseRegion strips away all elements and fields, and get the base region
// of them.
const MemRegion *MemRegion::getBaseRegion() const {
  const MemRegion *R = this;
  while (true) {
    switch (R->getKind()) {
      case MemRegion::ElementRegionKind:
      case MemRegion::FieldRegionKind:
      case MemRegion::ObjCIvarRegionKind:
      case MemRegion::CXXBaseObjectRegionKind:
      case MemRegion::CXXDerivedObjectRegionKind:
        R = cast<SubRegion>(R)->getSuperRegion();
        continue;
      default:
        break;
    }
    break;
  }
  return R;
}

// getgetMostDerivedObjectRegion gets the region of the root class of a C++
// class hierarchy.
const MemRegion *MemRegion::getMostDerivedObjectRegion() const {
  const MemRegion *R = this;
  while (const auto *BR = dyn_cast<CXXBaseObjectRegion>(R))
    R = BR->getSuperRegion();
  return R;
}

bool MemRegion::isSubRegionOf(const MemRegion *) const {
  return false;
}

//===----------------------------------------------------------------------===//
// View handling.
//===----------------------------------------------------------------------===//

const MemRegion *MemRegion::StripCasts(bool StripBaseAndDerivedCasts) const {
  const MemRegion *R = this;
  while (true) {
    switch (R->getKind()) {
    case ElementRegionKind: {
      const auto *ER = cast<ElementRegion>(R);
      if (!ER->getIndex().isZeroConstant())
        return R;
      R = ER->getSuperRegion();
      break;
    }
    case CXXBaseObjectRegionKind:
    case CXXDerivedObjectRegionKind:
      if (!StripBaseAndDerivedCasts)
        return R;
      R = cast<TypedValueRegion>(R)->getSuperRegion();
      break;
    default:
      return R;
    }
  }
}

const SymbolicRegion *MemRegion::getSymbolicBase() const {
  const auto *SubR = dyn_cast<SubRegion>(this);

  while (SubR) {
    if (const auto *SymR = dyn_cast<SymbolicRegion>(SubR))
      return SymR;
    SubR = dyn_cast<SubRegion>(SubR->getSuperRegion());
  }
  return nullptr;
}

RegionRawOffset ElementRegion::getAsArrayOffset() const {
  int64_t offset = 0;
  const ElementRegion *ER = this;
  const MemRegion *superR = nullptr;
  ASTContext &C = getContext();

  // FIXME: Handle multi-dimensional arrays.

  while (ER) {
    superR = ER->getSuperRegion();

    // FIXME: generalize to symbolic offsets.
    SVal index = ER->getIndex();
    if (auto CI = index.getAs<nonloc::ConcreteInt>()) {
      // Update the offset.
      int64_t i = CI->getValue().getSExtValue();

      if (i != 0) {
        QualType elemType = ER->getElementType();

        // If we are pointing to an incomplete type, go no further.
        if (elemType->isIncompleteType()) {
          superR = ER;
          break;
        }

        int64_t size = C.getTypeSizeInChars(elemType).getQuantity();
        if (auto NewOffset = llvm::checkedMulAdd(i, size, offset)) {
          offset = *NewOffset;
        } else {
          LLVM_DEBUG(llvm::dbgs() << "MemRegion::getAsArrayOffset: "
                                  << "offset overflowing, returning unknown\n");

          return nullptr;
        }
      }

      // Go to the next ElementRegion (if any).
      ER = dyn_cast<ElementRegion>(superR);
      continue;
    }

    return nullptr;
  }

  assert(superR && "super region cannot be NULL");
  return RegionRawOffset(superR, CharUnits::fromQuantity(offset));
}

/// Returns true if \p Base is an immediate base class of \p Child
static bool isImmediateBase(const CXXRecordDecl *Child,
                            const CXXRecordDecl *Base) {
  assert(Child && "Child must not be null");
  // Note that we do NOT canonicalize the base class here, because
  // ASTRecordLayout doesn't either. If that leads us down the wrong path,
  // so be it; at least we won't crash.
  for (const auto &I : Child->bases()) {
    if (I.getType()->getAsCXXRecordDecl() == Base)
      return true;
  }

  return false;
}

static RegionOffset calculateOffset(const MemRegion *R) {
  const MemRegion *SymbolicOffsetBase = nullptr;
  int64_t Offset = 0;

  while (true) {
    switch (R->getKind()) {
    case MemRegion::CodeSpaceRegionKind:
    case MemRegion::StackLocalsSpaceRegionKind:
    case MemRegion::StackArgumentsSpaceRegionKind:
    case MemRegion::HeapSpaceRegionKind:
    case MemRegion::UnknownSpaceRegionKind:
    case MemRegion::StaticGlobalSpaceRegionKind:
    case MemRegion::GlobalInternalSpaceRegionKind:
    case MemRegion::GlobalSystemSpaceRegionKind:
    case MemRegion::GlobalImmutableSpaceRegionKind:
      // Stores can bind directly to a region space to set a default value.
      assert(Offset == 0 && !SymbolicOffsetBase);
      goto Finish;

    case MemRegion::FunctionCodeRegionKind:
    case MemRegion::BlockCodeRegionKind:
    case MemRegion::BlockDataRegionKind:
      // These will never have bindings, but may end up having values requested
      // if the user does some strange casting.
      if (Offset != 0)
        SymbolicOffsetBase = R;
      goto Finish;

    case MemRegion::SymbolicRegionKind:
    case MemRegion::AllocaRegionKind:
    case MemRegion::CompoundLiteralRegionKind:
    case MemRegion::CXXThisRegionKind:
    case MemRegion::StringRegionKind:
    case MemRegion::ObjCStringRegionKind:
    case MemRegion::VarRegionKind:
    case MemRegion::CXXTempObjectRegionKind:
      // Usual base regions.
      goto Finish;

    case MemRegion::ObjCIvarRegionKind:
      // This is a little strange, but it's a compromise between
      // ObjCIvarRegions having unknown compile-time offsets (when using the
      // non-fragile runtime) and yet still being distinct, non-overlapping
      // regions. Thus we treat them as "like" base regions for the purposes
      // of computing offsets.
      goto Finish;

    case MemRegion::CXXBaseObjectRegionKind: {
      const auto *BOR = cast<CXXBaseObjectRegion>(R);
      R = BOR->getSuperRegion();

      QualType Ty;
      bool RootIsSymbolic = false;
      if (const auto *TVR = dyn_cast<TypedValueRegion>(R)) {
        Ty = TVR->getDesugaredValueType(R->getContext());
      } else if (const auto *SR = dyn_cast<SymbolicRegion>(R)) {
        // If our base region is symbolic, we don't know what type it really is.
        // Pretend the type of the symbol is the true dynamic type.
        // (This will at least be self-consistent for the life of the symbol.)
        Ty = SR->getSymbol()->getType()->getPointeeType();
        RootIsSymbolic = true;
      }

      const CXXRecordDecl *Child = Ty->getAsCXXRecordDecl();
      if (!Child) {
        // We cannot compute the offset of the base class.
        SymbolicOffsetBase = R;
      } else {
        if (RootIsSymbolic) {
          // Base layers on symbolic regions may not be type-correct.
          // Double-check the inheritance here, and revert to a symbolic offset
          // if it's invalid (e.g. due to a reinterpret_cast).
          if (BOR->isVirtual()) {
            if (!Child->isVirtuallyDerivedFrom(BOR->getDecl()))
              SymbolicOffsetBase = R;
          } else {
            if (!isImmediateBase(Child, BOR->getDecl()))
              SymbolicOffsetBase = R;
          }
        }
      }

      // Don't bother calculating precise offsets if we already have a
      // symbolic offset somewhere in the chain.
      if (SymbolicOffsetBase)
        continue;

      CharUnits BaseOffset;
      const ASTRecordLayout &Layout = R->getContext().getASTRecordLayout(Child);
      if (BOR->isVirtual())
        BaseOffset = Layout.getVBaseClassOffset(BOR->getDecl());
      else
        BaseOffset = Layout.getBaseClassOffset(BOR->getDecl());

      // The base offset is in chars, not in bits.
      Offset += BaseOffset.getQuantity() * R->getContext().getCharWidth();
      break;
    }

    case MemRegion::CXXDerivedObjectRegionKind: {
      // TODO: Store the base type in the CXXDerivedObjectRegion and use it.
      goto Finish;
    }

    case MemRegion::ElementRegionKind: {
      const auto *ER = cast<ElementRegion>(R);
      R = ER->getSuperRegion();

      QualType EleTy = ER->getValueType();
      if (EleTy->isIncompleteType()) {
        // We cannot compute the offset of the base class.
        SymbolicOffsetBase = R;
        continue;
      }

      SVal Index = ER->getIndex();
      if (Optional<nonloc::ConcreteInt> CI =
              Index.getAs<nonloc::ConcreteInt>()) {
        // Don't bother calculating precise offsets if we already have a
        // symbolic offset somewhere in the chain.
        if (SymbolicOffsetBase)
          continue;

        int64_t i = CI->getValue().getSExtValue();
        // This type size is in bits.
        Offset += i * R->getContext().getTypeSize(EleTy);
      } else {
        // We cannot compute offset for non-concrete index.
        SymbolicOffsetBase = R;
      }
      break;
    }
    case MemRegion::FieldRegionKind: {
      const auto *FR = cast<FieldRegion>(R);
      R = FR->getSuperRegion();

      const RecordDecl *RD = FR->getDecl()->getParent();
      if (RD->isUnion() || !RD->isCompleteDefinition()) {
        // We cannot compute offset for incomplete type.
        // For unions, we could treat everything as offset 0, but we'd rather
        // treat each field as a symbolic offset so they aren't stored on top
        // of each other, since we depend on things in typed regions actually
        // matching their types.
        SymbolicOffsetBase = R;
      }

      // Don't bother calculating precise offsets if we already have a
      // symbolic offset somewhere in the chain.
      if (SymbolicOffsetBase)
        continue;

      // Get the field number.
      unsigned idx = 0;
      for (RecordDecl::field_iterator FI = RD->field_begin(),
             FE = RD->field_end(); FI != FE; ++FI, ++idx) {
        if (FR->getDecl() == *FI)
          break;
      }
      const ASTRecordLayout &Layout = R->getContext().getASTRecordLayout(RD);
      // This is offset in bits.
      Offset += Layout.getFieldOffset(idx);
      break;
    }
    }
  }

 Finish:
  if (SymbolicOffsetBase)
    return RegionOffset(SymbolicOffsetBase, RegionOffset::Symbolic);
  return RegionOffset(R, Offset);
}

RegionOffset MemRegion::getAsOffset() const {
  if (!cachedOffset)
    cachedOffset = calculateOffset(this);
  return *cachedOffset;
}

//===----------------------------------------------------------------------===//
// BlockDataRegion
//===----------------------------------------------------------------------===//

std::pair<const VarRegion *, const VarRegion *>
BlockDataRegion::getCaptureRegions(const VarDecl *VD) {
  MemRegionManager &MemMgr = *getMemRegionManager();
  const VarRegion *VR = nullptr;
  const VarRegion *OriginalVR = nullptr;

  if (!VD->hasAttr<BlocksAttr>() && VD->hasLocalStorage()) {
    VR = MemMgr.getVarRegion(VD, this);
    OriginalVR = MemMgr.getVarRegion(VD, LC);
  }
  else {
    if (LC) {
      VR = MemMgr.getVarRegion(VD, LC);
      OriginalVR = VR;
    }
    else {
      VR = MemMgr.getVarRegion(VD, MemMgr.getUnknownRegion());
      OriginalVR = MemMgr.getVarRegion(VD, LC);
    }
  }
  return std::make_pair(VR, OriginalVR);
}

void BlockDataRegion::LazyInitializeReferencedVars() {
  if (ReferencedVars)
    return;

  AnalysisDeclContext *AC = getCodeRegion()->getAnalysisDeclContext();
  const auto &ReferencedBlockVars = AC->getReferencedBlockVars(BC->getDecl());
  auto NumBlockVars =
      std::distance(ReferencedBlockVars.begin(), ReferencedBlockVars.end());

  if (NumBlockVars == 0) {
    ReferencedVars = (void*) 0x1;
    return;
  }

  MemRegionManager &MemMgr = *getMemRegionManager();
  llvm::BumpPtrAllocator &A = MemMgr.getAllocator();
  BumpVectorContext BC(A);

  using VarVec = BumpVector<const MemRegion *>;

  auto *BV = A.Allocate<VarVec>();
  new (BV) VarVec(BC, NumBlockVars);
  auto *BVOriginal = A.Allocate<VarVec>();
  new (BVOriginal) VarVec(BC, NumBlockVars);

  for (const auto *VD : ReferencedBlockVars) {
    const VarRegion *VR = nullptr;
    const VarRegion *OriginalVR = nullptr;
    std::tie(VR, OriginalVR) = getCaptureRegions(VD);
    assert(VR);
    assert(OriginalVR);
    BV->push_back(VR, BC);
    BVOriginal->push_back(OriginalVR, BC);
  }

  ReferencedVars = BV;
  OriginalVars = BVOriginal;
}

BlockDataRegion::referenced_vars_iterator
BlockDataRegion::referenced_vars_begin() const {
  const_cast<BlockDataRegion*>(this)->LazyInitializeReferencedVars();

  auto *Vec = static_cast<BumpVector<const MemRegion *> *>(ReferencedVars);

  if (Vec == (void*) 0x1)
    return BlockDataRegion::referenced_vars_iterator(nullptr, nullptr);

  auto *VecOriginal =
      static_cast<BumpVector<const MemRegion *> *>(OriginalVars);

  return BlockDataRegion::referenced_vars_iterator(Vec->begin(),
                                                   VecOriginal->begin());
}

BlockDataRegion::referenced_vars_iterator
BlockDataRegion::referenced_vars_end() const {
  const_cast<BlockDataRegion*>(this)->LazyInitializeReferencedVars();

  auto *Vec = static_cast<BumpVector<const MemRegion *> *>(ReferencedVars);

  if (Vec == (void*) 0x1)
    return BlockDataRegion::referenced_vars_iterator(nullptr, nullptr);

  auto *VecOriginal =
      static_cast<BumpVector<const MemRegion *> *>(OriginalVars);

  return BlockDataRegion::referenced_vars_iterator(Vec->end(),
                                                   VecOriginal->end());
}

const VarRegion *BlockDataRegion::getOriginalRegion(const VarRegion *R) const {
  for (referenced_vars_iterator I = referenced_vars_begin(),
                                E = referenced_vars_end();
       I != E; ++I) {
    if (I.getCapturedRegion() == R)
      return I.getOriginalRegion();
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// RegionAndSymbolInvalidationTraits
//===----------------------------------------------------------------------===//

void RegionAndSymbolInvalidationTraits::setTrait(SymbolRef Sym,
                                                 InvalidationKinds IK) {
  SymTraitsMap[Sym] |= IK;
}

void RegionAndSymbolInvalidationTraits::setTrait(const MemRegion *MR,
                                                 InvalidationKinds IK) {
  assert(MR);
  if (const auto *SR = dyn_cast<SymbolicRegion>(MR))
    setTrait(SR->getSymbol(), IK);
  else
    MRTraitsMap[MR] |= IK;
}

bool RegionAndSymbolInvalidationTraits::hasTrait(SymbolRef Sym,
                                                 InvalidationKinds IK) const {
  const_symbol_iterator I = SymTraitsMap.find(Sym);
  if (I != SymTraitsMap.end())
    return I->second & IK;

  return false;
}

bool RegionAndSymbolInvalidationTraits::hasTrait(const MemRegion *MR,
                                                 InvalidationKinds IK) const {
  if (!MR)
    return false;

  if (const auto *SR = dyn_cast<SymbolicRegion>(MR))
    return hasTrait(SR->getSymbol(), IK);

  const_region_iterator I = MRTraitsMap.find(MR);
  if (I != MRTraitsMap.end())
    return I->second & IK;

  return false;
}
