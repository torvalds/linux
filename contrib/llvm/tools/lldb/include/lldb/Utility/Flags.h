//===-- Flags.h -------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_FLAGS_H
#define LLDB_UTILITY_FLAGS_H

#include <cstddef>
#include <cstdint>

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Flags Flags.h "lldb/Utility/Flags.h"
/// A class to manage flags.
///
/// The Flags class managed flag bits and allows testing and modification of
/// individual or multiple flag bits.
//----------------------------------------------------------------------
class Flags {
public:
  //----------------------------------------------------------------------
  /// The value type for flags is a 32 bit unsigned integer type.
  //----------------------------------------------------------------------
  typedef uint32_t ValueType;

  //----------------------------------------------------------------------
  /// Construct with initial flag bit values.
  ///
  /// Constructs this object with \a mask as the initial value for all of the
  /// flags.
  ///
  /// @param[in] mask
  ///     The initial value for all flags.
  //----------------------------------------------------------------------
  Flags(ValueType flags = 0) : m_flags(flags) {}

  //----------------------------------------------------------------------
  /// Copy constructor.
  ///
  /// Construct and copy the flags from \a rhs.
  ///
  /// @param[in] rhs
  ///     A const Flags object reference to copy.
  //----------------------------------------------------------------------
  Flags(const Flags &rhs) : m_flags(rhs.m_flags) {}

  //----------------------------------------------------------------------
  /// Destructor.
  //----------------------------------------------------------------------
  ~Flags() {}

  //----------------------------------------------------------------------
  /// Get accessor for all flags.
  ///
  /// @return
  ///     Returns all of the flags as a Flags::ValueType.
  //----------------------------------------------------------------------
  ValueType Get() const { return m_flags; }

  //----------------------------------------------------------------------
  /// Return the number of flags that can be represented in this object.
  ///
  /// @return
  ///     The maximum number bits in this flag object.
  //----------------------------------------------------------------------
  size_t GetBitSize() const { return sizeof(ValueType) * 8; }

  //----------------------------------------------------------------------
  /// Set accessor for all flags.
  ///
  /// @param[in] flags
  ///     The bits with which to replace all of the current flags.
  //----------------------------------------------------------------------
  void Reset(ValueType flags) { m_flags = flags; }

  //----------------------------------------------------------------------
  /// Clear one or more flags.
  ///
  /// @param[in] mask
  ///     A bitfield containing one or more flags.
  ///
  /// @return
  ///     The new flags after clearing all bits from \a mask.
  //----------------------------------------------------------------------
  ValueType Clear(ValueType mask = ~(ValueType)0) {
    m_flags &= ~mask;
    return m_flags;
  }

  //----------------------------------------------------------------------
  /// Set one or more flags by logical OR'ing \a mask with the current flags.
  ///
  /// @param[in] mask
  ///     A bitfield containing one or more flags.
  ///
  /// @return
  ///     The new flags after setting all bits from \a mask.
  //----------------------------------------------------------------------
  ValueType Set(ValueType mask) {
    m_flags |= mask;
    return m_flags;
  }

  //----------------------------------------------------------------------
  /// Test if all bits in \a mask are 1 in the current flags
  ///
  /// @return
  ///     \b true if all flags in \a mask are 1, \b false
  ///     otherwise.
  //----------------------------------------------------------------------
  bool AllSet(ValueType mask) const { return (m_flags & mask) == mask; }

  //----------------------------------------------------------------------
  /// Test one or more flags.
  ///
  /// @return
  ///     \b true if any flags in \a mask are 1, \b false
  ///     otherwise.
  //----------------------------------------------------------------------
  bool AnySet(ValueType mask) const { return (m_flags & mask) != 0; }

  //----------------------------------------------------------------------
  /// Test a single flag bit.
  ///
  /// @return
  ///     \b true if \a bit is set, \b false otherwise.
  //----------------------------------------------------------------------
  bool Test(ValueType bit) const { return (m_flags & bit) != 0; }

  //----------------------------------------------------------------------
  /// Test if all bits in \a mask are clear.
  ///
  /// @return
  ///     \b true if \b all flags in \a mask are clear, \b false
  ///     otherwise.
  //----------------------------------------------------------------------
  bool AllClear(ValueType mask) const { return (m_flags & mask) == 0; }

  bool AnyClear(ValueType mask) const { return (m_flags & mask) != mask; }

  //----------------------------------------------------------------------
  /// Test a single flag bit to see if it is clear (zero).
  ///
  /// @return
  ///     \b true if \a bit is 0, \b false otherwise.
  //----------------------------------------------------------------------
  bool IsClear(ValueType bit) const { return (m_flags & bit) == 0; }

  //----------------------------------------------------------------------
  /// Get the number of zero bits in \a m_flags.
  ///
  /// @return
  ///     The number of bits that are set to 0 in the current flags.
  //----------------------------------------------------------------------
  size_t ClearCount() const {
    size_t count = 0;
    for (ValueType shift = 0; shift < sizeof(ValueType) * 8; ++shift) {
      if ((m_flags & (1u << shift)) == 0)
        ++count;
    }
    return count;
  }

  //----------------------------------------------------------------------
  /// Get the number of one bits in \a m_flags.
  ///
  /// @return
  ///     The number of bits that are set to 1 in the current flags.
  //----------------------------------------------------------------------
  size_t SetCount() const {
    size_t count = 0;
    for (ValueType mask = m_flags; mask; mask >>= 1) {
      if (mask & 1u)
        ++count;
    }
    return count;
  }

protected:
  ValueType m_flags; ///< The flags.
};

} // namespace lldb_private

#endif // liblldb_Flags_h_
