//===-- ABI.cpp -------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ABI.h"
#include "Plugins/ExpressionParser/Clang/ClangPersistentVariables.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"

using namespace lldb;
using namespace lldb_private;

ABISP
ABI::FindPlugin(lldb::ProcessSP process_sp, const ArchSpec &arch) {
  ABISP abi_sp;
  ABICreateInstance create_callback;

  for (uint32_t idx = 0;
       (create_callback = PluginManager::GetABICreateCallbackAtIndex(idx)) !=
       nullptr;
       ++idx) {
    abi_sp = create_callback(process_sp, arch);

    if (abi_sp)
      return abi_sp;
  }
  abi_sp.reset();
  return abi_sp;
}

ABI::~ABI() = default;

bool ABI::GetRegisterInfoByName(const ConstString &name, RegisterInfo &info) {
  uint32_t count = 0;
  const RegisterInfo *register_info_array = GetRegisterInfoArray(count);
  if (register_info_array) {
    const char *unique_name_cstr = name.GetCString();
    uint32_t i;
    for (i = 0; i < count; ++i) {
      if (register_info_array[i].name == unique_name_cstr) {
        info = register_info_array[i];
        return true;
      }
    }
    for (i = 0; i < count; ++i) {
      if (register_info_array[i].alt_name == unique_name_cstr) {
        info = register_info_array[i];
        return true;
      }
    }
  }
  return false;
}

bool ABI::GetRegisterInfoByKind(RegisterKind reg_kind, uint32_t reg_num,
                                RegisterInfo &info) {
  if (reg_kind < eRegisterKindEHFrame || reg_kind >= kNumRegisterKinds)
    return false;

  uint32_t count = 0;
  const RegisterInfo *register_info_array = GetRegisterInfoArray(count);
  if (register_info_array) {
    for (uint32_t i = 0; i < count; ++i) {
      if (register_info_array[i].kinds[reg_kind] == reg_num) {
        info = register_info_array[i];
        return true;
      }
    }
  }
  return false;
}

ValueObjectSP ABI::GetReturnValueObject(Thread &thread, CompilerType &ast_type,
                                        bool persistent) const {
  if (!ast_type.IsValid())
    return ValueObjectSP();

  ValueObjectSP return_valobj_sp;

  return_valobj_sp = GetReturnValueObjectImpl(thread, ast_type);
  if (!return_valobj_sp)
    return return_valobj_sp;

  // Now turn this into a persistent variable.
  // FIXME: This code is duplicated from Target::EvaluateExpression, and it is
  // used in similar form in a couple
  // of other places.  Figure out the correct Create function to do all this
  // work.

  if (persistent) {
    Target &target = *thread.CalculateTarget();
    PersistentExpressionState *persistent_expression_state =
        target.GetPersistentExpressionStateForLanguage(
            ast_type.GetMinimumLanguage());

    if (!persistent_expression_state)
      return ValueObjectSP();

    auto prefix = persistent_expression_state->GetPersistentVariablePrefix();
    ConstString persistent_variable_name =
        persistent_expression_state->GetNextPersistentVariableName(target,
                                                                   prefix);

    lldb::ValueObjectSP const_valobj_sp;

    // Check in case our value is already a constant value
    if (return_valobj_sp->GetIsConstant()) {
      const_valobj_sp = return_valobj_sp;
      const_valobj_sp->SetName(persistent_variable_name);
    } else
      const_valobj_sp =
          return_valobj_sp->CreateConstantValue(persistent_variable_name);

    lldb::ValueObjectSP live_valobj_sp = return_valobj_sp;

    return_valobj_sp = const_valobj_sp;

    ExpressionVariableSP clang_expr_variable_sp(
        persistent_expression_state->CreatePersistentVariable(
            return_valobj_sp));

    assert(clang_expr_variable_sp);

    // Set flags and live data as appropriate

    const Value &result_value = live_valobj_sp->GetValue();

    switch (result_value.GetValueType()) {
    case Value::eValueTypeHostAddress:
    case Value::eValueTypeFileAddress:
      // we don't do anything with these for now
      break;
    case Value::eValueTypeScalar:
    case Value::eValueTypeVector:
      clang_expr_variable_sp->m_flags |=
          ClangExpressionVariable::EVIsFreezeDried;
      clang_expr_variable_sp->m_flags |=
          ClangExpressionVariable::EVIsLLDBAllocated;
      clang_expr_variable_sp->m_flags |=
          ClangExpressionVariable::EVNeedsAllocation;
      break;
    case Value::eValueTypeLoadAddress:
      clang_expr_variable_sp->m_live_sp = live_valobj_sp;
      clang_expr_variable_sp->m_flags |=
          ClangExpressionVariable::EVIsProgramReference;
      break;
    }

    return_valobj_sp = clang_expr_variable_sp->GetValueObject();
  }
  return return_valobj_sp;
}

ValueObjectSP ABI::GetReturnValueObject(Thread &thread, llvm::Type &ast_type,
                                        bool persistent) const {
  ValueObjectSP return_valobj_sp;
  return_valobj_sp = GetReturnValueObjectImpl(thread, ast_type);
  return return_valobj_sp;
}

// specialized to work with llvm IR types
//
// for now we will specify a default implementation so that we don't need to
// modify other ABIs
lldb::ValueObjectSP ABI::GetReturnValueObjectImpl(Thread &thread,
                                                  llvm::Type &ir_type) const {
  ValueObjectSP return_valobj_sp;

  /* this is a dummy and will only be called if an ABI does not override this */

  return return_valobj_sp;
}

bool ABI::PrepareTrivialCall(Thread &thread, lldb::addr_t sp,
                             lldb::addr_t functionAddress,
                             lldb::addr_t returnAddress, llvm::Type &returntype,
                             llvm::ArrayRef<ABI::CallArgument> args) const {
  // dummy prepare trivial call
  llvm_unreachable("Should never get here!");
}

bool ABI::GetFallbackRegisterLocation(
    const RegisterInfo *reg_info,
    UnwindPlan::Row::RegisterLocation &unwind_regloc) {
  // Did the UnwindPlan fail to give us the caller's stack pointer? The stack
  // pointer is defined to be the same as THIS frame's CFA, so return the CFA
  // value as the caller's stack pointer.  This is true on x86-32/x86-64 at
  // least.
  if (reg_info->kinds[eRegisterKindGeneric] == LLDB_REGNUM_GENERIC_SP) {
    unwind_regloc.SetIsCFAPlusOffset(0);
    return true;
  }

  // If a volatile register is being requested, we don't want to forward the
  // next frame's register contents up the stack -- the register is not
  // retrievable at this frame.
  if (RegisterIsVolatile(reg_info)) {
    unwind_regloc.SetUndefined();
    return true;
  }

  return false;
}
