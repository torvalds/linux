//===----- CGOpenMPRuntime.cpp - Interface to OpenMP Runtimes -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides a class for OpenMP runtime code generation.
//
//===----------------------------------------------------------------------===//

#include "CGCXXABI.h"
#include "CGCleanup.h"
#include "CGOpenMPRuntime.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "clang/CodeGen/ConstantInitBuilder.h"
#include "clang/AST/Decl.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/Basic/BitmaskEnum.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace clang;
using namespace CodeGen;

namespace {
/// Base class for handling code generation inside OpenMP regions.
class CGOpenMPRegionInfo : public CodeGenFunction::CGCapturedStmtInfo {
public:
  /// Kinds of OpenMP regions used in codegen.
  enum CGOpenMPRegionKind {
    /// Region with outlined function for standalone 'parallel'
    /// directive.
    ParallelOutlinedRegion,
    /// Region with outlined function for standalone 'task' directive.
    TaskOutlinedRegion,
    /// Region for constructs that do not require function outlining,
    /// like 'for', 'sections', 'atomic' etc. directives.
    InlinedRegion,
    /// Region with outlined function for standalone 'target' directive.
    TargetRegion,
  };

  CGOpenMPRegionInfo(const CapturedStmt &CS,
                     const CGOpenMPRegionKind RegionKind,
                     const RegionCodeGenTy &CodeGen, OpenMPDirectiveKind Kind,
                     bool HasCancel)
      : CGCapturedStmtInfo(CS, CR_OpenMP), RegionKind(RegionKind),
        CodeGen(CodeGen), Kind(Kind), HasCancel(HasCancel) {}

  CGOpenMPRegionInfo(const CGOpenMPRegionKind RegionKind,
                     const RegionCodeGenTy &CodeGen, OpenMPDirectiveKind Kind,
                     bool HasCancel)
      : CGCapturedStmtInfo(CR_OpenMP), RegionKind(RegionKind), CodeGen(CodeGen),
        Kind(Kind), HasCancel(HasCancel) {}

  /// Get a variable or parameter for storing global thread id
  /// inside OpenMP construct.
  virtual const VarDecl *getThreadIDVariable() const = 0;

  /// Emit the captured statement body.
  void EmitBody(CodeGenFunction &CGF, const Stmt *S) override;

  /// Get an LValue for the current ThreadID variable.
  /// \return LValue for thread id variable. This LValue always has type int32*.
  virtual LValue getThreadIDVariableLValue(CodeGenFunction &CGF);

  virtual void emitUntiedSwitch(CodeGenFunction & /*CGF*/) {}

  CGOpenMPRegionKind getRegionKind() const { return RegionKind; }

  OpenMPDirectiveKind getDirectiveKind() const { return Kind; }

  bool hasCancel() const { return HasCancel; }

  static bool classof(const CGCapturedStmtInfo *Info) {
    return Info->getKind() == CR_OpenMP;
  }

  ~CGOpenMPRegionInfo() override = default;

protected:
  CGOpenMPRegionKind RegionKind;
  RegionCodeGenTy CodeGen;
  OpenMPDirectiveKind Kind;
  bool HasCancel;
};

/// API for captured statement code generation in OpenMP constructs.
class CGOpenMPOutlinedRegionInfo final : public CGOpenMPRegionInfo {
public:
  CGOpenMPOutlinedRegionInfo(const CapturedStmt &CS, const VarDecl *ThreadIDVar,
                             const RegionCodeGenTy &CodeGen,
                             OpenMPDirectiveKind Kind, bool HasCancel,
                             StringRef HelperName)
      : CGOpenMPRegionInfo(CS, ParallelOutlinedRegion, CodeGen, Kind,
                           HasCancel),
        ThreadIDVar(ThreadIDVar), HelperName(HelperName) {
    assert(ThreadIDVar != nullptr && "No ThreadID in OpenMP region.");
  }

  /// Get a variable or parameter for storing global thread id
  /// inside OpenMP construct.
  const VarDecl *getThreadIDVariable() const override { return ThreadIDVar; }

  /// Get the name of the capture helper.
  StringRef getHelperName() const override { return HelperName; }

  static bool classof(const CGCapturedStmtInfo *Info) {
    return CGOpenMPRegionInfo::classof(Info) &&
           cast<CGOpenMPRegionInfo>(Info)->getRegionKind() ==
               ParallelOutlinedRegion;
  }

private:
  /// A variable or parameter storing global thread id for OpenMP
  /// constructs.
  const VarDecl *ThreadIDVar;
  StringRef HelperName;
};

/// API for captured statement code generation in OpenMP constructs.
class CGOpenMPTaskOutlinedRegionInfo final : public CGOpenMPRegionInfo {
public:
  class UntiedTaskActionTy final : public PrePostActionTy {
    bool Untied;
    const VarDecl *PartIDVar;
    const RegionCodeGenTy UntiedCodeGen;
    llvm::SwitchInst *UntiedSwitch = nullptr;

  public:
    UntiedTaskActionTy(bool Tied, const VarDecl *PartIDVar,
                       const RegionCodeGenTy &UntiedCodeGen)
        : Untied(!Tied), PartIDVar(PartIDVar), UntiedCodeGen(UntiedCodeGen) {}
    void Enter(CodeGenFunction &CGF) override {
      if (Untied) {
        // Emit task switching point.
        LValue PartIdLVal = CGF.EmitLoadOfPointerLValue(
            CGF.GetAddrOfLocalVar(PartIDVar),
            PartIDVar->getType()->castAs<PointerType>());
        llvm::Value *Res =
            CGF.EmitLoadOfScalar(PartIdLVal, PartIDVar->getLocation());
        llvm::BasicBlock *DoneBB = CGF.createBasicBlock(".untied.done.");
        UntiedSwitch = CGF.Builder.CreateSwitch(Res, DoneBB);
        CGF.EmitBlock(DoneBB);
        CGF.EmitBranchThroughCleanup(CGF.ReturnBlock);
        CGF.EmitBlock(CGF.createBasicBlock(".untied.jmp."));
        UntiedSwitch->addCase(CGF.Builder.getInt32(0),
                              CGF.Builder.GetInsertBlock());
        emitUntiedSwitch(CGF);
      }
    }
    void emitUntiedSwitch(CodeGenFunction &CGF) const {
      if (Untied) {
        LValue PartIdLVal = CGF.EmitLoadOfPointerLValue(
            CGF.GetAddrOfLocalVar(PartIDVar),
            PartIDVar->getType()->castAs<PointerType>());
        CGF.EmitStoreOfScalar(CGF.Builder.getInt32(UntiedSwitch->getNumCases()),
                              PartIdLVal);
        UntiedCodeGen(CGF);
        CodeGenFunction::JumpDest CurPoint =
            CGF.getJumpDestInCurrentScope(".untied.next.");
        CGF.EmitBranchThroughCleanup(CGF.ReturnBlock);
        CGF.EmitBlock(CGF.createBasicBlock(".untied.jmp."));
        UntiedSwitch->addCase(CGF.Builder.getInt32(UntiedSwitch->getNumCases()),
                              CGF.Builder.GetInsertBlock());
        CGF.EmitBranchThroughCleanup(CurPoint);
        CGF.EmitBlock(CurPoint.getBlock());
      }
    }
    unsigned getNumberOfParts() const { return UntiedSwitch->getNumCases(); }
  };
  CGOpenMPTaskOutlinedRegionInfo(const CapturedStmt &CS,
                                 const VarDecl *ThreadIDVar,
                                 const RegionCodeGenTy &CodeGen,
                                 OpenMPDirectiveKind Kind, bool HasCancel,
                                 const UntiedTaskActionTy &Action)
      : CGOpenMPRegionInfo(CS, TaskOutlinedRegion, CodeGen, Kind, HasCancel),
        ThreadIDVar(ThreadIDVar), Action(Action) {
    assert(ThreadIDVar != nullptr && "No ThreadID in OpenMP region.");
  }

  /// Get a variable or parameter for storing global thread id
  /// inside OpenMP construct.
  const VarDecl *getThreadIDVariable() const override { return ThreadIDVar; }

  /// Get an LValue for the current ThreadID variable.
  LValue getThreadIDVariableLValue(CodeGenFunction &CGF) override;

  /// Get the name of the capture helper.
  StringRef getHelperName() const override { return ".omp_outlined."; }

  void emitUntiedSwitch(CodeGenFunction &CGF) override {
    Action.emitUntiedSwitch(CGF);
  }

  static bool classof(const CGCapturedStmtInfo *Info) {
    return CGOpenMPRegionInfo::classof(Info) &&
           cast<CGOpenMPRegionInfo>(Info)->getRegionKind() ==
               TaskOutlinedRegion;
  }

private:
  /// A variable or parameter storing global thread id for OpenMP
  /// constructs.
  const VarDecl *ThreadIDVar;
  /// Action for emitting code for untied tasks.
  const UntiedTaskActionTy &Action;
};

/// API for inlined captured statement code generation in OpenMP
/// constructs.
class CGOpenMPInlinedRegionInfo : public CGOpenMPRegionInfo {
public:
  CGOpenMPInlinedRegionInfo(CodeGenFunction::CGCapturedStmtInfo *OldCSI,
                            const RegionCodeGenTy &CodeGen,
                            OpenMPDirectiveKind Kind, bool HasCancel)
      : CGOpenMPRegionInfo(InlinedRegion, CodeGen, Kind, HasCancel),
        OldCSI(OldCSI),
        OuterRegionInfo(dyn_cast_or_null<CGOpenMPRegionInfo>(OldCSI)) {}

  // Retrieve the value of the context parameter.
  llvm::Value *getContextValue() const override {
    if (OuterRegionInfo)
      return OuterRegionInfo->getContextValue();
    llvm_unreachable("No context value for inlined OpenMP region");
  }

  void setContextValue(llvm::Value *V) override {
    if (OuterRegionInfo) {
      OuterRegionInfo->setContextValue(V);
      return;
    }
    llvm_unreachable("No context value for inlined OpenMP region");
  }

  /// Lookup the captured field decl for a variable.
  const FieldDecl *lookup(const VarDecl *VD) const override {
    if (OuterRegionInfo)
      return OuterRegionInfo->lookup(VD);
    // If there is no outer outlined region,no need to lookup in a list of
    // captured variables, we can use the original one.
    return nullptr;
  }

  FieldDecl *getThisFieldDecl() const override {
    if (OuterRegionInfo)
      return OuterRegionInfo->getThisFieldDecl();
    return nullptr;
  }

  /// Get a variable or parameter for storing global thread id
  /// inside OpenMP construct.
  const VarDecl *getThreadIDVariable() const override {
    if (OuterRegionInfo)
      return OuterRegionInfo->getThreadIDVariable();
    return nullptr;
  }

  /// Get an LValue for the current ThreadID variable.
  LValue getThreadIDVariableLValue(CodeGenFunction &CGF) override {
    if (OuterRegionInfo)
      return OuterRegionInfo->getThreadIDVariableLValue(CGF);
    llvm_unreachable("No LValue for inlined OpenMP construct");
  }

  /// Get the name of the capture helper.
  StringRef getHelperName() const override {
    if (auto *OuterRegionInfo = getOldCSI())
      return OuterRegionInfo->getHelperName();
    llvm_unreachable("No helper name for inlined OpenMP construct");
  }

  void emitUntiedSwitch(CodeGenFunction &CGF) override {
    if (OuterRegionInfo)
      OuterRegionInfo->emitUntiedSwitch(CGF);
  }

  CodeGenFunction::CGCapturedStmtInfo *getOldCSI() const { return OldCSI; }

  static bool classof(const CGCapturedStmtInfo *Info) {
    return CGOpenMPRegionInfo::classof(Info) &&
           cast<CGOpenMPRegionInfo>(Info)->getRegionKind() == InlinedRegion;
  }

  ~CGOpenMPInlinedRegionInfo() override = default;

private:
  /// CodeGen info about outer OpenMP region.
  CodeGenFunction::CGCapturedStmtInfo *OldCSI;
  CGOpenMPRegionInfo *OuterRegionInfo;
};

/// API for captured statement code generation in OpenMP target
/// constructs. For this captures, implicit parameters are used instead of the
/// captured fields. The name of the target region has to be unique in a given
/// application so it is provided by the client, because only the client has
/// the information to generate that.
class CGOpenMPTargetRegionInfo final : public CGOpenMPRegionInfo {
public:
  CGOpenMPTargetRegionInfo(const CapturedStmt &CS,
                           const RegionCodeGenTy &CodeGen, StringRef HelperName)
      : CGOpenMPRegionInfo(CS, TargetRegion, CodeGen, OMPD_target,
                           /*HasCancel=*/false),
        HelperName(HelperName) {}

  /// This is unused for target regions because each starts executing
  /// with a single thread.
  const VarDecl *getThreadIDVariable() const override { return nullptr; }

  /// Get the name of the capture helper.
  StringRef getHelperName() const override { return HelperName; }

  static bool classof(const CGCapturedStmtInfo *Info) {
    return CGOpenMPRegionInfo::classof(Info) &&
           cast<CGOpenMPRegionInfo>(Info)->getRegionKind() == TargetRegion;
  }

private:
  StringRef HelperName;
};

static void EmptyCodeGen(CodeGenFunction &, PrePostActionTy &) {
  llvm_unreachable("No codegen for expressions");
}
/// API for generation of expressions captured in a innermost OpenMP
/// region.
class CGOpenMPInnerExprInfo final : public CGOpenMPInlinedRegionInfo {
public:
  CGOpenMPInnerExprInfo(CodeGenFunction &CGF, const CapturedStmt &CS)
      : CGOpenMPInlinedRegionInfo(CGF.CapturedStmtInfo, EmptyCodeGen,
                                  OMPD_unknown,
                                  /*HasCancel=*/false),
        PrivScope(CGF) {
    // Make sure the globals captured in the provided statement are local by
    // using the privatization logic. We assume the same variable is not
    // captured more than once.
    for (const auto &C : CS.captures()) {
      if (!C.capturesVariable() && !C.capturesVariableByCopy())
        continue;

      const VarDecl *VD = C.getCapturedVar();
      if (VD->isLocalVarDeclOrParm())
        continue;

      DeclRefExpr DRE(CGF.getContext(), const_cast<VarDecl *>(VD),
                      /*RefersToEnclosingVariableOrCapture=*/false,
                      VD->getType().getNonReferenceType(), VK_LValue,
                      C.getLocation());
      PrivScope.addPrivate(
          VD, [&CGF, &DRE]() { return CGF.EmitLValue(&DRE).getAddress(); });
    }
    (void)PrivScope.Privatize();
  }

  /// Lookup the captured field decl for a variable.
  const FieldDecl *lookup(const VarDecl *VD) const override {
    if (const FieldDecl *FD = CGOpenMPInlinedRegionInfo::lookup(VD))
      return FD;
    return nullptr;
  }

  /// Emit the captured statement body.
  void EmitBody(CodeGenFunction &CGF, const Stmt *S) override {
    llvm_unreachable("No body for expressions");
  }

  /// Get a variable or parameter for storing global thread id
  /// inside OpenMP construct.
  const VarDecl *getThreadIDVariable() const override {
    llvm_unreachable("No thread id for expressions");
  }

  /// Get the name of the capture helper.
  StringRef getHelperName() const override {
    llvm_unreachable("No helper name for expressions");
  }

  static bool classof(const CGCapturedStmtInfo *Info) { return false; }

private:
  /// Private scope to capture global variables.
  CodeGenFunction::OMPPrivateScope PrivScope;
};

/// RAII for emitting code of OpenMP constructs.
class InlinedOpenMPRegionRAII {
  CodeGenFunction &CGF;
  llvm::DenseMap<const VarDecl *, FieldDecl *> LambdaCaptureFields;
  FieldDecl *LambdaThisCaptureField = nullptr;
  const CodeGen::CGBlockInfo *BlockInfo = nullptr;

public:
  /// Constructs region for combined constructs.
  /// \param CodeGen Code generation sequence for combined directives. Includes
  /// a list of functions used for code generation of implicitly inlined
  /// regions.
  InlinedOpenMPRegionRAII(CodeGenFunction &CGF, const RegionCodeGenTy &CodeGen,
                          OpenMPDirectiveKind Kind, bool HasCancel)
      : CGF(CGF) {
    // Start emission for the construct.
    CGF.CapturedStmtInfo = new CGOpenMPInlinedRegionInfo(
        CGF.CapturedStmtInfo, CodeGen, Kind, HasCancel);
    std::swap(CGF.LambdaCaptureFields, LambdaCaptureFields);
    LambdaThisCaptureField = CGF.LambdaThisCaptureField;
    CGF.LambdaThisCaptureField = nullptr;
    BlockInfo = CGF.BlockInfo;
    CGF.BlockInfo = nullptr;
  }

  ~InlinedOpenMPRegionRAII() {
    // Restore original CapturedStmtInfo only if we're done with code emission.
    auto *OldCSI =
        cast<CGOpenMPInlinedRegionInfo>(CGF.CapturedStmtInfo)->getOldCSI();
    delete CGF.CapturedStmtInfo;
    CGF.CapturedStmtInfo = OldCSI;
    std::swap(CGF.LambdaCaptureFields, LambdaCaptureFields);
    CGF.LambdaThisCaptureField = LambdaThisCaptureField;
    CGF.BlockInfo = BlockInfo;
  }
};

/// Values for bit flags used in the ident_t to describe the fields.
/// All enumeric elements are named and described in accordance with the code
/// from http://llvm.org/svn/llvm-project/openmp/trunk/runtime/src/kmp.h
enum OpenMPLocationFlags : unsigned {
  /// Use trampoline for internal microtask.
  OMP_IDENT_IMD = 0x01,
  /// Use c-style ident structure.
  OMP_IDENT_KMPC = 0x02,
  /// Atomic reduction option for kmpc_reduce.
  OMP_ATOMIC_REDUCE = 0x10,
  /// Explicit 'barrier' directive.
  OMP_IDENT_BARRIER_EXPL = 0x20,
  /// Implicit barrier in code.
  OMP_IDENT_BARRIER_IMPL = 0x40,
  /// Implicit barrier in 'for' directive.
  OMP_IDENT_BARRIER_IMPL_FOR = 0x40,
  /// Implicit barrier in 'sections' directive.
  OMP_IDENT_BARRIER_IMPL_SECTIONS = 0xC0,
  /// Implicit barrier in 'single' directive.
  OMP_IDENT_BARRIER_IMPL_SINGLE = 0x140,
  /// Call of __kmp_for_static_init for static loop.
  OMP_IDENT_WORK_LOOP = 0x200,
  /// Call of __kmp_for_static_init for sections.
  OMP_IDENT_WORK_SECTIONS = 0x400,
  /// Call of __kmp_for_static_init for distribute.
  OMP_IDENT_WORK_DISTRIBUTE = 0x800,
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/OMP_IDENT_WORK_DISTRIBUTE)
};

/// Describes ident structure that describes a source location.
/// All descriptions are taken from
/// http://llvm.org/svn/llvm-project/openmp/trunk/runtime/src/kmp.h
/// Original structure:
/// typedef struct ident {
///    kmp_int32 reserved_1;   /**<  might be used in Fortran;
///                                  see above  */
///    kmp_int32 flags;        /**<  also f.flags; KMP_IDENT_xxx flags;
///                                  KMP_IDENT_KMPC identifies this union
///                                  member  */
///    kmp_int32 reserved_2;   /**<  not really used in Fortran any more;
///                                  see above */
///#if USE_ITT_BUILD
///                            /*  but currently used for storing
///                                region-specific ITT */
///                            /*  contextual information. */
///#endif /* USE_ITT_BUILD */
///    kmp_int32 reserved_3;   /**< source[4] in Fortran, do not use for
///                                 C++  */
///    char const *psource;    /**< String describing the source location.
///                            The string is composed of semi-colon separated
//                             fields which describe the source file,
///                            the function and a pair of line numbers that
///                            delimit the construct.
///                             */
/// } ident_t;
enum IdentFieldIndex {
  /// might be used in Fortran
  IdentField_Reserved_1,
  /// OMP_IDENT_xxx flags; OMP_IDENT_KMPC identifies this union member.
  IdentField_Flags,
  /// Not really used in Fortran any more
  IdentField_Reserved_2,
  /// Source[4] in Fortran, do not use for C++
  IdentField_Reserved_3,
  /// String describing the source location. The string is composed of
  /// semi-colon separated fields which describe the source file, the function
  /// and a pair of line numbers that delimit the construct.
  IdentField_PSource
};

/// Schedule types for 'omp for' loops (these enumerators are taken from
/// the enum sched_type in kmp.h).
enum OpenMPSchedType {
  /// Lower bound for default (unordered) versions.
  OMP_sch_lower = 32,
  OMP_sch_static_chunked = 33,
  OMP_sch_static = 34,
  OMP_sch_dynamic_chunked = 35,
  OMP_sch_guided_chunked = 36,
  OMP_sch_runtime = 37,
  OMP_sch_auto = 38,
  /// static with chunk adjustment (e.g., simd)
  OMP_sch_static_balanced_chunked = 45,
  /// Lower bound for 'ordered' versions.
  OMP_ord_lower = 64,
  OMP_ord_static_chunked = 65,
  OMP_ord_static = 66,
  OMP_ord_dynamic_chunked = 67,
  OMP_ord_guided_chunked = 68,
  OMP_ord_runtime = 69,
  OMP_ord_auto = 70,
  OMP_sch_default = OMP_sch_static,
  /// dist_schedule types
  OMP_dist_sch_static_chunked = 91,
  OMP_dist_sch_static = 92,
  /// Support for OpenMP 4.5 monotonic and nonmonotonic schedule modifiers.
  /// Set if the monotonic schedule modifier was present.
  OMP_sch_modifier_monotonic = (1 << 29),
  /// Set if the nonmonotonic schedule modifier was present.
  OMP_sch_modifier_nonmonotonic = (1 << 30),
};

enum OpenMPRTLFunction {
  /// Call to void __kmpc_fork_call(ident_t *loc, kmp_int32 argc,
  /// kmpc_micro microtask, ...);
  OMPRTL__kmpc_fork_call,
  /// Call to void *__kmpc_threadprivate_cached(ident_t *loc,
  /// kmp_int32 global_tid, void *data, size_t size, void ***cache);
  OMPRTL__kmpc_threadprivate_cached,
  /// Call to void __kmpc_threadprivate_register( ident_t *,
  /// void *data, kmpc_ctor ctor, kmpc_cctor cctor, kmpc_dtor dtor);
  OMPRTL__kmpc_threadprivate_register,
  // Call to __kmpc_int32 kmpc_global_thread_num(ident_t *loc);
  OMPRTL__kmpc_global_thread_num,
  // Call to void __kmpc_critical(ident_t *loc, kmp_int32 global_tid,
  // kmp_critical_name *crit);
  OMPRTL__kmpc_critical,
  // Call to void __kmpc_critical_with_hint(ident_t *loc, kmp_int32
  // global_tid, kmp_critical_name *crit, uintptr_t hint);
  OMPRTL__kmpc_critical_with_hint,
  // Call to void __kmpc_end_critical(ident_t *loc, kmp_int32 global_tid,
  // kmp_critical_name *crit);
  OMPRTL__kmpc_end_critical,
  // Call to kmp_int32 __kmpc_cancel_barrier(ident_t *loc, kmp_int32
  // global_tid);
  OMPRTL__kmpc_cancel_barrier,
  // Call to void __kmpc_barrier(ident_t *loc, kmp_int32 global_tid);
  OMPRTL__kmpc_barrier,
  // Call to void __kmpc_for_static_fini(ident_t *loc, kmp_int32 global_tid);
  OMPRTL__kmpc_for_static_fini,
  // Call to void __kmpc_serialized_parallel(ident_t *loc, kmp_int32
  // global_tid);
  OMPRTL__kmpc_serialized_parallel,
  // Call to void __kmpc_end_serialized_parallel(ident_t *loc, kmp_int32
  // global_tid);
  OMPRTL__kmpc_end_serialized_parallel,
  // Call to void __kmpc_push_num_threads(ident_t *loc, kmp_int32 global_tid,
  // kmp_int32 num_threads);
  OMPRTL__kmpc_push_num_threads,
  // Call to void __kmpc_flush(ident_t *loc);
  OMPRTL__kmpc_flush,
  // Call to kmp_int32 __kmpc_master(ident_t *, kmp_int32 global_tid);
  OMPRTL__kmpc_master,
  // Call to void __kmpc_end_master(ident_t *, kmp_int32 global_tid);
  OMPRTL__kmpc_end_master,
  // Call to kmp_int32 __kmpc_omp_taskyield(ident_t *, kmp_int32 global_tid,
  // int end_part);
  OMPRTL__kmpc_omp_taskyield,
  // Call to kmp_int32 __kmpc_single(ident_t *, kmp_int32 global_tid);
  OMPRTL__kmpc_single,
  // Call to void __kmpc_end_single(ident_t *, kmp_int32 global_tid);
  OMPRTL__kmpc_end_single,
  // Call to kmp_task_t * __kmpc_omp_task_alloc(ident_t *, kmp_int32 gtid,
  // kmp_int32 flags, size_t sizeof_kmp_task_t, size_t sizeof_shareds,
  // kmp_routine_entry_t *task_entry);
  OMPRTL__kmpc_omp_task_alloc,
  // Call to kmp_int32 __kmpc_omp_task(ident_t *, kmp_int32 gtid, kmp_task_t *
  // new_task);
  OMPRTL__kmpc_omp_task,
  // Call to void __kmpc_copyprivate(ident_t *loc, kmp_int32 global_tid,
  // size_t cpy_size, void *cpy_data, void(*cpy_func)(void *, void *),
  // kmp_int32 didit);
  OMPRTL__kmpc_copyprivate,
  // Call to kmp_int32 __kmpc_reduce(ident_t *loc, kmp_int32 global_tid,
  // kmp_int32 num_vars, size_t reduce_size, void *reduce_data, void
  // (*reduce_func)(void *lhs_data, void *rhs_data), kmp_critical_name *lck);
  OMPRTL__kmpc_reduce,
  // Call to kmp_int32 __kmpc_reduce_nowait(ident_t *loc, kmp_int32
  // global_tid, kmp_int32 num_vars, size_t reduce_size, void *reduce_data,
  // void (*reduce_func)(void *lhs_data, void *rhs_data), kmp_critical_name
  // *lck);
  OMPRTL__kmpc_reduce_nowait,
  // Call to void __kmpc_end_reduce(ident_t *loc, kmp_int32 global_tid,
  // kmp_critical_name *lck);
  OMPRTL__kmpc_end_reduce,
  // Call to void __kmpc_end_reduce_nowait(ident_t *loc, kmp_int32 global_tid,
  // kmp_critical_name *lck);
  OMPRTL__kmpc_end_reduce_nowait,
  // Call to void __kmpc_omp_task_begin_if0(ident_t *, kmp_int32 gtid,
  // kmp_task_t * new_task);
  OMPRTL__kmpc_omp_task_begin_if0,
  // Call to void __kmpc_omp_task_complete_if0(ident_t *, kmp_int32 gtid,
  // kmp_task_t * new_task);
  OMPRTL__kmpc_omp_task_complete_if0,
  // Call to void __kmpc_ordered(ident_t *loc, kmp_int32 global_tid);
  OMPRTL__kmpc_ordered,
  // Call to void __kmpc_end_ordered(ident_t *loc, kmp_int32 global_tid);
  OMPRTL__kmpc_end_ordered,
  // Call to kmp_int32 __kmpc_omp_taskwait(ident_t *loc, kmp_int32
  // global_tid);
  OMPRTL__kmpc_omp_taskwait,
  // Call to void __kmpc_taskgroup(ident_t *loc, kmp_int32 global_tid);
  OMPRTL__kmpc_taskgroup,
  // Call to void __kmpc_end_taskgroup(ident_t *loc, kmp_int32 global_tid);
  OMPRTL__kmpc_end_taskgroup,
  // Call to void __kmpc_push_proc_bind(ident_t *loc, kmp_int32 global_tid,
  // int proc_bind);
  OMPRTL__kmpc_push_proc_bind,
  // Call to kmp_int32 __kmpc_omp_task_with_deps(ident_t *loc_ref, kmp_int32
  // gtid, kmp_task_t * new_task, kmp_int32 ndeps, kmp_depend_info_t
  // *dep_list, kmp_int32 ndeps_noalias, kmp_depend_info_t *noalias_dep_list);
  OMPRTL__kmpc_omp_task_with_deps,
  // Call to void __kmpc_omp_wait_deps(ident_t *loc_ref, kmp_int32
  // gtid, kmp_int32 ndeps, kmp_depend_info_t *dep_list, kmp_int32
  // ndeps_noalias, kmp_depend_info_t *noalias_dep_list);
  OMPRTL__kmpc_omp_wait_deps,
  // Call to kmp_int32 __kmpc_cancellationpoint(ident_t *loc, kmp_int32
  // global_tid, kmp_int32 cncl_kind);
  OMPRTL__kmpc_cancellationpoint,
  // Call to kmp_int32 __kmpc_cancel(ident_t *loc, kmp_int32 global_tid,
  // kmp_int32 cncl_kind);
  OMPRTL__kmpc_cancel,
  // Call to void __kmpc_push_num_teams(ident_t *loc, kmp_int32 global_tid,
  // kmp_int32 num_teams, kmp_int32 thread_limit);
  OMPRTL__kmpc_push_num_teams,
  // Call to void __kmpc_fork_teams(ident_t *loc, kmp_int32 argc, kmpc_micro
  // microtask, ...);
  OMPRTL__kmpc_fork_teams,
  // Call to void __kmpc_taskloop(ident_t *loc, int gtid, kmp_task_t *task, int
  // if_val, kmp_uint64 *lb, kmp_uint64 *ub, kmp_int64 st, int nogroup, int
  // sched, kmp_uint64 grainsize, void *task_dup);
  OMPRTL__kmpc_taskloop,
  // Call to void __kmpc_doacross_init(ident_t *loc, kmp_int32 gtid, kmp_int32
  // num_dims, struct kmp_dim *dims);
  OMPRTL__kmpc_doacross_init,
  // Call to void __kmpc_doacross_fini(ident_t *loc, kmp_int32 gtid);
  OMPRTL__kmpc_doacross_fini,
  // Call to void __kmpc_doacross_post(ident_t *loc, kmp_int32 gtid, kmp_int64
  // *vec);
  OMPRTL__kmpc_doacross_post,
  // Call to void __kmpc_doacross_wait(ident_t *loc, kmp_int32 gtid, kmp_int64
  // *vec);
  OMPRTL__kmpc_doacross_wait,
  // Call to void *__kmpc_task_reduction_init(int gtid, int num_data, void
  // *data);
  OMPRTL__kmpc_task_reduction_init,
  // Call to void *__kmpc_task_reduction_get_th_data(int gtid, void *tg, void
  // *d);
  OMPRTL__kmpc_task_reduction_get_th_data,

  //
  // Offloading related calls
  //
  // Call to void __kmpc_push_target_tripcount(int64_t device_id, kmp_uint64
  // size);
  OMPRTL__kmpc_push_target_tripcount,
  // Call to int32_t __tgt_target(int64_t device_id, void *host_ptr, int32_t
  // arg_num, void** args_base, void **args, size_t *arg_sizes, int64_t
  // *arg_types);
  OMPRTL__tgt_target,
  // Call to int32_t __tgt_target_nowait(int64_t device_id, void *host_ptr,
  // int32_t arg_num, void** args_base, void **args, size_t *arg_sizes, int64_t
  // *arg_types);
  OMPRTL__tgt_target_nowait,
  // Call to int32_t __tgt_target_teams(int64_t device_id, void *host_ptr,
  // int32_t arg_num, void** args_base, void **args, size_t *arg_sizes, int64_t
  // *arg_types, int32_t num_teams, int32_t thread_limit);
  OMPRTL__tgt_target_teams,
  // Call to int32_t __tgt_target_teams_nowait(int64_t device_id, void
  // *host_ptr, int32_t arg_num, void** args_base, void **args, size_t
  // *arg_sizes, int64_t *arg_types, int32_t num_teams, int32_t thread_limit);
  OMPRTL__tgt_target_teams_nowait,
  // Call to void __tgt_register_lib(__tgt_bin_desc *desc);
  OMPRTL__tgt_register_lib,
  // Call to void __tgt_unregister_lib(__tgt_bin_desc *desc);
  OMPRTL__tgt_unregister_lib,
  // Call to void __tgt_target_data_begin(int64_t device_id, int32_t arg_num,
  // void** args_base, void **args, size_t *arg_sizes, int64_t *arg_types);
  OMPRTL__tgt_target_data_begin,
  // Call to void __tgt_target_data_begin_nowait(int64_t device_id, int32_t
  // arg_num, void** args_base, void **args, size_t *arg_sizes, int64_t
  // *arg_types);
  OMPRTL__tgt_target_data_begin_nowait,
  // Call to void __tgt_target_data_end(int64_t device_id, int32_t arg_num,
  // void** args_base, void **args, size_t *arg_sizes, int64_t *arg_types);
  OMPRTL__tgt_target_data_end,
  // Call to void __tgt_target_data_end_nowait(int64_t device_id, int32_t
  // arg_num, void** args_base, void **args, size_t *arg_sizes, int64_t
  // *arg_types);
  OMPRTL__tgt_target_data_end_nowait,
  // Call to void __tgt_target_data_update(int64_t device_id, int32_t arg_num,
  // void** args_base, void **args, size_t *arg_sizes, int64_t *arg_types);
  OMPRTL__tgt_target_data_update,
  // Call to void __tgt_target_data_update_nowait(int64_t device_id, int32_t
  // arg_num, void** args_base, void **args, size_t *arg_sizes, int64_t
  // *arg_types);
  OMPRTL__tgt_target_data_update_nowait,
};

/// A basic class for pre|post-action for advanced codegen sequence for OpenMP
/// region.
class CleanupTy final : public EHScopeStack::Cleanup {
  PrePostActionTy *Action;

public:
  explicit CleanupTy(PrePostActionTy *Action) : Action(Action) {}
  void Emit(CodeGenFunction &CGF, Flags /*flags*/) override {
    if (!CGF.HaveInsertPoint())
      return;
    Action->Exit(CGF);
  }
};

} // anonymous namespace

void RegionCodeGenTy::operator()(CodeGenFunction &CGF) const {
  CodeGenFunction::RunCleanupsScope Scope(CGF);
  if (PrePostAction) {
    CGF.EHStack.pushCleanup<CleanupTy>(NormalAndEHCleanup, PrePostAction);
    Callback(CodeGen, CGF, *PrePostAction);
  } else {
    PrePostActionTy Action;
    Callback(CodeGen, CGF, Action);
  }
}

/// Check if the combiner is a call to UDR combiner and if it is so return the
/// UDR decl used for reduction.
static const OMPDeclareReductionDecl *
getReductionInit(const Expr *ReductionOp) {
  if (const auto *CE = dyn_cast<CallExpr>(ReductionOp))
    if (const auto *OVE = dyn_cast<OpaqueValueExpr>(CE->getCallee()))
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(OVE->getSourceExpr()->IgnoreImpCasts()))
        if (const auto *DRD = dyn_cast<OMPDeclareReductionDecl>(DRE->getDecl()))
          return DRD;
  return nullptr;
}

static void emitInitWithReductionInitializer(CodeGenFunction &CGF,
                                             const OMPDeclareReductionDecl *DRD,
                                             const Expr *InitOp,
                                             Address Private, Address Original,
                                             QualType Ty) {
  if (DRD->getInitializer()) {
    std::pair<llvm::Function *, llvm::Function *> Reduction =
        CGF.CGM.getOpenMPRuntime().getUserDefinedReduction(DRD);
    const auto *CE = cast<CallExpr>(InitOp);
    const auto *OVE = cast<OpaqueValueExpr>(CE->getCallee());
    const Expr *LHS = CE->getArg(/*Arg=*/0)->IgnoreParenImpCasts();
    const Expr *RHS = CE->getArg(/*Arg=*/1)->IgnoreParenImpCasts();
    const auto *LHSDRE =
        cast<DeclRefExpr>(cast<UnaryOperator>(LHS)->getSubExpr());
    const auto *RHSDRE =
        cast<DeclRefExpr>(cast<UnaryOperator>(RHS)->getSubExpr());
    CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
    PrivateScope.addPrivate(cast<VarDecl>(LHSDRE->getDecl()),
                            [=]() { return Private; });
    PrivateScope.addPrivate(cast<VarDecl>(RHSDRE->getDecl()),
                            [=]() { return Original; });
    (void)PrivateScope.Privatize();
    RValue Func = RValue::get(Reduction.second);
    CodeGenFunction::OpaqueValueMapping Map(CGF, OVE, Func);
    CGF.EmitIgnoredExpr(InitOp);
  } else {
    llvm::Constant *Init = CGF.CGM.EmitNullConstant(Ty);
    std::string Name = CGF.CGM.getOpenMPRuntime().getName({"init"});
    auto *GV = new llvm::GlobalVariable(
        CGF.CGM.getModule(), Init->getType(), /*isConstant=*/true,
        llvm::GlobalValue::PrivateLinkage, Init, Name);
    LValue LV = CGF.MakeNaturalAlignAddrLValue(GV, Ty);
    RValue InitRVal;
    switch (CGF.getEvaluationKind(Ty)) {
    case TEK_Scalar:
      InitRVal = CGF.EmitLoadOfLValue(LV, DRD->getLocation());
      break;
    case TEK_Complex:
      InitRVal =
          RValue::getComplex(CGF.EmitLoadOfComplex(LV, DRD->getLocation()));
      break;
    case TEK_Aggregate:
      InitRVal = RValue::getAggregate(LV.getAddress());
      break;
    }
    OpaqueValueExpr OVE(DRD->getLocation(), Ty, VK_RValue);
    CodeGenFunction::OpaqueValueMapping OpaqueMap(CGF, &OVE, InitRVal);
    CGF.EmitAnyExprToMem(&OVE, Private, Ty.getQualifiers(),
                         /*IsInitializer=*/false);
  }
}

/// Emit initialization of arrays of complex types.
/// \param DestAddr Address of the array.
/// \param Type Type of array.
/// \param Init Initial expression of array.
/// \param SrcAddr Address of the original array.
static void EmitOMPAggregateInit(CodeGenFunction &CGF, Address DestAddr,
                                 QualType Type, bool EmitDeclareReductionInit,
                                 const Expr *Init,
                                 const OMPDeclareReductionDecl *DRD,
                                 Address SrcAddr = Address::invalid()) {
  // Perform element-by-element initialization.
  QualType ElementTy;

  // Drill down to the base element type on both arrays.
  const ArrayType *ArrayTy = Type->getAsArrayTypeUnsafe();
  llvm::Value *NumElements = CGF.emitArrayLength(ArrayTy, ElementTy, DestAddr);
  DestAddr =
      CGF.Builder.CreateElementBitCast(DestAddr, DestAddr.getElementType());
  if (DRD)
    SrcAddr =
        CGF.Builder.CreateElementBitCast(SrcAddr, DestAddr.getElementType());

  llvm::Value *SrcBegin = nullptr;
  if (DRD)
    SrcBegin = SrcAddr.getPointer();
  llvm::Value *DestBegin = DestAddr.getPointer();
  // Cast from pointer to array type to pointer to single element.
  llvm::Value *DestEnd = CGF.Builder.CreateGEP(DestBegin, NumElements);
  // The basic structure here is a while-do loop.
  llvm::BasicBlock *BodyBB = CGF.createBasicBlock("omp.arrayinit.body");
  llvm::BasicBlock *DoneBB = CGF.createBasicBlock("omp.arrayinit.done");
  llvm::Value *IsEmpty =
      CGF.Builder.CreateICmpEQ(DestBegin, DestEnd, "omp.arrayinit.isempty");
  CGF.Builder.CreateCondBr(IsEmpty, DoneBB, BodyBB);

  // Enter the loop body, making that address the current address.
  llvm::BasicBlock *EntryBB = CGF.Builder.GetInsertBlock();
  CGF.EmitBlock(BodyBB);

  CharUnits ElementSize = CGF.getContext().getTypeSizeInChars(ElementTy);

  llvm::PHINode *SrcElementPHI = nullptr;
  Address SrcElementCurrent = Address::invalid();
  if (DRD) {
    SrcElementPHI = CGF.Builder.CreatePHI(SrcBegin->getType(), 2,
                                          "omp.arraycpy.srcElementPast");
    SrcElementPHI->addIncoming(SrcBegin, EntryBB);
    SrcElementCurrent =
        Address(SrcElementPHI,
                SrcAddr.getAlignment().alignmentOfArrayElement(ElementSize));
  }
  llvm::PHINode *DestElementPHI = CGF.Builder.CreatePHI(
      DestBegin->getType(), 2, "omp.arraycpy.destElementPast");
  DestElementPHI->addIncoming(DestBegin, EntryBB);
  Address DestElementCurrent =
      Address(DestElementPHI,
              DestAddr.getAlignment().alignmentOfArrayElement(ElementSize));

  // Emit copy.
  {
    CodeGenFunction::RunCleanupsScope InitScope(CGF);
    if (EmitDeclareReductionInit) {
      emitInitWithReductionInitializer(CGF, DRD, Init, DestElementCurrent,
                                       SrcElementCurrent, ElementTy);
    } else
      CGF.EmitAnyExprToMem(Init, DestElementCurrent, ElementTy.getQualifiers(),
                           /*IsInitializer=*/false);
  }

  if (DRD) {
    // Shift the address forward by one element.
    llvm::Value *SrcElementNext = CGF.Builder.CreateConstGEP1_32(
        SrcElementPHI, /*Idx0=*/1, "omp.arraycpy.dest.element");
    SrcElementPHI->addIncoming(SrcElementNext, CGF.Builder.GetInsertBlock());
  }

  // Shift the address forward by one element.
  llvm::Value *DestElementNext = CGF.Builder.CreateConstGEP1_32(
      DestElementPHI, /*Idx0=*/1, "omp.arraycpy.dest.element");
  // Check whether we've reached the end.
  llvm::Value *Done =
      CGF.Builder.CreateICmpEQ(DestElementNext, DestEnd, "omp.arraycpy.done");
  CGF.Builder.CreateCondBr(Done, DoneBB, BodyBB);
  DestElementPHI->addIncoming(DestElementNext, CGF.Builder.GetInsertBlock());

  // Done.
  CGF.EmitBlock(DoneBB, /*IsFinished=*/true);
}

LValue ReductionCodeGen::emitSharedLValue(CodeGenFunction &CGF, const Expr *E) {
  return CGF.EmitOMPSharedLValue(E);
}

LValue ReductionCodeGen::emitSharedLValueUB(CodeGenFunction &CGF,
                                            const Expr *E) {
  if (const auto *OASE = dyn_cast<OMPArraySectionExpr>(E))
    return CGF.EmitOMPArraySectionExpr(OASE, /*IsLowerBound=*/false);
  return LValue();
}

void ReductionCodeGen::emitAggregateInitialization(
    CodeGenFunction &CGF, unsigned N, Address PrivateAddr, LValue SharedLVal,
    const OMPDeclareReductionDecl *DRD) {
  // Emit VarDecl with copy init for arrays.
  // Get the address of the original variable captured in current
  // captured region.
  const auto *PrivateVD =
      cast<VarDecl>(cast<DeclRefExpr>(ClausesData[N].Private)->getDecl());
  bool EmitDeclareReductionInit =
      DRD && (DRD->getInitializer() || !PrivateVD->hasInit());
  EmitOMPAggregateInit(CGF, PrivateAddr, PrivateVD->getType(),
                       EmitDeclareReductionInit,
                       EmitDeclareReductionInit ? ClausesData[N].ReductionOp
                                                : PrivateVD->getInit(),
                       DRD, SharedLVal.getAddress());
}

ReductionCodeGen::ReductionCodeGen(ArrayRef<const Expr *> Shareds,
                                   ArrayRef<const Expr *> Privates,
                                   ArrayRef<const Expr *> ReductionOps) {
  ClausesData.reserve(Shareds.size());
  SharedAddresses.reserve(Shareds.size());
  Sizes.reserve(Shareds.size());
  BaseDecls.reserve(Shareds.size());
  auto IPriv = Privates.begin();
  auto IRed = ReductionOps.begin();
  for (const Expr *Ref : Shareds) {
    ClausesData.emplace_back(Ref, *IPriv, *IRed);
    std::advance(IPriv, 1);
    std::advance(IRed, 1);
  }
}

void ReductionCodeGen::emitSharedLValue(CodeGenFunction &CGF, unsigned N) {
  assert(SharedAddresses.size() == N &&
         "Number of generated lvalues must be exactly N.");
  LValue First = emitSharedLValue(CGF, ClausesData[N].Ref);
  LValue Second = emitSharedLValueUB(CGF, ClausesData[N].Ref);
  SharedAddresses.emplace_back(First, Second);
}

void ReductionCodeGen::emitAggregateType(CodeGenFunction &CGF, unsigned N) {
  const auto *PrivateVD =
      cast<VarDecl>(cast<DeclRefExpr>(ClausesData[N].Private)->getDecl());
  QualType PrivateType = PrivateVD->getType();
  bool AsArraySection = isa<OMPArraySectionExpr>(ClausesData[N].Ref);
  if (!PrivateType->isVariablyModifiedType()) {
    Sizes.emplace_back(
        CGF.getTypeSize(
            SharedAddresses[N].first.getType().getNonReferenceType()),
        nullptr);
    return;
  }
  llvm::Value *Size;
  llvm::Value *SizeInChars;
  auto *ElemType =
      cast<llvm::PointerType>(SharedAddresses[N].first.getPointer()->getType())
          ->getElementType();
  auto *ElemSizeOf = llvm::ConstantExpr::getSizeOf(ElemType);
  if (AsArraySection) {
    Size = CGF.Builder.CreatePtrDiff(SharedAddresses[N].second.getPointer(),
                                     SharedAddresses[N].first.getPointer());
    Size = CGF.Builder.CreateNUWAdd(
        Size, llvm::ConstantInt::get(Size->getType(), /*V=*/1));
    SizeInChars = CGF.Builder.CreateNUWMul(Size, ElemSizeOf);
  } else {
    SizeInChars = CGF.getTypeSize(
        SharedAddresses[N].first.getType().getNonReferenceType());
    Size = CGF.Builder.CreateExactUDiv(SizeInChars, ElemSizeOf);
  }
  Sizes.emplace_back(SizeInChars, Size);
  CodeGenFunction::OpaqueValueMapping OpaqueMap(
      CGF,
      cast<OpaqueValueExpr>(
          CGF.getContext().getAsVariableArrayType(PrivateType)->getSizeExpr()),
      RValue::get(Size));
  CGF.EmitVariablyModifiedType(PrivateType);
}

void ReductionCodeGen::emitAggregateType(CodeGenFunction &CGF, unsigned N,
                                         llvm::Value *Size) {
  const auto *PrivateVD =
      cast<VarDecl>(cast<DeclRefExpr>(ClausesData[N].Private)->getDecl());
  QualType PrivateType = PrivateVD->getType();
  if (!PrivateType->isVariablyModifiedType()) {
    assert(!Size && !Sizes[N].second &&
           "Size should be nullptr for non-variably modified reduction "
           "items.");
    return;
  }
  CodeGenFunction::OpaqueValueMapping OpaqueMap(
      CGF,
      cast<OpaqueValueExpr>(
          CGF.getContext().getAsVariableArrayType(PrivateType)->getSizeExpr()),
      RValue::get(Size));
  CGF.EmitVariablyModifiedType(PrivateType);
}

void ReductionCodeGen::emitInitialization(
    CodeGenFunction &CGF, unsigned N, Address PrivateAddr, LValue SharedLVal,
    llvm::function_ref<bool(CodeGenFunction &)> DefaultInit) {
  assert(SharedAddresses.size() > N && "No variable was generated");
  const auto *PrivateVD =
      cast<VarDecl>(cast<DeclRefExpr>(ClausesData[N].Private)->getDecl());
  const OMPDeclareReductionDecl *DRD =
      getReductionInit(ClausesData[N].ReductionOp);
  QualType PrivateType = PrivateVD->getType();
  PrivateAddr = CGF.Builder.CreateElementBitCast(
      PrivateAddr, CGF.ConvertTypeForMem(PrivateType));
  QualType SharedType = SharedAddresses[N].first.getType();
  SharedLVal = CGF.MakeAddrLValue(
      CGF.Builder.CreateElementBitCast(SharedLVal.getAddress(),
                                       CGF.ConvertTypeForMem(SharedType)),
      SharedType, SharedAddresses[N].first.getBaseInfo(),
      CGF.CGM.getTBAAInfoForSubobject(SharedAddresses[N].first, SharedType));
  if (CGF.getContext().getAsArrayType(PrivateVD->getType())) {
    emitAggregateInitialization(CGF, N, PrivateAddr, SharedLVal, DRD);
  } else if (DRD && (DRD->getInitializer() || !PrivateVD->hasInit())) {
    emitInitWithReductionInitializer(CGF, DRD, ClausesData[N].ReductionOp,
                                     PrivateAddr, SharedLVal.getAddress(),
                                     SharedLVal.getType());
  } else if (!DefaultInit(CGF) && PrivateVD->hasInit() &&
             !CGF.isTrivialInitializer(PrivateVD->getInit())) {
    CGF.EmitAnyExprToMem(PrivateVD->getInit(), PrivateAddr,
                         PrivateVD->getType().getQualifiers(),
                         /*IsInitializer=*/false);
  }
}

bool ReductionCodeGen::needCleanups(unsigned N) {
  const auto *PrivateVD =
      cast<VarDecl>(cast<DeclRefExpr>(ClausesData[N].Private)->getDecl());
  QualType PrivateType = PrivateVD->getType();
  QualType::DestructionKind DTorKind = PrivateType.isDestructedType();
  return DTorKind != QualType::DK_none;
}

void ReductionCodeGen::emitCleanups(CodeGenFunction &CGF, unsigned N,
                                    Address PrivateAddr) {
  const auto *PrivateVD =
      cast<VarDecl>(cast<DeclRefExpr>(ClausesData[N].Private)->getDecl());
  QualType PrivateType = PrivateVD->getType();
  QualType::DestructionKind DTorKind = PrivateType.isDestructedType();
  if (needCleanups(N)) {
    PrivateAddr = CGF.Builder.CreateElementBitCast(
        PrivateAddr, CGF.ConvertTypeForMem(PrivateType));
    CGF.pushDestroy(DTorKind, PrivateAddr, PrivateType);
  }
}

static LValue loadToBegin(CodeGenFunction &CGF, QualType BaseTy, QualType ElTy,
                          LValue BaseLV) {
  BaseTy = BaseTy.getNonReferenceType();
  while ((BaseTy->isPointerType() || BaseTy->isReferenceType()) &&
         !CGF.getContext().hasSameType(BaseTy, ElTy)) {
    if (const auto *PtrTy = BaseTy->getAs<PointerType>()) {
      BaseLV = CGF.EmitLoadOfPointerLValue(BaseLV.getAddress(), PtrTy);
    } else {
      LValue RefLVal = CGF.MakeAddrLValue(BaseLV.getAddress(), BaseTy);
      BaseLV = CGF.EmitLoadOfReferenceLValue(RefLVal);
    }
    BaseTy = BaseTy->getPointeeType();
  }
  return CGF.MakeAddrLValue(
      CGF.Builder.CreateElementBitCast(BaseLV.getAddress(),
                                       CGF.ConvertTypeForMem(ElTy)),
      BaseLV.getType(), BaseLV.getBaseInfo(),
      CGF.CGM.getTBAAInfoForSubobject(BaseLV, BaseLV.getType()));
}

static Address castToBase(CodeGenFunction &CGF, QualType BaseTy, QualType ElTy,
                          llvm::Type *BaseLVType, CharUnits BaseLVAlignment,
                          llvm::Value *Addr) {
  Address Tmp = Address::invalid();
  Address TopTmp = Address::invalid();
  Address MostTopTmp = Address::invalid();
  BaseTy = BaseTy.getNonReferenceType();
  while ((BaseTy->isPointerType() || BaseTy->isReferenceType()) &&
         !CGF.getContext().hasSameType(BaseTy, ElTy)) {
    Tmp = CGF.CreateMemTemp(BaseTy);
    if (TopTmp.isValid())
      CGF.Builder.CreateStore(Tmp.getPointer(), TopTmp);
    else
      MostTopTmp = Tmp;
    TopTmp = Tmp;
    BaseTy = BaseTy->getPointeeType();
  }
  llvm::Type *Ty = BaseLVType;
  if (Tmp.isValid())
    Ty = Tmp.getElementType();
  Addr = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(Addr, Ty);
  if (Tmp.isValid()) {
    CGF.Builder.CreateStore(Addr, Tmp);
    return MostTopTmp;
  }
  return Address(Addr, BaseLVAlignment);
}

static const VarDecl *getBaseDecl(const Expr *Ref, const DeclRefExpr *&DE) {
  const VarDecl *OrigVD = nullptr;
  if (const auto *OASE = dyn_cast<OMPArraySectionExpr>(Ref)) {
    const Expr *Base = OASE->getBase()->IgnoreParenImpCasts();
    while (const auto *TempOASE = dyn_cast<OMPArraySectionExpr>(Base))
      Base = TempOASE->getBase()->IgnoreParenImpCasts();
    while (const auto *TempASE = dyn_cast<ArraySubscriptExpr>(Base))
      Base = TempASE->getBase()->IgnoreParenImpCasts();
    DE = cast<DeclRefExpr>(Base);
    OrigVD = cast<VarDecl>(DE->getDecl());
  } else if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(Ref)) {
    const Expr *Base = ASE->getBase()->IgnoreParenImpCasts();
    while (const auto *TempASE = dyn_cast<ArraySubscriptExpr>(Base))
      Base = TempASE->getBase()->IgnoreParenImpCasts();
    DE = cast<DeclRefExpr>(Base);
    OrigVD = cast<VarDecl>(DE->getDecl());
  }
  return OrigVD;
}

Address ReductionCodeGen::adjustPrivateAddress(CodeGenFunction &CGF, unsigned N,
                                               Address PrivateAddr) {
  const DeclRefExpr *DE;
  if (const VarDecl *OrigVD = ::getBaseDecl(ClausesData[N].Ref, DE)) {
    BaseDecls.emplace_back(OrigVD);
    LValue OriginalBaseLValue = CGF.EmitLValue(DE);
    LValue BaseLValue =
        loadToBegin(CGF, OrigVD->getType(), SharedAddresses[N].first.getType(),
                    OriginalBaseLValue);
    llvm::Value *Adjustment = CGF.Builder.CreatePtrDiff(
        BaseLValue.getPointer(), SharedAddresses[N].first.getPointer());
    llvm::Value *PrivatePointer =
        CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
            PrivateAddr.getPointer(),
            SharedAddresses[N].first.getAddress().getType());
    llvm::Value *Ptr = CGF.Builder.CreateGEP(PrivatePointer, Adjustment);
    return castToBase(CGF, OrigVD->getType(),
                      SharedAddresses[N].first.getType(),
                      OriginalBaseLValue.getAddress().getType(),
                      OriginalBaseLValue.getAlignment(), Ptr);
  }
  BaseDecls.emplace_back(
      cast<VarDecl>(cast<DeclRefExpr>(ClausesData[N].Ref)->getDecl()));
  return PrivateAddr;
}

bool ReductionCodeGen::usesReductionInitializer(unsigned N) const {
  const OMPDeclareReductionDecl *DRD =
      getReductionInit(ClausesData[N].ReductionOp);
  return DRD && DRD->getInitializer();
}

LValue CGOpenMPRegionInfo::getThreadIDVariableLValue(CodeGenFunction &CGF) {
  return CGF.EmitLoadOfPointerLValue(
      CGF.GetAddrOfLocalVar(getThreadIDVariable()),
      getThreadIDVariable()->getType()->castAs<PointerType>());
}

void CGOpenMPRegionInfo::EmitBody(CodeGenFunction &CGF, const Stmt * /*S*/) {
  if (!CGF.HaveInsertPoint())
    return;
  // 1.2.2 OpenMP Language Terminology
  // Structured block - An executable statement with a single entry at the
  // top and a single exit at the bottom.
  // The point of exit cannot be a branch out of the structured block.
  // longjmp() and throw() must not violate the entry/exit criteria.
  CGF.EHStack.pushTerminate();
  CodeGen(CGF);
  CGF.EHStack.popTerminate();
}

LValue CGOpenMPTaskOutlinedRegionInfo::getThreadIDVariableLValue(
    CodeGenFunction &CGF) {
  return CGF.MakeAddrLValue(CGF.GetAddrOfLocalVar(getThreadIDVariable()),
                            getThreadIDVariable()->getType(),
                            AlignmentSource::Decl);
}

static FieldDecl *addFieldToRecordDecl(ASTContext &C, DeclContext *DC,
                                       QualType FieldTy) {
  auto *Field = FieldDecl::Create(
      C, DC, SourceLocation(), SourceLocation(), /*Id=*/nullptr, FieldTy,
      C.getTrivialTypeSourceInfo(FieldTy, SourceLocation()),
      /*BW=*/nullptr, /*Mutable=*/false, /*InitStyle=*/ICIS_NoInit);
  Field->setAccess(AS_public);
  DC->addDecl(Field);
  return Field;
}

CGOpenMPRuntime::CGOpenMPRuntime(CodeGenModule &CGM, StringRef FirstSeparator,
                                 StringRef Separator)
    : CGM(CGM), FirstSeparator(FirstSeparator), Separator(Separator),
      OffloadEntriesInfoManager(CGM) {
  ASTContext &C = CGM.getContext();
  RecordDecl *RD = C.buildImplicitRecord("ident_t");
  QualType KmpInt32Ty = C.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/1);
  RD->startDefinition();
  // reserved_1
  addFieldToRecordDecl(C, RD, KmpInt32Ty);
  // flags
  addFieldToRecordDecl(C, RD, KmpInt32Ty);
  // reserved_2
  addFieldToRecordDecl(C, RD, KmpInt32Ty);
  // reserved_3
  addFieldToRecordDecl(C, RD, KmpInt32Ty);
  // psource
  addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  RD->completeDefinition();
  IdentQTy = C.getRecordType(RD);
  IdentTy = CGM.getTypes().ConvertRecordDeclType(RD);
  KmpCriticalNameTy = llvm::ArrayType::get(CGM.Int32Ty, /*NumElements*/ 8);

  loadOffloadInfoMetadata();
}

void CGOpenMPRuntime::clear() {
  InternalVars.clear();
  // Clean non-target variable declarations possibly used only in debug info.
  for (const auto &Data : EmittedNonTargetVariables) {
    if (!Data.getValue().pointsToAliveValue())
      continue;
    auto *GV = dyn_cast<llvm::GlobalVariable>(Data.getValue());
    if (!GV)
      continue;
    if (!GV->isDeclaration() || GV->getNumUses() > 0)
      continue;
    GV->eraseFromParent();
  }
}

std::string CGOpenMPRuntime::getName(ArrayRef<StringRef> Parts) const {
  SmallString<128> Buffer;
  llvm::raw_svector_ostream OS(Buffer);
  StringRef Sep = FirstSeparator;
  for (StringRef Part : Parts) {
    OS << Sep << Part;
    Sep = Separator;
  }
  return OS.str();
}

static llvm::Function *
emitCombinerOrInitializer(CodeGenModule &CGM, QualType Ty,
                          const Expr *CombinerInitializer, const VarDecl *In,
                          const VarDecl *Out, bool IsCombiner) {
  // void .omp_combiner.(Ty *in, Ty *out);
  ASTContext &C = CGM.getContext();
  QualType PtrTy = C.getPointerType(Ty).withRestrict();
  FunctionArgList Args;
  ImplicitParamDecl OmpOutParm(C, /*DC=*/nullptr, Out->getLocation(),
                               /*Id=*/nullptr, PtrTy, ImplicitParamDecl::Other);
  ImplicitParamDecl OmpInParm(C, /*DC=*/nullptr, In->getLocation(),
                              /*Id=*/nullptr, PtrTy, ImplicitParamDecl::Other);
  Args.push_back(&OmpOutParm);
  Args.push_back(&OmpInParm);
  const CGFunctionInfo &FnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *FnTy = CGM.getTypes().GetFunctionType(FnInfo);
  std::string Name = CGM.getOpenMPRuntime().getName(
      {IsCombiner ? "omp_combiner" : "omp_initializer", ""});
  auto *Fn = llvm::Function::Create(FnTy, llvm::GlobalValue::InternalLinkage,
                                    Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, FnInfo);
  Fn->removeFnAttr(llvm::Attribute::NoInline);
  Fn->removeFnAttr(llvm::Attribute::OptimizeNone);
  Fn->addFnAttr(llvm::Attribute::AlwaysInline);
  CodeGenFunction CGF(CGM);
  // Map "T omp_in;" variable to "*omp_in_parm" value in all expressions.
  // Map "T omp_out;" variable to "*omp_out_parm" value in all expressions.
  CGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, FnInfo, Args, In->getLocation(),
                    Out->getLocation());
  CodeGenFunction::OMPPrivateScope Scope(CGF);
  Address AddrIn = CGF.GetAddrOfLocalVar(&OmpInParm);
  Scope.addPrivate(In, [&CGF, AddrIn, PtrTy]() {
    return CGF.EmitLoadOfPointerLValue(AddrIn, PtrTy->castAs<PointerType>())
        .getAddress();
  });
  Address AddrOut = CGF.GetAddrOfLocalVar(&OmpOutParm);
  Scope.addPrivate(Out, [&CGF, AddrOut, PtrTy]() {
    return CGF.EmitLoadOfPointerLValue(AddrOut, PtrTy->castAs<PointerType>())
        .getAddress();
  });
  (void)Scope.Privatize();
  if (!IsCombiner && Out->hasInit() &&
      !CGF.isTrivialInitializer(Out->getInit())) {
    CGF.EmitAnyExprToMem(Out->getInit(), CGF.GetAddrOfLocalVar(Out),
                         Out->getType().getQualifiers(),
                         /*IsInitializer=*/true);
  }
  if (CombinerInitializer)
    CGF.EmitIgnoredExpr(CombinerInitializer);
  Scope.ForceCleanup();
  CGF.FinishFunction();
  return Fn;
}

void CGOpenMPRuntime::emitUserDefinedReduction(
    CodeGenFunction *CGF, const OMPDeclareReductionDecl *D) {
  if (UDRMap.count(D) > 0)
    return;
  llvm::Function *Combiner = emitCombinerOrInitializer(
      CGM, D->getType(), D->getCombiner(),
      cast<VarDecl>(cast<DeclRefExpr>(D->getCombinerIn())->getDecl()),
      cast<VarDecl>(cast<DeclRefExpr>(D->getCombinerOut())->getDecl()),
      /*IsCombiner=*/true);
  llvm::Function *Initializer = nullptr;
  if (const Expr *Init = D->getInitializer()) {
    Initializer = emitCombinerOrInitializer(
        CGM, D->getType(),
        D->getInitializerKind() == OMPDeclareReductionDecl::CallInit ? Init
                                                                     : nullptr,
        cast<VarDecl>(cast<DeclRefExpr>(D->getInitOrig())->getDecl()),
        cast<VarDecl>(cast<DeclRefExpr>(D->getInitPriv())->getDecl()),
        /*IsCombiner=*/false);
  }
  UDRMap.try_emplace(D, Combiner, Initializer);
  if (CGF) {
    auto &Decls = FunctionUDRMap.FindAndConstruct(CGF->CurFn);
    Decls.second.push_back(D);
  }
}

std::pair<llvm::Function *, llvm::Function *>
CGOpenMPRuntime::getUserDefinedReduction(const OMPDeclareReductionDecl *D) {
  auto I = UDRMap.find(D);
  if (I != UDRMap.end())
    return I->second;
  emitUserDefinedReduction(/*CGF=*/nullptr, D);
  return UDRMap.lookup(D);
}

static llvm::Value *emitParallelOrTeamsOutlinedFunction(
    CodeGenModule &CGM, const OMPExecutableDirective &D, const CapturedStmt *CS,
    const VarDecl *ThreadIDVar, OpenMPDirectiveKind InnermostKind,
    const StringRef OutlinedHelperName, const RegionCodeGenTy &CodeGen) {
  assert(ThreadIDVar->getType()->isPointerType() &&
         "thread id variable must be of type kmp_int32 *");
  CodeGenFunction CGF(CGM, true);
  bool HasCancel = false;
  if (const auto *OPD = dyn_cast<OMPParallelDirective>(&D))
    HasCancel = OPD->hasCancel();
  else if (const auto *OPSD = dyn_cast<OMPParallelSectionsDirective>(&D))
    HasCancel = OPSD->hasCancel();
  else if (const auto *OPFD = dyn_cast<OMPParallelForDirective>(&D))
    HasCancel = OPFD->hasCancel();
  else if (const auto *OPFD = dyn_cast<OMPTargetParallelForDirective>(&D))
    HasCancel = OPFD->hasCancel();
  else if (const auto *OPFD = dyn_cast<OMPDistributeParallelForDirective>(&D))
    HasCancel = OPFD->hasCancel();
  else if (const auto *OPFD =
               dyn_cast<OMPTeamsDistributeParallelForDirective>(&D))
    HasCancel = OPFD->hasCancel();
  else if (const auto *OPFD =
               dyn_cast<OMPTargetTeamsDistributeParallelForDirective>(&D))
    HasCancel = OPFD->hasCancel();
  CGOpenMPOutlinedRegionInfo CGInfo(*CS, ThreadIDVar, CodeGen, InnermostKind,
                                    HasCancel, OutlinedHelperName);
  CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGInfo);
  return CGF.GenerateOpenMPCapturedStmtFunction(*CS);
}

llvm::Value *CGOpenMPRuntime::emitParallelOutlinedFunction(
    const OMPExecutableDirective &D, const VarDecl *ThreadIDVar,
    OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen) {
  const CapturedStmt *CS = D.getCapturedStmt(OMPD_parallel);
  return emitParallelOrTeamsOutlinedFunction(
      CGM, D, CS, ThreadIDVar, InnermostKind, getOutlinedHelperName(), CodeGen);
}

llvm::Value *CGOpenMPRuntime::emitTeamsOutlinedFunction(
    const OMPExecutableDirective &D, const VarDecl *ThreadIDVar,
    OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen) {
  const CapturedStmt *CS = D.getCapturedStmt(OMPD_teams);
  return emitParallelOrTeamsOutlinedFunction(
      CGM, D, CS, ThreadIDVar, InnermostKind, getOutlinedHelperName(), CodeGen);
}

llvm::Value *CGOpenMPRuntime::emitTaskOutlinedFunction(
    const OMPExecutableDirective &D, const VarDecl *ThreadIDVar,
    const VarDecl *PartIDVar, const VarDecl *TaskTVar,
    OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen,
    bool Tied, unsigned &NumberOfParts) {
  auto &&UntiedCodeGen = [this, &D, TaskTVar](CodeGenFunction &CGF,
                                              PrePostActionTy &) {
    llvm::Value *ThreadID = getThreadID(CGF, D.getBeginLoc());
    llvm::Value *UpLoc = emitUpdateLocation(CGF, D.getBeginLoc());
    llvm::Value *TaskArgs[] = {
        UpLoc, ThreadID,
        CGF.EmitLoadOfPointerLValue(CGF.GetAddrOfLocalVar(TaskTVar),
                                    TaskTVar->getType()->castAs<PointerType>())
            .getPointer()};
    CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_omp_task), TaskArgs);
  };
  CGOpenMPTaskOutlinedRegionInfo::UntiedTaskActionTy Action(Tied, PartIDVar,
                                                            UntiedCodeGen);
  CodeGen.setAction(Action);
  assert(!ThreadIDVar->getType()->isPointerType() &&
         "thread id variable must be of type kmp_int32 for tasks");
  const OpenMPDirectiveKind Region =
      isOpenMPTaskLoopDirective(D.getDirectiveKind()) ? OMPD_taskloop
                                                      : OMPD_task;
  const CapturedStmt *CS = D.getCapturedStmt(Region);
  const auto *TD = dyn_cast<OMPTaskDirective>(&D);
  CodeGenFunction CGF(CGM, true);
  CGOpenMPTaskOutlinedRegionInfo CGInfo(*CS, ThreadIDVar, CodeGen,
                                        InnermostKind,
                                        TD ? TD->hasCancel() : false, Action);
  CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGInfo);
  llvm::Value *Res = CGF.GenerateCapturedStmtFunction(*CS);
  if (!Tied)
    NumberOfParts = Action.getNumberOfParts();
  return Res;
}

static void buildStructValue(ConstantStructBuilder &Fields, CodeGenModule &CGM,
                             const RecordDecl *RD, const CGRecordLayout &RL,
                             ArrayRef<llvm::Constant *> Data) {
  llvm::StructType *StructTy = RL.getLLVMType();
  unsigned PrevIdx = 0;
  ConstantInitBuilder CIBuilder(CGM);
  auto DI = Data.begin();
  for (const FieldDecl *FD : RD->fields()) {
    unsigned Idx = RL.getLLVMFieldNo(FD);
    // Fill the alignment.
    for (unsigned I = PrevIdx; I < Idx; ++I)
      Fields.add(llvm::Constant::getNullValue(StructTy->getElementType(I)));
    PrevIdx = Idx + 1;
    Fields.add(*DI);
    ++DI;
  }
}

template <class... As>
static llvm::GlobalVariable *
createGlobalStruct(CodeGenModule &CGM, QualType Ty, bool IsConstant,
                   ArrayRef<llvm::Constant *> Data, const Twine &Name,
                   As &&... Args) {
  const auto *RD = cast<RecordDecl>(Ty->getAsTagDecl());
  const CGRecordLayout &RL = CGM.getTypes().getCGRecordLayout(RD);
  ConstantInitBuilder CIBuilder(CGM);
  ConstantStructBuilder Fields = CIBuilder.beginStruct(RL.getLLVMType());
  buildStructValue(Fields, CGM, RD, RL, Data);
  return Fields.finishAndCreateGlobal(
      Name, CGM.getContext().getAlignOfGlobalVarInChars(Ty), IsConstant,
      std::forward<As>(Args)...);
}

template <typename T>
static void
createConstantGlobalStructAndAddToParent(CodeGenModule &CGM, QualType Ty,
                                         ArrayRef<llvm::Constant *> Data,
                                         T &Parent) {
  const auto *RD = cast<RecordDecl>(Ty->getAsTagDecl());
  const CGRecordLayout &RL = CGM.getTypes().getCGRecordLayout(RD);
  ConstantStructBuilder Fields = Parent.beginStruct(RL.getLLVMType());
  buildStructValue(Fields, CGM, RD, RL, Data);
  Fields.finishAndAddTo(Parent);
}

Address CGOpenMPRuntime::getOrCreateDefaultLocation(unsigned Flags) {
  CharUnits Align = CGM.getContext().getTypeAlignInChars(IdentQTy);
  unsigned Reserved2Flags = getDefaultLocationReserved2Flags();
  FlagsTy FlagsKey(Flags, Reserved2Flags);
  llvm::Value *Entry = OpenMPDefaultLocMap.lookup(FlagsKey);
  if (!Entry) {
    if (!DefaultOpenMPPSource) {
      // Initialize default location for psource field of ident_t structure of
      // all ident_t objects. Format is ";file;function;line;column;;".
      // Taken from
      // http://llvm.org/svn/llvm-project/openmp/trunk/runtime/src/kmp_str.c
      DefaultOpenMPPSource =
          CGM.GetAddrOfConstantCString(";unknown;unknown;0;0;;").getPointer();
      DefaultOpenMPPSource =
          llvm::ConstantExpr::getBitCast(DefaultOpenMPPSource, CGM.Int8PtrTy);
    }

    llvm::Constant *Data[] = {
        llvm::ConstantInt::getNullValue(CGM.Int32Ty),
        llvm::ConstantInt::get(CGM.Int32Ty, Flags),
        llvm::ConstantInt::get(CGM.Int32Ty, Reserved2Flags),
        llvm::ConstantInt::getNullValue(CGM.Int32Ty), DefaultOpenMPPSource};
    llvm::GlobalValue *DefaultOpenMPLocation =
        createGlobalStruct(CGM, IdentQTy, isDefaultLocationConstant(), Data, "",
                           llvm::GlobalValue::PrivateLinkage);
    DefaultOpenMPLocation->setUnnamedAddr(
        llvm::GlobalValue::UnnamedAddr::Global);

    OpenMPDefaultLocMap[FlagsKey] = Entry = DefaultOpenMPLocation;
  }
  return Address(Entry, Align);
}

void CGOpenMPRuntime::setLocThreadIdInsertPt(CodeGenFunction &CGF,
                                             bool AtCurrentPoint) {
  auto &Elem = OpenMPLocThreadIDMap.FindAndConstruct(CGF.CurFn);
  assert(!Elem.second.ServiceInsertPt && "Insert point is set already.");

  llvm::Value *Undef = llvm::UndefValue::get(CGF.Int32Ty);
  if (AtCurrentPoint) {
    Elem.second.ServiceInsertPt = new llvm::BitCastInst(
        Undef, CGF.Int32Ty, "svcpt", CGF.Builder.GetInsertBlock());
  } else {
    Elem.second.ServiceInsertPt =
        new llvm::BitCastInst(Undef, CGF.Int32Ty, "svcpt");
    Elem.second.ServiceInsertPt->insertAfter(CGF.AllocaInsertPt);
  }
}

void CGOpenMPRuntime::clearLocThreadIdInsertPt(CodeGenFunction &CGF) {
  auto &Elem = OpenMPLocThreadIDMap.FindAndConstruct(CGF.CurFn);
  if (Elem.second.ServiceInsertPt) {
    llvm::Instruction *Ptr = Elem.second.ServiceInsertPt;
    Elem.second.ServiceInsertPt = nullptr;
    Ptr->eraseFromParent();
  }
}

llvm::Value *CGOpenMPRuntime::emitUpdateLocation(CodeGenFunction &CGF,
                                                 SourceLocation Loc,
                                                 unsigned Flags) {
  Flags |= OMP_IDENT_KMPC;
  // If no debug info is generated - return global default location.
  if (CGM.getCodeGenOpts().getDebugInfo() == codegenoptions::NoDebugInfo ||
      Loc.isInvalid())
    return getOrCreateDefaultLocation(Flags).getPointer();

  assert(CGF.CurFn && "No function in current CodeGenFunction.");

  CharUnits Align = CGM.getContext().getTypeAlignInChars(IdentQTy);
  Address LocValue = Address::invalid();
  auto I = OpenMPLocThreadIDMap.find(CGF.CurFn);
  if (I != OpenMPLocThreadIDMap.end())
    LocValue = Address(I->second.DebugLoc, Align);

  // OpenMPLocThreadIDMap may have null DebugLoc and non-null ThreadID, if
  // GetOpenMPThreadID was called before this routine.
  if (!LocValue.isValid()) {
    // Generate "ident_t .kmpc_loc.addr;"
    Address AI = CGF.CreateMemTemp(IdentQTy, ".kmpc_loc.addr");
    auto &Elem = OpenMPLocThreadIDMap.FindAndConstruct(CGF.CurFn);
    Elem.second.DebugLoc = AI.getPointer();
    LocValue = AI;

    if (!Elem.second.ServiceInsertPt)
      setLocThreadIdInsertPt(CGF);
    CGBuilderTy::InsertPointGuard IPG(CGF.Builder);
    CGF.Builder.SetInsertPoint(Elem.second.ServiceInsertPt);
    CGF.Builder.CreateMemCpy(LocValue, getOrCreateDefaultLocation(Flags),
                             CGF.getTypeSize(IdentQTy));
  }

  // char **psource = &.kmpc_loc_<flags>.addr.psource;
  LValue Base = CGF.MakeAddrLValue(LocValue, IdentQTy);
  auto Fields = cast<RecordDecl>(IdentQTy->getAsTagDecl())->field_begin();
  LValue PSource =
      CGF.EmitLValueForField(Base, *std::next(Fields, IdentField_PSource));

  llvm::Value *OMPDebugLoc = OpenMPDebugLocMap.lookup(Loc.getRawEncoding());
  if (OMPDebugLoc == nullptr) {
    SmallString<128> Buffer2;
    llvm::raw_svector_ostream OS2(Buffer2);
    // Build debug location
    PresumedLoc PLoc = CGF.getContext().getSourceManager().getPresumedLoc(Loc);
    OS2 << ";" << PLoc.getFilename() << ";";
    if (const auto *FD = dyn_cast_or_null<FunctionDecl>(CGF.CurFuncDecl))
      OS2 << FD->getQualifiedNameAsString();
    OS2 << ";" << PLoc.getLine() << ";" << PLoc.getColumn() << ";;";
    OMPDebugLoc = CGF.Builder.CreateGlobalStringPtr(OS2.str());
    OpenMPDebugLocMap[Loc.getRawEncoding()] = OMPDebugLoc;
  }
  // *psource = ";<File>;<Function>;<Line>;<Column>;;";
  CGF.EmitStoreOfScalar(OMPDebugLoc, PSource);

  // Our callers always pass this to a runtime function, so for
  // convenience, go ahead and return a naked pointer.
  return LocValue.getPointer();
}

llvm::Value *CGOpenMPRuntime::getThreadID(CodeGenFunction &CGF,
                                          SourceLocation Loc) {
  assert(CGF.CurFn && "No function in current CodeGenFunction.");

  llvm::Value *ThreadID = nullptr;
  // Check whether we've already cached a load of the thread id in this
  // function.
  auto I = OpenMPLocThreadIDMap.find(CGF.CurFn);
  if (I != OpenMPLocThreadIDMap.end()) {
    ThreadID = I->second.ThreadID;
    if (ThreadID != nullptr)
      return ThreadID;
  }
  // If exceptions are enabled, do not use parameter to avoid possible crash.
  if (!CGF.EHStack.requiresLandingPad() || !CGF.getLangOpts().Exceptions ||
      !CGF.getLangOpts().CXXExceptions ||
      CGF.Builder.GetInsertBlock() == CGF.AllocaInsertPt->getParent()) {
    if (auto *OMPRegionInfo =
            dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo)) {
      if (OMPRegionInfo->getThreadIDVariable()) {
        // Check if this an outlined function with thread id passed as argument.
        LValue LVal = OMPRegionInfo->getThreadIDVariableLValue(CGF);
        ThreadID = CGF.EmitLoadOfScalar(LVal, Loc);
        // If value loaded in entry block, cache it and use it everywhere in
        // function.
        if (CGF.Builder.GetInsertBlock() == CGF.AllocaInsertPt->getParent()) {
          auto &Elem = OpenMPLocThreadIDMap.FindAndConstruct(CGF.CurFn);
          Elem.second.ThreadID = ThreadID;
        }
        return ThreadID;
      }
    }
  }

  // This is not an outlined function region - need to call __kmpc_int32
  // kmpc_global_thread_num(ident_t *loc).
  // Generate thread id value and cache this value for use across the
  // function.
  auto &Elem = OpenMPLocThreadIDMap.FindAndConstruct(CGF.CurFn);
  if (!Elem.second.ServiceInsertPt)
    setLocThreadIdInsertPt(CGF);
  CGBuilderTy::InsertPointGuard IPG(CGF.Builder);
  CGF.Builder.SetInsertPoint(Elem.second.ServiceInsertPt);
  llvm::CallInst *Call = CGF.Builder.CreateCall(
      createRuntimeFunction(OMPRTL__kmpc_global_thread_num),
      emitUpdateLocation(CGF, Loc));
  Call->setCallingConv(CGF.getRuntimeCC());
  Elem.second.ThreadID = Call;
  return Call;
}

void CGOpenMPRuntime::functionFinished(CodeGenFunction &CGF) {
  assert(CGF.CurFn && "No function in current CodeGenFunction.");
  if (OpenMPLocThreadIDMap.count(CGF.CurFn)) {
    clearLocThreadIdInsertPt(CGF);
    OpenMPLocThreadIDMap.erase(CGF.CurFn);
  }
  if (FunctionUDRMap.count(CGF.CurFn) > 0) {
    for(auto *D : FunctionUDRMap[CGF.CurFn])
      UDRMap.erase(D);
    FunctionUDRMap.erase(CGF.CurFn);
  }
}

llvm::Type *CGOpenMPRuntime::getIdentTyPointerTy() {
  return IdentTy->getPointerTo();
}

llvm::Type *CGOpenMPRuntime::getKmpc_MicroPointerTy() {
  if (!Kmpc_MicroTy) {
    // Build void (*kmpc_micro)(kmp_int32 *global_tid, kmp_int32 *bound_tid,...)
    llvm::Type *MicroParams[] = {llvm::PointerType::getUnqual(CGM.Int32Ty),
                                 llvm::PointerType::getUnqual(CGM.Int32Ty)};
    Kmpc_MicroTy = llvm::FunctionType::get(CGM.VoidTy, MicroParams, true);
  }
  return llvm::PointerType::getUnqual(Kmpc_MicroTy);
}

llvm::Constant *
CGOpenMPRuntime::createRuntimeFunction(unsigned Function) {
  llvm::Constant *RTLFn = nullptr;
  switch (static_cast<OpenMPRTLFunction>(Function)) {
  case OMPRTL__kmpc_fork_call: {
    // Build void __kmpc_fork_call(ident_t *loc, kmp_int32 argc, kmpc_micro
    // microtask, ...);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty,
                                getKmpc_MicroPointerTy()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ true);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_fork_call");
    break;
  }
  case OMPRTL__kmpc_global_thread_num: {
    // Build kmp_int32 __kmpc_global_thread_num(ident_t *loc);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_global_thread_num");
    break;
  }
  case OMPRTL__kmpc_threadprivate_cached: {
    // Build void *__kmpc_threadprivate_cached(ident_t *loc,
    // kmp_int32 global_tid, void *data, size_t size, void ***cache);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty,
                                CGM.VoidPtrTy, CGM.SizeTy,
                                CGM.VoidPtrTy->getPointerTo()->getPointerTo()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidPtrTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_threadprivate_cached");
    break;
  }
  case OMPRTL__kmpc_critical: {
    // Build void __kmpc_critical(ident_t *loc, kmp_int32 global_tid,
    // kmp_critical_name *crit);
    llvm::Type *TypeParams[] = {
        getIdentTyPointerTy(), CGM.Int32Ty,
        llvm::PointerType::getUnqual(KmpCriticalNameTy)};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_critical");
    break;
  }
  case OMPRTL__kmpc_critical_with_hint: {
    // Build void __kmpc_critical_with_hint(ident_t *loc, kmp_int32 global_tid,
    // kmp_critical_name *crit, uintptr_t hint);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty,
                                llvm::PointerType::getUnqual(KmpCriticalNameTy),
                                CGM.IntPtrTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_critical_with_hint");
    break;
  }
  case OMPRTL__kmpc_threadprivate_register: {
    // Build void __kmpc_threadprivate_register(ident_t *, void *data,
    // kmpc_ctor ctor, kmpc_cctor cctor, kmpc_dtor dtor);
    // typedef void *(*kmpc_ctor)(void *);
    auto *KmpcCtorTy =
        llvm::FunctionType::get(CGM.VoidPtrTy, CGM.VoidPtrTy,
                                /*isVarArg*/ false)->getPointerTo();
    // typedef void *(*kmpc_cctor)(void *, void *);
    llvm::Type *KmpcCopyCtorTyArgs[] = {CGM.VoidPtrTy, CGM.VoidPtrTy};
    auto *KmpcCopyCtorTy =
        llvm::FunctionType::get(CGM.VoidPtrTy, KmpcCopyCtorTyArgs,
                                /*isVarArg*/ false)
            ->getPointerTo();
    // typedef void (*kmpc_dtor)(void *);
    auto *KmpcDtorTy =
        llvm::FunctionType::get(CGM.VoidTy, CGM.VoidPtrTy, /*isVarArg*/ false)
            ->getPointerTo();
    llvm::Type *FnTyArgs[] = {getIdentTyPointerTy(), CGM.VoidPtrTy, KmpcCtorTy,
                              KmpcCopyCtorTy, KmpcDtorTy};
    auto *FnTy = llvm::FunctionType::get(CGM.VoidTy, FnTyArgs,
                                        /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_threadprivate_register");
    break;
  }
  case OMPRTL__kmpc_end_critical: {
    // Build void __kmpc_end_critical(ident_t *loc, kmp_int32 global_tid,
    // kmp_critical_name *crit);
    llvm::Type *TypeParams[] = {
        getIdentTyPointerTy(), CGM.Int32Ty,
        llvm::PointerType::getUnqual(KmpCriticalNameTy)};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_end_critical");
    break;
  }
  case OMPRTL__kmpc_cancel_barrier: {
    // Build kmp_int32 __kmpc_cancel_barrier(ident_t *loc, kmp_int32
    // global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name*/ "__kmpc_cancel_barrier");
    break;
  }
  case OMPRTL__kmpc_barrier: {
    // Build void __kmpc_barrier(ident_t *loc, kmp_int32 global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name*/ "__kmpc_barrier");
    break;
  }
  case OMPRTL__kmpc_for_static_fini: {
    // Build void __kmpc_for_static_fini(ident_t *loc, kmp_int32 global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_for_static_fini");
    break;
  }
  case OMPRTL__kmpc_push_num_threads: {
    // Build void __kmpc_push_num_threads(ident_t *loc, kmp_int32 global_tid,
    // kmp_int32 num_threads)
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty,
                                CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_push_num_threads");
    break;
  }
  case OMPRTL__kmpc_serialized_parallel: {
    // Build void __kmpc_serialized_parallel(ident_t *loc, kmp_int32
    // global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_serialized_parallel");
    break;
  }
  case OMPRTL__kmpc_end_serialized_parallel: {
    // Build void __kmpc_end_serialized_parallel(ident_t *loc, kmp_int32
    // global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_end_serialized_parallel");
    break;
  }
  case OMPRTL__kmpc_flush: {
    // Build void __kmpc_flush(ident_t *loc);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_flush");
    break;
  }
  case OMPRTL__kmpc_master: {
    // Build kmp_int32 __kmpc_master(ident_t *loc, kmp_int32 global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_master");
    break;
  }
  case OMPRTL__kmpc_end_master: {
    // Build void __kmpc_end_master(ident_t *loc, kmp_int32 global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_end_master");
    break;
  }
  case OMPRTL__kmpc_omp_taskyield: {
    // Build kmp_int32 __kmpc_omp_taskyield(ident_t *, kmp_int32 global_tid,
    // int end_part);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty, CGM.IntTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_omp_taskyield");
    break;
  }
  case OMPRTL__kmpc_single: {
    // Build kmp_int32 __kmpc_single(ident_t *loc, kmp_int32 global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_single");
    break;
  }
  case OMPRTL__kmpc_end_single: {
    // Build void __kmpc_end_single(ident_t *loc, kmp_int32 global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_end_single");
    break;
  }
  case OMPRTL__kmpc_omp_task_alloc: {
    // Build kmp_task_t *__kmpc_omp_task_alloc(ident_t *, kmp_int32 gtid,
    // kmp_int32 flags, size_t sizeof_kmp_task_t, size_t sizeof_shareds,
    // kmp_routine_entry_t *task_entry);
    assert(KmpRoutineEntryPtrTy != nullptr &&
           "Type kmp_routine_entry_t must be created.");
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty, CGM.Int32Ty,
                                CGM.SizeTy, CGM.SizeTy, KmpRoutineEntryPtrTy};
    // Return void * and then cast to particular kmp_task_t type.
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidPtrTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_omp_task_alloc");
    break;
  }
  case OMPRTL__kmpc_omp_task: {
    // Build kmp_int32 __kmpc_omp_task(ident_t *, kmp_int32 gtid, kmp_task_t
    // *new_task);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty,
                                CGM.VoidPtrTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_omp_task");
    break;
  }
  case OMPRTL__kmpc_copyprivate: {
    // Build void __kmpc_copyprivate(ident_t *loc, kmp_int32 global_tid,
    // size_t cpy_size, void *cpy_data, void(*cpy_func)(void *, void *),
    // kmp_int32 didit);
    llvm::Type *CpyTypeParams[] = {CGM.VoidPtrTy, CGM.VoidPtrTy};
    auto *CpyFnTy =
        llvm::FunctionType::get(CGM.VoidTy, CpyTypeParams, /*isVarArg=*/false);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty, CGM.SizeTy,
                                CGM.VoidPtrTy, CpyFnTy->getPointerTo(),
                                CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_copyprivate");
    break;
  }
  case OMPRTL__kmpc_reduce: {
    // Build kmp_int32 __kmpc_reduce(ident_t *loc, kmp_int32 global_tid,
    // kmp_int32 num_vars, size_t reduce_size, void *reduce_data, void
    // (*reduce_func)(void *lhs_data, void *rhs_data), kmp_critical_name *lck);
    llvm::Type *ReduceTypeParams[] = {CGM.VoidPtrTy, CGM.VoidPtrTy};
    auto *ReduceFnTy = llvm::FunctionType::get(CGM.VoidTy, ReduceTypeParams,
                                               /*isVarArg=*/false);
    llvm::Type *TypeParams[] = {
        getIdentTyPointerTy(), CGM.Int32Ty, CGM.Int32Ty, CGM.SizeTy,
        CGM.VoidPtrTy, ReduceFnTy->getPointerTo(),
        llvm::PointerType::getUnqual(KmpCriticalNameTy)};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_reduce");
    break;
  }
  case OMPRTL__kmpc_reduce_nowait: {
    // Build kmp_int32 __kmpc_reduce_nowait(ident_t *loc, kmp_int32
    // global_tid, kmp_int32 num_vars, size_t reduce_size, void *reduce_data,
    // void (*reduce_func)(void *lhs_data, void *rhs_data), kmp_critical_name
    // *lck);
    llvm::Type *ReduceTypeParams[] = {CGM.VoidPtrTy, CGM.VoidPtrTy};
    auto *ReduceFnTy = llvm::FunctionType::get(CGM.VoidTy, ReduceTypeParams,
                                               /*isVarArg=*/false);
    llvm::Type *TypeParams[] = {
        getIdentTyPointerTy(), CGM.Int32Ty, CGM.Int32Ty, CGM.SizeTy,
        CGM.VoidPtrTy, ReduceFnTy->getPointerTo(),
        llvm::PointerType::getUnqual(KmpCriticalNameTy)};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_reduce_nowait");
    break;
  }
  case OMPRTL__kmpc_end_reduce: {
    // Build void __kmpc_end_reduce(ident_t *loc, kmp_int32 global_tid,
    // kmp_critical_name *lck);
    llvm::Type *TypeParams[] = {
        getIdentTyPointerTy(), CGM.Int32Ty,
        llvm::PointerType::getUnqual(KmpCriticalNameTy)};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_end_reduce");
    break;
  }
  case OMPRTL__kmpc_end_reduce_nowait: {
    // Build __kmpc_end_reduce_nowait(ident_t *loc, kmp_int32 global_tid,
    // kmp_critical_name *lck);
    llvm::Type *TypeParams[] = {
        getIdentTyPointerTy(), CGM.Int32Ty,
        llvm::PointerType::getUnqual(KmpCriticalNameTy)};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn =
        CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_end_reduce_nowait");
    break;
  }
  case OMPRTL__kmpc_omp_task_begin_if0: {
    // Build void __kmpc_omp_task(ident_t *, kmp_int32 gtid, kmp_task_t
    // *new_task);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty,
                                CGM.VoidPtrTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn =
        CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_omp_task_begin_if0");
    break;
  }
  case OMPRTL__kmpc_omp_task_complete_if0: {
    // Build void __kmpc_omp_task(ident_t *, kmp_int32 gtid, kmp_task_t
    // *new_task);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty,
                                CGM.VoidPtrTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy,
                                      /*Name=*/"__kmpc_omp_task_complete_if0");
    break;
  }
  case OMPRTL__kmpc_ordered: {
    // Build void __kmpc_ordered(ident_t *loc, kmp_int32 global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_ordered");
    break;
  }
  case OMPRTL__kmpc_end_ordered: {
    // Build void __kmpc_end_ordered(ident_t *loc, kmp_int32 global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_end_ordered");
    break;
  }
  case OMPRTL__kmpc_omp_taskwait: {
    // Build kmp_int32 __kmpc_omp_taskwait(ident_t *loc, kmp_int32 global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_omp_taskwait");
    break;
  }
  case OMPRTL__kmpc_taskgroup: {
    // Build void __kmpc_taskgroup(ident_t *loc, kmp_int32 global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_taskgroup");
    break;
  }
  case OMPRTL__kmpc_end_taskgroup: {
    // Build void __kmpc_end_taskgroup(ident_t *loc, kmp_int32 global_tid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_end_taskgroup");
    break;
  }
  case OMPRTL__kmpc_push_proc_bind: {
    // Build void __kmpc_push_proc_bind(ident_t *loc, kmp_int32 global_tid,
    // int proc_bind)
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty, CGM.IntTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_push_proc_bind");
    break;
  }
  case OMPRTL__kmpc_omp_task_with_deps: {
    // Build kmp_int32 __kmpc_omp_task_with_deps(ident_t *, kmp_int32 gtid,
    // kmp_task_t *new_task, kmp_int32 ndeps, kmp_depend_info_t *dep_list,
    // kmp_int32 ndeps_noalias, kmp_depend_info_t *noalias_dep_list);
    llvm::Type *TypeParams[] = {
        getIdentTyPointerTy(), CGM.Int32Ty, CGM.VoidPtrTy, CGM.Int32Ty,
        CGM.VoidPtrTy,         CGM.Int32Ty, CGM.VoidPtrTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg=*/false);
    RTLFn =
        CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_omp_task_with_deps");
    break;
  }
  case OMPRTL__kmpc_omp_wait_deps: {
    // Build void __kmpc_omp_wait_deps(ident_t *, kmp_int32 gtid,
    // kmp_int32 ndeps, kmp_depend_info_t *dep_list, kmp_int32 ndeps_noalias,
    // kmp_depend_info_t *noalias_dep_list);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty,
                                CGM.Int32Ty,           CGM.VoidPtrTy,
                                CGM.Int32Ty,           CGM.VoidPtrTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_omp_wait_deps");
    break;
  }
  case OMPRTL__kmpc_cancellationpoint: {
    // Build kmp_int32 __kmpc_cancellationpoint(ident_t *loc, kmp_int32
    // global_tid, kmp_int32 cncl_kind)
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty, CGM.IntTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_cancellationpoint");
    break;
  }
  case OMPRTL__kmpc_cancel: {
    // Build kmp_int32 __kmpc_cancel(ident_t *loc, kmp_int32 global_tid,
    // kmp_int32 cncl_kind)
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty, CGM.IntTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_cancel");
    break;
  }
  case OMPRTL__kmpc_push_num_teams: {
    // Build void kmpc_push_num_teams (ident_t loc, kmp_int32 global_tid,
    // kmp_int32 num_teams, kmp_int32 num_threads)
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty, CGM.Int32Ty,
        CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_push_num_teams");
    break;
  }
  case OMPRTL__kmpc_fork_teams: {
    // Build void __kmpc_fork_teams(ident_t *loc, kmp_int32 argc, kmpc_micro
    // microtask, ...);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty,
                                getKmpc_MicroPointerTy()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ true);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_fork_teams");
    break;
  }
  case OMPRTL__kmpc_taskloop: {
    // Build void __kmpc_taskloop(ident_t *loc, int gtid, kmp_task_t *task, int
    // if_val, kmp_uint64 *lb, kmp_uint64 *ub, kmp_int64 st, int nogroup, int
    // sched, kmp_uint64 grainsize, void *task_dup);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(),
                                CGM.IntTy,
                                CGM.VoidPtrTy,
                                CGM.IntTy,
                                CGM.Int64Ty->getPointerTo(),
                                CGM.Int64Ty->getPointerTo(),
                                CGM.Int64Ty,
                                CGM.IntTy,
                                CGM.IntTy,
                                CGM.Int64Ty,
                                CGM.VoidPtrTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_taskloop");
    break;
  }
  case OMPRTL__kmpc_doacross_init: {
    // Build void __kmpc_doacross_init(ident_t *loc, kmp_int32 gtid, kmp_int32
    // num_dims, struct kmp_dim *dims);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(),
                                CGM.Int32Ty,
                                CGM.Int32Ty,
                                CGM.VoidPtrTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_doacross_init");
    break;
  }
  case OMPRTL__kmpc_doacross_fini: {
    // Build void __kmpc_doacross_fini(ident_t *loc, kmp_int32 gtid);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_doacross_fini");
    break;
  }
  case OMPRTL__kmpc_doacross_post: {
    // Build void __kmpc_doacross_post(ident_t *loc, kmp_int32 gtid, kmp_int64
    // *vec);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty,
                                CGM.Int64Ty->getPointerTo()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_doacross_post");
    break;
  }
  case OMPRTL__kmpc_doacross_wait: {
    // Build void __kmpc_doacross_wait(ident_t *loc, kmp_int32 gtid, kmp_int64
    // *vec);
    llvm::Type *TypeParams[] = {getIdentTyPointerTy(), CGM.Int32Ty,
                                CGM.Int64Ty->getPointerTo()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_doacross_wait");
    break;
  }
  case OMPRTL__kmpc_task_reduction_init: {
    // Build void *__kmpc_task_reduction_init(int gtid, int num_data, void
    // *data);
    llvm::Type *TypeParams[] = {CGM.IntTy, CGM.IntTy, CGM.VoidPtrTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidPtrTy, TypeParams, /*isVarArg=*/false);
    RTLFn =
        CGM.CreateRuntimeFunction(FnTy, /*Name=*/"__kmpc_task_reduction_init");
    break;
  }
  case OMPRTL__kmpc_task_reduction_get_th_data: {
    // Build void *__kmpc_task_reduction_get_th_data(int gtid, void *tg, void
    // *d);
    llvm::Type *TypeParams[] = {CGM.IntTy, CGM.VoidPtrTy, CGM.VoidPtrTy};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidPtrTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(
        FnTy, /*Name=*/"__kmpc_task_reduction_get_th_data");
    break;
  }
  case OMPRTL__kmpc_push_target_tripcount: {
    // Build void __kmpc_push_target_tripcount(int64_t device_id, kmp_uint64
    // size);
    llvm::Type *TypeParams[] = {CGM.Int64Ty, CGM.Int64Ty};
    llvm::FunctionType *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__kmpc_push_target_tripcount");
    break;
  }
  case OMPRTL__tgt_target: {
    // Build int32_t __tgt_target(int64_t device_id, void *host_ptr, int32_t
    // arg_num, void** args_base, void **args, size_t *arg_sizes, int64_t
    // *arg_types);
    llvm::Type *TypeParams[] = {CGM.Int64Ty,
                                CGM.VoidPtrTy,
                                CGM.Int32Ty,
                                CGM.VoidPtrPtrTy,
                                CGM.VoidPtrPtrTy,
                                CGM.SizeTy->getPointerTo(),
                                CGM.Int64Ty->getPointerTo()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__tgt_target");
    break;
  }
  case OMPRTL__tgt_target_nowait: {
    // Build int32_t __tgt_target_nowait(int64_t device_id, void *host_ptr,
    // int32_t arg_num, void** args_base, void **args, size_t *arg_sizes,
    // int64_t *arg_types);
    llvm::Type *TypeParams[] = {CGM.Int64Ty,
                                CGM.VoidPtrTy,
                                CGM.Int32Ty,
                                CGM.VoidPtrPtrTy,
                                CGM.VoidPtrPtrTy,
                                CGM.SizeTy->getPointerTo(),
                                CGM.Int64Ty->getPointerTo()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__tgt_target_nowait");
    break;
  }
  case OMPRTL__tgt_target_teams: {
    // Build int32_t __tgt_target_teams(int64_t device_id, void *host_ptr,
    // int32_t arg_num, void** args_base, void **args, size_t *arg_sizes,
    // int64_t *arg_types, int32_t num_teams, int32_t thread_limit);
    llvm::Type *TypeParams[] = {CGM.Int64Ty,
                                CGM.VoidPtrTy,
                                CGM.Int32Ty,
                                CGM.VoidPtrPtrTy,
                                CGM.VoidPtrPtrTy,
                                CGM.SizeTy->getPointerTo(),
                                CGM.Int64Ty->getPointerTo(),
                                CGM.Int32Ty,
                                CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__tgt_target_teams");
    break;
  }
  case OMPRTL__tgt_target_teams_nowait: {
    // Build int32_t __tgt_target_teams_nowait(int64_t device_id, void
    // *host_ptr, int32_t arg_num, void** args_base, void **args, size_t
    // *arg_sizes, int64_t *arg_types, int32_t num_teams, int32_t thread_limit);
    llvm::Type *TypeParams[] = {CGM.Int64Ty,
                                CGM.VoidPtrTy,
                                CGM.Int32Ty,
                                CGM.VoidPtrPtrTy,
                                CGM.VoidPtrPtrTy,
                                CGM.SizeTy->getPointerTo(),
                                CGM.Int64Ty->getPointerTo(),
                                CGM.Int32Ty,
                                CGM.Int32Ty};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__tgt_target_teams_nowait");
    break;
  }
  case OMPRTL__tgt_register_lib: {
    // Build void __tgt_register_lib(__tgt_bin_desc *desc);
    QualType ParamTy =
        CGM.getContext().getPointerType(getTgtBinaryDescriptorQTy());
    llvm::Type *TypeParams[] = {CGM.getTypes().ConvertTypeForMem(ParamTy)};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__tgt_register_lib");
    break;
  }
  case OMPRTL__tgt_unregister_lib: {
    // Build void __tgt_unregister_lib(__tgt_bin_desc *desc);
    QualType ParamTy =
        CGM.getContext().getPointerType(getTgtBinaryDescriptorQTy());
    llvm::Type *TypeParams[] = {CGM.getTypes().ConvertTypeForMem(ParamTy)};
    auto *FnTy =
        llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__tgt_unregister_lib");
    break;
  }
  case OMPRTL__tgt_target_data_begin: {
    // Build void __tgt_target_data_begin(int64_t device_id, int32_t arg_num,
    // void** args_base, void **args, size_t *arg_sizes, int64_t *arg_types);
    llvm::Type *TypeParams[] = {CGM.Int64Ty,
                                CGM.Int32Ty,
                                CGM.VoidPtrPtrTy,
                                CGM.VoidPtrPtrTy,
                                CGM.SizeTy->getPointerTo(),
                                CGM.Int64Ty->getPointerTo()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__tgt_target_data_begin");
    break;
  }
  case OMPRTL__tgt_target_data_begin_nowait: {
    // Build void __tgt_target_data_begin_nowait(int64_t device_id, int32_t
    // arg_num, void** args_base, void **args, size_t *arg_sizes, int64_t
    // *arg_types);
    llvm::Type *TypeParams[] = {CGM.Int64Ty,
                                CGM.Int32Ty,
                                CGM.VoidPtrPtrTy,
                                CGM.VoidPtrPtrTy,
                                CGM.SizeTy->getPointerTo(),
                                CGM.Int64Ty->getPointerTo()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__tgt_target_data_begin_nowait");
    break;
  }
  case OMPRTL__tgt_target_data_end: {
    // Build void __tgt_target_data_end(int64_t device_id, int32_t arg_num,
    // void** args_base, void **args, size_t *arg_sizes, int64_t *arg_types);
    llvm::Type *TypeParams[] = {CGM.Int64Ty,
                                CGM.Int32Ty,
                                CGM.VoidPtrPtrTy,
                                CGM.VoidPtrPtrTy,
                                CGM.SizeTy->getPointerTo(),
                                CGM.Int64Ty->getPointerTo()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__tgt_target_data_end");
    break;
  }
  case OMPRTL__tgt_target_data_end_nowait: {
    // Build void __tgt_target_data_end_nowait(int64_t device_id, int32_t
    // arg_num, void** args_base, void **args, size_t *arg_sizes, int64_t
    // *arg_types);
    llvm::Type *TypeParams[] = {CGM.Int64Ty,
                                CGM.Int32Ty,
                                CGM.VoidPtrPtrTy,
                                CGM.VoidPtrPtrTy,
                                CGM.SizeTy->getPointerTo(),
                                CGM.Int64Ty->getPointerTo()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__tgt_target_data_end_nowait");
    break;
  }
  case OMPRTL__tgt_target_data_update: {
    // Build void __tgt_target_data_update(int64_t device_id, int32_t arg_num,
    // void** args_base, void **args, size_t *arg_sizes, int64_t *arg_types);
    llvm::Type *TypeParams[] = {CGM.Int64Ty,
                                CGM.Int32Ty,
                                CGM.VoidPtrPtrTy,
                                CGM.VoidPtrPtrTy,
                                CGM.SizeTy->getPointerTo(),
                                CGM.Int64Ty->getPointerTo()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__tgt_target_data_update");
    break;
  }
  case OMPRTL__tgt_target_data_update_nowait: {
    // Build void __tgt_target_data_update_nowait(int64_t device_id, int32_t
    // arg_num, void** args_base, void **args, size_t *arg_sizes, int64_t
    // *arg_types);
    llvm::Type *TypeParams[] = {CGM.Int64Ty,
                                CGM.Int32Ty,
                                CGM.VoidPtrPtrTy,
                                CGM.VoidPtrPtrTy,
                                CGM.SizeTy->getPointerTo(),
                                CGM.Int64Ty->getPointerTo()};
    auto *FnTy =
        llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
    RTLFn = CGM.CreateRuntimeFunction(FnTy, "__tgt_target_data_update_nowait");
    break;
  }
  }
  assert(RTLFn && "Unable to find OpenMP runtime function");
  return RTLFn;
}

llvm::Constant *CGOpenMPRuntime::createForStaticInitFunction(unsigned IVSize,
                                                             bool IVSigned) {
  assert((IVSize == 32 || IVSize == 64) &&
         "IV size is not compatible with the omp runtime");
  StringRef Name = IVSize == 32 ? (IVSigned ? "__kmpc_for_static_init_4"
                                            : "__kmpc_for_static_init_4u")
                                : (IVSigned ? "__kmpc_for_static_init_8"
                                            : "__kmpc_for_static_init_8u");
  llvm::Type *ITy = IVSize == 32 ? CGM.Int32Ty : CGM.Int64Ty;
  auto *PtrTy = llvm::PointerType::getUnqual(ITy);
  llvm::Type *TypeParams[] = {
    getIdentTyPointerTy(),                     // loc
    CGM.Int32Ty,                               // tid
    CGM.Int32Ty,                               // schedtype
    llvm::PointerType::getUnqual(CGM.Int32Ty), // p_lastiter
    PtrTy,                                     // p_lower
    PtrTy,                                     // p_upper
    PtrTy,                                     // p_stride
    ITy,                                       // incr
    ITy                                        // chunk
  };
  auto *FnTy =
      llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
  return CGM.CreateRuntimeFunction(FnTy, Name);
}

llvm::Constant *CGOpenMPRuntime::createDispatchInitFunction(unsigned IVSize,
                                                            bool IVSigned) {
  assert((IVSize == 32 || IVSize == 64) &&
         "IV size is not compatible with the omp runtime");
  StringRef Name =
      IVSize == 32
          ? (IVSigned ? "__kmpc_dispatch_init_4" : "__kmpc_dispatch_init_4u")
          : (IVSigned ? "__kmpc_dispatch_init_8" : "__kmpc_dispatch_init_8u");
  llvm::Type *ITy = IVSize == 32 ? CGM.Int32Ty : CGM.Int64Ty;
  llvm::Type *TypeParams[] = { getIdentTyPointerTy(), // loc
                               CGM.Int32Ty,           // tid
                               CGM.Int32Ty,           // schedtype
                               ITy,                   // lower
                               ITy,                   // upper
                               ITy,                   // stride
                               ITy                    // chunk
  };
  auto *FnTy =
      llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg*/ false);
  return CGM.CreateRuntimeFunction(FnTy, Name);
}

llvm::Constant *CGOpenMPRuntime::createDispatchFiniFunction(unsigned IVSize,
                                                            bool IVSigned) {
  assert((IVSize == 32 || IVSize == 64) &&
         "IV size is not compatible with the omp runtime");
  StringRef Name =
      IVSize == 32
          ? (IVSigned ? "__kmpc_dispatch_fini_4" : "__kmpc_dispatch_fini_4u")
          : (IVSigned ? "__kmpc_dispatch_fini_8" : "__kmpc_dispatch_fini_8u");
  llvm::Type *TypeParams[] = {
      getIdentTyPointerTy(), // loc
      CGM.Int32Ty,           // tid
  };
  auto *FnTy =
      llvm::FunctionType::get(CGM.VoidTy, TypeParams, /*isVarArg=*/false);
  return CGM.CreateRuntimeFunction(FnTy, Name);
}

llvm::Constant *CGOpenMPRuntime::createDispatchNextFunction(unsigned IVSize,
                                                            bool IVSigned) {
  assert((IVSize == 32 || IVSize == 64) &&
         "IV size is not compatible with the omp runtime");
  StringRef Name =
      IVSize == 32
          ? (IVSigned ? "__kmpc_dispatch_next_4" : "__kmpc_dispatch_next_4u")
          : (IVSigned ? "__kmpc_dispatch_next_8" : "__kmpc_dispatch_next_8u");
  llvm::Type *ITy = IVSize == 32 ? CGM.Int32Ty : CGM.Int64Ty;
  auto *PtrTy = llvm::PointerType::getUnqual(ITy);
  llvm::Type *TypeParams[] = {
    getIdentTyPointerTy(),                     // loc
    CGM.Int32Ty,                               // tid
    llvm::PointerType::getUnqual(CGM.Int32Ty), // p_lastiter
    PtrTy,                                     // p_lower
    PtrTy,                                     // p_upper
    PtrTy                                      // p_stride
  };
  auto *FnTy =
      llvm::FunctionType::get(CGM.Int32Ty, TypeParams, /*isVarArg*/ false);
  return CGM.CreateRuntimeFunction(FnTy, Name);
}

Address CGOpenMPRuntime::getAddrOfDeclareTargetLink(const VarDecl *VD) {
  if (CGM.getLangOpts().OpenMPSimd)
    return Address::invalid();
  llvm::Optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD);
  if (Res && *Res == OMPDeclareTargetDeclAttr::MT_Link) {
    SmallString<64> PtrName;
    {
      llvm::raw_svector_ostream OS(PtrName);
      OS << CGM.getMangledName(GlobalDecl(VD)) << "_decl_tgt_link_ptr";
    }
    llvm::Value *Ptr = CGM.getModule().getNamedValue(PtrName);
    if (!Ptr) {
      QualType PtrTy = CGM.getContext().getPointerType(VD->getType());
      Ptr = getOrCreateInternalVariable(CGM.getTypes().ConvertTypeForMem(PtrTy),
                                        PtrName);
      if (!CGM.getLangOpts().OpenMPIsDevice) {
        auto *GV = cast<llvm::GlobalVariable>(Ptr);
        GV->setLinkage(llvm::GlobalValue::ExternalLinkage);
        GV->setInitializer(CGM.GetAddrOfGlobal(VD));
      }
      CGM.addUsedGlobal(cast<llvm::GlobalValue>(Ptr));
      registerTargetGlobalVariable(VD, cast<llvm::Constant>(Ptr));
    }
    return Address(Ptr, CGM.getContext().getDeclAlign(VD));
  }
  return Address::invalid();
}

llvm::Constant *
CGOpenMPRuntime::getOrCreateThreadPrivateCache(const VarDecl *VD) {
  assert(!CGM.getLangOpts().OpenMPUseTLS ||
         !CGM.getContext().getTargetInfo().isTLSSupported());
  // Lookup the entry, lazily creating it if necessary.
  std::string Suffix = getName({"cache", ""});
  return getOrCreateInternalVariable(
      CGM.Int8PtrPtrTy, Twine(CGM.getMangledName(VD)).concat(Suffix));
}

Address CGOpenMPRuntime::getAddrOfThreadPrivate(CodeGenFunction &CGF,
                                                const VarDecl *VD,
                                                Address VDAddr,
                                                SourceLocation Loc) {
  if (CGM.getLangOpts().OpenMPUseTLS &&
      CGM.getContext().getTargetInfo().isTLSSupported())
    return VDAddr;

  llvm::Type *VarTy = VDAddr.getElementType();
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
                         CGF.Builder.CreatePointerCast(VDAddr.getPointer(),
                                                       CGM.Int8PtrTy),
                         CGM.getSize(CGM.GetTargetTypeStoreSize(VarTy)),
                         getOrCreateThreadPrivateCache(VD)};
  return Address(CGF.EmitRuntimeCall(
      createRuntimeFunction(OMPRTL__kmpc_threadprivate_cached), Args),
                 VDAddr.getAlignment());
}

void CGOpenMPRuntime::emitThreadPrivateVarInit(
    CodeGenFunction &CGF, Address VDAddr, llvm::Value *Ctor,
    llvm::Value *CopyCtor, llvm::Value *Dtor, SourceLocation Loc) {
  // Call kmp_int32 __kmpc_global_thread_num(&loc) to init OpenMP runtime
  // library.
  llvm::Value *OMPLoc = emitUpdateLocation(CGF, Loc);
  CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_global_thread_num),
                      OMPLoc);
  // Call __kmpc_threadprivate_register(&loc, &var, ctor, cctor/*NULL*/, dtor)
  // to register constructor/destructor for variable.
  llvm::Value *Args[] = {
      OMPLoc, CGF.Builder.CreatePointerCast(VDAddr.getPointer(), CGM.VoidPtrTy),
      Ctor, CopyCtor, Dtor};
  CGF.EmitRuntimeCall(
      createRuntimeFunction(OMPRTL__kmpc_threadprivate_register), Args);
}

llvm::Function *CGOpenMPRuntime::emitThreadPrivateVarDefinition(
    const VarDecl *VD, Address VDAddr, SourceLocation Loc,
    bool PerformInit, CodeGenFunction *CGF) {
  if (CGM.getLangOpts().OpenMPUseTLS &&
      CGM.getContext().getTargetInfo().isTLSSupported())
    return nullptr;

  VD = VD->getDefinition(CGM.getContext());
  if (VD && ThreadPrivateWithDefinition.insert(CGM.getMangledName(VD)).second) {
    QualType ASTTy = VD->getType();

    llvm::Value *Ctor = nullptr, *CopyCtor = nullptr, *Dtor = nullptr;
    const Expr *Init = VD->getAnyInitializer();
    if (CGM.getLangOpts().CPlusPlus && PerformInit) {
      // Generate function that re-emits the declaration's initializer into the
      // threadprivate copy of the variable VD
      CodeGenFunction CtorCGF(CGM);
      FunctionArgList Args;
      ImplicitParamDecl Dst(CGM.getContext(), /*DC=*/nullptr, Loc,
                            /*Id=*/nullptr, CGM.getContext().VoidPtrTy,
                            ImplicitParamDecl::Other);
      Args.push_back(&Dst);

      const auto &FI = CGM.getTypes().arrangeBuiltinFunctionDeclaration(
          CGM.getContext().VoidPtrTy, Args);
      llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(FI);
      std::string Name = getName({"__kmpc_global_ctor_", ""});
      llvm::Function *Fn =
          CGM.CreateGlobalInitOrDestructFunction(FTy, Name, FI, Loc);
      CtorCGF.StartFunction(GlobalDecl(), CGM.getContext().VoidPtrTy, Fn, FI,
                            Args, Loc, Loc);
      llvm::Value *ArgVal = CtorCGF.EmitLoadOfScalar(
          CtorCGF.GetAddrOfLocalVar(&Dst), /*Volatile=*/false,
          CGM.getContext().VoidPtrTy, Dst.getLocation());
      Address Arg = Address(ArgVal, VDAddr.getAlignment());
      Arg = CtorCGF.Builder.CreateElementBitCast(
          Arg, CtorCGF.ConvertTypeForMem(ASTTy));
      CtorCGF.EmitAnyExprToMem(Init, Arg, Init->getType().getQualifiers(),
                               /*IsInitializer=*/true);
      ArgVal = CtorCGF.EmitLoadOfScalar(
          CtorCGF.GetAddrOfLocalVar(&Dst), /*Volatile=*/false,
          CGM.getContext().VoidPtrTy, Dst.getLocation());
      CtorCGF.Builder.CreateStore(ArgVal, CtorCGF.ReturnValue);
      CtorCGF.FinishFunction();
      Ctor = Fn;
    }
    if (VD->getType().isDestructedType() != QualType::DK_none) {
      // Generate function that emits destructor call for the threadprivate copy
      // of the variable VD
      CodeGenFunction DtorCGF(CGM);
      FunctionArgList Args;
      ImplicitParamDecl Dst(CGM.getContext(), /*DC=*/nullptr, Loc,
                            /*Id=*/nullptr, CGM.getContext().VoidPtrTy,
                            ImplicitParamDecl::Other);
      Args.push_back(&Dst);

      const auto &FI = CGM.getTypes().arrangeBuiltinFunctionDeclaration(
          CGM.getContext().VoidTy, Args);
      llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(FI);
      std::string Name = getName({"__kmpc_global_dtor_", ""});
      llvm::Function *Fn =
          CGM.CreateGlobalInitOrDestructFunction(FTy, Name, FI, Loc);
      auto NL = ApplyDebugLocation::CreateEmpty(DtorCGF);
      DtorCGF.StartFunction(GlobalDecl(), CGM.getContext().VoidTy, Fn, FI, Args,
                            Loc, Loc);
      // Create a scope with an artificial location for the body of this function.
      auto AL = ApplyDebugLocation::CreateArtificial(DtorCGF);
      llvm::Value *ArgVal = DtorCGF.EmitLoadOfScalar(
          DtorCGF.GetAddrOfLocalVar(&Dst),
          /*Volatile=*/false, CGM.getContext().VoidPtrTy, Dst.getLocation());
      DtorCGF.emitDestroy(Address(ArgVal, VDAddr.getAlignment()), ASTTy,
                          DtorCGF.getDestroyer(ASTTy.isDestructedType()),
                          DtorCGF.needsEHCleanup(ASTTy.isDestructedType()));
      DtorCGF.FinishFunction();
      Dtor = Fn;
    }
    // Do not emit init function if it is not required.
    if (!Ctor && !Dtor)
      return nullptr;

    llvm::Type *CopyCtorTyArgs[] = {CGM.VoidPtrTy, CGM.VoidPtrTy};
    auto *CopyCtorTy = llvm::FunctionType::get(CGM.VoidPtrTy, CopyCtorTyArgs,
                                               /*isVarArg=*/false)
                           ->getPointerTo();
    // Copying constructor for the threadprivate variable.
    // Must be NULL - reserved by runtime, but currently it requires that this
    // parameter is always NULL. Otherwise it fires assertion.
    CopyCtor = llvm::Constant::getNullValue(CopyCtorTy);
    if (Ctor == nullptr) {
      auto *CtorTy = llvm::FunctionType::get(CGM.VoidPtrTy, CGM.VoidPtrTy,
                                             /*isVarArg=*/false)
                         ->getPointerTo();
      Ctor = llvm::Constant::getNullValue(CtorTy);
    }
    if (Dtor == nullptr) {
      auto *DtorTy = llvm::FunctionType::get(CGM.VoidTy, CGM.VoidPtrTy,
                                             /*isVarArg=*/false)
                         ->getPointerTo();
      Dtor = llvm::Constant::getNullValue(DtorTy);
    }
    if (!CGF) {
      auto *InitFunctionTy =
          llvm::FunctionType::get(CGM.VoidTy, /*isVarArg*/ false);
      std::string Name = getName({"__omp_threadprivate_init_", ""});
      llvm::Function *InitFunction = CGM.CreateGlobalInitOrDestructFunction(
          InitFunctionTy, Name, CGM.getTypes().arrangeNullaryFunction());
      CodeGenFunction InitCGF(CGM);
      FunctionArgList ArgList;
      InitCGF.StartFunction(GlobalDecl(), CGM.getContext().VoidTy, InitFunction,
                            CGM.getTypes().arrangeNullaryFunction(), ArgList,
                            Loc, Loc);
      emitThreadPrivateVarInit(InitCGF, VDAddr, Ctor, CopyCtor, Dtor, Loc);
      InitCGF.FinishFunction();
      return InitFunction;
    }
    emitThreadPrivateVarInit(*CGF, VDAddr, Ctor, CopyCtor, Dtor, Loc);
  }
  return nullptr;
}

/// Obtain information that uniquely identifies a target entry. This
/// consists of the file and device IDs as well as line number associated with
/// the relevant entry source location.
static void getTargetEntryUniqueInfo(ASTContext &C, SourceLocation Loc,
                                     unsigned &DeviceID, unsigned &FileID,
                                     unsigned &LineNum) {
  SourceManager &SM = C.getSourceManager();

  // The loc should be always valid and have a file ID (the user cannot use
  // #pragma directives in macros)

  assert(Loc.isValid() && "Source location is expected to be always valid.");

  PresumedLoc PLoc = SM.getPresumedLoc(Loc);
  assert(PLoc.isValid() && "Source location is expected to be always valid.");

  llvm::sys::fs::UniqueID ID;
  if (auto EC = llvm::sys::fs::getUniqueID(PLoc.getFilename(), ID))
    SM.getDiagnostics().Report(diag::err_cannot_open_file)
        << PLoc.getFilename() << EC.message();

  DeviceID = ID.getDevice();
  FileID = ID.getFile();
  LineNum = PLoc.getLine();
}

bool CGOpenMPRuntime::emitDeclareTargetVarDefinition(const VarDecl *VD,
                                                     llvm::GlobalVariable *Addr,
                                                     bool PerformInit) {
  Optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD);
  if (!Res || *Res == OMPDeclareTargetDeclAttr::MT_Link)
    return CGM.getLangOpts().OpenMPIsDevice;
  VD = VD->getDefinition(CGM.getContext());
  if (VD && !DeclareTargetWithDefinition.insert(CGM.getMangledName(VD)).second)
    return CGM.getLangOpts().OpenMPIsDevice;

  QualType ASTTy = VD->getType();

  SourceLocation Loc = VD->getCanonicalDecl()->getBeginLoc();
  // Produce the unique prefix to identify the new target regions. We use
  // the source location of the variable declaration which we know to not
  // conflict with any target region.
  unsigned DeviceID;
  unsigned FileID;
  unsigned Line;
  getTargetEntryUniqueInfo(CGM.getContext(), Loc, DeviceID, FileID, Line);
  SmallString<128> Buffer, Out;
  {
    llvm::raw_svector_ostream OS(Buffer);
    OS << "__omp_offloading_" << llvm::format("_%x", DeviceID)
       << llvm::format("_%x_", FileID) << VD->getName() << "_l" << Line;
  }

  const Expr *Init = VD->getAnyInitializer();
  if (CGM.getLangOpts().CPlusPlus && PerformInit) {
    llvm::Constant *Ctor;
    llvm::Constant *ID;
    if (CGM.getLangOpts().OpenMPIsDevice) {
      // Generate function that re-emits the declaration's initializer into
      // the threadprivate copy of the variable VD
      CodeGenFunction CtorCGF(CGM);

      const CGFunctionInfo &FI = CGM.getTypes().arrangeNullaryFunction();
      llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(FI);
      llvm::Function *Fn = CGM.CreateGlobalInitOrDestructFunction(
          FTy, Twine(Buffer, "_ctor"), FI, Loc);
      auto NL = ApplyDebugLocation::CreateEmpty(CtorCGF);
      CtorCGF.StartFunction(GlobalDecl(), CGM.getContext().VoidTy, Fn, FI,
                            FunctionArgList(), Loc, Loc);
      auto AL = ApplyDebugLocation::CreateArtificial(CtorCGF);
      CtorCGF.EmitAnyExprToMem(Init,
                               Address(Addr, CGM.getContext().getDeclAlign(VD)),
                               Init->getType().getQualifiers(),
                               /*IsInitializer=*/true);
      CtorCGF.FinishFunction();
      Ctor = Fn;
      ID = llvm::ConstantExpr::getBitCast(Fn, CGM.Int8PtrTy);
      CGM.addUsedGlobal(cast<llvm::GlobalValue>(Ctor));
    } else {
      Ctor = new llvm::GlobalVariable(
          CGM.getModule(), CGM.Int8Ty, /*isConstant=*/true,
          llvm::GlobalValue::PrivateLinkage,
          llvm::Constant::getNullValue(CGM.Int8Ty), Twine(Buffer, "_ctor"));
      ID = Ctor;
    }

    // Register the information for the entry associated with the constructor.
    Out.clear();
    OffloadEntriesInfoManager.registerTargetRegionEntryInfo(
        DeviceID, FileID, Twine(Buffer, "_ctor").toStringRef(Out), Line, Ctor,
        ID, OffloadEntriesInfoManagerTy::OMPTargetRegionEntryCtor);
  }
  if (VD->getType().isDestructedType() != QualType::DK_none) {
    llvm::Constant *Dtor;
    llvm::Constant *ID;
    if (CGM.getLangOpts().OpenMPIsDevice) {
      // Generate function that emits destructor call for the threadprivate
      // copy of the variable VD
      CodeGenFunction DtorCGF(CGM);

      const CGFunctionInfo &FI = CGM.getTypes().arrangeNullaryFunction();
      llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(FI);
      llvm::Function *Fn = CGM.CreateGlobalInitOrDestructFunction(
          FTy, Twine(Buffer, "_dtor"), FI, Loc);
      auto NL = ApplyDebugLocation::CreateEmpty(DtorCGF);
      DtorCGF.StartFunction(GlobalDecl(), CGM.getContext().VoidTy, Fn, FI,
                            FunctionArgList(), Loc, Loc);
      // Create a scope with an artificial location for the body of this
      // function.
      auto AL = ApplyDebugLocation::CreateArtificial(DtorCGF);
      DtorCGF.emitDestroy(Address(Addr, CGM.getContext().getDeclAlign(VD)),
                          ASTTy, DtorCGF.getDestroyer(ASTTy.isDestructedType()),
                          DtorCGF.needsEHCleanup(ASTTy.isDestructedType()));
      DtorCGF.FinishFunction();
      Dtor = Fn;
      ID = llvm::ConstantExpr::getBitCast(Fn, CGM.Int8PtrTy);
      CGM.addUsedGlobal(cast<llvm::GlobalValue>(Dtor));
    } else {
      Dtor = new llvm::GlobalVariable(
          CGM.getModule(), CGM.Int8Ty, /*isConstant=*/true,
          llvm::GlobalValue::PrivateLinkage,
          llvm::Constant::getNullValue(CGM.Int8Ty), Twine(Buffer, "_dtor"));
      ID = Dtor;
    }
    // Register the information for the entry associated with the destructor.
    Out.clear();
    OffloadEntriesInfoManager.registerTargetRegionEntryInfo(
        DeviceID, FileID, Twine(Buffer, "_dtor").toStringRef(Out), Line, Dtor,
        ID, OffloadEntriesInfoManagerTy::OMPTargetRegionEntryDtor);
  }
  return CGM.getLangOpts().OpenMPIsDevice;
}

Address CGOpenMPRuntime::getAddrOfArtificialThreadPrivate(CodeGenFunction &CGF,
                                                          QualType VarType,
                                                          StringRef Name) {
  std::string Suffix = getName({"artificial", ""});
  std::string CacheSuffix = getName({"cache", ""});
  llvm::Type *VarLVType = CGF.ConvertTypeForMem(VarType);
  llvm::Value *GAddr =
      getOrCreateInternalVariable(VarLVType, Twine(Name).concat(Suffix));
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, SourceLocation()),
      getThreadID(CGF, SourceLocation()),
      CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(GAddr, CGM.VoidPtrTy),
      CGF.Builder.CreateIntCast(CGF.getTypeSize(VarType), CGM.SizeTy,
                                /*IsSigned=*/false),
      getOrCreateInternalVariable(
          CGM.VoidPtrPtrTy, Twine(Name).concat(Suffix).concat(CacheSuffix))};
  return Address(
      CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
          CGF.EmitRuntimeCall(
              createRuntimeFunction(OMPRTL__kmpc_threadprivate_cached), Args),
          VarLVType->getPointerTo(/*AddrSpace=*/0)),
      CGM.getPointerAlign());
}

void CGOpenMPRuntime::emitOMPIfClause(CodeGenFunction &CGF, const Expr *Cond,
                                      const RegionCodeGenTy &ThenGen,
                                      const RegionCodeGenTy &ElseGen) {
  CodeGenFunction::LexicalScope ConditionScope(CGF, Cond->getSourceRange());

  // If the condition constant folds and can be elided, try to avoid emitting
  // the condition and the dead arm of the if/else.
  bool CondConstant;
  if (CGF.ConstantFoldsToSimpleInteger(Cond, CondConstant)) {
    if (CondConstant)
      ThenGen(CGF);
    else
      ElseGen(CGF);
    return;
  }

  // Otherwise, the condition did not fold, or we couldn't elide it.  Just
  // emit the conditional branch.
  llvm::BasicBlock *ThenBlock = CGF.createBasicBlock("omp_if.then");
  llvm::BasicBlock *ElseBlock = CGF.createBasicBlock("omp_if.else");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("omp_if.end");
  CGF.EmitBranchOnBoolExpr(Cond, ThenBlock, ElseBlock, /*TrueCount=*/0);

  // Emit the 'then' code.
  CGF.EmitBlock(ThenBlock);
  ThenGen(CGF);
  CGF.EmitBranch(ContBlock);
  // Emit the 'else' code if present.
  // There is no need to emit line number for unconditional branch.
  (void)ApplyDebugLocation::CreateEmpty(CGF);
  CGF.EmitBlock(ElseBlock);
  ElseGen(CGF);
  // There is no need to emit line number for unconditional branch.
  (void)ApplyDebugLocation::CreateEmpty(CGF);
  CGF.EmitBranch(ContBlock);
  // Emit the continuation block for code after the if.
  CGF.EmitBlock(ContBlock, /*IsFinished=*/true);
}

void CGOpenMPRuntime::emitParallelCall(CodeGenFunction &CGF, SourceLocation Loc,
                                       llvm::Value *OutlinedFn,
                                       ArrayRef<llvm::Value *> CapturedVars,
                                       const Expr *IfCond) {
  if (!CGF.HaveInsertPoint())
    return;
  llvm::Value *RTLoc = emitUpdateLocation(CGF, Loc);
  auto &&ThenGen = [OutlinedFn, CapturedVars, RTLoc](CodeGenFunction &CGF,
                                                     PrePostActionTy &) {
    // Build call __kmpc_fork_call(loc, n, microtask, var1, .., varn);
    CGOpenMPRuntime &RT = CGF.CGM.getOpenMPRuntime();
    llvm::Value *Args[] = {
        RTLoc,
        CGF.Builder.getInt32(CapturedVars.size()), // Number of captured vars
        CGF.Builder.CreateBitCast(OutlinedFn, RT.getKmpc_MicroPointerTy())};
    llvm::SmallVector<llvm::Value *, 16> RealArgs;
    RealArgs.append(std::begin(Args), std::end(Args));
    RealArgs.append(CapturedVars.begin(), CapturedVars.end());

    llvm::Value *RTLFn = RT.createRuntimeFunction(OMPRTL__kmpc_fork_call);
    CGF.EmitRuntimeCall(RTLFn, RealArgs);
  };
  auto &&ElseGen = [OutlinedFn, CapturedVars, RTLoc, Loc](CodeGenFunction &CGF,
                                                          PrePostActionTy &) {
    CGOpenMPRuntime &RT = CGF.CGM.getOpenMPRuntime();
    llvm::Value *ThreadID = RT.getThreadID(CGF, Loc);
    // Build calls:
    // __kmpc_serialized_parallel(&Loc, GTid);
    llvm::Value *Args[] = {RTLoc, ThreadID};
    CGF.EmitRuntimeCall(
        RT.createRuntimeFunction(OMPRTL__kmpc_serialized_parallel), Args);

    // OutlinedFn(&GTid, &zero, CapturedStruct);
    Address ZeroAddr = CGF.CreateDefaultAlignTempAlloca(CGF.Int32Ty,
                                                        /*Name*/ ".zero.addr");
    CGF.InitTempAlloca(ZeroAddr, CGF.Builder.getInt32(/*C*/ 0));
    llvm::SmallVector<llvm::Value *, 16> OutlinedFnArgs;
    // ThreadId for serialized parallels is 0.
    OutlinedFnArgs.push_back(ZeroAddr.getPointer());
    OutlinedFnArgs.push_back(ZeroAddr.getPointer());
    OutlinedFnArgs.append(CapturedVars.begin(), CapturedVars.end());
    RT.emitOutlinedFunctionCall(CGF, Loc, OutlinedFn, OutlinedFnArgs);

    // __kmpc_end_serialized_parallel(&Loc, GTid);
    llvm::Value *EndArgs[] = {RT.emitUpdateLocation(CGF, Loc), ThreadID};
    CGF.EmitRuntimeCall(
        RT.createRuntimeFunction(OMPRTL__kmpc_end_serialized_parallel),
        EndArgs);
  };
  if (IfCond) {
    emitOMPIfClause(CGF, IfCond, ThenGen, ElseGen);
  } else {
    RegionCodeGenTy ThenRCG(ThenGen);
    ThenRCG(CGF);
  }
}

// If we're inside an (outlined) parallel region, use the region info's
// thread-ID variable (it is passed in a first argument of the outlined function
// as "kmp_int32 *gtid"). Otherwise, if we're not inside parallel region, but in
// regular serial code region, get thread ID by calling kmp_int32
// kmpc_global_thread_num(ident_t *loc), stash this thread ID in a temporary and
// return the address of that temp.
Address CGOpenMPRuntime::emitThreadIDAddress(CodeGenFunction &CGF,
                                             SourceLocation Loc) {
  if (auto *OMPRegionInfo =
          dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo))
    if (OMPRegionInfo->getThreadIDVariable())
      return OMPRegionInfo->getThreadIDVariableLValue(CGF).getAddress();

  llvm::Value *ThreadID = getThreadID(CGF, Loc);
  QualType Int32Ty =
      CGF.getContext().getIntTypeForBitwidth(/*DestWidth*/ 32, /*Signed*/ true);
  Address ThreadIDTemp = CGF.CreateMemTemp(Int32Ty, /*Name*/ ".threadid_temp.");
  CGF.EmitStoreOfScalar(ThreadID,
                        CGF.MakeAddrLValue(ThreadIDTemp, Int32Ty));

  return ThreadIDTemp;
}

llvm::Constant *
CGOpenMPRuntime::getOrCreateInternalVariable(llvm::Type *Ty,
                                             const llvm::Twine &Name) {
  SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  Out << Name;
  StringRef RuntimeName = Out.str();
  auto &Elem = *InternalVars.try_emplace(RuntimeName, nullptr).first;
  if (Elem.second) {
    assert(Elem.second->getType()->getPointerElementType() == Ty &&
           "OMP internal variable has different type than requested");
    return &*Elem.second;
  }

  return Elem.second = new llvm::GlobalVariable(
             CGM.getModule(), Ty, /*IsConstant*/ false,
             llvm::GlobalValue::CommonLinkage, llvm::Constant::getNullValue(Ty),
             Elem.first());
}

llvm::Value *CGOpenMPRuntime::getCriticalRegionLock(StringRef CriticalName) {
  std::string Prefix = Twine("gomp_critical_user_", CriticalName).str();
  std::string Name = getName({Prefix, "var"});
  return getOrCreateInternalVariable(KmpCriticalNameTy, Name);
}

namespace {
/// Common pre(post)-action for different OpenMP constructs.
class CommonActionTy final : public PrePostActionTy {
  llvm::Value *EnterCallee;
  ArrayRef<llvm::Value *> EnterArgs;
  llvm::Value *ExitCallee;
  ArrayRef<llvm::Value *> ExitArgs;
  bool Conditional;
  llvm::BasicBlock *ContBlock = nullptr;

public:
  CommonActionTy(llvm::Value *EnterCallee, ArrayRef<llvm::Value *> EnterArgs,
                 llvm::Value *ExitCallee, ArrayRef<llvm::Value *> ExitArgs,
                 bool Conditional = false)
      : EnterCallee(EnterCallee), EnterArgs(EnterArgs), ExitCallee(ExitCallee),
        ExitArgs(ExitArgs), Conditional(Conditional) {}
  void Enter(CodeGenFunction &CGF) override {
    llvm::Value *EnterRes = CGF.EmitRuntimeCall(EnterCallee, EnterArgs);
    if (Conditional) {
      llvm::Value *CallBool = CGF.Builder.CreateIsNotNull(EnterRes);
      auto *ThenBlock = CGF.createBasicBlock("omp_if.then");
      ContBlock = CGF.createBasicBlock("omp_if.end");
      // Generate the branch (If-stmt)
      CGF.Builder.CreateCondBr(CallBool, ThenBlock, ContBlock);
      CGF.EmitBlock(ThenBlock);
    }
  }
  void Done(CodeGenFunction &CGF) {
    // Emit the rest of blocks/branches
    CGF.EmitBranch(ContBlock);
    CGF.EmitBlock(ContBlock, true);
  }
  void Exit(CodeGenFunction &CGF) override {
    CGF.EmitRuntimeCall(ExitCallee, ExitArgs);
  }
};
} // anonymous namespace

void CGOpenMPRuntime::emitCriticalRegion(CodeGenFunction &CGF,
                                         StringRef CriticalName,
                                         const RegionCodeGenTy &CriticalOpGen,
                                         SourceLocation Loc, const Expr *Hint) {
  // __kmpc_critical[_with_hint](ident_t *, gtid, Lock[, hint]);
  // CriticalOpGen();
  // __kmpc_end_critical(ident_t *, gtid, Lock);
  // Prepare arguments and build a call to __kmpc_critical
  if (!CGF.HaveInsertPoint())
    return;
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
                         getCriticalRegionLock(CriticalName)};
  llvm::SmallVector<llvm::Value *, 4> EnterArgs(std::begin(Args),
                                                std::end(Args));
  if (Hint) {
    EnterArgs.push_back(CGF.Builder.CreateIntCast(
        CGF.EmitScalarExpr(Hint), CGM.IntPtrTy, /*isSigned=*/false));
  }
  CommonActionTy Action(
      createRuntimeFunction(Hint ? OMPRTL__kmpc_critical_with_hint
                                 : OMPRTL__kmpc_critical),
      EnterArgs, createRuntimeFunction(OMPRTL__kmpc_end_critical), Args);
  CriticalOpGen.setAction(Action);
  emitInlinedDirective(CGF, OMPD_critical, CriticalOpGen);
}

void CGOpenMPRuntime::emitMasterRegion(CodeGenFunction &CGF,
                                       const RegionCodeGenTy &MasterOpGen,
                                       SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  // if(__kmpc_master(ident_t *, gtid)) {
  //   MasterOpGen();
  //   __kmpc_end_master(ident_t *, gtid);
  // }
  // Prepare arguments and build a call to __kmpc_master
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc)};
  CommonActionTy Action(createRuntimeFunction(OMPRTL__kmpc_master), Args,
                        createRuntimeFunction(OMPRTL__kmpc_end_master), Args,
                        /*Conditional=*/true);
  MasterOpGen.setAction(Action);
  emitInlinedDirective(CGF, OMPD_master, MasterOpGen);
  Action.Done(CGF);
}

void CGOpenMPRuntime::emitTaskyieldCall(CodeGenFunction &CGF,
                                        SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  // Build call __kmpc_omp_taskyield(loc, thread_id, 0);
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
      llvm::ConstantInt::get(CGM.IntTy, /*V=*/0, /*isSigned=*/true)};
  CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_omp_taskyield), Args);
  if (auto *Region = dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo))
    Region->emitUntiedSwitch(CGF);
}

void CGOpenMPRuntime::emitTaskgroupRegion(CodeGenFunction &CGF,
                                          const RegionCodeGenTy &TaskgroupOpGen,
                                          SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  // __kmpc_taskgroup(ident_t *, gtid);
  // TaskgroupOpGen();
  // __kmpc_end_taskgroup(ident_t *, gtid);
  // Prepare arguments and build a call to __kmpc_taskgroup
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc)};
  CommonActionTy Action(createRuntimeFunction(OMPRTL__kmpc_taskgroup), Args,
                        createRuntimeFunction(OMPRTL__kmpc_end_taskgroup),
                        Args);
  TaskgroupOpGen.setAction(Action);
  emitInlinedDirective(CGF, OMPD_taskgroup, TaskgroupOpGen);
}

/// Given an array of pointers to variables, project the address of a
/// given variable.
static Address emitAddrOfVarFromArray(CodeGenFunction &CGF, Address Array,
                                      unsigned Index, const VarDecl *Var) {
  // Pull out the pointer to the variable.
  Address PtrAddr =
      CGF.Builder.CreateConstArrayGEP(Array, Index, CGF.getPointerSize());
  llvm::Value *Ptr = CGF.Builder.CreateLoad(PtrAddr);

  Address Addr = Address(Ptr, CGF.getContext().getDeclAlign(Var));
  Addr = CGF.Builder.CreateElementBitCast(
      Addr, CGF.ConvertTypeForMem(Var->getType()));
  return Addr;
}

static llvm::Value *emitCopyprivateCopyFunction(
    CodeGenModule &CGM, llvm::Type *ArgsType,
    ArrayRef<const Expr *> CopyprivateVars, ArrayRef<const Expr *> DestExprs,
    ArrayRef<const Expr *> SrcExprs, ArrayRef<const Expr *> AssignmentOps,
    SourceLocation Loc) {
  ASTContext &C = CGM.getContext();
  // void copy_func(void *LHSArg, void *RHSArg);
  FunctionArgList Args;
  ImplicitParamDecl LHSArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                           ImplicitParamDecl::Other);
  ImplicitParamDecl RHSArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                           ImplicitParamDecl::Other);
  Args.push_back(&LHSArg);
  Args.push_back(&RHSArg);
  const auto &CGFI =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  std::string Name =
      CGM.getOpenMPRuntime().getName({"omp", "copyprivate", "copy_func"});
  auto *Fn = llvm::Function::Create(CGM.getTypes().GetFunctionType(CGFI),
                                    llvm::GlobalValue::InternalLinkage, Name,
                                    &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, CGFI);
  Fn->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, CGFI, Args, Loc, Loc);
  // Dest = (void*[n])(LHSArg);
  // Src = (void*[n])(RHSArg);
  Address LHS(CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(&LHSArg)),
      ArgsType), CGF.getPointerAlign());
  Address RHS(CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(&RHSArg)),
      ArgsType), CGF.getPointerAlign());
  // *(Type0*)Dst[0] = *(Type0*)Src[0];
  // *(Type1*)Dst[1] = *(Type1*)Src[1];
  // ...
  // *(Typen*)Dst[n] = *(Typen*)Src[n];
  for (unsigned I = 0, E = AssignmentOps.size(); I < E; ++I) {
    const auto *DestVar =
        cast<VarDecl>(cast<DeclRefExpr>(DestExprs[I])->getDecl());
    Address DestAddr = emitAddrOfVarFromArray(CGF, LHS, I, DestVar);

    const auto *SrcVar =
        cast<VarDecl>(cast<DeclRefExpr>(SrcExprs[I])->getDecl());
    Address SrcAddr = emitAddrOfVarFromArray(CGF, RHS, I, SrcVar);

    const auto *VD = cast<DeclRefExpr>(CopyprivateVars[I])->getDecl();
    QualType Type = VD->getType();
    CGF.EmitOMPCopy(Type, DestAddr, SrcAddr, DestVar, SrcVar, AssignmentOps[I]);
  }
  CGF.FinishFunction();
  return Fn;
}

void CGOpenMPRuntime::emitSingleRegion(CodeGenFunction &CGF,
                                       const RegionCodeGenTy &SingleOpGen,
                                       SourceLocation Loc,
                                       ArrayRef<const Expr *> CopyprivateVars,
                                       ArrayRef<const Expr *> SrcExprs,
                                       ArrayRef<const Expr *> DstExprs,
                                       ArrayRef<const Expr *> AssignmentOps) {
  if (!CGF.HaveInsertPoint())
    return;
  assert(CopyprivateVars.size() == SrcExprs.size() &&
         CopyprivateVars.size() == DstExprs.size() &&
         CopyprivateVars.size() == AssignmentOps.size());
  ASTContext &C = CGM.getContext();
  // int32 did_it = 0;
  // if(__kmpc_single(ident_t *, gtid)) {
  //   SingleOpGen();
  //   __kmpc_end_single(ident_t *, gtid);
  //   did_it = 1;
  // }
  // call __kmpc_copyprivate(ident_t *, gtid, <buf_size>, <copyprivate list>,
  // <copy_func>, did_it);

  Address DidIt = Address::invalid();
  if (!CopyprivateVars.empty()) {
    // int32 did_it = 0;
    QualType KmpInt32Ty =
        C.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/1);
    DidIt = CGF.CreateMemTemp(KmpInt32Ty, ".omp.copyprivate.did_it");
    CGF.Builder.CreateStore(CGF.Builder.getInt32(0), DidIt);
  }
  // Prepare arguments and build a call to __kmpc_single
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc)};
  CommonActionTy Action(createRuntimeFunction(OMPRTL__kmpc_single), Args,
                        createRuntimeFunction(OMPRTL__kmpc_end_single), Args,
                        /*Conditional=*/true);
  SingleOpGen.setAction(Action);
  emitInlinedDirective(CGF, OMPD_single, SingleOpGen);
  if (DidIt.isValid()) {
    // did_it = 1;
    CGF.Builder.CreateStore(CGF.Builder.getInt32(1), DidIt);
  }
  Action.Done(CGF);
  // call __kmpc_copyprivate(ident_t *, gtid, <buf_size>, <copyprivate list>,
  // <copy_func>, did_it);
  if (DidIt.isValid()) {
    llvm::APInt ArraySize(/*unsigned int numBits=*/32, CopyprivateVars.size());
    QualType CopyprivateArrayTy =
        C.getConstantArrayType(C.VoidPtrTy, ArraySize, ArrayType::Normal,
                               /*IndexTypeQuals=*/0);
    // Create a list of all private variables for copyprivate.
    Address CopyprivateList =
        CGF.CreateMemTemp(CopyprivateArrayTy, ".omp.copyprivate.cpr_list");
    for (unsigned I = 0, E = CopyprivateVars.size(); I < E; ++I) {
      Address Elem = CGF.Builder.CreateConstArrayGEP(
          CopyprivateList, I, CGF.getPointerSize());
      CGF.Builder.CreateStore(
          CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
              CGF.EmitLValue(CopyprivateVars[I]).getPointer(), CGF.VoidPtrTy),
          Elem);
    }
    // Build function that copies private values from single region to all other
    // threads in the corresponding parallel region.
    llvm::Value *CpyFn = emitCopyprivateCopyFunction(
        CGM, CGF.ConvertTypeForMem(CopyprivateArrayTy)->getPointerTo(),
        CopyprivateVars, SrcExprs, DstExprs, AssignmentOps, Loc);
    llvm::Value *BufSize = CGF.getTypeSize(CopyprivateArrayTy);
    Address CL =
      CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(CopyprivateList,
                                                      CGF.VoidPtrTy);
    llvm::Value *DidItVal = CGF.Builder.CreateLoad(DidIt);
    llvm::Value *Args[] = {
        emitUpdateLocation(CGF, Loc), // ident_t *<loc>
        getThreadID(CGF, Loc),        // i32 <gtid>
        BufSize,                      // size_t <buf_size>
        CL.getPointer(),              // void *<copyprivate list>
        CpyFn,                        // void (*) (void *, void *) <copy_func>
        DidItVal                      // i32 did_it
    };
    CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_copyprivate), Args);
  }
}

void CGOpenMPRuntime::emitOrderedRegion(CodeGenFunction &CGF,
                                        const RegionCodeGenTy &OrderedOpGen,
                                        SourceLocation Loc, bool IsThreads) {
  if (!CGF.HaveInsertPoint())
    return;
  // __kmpc_ordered(ident_t *, gtid);
  // OrderedOpGen();
  // __kmpc_end_ordered(ident_t *, gtid);
  // Prepare arguments and build a call to __kmpc_ordered
  if (IsThreads) {
    llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc)};
    CommonActionTy Action(createRuntimeFunction(OMPRTL__kmpc_ordered), Args,
                          createRuntimeFunction(OMPRTL__kmpc_end_ordered),
                          Args);
    OrderedOpGen.setAction(Action);
    emitInlinedDirective(CGF, OMPD_ordered, OrderedOpGen);
    return;
  }
  emitInlinedDirective(CGF, OMPD_ordered, OrderedOpGen);
}

unsigned CGOpenMPRuntime::getDefaultFlagsForBarriers(OpenMPDirectiveKind Kind) {
  unsigned Flags;
  if (Kind == OMPD_for)
    Flags = OMP_IDENT_BARRIER_IMPL_FOR;
  else if (Kind == OMPD_sections)
    Flags = OMP_IDENT_BARRIER_IMPL_SECTIONS;
  else if (Kind == OMPD_single)
    Flags = OMP_IDENT_BARRIER_IMPL_SINGLE;
  else if (Kind == OMPD_barrier)
    Flags = OMP_IDENT_BARRIER_EXPL;
  else
    Flags = OMP_IDENT_BARRIER_IMPL;
  return Flags;
}

void CGOpenMPRuntime::emitBarrierCall(CodeGenFunction &CGF, SourceLocation Loc,
                                      OpenMPDirectiveKind Kind, bool EmitChecks,
                                      bool ForceSimpleCall) {
  if (!CGF.HaveInsertPoint())
    return;
  // Build call __kmpc_cancel_barrier(loc, thread_id);
  // Build call __kmpc_barrier(loc, thread_id);
  unsigned Flags = getDefaultFlagsForBarriers(Kind);
  // Build call __kmpc_cancel_barrier(loc, thread_id) or __kmpc_barrier(loc,
  // thread_id);
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc, Flags),
                         getThreadID(CGF, Loc)};
  if (auto *OMPRegionInfo =
          dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo)) {
    if (!ForceSimpleCall && OMPRegionInfo->hasCancel()) {
      llvm::Value *Result = CGF.EmitRuntimeCall(
          createRuntimeFunction(OMPRTL__kmpc_cancel_barrier), Args);
      if (EmitChecks) {
        // if (__kmpc_cancel_barrier()) {
        //   exit from construct;
        // }
        llvm::BasicBlock *ExitBB = CGF.createBasicBlock(".cancel.exit");
        llvm::BasicBlock *ContBB = CGF.createBasicBlock(".cancel.continue");
        llvm::Value *Cmp = CGF.Builder.CreateIsNotNull(Result);
        CGF.Builder.CreateCondBr(Cmp, ExitBB, ContBB);
        CGF.EmitBlock(ExitBB);
        //   exit from construct;
        CodeGenFunction::JumpDest CancelDestination =
            CGF.getOMPCancelDestination(OMPRegionInfo->getDirectiveKind());
        CGF.EmitBranchThroughCleanup(CancelDestination);
        CGF.EmitBlock(ContBB, /*IsFinished=*/true);
      }
      return;
    }
  }
  CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_barrier), Args);
}

/// Map the OpenMP loop schedule to the runtime enumeration.
static OpenMPSchedType getRuntimeSchedule(OpenMPScheduleClauseKind ScheduleKind,
                                          bool Chunked, bool Ordered) {
  switch (ScheduleKind) {
  case OMPC_SCHEDULE_static:
    return Chunked ? (Ordered ? OMP_ord_static_chunked : OMP_sch_static_chunked)
                   : (Ordered ? OMP_ord_static : OMP_sch_static);
  case OMPC_SCHEDULE_dynamic:
    return Ordered ? OMP_ord_dynamic_chunked : OMP_sch_dynamic_chunked;
  case OMPC_SCHEDULE_guided:
    return Ordered ? OMP_ord_guided_chunked : OMP_sch_guided_chunked;
  case OMPC_SCHEDULE_runtime:
    return Ordered ? OMP_ord_runtime : OMP_sch_runtime;
  case OMPC_SCHEDULE_auto:
    return Ordered ? OMP_ord_auto : OMP_sch_auto;
  case OMPC_SCHEDULE_unknown:
    assert(!Chunked && "chunk was specified but schedule kind not known");
    return Ordered ? OMP_ord_static : OMP_sch_static;
  }
  llvm_unreachable("Unexpected runtime schedule");
}

/// Map the OpenMP distribute schedule to the runtime enumeration.
static OpenMPSchedType
getRuntimeSchedule(OpenMPDistScheduleClauseKind ScheduleKind, bool Chunked) {
  // only static is allowed for dist_schedule
  return Chunked ? OMP_dist_sch_static_chunked : OMP_dist_sch_static;
}

bool CGOpenMPRuntime::isStaticNonchunked(OpenMPScheduleClauseKind ScheduleKind,
                                         bool Chunked) const {
  OpenMPSchedType Schedule =
      getRuntimeSchedule(ScheduleKind, Chunked, /*Ordered=*/false);
  return Schedule == OMP_sch_static;
}

bool CGOpenMPRuntime::isStaticNonchunked(
    OpenMPDistScheduleClauseKind ScheduleKind, bool Chunked) const {
  OpenMPSchedType Schedule = getRuntimeSchedule(ScheduleKind, Chunked);
  return Schedule == OMP_dist_sch_static;
}

bool CGOpenMPRuntime::isStaticChunked(OpenMPScheduleClauseKind ScheduleKind,
                                      bool Chunked) const {
  OpenMPSchedType Schedule =
      getRuntimeSchedule(ScheduleKind, Chunked, /*Ordered=*/false);
  return Schedule == OMP_sch_static_chunked;
}

bool CGOpenMPRuntime::isStaticChunked(
    OpenMPDistScheduleClauseKind ScheduleKind, bool Chunked) const {
  OpenMPSchedType Schedule = getRuntimeSchedule(ScheduleKind, Chunked);
  return Schedule == OMP_dist_sch_static_chunked;
}

bool CGOpenMPRuntime::isDynamic(OpenMPScheduleClauseKind ScheduleKind) const {
  OpenMPSchedType Schedule =
      getRuntimeSchedule(ScheduleKind, /*Chunked=*/false, /*Ordered=*/false);
  assert(Schedule != OMP_sch_static_chunked && "cannot be chunked here");
  return Schedule != OMP_sch_static;
}

static int addMonoNonMonoModifier(OpenMPSchedType Schedule,
                                  OpenMPScheduleClauseModifier M1,
                                  OpenMPScheduleClauseModifier M2) {
  int Modifier = 0;
  switch (M1) {
  case OMPC_SCHEDULE_MODIFIER_monotonic:
    Modifier = OMP_sch_modifier_monotonic;
    break;
  case OMPC_SCHEDULE_MODIFIER_nonmonotonic:
    Modifier = OMP_sch_modifier_nonmonotonic;
    break;
  case OMPC_SCHEDULE_MODIFIER_simd:
    if (Schedule == OMP_sch_static_chunked)
      Schedule = OMP_sch_static_balanced_chunked;
    break;
  case OMPC_SCHEDULE_MODIFIER_last:
  case OMPC_SCHEDULE_MODIFIER_unknown:
    break;
  }
  switch (M2) {
  case OMPC_SCHEDULE_MODIFIER_monotonic:
    Modifier = OMP_sch_modifier_monotonic;
    break;
  case OMPC_SCHEDULE_MODIFIER_nonmonotonic:
    Modifier = OMP_sch_modifier_nonmonotonic;
    break;
  case OMPC_SCHEDULE_MODIFIER_simd:
    if (Schedule == OMP_sch_static_chunked)
      Schedule = OMP_sch_static_balanced_chunked;
    break;
  case OMPC_SCHEDULE_MODIFIER_last:
  case OMPC_SCHEDULE_MODIFIER_unknown:
    break;
  }
  return Schedule | Modifier;
}

void CGOpenMPRuntime::emitForDispatchInit(
    CodeGenFunction &CGF, SourceLocation Loc,
    const OpenMPScheduleTy &ScheduleKind, unsigned IVSize, bool IVSigned,
    bool Ordered, const DispatchRTInput &DispatchValues) {
  if (!CGF.HaveInsertPoint())
    return;
  OpenMPSchedType Schedule = getRuntimeSchedule(
      ScheduleKind.Schedule, DispatchValues.Chunk != nullptr, Ordered);
  assert(Ordered ||
         (Schedule != OMP_sch_static && Schedule != OMP_sch_static_chunked &&
          Schedule != OMP_ord_static && Schedule != OMP_ord_static_chunked &&
          Schedule != OMP_sch_static_balanced_chunked));
  // Call __kmpc_dispatch_init(
  //          ident_t *loc, kmp_int32 tid, kmp_int32 schedule,
  //          kmp_int[32|64] lower, kmp_int[32|64] upper,
  //          kmp_int[32|64] stride, kmp_int[32|64] chunk);

  // If the Chunk was not specified in the clause - use default value 1.
  llvm::Value *Chunk = DispatchValues.Chunk ? DispatchValues.Chunk
                                            : CGF.Builder.getIntN(IVSize, 1);
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
      CGF.Builder.getInt32(addMonoNonMonoModifier(
          Schedule, ScheduleKind.M1, ScheduleKind.M2)), // Schedule type
      DispatchValues.LB,                                // Lower
      DispatchValues.UB,                                // Upper
      CGF.Builder.getIntN(IVSize, 1),                   // Stride
      Chunk                                             // Chunk
  };
  CGF.EmitRuntimeCall(createDispatchInitFunction(IVSize, IVSigned), Args);
}

static void emitForStaticInitCall(
    CodeGenFunction &CGF, llvm::Value *UpdateLocation, llvm::Value *ThreadId,
    llvm::Constant *ForStaticInitFunction, OpenMPSchedType Schedule,
    OpenMPScheduleClauseModifier M1, OpenMPScheduleClauseModifier M2,
    const CGOpenMPRuntime::StaticRTInput &Values) {
  if (!CGF.HaveInsertPoint())
    return;

  assert(!Values.Ordered);
  assert(Schedule == OMP_sch_static || Schedule == OMP_sch_static_chunked ||
         Schedule == OMP_sch_static_balanced_chunked ||
         Schedule == OMP_ord_static || Schedule == OMP_ord_static_chunked ||
         Schedule == OMP_dist_sch_static ||
         Schedule == OMP_dist_sch_static_chunked);

  // Call __kmpc_for_static_init(
  //          ident_t *loc, kmp_int32 tid, kmp_int32 schedtype,
  //          kmp_int32 *p_lastiter, kmp_int[32|64] *p_lower,
  //          kmp_int[32|64] *p_upper, kmp_int[32|64] *p_stride,
  //          kmp_int[32|64] incr, kmp_int[32|64] chunk);
  llvm::Value *Chunk = Values.Chunk;
  if (Chunk == nullptr) {
    assert((Schedule == OMP_sch_static || Schedule == OMP_ord_static ||
            Schedule == OMP_dist_sch_static) &&
           "expected static non-chunked schedule");
    // If the Chunk was not specified in the clause - use default value 1.
    Chunk = CGF.Builder.getIntN(Values.IVSize, 1);
  } else {
    assert((Schedule == OMP_sch_static_chunked ||
            Schedule == OMP_sch_static_balanced_chunked ||
            Schedule == OMP_ord_static_chunked ||
            Schedule == OMP_dist_sch_static_chunked) &&
           "expected static chunked schedule");
  }
  llvm::Value *Args[] = {
      UpdateLocation,
      ThreadId,
      CGF.Builder.getInt32(addMonoNonMonoModifier(Schedule, M1,
                                                  M2)), // Schedule type
      Values.IL.getPointer(),                           // &isLastIter
      Values.LB.getPointer(),                           // &LB
      Values.UB.getPointer(),                           // &UB
      Values.ST.getPointer(),                           // &Stride
      CGF.Builder.getIntN(Values.IVSize, 1),            // Incr
      Chunk                                             // Chunk
  };
  CGF.EmitRuntimeCall(ForStaticInitFunction, Args);
}

void CGOpenMPRuntime::emitForStaticInit(CodeGenFunction &CGF,
                                        SourceLocation Loc,
                                        OpenMPDirectiveKind DKind,
                                        const OpenMPScheduleTy &ScheduleKind,
                                        const StaticRTInput &Values) {
  OpenMPSchedType ScheduleNum = getRuntimeSchedule(
      ScheduleKind.Schedule, Values.Chunk != nullptr, Values.Ordered);
  assert(isOpenMPWorksharingDirective(DKind) &&
         "Expected loop-based or sections-based directive.");
  llvm::Value *UpdatedLocation = emitUpdateLocation(CGF, Loc,
                                             isOpenMPLoopDirective(DKind)
                                                 ? OMP_IDENT_WORK_LOOP
                                                 : OMP_IDENT_WORK_SECTIONS);
  llvm::Value *ThreadId = getThreadID(CGF, Loc);
  llvm::Constant *StaticInitFunction =
      createForStaticInitFunction(Values.IVSize, Values.IVSigned);
  emitForStaticInitCall(CGF, UpdatedLocation, ThreadId, StaticInitFunction,
                        ScheduleNum, ScheduleKind.M1, ScheduleKind.M2, Values);
}

void CGOpenMPRuntime::emitDistributeStaticInit(
    CodeGenFunction &CGF, SourceLocation Loc,
    OpenMPDistScheduleClauseKind SchedKind,
    const CGOpenMPRuntime::StaticRTInput &Values) {
  OpenMPSchedType ScheduleNum =
      getRuntimeSchedule(SchedKind, Values.Chunk != nullptr);
  llvm::Value *UpdatedLocation =
      emitUpdateLocation(CGF, Loc, OMP_IDENT_WORK_DISTRIBUTE);
  llvm::Value *ThreadId = getThreadID(CGF, Loc);
  llvm::Constant *StaticInitFunction =
      createForStaticInitFunction(Values.IVSize, Values.IVSigned);
  emitForStaticInitCall(CGF, UpdatedLocation, ThreadId, StaticInitFunction,
                        ScheduleNum, OMPC_SCHEDULE_MODIFIER_unknown,
                        OMPC_SCHEDULE_MODIFIER_unknown, Values);
}

void CGOpenMPRuntime::emitForStaticFinish(CodeGenFunction &CGF,
                                          SourceLocation Loc,
                                          OpenMPDirectiveKind DKind) {
  if (!CGF.HaveInsertPoint())
    return;
  // Call __kmpc_for_static_fini(ident_t *loc, kmp_int32 tid);
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc,
                         isOpenMPDistributeDirective(DKind)
                             ? OMP_IDENT_WORK_DISTRIBUTE
                             : isOpenMPLoopDirective(DKind)
                                   ? OMP_IDENT_WORK_LOOP
                                   : OMP_IDENT_WORK_SECTIONS),
      getThreadID(CGF, Loc)};
  CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_for_static_fini),
                      Args);
}

void CGOpenMPRuntime::emitForOrderedIterationEnd(CodeGenFunction &CGF,
                                                 SourceLocation Loc,
                                                 unsigned IVSize,
                                                 bool IVSigned) {
  if (!CGF.HaveInsertPoint())
    return;
  // Call __kmpc_for_dynamic_fini_(4|8)[u](ident_t *loc, kmp_int32 tid);
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc)};
  CGF.EmitRuntimeCall(createDispatchFiniFunction(IVSize, IVSigned), Args);
}

llvm::Value *CGOpenMPRuntime::emitForNext(CodeGenFunction &CGF,
                                          SourceLocation Loc, unsigned IVSize,
                                          bool IVSigned, Address IL,
                                          Address LB, Address UB,
                                          Address ST) {
  // Call __kmpc_dispatch_next(
  //          ident_t *loc, kmp_int32 tid, kmp_int32 *p_lastiter,
  //          kmp_int[32|64] *p_lower, kmp_int[32|64] *p_upper,
  //          kmp_int[32|64] *p_stride);
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc),
      getThreadID(CGF, Loc),
      IL.getPointer(), // &isLastIter
      LB.getPointer(), // &Lower
      UB.getPointer(), // &Upper
      ST.getPointer()  // &Stride
  };
  llvm::Value *Call =
      CGF.EmitRuntimeCall(createDispatchNextFunction(IVSize, IVSigned), Args);
  return CGF.EmitScalarConversion(
      Call, CGF.getContext().getIntTypeForBitwidth(32, /*Signed=*/1),
      CGF.getContext().BoolTy, Loc);
}

void CGOpenMPRuntime::emitNumThreadsClause(CodeGenFunction &CGF,
                                           llvm::Value *NumThreads,
                                           SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  // Build call __kmpc_push_num_threads(&loc, global_tid, num_threads)
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
      CGF.Builder.CreateIntCast(NumThreads, CGF.Int32Ty, /*isSigned*/ true)};
  CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_push_num_threads),
                      Args);
}

void CGOpenMPRuntime::emitProcBindClause(CodeGenFunction &CGF,
                                         OpenMPProcBindClauseKind ProcBind,
                                         SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  // Constants for proc bind value accepted by the runtime.
  enum ProcBindTy {
    ProcBindFalse = 0,
    ProcBindTrue,
    ProcBindMaster,
    ProcBindClose,
    ProcBindSpread,
    ProcBindIntel,
    ProcBindDefault
  } RuntimeProcBind;
  switch (ProcBind) {
  case OMPC_PROC_BIND_master:
    RuntimeProcBind = ProcBindMaster;
    break;
  case OMPC_PROC_BIND_close:
    RuntimeProcBind = ProcBindClose;
    break;
  case OMPC_PROC_BIND_spread:
    RuntimeProcBind = ProcBindSpread;
    break;
  case OMPC_PROC_BIND_unknown:
    llvm_unreachable("Unsupported proc_bind value.");
  }
  // Build call __kmpc_push_proc_bind(&loc, global_tid, proc_bind)
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
      llvm::ConstantInt::get(CGM.IntTy, RuntimeProcBind, /*isSigned=*/true)};
  CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_push_proc_bind), Args);
}

void CGOpenMPRuntime::emitFlush(CodeGenFunction &CGF, ArrayRef<const Expr *>,
                                SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  // Build call void __kmpc_flush(ident_t *loc)
  CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_flush),
                      emitUpdateLocation(CGF, Loc));
}

namespace {
/// Indexes of fields for type kmp_task_t.
enum KmpTaskTFields {
  /// List of shared variables.
  KmpTaskTShareds,
  /// Task routine.
  KmpTaskTRoutine,
  /// Partition id for the untied tasks.
  KmpTaskTPartId,
  /// Function with call of destructors for private variables.
  Data1,
  /// Task priority.
  Data2,
  /// (Taskloops only) Lower bound.
  KmpTaskTLowerBound,
  /// (Taskloops only) Upper bound.
  KmpTaskTUpperBound,
  /// (Taskloops only) Stride.
  KmpTaskTStride,
  /// (Taskloops only) Is last iteration flag.
  KmpTaskTLastIter,
  /// (Taskloops only) Reduction data.
  KmpTaskTReductions,
};
} // anonymous namespace

bool CGOpenMPRuntime::OffloadEntriesInfoManagerTy::empty() const {
  return OffloadEntriesTargetRegion.empty() &&
         OffloadEntriesDeviceGlobalVar.empty();
}

/// Initialize target region entry.
void CGOpenMPRuntime::OffloadEntriesInfoManagerTy::
    initializeTargetRegionEntryInfo(unsigned DeviceID, unsigned FileID,
                                    StringRef ParentName, unsigned LineNum,
                                    unsigned Order) {
  assert(CGM.getLangOpts().OpenMPIsDevice && "Initialization of entries is "
                                             "only required for the device "
                                             "code generation.");
  OffloadEntriesTargetRegion[DeviceID][FileID][ParentName][LineNum] =
      OffloadEntryInfoTargetRegion(Order, /*Addr=*/nullptr, /*ID=*/nullptr,
                                   OMPTargetRegionEntryTargetRegion);
  ++OffloadingEntriesNum;
}

void CGOpenMPRuntime::OffloadEntriesInfoManagerTy::
    registerTargetRegionEntryInfo(unsigned DeviceID, unsigned FileID,
                                  StringRef ParentName, unsigned LineNum,
                                  llvm::Constant *Addr, llvm::Constant *ID,
                                  OMPTargetRegionEntryKind Flags) {
  // If we are emitting code for a target, the entry is already initialized,
  // only has to be registered.
  if (CGM.getLangOpts().OpenMPIsDevice) {
    if (!hasTargetRegionEntryInfo(DeviceID, FileID, ParentName, LineNum)) {
      unsigned DiagID = CGM.getDiags().getCustomDiagID(
          DiagnosticsEngine::Error,
          "Unable to find target region on line '%0' in the device code.");
      CGM.getDiags().Report(DiagID) << LineNum;
      return;
    }
    auto &Entry =
        OffloadEntriesTargetRegion[DeviceID][FileID][ParentName][LineNum];
    assert(Entry.isValid() && "Entry not initialized!");
    Entry.setAddress(Addr);
    Entry.setID(ID);
    Entry.setFlags(Flags);
  } else {
    OffloadEntryInfoTargetRegion Entry(OffloadingEntriesNum, Addr, ID, Flags);
    OffloadEntriesTargetRegion[DeviceID][FileID][ParentName][LineNum] = Entry;
    ++OffloadingEntriesNum;
  }
}

bool CGOpenMPRuntime::OffloadEntriesInfoManagerTy::hasTargetRegionEntryInfo(
    unsigned DeviceID, unsigned FileID, StringRef ParentName,
    unsigned LineNum) const {
  auto PerDevice = OffloadEntriesTargetRegion.find(DeviceID);
  if (PerDevice == OffloadEntriesTargetRegion.end())
    return false;
  auto PerFile = PerDevice->second.find(FileID);
  if (PerFile == PerDevice->second.end())
    return false;
  auto PerParentName = PerFile->second.find(ParentName);
  if (PerParentName == PerFile->second.end())
    return false;
  auto PerLine = PerParentName->second.find(LineNum);
  if (PerLine == PerParentName->second.end())
    return false;
  // Fail if this entry is already registered.
  if (PerLine->second.getAddress() || PerLine->second.getID())
    return false;
  return true;
}

void CGOpenMPRuntime::OffloadEntriesInfoManagerTy::actOnTargetRegionEntriesInfo(
    const OffloadTargetRegionEntryInfoActTy &Action) {
  // Scan all target region entries and perform the provided action.
  for (const auto &D : OffloadEntriesTargetRegion)
    for (const auto &F : D.second)
      for (const auto &P : F.second)
        for (const auto &L : P.second)
          Action(D.first, F.first, P.first(), L.first, L.second);
}

void CGOpenMPRuntime::OffloadEntriesInfoManagerTy::
    initializeDeviceGlobalVarEntryInfo(StringRef Name,
                                       OMPTargetGlobalVarEntryKind Flags,
                                       unsigned Order) {
  assert(CGM.getLangOpts().OpenMPIsDevice && "Initialization of entries is "
                                             "only required for the device "
                                             "code generation.");
  OffloadEntriesDeviceGlobalVar.try_emplace(Name, Order, Flags);
  ++OffloadingEntriesNum;
}

void CGOpenMPRuntime::OffloadEntriesInfoManagerTy::
    registerDeviceGlobalVarEntryInfo(StringRef VarName, llvm::Constant *Addr,
                                     CharUnits VarSize,
                                     OMPTargetGlobalVarEntryKind Flags,
                                     llvm::GlobalValue::LinkageTypes Linkage) {
  if (CGM.getLangOpts().OpenMPIsDevice) {
    auto &Entry = OffloadEntriesDeviceGlobalVar[VarName];
    assert(Entry.isValid() && Entry.getFlags() == Flags &&
           "Entry not initialized!");
    assert((!Entry.getAddress() || Entry.getAddress() == Addr) &&
           "Resetting with the new address.");
    if (Entry.getAddress() && hasDeviceGlobalVarEntryInfo(VarName))
      return;
    Entry.setAddress(Addr);
    Entry.setVarSize(VarSize);
    Entry.setLinkage(Linkage);
  } else {
    if (hasDeviceGlobalVarEntryInfo(VarName))
      return;
    OffloadEntriesDeviceGlobalVar.try_emplace(
        VarName, OffloadingEntriesNum, Addr, VarSize, Flags, Linkage);
    ++OffloadingEntriesNum;
  }
}

void CGOpenMPRuntime::OffloadEntriesInfoManagerTy::
    actOnDeviceGlobalVarEntriesInfo(
        const OffloadDeviceGlobalVarEntryInfoActTy &Action) {
  // Scan all target region entries and perform the provided action.
  for (const auto &E : OffloadEntriesDeviceGlobalVar)
    Action(E.getKey(), E.getValue());
}

llvm::Function *
CGOpenMPRuntime::createOffloadingBinaryDescriptorRegistration() {
  // If we don't have entries or if we are emitting code for the device, we
  // don't need to do anything.
  if (CGM.getLangOpts().OpenMPIsDevice || OffloadEntriesInfoManager.empty())
    return nullptr;

  llvm::Module &M = CGM.getModule();
  ASTContext &C = CGM.getContext();

  // Get list of devices we care about
  const std::vector<llvm::Triple> &Devices = CGM.getLangOpts().OMPTargetTriples;

  // We should be creating an offloading descriptor only if there are devices
  // specified.
  assert(!Devices.empty() && "No OpenMP offloading devices??");

  // Create the external variables that will point to the begin and end of the
  // host entries section. These will be defined by the linker.
  llvm::Type *OffloadEntryTy =
      CGM.getTypes().ConvertTypeForMem(getTgtOffloadEntryQTy());
  std::string EntriesBeginName = getName({"omp_offloading", "entries_begin"});
  auto *HostEntriesBegin = new llvm::GlobalVariable(
      M, OffloadEntryTy, /*isConstant=*/true,
      llvm::GlobalValue::ExternalLinkage, /*Initializer=*/nullptr,
      EntriesBeginName);
  std::string EntriesEndName = getName({"omp_offloading", "entries_end"});
  auto *HostEntriesEnd =
      new llvm::GlobalVariable(M, OffloadEntryTy, /*isConstant=*/true,
                               llvm::GlobalValue::ExternalLinkage,
                               /*Initializer=*/nullptr, EntriesEndName);

  // Create all device images
  auto *DeviceImageTy = cast<llvm::StructType>(
      CGM.getTypes().ConvertTypeForMem(getTgtDeviceImageQTy()));
  ConstantInitBuilder DeviceImagesBuilder(CGM);
  ConstantArrayBuilder DeviceImagesEntries =
      DeviceImagesBuilder.beginArray(DeviceImageTy);

  for (const llvm::Triple &Device : Devices) {
    StringRef T = Device.getTriple();
    std::string BeginName = getName({"omp_offloading", "img_start", ""});
    auto *ImgBegin = new llvm::GlobalVariable(
        M, CGM.Int8Ty, /*isConstant=*/true,
        llvm::GlobalValue::ExternalWeakLinkage,
        /*Initializer=*/nullptr, Twine(BeginName).concat(T));
    std::string EndName = getName({"omp_offloading", "img_end", ""});
    auto *ImgEnd = new llvm::GlobalVariable(
        M, CGM.Int8Ty, /*isConstant=*/true,
        llvm::GlobalValue::ExternalWeakLinkage,
        /*Initializer=*/nullptr, Twine(EndName).concat(T));

    llvm::Constant *Data[] = {ImgBegin, ImgEnd, HostEntriesBegin,
                              HostEntriesEnd};
    createConstantGlobalStructAndAddToParent(CGM, getTgtDeviceImageQTy(), Data,
                                             DeviceImagesEntries);
  }

  // Create device images global array.
  std::string ImagesName = getName({"omp_offloading", "device_images"});
  llvm::GlobalVariable *DeviceImages =
      DeviceImagesEntries.finishAndCreateGlobal(ImagesName,
                                                CGM.getPointerAlign(),
                                                /*isConstant=*/true);
  DeviceImages->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  // This is a Zero array to be used in the creation of the constant expressions
  llvm::Constant *Index[] = {llvm::Constant::getNullValue(CGM.Int32Ty),
                             llvm::Constant::getNullValue(CGM.Int32Ty)};

  // Create the target region descriptor.
  llvm::Constant *Data[] = {
      llvm::ConstantInt::get(CGM.Int32Ty, Devices.size()),
      llvm::ConstantExpr::getGetElementPtr(DeviceImages->getValueType(),
                                           DeviceImages, Index),
      HostEntriesBegin, HostEntriesEnd};
  std::string Descriptor = getName({"omp_offloading", "descriptor"});
  llvm::GlobalVariable *Desc = createGlobalStruct(
      CGM, getTgtBinaryDescriptorQTy(), /*IsConstant=*/true, Data, Descriptor);

  // Emit code to register or unregister the descriptor at execution
  // startup or closing, respectively.

  llvm::Function *UnRegFn;
  {
    FunctionArgList Args;
    ImplicitParamDecl DummyPtr(C, C.VoidPtrTy, ImplicitParamDecl::Other);
    Args.push_back(&DummyPtr);

    CodeGenFunction CGF(CGM);
    // Disable debug info for global (de-)initializer because they are not part
    // of some particular construct.
    CGF.disableDebugInfo();
    const auto &FI =
        CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
    llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(FI);
    std::string UnregName = getName({"omp_offloading", "descriptor_unreg"});
    UnRegFn = CGM.CreateGlobalInitOrDestructFunction(FTy, UnregName, FI);
    CGF.StartFunction(GlobalDecl(), C.VoidTy, UnRegFn, FI, Args);
    CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__tgt_unregister_lib),
                        Desc);
    CGF.FinishFunction();
  }
  llvm::Function *RegFn;
  {
    CodeGenFunction CGF(CGM);
    // Disable debug info for global (de-)initializer because they are not part
    // of some particular construct.
    CGF.disableDebugInfo();
    const auto &FI = CGM.getTypes().arrangeNullaryFunction();
    llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(FI);

    // Encode offload target triples into the registration function name. It
    // will serve as a comdat key for the registration/unregistration code for
    // this particular combination of offloading targets.
    SmallVector<StringRef, 4U> RegFnNameParts(Devices.size() + 2U);
    RegFnNameParts[0] = "omp_offloading";
    RegFnNameParts[1] = "descriptor_reg";
    llvm::transform(Devices, std::next(RegFnNameParts.begin(), 2),
                    [](const llvm::Triple &T) -> const std::string& {
                      return T.getTriple();
                    });
    llvm::sort(std::next(RegFnNameParts.begin(), 2), RegFnNameParts.end());
    std::string Descriptor = getName(RegFnNameParts);
    RegFn = CGM.CreateGlobalInitOrDestructFunction(FTy, Descriptor, FI);
    CGF.StartFunction(GlobalDecl(), C.VoidTy, RegFn, FI, FunctionArgList());
    CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__tgt_register_lib), Desc);
    // Create a variable to drive the registration and unregistration of the
    // descriptor, so we can reuse the logic that emits Ctors and Dtors.
    ImplicitParamDecl RegUnregVar(C, C.getTranslationUnitDecl(),
                                  SourceLocation(), nullptr, C.CharTy,
                                  ImplicitParamDecl::Other);
    CGM.getCXXABI().registerGlobalDtor(CGF, RegUnregVar, UnRegFn, Desc);
    CGF.FinishFunction();
  }
  if (CGM.supportsCOMDAT()) {
    // It is sufficient to call registration function only once, so create a
    // COMDAT group for registration/unregistration functions and associated
    // data. That would reduce startup time and code size. Registration
    // function serves as a COMDAT group key.
    llvm::Comdat *ComdatKey = M.getOrInsertComdat(RegFn->getName());
    RegFn->setLinkage(llvm::GlobalValue::LinkOnceAnyLinkage);
    RegFn->setVisibility(llvm::GlobalValue::HiddenVisibility);
    RegFn->setComdat(ComdatKey);
    UnRegFn->setComdat(ComdatKey);
    DeviceImages->setComdat(ComdatKey);
    Desc->setComdat(ComdatKey);
  }
  return RegFn;
}

void CGOpenMPRuntime::createOffloadEntry(
    llvm::Constant *ID, llvm::Constant *Addr, uint64_t Size, int32_t Flags,
    llvm::GlobalValue::LinkageTypes Linkage) {
  StringRef Name = Addr->getName();
  llvm::Module &M = CGM.getModule();
  llvm::LLVMContext &C = M.getContext();

  // Create constant string with the name.
  llvm::Constant *StrPtrInit = llvm::ConstantDataArray::getString(C, Name);

  std::string StringName = getName({"omp_offloading", "entry_name"});
  auto *Str = new llvm::GlobalVariable(
      M, StrPtrInit->getType(), /*isConstant=*/true,
      llvm::GlobalValue::InternalLinkage, StrPtrInit, StringName);
  Str->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  llvm::Constant *Data[] = {llvm::ConstantExpr::getBitCast(ID, CGM.VoidPtrTy),
                            llvm::ConstantExpr::getBitCast(Str, CGM.Int8PtrTy),
                            llvm::ConstantInt::get(CGM.SizeTy, Size),
                            llvm::ConstantInt::get(CGM.Int32Ty, Flags),
                            llvm::ConstantInt::get(CGM.Int32Ty, 0)};
  std::string EntryName = getName({"omp_offloading", "entry", ""});
  llvm::GlobalVariable *Entry = createGlobalStruct(
      CGM, getTgtOffloadEntryQTy(), /*IsConstant=*/true, Data,
      Twine(EntryName).concat(Name), llvm::GlobalValue::WeakAnyLinkage);

  // The entry has to be created in the section the linker expects it to be.
  std::string Section = getName({"omp_offloading", "entries"});
  Entry->setSection(Section);
}

void CGOpenMPRuntime::createOffloadEntriesAndInfoMetadata() {
  // Emit the offloading entries and metadata so that the device codegen side
  // can easily figure out what to emit. The produced metadata looks like
  // this:
  //
  // !omp_offload.info = !{!1, ...}
  //
  // Right now we only generate metadata for function that contain target
  // regions.

  // If we do not have entries, we don't need to do anything.
  if (OffloadEntriesInfoManager.empty())
    return;

  llvm::Module &M = CGM.getModule();
  llvm::LLVMContext &C = M.getContext();
  SmallVector<const OffloadEntriesInfoManagerTy::OffloadEntryInfo *, 16>
      OrderedEntries(OffloadEntriesInfoManager.size());
  llvm::SmallVector<StringRef, 16> ParentFunctions(
      OffloadEntriesInfoManager.size());

  // Auxiliary methods to create metadata values and strings.
  auto &&GetMDInt = [this](unsigned V) {
    return llvm::ConstantAsMetadata::get(
        llvm::ConstantInt::get(CGM.Int32Ty, V));
  };

  auto &&GetMDString = [&C](StringRef V) { return llvm::MDString::get(C, V); };

  // Create the offloading info metadata node.
  llvm::NamedMDNode *MD = M.getOrInsertNamedMetadata("omp_offload.info");

  // Create function that emits metadata for each target region entry;
  auto &&TargetRegionMetadataEmitter =
      [&C, MD, &OrderedEntries, &ParentFunctions, &GetMDInt, &GetMDString](
          unsigned DeviceID, unsigned FileID, StringRef ParentName,
          unsigned Line,
          const OffloadEntriesInfoManagerTy::OffloadEntryInfoTargetRegion &E) {
        // Generate metadata for target regions. Each entry of this metadata
        // contains:
        // - Entry 0 -> Kind of this type of metadata (0).
        // - Entry 1 -> Device ID of the file where the entry was identified.
        // - Entry 2 -> File ID of the file where the entry was identified.
        // - Entry 3 -> Mangled name of the function where the entry was
        // identified.
        // - Entry 4 -> Line in the file where the entry was identified.
        // - Entry 5 -> Order the entry was created.
        // The first element of the metadata node is the kind.
        llvm::Metadata *Ops[] = {GetMDInt(E.getKind()), GetMDInt(DeviceID),
                                 GetMDInt(FileID),      GetMDString(ParentName),
                                 GetMDInt(Line),        GetMDInt(E.getOrder())};

        // Save this entry in the right position of the ordered entries array.
        OrderedEntries[E.getOrder()] = &E;
        ParentFunctions[E.getOrder()] = ParentName;

        // Add metadata to the named metadata node.
        MD->addOperand(llvm::MDNode::get(C, Ops));
      };

  OffloadEntriesInfoManager.actOnTargetRegionEntriesInfo(
      TargetRegionMetadataEmitter);

  // Create function that emits metadata for each device global variable entry;
  auto &&DeviceGlobalVarMetadataEmitter =
      [&C, &OrderedEntries, &GetMDInt, &GetMDString,
       MD](StringRef MangledName,
           const OffloadEntriesInfoManagerTy::OffloadEntryInfoDeviceGlobalVar
               &E) {
        // Generate metadata for global variables. Each entry of this metadata
        // contains:
        // - Entry 0 -> Kind of this type of metadata (1).
        // - Entry 1 -> Mangled name of the variable.
        // - Entry 2 -> Declare target kind.
        // - Entry 3 -> Order the entry was created.
        // The first element of the metadata node is the kind.
        llvm::Metadata *Ops[] = {
            GetMDInt(E.getKind()), GetMDString(MangledName),
            GetMDInt(E.getFlags()), GetMDInt(E.getOrder())};

        // Save this entry in the right position of the ordered entries array.
        OrderedEntries[E.getOrder()] = &E;

        // Add metadata to the named metadata node.
        MD->addOperand(llvm::MDNode::get(C, Ops));
      };

  OffloadEntriesInfoManager.actOnDeviceGlobalVarEntriesInfo(
      DeviceGlobalVarMetadataEmitter);

  for (const auto *E : OrderedEntries) {
    assert(E && "All ordered entries must exist!");
    if (const auto *CE =
            dyn_cast<OffloadEntriesInfoManagerTy::OffloadEntryInfoTargetRegion>(
                E)) {
      if (!CE->getID() || !CE->getAddress()) {
        // Do not blame the entry if the parent funtion is not emitted.
        StringRef FnName = ParentFunctions[CE->getOrder()];
        if (!CGM.GetGlobalValue(FnName))
          continue;
        unsigned DiagID = CGM.getDiags().getCustomDiagID(
            DiagnosticsEngine::Error,
            "Offloading entry for target region is incorrect: either the "
            "address or the ID is invalid.");
        CGM.getDiags().Report(DiagID);
        continue;
      }
      createOffloadEntry(CE->getID(), CE->getAddress(), /*Size=*/0,
                         CE->getFlags(), llvm::GlobalValue::WeakAnyLinkage);
    } else if (const auto *CE =
                   dyn_cast<OffloadEntriesInfoManagerTy::
                                OffloadEntryInfoDeviceGlobalVar>(E)) {
      OffloadEntriesInfoManagerTy::OMPTargetGlobalVarEntryKind Flags =
          static_cast<OffloadEntriesInfoManagerTy::OMPTargetGlobalVarEntryKind>(
              CE->getFlags());
      switch (Flags) {
      case OffloadEntriesInfoManagerTy::OMPTargetGlobalVarEntryTo: {
        if (!CE->getAddress()) {
          unsigned DiagID = CGM.getDiags().getCustomDiagID(
              DiagnosticsEngine::Error,
              "Offloading entry for declare target variable is incorrect: the "
              "address is invalid.");
          CGM.getDiags().Report(DiagID);
          continue;
        }
        // The vaiable has no definition - no need to add the entry.
        if (CE->getVarSize().isZero())
          continue;
        break;
      }
      case OffloadEntriesInfoManagerTy::OMPTargetGlobalVarEntryLink:
        assert(((CGM.getLangOpts().OpenMPIsDevice && !CE->getAddress()) ||
                (!CGM.getLangOpts().OpenMPIsDevice && CE->getAddress())) &&
               "Declaret target link address is set.");
        if (CGM.getLangOpts().OpenMPIsDevice)
          continue;
        if (!CE->getAddress()) {
          unsigned DiagID = CGM.getDiags().getCustomDiagID(
              DiagnosticsEngine::Error,
              "Offloading entry for declare target variable is incorrect: the "
              "address is invalid.");
          CGM.getDiags().Report(DiagID);
          continue;
        }
        break;
      }
      createOffloadEntry(CE->getAddress(), CE->getAddress(),
                         CE->getVarSize().getQuantity(), Flags,
                         CE->getLinkage());
    } else {
      llvm_unreachable("Unsupported entry kind.");
    }
  }
}

/// Loads all the offload entries information from the host IR
/// metadata.
void CGOpenMPRuntime::loadOffloadInfoMetadata() {
  // If we are in target mode, load the metadata from the host IR. This code has
  // to match the metadaata creation in createOffloadEntriesAndInfoMetadata().

  if (!CGM.getLangOpts().OpenMPIsDevice)
    return;

  if (CGM.getLangOpts().OMPHostIRFile.empty())
    return;

  auto Buf = llvm::MemoryBuffer::getFile(CGM.getLangOpts().OMPHostIRFile);
  if (auto EC = Buf.getError()) {
    CGM.getDiags().Report(diag::err_cannot_open_file)
        << CGM.getLangOpts().OMPHostIRFile << EC.message();
    return;
  }

  llvm::LLVMContext C;
  auto ME = expectedToErrorOrAndEmitErrors(
      C, llvm::parseBitcodeFile(Buf.get()->getMemBufferRef(), C));

  if (auto EC = ME.getError()) {
    unsigned DiagID = CGM.getDiags().getCustomDiagID(
        DiagnosticsEngine::Error, "Unable to parse host IR file '%0':'%1'");
    CGM.getDiags().Report(DiagID)
        << CGM.getLangOpts().OMPHostIRFile << EC.message();
    return;
  }

  llvm::NamedMDNode *MD = ME.get()->getNamedMetadata("omp_offload.info");
  if (!MD)
    return;

  for (llvm::MDNode *MN : MD->operands()) {
    auto &&GetMDInt = [MN](unsigned Idx) {
      auto *V = cast<llvm::ConstantAsMetadata>(MN->getOperand(Idx));
      return cast<llvm::ConstantInt>(V->getValue())->getZExtValue();
    };

    auto &&GetMDString = [MN](unsigned Idx) {
      auto *V = cast<llvm::MDString>(MN->getOperand(Idx));
      return V->getString();
    };

    switch (GetMDInt(0)) {
    default:
      llvm_unreachable("Unexpected metadata!");
      break;
    case OffloadEntriesInfoManagerTy::OffloadEntryInfo::
        OffloadingEntryInfoTargetRegion:
      OffloadEntriesInfoManager.initializeTargetRegionEntryInfo(
          /*DeviceID=*/GetMDInt(1), /*FileID=*/GetMDInt(2),
          /*ParentName=*/GetMDString(3), /*Line=*/GetMDInt(4),
          /*Order=*/GetMDInt(5));
      break;
    case OffloadEntriesInfoManagerTy::OffloadEntryInfo::
        OffloadingEntryInfoDeviceGlobalVar:
      OffloadEntriesInfoManager.initializeDeviceGlobalVarEntryInfo(
          /*MangledName=*/GetMDString(1),
          static_cast<OffloadEntriesInfoManagerTy::OMPTargetGlobalVarEntryKind>(
              /*Flags=*/GetMDInt(2)),
          /*Order=*/GetMDInt(3));
      break;
    }
  }
}

void CGOpenMPRuntime::emitKmpRoutineEntryT(QualType KmpInt32Ty) {
  if (!KmpRoutineEntryPtrTy) {
    // Build typedef kmp_int32 (* kmp_routine_entry_t)(kmp_int32, void *); type.
    ASTContext &C = CGM.getContext();
    QualType KmpRoutineEntryTyArgs[] = {KmpInt32Ty, C.VoidPtrTy};
    FunctionProtoType::ExtProtoInfo EPI;
    KmpRoutineEntryPtrQTy = C.getPointerType(
        C.getFunctionType(KmpInt32Ty, KmpRoutineEntryTyArgs, EPI));
    KmpRoutineEntryPtrTy = CGM.getTypes().ConvertType(KmpRoutineEntryPtrQTy);
  }
}

QualType CGOpenMPRuntime::getTgtOffloadEntryQTy() {
  // Make sure the type of the entry is already created. This is the type we
  // have to create:
  // struct __tgt_offload_entry{
  //   void      *addr;       // Pointer to the offload entry info.
  //                          // (function or global)
  //   char      *name;       // Name of the function or global.
  //   size_t     size;       // Size of the entry info (0 if it a function).
  //   int32_t    flags;      // Flags associated with the entry, e.g. 'link'.
  //   int32_t    reserved;   // Reserved, to use by the runtime library.
  // };
  if (TgtOffloadEntryQTy.isNull()) {
    ASTContext &C = CGM.getContext();
    RecordDecl *RD = C.buildImplicitRecord("__tgt_offload_entry");
    RD->startDefinition();
    addFieldToRecordDecl(C, RD, C.VoidPtrTy);
    addFieldToRecordDecl(C, RD, C.getPointerType(C.CharTy));
    addFieldToRecordDecl(C, RD, C.getSizeType());
    addFieldToRecordDecl(
        C, RD, C.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/true));
    addFieldToRecordDecl(
        C, RD, C.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/true));
    RD->completeDefinition();
    RD->addAttr(PackedAttr::CreateImplicit(C));
    TgtOffloadEntryQTy = C.getRecordType(RD);
  }
  return TgtOffloadEntryQTy;
}

QualType CGOpenMPRuntime::getTgtDeviceImageQTy() {
  // These are the types we need to build:
  // struct __tgt_device_image{
  // void   *ImageStart;       // Pointer to the target code start.
  // void   *ImageEnd;         // Pointer to the target code end.
  // // We also add the host entries to the device image, as it may be useful
  // // for the target runtime to have access to that information.
  // __tgt_offload_entry  *EntriesBegin;   // Begin of the table with all
  //                                       // the entries.
  // __tgt_offload_entry  *EntriesEnd;     // End of the table with all the
  //                                       // entries (non inclusive).
  // };
  if (TgtDeviceImageQTy.isNull()) {
    ASTContext &C = CGM.getContext();
    RecordDecl *RD = C.buildImplicitRecord("__tgt_device_image");
    RD->startDefinition();
    addFieldToRecordDecl(C, RD, C.VoidPtrTy);
    addFieldToRecordDecl(C, RD, C.VoidPtrTy);
    addFieldToRecordDecl(C, RD, C.getPointerType(getTgtOffloadEntryQTy()));
    addFieldToRecordDecl(C, RD, C.getPointerType(getTgtOffloadEntryQTy()));
    RD->completeDefinition();
    TgtDeviceImageQTy = C.getRecordType(RD);
  }
  return TgtDeviceImageQTy;
}

QualType CGOpenMPRuntime::getTgtBinaryDescriptorQTy() {
  // struct __tgt_bin_desc{
  //   int32_t              NumDevices;      // Number of devices supported.
  //   __tgt_device_image   *DeviceImages;   // Arrays of device images
  //                                         // (one per device).
  //   __tgt_offload_entry  *EntriesBegin;   // Begin of the table with all the
  //                                         // entries.
  //   __tgt_offload_entry  *EntriesEnd;     // End of the table with all the
  //                                         // entries (non inclusive).
  // };
  if (TgtBinaryDescriptorQTy.isNull()) {
    ASTContext &C = CGM.getContext();
    RecordDecl *RD = C.buildImplicitRecord("__tgt_bin_desc");
    RD->startDefinition();
    addFieldToRecordDecl(
        C, RD, C.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/true));
    addFieldToRecordDecl(C, RD, C.getPointerType(getTgtDeviceImageQTy()));
    addFieldToRecordDecl(C, RD, C.getPointerType(getTgtOffloadEntryQTy()));
    addFieldToRecordDecl(C, RD, C.getPointerType(getTgtOffloadEntryQTy()));
    RD->completeDefinition();
    TgtBinaryDescriptorQTy = C.getRecordType(RD);
  }
  return TgtBinaryDescriptorQTy;
}

namespace {
struct PrivateHelpersTy {
  PrivateHelpersTy(const VarDecl *Original, const VarDecl *PrivateCopy,
                   const VarDecl *PrivateElemInit)
      : Original(Original), PrivateCopy(PrivateCopy),
        PrivateElemInit(PrivateElemInit) {}
  const VarDecl *Original;
  const VarDecl *PrivateCopy;
  const VarDecl *PrivateElemInit;
};
typedef std::pair<CharUnits /*Align*/, PrivateHelpersTy> PrivateDataTy;
} // anonymous namespace

static RecordDecl *
createPrivatesRecordDecl(CodeGenModule &CGM, ArrayRef<PrivateDataTy> Privates) {
  if (!Privates.empty()) {
    ASTContext &C = CGM.getContext();
    // Build struct .kmp_privates_t. {
    //         /*  private vars  */
    //       };
    RecordDecl *RD = C.buildImplicitRecord(".kmp_privates.t");
    RD->startDefinition();
    for (const auto &Pair : Privates) {
      const VarDecl *VD = Pair.second.Original;
      QualType Type = VD->getType().getNonReferenceType();
      FieldDecl *FD = addFieldToRecordDecl(C, RD, Type);
      if (VD->hasAttrs()) {
        for (specific_attr_iterator<AlignedAttr> I(VD->getAttrs().begin()),
             E(VD->getAttrs().end());
             I != E; ++I)
          FD->addAttr(*I);
      }
    }
    RD->completeDefinition();
    return RD;
  }
  return nullptr;
}

static RecordDecl *
createKmpTaskTRecordDecl(CodeGenModule &CGM, OpenMPDirectiveKind Kind,
                         QualType KmpInt32Ty,
                         QualType KmpRoutineEntryPointerQTy) {
  ASTContext &C = CGM.getContext();
  // Build struct kmp_task_t {
  //         void *              shareds;
  //         kmp_routine_entry_t routine;
  //         kmp_int32           part_id;
  //         kmp_cmplrdata_t data1;
  //         kmp_cmplrdata_t data2;
  // For taskloops additional fields:
  //         kmp_uint64          lb;
  //         kmp_uint64          ub;
  //         kmp_int64           st;
  //         kmp_int32           liter;
  //         void *              reductions;
  //       };
  RecordDecl *UD = C.buildImplicitRecord("kmp_cmplrdata_t", TTK_Union);
  UD->startDefinition();
  addFieldToRecordDecl(C, UD, KmpInt32Ty);
  addFieldToRecordDecl(C, UD, KmpRoutineEntryPointerQTy);
  UD->completeDefinition();
  QualType KmpCmplrdataTy = C.getRecordType(UD);
  RecordDecl *RD = C.buildImplicitRecord("kmp_task_t");
  RD->startDefinition();
  addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  addFieldToRecordDecl(C, RD, KmpRoutineEntryPointerQTy);
  addFieldToRecordDecl(C, RD, KmpInt32Ty);
  addFieldToRecordDecl(C, RD, KmpCmplrdataTy);
  addFieldToRecordDecl(C, RD, KmpCmplrdataTy);
  if (isOpenMPTaskLoopDirective(Kind)) {
    QualType KmpUInt64Ty =
        CGM.getContext().getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/0);
    QualType KmpInt64Ty =
        CGM.getContext().getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/1);
    addFieldToRecordDecl(C, RD, KmpUInt64Ty);
    addFieldToRecordDecl(C, RD, KmpUInt64Ty);
    addFieldToRecordDecl(C, RD, KmpInt64Ty);
    addFieldToRecordDecl(C, RD, KmpInt32Ty);
    addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  }
  RD->completeDefinition();
  return RD;
}

static RecordDecl *
createKmpTaskTWithPrivatesRecordDecl(CodeGenModule &CGM, QualType KmpTaskTQTy,
                                     ArrayRef<PrivateDataTy> Privates) {
  ASTContext &C = CGM.getContext();
  // Build struct kmp_task_t_with_privates {
  //         kmp_task_t task_data;
  //         .kmp_privates_t. privates;
  //       };
  RecordDecl *RD = C.buildImplicitRecord("kmp_task_t_with_privates");
  RD->startDefinition();
  addFieldToRecordDecl(C, RD, KmpTaskTQTy);
  if (const RecordDecl *PrivateRD = createPrivatesRecordDecl(CGM, Privates))
    addFieldToRecordDecl(C, RD, C.getRecordType(PrivateRD));
  RD->completeDefinition();
  return RD;
}

/// Emit a proxy function which accepts kmp_task_t as the second
/// argument.
/// \code
/// kmp_int32 .omp_task_entry.(kmp_int32 gtid, kmp_task_t *tt) {
///   TaskFunction(gtid, tt->part_id, &tt->privates, task_privates_map, tt,
///   For taskloops:
///   tt->task_data.lb, tt->task_data.ub, tt->task_data.st, tt->task_data.liter,
///   tt->reductions, tt->shareds);
///   return 0;
/// }
/// \endcode
static llvm::Value *
emitProxyTaskFunction(CodeGenModule &CGM, SourceLocation Loc,
                      OpenMPDirectiveKind Kind, QualType KmpInt32Ty,
                      QualType KmpTaskTWithPrivatesPtrQTy,
                      QualType KmpTaskTWithPrivatesQTy, QualType KmpTaskTQTy,
                      QualType SharedsPtrTy, llvm::Value *TaskFunction,
                      llvm::Value *TaskPrivatesMap) {
  ASTContext &C = CGM.getContext();
  FunctionArgList Args;
  ImplicitParamDecl GtidArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, KmpInt32Ty,
                            ImplicitParamDecl::Other);
  ImplicitParamDecl TaskTypeArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
                                KmpTaskTWithPrivatesPtrQTy.withRestrict(),
                                ImplicitParamDecl::Other);
  Args.push_back(&GtidArg);
  Args.push_back(&TaskTypeArg);
  const auto &TaskEntryFnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(KmpInt32Ty, Args);
  llvm::FunctionType *TaskEntryTy =
      CGM.getTypes().GetFunctionType(TaskEntryFnInfo);
  std::string Name = CGM.getOpenMPRuntime().getName({"omp_task_entry", ""});
  auto *TaskEntry = llvm::Function::Create(
      TaskEntryTy, llvm::GlobalValue::InternalLinkage, Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), TaskEntry, TaskEntryFnInfo);
  TaskEntry->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), KmpInt32Ty, TaskEntry, TaskEntryFnInfo, Args,
                    Loc, Loc);

  // TaskFunction(gtid, tt->task_data.part_id, &tt->privates, task_privates_map,
  // tt,
  // For taskloops:
  // tt->task_data.lb, tt->task_data.ub, tt->task_data.st, tt->task_data.liter,
  // tt->task_data.shareds);
  llvm::Value *GtidParam = CGF.EmitLoadOfScalar(
      CGF.GetAddrOfLocalVar(&GtidArg), /*Volatile=*/false, KmpInt32Ty, Loc);
  LValue TDBase = CGF.EmitLoadOfPointerLValue(
      CGF.GetAddrOfLocalVar(&TaskTypeArg),
      KmpTaskTWithPrivatesPtrQTy->castAs<PointerType>());
  const auto *KmpTaskTWithPrivatesQTyRD =
      cast<RecordDecl>(KmpTaskTWithPrivatesQTy->getAsTagDecl());
  LValue Base =
      CGF.EmitLValueForField(TDBase, *KmpTaskTWithPrivatesQTyRD->field_begin());
  const auto *KmpTaskTQTyRD = cast<RecordDecl>(KmpTaskTQTy->getAsTagDecl());
  auto PartIdFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTPartId);
  LValue PartIdLVal = CGF.EmitLValueForField(Base, *PartIdFI);
  llvm::Value *PartidParam = PartIdLVal.getPointer();

  auto SharedsFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTShareds);
  LValue SharedsLVal = CGF.EmitLValueForField(Base, *SharedsFI);
  llvm::Value *SharedsParam = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      CGF.EmitLoadOfScalar(SharedsLVal, Loc),
      CGF.ConvertTypeForMem(SharedsPtrTy));

  auto PrivatesFI = std::next(KmpTaskTWithPrivatesQTyRD->field_begin(), 1);
  llvm::Value *PrivatesParam;
  if (PrivatesFI != KmpTaskTWithPrivatesQTyRD->field_end()) {
    LValue PrivatesLVal = CGF.EmitLValueForField(TDBase, *PrivatesFI);
    PrivatesParam = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
        PrivatesLVal.getPointer(), CGF.VoidPtrTy);
  } else {
    PrivatesParam = llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
  }

  llvm::Value *CommonArgs[] = {GtidParam, PartidParam, PrivatesParam,
                               TaskPrivatesMap,
                               CGF.Builder
                                   .CreatePointerBitCastOrAddrSpaceCast(
                                       TDBase.getAddress(), CGF.VoidPtrTy)
                                   .getPointer()};
  SmallVector<llvm::Value *, 16> CallArgs(std::begin(CommonArgs),
                                          std::end(CommonArgs));
  if (isOpenMPTaskLoopDirective(Kind)) {
    auto LBFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTLowerBound);
    LValue LBLVal = CGF.EmitLValueForField(Base, *LBFI);
    llvm::Value *LBParam = CGF.EmitLoadOfScalar(LBLVal, Loc);
    auto UBFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTUpperBound);
    LValue UBLVal = CGF.EmitLValueForField(Base, *UBFI);
    llvm::Value *UBParam = CGF.EmitLoadOfScalar(UBLVal, Loc);
    auto StFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTStride);
    LValue StLVal = CGF.EmitLValueForField(Base, *StFI);
    llvm::Value *StParam = CGF.EmitLoadOfScalar(StLVal, Loc);
    auto LIFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTLastIter);
    LValue LILVal = CGF.EmitLValueForField(Base, *LIFI);
    llvm::Value *LIParam = CGF.EmitLoadOfScalar(LILVal, Loc);
    auto RFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTReductions);
    LValue RLVal = CGF.EmitLValueForField(Base, *RFI);
    llvm::Value *RParam = CGF.EmitLoadOfScalar(RLVal, Loc);
    CallArgs.push_back(LBParam);
    CallArgs.push_back(UBParam);
    CallArgs.push_back(StParam);
    CallArgs.push_back(LIParam);
    CallArgs.push_back(RParam);
  }
  CallArgs.push_back(SharedsParam);

  CGM.getOpenMPRuntime().emitOutlinedFunctionCall(CGF, Loc, TaskFunction,
                                                  CallArgs);
  CGF.EmitStoreThroughLValue(RValue::get(CGF.Builder.getInt32(/*C=*/0)),
                             CGF.MakeAddrLValue(CGF.ReturnValue, KmpInt32Ty));
  CGF.FinishFunction();
  return TaskEntry;
}

static llvm::Value *emitDestructorsFunction(CodeGenModule &CGM,
                                            SourceLocation Loc,
                                            QualType KmpInt32Ty,
                                            QualType KmpTaskTWithPrivatesPtrQTy,
                                            QualType KmpTaskTWithPrivatesQTy) {
  ASTContext &C = CGM.getContext();
  FunctionArgList Args;
  ImplicitParamDecl GtidArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, KmpInt32Ty,
                            ImplicitParamDecl::Other);
  ImplicitParamDecl TaskTypeArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
                                KmpTaskTWithPrivatesPtrQTy.withRestrict(),
                                ImplicitParamDecl::Other);
  Args.push_back(&GtidArg);
  Args.push_back(&TaskTypeArg);
  const auto &DestructorFnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(KmpInt32Ty, Args);
  llvm::FunctionType *DestructorFnTy =
      CGM.getTypes().GetFunctionType(DestructorFnInfo);
  std::string Name =
      CGM.getOpenMPRuntime().getName({"omp_task_destructor", ""});
  auto *DestructorFn =
      llvm::Function::Create(DestructorFnTy, llvm::GlobalValue::InternalLinkage,
                             Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), DestructorFn,
                                    DestructorFnInfo);
  DestructorFn->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), KmpInt32Ty, DestructorFn, DestructorFnInfo,
                    Args, Loc, Loc);

  LValue Base = CGF.EmitLoadOfPointerLValue(
      CGF.GetAddrOfLocalVar(&TaskTypeArg),
      KmpTaskTWithPrivatesPtrQTy->castAs<PointerType>());
  const auto *KmpTaskTWithPrivatesQTyRD =
      cast<RecordDecl>(KmpTaskTWithPrivatesQTy->getAsTagDecl());
  auto FI = std::next(KmpTaskTWithPrivatesQTyRD->field_begin());
  Base = CGF.EmitLValueForField(Base, *FI);
  for (const auto *Field :
       cast<RecordDecl>(FI->getType()->getAsTagDecl())->fields()) {
    if (QualType::DestructionKind DtorKind =
            Field->getType().isDestructedType()) {
      LValue FieldLValue = CGF.EmitLValueForField(Base, Field);
      CGF.pushDestroy(DtorKind, FieldLValue.getAddress(), Field->getType());
    }
  }
  CGF.FinishFunction();
  return DestructorFn;
}

/// Emit a privates mapping function for correct handling of private and
/// firstprivate variables.
/// \code
/// void .omp_task_privates_map.(const .privates. *noalias privs, <ty1>
/// **noalias priv1,...,  <tyn> **noalias privn) {
///   *priv1 = &.privates.priv1;
///   ...;
///   *privn = &.privates.privn;
/// }
/// \endcode
static llvm::Value *
emitTaskPrivateMappingFunction(CodeGenModule &CGM, SourceLocation Loc,
                               ArrayRef<const Expr *> PrivateVars,
                               ArrayRef<const Expr *> FirstprivateVars,
                               ArrayRef<const Expr *> LastprivateVars,
                               QualType PrivatesQTy,
                               ArrayRef<PrivateDataTy> Privates) {
  ASTContext &C = CGM.getContext();
  FunctionArgList Args;
  ImplicitParamDecl TaskPrivatesArg(
      C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
      C.getPointerType(PrivatesQTy).withConst().withRestrict(),
      ImplicitParamDecl::Other);
  Args.push_back(&TaskPrivatesArg);
  llvm::DenseMap<const VarDecl *, unsigned> PrivateVarsPos;
  unsigned Counter = 1;
  for (const Expr *E : PrivateVars) {
    Args.push_back(ImplicitParamDecl::Create(
        C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
        C.getPointerType(C.getPointerType(E->getType()))
            .withConst()
            .withRestrict(),
        ImplicitParamDecl::Other));
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    PrivateVarsPos[VD] = Counter;
    ++Counter;
  }
  for (const Expr *E : FirstprivateVars) {
    Args.push_back(ImplicitParamDecl::Create(
        C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
        C.getPointerType(C.getPointerType(E->getType()))
            .withConst()
            .withRestrict(),
        ImplicitParamDecl::Other));
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    PrivateVarsPos[VD] = Counter;
    ++Counter;
  }
  for (const Expr *E : LastprivateVars) {
    Args.push_back(ImplicitParamDecl::Create(
        C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
        C.getPointerType(C.getPointerType(E->getType()))
            .withConst()
            .withRestrict(),
        ImplicitParamDecl::Other));
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    PrivateVarsPos[VD] = Counter;
    ++Counter;
  }
  const auto &TaskPrivatesMapFnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *TaskPrivatesMapTy =
      CGM.getTypes().GetFunctionType(TaskPrivatesMapFnInfo);
  std::string Name =
      CGM.getOpenMPRuntime().getName({"omp_task_privates_map", ""});
  auto *TaskPrivatesMap = llvm::Function::Create(
      TaskPrivatesMapTy, llvm::GlobalValue::InternalLinkage, Name,
      &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), TaskPrivatesMap,
                                    TaskPrivatesMapFnInfo);
  TaskPrivatesMap->removeFnAttr(llvm::Attribute::NoInline);
  TaskPrivatesMap->removeFnAttr(llvm::Attribute::OptimizeNone);
  TaskPrivatesMap->addFnAttr(llvm::Attribute::AlwaysInline);
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, TaskPrivatesMap,
                    TaskPrivatesMapFnInfo, Args, Loc, Loc);

  // *privi = &.privates.privi;
  LValue Base = CGF.EmitLoadOfPointerLValue(
      CGF.GetAddrOfLocalVar(&TaskPrivatesArg),
      TaskPrivatesArg.getType()->castAs<PointerType>());
  const auto *PrivatesQTyRD = cast<RecordDecl>(PrivatesQTy->getAsTagDecl());
  Counter = 0;
  for (const FieldDecl *Field : PrivatesQTyRD->fields()) {
    LValue FieldLVal = CGF.EmitLValueForField(Base, Field);
    const VarDecl *VD = Args[PrivateVarsPos[Privates[Counter].second.Original]];
    LValue RefLVal =
        CGF.MakeAddrLValue(CGF.GetAddrOfLocalVar(VD), VD->getType());
    LValue RefLoadLVal = CGF.EmitLoadOfPointerLValue(
        RefLVal.getAddress(), RefLVal.getType()->castAs<PointerType>());
    CGF.EmitStoreOfScalar(FieldLVal.getPointer(), RefLoadLVal);
    ++Counter;
  }
  CGF.FinishFunction();
  return TaskPrivatesMap;
}

static bool stable_sort_comparator(const PrivateDataTy P1,
                                   const PrivateDataTy P2) {
  return P1.first > P2.first;
}

/// Emit initialization for private variables in task-based directives.
static void emitPrivatesInit(CodeGenFunction &CGF,
                             const OMPExecutableDirective &D,
                             Address KmpTaskSharedsPtr, LValue TDBase,
                             const RecordDecl *KmpTaskTWithPrivatesQTyRD,
                             QualType SharedsTy, QualType SharedsPtrTy,
                             const OMPTaskDataTy &Data,
                             ArrayRef<PrivateDataTy> Privates, bool ForDup) {
  ASTContext &C = CGF.getContext();
  auto FI = std::next(KmpTaskTWithPrivatesQTyRD->field_begin());
  LValue PrivatesBase = CGF.EmitLValueForField(TDBase, *FI);
  OpenMPDirectiveKind Kind = isOpenMPTaskLoopDirective(D.getDirectiveKind())
                                 ? OMPD_taskloop
                                 : OMPD_task;
  const CapturedStmt &CS = *D.getCapturedStmt(Kind);
  CodeGenFunction::CGCapturedStmtInfo CapturesInfo(CS);
  LValue SrcBase;
  bool IsTargetTask =
      isOpenMPTargetDataManagementDirective(D.getDirectiveKind()) ||
      isOpenMPTargetExecutionDirective(D.getDirectiveKind());
  // For target-based directives skip 3 firstprivate arrays BasePointersArray,
  // PointersArray and SizesArray. The original variables for these arrays are
  // not captured and we get their addresses explicitly.
  if ((!IsTargetTask && !Data.FirstprivateVars.empty()) ||
      (IsTargetTask && KmpTaskSharedsPtr.isValid())) {
    SrcBase = CGF.MakeAddrLValue(
        CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
            KmpTaskSharedsPtr, CGF.ConvertTypeForMem(SharedsPtrTy)),
        SharedsTy);
  }
  FI = cast<RecordDecl>(FI->getType()->getAsTagDecl())->field_begin();
  for (const PrivateDataTy &Pair : Privates) {
    const VarDecl *VD = Pair.second.PrivateCopy;
    const Expr *Init = VD->getAnyInitializer();
    if (Init && (!ForDup || (isa<CXXConstructExpr>(Init) &&
                             !CGF.isTrivialInitializer(Init)))) {
      LValue PrivateLValue = CGF.EmitLValueForField(PrivatesBase, *FI);
      if (const VarDecl *Elem = Pair.second.PrivateElemInit) {
        const VarDecl *OriginalVD = Pair.second.Original;
        // Check if the variable is the target-based BasePointersArray,
        // PointersArray or SizesArray.
        LValue SharedRefLValue;
        QualType Type = OriginalVD->getType();
        const FieldDecl *SharedField = CapturesInfo.lookup(OriginalVD);
        if (IsTargetTask && !SharedField) {
          assert(isa<ImplicitParamDecl>(OriginalVD) &&
                 isa<CapturedDecl>(OriginalVD->getDeclContext()) &&
                 cast<CapturedDecl>(OriginalVD->getDeclContext())
                         ->getNumParams() == 0 &&
                 isa<TranslationUnitDecl>(
                     cast<CapturedDecl>(OriginalVD->getDeclContext())
                         ->getDeclContext()) &&
                 "Expected artificial target data variable.");
          SharedRefLValue =
              CGF.MakeAddrLValue(CGF.GetAddrOfLocalVar(OriginalVD), Type);
        } else {
          SharedRefLValue = CGF.EmitLValueForField(SrcBase, SharedField);
          SharedRefLValue = CGF.MakeAddrLValue(
              Address(SharedRefLValue.getPointer(), C.getDeclAlign(OriginalVD)),
              SharedRefLValue.getType(), LValueBaseInfo(AlignmentSource::Decl),
              SharedRefLValue.getTBAAInfo());
        }
        if (Type->isArrayType()) {
          // Initialize firstprivate array.
          if (!isa<CXXConstructExpr>(Init) || CGF.isTrivialInitializer(Init)) {
            // Perform simple memcpy.
            CGF.EmitAggregateAssign(PrivateLValue, SharedRefLValue, Type);
          } else {
            // Initialize firstprivate array using element-by-element
            // initialization.
            CGF.EmitOMPAggregateAssign(
                PrivateLValue.getAddress(), SharedRefLValue.getAddress(), Type,
                [&CGF, Elem, Init, &CapturesInfo](Address DestElement,
                                                  Address SrcElement) {
                  // Clean up any temporaries needed by the initialization.
                  CodeGenFunction::OMPPrivateScope InitScope(CGF);
                  InitScope.addPrivate(
                      Elem, [SrcElement]() -> Address { return SrcElement; });
                  (void)InitScope.Privatize();
                  // Emit initialization for single element.
                  CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(
                      CGF, &CapturesInfo);
                  CGF.EmitAnyExprToMem(Init, DestElement,
                                       Init->getType().getQualifiers(),
                                       /*IsInitializer=*/false);
                });
          }
        } else {
          CodeGenFunction::OMPPrivateScope InitScope(CGF);
          InitScope.addPrivate(Elem, [SharedRefLValue]() -> Address {
            return SharedRefLValue.getAddress();
          });
          (void)InitScope.Privatize();
          CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CapturesInfo);
          CGF.EmitExprAsInit(Init, VD, PrivateLValue,
                             /*capturedByInit=*/false);
        }
      } else {
        CGF.EmitExprAsInit(Init, VD, PrivateLValue, /*capturedByInit=*/false);
      }
    }
    ++FI;
  }
}

/// Check if duplication function is required for taskloops.
static bool checkInitIsRequired(CodeGenFunction &CGF,
                                ArrayRef<PrivateDataTy> Privates) {
  bool InitRequired = false;
  for (const PrivateDataTy &Pair : Privates) {
    const VarDecl *VD = Pair.second.PrivateCopy;
    const Expr *Init = VD->getAnyInitializer();
    InitRequired = InitRequired || (Init && isa<CXXConstructExpr>(Init) &&
                                    !CGF.isTrivialInitializer(Init));
    if (InitRequired)
      break;
  }
  return InitRequired;
}


/// Emit task_dup function (for initialization of
/// private/firstprivate/lastprivate vars and last_iter flag)
/// \code
/// void __task_dup_entry(kmp_task_t *task_dst, const kmp_task_t *task_src, int
/// lastpriv) {
/// // setup lastprivate flag
///    task_dst->last = lastpriv;
/// // could be constructor calls here...
/// }
/// \endcode
static llvm::Value *
emitTaskDupFunction(CodeGenModule &CGM, SourceLocation Loc,
                    const OMPExecutableDirective &D,
                    QualType KmpTaskTWithPrivatesPtrQTy,
                    const RecordDecl *KmpTaskTWithPrivatesQTyRD,
                    const RecordDecl *KmpTaskTQTyRD, QualType SharedsTy,
                    QualType SharedsPtrTy, const OMPTaskDataTy &Data,
                    ArrayRef<PrivateDataTy> Privates, bool WithLastIter) {
  ASTContext &C = CGM.getContext();
  FunctionArgList Args;
  ImplicitParamDecl DstArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
                           KmpTaskTWithPrivatesPtrQTy,
                           ImplicitParamDecl::Other);
  ImplicitParamDecl SrcArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
                           KmpTaskTWithPrivatesPtrQTy,
                           ImplicitParamDecl::Other);
  ImplicitParamDecl LastprivArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.IntTy,
                                ImplicitParamDecl::Other);
  Args.push_back(&DstArg);
  Args.push_back(&SrcArg);
  Args.push_back(&LastprivArg);
  const auto &TaskDupFnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *TaskDupTy = CGM.getTypes().GetFunctionType(TaskDupFnInfo);
  std::string Name = CGM.getOpenMPRuntime().getName({"omp_task_dup", ""});
  auto *TaskDup = llvm::Function::Create(
      TaskDupTy, llvm::GlobalValue::InternalLinkage, Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), TaskDup, TaskDupFnInfo);
  TaskDup->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, TaskDup, TaskDupFnInfo, Args, Loc,
                    Loc);

  LValue TDBase = CGF.EmitLoadOfPointerLValue(
      CGF.GetAddrOfLocalVar(&DstArg),
      KmpTaskTWithPrivatesPtrQTy->castAs<PointerType>());
  // task_dst->liter = lastpriv;
  if (WithLastIter) {
    auto LIFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTLastIter);
    LValue Base = CGF.EmitLValueForField(
        TDBase, *KmpTaskTWithPrivatesQTyRD->field_begin());
    LValue LILVal = CGF.EmitLValueForField(Base, *LIFI);
    llvm::Value *Lastpriv = CGF.EmitLoadOfScalar(
        CGF.GetAddrOfLocalVar(&LastprivArg), /*Volatile=*/false, C.IntTy, Loc);
    CGF.EmitStoreOfScalar(Lastpriv, LILVal);
  }

  // Emit initial values for private copies (if any).
  assert(!Privates.empty());
  Address KmpTaskSharedsPtr = Address::invalid();
  if (!Data.FirstprivateVars.empty()) {
    LValue TDBase = CGF.EmitLoadOfPointerLValue(
        CGF.GetAddrOfLocalVar(&SrcArg),
        KmpTaskTWithPrivatesPtrQTy->castAs<PointerType>());
    LValue Base = CGF.EmitLValueForField(
        TDBase, *KmpTaskTWithPrivatesQTyRD->field_begin());
    KmpTaskSharedsPtr = Address(
        CGF.EmitLoadOfScalar(CGF.EmitLValueForField(
                                 Base, *std::next(KmpTaskTQTyRD->field_begin(),
                                                  KmpTaskTShareds)),
                             Loc),
        CGF.getNaturalTypeAlignment(SharedsTy));
  }
  emitPrivatesInit(CGF, D, KmpTaskSharedsPtr, TDBase, KmpTaskTWithPrivatesQTyRD,
                   SharedsTy, SharedsPtrTy, Data, Privates, /*ForDup=*/true);
  CGF.FinishFunction();
  return TaskDup;
}

/// Checks if destructor function is required to be generated.
/// \return true if cleanups are required, false otherwise.
static bool
checkDestructorsRequired(const RecordDecl *KmpTaskTWithPrivatesQTyRD) {
  bool NeedsCleanup = false;
  auto FI = std::next(KmpTaskTWithPrivatesQTyRD->field_begin(), 1);
  const auto *PrivateRD = cast<RecordDecl>(FI->getType()->getAsTagDecl());
  for (const FieldDecl *FD : PrivateRD->fields()) {
    NeedsCleanup = NeedsCleanup || FD->getType().isDestructedType();
    if (NeedsCleanup)
      break;
  }
  return NeedsCleanup;
}

CGOpenMPRuntime::TaskResultTy
CGOpenMPRuntime::emitTaskInit(CodeGenFunction &CGF, SourceLocation Loc,
                              const OMPExecutableDirective &D,
                              llvm::Value *TaskFunction, QualType SharedsTy,
                              Address Shareds, const OMPTaskDataTy &Data) {
  ASTContext &C = CGM.getContext();
  llvm::SmallVector<PrivateDataTy, 4> Privates;
  // Aggregate privates and sort them by the alignment.
  auto I = Data.PrivateCopies.begin();
  for (const Expr *E : Data.PrivateVars) {
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    Privates.emplace_back(
        C.getDeclAlign(VD),
        PrivateHelpersTy(VD, cast<VarDecl>(cast<DeclRefExpr>(*I)->getDecl()),
                         /*PrivateElemInit=*/nullptr));
    ++I;
  }
  I = Data.FirstprivateCopies.begin();
  auto IElemInitRef = Data.FirstprivateInits.begin();
  for (const Expr *E : Data.FirstprivateVars) {
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    Privates.emplace_back(
        C.getDeclAlign(VD),
        PrivateHelpersTy(
            VD, cast<VarDecl>(cast<DeclRefExpr>(*I)->getDecl()),
            cast<VarDecl>(cast<DeclRefExpr>(*IElemInitRef)->getDecl())));
    ++I;
    ++IElemInitRef;
  }
  I = Data.LastprivateCopies.begin();
  for (const Expr *E : Data.LastprivateVars) {
    const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl());
    Privates.emplace_back(
        C.getDeclAlign(VD),
        PrivateHelpersTy(VD, cast<VarDecl>(cast<DeclRefExpr>(*I)->getDecl()),
                         /*PrivateElemInit=*/nullptr));
    ++I;
  }
  std::stable_sort(Privates.begin(), Privates.end(), stable_sort_comparator);
  QualType KmpInt32Ty = C.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/1);
  // Build type kmp_routine_entry_t (if not built yet).
  emitKmpRoutineEntryT(KmpInt32Ty);
  // Build type kmp_task_t (if not built yet).
  if (isOpenMPTaskLoopDirective(D.getDirectiveKind())) {
    if (SavedKmpTaskloopTQTy.isNull()) {
      SavedKmpTaskloopTQTy = C.getRecordType(createKmpTaskTRecordDecl(
          CGM, D.getDirectiveKind(), KmpInt32Ty, KmpRoutineEntryPtrQTy));
    }
    KmpTaskTQTy = SavedKmpTaskloopTQTy;
  } else {
    assert((D.getDirectiveKind() == OMPD_task ||
            isOpenMPTargetExecutionDirective(D.getDirectiveKind()) ||
            isOpenMPTargetDataManagementDirective(D.getDirectiveKind())) &&
           "Expected taskloop, task or target directive");
    if (SavedKmpTaskTQTy.isNull()) {
      SavedKmpTaskTQTy = C.getRecordType(createKmpTaskTRecordDecl(
          CGM, D.getDirectiveKind(), KmpInt32Ty, KmpRoutineEntryPtrQTy));
    }
    KmpTaskTQTy = SavedKmpTaskTQTy;
  }
  const auto *KmpTaskTQTyRD = cast<RecordDecl>(KmpTaskTQTy->getAsTagDecl());
  // Build particular struct kmp_task_t for the given task.
  const RecordDecl *KmpTaskTWithPrivatesQTyRD =
      createKmpTaskTWithPrivatesRecordDecl(CGM, KmpTaskTQTy, Privates);
  QualType KmpTaskTWithPrivatesQTy = C.getRecordType(KmpTaskTWithPrivatesQTyRD);
  QualType KmpTaskTWithPrivatesPtrQTy =
      C.getPointerType(KmpTaskTWithPrivatesQTy);
  llvm::Type *KmpTaskTWithPrivatesTy = CGF.ConvertType(KmpTaskTWithPrivatesQTy);
  llvm::Type *KmpTaskTWithPrivatesPtrTy =
      KmpTaskTWithPrivatesTy->getPointerTo();
  llvm::Value *KmpTaskTWithPrivatesTySize =
      CGF.getTypeSize(KmpTaskTWithPrivatesQTy);
  QualType SharedsPtrTy = C.getPointerType(SharedsTy);

  // Emit initial values for private copies (if any).
  llvm::Value *TaskPrivatesMap = nullptr;
  llvm::Type *TaskPrivatesMapTy =
      std::next(cast<llvm::Function>(TaskFunction)->arg_begin(), 3)->getType();
  if (!Privates.empty()) {
    auto FI = std::next(KmpTaskTWithPrivatesQTyRD->field_begin());
    TaskPrivatesMap = emitTaskPrivateMappingFunction(
        CGM, Loc, Data.PrivateVars, Data.FirstprivateVars, Data.LastprivateVars,
        FI->getType(), Privates);
    TaskPrivatesMap = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
        TaskPrivatesMap, TaskPrivatesMapTy);
  } else {
    TaskPrivatesMap = llvm::ConstantPointerNull::get(
        cast<llvm::PointerType>(TaskPrivatesMapTy));
  }
  // Build a proxy function kmp_int32 .omp_task_entry.(kmp_int32 gtid,
  // kmp_task_t *tt);
  llvm::Value *TaskEntry = emitProxyTaskFunction(
      CGM, Loc, D.getDirectiveKind(), KmpInt32Ty, KmpTaskTWithPrivatesPtrQTy,
      KmpTaskTWithPrivatesQTy, KmpTaskTQTy, SharedsPtrTy, TaskFunction,
      TaskPrivatesMap);

  // Build call kmp_task_t * __kmpc_omp_task_alloc(ident_t *, kmp_int32 gtid,
  // kmp_int32 flags, size_t sizeof_kmp_task_t, size_t sizeof_shareds,
  // kmp_routine_entry_t *task_entry);
  // Task flags. Format is taken from
  // http://llvm.org/svn/llvm-project/openmp/trunk/runtime/src/kmp.h,
  // description of kmp_tasking_flags struct.
  enum {
    TiedFlag = 0x1,
    FinalFlag = 0x2,
    DestructorsFlag = 0x8,
    PriorityFlag = 0x20
  };
  unsigned Flags = Data.Tied ? TiedFlag : 0;
  bool NeedsCleanup = false;
  if (!Privates.empty()) {
    NeedsCleanup = checkDestructorsRequired(KmpTaskTWithPrivatesQTyRD);
    if (NeedsCleanup)
      Flags = Flags | DestructorsFlag;
  }
  if (Data.Priority.getInt())
    Flags = Flags | PriorityFlag;
  llvm::Value *TaskFlags =
      Data.Final.getPointer()
          ? CGF.Builder.CreateSelect(Data.Final.getPointer(),
                                     CGF.Builder.getInt32(FinalFlag),
                                     CGF.Builder.getInt32(/*C=*/0))
          : CGF.Builder.getInt32(Data.Final.getInt() ? FinalFlag : 0);
  TaskFlags = CGF.Builder.CreateOr(TaskFlags, CGF.Builder.getInt32(Flags));
  llvm::Value *SharedsSize = CGM.getSize(C.getTypeSizeInChars(SharedsTy));
  llvm::Value *AllocArgs[] = {emitUpdateLocation(CGF, Loc),
                              getThreadID(CGF, Loc), TaskFlags,
                              KmpTaskTWithPrivatesTySize, SharedsSize,
                              CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
                                  TaskEntry, KmpRoutineEntryPtrTy)};
  llvm::Value *NewTask = CGF.EmitRuntimeCall(
      createRuntimeFunction(OMPRTL__kmpc_omp_task_alloc), AllocArgs);
  llvm::Value *NewTaskNewTaskTTy =
      CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
          NewTask, KmpTaskTWithPrivatesPtrTy);
  LValue Base = CGF.MakeNaturalAlignAddrLValue(NewTaskNewTaskTTy,
                                               KmpTaskTWithPrivatesQTy);
  LValue TDBase =
      CGF.EmitLValueForField(Base, *KmpTaskTWithPrivatesQTyRD->field_begin());
  // Fill the data in the resulting kmp_task_t record.
  // Copy shareds if there are any.
  Address KmpTaskSharedsPtr = Address::invalid();
  if (!SharedsTy->getAsStructureType()->getDecl()->field_empty()) {
    KmpTaskSharedsPtr =
        Address(CGF.EmitLoadOfScalar(
                    CGF.EmitLValueForField(
                        TDBase, *std::next(KmpTaskTQTyRD->field_begin(),
                                           KmpTaskTShareds)),
                    Loc),
                CGF.getNaturalTypeAlignment(SharedsTy));
    LValue Dest = CGF.MakeAddrLValue(KmpTaskSharedsPtr, SharedsTy);
    LValue Src = CGF.MakeAddrLValue(Shareds, SharedsTy);
    CGF.EmitAggregateCopy(Dest, Src, SharedsTy, AggValueSlot::DoesNotOverlap);
  }
  // Emit initial values for private copies (if any).
  TaskResultTy Result;
  if (!Privates.empty()) {
    emitPrivatesInit(CGF, D, KmpTaskSharedsPtr, Base, KmpTaskTWithPrivatesQTyRD,
                     SharedsTy, SharedsPtrTy, Data, Privates,
                     /*ForDup=*/false);
    if (isOpenMPTaskLoopDirective(D.getDirectiveKind()) &&
        (!Data.LastprivateVars.empty() || checkInitIsRequired(CGF, Privates))) {
      Result.TaskDupFn = emitTaskDupFunction(
          CGM, Loc, D, KmpTaskTWithPrivatesPtrQTy, KmpTaskTWithPrivatesQTyRD,
          KmpTaskTQTyRD, SharedsTy, SharedsPtrTy, Data, Privates,
          /*WithLastIter=*/!Data.LastprivateVars.empty());
    }
  }
  // Fields of union "kmp_cmplrdata_t" for destructors and priority.
  enum { Priority = 0, Destructors = 1 };
  // Provide pointer to function with destructors for privates.
  auto FI = std::next(KmpTaskTQTyRD->field_begin(), Data1);
  const RecordDecl *KmpCmplrdataUD =
      (*FI)->getType()->getAsUnionType()->getDecl();
  if (NeedsCleanup) {
    llvm::Value *DestructorFn = emitDestructorsFunction(
        CGM, Loc, KmpInt32Ty, KmpTaskTWithPrivatesPtrQTy,
        KmpTaskTWithPrivatesQTy);
    LValue Data1LV = CGF.EmitLValueForField(TDBase, *FI);
    LValue DestructorsLV = CGF.EmitLValueForField(
        Data1LV, *std::next(KmpCmplrdataUD->field_begin(), Destructors));
    CGF.EmitStoreOfScalar(CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
                              DestructorFn, KmpRoutineEntryPtrTy),
                          DestructorsLV);
  }
  // Set priority.
  if (Data.Priority.getInt()) {
    LValue Data2LV = CGF.EmitLValueForField(
        TDBase, *std::next(KmpTaskTQTyRD->field_begin(), Data2));
    LValue PriorityLV = CGF.EmitLValueForField(
        Data2LV, *std::next(KmpCmplrdataUD->field_begin(), Priority));
    CGF.EmitStoreOfScalar(Data.Priority.getPointer(), PriorityLV);
  }
  Result.NewTask = NewTask;
  Result.TaskEntry = TaskEntry;
  Result.NewTaskNewTaskTTy = NewTaskNewTaskTTy;
  Result.TDBase = TDBase;
  Result.KmpTaskTQTyRD = KmpTaskTQTyRD;
  return Result;
}

void CGOpenMPRuntime::emitTaskCall(CodeGenFunction &CGF, SourceLocation Loc,
                                   const OMPExecutableDirective &D,
                                   llvm::Value *TaskFunction,
                                   QualType SharedsTy, Address Shareds,
                                   const Expr *IfCond,
                                   const OMPTaskDataTy &Data) {
  if (!CGF.HaveInsertPoint())
    return;

  TaskResultTy Result =
      emitTaskInit(CGF, Loc, D, TaskFunction, SharedsTy, Shareds, Data);
  llvm::Value *NewTask = Result.NewTask;
  llvm::Value *TaskEntry = Result.TaskEntry;
  llvm::Value *NewTaskNewTaskTTy = Result.NewTaskNewTaskTTy;
  LValue TDBase = Result.TDBase;
  const RecordDecl *KmpTaskTQTyRD = Result.KmpTaskTQTyRD;
  ASTContext &C = CGM.getContext();
  // Process list of dependences.
  Address DependenciesArray = Address::invalid();
  unsigned NumDependencies = Data.Dependences.size();
  if (NumDependencies) {
    // Dependence kind for RTL.
    enum RTLDependenceKindTy { DepIn = 0x01, DepInOut = 0x3 };
    enum RTLDependInfoFieldsTy { BaseAddr, Len, Flags };
    RecordDecl *KmpDependInfoRD;
    QualType FlagsTy =
        C.getIntTypeForBitwidth(C.getTypeSize(C.BoolTy), /*Signed=*/false);
    llvm::Type *LLVMFlagsTy = CGF.ConvertTypeForMem(FlagsTy);
    if (KmpDependInfoTy.isNull()) {
      KmpDependInfoRD = C.buildImplicitRecord("kmp_depend_info");
      KmpDependInfoRD->startDefinition();
      addFieldToRecordDecl(C, KmpDependInfoRD, C.getIntPtrType());
      addFieldToRecordDecl(C, KmpDependInfoRD, C.getSizeType());
      addFieldToRecordDecl(C, KmpDependInfoRD, FlagsTy);
      KmpDependInfoRD->completeDefinition();
      KmpDependInfoTy = C.getRecordType(KmpDependInfoRD);
    } else {
      KmpDependInfoRD = cast<RecordDecl>(KmpDependInfoTy->getAsTagDecl());
    }
    CharUnits DependencySize = C.getTypeSizeInChars(KmpDependInfoTy);
    // Define type kmp_depend_info[<Dependences.size()>];
    QualType KmpDependInfoArrayTy = C.getConstantArrayType(
        KmpDependInfoTy, llvm::APInt(/*numBits=*/64, NumDependencies),
        ArrayType::Normal, /*IndexTypeQuals=*/0);
    // kmp_depend_info[<Dependences.size()>] deps;
    DependenciesArray =
        CGF.CreateMemTemp(KmpDependInfoArrayTy, ".dep.arr.addr");
    for (unsigned I = 0; I < NumDependencies; ++I) {
      const Expr *E = Data.Dependences[I].second;
      LValue Addr = CGF.EmitLValue(E);
      llvm::Value *Size;
      QualType Ty = E->getType();
      if (const auto *ASE =
              dyn_cast<OMPArraySectionExpr>(E->IgnoreParenImpCasts())) {
        LValue UpAddrLVal =
            CGF.EmitOMPArraySectionExpr(ASE, /*LowerBound=*/false);
        llvm::Value *UpAddr =
            CGF.Builder.CreateConstGEP1_32(UpAddrLVal.getPointer(), /*Idx0=*/1);
        llvm::Value *LowIntPtr =
            CGF.Builder.CreatePtrToInt(Addr.getPointer(), CGM.SizeTy);
        llvm::Value *UpIntPtr = CGF.Builder.CreatePtrToInt(UpAddr, CGM.SizeTy);
        Size = CGF.Builder.CreateNUWSub(UpIntPtr, LowIntPtr);
      } else {
        Size = CGF.getTypeSize(Ty);
      }
      LValue Base = CGF.MakeAddrLValue(
          CGF.Builder.CreateConstArrayGEP(DependenciesArray, I, DependencySize),
          KmpDependInfoTy);
      // deps[i].base_addr = &<Dependences[i].second>;
      LValue BaseAddrLVal = CGF.EmitLValueForField(
          Base, *std::next(KmpDependInfoRD->field_begin(), BaseAddr));
      CGF.EmitStoreOfScalar(
          CGF.Builder.CreatePtrToInt(Addr.getPointer(), CGF.IntPtrTy),
          BaseAddrLVal);
      // deps[i].len = sizeof(<Dependences[i].second>);
      LValue LenLVal = CGF.EmitLValueForField(
          Base, *std::next(KmpDependInfoRD->field_begin(), Len));
      CGF.EmitStoreOfScalar(Size, LenLVal);
      // deps[i].flags = <Dependences[i].first>;
      RTLDependenceKindTy DepKind;
      switch (Data.Dependences[I].first) {
      case OMPC_DEPEND_in:
        DepKind = DepIn;
        break;
      // Out and InOut dependencies must use the same code.
      case OMPC_DEPEND_out:
      case OMPC_DEPEND_inout:
        DepKind = DepInOut;
        break;
      case OMPC_DEPEND_source:
      case OMPC_DEPEND_sink:
      case OMPC_DEPEND_unknown:
        llvm_unreachable("Unknown task dependence type");
      }
      LValue FlagsLVal = CGF.EmitLValueForField(
          Base, *std::next(KmpDependInfoRD->field_begin(), Flags));
      CGF.EmitStoreOfScalar(llvm::ConstantInt::get(LLVMFlagsTy, DepKind),
                            FlagsLVal);
    }
    DependenciesArray = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
        CGF.Builder.CreateStructGEP(DependenciesArray, 0, CharUnits::Zero()),
        CGF.VoidPtrTy);
  }

  // NOTE: routine and part_id fields are initialized by __kmpc_omp_task_alloc()
  // libcall.
  // Build kmp_int32 __kmpc_omp_task_with_deps(ident_t *, kmp_int32 gtid,
  // kmp_task_t *new_task, kmp_int32 ndeps, kmp_depend_info_t *dep_list,
  // kmp_int32 ndeps_noalias, kmp_depend_info_t *noalias_dep_list) if dependence
  // list is not empty
  llvm::Value *ThreadID = getThreadID(CGF, Loc);
  llvm::Value *UpLoc = emitUpdateLocation(CGF, Loc);
  llvm::Value *TaskArgs[] = { UpLoc, ThreadID, NewTask };
  llvm::Value *DepTaskArgs[7];
  if (NumDependencies) {
    DepTaskArgs[0] = UpLoc;
    DepTaskArgs[1] = ThreadID;
    DepTaskArgs[2] = NewTask;
    DepTaskArgs[3] = CGF.Builder.getInt32(NumDependencies);
    DepTaskArgs[4] = DependenciesArray.getPointer();
    DepTaskArgs[5] = CGF.Builder.getInt32(0);
    DepTaskArgs[6] = llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
  }
  auto &&ThenCodeGen = [this, &Data, TDBase, KmpTaskTQTyRD, NumDependencies,
                        &TaskArgs,
                        &DepTaskArgs](CodeGenFunction &CGF, PrePostActionTy &) {
    if (!Data.Tied) {
      auto PartIdFI = std::next(KmpTaskTQTyRD->field_begin(), KmpTaskTPartId);
      LValue PartIdLVal = CGF.EmitLValueForField(TDBase, *PartIdFI);
      CGF.EmitStoreOfScalar(CGF.Builder.getInt32(0), PartIdLVal);
    }
    if (NumDependencies) {
      CGF.EmitRuntimeCall(
          createRuntimeFunction(OMPRTL__kmpc_omp_task_with_deps), DepTaskArgs);
    } else {
      CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_omp_task),
                          TaskArgs);
    }
    // Check if parent region is untied and build return for untied task;
    if (auto *Region =
            dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo))
      Region->emitUntiedSwitch(CGF);
  };

  llvm::Value *DepWaitTaskArgs[6];
  if (NumDependencies) {
    DepWaitTaskArgs[0] = UpLoc;
    DepWaitTaskArgs[1] = ThreadID;
    DepWaitTaskArgs[2] = CGF.Builder.getInt32(NumDependencies);
    DepWaitTaskArgs[3] = DependenciesArray.getPointer();
    DepWaitTaskArgs[4] = CGF.Builder.getInt32(0);
    DepWaitTaskArgs[5] = llvm::ConstantPointerNull::get(CGF.VoidPtrTy);
  }
  auto &&ElseCodeGen = [&TaskArgs, ThreadID, NewTaskNewTaskTTy, TaskEntry,
                        NumDependencies, &DepWaitTaskArgs,
                        Loc](CodeGenFunction &CGF, PrePostActionTy &) {
    CGOpenMPRuntime &RT = CGF.CGM.getOpenMPRuntime();
    CodeGenFunction::RunCleanupsScope LocalScope(CGF);
    // Build void __kmpc_omp_wait_deps(ident_t *, kmp_int32 gtid,
    // kmp_int32 ndeps, kmp_depend_info_t *dep_list, kmp_int32
    // ndeps_noalias, kmp_depend_info_t *noalias_dep_list); if dependence info
    // is specified.
    if (NumDependencies)
      CGF.EmitRuntimeCall(RT.createRuntimeFunction(OMPRTL__kmpc_omp_wait_deps),
                          DepWaitTaskArgs);
    // Call proxy_task_entry(gtid, new_task);
    auto &&CodeGen = [TaskEntry, ThreadID, NewTaskNewTaskTTy,
                      Loc](CodeGenFunction &CGF, PrePostActionTy &Action) {
      Action.Enter(CGF);
      llvm::Value *OutlinedFnArgs[] = {ThreadID, NewTaskNewTaskTTy};
      CGF.CGM.getOpenMPRuntime().emitOutlinedFunctionCall(CGF, Loc, TaskEntry,
                                                          OutlinedFnArgs);
    };

    // Build void __kmpc_omp_task_begin_if0(ident_t *, kmp_int32 gtid,
    // kmp_task_t *new_task);
    // Build void __kmpc_omp_task_complete_if0(ident_t *, kmp_int32 gtid,
    // kmp_task_t *new_task);
    RegionCodeGenTy RCG(CodeGen);
    CommonActionTy Action(
        RT.createRuntimeFunction(OMPRTL__kmpc_omp_task_begin_if0), TaskArgs,
        RT.createRuntimeFunction(OMPRTL__kmpc_omp_task_complete_if0), TaskArgs);
    RCG.setAction(Action);
    RCG(CGF);
  };

  if (IfCond) {
    emitOMPIfClause(CGF, IfCond, ThenCodeGen, ElseCodeGen);
  } else {
    RegionCodeGenTy ThenRCG(ThenCodeGen);
    ThenRCG(CGF);
  }
}

void CGOpenMPRuntime::emitTaskLoopCall(CodeGenFunction &CGF, SourceLocation Loc,
                                       const OMPLoopDirective &D,
                                       llvm::Value *TaskFunction,
                                       QualType SharedsTy, Address Shareds,
                                       const Expr *IfCond,
                                       const OMPTaskDataTy &Data) {
  if (!CGF.HaveInsertPoint())
    return;
  TaskResultTy Result =
      emitTaskInit(CGF, Loc, D, TaskFunction, SharedsTy, Shareds, Data);
  // NOTE: routine and part_id fields are initialized by __kmpc_omp_task_alloc()
  // libcall.
  // Call to void __kmpc_taskloop(ident_t *loc, int gtid, kmp_task_t *task, int
  // if_val, kmp_uint64 *lb, kmp_uint64 *ub, kmp_int64 st, int nogroup, int
  // sched, kmp_uint64 grainsize, void *task_dup);
  llvm::Value *ThreadID = getThreadID(CGF, Loc);
  llvm::Value *UpLoc = emitUpdateLocation(CGF, Loc);
  llvm::Value *IfVal;
  if (IfCond) {
    IfVal = CGF.Builder.CreateIntCast(CGF.EvaluateExprAsBool(IfCond), CGF.IntTy,
                                      /*isSigned=*/true);
  } else {
    IfVal = llvm::ConstantInt::getSigned(CGF.IntTy, /*V=*/1);
  }

  LValue LBLVal = CGF.EmitLValueForField(
      Result.TDBase,
      *std::next(Result.KmpTaskTQTyRD->field_begin(), KmpTaskTLowerBound));
  const auto *LBVar =
      cast<VarDecl>(cast<DeclRefExpr>(D.getLowerBoundVariable())->getDecl());
  CGF.EmitAnyExprToMem(LBVar->getInit(), LBLVal.getAddress(), LBLVal.getQuals(),
                       /*IsInitializer=*/true);
  LValue UBLVal = CGF.EmitLValueForField(
      Result.TDBase,
      *std::next(Result.KmpTaskTQTyRD->field_begin(), KmpTaskTUpperBound));
  const auto *UBVar =
      cast<VarDecl>(cast<DeclRefExpr>(D.getUpperBoundVariable())->getDecl());
  CGF.EmitAnyExprToMem(UBVar->getInit(), UBLVal.getAddress(), UBLVal.getQuals(),
                       /*IsInitializer=*/true);
  LValue StLVal = CGF.EmitLValueForField(
      Result.TDBase,
      *std::next(Result.KmpTaskTQTyRD->field_begin(), KmpTaskTStride));
  const auto *StVar =
      cast<VarDecl>(cast<DeclRefExpr>(D.getStrideVariable())->getDecl());
  CGF.EmitAnyExprToMem(StVar->getInit(), StLVal.getAddress(), StLVal.getQuals(),
                       /*IsInitializer=*/true);
  // Store reductions address.
  LValue RedLVal = CGF.EmitLValueForField(
      Result.TDBase,
      *std::next(Result.KmpTaskTQTyRD->field_begin(), KmpTaskTReductions));
  if (Data.Reductions) {
    CGF.EmitStoreOfScalar(Data.Reductions, RedLVal);
  } else {
    CGF.EmitNullInitialization(RedLVal.getAddress(),
                               CGF.getContext().VoidPtrTy);
  }
  enum { NoSchedule = 0, Grainsize = 1, NumTasks = 2 };
  llvm::Value *TaskArgs[] = {
      UpLoc,
      ThreadID,
      Result.NewTask,
      IfVal,
      LBLVal.getPointer(),
      UBLVal.getPointer(),
      CGF.EmitLoadOfScalar(StLVal, Loc),
      llvm::ConstantInt::getSigned(
              CGF.IntTy, 1), // Always 1 because taskgroup emitted by the compiler
      llvm::ConstantInt::getSigned(
          CGF.IntTy, Data.Schedule.getPointer()
                         ? Data.Schedule.getInt() ? NumTasks : Grainsize
                         : NoSchedule),
      Data.Schedule.getPointer()
          ? CGF.Builder.CreateIntCast(Data.Schedule.getPointer(), CGF.Int64Ty,
                                      /*isSigned=*/false)
          : llvm::ConstantInt::get(CGF.Int64Ty, /*V=*/0),
      Result.TaskDupFn ? CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
                             Result.TaskDupFn, CGF.VoidPtrTy)
                       : llvm::ConstantPointerNull::get(CGF.VoidPtrTy)};
  CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_taskloop), TaskArgs);
}

/// Emit reduction operation for each element of array (required for
/// array sections) LHS op = RHS.
/// \param Type Type of array.
/// \param LHSVar Variable on the left side of the reduction operation
/// (references element of array in original variable).
/// \param RHSVar Variable on the right side of the reduction operation
/// (references element of array in original variable).
/// \param RedOpGen Generator of reduction operation with use of LHSVar and
/// RHSVar.
static void EmitOMPAggregateReduction(
    CodeGenFunction &CGF, QualType Type, const VarDecl *LHSVar,
    const VarDecl *RHSVar,
    const llvm::function_ref<void(CodeGenFunction &CGF, const Expr *,
                                  const Expr *, const Expr *)> &RedOpGen,
    const Expr *XExpr = nullptr, const Expr *EExpr = nullptr,
    const Expr *UpExpr = nullptr) {
  // Perform element-by-element initialization.
  QualType ElementTy;
  Address LHSAddr = CGF.GetAddrOfLocalVar(LHSVar);
  Address RHSAddr = CGF.GetAddrOfLocalVar(RHSVar);

  // Drill down to the base element type on both arrays.
  const ArrayType *ArrayTy = Type->getAsArrayTypeUnsafe();
  llvm::Value *NumElements = CGF.emitArrayLength(ArrayTy, ElementTy, LHSAddr);

  llvm::Value *RHSBegin = RHSAddr.getPointer();
  llvm::Value *LHSBegin = LHSAddr.getPointer();
  // Cast from pointer to array type to pointer to single element.
  llvm::Value *LHSEnd = CGF.Builder.CreateGEP(LHSBegin, NumElements);
  // The basic structure here is a while-do loop.
  llvm::BasicBlock *BodyBB = CGF.createBasicBlock("omp.arraycpy.body");
  llvm::BasicBlock *DoneBB = CGF.createBasicBlock("omp.arraycpy.done");
  llvm::Value *IsEmpty =
      CGF.Builder.CreateICmpEQ(LHSBegin, LHSEnd, "omp.arraycpy.isempty");
  CGF.Builder.CreateCondBr(IsEmpty, DoneBB, BodyBB);

  // Enter the loop body, making that address the current address.
  llvm::BasicBlock *EntryBB = CGF.Builder.GetInsertBlock();
  CGF.EmitBlock(BodyBB);

  CharUnits ElementSize = CGF.getContext().getTypeSizeInChars(ElementTy);

  llvm::PHINode *RHSElementPHI = CGF.Builder.CreatePHI(
      RHSBegin->getType(), 2, "omp.arraycpy.srcElementPast");
  RHSElementPHI->addIncoming(RHSBegin, EntryBB);
  Address RHSElementCurrent =
      Address(RHSElementPHI,
              RHSAddr.getAlignment().alignmentOfArrayElement(ElementSize));

  llvm::PHINode *LHSElementPHI = CGF.Builder.CreatePHI(
      LHSBegin->getType(), 2, "omp.arraycpy.destElementPast");
  LHSElementPHI->addIncoming(LHSBegin, EntryBB);
  Address LHSElementCurrent =
      Address(LHSElementPHI,
              LHSAddr.getAlignment().alignmentOfArrayElement(ElementSize));

  // Emit copy.
  CodeGenFunction::OMPPrivateScope Scope(CGF);
  Scope.addPrivate(LHSVar, [=]() { return LHSElementCurrent; });
  Scope.addPrivate(RHSVar, [=]() { return RHSElementCurrent; });
  Scope.Privatize();
  RedOpGen(CGF, XExpr, EExpr, UpExpr);
  Scope.ForceCleanup();

  // Shift the address forward by one element.
  llvm::Value *LHSElementNext = CGF.Builder.CreateConstGEP1_32(
      LHSElementPHI, /*Idx0=*/1, "omp.arraycpy.dest.element");
  llvm::Value *RHSElementNext = CGF.Builder.CreateConstGEP1_32(
      RHSElementPHI, /*Idx0=*/1, "omp.arraycpy.src.element");
  // Check whether we've reached the end.
  llvm::Value *Done =
      CGF.Builder.CreateICmpEQ(LHSElementNext, LHSEnd, "omp.arraycpy.done");
  CGF.Builder.CreateCondBr(Done, DoneBB, BodyBB);
  LHSElementPHI->addIncoming(LHSElementNext, CGF.Builder.GetInsertBlock());
  RHSElementPHI->addIncoming(RHSElementNext, CGF.Builder.GetInsertBlock());

  // Done.
  CGF.EmitBlock(DoneBB, /*IsFinished=*/true);
}

/// Emit reduction combiner. If the combiner is a simple expression emit it as
/// is, otherwise consider it as combiner of UDR decl and emit it as a call of
/// UDR combiner function.
static void emitReductionCombiner(CodeGenFunction &CGF,
                                  const Expr *ReductionOp) {
  if (const auto *CE = dyn_cast<CallExpr>(ReductionOp))
    if (const auto *OVE = dyn_cast<OpaqueValueExpr>(CE->getCallee()))
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(OVE->getSourceExpr()->IgnoreImpCasts()))
        if (const auto *DRD =
                dyn_cast<OMPDeclareReductionDecl>(DRE->getDecl())) {
          std::pair<llvm::Function *, llvm::Function *> Reduction =
              CGF.CGM.getOpenMPRuntime().getUserDefinedReduction(DRD);
          RValue Func = RValue::get(Reduction.first);
          CodeGenFunction::OpaqueValueMapping Map(CGF, OVE, Func);
          CGF.EmitIgnoredExpr(ReductionOp);
          return;
        }
  CGF.EmitIgnoredExpr(ReductionOp);
}

llvm::Value *CGOpenMPRuntime::emitReductionFunction(
    CodeGenModule &CGM, SourceLocation Loc, llvm::Type *ArgsType,
    ArrayRef<const Expr *> Privates, ArrayRef<const Expr *> LHSExprs,
    ArrayRef<const Expr *> RHSExprs, ArrayRef<const Expr *> ReductionOps) {
  ASTContext &C = CGM.getContext();

  // void reduction_func(void *LHSArg, void *RHSArg);
  FunctionArgList Args;
  ImplicitParamDecl LHSArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                           ImplicitParamDecl::Other);
  ImplicitParamDecl RHSArg(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                           ImplicitParamDecl::Other);
  Args.push_back(&LHSArg);
  Args.push_back(&RHSArg);
  const auto &CGFI =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  std::string Name = getName({"omp", "reduction", "reduction_func"});
  auto *Fn = llvm::Function::Create(CGM.getTypes().GetFunctionType(CGFI),
                                    llvm::GlobalValue::InternalLinkage, Name,
                                    &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, CGFI);
  Fn->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, CGFI, Args, Loc, Loc);

  // Dst = (void*[n])(LHSArg);
  // Src = (void*[n])(RHSArg);
  Address LHS(CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(&LHSArg)),
      ArgsType), CGF.getPointerAlign());
  Address RHS(CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(&RHSArg)),
      ArgsType), CGF.getPointerAlign());

  //  ...
  //  *(Type<i>*)lhs[i] = RedOp<i>(*(Type<i>*)lhs[i], *(Type<i>*)rhs[i]);
  //  ...
  CodeGenFunction::OMPPrivateScope Scope(CGF);
  auto IPriv = Privates.begin();
  unsigned Idx = 0;
  for (unsigned I = 0, E = ReductionOps.size(); I < E; ++I, ++IPriv, ++Idx) {
    const auto *RHSVar =
        cast<VarDecl>(cast<DeclRefExpr>(RHSExprs[I])->getDecl());
    Scope.addPrivate(RHSVar, [&CGF, RHS, Idx, RHSVar]() {
      return emitAddrOfVarFromArray(CGF, RHS, Idx, RHSVar);
    });
    const auto *LHSVar =
        cast<VarDecl>(cast<DeclRefExpr>(LHSExprs[I])->getDecl());
    Scope.addPrivate(LHSVar, [&CGF, LHS, Idx, LHSVar]() {
      return emitAddrOfVarFromArray(CGF, LHS, Idx, LHSVar);
    });
    QualType PrivTy = (*IPriv)->getType();
    if (PrivTy->isVariablyModifiedType()) {
      // Get array size and emit VLA type.
      ++Idx;
      Address Elem =
          CGF.Builder.CreateConstArrayGEP(LHS, Idx, CGF.getPointerSize());
      llvm::Value *Ptr = CGF.Builder.CreateLoad(Elem);
      const VariableArrayType *VLA =
          CGF.getContext().getAsVariableArrayType(PrivTy);
      const auto *OVE = cast<OpaqueValueExpr>(VLA->getSizeExpr());
      CodeGenFunction::OpaqueValueMapping OpaqueMap(
          CGF, OVE, RValue::get(CGF.Builder.CreatePtrToInt(Ptr, CGF.SizeTy)));
      CGF.EmitVariablyModifiedType(PrivTy);
    }
  }
  Scope.Privatize();
  IPriv = Privates.begin();
  auto ILHS = LHSExprs.begin();
  auto IRHS = RHSExprs.begin();
  for (const Expr *E : ReductionOps) {
    if ((*IPriv)->getType()->isArrayType()) {
      // Emit reduction for array section.
      const auto *LHSVar = cast<VarDecl>(cast<DeclRefExpr>(*ILHS)->getDecl());
      const auto *RHSVar = cast<VarDecl>(cast<DeclRefExpr>(*IRHS)->getDecl());
      EmitOMPAggregateReduction(
          CGF, (*IPriv)->getType(), LHSVar, RHSVar,
          [=](CodeGenFunction &CGF, const Expr *, const Expr *, const Expr *) {
            emitReductionCombiner(CGF, E);
          });
    } else {
      // Emit reduction for array subscript or single variable.
      emitReductionCombiner(CGF, E);
    }
    ++IPriv;
    ++ILHS;
    ++IRHS;
  }
  Scope.ForceCleanup();
  CGF.FinishFunction();
  return Fn;
}

void CGOpenMPRuntime::emitSingleReductionCombiner(CodeGenFunction &CGF,
                                                  const Expr *ReductionOp,
                                                  const Expr *PrivateRef,
                                                  const DeclRefExpr *LHS,
                                                  const DeclRefExpr *RHS) {
  if (PrivateRef->getType()->isArrayType()) {
    // Emit reduction for array section.
    const auto *LHSVar = cast<VarDecl>(LHS->getDecl());
    const auto *RHSVar = cast<VarDecl>(RHS->getDecl());
    EmitOMPAggregateReduction(
        CGF, PrivateRef->getType(), LHSVar, RHSVar,
        [=](CodeGenFunction &CGF, const Expr *, const Expr *, const Expr *) {
          emitReductionCombiner(CGF, ReductionOp);
        });
  } else {
    // Emit reduction for array subscript or single variable.
    emitReductionCombiner(CGF, ReductionOp);
  }
}

void CGOpenMPRuntime::emitReduction(CodeGenFunction &CGF, SourceLocation Loc,
                                    ArrayRef<const Expr *> Privates,
                                    ArrayRef<const Expr *> LHSExprs,
                                    ArrayRef<const Expr *> RHSExprs,
                                    ArrayRef<const Expr *> ReductionOps,
                                    ReductionOptionsTy Options) {
  if (!CGF.HaveInsertPoint())
    return;

  bool WithNowait = Options.WithNowait;
  bool SimpleReduction = Options.SimpleReduction;

  // Next code should be emitted for reduction:
  //
  // static kmp_critical_name lock = { 0 };
  //
  // void reduce_func(void *lhs[<n>], void *rhs[<n>]) {
  //  *(Type0*)lhs[0] = ReductionOperation0(*(Type0*)lhs[0], *(Type0*)rhs[0]);
  //  ...
  //  *(Type<n>-1*)lhs[<n>-1] = ReductionOperation<n>-1(*(Type<n>-1*)lhs[<n>-1],
  //  *(Type<n>-1*)rhs[<n>-1]);
  // }
  //
  // ...
  // void *RedList[<n>] = {&<RHSExprs>[0], ..., &<RHSExprs>[<n>-1]};
  // switch (__kmpc_reduce{_nowait}(<loc>, <gtid>, <n>, sizeof(RedList),
  // RedList, reduce_func, &<lock>)) {
  // case 1:
  //  ...
  //  <LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]);
  //  ...
  // __kmpc_end_reduce{_nowait}(<loc>, <gtid>, &<lock>);
  // break;
  // case 2:
  //  ...
  //  Atomic(<LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]));
  //  ...
  // [__kmpc_end_reduce(<loc>, <gtid>, &<lock>);]
  // break;
  // default:;
  // }
  //
  // if SimpleReduction is true, only the next code is generated:
  //  ...
  //  <LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]);
  //  ...

  ASTContext &C = CGM.getContext();

  if (SimpleReduction) {
    CodeGenFunction::RunCleanupsScope Scope(CGF);
    auto IPriv = Privates.begin();
    auto ILHS = LHSExprs.begin();
    auto IRHS = RHSExprs.begin();
    for (const Expr *E : ReductionOps) {
      emitSingleReductionCombiner(CGF, E, *IPriv, cast<DeclRefExpr>(*ILHS),
                                  cast<DeclRefExpr>(*IRHS));
      ++IPriv;
      ++ILHS;
      ++IRHS;
    }
    return;
  }

  // 1. Build a list of reduction variables.
  // void *RedList[<n>] = {<ReductionVars>[0], ..., <ReductionVars>[<n>-1]};
  auto Size = RHSExprs.size();
  for (const Expr *E : Privates) {
    if (E->getType()->isVariablyModifiedType())
      // Reserve place for array size.
      ++Size;
  }
  llvm::APInt ArraySize(/*unsigned int numBits=*/32, Size);
  QualType ReductionArrayTy =
      C.getConstantArrayType(C.VoidPtrTy, ArraySize, ArrayType::Normal,
                             /*IndexTypeQuals=*/0);
  Address ReductionList =
      CGF.CreateMemTemp(ReductionArrayTy, ".omp.reduction.red_list");
  auto IPriv = Privates.begin();
  unsigned Idx = 0;
  for (unsigned I = 0, E = RHSExprs.size(); I < E; ++I, ++IPriv, ++Idx) {
    Address Elem =
      CGF.Builder.CreateConstArrayGEP(ReductionList, Idx, CGF.getPointerSize());
    CGF.Builder.CreateStore(
        CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
            CGF.EmitLValue(RHSExprs[I]).getPointer(), CGF.VoidPtrTy),
        Elem);
    if ((*IPriv)->getType()->isVariablyModifiedType()) {
      // Store array size.
      ++Idx;
      Elem = CGF.Builder.CreateConstArrayGEP(ReductionList, Idx,
                                             CGF.getPointerSize());
      llvm::Value *Size = CGF.Builder.CreateIntCast(
          CGF.getVLASize(
                 CGF.getContext().getAsVariableArrayType((*IPriv)->getType()))
              .NumElts,
          CGF.SizeTy, /*isSigned=*/false);
      CGF.Builder.CreateStore(CGF.Builder.CreateIntToPtr(Size, CGF.VoidPtrTy),
                              Elem);
    }
  }

  // 2. Emit reduce_func().
  llvm::Value *ReductionFn = emitReductionFunction(
      CGM, Loc, CGF.ConvertTypeForMem(ReductionArrayTy)->getPointerTo(),
      Privates, LHSExprs, RHSExprs, ReductionOps);

  // 3. Create static kmp_critical_name lock = { 0 };
  std::string Name = getName({"reduction"});
  llvm::Value *Lock = getCriticalRegionLock(Name);

  // 4. Build res = __kmpc_reduce{_nowait}(<loc>, <gtid>, <n>, sizeof(RedList),
  // RedList, reduce_func, &<lock>);
  llvm::Value *IdentTLoc = emitUpdateLocation(CGF, Loc, OMP_ATOMIC_REDUCE);
  llvm::Value *ThreadId = getThreadID(CGF, Loc);
  llvm::Value *ReductionArrayTySize = CGF.getTypeSize(ReductionArrayTy);
  llvm::Value *RL = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
      ReductionList.getPointer(), CGF.VoidPtrTy);
  llvm::Value *Args[] = {
      IdentTLoc,                             // ident_t *<loc>
      ThreadId,                              // i32 <gtid>
      CGF.Builder.getInt32(RHSExprs.size()), // i32 <n>
      ReductionArrayTySize,                  // size_type sizeof(RedList)
      RL,                                    // void *RedList
      ReductionFn, // void (*) (void *, void *) <reduce_func>
      Lock         // kmp_critical_name *&<lock>
  };
  llvm::Value *Res = CGF.EmitRuntimeCall(
      createRuntimeFunction(WithNowait ? OMPRTL__kmpc_reduce_nowait
                                       : OMPRTL__kmpc_reduce),
      Args);

  // 5. Build switch(res)
  llvm::BasicBlock *DefaultBB = CGF.createBasicBlock(".omp.reduction.default");
  llvm::SwitchInst *SwInst =
      CGF.Builder.CreateSwitch(Res, DefaultBB, /*NumCases=*/2);

  // 6. Build case 1:
  //  ...
  //  <LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]);
  //  ...
  // __kmpc_end_reduce{_nowait}(<loc>, <gtid>, &<lock>);
  // break;
  llvm::BasicBlock *Case1BB = CGF.createBasicBlock(".omp.reduction.case1");
  SwInst->addCase(CGF.Builder.getInt32(1), Case1BB);
  CGF.EmitBlock(Case1BB);

  // Add emission of __kmpc_end_reduce{_nowait}(<loc>, <gtid>, &<lock>);
  llvm::Value *EndArgs[] = {
      IdentTLoc, // ident_t *<loc>
      ThreadId,  // i32 <gtid>
      Lock       // kmp_critical_name *&<lock>
  };
  auto &&CodeGen = [Privates, LHSExprs, RHSExprs, ReductionOps](
                       CodeGenFunction &CGF, PrePostActionTy &Action) {
    CGOpenMPRuntime &RT = CGF.CGM.getOpenMPRuntime();
    auto IPriv = Privates.begin();
    auto ILHS = LHSExprs.begin();
    auto IRHS = RHSExprs.begin();
    for (const Expr *E : ReductionOps) {
      RT.emitSingleReductionCombiner(CGF, E, *IPriv, cast<DeclRefExpr>(*ILHS),
                                     cast<DeclRefExpr>(*IRHS));
      ++IPriv;
      ++ILHS;
      ++IRHS;
    }
  };
  RegionCodeGenTy RCG(CodeGen);
  CommonActionTy Action(
      nullptr, llvm::None,
      createRuntimeFunction(WithNowait ? OMPRTL__kmpc_end_reduce_nowait
                                       : OMPRTL__kmpc_end_reduce),
      EndArgs);
  RCG.setAction(Action);
  RCG(CGF);

  CGF.EmitBranch(DefaultBB);

  // 7. Build case 2:
  //  ...
  //  Atomic(<LHSExprs>[i] = RedOp<i>(*<LHSExprs>[i], *<RHSExprs>[i]));
  //  ...
  // break;
  llvm::BasicBlock *Case2BB = CGF.createBasicBlock(".omp.reduction.case2");
  SwInst->addCase(CGF.Builder.getInt32(2), Case2BB);
  CGF.EmitBlock(Case2BB);

  auto &&AtomicCodeGen = [Loc, Privates, LHSExprs, RHSExprs, ReductionOps](
                             CodeGenFunction &CGF, PrePostActionTy &Action) {
    auto ILHS = LHSExprs.begin();
    auto IRHS = RHSExprs.begin();
    auto IPriv = Privates.begin();
    for (const Expr *E : ReductionOps) {
      const Expr *XExpr = nullptr;
      const Expr *EExpr = nullptr;
      const Expr *UpExpr = nullptr;
      BinaryOperatorKind BO = BO_Comma;
      if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
        if (BO->getOpcode() == BO_Assign) {
          XExpr = BO->getLHS();
          UpExpr = BO->getRHS();
        }
      }
      // Try to emit update expression as a simple atomic.
      const Expr *RHSExpr = UpExpr;
      if (RHSExpr) {
        // Analyze RHS part of the whole expression.
        if (const auto *ACO = dyn_cast<AbstractConditionalOperator>(
                RHSExpr->IgnoreParenImpCasts())) {
          // If this is a conditional operator, analyze its condition for
          // min/max reduction operator.
          RHSExpr = ACO->getCond();
        }
        if (const auto *BORHS =
                dyn_cast<BinaryOperator>(RHSExpr->IgnoreParenImpCasts())) {
          EExpr = BORHS->getRHS();
          BO = BORHS->getOpcode();
        }
      }
      if (XExpr) {
        const auto *VD = cast<VarDecl>(cast<DeclRefExpr>(*ILHS)->getDecl());
        auto &&AtomicRedGen = [BO, VD,
                               Loc](CodeGenFunction &CGF, const Expr *XExpr,
                                    const Expr *EExpr, const Expr *UpExpr) {
          LValue X = CGF.EmitLValue(XExpr);
          RValue E;
          if (EExpr)
            E = CGF.EmitAnyExpr(EExpr);
          CGF.EmitOMPAtomicSimpleUpdateExpr(
              X, E, BO, /*IsXLHSInRHSPart=*/true,
              llvm::AtomicOrdering::Monotonic, Loc,
              [&CGF, UpExpr, VD, Loc](RValue XRValue) {
                CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
                PrivateScope.addPrivate(
                    VD, [&CGF, VD, XRValue, Loc]() {
                      Address LHSTemp = CGF.CreateMemTemp(VD->getType());
                      CGF.emitOMPSimpleStore(
                          CGF.MakeAddrLValue(LHSTemp, VD->getType()), XRValue,
                          VD->getType().getNonReferenceType(), Loc);
                      return LHSTemp;
                    });
                (void)PrivateScope.Privatize();
                return CGF.EmitAnyExpr(UpExpr);
              });
        };
        if ((*IPriv)->getType()->isArrayType()) {
          // Emit atomic reduction for array section.
          const auto *RHSVar =
              cast<VarDecl>(cast<DeclRefExpr>(*IRHS)->getDecl());
          EmitOMPAggregateReduction(CGF, (*IPriv)->getType(), VD, RHSVar,
                                    AtomicRedGen, XExpr, EExpr, UpExpr);
        } else {
          // Emit atomic reduction for array subscript or single variable.
          AtomicRedGen(CGF, XExpr, EExpr, UpExpr);
        }
      } else {
        // Emit as a critical region.
        auto &&CritRedGen = [E, Loc](CodeGenFunction &CGF, const Expr *,
                                           const Expr *, const Expr *) {
          CGOpenMPRuntime &RT = CGF.CGM.getOpenMPRuntime();
          std::string Name = RT.getName({"atomic_reduction"});
          RT.emitCriticalRegion(
              CGF, Name,
              [=](CodeGenFunction &CGF, PrePostActionTy &Action) {
                Action.Enter(CGF);
                emitReductionCombiner(CGF, E);
              },
              Loc);
        };
        if ((*IPriv)->getType()->isArrayType()) {
          const auto *LHSVar =
              cast<VarDecl>(cast<DeclRefExpr>(*ILHS)->getDecl());
          const auto *RHSVar =
              cast<VarDecl>(cast<DeclRefExpr>(*IRHS)->getDecl());
          EmitOMPAggregateReduction(CGF, (*IPriv)->getType(), LHSVar, RHSVar,
                                    CritRedGen);
        } else {
          CritRedGen(CGF, nullptr, nullptr, nullptr);
        }
      }
      ++ILHS;
      ++IRHS;
      ++IPriv;
    }
  };
  RegionCodeGenTy AtomicRCG(AtomicCodeGen);
  if (!WithNowait) {
    // Add emission of __kmpc_end_reduce(<loc>, <gtid>, &<lock>);
    llvm::Value *EndArgs[] = {
        IdentTLoc, // ident_t *<loc>
        ThreadId,  // i32 <gtid>
        Lock       // kmp_critical_name *&<lock>
    };
    CommonActionTy Action(nullptr, llvm::None,
                          createRuntimeFunction(OMPRTL__kmpc_end_reduce),
                          EndArgs);
    AtomicRCG.setAction(Action);
    AtomicRCG(CGF);
  } else {
    AtomicRCG(CGF);
  }

  CGF.EmitBranch(DefaultBB);
  CGF.EmitBlock(DefaultBB, /*IsFinished=*/true);
}

/// Generates unique name for artificial threadprivate variables.
/// Format is: <Prefix> "." <Decl_mangled_name> "_" "<Decl_start_loc_raw_enc>"
static std::string generateUniqueName(CodeGenModule &CGM, StringRef Prefix,
                                      const Expr *Ref) {
  SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  const clang::DeclRefExpr *DE;
  const VarDecl *D = ::getBaseDecl(Ref, DE);
  if (!D)
    D = cast<VarDecl>(cast<DeclRefExpr>(Ref)->getDecl());
  D = D->getCanonicalDecl();
  std::string Name = CGM.getOpenMPRuntime().getName(
      {D->isLocalVarDeclOrParm() ? D->getName() : CGM.getMangledName(D)});
  Out << Prefix << Name << "_"
      << D->getCanonicalDecl()->getBeginLoc().getRawEncoding();
  return Out.str();
}

/// Emits reduction initializer function:
/// \code
/// void @.red_init(void* %arg) {
/// %0 = bitcast void* %arg to <type>*
/// store <type> <init>, <type>* %0
/// ret void
/// }
/// \endcode
static llvm::Value *emitReduceInitFunction(CodeGenModule &CGM,
                                           SourceLocation Loc,
                                           ReductionCodeGen &RCG, unsigned N) {
  ASTContext &C = CGM.getContext();
  FunctionArgList Args;
  ImplicitParamDecl Param(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                          ImplicitParamDecl::Other);
  Args.emplace_back(&Param);
  const auto &FnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *FnTy = CGM.getTypes().GetFunctionType(FnInfo);
  std::string Name = CGM.getOpenMPRuntime().getName({"red_init", ""});
  auto *Fn = llvm::Function::Create(FnTy, llvm::GlobalValue::InternalLinkage,
                                    Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, FnInfo);
  Fn->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, FnInfo, Args, Loc, Loc);
  Address PrivateAddr = CGF.EmitLoadOfPointer(
      CGF.GetAddrOfLocalVar(&Param),
      C.getPointerType(C.VoidPtrTy).castAs<PointerType>());
  llvm::Value *Size = nullptr;
  // If the size of the reduction item is non-constant, load it from global
  // threadprivate variable.
  if (RCG.getSizes(N).second) {
    Address SizeAddr = CGM.getOpenMPRuntime().getAddrOfArtificialThreadPrivate(
        CGF, CGM.getContext().getSizeType(),
        generateUniqueName(CGM, "reduction_size", RCG.getRefExpr(N)));
    Size = CGF.EmitLoadOfScalar(SizeAddr, /*Volatile=*/false,
                                CGM.getContext().getSizeType(), Loc);
  }
  RCG.emitAggregateType(CGF, N, Size);
  LValue SharedLVal;
  // If initializer uses initializer from declare reduction construct, emit a
  // pointer to the address of the original reduction item (reuired by reduction
  // initializer)
  if (RCG.usesReductionInitializer(N)) {
    Address SharedAddr =
        CGM.getOpenMPRuntime().getAddrOfArtificialThreadPrivate(
            CGF, CGM.getContext().VoidPtrTy,
            generateUniqueName(CGM, "reduction", RCG.getRefExpr(N)));
    SharedAddr = CGF.EmitLoadOfPointer(
        SharedAddr,
        CGM.getContext().VoidPtrTy.castAs<PointerType>()->getTypePtr());
    SharedLVal = CGF.MakeAddrLValue(SharedAddr, CGM.getContext().VoidPtrTy);
  } else {
    SharedLVal = CGF.MakeNaturalAlignAddrLValue(
        llvm::ConstantPointerNull::get(CGM.VoidPtrTy),
        CGM.getContext().VoidPtrTy);
  }
  // Emit the initializer:
  // %0 = bitcast void* %arg to <type>*
  // store <type> <init>, <type>* %0
  RCG.emitInitialization(CGF, N, PrivateAddr, SharedLVal,
                         [](CodeGenFunction &) { return false; });
  CGF.FinishFunction();
  return Fn;
}

/// Emits reduction combiner function:
/// \code
/// void @.red_comb(void* %arg0, void* %arg1) {
/// %lhs = bitcast void* %arg0 to <type>*
/// %rhs = bitcast void* %arg1 to <type>*
/// %2 = <ReductionOp>(<type>* %lhs, <type>* %rhs)
/// store <type> %2, <type>* %lhs
/// ret void
/// }
/// \endcode
static llvm::Value *emitReduceCombFunction(CodeGenModule &CGM,
                                           SourceLocation Loc,
                                           ReductionCodeGen &RCG, unsigned N,
                                           const Expr *ReductionOp,
                                           const Expr *LHS, const Expr *RHS,
                                           const Expr *PrivateRef) {
  ASTContext &C = CGM.getContext();
  const auto *LHSVD = cast<VarDecl>(cast<DeclRefExpr>(LHS)->getDecl());
  const auto *RHSVD = cast<VarDecl>(cast<DeclRefExpr>(RHS)->getDecl());
  FunctionArgList Args;
  ImplicitParamDecl ParamInOut(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr,
                               C.VoidPtrTy, ImplicitParamDecl::Other);
  ImplicitParamDecl ParamIn(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                            ImplicitParamDecl::Other);
  Args.emplace_back(&ParamInOut);
  Args.emplace_back(&ParamIn);
  const auto &FnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *FnTy = CGM.getTypes().GetFunctionType(FnInfo);
  std::string Name = CGM.getOpenMPRuntime().getName({"red_comb", ""});
  auto *Fn = llvm::Function::Create(FnTy, llvm::GlobalValue::InternalLinkage,
                                    Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, FnInfo);
  Fn->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, FnInfo, Args, Loc, Loc);
  llvm::Value *Size = nullptr;
  // If the size of the reduction item is non-constant, load it from global
  // threadprivate variable.
  if (RCG.getSizes(N).second) {
    Address SizeAddr = CGM.getOpenMPRuntime().getAddrOfArtificialThreadPrivate(
        CGF, CGM.getContext().getSizeType(),
        generateUniqueName(CGM, "reduction_size", RCG.getRefExpr(N)));
    Size = CGF.EmitLoadOfScalar(SizeAddr, /*Volatile=*/false,
                                CGM.getContext().getSizeType(), Loc);
  }
  RCG.emitAggregateType(CGF, N, Size);
  // Remap lhs and rhs variables to the addresses of the function arguments.
  // %lhs = bitcast void* %arg0 to <type>*
  // %rhs = bitcast void* %arg1 to <type>*
  CodeGenFunction::OMPPrivateScope PrivateScope(CGF);
  PrivateScope.addPrivate(LHSVD, [&C, &CGF, &ParamInOut, LHSVD]() {
    // Pull out the pointer to the variable.
    Address PtrAddr = CGF.EmitLoadOfPointer(
        CGF.GetAddrOfLocalVar(&ParamInOut),
        C.getPointerType(C.VoidPtrTy).castAs<PointerType>());
    return CGF.Builder.CreateElementBitCast(
        PtrAddr, CGF.ConvertTypeForMem(LHSVD->getType()));
  });
  PrivateScope.addPrivate(RHSVD, [&C, &CGF, &ParamIn, RHSVD]() {
    // Pull out the pointer to the variable.
    Address PtrAddr = CGF.EmitLoadOfPointer(
        CGF.GetAddrOfLocalVar(&ParamIn),
        C.getPointerType(C.VoidPtrTy).castAs<PointerType>());
    return CGF.Builder.CreateElementBitCast(
        PtrAddr, CGF.ConvertTypeForMem(RHSVD->getType()));
  });
  PrivateScope.Privatize();
  // Emit the combiner body:
  // %2 = <ReductionOp>(<type> *%lhs, <type> *%rhs)
  // store <type> %2, <type>* %lhs
  CGM.getOpenMPRuntime().emitSingleReductionCombiner(
      CGF, ReductionOp, PrivateRef, cast<DeclRefExpr>(LHS),
      cast<DeclRefExpr>(RHS));
  CGF.FinishFunction();
  return Fn;
}

/// Emits reduction finalizer function:
/// \code
/// void @.red_fini(void* %arg) {
/// %0 = bitcast void* %arg to <type>*
/// <destroy>(<type>* %0)
/// ret void
/// }
/// \endcode
static llvm::Value *emitReduceFiniFunction(CodeGenModule &CGM,
                                           SourceLocation Loc,
                                           ReductionCodeGen &RCG, unsigned N) {
  if (!RCG.needCleanups(N))
    return nullptr;
  ASTContext &C = CGM.getContext();
  FunctionArgList Args;
  ImplicitParamDecl Param(C, /*DC=*/nullptr, Loc, /*Id=*/nullptr, C.VoidPtrTy,
                          ImplicitParamDecl::Other);
  Args.emplace_back(&Param);
  const auto &FnInfo =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, Args);
  llvm::FunctionType *FnTy = CGM.getTypes().GetFunctionType(FnInfo);
  std::string Name = CGM.getOpenMPRuntime().getName({"red_fini", ""});
  auto *Fn = llvm::Function::Create(FnTy, llvm::GlobalValue::InternalLinkage,
                                    Name, &CGM.getModule());
  CGM.SetInternalFunctionAttributes(GlobalDecl(), Fn, FnInfo);
  Fn->setDoesNotRecurse();
  CodeGenFunction CGF(CGM);
  CGF.StartFunction(GlobalDecl(), C.VoidTy, Fn, FnInfo, Args, Loc, Loc);
  Address PrivateAddr = CGF.EmitLoadOfPointer(
      CGF.GetAddrOfLocalVar(&Param),
      C.getPointerType(C.VoidPtrTy).castAs<PointerType>());
  llvm::Value *Size = nullptr;
  // If the size of the reduction item is non-constant, load it from global
  // threadprivate variable.
  if (RCG.getSizes(N).second) {
    Address SizeAddr = CGM.getOpenMPRuntime().getAddrOfArtificialThreadPrivate(
        CGF, CGM.getContext().getSizeType(),
        generateUniqueName(CGM, "reduction_size", RCG.getRefExpr(N)));
    Size = CGF.EmitLoadOfScalar(SizeAddr, /*Volatile=*/false,
                                CGM.getContext().getSizeType(), Loc);
  }
  RCG.emitAggregateType(CGF, N, Size);
  // Emit the finalizer body:
  // <destroy>(<type>* %0)
  RCG.emitCleanups(CGF, N, PrivateAddr);
  CGF.FinishFunction();
  return Fn;
}

llvm::Value *CGOpenMPRuntime::emitTaskReductionInit(
    CodeGenFunction &CGF, SourceLocation Loc, ArrayRef<const Expr *> LHSExprs,
    ArrayRef<const Expr *> RHSExprs, const OMPTaskDataTy &Data) {
  if (!CGF.HaveInsertPoint() || Data.ReductionVars.empty())
    return nullptr;

  // Build typedef struct:
  // kmp_task_red_input {
  //   void *reduce_shar; // shared reduction item
  //   size_t reduce_size; // size of data item
  //   void *reduce_init; // data initialization routine
  //   void *reduce_fini; // data finalization routine
  //   void *reduce_comb; // data combiner routine
  //   kmp_task_red_flags_t flags; // flags for additional info from compiler
  // } kmp_task_red_input_t;
  ASTContext &C = CGM.getContext();
  RecordDecl *RD = C.buildImplicitRecord("kmp_task_red_input_t");
  RD->startDefinition();
  const FieldDecl *SharedFD = addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  const FieldDecl *SizeFD = addFieldToRecordDecl(C, RD, C.getSizeType());
  const FieldDecl *InitFD  = addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  const FieldDecl *FiniFD = addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  const FieldDecl *CombFD = addFieldToRecordDecl(C, RD, C.VoidPtrTy);
  const FieldDecl *FlagsFD = addFieldToRecordDecl(
      C, RD, C.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/false));
  RD->completeDefinition();
  QualType RDType = C.getRecordType(RD);
  unsigned Size = Data.ReductionVars.size();
  llvm::APInt ArraySize(/*numBits=*/64, Size);
  QualType ArrayRDType = C.getConstantArrayType(
      RDType, ArraySize, ArrayType::Normal, /*IndexTypeQuals=*/0);
  // kmp_task_red_input_t .rd_input.[Size];
  Address TaskRedInput = CGF.CreateMemTemp(ArrayRDType, ".rd_input.");
  ReductionCodeGen RCG(Data.ReductionVars, Data.ReductionCopies,
                       Data.ReductionOps);
  for (unsigned Cnt = 0; Cnt < Size; ++Cnt) {
    // kmp_task_red_input_t &ElemLVal = .rd_input.[Cnt];
    llvm::Value *Idxs[] = {llvm::ConstantInt::get(CGM.SizeTy, /*V=*/0),
                           llvm::ConstantInt::get(CGM.SizeTy, Cnt)};
    llvm::Value *GEP = CGF.EmitCheckedInBoundsGEP(
        TaskRedInput.getPointer(), Idxs,
        /*SignedIndices=*/false, /*IsSubtraction=*/false, Loc,
        ".rd_input.gep.");
    LValue ElemLVal = CGF.MakeNaturalAlignAddrLValue(GEP, RDType);
    // ElemLVal.reduce_shar = &Shareds[Cnt];
    LValue SharedLVal = CGF.EmitLValueForField(ElemLVal, SharedFD);
    RCG.emitSharedLValue(CGF, Cnt);
    llvm::Value *CastedShared =
        CGF.EmitCastToVoidPtr(RCG.getSharedLValue(Cnt).getPointer());
    CGF.EmitStoreOfScalar(CastedShared, SharedLVal);
    RCG.emitAggregateType(CGF, Cnt);
    llvm::Value *SizeValInChars;
    llvm::Value *SizeVal;
    std::tie(SizeValInChars, SizeVal) = RCG.getSizes(Cnt);
    // We use delayed creation/initialization for VLAs, array sections and
    // custom reduction initializations. It is required because runtime does not
    // provide the way to pass the sizes of VLAs/array sections to
    // initializer/combiner/finalizer functions and does not pass the pointer to
    // original reduction item to the initializer. Instead threadprivate global
    // variables are used to store these values and use them in the functions.
    bool DelayedCreation = !!SizeVal;
    SizeValInChars = CGF.Builder.CreateIntCast(SizeValInChars, CGM.SizeTy,
                                               /*isSigned=*/false);
    LValue SizeLVal = CGF.EmitLValueForField(ElemLVal, SizeFD);
    CGF.EmitStoreOfScalar(SizeValInChars, SizeLVal);
    // ElemLVal.reduce_init = init;
    LValue InitLVal = CGF.EmitLValueForField(ElemLVal, InitFD);
    llvm::Value *InitAddr =
        CGF.EmitCastToVoidPtr(emitReduceInitFunction(CGM, Loc, RCG, Cnt));
    CGF.EmitStoreOfScalar(InitAddr, InitLVal);
    DelayedCreation = DelayedCreation || RCG.usesReductionInitializer(Cnt);
    // ElemLVal.reduce_fini = fini;
    LValue FiniLVal = CGF.EmitLValueForField(ElemLVal, FiniFD);
    llvm::Value *Fini = emitReduceFiniFunction(CGM, Loc, RCG, Cnt);
    llvm::Value *FiniAddr = Fini
                                ? CGF.EmitCastToVoidPtr(Fini)
                                : llvm::ConstantPointerNull::get(CGM.VoidPtrTy);
    CGF.EmitStoreOfScalar(FiniAddr, FiniLVal);
    // ElemLVal.reduce_comb = comb;
    LValue CombLVal = CGF.EmitLValueForField(ElemLVal, CombFD);
    llvm::Value *CombAddr = CGF.EmitCastToVoidPtr(emitReduceCombFunction(
        CGM, Loc, RCG, Cnt, Data.ReductionOps[Cnt], LHSExprs[Cnt],
        RHSExprs[Cnt], Data.ReductionCopies[Cnt]));
    CGF.EmitStoreOfScalar(CombAddr, CombLVal);
    // ElemLVal.flags = 0;
    LValue FlagsLVal = CGF.EmitLValueForField(ElemLVal, FlagsFD);
    if (DelayedCreation) {
      CGF.EmitStoreOfScalar(
          llvm::ConstantInt::get(CGM.Int32Ty, /*V=*/1, /*IsSigned=*/true),
          FlagsLVal);
    } else
      CGF.EmitNullInitialization(FlagsLVal.getAddress(), FlagsLVal.getType());
  }
  // Build call void *__kmpc_task_reduction_init(int gtid, int num_data, void
  // *data);
  llvm::Value *Args[] = {
      CGF.Builder.CreateIntCast(getThreadID(CGF, Loc), CGM.IntTy,
                                /*isSigned=*/true),
      llvm::ConstantInt::get(CGM.IntTy, Size, /*isSigned=*/true),
      CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(TaskRedInput.getPointer(),
                                                      CGM.VoidPtrTy)};
  return CGF.EmitRuntimeCall(
      createRuntimeFunction(OMPRTL__kmpc_task_reduction_init), Args);
}

void CGOpenMPRuntime::emitTaskReductionFixups(CodeGenFunction &CGF,
                                              SourceLocation Loc,
                                              ReductionCodeGen &RCG,
                                              unsigned N) {
  auto Sizes = RCG.getSizes(N);
  // Emit threadprivate global variable if the type is non-constant
  // (Sizes.second = nullptr).
  if (Sizes.second) {
    llvm::Value *SizeVal = CGF.Builder.CreateIntCast(Sizes.second, CGM.SizeTy,
                                                     /*isSigned=*/false);
    Address SizeAddr = getAddrOfArtificialThreadPrivate(
        CGF, CGM.getContext().getSizeType(),
        generateUniqueName(CGM, "reduction_size", RCG.getRefExpr(N)));
    CGF.Builder.CreateStore(SizeVal, SizeAddr, /*IsVolatile=*/false);
  }
  // Store address of the original reduction item if custom initializer is used.
  if (RCG.usesReductionInitializer(N)) {
    Address SharedAddr = getAddrOfArtificialThreadPrivate(
        CGF, CGM.getContext().VoidPtrTy,
        generateUniqueName(CGM, "reduction", RCG.getRefExpr(N)));
    CGF.Builder.CreateStore(
        CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
            RCG.getSharedLValue(N).getPointer(), CGM.VoidPtrTy),
        SharedAddr, /*IsVolatile=*/false);
  }
}

Address CGOpenMPRuntime::getTaskReductionItem(CodeGenFunction &CGF,
                                              SourceLocation Loc,
                                              llvm::Value *ReductionsPtr,
                                              LValue SharedLVal) {
  // Build call void *__kmpc_task_reduction_get_th_data(int gtid, void *tg, void
  // *d);
  llvm::Value *Args[] = {
      CGF.Builder.CreateIntCast(getThreadID(CGF, Loc), CGM.IntTy,
                                /*isSigned=*/true),
      ReductionsPtr,
      CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(SharedLVal.getPointer(),
                                                      CGM.VoidPtrTy)};
  return Address(
      CGF.EmitRuntimeCall(
          createRuntimeFunction(OMPRTL__kmpc_task_reduction_get_th_data), Args),
      SharedLVal.getAlignment());
}

void CGOpenMPRuntime::emitTaskwaitCall(CodeGenFunction &CGF,
                                       SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;
  // Build call kmp_int32 __kmpc_omp_taskwait(ident_t *loc, kmp_int32
  // global_tid);
  llvm::Value *Args[] = {emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc)};
  // Ignore return result until untied tasks are supported.
  CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_omp_taskwait), Args);
  if (auto *Region = dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo))
    Region->emitUntiedSwitch(CGF);
}

void CGOpenMPRuntime::emitInlinedDirective(CodeGenFunction &CGF,
                                           OpenMPDirectiveKind InnerKind,
                                           const RegionCodeGenTy &CodeGen,
                                           bool HasCancel) {
  if (!CGF.HaveInsertPoint())
    return;
  InlinedOpenMPRegionRAII Region(CGF, CodeGen, InnerKind, HasCancel);
  CGF.CapturedStmtInfo->EmitBody(CGF, /*S=*/nullptr);
}

namespace {
enum RTCancelKind {
  CancelNoreq = 0,
  CancelParallel = 1,
  CancelLoop = 2,
  CancelSections = 3,
  CancelTaskgroup = 4
};
} // anonymous namespace

static RTCancelKind getCancellationKind(OpenMPDirectiveKind CancelRegion) {
  RTCancelKind CancelKind = CancelNoreq;
  if (CancelRegion == OMPD_parallel)
    CancelKind = CancelParallel;
  else if (CancelRegion == OMPD_for)
    CancelKind = CancelLoop;
  else if (CancelRegion == OMPD_sections)
    CancelKind = CancelSections;
  else {
    assert(CancelRegion == OMPD_taskgroup);
    CancelKind = CancelTaskgroup;
  }
  return CancelKind;
}

void CGOpenMPRuntime::emitCancellationPointCall(
    CodeGenFunction &CGF, SourceLocation Loc,
    OpenMPDirectiveKind CancelRegion) {
  if (!CGF.HaveInsertPoint())
    return;
  // Build call kmp_int32 __kmpc_cancellationpoint(ident_t *loc, kmp_int32
  // global_tid, kmp_int32 cncl_kind);
  if (auto *OMPRegionInfo =
          dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo)) {
    // For 'cancellation point taskgroup', the task region info may not have a
    // cancel. This may instead happen in another adjacent task.
    if (CancelRegion == OMPD_taskgroup || OMPRegionInfo->hasCancel()) {
      llvm::Value *Args[] = {
          emitUpdateLocation(CGF, Loc), getThreadID(CGF, Loc),
          CGF.Builder.getInt32(getCancellationKind(CancelRegion))};
      // Ignore return result until untied tasks are supported.
      llvm::Value *Result = CGF.EmitRuntimeCall(
          createRuntimeFunction(OMPRTL__kmpc_cancellationpoint), Args);
      // if (__kmpc_cancellationpoint()) {
      //   exit from construct;
      // }
      llvm::BasicBlock *ExitBB = CGF.createBasicBlock(".cancel.exit");
      llvm::BasicBlock *ContBB = CGF.createBasicBlock(".cancel.continue");
      llvm::Value *Cmp = CGF.Builder.CreateIsNotNull(Result);
      CGF.Builder.CreateCondBr(Cmp, ExitBB, ContBB);
      CGF.EmitBlock(ExitBB);
      // exit from construct;
      CodeGenFunction::JumpDest CancelDest =
          CGF.getOMPCancelDestination(OMPRegionInfo->getDirectiveKind());
      CGF.EmitBranchThroughCleanup(CancelDest);
      CGF.EmitBlock(ContBB, /*IsFinished=*/true);
    }
  }
}

void CGOpenMPRuntime::emitCancelCall(CodeGenFunction &CGF, SourceLocation Loc,
                                     const Expr *IfCond,
                                     OpenMPDirectiveKind CancelRegion) {
  if (!CGF.HaveInsertPoint())
    return;
  // Build call kmp_int32 __kmpc_cancel(ident_t *loc, kmp_int32 global_tid,
  // kmp_int32 cncl_kind);
  if (auto *OMPRegionInfo =
          dyn_cast_or_null<CGOpenMPRegionInfo>(CGF.CapturedStmtInfo)) {
    auto &&ThenGen = [Loc, CancelRegion, OMPRegionInfo](CodeGenFunction &CGF,
                                                        PrePostActionTy &) {
      CGOpenMPRuntime &RT = CGF.CGM.getOpenMPRuntime();
      llvm::Value *Args[] = {
          RT.emitUpdateLocation(CGF, Loc), RT.getThreadID(CGF, Loc),
          CGF.Builder.getInt32(getCancellationKind(CancelRegion))};
      // Ignore return result until untied tasks are supported.
      llvm::Value *Result = CGF.EmitRuntimeCall(
          RT.createRuntimeFunction(OMPRTL__kmpc_cancel), Args);
      // if (__kmpc_cancel()) {
      //   exit from construct;
      // }
      llvm::BasicBlock *ExitBB = CGF.createBasicBlock(".cancel.exit");
      llvm::BasicBlock *ContBB = CGF.createBasicBlock(".cancel.continue");
      llvm::Value *Cmp = CGF.Builder.CreateIsNotNull(Result);
      CGF.Builder.CreateCondBr(Cmp, ExitBB, ContBB);
      CGF.EmitBlock(ExitBB);
      // exit from construct;
      CodeGenFunction::JumpDest CancelDest =
          CGF.getOMPCancelDestination(OMPRegionInfo->getDirectiveKind());
      CGF.EmitBranchThroughCleanup(CancelDest);
      CGF.EmitBlock(ContBB, /*IsFinished=*/true);
    };
    if (IfCond) {
      emitOMPIfClause(CGF, IfCond, ThenGen,
                      [](CodeGenFunction &, PrePostActionTy &) {});
    } else {
      RegionCodeGenTy ThenRCG(ThenGen);
      ThenRCG(CGF);
    }
  }
}

void CGOpenMPRuntime::emitTargetOutlinedFunction(
    const OMPExecutableDirective &D, StringRef ParentName,
    llvm::Function *&OutlinedFn, llvm::Constant *&OutlinedFnID,
    bool IsOffloadEntry, const RegionCodeGenTy &CodeGen) {
  assert(!ParentName.empty() && "Invalid target region parent name!");
  emitTargetOutlinedFunctionHelper(D, ParentName, OutlinedFn, OutlinedFnID,
                                   IsOffloadEntry, CodeGen);
}

void CGOpenMPRuntime::emitTargetOutlinedFunctionHelper(
    const OMPExecutableDirective &D, StringRef ParentName,
    llvm::Function *&OutlinedFn, llvm::Constant *&OutlinedFnID,
    bool IsOffloadEntry, const RegionCodeGenTy &CodeGen) {
  // Create a unique name for the entry function using the source location
  // information of the current target region. The name will be something like:
  //
  // __omp_offloading_DD_FFFF_PP_lBB
  //
  // where DD_FFFF is an ID unique to the file (device and file IDs), PP is the
  // mangled name of the function that encloses the target region and BB is the
  // line number of the target region.

  unsigned DeviceID;
  unsigned FileID;
  unsigned Line;
  getTargetEntryUniqueInfo(CGM.getContext(), D.getBeginLoc(), DeviceID, FileID,
                           Line);
  SmallString<64> EntryFnName;
  {
    llvm::raw_svector_ostream OS(EntryFnName);
    OS << "__omp_offloading" << llvm::format("_%x", DeviceID)
       << llvm::format("_%x_", FileID) << ParentName << "_l" << Line;
  }

  const CapturedStmt &CS = *D.getCapturedStmt(OMPD_target);

  CodeGenFunction CGF(CGM, true);
  CGOpenMPTargetRegionInfo CGInfo(CS, CodeGen, EntryFnName);
  CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGInfo);

  OutlinedFn = CGF.GenerateOpenMPCapturedStmtFunction(CS);

  // If this target outline function is not an offload entry, we don't need to
  // register it.
  if (!IsOffloadEntry)
    return;

  // The target region ID is used by the runtime library to identify the current
  // target region, so it only has to be unique and not necessarily point to
  // anything. It could be the pointer to the outlined function that implements
  // the target region, but we aren't using that so that the compiler doesn't
  // need to keep that, and could therefore inline the host function if proven
  // worthwhile during optimization. In the other hand, if emitting code for the
  // device, the ID has to be the function address so that it can retrieved from
  // the offloading entry and launched by the runtime library. We also mark the
  // outlined function to have external linkage in case we are emitting code for
  // the device, because these functions will be entry points to the device.

  if (CGM.getLangOpts().OpenMPIsDevice) {
    OutlinedFnID = llvm::ConstantExpr::getBitCast(OutlinedFn, CGM.Int8PtrTy);
    OutlinedFn->setLinkage(llvm::GlobalValue::WeakAnyLinkage);
    OutlinedFn->setDSOLocal(false);
  } else {
    std::string Name = getName({EntryFnName, "region_id"});
    OutlinedFnID = new llvm::GlobalVariable(
        CGM.getModule(), CGM.Int8Ty, /*isConstant=*/true,
        llvm::GlobalValue::WeakAnyLinkage,
        llvm::Constant::getNullValue(CGM.Int8Ty), Name);
  }

  // Register the information for the entry associated with this target region.
  OffloadEntriesInfoManager.registerTargetRegionEntryInfo(
      DeviceID, FileID, ParentName, Line, OutlinedFn, OutlinedFnID,
      OffloadEntriesInfoManagerTy::OMPTargetRegionEntryTargetRegion);
}

/// discard all CompoundStmts intervening between two constructs
static const Stmt *ignoreCompoundStmts(const Stmt *Body) {
  while (const auto *CS = dyn_cast_or_null<CompoundStmt>(Body))
    Body = CS->body_front();

  return Body;
}

/// Emit the number of teams for a target directive.  Inspect the num_teams
/// clause associated with a teams construct combined or closely nested
/// with the target directive.
///
/// Emit a team of size one for directives such as 'target parallel' that
/// have no associated teams construct.
///
/// Otherwise, return nullptr.
static llvm::Value *
emitNumTeamsForTargetDirective(CGOpenMPRuntime &OMPRuntime,
                               CodeGenFunction &CGF,
                               const OMPExecutableDirective &D) {
  assert(!CGF.getLangOpts().OpenMPIsDevice && "Clauses associated with the "
                                              "teams directive expected to be "
                                              "emitted only for the host!");

  CGBuilderTy &Bld = CGF.Builder;

  // If the target directive is combined with a teams directive:
  //   Return the value in the num_teams clause, if any.
  //   Otherwise, return 0 to denote the runtime default.
  if (isOpenMPTeamsDirective(D.getDirectiveKind())) {
    if (const auto *NumTeamsClause = D.getSingleClause<OMPNumTeamsClause>()) {
      CodeGenFunction::RunCleanupsScope NumTeamsScope(CGF);
      llvm::Value *NumTeams = CGF.EmitScalarExpr(NumTeamsClause->getNumTeams(),
                                                 /*IgnoreResultAssign*/ true);
      return Bld.CreateIntCast(NumTeams, CGF.Int32Ty,
                               /*IsSigned=*/true);
    }

    // The default value is 0.
    return Bld.getInt32(0);
  }

  // If the target directive is combined with a parallel directive but not a
  // teams directive, start one team.
  if (isOpenMPParallelDirective(D.getDirectiveKind()))
    return Bld.getInt32(1);

  // If the current target region has a teams region enclosed, we need to get
  // the number of teams to pass to the runtime function call. This is done
  // by generating the expression in a inlined region. This is required because
  // the expression is captured in the enclosing target environment when the
  // teams directive is not combined with target.

  const CapturedStmt &CS = *D.getCapturedStmt(OMPD_target);

  if (const auto *TeamsDir = dyn_cast_or_null<OMPExecutableDirective>(
          ignoreCompoundStmts(CS.getCapturedStmt()))) {
    if (isOpenMPTeamsDirective(TeamsDir->getDirectiveKind())) {
      if (const auto *NTE = TeamsDir->getSingleClause<OMPNumTeamsClause>()) {
        CGOpenMPInnerExprInfo CGInfo(CGF, CS);
        CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGInfo);
        llvm::Value *NumTeams = CGF.EmitScalarExpr(NTE->getNumTeams());
        return Bld.CreateIntCast(NumTeams, CGF.Int32Ty,
                                 /*IsSigned=*/true);
      }

      // If we have an enclosed teams directive but no num_teams clause we use
      // the default value 0.
      return Bld.getInt32(0);
    }
  }

  // No teams associated with the directive.
  return nullptr;
}

/// Emit the number of threads for a target directive.  Inspect the
/// thread_limit clause associated with a teams construct combined or closely
/// nested with the target directive.
///
/// Emit the num_threads clause for directives such as 'target parallel' that
/// have no associated teams construct.
///
/// Otherwise, return nullptr.
static llvm::Value *
emitNumThreadsForTargetDirective(CGOpenMPRuntime &OMPRuntime,
                                 CodeGenFunction &CGF,
                                 const OMPExecutableDirective &D) {
  assert(!CGF.getLangOpts().OpenMPIsDevice && "Clauses associated with the "
                                              "teams directive expected to be "
                                              "emitted only for the host!");

  CGBuilderTy &Bld = CGF.Builder;

  //
  // If the target directive is combined with a teams directive:
  //   Return the value in the thread_limit clause, if any.
  //
  // If the target directive is combined with a parallel directive:
  //   Return the value in the num_threads clause, if any.
  //
  // If both clauses are set, select the minimum of the two.
  //
  // If neither teams or parallel combined directives set the number of threads
  // in a team, return 0 to denote the runtime default.
  //
  // If this is not a teams directive return nullptr.

  if (isOpenMPTeamsDirective(D.getDirectiveKind()) ||
      isOpenMPParallelDirective(D.getDirectiveKind())) {
    llvm::Value *DefaultThreadLimitVal = Bld.getInt32(0);
    llvm::Value *NumThreadsVal = nullptr;
    llvm::Value *ThreadLimitVal = nullptr;

    if (const auto *ThreadLimitClause =
            D.getSingleClause<OMPThreadLimitClause>()) {
      CodeGenFunction::RunCleanupsScope ThreadLimitScope(CGF);
      llvm::Value *ThreadLimit =
          CGF.EmitScalarExpr(ThreadLimitClause->getThreadLimit(),
                             /*IgnoreResultAssign*/ true);
      ThreadLimitVal = Bld.CreateIntCast(ThreadLimit, CGF.Int32Ty,
                                         /*IsSigned=*/true);
    }

    if (const auto *NumThreadsClause =
            D.getSingleClause<OMPNumThreadsClause>()) {
      CodeGenFunction::RunCleanupsScope NumThreadsScope(CGF);
      llvm::Value *NumThreads =
          CGF.EmitScalarExpr(NumThreadsClause->getNumThreads(),
                             /*IgnoreResultAssign*/ true);
      NumThreadsVal =
          Bld.CreateIntCast(NumThreads, CGF.Int32Ty, /*IsSigned=*/true);
    }

    // Select the lesser of thread_limit and num_threads.
    if (NumThreadsVal)
      ThreadLimitVal = ThreadLimitVal
                           ? Bld.CreateSelect(Bld.CreateICmpSLT(NumThreadsVal,
                                                                ThreadLimitVal),
                                              NumThreadsVal, ThreadLimitVal)
                           : NumThreadsVal;

    // Set default value passed to the runtime if either teams or a target
    // parallel type directive is found but no clause is specified.
    if (!ThreadLimitVal)
      ThreadLimitVal = DefaultThreadLimitVal;

    return ThreadLimitVal;
  }

  // If the current target region has a teams region enclosed, we need to get
  // the thread limit to pass to the runtime function call. This is done
  // by generating the expression in a inlined region. This is required because
  // the expression is captured in the enclosing target environment when the
  // teams directive is not combined with target.

  const CapturedStmt &CS = *D.getCapturedStmt(OMPD_target);

  if (const auto *TeamsDir = dyn_cast_or_null<OMPExecutableDirective>(
          ignoreCompoundStmts(CS.getCapturedStmt()))) {
    if (isOpenMPTeamsDirective(TeamsDir->getDirectiveKind())) {
      if (const auto *TLE = TeamsDir->getSingleClause<OMPThreadLimitClause>()) {
        CGOpenMPInnerExprInfo CGInfo(CGF, CS);
        CodeGenFunction::CGCapturedStmtRAII CapInfoRAII(CGF, &CGInfo);
        llvm::Value *ThreadLimit = CGF.EmitScalarExpr(TLE->getThreadLimit());
        return CGF.Builder.CreateIntCast(ThreadLimit, CGF.Int32Ty,
                                         /*IsSigned=*/true);
      }

      // If we have an enclosed teams directive but no thread_limit clause we
      // use the default value 0.
      return CGF.Builder.getInt32(0);
    }
  }

  // No teams associated with the directive.
  return nullptr;
}

namespace {
LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

// Utility to handle information from clauses associated with a given
// construct that use mappable expressions (e.g. 'map' clause, 'to' clause).
// It provides a convenient interface to obtain the information and generate
// code for that information.
class MappableExprsHandler {
public:
  /// Values for bit flags used to specify the mapping type for
  /// offloading.
  enum OpenMPOffloadMappingFlags : uint64_t {
    /// No flags
    OMP_MAP_NONE = 0x0,
    /// Allocate memory on the device and move data from host to device.
    OMP_MAP_TO = 0x01,
    /// Allocate memory on the device and move data from device to host.
    OMP_MAP_FROM = 0x02,
    /// Always perform the requested mapping action on the element, even
    /// if it was already mapped before.
    OMP_MAP_ALWAYS = 0x04,
    /// Delete the element from the device environment, ignoring the
    /// current reference count associated with the element.
    OMP_MAP_DELETE = 0x08,
    /// The element being mapped is a pointer-pointee pair; both the
    /// pointer and the pointee should be mapped.
    OMP_MAP_PTR_AND_OBJ = 0x10,
    /// This flags signals that the base address of an entry should be
    /// passed to the target kernel as an argument.
    OMP_MAP_TARGET_PARAM = 0x20,
    /// Signal that the runtime library has to return the device pointer
    /// in the current position for the data being mapped. Used when we have the
    /// use_device_ptr clause.
    OMP_MAP_RETURN_PARAM = 0x40,
    /// This flag signals that the reference being passed is a pointer to
    /// private data.
    OMP_MAP_PRIVATE = 0x80,
    /// Pass the element to the device by value.
    OMP_MAP_LITERAL = 0x100,
    /// Implicit map
    OMP_MAP_IMPLICIT = 0x200,
    /// The 16 MSBs of the flags indicate whether the entry is member of some
    /// struct/class.
    OMP_MAP_MEMBER_OF = 0xffff000000000000,
    LLVM_MARK_AS_BITMASK_ENUM(/* LargestFlag = */ OMP_MAP_MEMBER_OF),
  };

  /// Class that associates information with a base pointer to be passed to the
  /// runtime library.
  class BasePointerInfo {
    /// The base pointer.
    llvm::Value *Ptr = nullptr;
    /// The base declaration that refers to this device pointer, or null if
    /// there is none.
    const ValueDecl *DevPtrDecl = nullptr;

  public:
    BasePointerInfo(llvm::Value *Ptr, const ValueDecl *DevPtrDecl = nullptr)
        : Ptr(Ptr), DevPtrDecl(DevPtrDecl) {}
    llvm::Value *operator*() const { return Ptr; }
    const ValueDecl *getDevicePtrDecl() const { return DevPtrDecl; }
    void setDevicePtrDecl(const ValueDecl *D) { DevPtrDecl = D; }
  };

  using MapBaseValuesArrayTy = SmallVector<BasePointerInfo, 4>;
  using MapValuesArrayTy = SmallVector<llvm::Value *, 4>;
  using MapFlagsArrayTy = SmallVector<OpenMPOffloadMappingFlags, 4>;

  /// Map between a struct and the its lowest & highest elements which have been
  /// mapped.
  /// [ValueDecl *] --> {LE(FieldIndex, Pointer),
  ///                    HE(FieldIndex, Pointer)}
  struct StructRangeInfoTy {
    std::pair<unsigned /*FieldIndex*/, Address /*Pointer*/> LowestElem = {
        0, Address::invalid()};
    std::pair<unsigned /*FieldIndex*/, Address /*Pointer*/> HighestElem = {
        0, Address::invalid()};
    Address Base = Address::invalid();
  };

private:
  /// Kind that defines how a device pointer has to be returned.
  struct MapInfo {
    OMPClauseMappableExprCommon::MappableExprComponentListRef Components;
    OpenMPMapClauseKind MapType = OMPC_MAP_unknown;
    ArrayRef<OpenMPMapModifierKind> MapModifiers;
    bool ReturnDevicePointer = false;
    bool IsImplicit = false;

    MapInfo() = default;
    MapInfo(
        OMPClauseMappableExprCommon::MappableExprComponentListRef Components,
        OpenMPMapClauseKind MapType,
        ArrayRef<OpenMPMapModifierKind> MapModifiers,
        bool ReturnDevicePointer, bool IsImplicit)
        : Components(Components), MapType(MapType), MapModifiers(MapModifiers),
          ReturnDevicePointer(ReturnDevicePointer), IsImplicit(IsImplicit) {}
  };

  /// If use_device_ptr is used on a pointer which is a struct member and there
  /// is no map information about it, then emission of that entry is deferred
  /// until the whole struct has been processed.
  struct DeferredDevicePtrEntryTy {
    const Expr *IE = nullptr;
    const ValueDecl *VD = nullptr;

    DeferredDevicePtrEntryTy(const Expr *IE, const ValueDecl *VD)
        : IE(IE), VD(VD) {}
  };

  /// Directive from where the map clauses were extracted.
  const OMPExecutableDirective &CurDir;

  /// Function the directive is being generated for.
  CodeGenFunction &CGF;

  /// Set of all first private variables in the current directive.
  llvm::SmallPtrSet<const VarDecl *, 8> FirstPrivateDecls;

  /// Map between device pointer declarations and their expression components.
  /// The key value for declarations in 'this' is null.
  llvm::DenseMap<
      const ValueDecl *,
      SmallVector<OMPClauseMappableExprCommon::MappableExprComponentListRef, 4>>
      DevPointersMap;

  llvm::Value *getExprTypeSize(const Expr *E) const {
    QualType ExprTy = E->getType().getCanonicalType();

    // Reference types are ignored for mapping purposes.
    if (const auto *RefTy = ExprTy->getAs<ReferenceType>())
      ExprTy = RefTy->getPointeeType().getCanonicalType();

    // Given that an array section is considered a built-in type, we need to
    // do the calculation based on the length of the section instead of relying
    // on CGF.getTypeSize(E->getType()).
    if (const auto *OAE = dyn_cast<OMPArraySectionExpr>(E)) {
      QualType BaseTy = OMPArraySectionExpr::getBaseOriginalType(
                            OAE->getBase()->IgnoreParenImpCasts())
                            .getCanonicalType();

      // If there is no length associated with the expression, that means we
      // are using the whole length of the base.
      if (!OAE->getLength() && OAE->getColonLoc().isValid())
        return CGF.getTypeSize(BaseTy);

      llvm::Value *ElemSize;
      if (const auto *PTy = BaseTy->getAs<PointerType>()) {
        ElemSize = CGF.getTypeSize(PTy->getPointeeType().getCanonicalType());
      } else {
        const auto *ATy = cast<ArrayType>(BaseTy.getTypePtr());
        assert(ATy && "Expecting array type if not a pointer type.");
        ElemSize = CGF.getTypeSize(ATy->getElementType().getCanonicalType());
      }

      // If we don't have a length at this point, that is because we have an
      // array section with a single element.
      if (!OAE->getLength())
        return ElemSize;

      llvm::Value *LengthVal = CGF.EmitScalarExpr(OAE->getLength());
      LengthVal =
          CGF.Builder.CreateIntCast(LengthVal, CGF.SizeTy, /*isSigned=*/false);
      return CGF.Builder.CreateNUWMul(LengthVal, ElemSize);
    }
    return CGF.getTypeSize(ExprTy);
  }

  /// Return the corresponding bits for a given map clause modifier. Add
  /// a flag marking the map as a pointer if requested. Add a flag marking the
  /// map as the first one of a series of maps that relate to the same map
  /// expression.
  OpenMPOffloadMappingFlags getMapTypeBits(
      OpenMPMapClauseKind MapType, ArrayRef<OpenMPMapModifierKind> MapModifiers,
      bool IsImplicit, bool AddPtrFlag, bool AddIsTargetParamFlag) const {
    OpenMPOffloadMappingFlags Bits =
        IsImplicit ? OMP_MAP_IMPLICIT : OMP_MAP_NONE;
    switch (MapType) {
    case OMPC_MAP_alloc:
    case OMPC_MAP_release:
      // alloc and release is the default behavior in the runtime library,  i.e.
      // if we don't pass any bits alloc/release that is what the runtime is
      // going to do. Therefore, we don't need to signal anything for these two
      // type modifiers.
      break;
    case OMPC_MAP_to:
      Bits |= OMP_MAP_TO;
      break;
    case OMPC_MAP_from:
      Bits |= OMP_MAP_FROM;
      break;
    case OMPC_MAP_tofrom:
      Bits |= OMP_MAP_TO | OMP_MAP_FROM;
      break;
    case OMPC_MAP_delete:
      Bits |= OMP_MAP_DELETE;
      break;
    case OMPC_MAP_unknown:
      llvm_unreachable("Unexpected map type!");
    }
    if (AddPtrFlag)
      Bits |= OMP_MAP_PTR_AND_OBJ;
    if (AddIsTargetParamFlag)
      Bits |= OMP_MAP_TARGET_PARAM;
    if (llvm::find(MapModifiers, OMPC_MAP_MODIFIER_always)
        != MapModifiers.end())
      Bits |= OMP_MAP_ALWAYS;
    return Bits;
  }

  /// Return true if the provided expression is a final array section. A
  /// final array section, is one whose length can't be proved to be one.
  bool isFinalArraySectionExpression(const Expr *E) const {
    const auto *OASE = dyn_cast<OMPArraySectionExpr>(E);

    // It is not an array section and therefore not a unity-size one.
    if (!OASE)
      return false;

    // An array section with no colon always refer to a single element.
    if (OASE->getColonLoc().isInvalid())
      return false;

    const Expr *Length = OASE->getLength();

    // If we don't have a length we have to check if the array has size 1
    // for this dimension. Also, we should always expect a length if the
    // base type is pointer.
    if (!Length) {
      QualType BaseQTy = OMPArraySectionExpr::getBaseOriginalType(
                             OASE->getBase()->IgnoreParenImpCasts())
                             .getCanonicalType();
      if (const auto *ATy = dyn_cast<ConstantArrayType>(BaseQTy.getTypePtr()))
        return ATy->getSize().getSExtValue() != 1;
      // If we don't have a constant dimension length, we have to consider
      // the current section as having any size, so it is not necessarily
      // unitary. If it happen to be unity size, that's user fault.
      return true;
    }

    // Check if the length evaluates to 1.
    Expr::EvalResult Result;
    if (!Length->EvaluateAsInt(Result, CGF.getContext()))
      return true; // Can have more that size 1.

    llvm::APSInt ConstLength = Result.Val.getInt();
    return ConstLength.getSExtValue() != 1;
  }

  /// Generate the base pointers, section pointers, sizes and map type
  /// bits for the provided map type, map modifier, and expression components.
  /// \a IsFirstComponent should be set to true if the provided set of
  /// components is the first associated with a capture.
  void generateInfoForComponentList(
      OpenMPMapClauseKind MapType,
      ArrayRef<OpenMPMapModifierKind> MapModifiers,
      OMPClauseMappableExprCommon::MappableExprComponentListRef Components,
      MapBaseValuesArrayTy &BasePointers, MapValuesArrayTy &Pointers,
      MapValuesArrayTy &Sizes, MapFlagsArrayTy &Types,
      StructRangeInfoTy &PartialStruct, bool IsFirstComponentList,
      bool IsImplicit,
      ArrayRef<OMPClauseMappableExprCommon::MappableExprComponentListRef>
          OverlappedElements = llvm::None) const {
    // The following summarizes what has to be generated for each map and the
    // types below. The generated information is expressed in this order:
    // base pointer, section pointer, size, flags
    // (to add to the ones that come from the map type and modifier).
    //
    // double d;
    // int i[100];
    // float *p;
    //
    // struct S1 {
    //   int i;
    //   float f[50];
    // }
    // struct S2 {
    //   int i;
    //   float f[50];
    //   S1 s;
    //   double *p;
    //   struct S2 *ps;
    // }
    // S2 s;
    // S2 *ps;
    //
    // map(d)
    // &d, &d, sizeof(double), TARGET_PARAM | TO | FROM
    //
    // map(i)
    // &i, &i, 100*sizeof(int), TARGET_PARAM | TO | FROM
    //
    // map(i[1:23])
    // &i(=&i[0]), &i[1], 23*sizeof(int), TARGET_PARAM | TO | FROM
    //
    // map(p)
    // &p, &p, sizeof(float*), TARGET_PARAM | TO | FROM
    //
    // map(p[1:24])
    // p, &p[1], 24*sizeof(float), TARGET_PARAM | TO | FROM
    //
    // map(s)
    // &s, &s, sizeof(S2), TARGET_PARAM | TO | FROM
    //
    // map(s.i)
    // &s, &(s.i), sizeof(int), TARGET_PARAM | TO | FROM
    //
    // map(s.s.f)
    // &s, &(s.s.f[0]), 50*sizeof(float), TARGET_PARAM | TO | FROM
    //
    // map(s.p)
    // &s, &(s.p), sizeof(double*), TARGET_PARAM | TO | FROM
    //
    // map(to: s.p[:22])
    // &s, &(s.p), sizeof(double*), TARGET_PARAM (*)
    // &s, &(s.p), sizeof(double*), MEMBER_OF(1) (**)
    // &(s.p), &(s.p[0]), 22*sizeof(double),
    //   MEMBER_OF(1) | PTR_AND_OBJ | TO (***)
    // (*) alloc space for struct members, only this is a target parameter
    // (**) map the pointer (nothing to be mapped in this example) (the compiler
    //      optimizes this entry out, same in the examples below)
    // (***) map the pointee (map: to)
    //
    // map(s.ps)
    // &s, &(s.ps), sizeof(S2*), TARGET_PARAM | TO | FROM
    //
    // map(from: s.ps->s.i)
    // &s, &(s.ps), sizeof(S2*), TARGET_PARAM
    // &s, &(s.ps), sizeof(S2*), MEMBER_OF(1)
    // &(s.ps), &(s.ps->s.i), sizeof(int), MEMBER_OF(1) | PTR_AND_OBJ  | FROM
    //
    // map(to: s.ps->ps)
    // &s, &(s.ps), sizeof(S2*), TARGET_PARAM
    // &s, &(s.ps), sizeof(S2*), MEMBER_OF(1)
    // &(s.ps), &(s.ps->ps), sizeof(S2*), MEMBER_OF(1) | PTR_AND_OBJ  | TO
    //
    // map(s.ps->ps->ps)
    // &s, &(s.ps), sizeof(S2*), TARGET_PARAM
    // &s, &(s.ps), sizeof(S2*), MEMBER_OF(1)
    // &(s.ps), &(s.ps->ps), sizeof(S2*), MEMBER_OF(1) | PTR_AND_OBJ
    // &(s.ps->ps), &(s.ps->ps->ps), sizeof(S2*), PTR_AND_OBJ | TO | FROM
    //
    // map(to: s.ps->ps->s.f[:22])
    // &s, &(s.ps), sizeof(S2*), TARGET_PARAM
    // &s, &(s.ps), sizeof(S2*), MEMBER_OF(1)
    // &(s.ps), &(s.ps->ps), sizeof(S2*), MEMBER_OF(1) | PTR_AND_OBJ
    // &(s.ps->ps), &(s.ps->ps->s.f[0]), 22*sizeof(float), PTR_AND_OBJ | TO
    //
    // map(ps)
    // &ps, &ps, sizeof(S2*), TARGET_PARAM | TO | FROM
    //
    // map(ps->i)
    // ps, &(ps->i), sizeof(int), TARGET_PARAM | TO | FROM
    //
    // map(ps->s.f)
    // ps, &(ps->s.f[0]), 50*sizeof(float), TARGET_PARAM | TO | FROM
    //
    // map(from: ps->p)
    // ps, &(ps->p), sizeof(double*), TARGET_PARAM | FROM
    //
    // map(to: ps->p[:22])
    // ps, &(ps->p), sizeof(double*), TARGET_PARAM
    // ps, &(ps->p), sizeof(double*), MEMBER_OF(1)
    // &(ps->p), &(ps->p[0]), 22*sizeof(double), MEMBER_OF(1) | PTR_AND_OBJ | TO
    //
    // map(ps->ps)
    // ps, &(ps->ps), sizeof(S2*), TARGET_PARAM | TO | FROM
    //
    // map(from: ps->ps->s.i)
    // ps, &(ps->ps), sizeof(S2*), TARGET_PARAM
    // ps, &(ps->ps), sizeof(S2*), MEMBER_OF(1)
    // &(ps->ps), &(ps->ps->s.i), sizeof(int), MEMBER_OF(1) | PTR_AND_OBJ | FROM
    //
    // map(from: ps->ps->ps)
    // ps, &(ps->ps), sizeof(S2*), TARGET_PARAM
    // ps, &(ps->ps), sizeof(S2*), MEMBER_OF(1)
    // &(ps->ps), &(ps->ps->ps), sizeof(S2*), MEMBER_OF(1) | PTR_AND_OBJ | FROM
    //
    // map(ps->ps->ps->ps)
    // ps, &(ps->ps), sizeof(S2*), TARGET_PARAM
    // ps, &(ps->ps), sizeof(S2*), MEMBER_OF(1)
    // &(ps->ps), &(ps->ps->ps), sizeof(S2*), MEMBER_OF(1) | PTR_AND_OBJ
    // &(ps->ps->ps), &(ps->ps->ps->ps), sizeof(S2*), PTR_AND_OBJ | TO | FROM
    //
    // map(to: ps->ps->ps->s.f[:22])
    // ps, &(ps->ps), sizeof(S2*), TARGET_PARAM
    // ps, &(ps->ps), sizeof(S2*), MEMBER_OF(1)
    // &(ps->ps), &(ps->ps->ps), sizeof(S2*), MEMBER_OF(1) | PTR_AND_OBJ
    // &(ps->ps->ps), &(ps->ps->ps->s.f[0]), 22*sizeof(float), PTR_AND_OBJ | TO
    //
    // map(to: s.f[:22]) map(from: s.p[:33])
    // &s, &(s.f[0]), 50*sizeof(float) + sizeof(struct S1) +
    //     sizeof(double*) (**), TARGET_PARAM
    // &s, &(s.f[0]), 22*sizeof(float), MEMBER_OF(1) | TO
    // &s, &(s.p), sizeof(double*), MEMBER_OF(1)
    // &(s.p), &(s.p[0]), 33*sizeof(double), MEMBER_OF(1) | PTR_AND_OBJ | FROM
    // (*) allocate contiguous space needed to fit all mapped members even if
    //     we allocate space for members not mapped (in this example,
    //     s.f[22..49] and s.s are not mapped, yet we must allocate space for
    //     them as well because they fall between &s.f[0] and &s.p)
    //
    // map(from: s.f[:22]) map(to: ps->p[:33])
    // &s, &(s.f[0]), 22*sizeof(float), TARGET_PARAM | FROM
    // ps, &(ps->p), sizeof(S2*), TARGET_PARAM
    // ps, &(ps->p), sizeof(double*), MEMBER_OF(2) (*)
    // &(ps->p), &(ps->p[0]), 33*sizeof(double), MEMBER_OF(2) | PTR_AND_OBJ | TO
    // (*) the struct this entry pertains to is the 2nd element in the list of
    //     arguments, hence MEMBER_OF(2)
    //
    // map(from: s.f[:22], s.s) map(to: ps->p[:33])
    // &s, &(s.f[0]), 50*sizeof(float) + sizeof(struct S1), TARGET_PARAM
    // &s, &(s.f[0]), 22*sizeof(float), MEMBER_OF(1) | FROM
    // &s, &(s.s), sizeof(struct S1), MEMBER_OF(1) | FROM
    // ps, &(ps->p), sizeof(S2*), TARGET_PARAM
    // ps, &(ps->p), sizeof(double*), MEMBER_OF(4) (*)
    // &(ps->p), &(ps->p[0]), 33*sizeof(double), MEMBER_OF(4) | PTR_AND_OBJ | TO
    // (*) the struct this entry pertains to is the 4th element in the list
    //     of arguments, hence MEMBER_OF(4)

    // Track if the map information being generated is the first for a capture.
    bool IsCaptureFirstInfo = IsFirstComponentList;
    bool IsLink = false; // Is this variable a "declare target link"?

    // Scan the components from the base to the complete expression.
    auto CI = Components.rbegin();
    auto CE = Components.rend();
    auto I = CI;

    // Track if the map information being generated is the first for a list of
    // components.
    bool IsExpressionFirstInfo = true;
    Address BP = Address::invalid();
    const Expr *AssocExpr = I->getAssociatedExpression();
    const auto *AE = dyn_cast<ArraySubscriptExpr>(AssocExpr);
    const auto *OASE = dyn_cast<OMPArraySectionExpr>(AssocExpr);

    if (isa<MemberExpr>(AssocExpr)) {
      // The base is the 'this' pointer. The content of the pointer is going
      // to be the base of the field being mapped.
      BP = CGF.LoadCXXThisAddress();
    } else if ((AE && isa<CXXThisExpr>(AE->getBase()->IgnoreParenImpCasts())) ||
               (OASE &&
                isa<CXXThisExpr>(OASE->getBase()->IgnoreParenImpCasts()))) {
      BP = CGF.EmitOMPSharedLValue(AssocExpr).getAddress();
    } else {
      // The base is the reference to the variable.
      // BP = &Var.
      BP = CGF.EmitOMPSharedLValue(AssocExpr).getAddress();
      if (const auto *VD =
              dyn_cast_or_null<VarDecl>(I->getAssociatedDeclaration())) {
        if (llvm::Optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
                OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD))
          if (*Res == OMPDeclareTargetDeclAttr::MT_Link) {
            IsLink = true;
            BP = CGF.CGM.getOpenMPRuntime().getAddrOfDeclareTargetLink(VD);
          }
      }

      // If the variable is a pointer and is being dereferenced (i.e. is not
      // the last component), the base has to be the pointer itself, not its
      // reference. References are ignored for mapping purposes.
      QualType Ty =
          I->getAssociatedDeclaration()->getType().getNonReferenceType();
      if (Ty->isAnyPointerType() && std::next(I) != CE) {
        BP = CGF.EmitLoadOfPointer(BP, Ty->castAs<PointerType>());

        // We do not need to generate individual map information for the
        // pointer, it can be associated with the combined storage.
        ++I;
      }
    }

    // Track whether a component of the list should be marked as MEMBER_OF some
    // combined entry (for partial structs). Only the first PTR_AND_OBJ entry
    // in a component list should be marked as MEMBER_OF, all subsequent entries
    // do not belong to the base struct. E.g.
    // struct S2 s;
    // s.ps->ps->ps->f[:]
    //   (1) (2) (3) (4)
    // ps(1) is a member pointer, ps(2) is a pointee of ps(1), so it is a
    // PTR_AND_OBJ entry; the PTR is ps(1), so MEMBER_OF the base struct. ps(3)
    // is the pointee of ps(2) which is not member of struct s, so it should not
    // be marked as such (it is still PTR_AND_OBJ).
    // The variable is initialized to false so that PTR_AND_OBJ entries which
    // are not struct members are not considered (e.g. array of pointers to
    // data).
    bool ShouldBeMemberOf = false;

    // Variable keeping track of whether or not we have encountered a component
    // in the component list which is a member expression. Useful when we have a
    // pointer or a final array section, in which case it is the previous
    // component in the list which tells us whether we have a member expression.
    // E.g. X.f[:]
    // While processing the final array section "[:]" it is "f" which tells us
    // whether we are dealing with a member of a declared struct.
    const MemberExpr *EncounteredME = nullptr;

    for (; I != CE; ++I) {
      // If the current component is member of a struct (parent struct) mark it.
      if (!EncounteredME) {
        EncounteredME = dyn_cast<MemberExpr>(I->getAssociatedExpression());
        // If we encounter a PTR_AND_OBJ entry from now on it should be marked
        // as MEMBER_OF the parent struct.
        if (EncounteredME)
          ShouldBeMemberOf = true;
      }

      auto Next = std::next(I);

      // We need to generate the addresses and sizes if this is the last
      // component, if the component is a pointer or if it is an array section
      // whose length can't be proved to be one. If this is a pointer, it
      // becomes the base address for the following components.

      // A final array section, is one whose length can't be proved to be one.
      bool IsFinalArraySection =
          isFinalArraySectionExpression(I->getAssociatedExpression());

      // Get information on whether the element is a pointer. Have to do a
      // special treatment for array sections given that they are built-in
      // types.
      const auto *OASE =
          dyn_cast<OMPArraySectionExpr>(I->getAssociatedExpression());
      bool IsPointer =
          (OASE && OMPArraySectionExpr::getBaseOriginalType(OASE)
                       .getCanonicalType()
                       ->isAnyPointerType()) ||
          I->getAssociatedExpression()->getType()->isAnyPointerType();

      if (Next == CE || IsPointer || IsFinalArraySection) {
        // If this is not the last component, we expect the pointer to be
        // associated with an array expression or member expression.
        assert((Next == CE ||
                isa<MemberExpr>(Next->getAssociatedExpression()) ||
                isa<ArraySubscriptExpr>(Next->getAssociatedExpression()) ||
                isa<OMPArraySectionExpr>(Next->getAssociatedExpression())) &&
               "Unexpected expression");

        Address LB =
            CGF.EmitOMPSharedLValue(I->getAssociatedExpression()).getAddress();

        // If this component is a pointer inside the base struct then we don't
        // need to create any entry for it - it will be combined with the object
        // it is pointing to into a single PTR_AND_OBJ entry.
        bool IsMemberPointer =
            IsPointer && EncounteredME &&
            (dyn_cast<MemberExpr>(I->getAssociatedExpression()) ==
             EncounteredME);
        if (!OverlappedElements.empty()) {
          // Handle base element with the info for overlapped elements.
          assert(!PartialStruct.Base.isValid() && "The base element is set.");
          assert(Next == CE &&
                 "Expected last element for the overlapped elements.");
          assert(!IsPointer &&
                 "Unexpected base element with the pointer type.");
          // Mark the whole struct as the struct that requires allocation on the
          // device.
          PartialStruct.LowestElem = {0, LB};
          CharUnits TypeSize = CGF.getContext().getTypeSizeInChars(
              I->getAssociatedExpression()->getType());
          Address HB = CGF.Builder.CreateConstGEP(
              CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(LB,
                                                              CGF.VoidPtrTy),
              TypeSize.getQuantity() - 1, CharUnits::One());
          PartialStruct.HighestElem = {
              std::numeric_limits<decltype(
                  PartialStruct.HighestElem.first)>::max(),
              HB};
          PartialStruct.Base = BP;
          // Emit data for non-overlapped data.
          OpenMPOffloadMappingFlags Flags =
              OMP_MAP_MEMBER_OF |
              getMapTypeBits(MapType, MapModifiers, IsImplicit,
                             /*AddPtrFlag=*/false,
                             /*AddIsTargetParamFlag=*/false);
          LB = BP;
          llvm::Value *Size = nullptr;
          // Do bitcopy of all non-overlapped structure elements.
          for (OMPClauseMappableExprCommon::MappableExprComponentListRef
                   Component : OverlappedElements) {
            Address ComponentLB = Address::invalid();
            for (const OMPClauseMappableExprCommon::MappableComponent &MC :
                 Component) {
              if (MC.getAssociatedDeclaration()) {
                ComponentLB =
                    CGF.EmitOMPSharedLValue(MC.getAssociatedExpression())
                        .getAddress();
                Size = CGF.Builder.CreatePtrDiff(
                    CGF.EmitCastToVoidPtr(ComponentLB.getPointer()),
                    CGF.EmitCastToVoidPtr(LB.getPointer()));
                break;
              }
            }
            BasePointers.push_back(BP.getPointer());
            Pointers.push_back(LB.getPointer());
            Sizes.push_back(Size);
            Types.push_back(Flags);
            LB = CGF.Builder.CreateConstGEP(ComponentLB, 1,
                                            CGF.getPointerSize());
          }
          BasePointers.push_back(BP.getPointer());
          Pointers.push_back(LB.getPointer());
          Size = CGF.Builder.CreatePtrDiff(
              CGF.EmitCastToVoidPtr(
                  CGF.Builder.CreateConstGEP(HB, 1, CharUnits::One())
                      .getPointer()),
              CGF.EmitCastToVoidPtr(LB.getPointer()));
          Sizes.push_back(Size);
          Types.push_back(Flags);
          break;
        }
        llvm::Value *Size = getExprTypeSize(I->getAssociatedExpression());
        if (!IsMemberPointer) {
          BasePointers.push_back(BP.getPointer());
          Pointers.push_back(LB.getPointer());
          Sizes.push_back(Size);

          // We need to add a pointer flag for each map that comes from the
          // same expression except for the first one. We also need to signal
          // this map is the first one that relates with the current capture
          // (there is a set of entries for each capture).
          OpenMPOffloadMappingFlags Flags = getMapTypeBits(
              MapType, MapModifiers, IsImplicit,
              !IsExpressionFirstInfo || IsLink, IsCaptureFirstInfo && !IsLink);

          if (!IsExpressionFirstInfo) {
            // If we have a PTR_AND_OBJ pair where the OBJ is a pointer as well,
            // then we reset the TO/FROM/ALWAYS/DELETE flags.
            if (IsPointer)
              Flags &= ~(OMP_MAP_TO | OMP_MAP_FROM | OMP_MAP_ALWAYS |
                         OMP_MAP_DELETE);

            if (ShouldBeMemberOf) {
              // Set placeholder value MEMBER_OF=FFFF to indicate that the flag
              // should be later updated with the correct value of MEMBER_OF.
              Flags |= OMP_MAP_MEMBER_OF;
              // From now on, all subsequent PTR_AND_OBJ entries should not be
              // marked as MEMBER_OF.
              ShouldBeMemberOf = false;
            }
          }

          Types.push_back(Flags);
        }

        // If we have encountered a member expression so far, keep track of the
        // mapped member. If the parent is "*this", then the value declaration
        // is nullptr.
        if (EncounteredME) {
          const auto *FD = dyn_cast<FieldDecl>(EncounteredME->getMemberDecl());
          unsigned FieldIndex = FD->getFieldIndex();

          // Update info about the lowest and highest elements for this struct
          if (!PartialStruct.Base.isValid()) {
            PartialStruct.LowestElem = {FieldIndex, LB};
            PartialStruct.HighestElem = {FieldIndex, LB};
            PartialStruct.Base = BP;
          } else if (FieldIndex < PartialStruct.LowestElem.first) {
            PartialStruct.LowestElem = {FieldIndex, LB};
          } else if (FieldIndex > PartialStruct.HighestElem.first) {
            PartialStruct.HighestElem = {FieldIndex, LB};
          }
        }

        // If we have a final array section, we are done with this expression.
        if (IsFinalArraySection)
          break;

        // The pointer becomes the base for the next element.
        if (Next != CE)
          BP = LB;

        IsExpressionFirstInfo = false;
        IsCaptureFirstInfo = false;
      }
    }
  }

  /// Return the adjusted map modifiers if the declaration a capture refers to
  /// appears in a first-private clause. This is expected to be used only with
  /// directives that start with 'target'.
  MappableExprsHandler::OpenMPOffloadMappingFlags
  getMapModifiersForPrivateClauses(const CapturedStmt::Capture &Cap) const {
    assert(Cap.capturesVariable() && "Expected capture by reference only!");

    // A first private variable captured by reference will use only the
    // 'private ptr' and 'map to' flag. Return the right flags if the captured
    // declaration is known as first-private in this handler.
    if (FirstPrivateDecls.count(Cap.getCapturedVar()))
      return MappableExprsHandler::OMP_MAP_PRIVATE |
             MappableExprsHandler::OMP_MAP_TO;
    return MappableExprsHandler::OMP_MAP_TO |
           MappableExprsHandler::OMP_MAP_FROM;
  }

  static OpenMPOffloadMappingFlags getMemberOfFlag(unsigned Position) {
    // Member of is given by the 16 MSB of the flag, so rotate by 48 bits.
    return static_cast<OpenMPOffloadMappingFlags>(((uint64_t)Position + 1)
                                                  << 48);
  }

  static void setCorrectMemberOfFlag(OpenMPOffloadMappingFlags &Flags,
                                     OpenMPOffloadMappingFlags MemberOfFlag) {
    // If the entry is PTR_AND_OBJ but has not been marked with the special
    // placeholder value 0xFFFF in the MEMBER_OF field, then it should not be
    // marked as MEMBER_OF.
    if ((Flags & OMP_MAP_PTR_AND_OBJ) &&
        ((Flags & OMP_MAP_MEMBER_OF) != OMP_MAP_MEMBER_OF))
      return;

    // Reset the placeholder value to prepare the flag for the assignment of the
    // proper MEMBER_OF value.
    Flags &= ~OMP_MAP_MEMBER_OF;
    Flags |= MemberOfFlag;
  }

  void getPlainLayout(const CXXRecordDecl *RD,
                      llvm::SmallVectorImpl<const FieldDecl *> &Layout,
                      bool AsBase) const {
    const CGRecordLayout &RL = CGF.getTypes().getCGRecordLayout(RD);

    llvm::StructType *St =
        AsBase ? RL.getBaseSubobjectLLVMType() : RL.getLLVMType();

    unsigned NumElements = St->getNumElements();
    llvm::SmallVector<
        llvm::PointerUnion<const CXXRecordDecl *, const FieldDecl *>, 4>
        RecordLayout(NumElements);

    // Fill bases.
    for (const auto &I : RD->bases()) {
      if (I.isVirtual())
        continue;
      const auto *Base = I.getType()->getAsCXXRecordDecl();
      // Ignore empty bases.
      if (Base->isEmpty() || CGF.getContext()
                                 .getASTRecordLayout(Base)
                                 .getNonVirtualSize()
                                 .isZero())
        continue;

      unsigned FieldIndex = RL.getNonVirtualBaseLLVMFieldNo(Base);
      RecordLayout[FieldIndex] = Base;
    }
    // Fill in virtual bases.
    for (const auto &I : RD->vbases()) {
      const auto *Base = I.getType()->getAsCXXRecordDecl();
      // Ignore empty bases.
      if (Base->isEmpty())
        continue;
      unsigned FieldIndex = RL.getVirtualBaseIndex(Base);
      if (RecordLayout[FieldIndex])
        continue;
      RecordLayout[FieldIndex] = Base;
    }
    // Fill in all the fields.
    assert(!RD->isUnion() && "Unexpected union.");
    for (const auto *Field : RD->fields()) {
      // Fill in non-bitfields. (Bitfields always use a zero pattern, which we
      // will fill in later.)
      if (!Field->isBitField()) {
        unsigned FieldIndex = RL.getLLVMFieldNo(Field);
        RecordLayout[FieldIndex] = Field;
      }
    }
    for (const llvm::PointerUnion<const CXXRecordDecl *, const FieldDecl *>
             &Data : RecordLayout) {
      if (Data.isNull())
        continue;
      if (const auto *Base = Data.dyn_cast<const CXXRecordDecl *>())
        getPlainLayout(Base, Layout, /*AsBase=*/true);
      else
        Layout.push_back(Data.get<const FieldDecl *>());
    }
  }

public:
  MappableExprsHandler(const OMPExecutableDirective &Dir, CodeGenFunction &CGF)
      : CurDir(Dir), CGF(CGF) {
    // Extract firstprivate clause information.
    for (const auto *C : Dir.getClausesOfKind<OMPFirstprivateClause>())
      for (const auto *D : C->varlists())
        FirstPrivateDecls.insert(
            cast<VarDecl>(cast<DeclRefExpr>(D)->getDecl())->getCanonicalDecl());
    // Extract device pointer clause information.
    for (const auto *C : Dir.getClausesOfKind<OMPIsDevicePtrClause>())
      for (auto L : C->component_lists())
        DevPointersMap[L.first].push_back(L.second);
  }

  /// Generate code for the combined entry if we have a partially mapped struct
  /// and take care of the mapping flags of the arguments corresponding to
  /// individual struct members.
  void emitCombinedEntry(MapBaseValuesArrayTy &BasePointers,
                         MapValuesArrayTy &Pointers, MapValuesArrayTy &Sizes,
                         MapFlagsArrayTy &Types, MapFlagsArrayTy &CurTypes,
                         const StructRangeInfoTy &PartialStruct) const {
    // Base is the base of the struct
    BasePointers.push_back(PartialStruct.Base.getPointer());
    // Pointer is the address of the lowest element
    llvm::Value *LB = PartialStruct.LowestElem.second.getPointer();
    Pointers.push_back(LB);
    // Size is (addr of {highest+1} element) - (addr of lowest element)
    llvm::Value *HB = PartialStruct.HighestElem.second.getPointer();
    llvm::Value *HAddr = CGF.Builder.CreateConstGEP1_32(HB, /*Idx0=*/1);
    llvm::Value *CLAddr = CGF.Builder.CreatePointerCast(LB, CGF.VoidPtrTy);
    llvm::Value *CHAddr = CGF.Builder.CreatePointerCast(HAddr, CGF.VoidPtrTy);
    llvm::Value *Diff = CGF.Builder.CreatePtrDiff(CHAddr, CLAddr);
    llvm::Value *Size = CGF.Builder.CreateIntCast(Diff, CGF.SizeTy,
                                                  /*isSinged=*/false);
    Sizes.push_back(Size);
    // Map type is always TARGET_PARAM
    Types.push_back(OMP_MAP_TARGET_PARAM);
    // Remove TARGET_PARAM flag from the first element
    (*CurTypes.begin()) &= ~OMP_MAP_TARGET_PARAM;

    // All other current entries will be MEMBER_OF the combined entry
    // (except for PTR_AND_OBJ entries which do not have a placeholder value
    // 0xFFFF in the MEMBER_OF field).
    OpenMPOffloadMappingFlags MemberOfFlag =
        getMemberOfFlag(BasePointers.size() - 1);
    for (auto &M : CurTypes)
      setCorrectMemberOfFlag(M, MemberOfFlag);
  }

  /// Generate all the base pointers, section pointers, sizes and map
  /// types for the extracted mappable expressions. Also, for each item that
  /// relates with a device pointer, a pair of the relevant declaration and
  /// index where it occurs is appended to the device pointers info array.
  void generateAllInfo(MapBaseValuesArrayTy &BasePointers,
                       MapValuesArrayTy &Pointers, MapValuesArrayTy &Sizes,
                       MapFlagsArrayTy &Types) const {
    // We have to process the component lists that relate with the same
    // declaration in a single chunk so that we can generate the map flags
    // correctly. Therefore, we organize all lists in a map.
    llvm::MapVector<const ValueDecl *, SmallVector<MapInfo, 8>> Info;

    // Helper function to fill the information map for the different supported
    // clauses.
    auto &&InfoGen = [&Info](
        const ValueDecl *D,
        OMPClauseMappableExprCommon::MappableExprComponentListRef L,
        OpenMPMapClauseKind MapType,
        ArrayRef<OpenMPMapModifierKind> MapModifiers,
        bool ReturnDevicePointer, bool IsImplicit) {
      const ValueDecl *VD =
          D ? cast<ValueDecl>(D->getCanonicalDecl()) : nullptr;
      Info[VD].emplace_back(L, MapType, MapModifiers, ReturnDevicePointer,
                            IsImplicit);
    };

    // FIXME: MSVC 2013 seems to require this-> to find member CurDir.
    for (const auto *C : this->CurDir.getClausesOfKind<OMPMapClause>())
      for (const auto &L : C->component_lists()) {
        InfoGen(L.first, L.second, C->getMapType(), C->getMapTypeModifiers(),
            /*ReturnDevicePointer=*/false, C->isImplicit());
      }
    for (const auto *C : this->CurDir.getClausesOfKind<OMPToClause>())
      for (const auto &L : C->component_lists()) {
        InfoGen(L.first, L.second, OMPC_MAP_to, llvm::None,
            /*ReturnDevicePointer=*/false, C->isImplicit());
      }
    for (const auto *C : this->CurDir.getClausesOfKind<OMPFromClause>())
      for (const auto &L : C->component_lists()) {
        InfoGen(L.first, L.second, OMPC_MAP_from, llvm::None,
            /*ReturnDevicePointer=*/false, C->isImplicit());
      }

    // Look at the use_device_ptr clause information and mark the existing map
    // entries as such. If there is no map information for an entry in the
    // use_device_ptr list, we create one with map type 'alloc' and zero size
    // section. It is the user fault if that was not mapped before. If there is
    // no map information and the pointer is a struct member, then we defer the
    // emission of that entry until the whole struct has been processed.
    llvm::MapVector<const ValueDecl *, SmallVector<DeferredDevicePtrEntryTy, 4>>
        DeferredInfo;

    // FIXME: MSVC 2013 seems to require this-> to find member CurDir.
    for (const auto *C :
        this->CurDir.getClausesOfKind<OMPUseDevicePtrClause>()) {
      for (const auto &L : C->component_lists()) {
        assert(!L.second.empty() && "Not expecting empty list of components!");
        const ValueDecl *VD = L.second.back().getAssociatedDeclaration();
        VD = cast<ValueDecl>(VD->getCanonicalDecl());
        const Expr *IE = L.second.back().getAssociatedExpression();
        // If the first component is a member expression, we have to look into
        // 'this', which maps to null in the map of map information. Otherwise
        // look directly for the information.
        auto It = Info.find(isa<MemberExpr>(IE) ? nullptr : VD);

        // We potentially have map information for this declaration already.
        // Look for the first set of components that refer to it.
        if (It != Info.end()) {
          auto CI = std::find_if(
              It->second.begin(), It->second.end(), [VD](const MapInfo &MI) {
                return MI.Components.back().getAssociatedDeclaration() == VD;
              });
          // If we found a map entry, signal that the pointer has to be returned
          // and move on to the next declaration.
          if (CI != It->second.end()) {
            CI->ReturnDevicePointer = true;
            continue;
          }
        }

        // We didn't find any match in our map information - generate a zero
        // size array section - if the pointer is a struct member we defer this
        // action until the whole struct has been processed.
        // FIXME: MSVC 2013 seems to require this-> to find member CGF.
        if (isa<MemberExpr>(IE)) {
          // Insert the pointer into Info to be processed by
          // generateInfoForComponentList. Because it is a member pointer
          // without a pointee, no entry will be generated for it, therefore
          // we need to generate one after the whole struct has been processed.
          // Nonetheless, generateInfoForComponentList must be called to take
          // the pointer into account for the calculation of the range of the
          // partial struct.
          InfoGen(nullptr, L.second, OMPC_MAP_unknown, llvm::None,
                  /*ReturnDevicePointer=*/false, C->isImplicit());
          DeferredInfo[nullptr].emplace_back(IE, VD);
        } else {
          llvm::Value *Ptr = this->CGF.EmitLoadOfScalar(
              this->CGF.EmitLValue(IE), IE->getExprLoc());
          BasePointers.emplace_back(Ptr, VD);
          Pointers.push_back(Ptr);
          Sizes.push_back(llvm::Constant::getNullValue(this->CGF.SizeTy));
          Types.push_back(OMP_MAP_RETURN_PARAM | OMP_MAP_TARGET_PARAM);
        }
      }
    }

    for (const auto &M : Info) {
      // We need to know when we generate information for the first component
      // associated with a capture, because the mapping flags depend on it.
      bool IsFirstComponentList = true;

      // Temporary versions of arrays
      MapBaseValuesArrayTy CurBasePointers;
      MapValuesArrayTy CurPointers;
      MapValuesArrayTy CurSizes;
      MapFlagsArrayTy CurTypes;
      StructRangeInfoTy PartialStruct;

      for (const MapInfo &L : M.second) {
        assert(!L.Components.empty() &&
               "Not expecting declaration with no component lists.");

        // Remember the current base pointer index.
        unsigned CurrentBasePointersIdx = CurBasePointers.size();
        // FIXME: MSVC 2013 seems to require this-> to find the member method.
        this->generateInfoForComponentList(
            L.MapType, L.MapModifiers, L.Components, CurBasePointers,
            CurPointers, CurSizes, CurTypes, PartialStruct,
            IsFirstComponentList, L.IsImplicit);

        // If this entry relates with a device pointer, set the relevant
        // declaration and add the 'return pointer' flag.
        if (L.ReturnDevicePointer) {
          assert(CurBasePointers.size() > CurrentBasePointersIdx &&
                 "Unexpected number of mapped base pointers.");

          const ValueDecl *RelevantVD =
              L.Components.back().getAssociatedDeclaration();
          assert(RelevantVD &&
                 "No relevant declaration related with device pointer??");

          CurBasePointers[CurrentBasePointersIdx].setDevicePtrDecl(RelevantVD);
          CurTypes[CurrentBasePointersIdx] |= OMP_MAP_RETURN_PARAM;
        }
        IsFirstComponentList = false;
      }

      // Append any pending zero-length pointers which are struct members and
      // used with use_device_ptr.
      auto CI = DeferredInfo.find(M.first);
      if (CI != DeferredInfo.end()) {
        for (const DeferredDevicePtrEntryTy &L : CI->second) {
          llvm::Value *BasePtr = this->CGF.EmitLValue(L.IE).getPointer();
          llvm::Value *Ptr = this->CGF.EmitLoadOfScalar(
              this->CGF.EmitLValue(L.IE), L.IE->getExprLoc());
          CurBasePointers.emplace_back(BasePtr, L.VD);
          CurPointers.push_back(Ptr);
          CurSizes.push_back(llvm::Constant::getNullValue(this->CGF.SizeTy));
          // Entry is PTR_AND_OBJ and RETURN_PARAM. Also, set the placeholder
          // value MEMBER_OF=FFFF so that the entry is later updated with the
          // correct value of MEMBER_OF.
          CurTypes.push_back(OMP_MAP_PTR_AND_OBJ | OMP_MAP_RETURN_PARAM |
                             OMP_MAP_MEMBER_OF);
        }
      }

      // If there is an entry in PartialStruct it means we have a struct with
      // individual members mapped. Emit an extra combined entry.
      if (PartialStruct.Base.isValid())
        emitCombinedEntry(BasePointers, Pointers, Sizes, Types, CurTypes,
                          PartialStruct);

      // We need to append the results of this capture to what we already have.
      BasePointers.append(CurBasePointers.begin(), CurBasePointers.end());
      Pointers.append(CurPointers.begin(), CurPointers.end());
      Sizes.append(CurSizes.begin(), CurSizes.end());
      Types.append(CurTypes.begin(), CurTypes.end());
    }
  }

  /// Emit capture info for lambdas for variables captured by reference.
  void generateInfoForLambdaCaptures(
      const ValueDecl *VD, llvm::Value *Arg, MapBaseValuesArrayTy &BasePointers,
      MapValuesArrayTy &Pointers, MapValuesArrayTy &Sizes,
      MapFlagsArrayTy &Types,
      llvm::DenseMap<llvm::Value *, llvm::Value *> &LambdaPointers) const {
    const auto *RD = VD->getType()
                         .getCanonicalType()
                         .getNonReferenceType()
                         ->getAsCXXRecordDecl();
    if (!RD || !RD->isLambda())
      return;
    Address VDAddr = Address(Arg, CGF.getContext().getDeclAlign(VD));
    LValue VDLVal = CGF.MakeAddrLValue(
        VDAddr, VD->getType().getCanonicalType().getNonReferenceType());
    llvm::DenseMap<const VarDecl *, FieldDecl *> Captures;
    FieldDecl *ThisCapture = nullptr;
    RD->getCaptureFields(Captures, ThisCapture);
    if (ThisCapture) {
      LValue ThisLVal =
          CGF.EmitLValueForFieldInitialization(VDLVal, ThisCapture);
      LValue ThisLValVal = CGF.EmitLValueForField(VDLVal, ThisCapture);
      LambdaPointers.try_emplace(ThisLVal.getPointer(), VDLVal.getPointer());
      BasePointers.push_back(ThisLVal.getPointer());
      Pointers.push_back(ThisLValVal.getPointer());
      Sizes.push_back(CGF.getTypeSize(CGF.getContext().VoidPtrTy));
      Types.push_back(OMP_MAP_PTR_AND_OBJ | OMP_MAP_LITERAL |
                      OMP_MAP_MEMBER_OF | OMP_MAP_IMPLICIT);
    }
    for (const LambdaCapture &LC : RD->captures()) {
      if (LC.getCaptureKind() != LCK_ByRef)
        continue;
      const VarDecl *VD = LC.getCapturedVar();
      auto It = Captures.find(VD);
      assert(It != Captures.end() && "Found lambda capture without field.");
      LValue VarLVal = CGF.EmitLValueForFieldInitialization(VDLVal, It->second);
      LValue VarLValVal = CGF.EmitLValueForField(VDLVal, It->second);
      LambdaPointers.try_emplace(VarLVal.getPointer(), VDLVal.getPointer());
      BasePointers.push_back(VarLVal.getPointer());
      Pointers.push_back(VarLValVal.getPointer());
      Sizes.push_back(CGF.getTypeSize(
          VD->getType().getCanonicalType().getNonReferenceType()));
      Types.push_back(OMP_MAP_PTR_AND_OBJ | OMP_MAP_LITERAL |
                      OMP_MAP_MEMBER_OF | OMP_MAP_IMPLICIT);
    }
  }

  /// Set correct indices for lambdas captures.
  void adjustMemberOfForLambdaCaptures(
      const llvm::DenseMap<llvm::Value *, llvm::Value *> &LambdaPointers,
      MapBaseValuesArrayTy &BasePointers, MapValuesArrayTy &Pointers,
      MapFlagsArrayTy &Types) const {
    for (unsigned I = 0, E = Types.size(); I < E; ++I) {
      // Set correct member_of idx for all implicit lambda captures.
      if (Types[I] != (OMP_MAP_PTR_AND_OBJ | OMP_MAP_LITERAL |
                       OMP_MAP_MEMBER_OF | OMP_MAP_IMPLICIT))
        continue;
      llvm::Value *BasePtr = LambdaPointers.lookup(*BasePointers[I]);
      assert(BasePtr && "Unable to find base lambda address.");
      int TgtIdx = -1;
      for (unsigned J = I; J > 0; --J) {
        unsigned Idx = J - 1;
        if (Pointers[Idx] != BasePtr)
          continue;
        TgtIdx = Idx;
        break;
      }
      assert(TgtIdx != -1 && "Unable to find parent lambda.");
      // All other current entries will be MEMBER_OF the combined entry
      // (except for PTR_AND_OBJ entries which do not have a placeholder value
      // 0xFFFF in the MEMBER_OF field).
      OpenMPOffloadMappingFlags MemberOfFlag = getMemberOfFlag(TgtIdx);
      setCorrectMemberOfFlag(Types[I], MemberOfFlag);
    }
  }

  /// Generate the base pointers, section pointers, sizes and map types
  /// associated to a given capture.
  void generateInfoForCapture(const CapturedStmt::Capture *Cap,
                              llvm::Value *Arg,
                              MapBaseValuesArrayTy &BasePointers,
                              MapValuesArrayTy &Pointers,
                              MapValuesArrayTy &Sizes, MapFlagsArrayTy &Types,
                              StructRangeInfoTy &PartialStruct) const {
    assert(!Cap->capturesVariableArrayType() &&
           "Not expecting to generate map info for a variable array type!");

    // We need to know when we generating information for the first component
    const ValueDecl *VD = Cap->capturesThis()
                              ? nullptr
                              : Cap->getCapturedVar()->getCanonicalDecl();

    // If this declaration appears in a is_device_ptr clause we just have to
    // pass the pointer by value. If it is a reference to a declaration, we just
    // pass its value.
    if (DevPointersMap.count(VD)) {
      BasePointers.emplace_back(Arg, VD);
      Pointers.push_back(Arg);
      Sizes.push_back(CGF.getTypeSize(CGF.getContext().VoidPtrTy));
      Types.push_back(OMP_MAP_LITERAL | OMP_MAP_TARGET_PARAM);
      return;
    }

    using MapData =
        std::tuple<OMPClauseMappableExprCommon::MappableExprComponentListRef,
                   OpenMPMapClauseKind, ArrayRef<OpenMPMapModifierKind>, bool>;
    SmallVector<MapData, 4> DeclComponentLists;
    // FIXME: MSVC 2013 seems to require this-> to find member CurDir.
    for (const auto *C : this->CurDir.getClausesOfKind<OMPMapClause>()) {
      for (const auto &L : C->decl_component_lists(VD)) {
        assert(L.first == VD &&
               "We got information for the wrong declaration??");
        assert(!L.second.empty() &&
               "Not expecting declaration with no component lists.");
        DeclComponentLists.emplace_back(L.second, C->getMapType(),
                                        C->getMapTypeModifiers(),
                                        C->isImplicit());
      }
    }

    // Find overlapping elements (including the offset from the base element).
    llvm::SmallDenseMap<
        const MapData *,
        llvm::SmallVector<
            OMPClauseMappableExprCommon::MappableExprComponentListRef, 4>,
        4>
        OverlappedData;
    size_t Count = 0;
    for (const MapData &L : DeclComponentLists) {
      OMPClauseMappableExprCommon::MappableExprComponentListRef Components;
      OpenMPMapClauseKind MapType;
      ArrayRef<OpenMPMapModifierKind> MapModifiers;
      bool IsImplicit;
      std::tie(Components, MapType, MapModifiers, IsImplicit) = L;
      ++Count;
      for (const MapData &L1 : makeArrayRef(DeclComponentLists).slice(Count)) {
        OMPClauseMappableExprCommon::MappableExprComponentListRef Components1;
        std::tie(Components1, MapType, MapModifiers, IsImplicit) = L1;
        auto CI = Components.rbegin();
        auto CE = Components.rend();
        auto SI = Components1.rbegin();
        auto SE = Components1.rend();
        for (; CI != CE && SI != SE; ++CI, ++SI) {
          if (CI->getAssociatedExpression()->getStmtClass() !=
              SI->getAssociatedExpression()->getStmtClass())
            break;
          // Are we dealing with different variables/fields?
          if (CI->getAssociatedDeclaration() != SI->getAssociatedDeclaration())
            break;
        }
        // Found overlapping if, at least for one component, reached the head of
        // the components list.
        if (CI == CE || SI == SE) {
          assert((CI != CE || SI != SE) &&
                 "Unexpected full match of the mapping components.");
          const MapData &BaseData = CI == CE ? L : L1;
          OMPClauseMappableExprCommon::MappableExprComponentListRef SubData =
              SI == SE ? Components : Components1;
          auto &OverlappedElements = OverlappedData.FindAndConstruct(&BaseData);
          OverlappedElements.getSecond().push_back(SubData);
        }
      }
    }
    // Sort the overlapped elements for each item.
    llvm::SmallVector<const FieldDecl *, 4> Layout;
    if (!OverlappedData.empty()) {
      if (const auto *CRD =
              VD->getType().getCanonicalType()->getAsCXXRecordDecl())
        getPlainLayout(CRD, Layout, /*AsBase=*/false);
      else {
        const auto *RD = VD->getType().getCanonicalType()->getAsRecordDecl();
        Layout.append(RD->field_begin(), RD->field_end());
      }
    }
    for (auto &Pair : OverlappedData) {
      llvm::sort(
          Pair.getSecond(),
          [&Layout](
              OMPClauseMappableExprCommon::MappableExprComponentListRef First,
              OMPClauseMappableExprCommon::MappableExprComponentListRef
                  Second) {
            auto CI = First.rbegin();
            auto CE = First.rend();
            auto SI = Second.rbegin();
            auto SE = Second.rend();
            for (; CI != CE && SI != SE; ++CI, ++SI) {
              if (CI->getAssociatedExpression()->getStmtClass() !=
                  SI->getAssociatedExpression()->getStmtClass())
                break;
              // Are we dealing with different variables/fields?
              if (CI->getAssociatedDeclaration() !=
                  SI->getAssociatedDeclaration())
                break;
            }

            // Lists contain the same elements.
            if (CI == CE && SI == SE)
              return false;

            // List with less elements is less than list with more elements.
            if (CI == CE || SI == SE)
              return CI == CE;

            const auto *FD1 = cast<FieldDecl>(CI->getAssociatedDeclaration());
            const auto *FD2 = cast<FieldDecl>(SI->getAssociatedDeclaration());
            if (FD1->getParent() == FD2->getParent())
              return FD1->getFieldIndex() < FD2->getFieldIndex();
            const auto It =
                llvm::find_if(Layout, [FD1, FD2](const FieldDecl *FD) {
                  return FD == FD1 || FD == FD2;
                });
            return *It == FD1;
          });
    }

    // Associated with a capture, because the mapping flags depend on it.
    // Go through all of the elements with the overlapped elements.
    for (const auto &Pair : OverlappedData) {
      const MapData &L = *Pair.getFirst();
      OMPClauseMappableExprCommon::MappableExprComponentListRef Components;
      OpenMPMapClauseKind MapType;
      ArrayRef<OpenMPMapModifierKind> MapModifiers;
      bool IsImplicit;
      std::tie(Components, MapType, MapModifiers, IsImplicit) = L;
      ArrayRef<OMPClauseMappableExprCommon::MappableExprComponentListRef>
          OverlappedComponents = Pair.getSecond();
      bool IsFirstComponentList = true;
      generateInfoForComponentList(MapType, MapModifiers, Components,
                                   BasePointers, Pointers, Sizes, Types,
                                   PartialStruct, IsFirstComponentList,
                                   IsImplicit, OverlappedComponents);
    }
    // Go through other elements without overlapped elements.
    bool IsFirstComponentList = OverlappedData.empty();
    for (const MapData &L : DeclComponentLists) {
      OMPClauseMappableExprCommon::MappableExprComponentListRef Components;
      OpenMPMapClauseKind MapType;
      ArrayRef<OpenMPMapModifierKind> MapModifiers;
      bool IsImplicit;
      std::tie(Components, MapType, MapModifiers, IsImplicit) = L;
      auto It = OverlappedData.find(&L);
      if (It == OverlappedData.end())
        generateInfoForComponentList(MapType, MapModifiers, Components,
                                     BasePointers, Pointers, Sizes, Types,
                                     PartialStruct, IsFirstComponentList,
                                     IsImplicit);
      IsFirstComponentList = false;
    }
  }

  /// Generate the base pointers, section pointers, sizes and map types
  /// associated with the declare target link variables.
  void generateInfoForDeclareTargetLink(MapBaseValuesArrayTy &BasePointers,
                                        MapValuesArrayTy &Pointers,
                                        MapValuesArrayTy &Sizes,
                                        MapFlagsArrayTy &Types) const {
    // Map other list items in the map clause which are not captured variables
    // but "declare target link" global variables.,
    for (const auto *C : this->CurDir.getClausesOfKind<OMPMapClause>()) {
      for (const auto &L : C->component_lists()) {
        if (!L.first)
          continue;
        const auto *VD = dyn_cast<VarDecl>(L.first);
        if (!VD)
          continue;
        llvm::Optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
            OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD);
        if (!Res || *Res != OMPDeclareTargetDeclAttr::MT_Link)
          continue;
        StructRangeInfoTy PartialStruct;
        generateInfoForComponentList(
            C->getMapType(), C->getMapTypeModifiers(), L.second, BasePointers,
            Pointers, Sizes, Types, PartialStruct,
            /*IsFirstComponentList=*/true, C->isImplicit());
        assert(!PartialStruct.Base.isValid() &&
               "No partial structs for declare target link expected.");
      }
    }
  }

  /// Generate the default map information for a given capture \a CI,
  /// record field declaration \a RI and captured value \a CV.
  void generateDefaultMapInfo(const CapturedStmt::Capture &CI,
                              const FieldDecl &RI, llvm::Value *CV,
                              MapBaseValuesArrayTy &CurBasePointers,
                              MapValuesArrayTy &CurPointers,
                              MapValuesArrayTy &CurSizes,
                              MapFlagsArrayTy &CurMapTypes) const {
    // Do the default mapping.
    if (CI.capturesThis()) {
      CurBasePointers.push_back(CV);
      CurPointers.push_back(CV);
      const auto *PtrTy = cast<PointerType>(RI.getType().getTypePtr());
      CurSizes.push_back(CGF.getTypeSize(PtrTy->getPointeeType()));
      // Default map type.
      CurMapTypes.push_back(OMP_MAP_TO | OMP_MAP_FROM);
    } else if (CI.capturesVariableByCopy()) {
      CurBasePointers.push_back(CV);
      CurPointers.push_back(CV);
      if (!RI.getType()->isAnyPointerType()) {
        // We have to signal to the runtime captures passed by value that are
        // not pointers.
        CurMapTypes.push_back(OMP_MAP_LITERAL);
        CurSizes.push_back(CGF.getTypeSize(RI.getType()));
      } else {
        // Pointers are implicitly mapped with a zero size and no flags
        // (other than first map that is added for all implicit maps).
        CurMapTypes.push_back(OMP_MAP_NONE);
        CurSizes.push_back(llvm::Constant::getNullValue(CGF.SizeTy));
      }
    } else {
      assert(CI.capturesVariable() && "Expected captured reference.");
      CurBasePointers.push_back(CV);
      CurPointers.push_back(CV);

      const auto *PtrTy = cast<ReferenceType>(RI.getType().getTypePtr());
      QualType ElementType = PtrTy->getPointeeType();
      CurSizes.push_back(CGF.getTypeSize(ElementType));
      // The default map type for a scalar/complex type is 'to' because by
      // default the value doesn't have to be retrieved. For an aggregate
      // type, the default is 'tofrom'.
      CurMapTypes.push_back(getMapModifiersForPrivateClauses(CI));
    }
    // Every default map produces a single argument which is a target parameter.
    CurMapTypes.back() |= OMP_MAP_TARGET_PARAM;

    // Add flag stating this is an implicit map.
    CurMapTypes.back() |= OMP_MAP_IMPLICIT;
  }
};

enum OpenMPOffloadingReservedDeviceIDs {
  /// Device ID if the device was not defined, runtime should get it
  /// from environment variables in the spec.
  OMP_DEVICEID_UNDEF = -1,
};
} // anonymous namespace

/// Emit the arrays used to pass the captures and map information to the
/// offloading runtime library. If there is no map or capture information,
/// return nullptr by reference.
static void
emitOffloadingArrays(CodeGenFunction &CGF,
                     MappableExprsHandler::MapBaseValuesArrayTy &BasePointers,
                     MappableExprsHandler::MapValuesArrayTy &Pointers,
                     MappableExprsHandler::MapValuesArrayTy &Sizes,
                     MappableExprsHandler::MapFlagsArrayTy &MapTypes,
                     CGOpenMPRuntime::TargetDataInfo &Info) {
  CodeGenModule &CGM = CGF.CGM;
  ASTContext &Ctx = CGF.getContext();

  // Reset the array information.
  Info.clearArrayInfo();
  Info.NumberOfPtrs = BasePointers.size();

  if (Info.NumberOfPtrs) {
    // Detect if we have any capture size requiring runtime evaluation of the
    // size so that a constant array could be eventually used.
    bool hasRuntimeEvaluationCaptureSize = false;
    for (llvm::Value *S : Sizes)
      if (!isa<llvm::Constant>(S)) {
        hasRuntimeEvaluationCaptureSize = true;
        break;
      }

    llvm::APInt PointerNumAP(32, Info.NumberOfPtrs, /*isSigned=*/true);
    QualType PointerArrayType =
        Ctx.getConstantArrayType(Ctx.VoidPtrTy, PointerNumAP, ArrayType::Normal,
                                 /*IndexTypeQuals=*/0);

    Info.BasePointersArray =
        CGF.CreateMemTemp(PointerArrayType, ".offload_baseptrs").getPointer();
    Info.PointersArray =
        CGF.CreateMemTemp(PointerArrayType, ".offload_ptrs").getPointer();

    // If we don't have any VLA types or other types that require runtime
    // evaluation, we can use a constant array for the map sizes, otherwise we
    // need to fill up the arrays as we do for the pointers.
    if (hasRuntimeEvaluationCaptureSize) {
      QualType SizeArrayType = Ctx.getConstantArrayType(
          Ctx.getSizeType(), PointerNumAP, ArrayType::Normal,
          /*IndexTypeQuals=*/0);
      Info.SizesArray =
          CGF.CreateMemTemp(SizeArrayType, ".offload_sizes").getPointer();
    } else {
      // We expect all the sizes to be constant, so we collect them to create
      // a constant array.
      SmallVector<llvm::Constant *, 16> ConstSizes;
      for (llvm::Value *S : Sizes)
        ConstSizes.push_back(cast<llvm::Constant>(S));

      auto *SizesArrayInit = llvm::ConstantArray::get(
          llvm::ArrayType::get(CGM.SizeTy, ConstSizes.size()), ConstSizes);
      std::string Name = CGM.getOpenMPRuntime().getName({"offload_sizes"});
      auto *SizesArrayGbl = new llvm::GlobalVariable(
          CGM.getModule(), SizesArrayInit->getType(),
          /*isConstant=*/true, llvm::GlobalValue::PrivateLinkage,
          SizesArrayInit, Name);
      SizesArrayGbl->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
      Info.SizesArray = SizesArrayGbl;
    }

    // The map types are always constant so we don't need to generate code to
    // fill arrays. Instead, we create an array constant.
    SmallVector<uint64_t, 4> Mapping(MapTypes.size(), 0);
    llvm::copy(MapTypes, Mapping.begin());
    llvm::Constant *MapTypesArrayInit =
        llvm::ConstantDataArray::get(CGF.Builder.getContext(), Mapping);
    std::string MaptypesName =
        CGM.getOpenMPRuntime().getName({"offload_maptypes"});
    auto *MapTypesArrayGbl = new llvm::GlobalVariable(
        CGM.getModule(), MapTypesArrayInit->getType(),
        /*isConstant=*/true, llvm::GlobalValue::PrivateLinkage,
        MapTypesArrayInit, MaptypesName);
    MapTypesArrayGbl->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    Info.MapTypesArray = MapTypesArrayGbl;

    for (unsigned I = 0; I < Info.NumberOfPtrs; ++I) {
      llvm::Value *BPVal = *BasePointers[I];
      llvm::Value *BP = CGF.Builder.CreateConstInBoundsGEP2_32(
          llvm::ArrayType::get(CGM.VoidPtrTy, Info.NumberOfPtrs),
          Info.BasePointersArray, 0, I);
      BP = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
          BP, BPVal->getType()->getPointerTo(/*AddrSpace=*/0));
      Address BPAddr(BP, Ctx.getTypeAlignInChars(Ctx.VoidPtrTy));
      CGF.Builder.CreateStore(BPVal, BPAddr);

      if (Info.requiresDevicePointerInfo())
        if (const ValueDecl *DevVD = BasePointers[I].getDevicePtrDecl())
          Info.CaptureDeviceAddrMap.try_emplace(DevVD, BPAddr);

      llvm::Value *PVal = Pointers[I];
      llvm::Value *P = CGF.Builder.CreateConstInBoundsGEP2_32(
          llvm::ArrayType::get(CGM.VoidPtrTy, Info.NumberOfPtrs),
          Info.PointersArray, 0, I);
      P = CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
          P, PVal->getType()->getPointerTo(/*AddrSpace=*/0));
      Address PAddr(P, Ctx.getTypeAlignInChars(Ctx.VoidPtrTy));
      CGF.Builder.CreateStore(PVal, PAddr);

      if (hasRuntimeEvaluationCaptureSize) {
        llvm::Value *S = CGF.Builder.CreateConstInBoundsGEP2_32(
            llvm::ArrayType::get(CGM.SizeTy, Info.NumberOfPtrs),
            Info.SizesArray,
            /*Idx0=*/0,
            /*Idx1=*/I);
        Address SAddr(S, Ctx.getTypeAlignInChars(Ctx.getSizeType()));
        CGF.Builder.CreateStore(
            CGF.Builder.CreateIntCast(Sizes[I], CGM.SizeTy, /*isSigned=*/true),
            SAddr);
      }
    }
  }
}
/// Emit the arguments to be passed to the runtime library based on the
/// arrays of pointers, sizes and map types.
static void emitOffloadingArraysArgument(
    CodeGenFunction &CGF, llvm::Value *&BasePointersArrayArg,
    llvm::Value *&PointersArrayArg, llvm::Value *&SizesArrayArg,
    llvm::Value *&MapTypesArrayArg, CGOpenMPRuntime::TargetDataInfo &Info) {
  CodeGenModule &CGM = CGF.CGM;
  if (Info.NumberOfPtrs) {
    BasePointersArrayArg = CGF.Builder.CreateConstInBoundsGEP2_32(
        llvm::ArrayType::get(CGM.VoidPtrTy, Info.NumberOfPtrs),
        Info.BasePointersArray,
        /*Idx0=*/0, /*Idx1=*/0);
    PointersArrayArg = CGF.Builder.CreateConstInBoundsGEP2_32(
        llvm::ArrayType::get(CGM.VoidPtrTy, Info.NumberOfPtrs),
        Info.PointersArray,
        /*Idx0=*/0,
        /*Idx1=*/0);
    SizesArrayArg = CGF.Builder.CreateConstInBoundsGEP2_32(
        llvm::ArrayType::get(CGM.SizeTy, Info.NumberOfPtrs), Info.SizesArray,
        /*Idx0=*/0, /*Idx1=*/0);
    MapTypesArrayArg = CGF.Builder.CreateConstInBoundsGEP2_32(
        llvm::ArrayType::get(CGM.Int64Ty, Info.NumberOfPtrs),
        Info.MapTypesArray,
        /*Idx0=*/0,
        /*Idx1=*/0);
  } else {
    BasePointersArrayArg = llvm::ConstantPointerNull::get(CGM.VoidPtrPtrTy);
    PointersArrayArg = llvm::ConstantPointerNull::get(CGM.VoidPtrPtrTy);
    SizesArrayArg = llvm::ConstantPointerNull::get(CGM.SizeTy->getPointerTo());
    MapTypesArrayArg =
        llvm::ConstantPointerNull::get(CGM.Int64Ty->getPointerTo());
  }
}

/// Checks if the expression is constant or does not have non-trivial function
/// calls.
static bool isTrivial(ASTContext &Ctx, const Expr * E) {
  // We can skip constant expressions.
  // We can skip expressions with trivial calls or simple expressions.
  return (E->isEvaluatable(Ctx, Expr::SE_AllowUndefinedBehavior) ||
          !E->hasNonTrivialCall(Ctx)) &&
         !E->HasSideEffects(Ctx, /*IncludePossibleEffects=*/true);
}

/// Checks if the \p Body is the \a CompoundStmt and returns its child statement
/// iff there is only one that is not evaluatable at the compile time.
static const Stmt *getSingleCompoundChild(ASTContext &Ctx, const Stmt *Body) {
  if (const auto *C = dyn_cast<CompoundStmt>(Body)) {
    const Stmt *Child = nullptr;
    for (const Stmt *S : C->body()) {
      if (const auto *E = dyn_cast<Expr>(S)) {
        if (isTrivial(Ctx, E))
          continue;
      }
      // Some of the statements can be ignored.
      if (isa<AsmStmt>(S) || isa<NullStmt>(S) || isa<OMPFlushDirective>(S) ||
          isa<OMPBarrierDirective>(S) || isa<OMPTaskyieldDirective>(S))
        continue;
      // Analyze declarations.
      if (const auto *DS = dyn_cast<DeclStmt>(S)) {
        if (llvm::all_of(DS->decls(), [&Ctx](const Decl *D) {
              if (isa<EmptyDecl>(D) || isa<DeclContext>(D) ||
                  isa<TypeDecl>(D) || isa<PragmaCommentDecl>(D) ||
                  isa<PragmaDetectMismatchDecl>(D) || isa<UsingDecl>(D) ||
                  isa<UsingDirectiveDecl>(D) ||
                  isa<OMPDeclareReductionDecl>(D) ||
                  isa<OMPThreadPrivateDecl>(D))
                return true;
              const auto *VD = dyn_cast<VarDecl>(D);
              if (!VD)
                return false;
              return VD->isConstexpr() ||
                     ((VD->getType().isTrivialType(Ctx) ||
                       VD->getType()->isReferenceType()) &&
                      (!VD->hasInit() || isTrivial(Ctx, VD->getInit())));
            }))
          continue;
      }
      // Found multiple children - cannot get the one child only.
      if (Child)
        return Body;
      Child = S;
    }
    if (Child)
      return Child;
  }
  return Body;
}

/// Check for inner distribute directive.
static const OMPExecutableDirective *
getNestedDistributeDirective(ASTContext &Ctx, const OMPExecutableDirective &D) {
  const auto *CS = D.getInnermostCapturedStmt();
  const auto *Body =
      CS->getCapturedStmt()->IgnoreContainers(/*IgnoreCaptured=*/true);
  const Stmt *ChildStmt = getSingleCompoundChild(Ctx, Body);

  if (const auto *NestedDir = dyn_cast<OMPExecutableDirective>(ChildStmt)) {
    OpenMPDirectiveKind DKind = NestedDir->getDirectiveKind();
    switch (D.getDirectiveKind()) {
    case OMPD_target:
      if (isOpenMPDistributeDirective(DKind))
        return NestedDir;
      if (DKind == OMPD_teams) {
        Body = NestedDir->getInnermostCapturedStmt()->IgnoreContainers(
            /*IgnoreCaptured=*/true);
        if (!Body)
          return nullptr;
        ChildStmt = getSingleCompoundChild(Ctx, Body);
        if (const auto *NND = dyn_cast<OMPExecutableDirective>(ChildStmt)) {
          DKind = NND->getDirectiveKind();
          if (isOpenMPDistributeDirective(DKind))
            return NND;
        }
      }
      return nullptr;
    case OMPD_target_teams:
      if (isOpenMPDistributeDirective(DKind))
        return NestedDir;
      return nullptr;
    case OMPD_target_parallel:
    case OMPD_target_simd:
    case OMPD_target_parallel_for:
    case OMPD_target_parallel_for_simd:
      return nullptr;
    case OMPD_target_teams_distribute:
    case OMPD_target_teams_distribute_simd:
    case OMPD_target_teams_distribute_parallel_for:
    case OMPD_target_teams_distribute_parallel_for_simd:
    case OMPD_parallel:
    case OMPD_for:
    case OMPD_parallel_for:
    case OMPD_parallel_sections:
    case OMPD_for_simd:
    case OMPD_parallel_for_simd:
    case OMPD_cancel:
    case OMPD_cancellation_point:
    case OMPD_ordered:
    case OMPD_threadprivate:
    case OMPD_task:
    case OMPD_simd:
    case OMPD_sections:
    case OMPD_section:
    case OMPD_single:
    case OMPD_master:
    case OMPD_critical:
    case OMPD_taskyield:
    case OMPD_barrier:
    case OMPD_taskwait:
    case OMPD_taskgroup:
    case OMPD_atomic:
    case OMPD_flush:
    case OMPD_teams:
    case OMPD_target_data:
    case OMPD_target_exit_data:
    case OMPD_target_enter_data:
    case OMPD_distribute:
    case OMPD_distribute_simd:
    case OMPD_distribute_parallel_for:
    case OMPD_distribute_parallel_for_simd:
    case OMPD_teams_distribute:
    case OMPD_teams_distribute_simd:
    case OMPD_teams_distribute_parallel_for:
    case OMPD_teams_distribute_parallel_for_simd:
    case OMPD_target_update:
    case OMPD_declare_simd:
    case OMPD_declare_target:
    case OMPD_end_declare_target:
    case OMPD_declare_reduction:
    case OMPD_taskloop:
    case OMPD_taskloop_simd:
    case OMPD_requires:
    case OMPD_unknown:
      llvm_unreachable("Unexpected directive.");
    }
  }

  return nullptr;
}

void CGOpenMPRuntime::emitTargetNumIterationsCall(
    CodeGenFunction &CGF, const OMPExecutableDirective &D, const Expr *Device,
    const llvm::function_ref<llvm::Value *(
        CodeGenFunction &CGF, const OMPLoopDirective &D)> &SizeEmitter) {
  OpenMPDirectiveKind Kind = D.getDirectiveKind();
  const OMPExecutableDirective *TD = &D;
  // Get nested teams distribute kind directive, if any.
  if (!isOpenMPDistributeDirective(Kind) || !isOpenMPTeamsDirective(Kind))
    TD = getNestedDistributeDirective(CGM.getContext(), D);
  if (!TD)
    return;
  const auto *LD = cast<OMPLoopDirective>(TD);
  auto &&CodeGen = [LD, &Device, &SizeEmitter, this](CodeGenFunction &CGF,
                                                     PrePostActionTy &) {
    llvm::Value *NumIterations = SizeEmitter(CGF, *LD);

    // Emit device ID if any.
    llvm::Value *DeviceID;
    if (Device)
      DeviceID = CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(Device),
                                           CGF.Int64Ty, /*isSigned=*/true);
    else
      DeviceID = CGF.Builder.getInt64(OMP_DEVICEID_UNDEF);

    llvm::Value *Args[] = {DeviceID, NumIterations};
    CGF.EmitRuntimeCall(
        createRuntimeFunction(OMPRTL__kmpc_push_target_tripcount), Args);
  };
  emitInlinedDirective(CGF, OMPD_unknown, CodeGen);
}

void CGOpenMPRuntime::emitTargetCall(CodeGenFunction &CGF,
                                     const OMPExecutableDirective &D,
                                     llvm::Value *OutlinedFn,
                                     llvm::Value *OutlinedFnID,
                                     const Expr *IfCond, const Expr *Device) {
  if (!CGF.HaveInsertPoint())
    return;

  assert(OutlinedFn && "Invalid outlined function!");

  const bool RequiresOuterTask = D.hasClausesOfKind<OMPDependClause>();
  llvm::SmallVector<llvm::Value *, 16> CapturedVars;
  const CapturedStmt &CS = *D.getCapturedStmt(OMPD_target);
  auto &&ArgsCodegen = [&CS, &CapturedVars](CodeGenFunction &CGF,
                                            PrePostActionTy &) {
    CGF.GenerateOpenMPCapturedVars(CS, CapturedVars);
  };
  emitInlinedDirective(CGF, OMPD_unknown, ArgsCodegen);

  CodeGenFunction::OMPTargetDataInfo InputInfo;
  llvm::Value *MapTypesArray = nullptr;
  // Fill up the pointer arrays and transfer execution to the device.
  auto &&ThenGen = [this, Device, OutlinedFn, OutlinedFnID, &D, &InputInfo,
                    &MapTypesArray, &CS, RequiresOuterTask,
                    &CapturedVars](CodeGenFunction &CGF, PrePostActionTy &) {
    // On top of the arrays that were filled up, the target offloading call
    // takes as arguments the device id as well as the host pointer. The host
    // pointer is used by the runtime library to identify the current target
    // region, so it only has to be unique and not necessarily point to
    // anything. It could be the pointer to the outlined function that
    // implements the target region, but we aren't using that so that the
    // compiler doesn't need to keep that, and could therefore inline the host
    // function if proven worthwhile during optimization.

    // From this point on, we need to have an ID of the target region defined.
    assert(OutlinedFnID && "Invalid outlined function ID!");

    // Emit device ID if any.
    llvm::Value *DeviceID;
    if (Device) {
      DeviceID = CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(Device),
                                           CGF.Int64Ty, /*isSigned=*/true);
    } else {
      DeviceID = CGF.Builder.getInt64(OMP_DEVICEID_UNDEF);
    }

    // Emit the number of elements in the offloading arrays.
    llvm::Value *PointerNum =
        CGF.Builder.getInt32(InputInfo.NumberOfTargetItems);

    // Return value of the runtime offloading call.
    llvm::Value *Return;

    llvm::Value *NumTeams = emitNumTeamsForTargetDirective(*this, CGF, D);
    llvm::Value *NumThreads = emitNumThreadsForTargetDirective(*this, CGF, D);

    bool HasNowait = D.hasClausesOfKind<OMPNowaitClause>();
    // The target region is an outlined function launched by the runtime
    // via calls __tgt_target() or __tgt_target_teams().
    //
    // __tgt_target() launches a target region with one team and one thread,
    // executing a serial region.  This master thread may in turn launch
    // more threads within its team upon encountering a parallel region,
    // however, no additional teams can be launched on the device.
    //
    // __tgt_target_teams() launches a target region with one or more teams,
    // each with one or more threads.  This call is required for target
    // constructs such as:
    //  'target teams'
    //  'target' / 'teams'
    //  'target teams distribute parallel for'
    //  'target parallel'
    // and so on.
    //
    // Note that on the host and CPU targets, the runtime implementation of
    // these calls simply call the outlined function without forking threads.
    // The outlined functions themselves have runtime calls to
    // __kmpc_fork_teams() and __kmpc_fork() for this purpose, codegen'd by
    // the compiler in emitTeamsCall() and emitParallelCall().
    //
    // In contrast, on the NVPTX target, the implementation of
    // __tgt_target_teams() launches a GPU kernel with the requested number
    // of teams and threads so no additional calls to the runtime are required.
    if (NumTeams) {
      // If we have NumTeams defined this means that we have an enclosed teams
      // region. Therefore we also expect to have NumThreads defined. These two
      // values should be defined in the presence of a teams directive,
      // regardless of having any clauses associated. If the user is using teams
      // but no clauses, these two values will be the default that should be
      // passed to the runtime library - a 32-bit integer with the value zero.
      assert(NumThreads && "Thread limit expression should be available along "
                           "with number of teams.");
      llvm::Value *OffloadingArgs[] = {DeviceID,
                                       OutlinedFnID,
                                       PointerNum,
                                       InputInfo.BasePointersArray.getPointer(),
                                       InputInfo.PointersArray.getPointer(),
                                       InputInfo.SizesArray.getPointer(),
                                       MapTypesArray,
                                       NumTeams,
                                       NumThreads};
      Return = CGF.EmitRuntimeCall(
          createRuntimeFunction(HasNowait ? OMPRTL__tgt_target_teams_nowait
                                          : OMPRTL__tgt_target_teams),
          OffloadingArgs);
    } else {
      llvm::Value *OffloadingArgs[] = {DeviceID,
                                       OutlinedFnID,
                                       PointerNum,
                                       InputInfo.BasePointersArray.getPointer(),
                                       InputInfo.PointersArray.getPointer(),
                                       InputInfo.SizesArray.getPointer(),
                                       MapTypesArray};
      Return = CGF.EmitRuntimeCall(
          createRuntimeFunction(HasNowait ? OMPRTL__tgt_target_nowait
                                          : OMPRTL__tgt_target),
          OffloadingArgs);
    }

    // Check the error code and execute the host version if required.
    llvm::BasicBlock *OffloadFailedBlock =
        CGF.createBasicBlock("omp_offload.failed");
    llvm::BasicBlock *OffloadContBlock =
        CGF.createBasicBlock("omp_offload.cont");
    llvm::Value *Failed = CGF.Builder.CreateIsNotNull(Return);
    CGF.Builder.CreateCondBr(Failed, OffloadFailedBlock, OffloadContBlock);

    CGF.EmitBlock(OffloadFailedBlock);
    if (RequiresOuterTask) {
      CapturedVars.clear();
      CGF.GenerateOpenMPCapturedVars(CS, CapturedVars);
    }
    emitOutlinedFunctionCall(CGF, D.getBeginLoc(), OutlinedFn, CapturedVars);
    CGF.EmitBranch(OffloadContBlock);

    CGF.EmitBlock(OffloadContBlock, /*IsFinished=*/true);
  };

  // Notify that the host version must be executed.
  auto &&ElseGen = [this, &D, OutlinedFn, &CS, &CapturedVars,
                    RequiresOuterTask](CodeGenFunction &CGF,
                                       PrePostActionTy &) {
    if (RequiresOuterTask) {
      CapturedVars.clear();
      CGF.GenerateOpenMPCapturedVars(CS, CapturedVars);
    }
    emitOutlinedFunctionCall(CGF, D.getBeginLoc(), OutlinedFn, CapturedVars);
  };

  auto &&TargetThenGen = [this, &ThenGen, &D, &InputInfo, &MapTypesArray,
                          &CapturedVars, RequiresOuterTask,
                          &CS](CodeGenFunction &CGF, PrePostActionTy &) {
    // Fill up the arrays with all the captured variables.
    MappableExprsHandler::MapBaseValuesArrayTy BasePointers;
    MappableExprsHandler::MapValuesArrayTy Pointers;
    MappableExprsHandler::MapValuesArrayTy Sizes;
    MappableExprsHandler::MapFlagsArrayTy MapTypes;

    // Get mappable expression information.
    MappableExprsHandler MEHandler(D, CGF);
    llvm::DenseMap<llvm::Value *, llvm::Value *> LambdaPointers;

    auto RI = CS.getCapturedRecordDecl()->field_begin();
    auto CV = CapturedVars.begin();
    for (CapturedStmt::const_capture_iterator CI = CS.capture_begin(),
                                              CE = CS.capture_end();
         CI != CE; ++CI, ++RI, ++CV) {
      MappableExprsHandler::MapBaseValuesArrayTy CurBasePointers;
      MappableExprsHandler::MapValuesArrayTy CurPointers;
      MappableExprsHandler::MapValuesArrayTy CurSizes;
      MappableExprsHandler::MapFlagsArrayTy CurMapTypes;
      MappableExprsHandler::StructRangeInfoTy PartialStruct;

      // VLA sizes are passed to the outlined region by copy and do not have map
      // information associated.
      if (CI->capturesVariableArrayType()) {
        CurBasePointers.push_back(*CV);
        CurPointers.push_back(*CV);
        CurSizes.push_back(CGF.getTypeSize(RI->getType()));
        // Copy to the device as an argument. No need to retrieve it.
        CurMapTypes.push_back(MappableExprsHandler::OMP_MAP_LITERAL |
                              MappableExprsHandler::OMP_MAP_TARGET_PARAM);
      } else {
        // If we have any information in the map clause, we use it, otherwise we
        // just do a default mapping.
        MEHandler.generateInfoForCapture(CI, *CV, CurBasePointers, CurPointers,
                                         CurSizes, CurMapTypes, PartialStruct);
        if (CurBasePointers.empty())
          MEHandler.generateDefaultMapInfo(*CI, **RI, *CV, CurBasePointers,
                                           CurPointers, CurSizes, CurMapTypes);
        // Generate correct mapping for variables captured by reference in
        // lambdas.
        if (CI->capturesVariable())
          MEHandler.generateInfoForLambdaCaptures(
              CI->getCapturedVar(), *CV, CurBasePointers, CurPointers, CurSizes,
              CurMapTypes, LambdaPointers);
      }
      // We expect to have at least an element of information for this capture.
      assert(!CurBasePointers.empty() &&
             "Non-existing map pointer for capture!");
      assert(CurBasePointers.size() == CurPointers.size() &&
             CurBasePointers.size() == CurSizes.size() &&
             CurBasePointers.size() == CurMapTypes.size() &&
             "Inconsistent map information sizes!");

      // If there is an entry in PartialStruct it means we have a struct with
      // individual members mapped. Emit an extra combined entry.
      if (PartialStruct.Base.isValid())
        MEHandler.emitCombinedEntry(BasePointers, Pointers, Sizes, MapTypes,
                                    CurMapTypes, PartialStruct);

      // We need to append the results of this capture to what we already have.
      BasePointers.append(CurBasePointers.begin(), CurBasePointers.end());
      Pointers.append(CurPointers.begin(), CurPointers.end());
      Sizes.append(CurSizes.begin(), CurSizes.end());
      MapTypes.append(CurMapTypes.begin(), CurMapTypes.end());
    }
    // Adjust MEMBER_OF flags for the lambdas captures.
    MEHandler.adjustMemberOfForLambdaCaptures(LambdaPointers, BasePointers,
                                              Pointers, MapTypes);
    // Map other list items in the map clause which are not captured variables
    // but "declare target link" global variables.
    MEHandler.generateInfoForDeclareTargetLink(BasePointers, Pointers, Sizes,
                                               MapTypes);

    TargetDataInfo Info;
    // Fill up the arrays and create the arguments.
    emitOffloadingArrays(CGF, BasePointers, Pointers, Sizes, MapTypes, Info);
    emitOffloadingArraysArgument(CGF, Info.BasePointersArray,
                                 Info.PointersArray, Info.SizesArray,
                                 Info.MapTypesArray, Info);
    InputInfo.NumberOfTargetItems = Info.NumberOfPtrs;
    InputInfo.BasePointersArray =
        Address(Info.BasePointersArray, CGM.getPointerAlign());
    InputInfo.PointersArray =
        Address(Info.PointersArray, CGM.getPointerAlign());
    InputInfo.SizesArray = Address(Info.SizesArray, CGM.getPointerAlign());
    MapTypesArray = Info.MapTypesArray;
    if (RequiresOuterTask)
      CGF.EmitOMPTargetTaskBasedDirective(D, ThenGen, InputInfo);
    else
      emitInlinedDirective(CGF, D.getDirectiveKind(), ThenGen);
  };

  auto &&TargetElseGen = [this, &ElseGen, &D, RequiresOuterTask](
                             CodeGenFunction &CGF, PrePostActionTy &) {
    if (RequiresOuterTask) {
      CodeGenFunction::OMPTargetDataInfo InputInfo;
      CGF.EmitOMPTargetTaskBasedDirective(D, ElseGen, InputInfo);
    } else {
      emitInlinedDirective(CGF, D.getDirectiveKind(), ElseGen);
    }
  };

  // If we have a target function ID it means that we need to support
  // offloading, otherwise, just execute on the host. We need to execute on host
  // regardless of the conditional in the if clause if, e.g., the user do not
  // specify target triples.
  if (OutlinedFnID) {
    if (IfCond) {
      emitOMPIfClause(CGF, IfCond, TargetThenGen, TargetElseGen);
    } else {
      RegionCodeGenTy ThenRCG(TargetThenGen);
      ThenRCG(CGF);
    }
  } else {
    RegionCodeGenTy ElseRCG(TargetElseGen);
    ElseRCG(CGF);
  }
}

void CGOpenMPRuntime::scanForTargetRegionsFunctions(const Stmt *S,
                                                    StringRef ParentName) {
  if (!S)
    return;

  // Codegen OMP target directives that offload compute to the device.
  bool RequiresDeviceCodegen =
      isa<OMPExecutableDirective>(S) &&
      isOpenMPTargetExecutionDirective(
          cast<OMPExecutableDirective>(S)->getDirectiveKind());

  if (RequiresDeviceCodegen) {
    const auto &E = *cast<OMPExecutableDirective>(S);
    unsigned DeviceID;
    unsigned FileID;
    unsigned Line;
    getTargetEntryUniqueInfo(CGM.getContext(), E.getBeginLoc(), DeviceID,
                             FileID, Line);

    // Is this a target region that should not be emitted as an entry point? If
    // so just signal we are done with this target region.
    if (!OffloadEntriesInfoManager.hasTargetRegionEntryInfo(DeviceID, FileID,
                                                            ParentName, Line))
      return;

    switch (E.getDirectiveKind()) {
    case OMPD_target:
      CodeGenFunction::EmitOMPTargetDeviceFunction(CGM, ParentName,
                                                   cast<OMPTargetDirective>(E));
      break;
    case OMPD_target_parallel:
      CodeGenFunction::EmitOMPTargetParallelDeviceFunction(
          CGM, ParentName, cast<OMPTargetParallelDirective>(E));
      break;
    case OMPD_target_teams:
      CodeGenFunction::EmitOMPTargetTeamsDeviceFunction(
          CGM, ParentName, cast<OMPTargetTeamsDirective>(E));
      break;
    case OMPD_target_teams_distribute:
      CodeGenFunction::EmitOMPTargetTeamsDistributeDeviceFunction(
          CGM, ParentName, cast<OMPTargetTeamsDistributeDirective>(E));
      break;
    case OMPD_target_teams_distribute_simd:
      CodeGenFunction::EmitOMPTargetTeamsDistributeSimdDeviceFunction(
          CGM, ParentName, cast<OMPTargetTeamsDistributeSimdDirective>(E));
      break;
    case OMPD_target_parallel_for:
      CodeGenFunction::EmitOMPTargetParallelForDeviceFunction(
          CGM, ParentName, cast<OMPTargetParallelForDirective>(E));
      break;
    case OMPD_target_parallel_for_simd:
      CodeGenFunction::EmitOMPTargetParallelForSimdDeviceFunction(
          CGM, ParentName, cast<OMPTargetParallelForSimdDirective>(E));
      break;
    case OMPD_target_simd:
      CodeGenFunction::EmitOMPTargetSimdDeviceFunction(
          CGM, ParentName, cast<OMPTargetSimdDirective>(E));
      break;
    case OMPD_target_teams_distribute_parallel_for:
      CodeGenFunction::EmitOMPTargetTeamsDistributeParallelForDeviceFunction(
          CGM, ParentName,
          cast<OMPTargetTeamsDistributeParallelForDirective>(E));
      break;
    case OMPD_target_teams_distribute_parallel_for_simd:
      CodeGenFunction::
          EmitOMPTargetTeamsDistributeParallelForSimdDeviceFunction(
              CGM, ParentName,
              cast<OMPTargetTeamsDistributeParallelForSimdDirective>(E));
      break;
    case OMPD_parallel:
    case OMPD_for:
    case OMPD_parallel_for:
    case OMPD_parallel_sections:
    case OMPD_for_simd:
    case OMPD_parallel_for_simd:
    case OMPD_cancel:
    case OMPD_cancellation_point:
    case OMPD_ordered:
    case OMPD_threadprivate:
    case OMPD_task:
    case OMPD_simd:
    case OMPD_sections:
    case OMPD_section:
    case OMPD_single:
    case OMPD_master:
    case OMPD_critical:
    case OMPD_taskyield:
    case OMPD_barrier:
    case OMPD_taskwait:
    case OMPD_taskgroup:
    case OMPD_atomic:
    case OMPD_flush:
    case OMPD_teams:
    case OMPD_target_data:
    case OMPD_target_exit_data:
    case OMPD_target_enter_data:
    case OMPD_distribute:
    case OMPD_distribute_simd:
    case OMPD_distribute_parallel_for:
    case OMPD_distribute_parallel_for_simd:
    case OMPD_teams_distribute:
    case OMPD_teams_distribute_simd:
    case OMPD_teams_distribute_parallel_for:
    case OMPD_teams_distribute_parallel_for_simd:
    case OMPD_target_update:
    case OMPD_declare_simd:
    case OMPD_declare_target:
    case OMPD_end_declare_target:
    case OMPD_declare_reduction:
    case OMPD_taskloop:
    case OMPD_taskloop_simd:
    case OMPD_requires:
    case OMPD_unknown:
      llvm_unreachable("Unknown target directive for OpenMP device codegen.");
    }
    return;
  }

  if (const auto *E = dyn_cast<OMPExecutableDirective>(S)) {
    if (!E->hasAssociatedStmt() || !E->getAssociatedStmt())
      return;

    scanForTargetRegionsFunctions(
        E->getInnermostCapturedStmt()->getCapturedStmt(), ParentName);
    return;
  }

  // If this is a lambda function, look into its body.
  if (const auto *L = dyn_cast<LambdaExpr>(S))
    S = L->getBody();

  // Keep looking for target regions recursively.
  for (const Stmt *II : S->children())
    scanForTargetRegionsFunctions(II, ParentName);
}

bool CGOpenMPRuntime::emitTargetFunctions(GlobalDecl GD) {
  // If emitting code for the host, we do not process FD here. Instead we do
  // the normal code generation.
  if (!CGM.getLangOpts().OpenMPIsDevice)
    return false;

  const ValueDecl *VD = cast<ValueDecl>(GD.getDecl());
  StringRef Name = CGM.getMangledName(GD);
  // Try to detect target regions in the function.
  if (const auto *FD = dyn_cast<FunctionDecl>(VD))
    scanForTargetRegionsFunctions(FD->getBody(), Name);

  // Do not to emit function if it is not marked as declare target.
  return !OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD) &&
         AlreadyEmittedTargetFunctions.count(Name) == 0;
}

bool CGOpenMPRuntime::emitTargetGlobalVariable(GlobalDecl GD) {
  if (!CGM.getLangOpts().OpenMPIsDevice)
    return false;

  // Check if there are Ctors/Dtors in this declaration and look for target
  // regions in it. We use the complete variant to produce the kernel name
  // mangling.
  QualType RDTy = cast<VarDecl>(GD.getDecl())->getType();
  if (const auto *RD = RDTy->getBaseElementTypeUnsafe()->getAsCXXRecordDecl()) {
    for (const CXXConstructorDecl *Ctor : RD->ctors()) {
      StringRef ParentName =
          CGM.getMangledName(GlobalDecl(Ctor, Ctor_Complete));
      scanForTargetRegionsFunctions(Ctor->getBody(), ParentName);
    }
    if (const CXXDestructorDecl *Dtor = RD->getDestructor()) {
      StringRef ParentName =
          CGM.getMangledName(GlobalDecl(Dtor, Dtor_Complete));
      scanForTargetRegionsFunctions(Dtor->getBody(), ParentName);
    }
  }

  // Do not to emit variable if it is not marked as declare target.
  llvm::Optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(
          cast<VarDecl>(GD.getDecl()));
  if (!Res || *Res == OMPDeclareTargetDeclAttr::MT_Link) {
    DeferredGlobalVariables.insert(cast<VarDecl>(GD.getDecl()));
    return true;
  }
  return false;
}

void CGOpenMPRuntime::registerTargetGlobalVariable(const VarDecl *VD,
                                                   llvm::Constant *Addr) {
  llvm::Optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD);
  if (!Res) {
    if (CGM.getLangOpts().OpenMPIsDevice) {
      // Register non-target variables being emitted in device code (debug info
      // may cause this).
      StringRef VarName = CGM.getMangledName(VD);
      EmittedNonTargetVariables.try_emplace(VarName, Addr);
    }
    return;
  }
  // Register declare target variables.
  OffloadEntriesInfoManagerTy::OMPTargetGlobalVarEntryKind Flags;
  StringRef VarName;
  CharUnits VarSize;
  llvm::GlobalValue::LinkageTypes Linkage;
  switch (*Res) {
  case OMPDeclareTargetDeclAttr::MT_To:
    Flags = OffloadEntriesInfoManagerTy::OMPTargetGlobalVarEntryTo;
    VarName = CGM.getMangledName(VD);
    if (VD->hasDefinition(CGM.getContext()) != VarDecl::DeclarationOnly) {
      VarSize = CGM.getContext().getTypeSizeInChars(VD->getType());
      assert(!VarSize.isZero() && "Expected non-zero size of the variable");
    } else {
      VarSize = CharUnits::Zero();
    }
    Linkage = CGM.getLLVMLinkageVarDefinition(VD, /*IsConstant=*/false);
    // Temp solution to prevent optimizations of the internal variables.
    if (CGM.getLangOpts().OpenMPIsDevice && !VD->isExternallyVisible()) {
      std::string RefName = getName({VarName, "ref"});
      if (!CGM.GetGlobalValue(RefName)) {
        llvm::Constant *AddrRef =
            getOrCreateInternalVariable(Addr->getType(), RefName);
        auto *GVAddrRef = cast<llvm::GlobalVariable>(AddrRef);
        GVAddrRef->setConstant(/*Val=*/true);
        GVAddrRef->setLinkage(llvm::GlobalValue::InternalLinkage);
        GVAddrRef->setInitializer(Addr);
        CGM.addCompilerUsedGlobal(GVAddrRef);
      }
    }
    break;
  case OMPDeclareTargetDeclAttr::MT_Link:
    Flags = OffloadEntriesInfoManagerTy::OMPTargetGlobalVarEntryLink;
    if (CGM.getLangOpts().OpenMPIsDevice) {
      VarName = Addr->getName();
      Addr = nullptr;
    } else {
      VarName = getAddrOfDeclareTargetLink(VD).getName();
      Addr = cast<llvm::Constant>(getAddrOfDeclareTargetLink(VD).getPointer());
    }
    VarSize = CGM.getPointerSize();
    Linkage = llvm::GlobalValue::WeakAnyLinkage;
    break;
  }
  OffloadEntriesInfoManager.registerDeviceGlobalVarEntryInfo(
      VarName, Addr, VarSize, Flags, Linkage);
}

bool CGOpenMPRuntime::emitTargetGlobal(GlobalDecl GD) {
  if (isa<FunctionDecl>(GD.getDecl()) ||
      isa<OMPDeclareReductionDecl>(GD.getDecl()))
    return emitTargetFunctions(GD);

  return emitTargetGlobalVariable(GD);
}

void CGOpenMPRuntime::emitDeferredTargetDecls() const {
  for (const VarDecl *VD : DeferredGlobalVariables) {
    llvm::Optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
        OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD);
    if (!Res)
      continue;
    if (*Res == OMPDeclareTargetDeclAttr::MT_To) {
      CGM.EmitGlobal(VD);
    } else {
      assert(*Res == OMPDeclareTargetDeclAttr::MT_Link &&
             "Expected to or link clauses.");
      (void)CGM.getOpenMPRuntime().getAddrOfDeclareTargetLink(VD);
    }
  }
}

void CGOpenMPRuntime::adjustTargetSpecificDataForLambdas(
    CodeGenFunction &CGF, const OMPExecutableDirective &D) const {
  assert(isOpenMPTargetExecutionDirective(D.getDirectiveKind()) &&
         " Expected target-based directive.");
}

CGOpenMPRuntime::DisableAutoDeclareTargetRAII::DisableAutoDeclareTargetRAII(
    CodeGenModule &CGM)
    : CGM(CGM) {
  if (CGM.getLangOpts().OpenMPIsDevice) {
    SavedShouldMarkAsGlobal = CGM.getOpenMPRuntime().ShouldMarkAsGlobal;
    CGM.getOpenMPRuntime().ShouldMarkAsGlobal = false;
  }
}

CGOpenMPRuntime::DisableAutoDeclareTargetRAII::~DisableAutoDeclareTargetRAII() {
  if (CGM.getLangOpts().OpenMPIsDevice)
    CGM.getOpenMPRuntime().ShouldMarkAsGlobal = SavedShouldMarkAsGlobal;
}

bool CGOpenMPRuntime::markAsGlobalTarget(GlobalDecl GD) {
  if (!CGM.getLangOpts().OpenMPIsDevice || !ShouldMarkAsGlobal)
    return true;

  StringRef Name = CGM.getMangledName(GD);
  const auto *D = cast<FunctionDecl>(GD.getDecl());
  // Do not to emit function if it is marked as declare target as it was already
  // emitted.
  if (OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(D)) {
    if (D->hasBody() && AlreadyEmittedTargetFunctions.count(Name) == 0) {
      if (auto *F = dyn_cast_or_null<llvm::Function>(CGM.GetGlobalValue(Name)))
        return !F->isDeclaration();
      return false;
    }
    return true;
  }

  return !AlreadyEmittedTargetFunctions.insert(Name).second;
}

llvm::Function *CGOpenMPRuntime::emitRegistrationFunction() {
  // If we have offloading in the current module, we need to emit the entries
  // now and register the offloading descriptor.
  createOffloadEntriesAndInfoMetadata();

  // Create and register the offloading binary descriptors. This is the main
  // entity that captures all the information about offloading in the current
  // compilation unit.
  return createOffloadingBinaryDescriptorRegistration();
}

void CGOpenMPRuntime::emitTeamsCall(CodeGenFunction &CGF,
                                    const OMPExecutableDirective &D,
                                    SourceLocation Loc,
                                    llvm::Value *OutlinedFn,
                                    ArrayRef<llvm::Value *> CapturedVars) {
  if (!CGF.HaveInsertPoint())
    return;

  llvm::Value *RTLoc = emitUpdateLocation(CGF, Loc);
  CodeGenFunction::RunCleanupsScope Scope(CGF);

  // Build call __kmpc_fork_teams(loc, n, microtask, var1, .., varn);
  llvm::Value *Args[] = {
      RTLoc,
      CGF.Builder.getInt32(CapturedVars.size()), // Number of captured vars
      CGF.Builder.CreateBitCast(OutlinedFn, getKmpc_MicroPointerTy())};
  llvm::SmallVector<llvm::Value *, 16> RealArgs;
  RealArgs.append(std::begin(Args), std::end(Args));
  RealArgs.append(CapturedVars.begin(), CapturedVars.end());

  llvm::Value *RTLFn = createRuntimeFunction(OMPRTL__kmpc_fork_teams);
  CGF.EmitRuntimeCall(RTLFn, RealArgs);
}

void CGOpenMPRuntime::emitNumTeamsClause(CodeGenFunction &CGF,
                                         const Expr *NumTeams,
                                         const Expr *ThreadLimit,
                                         SourceLocation Loc) {
  if (!CGF.HaveInsertPoint())
    return;

  llvm::Value *RTLoc = emitUpdateLocation(CGF, Loc);

  llvm::Value *NumTeamsVal =
      NumTeams
          ? CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(NumTeams),
                                      CGF.CGM.Int32Ty, /* isSigned = */ true)
          : CGF.Builder.getInt32(0);

  llvm::Value *ThreadLimitVal =
      ThreadLimit
          ? CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(ThreadLimit),
                                      CGF.CGM.Int32Ty, /* isSigned = */ true)
          : CGF.Builder.getInt32(0);

  // Build call __kmpc_push_num_teamss(&loc, global_tid, num_teams, thread_limit)
  llvm::Value *PushNumTeamsArgs[] = {RTLoc, getThreadID(CGF, Loc), NumTeamsVal,
                                     ThreadLimitVal};
  CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__kmpc_push_num_teams),
                      PushNumTeamsArgs);
}

void CGOpenMPRuntime::emitTargetDataCalls(
    CodeGenFunction &CGF, const OMPExecutableDirective &D, const Expr *IfCond,
    const Expr *Device, const RegionCodeGenTy &CodeGen, TargetDataInfo &Info) {
  if (!CGF.HaveInsertPoint())
    return;

  // Action used to replace the default codegen action and turn privatization
  // off.
  PrePostActionTy NoPrivAction;

  // Generate the code for the opening of the data environment. Capture all the
  // arguments of the runtime call by reference because they are used in the
  // closing of the region.
  auto &&BeginThenGen = [this, &D, Device, &Info,
                         &CodeGen](CodeGenFunction &CGF, PrePostActionTy &) {
    // Fill up the arrays with all the mapped variables.
    MappableExprsHandler::MapBaseValuesArrayTy BasePointers;
    MappableExprsHandler::MapValuesArrayTy Pointers;
    MappableExprsHandler::MapValuesArrayTy Sizes;
    MappableExprsHandler::MapFlagsArrayTy MapTypes;

    // Get map clause information.
    MappableExprsHandler MCHandler(D, CGF);
    MCHandler.generateAllInfo(BasePointers, Pointers, Sizes, MapTypes);

    // Fill up the arrays and create the arguments.
    emitOffloadingArrays(CGF, BasePointers, Pointers, Sizes, MapTypes, Info);

    llvm::Value *BasePointersArrayArg = nullptr;
    llvm::Value *PointersArrayArg = nullptr;
    llvm::Value *SizesArrayArg = nullptr;
    llvm::Value *MapTypesArrayArg = nullptr;
    emitOffloadingArraysArgument(CGF, BasePointersArrayArg, PointersArrayArg,
                                 SizesArrayArg, MapTypesArrayArg, Info);

    // Emit device ID if any.
    llvm::Value *DeviceID = nullptr;
    if (Device) {
      DeviceID = CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(Device),
                                           CGF.Int64Ty, /*isSigned=*/true);
    } else {
      DeviceID = CGF.Builder.getInt64(OMP_DEVICEID_UNDEF);
    }

    // Emit the number of elements in the offloading arrays.
    llvm::Value *PointerNum = CGF.Builder.getInt32(Info.NumberOfPtrs);

    llvm::Value *OffloadingArgs[] = {
        DeviceID,         PointerNum,    BasePointersArrayArg,
        PointersArrayArg, SizesArrayArg, MapTypesArrayArg};
    CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__tgt_target_data_begin),
                        OffloadingArgs);

    // If device pointer privatization is required, emit the body of the region
    // here. It will have to be duplicated: with and without privatization.
    if (!Info.CaptureDeviceAddrMap.empty())
      CodeGen(CGF);
  };

  // Generate code for the closing of the data region.
  auto &&EndThenGen = [this, Device, &Info](CodeGenFunction &CGF,
                                            PrePostActionTy &) {
    assert(Info.isValid() && "Invalid data environment closing arguments.");

    llvm::Value *BasePointersArrayArg = nullptr;
    llvm::Value *PointersArrayArg = nullptr;
    llvm::Value *SizesArrayArg = nullptr;
    llvm::Value *MapTypesArrayArg = nullptr;
    emitOffloadingArraysArgument(CGF, BasePointersArrayArg, PointersArrayArg,
                                 SizesArrayArg, MapTypesArrayArg, Info);

    // Emit device ID if any.
    llvm::Value *DeviceID = nullptr;
    if (Device) {
      DeviceID = CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(Device),
                                           CGF.Int64Ty, /*isSigned=*/true);
    } else {
      DeviceID = CGF.Builder.getInt64(OMP_DEVICEID_UNDEF);
    }

    // Emit the number of elements in the offloading arrays.
    llvm::Value *PointerNum = CGF.Builder.getInt32(Info.NumberOfPtrs);

    llvm::Value *OffloadingArgs[] = {
        DeviceID,         PointerNum,    BasePointersArrayArg,
        PointersArrayArg, SizesArrayArg, MapTypesArrayArg};
    CGF.EmitRuntimeCall(createRuntimeFunction(OMPRTL__tgt_target_data_end),
                        OffloadingArgs);
  };

  // If we need device pointer privatization, we need to emit the body of the
  // region with no privatization in the 'else' branch of the conditional.
  // Otherwise, we don't have to do anything.
  auto &&BeginElseGen = [&Info, &CodeGen, &NoPrivAction](CodeGenFunction &CGF,
                                                         PrePostActionTy &) {
    if (!Info.CaptureDeviceAddrMap.empty()) {
      CodeGen.setAction(NoPrivAction);
      CodeGen(CGF);
    }
  };

  // We don't have to do anything to close the region if the if clause evaluates
  // to false.
  auto &&EndElseGen = [](CodeGenFunction &CGF, PrePostActionTy &) {};

  if (IfCond) {
    emitOMPIfClause(CGF, IfCond, BeginThenGen, BeginElseGen);
  } else {
    RegionCodeGenTy RCG(BeginThenGen);
    RCG(CGF);
  }

  // If we don't require privatization of device pointers, we emit the body in
  // between the runtime calls. This avoids duplicating the body code.
  if (Info.CaptureDeviceAddrMap.empty()) {
    CodeGen.setAction(NoPrivAction);
    CodeGen(CGF);
  }

  if (IfCond) {
    emitOMPIfClause(CGF, IfCond, EndThenGen, EndElseGen);
  } else {
    RegionCodeGenTy RCG(EndThenGen);
    RCG(CGF);
  }
}

void CGOpenMPRuntime::emitTargetDataStandAloneCall(
    CodeGenFunction &CGF, const OMPExecutableDirective &D, const Expr *IfCond,
    const Expr *Device) {
  if (!CGF.HaveInsertPoint())
    return;

  assert((isa<OMPTargetEnterDataDirective>(D) ||
          isa<OMPTargetExitDataDirective>(D) ||
          isa<OMPTargetUpdateDirective>(D)) &&
         "Expecting either target enter, exit data, or update directives.");

  CodeGenFunction::OMPTargetDataInfo InputInfo;
  llvm::Value *MapTypesArray = nullptr;
  // Generate the code for the opening of the data environment.
  auto &&ThenGen = [this, &D, Device, &InputInfo,
                    &MapTypesArray](CodeGenFunction &CGF, PrePostActionTy &) {
    // Emit device ID if any.
    llvm::Value *DeviceID = nullptr;
    if (Device) {
      DeviceID = CGF.Builder.CreateIntCast(CGF.EmitScalarExpr(Device),
                                           CGF.Int64Ty, /*isSigned=*/true);
    } else {
      DeviceID = CGF.Builder.getInt64(OMP_DEVICEID_UNDEF);
    }

    // Emit the number of elements in the offloading arrays.
    llvm::Constant *PointerNum =
        CGF.Builder.getInt32(InputInfo.NumberOfTargetItems);

    llvm::Value *OffloadingArgs[] = {DeviceID,
                                     PointerNum,
                                     InputInfo.BasePointersArray.getPointer(),
                                     InputInfo.PointersArray.getPointer(),
                                     InputInfo.SizesArray.getPointer(),
                                     MapTypesArray};

    // Select the right runtime function call for each expected standalone
    // directive.
    const bool HasNowait = D.hasClausesOfKind<OMPNowaitClause>();
    OpenMPRTLFunction RTLFn;
    switch (D.getDirectiveKind()) {
    case OMPD_target_enter_data:
      RTLFn = HasNowait ? OMPRTL__tgt_target_data_begin_nowait
                        : OMPRTL__tgt_target_data_begin;
      break;
    case OMPD_target_exit_data:
      RTLFn = HasNowait ? OMPRTL__tgt_target_data_end_nowait
                        : OMPRTL__tgt_target_data_end;
      break;
    case OMPD_target_update:
      RTLFn = HasNowait ? OMPRTL__tgt_target_data_update_nowait
                        : OMPRTL__tgt_target_data_update;
      break;
    case OMPD_parallel:
    case OMPD_for:
    case OMPD_parallel_for:
    case OMPD_parallel_sections:
    case OMPD_for_simd:
    case OMPD_parallel_for_simd:
    case OMPD_cancel:
    case OMPD_cancellation_point:
    case OMPD_ordered:
    case OMPD_threadprivate:
    case OMPD_task:
    case OMPD_simd:
    case OMPD_sections:
    case OMPD_section:
    case OMPD_single:
    case OMPD_master:
    case OMPD_critical:
    case OMPD_taskyield:
    case OMPD_barrier:
    case OMPD_taskwait:
    case OMPD_taskgroup:
    case OMPD_atomic:
    case OMPD_flush:
    case OMPD_teams:
    case OMPD_target_data:
    case OMPD_distribute:
    case OMPD_distribute_simd:
    case OMPD_distribute_parallel_for:
    case OMPD_distribute_parallel_for_simd:
    case OMPD_teams_distribute:
    case OMPD_teams_distribute_simd:
    case OMPD_teams_distribute_parallel_for:
    case OMPD_teams_distribute_parallel_for_simd:
    case OMPD_declare_simd:
    case OMPD_declare_target:
    case OMPD_end_declare_target:
    case OMPD_declare_reduction:
    case OMPD_taskloop:
    case OMPD_taskloop_simd:
    case OMPD_target:
    case OMPD_target_simd:
    case OMPD_target_teams_distribute:
    case OMPD_target_teams_distribute_simd:
    case OMPD_target_teams_distribute_parallel_for:
    case OMPD_target_teams_distribute_parallel_for_simd:
    case OMPD_target_teams:
    case OMPD_target_parallel:
    case OMPD_target_parallel_for:
    case OMPD_target_parallel_for_simd:
    case OMPD_requires:
    case OMPD_unknown:
      llvm_unreachable("Unexpected standalone target data directive.");
      break;
    }
    CGF.EmitRuntimeCall(createRuntimeFunction(RTLFn), OffloadingArgs);
  };

  auto &&TargetThenGen = [this, &ThenGen, &D, &InputInfo, &MapTypesArray](
                             CodeGenFunction &CGF, PrePostActionTy &) {
    // Fill up the arrays with all the mapped variables.
    MappableExprsHandler::MapBaseValuesArrayTy BasePointers;
    MappableExprsHandler::MapValuesArrayTy Pointers;
    MappableExprsHandler::MapValuesArrayTy Sizes;
    MappableExprsHandler::MapFlagsArrayTy MapTypes;

    // Get map clause information.
    MappableExprsHandler MEHandler(D, CGF);
    MEHandler.generateAllInfo(BasePointers, Pointers, Sizes, MapTypes);

    TargetDataInfo Info;
    // Fill up the arrays and create the arguments.
    emitOffloadingArrays(CGF, BasePointers, Pointers, Sizes, MapTypes, Info);
    emitOffloadingArraysArgument(CGF, Info.BasePointersArray,
                                 Info.PointersArray, Info.SizesArray,
                                 Info.MapTypesArray, Info);
    InputInfo.NumberOfTargetItems = Info.NumberOfPtrs;
    InputInfo.BasePointersArray =
        Address(Info.BasePointersArray, CGM.getPointerAlign());
    InputInfo.PointersArray =
        Address(Info.PointersArray, CGM.getPointerAlign());
    InputInfo.SizesArray =
        Address(Info.SizesArray, CGM.getPointerAlign());
    MapTypesArray = Info.MapTypesArray;
    if (D.hasClausesOfKind<OMPDependClause>())
      CGF.EmitOMPTargetTaskBasedDirective(D, ThenGen, InputInfo);
    else
      emitInlinedDirective(CGF, D.getDirectiveKind(), ThenGen);
  };

  if (IfCond) {
    emitOMPIfClause(CGF, IfCond, TargetThenGen,
                    [](CodeGenFunction &CGF, PrePostActionTy &) {});
  } else {
    RegionCodeGenTy ThenRCG(TargetThenGen);
    ThenRCG(CGF);
  }
}

namespace {
  /// Kind of parameter in a function with 'declare simd' directive.
  enum ParamKindTy { LinearWithVarStride, Linear, Uniform, Vector };
  /// Attribute set of the parameter.
  struct ParamAttrTy {
    ParamKindTy Kind = Vector;
    llvm::APSInt StrideOrArg;
    llvm::APSInt Alignment;
  };
} // namespace

static unsigned evaluateCDTSize(const FunctionDecl *FD,
                                ArrayRef<ParamAttrTy> ParamAttrs) {
  // Every vector variant of a SIMD-enabled function has a vector length (VLEN).
  // If OpenMP clause "simdlen" is used, the VLEN is the value of the argument
  // of that clause. The VLEN value must be power of 2.
  // In other case the notion of the function`s "characteristic data type" (CDT)
  // is used to compute the vector length.
  // CDT is defined in the following order:
  //   a) For non-void function, the CDT is the return type.
  //   b) If the function has any non-uniform, non-linear parameters, then the
  //   CDT is the type of the first such parameter.
  //   c) If the CDT determined by a) or b) above is struct, union, or class
  //   type which is pass-by-value (except for the type that maps to the
  //   built-in complex data type), the characteristic data type is int.
  //   d) If none of the above three cases is applicable, the CDT is int.
  // The VLEN is then determined based on the CDT and the size of vector
  // register of that ISA for which current vector version is generated. The
  // VLEN is computed using the formula below:
  //   VLEN  = sizeof(vector_register) / sizeof(CDT),
  // where vector register size specified in section 3.2.1 Registers and the
  // Stack Frame of original AMD64 ABI document.
  QualType RetType = FD->getReturnType();
  if (RetType.isNull())
    return 0;
  ASTContext &C = FD->getASTContext();
  QualType CDT;
  if (!RetType.isNull() && !RetType->isVoidType()) {
    CDT = RetType;
  } else {
    unsigned Offset = 0;
    if (const auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
      if (ParamAttrs[Offset].Kind == Vector)
        CDT = C.getPointerType(C.getRecordType(MD->getParent()));
      ++Offset;
    }
    if (CDT.isNull()) {
      for (unsigned I = 0, E = FD->getNumParams(); I < E; ++I) {
        if (ParamAttrs[I + Offset].Kind == Vector) {
          CDT = FD->getParamDecl(I)->getType();
          break;
        }
      }
    }
  }
  if (CDT.isNull())
    CDT = C.IntTy;
  CDT = CDT->getCanonicalTypeUnqualified();
  if (CDT->isRecordType() || CDT->isUnionType())
    CDT = C.IntTy;
  return C.getTypeSize(CDT);
}

static void
emitX86DeclareSimdFunction(const FunctionDecl *FD, llvm::Function *Fn,
                           const llvm::APSInt &VLENVal,
                           ArrayRef<ParamAttrTy> ParamAttrs,
                           OMPDeclareSimdDeclAttr::BranchStateTy State) {
  struct ISADataTy {
    char ISA;
    unsigned VecRegSize;
  };
  ISADataTy ISAData[] = {
      {
          'b', 128
      }, // SSE
      {
          'c', 256
      }, // AVX
      {
          'd', 256
      }, // AVX2
      {
          'e', 512
      }, // AVX512
  };
  llvm::SmallVector<char, 2> Masked;
  switch (State) {
  case OMPDeclareSimdDeclAttr::BS_Undefined:
    Masked.push_back('N');
    Masked.push_back('M');
    break;
  case OMPDeclareSimdDeclAttr::BS_Notinbranch:
    Masked.push_back('N');
    break;
  case OMPDeclareSimdDeclAttr::BS_Inbranch:
    Masked.push_back('M');
    break;
  }
  for (char Mask : Masked) {
    for (const ISADataTy &Data : ISAData) {
      SmallString<256> Buffer;
      llvm::raw_svector_ostream Out(Buffer);
      Out << "_ZGV" << Data.ISA << Mask;
      if (!VLENVal) {
        Out << llvm::APSInt::getUnsigned(Data.VecRegSize /
                                         evaluateCDTSize(FD, ParamAttrs));
      } else {
        Out << VLENVal;
      }
      for (const ParamAttrTy &ParamAttr : ParamAttrs) {
        switch (ParamAttr.Kind){
        case LinearWithVarStride:
          Out << 's' << ParamAttr.StrideOrArg;
          break;
        case Linear:
          Out << 'l';
          if (!!ParamAttr.StrideOrArg)
            Out << ParamAttr.StrideOrArg;
          break;
        case Uniform:
          Out << 'u';
          break;
        case Vector:
          Out << 'v';
          break;
        }
        if (!!ParamAttr.Alignment)
          Out << 'a' << ParamAttr.Alignment;
      }
      Out << '_' << Fn->getName();
      Fn->addFnAttr(Out.str());
    }
  }
}

void CGOpenMPRuntime::emitDeclareSimdFunction(const FunctionDecl *FD,
                                              llvm::Function *Fn) {
  ASTContext &C = CGM.getContext();
  FD = FD->getMostRecentDecl();
  // Map params to their positions in function decl.
  llvm::DenseMap<const Decl *, unsigned> ParamPositions;
  if (isa<CXXMethodDecl>(FD))
    ParamPositions.try_emplace(FD, 0);
  unsigned ParamPos = ParamPositions.size();
  for (const ParmVarDecl *P : FD->parameters()) {
    ParamPositions.try_emplace(P->getCanonicalDecl(), ParamPos);
    ++ParamPos;
  }
  while (FD) {
    for (const auto *Attr : FD->specific_attrs<OMPDeclareSimdDeclAttr>()) {
      llvm::SmallVector<ParamAttrTy, 8> ParamAttrs(ParamPositions.size());
      // Mark uniform parameters.
      for (const Expr *E : Attr->uniforms()) {
        E = E->IgnoreParenImpCasts();
        unsigned Pos;
        if (isa<CXXThisExpr>(E)) {
          Pos = ParamPositions[FD];
        } else {
          const auto *PVD = cast<ParmVarDecl>(cast<DeclRefExpr>(E)->getDecl())
                                ->getCanonicalDecl();
          Pos = ParamPositions[PVD];
        }
        ParamAttrs[Pos].Kind = Uniform;
      }
      // Get alignment info.
      auto NI = Attr->alignments_begin();
      for (const Expr *E : Attr->aligneds()) {
        E = E->IgnoreParenImpCasts();
        unsigned Pos;
        QualType ParmTy;
        if (isa<CXXThisExpr>(E)) {
          Pos = ParamPositions[FD];
          ParmTy = E->getType();
        } else {
          const auto *PVD = cast<ParmVarDecl>(cast<DeclRefExpr>(E)->getDecl())
                                ->getCanonicalDecl();
          Pos = ParamPositions[PVD];
          ParmTy = PVD->getType();
        }
        ParamAttrs[Pos].Alignment =
            (*NI)
                ? (*NI)->EvaluateKnownConstInt(C)
                : llvm::APSInt::getUnsigned(
                      C.toCharUnitsFromBits(C.getOpenMPDefaultSimdAlign(ParmTy))
                          .getQuantity());
        ++NI;
      }
      // Mark linear parameters.
      auto SI = Attr->steps_begin();
      auto MI = Attr->modifiers_begin();
      for (const Expr *E : Attr->linears()) {
        E = E->IgnoreParenImpCasts();
        unsigned Pos;
        if (isa<CXXThisExpr>(E)) {
          Pos = ParamPositions[FD];
        } else {
          const auto *PVD = cast<ParmVarDecl>(cast<DeclRefExpr>(E)->getDecl())
                                ->getCanonicalDecl();
          Pos = ParamPositions[PVD];
        }
        ParamAttrTy &ParamAttr = ParamAttrs[Pos];
        ParamAttr.Kind = Linear;
        if (*SI) {
          Expr::EvalResult Result;
          if (!(*SI)->EvaluateAsInt(Result, C, Expr::SE_AllowSideEffects)) {
            if (const auto *DRE =
                    cast<DeclRefExpr>((*SI)->IgnoreParenImpCasts())) {
              if (const auto *StridePVD = cast<ParmVarDecl>(DRE->getDecl())) {
                ParamAttr.Kind = LinearWithVarStride;
                ParamAttr.StrideOrArg = llvm::APSInt::getUnsigned(
                    ParamPositions[StridePVD->getCanonicalDecl()]);
              }
            }
          } else {
            ParamAttr.StrideOrArg = Result.Val.getInt();
          }
        }
        ++SI;
        ++MI;
      }
      llvm::APSInt VLENVal;
      if (const Expr *VLEN = Attr->getSimdlen())
        VLENVal = VLEN->EvaluateKnownConstInt(C);
      OMPDeclareSimdDeclAttr::BranchStateTy State = Attr->getBranchState();
      if (CGM.getTriple().getArch() == llvm::Triple::x86 ||
          CGM.getTriple().getArch() == llvm::Triple::x86_64)
        emitX86DeclareSimdFunction(FD, Fn, VLENVal, ParamAttrs, State);
    }
    FD = FD->getPreviousDecl();
  }
}

namespace {
/// Cleanup action for doacross support.
class DoacrossCleanupTy final : public EHScopeStack::Cleanup {
public:
  static const int DoacrossFinArgs = 2;

private:
  llvm::Value *RTLFn;
  llvm::Value *Args[DoacrossFinArgs];

public:
  DoacrossCleanupTy(llvm::Value *RTLFn, ArrayRef<llvm::Value *> CallArgs)
      : RTLFn(RTLFn) {
    assert(CallArgs.size() == DoacrossFinArgs);
    std::copy(CallArgs.begin(), CallArgs.end(), std::begin(Args));
  }
  void Emit(CodeGenFunction &CGF, Flags /*flags*/) override {
    if (!CGF.HaveInsertPoint())
      return;
    CGF.EmitRuntimeCall(RTLFn, Args);
  }
};
} // namespace

void CGOpenMPRuntime::emitDoacrossInit(CodeGenFunction &CGF,
                                       const OMPLoopDirective &D,
                                       ArrayRef<Expr *> NumIterations) {
  if (!CGF.HaveInsertPoint())
    return;

  ASTContext &C = CGM.getContext();
  QualType Int64Ty = C.getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/true);
  RecordDecl *RD;
  if (KmpDimTy.isNull()) {
    // Build struct kmp_dim {  // loop bounds info casted to kmp_int64
    //  kmp_int64 lo; // lower
    //  kmp_int64 up; // upper
    //  kmp_int64 st; // stride
    // };
    RD = C.buildImplicitRecord("kmp_dim");
    RD->startDefinition();
    addFieldToRecordDecl(C, RD, Int64Ty);
    addFieldToRecordDecl(C, RD, Int64Ty);
    addFieldToRecordDecl(C, RD, Int64Ty);
    RD->completeDefinition();
    KmpDimTy = C.getRecordType(RD);
  } else {
    RD = cast<RecordDecl>(KmpDimTy->getAsTagDecl());
  }
  llvm::APInt Size(/*numBits=*/32, NumIterations.size());
  QualType ArrayTy =
      C.getConstantArrayType(KmpDimTy, Size, ArrayType::Normal, 0);

  Address DimsAddr = CGF.CreateMemTemp(ArrayTy, "dims");
  CGF.EmitNullInitialization(DimsAddr, ArrayTy);
  enum { LowerFD = 0, UpperFD, StrideFD };
  // Fill dims with data.
  for (unsigned I = 0, E = NumIterations.size(); I < E; ++I) {
    LValue DimsLVal =
        CGF.MakeAddrLValue(CGF.Builder.CreateConstArrayGEP(
                               DimsAddr, I, C.getTypeSizeInChars(KmpDimTy)),
                           KmpDimTy);
    // dims.upper = num_iterations;
    LValue UpperLVal = CGF.EmitLValueForField(
        DimsLVal, *std::next(RD->field_begin(), UpperFD));
    llvm::Value *NumIterVal =
        CGF.EmitScalarConversion(CGF.EmitScalarExpr(NumIterations[I]),
                                 D.getNumIterations()->getType(), Int64Ty,
                                 D.getNumIterations()->getExprLoc());
    CGF.EmitStoreOfScalar(NumIterVal, UpperLVal);
    // dims.stride = 1;
    LValue StrideLVal = CGF.EmitLValueForField(
        DimsLVal, *std::next(RD->field_begin(), StrideFD));
    CGF.EmitStoreOfScalar(llvm::ConstantInt::getSigned(CGM.Int64Ty, /*V=*/1),
                          StrideLVal);
  }

  // Build call void __kmpc_doacross_init(ident_t *loc, kmp_int32 gtid,
  // kmp_int32 num_dims, struct kmp_dim * dims);
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, D.getBeginLoc()),
      getThreadID(CGF, D.getBeginLoc()),
      llvm::ConstantInt::getSigned(CGM.Int32Ty, NumIterations.size()),
      CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
          CGF.Builder
              .CreateConstArrayGEP(DimsAddr, 0, C.getTypeSizeInChars(KmpDimTy))
              .getPointer(),
          CGM.VoidPtrTy)};

  llvm::Value *RTLFn = createRuntimeFunction(OMPRTL__kmpc_doacross_init);
  CGF.EmitRuntimeCall(RTLFn, Args);
  llvm::Value *FiniArgs[DoacrossCleanupTy::DoacrossFinArgs] = {
      emitUpdateLocation(CGF, D.getEndLoc()), getThreadID(CGF, D.getEndLoc())};
  llvm::Value *FiniRTLFn = createRuntimeFunction(OMPRTL__kmpc_doacross_fini);
  CGF.EHStack.pushCleanup<DoacrossCleanupTy>(NormalAndEHCleanup, FiniRTLFn,
                                             llvm::makeArrayRef(FiniArgs));
}

void CGOpenMPRuntime::emitDoacrossOrdered(CodeGenFunction &CGF,
                                          const OMPDependClause *C) {
  QualType Int64Ty =
      CGM.getContext().getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/1);
  llvm::APInt Size(/*numBits=*/32, C->getNumLoops());
  QualType ArrayTy = CGM.getContext().getConstantArrayType(
      Int64Ty, Size, ArrayType::Normal, 0);
  Address CntAddr = CGF.CreateMemTemp(ArrayTy, ".cnt.addr");
  for (unsigned I = 0, E = C->getNumLoops(); I < E; ++I) {
    const Expr *CounterVal = C->getLoopData(I);
    assert(CounterVal);
    llvm::Value *CntVal = CGF.EmitScalarConversion(
        CGF.EmitScalarExpr(CounterVal), CounterVal->getType(), Int64Ty,
        CounterVal->getExprLoc());
    CGF.EmitStoreOfScalar(
        CntVal,
        CGF.Builder.CreateConstArrayGEP(
            CntAddr, I, CGM.getContext().getTypeSizeInChars(Int64Ty)),
        /*Volatile=*/false, Int64Ty);
  }
  llvm::Value *Args[] = {
      emitUpdateLocation(CGF, C->getBeginLoc()),
      getThreadID(CGF, C->getBeginLoc()),
      CGF.Builder
          .CreateConstArrayGEP(CntAddr, 0,
                               CGM.getContext().getTypeSizeInChars(Int64Ty))
          .getPointer()};
  llvm::Value *RTLFn;
  if (C->getDependencyKind() == OMPC_DEPEND_source) {
    RTLFn = createRuntimeFunction(OMPRTL__kmpc_doacross_post);
  } else {
    assert(C->getDependencyKind() == OMPC_DEPEND_sink);
    RTLFn = createRuntimeFunction(OMPRTL__kmpc_doacross_wait);
  }
  CGF.EmitRuntimeCall(RTLFn, Args);
}

void CGOpenMPRuntime::emitCall(CodeGenFunction &CGF, SourceLocation Loc,
                               llvm::Value *Callee,
                               ArrayRef<llvm::Value *> Args) const {
  assert(Loc.isValid() && "Outlined function call location must be valid.");
  auto DL = ApplyDebugLocation::CreateDefaultArtificial(CGF, Loc);

  if (auto *Fn = dyn_cast<llvm::Function>(Callee)) {
    if (Fn->doesNotThrow()) {
      CGF.EmitNounwindRuntimeCall(Fn, Args);
      return;
    }
  }
  CGF.EmitRuntimeCall(Callee, Args);
}

void CGOpenMPRuntime::emitOutlinedFunctionCall(
    CodeGenFunction &CGF, SourceLocation Loc, llvm::Value *OutlinedFn,
    ArrayRef<llvm::Value *> Args) const {
  emitCall(CGF, Loc, OutlinedFn, Args);
}

Address CGOpenMPRuntime::getParameterAddress(CodeGenFunction &CGF,
                                             const VarDecl *NativeParam,
                                             const VarDecl *TargetParam) const {
  return CGF.GetAddrOfLocalVar(NativeParam);
}

Address CGOpenMPRuntime::getAddressOfLocalVariable(CodeGenFunction &CGF,
                                                   const VarDecl *VD) {
  return Address::invalid();
}

llvm::Value *CGOpenMPSIMDRuntime::emitParallelOutlinedFunction(
    const OMPExecutableDirective &D, const VarDecl *ThreadIDVar,
    OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

llvm::Value *CGOpenMPSIMDRuntime::emitTeamsOutlinedFunction(
    const OMPExecutableDirective &D, const VarDecl *ThreadIDVar,
    OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

llvm::Value *CGOpenMPSIMDRuntime::emitTaskOutlinedFunction(
    const OMPExecutableDirective &D, const VarDecl *ThreadIDVar,
    const VarDecl *PartIDVar, const VarDecl *TaskTVar,
    OpenMPDirectiveKind InnermostKind, const RegionCodeGenTy &CodeGen,
    bool Tied, unsigned &NumberOfParts) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitParallelCall(CodeGenFunction &CGF,
                                           SourceLocation Loc,
                                           llvm::Value *OutlinedFn,
                                           ArrayRef<llvm::Value *> CapturedVars,
                                           const Expr *IfCond) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitCriticalRegion(
    CodeGenFunction &CGF, StringRef CriticalName,
    const RegionCodeGenTy &CriticalOpGen, SourceLocation Loc,
    const Expr *Hint) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitMasterRegion(CodeGenFunction &CGF,
                                           const RegionCodeGenTy &MasterOpGen,
                                           SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskyieldCall(CodeGenFunction &CGF,
                                            SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskgroupRegion(
    CodeGenFunction &CGF, const RegionCodeGenTy &TaskgroupOpGen,
    SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitSingleRegion(
    CodeGenFunction &CGF, const RegionCodeGenTy &SingleOpGen,
    SourceLocation Loc, ArrayRef<const Expr *> CopyprivateVars,
    ArrayRef<const Expr *> DestExprs, ArrayRef<const Expr *> SrcExprs,
    ArrayRef<const Expr *> AssignmentOps) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitOrderedRegion(CodeGenFunction &CGF,
                                            const RegionCodeGenTy &OrderedOpGen,
                                            SourceLocation Loc,
                                            bool IsThreads) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitBarrierCall(CodeGenFunction &CGF,
                                          SourceLocation Loc,
                                          OpenMPDirectiveKind Kind,
                                          bool EmitChecks,
                                          bool ForceSimpleCall) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitForDispatchInit(
    CodeGenFunction &CGF, SourceLocation Loc,
    const OpenMPScheduleTy &ScheduleKind, unsigned IVSize, bool IVSigned,
    bool Ordered, const DispatchRTInput &DispatchValues) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitForStaticInit(
    CodeGenFunction &CGF, SourceLocation Loc, OpenMPDirectiveKind DKind,
    const OpenMPScheduleTy &ScheduleKind, const StaticRTInput &Values) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitDistributeStaticInit(
    CodeGenFunction &CGF, SourceLocation Loc,
    OpenMPDistScheduleClauseKind SchedKind, const StaticRTInput &Values) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitForOrderedIterationEnd(CodeGenFunction &CGF,
                                                     SourceLocation Loc,
                                                     unsigned IVSize,
                                                     bool IVSigned) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitForStaticFinish(CodeGenFunction &CGF,
                                              SourceLocation Loc,
                                              OpenMPDirectiveKind DKind) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

llvm::Value *CGOpenMPSIMDRuntime::emitForNext(CodeGenFunction &CGF,
                                              SourceLocation Loc,
                                              unsigned IVSize, bool IVSigned,
                                              Address IL, Address LB,
                                              Address UB, Address ST) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitNumThreadsClause(CodeGenFunction &CGF,
                                               llvm::Value *NumThreads,
                                               SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitProcBindClause(CodeGenFunction &CGF,
                                             OpenMPProcBindClauseKind ProcBind,
                                             SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

Address CGOpenMPSIMDRuntime::getAddrOfThreadPrivate(CodeGenFunction &CGF,
                                                    const VarDecl *VD,
                                                    Address VDAddr,
                                                    SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

llvm::Function *CGOpenMPSIMDRuntime::emitThreadPrivateVarDefinition(
    const VarDecl *VD, Address VDAddr, SourceLocation Loc, bool PerformInit,
    CodeGenFunction *CGF) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

Address CGOpenMPSIMDRuntime::getAddrOfArtificialThreadPrivate(
    CodeGenFunction &CGF, QualType VarType, StringRef Name) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitFlush(CodeGenFunction &CGF,
                                    ArrayRef<const Expr *> Vars,
                                    SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskCall(CodeGenFunction &CGF, SourceLocation Loc,
                                       const OMPExecutableDirective &D,
                                       llvm::Value *TaskFunction,
                                       QualType SharedsTy, Address Shareds,
                                       const Expr *IfCond,
                                       const OMPTaskDataTy &Data) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskLoopCall(
    CodeGenFunction &CGF, SourceLocation Loc, const OMPLoopDirective &D,
    llvm::Value *TaskFunction, QualType SharedsTy, Address Shareds,
    const Expr *IfCond, const OMPTaskDataTy &Data) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitReduction(
    CodeGenFunction &CGF, SourceLocation Loc, ArrayRef<const Expr *> Privates,
    ArrayRef<const Expr *> LHSExprs, ArrayRef<const Expr *> RHSExprs,
    ArrayRef<const Expr *> ReductionOps, ReductionOptionsTy Options) {
  assert(Options.SimpleReduction && "Only simple reduction is expected.");
  CGOpenMPRuntime::emitReduction(CGF, Loc, Privates, LHSExprs, RHSExprs,
                                 ReductionOps, Options);
}

llvm::Value *CGOpenMPSIMDRuntime::emitTaskReductionInit(
    CodeGenFunction &CGF, SourceLocation Loc, ArrayRef<const Expr *> LHSExprs,
    ArrayRef<const Expr *> RHSExprs, const OMPTaskDataTy &Data) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskReductionFixups(CodeGenFunction &CGF,
                                                  SourceLocation Loc,
                                                  ReductionCodeGen &RCG,
                                                  unsigned N) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

Address CGOpenMPSIMDRuntime::getTaskReductionItem(CodeGenFunction &CGF,
                                                  SourceLocation Loc,
                                                  llvm::Value *ReductionsPtr,
                                                  LValue SharedLVal) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTaskwaitCall(CodeGenFunction &CGF,
                                           SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitCancellationPointCall(
    CodeGenFunction &CGF, SourceLocation Loc,
    OpenMPDirectiveKind CancelRegion) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitCancelCall(CodeGenFunction &CGF,
                                         SourceLocation Loc, const Expr *IfCond,
                                         OpenMPDirectiveKind CancelRegion) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTargetOutlinedFunction(
    const OMPExecutableDirective &D, StringRef ParentName,
    llvm::Function *&OutlinedFn, llvm::Constant *&OutlinedFnID,
    bool IsOffloadEntry, const RegionCodeGenTy &CodeGen) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTargetCall(CodeGenFunction &CGF,
                                         const OMPExecutableDirective &D,
                                         llvm::Value *OutlinedFn,
                                         llvm::Value *OutlinedFnID,
                                         const Expr *IfCond, const Expr *Device) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

bool CGOpenMPSIMDRuntime::emitTargetFunctions(GlobalDecl GD) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

bool CGOpenMPSIMDRuntime::emitTargetGlobalVariable(GlobalDecl GD) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

bool CGOpenMPSIMDRuntime::emitTargetGlobal(GlobalDecl GD) {
  return false;
}

llvm::Function *CGOpenMPSIMDRuntime::emitRegistrationFunction() {
  return nullptr;
}

void CGOpenMPSIMDRuntime::emitTeamsCall(CodeGenFunction &CGF,
                                        const OMPExecutableDirective &D,
                                        SourceLocation Loc,
                                        llvm::Value *OutlinedFn,
                                        ArrayRef<llvm::Value *> CapturedVars) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitNumTeamsClause(CodeGenFunction &CGF,
                                             const Expr *NumTeams,
                                             const Expr *ThreadLimit,
                                             SourceLocation Loc) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTargetDataCalls(
    CodeGenFunction &CGF, const OMPExecutableDirective &D, const Expr *IfCond,
    const Expr *Device, const RegionCodeGenTy &CodeGen, TargetDataInfo &Info) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitTargetDataStandAloneCall(
    CodeGenFunction &CGF, const OMPExecutableDirective &D, const Expr *IfCond,
    const Expr *Device) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitDoacrossInit(CodeGenFunction &CGF,
                                           const OMPLoopDirective &D,
                                           ArrayRef<Expr *> NumIterations) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

void CGOpenMPSIMDRuntime::emitDoacrossOrdered(CodeGenFunction &CGF,
                                              const OMPDependClause *C) {
  llvm_unreachable("Not supported in SIMD-only mode");
}

const VarDecl *
CGOpenMPSIMDRuntime::translateParameter(const FieldDecl *FD,
                                        const VarDecl *NativeParam) const {
  llvm_unreachable("Not supported in SIMD-only mode");
}

Address
CGOpenMPSIMDRuntime::getParameterAddress(CodeGenFunction &CGF,
                                         const VarDecl *NativeParam,
                                         const VarDecl *TargetParam) const {
  llvm_unreachable("Not supported in SIMD-only mode");
}

