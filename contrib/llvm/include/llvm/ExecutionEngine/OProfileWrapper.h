//===-- OProfileWrapper.h - OProfile JIT API Wrapper ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This file defines a OProfileWrapper object that detects if the oprofile
// daemon is running, and provides wrappers for opagent functions used to
// communicate with the oprofile JIT interface. The dynamic library libopagent
// does not need to be linked directly as this object lazily loads the library
// when the first op_ function is called.
//
// See http://oprofile.sourceforge.net/doc/devel/jit-interface.html for the
// definition of the interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_OPROFILEWRAPPER_H
#define LLVM_EXECUTIONENGINE_OPROFILEWRAPPER_H

#include "llvm/Support/DataTypes.h"
#include <opagent.h>

namespace llvm {


class OProfileWrapper {
  typedef  op_agent_t    (*op_open_agent_ptr_t)();
  typedef  int           (*op_close_agent_ptr_t)(op_agent_t);
  typedef  int           (*op_write_native_code_ptr_t)(op_agent_t,
                                                const char*,
                                                uint64_t,
                                                void const*,
                                                const unsigned int);
  typedef  int           (*op_write_debug_line_info_ptr_t)(op_agent_t,
                                                void const*,
                                                size_t,
                                                struct debug_line_info const*);
  typedef  int           (*op_unload_native_code_ptr_t)(op_agent_t, uint64_t);

  // Also used for op_minor_version function which has the same signature
  typedef  int           (*op_major_version_ptr_t)();

  // This is not a part of the opagent API, but is useful nonetheless
  typedef  bool          (*IsOProfileRunningPtrT)();


  op_agent_t                      Agent;
  op_open_agent_ptr_t             OpenAgentFunc;
  op_close_agent_ptr_t            CloseAgentFunc;
  op_write_native_code_ptr_t      WriteNativeCodeFunc;
  op_write_debug_line_info_ptr_t  WriteDebugLineInfoFunc;
  op_unload_native_code_ptr_t     UnloadNativeCodeFunc;
  op_major_version_ptr_t          MajorVersionFunc;
  op_major_version_ptr_t          MinorVersionFunc;
  IsOProfileRunningPtrT           IsOProfileRunningFunc;

  bool Initialized;

public:
  OProfileWrapper();

  // For testing with a mock opagent implementation, skips the dynamic load and
  // the function resolution.
  OProfileWrapper(op_open_agent_ptr_t OpenAgentImpl,
                  op_close_agent_ptr_t CloseAgentImpl,
                  op_write_native_code_ptr_t WriteNativeCodeImpl,
                  op_write_debug_line_info_ptr_t WriteDebugLineInfoImpl,
                  op_unload_native_code_ptr_t UnloadNativeCodeImpl,
                  op_major_version_ptr_t MajorVersionImpl,
                  op_major_version_ptr_t MinorVersionImpl,
                  IsOProfileRunningPtrT MockIsOProfileRunningImpl = 0)
  : OpenAgentFunc(OpenAgentImpl),
    CloseAgentFunc(CloseAgentImpl),
    WriteNativeCodeFunc(WriteNativeCodeImpl),
    WriteDebugLineInfoFunc(WriteDebugLineInfoImpl),
    UnloadNativeCodeFunc(UnloadNativeCodeImpl),
    MajorVersionFunc(MajorVersionImpl),
    MinorVersionFunc(MinorVersionImpl),
    IsOProfileRunningFunc(MockIsOProfileRunningImpl),
    Initialized(true)
  {
  }

  // Calls op_open_agent in the oprofile JIT library and saves the returned
  // op_agent_t handle internally so it can be used when calling all the other
  // op_* functions. Callers of this class do not need to keep track of
  // op_agent_t objects.
  bool op_open_agent();

  int op_close_agent();
  int op_write_native_code(const char* name,
                           uint64_t addr,
                           void const* code,
                           const unsigned int size);
  int op_write_debug_line_info(void const* code,
                               size_t num_entries,
                               struct debug_line_info const* info);
  int op_unload_native_code(uint64_t addr);
  int op_major_version();
  int op_minor_version();

  // Returns true if the oprofiled process is running, the opagent library is
  // loaded and a connection to the agent has been established, and false
  // otherwise.
  bool isAgentAvailable();

private:
  // Loads the libopagent library and initializes this wrapper if the oprofile
  // daemon is running
  bool initialize();

  // Searches /proc for the oprofile daemon and returns true if the process if
  // found, or false otherwise.
  bool checkForOProfileProcEntry();

  bool isOProfileRunning();
};

} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_OPROFILEWRAPPER_H
