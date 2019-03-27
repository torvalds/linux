//===-- Opcode.cpp ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Opcode.h"

#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-forward.h"

#include <memory>

#include <inttypes.h>

using namespace lldb;
using namespace lldb_private;

int Opcode::Dump(Stream *s, uint32_t min_byte_width) {
  const uint32_t previous_bytes = s->GetWrittenBytes();
  switch (m_type) {
  case Opcode::eTypeInvalid:
    s->PutCString("<invalid>");
    break;
  case Opcode::eType8:
    s->Printf("0x%2.2x", m_data.inst8);
    break;
  case Opcode::eType16:
    s->Printf("0x%4.4x", m_data.inst16);
    break;
  case Opcode::eType16_2:
  case Opcode::eType32:
    s->Printf("0x%8.8x", m_data.inst32);
    break;

  case Opcode::eType64:
    s->Printf("0x%16.16" PRIx64, m_data.inst64);
    break;

  case Opcode::eTypeBytes:
    for (uint32_t i = 0; i < m_data.inst.length; ++i) {
      if (i > 0)
        s->PutChar(' ');
      s->Printf("%2.2x", m_data.inst.bytes[i]);
    }
    break;
  }

  uint32_t bytes_written_so_far = s->GetWrittenBytes() - previous_bytes;
  // Add spaces to make sure bytes display comes out even in case opcodes aren't
  // all the same size.
  if (bytes_written_so_far < min_byte_width)
    s->Printf("%*s", min_byte_width - bytes_written_so_far, "");
  return s->GetWrittenBytes() - previous_bytes;
}

lldb::ByteOrder Opcode::GetDataByteOrder() const {
  if (m_byte_order != eByteOrderInvalid) {
    return m_byte_order;
  }
  switch (m_type) {
  case Opcode::eTypeInvalid:
    break;
  case Opcode::eType8:
  case Opcode::eType16:
  case Opcode::eType16_2:
  case Opcode::eType32:
  case Opcode::eType64:
    return endian::InlHostByteOrder();
  case Opcode::eTypeBytes:
    break;
  }
  return eByteOrderInvalid;
}

uint32_t Opcode::GetData(DataExtractor &data) const {
  uint32_t byte_size = GetByteSize();
  uint8_t swap_buf[8];
  const void *buf = nullptr;

  if (byte_size > 0) {
    if (!GetEndianSwap()) {
      if (m_type == Opcode::eType16_2) {
        // 32 bit thumb instruction, we need to sizzle this a bit
        swap_buf[0] = m_data.inst.bytes[2];
        swap_buf[1] = m_data.inst.bytes[3];
        swap_buf[2] = m_data.inst.bytes[0];
        swap_buf[3] = m_data.inst.bytes[1];
        buf = swap_buf;
      } else {
        buf = GetOpcodeDataBytes();
      }
    } else {
      switch (m_type) {
      case Opcode::eTypeInvalid:
        break;
      case Opcode::eType8:
        buf = GetOpcodeDataBytes();
        break;
      case Opcode::eType16:
        *(uint16_t *)swap_buf = llvm::ByteSwap_16(m_data.inst16);
        buf = swap_buf;
        break;
      case Opcode::eType16_2:
        swap_buf[0] = m_data.inst.bytes[1];
        swap_buf[1] = m_data.inst.bytes[0];
        swap_buf[2] = m_data.inst.bytes[3];
        swap_buf[3] = m_data.inst.bytes[2];
        buf = swap_buf;
        break;
      case Opcode::eType32:
        *(uint32_t *)swap_buf = llvm::ByteSwap_32(m_data.inst32);
        buf = swap_buf;
        break;
      case Opcode::eType64:
        *(uint32_t *)swap_buf = llvm::ByteSwap_64(m_data.inst64);
        buf = swap_buf;
        break;
      case Opcode::eTypeBytes:
        buf = GetOpcodeDataBytes();
        break;
      }
    }
  }
  if (buf != nullptr) {
    DataBufferSP buffer_sp;

    buffer_sp = std::make_shared<DataBufferHeap>(buf, byte_size);
    data.SetByteOrder(GetDataByteOrder());
    data.SetData(buffer_sp);
    return byte_size;
  }
  data.Clear();
  return 0;
}
