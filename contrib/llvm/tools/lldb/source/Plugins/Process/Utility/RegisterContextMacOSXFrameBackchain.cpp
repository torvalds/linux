//===-- RegisterContextMacOSXFrameBackchain.cpp -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RegisterContextMacOSXFrameBackchain.h"

#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StringExtractorGDBRemote.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// RegisterContextMacOSXFrameBackchain constructor
//----------------------------------------------------------------------
RegisterContextMacOSXFrameBackchain::RegisterContextMacOSXFrameBackchain(
    Thread &thread, uint32_t concrete_frame_idx,
    const UnwindMacOSXFrameBackchain::Cursor &cursor)
    : RegisterContext(thread, concrete_frame_idx), m_cursor(cursor),
      m_cursor_is_valid(true) {}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
RegisterContextMacOSXFrameBackchain::~RegisterContextMacOSXFrameBackchain() {}

void RegisterContextMacOSXFrameBackchain::InvalidateAllRegisters() {
  m_cursor_is_valid = false;
}

size_t RegisterContextMacOSXFrameBackchain::GetRegisterCount() {
  return m_thread.GetRegisterContext()->GetRegisterCount();
}

const RegisterInfo *
RegisterContextMacOSXFrameBackchain::GetRegisterInfoAtIndex(size_t reg) {
  return m_thread.GetRegisterContext()->GetRegisterInfoAtIndex(reg);
}

size_t RegisterContextMacOSXFrameBackchain::GetRegisterSetCount() {
  return m_thread.GetRegisterContext()->GetRegisterSetCount();
}

const RegisterSet *
RegisterContextMacOSXFrameBackchain::GetRegisterSet(size_t reg_set) {
  return m_thread.GetRegisterContext()->GetRegisterSet(reg_set);
}

bool RegisterContextMacOSXFrameBackchain::ReadRegister(
    const RegisterInfo *reg_info, RegisterValue &value) {
  if (!m_cursor_is_valid)
    return false;

  uint64_t reg_value = LLDB_INVALID_ADDRESS;

  switch (reg_info->kinds[eRegisterKindGeneric]) {
  case LLDB_REGNUM_GENERIC_PC:
    if (m_cursor.pc == LLDB_INVALID_ADDRESS)
      return false;
    reg_value = m_cursor.pc;
    break;

  case LLDB_REGNUM_GENERIC_FP:
    if (m_cursor.fp == LLDB_INVALID_ADDRESS)
      return false;
    reg_value = m_cursor.fp;
    break;

  default:
    return false;
  }

  switch (reg_info->encoding) {
  case eEncodingInvalid:
  case eEncodingVector:
    break;

  case eEncodingUint:
  case eEncodingSint:
    value.SetUInt(reg_value, reg_info->byte_size);
    return true;

  case eEncodingIEEE754:
    switch (reg_info->byte_size) {
    case sizeof(float):
      if (sizeof(float) == sizeof(uint32_t)) {
        value.SetUInt32(reg_value, RegisterValue::eTypeFloat);
        return true;
      } else if (sizeof(float) == sizeof(uint64_t)) {
        value.SetUInt64(reg_value, RegisterValue::eTypeFloat);
        return true;
      }
      break;

    case sizeof(double):
      if (sizeof(double) == sizeof(uint32_t)) {
        value.SetUInt32(reg_value, RegisterValue::eTypeDouble);
        return true;
      } else if (sizeof(double) == sizeof(uint64_t)) {
        value.SetUInt64(reg_value, RegisterValue::eTypeDouble);
        return true;
      }
      break;

// TOOD: need a better way to detect when "long double" types are
// the same bytes size as "double"
#if !defined(__arm__) && !defined(__arm64__) && !defined(__aarch64__) &&       \
    !defined(_MSC_VER) && !defined(__mips__) && !defined(__powerpc__) &&       \
    !defined(__ANDROID__)
    case sizeof(long double):
      if (sizeof(long double) == sizeof(uint32_t)) {
        value.SetUInt32(reg_value, RegisterValue::eTypeLongDouble);
        return true;
      } else if (sizeof(long double) == sizeof(uint64_t)) {
        value.SetUInt64(reg_value, RegisterValue::eTypeLongDouble);
        return true;
      }
      break;
#endif
    }
    break;
  }
  return false;
}

bool RegisterContextMacOSXFrameBackchain::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &value) {
  // Not supported yet. We could easily add support for this by remembering the
  // address of each entry (it would need to be part of the cursor)
  return false;
}

bool RegisterContextMacOSXFrameBackchain::ReadAllRegisterValues(
    lldb::DataBufferSP &data_sp) {
  // libunwind frames can't handle this it doesn't always have all register
  // values. This call should only be called on frame zero anyway so there
  // shouldn't be any problem
  return false;
}

bool RegisterContextMacOSXFrameBackchain::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  // Since this class doesn't respond to "ReadAllRegisterValues()", it must not
  // have been the one that saved all the register values. So we just let the
  // thread's register context (the register context for frame zero) do the
  // writing.
  return m_thread.GetRegisterContext()->WriteAllRegisterValues(data_sp);
}

uint32_t
RegisterContextMacOSXFrameBackchain::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t num) {
  return m_thread.GetRegisterContext()->ConvertRegisterKindToRegisterNumber(
      kind, num);
}
