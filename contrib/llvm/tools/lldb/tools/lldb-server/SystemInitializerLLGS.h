//===-- SystemInitializerLLGS.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYSTEMINITIALIZERLLGS_H
#define LLDB_SYSTEMINITIALIZERLLGS_H

#include "lldb/Initialization/SystemInitializer.h"
#include "lldb/Initialization/SystemInitializerCommon.h"

class SystemInitializerLLGS : public lldb_private::SystemInitializerCommon {
public:
  llvm::Error
  Initialize(const lldb_private::InitializerOptions &options) override;
  void Terminate() override;
};

#endif // LLDB_SYSTEMINITIALIZERLLGS_H
