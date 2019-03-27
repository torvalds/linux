//===-- IRForTarget.h ---------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_IRForTarget_h_
#define liblldb_IRForTarget_h_

#include "lldb/Symbol/TaggedASTType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-public.h"
#include "llvm/Pass.h"

#include <functional>
#include <map>

namespace llvm {
class BasicBlock;
class CallInst;
class Constant;
class ConstantInt;
class Function;
class GlobalValue;
class GlobalVariable;
class Instruction;
class IntegerType;
class Module;
class StoreInst;
class DataLayout;
class Type;
class Value;
}

namespace lldb_private {
class ClangExpressionDeclMap;
class IRExecutionUnit;
class IRMemoryMap;
}

//----------------------------------------------------------------------
/// @class IRForTarget IRForTarget.h "lldb/Expression/IRForTarget.h"
/// Transforms the IR for a function to run in the target
///
/// Once an expression has been parsed and converted to IR, it can run in two
/// contexts: interpreted by LLDB as a DWARF location expression, or compiled
/// by the JIT and inserted into the target process for execution.
///
/// IRForTarget makes the second possible, by applying a series of
/// transformations to the IR which make it relocatable.  These
/// transformations are discussed in more detail next to their relevant
/// functions.
//----------------------------------------------------------------------
class IRForTarget : public llvm::ModulePass {
public:
  enum class LookupResult { Success, Fail, Ignore };

  //------------------------------------------------------------------
  /// Constructor
  ///
  /// @param[in] decl_map
  ///     The list of externally-referenced variables for the expression,
  ///     for use in looking up globals and allocating the argument
  ///     struct.  See the documentation for ClangExpressionDeclMap.
  ///
  /// @param[in] resolve_vars
  ///     True if the external variable references (including persistent
  ///     variables) should be resolved.  If not, only external functions
  ///     are resolved.
  ///
  /// @param[in] execution_policy
  ///     Determines whether an IR interpreter can be used to statically
  ///     evaluate the expression.
  ///
  /// @param[in] const_result
  ///     This variable is populated with the statically-computed result
  ///     of the function, if it has no side-effects and the result can
  ///     be computed statically.
  ///
  /// @param[in] execution_unit
  ///     The holder for raw data associated with the expression.
  ///
  /// @param[in] error_stream
  ///     If non-NULL, a stream on which errors can be printed.
  ///
  /// @param[in] func_name
  ///     The name of the function to prepare for execution in the target.
  //------------------------------------------------------------------
  IRForTarget(lldb_private::ClangExpressionDeclMap *decl_map, bool resolve_vars,
              lldb_private::IRExecutionUnit &execution_unit,
              lldb_private::Stream &error_stream,
              const char *func_name = "$__lldb_expr");

  //------------------------------------------------------------------
  /// Destructor
  //------------------------------------------------------------------
  ~IRForTarget() override;

  //------------------------------------------------------------------
  /// Run this IR transformer on a single module
  ///
  /// Implementation of the llvm::ModulePass::runOnModule() function.
  ///
  /// @param[in] llvm_module
  ///     The module to run on.  This module is searched for the function
  ///     $__lldb_expr, and that function is passed to the passes one by
  ///     one.
  ///
  /// @param[in] interpreter_error
  ///     An error.  If the expression fails to be interpreted, this error
  ///     is set to a reason why.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool runOnModule(llvm::Module &llvm_module) override;

  //------------------------------------------------------------------
  /// Interface stub
  ///
  /// Implementation of the llvm::ModulePass::assignPassManager() function.
  //------------------------------------------------------------------
  void assignPassManager(llvm::PMStack &pass_mgr_stack,
                         llvm::PassManagerType pass_mgr_type =
                             llvm::PMT_ModulePassManager) override;

  //------------------------------------------------------------------
  /// Returns PMT_ModulePassManager
  ///
  /// Implementation of the llvm::ModulePass::getPotentialPassManagerType()
  /// function.
  //------------------------------------------------------------------
  llvm::PassManagerType getPotentialPassManagerType() const override;

private:
  //------------------------------------------------------------------
  /// Ensures that the current function's linkage is set to external.
  /// Otherwise the JIT may not return an address for it.
  ///
  /// @param[in] llvm_function
  ///     The function whose linkage is to be fixed.
  ///
  /// @return
  ///     True on success; false otherwise.
  //------------------------------------------------------------------
  bool FixFunctionLinkage(llvm::Function &llvm_function);

  //------------------------------------------------------------------
  /// A module-level pass to replace all function pointers with their
  /// integer equivalents.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// The top-level pass implementation
  ///
  /// @param[in] llvm_module
  ///     The module currently being processed.
  ///
  /// @param[in] llvm_function
  ///     The function currently being processed.
  ///
  /// @return
  ///     True on success; false otherwise.
  //------------------------------------------------------------------
  bool HasSideEffects(llvm::Function &llvm_function);

  //------------------------------------------------------------------
  /// A function-level pass to check whether the function has side
  /// effects.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Get the address of a function, and a location to put the complete Value
  /// of the function if one is available.
  ///
  /// @param[in] function
  ///     The function to find the location of.
  ///
  /// @param[out] ptr
  ///     The location of the function in the target.
  ///
  /// @param[out] name
  ///     The resolved name of the function (matters for intrinsics).
  ///
  /// @param[out] value_ptr
  ///     A variable to put the function's completed Value* in, or NULL
  ///     if the Value* shouldn't be stored anywhere.
  ///
  /// @return
  ///     The pointer.
  //------------------------------------------------------------------
  LookupResult GetFunctionAddress(llvm::Function *function, uint64_t &ptr,
                                  lldb_private::ConstString &name,
                                  llvm::Constant **&value_ptr);

  //------------------------------------------------------------------
  /// A function-level pass to take the generated global value
  /// $__lldb_expr_result and make it into a persistent variable. Also see
  /// ASTResultSynthesizer.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Find the NamedDecl corresponding to a Value.  This interface is exposed
  /// for the IR interpreter.
  ///
  /// @param[in] module
  ///     The module containing metadata to search
  ///
  /// @param[in] global
  ///     The global entity to search for
  ///
  /// @return
  ///     The corresponding variable declaration
  //------------------------------------------------------------------
public:
  static clang::NamedDecl *DeclForGlobal(const llvm::GlobalValue *global_val,
                                         llvm::Module *module);

private:
  clang::NamedDecl *DeclForGlobal(llvm::GlobalValue *global);

  //------------------------------------------------------------------
  /// Set the constant result variable m_const_result to the provided
  /// constant, assuming it can be evaluated.  The result variable will be
  /// reset to NULL later if the expression has side effects.
  ///
  /// @param[in] initializer
  ///     The constant initializer for the variable.
  ///
  /// @param[in] name
  ///     The name of the result variable.
  ///
  /// @param[in] type
  ///     The Clang type of the result variable.
  //------------------------------------------------------------------
  void MaybeSetConstantResult(llvm::Constant *initializer,
                              const lldb_private::ConstString &name,
                              lldb_private::TypeFromParser type);

  //------------------------------------------------------------------
  /// If the IR represents a cast of a variable, set m_const_result to the
  /// result of the cast.  The result variable will be reset to
  /// NULL latger if the expression has side effects.
  ///
  /// @param[in] type
  ///     The Clang type of the result variable.
  //------------------------------------------------------------------
  void MaybeSetCastResult(lldb_private::TypeFromParser type);

  //------------------------------------------------------------------
  /// The top-level pass implementation
  ///
  /// @param[in] llvm_function
  ///     The function currently being processed.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool CreateResultVariable(llvm::Function &llvm_function);

  //------------------------------------------------------------------
  /// A module-level pass to find Objective-C constant strings and
  /// transform them to calls to CFStringCreateWithBytes.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Rewrite a single Objective-C constant string.
  ///
  /// @param[in] NSStr
  ///     The constant NSString to be transformed
  ///
  /// @param[in] CStr
  ///     The constant C string inside the NSString.  This will be
  ///     passed as the bytes argument to CFStringCreateWithBytes.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool RewriteObjCConstString(llvm::GlobalVariable *NSStr,
                              llvm::GlobalVariable *CStr);

  //------------------------------------------------------------------
  /// The top-level pass implementation
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool RewriteObjCConstStrings();

  //------------------------------------------------------------------
  /// A basic block-level pass to find all Objective-C method calls and
  /// rewrite them to use sel_registerName instead of statically allocated
  /// selectors.  The reason is that the selectors are created on the
  /// assumption that the Objective-C runtime will scan the appropriate
  /// section and prepare them.  This doesn't happen when code is copied into
  /// the target, though, and there's no easy way to induce the runtime to
  /// scan them.  So instead we get our selectors from sel_registerName.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Replace a single selector reference
  ///
  /// @param[in] selector_load
  ///     The load of the statically-allocated selector.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool RewriteObjCSelector(llvm::Instruction *selector_load);

  //------------------------------------------------------------------
  /// The top-level pass implementation
  ///
  /// @param[in] basic_block
  ///     The basic block currently being processed.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool RewriteObjCSelectors(llvm::BasicBlock &basic_block);

  //------------------------------------------------------------------
  /// A basic block-level pass to find all Objective-C class references that
  /// use the old-style Objective-C runtime and rewrite them to use
  /// class_getClass instead of statically allocated class references.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Replace a single old-style class reference
  ///
  /// @param[in] selector_load
  ///     The load of the statically-allocated selector.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool RewriteObjCClassReference(llvm::Instruction *class_load);

  //------------------------------------------------------------------
  /// The top-level pass implementation
  ///
  /// @param[in] basic_block
  ///     The basic block currently being processed.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool RewriteObjCClassReferences(llvm::BasicBlock &basic_block);

  //------------------------------------------------------------------
  /// A basic block-level pass to find all newly-declared persistent
  /// variables and register them with the ClangExprDeclMap.  This allows them
  /// to be materialized and dematerialized like normal external variables.
  /// Before transformation, these persistent variables look like normal
  /// locals, so they have an allocation. This pass excises these allocations
  /// and makes references look like external references where they will be
  /// resolved -- like all other external references -- by ResolveExternals().
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Handle a single allocation of a persistent variable
  ///
  /// @param[in] persistent_alloc
  ///     The allocation of the persistent variable.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool RewritePersistentAlloc(llvm::Instruction *persistent_alloc);

  //------------------------------------------------------------------
  /// The top-level pass implementation
  ///
  /// @param[in] basic_block
  ///     The basic block currently being processed.
  //------------------------------------------------------------------
  bool RewritePersistentAllocs(llvm::BasicBlock &basic_block);

  //------------------------------------------------------------------
  /// A function-level pass to find all external variables and functions
  /// used in the IR.  Each found external variable is added to the struct,
  /// and each external function is resolved in place, its call replaced with
  /// a call to a function pointer whose value is the address of the function
  /// in the target process.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Write an initializer to a memory array of assumed sufficient size.
  ///
  /// @param[in] data
  ///     A pointer to the data to write to.
  ///
  /// @param[in] initializer
  ///     The initializer itself.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool MaterializeInitializer(uint8_t *data, llvm::Constant *initializer);

  //------------------------------------------------------------------
  /// Move an internal variable into the static allocation section.
  ///
  /// @param[in] global_variable
  ///     The variable.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool MaterializeInternalVariable(llvm::GlobalVariable *global_variable);

  //------------------------------------------------------------------
  /// Handle a single externally-defined variable
  ///
  /// @param[in] value
  ///     The variable.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool MaybeHandleVariable(llvm::Value *value);

  //------------------------------------------------------------------
  /// Handle a single externally-defined symbol
  ///
  /// @param[in] symbol
  ///     The symbol.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool HandleSymbol(llvm::Value *symbol);

  //------------------------------------------------------------------
  /// Handle a single externally-defined Objective-C class
  ///
  /// @param[in] classlist_reference
  ///     The reference, usually "01L_OBJC_CLASSLIST_REFERENCES_$_n"
  ///     where n (if present) is an index.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool HandleObjCClass(llvm::Value *classlist_reference);

  //------------------------------------------------------------------
  /// Handle all the arguments to a function call
  ///
  /// @param[in] C
  ///     The call instruction.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool MaybeHandleCallArguments(llvm::CallInst *call_inst);

  //------------------------------------------------------------------
  /// Resolve variable references in calls to external functions
  ///
  /// @param[in] basic_block
  ///     The basic block currently being processed.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool ResolveCalls(llvm::BasicBlock &basic_block);

  //------------------------------------------------------------------
  /// Remove calls to __cxa_atexit, which should never be generated by
  /// expressions.
  ///
  /// @param[in] call_inst
  ///     The call instruction.
  ///
  /// @return
  ///     True if the scan was successful; false if some operation
  ///     failed
  //------------------------------------------------------------------
  bool RemoveCXAAtExit(llvm::BasicBlock &basic_block);

  //------------------------------------------------------------------
  /// The top-level pass implementation
  ///
  /// @param[in] basic_block
  ///     The function currently being processed.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool ResolveExternals(llvm::Function &llvm_function);

  //------------------------------------------------------------------
  /// A basic block-level pass to excise guard variables from the code.
  /// The result for the function is passed through Clang as a static
  /// variable.  Static variables normally have guard variables to ensure that
  /// they are only initialized once.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// Rewrite a load to a guard variable to return constant 0.
  ///
  /// @param[in] guard_load
  ///     The load instruction to zero out.
  //------------------------------------------------------------------
  void TurnGuardLoadIntoZero(llvm::Instruction *guard_load);

  //------------------------------------------------------------------
  /// The top-level pass implementation
  ///
  /// @param[in] basic_block
  ///     The basic block currently being processed.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool RemoveGuards(llvm::BasicBlock &basic_block);

  //------------------------------------------------------------------
  /// A function-level pass to make all external variable references
  /// point at the correct offsets from the void* passed into the function.
  /// ClangExpressionDeclMap::DoStructLayout() must be called beforehand, so
  /// that the offsets are valid.
  //------------------------------------------------------------------

  //------------------------------------------------------------------
  /// The top-level pass implementation
  ///
  /// @param[in] llvm_function
  ///     The function currently being processed.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool ReplaceVariables(llvm::Function &llvm_function);

  /// Flags
  bool m_resolve_vars; ///< True if external variable references and persistent
                       ///variable references should be resolved
  lldb_private::ConstString
      m_func_name; ///< The name of the function to translate
  lldb_private::ConstString
      m_result_name; ///< The name of the result variable ($0, $1, ...)
  lldb_private::TypeFromParser
      m_result_type;      ///< The type of the result variable.
  llvm::Module *m_module; ///< The module being processed, or NULL if that has
                          ///not been determined yet.
  std::unique_ptr<llvm::DataLayout> m_target_data; ///< The target data for the
                                                   ///module being processed, or
                                                   ///NULL if there is no
                                                   ///module.
  lldb_private::ClangExpressionDeclMap
      *m_decl_map; ///< The DeclMap containing the Decls
  llvm::Constant *m_CFStringCreateWithBytes; ///< The address of the function
                                             ///CFStringCreateWithBytes, cast to
                                             ///the
  /// appropriate function pointer type
  llvm::Constant *m_sel_registerName; ///< The address of the function
                                      ///sel_registerName, cast to the
                                      ///appropriate
                                      /// function pointer type
  llvm::Constant *m_objc_getClass; ///< The address of the function
                                   ///objc_getClass, cast to the
                                   ///appropriate
                                   /// function pointer type
  llvm::IntegerType
      *m_intptr_ty; ///< The type of an integer large enough to hold a pointer.
  lldb_private::Stream
      &m_error_stream; ///< The stream on which errors should be printed
  lldb_private::IRExecutionUnit &
      m_execution_unit; ///< The execution unit containing the IR being created.

  llvm::StoreInst *m_result_store; ///< If non-NULL, the store instruction that
                                   ///writes to the result variable.  If
                                   /// m_has_side_effects is true, this is
                                   /// NULL.
  bool m_result_is_pointer; ///< True if the function's result in the AST is a
                            ///pointer (see comments in
                            /// ASTResultSynthesizer::SynthesizeBodyResult)

  llvm::GlobalVariable *m_reloc_placeholder; ///< A placeholder that will be
                                             ///replaced by a pointer to the
                                             ///final
  /// location of the static allocation.

  //------------------------------------------------------------------
  /// UnfoldConstant operates on a constant [Old] which has just been replaced
  /// with a value [New].  We assume that new_value has been properly placed
  /// early in the function, in front of the first instruction in the entry
  /// basic block [FirstEntryInstruction].
  ///
  /// UnfoldConstant reads through the uses of Old and replaces Old in those
  /// uses with New.  Where those uses are constants, the function generates
  /// new instructions to compute the result of the new, non-constant
  /// expression and places them before FirstEntryInstruction.  These
  /// instructions replace the constant uses, so UnfoldConstant calls itself
  /// recursively for those.
  ///
  /// @param[in] llvm_function
  ///     The function currently being processed.
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------

  class FunctionValueCache {
  public:
    typedef std::function<llvm::Value *(llvm::Function *)> Maker;

    FunctionValueCache(Maker const &maker);
    ~FunctionValueCache();
    llvm::Value *GetValue(llvm::Function *function);

  private:
    Maker const m_maker;
    typedef std::map<llvm::Function *, llvm::Value *> FunctionValueMap;
    FunctionValueMap m_values;
  };

  FunctionValueCache m_entry_instruction_finder;

  static bool UnfoldConstant(llvm::Constant *old_constant,
                             llvm::Function *llvm_function,
                             FunctionValueCache &value_maker,
                             FunctionValueCache &entry_instruction_finder,
                             lldb_private::Stream &error_stream);

  //------------------------------------------------------------------
  /// Construct a reference to m_reloc_placeholder with a given type and
  /// offset.  This typically happens after inserting data into
  /// m_data_allocator.
  ///
  /// @param[in] type
  ///     The type of the value being loaded.
  ///
  /// @param[in] offset
  ///     The offset of the value from the base of m_data_allocator.
  ///
  /// @return
  ///     The Constant for the reference, usually a ConstantExpr.
  //------------------------------------------------------------------
  llvm::Constant *BuildRelocation(llvm::Type *type, uint64_t offset);

  //------------------------------------------------------------------
  /// Commit the allocation in m_data_allocator and use its final location to
  /// replace m_reloc_placeholder.
  ///
  /// @param[in] module
  ///     The module that m_data_allocator resides in
  ///
  /// @return
  ///     True on success; false otherwise
  //------------------------------------------------------------------
  bool CompleteDataAllocation();
};

#endif // liblldb_IRForTarget_h_
