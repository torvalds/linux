//===-- UnwindLLDB.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Module.h"
#include "lldb/Symbol/FuncUnwinders.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Log.h"

#include "RegisterContextLLDB.h"
#include "UnwindLLDB.h"

using namespace lldb;
using namespace lldb_private;

UnwindLLDB::UnwindLLDB(Thread &thread)
    : Unwind(thread), m_frames(), m_unwind_complete(false),
      m_user_supplied_trap_handler_functions() {
  ProcessSP process_sp(thread.GetProcess());
  if (process_sp) {
    Args args;
    process_sp->GetTarget().GetUserSpecifiedTrapHandlerNames(args);
    size_t count = args.GetArgumentCount();
    for (size_t i = 0; i < count; i++) {
      const char *func_name = args.GetArgumentAtIndex(i);
      m_user_supplied_trap_handler_functions.push_back(ConstString(func_name));
    }
  }
}

uint32_t UnwindLLDB::DoGetFrameCount() {
  if (!m_unwind_complete) {
//#define DEBUG_FRAME_SPEED 1
#if DEBUG_FRAME_SPEED
#define FRAME_COUNT 10000
    using namespace std::chrono;
    auto time_value = steady_clock::now();
#endif
    if (!AddFirstFrame())
      return 0;

    ProcessSP process_sp(m_thread.GetProcess());
    ABI *abi = process_sp ? process_sp->GetABI().get() : NULL;

    while (AddOneMoreFrame(abi)) {
#if DEBUG_FRAME_SPEED
      if ((m_frames.size() % FRAME_COUNT) == 0) {
        const auto now = steady_clock::now();
        const auto delta_t = now - time_value;
        printf("%u frames in %.9f ms (%g frames/sec)\n", FRAME_COUNT,
               duration<double, std::milli>(delta_t).count(),
               (float)FRAME_COUNT / duration<double>(delta_t).count());
        time_value = now;
      }
#endif
    }
  }
  return m_frames.size();
}

bool UnwindLLDB::AddFirstFrame() {
  if (m_frames.size() > 0)
    return true;

  ProcessSP process_sp(m_thread.GetProcess());
  ABI *abi = process_sp ? process_sp->GetABI().get() : NULL;

  // First, set up the 0th (initial) frame
  CursorSP first_cursor_sp(new Cursor());
  RegisterContextLLDBSP reg_ctx_sp(new RegisterContextLLDB(
      m_thread, RegisterContextLLDBSP(), first_cursor_sp->sctx, 0, *this));
  if (reg_ctx_sp.get() == NULL)
    goto unwind_done;

  if (!reg_ctx_sp->IsValid())
    goto unwind_done;

  if (!reg_ctx_sp->GetCFA(first_cursor_sp->cfa))
    goto unwind_done;

  if (!reg_ctx_sp->ReadPC(first_cursor_sp->start_pc))
    goto unwind_done;

  // Everything checks out, so release the auto pointer value and let the
  // cursor own it in its shared pointer
  first_cursor_sp->reg_ctx_lldb_sp = reg_ctx_sp;
  m_frames.push_back(first_cursor_sp);

  // Update the Full Unwind Plan for this frame if not valid
  UpdateUnwindPlanForFirstFrameIfInvalid(abi);

  return true;

unwind_done:
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
  if (log) {
    log->Printf("th%d Unwind of this thread is complete.",
                m_thread.GetIndexID());
  }
  m_unwind_complete = true;
  return false;
}

UnwindLLDB::CursorSP UnwindLLDB::GetOneMoreFrame(ABI *abi) {
  assert(m_frames.size() != 0 &&
         "Get one more frame called with empty frame list");

  // If we've already gotten to the end of the stack, don't bother to try
  // again...
  if (m_unwind_complete)
    return nullptr;

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));

  CursorSP prev_frame = m_frames.back();
  uint32_t cur_idx = m_frames.size();

  CursorSP cursor_sp(new Cursor());
  RegisterContextLLDBSP reg_ctx_sp(new RegisterContextLLDB(
      m_thread, prev_frame->reg_ctx_lldb_sp, cursor_sp->sctx, cur_idx, *this));

  uint64_t max_stack_depth = m_thread.GetMaxBacktraceDepth();

  // We want to detect an unwind that cycles erroneously and stop backtracing.
  // Don't want this maximum unwind limit to be too low -- if you have a
  // backtrace with an "infinitely recursing" bug, it will crash when the stack
  // blows out and the first 35,000 frames are uninteresting - it's the top
  // most 5 frames that you actually care about.  So you can't just cap the
  // unwind at 10,000 or something. Realistically anything over around 200,000
  // is going to blow out the stack space. If we're still unwinding at that
  // point, we're probably never going to finish.
  if (cur_idx >= max_stack_depth) {
    if (log)
      log->Printf("%*sFrame %d unwound too many frames, assuming unwind has "
                  "gone astray, stopping.",
                  cur_idx < 100 ? cur_idx : 100, "", cur_idx);
    return nullptr;
  }

  if (reg_ctx_sp.get() == NULL) {
    // If the RegisterContextLLDB has a fallback UnwindPlan, it will switch to
    // that and return true.  Subsequent calls to TryFallbackUnwindPlan() will
    // return false.
    if (prev_frame->reg_ctx_lldb_sp->TryFallbackUnwindPlan()) {
      // TryFallbackUnwindPlan for prev_frame succeeded and updated
      // reg_ctx_lldb_sp field of prev_frame. However, cfa field of prev_frame
      // still needs to be updated. Hence updating it.
      if (!(prev_frame->reg_ctx_lldb_sp->GetCFA(prev_frame->cfa)))
        return nullptr;

      return GetOneMoreFrame(abi);
    }

    if (log)
      log->Printf("%*sFrame %d did not get a RegisterContext, stopping.",
                  cur_idx < 100 ? cur_idx : 100, "", cur_idx);
    return nullptr;
  }

  if (!reg_ctx_sp->IsValid()) {
    // We failed to get a valid RegisterContext. See if the regctx below this
    // on the stack has a fallback unwind plan it can use. Subsequent calls to
    // TryFallbackUnwindPlan() will return false.
    if (prev_frame->reg_ctx_lldb_sp->TryFallbackUnwindPlan()) {
      // TryFallbackUnwindPlan for prev_frame succeeded and updated
      // reg_ctx_lldb_sp field of prev_frame. However, cfa field of prev_frame
      // still needs to be updated. Hence updating it.
      if (!(prev_frame->reg_ctx_lldb_sp->GetCFA(prev_frame->cfa)))
        return nullptr;

      return GetOneMoreFrame(abi);
    }

    if (log)
      log->Printf("%*sFrame %d invalid RegisterContext for this frame, "
                  "stopping stack walk",
                  cur_idx < 100 ? cur_idx : 100, "", cur_idx);
    return nullptr;
  }
  if (!reg_ctx_sp->GetCFA(cursor_sp->cfa)) {
    // If the RegisterContextLLDB has a fallback UnwindPlan, it will switch to
    // that and return true.  Subsequent calls to TryFallbackUnwindPlan() will
    // return false.
    if (prev_frame->reg_ctx_lldb_sp->TryFallbackUnwindPlan()) {
      // TryFallbackUnwindPlan for prev_frame succeeded and updated
      // reg_ctx_lldb_sp field of prev_frame. However, cfa field of prev_frame
      // still needs to be updated. Hence updating it.
      if (!(prev_frame->reg_ctx_lldb_sp->GetCFA(prev_frame->cfa)))
        return nullptr;

      return GetOneMoreFrame(abi);
    }

    if (log)
      log->Printf(
          "%*sFrame %d did not get CFA for this frame, stopping stack walk",
          cur_idx < 100 ? cur_idx : 100, "", cur_idx);
    return nullptr;
  }
  if (abi && !abi->CallFrameAddressIsValid(cursor_sp->cfa)) {
    // On Mac OS X, the _sigtramp asynchronous signal trampoline frame may not
    // have its (constructed) CFA aligned correctly -- don't do the abi
    // alignment check for these.
    if (!reg_ctx_sp->IsTrapHandlerFrame()) {
      // See if we can find a fallback unwind plan for THIS frame.  It may be
      // that the UnwindPlan we're using for THIS frame was bad and gave us a
      // bad CFA. If that's not it, then see if we can change the UnwindPlan
      // for the frame below us ("NEXT") -- see if using that other UnwindPlan
      // gets us a better unwind state.
      if (!reg_ctx_sp->TryFallbackUnwindPlan() ||
          !reg_ctx_sp->GetCFA(cursor_sp->cfa) ||
          !abi->CallFrameAddressIsValid(cursor_sp->cfa)) {
        if (prev_frame->reg_ctx_lldb_sp->TryFallbackUnwindPlan()) {
          // TryFallbackUnwindPlan for prev_frame succeeded and updated
          // reg_ctx_lldb_sp field of prev_frame. However, cfa field of
          // prev_frame still needs to be updated. Hence updating it.
          if (!(prev_frame->reg_ctx_lldb_sp->GetCFA(prev_frame->cfa)))
            return nullptr;

          return GetOneMoreFrame(abi);
        }

        if (log)
          log->Printf("%*sFrame %d did not get a valid CFA for this frame, "
                      "stopping stack walk",
                      cur_idx < 100 ? cur_idx : 100, "", cur_idx);
        return nullptr;
      } else {
        if (log)
          log->Printf("%*sFrame %d had a bad CFA value but we switched the "
                      "UnwindPlan being used and got one that looks more "
                      "realistic.",
                      cur_idx < 100 ? cur_idx : 100, "", cur_idx);
      }
    }
  }
  if (!reg_ctx_sp->ReadPC(cursor_sp->start_pc)) {
    // If the RegisterContextLLDB has a fallback UnwindPlan, it will switch to
    // that and return true.  Subsequent calls to TryFallbackUnwindPlan() will
    // return false.
    if (prev_frame->reg_ctx_lldb_sp->TryFallbackUnwindPlan()) {
      // TryFallbackUnwindPlan for prev_frame succeeded and updated
      // reg_ctx_lldb_sp field of prev_frame. However, cfa field of prev_frame
      // still needs to be updated. Hence updating it.
      if (!(prev_frame->reg_ctx_lldb_sp->GetCFA(prev_frame->cfa)))
        return nullptr;

      return GetOneMoreFrame(abi);
    }

    if (log)
      log->Printf(
          "%*sFrame %d did not get PC for this frame, stopping stack walk",
          cur_idx < 100 ? cur_idx : 100, "", cur_idx);
    return nullptr;
  }
  if (abi && !abi->CodeAddressIsValid(cursor_sp->start_pc)) {
    // If the RegisterContextLLDB has a fallback UnwindPlan, it will switch to
    // that and return true.  Subsequent calls to TryFallbackUnwindPlan() will
    // return false.
    if (prev_frame->reg_ctx_lldb_sp->TryFallbackUnwindPlan()) {
      // TryFallbackUnwindPlan for prev_frame succeeded and updated
      // reg_ctx_lldb_sp field of prev_frame. However, cfa field of prev_frame
      // still needs to be updated. Hence updating it.
      if (!(prev_frame->reg_ctx_lldb_sp->GetCFA(prev_frame->cfa)))
        return nullptr;

      return GetOneMoreFrame(abi);
    }

    if (log)
      log->Printf("%*sFrame %d did not get a valid PC, stopping stack walk",
                  cur_idx < 100 ? cur_idx : 100, "", cur_idx);
    return nullptr;
  }
  // Infinite loop where the current cursor is the same as the previous one...
  if (prev_frame->start_pc == cursor_sp->start_pc &&
      prev_frame->cfa == cursor_sp->cfa) {
    if (log)
      log->Printf("th%d pc of this frame is the same as the previous frame and "
                  "CFAs for both frames are identical -- stopping unwind",
                  m_thread.GetIndexID());
    return nullptr;
  }

  cursor_sp->reg_ctx_lldb_sp = reg_ctx_sp;
  return cursor_sp;
}

void UnwindLLDB::UpdateUnwindPlanForFirstFrameIfInvalid(ABI *abi) {
  // This function is called for First Frame only.
  assert(m_frames.size() == 1 && "No. of cursor frames are not 1");

  bool old_m_unwind_complete = m_unwind_complete;
  CursorSP old_m_candidate_frame = m_candidate_frame;

  // Try to unwind 2 more frames using the Unwinder. It uses Full UnwindPlan
  // and if Full UnwindPlan fails, then uses FallBack UnwindPlan. Also update
  // the cfa of Frame 0 (if required).
  AddOneMoreFrame(abi);

  // Remove all the frames added by above function as the purpose of using
  // above function was just to check whether Unwinder of Frame 0 works or not.
  for (uint32_t i = 1; i < m_frames.size(); i++)
    m_frames.pop_back();

  // Restore status after calling AddOneMoreFrame
  m_unwind_complete = old_m_unwind_complete;
  m_candidate_frame = old_m_candidate_frame;
  return;
}

bool UnwindLLDB::AddOneMoreFrame(ABI *abi) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));

  // Frame zero is a little different
  if (m_frames.empty())
    return false;

  // If we've already gotten to the end of the stack, don't bother to try
  // again...
  if (m_unwind_complete)
    return false;

  CursorSP new_frame = m_candidate_frame;
  if (new_frame == nullptr)
    new_frame = GetOneMoreFrame(abi);

  if (new_frame == nullptr) {
    if (log)
      log->Printf("th%d Unwind of this thread is complete.",
                  m_thread.GetIndexID());
    m_unwind_complete = true;
    return false;
  }

  m_frames.push_back(new_frame);

  // If we can get one more frame further then accept that we get back a
  // correct frame.
  m_candidate_frame = GetOneMoreFrame(abi);
  if (m_candidate_frame)
    return true;

  // We can't go further from the frame returned by GetOneMore frame. Lets try
  // to get a different frame with using the fallback unwind plan.
  if (!m_frames[m_frames.size() - 2]
           ->reg_ctx_lldb_sp->TryFallbackUnwindPlan()) {
    // We don't have a valid fallback unwind plan. Accept the frame as it is.
    // This is a valid situation when we are at the bottom of the stack.
    return true;
  }

  // Remove the possibly incorrect frame from the frame list and try to add a
  // different one with the newly selected fallback unwind plan.
  m_frames.pop_back();
  CursorSP new_frame_v2 = GetOneMoreFrame(abi);
  if (new_frame_v2 == nullptr) {
    // We haven't got a new frame from the fallback unwind plan. Accept the
    // frame from the original unwind plan. This is a valid situation when we
    // are at the bottom of the stack.
    m_frames.push_back(new_frame);
    return true;
  }

  // Push the new frame to the list and try to continue from this frame. If we
  // can get a new frame then accept it as the correct one.
  m_frames.push_back(new_frame_v2);
  m_candidate_frame = GetOneMoreFrame(abi);
  if (m_candidate_frame) {
    // If control reached here then TryFallbackUnwindPlan had succeeded for
    // Cursor::m_frames[m_frames.size() - 2]. It also succeeded to Unwind next
    // 2 frames i.e. m_frames[m_frames.size() - 1] and a frame after that. For
    // Cursor::m_frames[m_frames.size() - 2], reg_ctx_lldb_sp field was already
    // updated during TryFallbackUnwindPlan call above. However, cfa field
    // still needs to be updated. Hence updating it here and then returning.
    return m_frames[m_frames.size() - 2]->reg_ctx_lldb_sp->GetCFA(
        m_frames[m_frames.size() - 2]->cfa);
  }

  // The new frame hasn't helped in unwinding. Fall back to the original one as
  // the default unwind plan is usually more reliable then the fallback one.
  m_frames.pop_back();
  m_frames.push_back(new_frame);
  return true;
}

bool UnwindLLDB::DoGetFrameInfoAtIndex(uint32_t idx, addr_t &cfa, addr_t &pc) {
  if (m_frames.size() == 0) {
    if (!AddFirstFrame())
      return false;
  }

  ProcessSP process_sp(m_thread.GetProcess());
  ABI *abi = process_sp ? process_sp->GetABI().get() : NULL;

  while (idx >= m_frames.size() && AddOneMoreFrame(abi))
    ;

  if (idx < m_frames.size()) {
    cfa = m_frames[idx]->cfa;
    pc = m_frames[idx]->start_pc;
    return true;
  }
  return false;
}

lldb::RegisterContextSP
UnwindLLDB::DoCreateRegisterContextForFrame(StackFrame *frame) {
  lldb::RegisterContextSP reg_ctx_sp;
  uint32_t idx = frame->GetConcreteFrameIndex();

  if (idx == 0) {
    return m_thread.GetRegisterContext();
  }

  if (m_frames.size() == 0) {
    if (!AddFirstFrame())
      return reg_ctx_sp;
  }

  ProcessSP process_sp(m_thread.GetProcess());
  ABI *abi = process_sp ? process_sp->GetABI().get() : NULL;

  while (idx >= m_frames.size()) {
    if (!AddOneMoreFrame(abi))
      break;
  }

  const uint32_t num_frames = m_frames.size();
  if (idx < num_frames) {
    Cursor *frame_cursor = m_frames[idx].get();
    reg_ctx_sp = frame_cursor->reg_ctx_lldb_sp;
  }
  return reg_ctx_sp;
}

UnwindLLDB::RegisterContextLLDBSP
UnwindLLDB::GetRegisterContextForFrameNum(uint32_t frame_num) {
  RegisterContextLLDBSP reg_ctx_sp;
  if (frame_num < m_frames.size())
    reg_ctx_sp = m_frames[frame_num]->reg_ctx_lldb_sp;
  return reg_ctx_sp;
}

bool UnwindLLDB::SearchForSavedLocationForRegister(
    uint32_t lldb_regnum, lldb_private::UnwindLLDB::RegisterLocation &regloc,
    uint32_t starting_frame_num, bool pc_reg) {
  int64_t frame_num = starting_frame_num;
  if (static_cast<size_t>(frame_num) >= m_frames.size())
    return false;

  // Never interrogate more than one level while looking for the saved pc
  // value. If the value isn't saved by frame_num, none of the frames lower on
  // the stack will have a useful value.
  if (pc_reg) {
    UnwindLLDB::RegisterSearchResult result;
    result = m_frames[frame_num]->reg_ctx_lldb_sp->SavedLocationForRegister(
        lldb_regnum, regloc);
    return result == UnwindLLDB::RegisterSearchResult::eRegisterFound;
  }
  while (frame_num >= 0) {
    UnwindLLDB::RegisterSearchResult result;
    result = m_frames[frame_num]->reg_ctx_lldb_sp->SavedLocationForRegister(
        lldb_regnum, regloc);

    // We descended down to the live register context aka stack frame 0 and are
    // reading the value out of a live register.
    if (result == UnwindLLDB::RegisterSearchResult::eRegisterFound &&
        regloc.type ==
            UnwindLLDB::RegisterLocation::eRegisterInLiveRegisterContext) {
      return true;
    }

    // If we have unwind instructions saying that register N is saved in
    // register M in the middle of the stack (and N can equal M here, meaning
    // the register was not used in this function), then change the register
    // number we're looking for to M and keep looking for a concrete  location
    // down the stack, or an actual value from a live RegisterContext at frame
    // 0.
    if (result == UnwindLLDB::RegisterSearchResult::eRegisterFound &&
        regloc.type == UnwindLLDB::RegisterLocation::eRegisterInRegister &&
        frame_num > 0) {
      result = UnwindLLDB::RegisterSearchResult::eRegisterNotFound;
      lldb_regnum = regloc.location.register_number;
    }

    if (result == UnwindLLDB::RegisterSearchResult::eRegisterFound)
      return true;
    if (result == UnwindLLDB::RegisterSearchResult::eRegisterIsVolatile)
      return false;
    frame_num--;
  }
  return false;
}
