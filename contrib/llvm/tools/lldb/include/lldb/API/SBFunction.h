//===-- SBFunction.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBFunction_h_
#define LLDB_SBFunction_h_

#include "lldb/API/SBAddress.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBInstructionList.h"

namespace lldb {

class LLDB_API SBFunction {
public:
  SBFunction();

  SBFunction(const lldb::SBFunction &rhs);

  const lldb::SBFunction &operator=(const lldb::SBFunction &rhs);

  ~SBFunction();

  bool IsValid() const;

  const char *GetName() const;

  const char *GetDisplayName() const;

  const char *GetMangledName() const;

  lldb::SBInstructionList GetInstructions(lldb::SBTarget target);

  lldb::SBInstructionList GetInstructions(lldb::SBTarget target,
                                          const char *flavor);

  lldb::SBAddress GetStartAddress();

  lldb::SBAddress GetEndAddress();

  const char *GetArgumentName(uint32_t arg_idx);

  uint32_t GetPrologueByteSize();

  lldb::SBType GetType();

  lldb::SBBlock GetBlock();

  lldb::LanguageType GetLanguage();

  bool GetIsOptimized();

  bool operator==(const lldb::SBFunction &rhs) const;

  bool operator!=(const lldb::SBFunction &rhs) const;

  bool GetDescription(lldb::SBStream &description);

protected:
  lldb_private::Function *get();

  void reset(lldb_private::Function *lldb_object_ptr);

private:
  friend class SBAddress;
  friend class SBFrame;
  friend class SBSymbolContext;

  SBFunction(lldb_private::Function *lldb_object_ptr);

  lldb_private::Function *m_opaque_ptr;
};

} // namespace lldb

#endif // LLDB_SBFunction_h_
