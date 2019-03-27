//===-- ExpressionVariable.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ExpressionVariable_h_
#define liblldb_ExpressionVariable_h_

#include <memory>
#include <vector>

#include "llvm/ADT/DenseMap.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-public.h"

namespace lldb_private {

class ClangExpressionVariable;

class ExpressionVariable
    : public std::enable_shared_from_this<ExpressionVariable> {
public:
  //----------------------------------------------------------------------
  // See TypeSystem.h for how to add subclasses to this.
  //----------------------------------------------------------------------
  enum LLVMCastKind { eKindClang, eKindSwift, eKindGo, kNumKinds };

  LLVMCastKind getKind() const { return m_kind; }

  ExpressionVariable(LLVMCastKind kind) : m_flags(0), m_kind(kind) {}

  virtual ~ExpressionVariable();

  size_t GetByteSize() { return m_frozen_sp->GetByteSize(); }

  const ConstString &GetName() { return m_frozen_sp->GetName(); }

  lldb::ValueObjectSP GetValueObject() { return m_frozen_sp; }

  uint8_t *GetValueBytes();

  void ValueUpdated() { m_frozen_sp->ValueUpdated(); }

  RegisterInfo *GetRegisterInfo() {
    return m_frozen_sp->GetValue().GetRegisterInfo();
  }

  void SetRegisterInfo(const RegisterInfo *reg_info) {
    return m_frozen_sp->GetValue().SetContext(
        Value::eContextTypeRegisterInfo, const_cast<RegisterInfo *>(reg_info));
  }

  CompilerType GetCompilerType() { return m_frozen_sp->GetCompilerType(); }

  void SetCompilerType(const CompilerType &compiler_type) {
    m_frozen_sp->GetValue().SetCompilerType(compiler_type);
  }

  void SetName(const ConstString &name) { m_frozen_sp->SetName(name); }

  // this function is used to copy the address-of m_live_sp into m_frozen_sp
  // this is necessary because the results of certain cast and pointer-
  // arithmetic operations (such as those described in bugzilla issues 11588
  // and 11618) generate frozen objects that do not have a valid address-of,
  // which can be troublesome when using synthetic children providers.
  // Transferring the address-of the live object solves these issues and
  // provides the expected user-level behavior
  void TransferAddress(bool force = false) {
    if (m_live_sp.get() == nullptr)
      return;

    if (m_frozen_sp.get() == nullptr)
      return;

    if (force || (m_frozen_sp->GetLiveAddress() == LLDB_INVALID_ADDRESS))
      m_frozen_sp->SetLiveAddress(m_live_sp->GetLiveAddress());
  }

  enum Flags {
    EVNone = 0,
    EVIsLLDBAllocated = 1 << 0, ///< This variable is resident in a location
                                ///specifically allocated for it by LLDB in the
                                ///target process
    EVIsProgramReference = 1 << 1, ///< This variable is a reference to a
                                   ///(possibly invalid) area managed by the
                                   ///target program
    EVNeedsAllocation = 1 << 2,    ///< Space for this variable has yet to be
                                   ///allocated in the target process
    EVIsFreezeDried = 1 << 3, ///< This variable's authoritative version is in
                              ///m_frozen_sp (for example, for
                              ///statically-computed results)
    EVNeedsFreezeDry =
        1 << 4, ///< Copy from m_live_sp to m_frozen_sp during dematerialization
    EVKeepInTarget = 1 << 5, ///< Keep the allocation after the expression is
                             ///complete rather than freeze drying its contents
                             ///and freeing it
    EVTypeIsReference = 1 << 6, ///< The original type of this variable is a
                                ///reference, so materialize the value rather
                                ///than the location
    EVUnknownType = 1 << 7, ///< This is a symbol of unknown type, and the type
                            ///must be resolved after parsing is complete
    EVBareRegister = 1 << 8 ///< This variable is a direct reference to $pc or
                            ///some other entity.
  };

  typedef uint16_t FlagType;

  FlagType m_flags; // takes elements of Flags

  // these should be private
  lldb::ValueObjectSP m_frozen_sp;
  lldb::ValueObjectSP m_live_sp;
  LLVMCastKind m_kind;
};

//----------------------------------------------------------------------
/// @class ExpressionVariableList ExpressionVariable.h
/// "lldb/Expression/ExpressionVariable.h"
/// A list of variable references.
///
/// This class stores variables internally, acting as the permanent store.
//----------------------------------------------------------------------
class ExpressionVariableList {
public:
  //----------------------------------------------------------------------
  /// Implementation of methods in ExpressionVariableListBase
  //----------------------------------------------------------------------
  size_t GetSize() { return m_variables.size(); }

  lldb::ExpressionVariableSP GetVariableAtIndex(size_t index) {
    lldb::ExpressionVariableSP var_sp;
    if (index < m_variables.size())
      var_sp = m_variables[index];
    return var_sp;
  }

  size_t AddVariable(const lldb::ExpressionVariableSP &var_sp) {
    m_variables.push_back(var_sp);
    return m_variables.size() - 1;
  }

  lldb::ExpressionVariableSP
  AddNewlyConstructedVariable(ExpressionVariable *var) {
    lldb::ExpressionVariableSP var_sp(var);
    m_variables.push_back(var_sp);
    return m_variables.back();
  }

  bool ContainsVariable(const lldb::ExpressionVariableSP &var_sp) {
    const size_t size = m_variables.size();
    for (size_t index = 0; index < size; ++index) {
      if (m_variables[index].get() == var_sp.get())
        return true;
    }
    return false;
  }

  //----------------------------------------------------------------------
  /// Finds a variable by name in the list.
  ///
  /// @param[in] name
  ///     The name of the requested variable.
  ///
  /// @return
  ///     The variable requested, or nullptr if that variable is not in the
  ///     list.
  //----------------------------------------------------------------------
  lldb::ExpressionVariableSP GetVariable(const ConstString &name) {
    lldb::ExpressionVariableSP var_sp;
    for (size_t index = 0, size = GetSize(); index < size; ++index) {
      var_sp = GetVariableAtIndex(index);
      if (var_sp->GetName() == name)
        return var_sp;
    }
    var_sp.reset();
    return var_sp;
  }

  lldb::ExpressionVariableSP GetVariable(llvm::StringRef name) {
    if (name.empty())
      return nullptr;

    for (size_t index = 0, size = GetSize(); index < size; ++index) {
      auto var_sp = GetVariableAtIndex(index);
      llvm::StringRef var_name_str = var_sp->GetName().GetStringRef();
      if (var_name_str == name)
        return var_sp;
    }
    return nullptr;
  }

  void RemoveVariable(lldb::ExpressionVariableSP var_sp) {
    for (std::vector<lldb::ExpressionVariableSP>::iterator
             vi = m_variables.begin(),
             ve = m_variables.end();
         vi != ve; ++vi) {
      if (vi->get() == var_sp.get()) {
        m_variables.erase(vi);
        return;
      }
    }
  }

  void Clear() { m_variables.clear(); }

private:
  std::vector<lldb::ExpressionVariableSP> m_variables;
};

class PersistentExpressionState : public ExpressionVariableList {
public:
  //----------------------------------------------------------------------
  // See TypeSystem.h for how to add subclasses to this.
  //----------------------------------------------------------------------
  enum LLVMCastKind { eKindClang, eKindSwift, eKindGo, kNumKinds };

  LLVMCastKind getKind() const { return m_kind; }

  PersistentExpressionState(LLVMCastKind kind) : m_kind(kind) {}

  virtual ~PersistentExpressionState();

  virtual lldb::ExpressionVariableSP
  CreatePersistentVariable(const lldb::ValueObjectSP &valobj_sp) = 0;

  virtual lldb::ExpressionVariableSP
  CreatePersistentVariable(ExecutionContextScope *exe_scope,
                           const ConstString &name, const CompilerType &type,
                           lldb::ByteOrder byte_order,
                           uint32_t addr_byte_size) = 0;

  /// Return a new persistent variable name with the specified prefix.
  ConstString GetNextPersistentVariableName(Target &target,
                                            llvm::StringRef prefix);

  virtual llvm::StringRef
  GetPersistentVariablePrefix(bool is_error = false) const = 0;

  virtual void
  RemovePersistentVariable(lldb::ExpressionVariableSP variable) = 0;

  virtual lldb::addr_t LookupSymbol(const ConstString &name);

  void RegisterExecutionUnit(lldb::IRExecutionUnitSP &execution_unit_sp);

private:
  LLVMCastKind m_kind;

  typedef std::set<lldb::IRExecutionUnitSP> ExecutionUnitSet;
  ExecutionUnitSet
      m_execution_units; ///< The execution units that contain valuable symbols.

  typedef llvm::DenseMap<const char *, lldb::addr_t> SymbolMap;
  SymbolMap
      m_symbol_map; ///< The addresses of the symbols in m_execution_units.
};

} // namespace lldb_private

#endif // liblldb_ExpressionVariable_h_
