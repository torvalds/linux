//===-- ThreadMinidump.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadMinidump_h_
#define liblldb_ThreadMinidump_h_

#include "MinidumpTypes.h"

#include "lldb/Target/Thread.h"


namespace lldb_private {

namespace minidump {

class ThreadMinidump : public Thread {
public:
  ThreadMinidump(Process &process, const MinidumpThread &td,
                 llvm::ArrayRef<uint8_t> gpregset_data);

  ~ThreadMinidump() override;

  void RefreshStateAfterStop() override;

  lldb::RegisterContextSP GetRegisterContext() override;

  lldb::RegisterContextSP
  CreateRegisterContextForFrame(StackFrame *frame) override;

protected:
  lldb::RegisterContextSP m_thread_reg_ctx_sp;
  llvm::ArrayRef<uint8_t> m_gpregset_data;

  bool CalculateStopInfo() override;
};

} // namespace minidump
} // namespace lldb_private

#endif // liblldb_ThreadMinidump_h_
