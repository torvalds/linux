//===-- ThreadList.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadList_h_
#define liblldb_ThreadList_h_

#include <mutex>
#include <vector>

#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadCollection.h"
#include "lldb/Utility/Iterable.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

// This is a thread list with lots of functionality for use only by the process
// for which this is the thread list.  A generic container class with iterator
// functionality is ThreadCollection.
class ThreadList : public ThreadCollection {
  friend class Process;

public:
  ThreadList(Process *process);

  ThreadList(const ThreadList &rhs);

  ~ThreadList() override;

  const ThreadList &operator=(const ThreadList &rhs);

  uint32_t GetSize(bool can_update = true);

  // Return the selected thread if there is one.  Otherwise, return the thread
  // selected at index 0.
  lldb::ThreadSP GetSelectedThread();

  // Manage the thread to use for running expressions.  This is usually the
  // Selected thread, but sometimes (e.g. when evaluating breakpoint conditions
  // & stop hooks) it isn't.
  class ExpressionExecutionThreadPusher {
  public:
    ExpressionExecutionThreadPusher(ThreadList &thread_list, lldb::tid_t tid)
        : m_thread_list(&thread_list), m_tid(tid) {
      m_thread_list->PushExpressionExecutionThread(m_tid);
    }

    ExpressionExecutionThreadPusher(lldb::ThreadSP thread_sp);

    ~ExpressionExecutionThreadPusher() {
      if (m_thread_list && m_tid != LLDB_INVALID_THREAD_ID)
        m_thread_list->PopExpressionExecutionThread(m_tid);
    }

  private:
    ThreadList *m_thread_list;
    lldb::tid_t m_tid;
  };

  lldb::ThreadSP GetExpressionExecutionThread();

protected:
  void PushExpressionExecutionThread(lldb::tid_t tid);

  void PopExpressionExecutionThread(lldb::tid_t tid);

public:
  bool SetSelectedThreadByID(lldb::tid_t tid, bool notify = false);

  bool SetSelectedThreadByIndexID(uint32_t index_id, bool notify = false);

  void Clear();

  void Flush();

  void Destroy();

  // Note that "idx" is not the same as the "thread_index". It is a zero based
  // index to accessing the current threads, whereas "thread_index" is a unique
  // index assigned
  lldb::ThreadSP GetThreadAtIndex(uint32_t idx, bool can_update = true);

  lldb::ThreadSP FindThreadByID(lldb::tid_t tid, bool can_update = true);

  lldb::ThreadSP FindThreadByProtocolID(lldb::tid_t tid,
                                        bool can_update = true);

  lldb::ThreadSP RemoveThreadByID(lldb::tid_t tid, bool can_update = true);

  lldb::ThreadSP RemoveThreadByProtocolID(lldb::tid_t tid,
                                          bool can_update = true);

  lldb::ThreadSP FindThreadByIndexID(uint32_t index_id, bool can_update = true);

  lldb::ThreadSP GetThreadSPForThreadPtr(Thread *thread_ptr);

  lldb::ThreadSP GetBackingThread(const lldb::ThreadSP &real_thread);

  bool ShouldStop(Event *event_ptr);

  Vote ShouldReportStop(Event *event_ptr);

  Vote ShouldReportRun(Event *event_ptr);

  void RefreshStateAfterStop();

  //------------------------------------------------------------------
  /// The thread list asks tells all the threads it is about to resume.
  /// If a thread can "resume" without having to resume the target, it
  /// will return false for WillResume, and then the process will not be
  /// restarted.
  ///
  /// @return
  ///    \b true instructs the process to resume normally,
  ///    \b false means start & stopped events will be generated, but
  ///    the process will not actually run.  The thread must then return
  ///    the correct StopInfo when asked.
  ///
  //------------------------------------------------------------------
  bool WillResume();

  void DidResume();

  void DidStop();

  void DiscardThreadPlans();

  uint32_t GetStopID() const;

  void SetStopID(uint32_t stop_id);

  std::recursive_mutex &GetMutex() const override;

  void Update(ThreadList &rhs);

protected:
  void SetShouldReportStop(Vote vote);

  void NotifySelectedThreadChanged(lldb::tid_t tid);

  //------------------------------------------------------------------
  // Classes that inherit from Process can see and modify these
  //------------------------------------------------------------------
  Process *m_process; ///< The process that manages this thread list.
  uint32_t
      m_stop_id; ///< The process stop ID that this thread list is valid for.
  lldb::tid_t
      m_selected_tid; ///< For targets that need the notion of a current thread.
  std::vector<lldb::tid_t> m_expression_tid_stack;

private:
  ThreadList();
};

} // namespace lldb_private

#endif // liblldb_ThreadList_h_
