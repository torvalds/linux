//===-- SystemInitializerFull.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SYSTEM_INITIALIZER_FULL_H
#define LLDB_API_SYSTEM_INITIALIZER_FULL_H

#include "lldb/Initialization/SystemInitializerCommon.h"

namespace lldb_private {
//------------------------------------------------------------------
/// Initializes lldb.
///
/// This class is responsible for initializing all of lldb system
/// services needed to use the full LLDB application.  This class is
/// not intended to be used externally, but is instead used
/// internally by SBDebugger to initialize the system.
//------------------------------------------------------------------
class SystemInitializerFull : public SystemInitializerCommon {
public:
  SystemInitializerFull();
  ~SystemInitializerFull() override;

  llvm::Error Initialize(const InitializerOptions &options) override;
  void Terminate() override;

private:
  void InitializeSWIG();
};

} // namespace lldb_private

#endif // LLDB_API_SYSTEM_INITIALIZER_FULL_H
