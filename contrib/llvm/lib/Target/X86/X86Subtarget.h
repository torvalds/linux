//===-- X86Subtarget.h - Define Subtarget for the X86 ----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the X86 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86SUBTARGET_H
#define LLVM_LIB_TARGET_X86_X86SUBTARGET_H

#include "X86FrameLowering.h"
#include "X86ISelLowering.h"
#include "X86InstrInfo.h"
#include "X86SelectionDAGInfo.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Target/TargetMachine.h"
#include <climits>
#include <memory>

#define GET_SUBTARGETINFO_HEADER
#include "X86GenSubtargetInfo.inc"

namespace llvm {

class GlobalValue;

/// The X86 backend supports a number of different styles of PIC.
///
namespace PICStyles {

enum Style {
  StubPIC,          // Used on i386-darwin in pic mode.
  GOT,              // Used on 32 bit elf on when in pic mode.
  RIPRel,           // Used on X86-64 when in pic mode.
  None              // Set when not in pic mode.
};

} // end namespace PICStyles

class X86Subtarget final : public X86GenSubtargetInfo {
public:
  // NOTE: Do not add anything new to this list. Coarse, CPU name based flags
  // are not a good idea. We should be migrating away from these.
  enum X86ProcFamilyEnum {
    Others,
    IntelAtom,
    IntelSLM,
    IntelGLM,
    IntelGLP,
    IntelTRM
  };

protected:
  enum X86SSEEnum {
    NoSSE, SSE1, SSE2, SSE3, SSSE3, SSE41, SSE42, AVX, AVX2, AVX512F
  };

  enum X863DNowEnum {
    NoThreeDNow, MMX, ThreeDNow, ThreeDNowA
  };

  /// X86 processor family: Intel Atom, and others
  X86ProcFamilyEnum X86ProcFamily = Others;

  /// Which PIC style to use
  PICStyles::Style PICStyle;

  const TargetMachine &TM;

  /// SSE1, SSE2, SSE3, SSSE3, SSE41, SSE42, or none supported.
  X86SSEEnum X86SSELevel = NoSSE;

  /// MMX, 3DNow, 3DNow Athlon, or none supported.
  X863DNowEnum X863DNowLevel = NoThreeDNow;

  /// True if the processor supports X87 instructions.
  bool HasX87 = false;

  /// True if this processor has NOPL instruction
  /// (generally pentium pro+).
  bool HasNOPL = false;

  /// True if this processor has conditional move instructions
  /// (generally pentium pro+).
  bool HasCMov = false;

  /// True if the processor supports X86-64 instructions.
  bool HasX86_64 = false;

  /// True if the processor supports POPCNT.
  bool HasPOPCNT = false;

  /// True if the processor supports SSE4A instructions.
  bool HasSSE4A = false;

  /// Target has AES instructions
  bool HasAES = false;
  bool HasVAES = false;

  /// Target has FXSAVE/FXRESTOR instructions
  bool HasFXSR = false;

  /// Target has XSAVE instructions
  bool HasXSAVE = false;

  /// Target has XSAVEOPT instructions
  bool HasXSAVEOPT = false;

  /// Target has XSAVEC instructions
  bool HasXSAVEC = false;

  /// Target has XSAVES instructions
  bool HasXSAVES = false;

  /// Target has carry-less multiplication
  bool HasPCLMUL = false;
  bool HasVPCLMULQDQ = false;

  /// Target has Galois Field Arithmetic instructions
  bool HasGFNI = false;

  /// Target has 3-operand fused multiply-add
  bool HasFMA = false;

  /// Target has 4-operand fused multiply-add
  bool HasFMA4 = false;

  /// Target has XOP instructions
  bool HasXOP = false;

  /// Target has TBM instructions.
  bool HasTBM = false;

  /// Target has LWP instructions
  bool HasLWP = false;

  /// True if the processor has the MOVBE instruction.
  bool HasMOVBE = false;

  /// True if the processor has the RDRAND instruction.
  bool HasRDRAND = false;

  /// Processor has 16-bit floating point conversion instructions.
  bool HasF16C = false;

  /// Processor has FS/GS base insturctions.
  bool HasFSGSBase = false;

  /// Processor has LZCNT instruction.
  bool HasLZCNT = false;

  /// Processor has BMI1 instructions.
  bool HasBMI = false;

  /// Processor has BMI2 instructions.
  bool HasBMI2 = false;

  /// Processor has VBMI instructions.
  bool HasVBMI = false;

  /// Processor has VBMI2 instructions.
  bool HasVBMI2 = false;

  /// Processor has Integer Fused Multiply Add
  bool HasIFMA = false;

  /// Processor has RTM instructions.
  bool HasRTM = false;

  /// Processor has ADX instructions.
  bool HasADX = false;

  /// Processor has SHA instructions.
  bool HasSHA = false;

  /// Processor has PRFCHW instructions.
  bool HasPRFCHW = false;

  /// Processor has RDSEED instructions.
  bool HasRDSEED = false;

  /// Processor has LAHF/SAHF instructions.
  bool HasLAHFSAHF = false;

  /// Processor has MONITORX/MWAITX instructions.
  bool HasMWAITX = false;

  /// Processor has Cache Line Zero instruction
  bool HasCLZERO = false;

  /// Processor has Cache Line Demote instruction
  bool HasCLDEMOTE = false;

  /// Processor has MOVDIRI instruction (direct store integer).
  bool HasMOVDIRI = false;

  /// Processor has MOVDIR64B instruction (direct store 64 bytes).
  bool HasMOVDIR64B = false;

  /// Processor has ptwrite instruction.
  bool HasPTWRITE = false;

  /// Processor has Prefetch with intent to Write instruction
  bool HasPREFETCHWT1 = false;

  /// True if SHLD instructions are slow.
  bool IsSHLDSlow = false;

  /// True if the PMULLD instruction is slow compared to PMULLW/PMULHW and
  //  PMULUDQ.
  bool IsPMULLDSlow = false;

  /// True if the PMADDWD instruction is slow compared to PMULLD.
  bool IsPMADDWDSlow = false;

  /// True if unaligned memory accesses of 16-bytes are slow.
  bool IsUAMem16Slow = false;

  /// True if unaligned memory accesses of 32-bytes are slow.
  bool IsUAMem32Slow = false;

  /// True if SSE operations can have unaligned memory operands.
  /// This may require setting a configuration bit in the processor.
  bool HasSSEUnalignedMem = false;

  /// True if this processor has the CMPXCHG16B instruction;
  /// this is true for most x86-64 chips, but not the first AMD chips.
  bool HasCmpxchg16b = false;

  /// True if the LEA instruction should be used for adjusting
  /// the stack pointer. This is an optimization for Intel Atom processors.
  bool UseLeaForSP = false;

  /// True if POPCNT instruction has a false dependency on the destination register.
  bool HasPOPCNTFalseDeps = false;

  /// True if LZCNT/TZCNT instructions have a false dependency on the destination register.
  bool HasLZCNTFalseDeps = false;

  /// True if its preferable to combine to a single shuffle using a variable
  /// mask over multiple fixed shuffles.
  bool HasFastVariableShuffle = false;

  /// True if there is no performance penalty to writing only the lower parts
  /// of a YMM or ZMM register without clearing the upper part.
  bool HasFastPartialYMMorZMMWrite = false;

  /// True if there is no performance penalty for writing NOPs with up to
  /// 11 bytes.
  bool HasFast11ByteNOP = false;

  /// True if there is no performance penalty for writing NOPs with up to
  /// 15 bytes.
  bool HasFast15ByteNOP = false;

  /// True if gather is reasonably fast. This is true for Skylake client and
  /// all AVX-512 CPUs.
  bool HasFastGather = false;

  /// True if hardware SQRTSS instruction is at least as fast (latency) as
  /// RSQRTSS followed by a Newton-Raphson iteration.
  bool HasFastScalarFSQRT = false;

  /// True if hardware SQRTPS/VSQRTPS instructions are at least as fast
  /// (throughput) as RSQRTPS/VRSQRTPS followed by a Newton-Raphson iteration.
  bool HasFastVectorFSQRT = false;

  /// True if 8-bit divisions are significantly faster than
  /// 32-bit divisions and should be used when possible.
  bool HasSlowDivide32 = false;

  /// True if 32-bit divides are significantly faster than
  /// 64-bit divisions and should be used when possible.
  bool HasSlowDivide64 = false;

  /// True if LZCNT instruction is fast.
  bool HasFastLZCNT = false;

  /// True if SHLD based rotate is fast.
  bool HasFastSHLDRotate = false;

  /// True if the processor supports macrofusion.
  bool HasMacroFusion = false;

  /// True if the processor has enhanced REP MOVSB/STOSB.
  bool HasERMSB = false;

  /// True if the short functions should be padded to prevent
  /// a stall when returning too early.
  bool PadShortFunctions = false;

  /// True if two memory operand instructions should use a temporary register
  /// instead.
  bool SlowTwoMemOps = false;

  /// True if the LEA instruction inputs have to be ready at address generation
  /// (AG) time.
  bool LEAUsesAG = false;

  /// True if the LEA instruction with certain arguments is slow
  bool SlowLEA = false;

  /// True if the LEA instruction has all three source operands: base, index,
  /// and offset or if the LEA instruction uses base and index registers where
  /// the base is EBP, RBP,or R13
  bool Slow3OpsLEA = false;

  /// True if INC and DEC instructions are slow when writing to flags
  bool SlowIncDec = false;

  /// Processor has AVX-512 PreFetch Instructions
  bool HasPFI = false;

  /// Processor has AVX-512 Exponential and Reciprocal Instructions
  bool HasERI = false;

  /// Processor has AVX-512 Conflict Detection Instructions
  bool HasCDI = false;

  /// Processor has AVX-512 population count Instructions
  bool HasVPOPCNTDQ = false;

  /// Processor has AVX-512 Doubleword and Quadword instructions
  bool HasDQI = false;

  /// Processor has AVX-512 Byte and Word instructions
  bool HasBWI = false;

  /// Processor has AVX-512 Vector Length eXtenstions
  bool HasVLX = false;

  /// Processor has PKU extenstions
  bool HasPKU = false;

  /// Processor has AVX-512 Vector Neural Network Instructions
  bool HasVNNI = false;

  /// Processor has AVX-512 Bit Algorithms instructions
  bool HasBITALG = false;

  /// Processor supports MPX - Memory Protection Extensions
  bool HasMPX = false;

  /// Processor supports CET SHSTK - Control-Flow Enforcement Technology
  /// using Shadow Stack
  bool HasSHSTK = false;

  /// Processor supports Invalidate Process-Context Identifier
  bool HasINVPCID = false;

  /// Processor has Software Guard Extensions
  bool HasSGX = false;

  /// Processor supports Flush Cache Line instruction
  bool HasCLFLUSHOPT = false;

  /// Processor supports Cache Line Write Back instruction
  bool HasCLWB = false;

  /// Processor supports Write Back No Invalidate instruction
  bool HasWBNOINVD = false;

  /// Processor support RDPID instruction
  bool HasRDPID = false;

  /// Processor supports WaitPKG instructions
  bool HasWAITPKG = false;

  /// Processor supports PCONFIG instruction
  bool HasPCONFIG = false;

  /// Processor has a single uop BEXTR implementation.
  bool HasFastBEXTR = false;

  /// Try harder to combine to horizontal vector ops if they are fast.
  bool HasFastHorizontalOps = false;

  /// Use a retpoline thunk rather than indirect calls to block speculative
  /// execution.
  bool UseRetpolineIndirectCalls = false;

  /// Use a retpoline thunk or remove any indirect branch to block speculative
  /// execution.
  bool UseRetpolineIndirectBranches = false;

  /// Deprecated flag, query `UseRetpolineIndirectCalls` and
  /// `UseRetpolineIndirectBranches` instead.
  bool DeprecatedUseRetpoline = false;

  /// When using a retpoline thunk, call an externally provided thunk rather
  /// than emitting one inside the compiler.
  bool UseRetpolineExternalThunk = false;

  /// Use software floating point for code generation.
  bool UseSoftFloat = false;

  /// The minimum alignment known to hold of the stack frame on
  /// entry to the function and which must be maintained by every function.
  unsigned stackAlignment = 4;

  /// Max. memset / memcpy size that is turned into rep/movs, rep/stos ops.
  ///
  // FIXME: this is a known good value for Yonah. How about others?
  unsigned MaxInlineSizeThreshold = 128;

  /// Indicates target prefers 256 bit instructions.
  bool Prefer256Bit = false;

  /// Threeway branch is profitable in this subtarget.
  bool ThreewayBranchProfitable = false;

  /// What processor and OS we're targeting.
  Triple TargetTriple;

  /// GlobalISel related APIs.
  std::unique_ptr<CallLowering> CallLoweringInfo;
  std::unique_ptr<LegalizerInfo> Legalizer;
  std::unique_ptr<RegisterBankInfo> RegBankInfo;
  std::unique_ptr<InstructionSelector> InstSelector;

private:
  /// Override the stack alignment.
  unsigned StackAlignOverride;

  /// Preferred vector width from function attribute.
  unsigned PreferVectorWidthOverride;

  /// Resolved preferred vector width from function attribute and subtarget
  /// features.
  unsigned PreferVectorWidth = UINT32_MAX;

  /// Required vector width from function attribute.
  unsigned RequiredVectorWidth;

  /// True if compiling for 64-bit, false for 16-bit or 32-bit.
  bool In64BitMode;

  /// True if compiling for 32-bit, false for 16-bit or 64-bit.
  bool In32BitMode;

  /// True if compiling for 16-bit, false for 32-bit or 64-bit.
  bool In16BitMode;

  /// Contains the Overhead of gather\scatter instructions
  int GatherOverhead = 1024;
  int ScatterOverhead = 1024;

  X86SelectionDAGInfo TSInfo;
  // Ordering here is important. X86InstrInfo initializes X86RegisterInfo which
  // X86TargetLowering needs.
  X86InstrInfo InstrInfo;
  X86TargetLowering TLInfo;
  X86FrameLowering FrameLowering;

public:
  /// This constructor initializes the data members to match that
  /// of the specified triple.
  ///
  X86Subtarget(const Triple &TT, StringRef CPU, StringRef FS,
               const X86TargetMachine &TM, unsigned StackAlignOverride,
               unsigned PreferVectorWidthOverride,
               unsigned RequiredVectorWidth);

  const X86TargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }

  const X86InstrInfo *getInstrInfo() const override { return &InstrInfo; }

  const X86FrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }

  const X86SelectionDAGInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }

  const X86RegisterInfo *getRegisterInfo() const override {
    return &getInstrInfo()->getRegisterInfo();
  }

  /// Returns the minimum alignment known to hold of the
  /// stack frame on entry to the function and which must be maintained by every
  /// function for this subtarget.
  unsigned getStackAlignment() const { return stackAlignment; }

  /// Returns the maximum memset / memcpy size
  /// that still makes it profitable to inline the call.
  unsigned getMaxInlineSizeThreshold() const { return MaxInlineSizeThreshold; }

  /// ParseSubtargetFeatures - Parses features string setting specified
  /// subtarget options.  Definition of function is auto generated by tblgen.
  void ParseSubtargetFeatures(StringRef CPU, StringRef FS);

  /// Methods used by Global ISel
  const CallLowering *getCallLowering() const override;
  const InstructionSelector *getInstructionSelector() const override;
  const LegalizerInfo *getLegalizerInfo() const override;
  const RegisterBankInfo *getRegBankInfo() const override;

private:
  /// Initialize the full set of dependencies so we can use an initializer
  /// list for X86Subtarget.
  X86Subtarget &initializeSubtargetDependencies(StringRef CPU, StringRef FS);
  void initSubtargetFeatures(StringRef CPU, StringRef FS);

public:
  /// Is this x86_64? (disregarding specific ABI / programming model)
  bool is64Bit() const {
    return In64BitMode;
  }

  bool is32Bit() const {
    return In32BitMode;
  }

  bool is16Bit() const {
    return In16BitMode;
  }

  /// Is this x86_64 with the ILP32 programming model (x32 ABI)?
  bool isTarget64BitILP32() const {
    return In64BitMode && (TargetTriple.getEnvironment() == Triple::GNUX32 ||
                           TargetTriple.isOSNaCl());
  }

  /// Is this x86_64 with the LP64 programming model (standard AMD64, no x32)?
  bool isTarget64BitLP64() const {
    return In64BitMode && (TargetTriple.getEnvironment() != Triple::GNUX32 &&
                           !TargetTriple.isOSNaCl());
  }

  PICStyles::Style getPICStyle() const { return PICStyle; }
  void setPICStyle(PICStyles::Style Style)  { PICStyle = Style; }

  bool hasX87() const { return HasX87; }
  bool hasNOPL() const { return HasNOPL; }
  // SSE codegen depends on cmovs, and all SSE1+ processors support them.
  // All 64-bit processors support cmov.
  bool hasCMov() const { return HasCMov || X86SSELevel >= SSE1 || is64Bit(); }
  bool hasSSE1() const { return X86SSELevel >= SSE1; }
  bool hasSSE2() const { return X86SSELevel >= SSE2; }
  bool hasSSE3() const { return X86SSELevel >= SSE3; }
  bool hasSSSE3() const { return X86SSELevel >= SSSE3; }
  bool hasSSE41() const { return X86SSELevel >= SSE41; }
  bool hasSSE42() const { return X86SSELevel >= SSE42; }
  bool hasAVX() const { return X86SSELevel >= AVX; }
  bool hasAVX2() const { return X86SSELevel >= AVX2; }
  bool hasAVX512() const { return X86SSELevel >= AVX512F; }
  bool hasInt256() const { return hasAVX2(); }
  bool hasSSE4A() const { return HasSSE4A; }
  bool hasMMX() const { return X863DNowLevel >= MMX; }
  bool has3DNow() const { return X863DNowLevel >= ThreeDNow; }
  bool has3DNowA() const { return X863DNowLevel >= ThreeDNowA; }
  bool hasPOPCNT() const { return HasPOPCNT; }
  bool hasAES() const { return HasAES; }
  bool hasVAES() const { return HasVAES; }
  bool hasFXSR() const { return HasFXSR; }
  bool hasXSAVE() const { return HasXSAVE; }
  bool hasXSAVEOPT() const { return HasXSAVEOPT; }
  bool hasXSAVEC() const { return HasXSAVEC; }
  bool hasXSAVES() const { return HasXSAVES; }
  bool hasPCLMUL() const { return HasPCLMUL; }
  bool hasVPCLMULQDQ() const { return HasVPCLMULQDQ; }
  bool hasGFNI() const { return HasGFNI; }
  // Prefer FMA4 to FMA - its better for commutation/memory folding and
  // has equal or better performance on all supported targets.
  bool hasFMA() const { return HasFMA; }
  bool hasFMA4() const { return HasFMA4; }
  bool hasAnyFMA() const { return hasFMA() || hasFMA4(); }
  bool hasXOP() const { return HasXOP; }
  bool hasTBM() const { return HasTBM; }
  bool hasLWP() const { return HasLWP; }
  bool hasMOVBE() const { return HasMOVBE; }
  bool hasRDRAND() const { return HasRDRAND; }
  bool hasF16C() const { return HasF16C; }
  bool hasFSGSBase() const { return HasFSGSBase; }
  bool hasLZCNT() const { return HasLZCNT; }
  bool hasBMI() const { return HasBMI; }
  bool hasBMI2() const { return HasBMI2; }
  bool hasVBMI() const { return HasVBMI; }
  bool hasVBMI2() const { return HasVBMI2; }
  bool hasIFMA() const { return HasIFMA; }
  bool hasRTM() const { return HasRTM; }
  bool hasADX() const { return HasADX; }
  bool hasSHA() const { return HasSHA; }
  bool hasPRFCHW() const { return HasPRFCHW || HasPREFETCHWT1; }
  bool hasPREFETCHWT1() const { return HasPREFETCHWT1; }
  bool hasSSEPrefetch() const {
    // We implicitly enable these when we have a write prefix supporting cache
    // level OR if we have prfchw, but don't already have a read prefetch from
    // 3dnow.
    return hasSSE1() || (hasPRFCHW() && !has3DNow()) || hasPREFETCHWT1();
  }
  bool hasRDSEED() const { return HasRDSEED; }
  bool hasLAHFSAHF() const { return HasLAHFSAHF; }
  bool hasMWAITX() const { return HasMWAITX; }
  bool hasCLZERO() const { return HasCLZERO; }
  bool hasCLDEMOTE() const { return HasCLDEMOTE; }
  bool hasMOVDIRI() const { return HasMOVDIRI; }
  bool hasMOVDIR64B() const { return HasMOVDIR64B; }
  bool hasPTWRITE() const { return HasPTWRITE; }
  bool isSHLDSlow() const { return IsSHLDSlow; }
  bool isPMULLDSlow() const { return IsPMULLDSlow; }
  bool isPMADDWDSlow() const { return IsPMADDWDSlow; }
  bool isUnalignedMem16Slow() const { return IsUAMem16Slow; }
  bool isUnalignedMem32Slow() const { return IsUAMem32Slow; }
  int getGatherOverhead() const { return GatherOverhead; }
  int getScatterOverhead() const { return ScatterOverhead; }
  bool hasSSEUnalignedMem() const { return HasSSEUnalignedMem; }
  bool hasCmpxchg16b() const { return HasCmpxchg16b; }
  bool useLeaForSP() const { return UseLeaForSP; }
  bool hasPOPCNTFalseDeps() const { return HasPOPCNTFalseDeps; }
  bool hasLZCNTFalseDeps() const { return HasLZCNTFalseDeps; }
  bool hasFastVariableShuffle() const {
    return HasFastVariableShuffle;
  }
  bool hasFastPartialYMMorZMMWrite() const {
    return HasFastPartialYMMorZMMWrite;
  }
  bool hasFastGather() const { return HasFastGather; }
  bool hasFastScalarFSQRT() const { return HasFastScalarFSQRT; }
  bool hasFastVectorFSQRT() const { return HasFastVectorFSQRT; }
  bool hasFastLZCNT() const { return HasFastLZCNT; }
  bool hasFastSHLDRotate() const { return HasFastSHLDRotate; }
  bool hasFastBEXTR() const { return HasFastBEXTR; }
  bool hasFastHorizontalOps() const { return HasFastHorizontalOps; }
  bool hasMacroFusion() const { return HasMacroFusion; }
  bool hasERMSB() const { return HasERMSB; }
  bool hasSlowDivide32() const { return HasSlowDivide32; }
  bool hasSlowDivide64() const { return HasSlowDivide64; }
  bool padShortFunctions() const { return PadShortFunctions; }
  bool slowTwoMemOps() const { return SlowTwoMemOps; }
  bool LEAusesAG() const { return LEAUsesAG; }
  bool slowLEA() const { return SlowLEA; }
  bool slow3OpsLEA() const { return Slow3OpsLEA; }
  bool slowIncDec() const { return SlowIncDec; }
  bool hasCDI() const { return HasCDI; }
  bool hasVPOPCNTDQ() const { return HasVPOPCNTDQ; }
  bool hasPFI() const { return HasPFI; }
  bool hasERI() const { return HasERI; }
  bool hasDQI() const { return HasDQI; }
  bool hasBWI() const { return HasBWI; }
  bool hasVLX() const { return HasVLX; }
  bool hasPKU() const { return HasPKU; }
  bool hasVNNI() const { return HasVNNI; }
  bool hasBITALG() const { return HasBITALG; }
  bool hasMPX() const { return HasMPX; }
  bool hasSHSTK() const { return HasSHSTK; }
  bool hasCLFLUSHOPT() const { return HasCLFLUSHOPT; }
  bool hasCLWB() const { return HasCLWB; }
  bool hasWBNOINVD() const { return HasWBNOINVD; }
  bool hasRDPID() const { return HasRDPID; }
  bool hasWAITPKG() const { return HasWAITPKG; }
  bool hasPCONFIG() const { return HasPCONFIG; }
  bool hasSGX() const { return HasSGX; }
  bool threewayBranchProfitable() const { return ThreewayBranchProfitable; }
  bool hasINVPCID() const { return HasINVPCID; }
  bool useRetpolineIndirectCalls() const { return UseRetpolineIndirectCalls; }
  bool useRetpolineIndirectBranches() const {
    return UseRetpolineIndirectBranches;
  }
  bool useRetpolineExternalThunk() const { return UseRetpolineExternalThunk; }

  unsigned getPreferVectorWidth() const { return PreferVectorWidth; }
  unsigned getRequiredVectorWidth() const { return RequiredVectorWidth; }

  // Helper functions to determine when we should allow widening to 512-bit
  // during codegen.
  // TODO: Currently we're always allowing widening on CPUs without VLX,
  // because for many cases we don't have a better option.
  bool canExtendTo512DQ() const {
    return hasAVX512() && (!hasVLX() || getPreferVectorWidth() >= 512);
  }
  bool canExtendTo512BW() const  {
    return hasBWI() && canExtendTo512DQ();
  }

  // If there are no 512-bit vectors and we prefer not to use 512-bit registers,
  // disable them in the legalizer.
  bool useAVX512Regs() const {
    return hasAVX512() && (canExtendTo512DQ() || RequiredVectorWidth > 256);
  }

  bool useBWIRegs() const {
    return hasBWI() && useAVX512Regs();
  }

  bool isXRaySupported() const override { return is64Bit(); }

  X86ProcFamilyEnum getProcFamily() const { return X86ProcFamily; }

  /// TODO: to be removed later and replaced with suitable properties
  bool isAtom() const { return X86ProcFamily == IntelAtom; }
  bool isSLM() const { return X86ProcFamily == IntelSLM; }
  bool isGLM() const {
    return X86ProcFamily == IntelGLM ||
           X86ProcFamily == IntelGLP ||
           X86ProcFamily == IntelTRM;
  }
  bool useSoftFloat() const { return UseSoftFloat; }

  /// Use mfence if we have SSE2 or we're on x86-64 (even if we asked for
  /// no-sse2). There isn't any reason to disable it if the target processor
  /// supports it.
  bool hasMFence() const { return hasSSE2() || is64Bit(); }

  const Triple &getTargetTriple() const { return TargetTriple; }

  bool isTargetDarwin() const { return TargetTriple.isOSDarwin(); }
  bool isTargetFreeBSD() const { return TargetTriple.isOSFreeBSD(); }
  bool isTargetDragonFly() const { return TargetTriple.isOSDragonFly(); }
  bool isTargetSolaris() const { return TargetTriple.isOSSolaris(); }
  bool isTargetPS4() const { return TargetTriple.isPS4CPU(); }

  bool isTargetELF() const { return TargetTriple.isOSBinFormatELF(); }
  bool isTargetCOFF() const { return TargetTriple.isOSBinFormatCOFF(); }
  bool isTargetMachO() const { return TargetTriple.isOSBinFormatMachO(); }

  bool isTargetLinux() const { return TargetTriple.isOSLinux(); }
  bool isTargetKFreeBSD() const { return TargetTriple.isOSKFreeBSD(); }
  bool isTargetGlibc() const { return TargetTriple.isOSGlibc(); }
  bool isTargetAndroid() const { return TargetTriple.isAndroid(); }
  bool isTargetNaCl() const { return TargetTriple.isOSNaCl(); }
  bool isTargetNaCl32() const { return isTargetNaCl() && !is64Bit(); }
  bool isTargetNaCl64() const { return isTargetNaCl() && is64Bit(); }
  bool isTargetMCU() const { return TargetTriple.isOSIAMCU(); }
  bool isTargetFuchsia() const { return TargetTriple.isOSFuchsia(); }

  bool isTargetWindowsMSVC() const {
    return TargetTriple.isWindowsMSVCEnvironment();
  }

  bool isTargetKnownWindowsMSVC() const {
    return TargetTriple.isKnownWindowsMSVCEnvironment();
  }

  bool isTargetWindowsCoreCLR() const {
    return TargetTriple.isWindowsCoreCLREnvironment();
  }

  bool isTargetWindowsCygwin() const {
    return TargetTriple.isWindowsCygwinEnvironment();
  }

  bool isTargetWindowsGNU() const {
    return TargetTriple.isWindowsGNUEnvironment();
  }

  bool isTargetWindowsItanium() const {
    return TargetTriple.isWindowsItaniumEnvironment();
  }

  bool isTargetCygMing() const { return TargetTriple.isOSCygMing(); }

  bool isOSWindows() const { return TargetTriple.isOSWindows(); }

  bool isTargetWin64() const { return In64BitMode && isOSWindows(); }

  bool isTargetWin32() const { return !In64BitMode && isOSWindows(); }

  bool isPICStyleGOT() const { return PICStyle == PICStyles::GOT; }
  bool isPICStyleRIPRel() const { return PICStyle == PICStyles::RIPRel; }

  bool isPICStyleStubPIC() const {
    return PICStyle == PICStyles::StubPIC;
  }

  bool isPositionIndependent() const { return TM.isPositionIndependent(); }

  bool isCallingConvWin64(CallingConv::ID CC) const {
    switch (CC) {
    // On Win64, all these conventions just use the default convention.
    case CallingConv::C:
    case CallingConv::Fast:
    case CallingConv::Swift:
    case CallingConv::X86_FastCall:
    case CallingConv::X86_StdCall:
    case CallingConv::X86_ThisCall:
    case CallingConv::X86_VectorCall:
    case CallingConv::Intel_OCL_BI:
      return isTargetWin64();
    // This convention allows using the Win64 convention on other targets.
    case CallingConv::Win64:
      return true;
    // This convention allows using the SysV convention on Windows targets.
    case CallingConv::X86_64_SysV:
      return false;
    // Otherwise, who knows what this is.
    default:
      return false;
    }
  }

  /// Classify a global variable reference for the current subtarget according
  /// to how we should reference it in a non-pcrel context.
  unsigned char classifyLocalReference(const GlobalValue *GV) const;

  unsigned char classifyGlobalReference(const GlobalValue *GV,
                                        const Module &M) const;
  unsigned char classifyGlobalReference(const GlobalValue *GV) const;

  /// Classify a global function reference for the current subtarget.
  unsigned char classifyGlobalFunctionReference(const GlobalValue *GV,
                                                const Module &M) const;
  unsigned char classifyGlobalFunctionReference(const GlobalValue *GV) const;

  /// Classify a blockaddress reference for the current subtarget according to
  /// how we should reference it in a non-pcrel context.
  unsigned char classifyBlockAddressReference() const;

  /// Return true if the subtarget allows calls to immediate address.
  bool isLegalToCallImmediateAddr() const;

  /// If we are using retpolines, we need to expand indirectbr to avoid it
  /// lowering to an actual indirect jump.
  bool enableIndirectBrExpand() const override {
    return useRetpolineIndirectBranches();
  }

  /// Enable the MachineScheduler pass for all X86 subtargets.
  bool enableMachineScheduler() const override { return true; }

  // TODO: Update the regression tests and return true.
  bool supportPrintSchedInfo() const override { return false; }

  bool enableEarlyIfConversion() const override;

  AntiDepBreakMode getAntiDepBreakMode() const override {
    return TargetSubtargetInfo::ANTIDEP_CRITICAL;
  }

  bool enableAdvancedRASplitCost() const override { return true; }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_X86_X86SUBTARGET_H
