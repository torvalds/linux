//===-- ClangExpressionVariable.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ClangExpressionVariable.h"

#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Stream.h"
#include "clang/AST/ASTContext.h"

using namespace lldb_private;
using namespace clang;

ClangExpressionVariable::ClangExpressionVariable(
    ExecutionContextScope *exe_scope, lldb::ByteOrder byte_order,
    uint32_t addr_byte_size)
    : ExpressionVariable(LLVMCastKind::eKindClang), m_parser_vars(),
      m_jit_vars() {
  m_flags = EVNone;
  m_frozen_sp =
      ValueObjectConstResult::Create(exe_scope, byte_order, addr_byte_size);
}

ClangExpressionVariable::ClangExpressionVariable(
    ExecutionContextScope *exe_scope, Value &value, const ConstString &name,
    uint16_t flags)
    : ExpressionVariable(LLVMCastKind::eKindClang), m_parser_vars(),
      m_jit_vars() {
  m_flags = flags;
  m_frozen_sp = ValueObjectConstResult::Create(exe_scope, value, name);
}

ClangExpressionVariable::ClangExpressionVariable(
    const lldb::ValueObjectSP &valobj_sp)
    : ExpressionVariable(LLVMCastKind::eKindClang), m_parser_vars(),
      m_jit_vars() {
  m_flags = EVNone;
  m_frozen_sp = valobj_sp;
}

ClangExpressionVariable::ClangExpressionVariable(
    ExecutionContextScope *exe_scope, const ConstString &name,
    const TypeFromUser &user_type, lldb::ByteOrder byte_order,
    uint32_t addr_byte_size)
    : ExpressionVariable(LLVMCastKind::eKindClang), m_parser_vars(),
      m_jit_vars() {
  m_flags = EVNone;
  m_frozen_sp =
      ValueObjectConstResult::Create(exe_scope, byte_order, addr_byte_size);
  SetName(name);
  SetCompilerType(user_type);
}

TypeFromUser ClangExpressionVariable::GetTypeFromUser() {
  TypeFromUser tfu(m_frozen_sp->GetCompilerType());
  return tfu;
}
