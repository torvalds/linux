//===- VTTBuilder.h - C++ VTT layout builder --------------------*- C++ -*-===//
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

#ifndef LLVM_CLANG_AST_VTTBUILDER_H
#define LLVM_CLANG_AST_VTTBUILDER_H

#include "clang/AST/BaseSubobject.h"
#include "clang/AST/CharUnits.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>

namespace clang {

class ASTContext;
class ASTRecordLayout;
class CXXRecordDecl;

class VTTVTable {
  llvm::PointerIntPair<const CXXRecordDecl *, 1, bool> BaseAndIsVirtual;
  CharUnits BaseOffset;

public:
  VTTVTable() = default;
  VTTVTable(const CXXRecordDecl *Base, CharUnits BaseOffset, bool BaseIsVirtual)
      : BaseAndIsVirtual(Base, BaseIsVirtual), BaseOffset(BaseOffset) {}
  VTTVTable(BaseSubobject Base, bool BaseIsVirtual)
      : BaseAndIsVirtual(Base.getBase(), BaseIsVirtual),
        BaseOffset(Base.getBaseOffset()) {}

  const CXXRecordDecl *getBase() const {
    return BaseAndIsVirtual.getPointer();
  }

  CharUnits getBaseOffset() const {
    return BaseOffset;
  }

  bool isVirtual() const {
    return BaseAndIsVirtual.getInt();
  }

  BaseSubobject getBaseSubobject() const {
    return BaseSubobject(getBase(), getBaseOffset());
  }
};

struct VTTComponent {
  uint64_t VTableIndex;
  BaseSubobject VTableBase;

  VTTComponent() = default;
  VTTComponent(uint64_t VTableIndex, BaseSubobject VTableBase)
     : VTableIndex(VTableIndex), VTableBase(VTableBase) {}
};

/// Class for building VTT layout information.
class VTTBuilder {
  ASTContext &Ctx;

  /// The most derived class for which we're building this vtable.
  const CXXRecordDecl *MostDerivedClass;

  using VTTVTablesVectorTy = SmallVector<VTTVTable, 64>;

  /// The VTT vtables.
  VTTVTablesVectorTy VTTVTables;

  using VTTComponentsVectorTy = SmallVector<VTTComponent, 64>;

  /// The VTT components.
  VTTComponentsVectorTy VTTComponents;

  /// The AST record layout of the most derived class.
  const ASTRecordLayout &MostDerivedClassLayout;

  using VisitedVirtualBasesSetTy = llvm::SmallPtrSet<const CXXRecordDecl *, 4>;

  using AddressPointsMapTy = llvm::DenseMap<BaseSubobject, uint64_t>;

  /// The sub-VTT indices for the bases of the most derived class.
  llvm::DenseMap<BaseSubobject, uint64_t> SubVTTIndicies;

  /// The secondary virtual pointer indices of all subobjects of
  /// the most derived class.
  llvm::DenseMap<BaseSubobject, uint64_t> SecondaryVirtualPointerIndices;

  /// Whether the VTT builder should generate LLVM IR for the VTT.
  bool GenerateDefinition;

  /// Add a vtable pointer to the VTT currently being built.
  void AddVTablePointer(BaseSubobject Base, uint64_t VTableIndex,
                        const CXXRecordDecl *VTableClass);

  /// Lay out the secondary VTTs of the given base subobject.
  void LayoutSecondaryVTTs(BaseSubobject Base);

  /// Lay out the secondary virtual pointers for the given base
  /// subobject.
  ///
  /// \param BaseIsMorallyVirtual whether the base subobject is a virtual base
  /// or a direct or indirect base of a virtual base.
  void LayoutSecondaryVirtualPointers(BaseSubobject Base,
                                      bool BaseIsMorallyVirtual,
                                      uint64_t VTableIndex,
                                      const CXXRecordDecl *VTableClass,
                                      VisitedVirtualBasesSetTy &VBases);

  /// Lay out the secondary virtual pointers for the given base
  /// subobject.
  void LayoutSecondaryVirtualPointers(BaseSubobject Base,
                                      uint64_t VTableIndex);

  /// Lay out the VTTs for the virtual base classes of the given
  /// record declaration.
  void LayoutVirtualVTTs(const CXXRecordDecl *RD,
                         VisitedVirtualBasesSetTy &VBases);

  /// Lay out the VTT for the given subobject, including any
  /// secondary VTTs, secondary virtual pointers and virtual VTTs.
  void LayoutVTT(BaseSubobject Base, bool BaseIsVirtual);

public:
  VTTBuilder(ASTContext &Ctx, const CXXRecordDecl *MostDerivedClass,
             bool GenerateDefinition);

  // Returns a reference to the VTT components.
  const VTTComponentsVectorTy &getVTTComponents() const {
    return VTTComponents;
  }

  // Returns a reference to the VTT vtables.
  const VTTVTablesVectorTy &getVTTVTables() const {
    return VTTVTables;
  }

  /// Returns a reference to the sub-VTT indices.
  const llvm::DenseMap<BaseSubobject, uint64_t> &getSubVTTIndicies() const {
    return SubVTTIndicies;
  }

  /// Returns a reference to the secondary virtual pointer indices.
  const llvm::DenseMap<BaseSubobject, uint64_t> &
  getSecondaryVirtualPointerIndices() const {
    return SecondaryVirtualPointerIndices;
  }
};

} // namespace clang

#endif // LLVM_CLANG_AST_VTTBUILDER_H
