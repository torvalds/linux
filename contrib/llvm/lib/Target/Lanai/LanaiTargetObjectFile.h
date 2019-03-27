//===-- LanaiTargetObjectFile.h - Lanai Object Info -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LANAI_LANAITARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_LANAI_LANAITARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {
class LanaiTargetMachine;
class LanaiTargetObjectFile : public TargetLoweringObjectFileELF {
  MCSection *SmallDataSection;
  MCSection *SmallBSSSection;

  bool isGlobalInSmallSection(const GlobalObject *GO, const TargetMachine &TM,
                              SectionKind Kind) const;
  bool isGlobalInSmallSectionImpl(const GlobalObject *GO,
                                  const TargetMachine &TM) const;

public:
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  /// Return true if this global address should be placed into small data/bss
  /// section.
  bool isGlobalInSmallSection(const GlobalObject *GO,
                              const TargetMachine &TM) const;

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

  /// Return true if this constant should be placed into small data section.
  bool isConstantInSmallSection(const DataLayout &DL, const Constant *CN) const;

  MCSection *getSectionForConstant(const DataLayout &DL, SectionKind Kind,
                                   const Constant *C,
                                   unsigned &Align) const override;
};
} // end namespace llvm

#endif // LLVM_LIB_TARGET_LANAI_LANAITARGETOBJECTFILE_H
