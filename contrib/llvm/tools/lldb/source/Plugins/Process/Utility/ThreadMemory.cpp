//===-- ThreadMemory.cpp ----------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Plugins/Process/Utility/ThreadMemory.h"
#include "Plugins/Process/Utility/RegisterContextThreadMemory.h"
#include "lldb/Target/OperatingSystem.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Unwind.h"

using namespace lldb;
using namespace lldb_private;

ThreadMemory::ThreadMemory(Process &process, tid_t tid,
                           const ValueObjectSP &thread_info_valobj_sp)
    : Thread(process, tid), m_backing_thread_sp(),
      m_thread_info_valobj_sp(thread_info_valobj_sp), m_name(), m_queue() {}

ThreadMemory::ThreadMemory(Process &process, lldb::tid_t tid,
                           llvm::StringRef name, llvm::StringRef queue,
                           lldb::addr_t register_data_addr)
    : Thread(process, tid), m_backing_thread_sp(), m_thread_info_valobj_sp(),
      m_name(name), m_queue(queue), m_register_data_addr(register_data_addr) {}

ThreadMemory::~ThreadMemory() { DestroyThread(); }

void ThreadMemory::WillResume(StateType resume_state) {
  if (m_backing_thread_sp)
    m_backing_thread_sp->WillResume(resume_state);
}

void ThreadMemory::ClearStackFrames() {
  if (m_backing_thread_sp)
    m_backing_thread_sp->ClearStackFrames();
  Thread::ClearStackFrames();
}

RegisterContextSP ThreadMemory::GetRegisterContext() {
  if (!m_reg_context_sp)
    m_reg_context_sp.reset(
        new RegisterContextThreadMemory(*this, m_register_data_addr));
  return m_reg_context_sp;
}

RegisterContextSP
ThreadMemory::CreateRegisterContextForFrame(StackFrame *frame) {
  RegisterContextSP reg_ctx_sp;
  uint32_t concrete_frame_idx = 0;

  if (frame)
    concrete_frame_idx = frame->GetConcreteFrameIndex();

  if (concrete_frame_idx == 0) {
    reg_ctx_sp = GetRegisterContext();
  } else {
    Unwind *unwinder = GetUnwinder();
    if (unwinder != nullptr)
      reg_ctx_sp = unwinder->CreateRegisterContextForFrame(frame);
  }
  return reg_ctx_sp;
}

bool ThreadMemory::CalculateStopInfo() {
  if (m_backing_thread_sp) {
    lldb::StopInfoSP backing_stop_info_sp(
        m_backing_thread_sp->GetPrivateStopInfo());
    if (backing_stop_info_sp &&
        backing_stop_info_sp->IsValidForOperatingSystemThread(*this)) {
      backing_stop_info_sp->SetThread(shared_from_this());
      SetStopInfo(backing_stop_info_sp);
      return true;
    }
  } else {
    ProcessSP process_sp(GetProcess());

    if (process_sp) {
      OperatingSystem *os = process_sp->GetOperatingSystem();
      if (os) {
        SetStopInfo(os->CreateThreadStopReason(this));
        return true;
      }
    }
  }
  return false;
}

void ThreadMemory::RefreshStateAfterStop() {
  if (m_backing_thread_sp)
    return m_backing_thread_sp->RefreshStateAfterStop();

  if (m_reg_context_sp)
    m_reg_context_sp->InvalidateAllRegisters();
}
