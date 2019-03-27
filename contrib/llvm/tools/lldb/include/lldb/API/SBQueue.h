//===-- SBQueue.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBQueue_h_
#define LLDB_SBQueue_h_

#include <vector>

#include "lldb/API/SBDefines.h"
#include "lldb/lldb-forward.h"

namespace lldb {

class LLDB_API SBQueue {
public:
  SBQueue();

  SBQueue(const QueueSP &queue_sp);

  SBQueue(const SBQueue &rhs);

  const SBQueue &operator=(const lldb::SBQueue &rhs);

  ~SBQueue();

  bool IsValid() const;

  void Clear();

  lldb::SBProcess GetProcess();

  lldb::queue_id_t GetQueueID() const;

  const char *GetName() const;

  uint32_t GetIndexID() const;

  uint32_t GetNumThreads();

  lldb::SBThread GetThreadAtIndex(uint32_t);

  uint32_t GetNumPendingItems();

  lldb::SBQueueItem GetPendingItemAtIndex(uint32_t);

  uint32_t GetNumRunningItems();

  lldb::QueueKind GetKind();

protected:
  friend class SBProcess;
  friend class SBThread;

  void SetQueue(const lldb::QueueSP &queue_sp);

  void FetchThreads();

  void FetchItems();

private:
  std::shared_ptr<lldb_private::QueueImpl> m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_SBQueue_h_
