//===-- StackFrameList.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/StackFrameList.h"
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/SourceManager.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/Unwind.h"
#include "lldb/Utility/Log.h"
#include "llvm/ADT/SmallPtrSet.h"

//#define DEBUG_STACK_FRAMES 1

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// StackFrameList constructor
//----------------------------------------------------------------------
StackFrameList::StackFrameList(Thread &thread,
                               const lldb::StackFrameListSP &prev_frames_sp,
                               bool show_inline_frames)
    : m_thread(thread), m_prev_frames_sp(prev_frames_sp), m_mutex(), m_frames(),
      m_selected_frame_idx(0), m_concrete_frames_fetched(0),
      m_current_inlined_depth(UINT32_MAX),
      m_current_inlined_pc(LLDB_INVALID_ADDRESS),
      m_show_inlined_frames(show_inline_frames) {
  if (prev_frames_sp) {
    m_current_inlined_depth = prev_frames_sp->m_current_inlined_depth;
    m_current_inlined_pc = prev_frames_sp->m_current_inlined_pc;
  }
}

StackFrameList::~StackFrameList() {
  // Call clear since this takes a lock and clears the stack frame list in case
  // another thread is currently using this stack frame list
  Clear();
}

void StackFrameList::CalculateCurrentInlinedDepth() {
  uint32_t cur_inlined_depth = GetCurrentInlinedDepth();
  if (cur_inlined_depth == UINT32_MAX) {
    ResetCurrentInlinedDepth();
  }
}

uint32_t StackFrameList::GetCurrentInlinedDepth() {
  if (m_show_inlined_frames && m_current_inlined_pc != LLDB_INVALID_ADDRESS) {
    lldb::addr_t cur_pc = m_thread.GetRegisterContext()->GetPC();
    if (cur_pc != m_current_inlined_pc) {
      m_current_inlined_pc = LLDB_INVALID_ADDRESS;
      m_current_inlined_depth = UINT32_MAX;
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
      if (log && log->GetVerbose())
        log->Printf(
            "GetCurrentInlinedDepth: invalidating current inlined depth.\n");
    }
    return m_current_inlined_depth;
  } else {
    return UINT32_MAX;
  }
}

void StackFrameList::ResetCurrentInlinedDepth() {
  if (!m_show_inlined_frames)
    return;

  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  GetFramesUpTo(0);
  if (m_frames.empty())
    return;
  if (!m_frames[0]->IsInlined()) {
    m_current_inlined_depth = UINT32_MAX;
    m_current_inlined_pc = LLDB_INVALID_ADDRESS;
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
    if (log && log->GetVerbose())
      log->Printf(
          "ResetCurrentInlinedDepth: Invalidating current inlined depth.\n");
    return;
  }

  // We only need to do something special about inlined blocks when we are
  // at the beginning of an inlined function:
  // FIXME: We probably also have to do something special if the PC is at
  // the END of an inlined function, which coincides with the end of either
  // its containing function or another inlined function.

  Block *block_ptr = m_frames[0]->GetFrameBlock();
  if (!block_ptr)
    return;

  Address pc_as_address;
  lldb::addr_t curr_pc = m_thread.GetRegisterContext()->GetPC();
  pc_as_address.SetLoadAddress(curr_pc, &(m_thread.GetProcess()->GetTarget()));
  AddressRange containing_range;
  if (!block_ptr->GetRangeContainingAddress(pc_as_address, containing_range) ||
      pc_as_address != containing_range.GetBaseAddress())
    return;

  // If we got here because of a breakpoint hit, then set the inlined depth
  // depending on where the breakpoint was set. If we got here because of a
  // crash, then set the inlined depth to the deepest most block.  Otherwise,
  // we stopped here naturally as the result of a step, so set ourselves in the
  // containing frame of the whole set of nested inlines, so the user can then
  // "virtually" step into the frames one by one, or next over the whole mess.
  // Note: We don't have to handle being somewhere in the middle of the stack
  // here, since ResetCurrentInlinedDepth doesn't get called if there is a
  // valid inlined depth set.
  StopInfoSP stop_info_sp = m_thread.GetStopInfo();
  if (!stop_info_sp)
    return;
  switch (stop_info_sp->GetStopReason()) {
  case eStopReasonWatchpoint:
  case eStopReasonException:
  case eStopReasonExec:
  case eStopReasonSignal:
    // In all these cases we want to stop in the deepest frame.
    m_current_inlined_pc = curr_pc;
    m_current_inlined_depth = 0;
    break;
  case eStopReasonBreakpoint: {
    // FIXME: Figure out what this break point is doing, and set the inline
    // depth appropriately.  Be careful to take into account breakpoints that
    // implement step over prologue, since that should do the default
    // calculation. For now, if the breakpoints corresponding to this hit are
    // all internal, I set the stop location to the top of the inlined stack,
    // since that will make things like stepping over prologues work right.
    // But if there are any non-internal breakpoints I do to the bottom of the
    // stack, since that was the old behavior.
    uint32_t bp_site_id = stop_info_sp->GetValue();
    BreakpointSiteSP bp_site_sp(
        m_thread.GetProcess()->GetBreakpointSiteList().FindByID(bp_site_id));
    bool all_internal = true;
    if (bp_site_sp) {
      uint32_t num_owners = bp_site_sp->GetNumberOfOwners();
      for (uint32_t i = 0; i < num_owners; i++) {
        Breakpoint &bp_ref = bp_site_sp->GetOwnerAtIndex(i)->GetBreakpoint();
        if (!bp_ref.IsInternal()) {
          all_internal = false;
        }
      }
    }
    if (!all_internal) {
      m_current_inlined_pc = curr_pc;
      m_current_inlined_depth = 0;
      break;
    }
  }
    LLVM_FALLTHROUGH;
  default: {
    // Otherwise, we should set ourselves at the container of the inlining, so
    // that the user can descend into them. So first we check whether we have
    // more than one inlined block sharing this PC:
    int num_inlined_functions = 0;

    for (Block *container_ptr = block_ptr->GetInlinedParent();
         container_ptr != nullptr;
         container_ptr = container_ptr->GetInlinedParent()) {
      if (!container_ptr->GetRangeContainingAddress(pc_as_address,
                                                    containing_range))
        break;
      if (pc_as_address != containing_range.GetBaseAddress())
        break;

      num_inlined_functions++;
    }
    m_current_inlined_pc = curr_pc;
    m_current_inlined_depth = num_inlined_functions + 1;
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
    if (log && log->GetVerbose())
      log->Printf("ResetCurrentInlinedDepth: setting inlined "
                  "depth: %d 0x%" PRIx64 ".\n",
                  m_current_inlined_depth, curr_pc);

    break;
  }
  }
}

bool StackFrameList::DecrementCurrentInlinedDepth() {
  if (m_show_inlined_frames) {
    uint32_t current_inlined_depth = GetCurrentInlinedDepth();
    if (current_inlined_depth != UINT32_MAX) {
      if (current_inlined_depth > 0) {
        m_current_inlined_depth--;
        return true;
      }
    }
  }
  return false;
}

void StackFrameList::SetCurrentInlinedDepth(uint32_t new_depth) {
  m_current_inlined_depth = new_depth;
  if (new_depth == UINT32_MAX)
    m_current_inlined_pc = LLDB_INVALID_ADDRESS;
  else
    m_current_inlined_pc = m_thread.GetRegisterContext()->GetPC();
}

void StackFrameList::GetOnlyConcreteFramesUpTo(uint32_t end_idx,
                                               Unwind *unwinder) {
  assert(m_thread.IsValid() && "Expected valid thread");
  assert(m_frames.size() <= end_idx && "Expected there to be frames to fill");

  if (end_idx < m_concrete_frames_fetched)
    return;

  if (!unwinder)
    return;

  uint32_t num_frames = unwinder->GetFramesUpTo(end_idx);
  if (num_frames <= end_idx + 1) {
    // Done unwinding.
    m_concrete_frames_fetched = UINT32_MAX;
  }

  // Don't create the frames eagerly. Defer this work to GetFrameAtIndex,
  // which can lazily query the unwinder to create frames.
  m_frames.resize(num_frames);
}

/// Find the unique path through the call graph from \p begin (with return PC
/// \p return_pc) to \p end. On success this path is stored into \p path, and 
/// on failure \p path is unchanged.
static void FindInterveningFrames(Function &begin, Function &end,
                                  Target &target, addr_t return_pc,
                                  std::vector<Function *> &path,
                                  ModuleList &images, Log *log) {
  LLDB_LOG(log, "Finding frames between {0} and {1}, retn-pc={2:x}",
           begin.GetDisplayName(), end.GetDisplayName(), return_pc);

  // Find a non-tail calling edge with the correct return PC.
  auto first_level_edges = begin.GetCallEdges();
  if (log)
    for (const CallEdge &edge : first_level_edges)
      LLDB_LOG(log, "FindInterveningFrames: found call with retn-PC = {0:x}",
               edge.GetReturnPCAddress(begin, target));
  auto first_edge_it = std::lower_bound(
      first_level_edges.begin(), first_level_edges.end(), return_pc,
      [&](const CallEdge &edge, addr_t target_pc) {
        return edge.GetReturnPCAddress(begin, target) < target_pc;
      });
  if (first_edge_it == first_level_edges.end() ||
      first_edge_it->GetReturnPCAddress(begin, target) != return_pc) {
    LLDB_LOG(log, "No call edge outgoing from {0} with retn-PC == {1:x}",
             begin.GetDisplayName(), return_pc);
    return;
  }
  CallEdge &first_edge = const_cast<CallEdge &>(*first_edge_it);

  // The first callee may not be resolved, or there may be nothing to fill in.
  Function *first_callee = first_edge.GetCallee(images);
  if (!first_callee) {
    LLDB_LOG(log, "Could not resolve callee");
    return;
  }
  if (first_callee == &end) {
    LLDB_LOG(log, "Not searching further, first callee is {0} (retn-PC: {1:x})",
             end.GetDisplayName(), return_pc);
    return;
  }

  // Run DFS on the tail-calling edges out of the first callee to find \p end.
  // Fully explore the set of functions reachable from the first edge via tail
  // calls in order to detect ambiguous executions.
  struct DFS {
    std::vector<Function *> active_path = {};
    std::vector<Function *> solution_path = {};
    llvm::SmallPtrSet<Function *, 2> visited_nodes = {};
    bool ambiguous = false;
    Function *end;
    ModuleList &images;

    DFS(Function *end, ModuleList &images) : end(end), images(images) {}

    void search(Function *first_callee, std::vector<Function *> &path) {
      dfs(first_callee);
      if (!ambiguous)
        path = std::move(solution_path);
    }

    void dfs(Function *callee) {
      // Found a path to the target function.
      if (callee == end) {
        if (solution_path.empty())
          solution_path = active_path;
        else
          ambiguous = true;
        return;
      }

      // Terminate the search if tail recursion is found, or more generally if
      // there's more than one way to reach a target. This errs on the side of
      // caution: it conservatively stops searching when some solutions are
      // still possible to save time in the average case.
      if (!visited_nodes.insert(callee).second) {
        ambiguous = true;
        return;
      }

      // Search the calls made from this callee.
      active_path.push_back(callee);
      for (CallEdge &edge : callee->GetTailCallingEdges()) {
        Function *next_callee = edge.GetCallee(images);
        if (!next_callee)
          continue;

        dfs(next_callee);
        if (ambiguous)
          return;
      }
      active_path.pop_back();
    }
  };

  DFS(&end, images).search(first_callee, path);
}

/// Given that \p next_frame will be appended to the frame list, synthesize
/// tail call frames between the current end of the list and \p next_frame.
/// If any frames are added, adjust the frame index of \p next_frame.
///
///   --------------
///   |    ...     | <- Completed frames.
///   --------------
///   | prev_frame |
///   --------------
///   |    ...     | <- Artificial frames inserted here.
///   --------------
///   | next_frame |
///   --------------
///   |    ...     | <- Not-yet-visited frames.
///   --------------
void StackFrameList::SynthesizeTailCallFrames(StackFrame &next_frame) {
  TargetSP target_sp = next_frame.CalculateTarget();
  if (!target_sp)
    return;

  lldb::RegisterContextSP next_reg_ctx_sp = next_frame.GetRegisterContext();
  if (!next_reg_ctx_sp)
    return;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));

  assert(!m_frames.empty() && "Cannot synthesize frames in an empty stack");
  StackFrame &prev_frame = *m_frames.back().get();

  // Find the functions prev_frame and next_frame are stopped in. The function
  // objects are needed to search the lazy call graph for intervening frames.
  Function *prev_func =
      prev_frame.GetSymbolContext(eSymbolContextFunction).function;
  if (!prev_func) {
    LLDB_LOG(log, "SynthesizeTailCallFrames: can't find previous function");
    return;
  }
  Function *next_func =
      next_frame.GetSymbolContext(eSymbolContextFunction).function;
  if (!next_func) {
    LLDB_LOG(log, "SynthesizeTailCallFrames: can't find next function");
    return;
  }

  // Try to find the unique sequence of (tail) calls which led from next_frame
  // to prev_frame.
  std::vector<Function *> path;
  addr_t return_pc = next_reg_ctx_sp->GetPC();
  Target &target = *target_sp.get();
  ModuleList &images = next_frame.CalculateTarget()->GetImages();
  FindInterveningFrames(*next_func, *prev_func, target, return_pc, path, images,
                        log);

  // Push synthetic tail call frames.
  for (Function *callee : llvm::reverse(path)) {
    uint32_t frame_idx = m_frames.size();
    uint32_t concrete_frame_idx = next_frame.GetConcreteFrameIndex();
    addr_t cfa = LLDB_INVALID_ADDRESS;
    bool cfa_is_valid = false;
    addr_t pc =
        callee->GetAddressRange().GetBaseAddress().GetLoadAddress(&target);
    SymbolContext sc;
    callee->CalculateSymbolContext(&sc);
    auto synth_frame = std::make_shared<StackFrame>(
        m_thread.shared_from_this(), frame_idx, concrete_frame_idx, cfa,
        cfa_is_valid, pc, StackFrame::Kind::Artificial, &sc);
    m_frames.push_back(synth_frame);
    LLDB_LOG(log, "Pushed frame {0}", callee->GetDisplayName());
  }

  // If any frames were created, adjust next_frame's index.
  if (!path.empty())
    next_frame.SetFrameIndex(m_frames.size());
}

void StackFrameList::GetFramesUpTo(uint32_t end_idx) {
  // Do not fetch frames for an invalid thread.
  if (!m_thread.IsValid())
    return;

  // We've already gotten more frames than asked for, or we've already finished
  // unwinding, return.
  if (m_frames.size() > end_idx || GetAllFramesFetched())
    return;

  Unwind *unwinder = m_thread.GetUnwinder();

  if (!m_show_inlined_frames) {
    GetOnlyConcreteFramesUpTo(end_idx, unwinder);
    return;
  }

#if defined(DEBUG_STACK_FRAMES)
  StreamFile s(stdout, false);
#endif
  // If we are hiding some frames from the outside world, we need to add
  // those onto the total count of frames to fetch.  However, we don't need
  // to do that if end_idx is 0 since in that case we always get the first
  // concrete frame and all the inlined frames below it...  And of course, if
  // end_idx is UINT32_MAX that means get all, so just do that...

  uint32_t inlined_depth = 0;
  if (end_idx > 0 && end_idx != UINT32_MAX) {
    inlined_depth = GetCurrentInlinedDepth();
    if (inlined_depth != UINT32_MAX) {
      if (end_idx > 0)
        end_idx += inlined_depth;
    }
  }

  StackFrameSP unwind_frame_sp;
  do {
    uint32_t idx = m_concrete_frames_fetched++;
    lldb::addr_t pc = LLDB_INVALID_ADDRESS;
    lldb::addr_t cfa = LLDB_INVALID_ADDRESS;
    if (idx == 0) {
      // We might have already created frame zero, only create it if we need
      // to.
      if (m_frames.empty()) {
        RegisterContextSP reg_ctx_sp(m_thread.GetRegisterContext());

        if (reg_ctx_sp) {
          const bool success =
              unwinder && unwinder->GetFrameInfoAtIndex(idx, cfa, pc);
          // There shouldn't be any way not to get the frame info for frame
          // 0. But if the unwinder can't make one, lets make one by hand
          // with the SP as the CFA and see if that gets any further.
          if (!success) {
            cfa = reg_ctx_sp->GetSP();
            pc = reg_ctx_sp->GetPC();
          }

          unwind_frame_sp.reset(new StackFrame(m_thread.shared_from_this(),
                                               m_frames.size(), idx, reg_ctx_sp,
                                               cfa, pc, nullptr));
          m_frames.push_back(unwind_frame_sp);
        }
      } else {
        unwind_frame_sp = m_frames.front();
        cfa = unwind_frame_sp->m_id.GetCallFrameAddress();
      }
    } else {
      const bool success =
          unwinder && unwinder->GetFrameInfoAtIndex(idx, cfa, pc);
      if (!success) {
        // We've gotten to the end of the stack.
        SetAllFramesFetched();
        break;
      }
      const bool cfa_is_valid = true;
      unwind_frame_sp.reset(
          new StackFrame(m_thread.shared_from_this(), m_frames.size(), idx, cfa,
                         cfa_is_valid, pc, StackFrame::Kind::Regular, nullptr));

      // Create synthetic tail call frames between the previous frame and the
      // newly-found frame. The new frame's index may change after this call,
      // although its concrete index will stay the same.
      SynthesizeTailCallFrames(*unwind_frame_sp.get());

      m_frames.push_back(unwind_frame_sp);
    }

    assert(unwind_frame_sp);
    SymbolContext unwind_sc = unwind_frame_sp->GetSymbolContext(
        eSymbolContextBlock | eSymbolContextFunction);
    Block *unwind_block = unwind_sc.block;
    if (unwind_block) {
      Address curr_frame_address(unwind_frame_sp->GetFrameCodeAddress());
      TargetSP target_sp = m_thread.CalculateTarget();
      // Be sure to adjust the frame address to match the address that was
      // used to lookup the symbol context above. If we are in the first
      // concrete frame, then we lookup using the current address, else we
      // decrement the address by one to get the correct location.
      if (idx > 0) {
        if (curr_frame_address.GetOffset() == 0) {
          // If curr_frame_address points to the first address in a section
          // then after adjustment it will point to an other section. In that
          // case resolve the address again to the correct section plus
          // offset form.
          addr_t load_addr = curr_frame_address.GetOpcodeLoadAddress(
              target_sp.get(), AddressClass::eCode);
          curr_frame_address.SetOpcodeLoadAddress(
              load_addr - 1, target_sp.get(), AddressClass::eCode);
        } else {
          curr_frame_address.Slide(-1);
        }
      }

      SymbolContext next_frame_sc;
      Address next_frame_address;

      while (unwind_sc.GetParentOfInlinedScope(
          curr_frame_address, next_frame_sc, next_frame_address)) {
        next_frame_sc.line_entry.ApplyFileMappings(target_sp);
        StackFrameSP frame_sp(
            new StackFrame(m_thread.shared_from_this(), m_frames.size(), idx,
                           unwind_frame_sp->GetRegisterContextSP(), cfa,
                           next_frame_address, &next_frame_sc));

        m_frames.push_back(frame_sp);
        unwind_sc = next_frame_sc;
        curr_frame_address = next_frame_address;
      }
    }
  } while (m_frames.size() - 1 < end_idx);

  // Don't try to merge till you've calculated all the frames in this stack.
  if (GetAllFramesFetched() && m_prev_frames_sp) {
    StackFrameList *prev_frames = m_prev_frames_sp.get();
    StackFrameList *curr_frames = this;

#if defined(DEBUG_STACK_FRAMES)
    s.PutCString("\nprev_frames:\n");
    prev_frames->Dump(&s);
    s.PutCString("\ncurr_frames:\n");
    curr_frames->Dump(&s);
    s.EOL();
#endif
    size_t curr_frame_num, prev_frame_num;

    for (curr_frame_num = curr_frames->m_frames.size(),
        prev_frame_num = prev_frames->m_frames.size();
         curr_frame_num > 0 && prev_frame_num > 0;
         --curr_frame_num, --prev_frame_num) {
      const size_t curr_frame_idx = curr_frame_num - 1;
      const size_t prev_frame_idx = prev_frame_num - 1;
      StackFrameSP curr_frame_sp(curr_frames->m_frames[curr_frame_idx]);
      StackFrameSP prev_frame_sp(prev_frames->m_frames[prev_frame_idx]);

#if defined(DEBUG_STACK_FRAMES)
      s.Printf("\n\nCurr frame #%u ", curr_frame_idx);
      if (curr_frame_sp)
        curr_frame_sp->Dump(&s, true, false);
      else
        s.PutCString("NULL");
      s.Printf("\nPrev frame #%u ", prev_frame_idx);
      if (prev_frame_sp)
        prev_frame_sp->Dump(&s, true, false);
      else
        s.PutCString("NULL");
#endif

      StackFrame *curr_frame = curr_frame_sp.get();
      StackFrame *prev_frame = prev_frame_sp.get();

      if (curr_frame == nullptr || prev_frame == nullptr)
        break;

      // Check the stack ID to make sure they are equal.
      if (curr_frame->GetStackID() != prev_frame->GetStackID())
        break;

      prev_frame->UpdatePreviousFrameFromCurrentFrame(*curr_frame);
      // Now copy the fixed up previous frame into the current frames so the
      // pointer doesn't change.
      m_frames[curr_frame_idx] = prev_frame_sp;

#if defined(DEBUG_STACK_FRAMES)
      s.Printf("\n    Copying previous frame to current frame");
#endif
    }
    // We are done with the old stack frame list, we can release it now.
    m_prev_frames_sp.reset();
  }

#if defined(DEBUG_STACK_FRAMES)
  s.PutCString("\n\nNew frames:\n");
  Dump(&s);
  s.EOL();
#endif
}

uint32_t StackFrameList::GetNumFrames(bool can_create) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (can_create)
    GetFramesUpTo(UINT32_MAX);

  return GetVisibleStackFrameIndex(m_frames.size());
}

void StackFrameList::Dump(Stream *s) {
  if (s == nullptr)
    return;

  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  const_iterator pos, begin = m_frames.begin(), end = m_frames.end();
  for (pos = begin; pos != end; ++pos) {
    StackFrame *frame = (*pos).get();
    s->Printf("%p: ", static_cast<void *>(frame));
    if (frame) {
      frame->GetStackID().Dump(s);
      frame->DumpUsingSettingsFormat(s);
    } else
      s->Printf("frame #%u", (uint32_t)std::distance(begin, pos));
    s->EOL();
  }
  s->EOL();
}

StackFrameSP StackFrameList::GetFrameAtIndex(uint32_t idx) {
  StackFrameSP frame_sp;
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  uint32_t original_idx = idx;

  uint32_t inlined_depth = GetCurrentInlinedDepth();
  if (inlined_depth != UINT32_MAX)
    idx += inlined_depth;

  if (idx < m_frames.size())
    frame_sp = m_frames[idx];

  if (frame_sp)
    return frame_sp;

  // GetFramesUpTo will fill m_frames with as many frames as you asked for, if
  // there are that many.  If there weren't then you asked for too many frames.
  GetFramesUpTo(idx);
  if (idx < m_frames.size()) {
    if (m_show_inlined_frames) {
      // When inline frames are enabled we actually create all the frames in
      // GetFramesUpTo.
      frame_sp = m_frames[idx];
    } else {
      Unwind *unwinder = m_thread.GetUnwinder();
      if (unwinder) {
        addr_t pc, cfa;
        if (unwinder->GetFrameInfoAtIndex(idx, cfa, pc)) {
          const bool cfa_is_valid = true;
          frame_sp.reset(new StackFrame(m_thread.shared_from_this(), idx, idx,
                                        cfa, cfa_is_valid, pc,
                                        StackFrame::Kind::Regular, nullptr));

          Function *function =
              frame_sp->GetSymbolContext(eSymbolContextFunction).function;
          if (function) {
            // When we aren't showing inline functions we always use the top
            // most function block as the scope.
            frame_sp->SetSymbolContextScope(&function->GetBlock(false));
          } else {
            // Set the symbol scope from the symbol regardless if it is nullptr
            // or valid.
            frame_sp->SetSymbolContextScope(
                frame_sp->GetSymbolContext(eSymbolContextSymbol).symbol);
          }
          SetFrameAtIndex(idx, frame_sp);
        }
      }
    }
  } else if (original_idx == 0) {
    // There should ALWAYS be a frame at index 0.  If something went wrong with
    // the CurrentInlinedDepth such that there weren't as many frames as we
    // thought taking that into account, then reset the current inlined depth
    // and return the real zeroth frame.
    if (m_frames.empty()) {
      // Why do we have a thread with zero frames, that should not ever
      // happen...
      assert(!m_thread.IsValid() && "A valid thread has no frames.");
    } else {
      ResetCurrentInlinedDepth();
      frame_sp = m_frames[original_idx];
    }
  }

  return frame_sp;
}

StackFrameSP
StackFrameList::GetFrameWithConcreteFrameIndex(uint32_t unwind_idx) {
  // First try assuming the unwind index is the same as the frame index. The
  // unwind index is always greater than or equal to the frame index, so it is
  // a good place to start. If we have inlined frames we might have 5 concrete
  // frames (frame unwind indexes go from 0-4), but we might have 15 frames
  // after we make all the inlined frames. Most of the time the unwind frame
  // index (or the concrete frame index) is the same as the frame index.
  uint32_t frame_idx = unwind_idx;
  StackFrameSP frame_sp(GetFrameAtIndex(frame_idx));
  while (frame_sp) {
    if (frame_sp->GetFrameIndex() == unwind_idx)
      break;
    frame_sp = GetFrameAtIndex(++frame_idx);
  }
  return frame_sp;
}

static bool CompareStackID(const StackFrameSP &stack_sp,
                           const StackID &stack_id) {
  return stack_sp->GetStackID() < stack_id;
}

StackFrameSP StackFrameList::GetFrameWithStackID(const StackID &stack_id) {
  StackFrameSP frame_sp;

  if (stack_id.IsValid()) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    uint32_t frame_idx = 0;
    // Do a binary search in case the stack frame is already in our cache
    collection::const_iterator begin = m_frames.begin();
    collection::const_iterator end = m_frames.end();
    if (begin != end) {
      collection::const_iterator pos =
          std::lower_bound(begin, end, stack_id, CompareStackID);
      if (pos != end) {
        if ((*pos)->GetStackID() == stack_id)
          return *pos;
      }
    }
    do {
      frame_sp = GetFrameAtIndex(frame_idx);
      if (frame_sp && frame_sp->GetStackID() == stack_id)
        break;
      frame_idx++;
    } while (frame_sp);
  }
  return frame_sp;
}

bool StackFrameList::SetFrameAtIndex(uint32_t idx, StackFrameSP &frame_sp) {
  if (idx >= m_frames.size())
    m_frames.resize(idx + 1);
  // Make sure allocation succeeded by checking bounds again
  if (idx < m_frames.size()) {
    m_frames[idx] = frame_sp;
    return true;
  }
  return false; // resize failed, out of memory?
}

uint32_t StackFrameList::GetSelectedFrameIndex() const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  return m_selected_frame_idx;
}

uint32_t StackFrameList::SetSelectedFrame(lldb_private::StackFrame *frame) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  const_iterator pos;
  const_iterator begin = m_frames.begin();
  const_iterator end = m_frames.end();
  m_selected_frame_idx = 0;
  for (pos = begin; pos != end; ++pos) {
    if (pos->get() == frame) {
      m_selected_frame_idx = std::distance(begin, pos);
      uint32_t inlined_depth = GetCurrentInlinedDepth();
      if (inlined_depth != UINT32_MAX)
        m_selected_frame_idx -= inlined_depth;
      break;
    }
  }
  SetDefaultFileAndLineToSelectedFrame();
  return m_selected_frame_idx;
}

bool StackFrameList::SetSelectedFrameByIndex(uint32_t idx) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  StackFrameSP frame_sp(GetFrameAtIndex(idx));
  if (frame_sp) {
    SetSelectedFrame(frame_sp.get());
    return true;
  } else
    return false;
}

void StackFrameList::SetDefaultFileAndLineToSelectedFrame() {
  if (m_thread.GetID() ==
      m_thread.GetProcess()->GetThreadList().GetSelectedThread()->GetID()) {
    StackFrameSP frame_sp(GetFrameAtIndex(GetSelectedFrameIndex()));
    if (frame_sp) {
      SymbolContext sc = frame_sp->GetSymbolContext(eSymbolContextLineEntry);
      if (sc.line_entry.file)
        m_thread.CalculateTarget()->GetSourceManager().SetDefaultFileAndLine(
            sc.line_entry.file, sc.line_entry.line);
    }
  }
}

// The thread has been run, reset the number stack frames to zero so we can
// determine how many frames we have lazily.
void StackFrameList::Clear() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_frames.clear();
  m_concrete_frames_fetched = 0;
}

void StackFrameList::Merge(std::unique_ptr<StackFrameList> &curr_ap,
                           lldb::StackFrameListSP &prev_sp) {
  std::unique_lock<std::recursive_mutex> current_lock, previous_lock;
  if (curr_ap)
    current_lock = std::unique_lock<std::recursive_mutex>(curr_ap->m_mutex);
  if (prev_sp)
    previous_lock = std::unique_lock<std::recursive_mutex>(prev_sp->m_mutex);

#if defined(DEBUG_STACK_FRAMES)
  StreamFile s(stdout, false);
  s.PutCString("\n\nStackFrameList::Merge():\nPrev:\n");
  if (prev_sp)
    prev_sp->Dump(&s);
  else
    s.PutCString("NULL");
  s.PutCString("\nCurr:\n");
  if (curr_ap)
    curr_ap->Dump(&s);
  else
    s.PutCString("NULL");
  s.EOL();
#endif

  if (!curr_ap || curr_ap->GetNumFrames(false) == 0) {
#if defined(DEBUG_STACK_FRAMES)
    s.PutCString("No current frames, leave previous frames alone...\n");
#endif
    curr_ap.release();
    return;
  }

  if (!prev_sp || prev_sp->GetNumFrames(false) == 0) {
#if defined(DEBUG_STACK_FRAMES)
    s.PutCString("No previous frames, so use current frames...\n");
#endif
    // We either don't have any previous frames, or since we have more than one
    // current frames it means we have all the frames and can safely replace
    // our previous frames.
    prev_sp.reset(curr_ap.release());
    return;
  }

  const uint32_t num_curr_frames = curr_ap->GetNumFrames(false);

  if (num_curr_frames > 1) {
#if defined(DEBUG_STACK_FRAMES)
    s.PutCString(
        "We have more than one current frame, so use current frames...\n");
#endif
    // We have more than one current frames it means we have all the frames and
    // can safely replace our previous frames.
    prev_sp.reset(curr_ap.release());

#if defined(DEBUG_STACK_FRAMES)
    s.PutCString("\nMerged:\n");
    prev_sp->Dump(&s);
#endif
    return;
  }

  StackFrameSP prev_frame_zero_sp(prev_sp->GetFrameAtIndex(0));
  StackFrameSP curr_frame_zero_sp(curr_ap->GetFrameAtIndex(0));
  StackID curr_stack_id(curr_frame_zero_sp->GetStackID());
  StackID prev_stack_id(prev_frame_zero_sp->GetStackID());

#if defined(DEBUG_STACK_FRAMES)
  const uint32_t num_prev_frames = prev_sp->GetNumFrames(false);
  s.Printf("\n%u previous frames with one current frame\n", num_prev_frames);
#endif

  // We have only a single current frame
  // Our previous stack frames only had a single frame as well...
  if (curr_stack_id == prev_stack_id) {
#if defined(DEBUG_STACK_FRAMES)
    s.Printf("\nPrevious frame #0 is same as current frame #0, merge the "
             "cached data\n");
#endif

    curr_frame_zero_sp->UpdateCurrentFrameFromPreviousFrame(
        *prev_frame_zero_sp);
    //        prev_frame_zero_sp->UpdatePreviousFrameFromCurrentFrame
    //        (*curr_frame_zero_sp);
    //        prev_sp->SetFrameAtIndex (0, prev_frame_zero_sp);
  } else if (curr_stack_id < prev_stack_id) {
#if defined(DEBUG_STACK_FRAMES)
    s.Printf("\nCurrent frame #0 has a stack ID that is less than the previous "
             "frame #0, insert current frame zero in front of previous\n");
#endif
    prev_sp->m_frames.insert(prev_sp->m_frames.begin(), curr_frame_zero_sp);
  }

  curr_ap.release();

#if defined(DEBUG_STACK_FRAMES)
  s.PutCString("\nMerged:\n");
  prev_sp->Dump(&s);
#endif
}

lldb::StackFrameSP
StackFrameList::GetStackFrameSPForStackFramePtr(StackFrame *stack_frame_ptr) {
  const_iterator pos;
  const_iterator begin = m_frames.begin();
  const_iterator end = m_frames.end();
  lldb::StackFrameSP ret_sp;

  for (pos = begin; pos != end; ++pos) {
    if (pos->get() == stack_frame_ptr) {
      ret_sp = (*pos);
      break;
    }
  }
  return ret_sp;
}

size_t StackFrameList::GetStatus(Stream &strm, uint32_t first_frame,
                                 uint32_t num_frames, bool show_frame_info,
                                 uint32_t num_frames_with_source,
                                 bool show_unique,
                                 const char *selected_frame_marker) {
  size_t num_frames_displayed = 0;

  if (num_frames == 0)
    return 0;

  StackFrameSP frame_sp;
  uint32_t frame_idx = 0;
  uint32_t last_frame;

  // Don't let the last frame wrap around...
  if (num_frames == UINT32_MAX)
    last_frame = UINT32_MAX;
  else
    last_frame = first_frame + num_frames;

  StackFrameSP selected_frame_sp = m_thread.GetSelectedFrame();
  const char *unselected_marker = nullptr;
  std::string buffer;
  if (selected_frame_marker) {
    size_t len = strlen(selected_frame_marker);
    buffer.insert(buffer.begin(), len, ' ');
    unselected_marker = buffer.c_str();
  }
  const char *marker = nullptr;

  for (frame_idx = first_frame; frame_idx < last_frame; ++frame_idx) {
    frame_sp = GetFrameAtIndex(frame_idx);
    if (!frame_sp)
      break;

    if (selected_frame_marker != nullptr) {
      if (frame_sp == selected_frame_sp)
        marker = selected_frame_marker;
      else
        marker = unselected_marker;
    }

    if (!frame_sp->GetStatus(strm, show_frame_info,
                             num_frames_with_source > (first_frame - frame_idx),
                             show_unique, marker))
      break;
    ++num_frames_displayed;
  }

  strm.IndentLess();
  return num_frames_displayed;
}
