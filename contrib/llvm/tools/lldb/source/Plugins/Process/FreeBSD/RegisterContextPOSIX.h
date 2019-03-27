//===-- RegisterContextPOSIX.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextPOSIX_H_
#define liblldb_RegisterContextPOSIX_H_

#include "Plugins/Process/Utility/RegisterInfoInterface.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/ArchSpec.h"

//------------------------------------------------------------------------------
/// @class POSIXBreakpointProtocol
///
/// Extends RegisterClass with a few virtual operations useful on POSIX.
class POSIXBreakpointProtocol {
public:
  POSIXBreakpointProtocol() { m_watchpoints_initialized = false; }
  virtual ~POSIXBreakpointProtocol() {}

  /// Updates the register state of the associated thread after hitting a
  /// breakpoint (if that make sense for the architecture).  Default
  /// implementation simply returns true for architectures which do not
  /// require any update.
  ///
  /// @return
  ///    True if the operation succeeded and false otherwise.
  virtual bool UpdateAfterBreakpoint() = 0;

  /// Determines the index in lldb's register file given a kernel byte offset.
  virtual unsigned GetRegisterIndexFromOffset(unsigned offset) = 0;

  // Checks to see if a watchpoint specified by hw_index caused the inferior
  // to stop.
  virtual bool IsWatchpointHit(uint32_t hw_index) = 0;

  // Resets any watchpoints that have been hit.
  virtual bool ClearWatchpointHits() = 0;

  // Returns the watchpoint address associated with a watchpoint hardware
  // index.
  virtual lldb::addr_t GetWatchpointAddress(uint32_t hw_index) = 0;

  virtual bool IsWatchpointVacant(uint32_t hw_index) = 0;

  virtual bool SetHardwareWatchpointWithIndex(lldb::addr_t addr, size_t size,
                                              bool read, bool write,
                                              uint32_t hw_index) = 0;

  // From lldb_private::RegisterContext
  virtual uint32_t NumSupportedHardwareWatchpoints() = 0;

  // Force m_watchpoints_initialized to TRUE
  void ForceWatchpointsInitialized() { m_watchpoints_initialized = true; }

protected:
  bool m_watchpoints_initialized;
};

#endif // #ifndef liblldb_RegisterContextPOSIX_H_
