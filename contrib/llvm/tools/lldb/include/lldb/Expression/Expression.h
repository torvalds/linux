//===-- Expression.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Expression_h_
#define liblldb_Expression_h_

#include <map>
#include <string>
#include <vector>


#include "lldb/Expression/ExpressionTypeSystemHelper.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class RecordingMemoryManager;

//----------------------------------------------------------------------
/// @class Expression Expression.h "lldb/Expression/Expression.h" Encapsulates
/// a single expression for use in lldb
///
/// LLDB uses expressions for various purposes, notably to call functions
/// and as a backend for the expr command.  Expression encapsulates the
/// objects needed to parse and interpret or JIT an expression.  It uses the
/// expression parser appropriate to the language of the expression to produce
/// LLVM IR from the expression.
//----------------------------------------------------------------------
class Expression {
public:
  enum ResultType { eResultTypeAny, eResultTypeId };

  Expression(Target &target);

  Expression(ExecutionContextScope &exe_scope);

  //------------------------------------------------------------------
  /// Destructor
  //------------------------------------------------------------------
  virtual ~Expression() {}

  //------------------------------------------------------------------
  /// Return the string that the parser should parse.  Must be a full
  /// translation unit.
  //------------------------------------------------------------------
  virtual const char *Text() = 0;

  //------------------------------------------------------------------
  /// Return the function name that should be used for executing the
  /// expression.  Text() should contain the definition of this function.
  //------------------------------------------------------------------
  virtual const char *FunctionName() = 0;

  //------------------------------------------------------------------
  /// Return the language that should be used when parsing.  To use the
  /// default, return eLanguageTypeUnknown.
  //------------------------------------------------------------------
  virtual lldb::LanguageType Language() { return lldb::eLanguageTypeUnknown; }

  //------------------------------------------------------------------
  /// Return the desired result type of the function, or eResultTypeAny if
  /// indifferent.
  //------------------------------------------------------------------
  virtual ResultType DesiredResultType() { return eResultTypeAny; }

  //------------------------------------------------------------------
  /// Flags
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Return true if validation code should be inserted into the expression.
  //------------------------------------------------------------------
  virtual bool NeedsValidation() = 0;

  //------------------------------------------------------------------
  /// Return true if external variables in the expression should be resolved.
  //------------------------------------------------------------------
  virtual bool NeedsVariableResolution() = 0;

  virtual EvaluateExpressionOptions *GetOptions() { return nullptr; };

  //------------------------------------------------------------------
  /// Return the address of the function's JIT-compiled code, or
  /// LLDB_INVALID_ADDRESS if the function is not JIT compiled
  //------------------------------------------------------------------
  lldb::addr_t StartAddress() { return m_jit_start_addr; }

  //------------------------------------------------------------------
  /// Called to notify the expression that it is about to be executed.
  //------------------------------------------------------------------
  virtual void WillStartExecuting() {}

  //------------------------------------------------------------------
  /// Called to notify the expression that its execution has finished.
  //------------------------------------------------------------------
  virtual void DidFinishExecuting() {}

  virtual ExpressionTypeSystemHelper *GetTypeSystemHelper() { return nullptr; }

protected:
  lldb::TargetWP m_target_wp; /// Expression's always have to have a target...
  lldb::ProcessWP m_jit_process_wp; /// An expression might have a process, but
                                    /// it doesn't need to (e.g. calculator
                                    /// mode.)
  lldb::addr_t m_jit_start_addr; ///< The address of the JITted function within
                                 ///the JIT allocation.  LLDB_INVALID_ADDRESS if
                                 ///invalid.
  lldb::addr_t m_jit_end_addr;   ///< The address of the JITted function within
                                 ///the JIT allocation.  LLDB_INVALID_ADDRESS if
                                 ///invalid.
};

} // namespace lldb_private

#endif // liblldb_Expression_h_
