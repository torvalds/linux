//===-- IRDynamicChecks.h ---------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_IRDynamicChecks_h_
#define liblldb_IRDynamicChecks_h_

#include "lldb/lldb-types.h"
#include "llvm/Pass.h"

namespace llvm {
class BasicBlock;
class CallInst;
class Constant;
class Function;
class Instruction;
class Module;
class DataLayout;
class Value;
}

namespace lldb_private {

class ClangExpressionDeclMap;
class ExecutionContext;
class Stream;

//----------------------------------------------------------------------
/// @class DynamicCheckerFunctions IRDynamicChecks.h
/// "lldb/Expression/IRDynamicChecks.h" Encapsulates dynamic check functions
/// used by expressions.
///
/// Each of the utility functions encapsulated in this class is responsible
/// for validating some data that an expression is about to use.  Examples
/// are:
///
/// a = *b;     // check that b is a valid pointer [b init];   // check that b
/// is a valid object to send "init" to
///
/// The class installs each checker function into the target process and makes
/// it available to IRDynamicChecks to use.
//----------------------------------------------------------------------
class DynamicCheckerFunctions {
public:
  //------------------------------------------------------------------
  /// Constructor
  //------------------------------------------------------------------
  DynamicCheckerFunctions();

  //------------------------------------------------------------------
  /// Destructor
  //------------------------------------------------------------------
  ~DynamicCheckerFunctions();

  //------------------------------------------------------------------
  /// Install the utility functions into a process.  This binds the instance
  /// of DynamicCheckerFunctions to that process.
  ///
  /// @param[in] diagnostic_manager
  ///     A diagnostic manager to report errors to.
  ///
  /// @param[in] exe_ctx
  ///     The execution context to install the functions into.
  ///
  /// @return
  ///     True on success; false on failure, or if the functions have
  ///     already been installed.
  //------------------------------------------------------------------
  bool Install(DiagnosticManager &diagnostic_manager,
               ExecutionContext &exe_ctx);

  bool DoCheckersExplainStop(lldb::addr_t addr, Stream &message);

  std::unique_ptr<UtilityFunction> m_valid_pointer_check;
  std::unique_ptr<UtilityFunction> m_objc_object_check;
};

//----------------------------------------------------------------------
/// @class IRDynamicChecks IRDynamicChecks.h
/// "lldb/Expression/IRDynamicChecks.h" Adds dynamic checks to a user-entered
/// expression to reduce its likelihood of crashing
///
/// When an IR function is executed in the target process, it may cause
/// crashes or hangs by dereferencing NULL pointers, trying to call
/// Objective-C methods on objects that do not respond to them, and so forth.
///
/// IRDynamicChecks adds calls to the functions in DynamicCheckerFunctions to
/// appropriate locations in an expression's IR.
//----------------------------------------------------------------------
class IRDynamicChecks : public llvm::ModulePass {
public:
  //------------------------------------------------------------------
  /// Constructor
  ///
  /// @param[in] checker_functions
  ///     The checker functions for the target process.
  ///
  /// @param[in] func_name
  ///     The name of the function to prepare for execution in the target.
  ///
  /// @param[in] decl_map
  ///     The mapping used to look up entities in the target process. In
  ///     this case, used to find objc_msgSend
  //------------------------------------------------------------------
  IRDynamicChecks(DynamicCheckerFunctions &checker_functions,
                  const char *func_name = "$__lldb_expr");

  //------------------------------------------------------------------
  /// Destructor
  //------------------------------------------------------------------
  ~IRDynamicChecks() override;

  //------------------------------------------------------------------
  /// Run this IR transformer on a single module
  ///
  /// @param[in] M
  ///     The module to run on.  This module is searched for the function
  ///     $__lldb_expr, and that function is passed to the passes one by
  ///     one.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool runOnModule(llvm::Module &M) override;

  //------------------------------------------------------------------
  /// Interface stub
  //------------------------------------------------------------------
  void assignPassManager(
      llvm::PMStack &PMS,
      llvm::PassManagerType T = llvm::PMT_ModulePassManager) override;

  //------------------------------------------------------------------
  /// Returns PMT_ModulePassManager
  //------------------------------------------------------------------
  llvm::PassManagerType getPotentialPassManagerType() const override;

private:
  //------------------------------------------------------------------
  /// A basic block-level pass to find all pointer dereferences and
  /// validate them before use.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// The top-level pass implementation
  ///
  /// @param[in] M
  ///     The module currently being processed.
  ///
  /// @param[in] BB
  ///     The basic block currently being processed.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool FindDataLoads(llvm::Module &M, llvm::BasicBlock &BB);

  std::string m_func_name; ///< The name of the function to add checks to
  DynamicCheckerFunctions
      &m_checker_functions; ///< The checker functions for the process
};

} // namespace lldb_private

#endif // liblldb_IRDynamicChecks_h_
