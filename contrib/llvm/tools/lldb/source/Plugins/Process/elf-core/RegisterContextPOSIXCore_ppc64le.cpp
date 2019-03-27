//===-- RegisterContextPOSIXCore_ppc64le.cpp --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RegisterContextPOSIXCore_ppc64le.h"

#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/RegisterValue.h"

#include "Plugins/Process/Utility/lldb-ppc64le-register-enums.h"
#include "Plugins/Process/elf-core/RegisterUtilities.h"

using namespace lldb_private;

RegisterContextCorePOSIX_ppc64le::RegisterContextCorePOSIX_ppc64le(
    Thread &thread, RegisterInfoInterface *register_info,
    const DataExtractor &gpregset, llvm::ArrayRef<CoreNote> notes)
    : RegisterContextPOSIX_ppc64le(thread, 0, register_info) {
  m_gpr_buffer.reset(
      new DataBufferHeap(gpregset.GetDataStart(), gpregset.GetByteSize()));
  m_gpr.SetData(m_gpr_buffer);
  m_gpr.SetByteOrder(gpregset.GetByteOrder());

  ArchSpec arch = register_info->GetTargetArchitecture();
  DataExtractor fpregset = getRegset(notes, arch.GetTriple(), FPR_Desc);
  m_fpr_buffer.reset(
      new DataBufferHeap(fpregset.GetDataStart(), fpregset.GetByteSize()));
  m_fpr.SetData(m_fpr_buffer);
  m_fpr.SetByteOrder(fpregset.GetByteOrder());

  DataExtractor vmxregset = getRegset(notes, arch.GetTriple(), PPC_VMX_Desc);
  m_vmx_buffer.reset(
      new DataBufferHeap(vmxregset.GetDataStart(), vmxregset.GetByteSize()));
  m_vmx.SetData(m_vmx_buffer);
  m_vmx.SetByteOrder(vmxregset.GetByteOrder());

  DataExtractor vsxregset = getRegset(notes, arch.GetTriple(), PPC_VSX_Desc);
  m_vsx_buffer.reset(
      new DataBufferHeap(vsxregset.GetDataStart(), vsxregset.GetByteSize()));
  m_vsx.SetData(m_vsx_buffer);
  m_vsx.SetByteOrder(vsxregset.GetByteOrder());
}

size_t RegisterContextCorePOSIX_ppc64le::GetFPRSize() const {
  return k_num_fpr_registers_ppc64le * sizeof(uint64_t);
}

size_t RegisterContextCorePOSIX_ppc64le::GetVMXSize() const {
  return (k_num_vmx_registers_ppc64le - 1) * sizeof(uint64_t) * 2 +
         sizeof(uint32_t);
}

size_t RegisterContextCorePOSIX_ppc64le::GetVSXSize() const {
  return k_num_vsx_registers_ppc64le * sizeof(uint64_t) * 2;
}

bool RegisterContextCorePOSIX_ppc64le::ReadRegister(
    const RegisterInfo *reg_info, RegisterValue &value) {
  lldb::offset_t offset = reg_info->byte_offset;

  if (IsFPR(reg_info->kinds[lldb::eRegisterKindLLDB])) {
    uint64_t v;
    offset -= GetGPRSize();
    offset = m_fpr.CopyData(offset, reg_info->byte_size, &v);

    if (offset == reg_info->byte_size) {
      value.SetBytes(&v, reg_info->byte_size, m_fpr.GetByteOrder());
      return true;
    }
  } else if (IsVMX(reg_info->kinds[lldb::eRegisterKindLLDB])) {
    uint32_t v[4];
    offset -= GetGPRSize() + GetFPRSize();
    offset = m_vmx.CopyData(offset, reg_info->byte_size, &v);

    if (offset == reg_info->byte_size) {
      value.SetBytes(v, reg_info->byte_size, m_vmx.GetByteOrder());
      return true;
    }
  } else if (IsVSX(reg_info->kinds[lldb::eRegisterKindLLDB])) {
    uint32_t v[4];
    lldb::offset_t tmp_offset;
    offset -= GetGPRSize() + GetFPRSize() + GetVMXSize();

    if (offset < GetVSXSize() / 2) {
      tmp_offset = m_vsx.CopyData(offset / 2, reg_info->byte_size / 2, &v);

      if (tmp_offset != reg_info->byte_size / 2) {
        return false;
      }

      uint8_t *dst = (uint8_t *)&v + sizeof(uint64_t);
      tmp_offset = m_fpr.CopyData(offset / 2, reg_info->byte_size / 2, dst);

      if (tmp_offset != reg_info->byte_size / 2) {
        return false;
      }

      value.SetBytes(&v, reg_info->byte_size, m_vsx.GetByteOrder());
      return true;
    } else {
      offset =
          m_vmx.CopyData(offset - GetVSXSize() / 2, reg_info->byte_size, &v);
      if (offset == reg_info->byte_size) {
        value.SetBytes(v, reg_info->byte_size, m_vmx.GetByteOrder());
        return true;
      }
    }
  } else {
    uint64_t v = m_gpr.GetMaxU64(&offset, reg_info->byte_size);

    if (offset == reg_info->byte_offset + reg_info->byte_size) {
      if (reg_info->byte_size < sizeof(v))
        value = (uint32_t)v;
      else
        value = v;
      return true;
    }
  }

  return false;
}

bool RegisterContextCorePOSIX_ppc64le::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &value) {
  return false;
}
