//===-- ClangExpressionVariable.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ClangExpressionVariable_h_
#define liblldb_ClangExpressionVariable_h_

#include <signal.h>
#include <stdint.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "llvm/Support/Casting.h"

#include "lldb/Core/ClangForward.h"
#include "lldb/Core/Value.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Symbol/TaggedASTType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-public.h"

namespace llvm {
class Value;
}

namespace lldb_private {

class ValueObjectConstResult;

//----------------------------------------------------------------------
/// @class ClangExpressionVariable ClangExpressionVariable.h
/// "lldb/Expression/ClangExpressionVariable.h" Encapsulates one variable for
/// the expression parser.
///
/// The expression parser uses variables in three different contexts:
///
/// First, it stores persistent variables along with the process for use in
/// expressions.  These persistent variables contain their own data and are
/// typed.
///
/// Second, in an interpreted expression, it stores the local variables for
/// the expression along with the expression.  These variables contain their
/// own data and are typed.
///
/// Third, in a JIT-compiled expression, it stores the variables that the
/// expression needs to have materialized and dematerialized at each
/// execution.  These do not contain their own data but are named and typed.
///
/// This class supports all of these use cases using simple type polymorphism,
/// and provides necessary support methods.  Its interface is RTTI-neutral.
//----------------------------------------------------------------------
class ClangExpressionVariable : public ExpressionVariable {
public:
  ClangExpressionVariable(ExecutionContextScope *exe_scope,
                          lldb::ByteOrder byte_order, uint32_t addr_byte_size);

  ClangExpressionVariable(ExecutionContextScope *exe_scope, Value &value,
                          const ConstString &name, uint16_t flags = EVNone);

  ClangExpressionVariable(const lldb::ValueObjectSP &valobj_sp);

  ClangExpressionVariable(ExecutionContextScope *exe_scope,
                          const ConstString &name,
                          const TypeFromUser &user_type,
                          lldb::ByteOrder byte_order, uint32_t addr_byte_size);

  //----------------------------------------------------------------------
  /// Utility functions for dealing with ExpressionVariableLists in Clang-
  /// specific ways
  //----------------------------------------------------------------------

  //----------------------------------------------------------------------
  /// Finds a variable by NamedDecl in the list.
  ///
  /// @param[in] name
  ///     The name of the requested variable.
  ///
  /// @return
  ///     The variable requested, or NULL if that variable is not in the list.
  //----------------------------------------------------------------------
  static ClangExpressionVariable *
  FindVariableInList(ExpressionVariableList &list, const clang::NamedDecl *decl,
                     uint64_t parser_id) {
    lldb::ExpressionVariableSP var_sp;
    for (size_t index = 0, size = list.GetSize(); index < size; ++index) {
      var_sp = list.GetVariableAtIndex(index);

      if (ClangExpressionVariable *clang_var =
              llvm::dyn_cast<ClangExpressionVariable>(var_sp.get())) {
        ClangExpressionVariable::ParserVars *parser_vars =
            clang_var->GetParserVars(parser_id);

        if (parser_vars && parser_vars->m_named_decl == decl)
          return clang_var;
      }
    }
    return nullptr;
  }

  //----------------------------------------------------------------------
  /// If the variable contains its own data, make a Value point at it. If \a
  /// exe_ctx in not NULL, the value will be resolved in with that execution
  /// context.
  ///
  /// @param[in] value
  ///     The value to point at the data.
  ///
  /// @param[in] exe_ctx
  ///     The execution context to use to resolve \a value.
  ///
  /// @return
  ///     True on success; false otherwise (in particular, if this variable
  ///     does not contain its own data).
  //----------------------------------------------------------------------
  bool PointValueAtData(Value &value, ExecutionContext *exe_ctx);

  //----------------------------------------------------------------------
  /// The following values should not live beyond parsing
  //----------------------------------------------------------------------
  class ParserVars {
  public:
    ParserVars()
        : m_parser_type(), m_named_decl(NULL), m_llvm_value(NULL),
          m_lldb_value(), m_lldb_var(), m_lldb_sym(NULL) {}

    TypeFromParser
        m_parser_type; ///< The type of the variable according to the parser
    const clang::NamedDecl
        *m_named_decl;         ///< The Decl corresponding to this variable
    llvm::Value *m_llvm_value; ///< The IR value corresponding to this variable;
                               ///usually a GlobalValue
    lldb_private::Value
        m_lldb_value;            ///< The value found in LLDB for this variable
    lldb::VariableSP m_lldb_var; ///< The original variable for this variable
    const lldb_private::Symbol *m_lldb_sym; ///< The original symbol for this
                                            ///variable, if it was a symbol
  };

private:
  typedef std::map<uint64_t, ParserVars> ParserVarMap;
  ParserVarMap m_parser_vars;

public:
  //----------------------------------------------------------------------
  /// Make this variable usable by the parser by allocating space for parser-
  /// specific variables
  //----------------------------------------------------------------------
  void EnableParserVars(uint64_t parser_id) {
    m_parser_vars.insert(std::make_pair(parser_id, ParserVars()));
  }

  //----------------------------------------------------------------------
  /// Deallocate parser-specific variables
  //----------------------------------------------------------------------
  void DisableParserVars(uint64_t parser_id) { m_parser_vars.erase(parser_id); }

  //----------------------------------------------------------------------
  /// Access parser-specific variables
  //----------------------------------------------------------------------
  ParserVars *GetParserVars(uint64_t parser_id) {
    ParserVarMap::iterator i = m_parser_vars.find(parser_id);

    if (i == m_parser_vars.end())
      return NULL;
    else
      return &i->second;
  }

  //----------------------------------------------------------------------
  /// The following values are valid if the variable is used by JIT code
  //----------------------------------------------------------------------
  struct JITVars {
    JITVars() : m_alignment(0), m_size(0), m_offset(0) {}

    lldb::offset_t
        m_alignment; ///< The required alignment of the variable, in bytes
    size_t m_size;   ///< The space required for the variable, in bytes
    lldb::offset_t
        m_offset; ///< The offset of the variable in the struct, in bytes
  };

private:
  typedef std::map<uint64_t, JITVars> JITVarMap;
  JITVarMap m_jit_vars;

public:
  //----------------------------------------------------------------------
  /// Make this variable usable for materializing for the JIT by allocating
  /// space for JIT-specific variables
  //----------------------------------------------------------------------
  void EnableJITVars(uint64_t parser_id) {
    m_jit_vars.insert(std::make_pair(parser_id, JITVars()));
  }

  //----------------------------------------------------------------------
  /// Deallocate JIT-specific variables
  //----------------------------------------------------------------------
  void DisableJITVars(uint64_t parser_id) { m_jit_vars.erase(parser_id); }

  JITVars *GetJITVars(uint64_t parser_id) {
    JITVarMap::iterator i = m_jit_vars.find(parser_id);

    if (i == m_jit_vars.end())
      return NULL;
    else
      return &i->second;
  }

  TypeFromUser GetTypeFromUser();

  //------------------------------------------------------------------
  // llvm casting support
  //------------------------------------------------------------------
  static bool classof(const ExpressionVariable *ev) {
    return ev->getKind() == ExpressionVariable::eKindClang;
  }

  //----------------------------------------------------------------------
  /// Members
  //----------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(ClangExpressionVariable);
};

} // namespace lldb_private

#endif // liblldb_ClangExpressionVariable_h_
