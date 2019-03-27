//===-- PluginInterface.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_PluginInterface_h_
#define liblldb_PluginInterface_h_

#include "lldb/lldb-private.h"

namespace lldb_private {

class PluginInterface {
public:
  virtual ~PluginInterface() {}

  virtual ConstString GetPluginName() = 0;

  virtual uint32_t GetPluginVersion() = 0;
};

} // namespace lldb_private

#endif // liblldb_PluginInterface_h_
