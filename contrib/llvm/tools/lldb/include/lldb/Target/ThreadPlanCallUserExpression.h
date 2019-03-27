//===-- ThreadPlanCallUserExpression.h --------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadPlanCallUserExpression_h_
#define liblldb_ThreadPlanCallUserExpression_h_

#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanCallFunction.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/ArrayRef.h"

namespace lldb_private {

class ThreadPlanCallUserExpression : public ThreadPlanCallFunction {
public:
  ThreadPlanCallUserExpression(Thread &thread, Address &function,
                               llvm::ArrayRef<lldb::addr_t> args,
                               const EvaluateExpressionOptions &options,
                               lldb::UserExpressionSP &user_expression_sp);

  ~ThreadPlanCallUserExpression() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;

  void DidPush() override;

  void WillPop() override;

  lldb::StopInfoSP GetRealStopInfo() override;

  bool MischiefManaged() override;

  void TransferExpressionOwnership() { m_manage_materialization = true; }

  lldb::ExpressionVariableSP GetExpressionVariable() override {
    return m_result_var_sp;
  }

protected:
  void DoTakedown(bool success) override;
private:
  lldb::UserExpressionSP
      m_user_expression_sp; // This is currently just used to ensure the
                            // User expression the initiated this ThreadPlan
                            // lives as long as the thread plan does.
  bool m_manage_materialization = false;
  lldb::ExpressionVariableSP
      m_result_var_sp; // If we are left to manage the materialization,
                       // then stuff the result expression variable here.

  DISALLOW_COPY_AND_ASSIGN(ThreadPlanCallUserExpression);
};

} // namespace lldb_private

#endif // liblldb_ThreadPlanCallUserExpression_h_
