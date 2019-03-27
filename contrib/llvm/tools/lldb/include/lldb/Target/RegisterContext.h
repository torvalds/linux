//===-- RegisterContext.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContext_h_
#define liblldb_RegisterContext_h_

#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class RegisterContext : public std::enable_shared_from_this<RegisterContext>,
                        public ExecutionContextScope {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  RegisterContext(Thread &thread, uint32_t concrete_frame_idx);

  ~RegisterContext() override;

  void InvalidateIfNeeded(bool force);

  //------------------------------------------------------------------
  // Subclasses must override these functions
  //------------------------------------------------------------------
  virtual void InvalidateAllRegisters() = 0;

  virtual size_t GetRegisterCount() = 0;

  virtual const RegisterInfo *GetRegisterInfoAtIndex(size_t reg) = 0;

  // Detect the register size dynamically.
  uint32_t UpdateDynamicRegisterSize(const lldb_private::ArchSpec &arch,
                                     RegisterInfo *reg_info);

  virtual size_t GetRegisterSetCount() = 0;

  virtual const RegisterSet *GetRegisterSet(size_t reg_set) = 0;

  virtual bool ReadRegister(const RegisterInfo *reg_info,
                            RegisterValue &reg_value) = 0;

  virtual bool WriteRegister(const RegisterInfo *reg_info,
                             const RegisterValue &reg_value) = 0;

  virtual bool ReadAllRegisterValues(lldb::DataBufferSP &data_sp) {
    return false;
  }

  virtual bool WriteAllRegisterValues(const lldb::DataBufferSP &data_sp) {
    return false;
  }

  // These two functions are used to implement "push" and "pop" of register
  // states.  They are used primarily for expression evaluation, where we need
  // to push a new state (storing the old one in data_sp) and then restoring
  // the original state by passing the data_sp we got from ReadAllRegisters to
  // WriteAllRegisterValues. ReadAllRegisters will do what is necessary to
  // return a coherent set of register values for this thread, which may mean
  // e.g. interrupting a thread that is sitting in a kernel trap.  That is a
  // somewhat disruptive operation, so these API's should only be used when
  // this behavior is needed.

  virtual bool
  ReadAllRegisterValues(lldb_private::RegisterCheckpoint &reg_checkpoint);

  virtual bool WriteAllRegisterValues(
      const lldb_private::RegisterCheckpoint &reg_checkpoint);

  bool CopyFromRegisterContext(lldb::RegisterContextSP context);

  //------------------------------------------------------------------
  /// Convert from a given register numbering scheme to the lldb register
  /// numbering scheme
  ///
  /// There may be multiple ways to enumerate the registers for a given
  /// architecture.  ABI references will specify one to be used with
  /// DWARF, the register numberings from process plugin, there may
  /// be a variation used for eh_frame unwind instructions (e.g. on Darwin),
  /// and so on.  Register 5 by itself is meaningless - RegisterKind
  /// enumeration tells you what context that number should be translated as.
  ///
  /// Inside lldb, register numbers are in the eRegisterKindLLDB scheme;
  /// arguments which take a register number should take one in that
  /// scheme.
  ///
  /// eRegisterKindGeneric is a special numbering scheme which gives us
  /// constant values for the pc, frame register, stack register, etc., for
  /// use within lldb.  They may not be defined for all architectures but
  /// it allows generic code to translate these common registers into the
  /// lldb numbering scheme.
  ///
  /// This method translates a given register kind + register number into
  /// the eRegisterKindLLDB register numbering.
  ///
  /// @param [in] kind
  ///     The register numbering scheme (RegisterKind) that the following
  ///     register number is in.
  ///
  /// @param [in] num
  ///     A register number in the 'kind' register numbering scheme.
  ///
  /// @return
  ///     The equivalent register number in the eRegisterKindLLDB
  ///     numbering scheme, if possible, else LLDB_INVALID_REGNUM.
  //------------------------------------------------------------------
  virtual uint32_t ConvertRegisterKindToRegisterNumber(lldb::RegisterKind kind,
                                                       uint32_t num) = 0;

  //------------------------------------------------------------------
  // Subclasses can override these functions if desired
  //------------------------------------------------------------------
  virtual uint32_t NumSupportedHardwareBreakpoints();

  virtual uint32_t SetHardwareBreakpoint(lldb::addr_t addr, size_t size);

  virtual bool ClearHardwareBreakpoint(uint32_t hw_idx);

  virtual uint32_t NumSupportedHardwareWatchpoints();

  virtual uint32_t SetHardwareWatchpoint(lldb::addr_t addr, size_t size,
                                         bool read, bool write);

  virtual bool ClearHardwareWatchpoint(uint32_t hw_index);

  virtual bool HardwareSingleStep(bool enable);

  virtual Status
  ReadRegisterValueFromMemory(const lldb_private::RegisterInfo *reg_info,
                              lldb::addr_t src_addr, uint32_t src_len,
                              RegisterValue &reg_value);

  virtual Status
  WriteRegisterValueToMemory(const lldb_private::RegisterInfo *reg_info,
                             lldb::addr_t dst_addr, uint32_t dst_len,
                             const RegisterValue &reg_value);

  //------------------------------------------------------------------
  // Subclasses should not override these
  //------------------------------------------------------------------
  virtual lldb::tid_t GetThreadID() const;

  virtual Thread &GetThread() { return m_thread; }

  const RegisterInfo *GetRegisterInfoByName(llvm::StringRef reg_name,
                                            uint32_t start_idx = 0);

  const RegisterInfo *GetRegisterInfo(lldb::RegisterKind reg_kind,
                                      uint32_t reg_num);

  uint64_t GetPC(uint64_t fail_value = LLDB_INVALID_ADDRESS);

  bool SetPC(uint64_t pc);

  bool SetPC(Address addr);

  uint64_t GetSP(uint64_t fail_value = LLDB_INVALID_ADDRESS);

  bool SetSP(uint64_t sp);

  uint64_t GetFP(uint64_t fail_value = LLDB_INVALID_ADDRESS);

  bool SetFP(uint64_t fp);

  const char *GetRegisterName(uint32_t reg);

  uint64_t GetReturnAddress(uint64_t fail_value = LLDB_INVALID_ADDRESS);

  uint64_t GetFlags(uint64_t fail_value = 0);

  uint64_t ReadRegisterAsUnsigned(uint32_t reg, uint64_t fail_value);

  uint64_t ReadRegisterAsUnsigned(const RegisterInfo *reg_info,
                                  uint64_t fail_value);

  bool WriteRegisterFromUnsigned(uint32_t reg, uint64_t uval);

  bool WriteRegisterFromUnsigned(const RegisterInfo *reg_info, uint64_t uval);

  bool ConvertBetweenRegisterKinds(lldb::RegisterKind source_rk,
                                   uint32_t source_regnum,
                                   lldb::RegisterKind target_rk,
                                   uint32_t &target_regnum);

  //------------------------------------------------------------------
  // lldb::ExecutionContextScope pure virtual functions
  //------------------------------------------------------------------
  lldb::TargetSP CalculateTarget() override;

  lldb::ProcessSP CalculateProcess() override;

  lldb::ThreadSP CalculateThread() override;

  lldb::StackFrameSP CalculateStackFrame() override;

  void CalculateExecutionContext(ExecutionContext &exe_ctx) override;

  uint32_t GetStopID() const { return m_stop_id; }

  void SetStopID(uint32_t stop_id) { m_stop_id = stop_id; }

protected:
  //------------------------------------------------------------------
  // Classes that inherit from RegisterContext can see and modify these
  //------------------------------------------------------------------
  Thread &m_thread; // The thread that this register context belongs to.
  uint32_t m_concrete_frame_idx; // The concrete frame index for this register
                                 // context
  uint32_t m_stop_id; // The stop ID that any data in this context is valid for
private:
  //------------------------------------------------------------------
  // For RegisterContext only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(RegisterContext);
};

} // namespace lldb_private

#endif // liblldb_RegisterContext_h_
