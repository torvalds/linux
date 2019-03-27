//===-- SystemInitializerCommon.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INITIALIZATION_SYSTEM_INITIALIZER_COMMON_H
#define LLDB_INITIALIZATION_SYSTEM_INITIALIZER_COMMON_H

#include "SystemInitializer.h"

namespace lldb_private {
//------------------------------------------------------------------
/// Initializes common lldb functionality.
///
/// This class is responsible for initializing a subset of lldb
/// useful to both debug servers and debug clients.  Debug servers
/// do not use all of LLDB and desire small binary sizes, so this
/// functionality is separate.  This class is used by constructing
/// an instance of SystemLifetimeManager with this class passed to
/// the constructor.
//------------------------------------------------------------------
class SystemInitializerCommon : public SystemInitializer {
public:
  SystemInitializerCommon();
  ~SystemInitializerCommon() override;

  llvm::Error Initialize(const InitializerOptions &options) override;
  void Terminate() override;
};

} // namespace lldb_private

#endif // LLDB_INITIALIZATION_SYSTEM_INITIALIZER_COMMON_H
