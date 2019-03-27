//===- llvm/ADT/APFloat.h - Arbitrary Precision Floating Point ---*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief
/// This file declares a class to represent arbitrary precision floating point
/// values and provide a variety of arithmetic operations on them.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_APFLOAT_H
#define LLVM_ADT_APFLOAT_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/ErrorHandling.h"
#include <memory>

#define APFLOAT_DISPATCH_ON_SEMANTICS(METHOD_CALL)                             \
  do {                                                                         \
    if (usesLayout<IEEEFloat>(getSemantics()))                                 \
      return U.IEEE.METHOD_CALL;                                               \
    if (usesLayout<DoubleAPFloat>(getSemantics()))                             \
      return U.Double.METHOD_CALL;                                             \
    llvm_unreachable("Unexpected semantics");                                  \
  } while (false)

namespace llvm {

struct fltSemantics;
class APSInt;
class StringRef;
class APFloat;
class raw_ostream;

template <typename T> class SmallVectorImpl;

/// Enum that represents what fraction of the LSB truncated bits of an fp number
/// represent.
///
/// This essentially combines the roles of guard and sticky bits.
enum lostFraction { // Example of truncated bits:
  lfExactlyZero,    // 000000
  lfLessThanHalf,   // 0xxxxx  x's not all zero
  lfExactlyHalf,    // 100000
  lfMoreThanHalf    // 1xxxxx  x's not all zero
};

/// A self-contained host- and target-independent arbitrary-precision
/// floating-point software implementation.
///
/// APFloat uses bignum integer arithmetic as provided by static functions in
/// the APInt class.  The library will work with bignum integers whose parts are
/// any unsigned type at least 16 bits wide, but 64 bits is recommended.
///
/// Written for clarity rather than speed, in particular with a view to use in
/// the front-end of a cross compiler so that target arithmetic can be correctly
/// performed on the host.  Performance should nonetheless be reasonable,
/// particularly for its intended use.  It may be useful as a base
/// implementation for a run-time library during development of a faster
/// target-specific one.
///
/// All 5 rounding modes in the IEEE-754R draft are handled correctly for all
/// implemented operations.  Currently implemented operations are add, subtract,
/// multiply, divide, fused-multiply-add, conversion-to-float,
/// conversion-to-integer and conversion-from-integer.  New rounding modes
/// (e.g. away from zero) can be added with three or four lines of code.
///
/// Four formats are built-in: IEEE single precision, double precision,
/// quadruple precision, and x87 80-bit extended double (when operating with
/// full extended precision).  Adding a new format that obeys IEEE semantics
/// only requires adding two lines of code: a declaration and definition of the
/// format.
///
/// All operations return the status of that operation as an exception bit-mask,
/// so multiple operations can be done consecutively with their results or-ed
/// together.  The returned status can be useful for compiler diagnostics; e.g.,
/// inexact, underflow and overflow can be easily diagnosed on constant folding,
/// and compiler optimizers can determine what exceptions would be raised by
/// folding operations and optimize, or perhaps not optimize, accordingly.
///
/// At present, underflow tininess is detected after rounding; it should be
/// straight forward to add support for the before-rounding case too.
///
/// The library reads hexadecimal floating point numbers as per C99, and
/// correctly rounds if necessary according to the specified rounding mode.
/// Syntax is required to have been validated by the caller.  It also converts
/// floating point numbers to hexadecimal text as per the C99 %a and %A
/// conversions.  The output precision (or alternatively the natural minimal
/// precision) can be specified; if the requested precision is less than the
/// natural precision the output is correctly rounded for the specified rounding
/// mode.
///
/// It also reads decimal floating point numbers and correctly rounds according
/// to the specified rounding mode.
///
/// Conversion to decimal text is not currently implemented.
///
/// Non-zero finite numbers are represented internally as a sign bit, a 16-bit
/// signed exponent, and the significand as an array of integer parts.  After
/// normalization of a number of precision P the exponent is within the range of
/// the format, and if the number is not denormal the P-th bit of the
/// significand is set as an explicit integer bit.  For denormals the most
/// significant bit is shifted right so that the exponent is maintained at the
/// format's minimum, so that the smallest denormal has just the least
/// significant bit of the significand set.  The sign of zeroes and infinities
/// is significant; the exponent and significand of such numbers is not stored,
/// but has a known implicit (deterministic) value: 0 for the significands, 0
/// for zero exponent, all 1 bits for infinity exponent.  For NaNs the sign and
/// significand are deterministic, although not really meaningful, and preserved
/// in non-conversion operations.  The exponent is implicitly all 1 bits.
///
/// APFloat does not provide any exception handling beyond default exception
/// handling. We represent Signaling NaNs via IEEE-754R 2008 6.2.1 should clause
/// by encoding Signaling NaNs with the first bit of its trailing significand as
/// 0.
///
/// TODO
/// ====
///
/// Some features that may or may not be worth adding:
///
/// Binary to decimal conversion (hard).
///
/// Optional ability to detect underflow tininess before rounding.
///
/// New formats: x87 in single and double precision mode (IEEE apart from
/// extended exponent range) (hard).
///
/// New operations: sqrt, IEEE remainder, C90 fmod, nexttoward.
///

// This is the common type definitions shared by APFloat and its internal
// implementation classes. This struct should not define any non-static data
// members.
struct APFloatBase {
  typedef APInt::WordType integerPart;
  static const unsigned integerPartWidth = APInt::APINT_BITS_PER_WORD;

  /// A signed type to represent a floating point numbers unbiased exponent.
  typedef signed short ExponentType;

  /// \name Floating Point Semantics.
  /// @{

  static const fltSemantics &IEEEhalf() LLVM_READNONE;
  static const fltSemantics &IEEEsingle() LLVM_READNONE;
  static const fltSemantics &IEEEdouble() LLVM_READNONE;
  static const fltSemantics &IEEEquad() LLVM_READNONE;
  static const fltSemantics &PPCDoubleDouble() LLVM_READNONE;
  static const fltSemantics &x87DoubleExtended() LLVM_READNONE;

  /// A Pseudo fltsemantic used to construct APFloats that cannot conflict with
  /// anything real.
  static const fltSemantics &Bogus() LLVM_READNONE;

  /// @}

  /// IEEE-754R 5.11: Floating Point Comparison Relations.
  enum cmpResult {
    cmpLessThan,
    cmpEqual,
    cmpGreaterThan,
    cmpUnordered
  };

  /// IEEE-754R 4.3: Rounding-direction attributes.
  enum roundingMode {
    rmNearestTiesToEven,
    rmTowardPositive,
    rmTowardNegative,
    rmTowardZero,
    rmNearestTiesToAway
  };

  /// IEEE-754R 7: Default exception handling.
  ///
  /// opUnderflow or opOverflow are always returned or-ed with opInexact.
  enum opStatus {
    opOK = 0x00,
    opInvalidOp = 0x01,
    opDivByZero = 0x02,
    opOverflow = 0x04,
    opUnderflow = 0x08,
    opInexact = 0x10
  };

  /// Category of internally-represented number.
  enum fltCategory {
    fcInfinity,
    fcNaN,
    fcNormal,
    fcZero
  };

  /// Convenience enum used to construct an uninitialized APFloat.
  enum uninitializedTag {
    uninitialized
  };

  /// Enumeration of \c ilogb error results.
  enum IlogbErrorKinds {
    IEK_Zero = INT_MIN + 1,
    IEK_NaN = INT_MIN,
    IEK_Inf = INT_MAX
  };

  static unsigned int semanticsPrecision(const fltSemantics &);
  static ExponentType semanticsMinExponent(const fltSemantics &);
  static ExponentType semanticsMaxExponent(const fltSemantics &);
  static unsigned int semanticsSizeInBits(const fltSemantics &);

  /// Returns the size of the floating point number (in bits) in the given
  /// semantics.
  static unsigned getSizeInBits(const fltSemantics &Sem);
};

namespace detail {

class IEEEFloat final : public APFloatBase {
public:
  /// \name Constructors
  /// @{

  IEEEFloat(const fltSemantics &); // Default construct to 0.0
  IEEEFloat(const fltSemantics &, integerPart);
  IEEEFloat(const fltSemantics &, uninitializedTag);
  IEEEFloat(const fltSemantics &, const APInt &);
  explicit IEEEFloat(double d);
  explicit IEEEFloat(float f);
  IEEEFloat(const IEEEFloat &);
  IEEEFloat(IEEEFloat &&);
  ~IEEEFloat();

  /// @}

  /// Returns whether this instance allocated memory.
  bool needsCleanup() const { return partCount() > 1; }

  /// \name Convenience "constructors"
  /// @{

  /// @}

  /// \name Arithmetic
  /// @{

  opStatus add(const IEEEFloat &, roundingMode);
  opStatus subtract(const IEEEFloat &, roundingMode);
  opStatus multiply(const IEEEFloat &, roundingMode);
  opStatus divide(const IEEEFloat &, roundingMode);
  /// IEEE remainder.
  opStatus remainder(const IEEEFloat &);
  /// C fmod, or llvm frem.
  opStatus mod(const IEEEFloat &);
  opStatus fusedMultiplyAdd(const IEEEFloat &, const IEEEFloat &, roundingMode);
  opStatus roundToIntegral(roundingMode);
  /// IEEE-754R 5.3.1: nextUp/nextDown.
  opStatus next(bool nextDown);

  /// @}

  /// \name Sign operations.
  /// @{

  void changeSign();

  /// @}

  /// \name Conversions
  /// @{

  opStatus convert(const fltSemantics &, roundingMode, bool *);
  opStatus convertToInteger(MutableArrayRef<integerPart>, unsigned int, bool,
                            roundingMode, bool *) const;
  opStatus convertFromAPInt(const APInt &, bool, roundingMode);
  opStatus convertFromSignExtendedInteger(const integerPart *, unsigned int,
                                          bool, roundingMode);
  opStatus convertFromZeroExtendedInteger(const integerPart *, unsigned int,
                                          bool, roundingMode);
  opStatus convertFromString(StringRef, roundingMode);
  APInt bitcastToAPInt() const;
  double convertToDouble() const;
  float convertToFloat() const;

  /// @}

  /// The definition of equality is not straightforward for floating point, so
  /// we won't use operator==.  Use one of the following, or write whatever it
  /// is you really mean.
  bool operator==(const IEEEFloat &) const = delete;

  /// IEEE comparison with another floating point number (NaNs compare
  /// unordered, 0==-0).
  cmpResult compare(const IEEEFloat &) const;

  /// Bitwise comparison for equality (QNaNs compare equal, 0!=-0).
  bool bitwiseIsEqual(const IEEEFloat &) const;

  /// Write out a hexadecimal representation of the floating point value to DST,
  /// which must be of sufficient size, in the C99 form [-]0xh.hhhhp[+-]d.
  /// Return the number of characters written, excluding the terminating NUL.
  unsigned int convertToHexString(char *dst, unsigned int hexDigits,
                                  bool upperCase, roundingMode) const;

  /// \name IEEE-754R 5.7.2 General operations.
  /// @{

  /// IEEE-754R isSignMinus: Returns true if and only if the current value is
  /// negative.
  ///
  /// This applies to zeros and NaNs as well.
  bool isNegative() const { return sign; }

  /// IEEE-754R isNormal: Returns true if and only if the current value is normal.
  ///
  /// This implies that the current value of the float is not zero, subnormal,
  /// infinite, or NaN following the definition of normality from IEEE-754R.
  bool isNormal() const { return !isDenormal() && isFiniteNonZero(); }

  /// Returns true if and only if the current value is zero, subnormal, or
  /// normal.
  ///
  /// This means that the value is not infinite or NaN.
  bool isFinite() const { return !isNaN() && !isInfinity(); }

  /// Returns true if and only if the float is plus or minus zero.
  bool isZero() const { return category == fcZero; }

  /// IEEE-754R isSubnormal(): Returns true if and only if the float is a
  /// denormal.
  bool isDenormal() const;

  /// IEEE-754R isInfinite(): Returns true if and only if the float is infinity.
  bool isInfinity() const { return category == fcInfinity; }

  /// Returns true if and only if the float is a quiet or signaling NaN.
  bool isNaN() const { return category == fcNaN; }

  /// Returns true if and only if the float is a signaling NaN.
  bool isSignaling() const;

  /// @}

  /// \name Simple Queries
  /// @{

  fltCategory getCategory() const { return category; }
  const fltSemantics &getSemantics() const { return *semantics; }
  bool isNonZero() const { return category != fcZero; }
  bool isFiniteNonZero() const { return isFinite() && !isZero(); }
  bool isPosZero() const { return isZero() && !isNegative(); }
  bool isNegZero() const { return isZero() && isNegative(); }

  /// Returns true if and only if the number has the smallest possible non-zero
  /// magnitude in the current semantics.
  bool isSmallest() const;

  /// Returns true if and only if the number has the largest possible finite
  /// magnitude in the current semantics.
  bool isLargest() const;

  /// Returns true if and only if the number is an exact integer.
  bool isInteger() const;

  /// @}

  IEEEFloat &operator=(const IEEEFloat &);
  IEEEFloat &operator=(IEEEFloat &&);

  /// Overload to compute a hash code for an APFloat value.
  ///
  /// Note that the use of hash codes for floating point values is in general
  /// frought with peril. Equality is hard to define for these values. For
  /// example, should negative and positive zero hash to different codes? Are
  /// they equal or not? This hash value implementation specifically
  /// emphasizes producing different codes for different inputs in order to
  /// be used in canonicalization and memoization. As such, equality is
  /// bitwiseIsEqual, and 0 != -0.
  friend hash_code hash_value(const IEEEFloat &Arg);

  /// Converts this value into a decimal string.
  ///
  /// \param FormatPrecision The maximum number of digits of
  ///   precision to output.  If there are fewer digits available,
  ///   zero padding will not be used unless the value is
  ///   integral and small enough to be expressed in
  ///   FormatPrecision digits.  0 means to use the natural
  ///   precision of the number.
  /// \param FormatMaxPadding The maximum number of zeros to
  ///   consider inserting before falling back to scientific
  ///   notation.  0 means to always use scientific notation.
  ///
  /// \param TruncateZero Indicate whether to remove the trailing zero in
  ///   fraction part or not. Also setting this parameter to false forcing
  ///   producing of output more similar to default printf behavior.
  ///   Specifically the lower e is used as exponent delimiter and exponent
  ///   always contains no less than two digits.
  ///
  /// Number       Precision    MaxPadding      Result
  /// ------       ---------    ----------      ------
  /// 1.01E+4              5             2       10100
  /// 1.01E+4              4             2       1.01E+4
  /// 1.01E+4              5             1       1.01E+4
  /// 1.01E-2              5             2       0.0101
  /// 1.01E-2              4             2       0.0101
  /// 1.01E-2              4             1       1.01E-2
  void toString(SmallVectorImpl<char> &Str, unsigned FormatPrecision = 0,
                unsigned FormatMaxPadding = 3, bool TruncateZero = true) const;

  /// If this value has an exact multiplicative inverse, store it in inv and
  /// return true.
  bool getExactInverse(APFloat *inv) const;

  /// Returns the exponent of the internal representation of the APFloat.
  ///
  /// Because the radix of APFloat is 2, this is equivalent to floor(log2(x)).
  /// For special APFloat values, this returns special error codes:
  ///
  ///   NaN -> \c IEK_NaN
  ///   0   -> \c IEK_Zero
  ///   Inf -> \c IEK_Inf
  ///
  friend int ilogb(const IEEEFloat &Arg);

  /// Returns: X * 2^Exp for integral exponents.
  friend IEEEFloat scalbn(IEEEFloat X, int Exp, roundingMode);

  friend IEEEFloat frexp(const IEEEFloat &X, int &Exp, roundingMode);

  /// \name Special value setters.
  /// @{

  void makeLargest(bool Neg = false);
  void makeSmallest(bool Neg = false);
  void makeNaN(bool SNaN = false, bool Neg = false,
               const APInt *fill = nullptr);
  void makeInf(bool Neg = false);
  void makeZero(bool Neg = false);
  void makeQuiet();

  /// Returns the smallest (by magnitude) normalized finite number in the given
  /// semantics.
  ///
  /// \param Negative - True iff the number should be negative
  void makeSmallestNormalized(bool Negative = false);

  /// @}

  cmpResult compareAbsoluteValue(const IEEEFloat &) const;

private:
  /// \name Simple Queries
  /// @{

  integerPart *significandParts();
  const integerPart *significandParts() const;
  unsigned int partCount() const;

  /// @}

  /// \name Significand operations.
  /// @{

  integerPart addSignificand(const IEEEFloat &);
  integerPart subtractSignificand(const IEEEFloat &, integerPart);
  lostFraction addOrSubtractSignificand(const IEEEFloat &, bool subtract);
  lostFraction multiplySignificand(const IEEEFloat &, const IEEEFloat *);
  lostFraction divideSignificand(const IEEEFloat &);
  void incrementSignificand();
  void initialize(const fltSemantics *);
  void shiftSignificandLeft(unsigned int);
  lostFraction shiftSignificandRight(unsigned int);
  unsigned int significandLSB() const;
  unsigned int significandMSB() const;
  void zeroSignificand();
  /// Return true if the significand excluding the integral bit is all ones.
  bool isSignificandAllOnes() const;
  /// Return true if the significand excluding the integral bit is all zeros.
  bool isSignificandAllZeros() const;

  /// @}

  /// \name Arithmetic on special values.
  /// @{

  opStatus addOrSubtractSpecials(const IEEEFloat &, bool subtract);
  opStatus divideSpecials(const IEEEFloat &);
  opStatus multiplySpecials(const IEEEFloat &);
  opStatus modSpecials(const IEEEFloat &);

  /// @}

  /// \name Miscellany
  /// @{

  bool convertFromStringSpecials(StringRef str);
  opStatus normalize(roundingMode, lostFraction);
  opStatus addOrSubtract(const IEEEFloat &, roundingMode, bool subtract);
  opStatus handleOverflow(roundingMode);
  bool roundAwayFromZero(roundingMode, lostFraction, unsigned int) const;
  opStatus convertToSignExtendedInteger(MutableArrayRef<integerPart>,
                                        unsigned int, bool, roundingMode,
                                        bool *) const;
  opStatus convertFromUnsignedParts(const integerPart *, unsigned int,
                                    roundingMode);
  opStatus convertFromHexadecimalString(StringRef, roundingMode);
  opStatus convertFromDecimalString(StringRef, roundingMode);
  char *convertNormalToHexString(char *, unsigned int, bool,
                                 roundingMode) const;
  opStatus roundSignificandWithExponent(const integerPart *, unsigned int, int,
                                        roundingMode);

  /// @}

  APInt convertHalfAPFloatToAPInt() const;
  APInt convertFloatAPFloatToAPInt() const;
  APInt convertDoubleAPFloatToAPInt() const;
  APInt convertQuadrupleAPFloatToAPInt() const;
  APInt convertF80LongDoubleAPFloatToAPInt() const;
  APInt convertPPCDoubleDoubleAPFloatToAPInt() const;
  void initFromAPInt(const fltSemantics *Sem, const APInt &api);
  void initFromHalfAPInt(const APInt &api);
  void initFromFloatAPInt(const APInt &api);
  void initFromDoubleAPInt(const APInt &api);
  void initFromQuadrupleAPInt(const APInt &api);
  void initFromF80LongDoubleAPInt(const APInt &api);
  void initFromPPCDoubleDoubleAPInt(const APInt &api);

  void assign(const IEEEFloat &);
  void copySignificand(const IEEEFloat &);
  void freeSignificand();

  /// Note: this must be the first data member.
  /// The semantics that this value obeys.
  const fltSemantics *semantics;

  /// A binary fraction with an explicit integer bit.
  ///
  /// The significand must be at least one bit wider than the target precision.
  union Significand {
    integerPart part;
    integerPart *parts;
  } significand;

  /// The signed unbiased exponent of the value.
  ExponentType exponent;

  /// What kind of floating point number this is.
  ///
  /// Only 2 bits are required, but VisualStudio incorrectly sign extends it.
  /// Using the extra bit keeps it from failing under VisualStudio.
  fltCategory category : 3;

  /// Sign bit of the number.
  unsigned int sign : 1;
};

hash_code hash_value(const IEEEFloat &Arg);
int ilogb(const IEEEFloat &Arg);
IEEEFloat scalbn(IEEEFloat X, int Exp, IEEEFloat::roundingMode);
IEEEFloat frexp(const IEEEFloat &Val, int &Exp, IEEEFloat::roundingMode RM);

// This mode implements more precise float in terms of two APFloats.
// The interface and layout is designed for arbitray underlying semantics,
// though currently only PPCDoubleDouble semantics are supported, whose
// corresponding underlying semantics are IEEEdouble.
class DoubleAPFloat final : public APFloatBase {
  // Note: this must be the first data member.
  const fltSemantics *Semantics;
  std::unique_ptr<APFloat[]> Floats;

  opStatus addImpl(const APFloat &a, const APFloat &aa, const APFloat &c,
                   const APFloat &cc, roundingMode RM);

  opStatus addWithSpecial(const DoubleAPFloat &LHS, const DoubleAPFloat &RHS,
                          DoubleAPFloat &Out, roundingMode RM);

public:
  DoubleAPFloat(const fltSemantics &S);
  DoubleAPFloat(const fltSemantics &S, uninitializedTag);
  DoubleAPFloat(const fltSemantics &S, integerPart);
  DoubleAPFloat(const fltSemantics &S, const APInt &I);
  DoubleAPFloat(const fltSemantics &S, APFloat &&First, APFloat &&Second);
  DoubleAPFloat(const DoubleAPFloat &RHS);
  DoubleAPFloat(DoubleAPFloat &&RHS);

  DoubleAPFloat &operator=(const DoubleAPFloat &RHS);

  DoubleAPFloat &operator=(DoubleAPFloat &&RHS) {
    if (this != &RHS) {
      this->~DoubleAPFloat();
      new (this) DoubleAPFloat(std::move(RHS));
    }
    return *this;
  }

  bool needsCleanup() const { return Floats != nullptr; }

  APFloat &getFirst() { return Floats[0]; }
  const APFloat &getFirst() const { return Floats[0]; }
  APFloat &getSecond() { return Floats[1]; }
  const APFloat &getSecond() const { return Floats[1]; }

  opStatus add(const DoubleAPFloat &RHS, roundingMode RM);
  opStatus subtract(const DoubleAPFloat &RHS, roundingMode RM);
  opStatus multiply(const DoubleAPFloat &RHS, roundingMode RM);
  opStatus divide(const DoubleAPFloat &RHS, roundingMode RM);
  opStatus remainder(const DoubleAPFloat &RHS);
  opStatus mod(const DoubleAPFloat &RHS);
  opStatus fusedMultiplyAdd(const DoubleAPFloat &Multiplicand,
                            const DoubleAPFloat &Addend, roundingMode RM);
  opStatus roundToIntegral(roundingMode RM);
  void changeSign();
  cmpResult compareAbsoluteValue(const DoubleAPFloat &RHS) const;

  fltCategory getCategory() const;
  bool isNegative() const;

  void makeInf(bool Neg);
  void makeZero(bool Neg);
  void makeLargest(bool Neg);
  void makeSmallest(bool Neg);
  void makeSmallestNormalized(bool Neg);
  void makeNaN(bool SNaN, bool Neg, const APInt *fill);

  cmpResult compare(const DoubleAPFloat &RHS) const;
  bool bitwiseIsEqual(const DoubleAPFloat &RHS) const;
  APInt bitcastToAPInt() const;
  opStatus convertFromString(StringRef, roundingMode);
  opStatus next(bool nextDown);

  opStatus convertToInteger(MutableArrayRef<integerPart> Input,
                            unsigned int Width, bool IsSigned, roundingMode RM,
                            bool *IsExact) const;
  opStatus convertFromAPInt(const APInt &Input, bool IsSigned, roundingMode RM);
  opStatus convertFromSignExtendedInteger(const integerPart *Input,
                                          unsigned int InputSize, bool IsSigned,
                                          roundingMode RM);
  opStatus convertFromZeroExtendedInteger(const integerPart *Input,
                                          unsigned int InputSize, bool IsSigned,
                                          roundingMode RM);
  unsigned int convertToHexString(char *DST, unsigned int HexDigits,
                                  bool UpperCase, roundingMode RM) const;

  bool isDenormal() const;
  bool isSmallest() const;
  bool isLargest() const;
  bool isInteger() const;

  void toString(SmallVectorImpl<char> &Str, unsigned FormatPrecision,
                unsigned FormatMaxPadding, bool TruncateZero = true) const;

  bool getExactInverse(APFloat *inv) const;

  friend int ilogb(const DoubleAPFloat &Arg);
  friend DoubleAPFloat scalbn(DoubleAPFloat X, int Exp, roundingMode);
  friend DoubleAPFloat frexp(const DoubleAPFloat &X, int &Exp, roundingMode);
  friend hash_code hash_value(const DoubleAPFloat &Arg);
};

hash_code hash_value(const DoubleAPFloat &Arg);

} // End detail namespace

// This is a interface class that is currently forwarding functionalities from
// detail::IEEEFloat.
class APFloat : public APFloatBase {
  typedef detail::IEEEFloat IEEEFloat;
  typedef detail::DoubleAPFloat DoubleAPFloat;

  static_assert(std::is_standard_layout<IEEEFloat>::value, "");

  union Storage {
    const fltSemantics *semantics;
    IEEEFloat IEEE;
    DoubleAPFloat Double;

    explicit Storage(IEEEFloat F, const fltSemantics &S);
    explicit Storage(DoubleAPFloat F, const fltSemantics &S)
        : Double(std::move(F)) {
      assert(&S == &PPCDoubleDouble());
    }

    template <typename... ArgTypes>
    Storage(const fltSemantics &Semantics, ArgTypes &&... Args) {
      if (usesLayout<IEEEFloat>(Semantics)) {
        new (&IEEE) IEEEFloat(Semantics, std::forward<ArgTypes>(Args)...);
        return;
      }
      if (usesLayout<DoubleAPFloat>(Semantics)) {
        new (&Double) DoubleAPFloat(Semantics, std::forward<ArgTypes>(Args)...);
        return;
      }
      llvm_unreachable("Unexpected semantics");
    }

    ~Storage() {
      if (usesLayout<IEEEFloat>(*semantics)) {
        IEEE.~IEEEFloat();
        return;
      }
      if (usesLayout<DoubleAPFloat>(*semantics)) {
        Double.~DoubleAPFloat();
        return;
      }
      llvm_unreachable("Unexpected semantics");
    }

    Storage(const Storage &RHS) {
      if (usesLayout<IEEEFloat>(*RHS.semantics)) {
        new (this) IEEEFloat(RHS.IEEE);
        return;
      }
      if (usesLayout<DoubleAPFloat>(*RHS.semantics)) {
        new (this) DoubleAPFloat(RHS.Double);
        return;
      }
      llvm_unreachable("Unexpected semantics");
    }

    Storage(Storage &&RHS) {
      if (usesLayout<IEEEFloat>(*RHS.semantics)) {
        new (this) IEEEFloat(std::move(RHS.IEEE));
        return;
      }
      if (usesLayout<DoubleAPFloat>(*RHS.semantics)) {
        new (this) DoubleAPFloat(std::move(RHS.Double));
        return;
      }
      llvm_unreachable("Unexpected semantics");
    }

    Storage &operator=(const Storage &RHS) {
      if (usesLayout<IEEEFloat>(*semantics) &&
          usesLayout<IEEEFloat>(*RHS.semantics)) {
        IEEE = RHS.IEEE;
      } else if (usesLayout<DoubleAPFloat>(*semantics) &&
                 usesLayout<DoubleAPFloat>(*RHS.semantics)) {
        Double = RHS.Double;
      } else if (this != &RHS) {
        this->~Storage();
        new (this) Storage(RHS);
      }
      return *this;
    }

    Storage &operator=(Storage &&RHS) {
      if (usesLayout<IEEEFloat>(*semantics) &&
          usesLayout<IEEEFloat>(*RHS.semantics)) {
        IEEE = std::move(RHS.IEEE);
      } else if (usesLayout<DoubleAPFloat>(*semantics) &&
                 usesLayout<DoubleAPFloat>(*RHS.semantics)) {
        Double = std::move(RHS.Double);
      } else if (this != &RHS) {
        this->~Storage();
        new (this) Storage(std::move(RHS));
      }
      return *this;
    }
  } U;

  template <typename T> static bool usesLayout(const fltSemantics &Semantics) {
    static_assert(std::is_same<T, IEEEFloat>::value ||
                  std::is_same<T, DoubleAPFloat>::value, "");
    if (std::is_same<T, DoubleAPFloat>::value) {
      return &Semantics == &PPCDoubleDouble();
    }
    return &Semantics != &PPCDoubleDouble();
  }

  IEEEFloat &getIEEE() {
    if (usesLayout<IEEEFloat>(*U.semantics))
      return U.IEEE;
    if (usesLayout<DoubleAPFloat>(*U.semantics))
      return U.Double.getFirst().U.IEEE;
    llvm_unreachable("Unexpected semantics");
  }

  const IEEEFloat &getIEEE() const {
    if (usesLayout<IEEEFloat>(*U.semantics))
      return U.IEEE;
    if (usesLayout<DoubleAPFloat>(*U.semantics))
      return U.Double.getFirst().U.IEEE;
    llvm_unreachable("Unexpected semantics");
  }

  void makeZero(bool Neg) { APFLOAT_DISPATCH_ON_SEMANTICS(makeZero(Neg)); }

  void makeInf(bool Neg) { APFLOAT_DISPATCH_ON_SEMANTICS(makeInf(Neg)); }

  void makeNaN(bool SNaN, bool Neg, const APInt *fill) {
    APFLOAT_DISPATCH_ON_SEMANTICS(makeNaN(SNaN, Neg, fill));
  }

  void makeLargest(bool Neg) {
    APFLOAT_DISPATCH_ON_SEMANTICS(makeLargest(Neg));
  }

  void makeSmallest(bool Neg) {
    APFLOAT_DISPATCH_ON_SEMANTICS(makeSmallest(Neg));
  }

  void makeSmallestNormalized(bool Neg) {
    APFLOAT_DISPATCH_ON_SEMANTICS(makeSmallestNormalized(Neg));
  }

  // FIXME: This is due to clang 3.3 (or older version) always checks for the
  // default constructor in an array aggregate initialization, even if no
  // elements in the array is default initialized.
  APFloat() : U(IEEEdouble()) {
    llvm_unreachable("This is a workaround for old clang.");
  }

  explicit APFloat(IEEEFloat F, const fltSemantics &S) : U(std::move(F), S) {}
  explicit APFloat(DoubleAPFloat F, const fltSemantics &S)
      : U(std::move(F), S) {}

  cmpResult compareAbsoluteValue(const APFloat &RHS) const {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only compare APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.compareAbsoluteValue(RHS.U.IEEE);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.compareAbsoluteValue(RHS.U.Double);
    llvm_unreachable("Unexpected semantics");
  }

public:
  APFloat(const fltSemantics &Semantics) : U(Semantics) {}
  APFloat(const fltSemantics &Semantics, StringRef S);
  APFloat(const fltSemantics &Semantics, integerPart I) : U(Semantics, I) {}
  // TODO: Remove this constructor. This isn't faster than the first one.
  APFloat(const fltSemantics &Semantics, uninitializedTag)
      : U(Semantics, uninitialized) {}
  APFloat(const fltSemantics &Semantics, const APInt &I) : U(Semantics, I) {}
  explicit APFloat(double d) : U(IEEEFloat(d), IEEEdouble()) {}
  explicit APFloat(float f) : U(IEEEFloat(f), IEEEsingle()) {}
  APFloat(const APFloat &RHS) = default;
  APFloat(APFloat &&RHS) = default;

  ~APFloat() = default;

  bool needsCleanup() const { APFLOAT_DISPATCH_ON_SEMANTICS(needsCleanup()); }

  /// Factory for Positive and Negative Zero.
  ///
  /// \param Negative True iff the number should be negative.
  static APFloat getZero(const fltSemantics &Sem, bool Negative = false) {
    APFloat Val(Sem, uninitialized);
    Val.makeZero(Negative);
    return Val;
  }

  /// Factory for Positive and Negative Infinity.
  ///
  /// \param Negative True iff the number should be negative.
  static APFloat getInf(const fltSemantics &Sem, bool Negative = false) {
    APFloat Val(Sem, uninitialized);
    Val.makeInf(Negative);
    return Val;
  }

  /// Factory for NaN values.
  ///
  /// \param Negative - True iff the NaN generated should be negative.
  /// \param payload - The unspecified fill bits for creating the NaN, 0 by
  /// default.  The value is truncated as necessary.
  static APFloat getNaN(const fltSemantics &Sem, bool Negative = false,
                        uint64_t payload = 0) {
    if (payload) {
      APInt intPayload(64, payload);
      return getQNaN(Sem, Negative, &intPayload);
    } else {
      return getQNaN(Sem, Negative, nullptr);
    }
  }

  /// Factory for QNaN values.
  static APFloat getQNaN(const fltSemantics &Sem, bool Negative = false,
                         const APInt *payload = nullptr) {
    APFloat Val(Sem, uninitialized);
    Val.makeNaN(false, Negative, payload);
    return Val;
  }

  /// Factory for SNaN values.
  static APFloat getSNaN(const fltSemantics &Sem, bool Negative = false,
                         const APInt *payload = nullptr) {
    APFloat Val(Sem, uninitialized);
    Val.makeNaN(true, Negative, payload);
    return Val;
  }

  /// Returns the largest finite number in the given semantics.
  ///
  /// \param Negative - True iff the number should be negative
  static APFloat getLargest(const fltSemantics &Sem, bool Negative = false) {
    APFloat Val(Sem, uninitialized);
    Val.makeLargest(Negative);
    return Val;
  }

  /// Returns the smallest (by magnitude) finite number in the given semantics.
  /// Might be denormalized, which implies a relative loss of precision.
  ///
  /// \param Negative - True iff the number should be negative
  static APFloat getSmallest(const fltSemantics &Sem, bool Negative = false) {
    APFloat Val(Sem, uninitialized);
    Val.makeSmallest(Negative);
    return Val;
  }

  /// Returns the smallest (by magnitude) normalized finite number in the given
  /// semantics.
  ///
  /// \param Negative - True iff the number should be negative
  static APFloat getSmallestNormalized(const fltSemantics &Sem,
                                       bool Negative = false) {
    APFloat Val(Sem, uninitialized);
    Val.makeSmallestNormalized(Negative);
    return Val;
  }

  /// Returns a float which is bitcasted from an all one value int.
  ///
  /// \param BitWidth - Select float type
  /// \param isIEEE   - If 128 bit number, select between PPC and IEEE
  static APFloat getAllOnesValue(unsigned BitWidth, bool isIEEE = false);

  /// Used to insert APFloat objects, or objects that contain APFloat objects,
  /// into FoldingSets.
  void Profile(FoldingSetNodeID &NID) const;

  opStatus add(const APFloat &RHS, roundingMode RM) {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only call on two APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.add(RHS.U.IEEE, RM);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.add(RHS.U.Double, RM);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus subtract(const APFloat &RHS, roundingMode RM) {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only call on two APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.subtract(RHS.U.IEEE, RM);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.subtract(RHS.U.Double, RM);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus multiply(const APFloat &RHS, roundingMode RM) {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only call on two APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.multiply(RHS.U.IEEE, RM);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.multiply(RHS.U.Double, RM);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus divide(const APFloat &RHS, roundingMode RM) {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only call on two APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.divide(RHS.U.IEEE, RM);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.divide(RHS.U.Double, RM);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus remainder(const APFloat &RHS) {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only call on two APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.remainder(RHS.U.IEEE);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.remainder(RHS.U.Double);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus mod(const APFloat &RHS) {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only call on two APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.mod(RHS.U.IEEE);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.mod(RHS.U.Double);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus fusedMultiplyAdd(const APFloat &Multiplicand, const APFloat &Addend,
                            roundingMode RM) {
    assert(&getSemantics() == &Multiplicand.getSemantics() &&
           "Should only call on APFloats with the same semantics");
    assert(&getSemantics() == &Addend.getSemantics() &&
           "Should only call on APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.fusedMultiplyAdd(Multiplicand.U.IEEE, Addend.U.IEEE, RM);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.fusedMultiplyAdd(Multiplicand.U.Double, Addend.U.Double,
                                       RM);
    llvm_unreachable("Unexpected semantics");
  }
  opStatus roundToIntegral(roundingMode RM) {
    APFLOAT_DISPATCH_ON_SEMANTICS(roundToIntegral(RM));
  }

  // TODO: bool parameters are not readable and a source of bugs.
  // Do something.
  opStatus next(bool nextDown) {
    APFLOAT_DISPATCH_ON_SEMANTICS(next(nextDown));
  }

  /// Add two APFloats, rounding ties to the nearest even.
  /// No error checking.
  APFloat operator+(const APFloat &RHS) const {
    APFloat Result(*this);
    (void)Result.add(RHS, rmNearestTiesToEven);
    return Result;
  }

  /// Subtract two APFloats, rounding ties to the nearest even.
  /// No error checking.
  APFloat operator-(const APFloat &RHS) const {
    APFloat Result(*this);
    (void)Result.subtract(RHS, rmNearestTiesToEven);
    return Result;
  }

  /// Multiply two APFloats, rounding ties to the nearest even.
  /// No error checking.
  APFloat operator*(const APFloat &RHS) const {
    APFloat Result(*this);
    (void)Result.multiply(RHS, rmNearestTiesToEven);
    return Result;
  }

  /// Divide the first APFloat by the second, rounding ties to the nearest even.
  /// No error checking.
  APFloat operator/(const APFloat &RHS) const {
    APFloat Result(*this);
    (void)Result.divide(RHS, rmNearestTiesToEven);
    return Result;
  }

  void changeSign() { APFLOAT_DISPATCH_ON_SEMANTICS(changeSign()); }
  void clearSign() {
    if (isNegative())
      changeSign();
  }
  void copySign(const APFloat &RHS) {
    if (isNegative() != RHS.isNegative())
      changeSign();
  }

  /// A static helper to produce a copy of an APFloat value with its sign
  /// copied from some other APFloat.
  static APFloat copySign(APFloat Value, const APFloat &Sign) {
    Value.copySign(Sign);
    return Value;
  }

  opStatus convert(const fltSemantics &ToSemantics, roundingMode RM,
                   bool *losesInfo);
  opStatus convertToInteger(MutableArrayRef<integerPart> Input,
                            unsigned int Width, bool IsSigned, roundingMode RM,
                            bool *IsExact) const {
    APFLOAT_DISPATCH_ON_SEMANTICS(
        convertToInteger(Input, Width, IsSigned, RM, IsExact));
  }
  opStatus convertToInteger(APSInt &Result, roundingMode RM,
                            bool *IsExact) const;
  opStatus convertFromAPInt(const APInt &Input, bool IsSigned,
                            roundingMode RM) {
    APFLOAT_DISPATCH_ON_SEMANTICS(convertFromAPInt(Input, IsSigned, RM));
  }
  opStatus convertFromSignExtendedInteger(const integerPart *Input,
                                          unsigned int InputSize, bool IsSigned,
                                          roundingMode RM) {
    APFLOAT_DISPATCH_ON_SEMANTICS(
        convertFromSignExtendedInteger(Input, InputSize, IsSigned, RM));
  }
  opStatus convertFromZeroExtendedInteger(const integerPart *Input,
                                          unsigned int InputSize, bool IsSigned,
                                          roundingMode RM) {
    APFLOAT_DISPATCH_ON_SEMANTICS(
        convertFromZeroExtendedInteger(Input, InputSize, IsSigned, RM));
  }
  opStatus convertFromString(StringRef, roundingMode);
  APInt bitcastToAPInt() const {
    APFLOAT_DISPATCH_ON_SEMANTICS(bitcastToAPInt());
  }
  double convertToDouble() const { return getIEEE().convertToDouble(); }
  float convertToFloat() const { return getIEEE().convertToFloat(); }

  bool operator==(const APFloat &) const = delete;

  cmpResult compare(const APFloat &RHS) const {
    assert(&getSemantics() == &RHS.getSemantics() &&
           "Should only compare APFloats with the same semantics");
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.compare(RHS.U.IEEE);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.compare(RHS.U.Double);
    llvm_unreachable("Unexpected semantics");
  }

  bool bitwiseIsEqual(const APFloat &RHS) const {
    if (&getSemantics() != &RHS.getSemantics())
      return false;
    if (usesLayout<IEEEFloat>(getSemantics()))
      return U.IEEE.bitwiseIsEqual(RHS.U.IEEE);
    if (usesLayout<DoubleAPFloat>(getSemantics()))
      return U.Double.bitwiseIsEqual(RHS.U.Double);
    llvm_unreachable("Unexpected semantics");
  }

  /// We don't rely on operator== working on double values, as
  /// it returns true for things that are clearly not equal, like -0.0 and 0.0.
  /// As such, this method can be used to do an exact bit-for-bit comparison of
  /// two floating point values.
  ///
  /// We leave the version with the double argument here because it's just so
  /// convenient to write "2.0" and the like.  Without this function we'd
  /// have to duplicate its logic everywhere it's called.
  bool isExactlyValue(double V) const {
    bool ignored;
    APFloat Tmp(V);
    Tmp.convert(getSemantics(), APFloat::rmNearestTiesToEven, &ignored);
    return bitwiseIsEqual(Tmp);
  }

  unsigned int convertToHexString(char *DST, unsigned int HexDigits,
                                  bool UpperCase, roundingMode RM) const {
    APFLOAT_DISPATCH_ON_SEMANTICS(
        convertToHexString(DST, HexDigits, UpperCase, RM));
  }

  bool isZero() const { return getCategory() == fcZero; }
  bool isInfinity() const { return getCategory() == fcInfinity; }
  bool isNaN() const { return getCategory() == fcNaN; }

  bool isNegative() const { return getIEEE().isNegative(); }
  bool isDenormal() const { APFLOAT_DISPATCH_ON_SEMANTICS(isDenormal()); }
  bool isSignaling() const { return getIEEE().isSignaling(); }

  bool isNormal() const { return !isDenormal() && isFiniteNonZero(); }
  bool isFinite() const { return !isNaN() && !isInfinity(); }

  fltCategory getCategory() const { return getIEEE().getCategory(); }
  const fltSemantics &getSemantics() const { return *U.semantics; }
  bool isNonZero() const { return !isZero(); }
  bool isFiniteNonZero() const { return isFinite() && !isZero(); }
  bool isPosZero() const { return isZero() && !isNegative(); }
  bool isNegZero() const { return isZero() && isNegative(); }
  bool isSmallest() const { APFLOAT_DISPATCH_ON_SEMANTICS(isSmallest()); }
  bool isLargest() const { APFLOAT_DISPATCH_ON_SEMANTICS(isLargest()); }
  bool isInteger() const { APFLOAT_DISPATCH_ON_SEMANTICS(isInteger()); }

  APFloat &operator=(const APFloat &RHS) = default;
  APFloat &operator=(APFloat &&RHS) = default;

  void toString(SmallVectorImpl<char> &Str, unsigned FormatPrecision = 0,
                unsigned FormatMaxPadding = 3, bool TruncateZero = true) const {
    APFLOAT_DISPATCH_ON_SEMANTICS(
        toString(Str, FormatPrecision, FormatMaxPadding, TruncateZero));
  }

  void print(raw_ostream &) const;
  void dump() const;

  bool getExactInverse(APFloat *inv) const {
    APFLOAT_DISPATCH_ON_SEMANTICS(getExactInverse(inv));
  }

  friend hash_code hash_value(const APFloat &Arg);
  friend int ilogb(const APFloat &Arg) { return ilogb(Arg.getIEEE()); }
  friend APFloat scalbn(APFloat X, int Exp, roundingMode RM);
  friend APFloat frexp(const APFloat &X, int &Exp, roundingMode RM);
  friend IEEEFloat;
  friend DoubleAPFloat;
};

/// See friend declarations above.
///
/// These additional declarations are required in order to compile LLVM with IBM
/// xlC compiler.
hash_code hash_value(const APFloat &Arg);
inline APFloat scalbn(APFloat X, int Exp, APFloat::roundingMode RM) {
  if (APFloat::usesLayout<detail::IEEEFloat>(X.getSemantics()))
    return APFloat(scalbn(X.U.IEEE, Exp, RM), X.getSemantics());
  if (APFloat::usesLayout<detail::DoubleAPFloat>(X.getSemantics()))
    return APFloat(scalbn(X.U.Double, Exp, RM), X.getSemantics());
  llvm_unreachable("Unexpected semantics");
}

/// Equivalent of C standard library function.
///
/// While the C standard says Exp is an unspecified value for infinity and nan,
/// this returns INT_MAX for infinities, and INT_MIN for NaNs.
inline APFloat frexp(const APFloat &X, int &Exp, APFloat::roundingMode RM) {
  if (APFloat::usesLayout<detail::IEEEFloat>(X.getSemantics()))
    return APFloat(frexp(X.U.IEEE, Exp, RM), X.getSemantics());
  if (APFloat::usesLayout<detail::DoubleAPFloat>(X.getSemantics()))
    return APFloat(frexp(X.U.Double, Exp, RM), X.getSemantics());
  llvm_unreachable("Unexpected semantics");
}
/// Returns the absolute value of the argument.
inline APFloat abs(APFloat X) {
  X.clearSign();
  return X;
}

/// Returns the negated value of the argument.
inline APFloat neg(APFloat X) {
  X.changeSign();
  return X;
}

/// Implements IEEE minNum semantics. Returns the smaller of the 2 arguments if
/// both are not NaN. If either argument is a NaN, returns the other argument.
LLVM_READONLY
inline APFloat minnum(const APFloat &A, const APFloat &B) {
  if (A.isNaN())
    return B;
  if (B.isNaN())
    return A;
  return (B.compare(A) == APFloat::cmpLessThan) ? B : A;
}

/// Implements IEEE maxNum semantics. Returns the larger of the 2 arguments if
/// both are not NaN. If either argument is a NaN, returns the other argument.
LLVM_READONLY
inline APFloat maxnum(const APFloat &A, const APFloat &B) {
  if (A.isNaN())
    return B;
  if (B.isNaN())
    return A;
  return (A.compare(B) == APFloat::cmpLessThan) ? B : A;
}

/// Implements IEEE 754-2018 minimum semantics. Returns the smaller of 2
/// arguments, propagating NaNs and treating -0 as less than +0.
LLVM_READONLY
inline APFloat minimum(const APFloat &A, const APFloat &B) {
  if (A.isNaN())
    return A;
  if (B.isNaN())
    return B;
  if (A.isZero() && B.isZero() && (A.isNegative() != B.isNegative()))
    return A.isNegative() ? A : B;
  return (B.compare(A) == APFloat::cmpLessThan) ? B : A;
}

/// Implements IEEE 754-2018 maximum semantics. Returns the larger of 2
/// arguments, propagating NaNs and treating -0 as less than +0.
LLVM_READONLY
inline APFloat maximum(const APFloat &A, const APFloat &B) {
  if (A.isNaN())
    return A;
  if (B.isNaN())
    return B;
  if (A.isZero() && B.isZero() && (A.isNegative() != B.isNegative()))
    return A.isNegative() ? B : A;
  return (A.compare(B) == APFloat::cmpLessThan) ? B : A;
}

} // namespace llvm

#undef APFLOAT_DISPATCH_ON_SEMANTICS
#endif // LLVM_ADT_APFLOAT_H
