//===--------------------- RegisterNumber.cpp -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/RegisterNumber.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Thread.h"

using namespace lldb_private;

RegisterNumber::RegisterNumber(lldb_private::Thread &thread,
                               lldb::RegisterKind kind, uint32_t num)
    : m_reg_ctx_sp(thread.GetRegisterContext()), m_regnum(num), m_kind(kind),
      m_kind_regnum_map(), m_name("") {
  if (m_reg_ctx_sp.get()) {
    const lldb_private::RegisterInfo *reginfo =
        m_reg_ctx_sp->GetRegisterInfoAtIndex(
            GetAsKind(lldb::eRegisterKindLLDB));
    if (reginfo && reginfo->name) {
      m_name = reginfo->name;
    }
  }
}

RegisterNumber::RegisterNumber()
    : m_reg_ctx_sp(), m_regnum(LLDB_INVALID_REGNUM),
      m_kind(lldb::kNumRegisterKinds), m_kind_regnum_map(), m_name(nullptr) {}

void RegisterNumber::init(lldb_private::Thread &thread, lldb::RegisterKind kind,
                          uint32_t num) {
  m_reg_ctx_sp = thread.GetRegisterContext();
  m_regnum = num;
  m_kind = kind;
  if (m_reg_ctx_sp.get()) {
    const lldb_private::RegisterInfo *reginfo =
        m_reg_ctx_sp->GetRegisterInfoAtIndex(
            GetAsKind(lldb::eRegisterKindLLDB));
    if (reginfo && reginfo->name) {
      m_name = reginfo->name;
    }
  }
}

const RegisterNumber &RegisterNumber::operator=(const RegisterNumber &rhs) {
  m_reg_ctx_sp = rhs.m_reg_ctx_sp;
  m_regnum = rhs.m_regnum;
  m_kind = rhs.m_kind;
  for (auto it : rhs.m_kind_regnum_map)
    m_kind_regnum_map[it.first] = it.second;
  m_name = rhs.m_name;
  return *this;
}

bool RegisterNumber::operator==(RegisterNumber &rhs) {
  if (IsValid() != rhs.IsValid())
    return false;

  if (m_kind == rhs.m_kind) {
    return m_regnum == rhs.m_regnum;
  }

  uint32_t rhs_regnum = rhs.GetAsKind(m_kind);
  if (rhs_regnum != LLDB_INVALID_REGNUM) {
    return m_regnum == rhs_regnum;
  }
  uint32_t lhs_regnum = GetAsKind(rhs.m_kind);
  { return lhs_regnum == rhs.m_regnum; }
  return false;
}

bool RegisterNumber::operator!=(RegisterNumber &rhs) { return !(*this == rhs); }

bool RegisterNumber::IsValid() const {
  return m_reg_ctx_sp.get() && m_kind != lldb::kNumRegisterKinds &&
         m_regnum != LLDB_INVALID_REGNUM;
}

uint32_t RegisterNumber::GetAsKind(lldb::RegisterKind kind) {
  if (m_regnum == LLDB_INVALID_REGNUM)
    return LLDB_INVALID_REGNUM;

  if (kind == m_kind)
    return m_regnum;

  Collection::iterator iter = m_kind_regnum_map.find(kind);
  if (iter != m_kind_regnum_map.end()) {
    return iter->second;
  }
  uint32_t output_regnum = LLDB_INVALID_REGNUM;
  if (m_reg_ctx_sp &&
      m_reg_ctx_sp->ConvertBetweenRegisterKinds(m_kind, m_regnum, kind,
                                                output_regnum) &&
      output_regnum != LLDB_INVALID_REGNUM) {
    m_kind_regnum_map[kind] = output_regnum;
  }
  return output_regnum;
}

uint32_t RegisterNumber::GetRegisterNumber() const { return m_regnum; }

lldb::RegisterKind RegisterNumber::GetRegisterKind() const { return m_kind; }

const char *RegisterNumber::GetName() { return m_name; }
