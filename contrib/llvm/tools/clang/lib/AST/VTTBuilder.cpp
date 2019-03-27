//===- VTTBuilder.cpp - C++ VTT layout builder ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with generation of the layout of virtual table
// tables (VTT).
//
//===----------------------------------------------------------------------===//

#include "clang/AST/VTTBuilder.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/BaseSubobject.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstdint>

using namespace clang;

#define DUMP_OVERRIDERS 0

VTTBuilder::VTTBuilder(ASTContext &Ctx,
                       const CXXRecordDecl *MostDerivedClass,
                       bool GenerateDefinition)
    : Ctx(Ctx), MostDerivedClass(MostDerivedClass),
      MostDerivedClassLayout(Ctx.getASTRecordLayout(MostDerivedClass)),
      GenerateDefinition(GenerateDefinition) {
  // Lay out this VTT.
  LayoutVTT(BaseSubobject(MostDerivedClass, CharUnits::Zero()),
            /*BaseIsVirtual=*/false);
}

void VTTBuilder::AddVTablePointer(BaseSubobject Base, uint64_t VTableIndex,
                                  const CXXRecordDecl *VTableClass) {
  // Store the vtable pointer index if we're generating the primary VTT.
  if (VTableClass == MostDerivedClass) {
    assert(!SecondaryVirtualPointerIndices.count(Base) &&
           "A virtual pointer index already exists for this base subobject!");
    SecondaryVirtualPointerIndices[Base] = VTTComponents.size();
  }

  if (!GenerateDefinition) {
    VTTComponents.push_back(VTTComponent());
    return;
  }

  VTTComponents.push_back(VTTComponent(VTableIndex, Base));
}

void VTTBuilder::LayoutSecondaryVTTs(BaseSubobject Base) {
  const CXXRecordDecl *RD = Base.getBase();

  for (const auto &I : RD->bases()) {
    // Don't layout virtual bases.
    if (I.isVirtual())
        continue;

    const CXXRecordDecl *BaseDecl =
      cast<CXXRecordDecl>(I.getType()->getAs<RecordType>()->getDecl());

    const ASTRecordLayout &Layout = Ctx.getASTRecordLayout(RD);
    CharUnits BaseOffset = Base.getBaseOffset() +
      Layout.getBaseClassOffset(BaseDecl);

    // Layout the VTT for this base.
    LayoutVTT(BaseSubobject(BaseDecl, BaseOffset), /*BaseIsVirtual=*/false);
  }
}

void
VTTBuilder::LayoutSecondaryVirtualPointers(BaseSubobject Base,
                                           bool BaseIsMorallyVirtual,
                                           uint64_t VTableIndex,
                                           const CXXRecordDecl *VTableClass,
                                           VisitedVirtualBasesSetTy &VBases) {
  const CXXRecordDecl *RD = Base.getBase();

  // We're not interested in bases that don't have virtual bases, and not
  // morally virtual bases.
  if (!RD->getNumVBases() && !BaseIsMorallyVirtual)
    return;

  for (const auto &I : RD->bases()) {
    const CXXRecordDecl *BaseDecl =
      cast<CXXRecordDecl>(I.getType()->getAs<RecordType>()->getDecl());

    // Itanium C++ ABI 2.6.2:
    //   Secondary virtual pointers are present for all bases with either
    //   virtual bases or virtual function declarations overridden along a
    //   virtual path.
    //
    // If the base class is not dynamic, we don't want to add it, nor any
    // of its base classes.
    if (!BaseDecl->isDynamicClass())
      continue;

    bool BaseDeclIsMorallyVirtual = BaseIsMorallyVirtual;
    bool BaseDeclIsNonVirtualPrimaryBase = false;
    CharUnits BaseOffset;
    if (I.isVirtual()) {
      // Ignore virtual bases that we've already visited.
      if (!VBases.insert(BaseDecl).second)
        continue;

      BaseOffset = MostDerivedClassLayout.getVBaseClassOffset(BaseDecl);
      BaseDeclIsMorallyVirtual = true;
    } else {
      const ASTRecordLayout &Layout = Ctx.getASTRecordLayout(RD);

      BaseOffset = Base.getBaseOffset() +
        Layout.getBaseClassOffset(BaseDecl);

      if (!Layout.isPrimaryBaseVirtual() &&
          Layout.getPrimaryBase() == BaseDecl)
        BaseDeclIsNonVirtualPrimaryBase = true;
    }

    // Itanium C++ ABI 2.6.2:
    //   Secondary virtual pointers: for each base class X which (a) has virtual
    //   bases or is reachable along a virtual path from D, and (b) is not a
    //   non-virtual primary base, the address of the virtual table for X-in-D
    //   or an appropriate construction virtual table.
    if (!BaseDeclIsNonVirtualPrimaryBase &&
        (BaseDecl->getNumVBases() || BaseDeclIsMorallyVirtual)) {
      // Add the vtable pointer.
      AddVTablePointer(BaseSubobject(BaseDecl, BaseOffset), VTableIndex,
                       VTableClass);
    }

    // And lay out the secondary virtual pointers for the base class.
    LayoutSecondaryVirtualPointers(BaseSubobject(BaseDecl, BaseOffset),
                                   BaseDeclIsMorallyVirtual, VTableIndex,
                                   VTableClass, VBases);
  }
}

void
VTTBuilder::LayoutSecondaryVirtualPointers(BaseSubobject Base,
                                           uint64_t VTableIndex) {
  VisitedVirtualBasesSetTy VBases;
  LayoutSecondaryVirtualPointers(Base, /*BaseIsMorallyVirtual=*/false,
                                 VTableIndex, Base.getBase(), VBases);
}

void VTTBuilder::LayoutVirtualVTTs(const CXXRecordDecl *RD,
                                   VisitedVirtualBasesSetTy &VBases) {
  for (const auto &I : RD->bases()) {
    const CXXRecordDecl *BaseDecl =
      cast<CXXRecordDecl>(I.getType()->getAs<RecordType>()->getDecl());

    // Check if this is a virtual base.
    if (I.isVirtual()) {
      // Check if we've seen this base before.
      if (!VBases.insert(BaseDecl).second)
        continue;

      CharUnits BaseOffset =
        MostDerivedClassLayout.getVBaseClassOffset(BaseDecl);

      LayoutVTT(BaseSubobject(BaseDecl, BaseOffset), /*BaseIsVirtual=*/true);
    }

    // We only need to layout virtual VTTs for this base if it actually has
    // virtual bases.
    if (BaseDecl->getNumVBases())
      LayoutVirtualVTTs(BaseDecl, VBases);
  }
}

void VTTBuilder::LayoutVTT(BaseSubobject Base, bool BaseIsVirtual) {
  const CXXRecordDecl *RD = Base.getBase();

  // Itanium C++ ABI 2.6.2:
  //   An array of virtual table addresses, called the VTT, is declared for
  //   each class type that has indirect or direct virtual base classes.
  if (RD->getNumVBases() == 0)
    return;

  bool IsPrimaryVTT = Base.getBase() == MostDerivedClass;

  if (!IsPrimaryVTT) {
    // Remember the sub-VTT index.
    SubVTTIndicies[Base] = VTTComponents.size();
  }

  uint64_t VTableIndex = VTTVTables.size();
  VTTVTables.push_back(VTTVTable(Base, BaseIsVirtual));

  // Add the primary vtable pointer.
  AddVTablePointer(Base, VTableIndex, RD);

  // Add the secondary VTTs.
  LayoutSecondaryVTTs(Base);

  // Add the secondary virtual pointers.
  LayoutSecondaryVirtualPointers(Base, VTableIndex);

  // If this is the primary VTT, we want to lay out virtual VTTs as well.
  if (IsPrimaryVTT) {
    VisitedVirtualBasesSetTy VBases;
    LayoutVirtualVTTs(Base.getBase(), VBases);
  }
}
