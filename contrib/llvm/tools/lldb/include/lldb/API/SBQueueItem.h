//===-- SBQueueItem.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBQueueItem_h_
#define LLDB_SBQueueItem_h_

#include "lldb/API/SBAddress.h"
#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBQueueItem {
public:
  SBQueueItem();

  SBQueueItem(const lldb::QueueItemSP &queue_item_sp);

  ~SBQueueItem();

  bool IsValid() const;

  void Clear();

  lldb::QueueItemKind GetKind() const;

  void SetKind(lldb::QueueItemKind kind);

  lldb::SBAddress GetAddress() const;

  void SetAddress(lldb::SBAddress addr);

  void SetQueueItem(const lldb::QueueItemSP &queue_item_sp);

  SBThread GetExtendedBacktraceThread(const char *type);

private:
  lldb::QueueItemSP m_queue_item_sp;
};

} // namespace lldb

#endif // LLDB_SBQueueItem_h_
