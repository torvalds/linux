//===-- StackFrameList.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StackFrameList_h_
#define liblldb_StackFrameList_h_

#include <memory>
#include <mutex>
#include <vector>

#include "lldb/Target/StackFrame.h"

namespace lldb_private {

class StackFrameList {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  StackFrameList(Thread &thread, const lldb::StackFrameListSP &prev_frames_sp,
                 bool show_inline_frames);

  ~StackFrameList();

  /// Get the number of visible frames. Frames may be created if \p can_create
  /// is true. Synthetic (inline) frames expanded from the concrete frame #0
  /// (aka invisible frames) are not included in this count.
  uint32_t GetNumFrames(bool can_create = true);

  /// Get the frame at index \p idx. Invisible frames cannot be indexed.
  lldb::StackFrameSP GetFrameAtIndex(uint32_t idx);

  /// Get the first concrete frame with index greater than or equal to \p idx.
  /// Unlike \ref GetFrameAtIndex, this cannot return a synthetic frame.
  lldb::StackFrameSP GetFrameWithConcreteFrameIndex(uint32_t unwind_idx);

  /// Retrieve the stack frame with the given ID \p stack_id.
  lldb::StackFrameSP GetFrameWithStackID(const StackID &stack_id);

  /// Mark a stack frame as the currently selected frame and return its index.
  uint32_t SetSelectedFrame(lldb_private::StackFrame *frame);

  /// Get the currently selected frame index.
  uint32_t GetSelectedFrameIndex() const;

  /// Mark a stack frame as the currently selected frame using the frame index
  /// \p idx. Like \ref GetFrameAtIndex, invisible frames cannot be selected.
  bool SetSelectedFrameByIndex(uint32_t idx);

  /// If the current inline depth (i.e the number of invisible frames) is valid,
  /// subtract it from \p idx. Otherwise simply return \p idx.
  uint32_t GetVisibleStackFrameIndex(uint32_t idx) {
    if (m_current_inlined_depth < UINT32_MAX)
      return idx - m_current_inlined_depth;
    else
      return idx;
  }

  /// Calculate and set the current inline depth. This may be used to update
  /// the StackFrameList's set of inline frames when execution stops, e.g when
  /// a breakpoint is hit.
  void CalculateCurrentInlinedDepth();

  /// If the currently selected frame comes from the currently selected thread,
  /// point the default file and line of the thread's target to the location
  /// specified by the frame.
  void SetDefaultFileAndLineToSelectedFrame();

  /// Clear the cache of frames.
  void Clear();

  void Dump(Stream *s);

  /// If \p stack_frame_ptr is contained in this StackFrameList, return its
  /// wrapping shared pointer.
  lldb::StackFrameSP
  GetStackFrameSPForStackFramePtr(StackFrame *stack_frame_ptr);

  size_t GetStatus(Stream &strm, uint32_t first_frame, uint32_t num_frames,
                   bool show_frame_info, uint32_t num_frames_with_source,
                   bool show_unique = false,
                   const char *frame_marker = nullptr);

protected:
  friend class Thread;

  bool SetFrameAtIndex(uint32_t idx, lldb::StackFrameSP &frame_sp);

  static void Merge(std::unique_ptr<StackFrameList> &curr_ap,
                    lldb::StackFrameListSP &prev_sp);

  void GetFramesUpTo(uint32_t end_idx);

  void GetOnlyConcreteFramesUpTo(uint32_t end_idx, Unwind *unwinder);

  void SynthesizeTailCallFrames(StackFrame &next_frame);

  bool GetAllFramesFetched() { return m_concrete_frames_fetched == UINT32_MAX; }

  void SetAllFramesFetched() { m_concrete_frames_fetched = UINT32_MAX; }

  bool DecrementCurrentInlinedDepth();

  void ResetCurrentInlinedDepth();

  uint32_t GetCurrentInlinedDepth();

  void SetCurrentInlinedDepth(uint32_t new_depth);

  typedef std::vector<lldb::StackFrameSP> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  /// The thread this frame list describes.
  Thread &m_thread;

  /// The old stack frame list.
  // TODO: The old stack frame list is used to fill in missing frame info
  // heuristically when it's otherwise unavailable (say, because the unwinder
  // fails). We should have stronger checks to make sure that this is a valid
  // source of information.
  lldb::StackFrameListSP m_prev_frames_sp;

  /// A mutex for this frame list.
  // TODO: This mutex may not always be held when required. In particular, uses
  // of the StackFrameList APIs in lldb_private::Thread look suspect. Consider
  // passing around a lock_guard reference to enforce proper locking.
  mutable std::recursive_mutex m_mutex;

  /// A cache of frames. This may need to be updated when the program counter
  /// changes.
  collection m_frames;

  /// The currently selected frame.
  uint32_t m_selected_frame_idx;

  /// The number of concrete frames fetched while filling the frame list. This
  /// is only used when synthetic frames are enabled.
  uint32_t m_concrete_frames_fetched;

  /// The number of synthetic function activations (invisible frames) expanded
  /// from the concrete frame #0 activation.
  // TODO: Use an optional instead of UINT32_MAX to denote invalid values.
  uint32_t m_current_inlined_depth;

  /// The program counter value at the currently selected synthetic activation.
  /// This is only valid if m_current_inlined_depth is valid.
  // TODO: Use an optional instead of UINT32_MAX to denote invalid values.
  lldb::addr_t m_current_inlined_pc;

  /// Whether or not to show synthetic (inline) frames. Immutable.
  const bool m_show_inlined_frames;

private:
  DISALLOW_COPY_AND_ASSIGN(StackFrameList);
};

} // namespace lldb_private

#endif // liblldb_StackFrameList_h_
