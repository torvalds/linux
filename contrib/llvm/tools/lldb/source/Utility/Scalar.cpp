//===-- Scalar.cpp ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Scalar.h"

#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/SmallString.h"

#include <cinttypes>
#include <cstdio>

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// Promote to max type currently follows the ANSI C rule for type promotion in
// expressions.
//----------------------------------------------------------------------
static Scalar::Type PromoteToMaxType(
    const Scalar &lhs,  // The const left hand side object
    const Scalar &rhs,  // The const right hand side object
    Scalar &temp_value, // A modifiable temp value than can be used to hold
                        // either the promoted lhs or rhs object
    const Scalar *&promoted_lhs_ptr, // Pointer to the resulting possibly
                                     // promoted value of lhs (at most one of
                                     // lhs/rhs will get promoted)
    const Scalar *&promoted_rhs_ptr  // Pointer to the resulting possibly
                                     // promoted value of rhs (at most one of
                                     // lhs/rhs will get promoted)
) {
  Scalar result;
  // Initialize the promoted values for both the right and left hand side
  // values to be the objects themselves. If no promotion is needed (both right
  // and left have the same type), then the temp_value will not get used.
  promoted_lhs_ptr = &lhs;
  promoted_rhs_ptr = &rhs;
  // Extract the types of both the right and left hand side values
  Scalar::Type lhs_type = lhs.GetType();
  Scalar::Type rhs_type = rhs.GetType();

  if (lhs_type > rhs_type) {
    // Right hand side need to be promoted
    temp_value = rhs; // Copy right hand side into the temp value
    if (temp_value.Promote(lhs_type)) // Promote it
      promoted_rhs_ptr =
          &temp_value; // Update the pointer for the promoted right hand side
  } else if (lhs_type < rhs_type) {
    // Left hand side need to be promoted
    temp_value = lhs; // Copy left hand side value into the temp value
    if (temp_value.Promote(rhs_type)) // Promote it
      promoted_lhs_ptr =
          &temp_value; // Update the pointer for the promoted left hand side
  }

  // Make sure our type promotion worked as expected
  if (promoted_lhs_ptr->GetType() == promoted_rhs_ptr->GetType())
    return promoted_lhs_ptr->GetType(); // Return the resulting max type

  // Return the void type (zero) if we fail to promote either of the values.
  return Scalar::e_void;
}

Scalar::Scalar() : m_type(e_void), m_float((float)0) {}

Scalar::Scalar(const Scalar &rhs)
    : m_type(rhs.m_type), m_integer(rhs.m_integer), m_float(rhs.m_float) {}

bool Scalar::GetData(DataExtractor &data, size_t limit_byte_size) const {
  size_t byte_size = GetByteSize();
  if (byte_size > 0) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(GetBytes());

    if (limit_byte_size < byte_size) {
      if (endian::InlHostByteOrder() == eByteOrderLittle) {
        // On little endian systems if we want fewer bytes from the current
        // type we just specify fewer bytes since the LSByte is first...
        byte_size = limit_byte_size;
      } else if (endian::InlHostByteOrder() == eByteOrderBig) {
        // On big endian systems if we want fewer bytes from the current type
        // have to advance our initial byte pointer and trim down the number of
        // bytes since the MSByte is first
        bytes += byte_size - limit_byte_size;
        byte_size = limit_byte_size;
      }
    }

    data.SetData(bytes, byte_size, endian::InlHostByteOrder());
    return true;
  }
  data.Clear();
  return false;
}

const void *Scalar::GetBytes() const {
  const uint64_t *apint_words;
  const uint8_t *bytes;
  static float_t flt_val;
  static double_t dbl_val;
  static uint64_t swapped_words[4];
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
    bytes = reinterpret_cast<const uint8_t *>(m_integer.getRawData());
    // getRawData always returns a pointer to an uint64_t.  If we have a
    // smaller type, we need to update the pointer on big-endian systems.
    if (endian::InlHostByteOrder() == eByteOrderBig) {
      size_t byte_size = m_integer.getBitWidth() / 8;
      if (byte_size < 8)
        bytes += 8 - byte_size;
    }
    return bytes;
  case e_sint128:
  case e_uint128:
    apint_words = m_integer.getRawData();
    // getRawData always returns a pointer to an array of two uint64_t values,
    // where the least-significant word always comes first.  On big-endian
    // systems we need to swap the two words.
    if (endian::InlHostByteOrder() == eByteOrderBig) {
      swapped_words[0] = apint_words[1];
      swapped_words[1] = apint_words[0];
      apint_words = swapped_words;
    }
    return reinterpret_cast<const void *>(apint_words);
  case e_sint256:
  case e_uint256:
    apint_words = m_integer.getRawData();
    // getRawData always returns a pointer to an array of four uint64_t values,
    // where the least-significant word always comes first.  On big-endian
    // systems we need to swap the four words.
    if (endian::InlHostByteOrder() == eByteOrderBig) {
      swapped_words[0] = apint_words[3];
      swapped_words[1] = apint_words[2];
      swapped_words[2] = apint_words[1];
      swapped_words[3] = apint_words[0];
      apint_words = swapped_words;
    }
    return reinterpret_cast<const void *>(apint_words);
  case e_float:
    flt_val = m_float.convertToFloat();
    return reinterpret_cast<const void *>(&flt_val);
  case e_double:
    dbl_val = m_float.convertToDouble();
    return reinterpret_cast<const void *>(&dbl_val);
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    apint_words = ldbl_val.getRawData();
    // getRawData always returns a pointer to an array of two uint64_t values,
    // where the least-significant word always comes first.  On big-endian
    // systems we need to swap the two words.
    if (endian::InlHostByteOrder() == eByteOrderBig) {
      swapped_words[0] = apint_words[1];
      swapped_words[1] = apint_words[0];
      apint_words = swapped_words;
    }
    return reinterpret_cast<const void *>(apint_words);
  }
  return nullptr;
}

size_t Scalar::GetByteSize() const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return (m_integer.getBitWidth() / 8);
  case e_float:
    return sizeof(float_t);
  case e_double:
    return sizeof(double_t);
  case e_long_double:
    return sizeof(long_double_t);
  }
  return 0;
}

bool Scalar::IsZero() const {
  llvm::APInt zero_int = llvm::APInt::getNullValue(m_integer.getBitWidth() / 8);
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return llvm::APInt::isSameValue(zero_int, m_integer);
  case e_float:
  case e_double:
  case e_long_double:
    return m_float.isZero();
  }
  return false;
}

void Scalar::GetValue(Stream *s, bool show_type) const {
  if (show_type)
    s->Printf("(%s) ", GetTypeAsCString());

  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_slong:
  case e_slonglong:
  case e_sint128:
  case e_sint256:
    s->PutCString(m_integer.toString(10, true));
    break;
  case e_uint:
  case e_ulong:
  case e_ulonglong:
  case e_uint128:
  case e_uint256:
    s->PutCString(m_integer.toString(10, false));
    break;
  case e_float:
  case e_double:
  case e_long_double:
    llvm::SmallString<24> string;
    m_float.toString(string);
    s->Printf("%s", string.c_str());
    break;
  }
}

const char *Scalar::GetTypeAsCString() const {
  switch (m_type) {
  case e_void:
    return "void";
  case e_sint:
    return "int";
  case e_uint:
    return "unsigned int";
  case e_slong:
    return "long";
  case e_ulong:
    return "unsigned long";
  case e_slonglong:
    return "long long";
  case e_ulonglong:
    return "unsigned long long";
  case e_sint128:
    return "int128_t";
  case e_uint128:
    return "unsigned int128_t";
  case e_sint256:
    return "int256_t";
  case e_uint256:
    return "unsigned int256_t";
  case e_float:
    return "float";
  case e_double:
    return "double";
  case e_long_double:
    return "long double";
  }
  return "<invalid Scalar type>";
}

Scalar &Scalar::operator=(const Scalar &rhs) {
  if (this != &rhs) {
    m_type = rhs.m_type;
    m_integer = llvm::APInt(rhs.m_integer);
    m_float = rhs.m_float;
  }
  return *this;
}

Scalar &Scalar::operator=(const int v) {
  m_type = e_sint;
  m_integer = llvm::APInt(sizeof(int) * 8, v, true);
  return *this;
}

Scalar &Scalar::operator=(unsigned int v) {
  m_type = e_uint;
  m_integer = llvm::APInt(sizeof(int) * 8, v);
  return *this;
}

Scalar &Scalar::operator=(long v) {
  m_type = e_slong;
  m_integer = llvm::APInt(sizeof(long) * 8, v, true);
  return *this;
}

Scalar &Scalar::operator=(unsigned long v) {
  m_type = e_ulong;
  m_integer = llvm::APInt(sizeof(long) * 8, v);
  return *this;
}

Scalar &Scalar::operator=(long long v) {
  m_type = e_slonglong;
  m_integer = llvm::APInt(sizeof(long) * 8, v, true);
  return *this;
}

Scalar &Scalar::operator=(unsigned long long v) {
  m_type = e_ulonglong;
  m_integer = llvm::APInt(sizeof(long long) * 8, v);
  return *this;
}

Scalar &Scalar::operator=(float v) {
  m_type = e_float;
  m_float = llvm::APFloat(v);
  return *this;
}

Scalar &Scalar::operator=(double v) {
  m_type = e_double;
  m_float = llvm::APFloat(v);
  return *this;
}

Scalar &Scalar::operator=(long double v) {
  m_type = e_long_double;
  if (m_ieee_quad)
    m_float = llvm::APFloat(
        llvm::APFloat::IEEEquad(),
        llvm::APInt(BITWIDTH_INT128, NUM_OF_WORDS_INT128, ((type128 *)&v)->x));
  else
    m_float = llvm::APFloat(
        llvm::APFloat::x87DoubleExtended(),
        llvm::APInt(BITWIDTH_INT128, NUM_OF_WORDS_INT128, ((type128 *)&v)->x));
  return *this;
}

Scalar &Scalar::operator=(llvm::APInt rhs) {
  m_integer = llvm::APInt(rhs);
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
  return *this;
}

Scalar::~Scalar() = default;

bool Scalar::Promote(Scalar::Type type) {
  bool success = false;
  switch (m_type) {
  case e_void:
    break;

  case e_sint:
    switch (type) {
    case e_void:
      break;
    case e_sint:
      success = true;
      break;
    case e_uint:
      m_integer = m_integer.sextOrTrunc(sizeof(uint_t) * 8);
      success = true;
      break;

    case e_slong:
      m_integer = m_integer.sextOrTrunc(sizeof(slong_t) * 8);
      success = true;
      break;

    case e_ulong:
      m_integer = m_integer.sextOrTrunc(sizeof(ulong_t) * 8);
      success = true;
      break;

    case e_slonglong:
      m_integer = m_integer.sextOrTrunc(sizeof(slonglong_t) * 8);
      success = true;
      break;

    case e_ulonglong:
      m_integer = m_integer.sextOrTrunc(sizeof(ulonglong_t) * 8);
      success = true;
      break;

    case e_sint128:
    case e_uint128:
      m_integer = m_integer.sextOrTrunc(BITWIDTH_INT128);
      success = true;
      break;

    case e_sint256:
    case e_uint256:
      m_integer = m_integer.sextOrTrunc(BITWIDTH_INT256);
      success = true;
      break;

    case e_float:
      m_float = llvm::APFloat(llvm::APFloat::IEEEsingle());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_double:
      m_float = llvm::APFloat(llvm::APFloat::IEEEdouble());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_long_double:
      m_float = llvm::APFloat(m_ieee_quad ? llvm::APFloat::IEEEquad()
                                          : llvm::APFloat::x87DoubleExtended());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;
    }
    break;

  case e_uint:
    switch (type) {
    case e_void:
    case e_sint:
      break;
    case e_uint:
      success = true;
      break;
    case e_slong:
      m_integer = m_integer.zextOrTrunc(sizeof(slong_t) * 8);
      success = true;
      break;

    case e_ulong:
      m_integer = m_integer.zextOrTrunc(sizeof(ulong_t) * 8);
      success = true;
      break;

    case e_slonglong:
      m_integer = m_integer.zextOrTrunc(sizeof(slonglong_t) * 8);
      success = true;
      break;

    case e_ulonglong:
      m_integer = m_integer.zextOrTrunc(sizeof(ulonglong_t) * 8);
      success = true;
      break;

    case e_sint128:
    case e_uint128:
      m_integer = m_integer.zextOrTrunc(BITWIDTH_INT128);
      success = true;
      break;

    case e_sint256:
    case e_uint256:
      m_integer = m_integer.zextOrTrunc(BITWIDTH_INT256);
      success = true;
      break;

    case e_float:
      m_float = llvm::APFloat(llvm::APFloat::IEEEsingle());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_double:
      m_float = llvm::APFloat(llvm::APFloat::IEEEdouble());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_long_double:
      m_float = llvm::APFloat(m_ieee_quad ? llvm::APFloat::IEEEquad()
                                          : llvm::APFloat::x87DoubleExtended());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;
    }
    break;

  case e_slong:
    switch (type) {
    case e_void:
    case e_sint:
    case e_uint:
      break;
    case e_slong:
      success = true;
      break;
    case e_ulong:
      m_integer = m_integer.sextOrTrunc(sizeof(ulong_t) * 8);
      success = true;
      break;

    case e_slonglong:
      m_integer = m_integer.sextOrTrunc(sizeof(slonglong_t) * 8);
      success = true;
      break;

    case e_ulonglong:
      m_integer = m_integer.sextOrTrunc(sizeof(ulonglong_t) * 8);
      success = true;
      break;

    case e_sint128:
    case e_uint128:
      m_integer = m_integer.sextOrTrunc(BITWIDTH_INT128);
      success = true;
      break;

    case e_sint256:
    case e_uint256:
      m_integer = m_integer.sextOrTrunc(BITWIDTH_INT256);
      success = true;
      break;

    case e_float:
      m_float = llvm::APFloat(llvm::APFloat::IEEEsingle());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_double:
      m_float = llvm::APFloat(llvm::APFloat::IEEEdouble());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_long_double:
      m_float = llvm::APFloat(m_ieee_quad ? llvm::APFloat::IEEEquad()
                                          : llvm::APFloat::x87DoubleExtended());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;
    }
    break;

  case e_ulong:
    switch (type) {
    case e_void:
    case e_sint:
    case e_uint:
    case e_slong:
      break;
    case e_ulong:
      success = true;
      break;
    case e_slonglong:
      m_integer = m_integer.zextOrTrunc(sizeof(slonglong_t) * 8);
      success = true;
      break;

    case e_ulonglong:
      m_integer = m_integer.zextOrTrunc(sizeof(ulonglong_t) * 8);
      success = true;
      break;

    case e_sint128:
    case e_uint128:
      m_integer = m_integer.zextOrTrunc(BITWIDTH_INT128);
      success = true;
      break;

    case e_sint256:
    case e_uint256:
      m_integer = m_integer.zextOrTrunc(BITWIDTH_INT256);
      success = true;
      break;

    case e_float:
      m_float = llvm::APFloat(llvm::APFloat::IEEEsingle());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_double:
      m_float = llvm::APFloat(llvm::APFloat::IEEEdouble());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_long_double:
      m_float = llvm::APFloat(m_ieee_quad ? llvm::APFloat::IEEEquad()
                                          : llvm::APFloat::x87DoubleExtended());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;
    }
    break;

  case e_slonglong:
    switch (type) {
    case e_void:
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
      break;
    case e_slonglong:
      success = true;
      break;
    case e_ulonglong:
      m_integer = m_integer.sextOrTrunc(sizeof(ulonglong_t) * 8);
      success = true;
      break;

    case e_sint128:
    case e_uint128:
      m_integer = m_integer.sextOrTrunc(BITWIDTH_INT128);
      success = true;
      break;

    case e_sint256:
    case e_uint256:
      m_integer = m_integer.sextOrTrunc(BITWIDTH_INT256);
      success = true;
      break;

    case e_float:
      m_float = llvm::APFloat(llvm::APFloat::IEEEsingle());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_double:
      m_float = llvm::APFloat(llvm::APFloat::IEEEdouble());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_long_double:
      m_float = llvm::APFloat(m_ieee_quad ? llvm::APFloat::IEEEquad()
                                          : llvm::APFloat::x87DoubleExtended());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;
    }
    break;

  case e_ulonglong:
    switch (type) {
    case e_void:
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
      break;
    case e_ulonglong:
      success = true;
      break;
    case e_sint128:
    case e_uint128:
      m_integer = m_integer.zextOrTrunc(BITWIDTH_INT128);
      success = true;
      break;

    case e_sint256:
    case e_uint256:
      m_integer = m_integer.zextOrTrunc(BITWIDTH_INT256);
      success = true;
      break;

    case e_float:
      m_float = llvm::APFloat(llvm::APFloat::IEEEsingle());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_double:
      m_float = llvm::APFloat(llvm::APFloat::IEEEdouble());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_long_double:
      m_float = llvm::APFloat(m_ieee_quad ? llvm::APFloat::IEEEquad()
                                          : llvm::APFloat::x87DoubleExtended());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;
    }
    break;

  case e_sint128:
    switch (type) {
    case e_void:
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
    case e_ulonglong:
      break;
    case e_sint128:
      success = true;
      break;
    case e_uint128:
      m_integer = m_integer.sextOrTrunc(BITWIDTH_INT128);
      success = true;
      break;

    case e_sint256:
    case e_uint256:
      m_integer = m_integer.sextOrTrunc(BITWIDTH_INT256);
      success = true;
      break;

    case e_float:
      m_float = llvm::APFloat(llvm::APFloat::IEEEsingle());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_double:
      m_float = llvm::APFloat(llvm::APFloat::IEEEdouble());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_long_double:
      m_float = llvm::APFloat(m_ieee_quad ? llvm::APFloat::IEEEquad()
                                          : llvm::APFloat::x87DoubleExtended());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;
    }
    break;

  case e_uint128:
    switch (type) {
    case e_void:
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
    case e_ulonglong:
    case e_sint128:
      break;
    case e_uint128:
      success = true;
      break;
    case e_sint256:
    case e_uint256:
      m_integer = m_integer.zextOrTrunc(BITWIDTH_INT256);
      success = true;
      break;

    case e_float:
      m_float = llvm::APFloat(llvm::APFloat::IEEEsingle());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_double:
      m_float = llvm::APFloat(llvm::APFloat::IEEEdouble());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_long_double:
      m_float = llvm::APFloat(m_ieee_quad ? llvm::APFloat::IEEEquad()
                                          : llvm::APFloat::x87DoubleExtended());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;
    }
    break;

  case e_sint256:
    switch (type) {
    case e_void:
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
    case e_ulonglong:
    case e_sint128:
    case e_uint128:
      break;
    case e_sint256:
      success = true;
      break;
    case e_uint256:
      m_integer = m_integer.sextOrTrunc(BITWIDTH_INT256);
      success = true;
      break;

    case e_float:
      m_float = llvm::APFloat(llvm::APFloat::IEEEsingle());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_double:
      m_float = llvm::APFloat(llvm::APFloat::IEEEdouble());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_long_double:
      m_float = llvm::APFloat(m_ieee_quad ? llvm::APFloat::IEEEquad()
                                          : llvm::APFloat::x87DoubleExtended());
      m_float.convertFromAPInt(m_integer, true,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;
    }
    break;

  case e_uint256:
    switch (type) {
    case e_void:
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
    case e_ulonglong:
    case e_sint128:
    case e_uint128:
    case e_sint256:
      break;
    case e_uint256:
      success = true;
      break;
    case e_float:
      m_float = llvm::APFloat(llvm::APFloat::IEEEsingle());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_double:
      m_float = llvm::APFloat(llvm::APFloat::IEEEdouble());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;

    case e_long_double:
      m_float = llvm::APFloat(m_ieee_quad ? llvm::APFloat::IEEEquad()
                                          : llvm::APFloat::x87DoubleExtended());
      m_float.convertFromAPInt(m_integer, false,
                               llvm::APFloat::rmNearestTiesToEven);
      success = true;
      break;
    }
    break;

  case e_float:
    switch (type) {
    case e_void:
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
    case e_ulonglong:
    case e_sint128:
    case e_uint128:
    case e_sint256:
    case e_uint256:
      break;
    case e_float:
      success = true;
      break;
    case e_double:
      m_float = llvm::APFloat((double_t)m_float.convertToFloat());
      success = true;
      break;

    case e_long_double: {
      bool ignore;
      m_float.convert(m_ieee_quad ? llvm::APFloat::IEEEquad()
                                  : llvm::APFloat::x87DoubleExtended(),
                      llvm::APFloat::rmNearestTiesToEven, &ignore);
      success = true;
      break;
    }
    }
    break;

  case e_double:
    switch (type) {
    case e_void:
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
    case e_ulonglong:
    case e_sint128:
    case e_uint128:
    case e_sint256:
    case e_uint256:
    case e_float:
      break;
    case e_double:
      success = true;
      break;
    case e_long_double: {
      bool ignore;
      m_float.convert(m_ieee_quad ? llvm::APFloat::IEEEquad()
                                  : llvm::APFloat::x87DoubleExtended(),
                      llvm::APFloat::rmNearestTiesToEven, &ignore);
      success = true;
      break;
    }
    }
    break;

  case e_long_double:
    switch (type) {
    case e_void:
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
    case e_ulonglong:
    case e_sint128:
    case e_uint128:
    case e_sint256:
    case e_uint256:
    case e_float:
    case e_double:
      break;
    case e_long_double:
      success = true;
      break;
    }
    break;
  }

  if (success)
    m_type = type;
  return success;
}

const char *Scalar::GetValueTypeAsCString(Scalar::Type type) {
  switch (type) {
  case e_void:
    return "void";
  case e_sint:
    return "int";
  case e_uint:
    return "unsigned int";
  case e_slong:
    return "long";
  case e_ulong:
    return "unsigned long";
  case e_slonglong:
    return "long long";
  case e_ulonglong:
    return "unsigned long long";
  case e_float:
    return "float";
  case e_double:
    return "double";
  case e_long_double:
    return "long double";
  case e_sint128:
    return "int128_t";
  case e_uint128:
    return "uint128_t";
  case e_sint256:
    return "int256_t";
  case e_uint256:
    return "uint256_t";
  }
  return "???";
}

Scalar::Type
Scalar::GetValueTypeForSignedIntegerWithByteSize(size_t byte_size) {
  if (byte_size <= sizeof(sint_t))
    return e_sint;
  if (byte_size <= sizeof(slong_t))
    return e_slong;
  if (byte_size <= sizeof(slonglong_t))
    return e_slonglong;
  return e_void;
}

Scalar::Type
Scalar::GetValueTypeForUnsignedIntegerWithByteSize(size_t byte_size) {
  if (byte_size <= sizeof(uint_t))
    return e_uint;
  if (byte_size <= sizeof(ulong_t))
    return e_ulong;
  if (byte_size <= sizeof(ulonglong_t))
    return e_ulonglong;
  return e_void;
}

Scalar::Type Scalar::GetValueTypeForFloatWithByteSize(size_t byte_size) {
  if (byte_size == sizeof(float_t))
    return e_float;
  if (byte_size == sizeof(double_t))
    return e_double;
  if (byte_size == sizeof(long_double_t))
    return e_long_double;
  return e_void;
}

bool Scalar::MakeSigned() {
  bool success = false;

  switch (m_type) {
  case e_void:
    break;
  case e_sint:
    success = true;
    break;
  case e_uint:
    m_type = e_sint;
    success = true;
    break;
  case e_slong:
    success = true;
    break;
  case e_ulong:
    m_type = e_slong;
    success = true;
    break;
  case e_slonglong:
    success = true;
    break;
  case e_ulonglong:
    m_type = e_slonglong;
    success = true;
    break;
  case e_sint128:
    success = true;
    break;
  case e_uint128:
    m_type = e_sint128;
    success = true;
    break;
  case e_sint256:
    success = true;
    break;
  case e_uint256:
    m_type = e_sint256;
    success = true;
    break;
  case e_float:
    success = true;
    break;
  case e_double:
    success = true;
    break;
  case e_long_double:
    success = true;
    break;
  }

  return success;
}

bool Scalar::MakeUnsigned() {
  bool success = false;

  switch (m_type) {
  case e_void:
    break;
  case e_sint:
    m_type = e_uint;
    success = true;
    break;
  case e_uint:
    success = true;
    break;
  case e_slong:
    m_type = e_ulong;
    success = true;
    break;
  case e_ulong:
    success = true;
    break;
  case e_slonglong:
    m_type = e_ulonglong;
    success = true;
    break;
  case e_ulonglong:
    success = true;
    break;
  case e_sint128:
    m_type = e_uint128;
    success = true;
    break;
  case e_uint128:
    success = true;
    break;
  case e_sint256:
    m_type = e_uint256;
    success = true;
    break;
  case e_uint256:
    success = true;
    break;
  case e_float:
    success = true;
    break;
  case e_double:
    success = true;
    break;
  case e_long_double:
    success = true;
    break;
  }

  return success;
}

signed char Scalar::SChar(char fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return (schar_t)(m_integer.sextOrTrunc(sizeof(schar_t) * 8)).getSExtValue();
  case e_float:
    return (schar_t)m_float.convertToFloat();
  case e_double:
    return (schar_t)m_float.convertToDouble();
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return (schar_t)(ldbl_val.sextOrTrunc(sizeof(schar_t) * 8)).getSExtValue();
  }
  return fail_value;
}

unsigned char Scalar::UChar(unsigned char fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return (uchar_t)(m_integer.zextOrTrunc(sizeof(uchar_t) * 8)).getZExtValue();
  case e_float:
    return (uchar_t)m_float.convertToFloat();
  case e_double:
    return (uchar_t)m_float.convertToDouble();
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return (uchar_t)(ldbl_val.zextOrTrunc(sizeof(uchar_t) * 8)).getZExtValue();
  }
  return fail_value;
}

short Scalar::SShort(short fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return (sshort_t)(m_integer.sextOrTrunc(sizeof(sshort_t) * 8))
        .getSExtValue();
  case e_float:
    return (sshort_t)m_float.convertToFloat();
  case e_double:
    return (sshort_t)m_float.convertToDouble();
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return (sshort_t)(ldbl_val.sextOrTrunc(sizeof(sshort_t) * 8))
        .getSExtValue();
  }
  return fail_value;
}

unsigned short Scalar::UShort(unsigned short fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return (ushort_t)(m_integer.zextOrTrunc(sizeof(ushort_t) * 8))
        .getZExtValue();
  case e_float:
    return (ushort_t)m_float.convertToFloat();
  case e_double:
    return (ushort_t)m_float.convertToDouble();
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return (ushort_t)(ldbl_val.zextOrTrunc(sizeof(ushort_t) * 8))
        .getZExtValue();
  }
  return fail_value;
}

int Scalar::SInt(int fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return (sint_t)(m_integer.sextOrTrunc(sizeof(sint_t) * 8)).getSExtValue();
  case e_float:
    return (sint_t)m_float.convertToFloat();
  case e_double:
    return (sint_t)m_float.convertToDouble();
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return (sint_t)(ldbl_val.sextOrTrunc(sizeof(sint_t) * 8)).getSExtValue();
  }
  return fail_value;
}

unsigned int Scalar::UInt(unsigned int fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return (uint_t)(m_integer.zextOrTrunc(sizeof(uint_t) * 8)).getZExtValue();
  case e_float:
    return (uint_t)m_float.convertToFloat();
  case e_double:
    return (uint_t)m_float.convertToDouble();
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return (uint_t)(ldbl_val.zextOrTrunc(sizeof(uint_t) * 8)).getZExtValue();
  }
  return fail_value;
}

long Scalar::SLong(long fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return (slong_t)(m_integer.sextOrTrunc(sizeof(slong_t) * 8)).getSExtValue();
  case e_float:
    return (slong_t)m_float.convertToFloat();
  case e_double:
    return (slong_t)m_float.convertToDouble();
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return (slong_t)(ldbl_val.sextOrTrunc(sizeof(slong_t) * 8)).getSExtValue();
  }
  return fail_value;
}

unsigned long Scalar::ULong(unsigned long fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return (ulong_t)(m_integer.zextOrTrunc(sizeof(ulong_t) * 8)).getZExtValue();
  case e_float:
    return (ulong_t)m_float.convertToFloat();
  case e_double:
    return (ulong_t)m_float.convertToDouble();
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return (ulong_t)(ldbl_val.zextOrTrunc(sizeof(ulong_t) * 8)).getZExtValue();
  }
  return fail_value;
}

long long Scalar::SLongLong(long long fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return (slonglong_t)(m_integer.sextOrTrunc(sizeof(slonglong_t) * 8))
        .getSExtValue();
  case e_float:
    return (slonglong_t)m_float.convertToFloat();
  case e_double:
    return (slonglong_t)m_float.convertToDouble();
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return (slonglong_t)(ldbl_val.sextOrTrunc(sizeof(slonglong_t) * 8))
        .getSExtValue();
  }
  return fail_value;
}

unsigned long long Scalar::ULongLong(unsigned long long fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return (ulonglong_t)(m_integer.zextOrTrunc(sizeof(ulonglong_t) * 8))
        .getZExtValue();
  case e_float:
    return (ulonglong_t)m_float.convertToFloat();
  case e_double: {
    double d_val = m_float.convertToDouble();
    llvm::APInt rounded_double =
        llvm::APIntOps::RoundDoubleToAPInt(d_val, sizeof(ulonglong_t) * 8);
    return (ulonglong_t)(rounded_double.zextOrTrunc(sizeof(ulonglong_t) * 8))
        .getZExtValue();
  }
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return (ulonglong_t)(ldbl_val.zextOrTrunc(sizeof(ulonglong_t) * 8))
        .getZExtValue();
  }
  return fail_value;
}

llvm::APInt Scalar::SInt128(llvm::APInt &fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return m_integer;
  case e_float:
  case e_double:
  case e_long_double:
    return m_float.bitcastToAPInt();
  }
  return fail_value;
}

llvm::APInt Scalar::UInt128(const llvm::APInt &fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return m_integer;
  case e_float:
  case e_double:
  case e_long_double:
    return m_float.bitcastToAPInt();
  }
  return fail_value;
}

llvm::APInt Scalar::SInt256(llvm::APInt &fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return m_integer;
  case e_float:
  case e_double:
  case e_long_double:
    return m_float.bitcastToAPInt();
  }
  return fail_value;
}

llvm::APInt Scalar::UInt256(const llvm::APInt &fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return m_integer;
  case e_float:
  case e_double:
  case e_long_double:
    return m_float.bitcastToAPInt();
  }
  return fail_value;
}

float Scalar::Float(float fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return llvm::APIntOps::RoundAPIntToFloat(m_integer);
  case e_float:
    return m_float.convertToFloat();
  case e_double:
    return (float_t)m_float.convertToDouble();
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return ldbl_val.bitsToFloat();
  }
  return fail_value;
}

double Scalar::Double(double fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return llvm::APIntOps::RoundAPIntToDouble(m_integer);
  case e_float:
    return (double_t)m_float.convertToFloat();
  case e_double:
    return m_float.convertToDouble();
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return ldbl_val.bitsToFloat();
  }
  return fail_value;
}

long double Scalar::LongDouble(long double fail_value) const {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    return (long_double_t)llvm::APIntOps::RoundAPIntToDouble(m_integer);
  case e_float:
    return (long_double_t)m_float.convertToFloat();
  case e_double:
    return (long_double_t)m_float.convertToDouble();
  case e_long_double:
    llvm::APInt ldbl_val = m_float.bitcastToAPInt();
    return (long_double_t)ldbl_val.bitsToDouble();
  }
  return fail_value;
}

Scalar &Scalar::operator+=(const Scalar &rhs) {
  Scalar temp_value;
  const Scalar *a;
  const Scalar *b;
  if ((m_type = PromoteToMaxType(*this, rhs, temp_value, a, b)) !=
      Scalar::e_void) {
    switch (m_type) {
    case e_void:
      break;
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
    case e_ulonglong:
    case e_sint128:
    case e_uint128:
    case e_sint256:
    case e_uint256:
      m_integer = a->m_integer + b->m_integer;
      break;

    case e_float:
    case e_double:
    case e_long_double:
      m_float = a->m_float + b->m_float;
      break;
    }
  }
  return *this;
}

Scalar &Scalar::operator<<=(const Scalar &rhs) {
  switch (m_type) {
  case e_void:
  case e_float:
  case e_double:
  case e_long_double:
    m_type = e_void;
    break;

  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    switch (rhs.m_type) {
    case e_void:
    case e_float:
    case e_double:
    case e_long_double:
      m_type = e_void;
      break;
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
    case e_ulonglong:
    case e_sint128:
    case e_uint128:
    case e_sint256:
    case e_uint256:
      m_integer = m_integer << rhs.m_integer;
      break;
    }
    break;
  }
  return *this;
}

bool Scalar::ShiftRightLogical(const Scalar &rhs) {
  switch (m_type) {
  case e_void:
  case e_float:
  case e_double:
  case e_long_double:
    m_type = e_void;
    break;

  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    switch (rhs.m_type) {
    case e_void:
    case e_float:
    case e_double:
    case e_long_double:
      m_type = e_void;
      break;
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
    case e_ulonglong:
    case e_sint128:
    case e_uint128:
    case e_sint256:
    case e_uint256:
      m_integer = m_integer.lshr(rhs.m_integer);
      break;
    }
    break;
  }
  return m_type != e_void;
}

Scalar &Scalar::operator>>=(const Scalar &rhs) {
  switch (m_type) {
  case e_void:
  case e_float:
  case e_double:
  case e_long_double:
    m_type = e_void;
    break;

  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    switch (rhs.m_type) {
    case e_void:
    case e_float:
    case e_double:
    case e_long_double:
      m_type = e_void;
      break;
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
    case e_ulonglong:
    case e_sint128:
    case e_uint128:
    case e_sint256:
    case e_uint256:
      m_integer = m_integer.ashr(rhs.m_integer);
      break;
    }
    break;
  }
  return *this;
}

Scalar &Scalar::operator&=(const Scalar &rhs) {
  switch (m_type) {
  case e_void:
  case e_float:
  case e_double:
  case e_long_double:
    m_type = e_void;
    break;

  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    switch (rhs.m_type) {
    case e_void:
    case e_float:
    case e_double:
    case e_long_double:
      m_type = e_void;
      break;
    case e_sint:
    case e_uint:
    case e_slong:
    case e_ulong:
    case e_slonglong:
    case e_ulonglong:
    case e_sint128:
    case e_uint128:
    case e_sint256:
    case e_uint256:
      m_integer &= rhs.m_integer;
      break;
    }
    break;
  }
  return *this;
}

bool Scalar::AbsoluteValue() {
  switch (m_type) {
  case e_void:
    break;

  case e_sint:
  case e_slong:
  case e_slonglong:
  case e_sint128:
  case e_sint256:
    if (m_integer.isNegative())
      m_integer = -m_integer;
    return true;

  case e_uint:
  case e_ulong:
  case e_ulonglong:
    return true;
  case e_uint128:
  case e_uint256:
  case e_float:
  case e_double:
  case e_long_double:
    m_float.clearSign();
    return true;
  }
  return false;
}

bool Scalar::UnaryNegate() {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    m_integer = -m_integer;
    return true;
  case e_float:
  case e_double:
  case e_long_double:
    m_float.changeSign();
    return true;
  }
  return false;
}

bool Scalar::OnesComplement() {
  switch (m_type) {
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    m_integer = ~m_integer;
    return true;

  case e_void:
  case e_float:
  case e_double:
  case e_long_double:
    break;
  }
  return false;
}

const Scalar lldb_private::operator+(const Scalar &lhs, const Scalar &rhs) {
  Scalar result;
  Scalar temp_value;
  const Scalar *a;
  const Scalar *b;
  if ((result.m_type = PromoteToMaxType(lhs, rhs, temp_value, a, b)) !=
      Scalar::e_void) {
    switch (result.m_type) {
    case Scalar::e_void:
      break;
    case Scalar::e_sint:
    case Scalar::e_uint:
    case Scalar::e_slong:
    case Scalar::e_ulong:
    case Scalar::e_slonglong:
    case Scalar::e_ulonglong:
    case Scalar::e_sint128:
    case Scalar::e_uint128:
    case Scalar::e_sint256:
    case Scalar::e_uint256:
      result.m_integer = a->m_integer + b->m_integer;
      break;
    case Scalar::e_float:
    case Scalar::e_double:
    case Scalar::e_long_double:
      result.m_float = a->m_float + b->m_float;
      break;
    }
  }
  return result;
}

const Scalar lldb_private::operator-(const Scalar &lhs, const Scalar &rhs) {
  Scalar result;
  Scalar temp_value;
  const Scalar *a;
  const Scalar *b;
  if ((result.m_type = PromoteToMaxType(lhs, rhs, temp_value, a, b)) !=
      Scalar::e_void) {
    switch (result.m_type) {
    case Scalar::e_void:
      break;
    case Scalar::e_sint:
    case Scalar::e_uint:
    case Scalar::e_slong:
    case Scalar::e_ulong:
    case Scalar::e_slonglong:
    case Scalar::e_ulonglong:
    case Scalar::e_sint128:
    case Scalar::e_uint128:
    case Scalar::e_sint256:
    case Scalar::e_uint256:
      result.m_integer = a->m_integer - b->m_integer;
      break;
    case Scalar::e_float:
    case Scalar::e_double:
    case Scalar::e_long_double:
      result.m_float = a->m_float - b->m_float;
      break;
    }
  }
  return result;
}

const Scalar lldb_private::operator/(const Scalar &lhs, const Scalar &rhs) {
  Scalar result;
  Scalar temp_value;
  const Scalar *a;
  const Scalar *b;
  if ((result.m_type = PromoteToMaxType(lhs, rhs, temp_value, a, b)) !=
      Scalar::e_void) {
    switch (result.m_type) {
    case Scalar::e_void:
      break;
    case Scalar::e_sint:
    case Scalar::e_slong:
    case Scalar::e_slonglong:
    case Scalar::e_sint128:
    case Scalar::e_sint256:
      if (b->m_integer != 0) {
        result.m_integer = a->m_integer.sdiv(b->m_integer);
        return result;
      }
      break;
    case Scalar::e_uint:
    case Scalar::e_ulong:
    case Scalar::e_ulonglong:
    case Scalar::e_uint128:
    case Scalar::e_uint256:
      if (b->m_integer != 0) {
        result.m_integer = a->m_integer.udiv(b->m_integer);
        return result;
      }
      break;
    case Scalar::e_float:
    case Scalar::e_double:
    case Scalar::e_long_double:
      if (!b->m_float.isZero()) {
        result.m_float = a->m_float / b->m_float;
        return result;
      }
      break;
    }
  }
  // For division only, the only way it should make it here is if a promotion
  // failed, or if we are trying to do a divide by zero.
  result.m_type = Scalar::e_void;
  return result;
}

const Scalar lldb_private::operator*(const Scalar &lhs, const Scalar &rhs) {
  Scalar result;
  Scalar temp_value;
  const Scalar *a;
  const Scalar *b;
  if ((result.m_type = PromoteToMaxType(lhs, rhs, temp_value, a, b)) !=
      Scalar::e_void) {
    switch (result.m_type) {
    case Scalar::e_void:
      break;
    case Scalar::e_sint:
    case Scalar::e_uint:
    case Scalar::e_slong:
    case Scalar::e_ulong:
    case Scalar::e_slonglong:
    case Scalar::e_ulonglong:
    case Scalar::e_sint128:
    case Scalar::e_uint128:
    case Scalar::e_sint256:
    case Scalar::e_uint256:
      result.m_integer = a->m_integer * b->m_integer;
      break;
    case Scalar::e_float:
    case Scalar::e_double:
    case Scalar::e_long_double:
      result.m_float = a->m_float * b->m_float;
      break;
    }
  }
  return result;
}

const Scalar lldb_private::operator&(const Scalar &lhs, const Scalar &rhs) {
  Scalar result;
  Scalar temp_value;
  const Scalar *a;
  const Scalar *b;
  if ((result.m_type = PromoteToMaxType(lhs, rhs, temp_value, a, b)) !=
      Scalar::e_void) {
    switch (result.m_type) {
    case Scalar::e_sint:
    case Scalar::e_uint:
    case Scalar::e_slong:
    case Scalar::e_ulong:
    case Scalar::e_slonglong:
    case Scalar::e_ulonglong:
    case Scalar::e_sint128:
    case Scalar::e_uint128:
    case Scalar::e_sint256:
    case Scalar::e_uint256:
      result.m_integer = a->m_integer & b->m_integer;
      break;
    case Scalar::e_void:
    case Scalar::e_float:
    case Scalar::e_double:
    case Scalar::e_long_double:
      // No bitwise AND on floats, doubles of long doubles
      result.m_type = Scalar::e_void;
      break;
    }
  }
  return result;
}

const Scalar lldb_private::operator|(const Scalar &lhs, const Scalar &rhs) {
  Scalar result;
  Scalar temp_value;
  const Scalar *a;
  const Scalar *b;
  if ((result.m_type = PromoteToMaxType(lhs, rhs, temp_value, a, b)) !=
      Scalar::e_void) {
    switch (result.m_type) {
    case Scalar::e_sint:
    case Scalar::e_uint:
    case Scalar::e_slong:
    case Scalar::e_ulong:
    case Scalar::e_slonglong:
    case Scalar::e_ulonglong:
    case Scalar::e_sint128:
    case Scalar::e_uint128:
    case Scalar::e_sint256:
    case Scalar::e_uint256:
      result.m_integer = a->m_integer | b->m_integer;
      break;

    case Scalar::e_void:
    case Scalar::e_float:
    case Scalar::e_double:
    case Scalar::e_long_double:
      // No bitwise AND on floats, doubles of long doubles
      result.m_type = Scalar::e_void;
      break;
    }
  }
  return result;
}

const Scalar lldb_private::operator%(const Scalar &lhs, const Scalar &rhs) {
  Scalar result;
  Scalar temp_value;
  const Scalar *a;
  const Scalar *b;
  if ((result.m_type = PromoteToMaxType(lhs, rhs, temp_value, a, b)) !=
      Scalar::e_void) {
    switch (result.m_type) {
    default:
      break;
    case Scalar::e_void:
      break;
    case Scalar::e_sint:
    case Scalar::e_slong:
    case Scalar::e_slonglong:
    case Scalar::e_sint128:
    case Scalar::e_sint256:
      if (b->m_integer != 0) {
        result.m_integer = a->m_integer.srem(b->m_integer);
        return result;
      }
      break;
    case Scalar::e_uint:
    case Scalar::e_ulong:
    case Scalar::e_ulonglong:
    case Scalar::e_uint128:
    case Scalar::e_uint256:
      if (b->m_integer != 0) {
        result.m_integer = a->m_integer.urem(b->m_integer);
        return result;
      }
      break;
    }
  }
  result.m_type = Scalar::e_void;
  return result;
}

const Scalar lldb_private::operator^(const Scalar &lhs, const Scalar &rhs) {
  Scalar result;
  Scalar temp_value;
  const Scalar *a;
  const Scalar *b;
  if ((result.m_type = PromoteToMaxType(lhs, rhs, temp_value, a, b)) !=
      Scalar::e_void) {
    switch (result.m_type) {
    case Scalar::e_sint:
    case Scalar::e_uint:
    case Scalar::e_slong:
    case Scalar::e_ulong:
    case Scalar::e_slonglong:
    case Scalar::e_ulonglong:
    case Scalar::e_sint128:
    case Scalar::e_uint128:
    case Scalar::e_sint256:
    case Scalar::e_uint256:
      result.m_integer = a->m_integer ^ b->m_integer;
      break;

    case Scalar::e_void:
    case Scalar::e_float:
    case Scalar::e_double:
    case Scalar::e_long_double:
      // No bitwise AND on floats, doubles of long doubles
      result.m_type = Scalar::e_void;
      break;
    }
  }
  return result;
}

const Scalar lldb_private::operator<<(const Scalar &lhs, const Scalar &rhs) {
  Scalar result = lhs;
  result <<= rhs;
  return result;
}

const Scalar lldb_private::operator>>(const Scalar &lhs, const Scalar &rhs) {
  Scalar result = lhs;
  result >>= rhs;
  return result;
}

Status Scalar::SetValueFromCString(const char *value_str, Encoding encoding,
                                   size_t byte_size) {
  Status error;
  if (value_str == nullptr || value_str[0] == '\0') {
    error.SetErrorString("Invalid c-string value string.");
    return error;
  }
  switch (encoding) {
  case eEncodingInvalid:
    error.SetErrorString("Invalid encoding.");
    break;

  case eEncodingUint:
    if (byte_size <= sizeof(uint64_t)) {
      uint64_t uval64;
      if (!llvm::to_integer(value_str, uval64))
        error.SetErrorStringWithFormat(
            "'%s' is not a valid unsigned integer string value", value_str);
      else if (!UIntValueIsValidForSize(uval64, byte_size))
        error.SetErrorStringWithFormat("value 0x%" PRIx64
                                       " is too large to fit in a %" PRIu64
                                       " byte unsigned integer value",
                                       uval64, (uint64_t)byte_size);
      else {
        m_type = Scalar::GetValueTypeForUnsignedIntegerWithByteSize(byte_size);
        switch (m_type) {
        case e_uint:
          m_integer = llvm::APInt(sizeof(uint_t) * 8, uval64, false);
          break;
        case e_ulong:
          m_integer = llvm::APInt(sizeof(ulong_t) * 8, uval64, false);
          break;
        case e_ulonglong:
          m_integer = llvm::APInt(sizeof(ulonglong_t) * 8, uval64, false);
          break;
        default:
          error.SetErrorStringWithFormat(
              "unsupported unsigned integer byte size: %" PRIu64 "",
              (uint64_t)byte_size);
          break;
        }
      }
    } else {
      error.SetErrorStringWithFormat(
          "unsupported unsigned integer byte size: %" PRIu64 "",
          (uint64_t)byte_size);
      return error;
    }
    break;

  case eEncodingSint:
    if (byte_size <= sizeof(int64_t)) {
      int64_t sval64;
      if (!llvm::to_integer(value_str, sval64))
        error.SetErrorStringWithFormat(
            "'%s' is not a valid signed integer string value", value_str);
      else if (!SIntValueIsValidForSize(sval64, byte_size))
        error.SetErrorStringWithFormat("value 0x%" PRIx64
                                       " is too large to fit in a %" PRIu64
                                       " byte signed integer value",
                                       sval64, (uint64_t)byte_size);
      else {
        m_type = Scalar::GetValueTypeForSignedIntegerWithByteSize(byte_size);
        switch (m_type) {
        case e_sint:
          m_integer = llvm::APInt(sizeof(sint_t) * 8, sval64, true);
          break;
        case e_slong:
          m_integer = llvm::APInt(sizeof(slong_t) * 8, sval64, true);
          break;
        case e_slonglong:
          m_integer = llvm::APInt(sizeof(slonglong_t) * 8, sval64, true);
          break;
        default:
          error.SetErrorStringWithFormat(
              "unsupported signed integer byte size: %" PRIu64 "",
              (uint64_t)byte_size);
          break;
        }
      }
    } else {
      error.SetErrorStringWithFormat(
          "unsupported signed integer byte size: %" PRIu64 "",
          (uint64_t)byte_size);
      return error;
    }
    break;

  case eEncodingIEEE754:
    static float f_val;
    static double d_val;
    static long double l_val;
    if (byte_size == sizeof(float)) {
      if (::sscanf(value_str, "%f", &f_val) == 1) {
        m_float = llvm::APFloat(f_val);
        m_type = e_float;
      } else
        error.SetErrorStringWithFormat("'%s' is not a valid float string value",
                                       value_str);
    } else if (byte_size == sizeof(double)) {
      if (::sscanf(value_str, "%lf", &d_val) == 1) {
        m_float = llvm::APFloat(d_val);
        m_type = e_double;
      } else
        error.SetErrorStringWithFormat("'%s' is not a valid float string value",
                                       value_str);
    } else if (byte_size == sizeof(long double)) {
      if (::sscanf(value_str, "%Lf", &l_val) == 1) {
        m_float =
            llvm::APFloat(llvm::APFloat::x87DoubleExtended(),
                          llvm::APInt(BITWIDTH_INT128, NUM_OF_WORDS_INT128,
                                      ((type128 *)&l_val)->x));
        m_type = e_long_double;
      } else
        error.SetErrorStringWithFormat("'%s' is not a valid float string value",
                                       value_str);
    } else {
      error.SetErrorStringWithFormat("unsupported float byte size: %" PRIu64 "",
                                     (uint64_t)byte_size);
      return error;
    }
    break;

  case eEncodingVector:
    error.SetErrorString("vector encoding unsupported.");
    break;
  }
  if (error.Fail())
    m_type = e_void;

  return error;
}

Status Scalar::SetValueFromData(DataExtractor &data, lldb::Encoding encoding,
                                size_t byte_size) {
  Status error;

  type128 int128;
  type256 int256;
  switch (encoding) {
  case lldb::eEncodingInvalid:
    error.SetErrorString("invalid encoding");
    break;
  case lldb::eEncodingVector:
    error.SetErrorString("vector encoding unsupported");
    break;
  case lldb::eEncodingUint: {
    lldb::offset_t offset = 0;

    switch (byte_size) {
    case 1:
      operator=((uint8_t)data.GetU8(&offset));
      break;
    case 2:
      operator=((uint16_t)data.GetU16(&offset));
      break;
    case 4:
      operator=((uint32_t)data.GetU32(&offset));
      break;
    case 8:
      operator=((uint64_t)data.GetU64(&offset));
      break;
    case 16:
      if (data.GetByteOrder() == eByteOrderBig) {
        int128.x[1] = (uint64_t)data.GetU64(&offset);
        int128.x[0] = (uint64_t)data.GetU64(&offset);
      } else {
        int128.x[0] = (uint64_t)data.GetU64(&offset);
        int128.x[1] = (uint64_t)data.GetU64(&offset);
      }
      operator=(llvm::APInt(BITWIDTH_INT128, NUM_OF_WORDS_INT128, int128.x));
      break;
    case 32:
      if (data.GetByteOrder() == eByteOrderBig) {
        int256.x[3] = (uint64_t)data.GetU64(&offset);
        int256.x[2] = (uint64_t)data.GetU64(&offset);
        int256.x[1] = (uint64_t)data.GetU64(&offset);
        int256.x[0] = (uint64_t)data.GetU64(&offset);
      } else {
        int256.x[0] = (uint64_t)data.GetU64(&offset);
        int256.x[1] = (uint64_t)data.GetU64(&offset);
        int256.x[2] = (uint64_t)data.GetU64(&offset);
        int256.x[3] = (uint64_t)data.GetU64(&offset);
      }
      operator=(llvm::APInt(BITWIDTH_INT256, NUM_OF_WORDS_INT256, int256.x));
      break;
    default:
      error.SetErrorStringWithFormat(
          "unsupported unsigned integer byte size: %" PRIu64 "",
          (uint64_t)byte_size);
      break;
    }
  } break;
  case lldb::eEncodingSint: {
    lldb::offset_t offset = 0;

    switch (byte_size) {
    case 1:
      operator=((int8_t)data.GetU8(&offset));
      break;
    case 2:
      operator=((int16_t)data.GetU16(&offset));
      break;
    case 4:
      operator=((int32_t)data.GetU32(&offset));
      break;
    case 8:
      operator=((int64_t)data.GetU64(&offset));
      break;
    case 16:
      if (data.GetByteOrder() == eByteOrderBig) {
        int128.x[1] = (uint64_t)data.GetU64(&offset);
        int128.x[0] = (uint64_t)data.GetU64(&offset);
      } else {
        int128.x[0] = (uint64_t)data.GetU64(&offset);
        int128.x[1] = (uint64_t)data.GetU64(&offset);
      }
      operator=(llvm::APInt(BITWIDTH_INT128, NUM_OF_WORDS_INT128, int128.x));
      break;
    case 32:
      if (data.GetByteOrder() == eByteOrderBig) {
        int256.x[3] = (uint64_t)data.GetU64(&offset);
        int256.x[2] = (uint64_t)data.GetU64(&offset);
        int256.x[1] = (uint64_t)data.GetU64(&offset);
        int256.x[0] = (uint64_t)data.GetU64(&offset);
      } else {
        int256.x[0] = (uint64_t)data.GetU64(&offset);
        int256.x[1] = (uint64_t)data.GetU64(&offset);
        int256.x[2] = (uint64_t)data.GetU64(&offset);
        int256.x[3] = (uint64_t)data.GetU64(&offset);
      }
      operator=(llvm::APInt(BITWIDTH_INT256, NUM_OF_WORDS_INT256, int256.x));
      break;
    default:
      error.SetErrorStringWithFormat(
          "unsupported signed integer byte size: %" PRIu64 "",
          (uint64_t)byte_size);
      break;
    }
  } break;
  case lldb::eEncodingIEEE754: {
    lldb::offset_t offset = 0;

    if (byte_size == sizeof(float))
      operator=((float)data.GetFloat(&offset));
    else if (byte_size == sizeof(double))
      operator=((double)data.GetDouble(&offset));
    else if (byte_size == sizeof(long double))
      operator=((long double)data.GetLongDouble(&offset));
    else
      error.SetErrorStringWithFormat("unsupported float byte size: %" PRIu64 "",
                                     (uint64_t)byte_size);
  } break;
  }

  return error;
}

bool Scalar::SignExtend(uint32_t sign_bit_pos) {
  const uint32_t max_bit_pos = GetByteSize() * 8;

  if (sign_bit_pos < max_bit_pos) {
    switch (m_type) {
    case Scalar::e_void:
    case Scalar::e_float:
    case Scalar::e_double:
    case Scalar::e_long_double:
      return false;

    case Scalar::e_sint:
    case Scalar::e_uint:
    case Scalar::e_slong:
    case Scalar::e_ulong:
    case Scalar::e_slonglong:
    case Scalar::e_ulonglong:
    case Scalar::e_sint128:
    case Scalar::e_uint128:
    case Scalar::e_sint256:
    case Scalar::e_uint256:
      if (max_bit_pos == sign_bit_pos)
        return true;
      else if (sign_bit_pos < (max_bit_pos - 1)) {
        llvm::APInt sign_bit = llvm::APInt::getSignMask(sign_bit_pos + 1);
        llvm::APInt bitwize_and = m_integer & sign_bit;
        if (bitwize_and.getBoolValue()) {
          const llvm::APInt mask =
              ~(sign_bit) + llvm::APInt(m_integer.getBitWidth(), 1);
          m_integer |= mask;
        }
        return true;
      }
      break;
    }
  }
  return false;
}

size_t Scalar::GetAsMemoryData(void *dst, size_t dst_len,
                               lldb::ByteOrder dst_byte_order,
                               Status &error) const {
  // Get a data extractor that points to the native scalar data
  DataExtractor data;
  if (!GetData(data)) {
    error.SetErrorString("invalid scalar value");
    return 0;
  }

  const size_t src_len = data.GetByteSize();

  // Prepare a memory buffer that contains some or all of the register value
  const size_t bytes_copied =
      data.CopyByteOrderedData(0,               // src offset
                               src_len,         // src length
                               dst,             // dst buffer
                               dst_len,         // dst length
                               dst_byte_order); // dst byte order
  if (bytes_copied == 0)
    error.SetErrorString("failed to copy data");

  return bytes_copied;
}

bool Scalar::ExtractBitfield(uint32_t bit_size, uint32_t bit_offset) {
  if (bit_size == 0)
    return true;

  switch (m_type) {
  case Scalar::e_void:
  case Scalar::e_float:
  case Scalar::e_double:
  case Scalar::e_long_double:
    break;

  case Scalar::e_sint:
  case Scalar::e_slong:
  case Scalar::e_slonglong:
  case Scalar::e_sint128:
  case Scalar::e_sint256:
    m_integer = m_integer.ashr(bit_offset)
                    .sextOrTrunc(bit_size)
                    .sextOrSelf(8 * GetByteSize());
    return true;

  case Scalar::e_uint:
  case Scalar::e_ulong:
  case Scalar::e_ulonglong:
  case Scalar::e_uint128:
  case Scalar::e_uint256:
    m_integer = m_integer.lshr(bit_offset)
                    .zextOrTrunc(bit_size)
                    .zextOrSelf(8 * GetByteSize());
    return true;
  }
  return false;
}

bool lldb_private::operator==(const Scalar &lhs, const Scalar &rhs) {
  // If either entry is void then we can just compare the types
  if (lhs.m_type == Scalar::e_void || rhs.m_type == Scalar::e_void)
    return lhs.m_type == rhs.m_type;

  Scalar temp_value;
  const Scalar *a;
  const Scalar *b;
  llvm::APFloat::cmpResult result;
  switch (PromoteToMaxType(lhs, rhs, temp_value, a, b)) {
  case Scalar::e_void:
    break;
  case Scalar::e_sint:
  case Scalar::e_uint:
  case Scalar::e_slong:
  case Scalar::e_ulong:
  case Scalar::e_slonglong:
  case Scalar::e_ulonglong:
  case Scalar::e_sint128:
  case Scalar::e_uint128:
  case Scalar::e_sint256:
  case Scalar::e_uint256:
    return a->m_integer == b->m_integer;
  case Scalar::e_float:
  case Scalar::e_double:
  case Scalar::e_long_double:
    result = a->m_float.compare(b->m_float);
    if (result == llvm::APFloat::cmpEqual)
      return true;
  }
  return false;
}

bool lldb_private::operator!=(const Scalar &lhs, const Scalar &rhs) {
  return !(lhs == rhs);
}

bool lldb_private::operator<(const Scalar &lhs, const Scalar &rhs) {
  if (lhs.m_type == Scalar::e_void || rhs.m_type == Scalar::e_void)
    return false;

  Scalar temp_value;
  const Scalar *a;
  const Scalar *b;
  llvm::APFloat::cmpResult result;
  switch (PromoteToMaxType(lhs, rhs, temp_value, a, b)) {
  case Scalar::e_void:
    break;
  case Scalar::e_sint:
  case Scalar::e_slong:
  case Scalar::e_slonglong:
  case Scalar::e_sint128:
  case Scalar::e_sint256:
    return a->m_integer.slt(b->m_integer);
  case Scalar::e_uint:
  case Scalar::e_ulong:
  case Scalar::e_ulonglong:
  case Scalar::e_uint128:
  case Scalar::e_uint256:
    return a->m_integer.ult(b->m_integer);
  case Scalar::e_float:
  case Scalar::e_double:
  case Scalar::e_long_double:
    result = a->m_float.compare(b->m_float);
    if (result == llvm::APFloat::cmpLessThan)
      return true;
  }
  return false;
}

bool lldb_private::operator<=(const Scalar &lhs, const Scalar &rhs) {
  return !(rhs < lhs);
}

bool lldb_private::operator>(const Scalar &lhs, const Scalar &rhs) {
  return rhs < lhs;
}

bool lldb_private::operator>=(const Scalar &lhs, const Scalar &rhs) {
  return !(lhs < rhs);
}

bool Scalar::ClearBit(uint32_t bit) {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    m_integer.clearBit(bit);
    return true;
  case e_float:
  case e_double:
  case e_long_double:
    break;
  }
  return false;
}

bool Scalar::SetBit(uint32_t bit) {
  switch (m_type) {
  case e_void:
    break;
  case e_sint:
  case e_uint:
  case e_slong:
  case e_ulong:
  case e_slonglong:
  case e_ulonglong:
  case e_sint128:
  case e_uint128:
  case e_sint256:
  case e_uint256:
    m_integer.setBit(bit);
    return true;
  case e_float:
  case e_double:
  case e_long_double:
    break;
  }
  return false;
}
