//===-- RegisterContextPOSIXCore_s390x.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RegisterContextPOSIXCore_s390x.h"

#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/RegisterValue.h"

using namespace lldb_private;

RegisterContextCorePOSIX_s390x::RegisterContextCorePOSIX_s390x(
    Thread &thread, RegisterInfoInterface *register_info,
    const DataExtractor &gpregset, llvm::ArrayRef<CoreNote> notes)
    : RegisterContextPOSIX_s390x(thread, 0, register_info) {
  m_gpr_buffer.reset(
      new DataBufferHeap(gpregset.GetDataStart(), gpregset.GetByteSize()));
  m_gpr.SetData(m_gpr_buffer);
  m_gpr.SetByteOrder(gpregset.GetByteOrder());

  DataExtractor fpregset = getRegset(
      notes, register_info->GetTargetArchitecture().GetTriple(), FPR_Desc);
  m_fpr_buffer.reset(
      new DataBufferHeap(fpregset.GetDataStart(), fpregset.GetByteSize()));
  m_fpr.SetData(m_fpr_buffer);
  m_fpr.SetByteOrder(fpregset.GetByteOrder());
}

RegisterContextCorePOSIX_s390x::~RegisterContextCorePOSIX_s390x() {}

bool RegisterContextCorePOSIX_s390x::ReadGPR() { return true; }

bool RegisterContextCorePOSIX_s390x::ReadFPR() { return true; }

bool RegisterContextCorePOSIX_s390x::WriteGPR() {
  assert(0);
  return false;
}

bool RegisterContextCorePOSIX_s390x::WriteFPR() {
  assert(0);
  return false;
}

bool RegisterContextCorePOSIX_s390x::ReadRegister(const RegisterInfo *reg_info,
                                                  RegisterValue &value) {
  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (reg == LLDB_INVALID_REGNUM)
    return false;

  if (IsGPR(reg)) {
    lldb::offset_t offset = reg_info->byte_offset;
    uint64_t v = m_gpr.GetMaxU64(&offset, reg_info->byte_size);
    if (offset == reg_info->byte_offset + reg_info->byte_size) {
      value.SetUInt(v, reg_info->byte_size);
      return true;
    }
  }

  if (IsFPR(reg)) {
    lldb::offset_t offset = reg_info->byte_offset;
    uint64_t v = m_fpr.GetMaxU64(&offset, reg_info->byte_size);
    if (offset == reg_info->byte_offset + reg_info->byte_size) {
      value.SetUInt(v, reg_info->byte_size);
      return true;
    }
  }

  return false;
}

bool RegisterContextCorePOSIX_s390x::ReadAllRegisterValues(
    lldb::DataBufferSP &data_sp) {
  return false;
}

bool RegisterContextCorePOSIX_s390x::WriteRegister(const RegisterInfo *reg_info,
                                                   const RegisterValue &value) {
  return false;
}

bool RegisterContextCorePOSIX_s390x::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  return false;
}

bool RegisterContextCorePOSIX_s390x::HardwareSingleStep(bool enable) {
  return false;
}
