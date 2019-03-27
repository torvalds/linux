//===-- Queue.h ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Queue_h_
#define liblldb_Queue_h_

#include <string>
#include <vector>

#include "lldb/Target/QueueItem.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//------------------------------------------------------------------
// Queue:
// This class represents a libdispatch aka Grand Central Dispatch queue in the
// process.
//
// A program using libdispatch will create queues, put work items
// (functions, blocks) on the queues.  The system will create / reassign
// pthreads to execute the work items for the queues.  A serial queue will be
// associated with a single thread (or possibly no thread, if it is not doing
// any work).  A concurrent queue may be associated with multiple threads.
//------------------------------------------------------------------

class Queue : public std::enable_shared_from_this<Queue> {
public:
  Queue(lldb::ProcessSP process_sp, lldb::queue_id_t queue_id,
        const char *queue_name);

  ~Queue();

  //------------------------------------------------------------------
  /// Get the QueueID for this Queue
  ///
  /// A 64-bit ID number that uniquely identifies a queue at this particular
  /// stop_id.  Currently the libdispatch serialnum is used for the QueueID;
  /// it is a number that starts at 1 for each process and increments with
  /// each queue.  A serialnum is not reused for a different queue in the
  /// lifetime of that process execution.
  ///
  /// @return
  ///     The QueueID for this Queue.
  //------------------------------------------------------------------
  lldb::queue_id_t GetID();

  //------------------------------------------------------------------
  /// Get the name of this Queue
  ///
  /// @return
  ///     The name of the queue, if one is available.
  ///     A NULL pointer is returned if none is available.
  //------------------------------------------------------------------
  const char *GetName();

  //------------------------------------------------------------------
  /// Get the IndexID for this Queue
  ///
  /// This is currently the same as GetID().  If it changes in the future,
  /// it will be  a small integer value (starting with 1) assigned to
  /// each queue that is seen during a Process lifetime.
  ///
  /// Both the GetID and GetIndexID are being retained for Queues to
  /// maintain similar API to the Thread class, and allow for the
  /// possibility of GetID changing to a different source in the future.
  ///
  /// @return
  ///     The IndexID for this queue.
  //------------------------------------------------------------------
  uint32_t GetIndexID();

  //------------------------------------------------------------------
  /// Return the threads currently associated with this queue
  ///
  /// Zero, one, or many threads may be executing code for a queue at
  /// a given point in time.  This call returns the list of threads
  /// that are currently executing work for this queue.
  ///
  /// @return
  ///     The threads currently performing work for this queue
  //------------------------------------------------------------------
  std::vector<lldb::ThreadSP> GetThreads();

  //------------------------------------------------------------------
  /// Return the items that are currently enqueued
  ///
  /// "Enqueued" means that the item has been added to the queue to
  /// be done, but has not yet been done.  When the item is going to
  /// be processed it is "dequeued".
  ///
  /// @return
  ///     The vector of enqueued items for this queue
  //------------------------------------------------------------------
  const std::vector<lldb::QueueItemSP> &GetPendingItems();

  lldb::ProcessSP GetProcess() const { return m_process_wp.lock(); }

  //------------------------------------------------------------------
  /// Get the number of work items that this queue is currently running
  ///
  /// @return
  ///     The number of work items currently executing.  For a serial
  ///     queue, this will be 0 or 1.  For a concurrent queue, this
  ///     may be any number.
  //------------------------------------------------------------------
  uint32_t GetNumRunningWorkItems() const;

  //------------------------------------------------------------------
  /// Get the number of work items enqueued on this queue
  ///
  /// @return
  ///     The number of work items currently enqueued, waiting to
  ///     execute.
  //------------------------------------------------------------------
  uint32_t GetNumPendingWorkItems() const;

  //------------------------------------------------------------------
  /// Get the dispatch_queue_t structure address for this Queue
  ///
  /// Get the address in the inferior process' memory of this Queue's
  /// dispatch_queue_t structure.
  ///
  /// @return
  ///     The address of the dispatch_queue_t structure, if known.
  ///     LLDB_INVALID_ADDRESS will be returned if it is unavailable.
  //------------------------------------------------------------------
  lldb::addr_t GetLibdispatchQueueAddress() const;

  void SetNumRunningWorkItems(uint32_t count);

  void SetNumPendingWorkItems(uint32_t count);

  void SetLibdispatchQueueAddress(lldb::addr_t dispatch_queue_t_addr);

  void PushPendingQueueItem(lldb::QueueItemSP item) {
    m_pending_items.push_back(item);
  }

  //------------------------------------------------------------------
  /// Return the kind (serial, concurrent) of this queue
  ///
  /// @return
  //      Whether this is a serial or a concurrent queue
  //------------------------------------------------------------------
  lldb::QueueKind GetKind();

  void SetKind(lldb::QueueKind kind);

private:
  //------------------------------------------------------------------
  // For Queue only
  //------------------------------------------------------------------

  lldb::ProcessWP m_process_wp;
  lldb::queue_id_t m_queue_id;
  std::string m_queue_name;
  uint32_t m_running_work_items_count;
  uint32_t m_pending_work_items_count;
  std::vector<lldb::QueueItemSP> m_pending_items;
  lldb::addr_t m_dispatch_queue_t_addr; // address of libdispatch
                                        // dispatch_queue_t for this Queue
  lldb::QueueKind m_kind;

  DISALLOW_COPY_AND_ASSIGN(Queue);
};

} // namespace lldb_private

#endif // liblldb_Queue_h_
