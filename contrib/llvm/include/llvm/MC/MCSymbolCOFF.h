//===- MCSymbolCOFF.h -  ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSYMBOLCOFF_H
#define LLVM_MC_MCSYMBOLCOFF_H

#include "llvm/MC/MCSymbol.h"
#include <cstdint>

namespace llvm {

class MCSymbolCOFF : public MCSymbol {
  /// This corresponds to the e_type field of the COFF symbol.
  mutable uint16_t Type = 0;

  enum SymbolFlags : uint16_t {
    SF_ClassMask = 0x00FF,
    SF_ClassShift = 0,

    SF_WeakExternal = 0x0100,
    SF_SafeSEH = 0x0200,
  };

public:
  MCSymbolCOFF(const StringMapEntry<bool> *Name, bool isTemporary)
      : MCSymbol(SymbolKindCOFF, Name, isTemporary) {}

  uint16_t getType() const {
    return Type;
  }
  void setType(uint16_t Ty) const {
    Type = Ty;
  }

  uint16_t getClass() const {
    return (getFlags() & SF_ClassMask) >> SF_ClassShift;
  }
  void setClass(uint16_t StorageClass) const {
    modifyFlags(StorageClass << SF_ClassShift, SF_ClassMask);
  }

  bool isWeakExternal() const {
    return getFlags() & SF_WeakExternal;
  }
  void setIsWeakExternal() const {
    modifyFlags(SF_WeakExternal, SF_WeakExternal);
  }

  bool isSafeSEH() const {
    return getFlags() & SF_SafeSEH;
  }
  void setIsSafeSEH() const {
    modifyFlags(SF_SafeSEH, SF_SafeSEH);
  }

  static bool classof(const MCSymbol *S) { return S->isCOFF(); }
};

} // end namespace llvm

#endif // LLVM_MC_MCSYMBOLCOFF_H
