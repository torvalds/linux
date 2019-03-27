//===-- NativeProcessProtocol.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/common/NativeBreakpointList.h"
#include "lldb/Host/common/NativeRegisterContext.h"
#include "lldb/Host/common/NativeThreadProtocol.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/lldb-enumerations.h"

using namespace lldb;
using namespace lldb_private;

// -----------------------------------------------------------------------------
// NativeProcessProtocol Members
// -----------------------------------------------------------------------------

NativeProcessProtocol::NativeProcessProtocol(lldb::pid_t pid, int terminal_fd,
                                             NativeDelegate &delegate)
    : m_pid(pid), m_terminal_fd(terminal_fd) {
  bool registered = RegisterNativeDelegate(delegate);
  assert(registered);
  (void)registered;
}

lldb_private::Status NativeProcessProtocol::Interrupt() {
  Status error;
#if !defined(SIGSTOP)
  error.SetErrorString("local host does not support signaling");
  return error;
#else
  return Signal(SIGSTOP);
#endif
}

Status NativeProcessProtocol::IgnoreSignals(llvm::ArrayRef<int> signals) {
  m_signals_to_ignore.clear();
  m_signals_to_ignore.insert(signals.begin(), signals.end());
  return Status();
}

lldb_private::Status
NativeProcessProtocol::GetMemoryRegionInfo(lldb::addr_t load_addr,
                                           MemoryRegionInfo &range_info) {
  // Default: not implemented.
  return Status("not implemented");
}

llvm::Optional<WaitStatus> NativeProcessProtocol::GetExitStatus() {
  if (m_state == lldb::eStateExited)
    return m_exit_status;

  return llvm::None;
}

bool NativeProcessProtocol::SetExitStatus(WaitStatus status,
                                          bool bNotifyStateChange) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  LLDB_LOG(log, "status = {0}, notify = {1}", status, bNotifyStateChange);

  // Exit status already set
  if (m_state == lldb::eStateExited) {
    if (m_exit_status)
      LLDB_LOG(log, "exit status already set to {0}", *m_exit_status);
    else
      LLDB_LOG(log, "state is exited, but status not set");
    return false;
  }

  m_state = lldb::eStateExited;
  m_exit_status = status;

  if (bNotifyStateChange)
    SynchronouslyNotifyProcessStateChanged(lldb::eStateExited);

  return true;
}

NativeThreadProtocol *NativeProcessProtocol::GetThreadAtIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(m_threads_mutex);
  if (idx < m_threads.size())
    return m_threads[idx].get();
  return nullptr;
}

NativeThreadProtocol *
NativeProcessProtocol::GetThreadByIDUnlocked(lldb::tid_t tid) {
  for (const auto &thread : m_threads) {
    if (thread->GetID() == tid)
      return thread.get();
  }
  return nullptr;
}

NativeThreadProtocol *NativeProcessProtocol::GetThreadByID(lldb::tid_t tid) {
  std::lock_guard<std::recursive_mutex> guard(m_threads_mutex);
  return GetThreadByIDUnlocked(tid);
}

bool NativeProcessProtocol::IsAlive() const {
  return m_state != eStateDetached && m_state != eStateExited &&
         m_state != eStateInvalid && m_state != eStateUnloaded;
}

const NativeWatchpointList::WatchpointMap &
NativeProcessProtocol::GetWatchpointMap() const {
  return m_watchpoint_list.GetWatchpointMap();
}

llvm::Optional<std::pair<uint32_t, uint32_t>>
NativeProcessProtocol::GetHardwareDebugSupportInfo() const {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  // get any thread
  NativeThreadProtocol *thread(
      const_cast<NativeProcessProtocol *>(this)->GetThreadAtIndex(0));
  if (!thread) {
    LLDB_LOG(log, "failed to find a thread to grab a NativeRegisterContext!");
    return llvm::None;
  }

  NativeRegisterContext &reg_ctx = thread->GetRegisterContext();
  return std::make_pair(reg_ctx.NumSupportedHardwareBreakpoints(),
                        reg_ctx.NumSupportedHardwareWatchpoints());
}

Status NativeProcessProtocol::SetWatchpoint(lldb::addr_t addr, size_t size,
                                            uint32_t watch_flags,
                                            bool hardware) {
  // This default implementation assumes setting the watchpoint for the process
  // will require setting the watchpoint for each of the threads.  Furthermore,
  // it will track watchpoints set for the process and will add them to each
  // thread that is attached to via the (FIXME implement) OnThreadAttached ()
  // method.

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  // Update the thread list
  UpdateThreads();

  // Keep track of the threads we successfully set the watchpoint for.  If one
  // of the thread watchpoint setting operations fails, back off and remove the
  // watchpoint for all the threads that were successfully set so we get back
  // to a consistent state.
  std::vector<NativeThreadProtocol *> watchpoint_established_threads;

  // Tell each thread to set a watchpoint.  In the event that hardware
  // watchpoints are requested but the SetWatchpoint fails, try to set a
  // software watchpoint as a fallback.  It's conceivable that if there are
  // more threads than hardware watchpoints available, some of the threads will
  // fail to set hardware watchpoints while software ones may be available.
  std::lock_guard<std::recursive_mutex> guard(m_threads_mutex);
  for (const auto &thread : m_threads) {
    assert(thread && "thread list should not have a NULL thread!");

    Status thread_error =
        thread->SetWatchpoint(addr, size, watch_flags, hardware);
    if (thread_error.Fail() && hardware) {
      // Try software watchpoints since we failed on hardware watchpoint
      // setting and we may have just run out of hardware watchpoints.
      thread_error = thread->SetWatchpoint(addr, size, watch_flags, false);
      if (thread_error.Success())
        LLDB_LOG(log,
                 "hardware watchpoint requested but software watchpoint set");
    }

    if (thread_error.Success()) {
      // Remember that we set this watchpoint successfully in case we need to
      // clear it later.
      watchpoint_established_threads.push_back(thread.get());
    } else {
      // Unset the watchpoint for each thread we successfully set so that we
      // get back to a consistent state of "not set" for the watchpoint.
      for (auto unwatch_thread_sp : watchpoint_established_threads) {
        Status remove_error = unwatch_thread_sp->RemoveWatchpoint(addr);
        if (remove_error.Fail())
          LLDB_LOG(log, "RemoveWatchpoint failed for pid={0}, tid={1}: {2}",
                   GetID(), unwatch_thread_sp->GetID(), remove_error);
      }

      return thread_error;
    }
  }
  return m_watchpoint_list.Add(addr, size, watch_flags, hardware);
}

Status NativeProcessProtocol::RemoveWatchpoint(lldb::addr_t addr) {
  // Update the thread list
  UpdateThreads();

  Status overall_error;

  std::lock_guard<std::recursive_mutex> guard(m_threads_mutex);
  for (const auto &thread : m_threads) {
    assert(thread && "thread list should not have a NULL thread!");

    const Status thread_error = thread->RemoveWatchpoint(addr);
    if (thread_error.Fail()) {
      // Keep track of the first thread error if any threads fail. We want to
      // try to remove the watchpoint from every thread, though, even if one or
      // more have errors.
      if (!overall_error.Fail())
        overall_error = thread_error;
    }
  }
  const Status error = m_watchpoint_list.Remove(addr);
  return overall_error.Fail() ? overall_error : error;
}

const HardwareBreakpointMap &
NativeProcessProtocol::GetHardwareBreakpointMap() const {
  return m_hw_breakpoints_map;
}

Status NativeProcessProtocol::SetHardwareBreakpoint(lldb::addr_t addr,
                                                    size_t size) {
  // This default implementation assumes setting a hardware breakpoint for this
  // process will require setting same hardware breakpoint for each of its
  // existing threads. New thread will do the same once created.
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  // Update the thread list
  UpdateThreads();

  // Exit here if target does not have required hardware breakpoint capability.
  auto hw_debug_cap = GetHardwareDebugSupportInfo();

  if (hw_debug_cap == llvm::None || hw_debug_cap->first == 0 ||
      hw_debug_cap->first <= m_hw_breakpoints_map.size())
    return Status("Target does not have required no of hardware breakpoints");

  // Vector below stores all thread pointer for which we have we successfully
  // set this hardware breakpoint. If any of the current process threads fails
  // to set this hardware breakpoint then roll back and remove this breakpoint
  // for all the threads that had already set it successfully.
  std::vector<NativeThreadProtocol *> breakpoint_established_threads;

  // Request to set a hardware breakpoint for each of current process threads.
  std::lock_guard<std::recursive_mutex> guard(m_threads_mutex);
  for (const auto &thread : m_threads) {
    assert(thread && "thread list should not have a NULL thread!");

    Status thread_error = thread->SetHardwareBreakpoint(addr, size);
    if (thread_error.Success()) {
      // Remember that we set this breakpoint successfully in case we need to
      // clear it later.
      breakpoint_established_threads.push_back(thread.get());
    } else {
      // Unset the breakpoint for each thread we successfully set so that we
      // get back to a consistent state of "not set" for this hardware
      // breakpoint.
      for (auto rollback_thread_sp : breakpoint_established_threads) {
        Status remove_error =
            rollback_thread_sp->RemoveHardwareBreakpoint(addr);
        if (remove_error.Fail())
          LLDB_LOG(log,
                   "RemoveHardwareBreakpoint failed for pid={0}, tid={1}: {2}",
                   GetID(), rollback_thread_sp->GetID(), remove_error);
      }

      return thread_error;
    }
  }

  // Register new hardware breakpoint into hardware breakpoints map of current
  // process.
  m_hw_breakpoints_map[addr] = {addr, size};

  return Status();
}

Status NativeProcessProtocol::RemoveHardwareBreakpoint(lldb::addr_t addr) {
  // Update the thread list
  UpdateThreads();

  Status error;

  std::lock_guard<std::recursive_mutex> guard(m_threads_mutex);
  for (const auto &thread : m_threads) {
    assert(thread && "thread list should not have a NULL thread!");
    error = thread->RemoveHardwareBreakpoint(addr);
  }

  // Also remove from hardware breakpoint map of current process.
  m_hw_breakpoints_map.erase(addr);

  return error;
}

bool NativeProcessProtocol::RegisterNativeDelegate(
    NativeDelegate &native_delegate) {
  std::lock_guard<std::recursive_mutex> guard(m_delegates_mutex);
  if (std::find(m_delegates.begin(), m_delegates.end(), &native_delegate) !=
      m_delegates.end())
    return false;

  m_delegates.push_back(&native_delegate);
  native_delegate.InitializeDelegate(this);
  return true;
}

bool NativeProcessProtocol::UnregisterNativeDelegate(
    NativeDelegate &native_delegate) {
  std::lock_guard<std::recursive_mutex> guard(m_delegates_mutex);

  const auto initial_size = m_delegates.size();
  m_delegates.erase(
      remove(m_delegates.begin(), m_delegates.end(), &native_delegate),
      m_delegates.end());

  // We removed the delegate if the count of delegates shrank after removing
  // all copies of the given native_delegate from the vector.
  return m_delegates.size() < initial_size;
}

void NativeProcessProtocol::SynchronouslyNotifyProcessStateChanged(
    lldb::StateType state) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  std::lock_guard<std::recursive_mutex> guard(m_delegates_mutex);
  for (auto native_delegate : m_delegates)
    native_delegate->ProcessStateChanged(this, state);

  if (log) {
    if (!m_delegates.empty()) {
      log->Printf("NativeProcessProtocol::%s: sent state notification [%s] "
                  "from process %" PRIu64,
                  __FUNCTION__, lldb_private::StateAsCString(state), GetID());
    } else {
      log->Printf("NativeProcessProtocol::%s: would send state notification "
                  "[%s] from process %" PRIu64 ", but no delegates",
                  __FUNCTION__, lldb_private::StateAsCString(state), GetID());
    }
  }
}

void NativeProcessProtocol::NotifyDidExec() {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log)
    log->Printf("NativeProcessProtocol::%s - preparing to call delegates",
                __FUNCTION__);

  {
    std::lock_guard<std::recursive_mutex> guard(m_delegates_mutex);
    for (auto native_delegate : m_delegates)
      native_delegate->DidExec(this);
  }
}

Status NativeProcessProtocol::SetSoftwareBreakpoint(lldb::addr_t addr,
                                                    uint32_t size_hint) {
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));
  LLDB_LOG(log, "addr = {0:x}, size_hint = {1}", addr, size_hint);

  auto it = m_software_breakpoints.find(addr);
  if (it != m_software_breakpoints.end()) {
    ++it->second.ref_count;
    return Status();
  }
  auto expected_bkpt = EnableSoftwareBreakpoint(addr, size_hint);
  if (!expected_bkpt)
    return Status(expected_bkpt.takeError());

  m_software_breakpoints.emplace(addr, std::move(*expected_bkpt));
  return Status();
}

Status NativeProcessProtocol::RemoveSoftwareBreakpoint(lldb::addr_t addr) {
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));
  LLDB_LOG(log, "addr = {0:x}", addr);
  auto it = m_software_breakpoints.find(addr);
  if (it == m_software_breakpoints.end())
    return Status("Breakpoint not found.");
  assert(it->second.ref_count > 0);
  if (--it->second.ref_count > 0)
    return Status();

  // This is the last reference. Let's remove the breakpoint.
  Status error;

  // Clear a software breakpoint instruction
  llvm::SmallVector<uint8_t, 4> curr_break_op(
      it->second.breakpoint_opcodes.size(), 0);

  // Read the breakpoint opcode
  size_t bytes_read = 0;
  error =
      ReadMemory(addr, curr_break_op.data(), curr_break_op.size(), bytes_read);
  if (error.Fail() || bytes_read < curr_break_op.size()) {
    return Status("addr=0x%" PRIx64
                  ": tried to read %zu bytes but only read %zu",
                  addr, curr_break_op.size(), bytes_read);
  }
  const auto &saved = it->second.saved_opcodes;
  // Make sure the breakpoint opcode exists at this address
  if (makeArrayRef(curr_break_op) != it->second.breakpoint_opcodes) {
    if (curr_break_op != it->second.saved_opcodes)
      return Status("Original breakpoint trap is no longer in memory.");
    LLDB_LOG(log,
             "Saved opcodes ({0:@[x]}) have already been restored at {1:x}.",
             llvm::make_range(saved.begin(), saved.end()), addr);
  } else {
    // We found a valid breakpoint opcode at this address, now restore the
    // saved opcode.
    size_t bytes_written = 0;
    error = WriteMemory(addr, saved.data(), saved.size(), bytes_written);
    if (error.Fail() || bytes_written < saved.size()) {
      return Status("addr=0x%" PRIx64
                    ": tried to write %zu bytes but only wrote %zu",
                    addr, saved.size(), bytes_written);
    }

    // Verify that our original opcode made it back to the inferior
    llvm::SmallVector<uint8_t, 4> verify_opcode(saved.size(), 0);
    size_t verify_bytes_read = 0;
    error = ReadMemory(addr, verify_opcode.data(), verify_opcode.size(),
                       verify_bytes_read);
    if (error.Fail() || verify_bytes_read < verify_opcode.size()) {
      return Status("addr=0x%" PRIx64
                    ": tried to read %zu verification bytes but only read %zu",
                    addr, verify_opcode.size(), verify_bytes_read);
    }
    if (verify_opcode != saved)
      LLDB_LOG(log, "Restoring bytes at {0:x}: {1:@[x]}", addr,
               llvm::make_range(saved.begin(), saved.end()));
  }

  m_software_breakpoints.erase(it);
  return Status();
}

llvm::Expected<NativeProcessProtocol::SoftwareBreakpoint>
NativeProcessProtocol::EnableSoftwareBreakpoint(lldb::addr_t addr,
                                                uint32_t size_hint) {
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));

  auto expected_trap = GetSoftwareBreakpointTrapOpcode(size_hint);
  if (!expected_trap)
    return expected_trap.takeError();

  llvm::SmallVector<uint8_t, 4> saved_opcode_bytes(expected_trap->size(), 0);
  // Save the original opcodes by reading them so we can restore later.
  size_t bytes_read = 0;
  Status error = ReadMemory(addr, saved_opcode_bytes.data(),
                            saved_opcode_bytes.size(), bytes_read);
  if (error.Fail())
    return error.ToError();

  // Ensure we read as many bytes as we expected.
  if (bytes_read != saved_opcode_bytes.size()) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "Failed to read memory while attempting to set breakpoint: attempted "
        "to read {0} bytes but only read {1}.",
        saved_opcode_bytes.size(), bytes_read);
  }

  LLDB_LOG(
      log, "Overwriting bytes at {0:x}: {1:@[x]}", addr,
      llvm::make_range(saved_opcode_bytes.begin(), saved_opcode_bytes.end()));

  // Write a software breakpoint in place of the original opcode.
  size_t bytes_written = 0;
  error = WriteMemory(addr, expected_trap->data(), expected_trap->size(),
                      bytes_written);
  if (error.Fail())
    return error.ToError();

  // Ensure we wrote as many bytes as we expected.
  if (bytes_written != expected_trap->size()) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "Failed write memory while attempting to set "
        "breakpoint: attempted to write {0} bytes but only wrote {1}",
        expected_trap->size(), bytes_written);
  }

  llvm::SmallVector<uint8_t, 4> verify_bp_opcode_bytes(expected_trap->size(),
                                                       0);
  size_t verify_bytes_read = 0;
  error = ReadMemory(addr, verify_bp_opcode_bytes.data(),
                     verify_bp_opcode_bytes.size(), verify_bytes_read);
  if (error.Fail())
    return error.ToError();

  // Ensure we read as many verification bytes as we expected.
  if (verify_bytes_read != verify_bp_opcode_bytes.size()) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "Failed to read memory while "
        "attempting to verify breakpoint: attempted to read {0} bytes "
        "but only read {1}",
        verify_bp_opcode_bytes.size(), verify_bytes_read);
  }

  if (llvm::makeArrayRef(verify_bp_opcode_bytes.data(), verify_bytes_read) !=
      *expected_trap) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "Verification of software breakpoint "
        "writing failed - trap opcodes not successfully read back "
        "after writing when setting breakpoint at {0:x}",
        addr);
  }

  LLDB_LOG(log, "addr = {0:x}: SUCCESS", addr);
  return SoftwareBreakpoint{1, saved_opcode_bytes, *expected_trap};
}

llvm::Expected<llvm::ArrayRef<uint8_t>>
NativeProcessProtocol::GetSoftwareBreakpointTrapOpcode(size_t size_hint) {
  static const uint8_t g_aarch64_opcode[] = {0x00, 0x00, 0x20, 0xd4};
  static const uint8_t g_i386_opcode[] = {0xCC};
  static const uint8_t g_mips64_opcode[] = {0x00, 0x00, 0x00, 0x0d};
  static const uint8_t g_mips64el_opcode[] = {0x0d, 0x00, 0x00, 0x00};
  static const uint8_t g_s390x_opcode[] = {0x00, 0x01};
  static const uint8_t g_ppc64le_opcode[] = {0x08, 0x00, 0xe0, 0x7f}; // trap

  switch (GetArchitecture().GetMachine()) {
  case llvm::Triple::aarch64:
    return llvm::makeArrayRef(g_aarch64_opcode);

  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return llvm::makeArrayRef(g_i386_opcode);

  case llvm::Triple::mips:
  case llvm::Triple::mips64:
    return llvm::makeArrayRef(g_mips64_opcode);

  case llvm::Triple::mipsel:
  case llvm::Triple::mips64el:
    return llvm::makeArrayRef(g_mips64el_opcode);

  case llvm::Triple::systemz:
    return llvm::makeArrayRef(g_s390x_opcode);

  case llvm::Triple::ppc64le:
    return llvm::makeArrayRef(g_ppc64le_opcode);

  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "CPU type not supported!");
  }
}

size_t NativeProcessProtocol::GetSoftwareBreakpointPCOffset() {
  switch (GetArchitecture().GetMachine()) {
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
  case llvm::Triple::systemz:
    // These architectures report increment the PC after breakpoint is hit.
    return cantFail(GetSoftwareBreakpointTrapOpcode(0)).size();

  case llvm::Triple::arm:
  case llvm::Triple::aarch64:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::ppc64le:
    // On these architectures the PC doesn't get updated for breakpoint hits.
    return 0;

  default:
    llvm_unreachable("CPU type not supported!");
  }
}

void NativeProcessProtocol::FixupBreakpointPCAsNeeded(
    NativeThreadProtocol &thread) {
  Log *log = GetLogIfAnyCategoriesSet(LIBLLDB_LOG_BREAKPOINTS);

  Status error;

  // Find out the size of a breakpoint (might depend on where we are in the
  // code).
  NativeRegisterContext &context = thread.GetRegisterContext();

  uint32_t breakpoint_size = GetSoftwareBreakpointPCOffset();
  LLDB_LOG(log, "breakpoint size: {0}", breakpoint_size);
  if (breakpoint_size == 0)
    return;

  // First try probing for a breakpoint at a software breakpoint location: PC -
  // breakpoint size.
  const lldb::addr_t initial_pc_addr = context.GetPCfromBreakpointLocation();
  lldb::addr_t breakpoint_addr = initial_pc_addr;
  // Do not allow breakpoint probe to wrap around.
  if (breakpoint_addr >= breakpoint_size)
    breakpoint_addr -= breakpoint_size;

  if (m_software_breakpoints.count(breakpoint_addr) == 0) {
    // We didn't find one at a software probe location.  Nothing to do.
    LLDB_LOG(log,
             "pid {0} no lldb software breakpoint found at current pc with "
             "adjustment: {1}",
             GetID(), breakpoint_addr);
    return;
  }

  //
  // We have a software breakpoint and need to adjust the PC.
  //

  // Change the program counter.
  LLDB_LOG(log, "pid {0} tid {1}: changing PC from {2:x} to {3:x}", GetID(),
           thread.GetID(), initial_pc_addr, breakpoint_addr);

  error = context.SetPC(breakpoint_addr);
  if (error.Fail()) {
    // This can happen in case the process was killed between the time we read
    // the PC and when we are updating it. There's nothing better to do than to
    // swallow the error.
    LLDB_LOG(log, "pid {0} tid {1}: failed to set PC: {2}", GetID(),
             thread.GetID(), error);
  }
}

Status NativeProcessProtocol::RemoveBreakpoint(lldb::addr_t addr,
                                               bool hardware) {
  if (hardware)
    return RemoveHardwareBreakpoint(addr);
  else
    return RemoveSoftwareBreakpoint(addr);
}

Status NativeProcessProtocol::ReadMemoryWithoutTrap(lldb::addr_t addr,
                                                    void *buf, size_t size,
                                                    size_t &bytes_read) {
  Status error = ReadMemory(addr, buf, size, bytes_read);
  if (error.Fail())
    return error;

  auto data =
      llvm::makeMutableArrayRef(static_cast<uint8_t *>(buf), bytes_read);
  for (const auto &pair : m_software_breakpoints) {
    lldb::addr_t bp_addr = pair.first;
    auto saved_opcodes = makeArrayRef(pair.second.saved_opcodes);

    if (bp_addr + saved_opcodes.size() < addr || addr + bytes_read <= bp_addr)
      continue; // Breapoint not in range, ignore

    if (bp_addr < addr) {
      saved_opcodes = saved_opcodes.drop_front(addr - bp_addr);
      bp_addr = addr;
    }
    auto bp_data = data.drop_front(bp_addr - addr);
    std::copy_n(saved_opcodes.begin(),
                std::min(saved_opcodes.size(), bp_data.size()),
                bp_data.begin());
  }
  return Status();
}

lldb::StateType NativeProcessProtocol::GetState() const {
  std::lock_guard<std::recursive_mutex> guard(m_state_mutex);
  return m_state;
}

void NativeProcessProtocol::SetState(lldb::StateType state,
                                     bool notify_delegates) {
  std::lock_guard<std::recursive_mutex> guard(m_state_mutex);

  if (state == m_state)
    return;

  m_state = state;

  if (StateIsStoppedState(state, false)) {
    ++m_stop_id;

    // Give process a chance to do any stop id bump processing, such as
    // clearing cached data that is invalidated each time the process runs.
    // Note if/when we support some threads running, we'll end up needing to
    // manage this per thread and per process.
    DoStopIDBumped(m_stop_id);
  }

  // Optionally notify delegates of the state change.
  if (notify_delegates)
    SynchronouslyNotifyProcessStateChanged(state);
}

uint32_t NativeProcessProtocol::GetStopID() const {
  std::lock_guard<std::recursive_mutex> guard(m_state_mutex);
  return m_stop_id;
}

void NativeProcessProtocol::DoStopIDBumped(uint32_t /* newBumpId */) {
  // Default implementation does nothing.
}

NativeProcessProtocol::Factory::~Factory() = default;
