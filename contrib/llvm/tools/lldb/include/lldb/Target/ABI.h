//===-- ABI.h ---------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ABI_h_
#define liblldb_ABI_h_

#include "lldb/Core/PluginInterface.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/ArrayRef.h"

// forward define the llvm::Type class
namespace llvm {
class Type;
}

namespace lldb_private {

class ABI : public PluginInterface {
public:
  struct CallArgument {
    enum eType {
      HostPointer = 0, /* pointer to host data */
      TargetValue,     /* value is on the target or literal */
    };
    eType type;  /* value of eType */
    size_t size; /* size in bytes of this argument */

    lldb::addr_t value;                 /* literal value */
    std::unique_ptr<uint8_t[]> data_ap; /* host data pointer */
  };

  ~ABI() override;

  virtual size_t GetRedZoneSize() const = 0;

  virtual bool PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                                  lldb::addr_t functionAddress,
                                  lldb::addr_t returnAddress,
                                  llvm::ArrayRef<lldb::addr_t> args) const = 0;

  // Prepare trivial call used from ThreadPlanFunctionCallUsingABI
  // AD:
  //  . Because i don't want to change other ABI's this is not declared pure
  //  virtual.
  //    The dummy implementation will simply fail.  Only HexagonABI will
  //    currently
  //    use this method.
  //  . Two PrepareTrivialCall's is not good design so perhaps this should be
  //  combined.
  //
  virtual bool PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                                  lldb::addr_t functionAddress,
                                  lldb::addr_t returnAddress,
                                  llvm::Type &prototype,
                                  llvm::ArrayRef<CallArgument> args) const;

  virtual bool GetArgumentValues(Thread &thread, ValueList &values) const = 0;

  lldb::ValueObjectSP GetReturnValueObject(Thread &thread, CompilerType &type,
                                           bool persistent = true) const;

  // specialized to work with llvm IR types
  lldb::ValueObjectSP GetReturnValueObject(Thread &thread, llvm::Type &type,
                                           bool persistent = true) const;

  // Set the Return value object in the current frame as though a function with
  virtual Status SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                      lldb::ValueObjectSP &new_value) = 0;

protected:
  // This is the method the ABI will call to actually calculate the return
  // value. Don't put it in a persistent value object, that will be done by the
  // ABI::GetReturnValueObject.
  virtual lldb::ValueObjectSP
  GetReturnValueObjectImpl(Thread &thread, CompilerType &ast_type) const = 0;

  // specialized to work with llvm IR types
  virtual lldb::ValueObjectSP
  GetReturnValueObjectImpl(Thread &thread, llvm::Type &ir_type) const;

  //------------------------------------------------------------------
  /// Request to get a Process shared pointer.
  ///
  /// This ABI object may not have been created with a Process object,
  /// or the Process object may no longer be alive.  Be sure to handle
  /// the case where the shared pointer returned does not have an
  /// object inside it.
  //------------------------------------------------------------------
  lldb::ProcessSP GetProcessSP() const { return m_process_wp.lock(); }

public:
  virtual bool CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) = 0;

  virtual bool CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) = 0;

  virtual bool RegisterIsVolatile(const RegisterInfo *reg_info) = 0;

  virtual bool
  GetFallbackRegisterLocation(const RegisterInfo *reg_info,
                              UnwindPlan::Row::RegisterLocation &unwind_regloc);

  // Should take a look at a call frame address (CFA) which is just the stack
  // pointer value upon entry to a function. ABIs usually impose alignment
  // restrictions (4, 8 or 16 byte aligned), and zero is usually not allowed.
  // This function should return true if "cfa" is valid call frame address for
  // the ABI, and false otherwise. This is used by the generic stack frame
  // unwinding code to help determine when a stack ends.
  virtual bool CallFrameAddressIsValid(lldb::addr_t cfa) = 0;

  // Validates a possible PC value and returns true if an opcode can be at
  // "pc".
  virtual bool CodeAddressIsValid(lldb::addr_t pc) = 0;

  virtual lldb::addr_t FixCodeAddress(lldb::addr_t pc) {
    // Some targets might use bits in a code address to indicate a mode switch.
    // ARM uses bit zero to signify a code address is thumb, so any ARM ABI
    // plug-ins would strip those bits.
    return pc;
  }

  virtual const RegisterInfo *GetRegisterInfoArray(uint32_t &count) = 0;

  bool GetRegisterInfoByName(const ConstString &name, RegisterInfo &info);

  bool GetRegisterInfoByKind(lldb::RegisterKind reg_kind, uint32_t reg_num,
                             RegisterInfo &info);

  virtual bool GetPointerReturnRegister(const char *&name) { return false; }

  static lldb::ABISP FindPlugin(lldb::ProcessSP process_sp, const ArchSpec &arch);

protected:
  //------------------------------------------------------------------
  // Classes that inherit from ABI can see and modify these
  //------------------------------------------------------------------
  ABI(lldb::ProcessSP process_sp) {
    if (process_sp.get())
        m_process_wp = process_sp;
  }

  lldb::ProcessWP m_process_wp;

private:
  DISALLOW_COPY_AND_ASSIGN(ABI);
};

} // namespace lldb_private

#endif // liblldb_ABI_h_
