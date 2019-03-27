//===-- RegisterInfoInterface.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_RegisterInfoInterface_h
#define lldb_RegisterInfoInterface_h

#include "lldb/Utility/ArchSpec.h"
#include "lldb/lldb-private-types.h"
#include <vector>

namespace lldb_private {

///------------------------------------------------------------------------------
/// @class RegisterInfoInterface
///
/// RegisterInfo interface to patch RegisterInfo structure for archs.
///------------------------------------------------------------------------------
class RegisterInfoInterface {
public:
  RegisterInfoInterface(const lldb_private::ArchSpec &target_arch)
      : m_target_arch(target_arch) {}
  virtual ~RegisterInfoInterface() {}

  virtual size_t GetGPRSize() const = 0;

  virtual const lldb_private::RegisterInfo *GetRegisterInfo() const = 0;

  // Returns the number of registers including the user registers and the
  // lldb internal registers also
  virtual uint32_t GetRegisterCount() const = 0;

  // Returns the number of the user registers (excluding the registers
  // kept for lldb internal use only). Subclasses should override it if
  // they belongs to an architecture with lldb internal registers.
  virtual uint32_t GetUserRegisterCount() const { return GetRegisterCount(); }

  const lldb_private::ArchSpec &GetTargetArchitecture() const {
    return m_target_arch;
  }

  virtual const lldb_private::RegisterInfo *
  GetDynamicRegisterInfo(const char *reg_name) const {
    const std::vector<lldb_private::RegisterInfo> *d_register_infos =
        GetDynamicRegisterInfoP();
    if (d_register_infos != nullptr) {
      std::vector<lldb_private::RegisterInfo>::const_iterator pos =
          d_register_infos->begin();
      for (; pos < d_register_infos->end(); pos++) {
        if (::strcmp(reg_name, pos->name) == 0)
          return (d_register_infos->data() + (pos - d_register_infos->begin()));
      }
    }
    return nullptr;
  }

  virtual const std::vector<lldb_private::RegisterInfo> *
  GetDynamicRegisterInfoP() const {
    return nullptr;
  }

public:
  // FIXME make private.
  lldb_private::ArchSpec m_target_arch;
};
}

#endif
