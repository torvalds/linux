//===-- StackID.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/StackID.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Utility/Stream.h"

using namespace lldb_private;

void StackID::Dump(Stream *s) {
  s->Printf("StackID (pc = 0x%16.16" PRIx64 ", cfa = 0x%16.16" PRIx64
            ", symbol_scope = %p",
            m_pc, m_cfa, static_cast<void *>(m_symbol_scope));
  if (m_symbol_scope) {
    SymbolContext sc;

    m_symbol_scope->CalculateSymbolContext(&sc);
    if (sc.block)
      s->Printf(" (Block {0x%8.8" PRIx64 "})", sc.block->GetID());
    else if (sc.symbol)
      s->Printf(" (Symbol{0x%8.8x})", sc.symbol->GetID());
  }
  s->PutCString(") ");
}

bool lldb_private::operator==(const StackID &lhs, const StackID &rhs) {
  if (lhs.GetCallFrameAddress() != rhs.GetCallFrameAddress())
    return false;

  SymbolContextScope *lhs_scope = lhs.GetSymbolContextScope();
  SymbolContextScope *rhs_scope = rhs.GetSymbolContextScope();

  // Only compare the PC values if both symbol context scopes are nullptr
  if (lhs_scope == nullptr && rhs_scope == nullptr)
    return lhs.GetPC() == rhs.GetPC();

  return lhs_scope == rhs_scope;
}

bool lldb_private::operator!=(const StackID &lhs, const StackID &rhs) {
  if (lhs.GetCallFrameAddress() != rhs.GetCallFrameAddress())
    return true;

  SymbolContextScope *lhs_scope = lhs.GetSymbolContextScope();
  SymbolContextScope *rhs_scope = rhs.GetSymbolContextScope();

  if (lhs_scope == nullptr && rhs_scope == nullptr)
    return lhs.GetPC() != rhs.GetPC();

  return lhs_scope != rhs_scope;
}

bool lldb_private::operator<(const StackID &lhs, const StackID &rhs) {
  const lldb::addr_t lhs_cfa = lhs.GetCallFrameAddress();
  const lldb::addr_t rhs_cfa = rhs.GetCallFrameAddress();

  // FIXME: We are assuming that the stacks grow downward in memory.  That's not
  // necessary, but true on
  // all the machines we care about at present.  If this changes, we'll have to
  // deal with that.  The ABI is the agent who knows this ordering, but the
  // StackID has no access to the ABI. The most straightforward way to handle
  // this is to add a "m_grows_downward" bool to the StackID, and set it in the
  // constructor. But I'm not going to waste a bool per StackID on this till we
  // need it.

  if (lhs_cfa != rhs_cfa)
    return lhs_cfa < rhs_cfa;

  SymbolContextScope *lhs_scope = lhs.GetSymbolContextScope();
  SymbolContextScope *rhs_scope = rhs.GetSymbolContextScope();

  if (lhs_scope != nullptr && rhs_scope != nullptr) {
    // Same exact scope, lhs is not less than (younger than rhs)
    if (lhs_scope == rhs_scope)
      return false;

    SymbolContext lhs_sc;
    SymbolContext rhs_sc;
    lhs_scope->CalculateSymbolContext(&lhs_sc);
    rhs_scope->CalculateSymbolContext(&rhs_sc);

    // Items with the same function can only be compared
    if (lhs_sc.function == rhs_sc.function && lhs_sc.function != nullptr &&
        lhs_sc.block != nullptr && rhs_sc.function != nullptr &&
        rhs_sc.block != nullptr) {
      return rhs_sc.block->Contains(lhs_sc.block);
    }
  }
  return false;
}
