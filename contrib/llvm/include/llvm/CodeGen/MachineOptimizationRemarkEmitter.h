///===- MachineOptimizationRemarkEmitter.h - Opt Diagnostics -*- C++ -*----===//
///
///                     The LLVM Compiler Infrastructure
///
/// This file is distributed under the University of Illinois Open Source
/// License. See LICENSE.TXT for details.
///
///===---------------------------------------------------------------------===//
/// \file
/// Optimization diagnostic interfaces for machine passes.  It's packaged as an
/// analysis pass so that by using this service passes become dependent on MBFI
/// as well.  MBFI is used to compute the "hotness" of the diagnostic message.
///
///===---------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEOPTIMIZATIONREMARKEMITTER_H
#define LLVM_CODEGEN_MACHINEOPTIMIZATIONREMARKEMITTER_H

#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {
class MachineBasicBlock;
class MachineBlockFrequencyInfo;
class MachineInstr;

/// Common features for diagnostics dealing with optimization remarks
/// that are used by machine passes.
class DiagnosticInfoMIROptimization : public DiagnosticInfoOptimizationBase {
public:
  DiagnosticInfoMIROptimization(enum DiagnosticKind Kind, const char *PassName,
                                StringRef RemarkName,
                                const DiagnosticLocation &Loc,
                                const MachineBasicBlock *MBB)
      : DiagnosticInfoOptimizationBase(Kind, DS_Remark, PassName, RemarkName,
                                       MBB->getParent()->getFunction(), Loc),
        MBB(MBB) {}

  /// MI-specific kinds of diagnostic Arguments.
  struct MachineArgument : public DiagnosticInfoOptimizationBase::Argument {
    /// Print an entire MachineInstr.
    MachineArgument(StringRef Key, const MachineInstr &MI);
  };

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() >= DK_FirstMachineRemark &&
           DI->getKind() <= DK_LastMachineRemark;
  }

  const MachineBasicBlock *getBlock() const { return MBB; }

private:
  const MachineBasicBlock *MBB;
};

/// Diagnostic information for applied optimization remarks.
class MachineOptimizationRemark : public DiagnosticInfoMIROptimization {
public:
  /// \p PassName is the name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass=, then the diagnostic will
  /// be emitted.  \p RemarkName is a textual identifier for the remark.  \p
  /// Loc is the debug location and \p MBB is the block that the optimization
  /// operates in.
  MachineOptimizationRemark(const char *PassName, StringRef RemarkName,
                            const DiagnosticLocation &Loc,
                            const MachineBasicBlock *MBB)
      : DiagnosticInfoMIROptimization(DK_MachineOptimizationRemark, PassName,
                                      RemarkName, Loc, MBB) {}

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_MachineOptimizationRemark;
  }

  /// \see DiagnosticInfoOptimizationBase::isEnabled.
  bool isEnabled() const override {
    const Function &Fn = getFunction();
    LLVMContext &Ctx = Fn.getContext();
    return Ctx.getDiagHandlerPtr()->isPassedOptRemarkEnabled(getPassName());
  }
};

/// Diagnostic information for missed-optimization remarks.
class MachineOptimizationRemarkMissed : public DiagnosticInfoMIROptimization {
public:
  /// \p PassName is the name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass-missed=, then the
  /// diagnostic will be emitted.  \p RemarkName is a textual identifier for the
  /// remark.  \p Loc is the debug location and \p MBB is the block that the
  /// optimization operates in.
  MachineOptimizationRemarkMissed(const char *PassName, StringRef RemarkName,
                                  const DiagnosticLocation &Loc,
                                  const MachineBasicBlock *MBB)
      : DiagnosticInfoMIROptimization(DK_MachineOptimizationRemarkMissed,
                                      PassName, RemarkName, Loc, MBB) {}

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_MachineOptimizationRemarkMissed;
  }

  /// \see DiagnosticInfoOptimizationBase::isEnabled.
  bool isEnabled() const override {
    const Function &Fn = getFunction();
    LLVMContext &Ctx = Fn.getContext();
    return Ctx.getDiagHandlerPtr()->isMissedOptRemarkEnabled(getPassName());
  }
};

/// Diagnostic information for optimization analysis remarks.
class MachineOptimizationRemarkAnalysis : public DiagnosticInfoMIROptimization {
public:
  /// \p PassName is the name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass-analysis=, then the
  /// diagnostic will be emitted.  \p RemarkName is a textual identifier for the
  /// remark.  \p Loc is the debug location and \p MBB is the block that the
  /// optimization operates in.
  MachineOptimizationRemarkAnalysis(const char *PassName, StringRef RemarkName,
                                    const DiagnosticLocation &Loc,
                                    const MachineBasicBlock *MBB)
      : DiagnosticInfoMIROptimization(DK_MachineOptimizationRemarkAnalysis,
                                      PassName, RemarkName, Loc, MBB) {}

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_MachineOptimizationRemarkAnalysis;
  }

  /// \see DiagnosticInfoOptimizationBase::isEnabled.
  bool isEnabled() const override {
    const Function &Fn = getFunction();
    LLVMContext &Ctx = Fn.getContext();
    return Ctx.getDiagHandlerPtr()->isAnalysisRemarkEnabled(getPassName());
  }
};

/// Extend llvm::ore:: with MI-specific helper names.
namespace ore {
using MNV = DiagnosticInfoMIROptimization::MachineArgument;
}

/// The optimization diagnostic interface.
///
/// It allows reporting when optimizations are performed and when they are not
/// along with the reasons for it.  Hotness information of the corresponding
/// code region can be included in the remark if DiagnosticsHotnessRequested is
/// enabled in the LLVM context.
class MachineOptimizationRemarkEmitter {
public:
  MachineOptimizationRemarkEmitter(MachineFunction &MF,
                                   MachineBlockFrequencyInfo *MBFI)
      : MF(MF), MBFI(MBFI) {}

  /// Emit an optimization remark.
  void emit(DiagnosticInfoOptimizationBase &OptDiag);

  /// Whether we allow for extra compile-time budget to perform more
  /// analysis to be more informative.
  ///
  /// This is useful to enable additional missed optimizations to be reported
  /// that are normally too noisy.  In this mode, we can use the extra analysis
  /// (1) to filter trivial false positives or (2) to provide more context so
  /// that non-trivial false positives can be quickly detected by the user.
  bool allowExtraAnalysis(StringRef PassName) const {
    return (MF.getFunction().getContext().getDiagnosticsOutputFile() ||
            MF.getFunction().getContext()
            .getDiagHandlerPtr()->isAnyRemarkEnabled(PassName));
  }

  /// Take a lambda that returns a remark which will be emitted.  Second
  /// argument is only used to restrict this to functions.
  template <typename T>
  void emit(T RemarkBuilder, decltype(RemarkBuilder()) * = nullptr) {
    // Avoid building the remark unless we know there are at least *some*
    // remarks enabled. We can't currently check whether remarks are requested
    // for the calling pass since that requires actually building the remark.

    if (MF.getFunction().getContext().getDiagnosticsOutputFile() ||
        MF.getFunction().getContext().getDiagHandlerPtr()->isAnyRemarkEnabled()) {
      auto R = RemarkBuilder();
      emit((DiagnosticInfoOptimizationBase &)R);
    }
  }

private:
  MachineFunction &MF;

  /// MBFI is only set if hotness is requested.
  MachineBlockFrequencyInfo *MBFI;

  /// Compute hotness from IR value (currently assumed to be a block) if PGO is
  /// available.
  Optional<uint64_t> computeHotness(const MachineBasicBlock &MBB);

  /// Similar but use value from \p OptDiag and update hotness there.
  void computeHotness(DiagnosticInfoMIROptimization &Remark);

  /// Only allow verbose messages if we know we're filtering by hotness
  /// (BFI is only set in this case).
  bool shouldEmitVerbose() { return MBFI != nullptr; }
};

/// The analysis pass
///
/// Note that this pass shouldn't generally be marked as preserved by other
/// passes.  It's holding onto BFI, so if the pass does not preserve BFI, BFI
/// could be freed.
class MachineOptimizationRemarkEmitterPass : public MachineFunctionPass {
  std::unique_ptr<MachineOptimizationRemarkEmitter> ORE;

public:
  MachineOptimizationRemarkEmitterPass();

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineOptimizationRemarkEmitter &getORE() {
    assert(ORE && "pass not run yet");
    return *ORE;
  }

  static char ID;
};
}

#endif
