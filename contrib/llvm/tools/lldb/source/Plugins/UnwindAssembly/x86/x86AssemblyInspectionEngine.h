//===-- x86AssemblyInspectionEngine.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_x86AssemblyInspectionEngine_h_
#define liblldb_x86AssemblyInspectionEngine_h_

#include "llvm-c/Disassembler.h"

#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

#include <map>
#include <vector>

namespace lldb_private {

// x86AssemblyInspectionEngine - a class which will take a buffer of bytes
// of i386/x86_64 instructions and create an UnwindPlan based on those
// assembly instructions.
class x86AssemblyInspectionEngine {

public:
  /// default ctor
  x86AssemblyInspectionEngine(const lldb_private::ArchSpec &arch);

  /// default dtor
  ~x86AssemblyInspectionEngine();

  /// One of the two initialize methods that can be called on this object;
  /// they must be called before any of the assembly inspection methods
  /// are called.  This one should be used if the caller has access to a
  /// valid RegisterContext.
  void Initialize(lldb::RegisterContextSP &reg_ctx);

  /// One of the two initialize methods that can be called on this object;
  /// they must be called before any of the assembly inspection methods
  /// are called.  This one takes a vector of register name and lldb
  /// register numbers.
  struct lldb_reg_info {
    const char *name;
    uint32_t lldb_regnum;
    lldb_reg_info() : name(nullptr), lldb_regnum(LLDB_INVALID_REGNUM) {}
  };
  void Initialize(std::vector<lldb_reg_info> &reg_info);

  /// Create an UnwindPlan for a "non-call site" stack frame situation.
  /// This is usually when this function/method is currently executing, and may
  /// be at
  /// a location where exception-handling style unwind information (eh_frame,
  /// compact unwind info, arm unwind info)
  /// are not valid.
  /// \p data is a pointer to the instructions for the function
  /// \p size is the size of the instruction buffer above
  /// \p func_range is the start Address and size of the function, to be
  /// included in the UnwindPlan
  /// \p unwind_plan is the unwind plan that this method creates
  /// \returns true if it was able to create an UnwindPlan; false if not.
  bool
  GetNonCallSiteUnwindPlanFromAssembly(uint8_t *data, size_t size,
                                       lldb_private::AddressRange &func_range,
                                       lldb_private::UnwindPlan &unwind_plan);

  /// Take an existing UnwindPlan, probably from eh_frame which may be missing
  /// description
  /// of the epilogue instructions, and add the epilogue description to it based
  /// on the
  /// instructions in the function.
  ///
  /// The \p unwind_plan 's register numbers must be converted into the lldb
  /// register numbering
  /// scheme OR a RegisterContext must be provided in \p reg_ctx.  If the \p
  /// unwind_plan
  /// register numbers are already in lldb register numbering, \p reg_ctx may be
  /// null.
  /// \returns true if the \p unwind_plan was updated, false if it was not.
  bool AugmentUnwindPlanFromCallSite(uint8_t *data, size_t size,
                                     lldb_private::AddressRange &func_range,
                                     lldb_private::UnwindPlan &unwind_plan,
                                     lldb::RegisterContextSP &reg_ctx);

  bool FindFirstNonPrologueInstruction(uint8_t *data, size_t size,
                                       size_t &offset);

private:
  bool nonvolatile_reg_p(int machine_regno);
  bool push_rbp_pattern_p();
  bool push_0_pattern_p();
  bool push_imm_pattern_p();
  bool push_extended_pattern_p();
  bool push_misc_reg_p();
  bool mov_rsp_rbp_pattern_p();
  bool mov_rsp_rbx_pattern_p();
  bool mov_rbp_rsp_pattern_p();
  bool mov_rbx_rsp_pattern_p();
  bool sub_rsp_pattern_p(int &amount);
  bool add_rsp_pattern_p(int &amount);
  bool lea_rsp_pattern_p(int &amount);
  bool lea_rbp_rsp_pattern_p(int &amount);
  bool lea_rbx_rsp_pattern_p(int &amount);
  bool and_rsp_pattern_p();
  bool push_reg_p(int &regno);
  bool pop_reg_p(int &regno);
  bool pop_rbp_pattern_p();
  bool pop_misc_reg_p();
  bool leave_pattern_p();
  bool call_next_insn_pattern_p();
  bool mov_reg_to_local_stack_frame_p(int &regno, int &rbp_offset);
  bool ret_pattern_p();
  uint32_t extract_4(uint8_t *b);

  bool instruction_length(uint8_t *insn, int &length, uint32_t buffer_remaining_bytes);

  bool machine_regno_to_lldb_regno(int machine_regno, uint32_t &lldb_regno);

  enum CPU { k_i386, k_x86_64, k_cpu_unspecified };

  enum i386_register_numbers {
    k_machine_eax = 0,
    k_machine_ecx = 1,
    k_machine_edx = 2,
    k_machine_ebx = 3,
    k_machine_esp = 4,
    k_machine_ebp = 5,
    k_machine_esi = 6,
    k_machine_edi = 7,
    k_machine_eip = 8
  };

  enum x86_64_register_numbers {
    k_machine_rax = 0,
    k_machine_rcx = 1,
    k_machine_rdx = 2,
    k_machine_rbx = 3,
    k_machine_rsp = 4,
    k_machine_rbp = 5,
    k_machine_rsi = 6,
    k_machine_rdi = 7,
    k_machine_r8 = 8,
    k_machine_r9 = 9,
    k_machine_r10 = 10,
    k_machine_r11 = 11,
    k_machine_r12 = 12,
    k_machine_r13 = 13,
    k_machine_r14 = 14,
    k_machine_r15 = 15,
    k_machine_rip = 16
  };

  enum { kMaxInstructionByteSize = 32 };

  uint8_t *m_cur_insn;

  uint32_t m_machine_ip_regnum;
  uint32_t m_machine_sp_regnum;
  uint32_t m_machine_fp_regnum;
  uint32_t m_machine_alt_fp_regnum;
  uint32_t m_lldb_ip_regnum;
  uint32_t m_lldb_sp_regnum;
  uint32_t m_lldb_fp_regnum;
  uint32_t m_lldb_alt_fp_regnum;

  typedef std::map<uint32_t, lldb_reg_info> MachineRegnumToNameAndLLDBRegnum;

  MachineRegnumToNameAndLLDBRegnum m_reg_map;

  lldb_private::ArchSpec m_arch;
  CPU m_cpu;
  int m_wordsize;

  bool m_register_map_initialized;

  ::LLVMDisasmContextRef m_disasm_context;

  DISALLOW_COPY_AND_ASSIGN(x86AssemblyInspectionEngine);
};

} // namespace lldb_private

#endif // liblldb_x86AssemblyInspectionEngine_h_
