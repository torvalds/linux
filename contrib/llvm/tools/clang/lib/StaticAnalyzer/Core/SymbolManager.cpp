//===- SymbolManager.h - Management of Symbolic Values --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines SymbolManager, a class that manages symbolic values
//  created for use by ExprEngine and related classes.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/Store.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace clang;
using namespace ento;

void SymExpr::anchor() {}

LLVM_DUMP_METHOD void SymExpr::dump() const {
  dumpToStream(llvm::errs());
}

void SymIntExpr::dumpToStream(raw_ostream &os) const {
  os << '(';
  getLHS()->dumpToStream(os);
  os << ") "
     << BinaryOperator::getOpcodeStr(getOpcode()) << ' ';
  if (getRHS().isUnsigned())
    os << getRHS().getZExtValue();
  else
    os << getRHS().getSExtValue();
  if (getRHS().isUnsigned())
    os << 'U';
}

void IntSymExpr::dumpToStream(raw_ostream &os) const {
  if (getLHS().isUnsigned())
    os << getLHS().getZExtValue();
  else
    os << getLHS().getSExtValue();
  if (getLHS().isUnsigned())
    os << 'U';
  os << ' '
     << BinaryOperator::getOpcodeStr(getOpcode())
     << " (";
  getRHS()->dumpToStream(os);
  os << ')';
}

void SymSymExpr::dumpToStream(raw_ostream &os) const {
  os << '(';
  getLHS()->dumpToStream(os);
  os << ") "
     << BinaryOperator::getOpcodeStr(getOpcode())
     << " (";
  getRHS()->dumpToStream(os);
  os << ')';
}

void SymbolCast::dumpToStream(raw_ostream &os) const {
  os << '(' << ToTy.getAsString() << ") (";
  Operand->dumpToStream(os);
  os << ')';
}

void SymbolConjured::dumpToStream(raw_ostream &os) const {
  os << "conj_$" << getSymbolID() << '{' << T.getAsString() << ", LC"
     << LCtx->getID();
  if (S)
    os << ", S" << S->getID(LCtx->getDecl()->getASTContext());
  else
    os << ", no stmt";
  os << ", #" << Count << '}';
}

void SymbolDerived::dumpToStream(raw_ostream &os) const {
  os << "derived_$" << getSymbolID() << '{'
     << getParentSymbol() << ',' << getRegion() << '}';
}

void SymbolExtent::dumpToStream(raw_ostream &os) const {
  os << "extent_$" << getSymbolID() << '{' << getRegion() << '}';
}

void SymbolMetadata::dumpToStream(raw_ostream &os) const {
  os << "meta_$" << getSymbolID() << '{'
     << getRegion() << ',' << T.getAsString() << '}';
}

void SymbolData::anchor() {}

void SymbolRegionValue::dumpToStream(raw_ostream &os) const {
  os << "reg_$" << getSymbolID()
     << '<' << getType().getAsString() << ' ' << R << '>';
}

bool SymExpr::symbol_iterator::operator==(const symbol_iterator &X) const {
  return itr == X.itr;
}

bool SymExpr::symbol_iterator::operator!=(const symbol_iterator &X) const {
  return itr != X.itr;
}

SymExpr::symbol_iterator::symbol_iterator(const SymExpr *SE) {
  itr.push_back(SE);
}

SymExpr::symbol_iterator &SymExpr::symbol_iterator::operator++() {
  assert(!itr.empty() && "attempting to iterate on an 'end' iterator");
  expand();
  return *this;
}

SymbolRef SymExpr::symbol_iterator::operator*() {
  assert(!itr.empty() && "attempting to dereference an 'end' iterator");
  return itr.back();
}

void SymExpr::symbol_iterator::expand() {
  const SymExpr *SE = itr.pop_back_val();

  switch (SE->getKind()) {
    case SymExpr::SymbolRegionValueKind:
    case SymExpr::SymbolConjuredKind:
    case SymExpr::SymbolDerivedKind:
    case SymExpr::SymbolExtentKind:
    case SymExpr::SymbolMetadataKind:
      return;
    case SymExpr::SymbolCastKind:
      itr.push_back(cast<SymbolCast>(SE)->getOperand());
      return;
    case SymExpr::SymIntExprKind:
      itr.push_back(cast<SymIntExpr>(SE)->getLHS());
      return;
    case SymExpr::IntSymExprKind:
      itr.push_back(cast<IntSymExpr>(SE)->getRHS());
      return;
    case SymExpr::SymSymExprKind: {
      const auto *x = cast<SymSymExpr>(SE);
      itr.push_back(x->getLHS());
      itr.push_back(x->getRHS());
      return;
    }
  }
  llvm_unreachable("unhandled expansion case");
}

const SymbolRegionValue*
SymbolManager::getRegionValueSymbol(const TypedValueRegion* R) {
  llvm::FoldingSetNodeID profile;
  SymbolRegionValue::Profile(profile, R);
  void *InsertPos;
  SymExpr *SD = DataSet.FindNodeOrInsertPos(profile, InsertPos);
  if (!SD) {
    SD = (SymExpr*) BPAlloc.Allocate<SymbolRegionValue>();
    new (SD) SymbolRegionValue(SymbolCounter, R);
    DataSet.InsertNode(SD, InsertPos);
    ++SymbolCounter;
  }

  return cast<SymbolRegionValue>(SD);
}

const SymbolConjured* SymbolManager::conjureSymbol(const Stmt *E,
                                                   const LocationContext *LCtx,
                                                   QualType T,
                                                   unsigned Count,
                                                   const void *SymbolTag) {
  llvm::FoldingSetNodeID profile;
  SymbolConjured::Profile(profile, E, T, Count, LCtx, SymbolTag);
  void *InsertPos;
  SymExpr *SD = DataSet.FindNodeOrInsertPos(profile, InsertPos);
  if (!SD) {
    SD = (SymExpr*) BPAlloc.Allocate<SymbolConjured>();
    new (SD) SymbolConjured(SymbolCounter, E, LCtx, T, Count, SymbolTag);
    DataSet.InsertNode(SD, InsertPos);
    ++SymbolCounter;
  }

  return cast<SymbolConjured>(SD);
}

const SymbolDerived*
SymbolManager::getDerivedSymbol(SymbolRef parentSymbol,
                                const TypedValueRegion *R) {
  llvm::FoldingSetNodeID profile;
  SymbolDerived::Profile(profile, parentSymbol, R);
  void *InsertPos;
  SymExpr *SD = DataSet.FindNodeOrInsertPos(profile, InsertPos);
  if (!SD) {
    SD = (SymExpr*) BPAlloc.Allocate<SymbolDerived>();
    new (SD) SymbolDerived(SymbolCounter, parentSymbol, R);
    DataSet.InsertNode(SD, InsertPos);
    ++SymbolCounter;
  }

  return cast<SymbolDerived>(SD);
}

const SymbolExtent*
SymbolManager::getExtentSymbol(const SubRegion *R) {
  llvm::FoldingSetNodeID profile;
  SymbolExtent::Profile(profile, R);
  void *InsertPos;
  SymExpr *SD = DataSet.FindNodeOrInsertPos(profile, InsertPos);
  if (!SD) {
    SD = (SymExpr*) BPAlloc.Allocate<SymbolExtent>();
    new (SD) SymbolExtent(SymbolCounter, R);
    DataSet.InsertNode(SD, InsertPos);
    ++SymbolCounter;
  }

  return cast<SymbolExtent>(SD);
}

const SymbolMetadata *
SymbolManager::getMetadataSymbol(const MemRegion* R, const Stmt *S, QualType T,
                                 const LocationContext *LCtx,
                                 unsigned Count, const void *SymbolTag) {
  llvm::FoldingSetNodeID profile;
  SymbolMetadata::Profile(profile, R, S, T, LCtx, Count, SymbolTag);
  void *InsertPos;
  SymExpr *SD = DataSet.FindNodeOrInsertPos(profile, InsertPos);
  if (!SD) {
    SD = (SymExpr*) BPAlloc.Allocate<SymbolMetadata>();
    new (SD) SymbolMetadata(SymbolCounter, R, S, T, LCtx, Count, SymbolTag);
    DataSet.InsertNode(SD, InsertPos);
    ++SymbolCounter;
  }

  return cast<SymbolMetadata>(SD);
}

const SymbolCast*
SymbolManager::getCastSymbol(const SymExpr *Op,
                             QualType From, QualType To) {
  llvm::FoldingSetNodeID ID;
  SymbolCast::Profile(ID, Op, From, To);
  void *InsertPos;
  SymExpr *data = DataSet.FindNodeOrInsertPos(ID, InsertPos);
  if (!data) {
    data = (SymbolCast*) BPAlloc.Allocate<SymbolCast>();
    new (data) SymbolCast(Op, From, To);
    DataSet.InsertNode(data, InsertPos);
  }

  return cast<SymbolCast>(data);
}

const SymIntExpr *SymbolManager::getSymIntExpr(const SymExpr *lhs,
                                               BinaryOperator::Opcode op,
                                               const llvm::APSInt& v,
                                               QualType t) {
  llvm::FoldingSetNodeID ID;
  SymIntExpr::Profile(ID, lhs, op, v, t);
  void *InsertPos;
  SymExpr *data = DataSet.FindNodeOrInsertPos(ID, InsertPos);

  if (!data) {
    data = (SymIntExpr*) BPAlloc.Allocate<SymIntExpr>();
    new (data) SymIntExpr(lhs, op, v, t);
    DataSet.InsertNode(data, InsertPos);
  }

  return cast<SymIntExpr>(data);
}

const IntSymExpr *SymbolManager::getIntSymExpr(const llvm::APSInt& lhs,
                                               BinaryOperator::Opcode op,
                                               const SymExpr *rhs,
                                               QualType t) {
  llvm::FoldingSetNodeID ID;
  IntSymExpr::Profile(ID, lhs, op, rhs, t);
  void *InsertPos;
  SymExpr *data = DataSet.FindNodeOrInsertPos(ID, InsertPos);

  if (!data) {
    data = (IntSymExpr*) BPAlloc.Allocate<IntSymExpr>();
    new (data) IntSymExpr(lhs, op, rhs, t);
    DataSet.InsertNode(data, InsertPos);
  }

  return cast<IntSymExpr>(data);
}

const SymSymExpr *SymbolManager::getSymSymExpr(const SymExpr *lhs,
                                               BinaryOperator::Opcode op,
                                               const SymExpr *rhs,
                                               QualType t) {
  llvm::FoldingSetNodeID ID;
  SymSymExpr::Profile(ID, lhs, op, rhs, t);
  void *InsertPos;
  SymExpr *data = DataSet.FindNodeOrInsertPos(ID, InsertPos);

  if (!data) {
    data = (SymSymExpr*) BPAlloc.Allocate<SymSymExpr>();
    new (data) SymSymExpr(lhs, op, rhs, t);
    DataSet.InsertNode(data, InsertPos);
  }

  return cast<SymSymExpr>(data);
}

QualType SymbolConjured::getType() const {
  return T;
}

QualType SymbolDerived::getType() const {
  return R->getValueType();
}

QualType SymbolExtent::getType() const {
  ASTContext &Ctx = R->getMemRegionManager()->getContext();
  return Ctx.getSizeType();
}

QualType SymbolMetadata::getType() const {
  return T;
}

QualType SymbolRegionValue::getType() const {
  return R->getValueType();
}

SymbolManager::~SymbolManager() {
  llvm::DeleteContainerSeconds(SymbolDependencies);
}

bool SymbolManager::canSymbolicate(QualType T) {
  T = T.getCanonicalType();

  if (Loc::isLocType(T))
    return true;

  if (T->isIntegralOrEnumerationType())
    return true;

  if (T->isRecordType() && !T->isUnionType())
    return true;

  return false;
}

void SymbolManager::addSymbolDependency(const SymbolRef Primary,
                                        const SymbolRef Dependent) {
  SymbolDependTy::iterator I = SymbolDependencies.find(Primary);
  SymbolRefSmallVectorTy *dependencies = nullptr;
  if (I == SymbolDependencies.end()) {
    dependencies = new SymbolRefSmallVectorTy();
    SymbolDependencies[Primary] = dependencies;
  } else {
    dependencies = I->second;
  }
  dependencies->push_back(Dependent);
}

const SymbolRefSmallVectorTy *SymbolManager::getDependentSymbols(
                                                     const SymbolRef Primary) {
  SymbolDependTy::const_iterator I = SymbolDependencies.find(Primary);
  if (I == SymbolDependencies.end())
    return nullptr;
  return I->second;
}

void SymbolReaper::markDependentsLive(SymbolRef sym) {
  // Do not mark dependents more then once.
  SymbolMapTy::iterator LI = TheLiving.find(sym);
  assert(LI != TheLiving.end() && "The primary symbol is not live.");
  if (LI->second == HaveMarkedDependents)
    return;
  LI->second = HaveMarkedDependents;

  if (const SymbolRefSmallVectorTy *Deps = SymMgr.getDependentSymbols(sym)) {
    for (const auto I : *Deps) {
      if (TheLiving.find(I) != TheLiving.end())
        continue;
      markLive(I);
    }
  }
}

void SymbolReaper::markLive(SymbolRef sym) {
  TheLiving[sym] = NotProcessed;
  markDependentsLive(sym);
}

void SymbolReaper::markLive(const MemRegion *region) {
  RegionRoots.insert(region);
  markElementIndicesLive(region);
}

void SymbolReaper::markElementIndicesLive(const MemRegion *region) {
  for (auto SR = dyn_cast<SubRegion>(region); SR;
       SR = dyn_cast<SubRegion>(SR->getSuperRegion())) {
    if (const auto ER = dyn_cast<ElementRegion>(SR)) {
      SVal Idx = ER->getIndex();
      for (auto SI = Idx.symbol_begin(), SE = Idx.symbol_end(); SI != SE; ++SI)
        markLive(*SI);
    }
  }
}

void SymbolReaper::markInUse(SymbolRef sym) {
  if (isa<SymbolMetadata>(sym))
    MetadataInUse.insert(sym);
}

bool SymbolReaper::isLiveRegion(const MemRegion *MR) {
  if (RegionRoots.count(MR))
    return true;

  MR = MR->getBaseRegion();

  if (const auto *SR = dyn_cast<SymbolicRegion>(MR))
    return isLive(SR->getSymbol());

  if (const auto *VR = dyn_cast<VarRegion>(MR))
    return isLive(VR, true);

  // FIXME: This is a gross over-approximation. What we really need is a way to
  // tell if anything still refers to this region. Unlike SymbolicRegions,
  // AllocaRegions don't have associated symbols, though, so we don't actually
  // have a way to track their liveness.
  if (isa<AllocaRegion>(MR))
    return true;

  if (isa<CXXThisRegion>(MR))
    return true;

  if (isa<MemSpaceRegion>(MR))
    return true;

  if (isa<CodeTextRegion>(MR))
    return true;

  return false;
}

bool SymbolReaper::isLive(SymbolRef sym) {
  if (TheLiving.count(sym)) {
    markDependentsLive(sym);
    return true;
  }

  bool KnownLive;

  switch (sym->getKind()) {
  case SymExpr::SymbolRegionValueKind:
    KnownLive = isLiveRegion(cast<SymbolRegionValue>(sym)->getRegion());
    break;
  case SymExpr::SymbolConjuredKind:
    KnownLive = false;
    break;
  case SymExpr::SymbolDerivedKind:
    KnownLive = isLive(cast<SymbolDerived>(sym)->getParentSymbol());
    break;
  case SymExpr::SymbolExtentKind:
    KnownLive = isLiveRegion(cast<SymbolExtent>(sym)->getRegion());
    break;
  case SymExpr::SymbolMetadataKind:
    KnownLive = MetadataInUse.count(sym) &&
                isLiveRegion(cast<SymbolMetadata>(sym)->getRegion());
    if (KnownLive)
      MetadataInUse.erase(sym);
    break;
  case SymExpr::SymIntExprKind:
    KnownLive = isLive(cast<SymIntExpr>(sym)->getLHS());
    break;
  case SymExpr::IntSymExprKind:
    KnownLive = isLive(cast<IntSymExpr>(sym)->getRHS());
    break;
  case SymExpr::SymSymExprKind:
    KnownLive = isLive(cast<SymSymExpr>(sym)->getLHS()) &&
                isLive(cast<SymSymExpr>(sym)->getRHS());
    break;
  case SymExpr::SymbolCastKind:
    KnownLive = isLive(cast<SymbolCast>(sym)->getOperand());
    break;
  }

  if (KnownLive)
    markLive(sym);

  return KnownLive;
}

bool
SymbolReaper::isLive(const Stmt *ExprVal, const LocationContext *ELCtx) const {
  if (LCtx == nullptr)
    return false;

  if (LCtx != ELCtx) {
    // If the reaper's location context is a parent of the expression's
    // location context, then the expression value is now "out of scope".
    if (LCtx->isParentOf(ELCtx))
      return false;
    return true;
  }

  // If no statement is provided, everything is this and parent contexts is live.
  if (!Loc)
    return true;

  return LCtx->getAnalysis<RelaxedLiveVariables>()->isLive(Loc, ExprVal);
}

bool SymbolReaper::isLive(const VarRegion *VR, bool includeStoreBindings) const{
  const StackFrameContext *VarContext = VR->getStackFrame();

  if (!VarContext)
    return true;

  if (!LCtx)
    return false;
  const StackFrameContext *CurrentContext = LCtx->getStackFrame();

  if (VarContext == CurrentContext) {
    // If no statement is provided, everything is live.
    if (!Loc)
      return true;

    if (LCtx->getAnalysis<RelaxedLiveVariables>()->isLive(Loc, VR->getDecl()))
      return true;

    if (!includeStoreBindings)
      return false;

    unsigned &cachedQuery =
      const_cast<SymbolReaper *>(this)->includedRegionCache[VR];

    if (cachedQuery) {
      return cachedQuery == 1;
    }

    // Query the store to see if the region occurs in any live bindings.
    if (Store store = reapedStore.getStore()) {
      bool hasRegion =
        reapedStore.getStoreManager().includedInBindings(store, VR);
      cachedQuery = hasRegion ? 1 : 2;
      return hasRegion;
    }

    return false;
  }

  return VarContext->isParentOf(CurrentContext);
}
