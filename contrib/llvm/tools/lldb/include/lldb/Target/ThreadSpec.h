//===-- ThreadSpec.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadSpec_h_
#define liblldb_ThreadSpec_h_

#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-private.h"
#include <string>

namespace lldb_private {

// Note: For now the thread spec has only fixed elements -
//   Thread ID
//   Thread Index
//   Thread Name
//   Thread Queue Name
//
//  But if we need more generality, we can hang a key/value map off of this
//  structure.
//  That's why the thread matches spec test is done as a virtual method in
//  Thread::MatchesSpec,
//  since it is the native thread that would know how to interpret the keys.
//  I was going to do the Queue Name this way out of sheer orneriness, but that
//  seems a
//  sufficiently general concept, so I put it in here on its own.

class ThreadSpec {
public:
  ThreadSpec();

  ThreadSpec(const ThreadSpec &rhs);

  const ThreadSpec &operator=(const ThreadSpec &rhs);

  static std::unique_ptr<ThreadSpec>
  CreateFromStructuredData(const StructuredData::Dictionary &data_dict,
                           Status &error);

  StructuredData::ObjectSP SerializeToStructuredData();

  static const char *GetSerializationKey() { return "ThreadSpec"; }

  void SetIndex(uint32_t index) { m_index = index; }

  void SetTID(lldb::tid_t tid) { m_tid = tid; }

  void SetName(llvm::StringRef name) { m_name = name; }

  void SetQueueName(llvm::StringRef queue_name) { m_queue_name = queue_name; }

  uint32_t GetIndex() const { return m_index; }

  lldb::tid_t GetTID() const { return m_tid; }

  const char *GetName() const;

  const char *GetQueueName() const;

  bool TIDMatches(lldb::tid_t thread_id) const {
    if (m_tid == LLDB_INVALID_THREAD_ID || thread_id == LLDB_INVALID_THREAD_ID)
      return true;
    else
      return thread_id == m_tid;
  }

  bool TIDMatches(Thread &thread) const;

  bool IndexMatches(uint32_t index) const {
    if (m_index == UINT32_MAX || index == UINT32_MAX)
      return true;
    else
      return index == m_index;
  }

  bool IndexMatches(Thread &thread) const;

  bool NameMatches(const char *name) const {
    if (m_name.empty())
      return true;
    else if (name == nullptr)
      return false;
    else
      return m_name == name;
  }

  bool NameMatches(Thread &thread) const;

  bool QueueNameMatches(const char *queue_name) const {
    if (m_queue_name.empty())
      return true;
    else if (queue_name == nullptr)
      return false;
    else
      return m_queue_name == queue_name;
  }

  bool QueueNameMatches(Thread &thread) const;

  bool ThreadPassesBasicTests(Thread &thread) const;

  bool HasSpecification() const;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) const;

private:
  enum class OptionNames {
    ThreadIndex = 0,
    ThreadID,
    ThreadName,
    QueueName,
    LastOptionName
  };
  static const char *g_option_names[(size_t)OptionNames::LastOptionName];

  static const char *GetKey(OptionNames enum_value) {
    return g_option_names[(size_t) enum_value];
  }

  uint32_t m_index;
  lldb::tid_t m_tid;
  std::string m_name;
  std::string m_queue_name;
};

} // namespace lldb_private

#endif // liblldb_ThreadSpec_h_
