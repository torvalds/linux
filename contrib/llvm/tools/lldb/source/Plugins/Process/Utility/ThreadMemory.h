//===-- ThreadMemory.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadMemory_h_
#define liblldb_ThreadMemory_h_

#include <string>

#include "lldb/Target/Thread.h"

class ThreadMemory : public lldb_private::Thread {
public:
  ThreadMemory(lldb_private::Process &process, lldb::tid_t tid,
               const lldb::ValueObjectSP &thread_info_valobj_sp);

  ThreadMemory(lldb_private::Process &process, lldb::tid_t tid,
               llvm::StringRef name, llvm::StringRef queue,
               lldb::addr_t register_data_addr);

  ~ThreadMemory() override;

  lldb::RegisterContextSP GetRegisterContext() override;

  lldb::RegisterContextSP
  CreateRegisterContextForFrame(lldb_private::StackFrame *frame) override;

  bool CalculateStopInfo() override;

  const char *GetInfo() override {
    if (m_backing_thread_sp)
      m_backing_thread_sp->GetInfo();
    return nullptr;
  }

  const char *GetName() override {
    if (!m_name.empty())
      return m_name.c_str();
    if (m_backing_thread_sp)
      m_backing_thread_sp->GetName();
    return nullptr;
  }

  const char *GetQueueName() override {
    if (!m_queue.empty())
      return m_queue.c_str();
    if (m_backing_thread_sp)
      m_backing_thread_sp->GetQueueName();
    return nullptr;
  }

  void WillResume(lldb::StateType resume_state) override;

  void DidResume() override {
    if (m_backing_thread_sp)
      m_backing_thread_sp->DidResume();
  }

  lldb::user_id_t GetProtocolID() const override {
    if (m_backing_thread_sp)
      return m_backing_thread_sp->GetProtocolID();
    return Thread::GetProtocolID();
  }

  void RefreshStateAfterStop() override;

  lldb::ValueObjectSP &GetValueObject() { return m_thread_info_valobj_sp; }

  void ClearStackFrames() override;

  void ClearBackingThread() override { m_backing_thread_sp.reset(); }

  bool SetBackingThread(const lldb::ThreadSP &thread_sp) override {
    // printf ("Thread 0x%llx is being backed by thread 0x%llx\n", GetID(),
    // thread_sp->GetID());
    m_backing_thread_sp = thread_sp;
    return (bool)thread_sp;
  }

  lldb::ThreadSP GetBackingThread() const override {
    return m_backing_thread_sp;
  }

protected:
  bool IsOperatingSystemPluginThread() const override { return true; }

  // If this memory thread is actually represented by a thread from the
  // lldb_private::Process subclass, then fill in the thread here and
  // all APIs will be routed through this thread object. If m_backing_thread_sp
  // is empty, then this thread is simply in memory with no representation
  // through the process plug-in.
  lldb::ThreadSP m_backing_thread_sp;
  lldb::ValueObjectSP m_thread_info_valobj_sp;
  std::string m_name;
  std::string m_queue;
  lldb::addr_t m_register_data_addr;

private:
  DISALLOW_COPY_AND_ASSIGN(ThreadMemory);
};

#endif // liblldb_ThreadMemory_h_
