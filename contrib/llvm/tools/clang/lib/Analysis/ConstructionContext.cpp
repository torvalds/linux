//===- ConstructionContext.cpp - CFG constructor information --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the ConstructionContext class and its sub-classes,
// which represent various different ways of constructing C++ objects
// with the additional information the users may want to know about
// the constructor.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/ConstructionContext.h"
#include "clang/AST/ExprObjC.h"

using namespace clang;

const ConstructionContextLayer *
ConstructionContextLayer::create(BumpVectorContext &C,
                                 const ConstructionContextItem &Item,
                                 const ConstructionContextLayer *Parent) {
  ConstructionContextLayer *CC =
      C.getAllocator().Allocate<ConstructionContextLayer>();
  return new (CC) ConstructionContextLayer(Item, Parent);
}

bool ConstructionContextLayer::isStrictlyMoreSpecificThan(
    const ConstructionContextLayer *Other) const {
  const ConstructionContextLayer *Self = this;
  while (true) {
    if (!Other)
      return Self;
    if (!Self || !(Self->Item == Other->Item))
      return false;
    Self = Self->getParent();
    Other = Other->getParent();
  }
  llvm_unreachable("The above loop can only be terminated via return!");
}

const ConstructionContext *
ConstructionContext::createMaterializedTemporaryFromLayers(
    BumpVectorContext &C, const MaterializeTemporaryExpr *MTE,
    const CXXBindTemporaryExpr *BTE,
    const ConstructionContextLayer *ParentLayer) {
  assert(MTE);

  // If the object requires destruction and is not lifetime-extended,
  // then it must have a BTE within its MTE, otherwise it shouldn't.
  // FIXME: This should be an assertion.
  if (!BTE && !(MTE->getType().getCanonicalType()->getAsCXXRecordDecl()
                    ->hasTrivialDestructor() ||
                MTE->getStorageDuration() != SD_FullExpression)) {
    return nullptr;
  }

  // If the temporary is lifetime-extended, don't save the BTE,
  // because we don't need a temporary destructor, but an automatic
  // destructor.
  if (MTE->getStorageDuration() != SD_FullExpression) {
    BTE = nullptr;
  }

  // Handle pre-C++17 copy and move elision.
  const CXXConstructExpr *ElidedCE = nullptr;
  const ConstructionContext *ElidedCC = nullptr;
  if (ParentLayer) {
    const ConstructionContextItem &ElidedItem = ParentLayer->getItem();
    assert(ElidedItem.getKind() ==
           ConstructionContextItem::ElidableConstructorKind);
    ElidedCE = cast<CXXConstructExpr>(ElidedItem.getStmt());
    assert(ElidedCE->isElidable());
    // We're creating a construction context that might have already
    // been created elsewhere. Maybe we should unique our construction
    // contexts. That's what we often do, but in this case it's unlikely
    // to bring any benefits.
    ElidedCC = createFromLayers(C, ParentLayer->getParent());
    if (!ElidedCC) {
      // We may fail to create the elided construction context.
      // In this case, skip copy elision entirely.
      return create<SimpleTemporaryObjectConstructionContext>(C, BTE, MTE);
    }
    return create<ElidedTemporaryObjectConstructionContext>(
        C, BTE, MTE, ElidedCE, ElidedCC);
  }

  // This is a normal temporary.
  assert(!ParentLayer);
  return create<SimpleTemporaryObjectConstructionContext>(C, BTE, MTE);
}

const ConstructionContext *ConstructionContext::createBoundTemporaryFromLayers(
    BumpVectorContext &C, const CXXBindTemporaryExpr *BTE,
    const ConstructionContextLayer *ParentLayer) {
  if (!ParentLayer) {
    // A temporary object that doesn't require materialization.
    // In particular, it shouldn't require copy elision, because
    // copy/move constructors take a reference, which requires
    // materialization to obtain the glvalue.
    return create<SimpleTemporaryObjectConstructionContext>(C, BTE,
                                                            /*MTE=*/nullptr);
  }

  const ConstructionContextItem &ParentItem = ParentLayer->getItem();
  switch (ParentItem.getKind()) {
  case ConstructionContextItem::VariableKind: {
    const auto *DS = cast<DeclStmt>(ParentItem.getStmt());
    assert(!cast<VarDecl>(DS->getSingleDecl())->getType().getCanonicalType()
                            ->getAsCXXRecordDecl()->hasTrivialDestructor());
    return create<CXX17ElidedCopyVariableConstructionContext>(C, DS, BTE);
  }
  case ConstructionContextItem::NewAllocatorKind: {
    llvm_unreachable("This context does not accept a bound temporary!");
  }
  case ConstructionContextItem::ReturnKind: {
    assert(ParentLayer->isLast());
    const auto *RS = cast<ReturnStmt>(ParentItem.getStmt());
    assert(!RS->getRetValue()->getType().getCanonicalType()
              ->getAsCXXRecordDecl()->hasTrivialDestructor());
    return create<CXX17ElidedCopyReturnedValueConstructionContext>(C, RS,
                                                                   BTE);
  }

  case ConstructionContextItem::MaterializationKind: {
    // No assert. We may have an elidable copy on the grandparent layer.
    const auto *MTE = cast<MaterializeTemporaryExpr>(ParentItem.getStmt());
    return createMaterializedTemporaryFromLayers(C, MTE, BTE,
                                                 ParentLayer->getParent());
  }
  case ConstructionContextItem::TemporaryDestructorKind: {
    llvm_unreachable("Duplicate CXXBindTemporaryExpr in the AST!");
  }
  case ConstructionContextItem::ElidedDestructorKind: {
    llvm_unreachable("Elided destructor items are not produced by the CFG!");
  }
  case ConstructionContextItem::ElidableConstructorKind: {
    llvm_unreachable("Materialization is necessary to put temporary into a "
                     "copy or move constructor!");
  }
  case ConstructionContextItem::ArgumentKind: {
    assert(ParentLayer->isLast());
    const auto *E = cast<Expr>(ParentItem.getStmt());
    assert(isa<CallExpr>(E) || isa<CXXConstructExpr>(E) ||
           isa<ObjCMessageExpr>(E));
    return create<ArgumentConstructionContext>(C, E, ParentItem.getIndex(),
                                               BTE);
  }
  case ConstructionContextItem::InitializerKind: {
    assert(ParentLayer->isLast());
    const auto *I = ParentItem.getCXXCtorInitializer();
    assert(!I->getAnyMember()->getType().getCanonicalType()
             ->getAsCXXRecordDecl()->hasTrivialDestructor());
    return create<CXX17ElidedCopyConstructorInitializerConstructionContext>(
        C, I, BTE);
  }
  } // switch (ParentItem.getKind())

  llvm_unreachable("Unexpected construction context with destructor!");
}

const ConstructionContext *ConstructionContext::createFromLayers(
    BumpVectorContext &C, const ConstructionContextLayer *TopLayer) {
  // Before this point all we've had was a stockpile of arbitrary layers.
  // Now validate that it is shaped as one of the finite amount of expected
  // patterns.
  const ConstructionContextItem &TopItem = TopLayer->getItem();
  switch (TopItem.getKind()) {
  case ConstructionContextItem::VariableKind: {
    assert(TopLayer->isLast());
    const auto *DS = cast<DeclStmt>(TopItem.getStmt());
    return create<SimpleVariableConstructionContext>(C, DS);
  }
  case ConstructionContextItem::NewAllocatorKind: {
    assert(TopLayer->isLast());
    const auto *NE = cast<CXXNewExpr>(TopItem.getStmt());
    return create<NewAllocatedObjectConstructionContext>(C, NE);
  }
  case ConstructionContextItem::ReturnKind: {
    assert(TopLayer->isLast());
    const auto *RS = cast<ReturnStmt>(TopItem.getStmt());
    return create<SimpleReturnedValueConstructionContext>(C, RS);
  }
  case ConstructionContextItem::MaterializationKind: {
    const auto *MTE = cast<MaterializeTemporaryExpr>(TopItem.getStmt());
    return createMaterializedTemporaryFromLayers(C, MTE, /*BTE=*/nullptr,
                                                 TopLayer->getParent());
  }
  case ConstructionContextItem::TemporaryDestructorKind: {
    const auto *BTE = cast<CXXBindTemporaryExpr>(TopItem.getStmt());
    assert(BTE->getType().getCanonicalType()->getAsCXXRecordDecl()
              ->hasNonTrivialDestructor());
    return createBoundTemporaryFromLayers(C, BTE, TopLayer->getParent());
  }
  case ConstructionContextItem::ElidedDestructorKind: {
    llvm_unreachable("Elided destructor items are not produced by the CFG!");
  }
  case ConstructionContextItem::ElidableConstructorKind: {
    llvm_unreachable("The argument needs to be materialized first!");
  }
  case ConstructionContextItem::InitializerKind: {
    assert(TopLayer->isLast());
    const CXXCtorInitializer *I = TopItem.getCXXCtorInitializer();
    return create<SimpleConstructorInitializerConstructionContext>(C, I);
  }
  case ConstructionContextItem::ArgumentKind: {
    assert(TopLayer->isLast());
    const auto *E = cast<Expr>(TopItem.getStmt());
    return create<ArgumentConstructionContext>(C, E, TopItem.getIndex(),
                                               /*BTE=*/nullptr);
  }
  } // switch (TopItem.getKind())
  llvm_unreachable("Unexpected construction context!");
}
