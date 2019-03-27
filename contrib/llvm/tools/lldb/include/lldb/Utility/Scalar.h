//===-- Scalar.h ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_SCALAR_H
#define LLDB_UTILITY_SCALAR_H

#include "lldb/Utility/Status.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private-types.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include <cstddef>
#include <cstdint>

namespace lldb_private {
class DataExtractor;
class Stream;
} // namespace lldb_private

#define NUM_OF_WORDS_INT128 2
#define BITWIDTH_INT128 128
#define NUM_OF_WORDS_INT256 4
#define BITWIDTH_INT256 256

namespace lldb_private {

//----------------------------------------------------------------------
// A class designed to hold onto values and their corresponding types.
// Operators are defined and Scalar objects will correctly promote their types
// and values before performing these operations. Type promotion currently
// follows the ANSI C type promotion rules.
//----------------------------------------------------------------------
class Scalar {
public:
  enum Type {
    e_void = 0,
    e_sint,
    e_uint,
    e_slong,
    e_ulong,
    e_slonglong,
    e_ulonglong,
    e_sint128,
    e_uint128,
    e_sint256,
    e_uint256,
    e_float,
    e_double,
    e_long_double
  };

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  Scalar();
  Scalar(int v) : m_type(e_sint), m_float((float)0) {
    m_integer = llvm::APInt(sizeof(int) * 8, v, true);
  }
  Scalar(unsigned int v) : m_type(e_uint), m_float((float)0) {
    m_integer = llvm::APInt(sizeof(int) * 8, v);
  }
  Scalar(long v) : m_type(e_slong), m_float((float)0) {
    m_integer = llvm::APInt(sizeof(long) * 8, v, true);
  }
  Scalar(unsigned long v) : m_type(e_ulong), m_float((float)0) {
    m_integer = llvm::APInt(sizeof(long) * 8, v);
  }
  Scalar(long long v) : m_type(e_slonglong), m_float((float)0) {
    m_integer = llvm::APInt(sizeof(long long) * 8, v, true);
  }
  Scalar(unsigned long long v) : m_type(e_ulonglong), m_float((float)0) {
    m_integer = llvm::APInt(sizeof(long long) * 8, v);
  }
  Scalar(float v) : m_type(e_float), m_float(v) { m_float = llvm::APFloat(v); }
  Scalar(double v) : m_type(e_double), m_float(v) {
    m_float = llvm::APFloat(v);
  }
  Scalar(long double v, bool ieee_quad)
      : m_type(e_long_double), m_float((float)0), m_ieee_quad(ieee_quad) {
    if (ieee_quad)
      m_float = llvm::APFloat(llvm::APFloat::IEEEquad(),
                              llvm::APInt(BITWIDTH_INT128, NUM_OF_WORDS_INT128,
                                          ((type128 *)&v)->x));
    else
      m_float = llvm::APFloat(llvm::APFloat::x87DoubleExtended(),
                              llvm::APInt(BITWIDTH_INT128, NUM_OF_WORDS_INT128,
                                          ((type128 *)&v)->x));
  }
  Scalar(llvm::APInt v) : m_type(), m_float((float)0) {
    m_integer = llvm::APInt(v);
    switch (m_integer.getBitWidth()) {
    case 8:
    case 16:
    case 32:
      if (m_integer.isSignedIntN(sizeof(sint_t) * 8))
        m_type = e_sint;
      else
        m_type = e_uint;
      break;
    case 64:
      if (m_integer.isSignedIntN(sizeof(slonglong_t) * 8))
        m_type = e_slonglong;
      else
        m_type = e_ulonglong;
      break;
    case 128:
      if (m_integer.isSignedIntN(BITWIDTH_INT128))
        m_type = e_sint128;
      else
        m_type = e_uint128;
      break;
    case 256:
      if (m_integer.isSignedIntN(BITWIDTH_INT256))
        m_type = e_sint256;
      else
        m_type = e_uint256;
      break;
    }
  }
  Scalar(const Scalar &rhs);
  // Scalar(const RegisterValue& reg_value);
  virtual ~Scalar();

  bool SignExtend(uint32_t bit_pos);

  bool ExtractBitfield(uint32_t bit_size, uint32_t bit_offset);

  bool SetBit(uint32_t bit);

  bool ClearBit(uint32_t bit);

  const void *GetBytes() const;

  size_t GetByteSize() const;

  bool GetData(DataExtractor &data, size_t limit_byte_size = UINT32_MAX) const;

  size_t GetAsMemoryData(void *dst, size_t dst_len,
                         lldb::ByteOrder dst_byte_order, Status &error) const;

  bool IsZero() const;

  void Clear() {
    m_type = e_void;
    m_integer.clearAllBits();
  }

  const char *GetTypeAsCString() const;

  void GetValue(Stream *s, bool show_type) const;

  bool IsValid() const {
    return (m_type >= e_sint) && (m_type <= e_long_double);
  }

  bool Promote(Scalar::Type type);

  bool MakeSigned();

  bool MakeUnsigned();

  static const char *GetValueTypeAsCString(Scalar::Type value_type);

  static Scalar::Type
  GetValueTypeForSignedIntegerWithByteSize(size_t byte_size);

  static Scalar::Type
  GetValueTypeForUnsignedIntegerWithByteSize(size_t byte_size);

  static Scalar::Type GetValueTypeForFloatWithByteSize(size_t byte_size);

  //----------------------------------------------------------------------
  // All operators can benefits from the implicit conversions that will happen
  // automagically by the compiler, so no temporary objects will need to be
  // created. As a result, we currently don't need a variety of overloaded set
  // value accessors.
  //----------------------------------------------------------------------
  Scalar &operator=(const int i);
  Scalar &operator=(unsigned int v);
  Scalar &operator=(long v);
  Scalar &operator=(unsigned long v);
  Scalar &operator=(long long v);
  Scalar &operator=(unsigned long long v);
  Scalar &operator=(float v);
  Scalar &operator=(double v);
  Scalar &operator=(long double v);
  Scalar &operator=(llvm::APInt v);
  Scalar &operator=(const Scalar &rhs); // Assignment operator
  Scalar &operator+=(const Scalar &rhs);
  Scalar &operator<<=(const Scalar &rhs); // Shift left
  Scalar &operator>>=(const Scalar &rhs); // Shift right (arithmetic)
  Scalar &operator&=(const Scalar &rhs);

  //----------------------------------------------------------------------
  // Shifts the current value to the right without maintaining the current sign
  // of the value (if it is signed).
  //----------------------------------------------------------------------
  bool ShiftRightLogical(const Scalar &rhs); // Returns true on success

  //----------------------------------------------------------------------
  // Takes the absolute value of the current value if it is signed, else the
  // value remains unchanged. Returns false if the contained value has a void
  // type.
  //----------------------------------------------------------------------
  bool AbsoluteValue(); // Returns true on success
  //----------------------------------------------------------------------
  // Negates the current value (even for unsigned values). Returns false if the
  // contained value has a void type.
  //----------------------------------------------------------------------
  bool UnaryNegate(); // Returns true on success
  //----------------------------------------------------------------------
  // Inverts all bits in the current value as long as it isn't void or a
  // float/double/long double type. Returns false if the contained value has a
  // void/float/double/long double type, else the value is inverted and true is
  // returned.
  //----------------------------------------------------------------------
  bool OnesComplement(); // Returns true on success

  //----------------------------------------------------------------------
  // Access the type of the current value.
  //----------------------------------------------------------------------
  Scalar::Type GetType() const { return m_type; }

  //----------------------------------------------------------------------
  // Returns a casted value of the current contained data without modifying the
  // current value. FAIL_VALUE will be returned if the type of the value is
  // void or invalid.
  //----------------------------------------------------------------------
  int SInt(int fail_value = 0) const;

  unsigned char UChar(unsigned char fail_value = 0) const;

  signed char SChar(char fail_value = 0) const;

  unsigned short UShort(unsigned short fail_value = 0) const;

  short SShort(short fail_value = 0) const;

  unsigned int UInt(unsigned int fail_value = 0) const;

  long SLong(long fail_value = 0) const;

  unsigned long ULong(unsigned long fail_value = 0) const;

  long long SLongLong(long long fail_value = 0) const;

  unsigned long long ULongLong(unsigned long long fail_value = 0) const;

  llvm::APInt SInt128(llvm::APInt &fail_value) const;

  llvm::APInt UInt128(const llvm::APInt &fail_value) const;

  llvm::APInt SInt256(llvm::APInt &fail_value) const;

  llvm::APInt UInt256(const llvm::APInt &fail_value) const;

  float Float(float fail_value = 0.0f) const;

  double Double(double fail_value = 0.0) const;

  long double LongDouble(long double fail_value = 0.0) const;

  Status SetValueFromCString(const char *s, lldb::Encoding encoding,
                             size_t byte_size);

  Status SetValueFromData(DataExtractor &data, lldb::Encoding encoding,
                          size_t byte_size);

  static bool UIntValueIsValidForSize(uint64_t uval64, size_t total_byte_size) {
    if (total_byte_size > 8)
      return false;

    if (total_byte_size == 8)
      return true;

    const uint64_t max = ((uint64_t)1 << (uint64_t)(total_byte_size * 8)) - 1;
    return uval64 <= max;
  }

  static bool SIntValueIsValidForSize(int64_t sval64, size_t total_byte_size) {
    if (total_byte_size > 8)
      return false;

    if (total_byte_size == 8)
      return true;

    const int64_t max = ((int64_t)1 << (uint64_t)(total_byte_size * 8 - 1)) - 1;
    const int64_t min = ~(max);
    return min <= sval64 && sval64 <= max;
  }

protected:
  typedef char schar_t;
  typedef unsigned char uchar_t;
  typedef short sshort_t;
  typedef unsigned short ushort_t;
  typedef int sint_t;
  typedef unsigned int uint_t;
  typedef long slong_t;
  typedef unsigned long ulong_t;
  typedef long long slonglong_t;
  typedef unsigned long long ulonglong_t;
  typedef float float_t;
  typedef double double_t;
  typedef long double long_double_t;

  //------------------------------------------------------------------
  // Classes that inherit from Scalar can see and modify these
  //------------------------------------------------------------------
  Scalar::Type m_type;
  llvm::APInt m_integer;
  llvm::APFloat m_float;
  bool m_ieee_quad = false;

private:
  friend const Scalar operator+(const Scalar &lhs, const Scalar &rhs);
  friend const Scalar operator-(const Scalar &lhs, const Scalar &rhs);
  friend const Scalar operator/(const Scalar &lhs, const Scalar &rhs);
  friend const Scalar operator*(const Scalar &lhs, const Scalar &rhs);
  friend const Scalar operator&(const Scalar &lhs, const Scalar &rhs);
  friend const Scalar operator|(const Scalar &lhs, const Scalar &rhs);
  friend const Scalar operator%(const Scalar &lhs, const Scalar &rhs);
  friend const Scalar operator^(const Scalar &lhs, const Scalar &rhs);
  friend const Scalar operator<<(const Scalar &lhs, const Scalar &rhs);
  friend const Scalar operator>>(const Scalar &lhs, const Scalar &rhs);
  friend bool operator==(const Scalar &lhs, const Scalar &rhs);
  friend bool operator!=(const Scalar &lhs, const Scalar &rhs);
  friend bool operator<(const Scalar &lhs, const Scalar &rhs);
  friend bool operator<=(const Scalar &lhs, const Scalar &rhs);
  friend bool operator>(const Scalar &lhs, const Scalar &rhs);
  friend bool operator>=(const Scalar &lhs, const Scalar &rhs);
};

//----------------------------------------------------------------------
// Split out the operators into a format where the compiler will be able to
// implicitly convert numbers into Scalar objects.
//
// This allows code like:
//      Scalar two(2);
//      Scalar four = two * 2;
//      Scalar eight = 2 * four;    // This would cause an error if the
//                                  // operator* was implemented as a
//                                  // member function.
// SEE:
//  Item 19 of "Effective C++ Second Edition" by Scott Meyers
//  Differentiate among members functions, non-member functions, and
//  friend functions
//----------------------------------------------------------------------
const Scalar operator+(const Scalar &lhs, const Scalar &rhs);
const Scalar operator-(const Scalar &lhs, const Scalar &rhs);
const Scalar operator/(const Scalar &lhs, const Scalar &rhs);
const Scalar operator*(const Scalar &lhs, const Scalar &rhs);
const Scalar operator&(const Scalar &lhs, const Scalar &rhs);
const Scalar operator|(const Scalar &lhs, const Scalar &rhs);
const Scalar operator%(const Scalar &lhs, const Scalar &rhs);
const Scalar operator^(const Scalar &lhs, const Scalar &rhs);
const Scalar operator<<(const Scalar &lhs, const Scalar &rhs);
const Scalar operator>>(const Scalar &lhs, const Scalar &rhs);
bool operator==(const Scalar &lhs, const Scalar &rhs);
bool operator!=(const Scalar &lhs, const Scalar &rhs);
bool operator<(const Scalar &lhs, const Scalar &rhs);
bool operator<=(const Scalar &lhs, const Scalar &rhs);
bool operator>(const Scalar &lhs, const Scalar &rhs);
bool operator>=(const Scalar &lhs, const Scalar &rhs);

} // namespace lldb_private

#endif // LLDB_UTILITY_SCALAR_H
