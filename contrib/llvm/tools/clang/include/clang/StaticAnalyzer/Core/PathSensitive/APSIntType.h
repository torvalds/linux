//== APSIntType.h - Simple record of the type of APSInts --------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_APSINTTYPE_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_APSINTTYPE_H

#include "llvm/ADT/APSInt.h"
#include <tuple>

namespace clang {
namespace ento {

/// A record of the "type" of an APSInt, used for conversions.
class APSIntType {
  uint32_t BitWidth;
  bool IsUnsigned;

public:
  APSIntType(uint32_t Width, bool Unsigned)
    : BitWidth(Width), IsUnsigned(Unsigned) {}

  /* implicit */ APSIntType(const llvm::APSInt &Value)
    : BitWidth(Value.getBitWidth()), IsUnsigned(Value.isUnsigned()) {}

  uint32_t getBitWidth() const { return BitWidth; }
  bool isUnsigned() const { return IsUnsigned; }

  /// Convert a given APSInt, in place, to match this type.
  ///
  /// This behaves like a C cast: converting 255u8 (0xFF) to s16 gives
  /// 255 (0x00FF), and converting -1s8 (0xFF) to u16 gives 65535 (0xFFFF).
  void apply(llvm::APSInt &Value) const {
    // Note the order here. We extend first to preserve the sign, if this value
    // is signed, /then/ match the signedness of the result type.
    Value = Value.extOrTrunc(BitWidth);
    Value.setIsUnsigned(IsUnsigned);
  }

  /// Convert and return a new APSInt with the given value, but this
  /// type's bit width and signedness.
  ///
  /// \see apply
  llvm::APSInt convert(const llvm::APSInt &Value) const LLVM_READONLY {
    llvm::APSInt Result(Value, Value.isUnsigned());
    apply(Result);
    return Result;
  }

  /// Returns an all-zero value for this type.
  llvm::APSInt getZeroValue() const LLVM_READONLY {
    return llvm::APSInt(BitWidth, IsUnsigned);
  }

  /// Returns the minimum value for this type.
  llvm::APSInt getMinValue() const LLVM_READONLY {
    return llvm::APSInt::getMinValue(BitWidth, IsUnsigned);
  }

  /// Returns the maximum value for this type.
  llvm::APSInt getMaxValue() const LLVM_READONLY {
    return llvm::APSInt::getMaxValue(BitWidth, IsUnsigned);
  }

  llvm::APSInt getValue(uint64_t RawValue) const LLVM_READONLY {
    return (llvm::APSInt(BitWidth, IsUnsigned) = RawValue);
  }

  /// Used to classify whether a value is representable using this type.
  ///
  /// \see testInRange
  enum RangeTestResultKind {
    RTR_Below = -1, ///< Value is less than the minimum representable value.
    RTR_Within = 0, ///< Value is representable using this type.
    RTR_Above = 1   ///< Value is greater than the maximum representable value.
  };

  /// Tests whether a given value is losslessly representable using this type.
  ///
  /// \param Val The value to test.
  /// \param AllowMixedSign Whether or not to allow signedness conversions.
  ///                       This determines whether -1s8 is considered in range
  ///                       for 'unsigned char' (u8).
  RangeTestResultKind testInRange(const llvm::APSInt &Val,
                                  bool AllowMixedSign) const LLVM_READONLY;

  bool operator==(const APSIntType &Other) const {
    return BitWidth == Other.BitWidth && IsUnsigned == Other.IsUnsigned;
  }

  /// Provide an ordering for finding a common conversion type.
  ///
  /// Unsigned integers are considered to be better conversion types than
  /// signed integers of the same width.
  bool operator<(const APSIntType &Other) const {
    return std::tie(BitWidth, IsUnsigned) <
           std::tie(Other.BitWidth, Other.IsUnsigned);
  }
};

} // end ento namespace
} // end clang namespace

#endif
