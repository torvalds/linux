//===-- Value.h -------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Value_h_
#define liblldb_Value_h_

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-private-types.h"

#include "llvm/ADT/APInt.h"

#include <vector>

#include <stdint.h>
#include <string.h>

namespace lldb_private {
class DataExtractor;
}
namespace lldb_private {
class ExecutionContext;
}
namespace lldb_private {
class Module;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class Type;
}
namespace lldb_private {
class Variable;
}

namespace lldb_private {

class Value {
public:
  // Values Less than zero are an error, greater than or equal to zero returns
  // what the Scalar result is.
  enum ValueType {
    // m_value contains...
    // ============================
    eValueTypeScalar,      // raw scalar value
    eValueTypeVector,      // byte array of m_vector.length with endianness of
                           // m_vector.byte_order
    eValueTypeFileAddress, // file address value
    eValueTypeLoadAddress, // load address value
    eValueTypeHostAddress  // host address value (for memory in the process that
                           // is using liblldb)
  };

  enum ContextType // Type that describes Value::m_context
  {
    // m_context contains...
    // ====================
    eContextTypeInvalid,      // undefined
    eContextTypeRegisterInfo, // RegisterInfo * (can be a scalar or a vector
                              // register)
    eContextTypeLLDBType,     // lldb_private::Type *
    eContextTypeVariable      // lldb_private::Variable *
  };

  const static size_t kMaxByteSize = 32u;

  struct Vector {
    // The byte array must be big enough to hold vector registers for any
    // supported target.
    uint8_t bytes[kMaxByteSize];
    size_t length;
    lldb::ByteOrder byte_order;

    Vector() : length(0), byte_order(lldb::eByteOrderInvalid) {}

    Vector(const Vector &vector) { *this = vector; }
    const Vector &operator=(const Vector &vector) {
      SetBytes(vector.bytes, vector.length, vector.byte_order);
      return *this;
    }

    void Clear() { length = 0; }

    bool SetBytes(const void *bytes, size_t length,
                  lldb::ByteOrder byte_order) {
      this->length = length;
      this->byte_order = byte_order;
      if (length)
        ::memcpy(this->bytes, bytes,
                 length < kMaxByteSize ? length : kMaxByteSize);
      return IsValid();
    }

    bool IsValid() const {
      return (length > 0 && length < kMaxByteSize &&
              byte_order != lldb::eByteOrderInvalid);
    }
    // Casts a vector, if valid, to an unsigned int of matching or largest
    // supported size. Truncates to the beginning of the vector if required.
    // Returns a default constructed Scalar if the Vector data is internally
    // inconsistent.
    llvm::APInt rhs = llvm::APInt(BITWIDTH_INT128, NUM_OF_WORDS_INT128,
                                  ((type128 *)bytes)->x);
    Scalar GetAsScalar() const {
      Scalar scalar;
      if (IsValid()) {
        if (length == 1)
          scalar = *(const uint8_t *)bytes;
        else if (length == 2)
          scalar = *(const uint16_t *)bytes;
        else if (length == 4)
          scalar = *(const uint32_t *)bytes;
        else if (length == 8)
          scalar = *(const uint64_t *)bytes;
        else if (length >= 16)
          scalar = rhs;
      }
      return scalar;
    }
  };

  Value();
  Value(const Scalar &scalar);
  Value(const Vector &vector);
  Value(const void *bytes, int len);
  Value(const Value &rhs);

  void SetBytes(const void *bytes, int len);

  void AppendBytes(const void *bytes, int len);

  Value &operator=(const Value &rhs);

  const CompilerType &GetCompilerType();

  void SetCompilerType(const CompilerType &compiler_type);

  ValueType GetValueType() const;

  AddressType GetValueAddressType() const;

  ContextType GetContextType() const { return m_context_type; }

  void SetValueType(ValueType value_type) { m_value_type = value_type; }

  void ClearContext() {
    m_context = nullptr;
    m_context_type = eContextTypeInvalid;
  }

  void SetContext(ContextType context_type, void *p) {
    m_context_type = context_type;
    m_context = p;
    if (m_context_type == eContextTypeRegisterInfo) {
      RegisterInfo *reg_info = GetRegisterInfo();
      if (reg_info->encoding == lldb::eEncodingVector &&
          m_vector.byte_order != lldb::eByteOrderInvalid)
        SetValueType(eValueTypeScalar);
    }
  }

  RegisterInfo *GetRegisterInfo() const;

  Type *GetType();

  Scalar &ResolveValue(ExecutionContext *exe_ctx);

  const Scalar &GetScalar() const { return m_value; }

  const Vector &GetVector() const { return m_vector; }

  Scalar &GetScalar() { return m_value; }

  Vector &GetVector() { return m_vector; }

  bool SetVectorBytes(const Vector &vector) {
    m_vector = vector;
    return m_vector.IsValid();
  }

  bool SetVectorBytes(uint8_t *bytes, size_t length,
                      lldb::ByteOrder byte_order) {
    return m_vector.SetBytes(bytes, length, byte_order);
  }

  bool SetScalarFromVector() {
    if (m_vector.IsValid()) {
      m_value = m_vector.GetAsScalar();
      return true;
    }
    return false;
  }

  size_t ResizeData(size_t len);

  size_t AppendDataToHostBuffer(const Value &rhs);

  DataBufferHeap &GetBuffer() { return m_data_buffer; }

  const DataBufferHeap &GetBuffer() const { return m_data_buffer; }

  bool ValueOf(ExecutionContext *exe_ctx);

  Variable *GetVariable();

  void Dump(Stream *strm);

  lldb::Format GetValueDefaultFormat();

  uint64_t GetValueByteSize(Status *error_ptr, ExecutionContext *exe_ctx);

  Status GetValueAsData(ExecutionContext *exe_ctx, DataExtractor &data,
                        uint32_t data_offset,
                        Module *module); // Can be nullptr

  static const char *GetValueTypeAsCString(ValueType context_type);

  static const char *GetContextTypeAsCString(ContextType context_type);

  /// Convert this value's file address to a load address, if possible.
  void ConvertToLoadAddress(Module *module, Target *target);

  bool GetData(DataExtractor &data);

  void Clear();

protected:
  Scalar m_value;
  Vector m_vector;
  CompilerType m_compiler_type;
  void *m_context;
  ValueType m_value_type;
  ContextType m_context_type;
  DataBufferHeap m_data_buffer;
};

class ValueList {
public:
  ValueList() : m_values() {}

  ValueList(const ValueList &rhs);

  ~ValueList() = default;

  const ValueList &operator=(const ValueList &rhs);

  // void InsertValue (Value *value, size_t idx);
  void PushValue(const Value &value);

  size_t GetSize();
  Value *GetValueAtIndex(size_t idx);
  void Clear();

private:
  typedef std::vector<Value> collection;

  collection m_values;
};

} // namespace lldb_private

#endif // liblldb_Value_h_
