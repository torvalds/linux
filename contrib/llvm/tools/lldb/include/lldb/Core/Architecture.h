//===-- Architecture.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_ARCHITECTURE_H
#define LLDB_CORE_ARCHITECTURE_H

#include "lldb/Core/PluginInterface.h"

namespace lldb_private {

class Architecture : public PluginInterface {
public:
  Architecture() = default;
  virtual ~Architecture() = default;

  //------------------------------------------------------------------
  /// This is currently intended to handle cases where a
  /// program stops at an instruction that won't get executed and it
  /// allows the stop reason, like "breakpoint hit", to be replaced
  /// with a different stop reason like "no stop reason".
  ///
  /// This is specifically used for ARM in Thumb code when we stop in
  /// an IT instruction (if/then/else) where the instruction won't get
  /// executed and therefore it wouldn't be correct to show the program
  /// stopped at the current PC. The code is generic and applies to all
  /// ARM CPUs.
  //------------------------------------------------------------------
  virtual void OverrideStopInfo(Thread &thread) const = 0;

  //------------------------------------------------------------------
  /// This method is used to get the number of bytes that should be
  /// skipped, from function start address, to reach the first
  /// instruction after the prologue. If overrode, it must return
  /// non-zero only if the current address matches one of the known
  /// function entry points.
  ///
  /// This method is called only if the standard platform-independent
  /// code fails to get the number of bytes to skip, giving the plugin
  /// a chance to try to find the missing info.
  ///
  /// This is specifically used for PPC64, where functions may have
  /// more than one entry point, global and local, so both should
  /// be compared with current address, in order to find out the
  /// number of bytes that should be skipped, in case we are stopped
  /// at either function entry point.
  //------------------------------------------------------------------
  virtual size_t GetBytesToSkip(Symbol &func, const Address &curr_addr) const {
    return 0;
  }

  //------------------------------------------------------------------
  /// Adjust function breakpoint address, if needed. In some cases,
  /// the function start address is not the right place to set the
  /// breakpoint, specially in functions with multiple entry points.
  ///
  /// This is specifically used for PPC64, for functions that have
  /// both a global and a local entry point. In this case, the
  /// breakpoint is adjusted to the first function address reached
  /// by both entry points.
  //------------------------------------------------------------------
  virtual void AdjustBreakpointAddress(const Symbol &func,
                                       Address &addr) const {}


  //------------------------------------------------------------------
  /// Get \a load_addr as a callable code load address for this target
  ///
  /// Take \a load_addr and potentially add any address bits that are
  /// needed to make the address callable. For ARM this can set bit
  /// zero (if it already isn't) if \a load_addr is a thumb function.
  /// If \a addr_class is set to AddressClass::eInvalid, then the address
  /// adjustment will always happen. If it is set to an address class
  /// that doesn't have code in it, LLDB_INVALID_ADDRESS will be
  /// returned.
  //------------------------------------------------------------------
  virtual lldb::addr_t GetCallableLoadAddress(
      lldb::addr_t addr, AddressClass addr_class = AddressClass::eInvalid) const {
    return addr;
  }

  //------------------------------------------------------------------
  /// Get \a load_addr as an opcode for this target.
  ///
  /// Take \a load_addr and potentially strip any address bits that are
  /// needed to make the address point to an opcode. For ARM this can
  /// clear bit zero (if it already isn't) if \a load_addr is a
  /// thumb function and load_addr is in code.
  /// If \a addr_class is set to AddressClass::eInvalid, then the address
  /// adjustment will always happen. If it is set to an address class
  /// that doesn't have code in it, LLDB_INVALID_ADDRESS will be
  /// returned.
  //------------------------------------------------------------------

  virtual lldb::addr_t GetOpcodeLoadAddress(
      lldb::addr_t addr, AddressClass addr_class = AddressClass::eInvalid) const {
    return addr;
  }

  // Get load_addr as breakable load address for this target. Take a addr and
  // check if for any reason there is a better address than this to put a
  // breakpoint on. If there is then return that address. For MIPS, if
  // instruction at addr is a delay slot instruction then this method will find
  // the address of its previous instruction and return that address.
  virtual lldb::addr_t GetBreakableLoadAddress(lldb::addr_t addr,
                                               Target &target) const {
    return addr;
  }

private:
  Architecture(const Architecture &) = delete;
  void operator=(const Architecture &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_ARCHITECTURE_H
