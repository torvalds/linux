//===-- SBInitializerOptions.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBInitializerOptuions_h_
#define LLDB_SBInitializerOptuions_h_

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBFileSpec.h"

namespace lldb_private {
struct InitializerOptions;
}

namespace lldb {

class LLDB_API SBInitializerOptions {
public:
  SBInitializerOptions();
  SBInitializerOptions(const lldb::SBInitializerOptions &rhs);
  ~SBInitializerOptions();
  const SBInitializerOptions &operator=(const lldb::SBInitializerOptions &rhs);

  void SetCaptureReproducer(bool b);
  void SetReplayReproducer(bool b);
  void SetReproducerPath(const char *path);

  lldb_private::InitializerOptions &ref() const;

private:
  friend class SBDebugger;

  std::unique_ptr<lldb_private::InitializerOptions> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_SBInitializerOptuions_h_
