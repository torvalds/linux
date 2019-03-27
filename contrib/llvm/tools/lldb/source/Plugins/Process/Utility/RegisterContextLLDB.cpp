//===-- RegisterContextLLDB.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Address.h"
#include "lldb/Core/AddressRange.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Value.h"
#include "lldb/Expression/DWARFExpression.h"
#include "lldb/Symbol/ArmUnwindInfo.h"
#include "lldb/Symbol/DWARFCallFrameInfo.h"
#include "lldb/Symbol/FuncUnwinders.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/lldb-private.h"

#include "RegisterContextLLDB.h"

using namespace lldb;
using namespace lldb_private;

static ConstString GetSymbolOrFunctionName(const SymbolContext &sym_ctx) {
  if (sym_ctx.symbol)
    return sym_ctx.symbol->GetName();
  else if (sym_ctx.function)
    return sym_ctx.function->GetName();
  return ConstString();
}

RegisterContextLLDB::RegisterContextLLDB(Thread &thread,
                                         const SharedPtr &next_frame,
                                         SymbolContext &sym_ctx,
                                         uint32_t frame_number,
                                         UnwindLLDB &unwind_lldb)
    : RegisterContext(thread, frame_number), m_thread(thread),
      m_fast_unwind_plan_sp(), m_full_unwind_plan_sp(),
      m_fallback_unwind_plan_sp(), m_all_registers_available(false),
      m_frame_type(-1), m_cfa(LLDB_INVALID_ADDRESS),
      m_afa(LLDB_INVALID_ADDRESS), m_start_pc(),
      m_current_pc(), m_current_offset(0), m_current_offset_backed_up_one(0),
      m_sym_ctx(sym_ctx), m_sym_ctx_valid(false), m_frame_number(frame_number),
      m_registers(), m_parent_unwind(unwind_lldb) {
  m_sym_ctx.Clear(false);
  m_sym_ctx_valid = false;

  if (IsFrameZero()) {
    InitializeZerothFrame();
  } else {
    InitializeNonZerothFrame();
  }

  // This same code exists over in the GetFullUnwindPlanForFrame() but it may
  // not have been executed yet
  if (IsFrameZero() || next_frame->m_frame_type == eTrapHandlerFrame ||
      next_frame->m_frame_type == eDebuggerFrame) {
    m_all_registers_available = true;
  }
}

bool RegisterContextLLDB::IsUnwindPlanValidForCurrentPC(
    lldb::UnwindPlanSP unwind_plan_sp, int &valid_pc_offset) {
  if (!unwind_plan_sp)
    return false;

  // check if m_current_pc is valid
  if (unwind_plan_sp->PlanValidAtAddress(m_current_pc)) {
    // yes - current offset can be used as is
    valid_pc_offset = m_current_offset;
    return true;
  }

  // if m_current_offset <= 0, we've got nothing else to try
  if (m_current_offset <= 0)
    return false;

  // check pc - 1 to see if it's valid
  Address pc_minus_one(m_current_pc);
  pc_minus_one.SetOffset(m_current_pc.GetOffset() - 1);
  if (unwind_plan_sp->PlanValidAtAddress(pc_minus_one)) {
    // *valid_pc_offset = m_current_offset - 1;
    valid_pc_offset = m_current_pc.GetOffset() - 1;
    return true;
  }

  return false;
}

// Initialize a RegisterContextLLDB which is the first frame of a stack -- the
// zeroth frame or currently executing frame.

void RegisterContextLLDB::InitializeZerothFrame() {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
  ExecutionContext exe_ctx(m_thread.shared_from_this());
  RegisterContextSP reg_ctx_sp = m_thread.GetRegisterContext();

  if (reg_ctx_sp.get() == NULL) {
    m_frame_type = eNotAValidFrame;
    UnwindLogMsg("frame does not have a register context");
    return;
  }

  addr_t current_pc = reg_ctx_sp->GetPC();

  if (current_pc == LLDB_INVALID_ADDRESS) {
    m_frame_type = eNotAValidFrame;
    UnwindLogMsg("frame does not have a pc");
    return;
  }

  Process *process = exe_ctx.GetProcessPtr();

  // Let ABIs fixup code addresses to make sure they are valid. In ARM ABIs
  // this will strip bit zero in case we read a PC from memory or from the LR.
  // (which would be a no-op in frame 0 where we get it from the register set,
  // but still a good idea to make the call here for other ABIs that may
  // exist.)
  ABI *abi = process->GetABI().get();
  if (abi)
    current_pc = abi->FixCodeAddress(current_pc);

  // Initialize m_current_pc, an Address object, based on current_pc, an
  // addr_t.
  m_current_pc.SetLoadAddress(current_pc, &process->GetTarget());

  // If we don't have a Module for some reason, we're not going to find
  // symbol/function information - just stick in some reasonable defaults and
  // hope we can unwind past this frame.
  ModuleSP pc_module_sp(m_current_pc.GetModule());
  if (!m_current_pc.IsValid() || !pc_module_sp) {
    UnwindLogMsg("using architectural default unwind method");
  }

  // We require either a symbol or function in the symbols context to be
  // successfully filled in or this context is of no use to us.
  const SymbolContextItem resolve_scope =
      eSymbolContextFunction | eSymbolContextSymbol;
  if (pc_module_sp.get() && (pc_module_sp->ResolveSymbolContextForAddress(
                                 m_current_pc, resolve_scope, m_sym_ctx) &
                             resolve_scope)) {
    m_sym_ctx_valid = true;
  }

  if (m_sym_ctx.symbol) {
    UnwindLogMsg("with pc value of 0x%" PRIx64 ", symbol name is '%s'",
                 current_pc, GetSymbolOrFunctionName(m_sym_ctx).AsCString(""));
  } else if (m_sym_ctx.function) {
    UnwindLogMsg("with pc value of 0x%" PRIx64 ", function name is '%s'",
                 current_pc, GetSymbolOrFunctionName(m_sym_ctx).AsCString(""));
  } else {
    UnwindLogMsg("with pc value of 0x%" PRIx64
                 ", no symbol/function name is known.",
                 current_pc);
  }

  AddressRange addr_range;
  m_sym_ctx.GetAddressRange(resolve_scope, 0, false, addr_range);

  if (IsTrapHandlerSymbol(process, m_sym_ctx)) {
    m_frame_type = eTrapHandlerFrame;
  } else {
    // FIXME:  Detect eDebuggerFrame here.
    m_frame_type = eNormalFrame;
  }

  // If we were able to find a symbol/function, set addr_range to the bounds of
  // that symbol/function. else treat the current pc value as the start_pc and
  // record no offset.
  if (addr_range.GetBaseAddress().IsValid()) {
    m_start_pc = addr_range.GetBaseAddress();
    if (m_current_pc.GetSection() == m_start_pc.GetSection()) {
      m_current_offset = m_current_pc.GetOffset() - m_start_pc.GetOffset();
    } else if (m_current_pc.GetModule() == m_start_pc.GetModule()) {
      // This means that whatever symbol we kicked up isn't really correct ---
      // we should not cross section boundaries ... We really should NULL out
      // the function/symbol in this case unless there is a bad assumption here
      // due to inlined functions?
      m_current_offset =
          m_current_pc.GetFileAddress() - m_start_pc.GetFileAddress();
    }
    m_current_offset_backed_up_one = m_current_offset;
  } else {
    m_start_pc = m_current_pc;
    m_current_offset = -1;
    m_current_offset_backed_up_one = -1;
  }

  // We've set m_frame_type and m_sym_ctx before these calls.

  m_fast_unwind_plan_sp = GetFastUnwindPlanForFrame();
  m_full_unwind_plan_sp = GetFullUnwindPlanForFrame();

  UnwindPlan::RowSP active_row;
  lldb::RegisterKind row_register_kind = eRegisterKindGeneric;
  if (m_full_unwind_plan_sp &&
      m_full_unwind_plan_sp->PlanValidAtAddress(m_current_pc)) {
    active_row =
        m_full_unwind_plan_sp->GetRowForFunctionOffset(m_current_offset);
    row_register_kind = m_full_unwind_plan_sp->GetRegisterKind();
    if (active_row.get() && log) {
      StreamString active_row_strm;
      active_row->Dump(active_row_strm, m_full_unwind_plan_sp.get(), &m_thread,
                       m_start_pc.GetLoadAddress(exe_ctx.GetTargetPtr()));
      UnwindLogMsg("%s", active_row_strm.GetData());
    }
  }

  if (!active_row.get()) {
    UnwindLogMsg("could not find an unwindplan row for this frame's pc");
    m_frame_type = eNotAValidFrame;
    return;
  }

  if (!ReadFrameAddress(row_register_kind, active_row->GetCFAValue(), m_cfa)) {
    // Try the fall back unwind plan since the
    // full unwind plan failed.
    FuncUnwindersSP func_unwinders_sp;
    UnwindPlanSP call_site_unwind_plan;
    bool cfa_status = false;

    if (m_sym_ctx_valid) {
      func_unwinders_sp =
          pc_module_sp->GetObjectFile()
              ->GetUnwindTable()
              .GetFuncUnwindersContainingAddress(m_current_pc, m_sym_ctx);
    }

    if (func_unwinders_sp.get() != nullptr)
      call_site_unwind_plan = func_unwinders_sp->GetUnwindPlanAtCallSite(
          process->GetTarget(), m_current_offset_backed_up_one);

    if (call_site_unwind_plan.get() != nullptr) {
      m_fallback_unwind_plan_sp = call_site_unwind_plan;
      if (TryFallbackUnwindPlan())
        cfa_status = true;
    }
    if (!cfa_status) {
      UnwindLogMsg("could not read CFA value for first frame.");
      m_frame_type = eNotAValidFrame;
      return;
    }
  } else
    ReadFrameAddress(row_register_kind, active_row->GetAFAValue(), m_afa);

  UnwindLogMsg("initialized frame current pc is 0x%" PRIx64 " cfa is 0x%" PRIx64
               " afa is 0x%" PRIx64 " using %s UnwindPlan",
               (uint64_t)m_current_pc.GetLoadAddress(exe_ctx.GetTargetPtr()),
               (uint64_t)m_cfa,
               (uint64_t)m_afa,
               m_full_unwind_plan_sp->GetSourceName().GetCString());
}

// Initialize a RegisterContextLLDB for the non-zeroth frame -- rely on the
// RegisterContextLLDB "below" it to provide things like its current pc value.

void RegisterContextLLDB::InitializeNonZerothFrame() {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
  if (IsFrameZero()) {
    m_frame_type = eNotAValidFrame;
    UnwindLogMsg("non-zeroth frame tests positive for IsFrameZero -- that "
                 "shouldn't happen.");
    return;
  }

  if (!GetNextFrame().get() || !GetNextFrame()->IsValid()) {
    m_frame_type = eNotAValidFrame;
    UnwindLogMsg("Could not get next frame, marking this frame as invalid.");
    return;
  }
  if (!m_thread.GetRegisterContext()) {
    m_frame_type = eNotAValidFrame;
    UnwindLogMsg("Could not get register context for this thread, marking this "
                 "frame as invalid.");
    return;
  }

  addr_t pc;
  if (!ReadGPRValue(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, pc)) {
    UnwindLogMsg("could not get pc value");
    m_frame_type = eNotAValidFrame;
    return;
  }

  ExecutionContext exe_ctx(m_thread.shared_from_this());
  Process *process = exe_ctx.GetProcessPtr();
  // Let ABIs fixup code addresses to make sure they are valid. In ARM ABIs
  // this will strip bit zero in case we read a PC from memory or from the LR.
  ABI *abi = process->GetABI().get();
  if (abi)
    pc = abi->FixCodeAddress(pc);

  if (log) {
    UnwindLogMsg("pc = 0x%" PRIx64, pc);
    addr_t reg_val;
    if (ReadGPRValue(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_FP, reg_val))
      UnwindLogMsg("fp = 0x%" PRIx64, reg_val);
    if (ReadGPRValue(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, reg_val))
      UnwindLogMsg("sp = 0x%" PRIx64, reg_val);
  }

  // A pc of 0x0 means it's the end of the stack crawl unless we're above a trap
  // handler function
  bool above_trap_handler = false;
  if (GetNextFrame().get() && GetNextFrame()->IsValid() &&
      GetNextFrame()->IsTrapHandlerFrame())
    above_trap_handler = true;

  if (pc == 0 || pc == 0x1) {
    if (!above_trap_handler) {
      m_frame_type = eNotAValidFrame;
      UnwindLogMsg("this frame has a pc of 0x0");
      return;
    }
  }

  const bool allow_section_end = true;
  m_current_pc.SetLoadAddress(pc, &process->GetTarget(), allow_section_end);

  // If we don't have a Module for some reason, we're not going to find
  // symbol/function information - just stick in some reasonable defaults and
  // hope we can unwind past this frame.
  ModuleSP pc_module_sp(m_current_pc.GetModule());
  if (!m_current_pc.IsValid() || !pc_module_sp) {
    UnwindLogMsg("using architectural default unwind method");

    // Test the pc value to see if we know it's in an unmapped/non-executable
    // region of memory.
    uint32_t permissions;
    if (process->GetLoadAddressPermissions(pc, permissions) &&
        (permissions & ePermissionsExecutable) == 0) {
      // If this is the second frame off the stack, we may have unwound the
      // first frame incorrectly.  But using the architecture default unwind
      // plan may get us back on track -- albeit possibly skipping a real
      // frame.  Give this frame a clearly-invalid pc and see if we can get any
      // further.
      if (GetNextFrame().get() && GetNextFrame()->IsValid() &&
          GetNextFrame()->IsFrameZero()) {
        UnwindLogMsg("had a pc of 0x%" PRIx64 " which is not in executable "
                                              "memory but on frame 1 -- "
                                              "allowing it once.",
                     (uint64_t)pc);
        m_frame_type = eSkipFrame;
      } else {
        // anywhere other than the second frame, a non-executable pc means
        // we're off in the weeds -- stop now.
        m_frame_type = eNotAValidFrame;
        UnwindLogMsg("pc is in a non-executable section of memory and this "
                     "isn't the 2nd frame in the stack walk.");
        return;
      }
    }

    if (abi) {
      m_fast_unwind_plan_sp.reset();
      m_full_unwind_plan_sp.reset(new UnwindPlan(lldb::eRegisterKindGeneric));
      abi->CreateDefaultUnwindPlan(*m_full_unwind_plan_sp);
      if (m_frame_type != eSkipFrame) // don't override eSkipFrame
      {
        m_frame_type = eNormalFrame;
      }
      m_all_registers_available = false;
      m_current_offset = -1;
      m_current_offset_backed_up_one = -1;
      RegisterKind row_register_kind = m_full_unwind_plan_sp->GetRegisterKind();
      UnwindPlan::RowSP row = m_full_unwind_plan_sp->GetRowForFunctionOffset(0);
      if (row.get()) {
        if (!ReadFrameAddress(row_register_kind, row->GetCFAValue(), m_cfa)) {
          UnwindLogMsg("failed to get cfa value");
          if (m_frame_type != eSkipFrame) // don't override eSkipFrame
          {
            m_frame_type = eNotAValidFrame;
          }
          return;
        }

        ReadFrameAddress(row_register_kind, row->GetAFAValue(), m_afa);

        // A couple of sanity checks..
        if (m_cfa == LLDB_INVALID_ADDRESS || m_cfa == 0 || m_cfa == 1) {
          UnwindLogMsg("could not find a valid cfa address");
          m_frame_type = eNotAValidFrame;
          return;
        }

        // m_cfa should point into the stack memory; if we can query memory
        // region permissions, see if the memory is allocated & readable.
        if (process->GetLoadAddressPermissions(m_cfa, permissions) &&
            (permissions & ePermissionsReadable) == 0) {
          m_frame_type = eNotAValidFrame;
          UnwindLogMsg(
              "the CFA points to a region of memory that is not readable");
          return;
        }
      } else {
        UnwindLogMsg("could not find a row for function offset zero");
        m_frame_type = eNotAValidFrame;
        return;
      }

      if (CheckIfLoopingStack()) {
        TryFallbackUnwindPlan();
        if (CheckIfLoopingStack()) {
          UnwindLogMsg("same CFA address as next frame, assuming the unwind is "
                       "looping - stopping");
          m_frame_type = eNotAValidFrame;
          return;
        }
      }

      UnwindLogMsg("initialized frame cfa is 0x%" PRIx64 " afa is 0x%" PRIx64,
                   (uint64_t)m_cfa, (uint64_t)m_afa);
      return;
    }
    m_frame_type = eNotAValidFrame;
    UnwindLogMsg("could not find any symbol for this pc, or a default unwind "
                 "plan, to continue unwind.");
    return;
  }

  bool resolve_tail_call_address = false; // m_current_pc can be one past the
                                          // address range of the function...
  // If the saved pc does not point to a function/symbol because it is beyond
  // the bounds of the correct function and there's no symbol there, we do
  // *not* want ResolveSymbolContextForAddress to back up the pc by 1, because
  // then we might not find the correct unwind information later. Instead, let
  // ResolveSymbolContextForAddress fail, and handle the case via
  // decr_pc_and_recompute_addr_range below.
  const SymbolContextItem resolve_scope =
      eSymbolContextFunction | eSymbolContextSymbol;
  uint32_t resolved_scope = pc_module_sp->ResolveSymbolContextForAddress(
      m_current_pc, resolve_scope, m_sym_ctx, resolve_tail_call_address);

  // We require either a symbol or function in the symbols context to be
  // successfully filled in or this context is of no use to us.
  if (resolve_scope & resolved_scope) {
    m_sym_ctx_valid = true;
  }

  if (m_sym_ctx.symbol) {
    UnwindLogMsg("with pc value of 0x%" PRIx64 ", symbol name is '%s'", pc,
                 GetSymbolOrFunctionName(m_sym_ctx).AsCString(""));
  } else if (m_sym_ctx.function) {
    UnwindLogMsg("with pc value of 0x%" PRIx64 ", function name is '%s'", pc,
                 GetSymbolOrFunctionName(m_sym_ctx).AsCString(""));
  } else {
    UnwindLogMsg("with pc value of 0x%" PRIx64
                 ", no symbol/function name is known.",
                 pc);
  }

  AddressRange addr_range;
  if (!m_sym_ctx.GetAddressRange(resolve_scope, 0, false, addr_range)) {
    m_sym_ctx_valid = false;
  }

  bool decr_pc_and_recompute_addr_range = false;

  // If the symbol lookup failed...
  if (!m_sym_ctx_valid)
    decr_pc_and_recompute_addr_range = true;

  // Or if we're in the middle of the stack (and not "above" an asynchronous
  // event like sigtramp), and our "current" pc is the start of a function...
  if (GetNextFrame()->m_frame_type != eTrapHandlerFrame &&
      GetNextFrame()->m_frame_type != eDebuggerFrame &&
      (!m_sym_ctx_valid ||
       (addr_range.GetBaseAddress().IsValid() &&
        addr_range.GetBaseAddress().GetSection() == m_current_pc.GetSection() &&
        addr_range.GetBaseAddress().GetOffset() == m_current_pc.GetOffset()))) {
    decr_pc_and_recompute_addr_range = true;
  }

  // We need to back up the pc by 1 byte and re-search for the Symbol to handle
  // the case where the "saved pc" value is pointing to the next function, e.g.
  // if a function ends with a CALL instruction.
  // FIXME this may need to be an architectural-dependent behavior; if so we'll
  // need to add a member function
  // to the ABI plugin and consult that.
  if (decr_pc_and_recompute_addr_range) {
    UnwindLogMsg("Backing up the pc value of 0x%" PRIx64
                 " by 1 and re-doing symbol lookup; old symbol was %s",
                 pc, GetSymbolOrFunctionName(m_sym_ctx).AsCString(""));
    Address temporary_pc;
    temporary_pc.SetLoadAddress(pc - 1, &process->GetTarget());
    m_sym_ctx.Clear(false);
    m_sym_ctx_valid = false;
    SymbolContextItem resolve_scope =
        eSymbolContextFunction | eSymbolContextSymbol;

    ModuleSP temporary_module_sp = temporary_pc.GetModule();
    if (temporary_module_sp &&
        temporary_module_sp->ResolveSymbolContextForAddress(
            temporary_pc, resolve_scope, m_sym_ctx) &
            resolve_scope) {
      if (m_sym_ctx.GetAddressRange(resolve_scope, 0, false, addr_range))
        m_sym_ctx_valid = true;
    }
    UnwindLogMsg("Symbol is now %s",
                 GetSymbolOrFunctionName(m_sym_ctx).AsCString(""));
  }

  // If we were able to find a symbol/function, set addr_range_ptr to the
  // bounds of that symbol/function. else treat the current pc value as the
  // start_pc and record no offset.
  if (addr_range.GetBaseAddress().IsValid()) {
    m_start_pc = addr_range.GetBaseAddress();
    m_current_offset = pc - m_start_pc.GetLoadAddress(&process->GetTarget());
    m_current_offset_backed_up_one = m_current_offset;
    if (decr_pc_and_recompute_addr_range &&
        m_current_offset_backed_up_one > 0) {
      m_current_offset_backed_up_one--;
      if (m_sym_ctx_valid) {
        m_current_pc.SetLoadAddress(pc - 1, &process->GetTarget());
      }
    }
  } else {
    m_start_pc = m_current_pc;
    m_current_offset = -1;
    m_current_offset_backed_up_one = -1;
  }

  if (IsTrapHandlerSymbol(process, m_sym_ctx)) {
    m_frame_type = eTrapHandlerFrame;
  } else {
    // FIXME:  Detect eDebuggerFrame here.
    if (m_frame_type != eSkipFrame) // don't override eSkipFrame
    {
      m_frame_type = eNormalFrame;
    }
  }

  // We've set m_frame_type and m_sym_ctx before this call.
  m_fast_unwind_plan_sp = GetFastUnwindPlanForFrame();

  UnwindPlan::RowSP active_row;
  RegisterKind row_register_kind = eRegisterKindGeneric;

  // Try to get by with just the fast UnwindPlan if possible - the full
  // UnwindPlan may be expensive to get (e.g. if we have to parse the entire
  // eh_frame section of an ObjectFile for the first time.)

  if (m_fast_unwind_plan_sp &&
      m_fast_unwind_plan_sp->PlanValidAtAddress(m_current_pc)) {
    active_row =
        m_fast_unwind_plan_sp->GetRowForFunctionOffset(m_current_offset);
    row_register_kind = m_fast_unwind_plan_sp->GetRegisterKind();
    if (active_row.get() && log) {
      StreamString active_row_strm;
      active_row->Dump(active_row_strm, m_fast_unwind_plan_sp.get(), &m_thread,
                       m_start_pc.GetLoadAddress(exe_ctx.GetTargetPtr()));
      UnwindLogMsg("active row: %s", active_row_strm.GetData());
    }
  } else {
    m_full_unwind_plan_sp = GetFullUnwindPlanForFrame();
    int valid_offset = -1;
    if (IsUnwindPlanValidForCurrentPC(m_full_unwind_plan_sp, valid_offset)) {
      active_row = m_full_unwind_plan_sp->GetRowForFunctionOffset(valid_offset);
      row_register_kind = m_full_unwind_plan_sp->GetRegisterKind();
      if (active_row.get() && log) {
        StreamString active_row_strm;
        active_row->Dump(active_row_strm, m_full_unwind_plan_sp.get(),
                         &m_thread,
                         m_start_pc.GetLoadAddress(exe_ctx.GetTargetPtr()));
        UnwindLogMsg("active row: %s", active_row_strm.GetData());
      }
    }
  }

  if (!active_row.get()) {
    m_frame_type = eNotAValidFrame;
    UnwindLogMsg("could not find unwind row for this pc");
    return;
  }

  if (!ReadFrameAddress(row_register_kind, active_row->GetCFAValue(), m_cfa)) {
    UnwindLogMsg("failed to get cfa");
    m_frame_type = eNotAValidFrame;
    return;
  }

  ReadFrameAddress(row_register_kind, active_row->GetAFAValue(), m_afa);

  UnwindLogMsg("m_cfa = 0x%" PRIx64 " m_afa = 0x%" PRIx64, m_cfa, m_afa);

  if (CheckIfLoopingStack()) {
    TryFallbackUnwindPlan();
    if (CheckIfLoopingStack()) {
      UnwindLogMsg("same CFA address as next frame, assuming the unwind is "
                   "looping - stopping");
      m_frame_type = eNotAValidFrame;
      return;
    }
  }

  UnwindLogMsg("initialized frame current pc is 0x%" PRIx64
               " cfa is 0x%" PRIx64 " afa is 0x%" PRIx64,
               (uint64_t)m_current_pc.GetLoadAddress(exe_ctx.GetTargetPtr()),
               (uint64_t)m_cfa,
               (uint64_t)m_afa);
}

bool RegisterContextLLDB::CheckIfLoopingStack() {
  // If we have a bad stack setup, we can get the same CFA value multiple times
  // -- or even more devious, we can actually oscillate between two CFA values.
  // Detect that here and break out to avoid a possible infinite loop in lldb
  // trying to unwind the stack. To detect when we have the same CFA value
  // multiple times, we compare the
  // CFA of the current
  // frame with the 2nd next frame because in some specail case (e.g. signal
  // hanlders, hand written assembly without ABI compiance) we can have 2
  // frames with the same
  // CFA (in theory we
  // can have arbitrary number of frames with the same CFA, but more then 2 is
  // very very unlikely)

  RegisterContextLLDB::SharedPtr next_frame = GetNextFrame();
  if (next_frame) {
    RegisterContextLLDB::SharedPtr next_next_frame = next_frame->GetNextFrame();
    addr_t next_next_frame_cfa = LLDB_INVALID_ADDRESS;
    if (next_next_frame && next_next_frame->GetCFA(next_next_frame_cfa)) {
      if (next_next_frame_cfa == m_cfa) {
        // We have a loop in the stack unwind
        return true;
      }
    }
  }
  return false;
}

bool RegisterContextLLDB::IsFrameZero() const { return m_frame_number == 0; }

// Find a fast unwind plan for this frame, if possible.
//
// On entry to this method,
//
//   1. m_frame_type should already be set to eTrapHandlerFrame/eDebuggerFrame
//   if either of those are correct,
//   2. m_sym_ctx should already be filled in, and
//   3. m_current_pc should have the current pc value for this frame
//   4. m_current_offset_backed_up_one should have the current byte offset into
//   the function, maybe backed up by 1, -1 if unknown

UnwindPlanSP RegisterContextLLDB::GetFastUnwindPlanForFrame() {
  UnwindPlanSP unwind_plan_sp;
  ModuleSP pc_module_sp(m_current_pc.GetModule());

  if (!m_current_pc.IsValid() || !pc_module_sp ||
      pc_module_sp->GetObjectFile() == NULL)
    return unwind_plan_sp;

  if (IsFrameZero())
    return unwind_plan_sp;

  FuncUnwindersSP func_unwinders_sp(
      pc_module_sp->GetObjectFile()
          ->GetUnwindTable()
          .GetFuncUnwindersContainingAddress(m_current_pc, m_sym_ctx));
  if (!func_unwinders_sp)
    return unwind_plan_sp;

  // If we're in _sigtramp(), unwinding past this frame requires special
  // knowledge.
  if (m_frame_type == eTrapHandlerFrame || m_frame_type == eDebuggerFrame)
    return unwind_plan_sp;

  unwind_plan_sp = func_unwinders_sp->GetUnwindPlanFastUnwind(
      *m_thread.CalculateTarget(), m_thread);
  if (unwind_plan_sp) {
    if (unwind_plan_sp->PlanValidAtAddress(m_current_pc)) {
      Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
      if (log && log->GetVerbose()) {
        if (m_fast_unwind_plan_sp)
          UnwindLogMsgVerbose("frame, and has a fast UnwindPlan");
        else
          UnwindLogMsgVerbose("frame");
      }
      m_frame_type = eNormalFrame;
      return unwind_plan_sp;
    } else {
      unwind_plan_sp.reset();
    }
  }
  return unwind_plan_sp;
}

// On entry to this method,
//
//   1. m_frame_type should already be set to eTrapHandlerFrame/eDebuggerFrame
//   if either of those are correct,
//   2. m_sym_ctx should already be filled in, and
//   3. m_current_pc should have the current pc value for this frame
//   4. m_current_offset_backed_up_one should have the current byte offset into
//   the function, maybe backed up by 1, -1 if unknown

UnwindPlanSP RegisterContextLLDB::GetFullUnwindPlanForFrame() {
  UnwindPlanSP unwind_plan_sp;
  UnwindPlanSP arch_default_unwind_plan_sp;
  ExecutionContext exe_ctx(m_thread.shared_from_this());
  Process *process = exe_ctx.GetProcessPtr();
  ABI *abi = process ? process->GetABI().get() : NULL;
  if (abi) {
    arch_default_unwind_plan_sp.reset(
        new UnwindPlan(lldb::eRegisterKindGeneric));
    abi->CreateDefaultUnwindPlan(*arch_default_unwind_plan_sp);
  } else {
    UnwindLogMsg(
        "unable to get architectural default UnwindPlan from ABI plugin");
  }

  bool behaves_like_zeroth_frame = false;
  if (IsFrameZero() || GetNextFrame()->m_frame_type == eTrapHandlerFrame ||
      GetNextFrame()->m_frame_type == eDebuggerFrame) {
    behaves_like_zeroth_frame = true;
    // If this frame behaves like a 0th frame (currently executing or
    // interrupted asynchronously), all registers can be retrieved.
    m_all_registers_available = true;
  }

  // If we've done a jmp 0x0 / bl 0x0 (called through a null function pointer)
  // so the pc is 0x0 in the zeroth frame, we need to use the "unwind at first
  // instruction" arch default UnwindPlan Also, if this Process can report on
  // memory region attributes, any non-executable region means we jumped
  // through a bad function pointer - handle the same way as 0x0. Note, if we
  // have a symbol context & a symbol, we don't want to follow this code path.
  // This is for jumping to memory regions without any information available.

  if ((!m_sym_ctx_valid ||
       (m_sym_ctx.function == NULL && m_sym_ctx.symbol == NULL)) &&
      behaves_like_zeroth_frame && m_current_pc.IsValid()) {
    uint32_t permissions;
    addr_t current_pc_addr =
        m_current_pc.GetLoadAddress(exe_ctx.GetTargetPtr());
    if (current_pc_addr == 0 ||
        (process &&
         process->GetLoadAddressPermissions(current_pc_addr, permissions) &&
         (permissions & ePermissionsExecutable) == 0)) {
      if (abi) {
        unwind_plan_sp.reset(new UnwindPlan(lldb::eRegisterKindGeneric));
        abi->CreateFunctionEntryUnwindPlan(*unwind_plan_sp);
        m_frame_type = eNormalFrame;
        return unwind_plan_sp;
      }
    }
  }

  // No Module for the current pc, try using the architecture default unwind.
  ModuleSP pc_module_sp(m_current_pc.GetModule());
  if (!m_current_pc.IsValid() || !pc_module_sp ||
      pc_module_sp->GetObjectFile() == NULL) {
    m_frame_type = eNormalFrame;
    return arch_default_unwind_plan_sp;
  }

  FuncUnwindersSP func_unwinders_sp;
  if (m_sym_ctx_valid) {
    func_unwinders_sp =
        pc_module_sp->GetObjectFile()
            ->GetUnwindTable()
            .GetFuncUnwindersContainingAddress(m_current_pc, m_sym_ctx);
  }

  // No FuncUnwinders available for this pc (stripped function symbols, lldb
  // could not augment its function table with another source, like
  // LC_FUNCTION_STARTS or eh_frame in ObjectFileMachO). See if eh_frame or the
  // .ARM.exidx tables have unwind information for this address, else fall back
  // to the architectural default unwind.
  if (!func_unwinders_sp) {
    m_frame_type = eNormalFrame;

    if (!pc_module_sp || !pc_module_sp->GetObjectFile() ||
        !m_current_pc.IsValid())
      return arch_default_unwind_plan_sp;

    // Even with -fomit-frame-pointer, we can try eh_frame to get back on
    // track.
    DWARFCallFrameInfo *eh_frame =
        pc_module_sp->GetObjectFile()->GetUnwindTable().GetEHFrameInfo();
    if (eh_frame) {
      unwind_plan_sp.reset(new UnwindPlan(lldb::eRegisterKindGeneric));
      if (eh_frame->GetUnwindPlan(m_current_pc, *unwind_plan_sp))
        return unwind_plan_sp;
      else
        unwind_plan_sp.reset();
    }

    ArmUnwindInfo *arm_exidx =
        pc_module_sp->GetObjectFile()->GetUnwindTable().GetArmUnwindInfo();
    if (arm_exidx) {
      unwind_plan_sp.reset(new UnwindPlan(lldb::eRegisterKindGeneric));
      if (arm_exidx->GetUnwindPlan(exe_ctx.GetTargetRef(), m_current_pc,
                                   *unwind_plan_sp))
        return unwind_plan_sp;
      else
        unwind_plan_sp.reset();
    }

    return arch_default_unwind_plan_sp;
  }

  // If we're in _sigtramp(), unwinding past this frame requires special
  // knowledge.  On Mac OS X this knowledge is properly encoded in the eh_frame
  // section, so prefer that if available. On other platforms we may need to
  // provide a platform-specific UnwindPlan which encodes the details of how to
  // unwind out of sigtramp.
  if (m_frame_type == eTrapHandlerFrame && process) {
    m_fast_unwind_plan_sp.reset();
    unwind_plan_sp = func_unwinders_sp->GetEHFrameUnwindPlan(
        process->GetTarget(), m_current_offset_backed_up_one);
    if (unwind_plan_sp && unwind_plan_sp->PlanValidAtAddress(m_current_pc) &&
        unwind_plan_sp->GetSourcedFromCompiler() == eLazyBoolYes) {
      return unwind_plan_sp;
    }
  }

  // Ask the DynamicLoader if the eh_frame CFI should be trusted in this frame
  // even when it's frame zero This comes up if we have hand-written functions
  // in a Module and hand-written eh_frame.  The assembly instruction
  // inspection may fail and the eh_frame CFI were probably written with some
  // care to do the right thing.  It'd be nice if there was a way to ask the
  // eh_frame directly if it is asynchronous (can be trusted at every
  // instruction point) or synchronous (the normal case - only at call sites).
  // But there is not.
  if (process && process->GetDynamicLoader() &&
      process->GetDynamicLoader()->AlwaysRelyOnEHUnwindInfo(m_sym_ctx)) {
    // We must specifically call the GetEHFrameUnwindPlan() method here --
    // normally we would call GetUnwindPlanAtCallSite() -- because CallSite may
    // return an unwind plan sourced from either eh_frame (that's what we
    // intend) or compact unwind (this won't work)
    unwind_plan_sp = func_unwinders_sp->GetEHFrameUnwindPlan(
        process->GetTarget(), m_current_offset_backed_up_one);
    if (unwind_plan_sp && unwind_plan_sp->PlanValidAtAddress(m_current_pc)) {
      UnwindLogMsgVerbose("frame uses %s for full UnwindPlan because the "
                          "DynamicLoader suggested we prefer it",
                          unwind_plan_sp->GetSourceName().GetCString());
      return unwind_plan_sp;
    }
  }

  // Typically the NonCallSite UnwindPlan is the unwind created by inspecting
  // the assembly language instructions
  if (behaves_like_zeroth_frame && process) {
    unwind_plan_sp = func_unwinders_sp->GetUnwindPlanAtNonCallSite(
        process->GetTarget(), m_thread, m_current_offset_backed_up_one);
    if (unwind_plan_sp && unwind_plan_sp->PlanValidAtAddress(m_current_pc)) {
      if (unwind_plan_sp->GetSourcedFromCompiler() == eLazyBoolNo) {
        // We probably have an UnwindPlan created by inspecting assembly
        // instructions. The assembly profilers work really well with compiler-
        // generated functions but hand- written assembly can be problematic.
        // We set the eh_frame based unwind plan as our fallback unwind plan if
        // instruction emulation doesn't work out even for non call sites if it
        // is available and use the architecture default unwind plan if it is
        // not available. The eh_frame unwind plan is more reliable even on non
        // call sites then the architecture default plan and for hand written
        // assembly code it is often written in a way that it valid at all
        // location what helps in the most common cases when the instruction
        // emulation fails.
        UnwindPlanSP call_site_unwind_plan =
            func_unwinders_sp->GetUnwindPlanAtCallSite(
                process->GetTarget(), m_current_offset_backed_up_one);
        if (call_site_unwind_plan &&
            call_site_unwind_plan.get() != unwind_plan_sp.get() &&
            call_site_unwind_plan->GetSourceName() !=
                unwind_plan_sp->GetSourceName()) {
          m_fallback_unwind_plan_sp = call_site_unwind_plan;
        } else {
          m_fallback_unwind_plan_sp = arch_default_unwind_plan_sp;
        }
      }
      UnwindLogMsgVerbose("frame uses %s for full UnwindPlan",
                          unwind_plan_sp->GetSourceName().GetCString());
      return unwind_plan_sp;
    }
  }

  // Typically this is unwind info from an eh_frame section intended for
  // exception handling; only valid at call sites
  if (process) {
    unwind_plan_sp = func_unwinders_sp->GetUnwindPlanAtCallSite(
        process->GetTarget(), m_current_offset_backed_up_one);
  }
  int valid_offset = -1;
  if (IsUnwindPlanValidForCurrentPC(unwind_plan_sp, valid_offset)) {
    UnwindLogMsgVerbose("frame uses %s for full UnwindPlan",
                        unwind_plan_sp->GetSourceName().GetCString());
    return unwind_plan_sp;
  }

  // We'd prefer to use an UnwindPlan intended for call sites when we're at a
  // call site but if we've struck out on that, fall back to using the non-
  // call-site assembly inspection UnwindPlan if possible.
  if (process) {
    unwind_plan_sp = func_unwinders_sp->GetUnwindPlanAtNonCallSite(
        process->GetTarget(), m_thread, m_current_offset_backed_up_one);
  }
  if (unwind_plan_sp &&
      unwind_plan_sp->GetSourcedFromCompiler() == eLazyBoolNo) {
    // We probably have an UnwindPlan created by inspecting assembly
    // instructions. The assembly profilers work really well with compiler-
    // generated functions but hand- written assembly can be problematic. We
    // set the eh_frame based unwind plan as our fallback unwind plan if
    // instruction emulation doesn't work out even for non call sites if it is
    // available and use the architecture default unwind plan if it is not
    // available. The eh_frame unwind plan is more reliable even on non call
    // sites then the architecture default plan and for hand written assembly
    // code it is often written in a way that it valid at all location what
    // helps in the most common cases when the instruction emulation fails.
    UnwindPlanSP call_site_unwind_plan =
        func_unwinders_sp->GetUnwindPlanAtCallSite(
            process->GetTarget(), m_current_offset_backed_up_one);
    if (call_site_unwind_plan &&
        call_site_unwind_plan.get() != unwind_plan_sp.get() &&
        call_site_unwind_plan->GetSourceName() !=
            unwind_plan_sp->GetSourceName()) {
      m_fallback_unwind_plan_sp = call_site_unwind_plan;
    } else {
      m_fallback_unwind_plan_sp = arch_default_unwind_plan_sp;
    }
  }

  if (IsUnwindPlanValidForCurrentPC(unwind_plan_sp, valid_offset)) {
    UnwindLogMsgVerbose("frame uses %s for full UnwindPlan",
                        unwind_plan_sp->GetSourceName().GetCString());
    return unwind_plan_sp;
  }

  // If we're on the first instruction of a function, and we have an
  // architectural default UnwindPlan for the initial instruction of a
  // function, use that.
  if (m_current_offset_backed_up_one == 0) {
    unwind_plan_sp =
        func_unwinders_sp->GetUnwindPlanArchitectureDefaultAtFunctionEntry(
            m_thread);
    if (unwind_plan_sp) {
      UnwindLogMsgVerbose("frame uses %s for full UnwindPlan",
                          unwind_plan_sp->GetSourceName().GetCString());
      return unwind_plan_sp;
    }
  }

  // If nothing else, use the architectural default UnwindPlan and hope that
  // does the job.
  if (arch_default_unwind_plan_sp)
    UnwindLogMsgVerbose(
        "frame uses %s for full UnwindPlan",
        arch_default_unwind_plan_sp->GetSourceName().GetCString());
  else
    UnwindLogMsg(
        "Unable to find any UnwindPlan for full unwind of this frame.");

  return arch_default_unwind_plan_sp;
}

void RegisterContextLLDB::InvalidateAllRegisters() {
  m_frame_type = eNotAValidFrame;
}

size_t RegisterContextLLDB::GetRegisterCount() {
  return m_thread.GetRegisterContext()->GetRegisterCount();
}

const RegisterInfo *RegisterContextLLDB::GetRegisterInfoAtIndex(size_t reg) {
  return m_thread.GetRegisterContext()->GetRegisterInfoAtIndex(reg);
}

size_t RegisterContextLLDB::GetRegisterSetCount() {
  return m_thread.GetRegisterContext()->GetRegisterSetCount();
}

const RegisterSet *RegisterContextLLDB::GetRegisterSet(size_t reg_set) {
  return m_thread.GetRegisterContext()->GetRegisterSet(reg_set);
}

uint32_t RegisterContextLLDB::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t num) {
  return m_thread.GetRegisterContext()->ConvertRegisterKindToRegisterNumber(
      kind, num);
}

bool RegisterContextLLDB::ReadRegisterValueFromRegisterLocation(
    lldb_private::UnwindLLDB::RegisterLocation regloc,
    const RegisterInfo *reg_info, RegisterValue &value) {
  if (!IsValid())
    return false;
  bool success = false;

  switch (regloc.type) {
  case UnwindLLDB::RegisterLocation::eRegisterInLiveRegisterContext: {
    const RegisterInfo *other_reg_info =
        GetRegisterInfoAtIndex(regloc.location.register_number);

    if (!other_reg_info)
      return false;

    success =
        m_thread.GetRegisterContext()->ReadRegister(other_reg_info, value);
  } break;
  case UnwindLLDB::RegisterLocation::eRegisterInRegister: {
    const RegisterInfo *other_reg_info =
        GetRegisterInfoAtIndex(regloc.location.register_number);

    if (!other_reg_info)
      return false;

    if (IsFrameZero()) {
      success =
          m_thread.GetRegisterContext()->ReadRegister(other_reg_info, value);
    } else {
      success = GetNextFrame()->ReadRegister(other_reg_info, value);
    }
  } break;
  case UnwindLLDB::RegisterLocation::eRegisterValueInferred:
    success =
        value.SetUInt(regloc.location.inferred_value, reg_info->byte_size);
    break;

  case UnwindLLDB::RegisterLocation::eRegisterNotSaved:
    break;
  case UnwindLLDB::RegisterLocation::eRegisterSavedAtHostMemoryLocation:
    llvm_unreachable("FIXME debugger inferior function call unwind");
  case UnwindLLDB::RegisterLocation::eRegisterSavedAtMemoryLocation: {
    Status error(ReadRegisterValueFromMemory(
        reg_info, regloc.location.target_memory_location, reg_info->byte_size,
        value));
    success = error.Success();
  } break;
  default:
    llvm_unreachable("Unknown RegisterLocation type.");
  }
  return success;
}

bool RegisterContextLLDB::WriteRegisterValueToRegisterLocation(
    lldb_private::UnwindLLDB::RegisterLocation regloc,
    const RegisterInfo *reg_info, const RegisterValue &value) {
  if (!IsValid())
    return false;

  bool success = false;

  switch (regloc.type) {
  case UnwindLLDB::RegisterLocation::eRegisterInLiveRegisterContext: {
    const RegisterInfo *other_reg_info =
        GetRegisterInfoAtIndex(regloc.location.register_number);
    success =
        m_thread.GetRegisterContext()->WriteRegister(other_reg_info, value);
  } break;
  case UnwindLLDB::RegisterLocation::eRegisterInRegister: {
    const RegisterInfo *other_reg_info =
        GetRegisterInfoAtIndex(regloc.location.register_number);
    if (IsFrameZero()) {
      success =
          m_thread.GetRegisterContext()->WriteRegister(other_reg_info, value);
    } else {
      success = GetNextFrame()->WriteRegister(other_reg_info, value);
    }
  } break;
  case UnwindLLDB::RegisterLocation::eRegisterValueInferred:
  case UnwindLLDB::RegisterLocation::eRegisterNotSaved:
    break;
  case UnwindLLDB::RegisterLocation::eRegisterSavedAtHostMemoryLocation:
    llvm_unreachable("FIXME debugger inferior function call unwind");
  case UnwindLLDB::RegisterLocation::eRegisterSavedAtMemoryLocation: {
    Status error(WriteRegisterValueToMemory(
        reg_info, regloc.location.target_memory_location, reg_info->byte_size,
        value));
    success = error.Success();
  } break;
  default:
    llvm_unreachable("Unknown RegisterLocation type.");
  }
  return success;
}

bool RegisterContextLLDB::IsValid() const {
  return m_frame_type != eNotAValidFrame;
}

// After the final stack frame in a stack walk we'll get one invalid
// (eNotAValidFrame) stack frame -- one past the end of the stack walk.  But
// higher-level code will need to tell the differnece between "the unwind plan
// below this frame failed" versus "we successfully completed the stack walk"
// so this method helps to disambiguate that.

bool RegisterContextLLDB::IsTrapHandlerFrame() const {
  return m_frame_type == eTrapHandlerFrame;
}

// A skip frame is a bogus frame on the stack -- but one where we're likely to
// find a real frame farther
// up the stack if we keep looking.  It's always the second frame in an unwind
// (i.e. the first frame after frame zero) where unwinding can be the
// trickiest.  Ideally we'll mark up this frame in some way so the user knows
// we're displaying bad data and we may have skipped one frame of their real
// program in the process of getting back on track.

bool RegisterContextLLDB::IsSkipFrame() const {
  return m_frame_type == eSkipFrame;
}

bool RegisterContextLLDB::IsTrapHandlerSymbol(
    lldb_private::Process *process,
    const lldb_private::SymbolContext &m_sym_ctx) const {
  PlatformSP platform_sp(process->GetTarget().GetPlatform());
  if (platform_sp) {
    const std::vector<ConstString> trap_handler_names(
        platform_sp->GetTrapHandlerSymbolNames());
    for (ConstString name : trap_handler_names) {
      if ((m_sym_ctx.function && m_sym_ctx.function->GetName() == name) ||
          (m_sym_ctx.symbol && m_sym_ctx.symbol->GetName() == name)) {
        return true;
      }
    }
  }
  const std::vector<ConstString> user_specified_trap_handler_names(
      m_parent_unwind.GetUserSpecifiedTrapHandlerFunctionNames());
  for (ConstString name : user_specified_trap_handler_names) {
    if ((m_sym_ctx.function && m_sym_ctx.function->GetName() == name) ||
        (m_sym_ctx.symbol && m_sym_ctx.symbol->GetName() == name)) {
      return true;
    }
  }

  return false;
}

// Answer the question: Where did THIS frame save the CALLER frame ("previous"
// frame)'s register value?

enum UnwindLLDB::RegisterSearchResult
RegisterContextLLDB::SavedLocationForRegister(
    uint32_t lldb_regnum, lldb_private::UnwindLLDB::RegisterLocation &regloc) {
  RegisterNumber regnum(m_thread, eRegisterKindLLDB, lldb_regnum);

  // Have we already found this register location?
  if (!m_registers.empty()) {
    std::map<uint32_t,
             lldb_private::UnwindLLDB::RegisterLocation>::const_iterator
        iterator;
    iterator = m_registers.find(regnum.GetAsKind(eRegisterKindLLDB));
    if (iterator != m_registers.end()) {
      regloc = iterator->second;
      UnwindLogMsg("supplying caller's saved %s (%d)'s location, cached",
                   regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
      return UnwindLLDB::RegisterSearchResult::eRegisterFound;
    }
  }

  // Look through the available UnwindPlans for the register location.

  UnwindPlan::Row::RegisterLocation unwindplan_regloc;
  bool have_unwindplan_regloc = false;
  RegisterKind unwindplan_registerkind = kNumRegisterKinds;

  if (m_fast_unwind_plan_sp) {
    UnwindPlan::RowSP active_row =
        m_fast_unwind_plan_sp->GetRowForFunctionOffset(m_current_offset);
    unwindplan_registerkind = m_fast_unwind_plan_sp->GetRegisterKind();
    if (regnum.GetAsKind(unwindplan_registerkind) == LLDB_INVALID_REGNUM) {
      UnwindLogMsg("could not convert lldb regnum %s (%d) into %d RegisterKind "
                   "reg numbering scheme",
                   regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB),
                   (int)unwindplan_registerkind);
      return UnwindLLDB::RegisterSearchResult::eRegisterNotFound;
    }
    if (active_row->GetRegisterInfo(regnum.GetAsKind(unwindplan_registerkind),
                                    unwindplan_regloc)) {
      UnwindLogMsg(
          "supplying caller's saved %s (%d)'s location using FastUnwindPlan",
          regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
      have_unwindplan_regloc = true;
    }
  }

  if (!have_unwindplan_regloc) {
    // m_full_unwind_plan_sp being NULL means that we haven't tried to find a
    // full UnwindPlan yet
    if (!m_full_unwind_plan_sp)
      m_full_unwind_plan_sp = GetFullUnwindPlanForFrame();

    if (m_full_unwind_plan_sp) {
      RegisterNumber pc_regnum(m_thread, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_PC);

      UnwindPlan::RowSP active_row =
          m_full_unwind_plan_sp->GetRowForFunctionOffset(m_current_offset);
      unwindplan_registerkind = m_full_unwind_plan_sp->GetRegisterKind();

      RegisterNumber return_address_reg;

      // If we're fetching the saved pc and this UnwindPlan defines a
      // ReturnAddress register (e.g. lr on arm), look for the return address
      // register number in the UnwindPlan's row.
      if (pc_regnum.IsValid() && pc_regnum == regnum &&
          m_full_unwind_plan_sp->GetReturnAddressRegister() !=
              LLDB_INVALID_REGNUM) {

        return_address_reg.init(
            m_thread, m_full_unwind_plan_sp->GetRegisterKind(),
            m_full_unwind_plan_sp->GetReturnAddressRegister());
        regnum = return_address_reg;
        UnwindLogMsg("requested caller's saved PC but this UnwindPlan uses a "
                     "RA reg; getting %s (%d) instead",
                     return_address_reg.GetName(),
                     return_address_reg.GetAsKind(eRegisterKindLLDB));
      } else {
        if (regnum.GetAsKind(unwindplan_registerkind) == LLDB_INVALID_REGNUM) {
          if (unwindplan_registerkind == eRegisterKindGeneric) {
            UnwindLogMsg("could not convert lldb regnum %s (%d) into "
                         "eRegisterKindGeneric reg numbering scheme",
                         regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
          } else {
            UnwindLogMsg("could not convert lldb regnum %s (%d) into %d "
                         "RegisterKind reg numbering scheme",
                         regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB),
                         (int)unwindplan_registerkind);
          }
          return UnwindLLDB::RegisterSearchResult::eRegisterNotFound;
        }
      }

      if (regnum.IsValid() &&
          active_row->GetRegisterInfo(regnum.GetAsKind(unwindplan_registerkind),
                                      unwindplan_regloc)) {
        have_unwindplan_regloc = true;
        UnwindLogMsg(
            "supplying caller's saved %s (%d)'s location using %s UnwindPlan",
            regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB),
            m_full_unwind_plan_sp->GetSourceName().GetCString());
      }

      // This is frame 0 and we're retrieving the PC and it's saved in a Return
      // Address register and it hasn't been saved anywhere yet -- that is,
      // it's still live in the actual register. Handle this specially.

      if (!have_unwindplan_regloc && return_address_reg.IsValid() &&
          IsFrameZero()) {
        if (return_address_reg.GetAsKind(eRegisterKindLLDB) !=
            LLDB_INVALID_REGNUM) {
          lldb_private::UnwindLLDB::RegisterLocation new_regloc;
          new_regloc.type =
              UnwindLLDB::RegisterLocation::eRegisterInLiveRegisterContext;
          new_regloc.location.register_number =
              return_address_reg.GetAsKind(eRegisterKindLLDB);
          m_registers[regnum.GetAsKind(eRegisterKindLLDB)] = new_regloc;
          regloc = new_regloc;
          UnwindLogMsg("supplying caller's register %s (%d) from the live "
                       "RegisterContext at frame 0, saved in %d",
                       return_address_reg.GetName(),
                       return_address_reg.GetAsKind(eRegisterKindLLDB),
                       return_address_reg.GetAsKind(eRegisterKindLLDB));
          return UnwindLLDB::RegisterSearchResult::eRegisterFound;
        }
      }

      // If this architecture stores the return address in a register (it
      // defines a Return Address register) and we're on a non-zero stack frame
      // and the Full UnwindPlan says that the pc is stored in the
      // RA registers (e.g. lr on arm), then we know that the full unwindplan is
      // not trustworthy -- this
      // is an impossible situation and the instruction emulation code has
      // likely been misled. If this stack frame meets those criteria, we need
      // to throw away the Full UnwindPlan that the instruction emulation came
      // up with and fall back to the architecture's Default UnwindPlan so the
      // stack walk can get past this point.

      // Special note:  If the Full UnwindPlan was generated from the compiler,
      // don't second-guess it when we're at a call site location.

      // arch_default_ra_regnum is the return address register # in the Full
      // UnwindPlan register numbering
      RegisterNumber arch_default_ra_regnum(m_thread, eRegisterKindGeneric,
                                            LLDB_REGNUM_GENERIC_RA);

      if (arch_default_ra_regnum.GetAsKind(unwindplan_registerkind) !=
              LLDB_INVALID_REGNUM &&
          pc_regnum == regnum && unwindplan_regloc.IsInOtherRegister() &&
          unwindplan_regloc.GetRegisterNumber() ==
              arch_default_ra_regnum.GetAsKind(unwindplan_registerkind) &&
          m_full_unwind_plan_sp->GetSourcedFromCompiler() != eLazyBoolYes &&
          !m_all_registers_available) {
        UnwindLogMsg("%s UnwindPlan tried to restore the pc from the link "
                     "register but this is a non-zero frame",
                     m_full_unwind_plan_sp->GetSourceName().GetCString());

        // Throw away the full unwindplan; install the arch default unwindplan
        if (ForceSwitchToFallbackUnwindPlan()) {
          // Update for the possibly new unwind plan
          unwindplan_registerkind = m_full_unwind_plan_sp->GetRegisterKind();
          UnwindPlan::RowSP active_row =
              m_full_unwind_plan_sp->GetRowForFunctionOffset(m_current_offset);

          // Sanity check: Verify that we can fetch a pc value and CFA value
          // with this unwind plan

          RegisterNumber arch_default_pc_reg(m_thread, eRegisterKindGeneric,
                                             LLDB_REGNUM_GENERIC_PC);
          bool can_fetch_pc_value = false;
          bool can_fetch_cfa = false;
          addr_t cfa_value;
          if (active_row) {
            if (arch_default_pc_reg.GetAsKind(unwindplan_registerkind) !=
                    LLDB_INVALID_REGNUM &&
                active_row->GetRegisterInfo(
                    arch_default_pc_reg.GetAsKind(unwindplan_registerkind),
                    unwindplan_regloc)) {
              can_fetch_pc_value = true;
            }
            if (ReadFrameAddress(unwindplan_registerkind,
                                 active_row->GetCFAValue(), cfa_value)) {
              can_fetch_cfa = true;
            }
          }

          have_unwindplan_regloc = can_fetch_pc_value && can_fetch_cfa;
        } else {
          // We were unable to fall back to another unwind plan
          have_unwindplan_regloc = false;
        }
      }
    }
  }

  ExecutionContext exe_ctx(m_thread.shared_from_this());
  Process *process = exe_ctx.GetProcessPtr();
  if (!have_unwindplan_regloc) {
    // If the UnwindPlan failed to give us an unwind location for this
    // register, we may be able to fall back to some ABI-defined default.  For
    // example, some ABIs allow to determine the caller's SP via the CFA. Also,
    // the ABI may set volatile registers to the undefined state.
    ABI *abi = process ? process->GetABI().get() : NULL;
    if (abi) {
      const RegisterInfo *reg_info =
          GetRegisterInfoAtIndex(regnum.GetAsKind(eRegisterKindLLDB));
      if (reg_info &&
          abi->GetFallbackRegisterLocation(reg_info, unwindplan_regloc)) {
        UnwindLogMsg(
            "supplying caller's saved %s (%d)'s location using ABI default",
            regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
        have_unwindplan_regloc = true;
      }
    }
  }

  if (!have_unwindplan_regloc) {
    if (IsFrameZero()) {
      // This is frame 0 - we should return the actual live register context
      // value
      lldb_private::UnwindLLDB::RegisterLocation new_regloc;
      new_regloc.type =
          UnwindLLDB::RegisterLocation::eRegisterInLiveRegisterContext;
      new_regloc.location.register_number = regnum.GetAsKind(eRegisterKindLLDB);
      m_registers[regnum.GetAsKind(eRegisterKindLLDB)] = new_regloc;
      regloc = new_regloc;
      UnwindLogMsg("supplying caller's register %s (%d) from the live "
                   "RegisterContext at frame 0",
                   regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
      return UnwindLLDB::RegisterSearchResult::eRegisterFound;
    } else {
      std::string unwindplan_name("");
      if (m_full_unwind_plan_sp) {
        unwindplan_name += "via '";
        unwindplan_name += m_full_unwind_plan_sp->GetSourceName().AsCString();
        unwindplan_name += "'";
      }
      UnwindLogMsg("no save location for %s (%d) %s", regnum.GetName(),
                   regnum.GetAsKind(eRegisterKindLLDB),
                   unwindplan_name.c_str());
    }
    return UnwindLLDB::RegisterSearchResult::eRegisterNotFound;
  }

  // unwindplan_regloc has valid contents about where to retrieve the register
  if (unwindplan_regloc.IsUnspecified()) {
    lldb_private::UnwindLLDB::RegisterLocation new_regloc;
    new_regloc.type = UnwindLLDB::RegisterLocation::eRegisterNotSaved;
    m_registers[regnum.GetAsKind(eRegisterKindLLDB)] = new_regloc;
    UnwindLogMsg("save location for %s (%d) is unspecified, continue searching",
                 regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
    return UnwindLLDB::RegisterSearchResult::eRegisterNotFound;
  }

  if (unwindplan_regloc.IsUndefined()) {
    UnwindLogMsg(
        "did not supply reg location for %s (%d) because it is volatile",
        regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
    return UnwindLLDB::RegisterSearchResult::eRegisterIsVolatile;
  }

  if (unwindplan_regloc.IsSame()) {
    if (!IsFrameZero() &&
        (regnum.GetAsKind(eRegisterKindGeneric) == LLDB_REGNUM_GENERIC_PC ||
         regnum.GetAsKind(eRegisterKindGeneric) == LLDB_REGNUM_GENERIC_RA)) {
      UnwindLogMsg("register %s (%d) is marked as 'IsSame' - it is a pc or "
                   "return address reg on a non-zero frame -- treat as if we "
                   "have no information",
                   regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
      return UnwindLLDB::RegisterSearchResult::eRegisterNotFound;
    } else {
      regloc.type = UnwindLLDB::RegisterLocation::eRegisterInRegister;
      regloc.location.register_number = regnum.GetAsKind(eRegisterKindLLDB);
      m_registers[regnum.GetAsKind(eRegisterKindLLDB)] = regloc;
      UnwindLogMsg(
          "supplying caller's register %s (%d), saved in register %s (%d)",
          regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB),
          regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
      return UnwindLLDB::RegisterSearchResult::eRegisterFound;
    }
  }

  if (unwindplan_regloc.IsCFAPlusOffset()) {
    int offset = unwindplan_regloc.GetOffset();
    regloc.type = UnwindLLDB::RegisterLocation::eRegisterValueInferred;
    regloc.location.inferred_value = m_cfa + offset;
    m_registers[regnum.GetAsKind(eRegisterKindLLDB)] = regloc;
    UnwindLogMsg("supplying caller's register %s (%d), value is CFA plus "
                 "offset %d [value is 0x%" PRIx64 "]",
                 regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB), offset,
                 regloc.location.inferred_value);
    return UnwindLLDB::RegisterSearchResult::eRegisterFound;
  }

  if (unwindplan_regloc.IsAtCFAPlusOffset()) {
    int offset = unwindplan_regloc.GetOffset();
    regloc.type = UnwindLLDB::RegisterLocation::eRegisterSavedAtMemoryLocation;
    regloc.location.target_memory_location = m_cfa + offset;
    m_registers[regnum.GetAsKind(eRegisterKindLLDB)] = regloc;
    UnwindLogMsg("supplying caller's register %s (%d) from the stack, saved at "
                 "CFA plus offset %d [saved at 0x%" PRIx64 "]",
                 regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB), offset,
                 regloc.location.target_memory_location);
    return UnwindLLDB::RegisterSearchResult::eRegisterFound;
  }

  if (unwindplan_regloc.IsAFAPlusOffset()) {
    if (m_afa == LLDB_INVALID_ADDRESS)
        return UnwindLLDB::RegisterSearchResult::eRegisterNotFound;

    int offset = unwindplan_regloc.GetOffset();
    regloc.type = UnwindLLDB::RegisterLocation::eRegisterValueInferred;
    regloc.location.inferred_value = m_afa + offset;
    m_registers[regnum.GetAsKind(eRegisterKindLLDB)] = regloc;
    UnwindLogMsg("supplying caller's register %s (%d), value is AFA plus "
                 "offset %d [value is 0x%" PRIx64 "]",
                 regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB), offset,
                 regloc.location.inferred_value);
    return UnwindLLDB::RegisterSearchResult::eRegisterFound;
  }

  if (unwindplan_regloc.IsAtAFAPlusOffset()) {
    if (m_afa == LLDB_INVALID_ADDRESS)
        return UnwindLLDB::RegisterSearchResult::eRegisterNotFound;

    int offset = unwindplan_regloc.GetOffset();
    regloc.type = UnwindLLDB::RegisterLocation::eRegisterSavedAtMemoryLocation;
    regloc.location.target_memory_location = m_afa + offset;
    m_registers[regnum.GetAsKind(eRegisterKindLLDB)] = regloc;
    UnwindLogMsg("supplying caller's register %s (%d) from the stack, saved at "
                 "AFA plus offset %d [saved at 0x%" PRIx64 "]",
                 regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB), offset,
                 regloc.location.target_memory_location);
    return UnwindLLDB::RegisterSearchResult::eRegisterFound;
  }

  if (unwindplan_regloc.IsInOtherRegister()) {
    uint32_t unwindplan_regnum = unwindplan_regloc.GetRegisterNumber();
    RegisterNumber row_regnum(m_thread, unwindplan_registerkind,
                              unwindplan_regnum);
    if (row_regnum.GetAsKind(eRegisterKindLLDB) == LLDB_INVALID_REGNUM) {
      UnwindLogMsg("could not supply caller's %s (%d) location - was saved in "
                   "another reg but couldn't convert that regnum",
                   regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
      return UnwindLLDB::RegisterSearchResult::eRegisterNotFound;
    }
    regloc.type = UnwindLLDB::RegisterLocation::eRegisterInRegister;
    regloc.location.register_number = row_regnum.GetAsKind(eRegisterKindLLDB);
    m_registers[regnum.GetAsKind(eRegisterKindLLDB)] = regloc;
    UnwindLogMsg(
        "supplying caller's register %s (%d), saved in register %s (%d)",
        regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB),
        row_regnum.GetName(), row_regnum.GetAsKind(eRegisterKindLLDB));
    return UnwindLLDB::RegisterSearchResult::eRegisterFound;
  }

  if (unwindplan_regloc.IsDWARFExpression() ||
      unwindplan_regloc.IsAtDWARFExpression()) {
    DataExtractor dwarfdata(unwindplan_regloc.GetDWARFExpressionBytes(),
                            unwindplan_regloc.GetDWARFExpressionLength(),
                            process->GetByteOrder(),
                            process->GetAddressByteSize());
    ModuleSP opcode_ctx;
    DWARFExpression dwarfexpr(opcode_ctx, dwarfdata, nullptr, 0,
                              unwindplan_regloc.GetDWARFExpressionLength());
    dwarfexpr.SetRegisterKind(unwindplan_registerkind);
    Value result;
    Status error;
    if (dwarfexpr.Evaluate(&exe_ctx, this, 0, nullptr, nullptr, result,
                           &error)) {
      addr_t val;
      val = result.GetScalar().ULongLong();
      if (unwindplan_regloc.IsDWARFExpression()) {
        regloc.type = UnwindLLDB::RegisterLocation::eRegisterValueInferred;
        regloc.location.inferred_value = val;
        m_registers[regnum.GetAsKind(eRegisterKindLLDB)] = regloc;
        UnwindLogMsg("supplying caller's register %s (%d) via DWARF expression "
                     "(IsDWARFExpression)",
                     regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
        return UnwindLLDB::RegisterSearchResult::eRegisterFound;
      } else {
        regloc.type =
            UnwindLLDB::RegisterLocation::eRegisterSavedAtMemoryLocation;
        regloc.location.target_memory_location = val;
        m_registers[regnum.GetAsKind(eRegisterKindLLDB)] = regloc;
        UnwindLogMsg("supplying caller's register %s (%d) via DWARF expression "
                     "(IsAtDWARFExpression)",
                     regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
        return UnwindLLDB::RegisterSearchResult::eRegisterFound;
      }
    }
    UnwindLogMsg("tried to use IsDWARFExpression or IsAtDWARFExpression for %s "
                 "(%d) but failed",
                 regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));
    return UnwindLLDB::RegisterSearchResult::eRegisterNotFound;
  }

  UnwindLogMsg("no save location for %s (%d) in this stack frame",
               regnum.GetName(), regnum.GetAsKind(eRegisterKindLLDB));

  // FIXME UnwindPlan::Row types atDWARFExpression and isDWARFExpression are
  // unsupported.

  return UnwindLLDB::RegisterSearchResult::eRegisterNotFound;
}

// TryFallbackUnwindPlan() -- this method is a little tricky.
//
// When this is called, the frame above -- the caller frame, the "previous"
// frame -- is invalid or bad.
//
// Instead of stopping the stack walk here, we'll try a different UnwindPlan
// and see if we can get a valid frame above us.
//
// This most often happens when an unwind plan based on assembly instruction
// inspection is not correct -- mostly with hand-written assembly functions or
// functions where the stack frame is set up "out of band", e.g. the kernel
// saved the register context and then called an asynchronous trap handler like
// _sigtramp.
//
// Often in these cases, if we just do a dumb stack walk we'll get past this
// tricky frame and our usual techniques can continue to be used.

bool RegisterContextLLDB::TryFallbackUnwindPlan() {
  if (m_fallback_unwind_plan_sp.get() == nullptr)
    return false;

  if (m_full_unwind_plan_sp.get() == nullptr)
    return false;

  if (m_full_unwind_plan_sp.get() == m_fallback_unwind_plan_sp.get() ||
      m_full_unwind_plan_sp->GetSourceName() ==
          m_fallback_unwind_plan_sp->GetSourceName()) {
    return false;
  }

  // If a compiler generated unwind plan failed, trying the arch default
  // unwindplan isn't going to do any better.
  if (m_full_unwind_plan_sp->GetSourcedFromCompiler() == eLazyBoolYes)
    return false;

  // Get the caller's pc value and our own CFA value. Swap in the fallback
  // unwind plan, re-fetch the caller's pc value and CFA value. If they're the
  // same, then the fallback unwind plan provides no benefit.

  RegisterNumber pc_regnum(m_thread, eRegisterKindGeneric,
                           LLDB_REGNUM_GENERIC_PC);

  addr_t old_caller_pc_value = LLDB_INVALID_ADDRESS;
  addr_t new_caller_pc_value = LLDB_INVALID_ADDRESS;
  UnwindLLDB::RegisterLocation regloc;
  if (SavedLocationForRegister(pc_regnum.GetAsKind(eRegisterKindLLDB),
                               regloc) ==
      UnwindLLDB::RegisterSearchResult::eRegisterFound) {
    const RegisterInfo *reg_info =
        GetRegisterInfoAtIndex(pc_regnum.GetAsKind(eRegisterKindLLDB));
    if (reg_info) {
      RegisterValue reg_value;
      if (ReadRegisterValueFromRegisterLocation(regloc, reg_info, reg_value)) {
        old_caller_pc_value = reg_value.GetAsUInt64();
      }
    }
  }

  // This is a tricky wrinkle!  If SavedLocationForRegister() detects a really
  // impossible register location for the full unwind plan, it may call
  // ForceSwitchToFallbackUnwindPlan() which in turn replaces the full
  // unwindplan with the fallback... in short, we're done, we're using the
  // fallback UnwindPlan. We checked if m_fallback_unwind_plan_sp was nullptr
  // at the top -- the only way it became nullptr since then is via
  // SavedLocationForRegister().
  if (m_fallback_unwind_plan_sp.get() == nullptr)
    return true;

  // Switch the full UnwindPlan to be the fallback UnwindPlan.  If we decide
  // this isn't working, we need to restore. We'll also need to save & restore
  // the value of the m_cfa ivar.  Save is down below a bit in 'old_cfa'.
  UnwindPlanSP original_full_unwind_plan_sp = m_full_unwind_plan_sp;
  addr_t old_cfa = m_cfa;
  addr_t old_afa = m_afa;

  m_registers.clear();

  m_full_unwind_plan_sp = m_fallback_unwind_plan_sp;

  UnwindPlan::RowSP active_row =
      m_fallback_unwind_plan_sp->GetRowForFunctionOffset(m_current_offset);

  if (active_row &&
      active_row->GetCFAValue().GetValueType() !=
          UnwindPlan::Row::FAValue::unspecified) {
    addr_t new_cfa;
    if (!ReadFrameAddress(m_fallback_unwind_plan_sp->GetRegisterKind(),
                            active_row->GetCFAValue(), new_cfa) ||
        new_cfa == 0 || new_cfa == 1 || new_cfa == LLDB_INVALID_ADDRESS) {
      UnwindLogMsg("failed to get cfa with fallback unwindplan");
      m_fallback_unwind_plan_sp.reset();
      m_full_unwind_plan_sp = original_full_unwind_plan_sp;
      return false;
    }
    m_cfa = new_cfa;

    ReadFrameAddress(m_fallback_unwind_plan_sp->GetRegisterKind(),
                     active_row->GetAFAValue(), m_afa);

    if (SavedLocationForRegister(pc_regnum.GetAsKind(eRegisterKindLLDB),
                                 regloc) ==
        UnwindLLDB::RegisterSearchResult::eRegisterFound) {
      const RegisterInfo *reg_info =
          GetRegisterInfoAtIndex(pc_regnum.GetAsKind(eRegisterKindLLDB));
      if (reg_info) {
        RegisterValue reg_value;
        if (ReadRegisterValueFromRegisterLocation(regloc, reg_info,
                                                  reg_value)) {
          new_caller_pc_value = reg_value.GetAsUInt64();
        }
      }
    }

    if (new_caller_pc_value == LLDB_INVALID_ADDRESS) {
      UnwindLogMsg("failed to get a pc value for the caller frame with the "
                   "fallback unwind plan");
      m_fallback_unwind_plan_sp.reset();
      m_full_unwind_plan_sp = original_full_unwind_plan_sp;
      m_cfa = old_cfa;
      m_afa = old_afa;
      return false;
    }

    if (old_caller_pc_value == new_caller_pc_value &&
        m_cfa == old_cfa &&
        m_afa == old_afa) {
      UnwindLogMsg("fallback unwind plan got the same values for this frame "
                   "CFA and caller frame pc, not using");
      m_fallback_unwind_plan_sp.reset();
      m_full_unwind_plan_sp = original_full_unwind_plan_sp;
      return false;
    }

    UnwindLogMsg("trying to unwind from this function with the UnwindPlan '%s' "
                 "because UnwindPlan '%s' failed.",
                 m_fallback_unwind_plan_sp->GetSourceName().GetCString(),
                 original_full_unwind_plan_sp->GetSourceName().GetCString());

    // We've copied the fallback unwind plan into the full - now clear the
    // fallback.
    m_fallback_unwind_plan_sp.reset();
  }

  return true;
}

bool RegisterContextLLDB::ForceSwitchToFallbackUnwindPlan() {
  if (m_fallback_unwind_plan_sp.get() == NULL)
    return false;

  if (m_full_unwind_plan_sp.get() == NULL)
    return false;

  if (m_full_unwind_plan_sp.get() == m_fallback_unwind_plan_sp.get() ||
      m_full_unwind_plan_sp->GetSourceName() ==
          m_fallback_unwind_plan_sp->GetSourceName()) {
    return false;
  }

  UnwindPlan::RowSP active_row =
      m_fallback_unwind_plan_sp->GetRowForFunctionOffset(m_current_offset);

  if (active_row &&
      active_row->GetCFAValue().GetValueType() !=
          UnwindPlan::Row::FAValue::unspecified) {
    addr_t new_cfa;
    if (!ReadFrameAddress(m_fallback_unwind_plan_sp->GetRegisterKind(),
                            active_row->GetCFAValue(), new_cfa) ||
        new_cfa == 0 || new_cfa == 1 || new_cfa == LLDB_INVALID_ADDRESS) {
      UnwindLogMsg("failed to get cfa with fallback unwindplan");
      m_fallback_unwind_plan_sp.reset();
      return false;
    }

    ReadFrameAddress(m_fallback_unwind_plan_sp->GetRegisterKind(),
                     active_row->GetAFAValue(), m_afa);

    m_full_unwind_plan_sp = m_fallback_unwind_plan_sp;
    m_fallback_unwind_plan_sp.reset();

    m_registers.clear();

    m_cfa = new_cfa;

    UnwindLogMsg("switched unconditionally to the fallback unwindplan %s",
                 m_full_unwind_plan_sp->GetSourceName().GetCString());
    return true;
  }
  return false;
}

bool RegisterContextLLDB::ReadFrameAddress(
    lldb::RegisterKind row_register_kind, UnwindPlan::Row::FAValue &fa,
    addr_t &address) {
  RegisterValue reg_value;

  address = LLDB_INVALID_ADDRESS;
  addr_t cfa_reg_contents;

  switch (fa.GetValueType()) {
  case UnwindPlan::Row::FAValue::isRegisterDereferenced: {
    RegisterNumber cfa_reg(m_thread, row_register_kind,
                           fa.GetRegisterNumber());
    if (ReadGPRValue(cfa_reg, cfa_reg_contents)) {
      const RegisterInfo *reg_info =
          GetRegisterInfoAtIndex(cfa_reg.GetAsKind(eRegisterKindLLDB));
      RegisterValue reg_value;
      if (reg_info) {
        Status error = ReadRegisterValueFromMemory(
            reg_info, cfa_reg_contents, reg_info->byte_size, reg_value);
        if (error.Success()) {
          address = reg_value.GetAsUInt64();
          UnwindLogMsg(
              "CFA value via dereferencing reg %s (%d): reg has val 0x%" PRIx64
              ", CFA value is 0x%" PRIx64,
              cfa_reg.GetName(), cfa_reg.GetAsKind(eRegisterKindLLDB),
              cfa_reg_contents, address);
          return true;
        } else {
          UnwindLogMsg("Tried to deref reg %s (%d) [0x%" PRIx64
                       "] but memory read failed.",
                       cfa_reg.GetName(), cfa_reg.GetAsKind(eRegisterKindLLDB),
                       cfa_reg_contents);
        }
      }
    }
    break;
  }
  case UnwindPlan::Row::FAValue::isRegisterPlusOffset: {
    RegisterNumber cfa_reg(m_thread, row_register_kind,
                           fa.GetRegisterNumber());
    if (ReadGPRValue(cfa_reg, cfa_reg_contents)) {
      if (cfa_reg_contents == LLDB_INVALID_ADDRESS || cfa_reg_contents == 0 ||
          cfa_reg_contents == 1) {
        UnwindLogMsg(
            "Got an invalid CFA register value - reg %s (%d), value 0x%" PRIx64,
            cfa_reg.GetName(), cfa_reg.GetAsKind(eRegisterKindLLDB),
            cfa_reg_contents);
        cfa_reg_contents = LLDB_INVALID_ADDRESS;
        return false;
      }
      address = cfa_reg_contents + fa.GetOffset();
      UnwindLogMsg(
          "CFA is 0x%" PRIx64 ": Register %s (%d) contents are 0x%" PRIx64
          ", offset is %d",
          address, cfa_reg.GetName(), cfa_reg.GetAsKind(eRegisterKindLLDB),
          cfa_reg_contents, fa.GetOffset());
      return true;
    }
    break;
  }
  case UnwindPlan::Row::FAValue::isDWARFExpression: {
    ExecutionContext exe_ctx(m_thread.shared_from_this());
    Process *process = exe_ctx.GetProcessPtr();
    DataExtractor dwarfdata(fa.GetDWARFExpressionBytes(),
                            fa.GetDWARFExpressionLength(),
                            process->GetByteOrder(),
                            process->GetAddressByteSize());
    ModuleSP opcode_ctx;
    DWARFExpression dwarfexpr(opcode_ctx, dwarfdata, nullptr, 0,
                              fa.GetDWARFExpressionLength());
    dwarfexpr.SetRegisterKind(row_register_kind);
    Value result;
    Status error;
    if (dwarfexpr.Evaluate(&exe_ctx, this, 0, nullptr, nullptr, result,
                           &error)) {
      address = result.GetScalar().ULongLong();

      UnwindLogMsg("CFA value set by DWARF expression is 0x%" PRIx64,
                   address);
      return true;
    }
    UnwindLogMsg("Failed to set CFA value via DWARF expression: %s",
                 error.AsCString());
    break;
  }
  default:
    return false;
  }
  return false;
}

// Retrieve a general purpose register value for THIS frame, as saved by the
// NEXT frame, i.e. the frame that
// this frame called.  e.g.
//
//  foo () { }
//  bar () { foo (); }
//  main () { bar (); }
//
//  stopped in foo() so
//     frame 0 - foo
//     frame 1 - bar
//     frame 2 - main
//  and this RegisterContext is for frame 1 (bar) - if we want to get the pc
//  value for frame 1, we need to ask
//  where frame 0 (the "next" frame) saved that and retrieve the value.

bool RegisterContextLLDB::ReadGPRValue(lldb::RegisterKind register_kind,
                                       uint32_t regnum, addr_t &value) {
  if (!IsValid())
    return false;

  uint32_t lldb_regnum;
  if (register_kind == eRegisterKindLLDB) {
    lldb_regnum = regnum;
  } else if (!m_thread.GetRegisterContext()->ConvertBetweenRegisterKinds(
                 register_kind, regnum, eRegisterKindLLDB, lldb_regnum)) {
    return false;
  }

  const RegisterInfo *reg_info = GetRegisterInfoAtIndex(lldb_regnum);
  RegisterValue reg_value;
  // if this is frame 0 (currently executing frame), get the requested reg
  // contents from the actual thread registers
  if (IsFrameZero()) {
    if (m_thread.GetRegisterContext()->ReadRegister(reg_info, reg_value)) {
      value = reg_value.GetAsUInt64();
      return true;
    }
    return false;
  }

  bool pc_register = false;
  uint32_t generic_regnum;
  if (register_kind == eRegisterKindGeneric &&
      (regnum == LLDB_REGNUM_GENERIC_PC || regnum == LLDB_REGNUM_GENERIC_RA)) {
    pc_register = true;
  } else if (m_thread.GetRegisterContext()->ConvertBetweenRegisterKinds(
                 register_kind, regnum, eRegisterKindGeneric, generic_regnum) &&
             (generic_regnum == LLDB_REGNUM_GENERIC_PC ||
              generic_regnum == LLDB_REGNUM_GENERIC_RA)) {
    pc_register = true;
  }

  lldb_private::UnwindLLDB::RegisterLocation regloc;
  if (!m_parent_unwind.SearchForSavedLocationForRegister(
          lldb_regnum, regloc, m_frame_number - 1, pc_register)) {
    return false;
  }
  if (ReadRegisterValueFromRegisterLocation(regloc, reg_info, reg_value)) {
    value = reg_value.GetAsUInt64();
    return true;
  }
  return false;
}

bool RegisterContextLLDB::ReadGPRValue(const RegisterNumber &regnum,
                                       addr_t &value) {
  return ReadGPRValue(regnum.GetRegisterKind(), regnum.GetRegisterNumber(),
                      value);
}

// Find the value of a register in THIS frame

bool RegisterContextLLDB::ReadRegister(const RegisterInfo *reg_info,
                                       RegisterValue &value) {
  if (!IsValid())
    return false;

  const uint32_t lldb_regnum = reg_info->kinds[eRegisterKindLLDB];
  UnwindLogMsgVerbose("looking for register saved location for reg %d",
                      lldb_regnum);

  // If this is the 0th frame, hand this over to the live register context
  if (IsFrameZero()) {
    UnwindLogMsgVerbose("passing along to the live register context for reg %d",
                        lldb_regnum);
    return m_thread.GetRegisterContext()->ReadRegister(reg_info, value);
  }

  bool is_pc_regnum = false;
  if (reg_info->kinds[eRegisterKindGeneric] == LLDB_REGNUM_GENERIC_PC ||
      reg_info->kinds[eRegisterKindGeneric] == LLDB_REGNUM_GENERIC_RA) {
    is_pc_regnum = true;
  }

  lldb_private::UnwindLLDB::RegisterLocation regloc;
  // Find out where the NEXT frame saved THIS frame's register contents
  if (!m_parent_unwind.SearchForSavedLocationForRegister(
          lldb_regnum, regloc, m_frame_number - 1, is_pc_regnum))
    return false;

  return ReadRegisterValueFromRegisterLocation(regloc, reg_info, value);
}

bool RegisterContextLLDB::WriteRegister(const RegisterInfo *reg_info,
                                        const RegisterValue &value) {
  if (!IsValid())
    return false;

  const uint32_t lldb_regnum = reg_info->kinds[eRegisterKindLLDB];
  UnwindLogMsgVerbose("looking for register saved location for reg %d",
                      lldb_regnum);

  // If this is the 0th frame, hand this over to the live register context
  if (IsFrameZero()) {
    UnwindLogMsgVerbose("passing along to the live register context for reg %d",
                        lldb_regnum);
    return m_thread.GetRegisterContext()->WriteRegister(reg_info, value);
  }

  lldb_private::UnwindLLDB::RegisterLocation regloc;
  // Find out where the NEXT frame saved THIS frame's register contents
  if (!m_parent_unwind.SearchForSavedLocationForRegister(
          lldb_regnum, regloc, m_frame_number - 1, false))
    return false;

  return WriteRegisterValueToRegisterLocation(regloc, reg_info, value);
}

// Don't need to implement this one
bool RegisterContextLLDB::ReadAllRegisterValues(lldb::DataBufferSP &data_sp) {
  return false;
}

// Don't need to implement this one
bool RegisterContextLLDB::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  return false;
}

// Retrieve the pc value for THIS from

bool RegisterContextLLDB::GetCFA(addr_t &cfa) {
  if (!IsValid()) {
    return false;
  }
  if (m_cfa == LLDB_INVALID_ADDRESS) {
    return false;
  }
  cfa = m_cfa;
  return true;
}

RegisterContextLLDB::SharedPtr RegisterContextLLDB::GetNextFrame() const {
  RegisterContextLLDB::SharedPtr regctx;
  if (m_frame_number == 0)
    return regctx;
  return m_parent_unwind.GetRegisterContextForFrameNum(m_frame_number - 1);
}

RegisterContextLLDB::SharedPtr RegisterContextLLDB::GetPrevFrame() const {
  RegisterContextLLDB::SharedPtr regctx;
  return m_parent_unwind.GetRegisterContextForFrameNum(m_frame_number + 1);
}

// Retrieve the address of the start of the function of THIS frame

bool RegisterContextLLDB::GetStartPC(addr_t &start_pc) {
  if (!IsValid())
    return false;

  if (!m_start_pc.IsValid()) {
        bool read_successfully = ReadPC (start_pc);
        if (read_successfully)
        {
            ProcessSP process_sp (m_thread.GetProcess());
            if (process_sp)
            {
                ABI *abi = process_sp->GetABI().get();
                if (abi)
                    start_pc = abi->FixCodeAddress(start_pc);
            }
        }
        return read_successfully;
  }
  start_pc = m_start_pc.GetLoadAddress(CalculateTarget().get());
  return true;
}

// Retrieve the current pc value for THIS frame, as saved by the NEXT frame.

bool RegisterContextLLDB::ReadPC(addr_t &pc) {
  if (!IsValid())
    return false;

  bool above_trap_handler = false;
  if (GetNextFrame().get() && GetNextFrame()->IsValid() &&
      GetNextFrame()->IsTrapHandlerFrame())
    above_trap_handler = true;

  if (ReadGPRValue(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, pc)) {
    // A pc value of 0 or 1 is impossible in the middle of the stack -- it
    // indicates the end of a stack walk.
    // On the currently executing frame (or such a frame interrupted
    // asynchronously by sigtramp et al) this may occur if code has jumped
    // through a NULL pointer -- we want to be able to unwind past that frame
    // to help find the bug.

    ProcessSP process_sp (m_thread.GetProcess());
    if (process_sp)
    {
        ABI *abi = process_sp->GetABI().get();
        if (abi)
            pc = abi->FixCodeAddress(pc);
    }

    return !(m_all_registers_available == false &&
             above_trap_handler == false && (pc == 0 || pc == 1));
  } else {
    return false;
  }
}

void RegisterContextLLDB::UnwindLogMsg(const char *fmt, ...) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
  if (log) {
    va_list args;
    va_start(args, fmt);

    char *logmsg;
    if (vasprintf(&logmsg, fmt, args) == -1 || logmsg == NULL) {
      if (logmsg)
        free(logmsg);
      va_end(args);
      return;
    }
    va_end(args);

    log->Printf("%*sth%d/fr%u %s", m_frame_number < 100 ? m_frame_number : 100,
                "", m_thread.GetIndexID(), m_frame_number, logmsg);
    free(logmsg);
  }
}

void RegisterContextLLDB::UnwindLogMsgVerbose(const char *fmt, ...) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
  if (log && log->GetVerbose()) {
    va_list args;
    va_start(args, fmt);

    char *logmsg;
    if (vasprintf(&logmsg, fmt, args) == -1 || logmsg == NULL) {
      if (logmsg)
        free(logmsg);
      va_end(args);
      return;
    }
    va_end(args);

    log->Printf("%*sth%d/fr%u %s", m_frame_number < 100 ? m_frame_number : 100,
                "", m_thread.GetIndexID(), m_frame_number, logmsg);
    free(logmsg);
  }
}
