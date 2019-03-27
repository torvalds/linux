//===-- StreamBuffer.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StreamBuffer_h_
#define liblldb_StreamBuffer_h_

#include "lldb/Utility/Stream.h"
#include "llvm/ADT/SmallVector.h"
#include <stdio.h>
#include <string>

namespace lldb_private {

template <unsigned N> class StreamBuffer : public Stream {
public:
  StreamBuffer() : Stream(0, 4, lldb::eByteOrderBig), m_packet() {}

  StreamBuffer(uint32_t flags, uint32_t addr_size, lldb::ByteOrder byte_order)
      : Stream(flags, addr_size, byte_order), m_packet() {}

  virtual ~StreamBuffer() {}

  virtual void Flush() {
    // Nothing to do when flushing a buffer based stream...
  }

  void Clear() { m_packet.clear(); }

  // Beware, this might not be NULL terminated as you can expect from
  // StringString as there may be random bits in the llvm::SmallVector. If you
  // are using this class to create a C string, be sure the call PutChar ('\0')
  // after you have created your string, or use StreamString.
  const char *GetData() const { return m_packet.data(); }

  size_t GetSize() const { return m_packet.size(); }

protected:
  llvm::SmallVector<char, N> m_packet;

  virtual size_t WriteImpl(const void *s, size_t length) {
    if (s && length)
      m_packet.append((const char *)s, ((const char *)s) + length);
    return length;
  }
};

} // namespace lldb_private

#endif // #ifndef liblldb_StreamBuffer_h_
