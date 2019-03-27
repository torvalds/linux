//===- lib/MC/MCSymbolELF.cpp ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSymbolELF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCFixupKindInfo.h"

namespace llvm {

namespace {
enum {
  // Shift value for STT_* flags. 7 possible values. 3 bits.
  ELF_STT_Shift = 0,

  // Shift value for STB_* flags. 4 possible values, 2 bits.
  ELF_STB_Shift = 3,

  // Shift value for STV_* flags. 4 possible values, 2 bits.
  ELF_STV_Shift = 5,

  // Shift value for STO_* flags. 3 bits. All the values are between 0x20 and
  // 0xe0, so we shift right by 5 before storing.
  ELF_STO_Shift = 7,

  // One bit.
  ELF_IsSignature_Shift = 10,

  // One bit.
  ELF_WeakrefUsedInReloc_Shift = 11,

  // One bit.
  ELF_BindingSet_Shift = 12
};
}

void MCSymbolELF::setBinding(unsigned Binding) const {
  setIsBindingSet();
  if (getType() == ELF::STT_SECTION && Binding != ELF::STB_LOCAL)
    setType(ELF::STT_NOTYPE);
  unsigned Val;
  switch (Binding) {
  default:
    llvm_unreachable("Unsupported Binding");
  case ELF::STB_LOCAL:
    Val = 0;
    break;
  case ELF::STB_GLOBAL:
    Val = 1;
    break;
  case ELF::STB_WEAK:
    Val = 2;
    break;
  case ELF::STB_GNU_UNIQUE:
    Val = 3;
    break;
  }
  uint32_t OtherFlags = getFlags() & ~(0x3 << ELF_STB_Shift);
  setFlags(OtherFlags | (Val << ELF_STB_Shift));
}

unsigned MCSymbolELF::getBinding() const {
  if (isBindingSet()) {
    uint32_t Val = (getFlags() & (0x3 << ELF_STB_Shift)) >> ELF_STB_Shift;
    switch (Val) {
    default:
      llvm_unreachable("Invalid value");
    case 0:
      return ELF::STB_LOCAL;
    case 1:
      return ELF::STB_GLOBAL;
    case 2:
      return ELF::STB_WEAK;
    case 3:
      return ELF::STB_GNU_UNIQUE;
    }
  }

  if (isDefined())
    return ELF::STB_LOCAL;
  if (isUsedInReloc())
    return ELF::STB_GLOBAL;
  if (isWeakrefUsedInReloc())
    return ELF::STB_WEAK;
  if (isSignature())
    return ELF::STB_LOCAL;
  return ELF::STB_GLOBAL;
}

void MCSymbolELF::setType(unsigned Type) const {
  unsigned Val;
  if (Type == ELF::STT_SECTION && getBinding() != ELF::STB_LOCAL)
    return;
  switch (Type) {
  default:
    llvm_unreachable("Unsupported Binding");
  case ELF::STT_NOTYPE:
    Val = 0;
    break;
  case ELF::STT_OBJECT:
    Val = 1;
    break;
  case ELF::STT_FUNC:
    Val = 2;
    break;
  case ELF::STT_SECTION:
    Val = 3;
    break;
  case ELF::STT_COMMON:
    Val = 4;
    break;
  case ELF::STT_TLS:
    Val = 5;
    break;
  case ELF::STT_GNU_IFUNC:
    Val = 6;
    break;
  }
  uint32_t OtherFlags = getFlags() & ~(0x7 << ELF_STT_Shift);
  setFlags(OtherFlags | (Val << ELF_STT_Shift));
}

unsigned MCSymbolELF::getType() const {
  uint32_t Val = (getFlags() & (0x7 << ELF_STT_Shift)) >> ELF_STT_Shift;
  switch (Val) {
  default:
    llvm_unreachable("Invalid value");
  case 0:
    return ELF::STT_NOTYPE;
  case 1:
    return ELF::STT_OBJECT;
  case 2:
    return ELF::STT_FUNC;
  case 3:
    return ELF::STT_SECTION;
  case 4:
    return ELF::STT_COMMON;
  case 5:
    return ELF::STT_TLS;
  case 6:
    return ELF::STT_GNU_IFUNC;
  }
}

void MCSymbolELF::setVisibility(unsigned Visibility) {
  assert(Visibility == ELF::STV_DEFAULT || Visibility == ELF::STV_INTERNAL ||
         Visibility == ELF::STV_HIDDEN || Visibility == ELF::STV_PROTECTED);

  uint32_t OtherFlags = getFlags() & ~(0x3 << ELF_STV_Shift);
  setFlags(OtherFlags | (Visibility << ELF_STV_Shift));
}

unsigned MCSymbolELF::getVisibility() const {
  unsigned Visibility = (getFlags() & (0x3 << ELF_STV_Shift)) >> ELF_STV_Shift;
  assert(Visibility == ELF::STV_DEFAULT || Visibility == ELF::STV_INTERNAL ||
         Visibility == ELF::STV_HIDDEN || Visibility == ELF::STV_PROTECTED);
  return Visibility;
}

void MCSymbolELF::setOther(unsigned Other) {
  assert((Other & 0x1f) == 0);
  Other >>= 5;
  assert(Other <= 0x7);
  uint32_t OtherFlags = getFlags() & ~(0x7 << ELF_STO_Shift);
  setFlags(OtherFlags | (Other << ELF_STO_Shift));
}

unsigned MCSymbolELF::getOther() const {
  unsigned Other = (getFlags() & (0x7 << ELF_STO_Shift)) >> ELF_STO_Shift;
  return Other << 5;
}

void MCSymbolELF::setIsWeakrefUsedInReloc() const {
  uint32_t OtherFlags = getFlags() & ~(0x1 << ELF_WeakrefUsedInReloc_Shift);
  setFlags(OtherFlags | (1 << ELF_WeakrefUsedInReloc_Shift));
}

bool MCSymbolELF::isWeakrefUsedInReloc() const {
  return getFlags() & (0x1 << ELF_WeakrefUsedInReloc_Shift);
}

void MCSymbolELF::setIsSignature() const {
  uint32_t OtherFlags = getFlags() & ~(0x1 << ELF_IsSignature_Shift);
  setFlags(OtherFlags | (1 << ELF_IsSignature_Shift));
}

bool MCSymbolELF::isSignature() const {
  return getFlags() & (0x1 << ELF_IsSignature_Shift);
}

void MCSymbolELF::setIsBindingSet() const {
  uint32_t OtherFlags = getFlags() & ~(0x1 << ELF_BindingSet_Shift);
  setFlags(OtherFlags | (1 << ELF_BindingSet_Shift));
}

bool MCSymbolELF::isBindingSet() const {
  return getFlags() & (0x1 << ELF_BindingSet_Shift);
}
}
