//===-- StoppointLocation.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StoppointLocation_h_
#define liblldb_StoppointLocation_h_

#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"
// #include "lldb/Breakpoint/BreakpointOptions.h"

namespace lldb_private {

class StoppointLocation {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  StoppointLocation(lldb::break_id_t bid, lldb::addr_t m_addr, bool hardware);

  StoppointLocation(lldb::break_id_t bid, lldb::addr_t m_addr,
                    uint32_t byte_size, bool hardware);

  virtual ~StoppointLocation();

  //------------------------------------------------------------------
  // Operators
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  // Methods
  //------------------------------------------------------------------
  virtual lldb::addr_t GetLoadAddress() const { return m_addr; }

  virtual void SetLoadAddress(lldb::addr_t addr) { m_addr = addr; }

  uint32_t GetByteSize() const { return m_byte_size; }

  uint32_t GetHitCount() const { return m_hit_count; }

  uint32_t GetHardwareIndex() const { return m_hardware_index; }

  bool HardwareRequired() const { return m_hardware; }

  virtual bool IsHardware() const {
    return m_hardware_index != LLDB_INVALID_INDEX32;
  }

  virtual bool ShouldStop(StoppointCallbackContext *context) { return true; }

  virtual void Dump(Stream *stream) const {}

  void SetHardwareIndex(uint32_t index) { m_hardware_index = index; }

  lldb::break_id_t GetID() const { return m_loc_id; }

protected:
  //------------------------------------------------------------------
  // Classes that inherit from StoppointLocation can see and modify these
  //------------------------------------------------------------------
  lldb::break_id_t m_loc_id; // Stoppoint location ID
  lldb::addr_t
      m_addr; // The load address of this stop point. The base Stoppoint doesn't
  // store a full Address since that's not needed for the breakpoint sites.
  bool m_hardware; // True if this point has been is required to use hardware
                   // (which may fail due to lack of resources)
  uint32_t m_hardware_index; // The hardware resource index for this
                             // breakpoint/watchpoint
  uint32_t m_byte_size; // The size in bytes of stop location.  e.g. the length
                        // of the trap opcode for
  // software breakpoints, or the optional length in bytes for hardware
  // breakpoints, or the length of the watchpoint.
  uint32_t
      m_hit_count; // Number of times this breakpoint/watchpoint has been hit

  // If you override this, be sure to call the base class to increment the
  // internal counter.
  void IncrementHitCount() { ++m_hit_count; }

  void DecrementHitCount();

private:
  //------------------------------------------------------------------
  // For StoppointLocation only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(StoppointLocation);
  StoppointLocation(); // Disallow default constructor
};

} // namespace lldb_private

#endif // liblldb_StoppointLocation_h_
