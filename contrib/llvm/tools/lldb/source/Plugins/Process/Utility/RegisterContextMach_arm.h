//===-- RegisterContextMach_arm.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextMach_arm_h_
#define liblldb_RegisterContextMach_arm_h_


#include "RegisterContextDarwin_arm.h"

class RegisterContextMach_arm : public RegisterContextDarwin_arm {
public:
  RegisterContextMach_arm(lldb_private::Thread &thread,
                          uint32_t concrete_frame_idx);

  virtual ~RegisterContextMach_arm();

protected:
  virtual int DoReadGPR(lldb::tid_t tid, int flavor, GPR &gpr);

  int DoReadFPU(lldb::tid_t tid, int flavor, FPU &fpu);

  int DoReadEXC(lldb::tid_t tid, int flavor, EXC &exc);

  int DoReadDBG(lldb::tid_t tid, int flavor, DBG &dbg);

  int DoWriteGPR(lldb::tid_t tid, int flavor, const GPR &gpr);

  int DoWriteFPU(lldb::tid_t tid, int flavor, const FPU &fpu);

  int DoWriteEXC(lldb::tid_t tid, int flavor, const EXC &exc);

  int DoWriteDBG(lldb::tid_t tid, int flavor, const DBG &dbg);
};

#endif // liblldb_RegisterContextMach_arm_h_
