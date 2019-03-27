//===-- SBInstructionList.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBInstructionList_h_
#define LLDB_SBInstructionList_h_

#include "lldb/API/SBDefines.h"

#include <stdio.h>

namespace lldb {

class LLDB_API SBInstructionList {
public:
  SBInstructionList();

  SBInstructionList(const SBInstructionList &rhs);

  const SBInstructionList &operator=(const SBInstructionList &rhs);

  ~SBInstructionList();

  bool IsValid() const;

  size_t GetSize();

  lldb::SBInstruction GetInstructionAtIndex(uint32_t idx);

  // ----------------------------------------------------------------------
  // Returns the number of instructions between the start and end address. If
  // canSetBreakpoint is true then the count will be the number of
  // instructions on which a breakpoint can be set.
  // ----------------------------------------------------------------------
  size_t GetInstructionsCount(const SBAddress &start,
                              const SBAddress &end,
                              bool canSetBreakpoint = false);                                   

  void Clear();

  void AppendInstruction(lldb::SBInstruction inst);

  void Print(FILE *out);

  bool GetDescription(lldb::SBStream &description);

  bool DumpEmulationForAllInstructions(const char *triple);

protected:
  friend class SBFunction;
  friend class SBSymbol;
  friend class SBTarget;

  void SetDisassembler(const lldb::DisassemblerSP &opaque_sp);

private:
  lldb::DisassemblerSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_SBInstructionList_h_
