//===-- RegisterContextDarwin_x86_64.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextDarwin_x86_64_h_
#define liblldb_RegisterContextDarwin_x86_64_h_

#include "lldb/Target/RegisterContext.h"
#include "lldb/lldb-private.h"

class RegisterContextDarwin_x86_64 : public lldb_private::RegisterContext {
public:
  RegisterContextDarwin_x86_64(lldb_private::Thread &thread,
                               uint32_t concrete_frame_idx);

  ~RegisterContextDarwin_x86_64() override;

  void InvalidateAllRegisters() override;

  size_t GetRegisterCount() override;

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(size_t reg) override;

  size_t GetRegisterSetCount() override;

  const lldb_private::RegisterSet *GetRegisterSet(size_t set) override;

  bool ReadRegister(const lldb_private::RegisterInfo *reg_info,
                    lldb_private::RegisterValue &value) override;

  bool WriteRegister(const lldb_private::RegisterInfo *reg_info,
                     const lldb_private::RegisterValue &value) override;

  bool ReadAllRegisterValues(lldb::DataBufferSP &data_sp) override;

  bool WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) override;

  uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                               uint32_t num) override;

  bool HardwareSingleStep(bool enable) override;

  struct GPR {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cs;
    uint64_t fs;
    uint64_t gs;
  };

  struct MMSReg {
    uint8_t bytes[10];
    uint8_t pad[6];
  };

  struct XMMReg {
    uint8_t bytes[16];
  };

  struct FPU {
    uint32_t pad[2];
    uint16_t fcw; // "fctrl"
    uint16_t fsw; // "fstat"
    uint8_t ftw;  // "ftag"
    uint8_t pad1;
    uint16_t fop; // "fop"
    uint32_t ip;  // "fioff"
    uint16_t cs;  // "fiseg"
    uint16_t pad2;
    uint32_t dp; // "fooff"
    uint16_t ds; // "foseg"
    uint16_t pad3;
    uint32_t mxcsr;
    uint32_t mxcsrmask;
    MMSReg stmm[8];
    XMMReg xmm[16];
    uint8_t pad4[6 * 16];
    int pad5;
  };

  struct EXC {
    uint32_t trapno;
    uint32_t err;
    uint64_t faultvaddr;
  };

protected:
  enum { GPRRegSet = 4, FPURegSet = 5, EXCRegSet = 6 };

  enum {
    GPRWordCount = sizeof(GPR) / sizeof(uint32_t),
    FPUWordCount = sizeof(FPU) / sizeof(uint32_t),
    EXCWordCount = sizeof(EXC) / sizeof(uint32_t)
  };

  enum { Read = 0, Write = 1, kNumErrors = 2 };

  GPR gpr;
  FPU fpu;
  EXC exc;
  int gpr_errs[2]; // Read/Write errors
  int fpu_errs[2]; // Read/Write errors
  int exc_errs[2]; // Read/Write errors

  void InvalidateAllRegisterStates() {
    SetError(GPRRegSet, Read, -1);
    SetError(FPURegSet, Read, -1);
    SetError(EXCRegSet, Read, -1);
  }

  int GetError(int flavor, uint32_t err_idx) const {
    if (err_idx < kNumErrors) {
      switch (flavor) {
      // When getting all errors, just OR all values together to see if
      // we got any kind of error.
      case GPRRegSet:
        return gpr_errs[err_idx];
      case FPURegSet:
        return fpu_errs[err_idx];
      case EXCRegSet:
        return exc_errs[err_idx];
      default:
        break;
      }
    }
    return -1;
  }

  bool SetError(int flavor, uint32_t err_idx, int err) {
    if (err_idx < kNumErrors) {
      switch (flavor) {
      case GPRRegSet:
        gpr_errs[err_idx] = err;
        return true;

      case FPURegSet:
        fpu_errs[err_idx] = err;
        return true;

      case EXCRegSet:
        exc_errs[err_idx] = err;
        return true;

      default:
        break;
      }
    }
    return false;
  }

  bool RegisterSetIsCached(int set) const { return GetError(set, Read) == 0; }

  void LogGPR(lldb_private::Log *log, const char *format, ...);

  int ReadGPR(bool force);

  int ReadFPU(bool force);

  int ReadEXC(bool force);

  int WriteGPR();

  int WriteFPU();

  int WriteEXC();

  // Subclasses override these to do the actual reading.
  virtual int DoReadGPR(lldb::tid_t tid, int flavor, GPR &gpr) = 0;

  virtual int DoReadFPU(lldb::tid_t tid, int flavor, FPU &fpu) = 0;

  virtual int DoReadEXC(lldb::tid_t tid, int flavor, EXC &exc) = 0;

  virtual int DoWriteGPR(lldb::tid_t tid, int flavor, const GPR &gpr) = 0;

  virtual int DoWriteFPU(lldb::tid_t tid, int flavor, const FPU &fpu) = 0;

  virtual int DoWriteEXC(lldb::tid_t tid, int flavor, const EXC &exc) = 0;

  int ReadRegisterSet(uint32_t set, bool force);

  int WriteRegisterSet(uint32_t set);

  static uint32_t GetRegisterNumber(uint32_t reg_kind, uint32_t reg_num);

  static int GetSetForNativeRegNum(int reg_num);

  static size_t GetRegisterInfosCount();

  static const lldb_private::RegisterInfo *GetRegisterInfos();
};

#endif // liblldb_RegisterContextDarwin_x86_64_h_
