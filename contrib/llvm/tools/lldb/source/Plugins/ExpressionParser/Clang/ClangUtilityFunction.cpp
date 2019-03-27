//===-- ClangUtilityFunction.cpp ---------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ClangUtilityFunction.h"
#include "ClangExpressionDeclMap.h"
#include "ClangExpressionParser.h"

#include <stdio.h>
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif


#include "lldb/Core/Module.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Expression/ExpressionSourceCode.h"
#include "lldb/Expression/IRExecutionUnit.h"
#include "lldb/Host/Host.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb_private;

//------------------------------------------------------------------
/// Constructor
///
/// @param[in] text
///     The text of the function.  Must be a full translation unit.
///
/// @param[in] name
///     The name of the function, as used in the text.
//------------------------------------------------------------------
ClangUtilityFunction::ClangUtilityFunction(ExecutionContextScope &exe_scope,
                                           const char *text, const char *name)
    : UtilityFunction(exe_scope, text, name) {}

ClangUtilityFunction::~ClangUtilityFunction() {}

//------------------------------------------------------------------
/// Install the utility function into a process
///
/// @param[in] diagnostic_manager
///     A diagnostic manager to report errors and warnings to.
///
/// @param[in] exe_ctx
///     The execution context to install the utility function to.
///
/// @return
///     True on success (no errors); false otherwise.
//------------------------------------------------------------------
bool ClangUtilityFunction::Install(DiagnosticManager &diagnostic_manager,
                                   ExecutionContext &exe_ctx) {
  if (m_jit_start_addr != LLDB_INVALID_ADDRESS) {
    diagnostic_manager.PutString(eDiagnosticSeverityWarning,
                                 "already installed");
    return false;
  }

  ////////////////////////////////////
  // Set up the target and compiler
  //

  Target *target = exe_ctx.GetTargetPtr();

  if (!target) {
    diagnostic_manager.PutString(eDiagnosticSeverityError, "invalid target");
    return false;
  }

  Process *process = exe_ctx.GetProcessPtr();

  if (!process) {
    diagnostic_manager.PutString(eDiagnosticSeverityError, "invalid process");
    return false;
  }

  //////////////////////////
  // Parse the expression
  //

  bool keep_result_in_memory = false;

  ResetDeclMap(exe_ctx, keep_result_in_memory);

  if (!DeclMap()->WillParse(exe_ctx, NULL)) {
    diagnostic_manager.PutString(
        eDiagnosticSeverityError,
        "current process state is unsuitable for expression parsing");
    return false;
  }

  const bool generate_debug_info = true;
  ClangExpressionParser parser(exe_ctx.GetBestExecutionContextScope(), *this,
                               generate_debug_info);

  unsigned num_errors = parser.Parse(diagnostic_manager);

  if (num_errors) {
    ResetDeclMap();

    return false;
  }

  //////////////////////////////////
  // JIT the output of the parser
  //

  bool can_interpret = false; // should stay that way

  Status jit_error = parser.PrepareForExecution(
      m_jit_start_addr, m_jit_end_addr, m_execution_unit_sp, exe_ctx,
      can_interpret, eExecutionPolicyAlways);

  if (m_jit_start_addr != LLDB_INVALID_ADDRESS) {
    m_jit_process_wp = process->shared_from_this();
    if (parser.GetGenerateDebugInfo()) {
      lldb::ModuleSP jit_module_sp(m_execution_unit_sp->GetJITModule());

      if (jit_module_sp) {
        ConstString const_func_name(FunctionName());
        FileSpec jit_file;
        jit_file.GetFilename() = const_func_name;
        jit_module_sp->SetFileSpecAndObjectName(jit_file, ConstString());
        m_jit_module_wp = jit_module_sp;
        target->GetImages().Append(jit_module_sp);
      }
    }
  }

  DeclMap()->DidParse();

  ResetDeclMap();

  if (jit_error.Success()) {
    return true;
  } else {
    const char *error_cstr = jit_error.AsCString();
    if (error_cstr && error_cstr[0]) {
      diagnostic_manager.Printf(eDiagnosticSeverityError, "%s", error_cstr);
    } else {
      diagnostic_manager.PutString(eDiagnosticSeverityError,
                                   "expression can't be interpreted or run");
    }
    return false;
  }
}

void ClangUtilityFunction::ClangUtilityFunctionHelper::ResetDeclMap(
    ExecutionContext &exe_ctx, bool keep_result_in_memory) {
  m_expr_decl_map_up.reset(
      new ClangExpressionDeclMap(keep_result_in_memory, nullptr, exe_ctx));
}
