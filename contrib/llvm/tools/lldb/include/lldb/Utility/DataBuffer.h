//===-- DataBuffer.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DataBuffer_h_
#define liblldb_DataBuffer_h_
#if defined(__cplusplus)

#include <stdint.h>
#include <string.h>

#include "lldb/lldb-types.h"

#include "llvm/ADT/ArrayRef.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class DataBuffer DataBuffer.h "lldb/Core/DataBuffer.h"
/// A pure virtual protocol class for abstracted data buffers.
///
/// DataBuffer is an abstract class that gets packaged into a shared pointer
/// that can use to implement various ways to store data (on the heap, memory
/// mapped, cached inferior memory). It gets used by DataExtractor so many
/// DataExtractor objects can share the same data and sub-ranges of that
/// shared data, and the last object that contains a reference to the shared
/// data will free it.
///
/// Subclasses can implement as many different constructors or member
/// functions that allow data to be stored in the object's buffer prior to
/// handing the shared data to clients that use these buffers.
///
/// All subclasses must override all of the pure virtual functions as they are
/// used by clients to access the data. Having a common interface allows
/// different ways of storing data, yet using it in one common way.
///
/// This class currently expects all data to be available without any extra
/// calls being made, but we can modify it to optionally get data on demand
/// with some extra function calls to load the data before it gets accessed.
//----------------------------------------------------------------------
class DataBuffer {
public:
  //------------------------------------------------------------------
  /// Destructor
  ///
  /// The destructor is virtual as other classes will inherit from this class
  /// and be downcast to the DataBuffer pure virtual interface. The virtual
  /// destructor ensures that destructing the base class will destruct the
  /// class that inherited from it correctly.
  //------------------------------------------------------------------
  virtual ~DataBuffer() {}

  //------------------------------------------------------------------
  /// Get a pointer to the data.
  ///
  /// @return
  ///     A pointer to the bytes owned by this object, or NULL if the
  ///     object contains no bytes.
  //------------------------------------------------------------------
  virtual uint8_t *GetBytes() = 0;

  //------------------------------------------------------------------
  /// Get a const pointer to the data.
  ///
  /// @return
  ///     A const pointer to the bytes owned by this object, or NULL
  ///     if the object contains no bytes.
  //------------------------------------------------------------------
  virtual const uint8_t *GetBytes() const = 0;

  //------------------------------------------------------------------
  /// Get the number of bytes in the data buffer.
  ///
  /// @return
  ///     The number of bytes this object currently contains.
  //------------------------------------------------------------------
  virtual lldb::offset_t GetByteSize() const = 0;

  llvm::ArrayRef<uint8_t> GetData() const {
    return llvm::ArrayRef<uint8_t>(GetBytes(), GetByteSize());
  }

  llvm::MutableArrayRef<uint8_t> GetData() {
    return llvm::MutableArrayRef<uint8_t>(GetBytes(), GetByteSize());
  }
};

} // namespace lldb_private

#endif /// #if defined(__cplusplus)
#endif /// lldb_DataBuffer_h_
