//===-- ubsan_value.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Representation of data which is passed from the compiler-generated calls into
// the ubsan runtime.
//
//===----------------------------------------------------------------------===//
#ifndef UBSAN_VALUE_H
#define UBSAN_VALUE_H

#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"

// FIXME: Move this out to a config header.
#if __SIZEOF_INT128__
__extension__ typedef __int128 s128;
__extension__ typedef unsigned __int128 u128;
#define HAVE_INT128_T 1
#else
#define HAVE_INT128_T 0
#endif

namespace __ubsan {

/// \brief Largest integer types we support.
#if HAVE_INT128_T
typedef s128 SIntMax;
typedef u128 UIntMax;
#else
typedef s64 SIntMax;
typedef u64 UIntMax;
#endif

/// \brief Largest floating-point type we support.
typedef long double FloatMax;

/// \brief A description of a source location. This corresponds to Clang's
/// \c PresumedLoc type.
class SourceLocation {
  const char *Filename;
  u32 Line;
  u32 Column;

public:
  SourceLocation() : Filename(), Line(), Column() {}
  SourceLocation(const char *Filename, unsigned Line, unsigned Column)
    : Filename(Filename), Line(Line), Column(Column) {}

  /// \brief Determine whether the source location is known.
  bool isInvalid() const { return !Filename; }

  /// \brief Atomically acquire a copy, disabling original in-place.
  /// Exactly one call to acquire() returns a copy that isn't disabled.
  SourceLocation acquire() {
    u32 OldColumn = __sanitizer::atomic_exchange(
                        (__sanitizer::atomic_uint32_t *)&Column, ~u32(0),
                        __sanitizer::memory_order_relaxed);
    return SourceLocation(Filename, Line, OldColumn);
  }

  /// \brief Determine if this Location has been disabled.
  /// Disabled SourceLocations are invalid to use.
  bool isDisabled() {
    return Column == ~u32(0);
  }

  /// \brief Get the presumed filename for the source location.
  const char *getFilename() const { return Filename; }
  /// \brief Get the presumed line number.
  unsigned getLine() const { return Line; }
  /// \brief Get the column within the presumed line.
  unsigned getColumn() const { return Column; }
};


/// \brief A description of a type.
class TypeDescriptor {
  /// A value from the \c Kind enumeration, specifying what flavor of type we
  /// have.
  u16 TypeKind;

  /// A \c Type-specific value providing information which allows us to
  /// interpret the meaning of a ValueHandle of this type.
  u16 TypeInfo;

  /// The name of the type follows, in a format suitable for including in
  /// diagnostics.
  char TypeName[1];

public:
  enum Kind {
    /// An integer type. Lowest bit is 1 for a signed value, 0 for an unsigned
    /// value. Remaining bits are log_2(bit width). The value representation is
    /// the integer itself if it fits into a ValueHandle, and a pointer to the
    /// integer otherwise.
    TK_Integer = 0x0000,
    /// A floating-point type. Low 16 bits are bit width. The value
    /// representation is that of bitcasting the floating-point value to an
    /// integer type.
    TK_Float = 0x0001,
    /// Any other type. The value representation is unspecified.
    TK_Unknown = 0xffff
  };

  const char *getTypeName() const { return TypeName; }

  Kind getKind() const {
    return static_cast<Kind>(TypeKind);
  }

  bool isIntegerTy() const { return getKind() == TK_Integer; }
  bool isSignedIntegerTy() const {
    return isIntegerTy() && (TypeInfo & 1);
  }
  bool isUnsignedIntegerTy() const {
    return isIntegerTy() && !(TypeInfo & 1);
  }
  unsigned getIntegerBitWidth() const {
    CHECK(isIntegerTy());
    return 1 << (TypeInfo >> 1);
  }

  bool isFloatTy() const { return getKind() == TK_Float; }
  unsigned getFloatBitWidth() const {
    CHECK(isFloatTy());
    return TypeInfo;
  }
};

/// \brief An opaque handle to a value.
typedef uptr ValueHandle;


/// \brief Representation of an operand value provided by the instrumented code.
///
/// This is a combination of a TypeDescriptor (which is emitted as constant data
/// as an operand to a handler function) and a ValueHandle (which is passed at
/// runtime when a check failure occurs).
class Value {
  /// The type of the value.
  const TypeDescriptor &Type;
  /// The encoded value itself.
  ValueHandle Val;

  /// Is \c Val a (zero-extended) integer?
  bool isInlineInt() const {
    CHECK(getType().isIntegerTy());
    const unsigned InlineBits = sizeof(ValueHandle) * 8;
    const unsigned Bits = getType().getIntegerBitWidth();
    return Bits <= InlineBits;
  }

  /// Is \c Val a (zero-extended) integer representation of a float?
  bool isInlineFloat() const {
    CHECK(getType().isFloatTy());
    const unsigned InlineBits = sizeof(ValueHandle) * 8;
    const unsigned Bits = getType().getFloatBitWidth();
    return Bits <= InlineBits;
  }

public:
  Value(const TypeDescriptor &Type, ValueHandle Val) : Type(Type), Val(Val) {}

  const TypeDescriptor &getType() const { return Type; }

  /// \brief Get this value as a signed integer.
  SIntMax getSIntValue() const;

  /// \brief Get this value as an unsigned integer.
  UIntMax getUIntValue() const;

  /// \brief Decode this value, which must be a positive or unsigned integer.
  UIntMax getPositiveIntValue() const;

  /// Is this an integer with value -1?
  bool isMinusOne() const {
    return getType().isSignedIntegerTy() && getSIntValue() == -1;
  }

  /// Is this a negative integer?
  bool isNegative() const {
    return getType().isSignedIntegerTy() && getSIntValue() < 0;
  }

  /// \brief Get this value as a floating-point quantity.
  FloatMax getFloatValue() const;
};

} // namespace __ubsan

#endif // UBSAN_VALUE_H
