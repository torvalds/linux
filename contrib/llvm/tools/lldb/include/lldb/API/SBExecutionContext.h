//===-- SBExecutionContext.h -----------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBExecutionContext_h_
#define LLDB_SBExecutionContext_h_

#include "lldb/API/SBDefines.h"

#include <stdio.h>
#include <vector>

namespace lldb {

class LLDB_API SBExecutionContext {
  friend class SBCommandInterpreter;

public:
  SBExecutionContext();

  SBExecutionContext(const lldb::SBExecutionContext &rhs);

  SBExecutionContext(lldb::ExecutionContextRefSP exe_ctx_ref_sp);

  SBExecutionContext(const lldb::SBTarget &target);

  SBExecutionContext(const lldb::SBProcess &process);

  SBExecutionContext(lldb::SBThread thread); // can't be a const& because
                                             // SBThread::get() isn't itself a
                                             // const function

  SBExecutionContext(const lldb::SBFrame &frame);

  ~SBExecutionContext();

  const SBExecutionContext &operator=(const lldb::SBExecutionContext &rhs);

  SBTarget GetTarget() const;

  SBProcess GetProcess() const;

  SBThread GetThread() const;

  SBFrame GetFrame() const;

protected:
  ExecutionContextRefSP &GetSP() const;

  void reset(lldb::ExecutionContextRefSP &event_sp);

  lldb_private::ExecutionContextRef *get() const;

private:
  mutable lldb::ExecutionContextRefSP m_exe_ctx_sp;
};

} // namespace lldb

#endif // LLDB_SBExecutionContext_h_
