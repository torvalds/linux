//===-- DataBufferHeap.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DataBufferHeap_h_
#define liblldb_DataBufferHeap_h_

#include "lldb/Utility/DataBuffer.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <vector>

namespace lldb_private {

//----------------------------------------------------------------------
/// @class DataBufferHeap DataBufferHeap.h "lldb/Core/DataBufferHeap.h"
/// A subclass of DataBuffer that stores a data buffer on the heap.
///
/// This class keeps its data in a heap based buffer that is owned by the
/// object. This class is best used to store chunks of data that are created
/// or read from sources that can't intelligently and lazily fault new data
/// pages in. Large amounts of data that comes from files should probably use
/// DataBufferLLVM, which can intelligently determine when memory mapping is
/// optimal.
//----------------------------------------------------------------------
class DataBufferHeap : public DataBuffer {
public:
  //------------------------------------------------------------------
  /// Default constructor
  ///
  /// Initializes the heap based buffer with no bytes.
  //------------------------------------------------------------------
  DataBufferHeap();

  //------------------------------------------------------------------
  /// Construct with size \a n and fill with \a ch.
  ///
  /// Initialize this class with \a n bytes and fills the buffer with \a ch.
  ///
  /// @param[in] n
  ///     The number of bytes that heap based buffer should contain.
  ///
  /// @param[in] ch
  ///     The character to use when filling the buffer initially.
  //------------------------------------------------------------------
  DataBufferHeap(lldb::offset_t n, uint8_t ch);

  //------------------------------------------------------------------
  /// Construct by making a copy of \a src_len bytes from \a src.
  ///
  /// @param[in] src
  ///     A pointer to the data to copy.
  ///
  /// @param[in] src_len
  ///     The number of bytes in \a src to copy.
  //------------------------------------------------------------------
  DataBufferHeap(const void *src, lldb::offset_t src_len);

  //------------------------------------------------------------------
  /// Destructor.
  ///
  /// Virtual destructor since this class inherits from a pure virtual base
  /// class #DataBuffer.
  //------------------------------------------------------------------
  ~DataBufferHeap() override;

  //------------------------------------------------------------------
  /// @copydoc DataBuffer::GetBytes()
  //------------------------------------------------------------------
  uint8_t *GetBytes() override;

  //------------------------------------------------------------------
  /// @copydoc DataBuffer::GetBytes() const
  //------------------------------------------------------------------
  const uint8_t *GetBytes() const override;

  //------------------------------------------------------------------
  /// @copydoc DataBuffer::GetByteSize() const
  //------------------------------------------------------------------
  lldb::offset_t GetByteSize() const override;

  //------------------------------------------------------------------
  /// Set the number of bytes in the data buffer.
  ///
  /// Sets the number of bytes that this object should be able to contain.
  /// This can be used prior to copying data into the buffer. Note that this
  /// zero-initializes up to \p byte_size bytes.
  ///
  /// @param[in] byte_size
  ///     The new size in bytes that this data buffer should attempt
  ///     to resize itself to.
  ///
  /// @return
  ///     The size in bytes after that this heap buffer was
  ///     successfully resized to.
  //------------------------------------------------------------------
  lldb::offset_t SetByteSize(lldb::offset_t byte_size);

  //------------------------------------------------------------------
  /// Makes a copy of the \a src_len bytes in \a src.
  ///
  /// Copies the data in \a src into an internal buffer.
  ///
  /// @param[in] src
  ///     A pointer to the data to copy.
  ///
  /// @param[in] src_len
  ///     The number of bytes in \a src to copy.
  //------------------------------------------------------------------
  void CopyData(const void *src, lldb::offset_t src_len);
  void CopyData(llvm::StringRef src) { CopyData(src.data(), src.size()); }

  void AppendData(const void *src, uint64_t src_len);

  void Clear();

private:
  //------------------------------------------------------------------
  // This object uses a std::vector<uint8_t> to store its data. This takes care
  // of free the data when the object is deleted.
  //------------------------------------------------------------------
  typedef std::vector<uint8_t> buffer_t; ///< Buffer type
  buffer_t m_data; ///< The heap based buffer where data is stored
};

} // namespace lldb_private

#endif // liblldb_DataBufferHeap_h_
