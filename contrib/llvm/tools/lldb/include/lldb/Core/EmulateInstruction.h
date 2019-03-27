//===-- EmulateInstruction.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_EmulateInstruction_h_
#define lldb_EmulateInstruction_h_

#include <string>

#include "lldb/Core/Address.h"
#include "lldb/Core/Opcode.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-private-types.h"
#include "lldb/lldb-types.h"

#include <stddef.h>
#include <stdint.h>
namespace lldb_private {
class OptionValueDictionary;
}
namespace lldb_private {
class RegisterContext;
}
namespace lldb_private {
class RegisterValue;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class Target;
}
namespace lldb_private {
class UnwindPlan;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class EmulateInstruction EmulateInstruction.h
/// "lldb/Core/EmulateInstruction.h"
/// A class that allows emulation of CPU opcodes.
///
/// This class is a plug-in interface that is accessed through the standard
/// static FindPlugin function call in the EmulateInstruction class. The
/// FindPlugin takes a target triple and returns a new object if there is a
/// plug-in that supports the architecture and OS. Four callbacks and a baton
/// are provided. The four callbacks are read register, write register, read
/// memory and write memory.
///
/// This class is currently designed for these main use cases: - Auto
/// generation of Call Frame Information (CFI) from assembly code - Predicting
/// single step breakpoint locations - Emulating instructions for breakpoint
/// traps
///
/// Objects can be asked to read an instruction which will cause a call to the
/// read register callback to get the PC, followed by a read memory call to
/// read the opcode. If ReadInstruction () returns true, then a call to
/// EmulateInstruction::EvaluateInstruction () can be made. At this point the
/// EmulateInstruction subclass will use all of the callbacks to emulate an
/// instruction.
///
/// Clients that provide the callbacks can either do the read/write
/// registers/memory to actually emulate the instruction on a real or virtual
/// CPU, or watch for the EmulateInstruction::Context which is context for the
/// read/write register/memory which explains why the callback is being
/// called. Examples of a context are: "pushing register 3 onto the stack at
/// offset -12", or "adjusting stack pointer by -16". This extra context
/// allows the generation of
/// CFI information from assembly code without having to actually do
/// the read/write register/memory.
///
/// Clients must be prepared that not all instructions for an Instruction Set
/// Architecture (ISA) will be emulated.
///
/// Subclasses at the very least should implement the instructions that save
/// and restore registers onto the stack and adjustment to the stack pointer.
/// By just implementing a few instructions for an ISA that are the typical
/// prologue opcodes, you can then generate CFI using a class that will soon
/// be available.
///
/// Implementing all of the instructions that affect the PC can then allow
/// single step prediction support.
///
/// Implementing all of the instructions allows for emulation of opcodes for
/// breakpoint traps and will pave the way for "thread centric" debugging. The
/// current debugging model is "process centric" where all threads must be
/// stopped when any thread is stopped; when hitting software breakpoints we
/// must disable the breakpoint by restoring the original breakpoint opcode,
/// single stepping and restoring the breakpoint trap. If all threads were
/// allowed to run then other threads could miss the breakpoint.
///
/// This class centralizes the code that usually is done in separate code
/// paths in a debugger (single step prediction, finding save restore
/// locations of registers for unwinding stack frame variables) and emulating
/// the instruction is just a bonus.
//----------------------------------------------------------------------

class EmulateInstruction : public PluginInterface {
public:
  static EmulateInstruction *FindPlugin(const ArchSpec &arch,
                                        InstructionType supported_inst_type,
                                        const char *plugin_name);

  enum ContextType {
    eContextInvalid = 0,
    // Read an instruction opcode from memory
    eContextReadOpcode,

    // Usually used for writing a register value whose source value is an
    // immediate
    eContextImmediate,

    // Exclusively used when saving a register to the stack as part of the
    // prologue
    eContextPushRegisterOnStack,

    // Exclusively used when restoring a register off the stack as part of the
    // epilogue
    eContextPopRegisterOffStack,

    // Add or subtract a value from the stack
    eContextAdjustStackPointer,

    // Adjust the frame pointer for the current frame
    eContextSetFramePointer,

    // Typically in an epilogue sequence.  Copy the frame pointer back into the
    // stack pointer, use SP for CFA calculations again.
    eContextRestoreStackPointer,

    // Add or subtract a value from a base address register (other than SP)
    eContextAdjustBaseRegister,

    // Add or subtract a value from the PC or store a value to the PC.
    eContextAdjustPC,

    // Used in WriteRegister callbacks to indicate where the
    eContextRegisterPlusOffset,

    // Used in WriteMemory callback to indicate where the data came from
    eContextRegisterStore,

    eContextRegisterLoad,

    // Used when performing a PC-relative branch where the
    eContextRelativeBranchImmediate,

    // Used when performing an absolute branch where the
    eContextAbsoluteBranchRegister,

    // Used when performing a supervisor call to an operating system to provide
    // a service:
    eContextSupervisorCall,

    // Used when performing a MemU operation to read the PC-relative offset
    // from an address.
    eContextTableBranchReadMemory,

    // Used when random bits are written into a register
    eContextWriteRegisterRandomBits,

    // Used when random bits are written to memory
    eContextWriteMemoryRandomBits,

    eContextArithmetic,

    eContextAdvancePC,

    eContextReturnFromException
  };

  enum InfoType {
    eInfoTypeRegisterPlusOffset,
    eInfoTypeRegisterPlusIndirectOffset,
    eInfoTypeRegisterToRegisterPlusOffset,
    eInfoTypeRegisterToRegisterPlusIndirectOffset,
    eInfoTypeRegisterRegisterOperands,
    eInfoTypeOffset,
    eInfoTypeRegister,
    eInfoTypeImmediate,
    eInfoTypeImmediateSigned,
    eInfoTypeAddress,
    eInfoTypeISAAndImmediate,
    eInfoTypeISAAndImmediateSigned,
    eInfoTypeISA,
    eInfoTypeNoArgs
  } InfoType;

  struct Context {
    ContextType type;
    enum InfoType info_type;
    union {
      struct RegisterPlusOffset {
        RegisterInfo reg;      // base register
        int64_t signed_offset; // signed offset added to base register
      } RegisterPlusOffset;

      struct RegisterPlusIndirectOffset {
        RegisterInfo base_reg;   // base register number
        RegisterInfo offset_reg; // offset register kind
      } RegisterPlusIndirectOffset;

      struct RegisterToRegisterPlusOffset {
        RegisterInfo data_reg; // source/target register for data
        RegisterInfo base_reg; // base register for address calculation
        int64_t offset;        // offset for address calculation
      } RegisterToRegisterPlusOffset;

      struct RegisterToRegisterPlusIndirectOffset {
        RegisterInfo base_reg;   // base register for address calculation
        RegisterInfo offset_reg; // offset register for address calculation
        RegisterInfo data_reg;   // source/target register for data
      } RegisterToRegisterPlusIndirectOffset;

      struct RegisterRegisterOperands {
        RegisterInfo
            operand1; // register containing first operand for binary op
        RegisterInfo
            operand2; // register containing second operand for binary op
      } RegisterRegisterOperands;

      int64_t signed_offset; // signed offset by which to adjust self (for
                             // registers only)

      RegisterInfo reg; // plain register

      uint64_t unsigned_immediate; // unsigned immediate value
      int64_t signed_immediate;    // signed immediate value

      lldb::addr_t address; // direct address

      struct ISAAndImmediate {
        uint32_t isa;
        uint32_t unsigned_data32; // immediate data
      } ISAAndImmediate;

      struct ISAAndImmediateSigned {
        uint32_t isa;
        int32_t signed_data32; // signed immediate data
      } ISAAndImmediateSigned;

      uint32_t isa;
    } info;

    Context() : type(eContextInvalid), info_type(eInfoTypeNoArgs) {}

    void SetRegisterPlusOffset(RegisterInfo base_reg, int64_t signed_offset) {
      info_type = eInfoTypeRegisterPlusOffset;
      info.RegisterPlusOffset.reg = base_reg;
      info.RegisterPlusOffset.signed_offset = signed_offset;
    }

    void SetRegisterPlusIndirectOffset(RegisterInfo base_reg,
                                       RegisterInfo offset_reg) {
      info_type = eInfoTypeRegisterPlusIndirectOffset;
      info.RegisterPlusIndirectOffset.base_reg = base_reg;
      info.RegisterPlusIndirectOffset.offset_reg = offset_reg;
    }

    void SetRegisterToRegisterPlusOffset(RegisterInfo data_reg,
                                         RegisterInfo base_reg,
                                         int64_t offset) {
      info_type = eInfoTypeRegisterToRegisterPlusOffset;
      info.RegisterToRegisterPlusOffset.data_reg = data_reg;
      info.RegisterToRegisterPlusOffset.base_reg = base_reg;
      info.RegisterToRegisterPlusOffset.offset = offset;
    }

    void SetRegisterToRegisterPlusIndirectOffset(RegisterInfo base_reg,
                                                 RegisterInfo offset_reg,
                                                 RegisterInfo data_reg) {
      info_type = eInfoTypeRegisterToRegisterPlusIndirectOffset;
      info.RegisterToRegisterPlusIndirectOffset.base_reg = base_reg;
      info.RegisterToRegisterPlusIndirectOffset.offset_reg = offset_reg;
      info.RegisterToRegisterPlusIndirectOffset.data_reg = data_reg;
    }

    void SetRegisterRegisterOperands(RegisterInfo op1_reg,
                                     RegisterInfo op2_reg) {
      info_type = eInfoTypeRegisterRegisterOperands;
      info.RegisterRegisterOperands.operand1 = op1_reg;
      info.RegisterRegisterOperands.operand2 = op2_reg;
    }

    void SetOffset(int64_t signed_offset) {
      info_type = eInfoTypeOffset;
      info.signed_offset = signed_offset;
    }

    void SetRegister(RegisterInfo reg) {
      info_type = eInfoTypeRegister;
      info.reg = reg;
    }

    void SetImmediate(uint64_t immediate) {
      info_type = eInfoTypeImmediate;
      info.unsigned_immediate = immediate;
    }

    void SetImmediateSigned(int64_t signed_immediate) {
      info_type = eInfoTypeImmediateSigned;
      info.signed_immediate = signed_immediate;
    }

    void SetAddress(lldb::addr_t address) {
      info_type = eInfoTypeAddress;
      info.address = address;
    }
    void SetISAAndImmediate(uint32_t isa, uint32_t data) {
      info_type = eInfoTypeISAAndImmediate;
      info.ISAAndImmediate.isa = isa;
      info.ISAAndImmediate.unsigned_data32 = data;
    }

    void SetISAAndImmediateSigned(uint32_t isa, int32_t data) {
      info_type = eInfoTypeISAAndImmediateSigned;
      info.ISAAndImmediateSigned.isa = isa;
      info.ISAAndImmediateSigned.signed_data32 = data;
    }

    void SetISA(uint32_t isa) {
      info_type = eInfoTypeISA;
      info.isa = isa;
    }

    void SetNoArgs() { info_type = eInfoTypeNoArgs; }

    void Dump(Stream &s, EmulateInstruction *instruction) const;
  };

  typedef size_t (*ReadMemoryCallback)(EmulateInstruction *instruction,
                                       void *baton, const Context &context,
                                       lldb::addr_t addr, void *dst,
                                       size_t length);

  typedef size_t (*WriteMemoryCallback)(EmulateInstruction *instruction,
                                        void *baton, const Context &context,
                                        lldb::addr_t addr, const void *dst,
                                        size_t length);

  typedef bool (*ReadRegisterCallback)(EmulateInstruction *instruction,
                                       void *baton,
                                       const RegisterInfo *reg_info,
                                       RegisterValue &reg_value);

  typedef bool (*WriteRegisterCallback)(EmulateInstruction *instruction,
                                        void *baton, const Context &context,
                                        const RegisterInfo *reg_info,
                                        const RegisterValue &reg_value);

  // Type to represent the condition of an instruction. The UINT32 value is
  // reserved for the unconditional case and all other value can be used in an
  // architecture dependent way.
  typedef uint32_t InstructionCondition;
  static const InstructionCondition UnconditionalCondition = UINT32_MAX;

  EmulateInstruction(const ArchSpec &arch);

  ~EmulateInstruction() override = default;

  //----------------------------------------------------------------------
  // Mandatory overrides
  //----------------------------------------------------------------------
  virtual bool
  SupportsEmulatingInstructionsOfType(InstructionType inst_type) = 0;

  virtual bool SetTargetTriple(const ArchSpec &arch) = 0;

  virtual bool ReadInstruction() = 0;

  virtual bool EvaluateInstruction(uint32_t evaluate_options) = 0;

  virtual InstructionCondition GetInstructionCondition() {
    return UnconditionalCondition;
  }

  virtual bool TestEmulation(Stream *out_stream, ArchSpec &arch,
                             OptionValueDictionary *test_data) = 0;

  virtual bool GetRegisterInfo(lldb::RegisterKind reg_kind, uint32_t reg_num,
                               RegisterInfo &reg_info) = 0;

  //----------------------------------------------------------------------
  // Optional overrides
  //----------------------------------------------------------------------
  virtual bool SetInstruction(const Opcode &insn_opcode,
                              const Address &inst_addr, Target *target);

  virtual bool CreateFunctionEntryUnwind(UnwindPlan &unwind_plan);

  static const char *TranslateRegister(lldb::RegisterKind reg_kind,
                                       uint32_t reg_num, std::string &reg_name);

  //----------------------------------------------------------------------
  // RegisterInfo variants
  //----------------------------------------------------------------------
  bool ReadRegister(const RegisterInfo *reg_info, RegisterValue &reg_value);

  uint64_t ReadRegisterUnsigned(const RegisterInfo *reg_info,
                                uint64_t fail_value, bool *success_ptr);

  bool WriteRegister(const Context &context, const RegisterInfo *ref_info,
                     const RegisterValue &reg_value);

  bool WriteRegisterUnsigned(const Context &context,
                             const RegisterInfo *reg_info, uint64_t reg_value);

  //----------------------------------------------------------------------
  // Register kind and number variants
  //----------------------------------------------------------------------
  bool ReadRegister(lldb::RegisterKind reg_kind, uint32_t reg_num,
                    RegisterValue &reg_value);

  bool WriteRegister(const Context &context, lldb::RegisterKind reg_kind,
                     uint32_t reg_num, const RegisterValue &reg_value);

  uint64_t ReadRegisterUnsigned(lldb::RegisterKind reg_kind, uint32_t reg_num,
                                uint64_t fail_value, bool *success_ptr);

  bool WriteRegisterUnsigned(const Context &context,
                             lldb::RegisterKind reg_kind, uint32_t reg_num,
                             uint64_t reg_value);

  size_t ReadMemory(const Context &context, lldb::addr_t addr, void *dst,
                    size_t dst_len);

  uint64_t ReadMemoryUnsigned(const Context &context, lldb::addr_t addr,
                              size_t byte_size, uint64_t fail_value,
                              bool *success_ptr);

  bool WriteMemory(const Context &context, lldb::addr_t addr, const void *src,
                   size_t src_len);

  bool WriteMemoryUnsigned(const Context &context, lldb::addr_t addr,
                           uint64_t uval, size_t uval_byte_size);

  uint32_t GetAddressByteSize() const { return m_arch.GetAddressByteSize(); }

  lldb::ByteOrder GetByteOrder() const { return m_arch.GetByteOrder(); }

  const Opcode &GetOpcode() const { return m_opcode; }

  lldb::addr_t GetAddress() const { return m_addr; }

  const ArchSpec &GetArchitecture() const { return m_arch; }

  static size_t ReadMemoryFrame(EmulateInstruction *instruction, void *baton,
                                const Context &context, lldb::addr_t addr,
                                void *dst, size_t length);

  static size_t WriteMemoryFrame(EmulateInstruction *instruction, void *baton,
                                 const Context &context, lldb::addr_t addr,
                                 const void *dst, size_t length);

  static bool ReadRegisterFrame(EmulateInstruction *instruction, void *baton,
                                const RegisterInfo *reg_info,
                                RegisterValue &reg_value);

  static bool WriteRegisterFrame(EmulateInstruction *instruction, void *baton,
                                 const Context &context,
                                 const RegisterInfo *reg_info,
                                 const RegisterValue &reg_value);

  static size_t ReadMemoryDefault(EmulateInstruction *instruction, void *baton,
                                  const Context &context, lldb::addr_t addr,
                                  void *dst, size_t length);

  static size_t WriteMemoryDefault(EmulateInstruction *instruction, void *baton,
                                   const Context &context, lldb::addr_t addr,
                                   const void *dst, size_t length);

  static bool ReadRegisterDefault(EmulateInstruction *instruction, void *baton,
                                  const RegisterInfo *reg_info,
                                  RegisterValue &reg_value);

  static bool WriteRegisterDefault(EmulateInstruction *instruction, void *baton,
                                   const Context &context,
                                   const RegisterInfo *reg_info,
                                   const RegisterValue &reg_value);

  void SetBaton(void *baton);

  void SetCallbacks(ReadMemoryCallback read_mem_callback,
                    WriteMemoryCallback write_mem_callback,
                    ReadRegisterCallback read_reg_callback,
                    WriteRegisterCallback write_reg_callback);

  void SetReadMemCallback(ReadMemoryCallback read_mem_callback);

  void SetWriteMemCallback(WriteMemoryCallback write_mem_callback);

  void SetReadRegCallback(ReadRegisterCallback read_reg_callback);

  void SetWriteRegCallback(WriteRegisterCallback write_reg_callback);

  static bool GetBestRegisterKindAndNumber(const RegisterInfo *reg_info,
                                           lldb::RegisterKind &reg_kind,
                                           uint32_t &reg_num);

  static uint32_t GetInternalRegisterNumber(RegisterContext *reg_ctx,
                                            const RegisterInfo &reg_info);

protected:
  ArchSpec m_arch;
  void *m_baton;
  ReadMemoryCallback m_read_mem_callback;
  WriteMemoryCallback m_write_mem_callback;
  ReadRegisterCallback m_read_reg_callback;
  WriteRegisterCallback m_write_reg_callback;
  lldb::addr_t m_addr;
  Opcode m_opcode;

private:
  //------------------------------------------------------------------
  // For EmulateInstruction only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(EmulateInstruction);
};

} // namespace lldb_private

#endif // lldb_EmulateInstruction_h_
