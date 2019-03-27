//===-- ClangFunctionCaller.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ClangFunctionCaller.h"

#include "ASTStructExtractor.h"
#include "ClangExpressionParser.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/IR/Module.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectList.h"
#include "lldb/Expression/IRExecutionUnit.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanCallFunction.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

using namespace lldb_private;

//----------------------------------------------------------------------
// ClangFunctionCaller constructor
//----------------------------------------------------------------------
ClangFunctionCaller::ClangFunctionCaller(ExecutionContextScope &exe_scope,
                                         const CompilerType &return_type,
                                         const Address &functionAddress,
                                         const ValueList &arg_value_list,
                                         const char *name)
    : FunctionCaller(exe_scope, return_type, functionAddress, arg_value_list,
                     name),
      m_type_system_helper(*this) {
  m_jit_process_wp = lldb::ProcessWP(exe_scope.CalculateProcess());
  // Can't make a ClangFunctionCaller without a process.
  assert(m_jit_process_wp.lock());
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
ClangFunctionCaller::~ClangFunctionCaller() {}

unsigned

ClangFunctionCaller::CompileFunction(lldb::ThreadSP thread_to_use_sp,
                                     DiagnosticManager &diagnostic_manager) {
  if (m_compiled)
    return 0;

  // Compilation might call code, make sure to keep on the thread the caller
  // indicated.
  ThreadList::ExpressionExecutionThreadPusher execution_thread_pusher(
      thread_to_use_sp);

  // FIXME: How does clang tell us there's no return value?  We need to handle
  // that case.
  unsigned num_errors = 0;

  std::string return_type_str(
      m_function_return_type.GetTypeName().AsCString(""));

  // Cons up the function we're going to wrap our call in, then compile it...
  // We declare the function "extern "C"" because the compiler might be in C++
  // mode which would mangle the name and then we couldn't find it again...
  m_wrapper_function_text.clear();
  m_wrapper_function_text.append("extern \"C\" void ");
  m_wrapper_function_text.append(m_wrapper_function_name);
  m_wrapper_function_text.append(" (void *input)\n{\n    struct ");
  m_wrapper_function_text.append(m_wrapper_struct_name);
  m_wrapper_function_text.append(" \n  {\n");
  m_wrapper_function_text.append("    ");
  m_wrapper_function_text.append(return_type_str);
  m_wrapper_function_text.append(" (*fn_ptr) (");

  // Get the number of arguments.  If we have a function type and it is
  // prototyped, trust that, otherwise use the values we were given.

  // FIXME: This will need to be extended to handle Variadic functions.  We'll
  // need
  // to pull the defined arguments out of the function, then add the types from
  // the arguments list for the variable arguments.

  uint32_t num_args = UINT32_MAX;
  bool trust_function = false;
  // GetArgumentCount returns -1 for an unprototyped function.
  CompilerType function_clang_type;
  if (m_function_ptr) {
    function_clang_type = m_function_ptr->GetCompilerType();
    if (function_clang_type) {
      int num_func_args = function_clang_type.GetFunctionArgumentCount();
      if (num_func_args >= 0) {
        trust_function = true;
        num_args = num_func_args;
      }
    }
  }

  if (num_args == UINT32_MAX)
    num_args = m_arg_values.GetSize();

  std::string args_buffer; // This one stores the definition of all the args in
                           // "struct caller".
  std::string args_list_buffer; // This one stores the argument list called from
                                // the structure.
  for (size_t i = 0; i < num_args; i++) {
    std::string type_name;

    if (trust_function) {
      type_name = function_clang_type.GetFunctionArgumentTypeAtIndex(i)
                      .GetTypeName()
                      .AsCString("");
    } else {
      CompilerType clang_qual_type =
          m_arg_values.GetValueAtIndex(i)->GetCompilerType();
      if (clang_qual_type) {
        type_name = clang_qual_type.GetTypeName().AsCString("");
      } else {
        diagnostic_manager.Printf(
            eDiagnosticSeverityError,
            "Could not determine type of input value %" PRIu64 ".",
            (uint64_t)i);
        return 1;
      }
    }

    m_wrapper_function_text.append(type_name);
    if (i < num_args - 1)
      m_wrapper_function_text.append(", ");

    char arg_buf[32];
    args_buffer.append("    ");
    args_buffer.append(type_name);
    snprintf(arg_buf, 31, "arg_%" PRIu64, (uint64_t)i);
    args_buffer.push_back(' ');
    args_buffer.append(arg_buf);
    args_buffer.append(";\n");

    args_list_buffer.append("__lldb_fn_data->");
    args_list_buffer.append(arg_buf);
    if (i < num_args - 1)
      args_list_buffer.append(", ");
  }
  m_wrapper_function_text.append(
      ");\n"); // Close off the function calling prototype.

  m_wrapper_function_text.append(args_buffer);

  m_wrapper_function_text.append("    ");
  m_wrapper_function_text.append(return_type_str);
  m_wrapper_function_text.append(" return_value;");
  m_wrapper_function_text.append("\n  };\n  struct ");
  m_wrapper_function_text.append(m_wrapper_struct_name);
  m_wrapper_function_text.append("* __lldb_fn_data = (struct ");
  m_wrapper_function_text.append(m_wrapper_struct_name);
  m_wrapper_function_text.append(" *) input;\n");

  m_wrapper_function_text.append(
      "  __lldb_fn_data->return_value = __lldb_fn_data->fn_ptr (");
  m_wrapper_function_text.append(args_list_buffer);
  m_wrapper_function_text.append(");\n}\n");

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));
  if (log)
    log->Printf("Expression: \n\n%s\n\n", m_wrapper_function_text.c_str());

  // Okay, now compile this expression

  lldb::ProcessSP jit_process_sp(m_jit_process_wp.lock());
  if (jit_process_sp) {
    const bool generate_debug_info = true;
    m_parser.reset(new ClangExpressionParser(jit_process_sp.get(), *this,
                                             generate_debug_info));

    num_errors = m_parser->Parse(diagnostic_manager);
  } else {
    diagnostic_manager.PutString(eDiagnosticSeverityError,
                                 "no process - unable to inject function");
    num_errors = 1;
  }

  m_compiled = (num_errors == 0);

  if (!m_compiled)
    return num_errors;

  return num_errors;
}

clang::ASTConsumer *
ClangFunctionCaller::ClangFunctionCallerHelper::ASTTransformer(
    clang::ASTConsumer *passthrough) {
  m_struct_extractor.reset(new ASTStructExtractor(
      passthrough, m_owner.GetWrapperStructName(), m_owner));

  return m_struct_extractor.get();
}
