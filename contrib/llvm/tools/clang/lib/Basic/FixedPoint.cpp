//===- FixedPoint.cpp - Fixed point constant handling -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the implementation for the fixed point number interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/FixedPoint.h"

namespace clang {

APFixedPoint APFixedPoint::convert(const FixedPointSemantics &DstSema) const {
  llvm::APSInt NewVal = Val;
  unsigned DstWidth = DstSema.getWidth();
  unsigned DstScale = DstSema.getScale();
  bool Upscaling = DstScale > getScale();

  if (Upscaling) {
    NewVal = NewVal.extend(NewVal.getBitWidth() + DstScale - getScale());
    NewVal <<= (DstScale - getScale());
  } else {
    NewVal >>= (getScale() - DstScale);
  }

  if (DstSema.isSaturated()) {
    auto Mask = llvm::APInt::getBitsSetFrom(
        NewVal.getBitWidth(),
        std::min(DstScale + DstSema.getIntegralBits(), NewVal.getBitWidth()));
    llvm::APInt Masked(NewVal & Mask);

    // Change in the bits above the sign
    if (!(Masked == Mask || Masked == 0))
      NewVal = NewVal.isNegative() ? Mask : ~Mask;

    if (!DstSema.isSigned() && NewVal.isNegative())
      NewVal = 0;
  }

  NewVal = NewVal.extOrTrunc(DstWidth);
  NewVal.setIsSigned(DstSema.isSigned());
  return APFixedPoint(NewVal, DstSema);
}

int APFixedPoint::compare(const APFixedPoint &Other) const {
  llvm::APSInt ThisVal = getValue();
  llvm::APSInt OtherVal = Other.getValue();
  bool ThisSigned = Val.isSigned();
  bool OtherSigned = OtherVal.isSigned();
  unsigned OtherScale = Other.getScale();
  unsigned OtherWidth = OtherVal.getBitWidth();

  unsigned CommonWidth = std::max(Val.getBitWidth(), OtherWidth);

  // Prevent overflow in the event the widths are the same but the scales differ
  CommonWidth += getScale() >= OtherScale ? getScale() - OtherScale
                                          : OtherScale - getScale();

  ThisVal = ThisVal.extOrTrunc(CommonWidth);
  OtherVal = OtherVal.extOrTrunc(CommonWidth);

  unsigned CommonScale = std::max(getScale(), OtherScale);
  ThisVal = ThisVal.shl(CommonScale - getScale());
  OtherVal = OtherVal.shl(CommonScale - OtherScale);

  if (ThisSigned && OtherSigned) {
    if (ThisVal.sgt(OtherVal))
      return 1;
    else if (ThisVal.slt(OtherVal))
      return -1;
  } else if (!ThisSigned && !OtherSigned) {
    if (ThisVal.ugt(OtherVal))
      return 1;
    else if (ThisVal.ult(OtherVal))
      return -1;
  } else if (ThisSigned && !OtherSigned) {
    if (ThisVal.isSignBitSet())
      return -1;
    else if (ThisVal.ugt(OtherVal))
      return 1;
    else if (ThisVal.ult(OtherVal))
      return -1;
  } else {
    // !ThisSigned && OtherSigned
    if (OtherVal.isSignBitSet())
      return 1;
    else if (ThisVal.ugt(OtherVal))
      return 1;
    else if (ThisVal.ult(OtherVal))
      return -1;
  }

  return 0;
}

APFixedPoint APFixedPoint::getMax(const FixedPointSemantics &Sema) {
  bool IsUnsigned = !Sema.isSigned();
  auto Val = llvm::APSInt::getMaxValue(Sema.getWidth(), IsUnsigned);
  if (IsUnsigned && Sema.hasUnsignedPadding())
    Val = Val.lshr(1);
  return APFixedPoint(Val, Sema);
}

APFixedPoint APFixedPoint::getMin(const FixedPointSemantics &Sema) {
  auto Val = llvm::APSInt::getMinValue(Sema.getWidth(), !Sema.isSigned());
  return APFixedPoint(Val, Sema);
}

}  // namespace clang
