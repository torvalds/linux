//===-- UnwindAssembly.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_UnwindAssembly_h_
#define utility_UnwindAssembly_h_

#include "lldb/Core/PluginInterface.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class UnwindAssembly : public std::enable_shared_from_this<UnwindAssembly>,
                       public PluginInterface {
public:
  static lldb::UnwindAssemblySP FindPlugin(const ArchSpec &arch);

  ~UnwindAssembly() override;

  virtual bool
  GetNonCallSiteUnwindPlanFromAssembly(AddressRange &func, Thread &thread,
                                       UnwindPlan &unwind_plan) = 0;

  virtual bool AugmentUnwindPlanFromCallSite(AddressRange &func, Thread &thread,
                                             UnwindPlan &unwind_plan) = 0;

  virtual bool GetFastUnwindPlan(AddressRange &func, Thread &thread,
                                 UnwindPlan &unwind_plan) = 0;

  // thread may be NULL in which case we only use the Target (e.g. if this is
  // called pre-process-launch).
  virtual bool
  FirstNonPrologueInsn(AddressRange &func,
                       const lldb_private::ExecutionContext &exe_ctx,
                       Address &first_non_prologue_insn) = 0;

protected:
  UnwindAssembly(const ArchSpec &arch);
  ArchSpec m_arch;

private:
  UnwindAssembly(); // Outlaw default constructor
  DISALLOW_COPY_AND_ASSIGN(UnwindAssembly);
};

} // namespace lldb_private

#endif // utility_UnwindAssembly_h_
