//===-- OperatingSystem.h ----------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OperatingSystem_h_
#define liblldb_OperatingSystem_h_


#include "lldb/Core/PluginInterface.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class OperatingSystem OperatingSystem.h "lldb/Target/OperatingSystem.h"
/// A plug-in interface definition class for halted OS helpers.
///
/// Halted OS plug-ins can be used by any process to locate and create
/// OS objects, like threads, during the lifetime of a debug session.
/// This is commonly used when attaching to an operating system that is
/// halted, such as when debugging over JTAG or connecting to low level kernel
/// debug services.
//----------------------------------------------------------------------

class OperatingSystem : public PluginInterface {
public:
  //------------------------------------------------------------------
  /// Find a halted OS plugin for a given process.
  ///
  /// Scans the installed OperatingSystem plug-ins and tries to find an
  /// instance that matches the current target triple and executable.
  ///
  /// @param[in] process
  ///     The process for which to try and locate a halted OS
  ///     plug-in instance.
  ///
  /// @param[in] plugin_name
  ///     An optional name of a specific halted OS plug-in that
  ///     should be used. If NULL, pick the best plug-in.
  //------------------------------------------------------------------
  static OperatingSystem *FindPlugin(Process *process, const char *plugin_name);

  //------------------------------------------------------------------
  // Class Methods
  //------------------------------------------------------------------
  OperatingSystem(Process *process);

  ~OperatingSystem() override;

  //------------------------------------------------------------------
  // Plug-in Methods
  //------------------------------------------------------------------
  virtual bool UpdateThreadList(ThreadList &old_thread_list,
                                ThreadList &real_thread_list,
                                ThreadList &new_thread_list) = 0;

  virtual void ThreadWasSelected(Thread *thread) = 0;

  virtual lldb::RegisterContextSP
  CreateRegisterContextForThread(Thread *thread,
                                 lldb::addr_t reg_data_addr) = 0;

  virtual lldb::StopInfoSP CreateThreadStopReason(Thread *thread) = 0;

  virtual lldb::ThreadSP CreateThread(lldb::tid_t tid, lldb::addr_t context) {
    return lldb::ThreadSP();
  }

  virtual bool IsOperatingSystemPluginThread(const lldb::ThreadSP &thread_sp);

protected:
  //------------------------------------------------------------------
  // Member variables.
  //------------------------------------------------------------------
  Process
      *m_process; ///< The process that this dynamic loader plug-in is tracking.
private:
  DISALLOW_COPY_AND_ASSIGN(OperatingSystem);
};

} // namespace lldb_private

#endif // liblldb_OperatingSystem_h_
