//===-- SBSymbolContext.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBSymbolContext_h_
#define LLDB_SBSymbolContext_h_

#include "lldb/API/SBBlock.h"
#include "lldb/API/SBCompileUnit.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBFunction.h"
#include "lldb/API/SBLineEntry.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBSymbol.h"

namespace lldb {

class LLDB_API SBSymbolContext {
public:
  SBSymbolContext();

  SBSymbolContext(const lldb::SBSymbolContext &rhs);

  SBSymbolContext(const lldb_private::SymbolContext *sc_ptr);

  ~SBSymbolContext();

  bool IsValid() const;

  const lldb::SBSymbolContext &operator=(const lldb::SBSymbolContext &rhs);

  lldb::SBModule GetModule();
  lldb::SBCompileUnit GetCompileUnit();
  lldb::SBFunction GetFunction();
  lldb::SBBlock GetBlock();
  lldb::SBLineEntry GetLineEntry();
  lldb::SBSymbol GetSymbol();

  void SetModule(lldb::SBModule module);
  void SetCompileUnit(lldb::SBCompileUnit compile_unit);
  void SetFunction(lldb::SBFunction function);
  void SetBlock(lldb::SBBlock block);
  void SetLineEntry(lldb::SBLineEntry line_entry);
  void SetSymbol(lldb::SBSymbol symbol);

  SBSymbolContext GetParentOfInlinedScope(const SBAddress &curr_frame_pc,
                                          SBAddress &parent_frame_addr) const;

  bool GetDescription(lldb::SBStream &description);

protected:
  friend class SBAddress;
  friend class SBFrame;
  friend class SBModule;
  friend class SBThread;
  friend class SBTarget;
  friend class SBSymbolContextList;

  lldb_private::SymbolContext *operator->() const;

  lldb_private::SymbolContext &operator*();

  lldb_private::SymbolContext &ref();

  const lldb_private::SymbolContext &operator*() const;

  lldb_private::SymbolContext *get() const;

  void SetSymbolContext(const lldb_private::SymbolContext *sc_ptr);

private:
  std::unique_ptr<lldb_private::SymbolContext> m_opaque_ap;
};

} // namespace lldb

#endif // LLDB_SBSymbolContext_h_
