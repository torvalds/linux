//===-- StopInfoMachException.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "StopInfoMachException.h"


#if defined(__APPLE__)
// Needed for the EXC_RESOURCE interpretation macros
#include <kern/exc_resource.h>
#endif

#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

const char *StopInfoMachException::GetDescription() {
  if (m_description.empty() && m_value != 0) {
    ExecutionContext exe_ctx(m_thread_wp.lock());
    Target *target = exe_ctx.GetTargetPtr();
    const llvm::Triple::ArchType cpu =
        target ? target->GetArchitecture().GetMachine()
               : llvm::Triple::UnknownArch;

    const char *exc_desc = NULL;
    const char *code_label = "code";
    const char *code_desc = NULL;
    const char *subcode_label = "subcode";
    const char *subcode_desc = NULL;

#if defined(__APPLE__)
    char code_desc_buf[32];
    char subcode_desc_buf[32];
#endif

    switch (m_value) {
    case 1: // EXC_BAD_ACCESS
      exc_desc = "EXC_BAD_ACCESS";
      subcode_label = "address";
      switch (cpu) {
      case llvm::Triple::x86:
      case llvm::Triple::x86_64:
        switch (m_exc_code) {
        case 0xd:
          code_desc = "EXC_I386_GPFLT";
          m_exc_data_count = 1;
          break;
        }
        break;
      case llvm::Triple::arm:
      case llvm::Triple::thumb:
        switch (m_exc_code) {
        case 0x101:
          code_desc = "EXC_ARM_DA_ALIGN";
          break;
        case 0x102:
          code_desc = "EXC_ARM_DA_DEBUG";
          break;
        }
        break;

      case llvm::Triple::ppc:
      case llvm::Triple::ppc64:
        switch (m_exc_code) {
        case 0x101:
          code_desc = "EXC_PPC_VM_PROT_READ";
          break;
        case 0x102:
          code_desc = "EXC_PPC_BADSPACE";
          break;
        case 0x103:
          code_desc = "EXC_PPC_UNALIGNED";
          break;
        }
        break;

      default:
        break;
      }
      break;

    case 2: // EXC_BAD_INSTRUCTION
      exc_desc = "EXC_BAD_INSTRUCTION";
      switch (cpu) {
      case llvm::Triple::x86:
      case llvm::Triple::x86_64:
        if (m_exc_code == 1)
          code_desc = "EXC_I386_INVOP";
        break;

      case llvm::Triple::ppc:
      case llvm::Triple::ppc64:
        switch (m_exc_code) {
        case 1:
          code_desc = "EXC_PPC_INVALID_SYSCALL";
          break;
        case 2:
          code_desc = "EXC_PPC_UNIPL_INST";
          break;
        case 3:
          code_desc = "EXC_PPC_PRIVINST";
          break;
        case 4:
          code_desc = "EXC_PPC_PRIVREG";
          break;
        case 5:
          code_desc = "EXC_PPC_TRACE";
          break;
        case 6:
          code_desc = "EXC_PPC_PERFMON";
          break;
        }
        break;

      case llvm::Triple::arm:
      case llvm::Triple::thumb:
        if (m_exc_code == 1)
          code_desc = "EXC_ARM_UNDEFINED";
        break;

      default:
        break;
      }
      break;

    case 3: // EXC_ARITHMETIC
      exc_desc = "EXC_ARITHMETIC";
      switch (cpu) {
      case llvm::Triple::x86:
      case llvm::Triple::x86_64:
        switch (m_exc_code) {
        case 1:
          code_desc = "EXC_I386_DIV";
          break;
        case 2:
          code_desc = "EXC_I386_INTO";
          break;
        case 3:
          code_desc = "EXC_I386_NOEXT";
          break;
        case 4:
          code_desc = "EXC_I386_EXTOVR";
          break;
        case 5:
          code_desc = "EXC_I386_EXTERR";
          break;
        case 6:
          code_desc = "EXC_I386_EMERR";
          break;
        case 7:
          code_desc = "EXC_I386_BOUND";
          break;
        case 8:
          code_desc = "EXC_I386_SSEEXTERR";
          break;
        }
        break;

      case llvm::Triple::ppc:
      case llvm::Triple::ppc64:
        switch (m_exc_code) {
        case 1:
          code_desc = "EXC_PPC_OVERFLOW";
          break;
        case 2:
          code_desc = "EXC_PPC_ZERO_DIVIDE";
          break;
        case 3:
          code_desc = "EXC_PPC_FLT_INEXACT";
          break;
        case 4:
          code_desc = "EXC_PPC_FLT_ZERO_DIVIDE";
          break;
        case 5:
          code_desc = "EXC_PPC_FLT_UNDERFLOW";
          break;
        case 6:
          code_desc = "EXC_PPC_FLT_OVERFLOW";
          break;
        case 7:
          code_desc = "EXC_PPC_FLT_NOT_A_NUMBER";
          break;
        }
        break;

      default:
        break;
      }
      break;

    case 4: // EXC_EMULATION
      exc_desc = "EXC_EMULATION";
      break;

    case 5: // EXC_SOFTWARE
      exc_desc = "EXC_SOFTWARE";
      if (m_exc_code == 0x10003) {
        subcode_desc = "EXC_SOFT_SIGNAL";
        subcode_label = "signo";
      }
      break;

    case 6: // EXC_BREAKPOINT
    {
      exc_desc = "EXC_BREAKPOINT";
      switch (cpu) {
      case llvm::Triple::x86:
      case llvm::Triple::x86_64:
        switch (m_exc_code) {
        case 1:
          code_desc = "EXC_I386_SGL";
          break;
        case 2:
          code_desc = "EXC_I386_BPT";
          break;
        }
        break;

      case llvm::Triple::ppc:
      case llvm::Triple::ppc64:
        switch (m_exc_code) {
        case 1:
          code_desc = "EXC_PPC_BREAKPOINT";
          break;
        }
        break;

      case llvm::Triple::arm:
      case llvm::Triple::thumb:
        switch (m_exc_code) {
        case 0x101:
          code_desc = "EXC_ARM_DA_ALIGN";
          break;
        case 0x102:
          code_desc = "EXC_ARM_DA_DEBUG";
          break;
        case 1:
          code_desc = "EXC_ARM_BREAKPOINT";
          break;
        // FIXME temporary workaround, exc_code 0 does not really mean
        // EXC_ARM_BREAKPOINT
        case 0:
          code_desc = "EXC_ARM_BREAKPOINT";
          break;
        }
        break;

      default:
        break;
      }
    } break;

    case 7:
      exc_desc = "EXC_SYSCALL";
      break;

    case 8:
      exc_desc = "EXC_MACH_SYSCALL";
      break;

    case 9:
      exc_desc = "EXC_RPC_ALERT";
      break;

    case 10:
      exc_desc = "EXC_CRASH";
      break;
    case 11:
      exc_desc = "EXC_RESOURCE";
#if defined(__APPLE__)
      {
        int resource_type = EXC_RESOURCE_DECODE_RESOURCE_TYPE(m_exc_code);

        code_label = "limit";
        code_desc = code_desc_buf;
        subcode_label = "observed";
        subcode_desc = subcode_desc_buf;

        switch (resource_type) {
        case RESOURCE_TYPE_CPU:
          exc_desc = "EXC_RESOURCE RESOURCE_TYPE_CPU";
          snprintf(code_desc_buf, sizeof(code_desc_buf), "%d%%",
            (int)EXC_RESOURCE_CPUMONITOR_DECODE_PERCENTAGE(m_exc_code));
          snprintf(subcode_desc_buf, sizeof(subcode_desc_buf), "%d%%",
            (int)EXC_RESOURCE_CPUMONITOR_DECODE_PERCENTAGE_OBSERVED(m_exc_subcode));
          break;
        case RESOURCE_TYPE_WAKEUPS:
          exc_desc = "EXC_RESOURCE RESOURCE_TYPE_WAKEUPS";
          snprintf(code_desc_buf, sizeof(code_desc_buf), "%d w/s",
            (int)EXC_RESOURCE_CPUMONITOR_DECODE_WAKEUPS_PERMITTED(m_exc_code));
          snprintf(subcode_desc_buf, sizeof(subcode_desc_buf), "%d w/s",
            (int)EXC_RESOURCE_CPUMONITOR_DECODE_WAKEUPS_OBSERVED(m_exc_subcode));
          break;
        case RESOURCE_TYPE_MEMORY:
          exc_desc = "EXC_RESOURCE RESOURCE_TYPE_MEMORY";
          snprintf(code_desc_buf, sizeof(code_desc_buf), "%d MB",
            (int)EXC_RESOURCE_HWM_DECODE_LIMIT(m_exc_code));
          subcode_desc = nullptr;
          subcode_label = "unused";
          break;
        case RESOURCE_TYPE_IO:
          exc_desc = "EXC_RESOURCE RESOURCE_TYPE_IO";
          snprintf(code_desc_buf, sizeof(code_desc_buf), "%d MB",
            (int)EXC_RESOURCE_IO_DECODE_LIMIT(m_exc_code));
          snprintf(subcode_desc_buf, sizeof(subcode_desc_buf), "%d MB",
            (int)EXC_RESOURCE_IO_OBSERVED(m_exc_subcode));;
          break;
        }
      }
#endif
      break;
    case 12:
      exc_desc = "EXC_GUARD";
      break;
    }

    StreamString strm;

    if (exc_desc)
      strm.PutCString(exc_desc);
    else
      strm.Printf("EXC_??? (%" PRIu64 ")", m_value);

    if (m_exc_data_count >= 1) {
      if (code_desc)
        strm.Printf(" (%s=%s", code_label, code_desc);
      else
        strm.Printf(" (%s=%" PRIu64, code_label, m_exc_code);
    }

    if (m_exc_data_count >= 2) {
      if (subcode_desc)
        strm.Printf(", %s=%s", subcode_label, subcode_desc);
      else
        strm.Printf(", %s=0x%" PRIx64, subcode_label, m_exc_subcode);
    }

    if (m_exc_data_count > 0)
      strm.PutChar(')');

    m_description = strm.GetString();
  }
  return m_description.c_str();
}

StopInfoSP StopInfoMachException::CreateStopReasonWithMachException(
    Thread &thread, uint32_t exc_type, uint32_t exc_data_count,
    uint64_t exc_code, uint64_t exc_sub_code, uint64_t exc_sub_sub_code,
    bool pc_already_adjusted, bool adjust_pc_if_needed) {
  if (exc_type != 0) {
    uint32_t pc_decrement = 0;
    ExecutionContext exe_ctx(thread.shared_from_this());
    Target *target = exe_ctx.GetTargetPtr();
    const llvm::Triple::ArchType cpu =
        target ? target->GetArchitecture().GetMachine()
               : llvm::Triple::UnknownArch;

    switch (exc_type) {
    case 1: // EXC_BAD_ACCESS
      break;

    case 2: // EXC_BAD_INSTRUCTION
      switch (cpu) {
      case llvm::Triple::ppc:
      case llvm::Triple::ppc64:
        switch (exc_code) {
        case 1: // EXC_PPC_INVALID_SYSCALL
        case 2: // EXC_PPC_UNIPL_INST
        case 3: // EXC_PPC_PRIVINST
        case 4: // EXC_PPC_PRIVREG
          break;
        case 5: // EXC_PPC_TRACE
          return StopInfo::CreateStopReasonToTrace(thread);
        case 6: // EXC_PPC_PERFMON
          break;
        }
        break;

      default:
        break;
      }
      break;

    case 3: // EXC_ARITHMETIC
    case 4: // EXC_EMULATION
      break;

    case 5:                    // EXC_SOFTWARE
      if (exc_code == 0x10003) // EXC_SOFT_SIGNAL
      {
        if (exc_sub_code == 5) {
          // On MacOSX, a SIGTRAP can signify that a process has called exec,
          // so we should check with our dynamic loader to verify.
          ProcessSP process_sp(thread.GetProcess());
          if (process_sp) {
            DynamicLoader *dynamic_loader = process_sp->GetDynamicLoader();
            if (dynamic_loader && dynamic_loader->ProcessDidExec()) {
              // The program was re-exec'ed
              return StopInfo::CreateStopReasonWithExec(thread);
            }
            //                        if (!process_did_exec)
            //                        {
            //                            // We have a SIGTRAP, make sure we
            //                            didn't exec by checking
            //                            // for the PC being at
            //                            "_dyld_start"...
            //                            lldb::StackFrameSP frame_sp
            //                            (thread.GetStackFrameAtIndex(0));
            //                            if (frame_sp)
            //                            {
            //                                const Symbol *symbol =
            //                                frame_sp->GetSymbolContext(eSymbolContextSymbol).symbol;
            //                                if (symbol)
            //                                {
            //                                    if (symbol->GetName() ==
            //                                    ConstString("_dyld_start"))
            //                                        process_did_exec = true;
            //                                }
            //                            }
            //                        }
          }
        }
        return StopInfo::CreateStopReasonWithSignal(thread, exc_sub_code);
      }
      break;

    case 6: // EXC_BREAKPOINT
    {
      bool is_actual_breakpoint = false;
      bool is_trace_if_actual_breakpoint_missing = false;
      switch (cpu) {
      case llvm::Triple::x86:
      case llvm::Triple::x86_64:
        if (exc_code == 1) // EXC_I386_SGL
        {
          if (!exc_sub_code) {
            // This looks like a plain trap.
            // Have to check if there is a breakpoint here as well.  When you
            // single-step onto a trap, the single step stops you not to trap.
            // Since we also do that check below, let's just use that logic.
            is_actual_breakpoint = true;
            is_trace_if_actual_breakpoint_missing = true;
          } else {

            // It's a watchpoint, then.
            // The exc_sub_code indicates the data break address.
            lldb::WatchpointSP wp_sp;
            if (target)
              wp_sp = target->GetWatchpointList().FindByAddress(
                  (lldb::addr_t)exc_sub_code);
            if (wp_sp && wp_sp->IsEnabled()) {
              // Debugserver may piggyback the hardware index of the fired
              // watchpoint in the exception data. Set the hardware index if
              // that's the case.
              if (exc_data_count >= 3)
                wp_sp->SetHardwareIndex((uint32_t)exc_sub_sub_code);
              return StopInfo::CreateStopReasonWithWatchpointID(thread,
                                                                wp_sp->GetID());
            }
          }
        } else if (exc_code == 2 || // EXC_I386_BPT
                   exc_code == 3)   // EXC_I386_BPTFLT
        {
          // KDP returns EXC_I386_BPTFLT for trace breakpoints
          if (exc_code == 3)
            is_trace_if_actual_breakpoint_missing = true;

          is_actual_breakpoint = true;
          if (!pc_already_adjusted)
            pc_decrement = 1;
        }
        break;

      case llvm::Triple::ppc:
      case llvm::Triple::ppc64:
        is_actual_breakpoint = exc_code == 1; // EXC_PPC_BREAKPOINT
        break;

      case llvm::Triple::arm:
      case llvm::Triple::thumb:
        if (exc_code == 0x102) // EXC_ARM_DA_DEBUG
        {
          // It's a watchpoint, then, if the exc_sub_code indicates a
          // known/enabled data break address from our watchpoint list.
          lldb::WatchpointSP wp_sp;
          if (target)
            wp_sp = target->GetWatchpointList().FindByAddress(
                (lldb::addr_t)exc_sub_code);
          if (wp_sp && wp_sp->IsEnabled()) {
            // Debugserver may piggyback the hardware index of the fired
            // watchpoint in the exception data. Set the hardware index if
            // that's the case.
            if (exc_data_count >= 3)
              wp_sp->SetHardwareIndex((uint32_t)exc_sub_sub_code);
            return StopInfo::CreateStopReasonWithWatchpointID(thread,
                                                              wp_sp->GetID());
          } else {
            is_actual_breakpoint = true;
            is_trace_if_actual_breakpoint_missing = true;
          }
        } else if (exc_code == 1) // EXC_ARM_BREAKPOINT
        {
          is_actual_breakpoint = true;
          is_trace_if_actual_breakpoint_missing = true;
        } else if (exc_code == 0) // FIXME not EXC_ARM_BREAKPOINT but a kernel
                                  // is currently returning this so accept it
                                  // as indicating a breakpoint until the
                                  // kernel is fixed
        {
          is_actual_breakpoint = true;
          is_trace_if_actual_breakpoint_missing = true;
        }
        break;

      case llvm::Triple::aarch64: {
        if (exc_code == 1 && exc_sub_code == 0) // EXC_ARM_BREAKPOINT
        {
          // This is hit when we single instruction step aka MDSCR_EL1 SS bit 0
          // is set
          is_actual_breakpoint = false;
          is_trace_if_actual_breakpoint_missing = true;
        }
        if (exc_code == 0x102) // EXC_ARM_DA_DEBUG
        {
          // It's a watchpoint, then, if the exc_sub_code indicates a
          // known/enabled data break address from our watchpoint list.
          lldb::WatchpointSP wp_sp;
          if (target)
            wp_sp = target->GetWatchpointList().FindByAddress(
                (lldb::addr_t)exc_sub_code);
          if (wp_sp && wp_sp->IsEnabled()) {
            // Debugserver may piggyback the hardware index of the fired
            // watchpoint in the exception data. Set the hardware index if
            // that's the case.
            if (exc_data_count >= 3)
              wp_sp->SetHardwareIndex((uint32_t)exc_sub_sub_code);
            return StopInfo::CreateStopReasonWithWatchpointID(thread,
                                                              wp_sp->GetID());
          }
          // EXC_ARM_DA_DEBUG seems to be reused for EXC_BREAKPOINT as well as
          // EXC_BAD_ACCESS
          if (thread.GetTemporaryResumeState() == eStateStepping)
            return StopInfo::CreateStopReasonToTrace(thread);
        }
        // It looks like exc_sub_code has the 4 bytes of the instruction that
        // triggered the exception, i.e. our breakpoint opcode
        is_actual_breakpoint = exc_code == 1;
        break;
      }

      default:
        break;
      }

      if (is_actual_breakpoint) {
        RegisterContextSP reg_ctx_sp(thread.GetRegisterContext());
        addr_t pc = reg_ctx_sp->GetPC() - pc_decrement;

        ProcessSP process_sp(thread.CalculateProcess());

        lldb::BreakpointSiteSP bp_site_sp;
        if (process_sp)
          bp_site_sp = process_sp->GetBreakpointSiteList().FindByAddress(pc);
        if (bp_site_sp && bp_site_sp->IsEnabled()) {
          // Update the PC if we were asked to do so, but only do so if we find
          // a breakpoint that we know about cause this could be a trap
          // instruction in the code
          if (pc_decrement > 0 && adjust_pc_if_needed)
            reg_ctx_sp->SetPC(pc);

          // If the breakpoint is for this thread, then we'll report the hit,
          // but if it is for another thread, we can just report no reason.  We
          // don't need to worry about stepping over the breakpoint here, that
          // will be taken care of when the thread resumes and notices that
          // there's a breakpoint under the pc. If we have an operating system
          // plug-in, we might have set a thread specific breakpoint using the
          // operating system thread ID, so we can't make any assumptions about
          // the thread ID so we must always report the breakpoint regardless
          // of the thread.
          if (bp_site_sp->ValidForThisThread(&thread) ||
              thread.GetProcess()->GetOperatingSystem() != NULL)
            return StopInfo::CreateStopReasonWithBreakpointSiteID(
                thread, bp_site_sp->GetID());
          else if (is_trace_if_actual_breakpoint_missing)
            return StopInfo::CreateStopReasonToTrace(thread);
          else
            return StopInfoSP();
        }

        // Don't call this a trace if we weren't single stepping this thread.
        if (is_trace_if_actual_breakpoint_missing &&
            thread.GetTemporaryResumeState() == eStateStepping) {
          return StopInfo::CreateStopReasonToTrace(thread);
        }
      }
    } break;

    case 7:  // EXC_SYSCALL
    case 8:  // EXC_MACH_SYSCALL
    case 9:  // EXC_RPC_ALERT
    case 10: // EXC_CRASH
      break;
    }

    return StopInfoSP(new StopInfoMachException(
        thread, exc_type, exc_data_count, exc_code, exc_sub_code));
  }
  return StopInfoSP();
}
