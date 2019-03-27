//===-- ARMSubtarget.h - Define Subtarget for the ARM ----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ARM specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMSUBTARGET_H
#define LLVM_LIB_TARGET_ARM_ARMSUBTARGET_H

#include "ARMBaseInstrInfo.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMConstantPoolValue.h"
#include "ARMFrameLowering.h"
#include "ARMISelLowering.h"
#include "ARMSelectionDAGInfo.h"
#include "llvm/ADT/Triple.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/RegisterBankInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Target/TargetOptions.h"
#include <memory>
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "ARMGenSubtargetInfo.inc"

namespace llvm {

class ARMBaseTargetMachine;
class GlobalValue;
class StringRef;

class ARMSubtarget : public ARMGenSubtargetInfo {
protected:
  enum ARMProcFamilyEnum {
    Others,

    CortexA12,
    CortexA15,
    CortexA17,
    CortexA32,
    CortexA35,
    CortexA5,
    CortexA53,
    CortexA55,
    CortexA57,
    CortexA7,
    CortexA72,
    CortexA73,
    CortexA75,
    CortexA8,
    CortexA9,
    CortexM3,
    CortexR4,
    CortexR4F,
    CortexR5,
    CortexR52,
    CortexR7,
    Exynos,
    Krait,
    Kryo,
    Swift
  };
  enum ARMProcClassEnum {
    None,

    AClass,
    MClass,
    RClass
  };
  enum ARMArchEnum {
    ARMv2,
    ARMv2a,
    ARMv3,
    ARMv3m,
    ARMv4,
    ARMv4t,
    ARMv5,
    ARMv5t,
    ARMv5te,
    ARMv5tej,
    ARMv6,
    ARMv6k,
    ARMv6kz,
    ARMv6m,
    ARMv6sm,
    ARMv6t2,
    ARMv7a,
    ARMv7em,
    ARMv7m,
    ARMv7r,
    ARMv7ve,
    ARMv81a,
    ARMv82a,
    ARMv83a,
    ARMv84a,
    ARMv85a,
    ARMv8a,
    ARMv8mBaseline,
    ARMv8mMainline,
    ARMv8r
  };

public:
  /// What kind of timing do load multiple/store multiple instructions have.
  enum ARMLdStMultipleTiming {
    /// Can load/store 2 registers/cycle.
    DoubleIssue,
    /// Can load/store 2 registers/cycle, but needs an extra cycle if the access
    /// is not 64-bit aligned.
    DoubleIssueCheckUnalignedAccess,
    /// Can load/store 1 register/cycle.
    SingleIssue,
    /// Can load/store 1 register/cycle, but needs an extra cycle for address
    /// computation and potentially also for register writeback.
    SingleIssuePlusExtras,
  };

protected:
  /// ARMProcFamily - ARM processor family: Cortex-A8, Cortex-A9, and others.
  ARMProcFamilyEnum ARMProcFamily = Others;

  /// ARMProcClass - ARM processor class: None, AClass, RClass or MClass.
  ARMProcClassEnum ARMProcClass = None;

  /// ARMArch - ARM architecture
  ARMArchEnum ARMArch = ARMv4t;

  /// HasV4TOps, HasV5TOps, HasV5TEOps,
  /// HasV6Ops, HasV6MOps, HasV6KOps, HasV6T2Ops, HasV7Ops, HasV8Ops -
  /// Specify whether target support specific ARM ISA variants.
  bool HasV4TOps = false;
  bool HasV5TOps = false;
  bool HasV5TEOps = false;
  bool HasV6Ops = false;
  bool HasV6MOps = false;
  bool HasV6KOps = false;
  bool HasV6T2Ops = false;
  bool HasV7Ops = false;
  bool HasV8Ops = false;
  bool HasV8_1aOps = false;
  bool HasV8_2aOps = false;
  bool HasV8_3aOps = false;
  bool HasV8_4aOps = false;
  bool HasV8_5aOps = false;
  bool HasV8MBaselineOps = false;
  bool HasV8MMainlineOps = false;

  /// HasVFPv2, HasVFPv3, HasVFPv4, HasFPARMv8, HasNEON - Specify what
  /// floating point ISAs are supported.
  bool HasVFPv2 = false;
  bool HasVFPv3 = false;
  bool HasVFPv4 = false;
  bool HasFPARMv8 = false;
  bool HasNEON = false;

  /// HasDotProd - True if the ARMv8.2A dot product instructions are supported.
  bool HasDotProd = false;

  /// UseNEONForSinglePrecisionFP - if the NEONFP attribute has been
  /// specified. Use the method useNEONForSinglePrecisionFP() to
  /// determine if NEON should actually be used.
  bool UseNEONForSinglePrecisionFP = false;

  /// UseMulOps - True if non-microcoded fused integer multiply-add and
  /// multiply-subtract instructions should be used.
  bool UseMulOps = false;

  /// SlowFPVMLx - If the VFP2 / NEON instructions are available, indicates
  /// whether the FP VML[AS] instructions are slow (if so, don't use them).
  bool SlowFPVMLx = false;

  /// HasVMLxForwarding - If true, NEON has special multiplier accumulator
  /// forwarding to allow mul + mla being issued back to back.
  bool HasVMLxForwarding = false;

  /// SlowFPBrcc - True if floating point compare + branch is slow.
  bool SlowFPBrcc = false;

  /// InThumbMode - True if compiling for Thumb, false for ARM.
  bool InThumbMode = false;

  /// UseSoftFloat - True if we're using software floating point features.
  bool UseSoftFloat = false;

  /// UseMISched - True if MachineScheduler should be used for this subtarget.
  bool UseMISched = false;

  /// DisablePostRAScheduler - False if scheduling should happen again after
  /// register allocation.
  bool DisablePostRAScheduler = false;

  /// UseAA - True if using AA during codegen (DAGCombine, MISched, etc)
  bool UseAA = false;

  /// HasThumb2 - True if Thumb2 instructions are supported.
  bool HasThumb2 = false;

  /// NoARM - True if subtarget does not support ARM mode execution.
  bool NoARM = false;

  /// ReserveR9 - True if R9 is not available as a general purpose register.
  bool ReserveR9 = false;

  /// NoMovt - True if MOVT / MOVW pairs are not used for materialization of
  /// 32-bit imms (including global addresses).
  bool NoMovt = false;

  /// SupportsTailCall - True if the OS supports tail call. The dynamic linker
  /// must be able to synthesize call stubs for interworking between ARM and
  /// Thumb.
  bool SupportsTailCall = false;

  /// HasFP16 - True if subtarget supports half-precision FP conversions
  bool HasFP16 = false;

  /// HasFullFP16 - True if subtarget supports half-precision FP operations
  bool HasFullFP16 = false;

  /// HasFP16FML - True if subtarget supports half-precision FP fml operations
  bool HasFP16FML = false;

  /// HasD16 - True if subtarget is limited to 16 double precision
  /// FP registers for VFPv3.
  bool HasD16 = false;

  /// HasHardwareDivide - True if subtarget supports [su]div in Thumb mode
  bool HasHardwareDivideInThumb = false;

  /// HasHardwareDivideInARM - True if subtarget supports [su]div in ARM mode
  bool HasHardwareDivideInARM = false;

  /// HasDataBarrier - True if the subtarget supports DMB / DSB data barrier
  /// instructions.
  bool HasDataBarrier = false;

  /// HasFullDataBarrier - True if the subtarget supports DFB data barrier
  /// instruction.
  bool HasFullDataBarrier = false;

  /// HasV7Clrex - True if the subtarget supports CLREX instructions
  bool HasV7Clrex = false;

  /// HasAcquireRelease - True if the subtarget supports v8 atomics (LDA/LDAEX etc)
  /// instructions
  bool HasAcquireRelease = false;

  /// Pref32BitThumb - If true, codegen would prefer 32-bit Thumb instructions
  /// over 16-bit ones.
  bool Pref32BitThumb = false;

  /// AvoidCPSRPartialUpdate - If true, codegen would avoid using instructions
  /// that partially update CPSR and add false dependency on the previous
  /// CPSR setting instruction.
  bool AvoidCPSRPartialUpdate = false;

  /// CheapPredicableCPSRDef - If true, disable +1 predication cost
  /// for instructions updating CPSR. Enabled for Cortex-A57.
  bool CheapPredicableCPSRDef = false;

  /// AvoidMOVsShifterOperand - If true, codegen should avoid using flag setting
  /// movs with shifter operand (i.e. asr, lsl, lsr).
  bool AvoidMOVsShifterOperand = false;

  /// HasRetAddrStack - Some processors perform return stack prediction. CodeGen should
  /// avoid issue "normal" call instructions to callees which do not return.
  bool HasRetAddrStack = false;

  /// HasBranchPredictor - True if the subtarget has a branch predictor. Having
  /// a branch predictor or not changes the expected cost of taking a branch
  /// which affects the choice of whether to use predicated instructions.
  bool HasBranchPredictor = true;

  /// HasMPExtension - True if the subtarget supports Multiprocessing
  /// extension (ARMv7 only).
  bool HasMPExtension = false;

  /// HasVirtualization - True if the subtarget supports the Virtualization
  /// extension.
  bool HasVirtualization = false;

  /// FPOnlySP - If true, the floating point unit only supports single
  /// precision.
  bool FPOnlySP = false;

  /// If true, the processor supports the Performance Monitor Extensions. These
  /// include a generic cycle-counter as well as more fine-grained (often
  /// implementation-specific) events.
  bool HasPerfMon = false;

  /// HasTrustZone - if true, processor supports TrustZone security extensions
  bool HasTrustZone = false;

  /// Has8MSecExt - if true, processor supports ARMv8-M Security Extensions
  bool Has8MSecExt = false;

  /// HasSHA2 - if true, processor supports SHA1 and SHA256
  bool HasSHA2 = false;

  /// HasAES - if true, processor supports AES
  bool HasAES = false;

  /// HasCrypto - if true, processor supports Cryptography extensions
  bool HasCrypto = false;

  /// HasCRC - if true, processor supports CRC instructions
  bool HasCRC = false;

  /// HasRAS - if true, the processor supports RAS extensions
  bool HasRAS = false;

  /// If true, the instructions "vmov.i32 d0, #0" and "vmov.i32 q0, #0" are
  /// particularly effective at zeroing a VFP register.
  bool HasZeroCycleZeroing = false;

  /// HasFPAO - if true, processor  does positive address offset computation faster
  bool HasFPAO = false;

  /// HasFuseAES - if true, processor executes back to back AES instruction
  /// pairs faster.
  bool HasFuseAES = false;

  /// HasFuseLiterals - if true, processor executes back to back
  /// bottom and top halves of literal generation faster.
  bool HasFuseLiterals = false;

  /// If true, if conversion may decide to leave some instructions unpredicated.
  bool IsProfitableToUnpredicate = false;

  /// If true, VMOV will be favored over VGETLNi32.
  bool HasSlowVGETLNi32 = false;

  /// If true, VMOV will be favored over VDUP.
  bool HasSlowVDUP32 = false;

  /// If true, VMOVSR will be favored over VMOVDRR.
  bool PreferVMOVSR = false;

  /// If true, ISHST barriers will be used for Release semantics.
  bool PreferISHST = false;

  /// If true, a VLDM/VSTM starting with an odd register number is considered to
  /// take more microops than single VLDRS/VSTRS.
  bool SlowOddRegister = false;

  /// If true, loading into a D subregister will be penalized.
  bool SlowLoadDSubregister = false;

  /// If true, use a wider stride when allocating VFP registers.
  bool UseWideStrideVFP = false;

  /// If true, the AGU and NEON/FPU units are multiplexed.
  bool HasMuxedUnits = false;

  /// If true, VMOVS will never be widened to VMOVD.
  bool DontWidenVMOVS = false;

  /// If true, splat a register between VFP and NEON instructions.
  bool SplatVFPToNeon = false;

  /// If true, run the MLx expansion pass.
  bool ExpandMLx = false;

  /// If true, VFP/NEON VMLA/VMLS have special RAW hazards.
  bool HasVMLxHazards = false;

  // If true, read thread pointer from coprocessor register.
  bool ReadTPHard = false;

  /// If true, VMOVRS, VMOVSR and VMOVS will be converted from VFP to NEON.
  bool UseNEONForFPMovs = false;

  /// If true, VLDn instructions take an extra cycle for unaligned accesses.
  bool CheckVLDnAlign = false;

  /// If true, VFP instructions are not pipelined.
  bool NonpipelinedVFP = false;

  /// StrictAlign - If true, the subtarget disallows unaligned memory
  /// accesses for some types.  For details, see
  /// ARMTargetLowering::allowsMisalignedMemoryAccesses().
  bool StrictAlign = false;

  /// RestrictIT - If true, the subtarget disallows generation of deprecated IT
  ///  blocks to conform to ARMv8 rule.
  bool RestrictIT = false;

  /// HasDSP - If true, the subtarget supports the DSP (saturating arith
  /// and such) instructions.
  bool HasDSP = false;

  /// NaCl TRAP instruction is generated instead of the regular TRAP.
  bool UseNaClTrap = false;

  /// Generate calls via indirect call instructions.
  bool GenLongCalls = false;

  /// Generate code that does not contain data access to code sections.
  bool GenExecuteOnly = false;

  /// Target machine allowed unsafe FP math (such as use of NEON fp)
  bool UnsafeFPMath = false;

  /// UseSjLjEH - If true, the target uses SjLj exception handling (e.g. iOS).
  bool UseSjLjEH = false;

  /// Has speculation barrier
  bool HasSB = false;

  /// Implicitly convert an instruction to a different one if its immediates
  /// cannot be encoded. For example, ADD r0, r1, #FFFFFFFF -> SUB r0, r1, #1.
  bool NegativeImmediates = true;

  /// stackAlignment - The minimum alignment known to hold of the stack frame on
  /// entry to the function and which must be maintained by every function.
  unsigned stackAlignment = 4;

  /// CPUString - String name of used CPU.
  std::string CPUString;

  unsigned MaxInterleaveFactor = 1;

  /// Clearance before partial register updates (in number of instructions)
  unsigned PartialUpdateClearance = 0;

  /// What kind of timing do load multiple/store multiple have (double issue,
  /// single issue etc).
  ARMLdStMultipleTiming LdStMultipleTiming = SingleIssue;

  /// The adjustment that we need to apply to get the operand latency from the
  /// operand cycle returned by the itinerary data for pre-ISel operands.
  int PreISelOperandLatencyAdjustment = 2;

  /// What alignment is preferred for loop bodies, in log2(bytes).
  unsigned PrefLoopAlignment = 0;

  /// IsLittle - The target is Little Endian
  bool IsLittle;

  /// TargetTriple - What processor and OS we're targeting.
  Triple TargetTriple;

  /// SchedModel - Processor specific instruction costs.
  MCSchedModel SchedModel;

  /// Selected instruction itineraries (one entry per itinerary class.)
  InstrItineraryData InstrItins;

  /// Options passed via command line that could influence the target
  const TargetOptions &Options;

  const ARMBaseTargetMachine &TM;

public:
  /// This constructor initializes the data members to match that
  /// of the specified triple.
  ///
  ARMSubtarget(const Triple &TT, const std::string &CPU, const std::string &FS,
               const ARMBaseTargetMachine &TM, bool IsLittle);

  /// getMaxInlineSizeThreshold - Returns the maximum memset / memcpy size
  /// that still makes it profitable to inline the call.
  unsigned getMaxInlineSizeThreshold() const {
    return 64;
  }

  /// ParseSubtargetFeatures - Parses features string setting specified
  /// subtarget options.  Definition of function is auto generated by tblgen.
  void ParseSubtargetFeatures(StringRef CPU, StringRef FS);

  /// initializeSubtargetDependencies - Initializes using a CPU and feature string
  /// so that we can use initializer lists for subtarget initialization.
  ARMSubtarget &initializeSubtargetDependencies(StringRef CPU, StringRef FS);

  const ARMSelectionDAGInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }

  const ARMBaseInstrInfo *getInstrInfo() const override {
    return InstrInfo.get();
  }

  const ARMTargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }

  const ARMFrameLowering *getFrameLowering() const override {
    return FrameLowering.get();
  }

  const ARMBaseRegisterInfo *getRegisterInfo() const override {
    return &InstrInfo->getRegisterInfo();
  }

  const CallLowering *getCallLowering() const override;
  const InstructionSelector *getInstructionSelector() const override;
  const LegalizerInfo *getLegalizerInfo() const override;
  const RegisterBankInfo *getRegBankInfo() const override;

private:
  ARMSelectionDAGInfo TSInfo;
  // Either Thumb1FrameLowering or ARMFrameLowering.
  std::unique_ptr<ARMFrameLowering> FrameLowering;
  // Either Thumb1InstrInfo or Thumb2InstrInfo.
  std::unique_ptr<ARMBaseInstrInfo> InstrInfo;
  ARMTargetLowering   TLInfo;

  /// GlobalISel related APIs.
  std::unique_ptr<CallLowering> CallLoweringInfo;
  std::unique_ptr<InstructionSelector> InstSelector;
  std::unique_ptr<LegalizerInfo> Legalizer;
  std::unique_ptr<RegisterBankInfo> RegBankInfo;

  void initializeEnvironment();
  void initSubtargetFeatures(StringRef CPU, StringRef FS);
  ARMFrameLowering *initializeFrameLowering(StringRef CPU, StringRef FS);

public:
  void computeIssueWidth();

  bool hasV4TOps()  const { return HasV4TOps;  }
  bool hasV5TOps()  const { return HasV5TOps;  }
  bool hasV5TEOps() const { return HasV5TEOps; }
  bool hasV6Ops()   const { return HasV6Ops;   }
  bool hasV6MOps()  const { return HasV6MOps;  }
  bool hasV6KOps()  const { return HasV6KOps; }
  bool hasV6T2Ops() const { return HasV6T2Ops; }
  bool hasV7Ops()   const { return HasV7Ops;  }
  bool hasV8Ops()   const { return HasV8Ops;  }
  bool hasV8_1aOps() const { return HasV8_1aOps; }
  bool hasV8_2aOps() const { return HasV8_2aOps; }
  bool hasV8_3aOps() const { return HasV8_3aOps; }
  bool hasV8_4aOps() const { return HasV8_4aOps; }
  bool hasV8_5aOps() const { return HasV8_5aOps; }
  bool hasV8MBaselineOps() const { return HasV8MBaselineOps; }
  bool hasV8MMainlineOps() const { return HasV8MMainlineOps; }

  /// @{
  /// These functions are obsolete, please consider adding subtarget features
  /// or properties instead of calling them.
  bool isCortexA5() const { return ARMProcFamily == CortexA5; }
  bool isCortexA7() const { return ARMProcFamily == CortexA7; }
  bool isCortexA8() const { return ARMProcFamily == CortexA8; }
  bool isCortexA9() const { return ARMProcFamily == CortexA9; }
  bool isCortexA15() const { return ARMProcFamily == CortexA15; }
  bool isSwift()    const { return ARMProcFamily == Swift; }
  bool isCortexM3() const { return ARMProcFamily == CortexM3; }
  bool isLikeA9() const { return isCortexA9() || isCortexA15() || isKrait(); }
  bool isCortexR5() const { return ARMProcFamily == CortexR5; }
  bool isKrait() const { return ARMProcFamily == Krait; }
  /// @}

  bool hasARMOps() const { return !NoARM; }

  bool hasVFP2() const { return HasVFPv2; }
  bool hasVFP3() const { return HasVFPv3; }
  bool hasVFP4() const { return HasVFPv4; }
  bool hasFPARMv8() const { return HasFPARMv8; }
  bool hasNEON() const { return HasNEON;  }
  bool hasSHA2() const { return HasSHA2; }
  bool hasAES() const { return HasAES; }
  bool hasCrypto() const { return HasCrypto; }
  bool hasDotProd() const { return HasDotProd; }
  bool hasCRC() const { return HasCRC; }
  bool hasRAS() const { return HasRAS; }
  bool hasVirtualization() const { return HasVirtualization; }

  bool useNEONForSinglePrecisionFP() const {
    return hasNEON() && UseNEONForSinglePrecisionFP;
  }

  bool hasDivideInThumbMode() const { return HasHardwareDivideInThumb; }
  bool hasDivideInARMMode() const { return HasHardwareDivideInARM; }
  bool hasDataBarrier() const { return HasDataBarrier; }
  bool hasFullDataBarrier() const { return HasFullDataBarrier; }
  bool hasV7Clrex() const { return HasV7Clrex; }
  bool hasAcquireRelease() const { return HasAcquireRelease; }

  bool hasAnyDataBarrier() const {
    return HasDataBarrier || (hasV6Ops() && !isThumb());
  }

  bool useMulOps() const { return UseMulOps; }
  bool useFPVMLx() const { return !SlowFPVMLx; }
  bool hasVMLxForwarding() const { return HasVMLxForwarding; }
  bool isFPBrccSlow() const { return SlowFPBrcc; }
  bool isFPOnlySP() const { return FPOnlySP; }
  bool hasPerfMon() const { return HasPerfMon; }
  bool hasTrustZone() const { return HasTrustZone; }
  bool has8MSecExt() const { return Has8MSecExt; }
  bool hasZeroCycleZeroing() const { return HasZeroCycleZeroing; }
  bool hasFPAO() const { return HasFPAO; }
  bool isProfitableToUnpredicate() const { return IsProfitableToUnpredicate; }
  bool hasSlowVGETLNi32() const { return HasSlowVGETLNi32; }
  bool hasSlowVDUP32() const { return HasSlowVDUP32; }
  bool preferVMOVSR() const { return PreferVMOVSR; }
  bool preferISHSTBarriers() const { return PreferISHST; }
  bool expandMLx() const { return ExpandMLx; }
  bool hasVMLxHazards() const { return HasVMLxHazards; }
  bool hasSlowOddRegister() const { return SlowOddRegister; }
  bool hasSlowLoadDSubregister() const { return SlowLoadDSubregister; }
  bool useWideStrideVFP() const { return UseWideStrideVFP; }
  bool hasMuxedUnits() const { return HasMuxedUnits; }
  bool dontWidenVMOVS() const { return DontWidenVMOVS; }
  bool useSplatVFPToNeon() const { return SplatVFPToNeon; }
  bool useNEONForFPMovs() const { return UseNEONForFPMovs; }
  bool checkVLDnAccessAlignment() const { return CheckVLDnAlign; }
  bool nonpipelinedVFP() const { return NonpipelinedVFP; }
  bool prefers32BitThumb() const { return Pref32BitThumb; }
  bool avoidCPSRPartialUpdate() const { return AvoidCPSRPartialUpdate; }
  bool cheapPredicableCPSRDef() const { return CheapPredicableCPSRDef; }
  bool avoidMOVsShifterOperand() const { return AvoidMOVsShifterOperand; }
  bool hasRetAddrStack() const { return HasRetAddrStack; }
  bool hasBranchPredictor() const { return HasBranchPredictor; }
  bool hasMPExtension() const { return HasMPExtension; }
  bool hasDSP() const { return HasDSP; }
  bool useNaClTrap() const { return UseNaClTrap; }
  bool useSjLjEH() const { return UseSjLjEH; }
  bool hasSB() const { return HasSB; }
  bool genLongCalls() const { return GenLongCalls; }
  bool genExecuteOnly() const { return GenExecuteOnly; }

  bool hasFP16() const { return HasFP16; }
  bool hasD16() const { return HasD16; }
  bool hasFullFP16() const { return HasFullFP16; }
  bool hasFP16FML() const { return HasFP16FML; }

  bool hasFuseAES() const { return HasFuseAES; }
  bool hasFuseLiterals() const { return HasFuseLiterals; }
  /// Return true if the CPU supports any kind of instruction fusion.
  bool hasFusion() const { return hasFuseAES() || hasFuseLiterals(); }

  const Triple &getTargetTriple() const { return TargetTriple; }

  bool isTargetDarwin() const { return TargetTriple.isOSDarwin(); }
  bool isTargetIOS() const { return TargetTriple.isiOS(); }
  bool isTargetWatchOS() const { return TargetTriple.isWatchOS(); }
  bool isTargetWatchABI() const { return TargetTriple.isWatchABI(); }
  bool isTargetLinux() const { return TargetTriple.isOSLinux(); }
  bool isTargetNaCl() const { return TargetTriple.isOSNaCl(); }
  bool isTargetNetBSD() const { return TargetTriple.isOSNetBSD(); }
  bool isTargetWindows() const { return TargetTriple.isOSWindows(); }

  bool isTargetCOFF() const { return TargetTriple.isOSBinFormatCOFF(); }
  bool isTargetELF() const { return TargetTriple.isOSBinFormatELF(); }
  bool isTargetMachO() const { return TargetTriple.isOSBinFormatMachO(); }

  // ARM EABI is the bare-metal EABI described in ARM ABI documents and
  // can be accessed via -target arm-none-eabi. This is NOT GNUEABI.
  // FIXME: Add a flag for bare-metal for that target and set Triple::EABI
  // even for GNUEABI, so we can make a distinction here and still conform to
  // the EABI on GNU (and Android) mode. This requires change in Clang, too.
  // FIXME: The Darwin exception is temporary, while we move users to
  // "*-*-*-macho" triples as quickly as possible.
  bool isTargetAEABI() const {
    return (TargetTriple.getEnvironment() == Triple::EABI ||
            TargetTriple.getEnvironment() == Triple::EABIHF) &&
           !isTargetDarwin() && !isTargetWindows();
  }
  bool isTargetGNUAEABI() const {
    return (TargetTriple.getEnvironment() == Triple::GNUEABI ||
            TargetTriple.getEnvironment() == Triple::GNUEABIHF) &&
           !isTargetDarwin() && !isTargetWindows();
  }
  bool isTargetMuslAEABI() const {
    return (TargetTriple.getEnvironment() == Triple::MuslEABI ||
            TargetTriple.getEnvironment() == Triple::MuslEABIHF) &&
           !isTargetDarwin() && !isTargetWindows();
  }

  // ARM Targets that support EHABI exception handling standard
  // Darwin uses SjLj. Other targets might need more checks.
  bool isTargetEHABICompatible() const {
    return (TargetTriple.getEnvironment() == Triple::EABI ||
            TargetTriple.getEnvironment() == Triple::GNUEABI ||
            TargetTriple.getEnvironment() == Triple::MuslEABI ||
            TargetTriple.getEnvironment() == Triple::EABIHF ||
            TargetTriple.getEnvironment() == Triple::GNUEABIHF ||
            TargetTriple.getEnvironment() == Triple::MuslEABIHF ||
            isTargetAndroid()) &&
           !isTargetDarwin() && !isTargetWindows();
  }

  bool isTargetHardFloat() const;

  bool isTargetAndroid() const { return TargetTriple.isAndroid(); }

  bool isXRaySupported() const override;

  bool isAPCS_ABI() const;
  bool isAAPCS_ABI() const;
  bool isAAPCS16_ABI() const;

  bool isROPI() const;
  bool isRWPI() const;

  bool useMachineScheduler() const { return UseMISched; }
  bool disablePostRAScheduler() const { return DisablePostRAScheduler; }
  bool useSoftFloat() const { return UseSoftFloat; }
  bool isThumb() const { return InThumbMode; }
  bool isThumb1Only() const { return InThumbMode && !HasThumb2; }
  bool isThumb2() const { return InThumbMode && HasThumb2; }
  bool hasThumb2() const { return HasThumb2; }
  bool isMClass() const { return ARMProcClass == MClass; }
  bool isRClass() const { return ARMProcClass == RClass; }
  bool isAClass() const { return ARMProcClass == AClass; }
  bool isReadTPHard() const { return ReadTPHard; }

  bool isR9Reserved() const {
    return isTargetMachO() ? (ReserveR9 || !HasV6Ops) : ReserveR9;
  }

  bool useR7AsFramePointer() const {
    return isTargetDarwin() || (!isTargetWindows() && isThumb());
  }

  /// Returns true if the frame setup is split into two separate pushes (first
  /// r0-r7,lr then r8-r11), principally so that the frame pointer is adjacent
  /// to lr. This is always required on Thumb1-only targets, as the push and
  /// pop instructions can't access the high registers.
  bool splitFramePushPop(const MachineFunction &MF) const {
    return (useR7AsFramePointer() &&
            MF.getTarget().Options.DisableFramePointerElim(MF)) ||
           isThumb1Only();
  }

  bool useStride4VFPs(const MachineFunction &MF) const;

  bool useMovt(const MachineFunction &MF) const;

  bool supportsTailCall() const { return SupportsTailCall; }

  bool allowsUnalignedMem() const { return !StrictAlign; }

  bool restrictIT() const { return RestrictIT; }

  const std::string & getCPUString() const { return CPUString; }

  bool isLittle() const { return IsLittle; }

  unsigned getMispredictionPenalty() const;

  /// Returns true if machine scheduler should be enabled.
  bool enableMachineScheduler() const override;

  /// True for some subtargets at > -O0.
  bool enablePostRAScheduler() const override;

  /// Enable use of alias analysis during code generation (during MI
  /// scheduling, DAGCombine, etc.).
  bool useAA() const override { return UseAA; }

  // enableAtomicExpand- True if we need to expand our atomics.
  bool enableAtomicExpand() const override;

  /// getInstrItins - Return the instruction itineraries based on subtarget
  /// selection.
  const InstrItineraryData *getInstrItineraryData() const override {
    return &InstrItins;
  }

  /// getStackAlignment - Returns the minimum alignment known to hold of the
  /// stack frame on entry to the function and which must be maintained by every
  /// function for this subtarget.
  unsigned getStackAlignment() const { return stackAlignment; }

  unsigned getMaxInterleaveFactor() const { return MaxInterleaveFactor; }

  unsigned getPartialUpdateClearance() const { return PartialUpdateClearance; }

  ARMLdStMultipleTiming getLdStMultipleTiming() const {
    return LdStMultipleTiming;
  }

  int getPreISelOperandLatencyAdjustment() const {
    return PreISelOperandLatencyAdjustment;
  }

  /// True if the GV will be accessed via an indirect symbol.
  bool isGVIndirectSymbol(const GlobalValue *GV) const;

  /// Returns the constant pool modifier needed to access the GV.
  bool isGVInGOT(const GlobalValue *GV) const;

  /// True if fast-isel is used.
  bool useFastISel() const;

  /// Returns the correct return opcode for the current feature set.
  /// Use BX if available to allow mixing thumb/arm code, but fall back
  /// to plain mov pc,lr on ARMv4.
  unsigned getReturnOpcode() const {
    if (isThumb())
      return ARM::tBX_RET;
    if (hasV4TOps())
      return ARM::BX_RET;
    return ARM::MOVPCLR;
  }

  /// Allow movt+movw for PIC global address calculation.
  /// ELF does not have GOT relocations for movt+movw.
  /// ROPI does not use GOT.
  bool allowPositionIndependentMovt() const {
    return isROPI() || !isTargetELF();
  }

  unsigned getPrefLoopAlignment() const {
    return PrefLoopAlignment;
  }
};

} // end namespace llvm

#endif  // LLVM_LIB_TARGET_ARM_ARMSUBTARGET_H
