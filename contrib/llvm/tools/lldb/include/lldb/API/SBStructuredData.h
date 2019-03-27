//===-- SBStructuredData.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SBStructuredData_h
#define SBStructuredData_h

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBModule.h"

namespace lldb {

class SBStructuredData {
public:
  SBStructuredData();

  SBStructuredData(const lldb::SBStructuredData &rhs);

  SBStructuredData(const lldb::EventSP &event_sp);
  
  SBStructuredData(lldb_private::StructuredDataImpl *impl);

  ~SBStructuredData();

  lldb::SBStructuredData &operator=(const lldb::SBStructuredData &rhs);

  bool IsValid() const;

  lldb::SBError SetFromJSON(lldb::SBStream &stream);

  void Clear();

  lldb::SBError GetAsJSON(lldb::SBStream &stream) const;

  lldb::SBError GetDescription(lldb::SBStream &stream) const;

  //------------------------------------------------------------------
  /// Return the type of data in this data structure
  //------------------------------------------------------------------
  lldb::StructuredDataType GetType() const;
  
  //------------------------------------------------------------------
  /// Return the size (i.e. number of elements) in this data structure
  /// if it is an array or dictionary type. For other types, 0 will be
  //  returned.
  //------------------------------------------------------------------
  size_t GetSize() const;

  //------------------------------------------------------------------
  /// Fill keys with the keys in this object and return true if this data
  /// structure is a dictionary.  Returns false otherwise.
  //------------------------------------------------------------------
   bool GetKeys(lldb::SBStringList &keys) const;
  
  //------------------------------------------------------------------
  /// Return the value corresponding to a key if this data structure
  /// is a dictionary type.
  //------------------------------------------------------------------
  lldb::SBStructuredData GetValueForKey(const char *key) const;

  //------------------------------------------------------------------
  /// Return the value corresponding to an index if this data structure
  /// is array.
  //------------------------------------------------------------------
  lldb::SBStructuredData GetItemAtIndex(size_t idx) const;

  //------------------------------------------------------------------
  /// Return the integer value if this data structure is an integer type.
  //------------------------------------------------------------------
  uint64_t GetIntegerValue(uint64_t fail_value = 0) const;

  //------------------------------------------------------------------
  /// Return the floating point value if this data structure is a floating
  /// type.
  //------------------------------------------------------------------
  double GetFloatValue(double fail_value = 0.0) const;

  //------------------------------------------------------------------
  /// Return the boolean value if this data structure is a boolean type.
  //------------------------------------------------------------------
  bool GetBooleanValue(bool fail_value = false) const;

  //------------------------------------------------------------------
  /// Provides the string value if this data structure is a string type.
  ///
  /// @param[out] dst
  ///     pointer where the string value will be written. In case it is null,
  ///     nothing will be written at @dst.
  ///
  /// @param[in] dst_len
  ///     max number of characters that can be written at @dst. In case it is
  ///     zero, nothing will be written at @dst. If this length is not enough
  ///     to write the complete string value, (dst_len-1) bytes of the string
  ///     value will be written at @dst followed by a null character.
  ///
  /// @return
  ///     Returns the byte size needed to completely write the string value at
  ///     @dst in all cases.
  //------------------------------------------------------------------
  size_t GetStringValue(char *dst, size_t dst_len) const;

protected:
  friend class SBTraceOptions;
  friend class SBDebugger;
  friend class SBTarget;

  StructuredDataImplUP m_impl_up;
};
} // namespace lldb

#endif /* SBStructuredData_h */
