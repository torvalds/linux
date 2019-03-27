//===-- SystemInitializer.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INITIALIZATION_SYSTEM_INITIALIZER_H
#define LLDB_INITIALIZATION_SYSTEM_INITIALIZER_H

#include "llvm/Support/Error.h"

#include <string>

namespace lldb_private {

struct InitializerOptions {
  bool reproducer_capture = false;
  bool reproducer_replay = false;
  std::string reproducer_path;
};

class SystemInitializer {
public:
  SystemInitializer();
  virtual ~SystemInitializer();

  virtual llvm::Error Initialize(const InitializerOptions &options) = 0;
  virtual void Terminate() = 0;
};
}

#endif
