//===-- RegisterContextMach_arm.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#if defined(__APPLE__)

#include "RegisterContextMach_arm.h"

#include <mach/mach_types.h>
#include <mach/thread_act.h>


using namespace lldb;
using namespace lldb_private;

RegisterContextMach_arm::RegisterContextMach_arm(Thread &thread,
                                                 uint32_t concrete_frame_idx)
    : RegisterContextDarwin_arm(thread, concrete_frame_idx) {}

RegisterContextMach_arm::~RegisterContextMach_arm() {}

int RegisterContextMach_arm::DoReadGPR(lldb::tid_t tid, int flavor, GPR &gpr) {
  mach_msg_type_number_t count = GPRWordCount;
  return ::thread_get_state(tid, flavor, (thread_state_t)&gpr, &count);
}

int RegisterContextMach_arm::DoReadFPU(lldb::tid_t tid, int flavor, FPU &fpu) {
  mach_msg_type_number_t count = FPUWordCount;
  return ::thread_get_state(tid, flavor, (thread_state_t)&fpu, &count);
}

int RegisterContextMach_arm::DoReadEXC(lldb::tid_t tid, int flavor, EXC &exc) {
  mach_msg_type_number_t count = EXCWordCount;
  return ::thread_get_state(tid, flavor, (thread_state_t)&exc, &count);
}

int RegisterContextMach_arm::DoReadDBG(lldb::tid_t tid, int flavor, DBG &dbg) {
  mach_msg_type_number_t count = DBGWordCount;
  return ::thread_get_state(tid, flavor, (thread_state_t)&dbg, &count);
}

int RegisterContextMach_arm::DoWriteGPR(lldb::tid_t tid, int flavor,
                                        const GPR &gpr) {
  return ::thread_set_state(
      tid, flavor, reinterpret_cast<thread_state_t>(const_cast<GPR *>(&gpr)),
      GPRWordCount);
}

int RegisterContextMach_arm::DoWriteFPU(lldb::tid_t tid, int flavor,
                                        const FPU &fpu) {
  return ::thread_set_state(
      tid, flavor, reinterpret_cast<thread_state_t>(const_cast<FPU *>(&fpu)),
      FPUWordCount);
}

int RegisterContextMach_arm::DoWriteEXC(lldb::tid_t tid, int flavor,
                                        const EXC &exc) {
  return ::thread_set_state(
      tid, flavor, reinterpret_cast<thread_state_t>(const_cast<EXC *>(&exc)),
      EXCWordCount);
}

int RegisterContextMach_arm::DoWriteDBG(lldb::tid_t tid, int flavor,
                                        const DBG &dbg) {
  return ::thread_set_state(
      tid, flavor, reinterpret_cast<thread_state_t>(const_cast<DBG *>(&dbg)),
      DBGWordCount);
}

#endif
