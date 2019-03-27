//===-- ThreadPlanCallFunctionUsingABI.h --------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadPlanCallFunctionUsingABI_h_
#define liblldb_ThreadPlanCallFunctionUsingABI_h_

#include "lldb/Target/ABI.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanCallFunction.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/DerivedTypes.h"

namespace lldb_private {

class ThreadPlanCallFunctionUsingABI : public ThreadPlanCallFunction {
  // Create a thread plan to call a function at the address passed in the
  // "function" argument, this function is executed using register manipulation
  // instead of JIT. Class derives from ThreadPlanCallFunction and differs by
  // calling a alternative
  // ABI interface ABI::PrepareTrivialCall() which provides more detailed
  // information.
public:
  ThreadPlanCallFunctionUsingABI(Thread &thread,
                                 const Address &function_address,
                                 llvm::Type &function_prototype,
                                 llvm::Type &return_type,
                                 llvm::ArrayRef<ABI::CallArgument> args,
                                 const EvaluateExpressionOptions &options);

  ~ThreadPlanCallFunctionUsingABI() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;

protected:
  void SetReturnValue() override;

private:
  llvm::Type &m_return_type;
  DISALLOW_COPY_AND_ASSIGN(ThreadPlanCallFunctionUsingABI);
};

} // namespace lldb_private

#endif // liblldb_ThreadPlanCallFunctionUsingABI_h_
