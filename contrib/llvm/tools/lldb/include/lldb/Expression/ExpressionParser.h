//===-- ExpressionParser.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ExpressionParser_h_
#define liblldb_ExpressionParser_h_

#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-public.h"

namespace lldb_private {

class IRExecutionUnit;

//----------------------------------------------------------------------
/// @class ExpressionParser ExpressionParser.h
/// "lldb/Expression/ExpressionParser.h" Encapsulates an instance of a
/// compiler that can parse expressions.
///
/// ExpressionParser is the base class for llvm based Expression parsers.
//----------------------------------------------------------------------
class ExpressionParser {
public:
  //------------------------------------------------------------------
  /// Constructor
  ///
  /// Initializes class variables.
  ///
  /// @param[in] exe_scope,
  ///     If non-NULL, an execution context scope that can help to
  ///     correctly create an expression with a valid process for
  ///     optional tuning Objective-C runtime support. Can be NULL.
  ///
  /// @param[in] expr
  ///     The expression to be parsed.
  //------------------------------------------------------------------
  ExpressionParser(ExecutionContextScope *exe_scope, Expression &expr,
                   bool generate_debug_info)
      : m_expr(expr), m_generate_debug_info(generate_debug_info) {}

  //------------------------------------------------------------------
  /// Destructor
  //------------------------------------------------------------------
  virtual ~ExpressionParser(){};

  //------------------------------------------------------------------
  /// Attempts to find possible command line completions for the given
  /// expression.
  ///
  /// @param[out] request
  ///     The completion request to fill out. The completion should be a string
  ///     that would complete the current token at the cursor position.
  ///     Note that the string in the list replaces the current token
  ///     in the command line.
  ///
  /// @param[in] line
  ///     The line with the completion cursor inside the expression as a string.
  ///     The first line in the expression has the number 0.
  ///
  /// @param[in] pos
  ///     The character position in the line with the completion cursor.
  ///     If the value is 0, then the cursor is on top of the first character
  ///     in the line (i.e. the user has requested completion from the start of
  ///     the expression).
  ///
  /// @param[in] typed_pos
  ///     The cursor position in the line as typed by the user. If the user
  ///     expression has not been transformed in some form (e.g. wrapping it
  ///     in a function body for C languages), then this is equal to the
  ///     'pos' parameter. The semantics of this value are otherwise equal to
  ///     'pos' (e.g. a value of 0 means the cursor is at start of the
  ///     expression).
  ///
  /// @return
  ///     True if we added any completion results to the output;
  ///     false otherwise.
  //------------------------------------------------------------------
  virtual bool Complete(CompletionRequest &request, unsigned line, unsigned pos,
                        unsigned typed_pos) = 0;

  //------------------------------------------------------------------
  /// Parse a single expression and convert it to IR using Clang.  Don't wrap
  /// the expression in anything at all.
  ///
  /// @param[in] diagnostic_manager
  ///     The diagnostic manager in which to store the errors and warnings.
  ///
  /// @return
  ///     The number of errors encountered during parsing.  0 means
  ///     success.
  //------------------------------------------------------------------
  virtual unsigned Parse(DiagnosticManager &diagnostic_manager) = 0;

  //------------------------------------------------------------------
  /// Try to use the FixIts in the diagnostic_manager to rewrite the
  /// expression.  If successful, the rewritten expression is stored in the
  /// diagnostic_manager, get it out with GetFixedExpression.
  ///
  /// @param[in] diagnostic_manager
  ///     The diagnostic manager containing fixit's to apply.
  ///
  /// @return
  ///     \b true if the rewrite was successful, \b false otherwise.
  //------------------------------------------------------------------
  virtual bool RewriteExpression(DiagnosticManager &diagnostic_manager) {
    return false;
  }

  //------------------------------------------------------------------
  /// Ready an already-parsed expression for execution, possibly evaluating it
  /// statically.
  ///
  /// @param[out] func_addr
  ///     The address to which the function has been written.
  ///
  /// @param[out] func_end
  ///     The end of the function's allocated memory region.  (func_addr
  ///     and func_end do not delimit an allocated region; the allocated
  ///     region may begin before func_addr.)
  ///
  /// @param[in] execution_unit_sp
  ///     After parsing, ownership of the execution unit for
  ///     for the expression is handed to this shared pointer.
  ///
  /// @param[in] exe_ctx
  ///     The execution context to write the function into.
  ///
  /// @param[out] can_interpret
  ///     Set to true if the expression could be interpreted statically;
  ///     untouched otherwise.
  ///
  /// @param[in] execution_policy
  ///     Determines whether the expression must be JIT-compiled, must be
  ///     evaluated statically, or whether this decision may be made
  ///     opportunistically.
  ///
  /// @return
  ///     An error code indicating the success or failure of the operation.
  ///     Test with Success().
  //------------------------------------------------------------------
  virtual Status
  PrepareForExecution(lldb::addr_t &func_addr, lldb::addr_t &func_end,
                      std::shared_ptr<IRExecutionUnit> &execution_unit_sp,
                      ExecutionContext &exe_ctx, bool &can_interpret,
                      lldb_private::ExecutionPolicy execution_policy) = 0;

  bool GetGenerateDebugInfo() const { return m_generate_debug_info; }

protected:
  Expression &m_expr; ///< The expression to be parsed
  bool m_generate_debug_info;
};
}

#endif // liblldb_ExpressionParser_h_
