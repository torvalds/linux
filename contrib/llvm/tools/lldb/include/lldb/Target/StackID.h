//===-- StackID.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StackID_h_
#define liblldb_StackID_h_

#include "lldb/Core/AddressRange.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class StackID {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  StackID()
      : m_pc(LLDB_INVALID_ADDRESS), m_cfa(LLDB_INVALID_ADDRESS),
        m_symbol_scope(nullptr) {}

  explicit StackID(lldb::addr_t pc, lldb::addr_t cfa,
                   SymbolContextScope *symbol_scope)
      : m_pc(pc), m_cfa(cfa), m_symbol_scope(symbol_scope) {}

  StackID(const StackID &rhs)
      : m_pc(rhs.m_pc), m_cfa(rhs.m_cfa), m_symbol_scope(rhs.m_symbol_scope) {}

  ~StackID() = default;

  lldb::addr_t GetPC() const { return m_pc; }

  lldb::addr_t GetCallFrameAddress() const { return m_cfa; }

  SymbolContextScope *GetSymbolContextScope() const { return m_symbol_scope; }

  void SetSymbolContextScope(SymbolContextScope *symbol_scope) {
    m_symbol_scope = symbol_scope;
  }

  void Clear() {
    m_pc = LLDB_INVALID_ADDRESS;
    m_cfa = LLDB_INVALID_ADDRESS;
    m_symbol_scope = nullptr;
  }

  bool IsValid() const {
    return m_pc != LLDB_INVALID_ADDRESS || m_cfa != LLDB_INVALID_ADDRESS;
  }

  void Dump(Stream *s);

  //------------------------------------------------------------------
  // Operators
  //------------------------------------------------------------------
  const StackID &operator=(const StackID &rhs) {
    if (this != &rhs) {
      m_pc = rhs.m_pc;
      m_cfa = rhs.m_cfa;
      m_symbol_scope = rhs.m_symbol_scope;
    }
    return *this;
  }

protected:
  friend class StackFrame;

  void SetPC(lldb::addr_t pc) { m_pc = pc; }

  void SetCFA(lldb::addr_t cfa) { m_cfa = cfa; }

  lldb::addr_t
      m_pc; // The pc value for the function/symbol for this frame. This will
  // only get used if the symbol scope is nullptr (the code where we are
  // stopped is not represented by any function or symbol in any shared
  // library).
  lldb::addr_t m_cfa; // The call frame address (stack pointer) value
                      // at the beginning of the function that uniquely
                      // identifies this frame (along with m_symbol_scope
                      // below)
  SymbolContextScope *
      m_symbol_scope; // If nullptr, there is no block or symbol for this frame.
                      // If not nullptr, this will either be the scope for the
                      // lexical block for the frame, or the scope for the
                      // symbol. Symbol context scopes are always be unique
                      // pointers since the are part of the Block and Symbol
                      // objects and can easily be used to tell if a stack ID
                      // is the same as another.
};

bool operator==(const StackID &lhs, const StackID &rhs);
bool operator!=(const StackID &lhs, const StackID &rhs);

// frame_id_1 < frame_id_2 means "frame_id_1 is YOUNGER than frame_id_2"
bool operator<(const StackID &lhs, const StackID &rhs);

} // namespace lldb_private

#endif // liblldb_StackID_h_
