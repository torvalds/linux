//===----- CGOpenMPRuntimeNVPTX.h - Interface to OpenMP NVPTX Runtimes ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides a class for OpenMP runtime code generation specialized to NVPTX
// targets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGOPENMPRUNTIMENVPTX_H
#define LLVM_CLANG_LIB_CODEGEN_CGOPENMPRUNTIMENVPTX_H

#include "CGOpenMPRuntime.h"
#include "CodeGenFunction.h"
#include "clang/AST/StmtOpenMP.h"
#include "llvm/IR/CallSite.h"

namespace clang {
namespace CodeGen {

class CGOpenMPRuntimeNVPTX : public CGOpenMPRuntime {
public:
  /// Defines the execution mode.
  enum ExecutionMode {
    /// SPMD execution mode (all threads are worker threads).
    EM_SPMD,
    /// Non-SPMD execution mode (1 master thread, others are workers).
    EM_NonSPMD,
    /// Unknown execution mode (orphaned directive).
    EM_Unknown,
  };
private:
  /// Parallel outlined function work for workers to execute.
  llvm::SmallVector<llvm::Function *, 16> Work;

  struct EntryFunctionState {
    llvm::BasicBlock *ExitBB = nullptr;
  };

  class WorkerFunctionState {
  public:
    llvm::Function *WorkerFn;
    const CGFunctionInfo &CGFI;
    SourceLocation Loc;

    WorkerFunctionState(CodeGenModule &CGM, SourceLocation Loc);

  private:
    void createWorkerFunction(CodeGenModule &CGM);
  };

  ExecutionMode getExecutionMode() const;

  bool requiresFullRuntime() const { return RequiresFullRuntime; }

  /// Get barrier to synchronize all threads in a block.
  void syncCTAThreads(CodeGenFunction &CGF);

  /// Emit the worker function for the current target region.
  void emitWorkerFunction(WorkerFunctionState &WST);

  /// Helper for worker function. Emit body of worker loop.
  void emitWorkerLoop(CodeGenFunction &CGF, WorkerFunctionState &WST);

  /// Helper for non-SPMD target entry function. Guide the master and
  /// worker threads to their respective locations.
  void emitNonSPMDEntryHeader(CodeGenFunction &CGF, EntryFunctionState &EST,
                              WorkerFunctionState &WST);

  /// Signal termination of OMP execution for non-SPMD target entry
  /// function.
  void emitNonSPMDEntryFooter(CodeGenFunction &CGF, EntryFunctionState &EST);

  /// Helper for generic variables globalization prolog.
  void emitGenericVarsProlog(CodeGenFunction &CGF, SourceLocation Loc,
                             bool WithSPMDCheck = false);

  /// Helper for generic variables globalization epilog.
  void emitGenericVarsEpilog(CodeGenFunction &CGF, bool WithSPMDCheck = false);

  /// Helper for SPMD mode target directive's entry function.
  void emitSPMDEntryHeader(CodeGenFunction &CGF, EntryFunctionState &EST,
                           const OMPExecutableDirective &D);

  /// Signal termination of SPMD mode execution.
  void emitSPMDEntryFooter(CodeGenFunction &CGF, EntryFunctionState &EST);

  //
  // Base class overrides.
  //

  /// Creates offloading entry for the provided entry ID \a ID,
  /// address \a Addr, size \a Size, and flags \a Flags.
  void createOffloadEntry(llvm::Constant *ID, llvm::Constant *Addr,
                          uint64_t Size, int32_t Flags,
                          llvm::GlobalValue::LinkageTypes Linkage) override;

  /// Emit outlined function specialized for the Fork-Join
  /// programming model for applicable target directives on the NVPTX device.
  /// \param D Directive to emit.
  /// \param ParentName Name of the function that encloses the target region.
  /// \param OutlinedFn Outlined function value to be defined by this call.
  /// \param OutlinedFnID Outlined function ID value to be defined by this call.
  /// \param IsOffloadEntry True if the outlined function is an offload entry.
  /// An outlined function may not be an entry if, e.g. the if clause always
  /// evaluates to false.
  void emitNonSPMDKernel(const OMPExecutableDirective &D, StringRef ParentName,
                         llvm::Function *&OutlinedFn,
                         llvm::Constant *&OutlinedFnID, bool IsOffloadEntry,
                         const RegionCodeGenTy &CodeGen);

  /// Emit outlined function specialized for the Single Program
  /// Multiple Data programming model for applicable target directives on the
  /// NVPTX device.
  /// \param D Directive to emit.
  /// \param ParentName Name of the function that encloses the target region.
  /// \param OutlinedFn Outlined function value to be defined by this call.
  /// \param OutlinedFnID Outlined function ID value to be defined by this call.
  /// \param IsOffloadEntry True if the outlined function is an offload entry.
  /// \param CodeGen Object containing the target statements.
  /// An outlined function may not be an entry if, e.g. the if clause always
  /// evaluates to false.
  void emitSPMDKernel(const OMPExecutableDirective &D, StringRef ParentName,
                      llvm::Function *&OutlinedFn,
                      llvm::Constant *&OutlinedFnID, bool IsOffloadEntry,
                      const RegionCodeGenTy &CodeGen);

  /// Emit outlined function for 'target' directive on the NVPTX
  /// device.
  /// \param D Directive to emit.
  /// \param ParentName Name of the function that encloses the target region.
  /// \param OutlinedFn Outlined function value to be defined by this call.
  /// \param OutlinedFnID Outlined function ID value to be defined by this call.
  /// \param IsOffloadEntry True if the outlined function is an offload entry.
  /// An outlined function may not be an entry if, e.g. the if clause always
  /// evaluates to false.
  void emitTargetOutlinedFunction(const OMPExecutableDirective &D,
                                  StringRef ParentName,
                                  llvm::Function *&OutlinedFn,
                                  llvm::Constant *&OutlinedFnID,
                                  bool IsOffloadEntry,
                                  const RegionCodeGenTy &CodeGen) override;

  /// Emits code for parallel or serial call of the \a OutlinedFn with
  /// variables captured in a record which address is stored in \a
  /// CapturedStruct.
  /// This call is for the Non-SPMD Execution Mode.
  /// \param OutlinedFn Outlined function to be run in parallel threads. Type of
  /// this function is void(*)(kmp_int32 *, kmp_int32, struct context_vars*).
  /// \param CapturedVars A pointer to the record with the references to
  /// variables used in \a OutlinedFn function.
  /// \param IfCond Condition in the associated 'if' clause, if it was
  /// specified, nullptr otherwise.
  void emitNonSPMDParallelCall(CodeGenFunction &CGF, SourceLocation Loc,
                               llvm::Value *OutlinedFn,
                               ArrayRef<llvm::Value *> CapturedVars,
                               const Expr *IfCond);

  /// Emits code for parallel or serial call of the \a OutlinedFn with
  /// variables captured in a record which address is stored in \a
  /// CapturedStruct.
  /// This call is for a parallel directive within an SPMD target directive.
  /// \param OutlinedFn Outlined function to be run in parallel threads. Type of
  /// this function is void(*)(kmp_int32 *, kmp_int32, struct context_vars*).
  /// \param CapturedVars A pointer to the record with the references to
  /// variables used in \a OutlinedFn function.
  /// \param IfCond Condition in the associated 'if' clause, if it was
  /// specified, nullptr otherwise.
  ///
  void emitSPMDParallelCall(CodeGenFunction &CGF, SourceLocation Loc,
                            llvm::Value *OutlinedFn,
                            ArrayRef<llvm::Value *> CapturedVars,
                            const Expr *IfCond);

protected:
  /// Get the function name of an outlined region.
  //  The name can be customized depending on the target.
  //
  StringRef getOutlinedHelperName() const override {
    return "__omp_outlined__";
  }

  /// Check if the default location must be constant.
  /// Constant for NVPTX for better optimization.
  bool isDefaultLocationConstant() const override { return true; }

  /// Returns additional flags that can be stored in reserved_2 field of the
  /// default location.
  /// For NVPTX target contains data about SPMD/Non-SPMD execution mode +
  /// Full/Lightweight runtime mode. Used for better optimization.
  unsigned getDefaultLocationReserved2Flags() const override;

public:
  explicit CGOpenMPRuntimeNVPTX(CodeGenModule &CGM);
  void clear() override;

  /// Emit call to void __kmpc_push_proc_bind(ident_t *loc, kmp_int32
  /// global_tid, int proc_bind) to generate code for 'proc_bind' clause.
  virtual void emitProcBindClause(CodeGenFunction &CGF,
                                  OpenMPProcBindClauseKind ProcBind,
                                  SourceLocation Loc) override;

  /// Emits call to void __kmpc_push_num_threads(ident_t *loc, kmp_int32
  /// global_tid, kmp_int32 num_threads) to generate code for 'num_threads'
  /// clause.
  /// \param NumThreads An integer value of threads.
  virtual void emitNumThreadsClause(CodeGenFunction &CGF,
                                    llvm::Value *NumThreads,
                                    SourceLocation Loc) override;

  /// This function ought to emit, in the general case, a call to
  // the openmp runtime kmpc_push_num_teams. In NVPTX backend it is not needed
  // as these numbers are obtained through the PTX grid and block configuration.
  /// \param NumTeams An integer expression of teams.
  /// \param ThreadLimit An integer expression of threads.
  void emitNumTeamsClause(CodeGenFunction &CGF, const Expr *NumTeams,
                          const Expr *ThreadLimit, SourceLocation Loc) override;

  /// Emits inlined function for the specified OpenMP parallel
  //  directive.
  /// \a D. This outlined function has type void(*)(kmp_int32 *ThreadID,
  /// kmp_int32 BoundID, struct context_vars*).
  /// \param D OpenMP directive.
  /// \param ThreadIDVar Variable for thread id in the current OpenMP region.
  /// \param InnermostKind Kind of innermost directive (for simple directives it
  /// is a directive itself, for combined - its innermost directive).
  /// \param CodeGen Code generation sequence for the \a D directive.
  llvm::Value *
  emitParallelOutlinedFunction(const OMPExecutableDirective &D,
                               const VarDecl *ThreadIDVar,
                               OpenMPDirectiveKind InnermostKind,
                               const RegionCodeGenTy &CodeGen) override;

  /// Emits inlined function for the specified OpenMP teams
  //  directive.
  /// \a D. This outlined function has type void(*)(kmp_int32 *ThreadID,
  /// kmp_int32 BoundID, struct context_vars*).
  /// \param D OpenMP directive.
  /// \param ThreadIDVar Variable for thread id in the current OpenMP region.
  /// \param InnermostKind Kind of innermost directive (for simple directives it
  /// is a directive itself, for combined - its innermost directive).
  /// \param CodeGen Code generation sequence for the \a D directive.
  llvm::Value *
  emitTeamsOutlinedFunction(const OMPExecutableDirective &D,
                            const VarDecl *ThreadIDVar,
                            OpenMPDirectiveKind InnermostKind,
                            const RegionCodeGenTy &CodeGen) override;

  /// Emits code for teams call of the \a OutlinedFn with
  /// variables captured in a record which address is stored in \a
  /// CapturedStruct.
  /// \param OutlinedFn Outlined function to be run by team masters. Type of
  /// this function is void(*)(kmp_int32 *, kmp_int32, struct context_vars*).
  /// \param CapturedVars A pointer to the record with the references to
  /// variables used in \a OutlinedFn function.
  ///
  void emitTeamsCall(CodeGenFunction &CGF, const OMPExecutableDirective &D,
                     SourceLocation Loc, llvm::Value *OutlinedFn,
                     ArrayRef<llvm::Value *> CapturedVars) override;

  /// Emits code for parallel or serial call of the \a OutlinedFn with
  /// variables captured in a record which address is stored in \a
  /// CapturedStruct.
  /// \param OutlinedFn Outlined function to be run in parallel threads. Type of
  /// this function is void(*)(kmp_int32 *, kmp_int32, struct context_vars*).
  /// \param CapturedVars A pointer to the record with the references to
  /// variables used in \a OutlinedFn function.
  /// \param IfCond Condition in the associated 'if' clause, if it was
  /// specified, nullptr otherwise.
  void emitParallelCall(CodeGenFunction &CGF, SourceLocation Loc,
                        llvm::Value *OutlinedFn,
                        ArrayRef<llvm::Value *> CapturedVars,
                        const Expr *IfCond) override;

  /// Emit an implicit/explicit barrier for OpenMP threads.
  /// \param Kind Directive for which this implicit barrier call must be
  /// generated. Must be OMPD_barrier for explicit barrier generation.
  /// \param EmitChecks true if need to emit checks for cancellation barriers.
  /// \param ForceSimpleCall true simple barrier call must be emitted, false if
  /// runtime class decides which one to emit (simple or with cancellation
  /// checks).
  ///
  void emitBarrierCall(CodeGenFunction &CGF, SourceLocation Loc,
                       OpenMPDirectiveKind Kind, bool EmitChecks = true,
                       bool ForceSimpleCall = false) override;

  /// Emits a critical region.
  /// \param CriticalName Name of the critical region.
  /// \param CriticalOpGen Generator for the statement associated with the given
  /// critical region.
  /// \param Hint Value of the 'hint' clause (optional).
  void emitCriticalRegion(CodeGenFunction &CGF, StringRef CriticalName,
                          const RegionCodeGenTy &CriticalOpGen,
                          SourceLocation Loc,
                          const Expr *Hint = nullptr) override;

  /// Emit a code for reduction clause.
  ///
  /// \param Privates List of private copies for original reduction arguments.
  /// \param LHSExprs List of LHS in \a ReductionOps reduction operations.
  /// \param RHSExprs List of RHS in \a ReductionOps reduction operations.
  /// \param ReductionOps List of reduction operations in form 'LHS binop RHS'
  /// or 'operator binop(LHS, RHS)'.
  /// \param Options List of options for reduction codegen:
  ///     WithNowait true if parent directive has also nowait clause, false
  ///     otherwise.
  ///     SimpleReduction Emit reduction operation only. Used for omp simd
  ///     directive on the host.
  ///     ReductionKind The kind of reduction to perform.
  virtual void emitReduction(CodeGenFunction &CGF, SourceLocation Loc,
                             ArrayRef<const Expr *> Privates,
                             ArrayRef<const Expr *> LHSExprs,
                             ArrayRef<const Expr *> RHSExprs,
                             ArrayRef<const Expr *> ReductionOps,
                             ReductionOptionsTy Options) override;

  /// Returns specified OpenMP runtime function for the current OpenMP
  /// implementation.  Specialized for the NVPTX device.
  /// \param Function OpenMP runtime function.
  /// \return Specified function.
  llvm::Constant *createNVPTXRuntimeFunction(unsigned Function);

  /// Translates the native parameter of outlined function if this is required
  /// for target.
  /// \param FD Field decl from captured record for the parameter.
  /// \param NativeParam Parameter itself.
  const VarDecl *translateParameter(const FieldDecl *FD,
                                    const VarDecl *NativeParam) const override;

  /// Gets the address of the native argument basing on the address of the
  /// target-specific parameter.
  /// \param NativeParam Parameter itself.
  /// \param TargetParam Corresponding target-specific parameter.
  Address getParameterAddress(CodeGenFunction &CGF, const VarDecl *NativeParam,
                              const VarDecl *TargetParam) const override;

  /// Emits call of the outlined function with the provided arguments,
  /// translating these arguments to correct target-specific arguments.
  void emitOutlinedFunctionCall(
      CodeGenFunction &CGF, SourceLocation Loc, llvm::Value *OutlinedFn,
      ArrayRef<llvm::Value *> Args = llvm::None) const override;

  /// Emits OpenMP-specific function prolog.
  /// Required for device constructs.
  void emitFunctionProlog(CodeGenFunction &CGF, const Decl *D) override;

  /// Gets the OpenMP-specific address of the local variable.
  Address getAddressOfLocalVariable(CodeGenFunction &CGF,
                                    const VarDecl *VD) override;

  /// Target codegen is specialized based on two data-sharing modes: CUDA, in
  /// which the local variables are actually global threadlocal, and Generic, in
  /// which the local variables are placed in global memory if they may escape
  /// their declaration context.
  enum DataSharingMode {
    /// CUDA data sharing mode.
    CUDA,
    /// Generic data-sharing mode.
    Generic,
  };

  /// Cleans up references to the objects in finished function.
  ///
  void functionFinished(CodeGenFunction &CGF) override;

  /// Choose a default value for the dist_schedule clause.
  void getDefaultDistScheduleAndChunk(CodeGenFunction &CGF,
      const OMPLoopDirective &S, OpenMPDistScheduleClauseKind &ScheduleKind,
      llvm::Value *&Chunk) const override;

  /// Choose a default value for the schedule clause.
  void getDefaultScheduleAndChunk(CodeGenFunction &CGF,
      const OMPLoopDirective &S, OpenMPScheduleClauseKind &ScheduleKind,
      const Expr *&ChunkExpr) const override;

  /// Adjust some parameters for the target-based directives, like addresses of
  /// the variables captured by reference in lambdas.
  void adjustTargetSpecificDataForLambdas(
      CodeGenFunction &CGF, const OMPExecutableDirective &D) const override;

  /// Perform check on requires decl to ensure that target architecture
  /// supports unified addressing
  void checkArchForUnifiedAddressing(CodeGenModule &CGM,
                                     const OMPRequiresDecl *D) const override;

private:
  /// Track the execution mode when codegening directives within a target
  /// region. The appropriate mode (SPMD/NON-SPMD) is set on entry to the
  /// target region and used by containing directives such as 'parallel'
  /// to emit optimized code.
  ExecutionMode CurrentExecutionMode = EM_Unknown;

  /// Check if the full runtime is required (default - yes).
  bool RequiresFullRuntime = true;

  /// true if we're emitting the code for the target region and next parallel
  /// region is L0 for sure.
  bool IsInTargetMasterThreadRegion = false;
  /// true if currently emitting code for target/teams/distribute region, false
  /// - otherwise.
  bool IsInTTDRegion = false;
  /// true if we're definitely in the parallel region.
  bool IsInParallelRegion = false;

  /// Map between an outlined function and its wrapper.
  llvm::DenseMap<llvm::Function *, llvm::Function *> WrapperFunctionsMap;

  /// Emit function which wraps the outline parallel region
  /// and controls the parameters which are passed to this function.
  /// The wrapper ensures that the outlined function is called
  /// with the correct arguments when data is shared.
  llvm::Function *createParallelDataSharingWrapper(
      llvm::Function *OutlinedParallelFn, const OMPExecutableDirective &D);

  /// The data for the single globalized variable.
  struct MappedVarData {
    /// Corresponding field in the global record.
    const FieldDecl *FD = nullptr;
    /// Corresponding address.
    Address PrivateAddr = Address::invalid();
    /// true, if only one element is required (for latprivates in SPMD mode),
    /// false, if need to create based on the warp-size.
    bool IsOnePerTeam = false;
    MappedVarData() = delete;
    MappedVarData(const FieldDecl *FD, bool IsOnePerTeam = false)
        : FD(FD), IsOnePerTeam(IsOnePerTeam) {}
  };
  /// The map of local variables to their addresses in the global memory.
  using DeclToAddrMapTy = llvm::MapVector<const Decl *, MappedVarData>;
  /// Set of the parameters passed by value escaping OpenMP context.
  using EscapedParamsTy = llvm::SmallPtrSet<const Decl *, 4>;
  struct FunctionData {
    DeclToAddrMapTy LocalVarData;
    llvm::Optional<DeclToAddrMapTy> SecondaryLocalVarData = llvm::None;
    EscapedParamsTy EscapedParameters;
    llvm::SmallVector<const ValueDecl*, 4> EscapedVariableLengthDecls;
    llvm::SmallVector<llvm::Value *, 4> EscapedVariableLengthDeclsAddrs;
    const RecordDecl *GlobalRecord = nullptr;
    llvm::Optional<const RecordDecl *> SecondaryGlobalRecord = llvm::None;
    llvm::Value *GlobalRecordAddr = nullptr;
    llvm::Value *IsInSPMDModeFlag = nullptr;
    std::unique_ptr<CodeGenFunction::OMPMapVars> MappedParams;
  };
  /// Maps the function to the list of the globalized variables with their
  /// addresses.
  llvm::SmallDenseMap<llvm::Function *, FunctionData> FunctionGlobalizedDecls;
  /// List of records for the globalized variables in target/teams/distribute
  /// contexts. Inner records are going to be joined into the single record,
  /// while those resulting records are going to be joined into the single
  /// union. This resulting union (one per CU) is the entry point for the static
  /// memory management runtime functions.
  struct GlobalPtrSizeRecsTy {
    llvm::GlobalVariable *UseSharedMemory = nullptr;
    llvm::GlobalVariable *RecSize = nullptr;
    llvm::GlobalVariable *Buffer = nullptr;
    SourceLocation Loc;
    llvm::SmallVector<const RecordDecl *, 2> Records;
    unsigned RegionCounter = 0;
  };
  llvm::SmallVector<GlobalPtrSizeRecsTy, 8> GlobalizedRecords;
  /// Shared pointer for the global memory in the global memory buffer used for
  /// the given kernel.
  llvm::GlobalVariable *KernelStaticGlobalized = nullptr;
  /// Pair of the Non-SPMD team and all reductions variables in this team
  /// region.
  std::pair<const Decl *, llvm::SmallVector<const ValueDecl *, 4>>
      TeamAndReductions;
};

} // CodeGen namespace.
} // clang namespace.

#endif // LLVM_CLANG_LIB_CODEGEN_CGOPENMPRUNTIMENVPTX_H
