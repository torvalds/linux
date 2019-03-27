//===-- ExecutionContextScope.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ExecutionContextScope_h_
#define liblldb_ExecutionContextScope_h_

#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class ExecutionContextScope ExecutionContextScope.h
/// "lldb/Symbol/ExecutionContextScope.h" Inherit from this if your object can
/// reconstruct its
///        execution context.
///
/// Many objects that have pointers back to parent execution context objects
/// can inherit from this pure virtual class can reconstruct their execution
/// context without having to keep a complete ExecutionContext object in the
/// object state. Examples of these objects include: Process, Thread,
/// RegisterContext and StackFrame.
///
/// Objects can contain a valid pointer to an instance of this so they can
/// reconstruct the execution context.
///
/// Objects that adhere to this protocol can reconstruct enough of a execution
/// context to allow functions that take a execution contexts to be called.
//----------------------------------------------------------------------
class ExecutionContextScope {
public:
  virtual ~ExecutionContextScope() {}

  virtual lldb::TargetSP CalculateTarget() = 0;

  virtual lldb::ProcessSP CalculateProcess() = 0;

  virtual lldb::ThreadSP CalculateThread() = 0;

  virtual lldb::StackFrameSP CalculateStackFrame() = 0;

  //------------------------------------------------------------------
  /// Reconstruct the object's execution context into \a sc.
  ///
  /// The object should fill in as much of the ExecutionContextScope as it can
  /// so function calls that require a execution context can be made for the
  /// given object.
  ///
  /// @param[out] exe_ctx
  ///     A reference to an execution context object that gets filled
  ///     in.
  //------------------------------------------------------------------
  virtual void CalculateExecutionContext(ExecutionContext &exe_ctx) = 0;
};

} // namespace lldb_private

#endif // liblldb_ExecutionContextScope_h_
