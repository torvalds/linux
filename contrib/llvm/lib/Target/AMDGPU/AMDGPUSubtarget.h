//=====-- AMDGPUSubtarget.h - Define Subtarget for AMDGPU ------*- C++ -*-====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//==-----------------------------------------------------------------------===//
//
/// \file
/// AMDGPU specific subclass of TargetSubtarget.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUSUBTARGET_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUSUBTARGET_H

#include "AMDGPU.h"
#include "AMDGPUCallLowering.h"
#include "R600FrameLowering.h"
#include "R600ISelLowering.h"
#include "R600InstrInfo.h"
#include "SIFrameLowering.h"
#include "SIISelLowering.h"
#include "SIInstrInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/Triple.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/RegisterBankInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>

#define GET_SUBTARGETINFO_HEADER
#include "AMDGPUGenSubtargetInfo.inc"
#define GET_SUBTARGETINFO_HEADER
#include "R600GenSubtargetInfo.inc"

namespace llvm {

class StringRef;

class AMDGPUSubtarget {
public:
  enum Generation {
    R600 = 0,
    R700 = 1,
    EVERGREEN = 2,
    NORTHERN_ISLANDS = 3,
    SOUTHERN_ISLANDS = 4,
    SEA_ISLANDS = 5,
    VOLCANIC_ISLANDS = 6,
    GFX9 = 7
  };

private:
  Triple TargetTriple;

protected:
  bool Has16BitInsts;
  bool HasMadMixInsts;
  bool FP32Denormals;
  bool FPExceptions;
  bool HasSDWA;
  bool HasVOP3PInsts;
  bool HasMulI24;
  bool HasMulU24;
  bool HasInv2PiInlineImm;
  bool HasFminFmaxLegacy;
  bool EnablePromoteAlloca;
  bool HasTrigReducedRange;
  int LocalMemorySize;
  unsigned WavefrontSize;

public:
  AMDGPUSubtarget(const Triple &TT);

  static const AMDGPUSubtarget &get(const MachineFunction &MF);
  static const AMDGPUSubtarget &get(const TargetMachine &TM,
                                    const Function &F);

  /// \returns Default range flat work group size for a calling convention.
  std::pair<unsigned, unsigned> getDefaultFlatWorkGroupSize(CallingConv::ID CC) const;

  /// \returns Subtarget's default pair of minimum/maximum flat work group sizes
  /// for function \p F, or minimum/maximum flat work group sizes explicitly
  /// requested using "amdgpu-flat-work-group-size" attribute attached to
  /// function \p F.
  ///
  /// \returns Subtarget's default values if explicitly requested values cannot
  /// be converted to integer, or violate subtarget's specifications.
  std::pair<unsigned, unsigned> getFlatWorkGroupSizes(const Function &F) const;

  /// \returns Subtarget's default pair of minimum/maximum number of waves per
  /// execution unit for function \p F, or minimum/maximum number of waves per
  /// execution unit explicitly requested using "amdgpu-waves-per-eu" attribute
  /// attached to function \p F.
  ///
  /// \returns Subtarget's default values if explicitly requested values cannot
  /// be converted to integer, violate subtarget's specifications, or are not
  /// compatible with minimum/maximum number of waves limited by flat work group
  /// size, register usage, and/or lds usage.
  std::pair<unsigned, unsigned> getWavesPerEU(const Function &F) const;

  /// Return the amount of LDS that can be used that will not restrict the
  /// occupancy lower than WaveCount.
  unsigned getMaxLocalMemSizeWithWaveCount(unsigned WaveCount,
                                           const Function &) const;

  /// Inverse of getMaxLocalMemWithWaveCount. Return the maximum wavecount if
  /// the given LDS memory size is the only constraint.
  unsigned getOccupancyWithLocalMemSize(uint32_t Bytes, const Function &) const;

  unsigned getOccupancyWithLocalMemSize(const MachineFunction &MF) const;

  bool isAmdHsaOS() const {
    return TargetTriple.getOS() == Triple::AMDHSA;
  }

  bool isAmdPalOS() const {
    return TargetTriple.getOS() == Triple::AMDPAL;
  }

  bool isMesa3DOS() const {
    return TargetTriple.getOS() == Triple::Mesa3D;
  }

  bool isMesaKernel(const Function &F) const {
    return isMesa3DOS() && !AMDGPU::isShader(F.getCallingConv());
  }

  bool isAmdHsaOrMesa(const Function &F) const {
    return isAmdHsaOS() || isMesaKernel(F);
  }

  bool has16BitInsts() const {
    return Has16BitInsts;
  }

  bool hasMadMixInsts() const {
    return HasMadMixInsts;
  }

  bool hasFP32Denormals() const {
    return FP32Denormals;
  }

  bool hasFPExceptions() const {
    return FPExceptions;
  }

  bool hasSDWA() const {
    return HasSDWA;
  }

  bool hasVOP3PInsts() const {
    return HasVOP3PInsts;
  }

  bool hasMulI24() const {
    return HasMulI24;
  }

  bool hasMulU24() const {
    return HasMulU24;
  }

  bool hasInv2PiInlineImm() const {
    return HasInv2PiInlineImm;
  }

  bool hasFminFmaxLegacy() const {
    return HasFminFmaxLegacy;
  }

  bool hasTrigReducedRange() const {
    return HasTrigReducedRange;
  }

  bool isPromoteAllocaEnabled() const {
    return EnablePromoteAlloca;
  }

  unsigned getWavefrontSize() const {
    return WavefrontSize;
  }

  int getLocalMemorySize() const {
    return LocalMemorySize;
  }

  unsigned getAlignmentForImplicitArgPtr() const {
    return isAmdHsaOS() ? 8 : 4;
  }

  /// Returns the offset in bytes from the start of the input buffer
  ///        of the first explicit kernel argument.
  unsigned getExplicitKernelArgOffset(const Function &F) const {
    return isAmdHsaOrMesa(F) ? 0 : 36;
  }

  /// \returns Maximum number of work groups per compute unit supported by the
  /// subtarget and limited by given \p FlatWorkGroupSize.
  virtual unsigned getMaxWorkGroupsPerCU(unsigned FlatWorkGroupSize) const = 0;

  /// \returns Minimum flat work group size supported by the subtarget.
  virtual unsigned getMinFlatWorkGroupSize() const = 0;

  /// \returns Maximum flat work group size supported by the subtarget.
  virtual unsigned getMaxFlatWorkGroupSize() const = 0;

  /// \returns Maximum number of waves per execution unit supported by the
  /// subtarget and limited by given \p FlatWorkGroupSize.
  virtual unsigned getMaxWavesPerEU(unsigned FlatWorkGroupSize) const  = 0;

  /// \returns Minimum number of waves per execution unit supported by the
  /// subtarget.
  virtual unsigned getMinWavesPerEU() const = 0;

  unsigned getMaxWavesPerEU() const { return 10; }

  /// Creates value range metadata on an workitemid.* inrinsic call or load.
  bool makeLIDRangeMetadata(Instruction *I) const;

  /// \returns Number of bytes of arguments that are passed to a shader or
  /// kernel in addition to the explicit ones declared for the function.
  unsigned getImplicitArgNumBytes(const Function &F) const {
    if (isMesaKernel(F))
      return 16;
    return AMDGPU::getIntegerAttribute(F, "amdgpu-implicitarg-num-bytes", 0);
  }
  uint64_t getExplicitKernArgSize(const Function &F,
                                  unsigned &MaxAlign) const;
  unsigned getKernArgSegmentSize(const Function &F,
                                 unsigned &MaxAlign) const;

  virtual ~AMDGPUSubtarget() {}
};

class GCNSubtarget : public AMDGPUGenSubtargetInfo,
                     public AMDGPUSubtarget {
public:
  enum {
    ISAVersion0_0_0,
    ISAVersion6_0_0,
    ISAVersion6_0_1,
    ISAVersion7_0_0,
    ISAVersion7_0_1,
    ISAVersion7_0_2,
    ISAVersion7_0_3,
    ISAVersion7_0_4,
    ISAVersion8_0_1,
    ISAVersion8_0_2,
    ISAVersion8_0_3,
    ISAVersion8_1_0,
    ISAVersion9_0_0,
    ISAVersion9_0_2,
    ISAVersion9_0_4,
    ISAVersion9_0_6,
    ISAVersion9_0_9,
  };

  enum TrapHandlerAbi {
    TrapHandlerAbiNone = 0,
    TrapHandlerAbiHsa = 1
  };

  enum TrapID {
    TrapIDHardwareReserved = 0,
    TrapIDHSADebugTrap = 1,
    TrapIDLLVMTrap = 2,
    TrapIDLLVMDebugTrap = 3,
    TrapIDDebugBreakpoint = 7,
    TrapIDDebugReserved8 = 8,
    TrapIDDebugReservedFE = 0xfe,
    TrapIDDebugReservedFF = 0xff
  };

  enum TrapRegValues {
    LLVMTrapHandlerRegValue = 1
  };

private:
  /// GlobalISel related APIs.
  std::unique_ptr<AMDGPUCallLowering> CallLoweringInfo;
  std::unique_ptr<InstructionSelector> InstSelector;
  std::unique_ptr<LegalizerInfo> Legalizer;
  std::unique_ptr<RegisterBankInfo> RegBankInfo;

protected:
  // Basic subtarget description.
  Triple TargetTriple;
  unsigned Gen;
  unsigned IsaVersion;
  InstrItineraryData InstrItins;
  int LDSBankCount;
  unsigned MaxPrivateElementSize;

  // Possibly statically set by tablegen, but may want to be overridden.
  bool FastFMAF32;
  bool HalfRate64Ops;

  // Dynamially set bits that enable features.
  bool FP64FP16Denormals;
  bool DX10Clamp;
  bool FlatForGlobal;
  bool AutoWaitcntBeforeBarrier;
  bool CodeObjectV3;
  bool UnalignedScratchAccess;
  bool UnalignedBufferAccess;
  bool HasApertureRegs;
  bool EnableXNACK;
  bool TrapHandler;
  bool DebuggerInsertNops;
  bool DebuggerEmitPrologue;

  // Used as options.
  bool EnableHugePrivateBuffer;
  bool EnableLoadStoreOpt;
  bool EnableUnsafeDSOffsetFolding;
  bool EnableSIScheduler;
  bool EnableDS128;
  bool EnablePRTStrictNull;
  bool DumpCode;

  // Subtarget statically properties set by tablegen
  bool FP64;
  bool FMA;
  bool MIMG_R128;
  bool IsGCN;
  bool GCN3Encoding;
  bool CIInsts;
  bool VIInsts;
  bool GFX9Insts;
  bool SGPRInitBug;
  bool HasSMemRealTime;
  bool HasIntClamp;
  bool HasFmaMixInsts;
  bool HasMovrel;
  bool HasVGPRIndexMode;
  bool HasScalarStores;
  bool HasScalarAtomics;
  bool HasSDWAOmod;
  bool HasSDWAScalar;
  bool HasSDWASdst;
  bool HasSDWAMac;
  bool HasSDWAOutModsVOPC;
  bool HasDPP;
  bool HasR128A16;
  bool HasDLInsts;
  bool HasDotInsts;
  bool EnableSRAMECC;
  bool FlatAddressSpace;
  bool FlatInstOffsets;
  bool FlatGlobalInsts;
  bool FlatScratchInsts;
  bool AddNoCarryInsts;
  bool HasUnpackedD16VMem;
  bool R600ALUInst;
  bool CaymanISA;
  bool CFALUBug;
  bool HasVertexCache;
  short TexVTXClauseSize;
  bool ScalarizeGlobal;

  // Dummy feature to use for assembler in tablegen.
  bool FeatureDisable;

  SelectionDAGTargetInfo TSInfo;
private:
  SIInstrInfo InstrInfo;
  SITargetLowering TLInfo;
  SIFrameLowering FrameLowering;

public:
  GCNSubtarget(const Triple &TT, StringRef GPU, StringRef FS,
               const GCNTargetMachine &TM);
  ~GCNSubtarget() override;

  GCNSubtarget &initializeSubtargetDependencies(const Triple &TT,
                                                   StringRef GPU, StringRef FS);

  const SIInstrInfo *getInstrInfo() const override {
    return &InstrInfo;
  }

  const SIFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }

  const SITargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }

  const SIRegisterInfo *getRegisterInfo() const override {
    return &InstrInfo.getRegisterInfo();
  }

  const CallLowering *getCallLowering() const override {
    return CallLoweringInfo.get();
  }

  const InstructionSelector *getInstructionSelector() const override {
    return InstSelector.get();
  }

  const LegalizerInfo *getLegalizerInfo() const override {
    return Legalizer.get();
  }

  const RegisterBankInfo *getRegBankInfo() const override {
    return RegBankInfo.get();
  }

  // Nothing implemented, just prevent crashes on use.
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }

  const InstrItineraryData *getInstrItineraryData() const override {
    return &InstrItins;
  }

  void ParseSubtargetFeatures(StringRef CPU, StringRef FS);

  Generation getGeneration() const {
    return (Generation)Gen;
  }

  unsigned getWavefrontSizeLog2() const {
    return Log2_32(WavefrontSize);
  }

  int getLDSBankCount() const {
    return LDSBankCount;
  }

  unsigned getMaxPrivateElementSize() const {
    return MaxPrivateElementSize;
  }

  bool hasIntClamp() const {
    return HasIntClamp;
  }

  bool hasFP64() const {
    return FP64;
  }

  bool hasMIMG_R128() const {
    return MIMG_R128;
  }

  bool hasHWFP64() const {
    return FP64;
  }

  bool hasFastFMAF32() const {
    return FastFMAF32;
  }

  bool hasHalfRate64Ops() const {
    return HalfRate64Ops;
  }

  bool hasAddr64() const {
    return (getGeneration() < AMDGPUSubtarget::VOLCANIC_ISLANDS);
  }

  bool hasBFE() const {
    return true;
  }

  bool hasBFI() const {
    return true;
  }

  bool hasBFM() const {
    return hasBFE();
  }

  bool hasBCNT(unsigned Size) const {
    return true;
  }

  bool hasFFBL() const {
    return true;
  }

  bool hasFFBH() const {
    return true;
  }

  bool hasMed3_16() const {
    return getGeneration() >= AMDGPUSubtarget::GFX9;
  }

  bool hasMin3Max3_16() const {
    return getGeneration() >= AMDGPUSubtarget::GFX9;
  }

  bool hasFmaMixInsts() const {
    return HasFmaMixInsts;
  }

  bool hasCARRY() const {
    return true;
  }

  bool hasFMA() const {
    return FMA;
  }

  bool hasSwap() const {
    return GFX9Insts;
  }

  TrapHandlerAbi getTrapHandlerAbi() const {
    return isAmdHsaOS() ? TrapHandlerAbiHsa : TrapHandlerAbiNone;
  }

  bool enableHugePrivateBuffer() const {
    return EnableHugePrivateBuffer;
  }

  bool unsafeDSOffsetFoldingEnabled() const {
    return EnableUnsafeDSOffsetFolding;
  }

  bool dumpCode() const {
    return DumpCode;
  }

  /// Return the amount of LDS that can be used that will not restrict the
  /// occupancy lower than WaveCount.
  unsigned getMaxLocalMemSizeWithWaveCount(unsigned WaveCount,
                                           const Function &) const;

  bool hasFP16Denormals() const {
    return FP64FP16Denormals;
  }

  bool hasFP64Denormals() const {
    return FP64FP16Denormals;
  }

  bool supportsMinMaxDenormModes() const {
    return getGeneration() >= AMDGPUSubtarget::GFX9;
  }

  bool enableDX10Clamp() const {
    return DX10Clamp;
  }

  bool enableIEEEBit(const MachineFunction &MF) const {
    return AMDGPU::isCompute(MF.getFunction().getCallingConv());
  }

  bool useFlatForGlobal() const {
    return FlatForGlobal;
  }

  /// \returns If target supports ds_read/write_b128 and user enables generation
  /// of ds_read/write_b128.
  bool useDS128() const {
    return CIInsts && EnableDS128;
  }

  /// \returns If MUBUF instructions always perform range checking, even for
  /// buffer resources used for private memory access.
  bool privateMemoryResourceIsRangeChecked() const {
    return getGeneration() < AMDGPUSubtarget::GFX9;
  }

  /// \returns If target requires PRT Struct NULL support (zero result registers
  /// for sparse texture support).
  bool usePRTStrictNull() const {
    return EnablePRTStrictNull;
  }

  bool hasAutoWaitcntBeforeBarrier() const {
    return AutoWaitcntBeforeBarrier;
  }

  bool hasCodeObjectV3() const {
    // FIXME: Need to add code object v3 support for mesa and pal.
    return isAmdHsaOS() ? CodeObjectV3 : false;
  }

  bool hasUnalignedBufferAccess() const {
    return UnalignedBufferAccess;
  }

  bool hasUnalignedScratchAccess() const {
    return UnalignedScratchAccess;
  }

  bool hasApertureRegs() const {
    return HasApertureRegs;
  }

  bool isTrapHandlerEnabled() const {
    return TrapHandler;
  }

  bool isXNACKEnabled() const {
    return EnableXNACK;
  }

  bool hasFlatAddressSpace() const {
    return FlatAddressSpace;
  }

  bool hasFlatInstOffsets() const {
    return FlatInstOffsets;
  }

  bool hasFlatGlobalInsts() const {
    return FlatGlobalInsts;
  }

  bool hasFlatScratchInsts() const {
    return FlatScratchInsts;
  }

  bool hasFlatLgkmVMemCountInOrder() const {
    return getGeneration() > GFX9;
  }

  bool hasD16LoadStore() const {
    return getGeneration() >= GFX9;
  }

  /// Return if most LDS instructions have an m0 use that require m0 to be
  /// iniitalized.
  bool ldsRequiresM0Init() const {
    return getGeneration() < GFX9;
  }

  bool hasAddNoCarry() const {
    return AddNoCarryInsts;
  }

  bool hasUnpackedD16VMem() const {
    return HasUnpackedD16VMem;
  }

  // Covers VS/PS/CS graphics shaders
  bool isMesaGfxShader(const Function &F) const {
    return isMesa3DOS() && AMDGPU::isShader(F.getCallingConv());
  }

  bool hasMad64_32() const {
    return getGeneration() >= SEA_ISLANDS;
  }

  bool hasSDWAOmod() const {
    return HasSDWAOmod;
  }

  bool hasSDWAScalar() const {
    return HasSDWAScalar;
  }

  bool hasSDWASdst() const {
    return HasSDWASdst;
  }

  bool hasSDWAMac() const {
    return HasSDWAMac;
  }

  bool hasSDWAOutModsVOPC() const {
    return HasSDWAOutModsVOPC;
  }

  bool vmemWriteNeedsExpWaitcnt() const {
    return getGeneration() < SEA_ISLANDS;
  }

  bool hasDLInsts() const {
    return HasDLInsts;
  }

  bool hasDotInsts() const {
    return HasDotInsts;
  }

  bool isSRAMECCEnabled() const {
    return EnableSRAMECC;
  }

  // Scratch is allocated in 256 dword per wave blocks for the entire
  // wavefront. When viewed from the perspecive of an arbitrary workitem, this
  // is 4-byte aligned.
  //
  // Only 4-byte alignment is really needed to access anything. Transformations
  // on the pointer value itself may rely on the alignment / known low bits of
  // the pointer. Set this to something above the minimum to avoid needing
  // dynamic realignment in common cases.
  unsigned getStackAlignment() const {
    return 16;
  }

  bool enableMachineScheduler() const override {
    return true;
  }

  bool enableSubRegLiveness() const override {
    return true;
  }

  void setScalarizeGlobalBehavior(bool b) { ScalarizeGlobal = b; }
  bool getScalarizeGlobalBehavior() const { return ScalarizeGlobal; }

  /// \returns Number of execution units per compute unit supported by the
  /// subtarget.
  unsigned getEUsPerCU() const {
    return AMDGPU::IsaInfo::getEUsPerCU(this);
  }

  /// \returns Maximum number of waves per compute unit supported by the
  /// subtarget without any kind of limitation.
  unsigned getMaxWavesPerCU() const {
    return AMDGPU::IsaInfo::getMaxWavesPerCU(this);
  }

  /// \returns Maximum number of waves per compute unit supported by the
  /// subtarget and limited by given \p FlatWorkGroupSize.
  unsigned getMaxWavesPerCU(unsigned FlatWorkGroupSize) const {
    return AMDGPU::IsaInfo::getMaxWavesPerCU(this, FlatWorkGroupSize);
  }

  /// \returns Maximum number of waves per execution unit supported by the
  /// subtarget without any kind of limitation.
  unsigned getMaxWavesPerEU() const {
    return AMDGPU::IsaInfo::getMaxWavesPerEU();
  }

  /// \returns Number of waves per work group supported by the subtarget and
  /// limited by given \p FlatWorkGroupSize.
  unsigned getWavesPerWorkGroup(unsigned FlatWorkGroupSize) const {
    return AMDGPU::IsaInfo::getWavesPerWorkGroup(this, FlatWorkGroupSize);
  }

  // static wrappers
  static bool hasHalfRate64Ops(const TargetSubtargetInfo &STI);

  // XXX - Why is this here if it isn't in the default pass set?
  bool enableEarlyIfConversion() const override {
    return true;
  }

  void overrideSchedPolicy(MachineSchedPolicy &Policy,
                           unsigned NumRegionInstrs) const override;

  unsigned getMaxNumUserSGPRs() const {
    return 16;
  }

  bool hasSMemRealTime() const {
    return HasSMemRealTime;
  }

  bool hasMovrel() const {
    return HasMovrel;
  }

  bool hasVGPRIndexMode() const {
    return HasVGPRIndexMode;
  }

  bool useVGPRIndexMode(bool UserEnable) const {
    return !hasMovrel() || (UserEnable && hasVGPRIndexMode());
  }

  bool hasScalarCompareEq64() const {
    return getGeneration() >= VOLCANIC_ISLANDS;
  }

  bool hasScalarStores() const {
    return HasScalarStores;
  }

  bool hasScalarAtomics() const {
    return HasScalarAtomics;
  }


  bool hasDPP() const {
    return HasDPP;
  }

  bool hasR128A16() const {
    return HasR128A16;
  }

  bool enableSIScheduler() const {
    return EnableSIScheduler;
  }

  bool debuggerSupported() const {
    return debuggerInsertNops() && debuggerEmitPrologue();
  }

  bool debuggerInsertNops() const {
    return DebuggerInsertNops;
  }

  bool debuggerEmitPrologue() const {
    return DebuggerEmitPrologue;
  }

  bool loadStoreOptEnabled() const {
    return EnableLoadStoreOpt;
  }

  bool hasSGPRInitBug() const {
    return SGPRInitBug;
  }

  bool has12DWordStoreHazard() const {
    return getGeneration() != AMDGPUSubtarget::SOUTHERN_ISLANDS;
  }

  // \returns true if the subtarget supports DWORDX3 load/store instructions.
  bool hasDwordx3LoadStores() const {
    return CIInsts;
  }

  bool hasSMovFedHazard() const {
    return getGeneration() >= AMDGPUSubtarget::GFX9;
  }

  bool hasReadM0MovRelInterpHazard() const {
    return getGeneration() >= AMDGPUSubtarget::GFX9;
  }

  bool hasReadM0SendMsgHazard() const {
    return getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS;
  }

  /// Return the maximum number of waves per SIMD for kernels using \p SGPRs
  /// SGPRs
  unsigned getOccupancyWithNumSGPRs(unsigned SGPRs) const;

  /// Return the maximum number of waves per SIMD for kernels using \p VGPRs
  /// VGPRs
  unsigned getOccupancyWithNumVGPRs(unsigned VGPRs) const;

  /// \returns true if the flat_scratch register should be initialized with the
  /// pointer to the wave's scratch memory rather than a size and offset.
  bool flatScratchIsPointer() const {
    return getGeneration() >= AMDGPUSubtarget::GFX9;
  }

  /// \returns true if the machine has merged shaders in which s0-s7 are
  /// reserved by the hardware and user SGPRs start at s8
  bool hasMergedShaders() const {
    return getGeneration() >= GFX9;
  }

  /// \returns SGPR allocation granularity supported by the subtarget.
  unsigned getSGPRAllocGranule() const {
    return AMDGPU::IsaInfo::getSGPRAllocGranule(this);
  }

  /// \returns SGPR encoding granularity supported by the subtarget.
  unsigned getSGPREncodingGranule() const {
    return AMDGPU::IsaInfo::getSGPREncodingGranule(this);
  }

  /// \returns Total number of SGPRs supported by the subtarget.
  unsigned getTotalNumSGPRs() const {
    return AMDGPU::IsaInfo::getTotalNumSGPRs(this);
  }

  /// \returns Addressable number of SGPRs supported by the subtarget.
  unsigned getAddressableNumSGPRs() const {
    return AMDGPU::IsaInfo::getAddressableNumSGPRs(this);
  }

  /// \returns Minimum number of SGPRs that meets the given number of waves per
  /// execution unit requirement supported by the subtarget.
  unsigned getMinNumSGPRs(unsigned WavesPerEU) const {
    return AMDGPU::IsaInfo::getMinNumSGPRs(this, WavesPerEU);
  }

  /// \returns Maximum number of SGPRs that meets the given number of waves per
  /// execution unit requirement supported by the subtarget.
  unsigned getMaxNumSGPRs(unsigned WavesPerEU, bool Addressable) const {
    return AMDGPU::IsaInfo::getMaxNumSGPRs(this, WavesPerEU, Addressable);
  }

  /// \returns Reserved number of SGPRs for given function \p MF.
  unsigned getReservedNumSGPRs(const MachineFunction &MF) const;

  /// \returns Maximum number of SGPRs that meets number of waves per execution
  /// unit requirement for function \p MF, or number of SGPRs explicitly
  /// requested using "amdgpu-num-sgpr" attribute attached to function \p MF.
  ///
  /// \returns Value that meets number of waves per execution unit requirement
  /// if explicitly requested value cannot be converted to integer, violates
  /// subtarget's specifications, or does not meet number of waves per execution
  /// unit requirement.
  unsigned getMaxNumSGPRs(const MachineFunction &MF) const;

  /// \returns VGPR allocation granularity supported by the subtarget.
  unsigned getVGPRAllocGranule() const {
    return AMDGPU::IsaInfo::getVGPRAllocGranule(this);
  }

  /// \returns VGPR encoding granularity supported by the subtarget.
  unsigned getVGPREncodingGranule() const {
    return AMDGPU::IsaInfo::getVGPREncodingGranule(this);
  }

  /// \returns Total number of VGPRs supported by the subtarget.
  unsigned getTotalNumVGPRs() const {
    return AMDGPU::IsaInfo::getTotalNumVGPRs(this);
  }

  /// \returns Addressable number of VGPRs supported by the subtarget.
  unsigned getAddressableNumVGPRs() const {
    return AMDGPU::IsaInfo::getAddressableNumVGPRs(this);
  }

  /// \returns Minimum number of VGPRs that meets given number of waves per
  /// execution unit requirement supported by the subtarget.
  unsigned getMinNumVGPRs(unsigned WavesPerEU) const {
    return AMDGPU::IsaInfo::getMinNumVGPRs(this, WavesPerEU);
  }

  /// \returns Maximum number of VGPRs that meets given number of waves per
  /// execution unit requirement supported by the subtarget.
  unsigned getMaxNumVGPRs(unsigned WavesPerEU) const {
    return AMDGPU::IsaInfo::getMaxNumVGPRs(this, WavesPerEU);
  }

  /// \returns Maximum number of VGPRs that meets number of waves per execution
  /// unit requirement for function \p MF, or number of VGPRs explicitly
  /// requested using "amdgpu-num-vgpr" attribute attached to function \p MF.
  ///
  /// \returns Value that meets number of waves per execution unit requirement
  /// if explicitly requested value cannot be converted to integer, violates
  /// subtarget's specifications, or does not meet number of waves per execution
  /// unit requirement.
  unsigned getMaxNumVGPRs(const MachineFunction &MF) const;

  void getPostRAMutations(
      std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations)
      const override;

  /// \returns Maximum number of work groups per compute unit supported by the
  /// subtarget and limited by given \p FlatWorkGroupSize.
  unsigned getMaxWorkGroupsPerCU(unsigned FlatWorkGroupSize) const override {
    return AMDGPU::IsaInfo::getMaxWorkGroupsPerCU(this, FlatWorkGroupSize);
  }

  /// \returns Minimum flat work group size supported by the subtarget.
  unsigned getMinFlatWorkGroupSize() const override {
    return AMDGPU::IsaInfo::getMinFlatWorkGroupSize(this);
  }

  /// \returns Maximum flat work group size supported by the subtarget.
  unsigned getMaxFlatWorkGroupSize() const override {
    return AMDGPU::IsaInfo::getMaxFlatWorkGroupSize(this);
  }

  /// \returns Maximum number of waves per execution unit supported by the
  /// subtarget and limited by given \p FlatWorkGroupSize.
  unsigned getMaxWavesPerEU(unsigned FlatWorkGroupSize) const override {
    return AMDGPU::IsaInfo::getMaxWavesPerEU(this, FlatWorkGroupSize);
  }

  /// \returns Minimum number of waves per execution unit supported by the
  /// subtarget.
  unsigned getMinWavesPerEU() const override {
    return AMDGPU::IsaInfo::getMinWavesPerEU(this);
  }
};

class R600Subtarget final : public R600GenSubtargetInfo,
                            public AMDGPUSubtarget {
private:
  R600InstrInfo InstrInfo;
  R600FrameLowering FrameLowering;
  bool FMA;
  bool CaymanISA;
  bool CFALUBug;
  bool DX10Clamp;
  bool HasVertexCache;
  bool R600ALUInst;
  bool FP64;
  short TexVTXClauseSize;
  Generation Gen;
  R600TargetLowering TLInfo;
  InstrItineraryData InstrItins;
  SelectionDAGTargetInfo TSInfo;

public:
  R600Subtarget(const Triple &TT, StringRef CPU, StringRef FS,
                const TargetMachine &TM);

  const R600InstrInfo *getInstrInfo() const override { return &InstrInfo; }

  const R600FrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }

  const R600TargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }

  const R600RegisterInfo *getRegisterInfo() const override {
    return &InstrInfo.getRegisterInfo();
  }

  const InstrItineraryData *getInstrItineraryData() const override {
    return &InstrItins;
  }

  // Nothing implemented, just prevent crashes on use.
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }

  void ParseSubtargetFeatures(StringRef CPU, StringRef FS);

  Generation getGeneration() const {
    return Gen;
  }

  unsigned getStackAlignment() const {
    return 4;
  }

  R600Subtarget &initializeSubtargetDependencies(const Triple &TT,
                                                 StringRef GPU, StringRef FS);

  bool hasBFE() const {
    return (getGeneration() >= EVERGREEN);
  }

  bool hasBFI() const {
    return (getGeneration() >= EVERGREEN);
  }

  bool hasBCNT(unsigned Size) const {
    if (Size == 32)
      return (getGeneration() >= EVERGREEN);

    return false;
  }

  bool hasBORROW() const {
    return (getGeneration() >= EVERGREEN);
  }

  bool hasCARRY() const {
    return (getGeneration() >= EVERGREEN);
  }

  bool hasCaymanISA() const {
    return CaymanISA;
  }

  bool hasFFBL() const {
    return (getGeneration() >= EVERGREEN);
  }

  bool hasFFBH() const {
    return (getGeneration() >= EVERGREEN);
  }

  bool hasFMA() const { return FMA; }

  bool hasCFAluBug() const { return CFALUBug; }

  bool hasVertexCache() const { return HasVertexCache; }

  short getTexVTXClauseSize() const { return TexVTXClauseSize; }

  bool enableMachineScheduler() const override {
    return true;
  }

  bool enableSubRegLiveness() const override {
    return true;
  }

  /// \returns Maximum number of work groups per compute unit supported by the
  /// subtarget and limited by given \p FlatWorkGroupSize.
  unsigned getMaxWorkGroupsPerCU(unsigned FlatWorkGroupSize) const override {
    return AMDGPU::IsaInfo::getMaxWorkGroupsPerCU(this, FlatWorkGroupSize);
  }

  /// \returns Minimum flat work group size supported by the subtarget.
  unsigned getMinFlatWorkGroupSize() const override {
    return AMDGPU::IsaInfo::getMinFlatWorkGroupSize(this);
  }

  /// \returns Maximum flat work group size supported by the subtarget.
  unsigned getMaxFlatWorkGroupSize() const override {
    return AMDGPU::IsaInfo::getMaxFlatWorkGroupSize(this);
  }

  /// \returns Maximum number of waves per execution unit supported by the
  /// subtarget and limited by given \p FlatWorkGroupSize.
  unsigned getMaxWavesPerEU(unsigned FlatWorkGroupSize) const override {
    return AMDGPU::IsaInfo::getMaxWavesPerEU(this, FlatWorkGroupSize);
  }

  /// \returns Minimum number of waves per execution unit supported by the
  /// subtarget.
  unsigned getMinWavesPerEU() const override {
    return AMDGPU::IsaInfo::getMinWavesPerEU(this);
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUSUBTARGET_H
