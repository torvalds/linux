//===-- SBInstruction.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBInstruction_h_
#define LLDB_SBInstruction_h_

#include "lldb/API/SBData.h"
#include "lldb/API/SBDefines.h"

#include <stdio.h>

// There's a lot to be fixed here, but need to wait for underlying insn
// implementation to be revised & settle down first.

class InstructionImpl;

namespace lldb {

class LLDB_API SBInstruction {
public:
  SBInstruction();

  SBInstruction(const SBInstruction &rhs);

  const SBInstruction &operator=(const SBInstruction &rhs);

  ~SBInstruction();

  bool IsValid();

  SBAddress GetAddress();

  const char *GetMnemonic(lldb::SBTarget target);

  const char *GetOperands(lldb::SBTarget target);

  const char *GetComment(lldb::SBTarget target);

  lldb::SBData GetData(lldb::SBTarget target);

  size_t GetByteSize();

  bool DoesBranch();

  bool HasDelaySlot();

  bool CanSetBreakpoint();

  void Print(FILE *out);

  bool GetDescription(lldb::SBStream &description);

  bool EmulateWithFrame(lldb::SBFrame &frame, uint32_t evaluate_options);

  bool DumpEmulation(const char *triple); // triple is to specify the
                                          // architecture, e.g. 'armv6' or
                                          // 'armv7-apple-ios'

  bool TestEmulation(lldb::SBStream &output_stream, const char *test_file);

protected:
  friend class SBInstructionList;

  SBInstruction(const lldb::DisassemblerSP &disasm_sp,
                const lldb::InstructionSP &inst_sp);

  void SetOpaque(const lldb::DisassemblerSP &disasm_sp,
                 const lldb::InstructionSP &inst_sp);

  lldb::InstructionSP GetOpaque();

private:
  std::shared_ptr<InstructionImpl> m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_SBInstruction_h_
